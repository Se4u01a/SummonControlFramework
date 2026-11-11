#include "Integrations/Papyrus.h"
#include "Core/FrameworkContext.h"
#include "Core/SummonRegistry.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace scf::papyrus
{
	namespace
	{
		// The class name and the five function names are the Papyrus side of the pair:
		// scripts/Source/SCF.psc declares `Scriptname SCF` and one `global native` per
		// function below. Changing a string here without changing the .psc silently
		// breaks the binding (the VM resolves natives by these two strings).
		constexpr const char* kClassName{ "SCF" };

		// Threading model.
		//
		// The registry is owned by the game thread: the tactical tick reads and writes it,
		// and it hands out raw pointers into its records (SummonRegistry::GetModel), so it
		// cannot simply take a lock without changing that contract for every caller.
		//
		// Papyrus natives are called by the VM, which the game's main loop ticks - that is
		// the game thread - so the normal path below is a direct call, and it returns the
		// real result. The natives are registered without `callableFromTasklets`, so the VM
		// never dispatches them from a tasklet thread either.
		//
		// The thread id is recorded by MarkGameThread() from the tactical tick, not by the
		// loader, so a plugin loaded from an unexpected thread cannot poison it: the first
		// tick states the truth. Until then (a script running before the first tick) the
		// caller is assumed to be the game thread.
		//
		// If a native ever runs on another thread the registry is not touched: a mutation is
		// queued through SKSE::GetTaskInterface()->AddTask, a query answers conservatively,
		// and both log once, so the violation is visible instead of being a race.
		std::atomic<std::thread::id> s_gameThreadId{};
		std::atomic<bool> s_offThreadReported{ false };

		[[nodiscard]] bool OnGameThread() noexcept
		{
			const std::thread::id gameThread = s_gameThreadId.load(std::memory_order_relaxed);
			return gameThread == std::thread::id{} || gameThread == std::this_thread::get_id();
		}

		void ReportOffThread(const char* a_what)
		{
			if (!s_offThreadReported.exchange(true, std::memory_order_relaxed)) {
				logger::warn("[Papyrus] SCF: {} was called off the game thread; SCF touches the summon registry "
							 "only on the game thread (deferred, or answered conservatively).",
					a_what);
			}
		}

		// A mutation runs inline on the game thread and is queued for it otherwise. `false`
		// means the request was refused: either the work ran and failed, or it could not be
		// scheduled at all. A queued request is reported as accepted - the caller sees the
		// outcome of the registration, not of that call.
		template <class F>
		bool CallOnGameThread(const char* a_what, F&& a_task)
		{
			if (OnGameThread()) {
				return a_task();
			}

			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				logger::error("[Papyrus] SCF: {} was called off the game thread and the SKSE task queue "
							  "is unavailable; the request is lost.",
					a_what);
				return false;
			}

			ReportOffThread(a_what);
			tasks->AddTask(std::forward<F>(a_task));
			return true;
		}

		[[nodiscard]] core::EntityID PlayerId()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? static_cast<core::EntityID>(player->GetFormID()) : core::InvalidEntityID;
		}

		// A script that does not name an owner means the player: SCF serves only the
		// player's roster (see IsSupportedOwner), so `None` is the only useful default.
		[[nodiscard]] core::EntityID OwnerIdOrDefault(RE::Actor* a_owner)
		{
			return a_owner ? static_cast<core::EntityID>(a_owner->GetFormID()) : PlayerId();
		}

		// Stage 9 owner policy: the only owner SCF can follow is the player, because the
		// commanded-actor roster and every AI seat are the player's.
		[[nodiscard]] bool IsSupportedOwner(core::EntityID a_ownerId)
		{
			const auto playerId = PlayerId();
			if (playerId != core::InvalidEntityID && a_ownerId == playerId) {
				return true;
			}
			logger::warn("[Papyrus] SCF: owner 0x{:08X} is not the player (0x{:08X}); only the player is supported, request refused.",
				a_ownerId, playerId);
			return false;
		}

		// Game thread only.
		bool ApplyRegister(core::EntityID a_ownerId, core::EntityID a_summonId)
		{
			if (!IsSupportedOwner(a_ownerId)) {
				return false;
			}

			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_summonId);
			if (!actor) {
				logger::warn("[Papyrus] SCF.RegisterSummon: actor 0x{:08X} no longer exists, request refused.", a_summonId);
				return false;
			}
			if (a_summonId == a_ownerId) {
				logger::warn("[Papyrus] SCF.RegisterSummon: 0x{:08X} is the owner itself, request refused.", a_summonId);
				return false;
			}
			if (actor->IsDead()) {
				logger::warn("[Papyrus] SCF.RegisterSummon: actor 0x{:08X} ({}) is dead, request refused.",
					a_summonId, actor->GetName());
				return false;
			}

			auto& ctx = core::FrameworkContext::GetSingleton();
			auto& registry = ctx.GetRegistry();
			const auto* previous = registry.FindRecord(a_summonId);
			const bool refreshed = previous && previous->explicitRegistration;

			// No effect duration exists for a scripted registration: it stays until the
			// script unregisters it (kPermanentDuration), which is also what a Dead Thrall
			// looks like - SCF never expires a summon on its own clock.
			if (!registry.RegisterSummon(
					a_ownerId,
					a_summonId,
					core::SummonType::Scripted,
					core::kPermanentDuration,
					ctx.GetScheduler().GetTotalElapsedTime(),
					nullptr,
					/*a_explicitRegistration*/ true)) {
				logger::warn("[Papyrus] SCF.RegisterSummon: registry refused actor 0x{:08X} for owner 0x{:08X}.",
					a_summonId, a_ownerId);
				return false;
			}

			logger::info("[Papyrus] SCF.RegisterSummon: actor 0x{:08X} ({}) {} for owner 0x{:08X} (roster-independent until UnregisterSummon, death or the form is gone).",
				a_summonId,
				actor->GetName(),
				refreshed ? "refreshed" : "registered",
				a_ownerId);
			return true;
		}

		// Game thread only.
		bool ApplyUnregister(core::EntityID a_summonId)
		{
			auto& registry = core::FrameworkContext::GetSingleton().GetRegistry();
			if (!registry.UnregisterSummon(a_summonId, "Papyrus SCF.UnregisterSummon")) {
				logger::debug("[Papyrus] SCF.UnregisterSummon: 0x{:08X} was not tracked by SCF.", a_summonId);
				return false;
			}
			logger::info("[Papyrus] SCF.UnregisterSummon: summon 0x{:08X} removed from the registry.", a_summonId);
			return true;
		}

		// Queries read the registry, so they need the game thread. The answers are derived
		// from the registry as it is right now, not from a snapshot: a stale answer to a
		// script that just registered or unregistered something is worse than none, and
		// the engine-side actors of GetSummons can only be resolved on the game thread
		// anyway.

		bool RegisterSummonNative(RE::StaticFunctionTag*, RE::Actor* a_summon, RE::Actor* a_owner)
		{
			if (!a_summon) {
				logger::warn("[Papyrus] SCF.RegisterSummon: null summon argument, request refused.");
				return false;
			}
			const auto summonId = static_cast<core::EntityID>(a_summon->GetFormID());
			const auto ownerId = OwnerIdOrDefault(a_owner);
			return CallOnGameThread("SCF.RegisterSummon", [ownerId, summonId]() {
				return ApplyRegister(ownerId, summonId);
			});
		}

		bool UnregisterSummonNative(RE::StaticFunctionTag*, RE::Actor* a_summon)
		{
			if (!a_summon) {
				return false;
			}
			const auto summonId = static_cast<core::EntityID>(a_summon->GetFormID());
			return CallOnGameThread("SCF.UnregisterSummon", [summonId]() {
				return ApplyUnregister(summonId);
			});
		}

		bool IsRegisteredNative(RE::StaticFunctionTag*, RE::Actor* a_summon)
		{
			if (!a_summon) {
				return false;
			}
			if (!OnGameThread()) {
				ReportOffThread("SCF.IsRegistered");
				return false;
			}
			return core::FrameworkContext::GetSingleton().GetRegistry().IsActive(
				static_cast<core::EntityID>(a_summon->GetFormID()));
		}

		// `Actor[]`: resolved from the tracked ids on the game thread, live actors only.
		std::vector<RE::Actor*> GetSummonsNative(RE::StaticFunctionTag*, RE::Actor* a_owner)
		{
			std::vector<RE::Actor*> summons;
			if (!OnGameThread()) {
				ReportOffThread("SCF.GetSummons");
				return summons;
			}

			auto ids = core::FrameworkContext::GetSingleton().GetRegistry().GetActiveSummonsForOwner(
				OwnerIdOrDefault(a_owner));
			std::sort(ids.begin(), ids.end());
			summons.reserve(ids.size());
			for (const auto summonId : ids) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(summonId);
				if (actor && !actor->IsDead()) {
					summons.push_back(actor);
				}
			}
			return summons;
		}

		// `int`: the width the VM carries. A size_t return would narrow silently.
		std::int32_t GetSummonCountNative(RE::StaticFunctionTag*, RE::Actor* a_owner)
		{
			if (!OnGameThread()) {
				ReportOffThread("SCF.GetSummonCount");
				return 0;
			}
			const auto summons = core::FrameworkContext::GetSingleton().GetRegistry().GetActiveSummonsForOwner(
				OwnerIdOrDefault(a_owner));
			return static_cast<std::int32_t>(summons.size());
		}
	}

	void MarkGameThread() noexcept
	{
		const std::thread::id current = std::this_thread::get_id();
		const std::thread::id previous = s_gameThreadId.exchange(current, std::memory_order_relaxed);
		if (previous != std::thread::id{} && previous != current) {
			logger::warn("[Papyrus] SCF: the game thread changed; requests deferred from the previous one are lost.");
		}
	}

	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			logger::error("[Papyrus] SCF: the Papyrus virtual machine is unavailable; the SCF global functions stay unregistered.");
			return false;
		}

		// The game thread is not recorded here: the loader thread says nothing about it.
		// Until the first tactical tick calls MarkGameThread() every caller is treated as
		// the game thread, and the VM only runs non-tasklet natives from its own thread.
		a_vm->RegisterFunction("RegisterSummon", kClassName, RegisterSummonNative);
		a_vm->RegisterFunction("UnregisterSummon", kClassName, UnregisterSummonNative);
		a_vm->RegisterFunction("IsRegistered", kClassName, IsRegisteredNative);
		a_vm->RegisterFunction("GetSummons", kClassName, GetSummonsNative);
		a_vm->RegisterFunction("GetSummonCount", kClassName, GetSummonCountNative);

		logger::info("[Papyrus] Registered SCF global functions: "
					 "RegisterSummon, UnregisterSummon, IsRegistered, GetSummons, GetSummonCount "
					 "(class 'SCF', not callable from tasklets).");
		return true;
	}
}
