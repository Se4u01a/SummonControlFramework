#include "Engine/EventAdapter.h"
#include "Engine/UpdateHook.h"
#include "Core/Assist.h"
#include "Core/CommandQueue.h"
#include "Core/EventDispatcher.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

#include <RE/A/ActiveEffect.h>
#include <RE/A/Actor.h>
#include <RE/E/EffectSetting.h>
#include <RE/M/MagicTarget.h>
#include <RE/P/PlayerCharacter.h>

namespace scf::engine
{
	namespace
	{
		// Which magic effect is the event about? The ApplyRemove event carries only the
		// effect's unique id, and the effect itself sits in the target's own magic target -
		// the list the event reports on. The dispatcher's contract has no archetype in it
		// (core::EventDispatcher files every applied effect as a plain summon), so this is
		// where a raised corpse can be told apart from a conjuration.
		const RE::EffectSetting* FindAppliedEffectSetting(RE::TESObjectREFR* a_target, std::uint16_t a_uniqueId)
		{
			auto* actor = a_target ? a_target->As<RE::Actor>() : nullptr;
			auto* magicTarget = actor ? actor->AsMagicTarget() : nullptr;
			if (!magicTarget) {
				return nullptr;
			}
			auto* effects = magicTarget->GetActiveEffectList();
			if (!effects) {
				return nullptr;
			}
			for (auto it = effects->begin(); it != effects->end(); ++it) {
				auto* effect = *it;
				if (effect && effect->usUniqueID == a_uniqueId) {
					return effect->GetBaseObject();
				}
			}
			return nullptr;
		}

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

				// A raised corpse is not a conjuration. Classifying it here, on the effect
				// that was just applied, keeps the record a reanimate even when the engine's
				// commanded roster does not list thralls at all: a reanimate record is
				// roster-independent, so the tactical sync leaves it alone and the end of
				// the effect (the corpse dies or loses its commander) is what releases it.
				//
				// Only the player's own reanimates: the registry's release pass and every AI
				// seat are the player's, so a thrall owned by anyone else would sit in the
				// registry forever.
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (evt.isApplied && player && a_event->caster.get() == player &&
				    evt.targetId != evt.casterId && a_event->target) {
					if (const auto* base = FindAppliedEffectSetting(a_event->target.get(), a_event->activeEffectUniqueID);
						base && base->HasArchetype(RE::EffectSetting::Archetype::kReanimate)) {
						ctx.GetRegistry().RegisterSummon(
							evt.casterId,
							evt.targetId,
							core::SummonType::Reanimate,
							core::kPermanentDuration,
							ctx.GetScheduler().GetTotalElapsedTime());
						logger::info("[EventAdapter] Actor 0x{:08X} was reanimated by the player; tracked as a reanimate.",
							evt.targetId);
					}
				}

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
