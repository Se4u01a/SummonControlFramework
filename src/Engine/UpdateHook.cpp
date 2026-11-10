#include "Engine/UpdateHook.h"
#include "Core/CommandQueue.h"
#include "Core/CombatPolicy.h"
#include "Core/FrameworkContext.h"
#include "Core/SummonModel.h"
#include "Core/SummonRegistry.h"
#include "Engine/ActorSpace.h"
#include "Engine/CombatGate.h"
#include "Engine/SummonMovement.h"
#include "PCH.h"

#include <RE/A/ActiveEffect.h>
#include <RE/A/Actor.h>
#include <RE/A/AIProcess.h>
#include <RE/B/BSContainer.h>
#include <RE/C/CombatController.h>
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

		void ExecuteCommandOnActor(
			RE::PlayerCharacter* a_player,
			RE::Actor* a_summon,
			[[maybe_unused]] core::SummonModel* a_model,
			const core::Command& a_cmd)
		{
			if (!a_summon || a_summon->IsDead()) {
				return;
			}

			switch (a_cmd.type) {
			case core::CommandType::SetFocusTarget:
				{
					auto* target = RE::TESForm::LookupByID<RE::Actor>(a_cmd.focusTargetId);
					if (target && !target->IsDead() && target != a_summon && target != a_player) {
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

			const auto& commanded = currentProcess->middleHigh->commandedActors;
			std::unordered_set<core::EntityID> currentEngineSummons;
			std::unordered_map<core::EntityID, RE::Actor*> liveActors;

			// 1. Sync engine commanded actors with SummonRegistry
			for (const auto& data : commanded) {
				auto actorPtr = data.commandedActor.get();
				if (!actorPtr) {
					continue;
				}

				const auto summonId = actorPtr->GetFormID();
				if (actorPtr->IsDead()) {
					registry.NotifyDeath(summonId, a_timestamp);
					continue;
				}

				currentEngineSummons.insert(summonId);
				liveActors[summonId] = actorPtr.get();

				float remainingDuration = -1.0f;
				if (data.activeEffect) {
					remainingDuration = data.activeEffect->duration - data.activeEffect->elapsedSeconds;
				}

				core::EntityID displaced = core::InvalidEntityID;
				registry.RegisterSummon(
					ownerId,
					summonId,
					core::SummonType::SummonCreature,
					remainingDuration,
					a_timestamp,
					&displaced);

				if (displaced != core::InvalidEntityID) {
					logger::info("[UpdateHook] Summon limit exceeded, displacing oldest summon 0x{:08X}", displaced);
				}
			}

			// 2. Cleanly remove any summons that are no longer commanded by the player
			const auto activeSummons = registry.GetActiveSummonsForOwner(ownerId);
			for (const auto prevSummonId : activeSummons) {
				if (currentEngineSummons.find(prevSummonId) == currentEngineSummons.end()) {
					registry.NotifyEffectRemoved(ownerId, prevSummonId, a_timestamp);
				}
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
			}

			// 5. Per-tick order execution and continuous AI enforcement
			const auto& settings = ctx.GetSettings().Get();
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

			// Drop bookkeeping for summons that left the engine roster (dispel, death,
			// cell detach, displacement) so a recycled handle cannot inherit it.
			movement::RetainOnly(currentEngineSummons);
			combat::RetainOnly(currentEngineSummons);
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
		core::FrameworkContext::GetSingleton().ResetRuntimeState();
	}
}
