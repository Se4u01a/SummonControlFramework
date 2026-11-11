#include "Engine/UpdateHook.h"
#include "Core/CommandQueue.h"
#include "Core/CombatPolicy.h"
#include "Core/FrameworkContext.h"
#include "Core/SummonModel.h"
#include "Core/SummonRegistry.h"
#include "Engine/ActorSpace.h"
#include "Engine/CombatGate.h"
#include "Engine/CommandActions.h"
#include "Engine/KeywordApi.h"
#include "Engine/PassThrough.h"
#include "Engine/Persistence.h"
#include "Engine/SummonLimit.h"
#include "Core/SummonLimit.h"
#include "Integrations/MinionPanel.h"
#include "Integrations/Papyrus.h"
#include "Engine/SummonMovement.h"
#include "PCH.h"

#include <RE/A/ActiveEffect.h>
#include <RE/A/Actor.h>
#include <RE/A/AIProcess.h>
#include <RE/B/BSContainer.h>
#include <RE/C/CombatController.h>
#include <RE/E/EffectSetting.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/N/NiPoint3.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/U/UI.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scf::engine::update
{
	namespace
	{
		float s_totalElapsed = 0.0f;

		// Engage one explicit target (Focus order, or a Command Rush): the model must already
		// carry it (CommandQueue::ApplyToModel of a SetFocusTarget).
		void EngageFocus(RE::PlayerCharacter* a_player, RE::Actor* a_summon, core::SummonModel* a_model, RE::Actor* a_target)
		{
			auto* target = a_target;
			if (!target || target->IsDead() || target == a_summon || target == a_player) {
				return;
			}

			logger::info("[Execution] Summon 0x{:08X} switching focus to 0x{:08X} ({})",
				a_summon->GetFormID(), target->GetFormID(), target->GetName());

			// Publish the new policy first: the StartCombat seat must already know
			// that this one target is permitted (a previous Attack order may have
			// restricted combat to a different actor).
			if (a_model) {
				combat::UpdateForSummon(a_summon, core::BuildCombatPolicy(*a_model, true));
			}

			// Fresh engagement: StartCombat (through the seat, which now permits
			// exactly this actor) builds a new CombatController/CombatGroup whose
			// first combat target is the focus. From then on the target pin makes
			// the controller's own selectors keep choosing it every combat update;
			// no CombatController member is written from here (the combat workers
			// own them and used to overwrite such writes on the next update).
			combat::BreakCombat(a_summon);
			const bool started = a_summon->StartCombat(target, nullptr);

			const float distance = a_summon->GetPosition().GetDistance(target->GetPosition());
			logger::info("[Execution] Summon 0x{:08X} StartCombat(0x{:08X}) -> {} (dist {:.0f}, hostile {}, inCombat {})",
				a_summon->GetFormID(),
				target->GetFormID(),
				started ? "true" : "false",
				distance,
				target->IsHostileToActor(a_summon) ? "yes" : "no",
				a_summon->IsInCombat() ? "yes" : "no");
			if (!started) {
				logger::warn("[Execution] Summon 0x{:08X} could not start combat with 0x{:08X} (combat policy or engine refusal)",
					a_summon->GetFormID(), target->GetFormID());
			}
		}

		void ExecuteCommandOnActor(
			RE::PlayerCharacter* a_player,
			RE::Actor* a_summon,
			core::SummonModel* a_model,
			const core::Command& a_cmd)
		{
			if (!a_summon || a_summon->IsDead()) {
				return;
			}

			switch (a_cmd.type) {
			case core::CommandType::SetFocusTarget:
				EngageFocus(a_player, a_summon, a_model, RE::TESForm::LookupByID<RE::Actor>(a_cmd.focusTargetId));
				break;

			case core::CommandType::CommandAction:
				{
					auto* target = RE::TESForm::LookupByID<RE::Actor>(a_cmd.focusTargetId);
					auto& ctx = core::FrameworkContext::GetSingleton();
					const auto outcome = actions::Execute(a_summon, a_model, target, s_totalElapsed);
					if (outcome == actions::Outcome::Rush && a_model) {
						core::Command focus = a_cmd;
						focus.type = core::CommandType::SetFocusTarget;
						ctx.GetCommandQueue().ApplyToModel(focus, *a_model, &ctx.GetStateMachine());
						EngageFocus(a_player, a_summon, a_model, target);
					}
				}
				break;

			case core::CommandType::SetOrder:
				switch (a_cmd.order) {
				case core::SummonOrder::Return:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to RETURN to owner", a_summon->GetFormID());

						combat::BreakCombat(a_summon);
					}
					break;

				case core::SummonOrder::WaitGuard:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to GUARD / WAIT", a_summon->GetFormID());

						combat::BreakCombat(a_summon);
					}
					break;

				case core::SummonOrder::Follow:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to FOLLOW", a_summon->GetFormID());

						combat::BreakCombat(a_summon);
					}
					break;

				case core::SummonOrder::MoveTo:
					{
						// The physical walking is driven by engine::movement from the
						// SummonModel destination stored by CommandQueue::ApplyToModel.
						logger::info("[Execution] Summon 0x{:08X} ordered to MOVE TO ({:.1f}, {:.1f}, {:.1f})",
							a_summon->GetFormID(), a_cmd.targetPosition.x, a_cmd.targetPosition.y, a_cmd.targetPosition.z);
					}
					break;
				}
				break;

			case core::CommandType::SetAggression:
				if (a_cmd.aggression == core::AggressionMode::Passive) {
					logger::info("[Execution] Summon 0x{:08X} set to PASSIVE - disengaging combat", a_summon->GetFormID());

					combat::BreakCombat(a_summon);

					a_summon->EvaluatePackage(true, false);
				}
				break;

			case core::CommandType::Dispel:
				logger::info("[Execution] Summon 0x{:08X} DISPELLED", a_summon->GetFormID());
				a_summon->KillImmediate();
				break;
			}
		}

		// Which archetype does the effect the engine attached to a commanded actor carry?
		// A raised corpse is the same "command" machinery as a summon (RE::ReanimateEffect
		// derives from RE::CommandEffect, which owns the commandedActor handle), only its
		// magic effect archetype differs. Everything that is not a reanimate keeps the
		// existing classification, so unknown modded commands behave as before.
		[[nodiscard]] core::SummonType ClassifySummon(const RE::ActiveEffect* a_effect) noexcept
		{
			const auto* base = a_effect ? a_effect->GetBaseObject() : nullptr;
			if (base && base->HasArchetype(RE::EffectSetting::Archetype::kReanimate)) {
				return core::SummonType::Reanimate;
			}
			return core::SummonType::SummonCreature;
		}

		void InternalSyncPlayerSummons(RE::PlayerCharacter* a_player, float a_timestamp)
		{
			if (!a_player) {
				return;
			}

			auto* currentProcess = a_player->GetActorRuntimeData().currentProcess;
			if (!currentProcess || !currentProcess->middleHigh) {
				return;
			}

			auto& ctx = core::FrameworkContext::GetSingleton();
			auto& registry = ctx.GetRegistry();
			auto& cmdQueue = ctx.GetCommandQueue();
			const auto ownerId = a_player->GetFormID();

			// The tactical tick runs on the game thread; the Papyrus natives use this call
			// to tell the registry-owning thread from any other one (see Integrations/Papyrus.cpp).
			papyrus::MarkGameThread();

			const auto& commanded = currentProcess->middleHigh->commandedActors;
			std::unordered_set<core::EntityID> currentEngineSummons;
			std::unordered_map<core::EntityID, RE::Actor*> liveActors;

			// PoE2 M5: the engine limit for the player (seat in engine::summonlimit), and SCF's
			// tracking cap: never below what the engine keeps, so the registry cannot
			// "displace" a summon the engine still commands (the displacement used to be
			// undone on the next tick anyway, when RegisterSummon refreshed the record).
			{
				const auto& general = ctx.GetSettings().Get().general;
				summonlimit::Publish(general.playerSummonLimit);
				std::size_t rosterSize = 0;
				for (const auto& data : commanded) {
					const auto actor = data.commandedActor.get();
					rosterSize += (actor && !actor->IsDead()) ? 1 : 0;
				}
				registry.SetOwnerMaxSummons(ownerId, core::TrackingCapacity(general.maxSummonsPerOwner, rosterSize));
			}

			// 1. Sync engine commanded actors with SummonRegistry
			for (const auto& data : commanded) {
				auto actorPtr = data.commandedActor.get();
				if (!actorPtr) {
					continue;
				}

				const auto summonId = actorPtr->GetFormID();
				if (actorPtr->IsDead()) {
					registry.NotifyDeath(summonId, a_timestamp);
					persistence::OnSummonDied(summonId, a_timestamp);  // idempotent while the corpse stays listed
					continue;
				}

				currentEngineSummons.insert(summonId);
				liveActors[summonId] = actorPtr.get();

				// A reanimated corpse is a commanded actor like a summoned creature; only
				// the archetype of the effect the engine linked to it tells them apart.
				const auto type = ClassifySummon(data.activeEffect);
				float remainingDuration = core::kPermanentDuration;
				if (data.activeEffect) {
					remainingDuration = core::RemainingDurationFromEffect(
						data.activeEffect->duration,
						data.activeEffect->elapsedSeconds);
				}

				core::EntityID displaced = core::InvalidEntityID;
				registry.RegisterSummon(
					ownerId,
					summonId,
					type,
					remainingDuration,
					a_timestamp,
					&displaced);

				if (auto* model = registry.GetModel(summonId)) {
					model->isReanimated = (type == core::SummonType::Reanimate);
				}

				if (displaced != core::InvalidEntityID) {
					logger::info("[UpdateHook] Summon limit exceeded, displacing oldest summon 0x{:08X}", displaced);
				}
			}

			// 1b. Explicitly registered summons are absent from the engine roster: the
			//     script put them in the registry, so the registry is where the tick finds
			//     them. They are driven like any other summon (orders, movement, combat
			//     policy, panel row) while the actor is alive and shares the player's loaded
			//     space; step 2b releases the ones that are gone.
			for (const auto summonId : registry.GetActiveSummonsForOwner(ownerId)) {
				if (currentEngineSummons.contains(summonId)) {
					continue;  // the roster already supplied it
				}
				const auto* record = registry.FindRecord(summonId);
				if (!record || !record->explicitRegistration) {
					continue;  // a reanimate is released by step 2b, not driven from here
				}
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(summonId);
				if (!actor || actor->IsDead()) {
					continue;  // step 2b releases these
				}
				if (!ShareLoadedSpace(actor, a_player)) {
					// An unloaded scripted summon cannot be moved or measured; keep the
					// record and pick it up again once the player is in its cell.
					continue;
				}
				liveActors[summonId] = actor;
			}

			// 2. Release the records the engine roster no longer backs. Records whose
			//    lifetime SCF owns are skipped here and released by their own signal (2b).
			const std::vector<core::EntityID> engineRoster(currentEngineSummons.begin(), currentEngineSummons.end());
			for (const auto prevSummonId : registry.GetOrphanedSummons(ownerId, engineRoster)) {
				registry.NotifyEffectRemoved(ownerId, prevSummonId, a_timestamp);
				persistence::OnSummonRemoved(prevSummonId);
			}

			// 2b. Explicitly registered summons and reanimated corpses: the roster says
			//     nothing about them, so release only on the signal that owns the record -
			//     the script's UnregisterSummon, the actor's death, a vanished form, or the
			//     end of the reanimate effect (the engine clears the commanding link then).
			for (const auto summonId : registry.GetActiveSummonsForOwner(ownerId)) {
				const auto* record = registry.FindRecord(summonId);
				if (!record || (!record->explicitRegistration && record->type != core::SummonType::Reanimate)) {
					continue;
				}
				if (currentEngineSummons.contains(summonId)) {
					continue;  // the roster backs it after all: step 1 owns it
				}

				auto* actor = RE::TESForm::LookupByID<RE::Actor>(summonId);
				if (!actor) {
					logger::info("[UpdateHook] Summon 0x{:08X} ({}) no longer exists; releasing the record.",
						summonId, record->explicitRegistration ? "explicit registration" : "reanimated");
					registry.UnregisterSummon(summonId,
						record->explicitRegistration ? "explicit registration: form gone" : "reanimated: form gone");
					persistence::OnSummonRemoved(summonId);
					continue;
				}
				if (record->explicitRegistration) {
					continue;  // alive: the script decides through SCF.UnregisterSummon
				}
				if (actor->IsDead()) {
					// A thrall dies when its reanimate effect ends; the death event usually
					// arrives first, this keeps the record consistent if it does not.
					registry.NotifyDeath(summonId, a_timestamp);
					persistence::OnSummonDied(summonId, a_timestamp);
					continue;
				}
				if (actor->GetCommandingActor().get() == a_player) {
					continue;  // still a commanded thrall
				}
				logger::info("[UpdateHook] Reanimated summon 0x{:08X} is no longer commanded; releasing the record.", summonId);
				registry.NotifyEffectRemoved(ownerId, summonId, a_timestamp);
				persistence::OnSummonRemoved(summonId);
			}

			// 3. Automatically garbage collect dead / dispatched summons after 3 seconds grace period
			registry.ClearStaleRecords(a_timestamp, 3.0f);

			// 4. Drain pending commands from CommandQueue and dispatch to engine actors
			//    Iteration order is stable (ascending FormID) so formation slots and
			//    broadcast command handling do not shuffle between ticks.
			std::vector<std::pair<core::EntityID, RE::Actor*>> orderedSummons;
			orderedSummons.reserve(liveActors.size());
			for (const auto& [id, actor] : liveActors) {
				orderedSummons.emplace_back(id, actor);
			}
			std::sort(orderedSummons.begin(), orderedSummons.end(),
				[](const auto& a_lhs, const auto& a_rhs) { return a_lhs.first < a_rhs.first; });

			// Per-summon movement and combat bookkeeping must survive for every driven
			// record, including the explicit registrations the engine roster does not list.
			std::unordered_set<core::EntityID> drivenSummons;
			drivenSummons.reserve(orderedSummons.size());
			for (const auto& [id, actor] : orderedSummons) {
				drivenSummons.insert(id);
			}

			core::Command cmd{};
			while (cmdQueue.PopNext(a_timestamp, cmd)) {
				logger::info("[UpdateHook] Popped command (Type: {}, SummonId: 0x{:08X}, Priority: {})",
					core::ToString(cmd.type), cmd.summonId, core::ToString(cmd.priority));

				std::vector<core::EntityID> targetSummonIds;
				if (cmd.summonId != core::InvalidEntityID) {
					targetSummonIds.push_back(cmd.summonId);
				} else {
					for (const auto& [id, _] : orderedSummons) {
						targetSummonIds.push_back(id);
					}
				}

				for (const auto sid : targetSummonIds) {
					auto it = liveActors.find(sid);
					if (it == liveActors.end() || !it->second) {
						continue;
					}

					auto* summonActor = it->second;
					auto* model = registry.GetModel(sid);
					if (model) {
						cmdQueue.ApplyToModel(cmd, *model, &ctx.GetStateMachine());
					}

					ExecuteCommandOnActor(a_player, summonActor, model, cmd);
				}
				if (cmd.type == core::CommandType::CommandAction) {
					actions::ReportBatch();
				}
			}
			actions::Tick(a_timestamp);

			// 5. Per-tick order execution and continuous AI enforcement
			const auto& settings = ctx.GetSettings().Get();

			// Pass-through table for the movement-avoidance and collision-filter seats:
			// the owner and its live summons (system groups read from their controllers).
			{
				std::vector<passthrough::Member> members;
				members.reserve(orderedSummons.size() + 1);
				members.push_back({ core::PassThroughParty{ ownerId, core::InvalidEntityID }, a_player });
				for (const auto& [id, actor] : orderedSummons) {
					members.push_back({ core::PassThroughParty{ id, ownerId }, actor });
				}
				// Keyword API snapshot (empty while no mod defines the keywords).
				const auto world = keywords::ScanWorld(a_player);
				passthrough::Publish(members, world, settings.collision);
				combat::PublishUntargetable(world);
			}

			// PoE2-style aggression radius around the owner for engine-chosen fights.
			{
				const auto& ownerPosition = a_player->GetPosition();
				combat::PublishAggressionArea({ ownerPosition.x, ownerPosition.y, ownerPosition.z }, settings.combat.aggressionRadius);
			}
			for (std::size_t i = 0; i < orderedSummons.size(); ++i) {
				const auto summonId = orderedSummons[i].first;
				auto* summonActor = orderedSummons[i].second;
				auto* model = registry.GetModel(summonId);
				if (!model || !summonActor) {
					continue;
				}

				// A focus target that vanished must not keep the summon locked in "attack":
				// clear it before the combat policy is derived from it.
				if (model->currentTargetId != core::InvalidEntityID) {
					auto* focus = RE::TESForm::LookupByID<RE::Actor>(model->currentTargetId);
					if (!focus || focus->IsDead() || !ShareLoadedSpace(focus, summonActor)) {
						logger::info("[UpdateHook] Summon 0x{:08X} focus target 0x{:08X} is gone; resuming order",
							summonId, model->currentTargetId);
						model->currentTargetId = core::InvalidEntityID;
					}
				}

				// Combat policy: may this summon fight at all this tick, and against whom?
				// The gate denies StartCombat for managed summons, sets the native
				// attack/cast deny bits and breaks an existing fight when forbidden.
				const core::CombatPolicy policy = core::BuildCombatPolicy(*model, model->currentTargetId != core::InvalidEntityID);
				combat::UpdateForSummon(summonActor, policy);

				// Movement execution for the current order (follow / return / move-to /
				// guard / attack approach). It also flips Return back to Follow on arrival.
				movement::UpdateForSummon(
					a_player,
					summonActor,
					*model,
					ctx.GetStateMachine(),
					settings,
					i,
					orderedSummons.size(),
					a_timestamp,
					policy.allowCombat);

				// Combat Leash enforcement
				if (settings.combat.enableCombatLeash && policy.allowCombat && summonActor->IsInCombat()) {
					const float distToPlayerSq = summonActor->GetPosition().GetSquaredDistance(a_player->GetPosition());
					const float leashRadius = settings.combat.combatLeashRadius;
					if (distToPlayerSq > leashRadius * leashRadius) {
						if (settings.combat.returnToOwnerWhenOutsideLeash) {
							logger::info("[UpdateHook] Summon 0x{:08X} outside combat leash radius ({:.1f} > {:.1f}), returning to owner",
								summonId, std::sqrt(distToPlayerSq), leashRadius);
							combat::BreakCombat(summonActor);
							model->order = core::SummonOrder::Return;
							model->hasMoveTarget = false;
							ctx.GetStateMachine().TransitionTo(*model, core::SummonState::Return, "Exceeded combat leash");
						}
					}
				}
			}

			// Drop bookkeeping for summons that stopped being driven (dispel, death,
			// cell detach, displacement) so a recycled handle cannot inherit it.
			movement::RetainOnly(drivenSummons);
			combat::RetainOnly(drivenSummons);

			// PoE2 M4: persistent summons (effect kept alive, Magicka reserve, revives).
			{
				std::vector<RE::Actor*> liveSummonActors;
				liveSummonActors.reserve(orderedSummons.size());
				for (const auto& [id, actor] : orderedSummons) {
					liveSummonActors.push_back(actor);
				}
				persistence::Tick(a_player, a_timestamp, liveSummonActors, registry, settings.persistence);
			}

			// PoE2 M6: minion panel snapshot (drawn by FLICK from this copy).
			{
				std::vector<integrations::CommandCooldownView> cooldowns;
				cooldowns.reserve(orderedSummons.size());
				for (const auto& [id, actor] : orderedSummons) {
					if (actions::HasCommand(actor)) {
						cooldowns.push_back({ id, actions::CooldownRemaining(id, a_timestamp), 0.0f });
					}
				}
				integrations::MinionPanel::PublishSnapshot(ownerId, a_timestamp, cooldowns);
			}
		}

		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_player, float a_delta)
			{
				Tick(a_player, a_delta);
				func(a_player, a_delta);
			}

			static void Tick(RE::PlayerCharacter* a_player, float a_delta)
			{
				if (!a_player || !(a_delta > 0.0f)) {
					return;
				}

				auto* ui = RE::UI::GetSingleton();
				if (ui && ui->GameIsPaused()) {
					return;
				}

				s_totalElapsed += a_delta;

				auto& ctx = core::FrameworkContext::GetSingleton();
				if (!ctx.GetSettings().Get().general.enabled) {
					return;
				}

				if (ctx.GetScheduler().ShouldTick(a_delta)) {
					InternalSyncPlayerSummons(a_player, s_totalElapsed);
				}
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void SyncPlayerSummons()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (player) {
			InternalSyncPlayerSummons(player, s_totalElapsed);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		logger::info("[UpdateHook] PlayerCharacter::Update hooked successfully at vfunc 0xAD.");
	}

	void Reset()
	{
		s_totalElapsed = 0.0f;
		movement::ResetAll();
		combat::ResetAll();
		passthrough::ResetAll();
		actions::Reset();
		persistence::Reset();
		integrations::MinionPanel::ClearSnapshot();
		core::FrameworkContext::GetSingleton().ResetRuntimeState();
	}
}
