#include "Integrations/TrueHUDBackend.h"
#include "PCH.h"
#include "third_party/TrueHUDAPI.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorValues.h>
#include <RE/T/TESForm.h>

#include <thread>

namespace scf::integrations
{
	namespace
	{
		// V1 covers everything this backend uses (AddActorInfoBar / RemoveActorInfoBar /
		// FlashActorValue) and is the one version every TrueHUD build answers, so there is
		// no version-probing loop like the Precision backend needs.
		TRUEHUD_API::IVTrueHUD1* s_trueHUD{ nullptr };
		bool s_initialized{ false };

		// The thread TrueHUD handed the interface out on. Every later call is compared
		// against it instead of asking the engine, so the check has no game dependency.
		std::thread::id s_apiThread{};

		// What is currently drawn: the form id Sync() last accepted and the handle TrueHUD
		// was given for it. InvalidEntityID / 0 when no bar of ours exists.
		core::EntityID s_shownFormId{ core::InvalidEntityID };
		RE::ActorHandle s_shownHandle{};

		// Last target that could not be resolved, only used to keep the diagnostic out of
		// the per-tick path (a focus target stays unresolvable for many ticks).
		core::EntityID s_unresolvedFormId{ core::InvalidEntityID };

		bool OnApiThread(const char* a_caller) noexcept
		{
			if (s_apiThread == std::this_thread::get_id()) {
				return true;
			}

			// Once per process: a wrong thread is a code error, not runtime noise.
			static bool s_reported{ false };
			if (!s_reported) {
				s_reported = true;
				logger::warn("[TrueHUDBackend] {} called off the main thread; TrueHUD call skipped.", a_caller);
			}
			return false;
		}

		void ReportUnresolved(core::EntityID a_formId, const char* a_reason) noexcept
		{
			if (s_unresolvedFormId == a_formId) {
				return;
			}
			s_unresolvedFormId = a_formId;
			logger::debug("[TrueHUDBackend] Focus target 0x{:08X} not shown ({}).", a_formId, a_reason);
		}
	}

	bool TrueHUDBackend::Initialize() noexcept
	{
		if (s_initialized) {
			return s_trueHUD != nullptr;
		}
		s_initialized = true;

		auto* api = static_cast<TRUEHUD_API::IVTrueHUD1*>(
			TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V1));
		if (!api) {
			// Optional dependency: the feature is off and this is the only line saying why.
			logger::info("[TrueHUDBackend] TrueHUD not found. Focus target info bar inactive.");
			return false;
		}

		s_trueHUD = api;
		s_apiThread = std::this_thread::get_id();
		logger::info("[TrueHUDBackend] TrueHUD API acquired (V1). Focus target info bar enabled.");
		return true;
	}

	bool TrueHUDBackend::IsAvailable() noexcept
	{
		return s_trueHUD != nullptr;
	}

	void TrueHUDBackend::SetFocusTarget(RE::Actor* a_target) noexcept
	{
		if (!s_trueHUD || !a_target) {
			return;
		}
		if (!OnApiThread("SetFocusTarget")) {
			return;
		}

		const auto formId = a_target->GetFormID();

		// An actor outside every process list has no handle, and TrueHUD keys its widgets by
		// handle - nothing to draw for it yet.
		RE::ActorHandle handle = a_target->GetHandle();
		if (!handle) {
			ReportUnresolved(formId, "no actor handle");
			return;
		}

		if (s_shownFormId == formId && s_shownHandle == handle) {
			return;  // already drawn for exactly this actor
		}

		if (s_shownFormId != core::InvalidEntityID) {
			// Callers are expected to go through Sync(), which removes the old bar first.
			// Never leave two indicators behind.
			ClearFocusTarget();
		}

		s_trueHUD->AddActorInfoBar(handle);
		// Health is the bar TrueHUD draws for an actor; the long flash makes the new focus
		// distinguishable from an incidental target change.
		s_trueHUD->FlashActorValue(handle, RE::ActorValue::kHealth, true);

		s_shownHandle = handle;
		s_shownFormId = formId;
		s_unresolvedFormId = core::InvalidEntityID;

		const char* name = a_target->GetName();
		logger::info("[TrueHUDBackend] Info bar added for focus target 0x{:08X} ({}).", formId, name ? name : "");
	}

	void TrueHUDBackend::ClearFocusTarget() noexcept
	{
		if (!s_trueHUD || s_shownFormId == core::InvalidEntityID) {
			return;
		}
		if (!OnApiThread("ClearFocusTarget")) {
			return;
		}

		// Normal rather than Immediate: the bar fades out the way TrueHUD hides its own bar
		// when a target is dropped. A stale handle (the actor died meanwhile) simply finds no
		// widget to remove.
		s_trueHUD->RemoveActorInfoBar(s_shownHandle, TRUEHUD_API::WidgetRemovalMode::Normal);

		logger::info("[TrueHUDBackend] Info bar removed for focus target 0x{:08X}.", s_shownFormId);
		s_shownHandle.reset();
		s_shownFormId = core::InvalidEntityID;
	}

	void TrueHUDBackend::Sync(core::EntityID a_focusTargetFormId) noexcept
	{
		if (!s_trueHUD) {
			return;
		}
		if (!OnApiThread("Sync")) {
			return;
		}

		if (a_focusTargetFormId == s_shownFormId) {
			// The common case: the requested target did not change. The bar can still be
			// gone (a save load rebuilds TrueHUD's widgets while this record survives), so
			// the shown handle is re-resolved - once per tick, not per resource access.
			if (a_focusTargetFormId == core::InvalidEntityID) {
				return;
			}
			const auto shownPtr = s_shownHandle.get();
			const RE::Actor* shown = shownPtr.get();
			if (shown && shown->GetFormID() == s_shownFormId) {
				return;
			}
			// Stale record: forget it and rebuild the bar below.
			s_shownHandle.reset();
			s_shownFormId = core::InvalidEntityID;
			s_unresolvedFormId = core::InvalidEntityID;
		}

		ClearFocusTarget();

		if (a_focusTargetFormId == core::InvalidEntityID) {
			return;
		}

		auto* target = RE::TESForm::LookupByID<RE::Actor>(a_focusTargetFormId);
		if (!target) {
			// Still pinned in the model but not resolvable right now (unloaded, or the actor
			// was just removed): retried next tick, so this stays a debug line.
			ReportUnresolved(a_focusTargetFormId, "not a loaded actor");
			return;
		}

		SetFocusTarget(target);
	}
}
