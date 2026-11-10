#include "Engine/EventAdapter.h"
#include "Engine/UpdateHook.h"
#include "Core/EventDispatcher.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

namespace scf::engine
{
	namespace
	{
		class ActiveEffectEventSink : public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>
		{
		public:
			static ActiveEffectEventSink* GetSingleton()
			{
				static ActiveEffectEventSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESActiveEffectApplyRemoveEvent* a_event,
				RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}

				core::ActiveEffectEvent evt;
				evt.casterId = a_event->caster ? a_event->caster->GetFormID() : core::InvalidEntityID;
				evt.targetId = a_event->target ? a_event->target->GetFormID() : core::InvalidEntityID;
				evt.activeEffectUniqueId = a_event->activeEffectUniqueID;
				evt.isApplied = a_event->isApplied;

				auto& ctx = core::FrameworkContext::GetSingleton();
				static core::EventDispatcher dispatcher([](const char* msg) {
					logger::info("{}", msg);
				});

				dispatcher.HandleActiveEffect(evt, ctx.GetRegistry());

				// Immediately synchronize player's summoned actors on effect change
				update::SyncPlayerSummons();

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class DeathEventSink : public RE::BSTEventSink<RE::TESDeathEvent>
		{
		public:
			static DeathEventSink* GetSingleton()
			{
				static DeathEventSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESDeathEvent* a_event,
				RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}

				core::DeathEvent evt;
				evt.dyingId = a_event->actorDying ? a_event->actorDying->GetFormID() : core::InvalidEntityID;
				evt.killerId = a_event->actorKiller ? a_event->actorKiller->GetFormID() : core::InvalidEntityID;
				evt.isDead = a_event->dead;

				auto& ctx = core::FrameworkContext::GetSingleton();
				static core::EventDispatcher dispatcher([](const char* msg) {
					logger::info("{}", msg);
				});

				dispatcher.HandleDeath(evt, ctx.GetRegistry());

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class CellAttachDetachEventSink : public RE::BSTEventSink<RE::TESCellAttachDetachEvent>
		{
		public:
			static CellAttachDetachEventSink* GetSingleton()
			{
				static CellAttachDetachEventSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESCellAttachDetachEvent* a_event,
				RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}

				core::CellAttachDetachEvent evt;
				evt.entityId = a_event->reference ? a_event->reference->GetFormID() : core::InvalidEntityID;
				evt.isAttached = a_event->attached;

				auto& ctx = core::FrameworkContext::GetSingleton();
				static core::EventDispatcher dispatcher([](const char* msg) {
					logger::info("{}", msg);
				});

				dispatcher.HandleCellAttachDetach(evt, ctx.GetRegistry());

				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	bool EventAdapter::RegisterEventSinks()
	{
		auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!eventHolder) {
			logger::error("[EventAdapter] Failed to acquire ScriptEventSourceHolder singleton!");
			return false;
		}

		eventHolder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(ActiveEffectEventSink::GetSingleton());
		eventHolder->AddEventSink<RE::TESDeathEvent>(DeathEventSink::GetSingleton());
		eventHolder->AddEventSink<RE::TESCellAttachDetachEvent>(CellAttachDetachEventSink::GetSingleton());

		logger::info("[EventAdapter] Registered event sinks: TESActiveEffectApplyRemoveEvent, TESDeathEvent, TESCellAttachDetachEvent.");
		return true;
	}
}
