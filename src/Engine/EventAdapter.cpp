#include "Engine/EventAdapter.h"
#include "Engine/UpdateHook.h"
#include "Core/Assist.h"
#include "Core/CommandQueue.h"
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

		// PoE2-style "attack what I attack" (stage M2): when the player hits an enemy, every
		// summon whose order allows it (core::ShouldAssistPlayerHit) gets a FocusTarget on
		// it - the same command path as Numpad 1, so the StartCombat seat and the target pin
		// do the rest. Script events are dispatched on the main thread, so the registry is
		// read here directly.
		class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			static HitEventSink* GetSingleton()
			{
				static HitEventSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent* a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (!a_event || !a_event->cause || !a_event->target) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto& ctx = core::FrameworkContext::GetSingleton();
				const auto& settings = ctx.GetSettings().Get();
				if (!settings.general.enabled || !settings.combat.assistPlayerAttacks) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player || a_event->cause.get() != player) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* target = a_event->target->As<RE::Actor>();
				if (!target || target == player || target->IsDead() || target->IsPlayerTeammate()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				// Never the player's own summons (or an ally's): friendly-fire filtered hits
				// still raise TESHitEvent.
				if (const auto commander = target->GetCommandingActor();
					commander && (commander.get() == player || commander->IsPlayerTeammate())) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto& registry = ctx.GetRegistry();
				const auto targetId = static_cast<core::EntityID>(target->GetFormID());
				std::size_t assisted = 0;
				for (const auto summonId : registry.GetActiveSummonsForOwner(player->GetFormID())) {
					const auto* model = registry.GetModel(summonId);
					if (!model || !core::ShouldAssistPlayerHit(*model, targetId)) {
						continue;
					}
					core::Command cmd{};
					cmd.type = core::CommandType::SetFocusTarget;
					cmd.priority = core::CommandPriority::Tactical;
					cmd.summonId = summonId;
					cmd.ownerId = player->GetFormID();
					cmd.focusTargetId = targetId;
					cmd.timestamp = ctx.GetScheduler().GetTotalElapsedTime();
					ctx.GetCommandQueue().Enqueue(cmd);
					++assisted;
				}

				if (assisted > 0) {
					ctx.GetScheduler().RequestImmediateTick();
					logger::info("[Assist] Player hit 0x{:08X} ({}): {} summon(s) switch focus to it",
						targetId, target->GetName(), assisted);
				}
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
		eventHolder->AddEventSink<RE::TESHitEvent>(HitEventSink::GetSingleton());

		logger::info("[EventAdapter] Registered event sinks: TESActiveEffectApplyRemoveEvent, TESDeathEvent, TESCellAttachDetachEvent, TESHitEvent.");
		return true;
	}
}
