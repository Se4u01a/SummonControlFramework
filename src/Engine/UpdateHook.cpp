#include "Engine/UpdateHook.h"
#include "Core/CommandQueue.h"
#include "Core/FrameworkContext.h"
#include "Core/SummonModel.h"
#include "Core/SummonRegistry.h"
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

						a_summon->StopCombat();
						a_summon->StartCombat(target);
						a_summon->GetActorRuntimeData().currentCombatTarget = target->GetHandle();

						if (auto* cc = a_summon->GetActorRuntimeData().combatController) {
							cc->targetHandle = target->GetHandle();
							cc->startedCombat = true;
						}

						a_summon->EvaluatePackage(true, true);
					}
				}
				break;

			case core::CommandType::SetOrder:
				switch (a_cmd.order) {
				case core::SummonOrder::Return:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to RETURN to owner", a_summon->GetFormID());

						a_summon->StopCombat();
						a_summon->GetActorRuntimeData().currentCombatTarget.reset();

						if (auto* cc = a_summon->GetActorRuntimeData().combatController) {
							cc->targetHandle.reset();
							cc->startedCombat = false;
						}

						a_summon->EvaluatePackage(true, true);
					}
					break;

				case core::SummonOrder::WaitGuard:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to GUARD / WAIT", a_summon->GetFormID());

						a_summon->StopCombat();
						a_summon->GetActorRuntimeData().currentCombatTarget.reset();

						if (auto* cc = a_summon->GetActorRuntimeData().combatController) {
							cc->targetHandle.reset();
							cc->startedCombat = false;
						}

						a_summon->EvaluatePackage(true, true);
					}
					break;

				case core::SummonOrder::Follow:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to FOLLOW", a_summon->GetFormID());
						a_summon->EvaluatePackage(true, true);
					}
					break;

				case core::SummonOrder::MoveTo:
					{
						logger::info("[Execution] Summon 0x{:08X} ordered to MOVE TO ({:.1f}, {:.1f}, {:.1f})",
							a_summon->GetFormID(), a_cmd.targetPosition.x, a_cmd.targetPosition.y, a_cmd.targetPosition.z);

						a_summon->StopCombat();
						a_summon->GetActorRuntimeData().currentCombatTarget.reset();

						if (auto* cc = a_summon->GetActorRuntimeData().combatController) {
							cc->targetHandle.reset();
							cc->startedCombat = false;
						}

						RE::NiPoint3 dest{ a_cmd.targetPosition.x, a_cmd.targetPosition.y, a_cmd.targetPosition.z + 5.0f };
						static_cast<RE::TESObjectREFR*>(a_summon)->SetPosition(dest);
						a_summon->SetPosition(dest, true);
						a_summon->MoveToNearestNavmesh(0.0f);
						a_summon->EvaluatePackage(true, true);
					}
					break;
				}
				break;

			case core::CommandType::SetAggression:
				if (a_cmd.aggression == core::AggressionMode::Passive) {
					logger::info("[Execution] Summon 0x{:08X} set to PASSIVE - disengaging combat", a_summon->GetFormID());

					a_summon->StopCombat();
					a_summon->GetActorRuntimeData().currentCombatTarget.reset();

					if (auto* cc = a_summon->GetActorRuntimeData().combatController) {
						cc->targetHandle.reset();
						cc->startedCombat = false;
					}

					a_summon->EvaluatePackage(true, true);
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
			core::Command cmd{};
			while (cmdQueue.PopNext(a_timestamp, cmd)) {
				logger::info("[UpdateHook] Popped command (Type: {}, SummonId: 0x{:08X}, Priority: {})",
					core::ToString(cmd.type), cmd.summonId, core::ToString(cmd.priority));

				std::vector<core::EntityID> targetSummonIds;
				if (cmd.summonId != core::InvalidEntityID) {
					targetSummonIds.push_back(cmd.summonId);
				} else {
					for (const auto& [id, _] : liveActors) {
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

			// 5. Per-tick continuous AI enforcement for active summons
			for (const auto& [summonId, summonActor] : liveActors) {
				auto* model = registry.GetModel(summonId);
				if (!model || !summonActor) {
					continue;
				}

				// Passive mode enforcement
				if (model->aggression == core::AggressionMode::Passive) {
					if (summonActor->IsInCombat()) {
						summonActor->StopCombat();
						summonActor->GetActorRuntimeData().currentCombatTarget.reset();
						if (auto* cc = summonActor->GetActorRuntimeData().combatController) {
							cc->targetHandle.reset();
							cc->startedCombat = false;
						}
					}
				}
				// Return order enforcement: keep combat disengaged until arriving near owner
				else if (model->order == core::SummonOrder::Return) {
					if (summonActor->IsInCombat()) {
						summonActor->StopCombat();
						summonActor->GetActorRuntimeData().currentCombatTarget.reset();
						if (auto* cc = summonActor->GetActorRuntimeData().combatController) {
							cc->targetHandle.reset();
							cc->startedCombat = false;
						}
					}

					const float distSq = summonActor->GetPosition().GetSquaredDistance(a_player->GetPosition());
					if (distSq <= 350.0f * 350.0f) {
						model->order = core::SummonOrder::Follow;
						ctx.GetStateMachine().TransitionTo(*model, core::SummonState::FollowOwner, "Returned to owner range");
						summonActor->EvaluatePackage(true, true);
					}
				}
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
		core::FrameworkContext::GetSingleton().ResetRuntimeState();
	}
}
