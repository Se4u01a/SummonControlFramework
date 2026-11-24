#include "Engine/UpdateHook.h"
#include "Core/CombatPolicy.h"
#include "Core/CommandQueue.h"
#include "Core/Formation.h"
#include "Core/FrameworkContext.h"
#include "Core/KeywordApi.h"
#include "Core/SummonLimit.h"
#include "Core/SummonModel.h"
#include "Core/SummonRegistry.h"
#include "Engine/ActorSpace.h"
#include "Engine/AttackTrace.h"
#include "Engine/CombatGate.h"
#include "Engine/CombatRadius.h"
#include "Engine/CommandActions.h"
#include "Engine/KeywordApi.h"
#include "Engine/PassThrough.h"
#include "Engine/Persistence.h"
#include "Engine/SummonLimit.h"
#include "Engine/SummonMovement.h"
#include "Engine/WolfManeuver.h"
#include "Integrations/MinionPanel.h"
#include "Integrations/Papyrus.h"
#include "Integrations/TrueHUDBackend.h"
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
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scf::engine::update
{
	namespace
	{
		float s_totalElapsed = 0.0f;

		// Keyword API v2 per-session tracking (cleared in Reset()).
		std::unordered_set<core::EntityID> s_autoRegistered;
		std::unordered_set<core::EntityID> s_defaultsApplied;

		// Engage one explicit target (Focus order, or a Command Rush): the model must already
		// carry it (CommandQueue::ApplyToModel of a SetFocusTarget).
		void EngageFocus(
			RE::PlayerCharacter* a_player,
			RE::Actor* a_summon,
			core::SummonModel* a_model,
			RE::Actor* a_target,
			const core::SummonKeywordProfile* a_profile = nullptr)
		{
			auto* target = a_target;
			if (!target || target->IsDead() || target == a_summon || target == a_player) {
				return;
			}
			if (a_profile && a_profile->Has(core::kKwNonCombat)) {
				return;
			}

			logger::info("[Execution] Summon 0x{:08X} switching focus to 0x{:08X} ({})",
				a_summon->GetFormID(), target->GetFormID(), target->GetName());

			// Publish the new policy first: the StartCombat / CanAttackActor seats must already
			// know that this one target is permitted (a previous Attack order may have
			// restricted combat to a different actor).
			if (a_model) {
				auto policy = core::BuildCombatPolicy(*a_model, true);
				combat::SummonCombatDirectives directives{};
				directives.ownerId = a_player ? a_player->GetFormID() : core::InvalidEntityID;
				if (a_profile) {
					policy = core::ApplyKeywordCombatPolicy(policy, *a_profile, false, true, true);
					directives.alwaysNoCircling = a_profile->Has(core::kKwNoCircling);
					directives.ignoreAggressionRadius = a_profile->Has(core::kKwIgnoreCombatRadius);
					directives.specialtyFlags = a_profile->flags & core::kKwTargetingSpecialtyMask;
				}
				combat::UpdateForSummon(a_summon, policy, directives);
			}

			// Avoid tearing down and rebuilding the CombatController if the summon is already
			// fighting this exact focus target.
			const auto currentTarget = a_summon->GetActorRuntimeData().currentCombatTarget.get();
			const bool alreadyFightingTarget = a_summon->IsInCombat() && currentTarget && currentTarget.get() == target;
			if (alreadyFightingTarget) {
				logger::info("[Execution] Summon 0x{:08X} already fighting focus 0x{:08X}; keeping active combat",
					a_summon->GetFormID(), target->GetFormID());
				return;
			}

			// Fresh engagement: StartCombat (through the StartCombat + CanAttackActor seats,
			// which now permit exactly this actor even before the target enters combat) builds
			// a new CombatController/CombatGroup whose first combat target is the focus. From
			// then on the target pin makes the controller's own selectors keep choosing it
			// every combat update; no CombatController member is written from here (the combat
			// workers own them and used to overwrite such writes on the next update).
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

		// Publishes the updated CombatPolicy BEFORE calling BreakCombat so no combat worker
		// thread can re-enter combat in the window between StopCombat and step 5.
		void PublishPolicyAndBreakCombat(
			RE::Actor* a_summon,
			const core::SummonModel* a_model,
			bool a_evaluatePackage,
			const core::SummonKeywordProfile* a_profile = nullptr,
			core::EntityID a_ownerId = core::InvalidEntityID)
		{
			if (a_model) {
				auto policy = core::BuildCombatPolicy(*a_model, a_model->currentTargetId != core::InvalidEntityID);
				combat::SummonCombatDirectives directives{};
				directives.ownerId = a_ownerId;
				if (a_profile) {
					policy = core::ApplyKeywordCombatPolicy(policy, *a_profile, false, true, false);
					directives.alwaysNoCircling = a_profile->Has(core::kKwNoCircling);
					directives.ignoreAggressionRadius = a_profile->Has(core::kKwIgnoreCombatRadius);
					directives.specialtyFlags = a_profile->flags & core::kKwTargetingSpecialtyMask;
				}
				combat::UpdateForSummon(a_summon, policy, directives);
			}
			combat::BreakCombat(a_summon);
			if (a_evaluatePackage) {
				a_summon->EvaluatePackage(true, false);
			}
		}

		void ExecuteCommandOnActor(
			RE::PlayerCharacter* a_player,
			RE::Actor* a_summon,
			core::SummonModel* a_model,
			const core::Command& a_cmd,
			const core::SummonKeywordProfile* a_profile,
			const RE::EffectSetting* a_sourceEffect)
		{
			if (!a_summon || a_summon->IsDead()) {
				return;
			}
			const auto ownerId = a_player ? a_player->GetFormID() : core::InvalidEntityID;

			switch (a_cmd.type) {
			case core::CommandType::SetFocusTarget:
				{
					auto* target = RE::TESForm::LookupByID<RE::Actor>(a_cmd.focusTargetId);
					if (a_cmd.attackTrace && target && !target->IsDead() && a_model &&
					    core::BuildCombatPolicy(*a_model, true).restrictToTarget &&
					    a_model->currentTargetId == a_cmd.focusTargetId) {
						attack_trace::Begin(*a_summon, *target, *a_cmd.attackTrace);
					}
					EngageFocus(a_player, a_summon, a_model, target, a_profile);
				}
				break;

			case core::CommandType::CommandAction:
				{
					auto* target = RE::TESForm::LookupByID<RE::Actor>(a_cmd.focusTargetId);
					auto& ctx = core::FrameworkContext::GetSingleton();
					const auto outcome = actions::Execute(a_summon, a_model, target, s_totalElapsed, a_sourceEffect);
					if (outcome == actions::Outcome::Rush && a_model) {
						core::Command focus = a_cmd;
						focus.type = core::CommandType::SetFocusTarget;
						ctx.GetCommandQueue().ApplyToModel(focus, *a_model, &ctx.GetStateMachine());
						EngageFocus(a_player, a_summon, a_model, target, a_profile);
					}
				}
				break;

			case core::CommandType::SetOrder:
				movement::NotifyOrderIssued(a_summon->GetFormID(), a_cmd.order, s_totalElapsed);
				switch (a_cmd.order) {
				case core::SummonOrder::Return:
					logger::info("[Execution] Summon 0x{:08X} ordered to RETURN to owner", a_summon->GetFormID());
					PublishPolicyAndBreakCombat(a_summon, a_model, true, a_profile, ownerId);
					break;

				case core::SummonOrder::WaitGuard:
					logger::info("[Execution] Summon 0x{:08X} ordered to GUARD / WAIT", a_summon->GetFormID());
					PublishPolicyAndBreakCombat(a_summon, a_model, false, a_profile, ownerId);
					break;

				case core::SummonOrder::Follow:
					logger::info("[Execution] Summon 0x{:08X} ordered to FOLLOW", a_summon->GetFormID());
					PublishPolicyAndBreakCombat(a_summon, a_model, true, a_profile, ownerId);
					break;

				case core::SummonOrder::MoveTo:
					// The physical walking is driven by engine::movement from the
					// SummonModel destination stored by CommandQueue::ApplyToModel.
					logger::info("[Execution] Summon 0x{:08X} ordered to MOVE TO ({:.1f}, {:.1f}, {:.1f})",
						a_summon->GetFormID(), a_cmd.targetPosition.x, a_cmd.targetPosition.y, a_cmd.targetPosition.z);
					PublishPolicyAndBreakCombat(a_summon, a_model, false, a_profile, ownerId);
					break;

				default:
					break;
				}
				break;

			case core::CommandType::SetAggression:
				if (a_cmd.aggression == core::AggressionMode::Passive) {
					logger::info("[Execution] Summon 0x{:08X} set to PASSIVE - disengaging combat", a_summon->GetFormID());
					PublishPolicyAndBreakCombat(a_summon, a_model, true, a_profile, ownerId);
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

			radius::BeginTick();
			const auto& commanded = currentProcess->middleHigh->commandedActors;
			std::unordered_set<core::EntityID> currentEngineSummons;
			std::unordered_map<core::EntityID, RE::Actor*> liveActors;
			std::unordered_map<core::EntityID, core::SummonKeywordProfile> profiles;
			std::unordered_map<core::EntityID, const RE::EffectSetting*> sourceEffects;
			std::unordered_map<core::EntityID, float> taggedSummons;
			const auto resolveRadius = [&](RE::Actor& actor, const RE::EffectSetting* source, const core::SummonKeywordProfile& profile) {
				if (actor.Is3DLoaded() && ShareLoadedSpace(&actor, a_player)) {
					const float scrRadius = radius::Resolve(actor, source);
					const float effective = core::EffectiveCombatRadius(scrRadius, profile);
					if (effective > 0.0f) {
						taggedSummons.insert_or_assign(actor.GetFormID(), effective);
					}
				}
			};

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
			std::uint32_t freeSlotCount = 0;
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

				const auto* sourceEffect = data.activeEffect ? data.activeEffect->GetBaseObject() : nullptr;
				const auto profile = keywords::ProfileOf(actorPtr.get(), sourceEffect);
				if (profile.Has(core::kKwUnmanaged)) {
					if (registry.IsRegistered(summonId)) {
						registry.UnregisterSummon(summonId, "SCF_Unmanaged");
						persistence::OnSummonRemoved(summonId);
					}
					continue;
				}

				const bool freeSlot = profile.Has(core::kKwFreeSlot);
				if (freeSlot) {
					++freeSlotCount;
				}

				currentEngineSummons.insert(summonId);
				liveActors[summonId] = actorPtr.get();
				profiles[summonId] = profile;
				sourceEffects[summonId] = sourceEffect;
				resolveRadius(*actorPtr, sourceEffect, profile);

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
					&displaced,
					false,
					freeSlot);
				registry.SetFreeSlot(summonId, freeSlot);

				if (auto* model = registry.GetModel(summonId)) {
					model->isReanimated = (type == core::SummonType::Reanimate);
				}

				if (displaced != core::InvalidEntityID) {
					logger::info("[UpdateHook] Summon limit exceeded, displacing oldest summon 0x{:08X}", displaced);
				}
			}
			summonlimit::PublishFreeSlots(freeSlotCount);

			// 1a. Keyword API world scan (when any world keyword is defined in the load order)
			//     and SCF_AutoRegister for scripted/custom friendly actors outside the roster.
			std::vector<keywords::WorldActor> world;
			bool worldScanned = false;
			if (keywords::WorldActive()) {
				world = keywords::ScanWorld(a_player, false);
				worldScanned = true;

				for (const auto& wa : world) {
					if (!wa.actor || wa.id == ownerId || currentEngineSummons.contains(wa.id)) {
						continue;
					}
					if ((wa.flags & core::kKwAutoRegister) == 0) {
						continue;
					}
					if (wa.actor->IsHostileToActor(a_player) || a_player->IsHostileToActor(wa.actor)) {
						continue;
					}
					if ((keywords::FlagsOf(wa.actor) & core::kKwUnmanaged) != 0) {
						continue;
					}
					if (!registry.IsRegistered(wa.id)) {
						registry.RegisterSummon(
							ownerId,
							wa.id,
							core::SummonType::Scripted,
							core::kPermanentDuration,
							a_timestamp,
							nullptr,
							true,
							true);
						s_autoRegistered.insert(wa.id);
						logger::info("[KeywordApi] Auto-registered actor 0x{:08X} ({}) via SCF_AutoRegister.",
							wa.id, wa.actor->GetName());
					}
				}

				for (auto it = s_autoRegistered.begin(); it != s_autoRegistered.end();) {
					const auto autoId = *it;
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(autoId);
					bool drop = !actor || actor->IsDead();
					if (!drop && ShareLoadedSpace(actor, a_player)) {
						const auto flags = keywords::FlagsOf(actor);
						drop = (flags & core::kKwAutoRegister) == 0 ||
						       (flags & core::kKwUnmanaged) != 0 ||
						       actor->IsHostileToActor(a_player) ||
						       a_player->IsHostileToActor(actor);
					}
					if (drop) {
						if (registry.IsRegistered(autoId)) {
							registry.UnregisterSummon(autoId, "SCF_AutoRegister ended");
							persistence::OnSummonRemoved(autoId);
						}
						it = s_autoRegistered.erase(it);
					} else {
						++it;
					}
				}
			}

			// 1b. Explicitly registered summons are absent from the engine roster: the
			//     script (or SCF_AutoRegister) put them in the registry, so the registry is
			//     where the tick finds them. They are driven like any other summon (orders,
			//     movement, combat policy, panel row) while the actor is alive and shares
			//     the player's loaded space; step 2b releases the ones that are gone.
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
				const auto profile = keywords::ProfileOf(actor, nullptr);
				if (profile.Has(core::kKwUnmanaged)) {
					registry.UnregisterSummon(summonId, "SCF_Unmanaged");
					s_autoRegistered.erase(summonId);
					persistence::OnSummonRemoved(summonId);
					continue;
				}
				liveActors[summonId] = actor;
				profiles[summonId] = profile;
				sourceEffects[summonId] = nullptr;
				resolveRadius(*actor, nullptr, profile);
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
					s_autoRegistered.erase(summonId);
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

			// Apply one-time keyword default aggression modes (SCF_DefaultPassive /
			// SCF_DefaultDefensive / SCF_DefaultAggressive / SCF_NonCombat) on first sight.
			for (const auto& [id, actor] : orderedSummons) {
				if (s_defaultsApplied.insert(id).second) {
					if (auto* model = registry.GetModel(id)) {
						const auto profIt = profiles.find(id);
						if (profIt != profiles.end()) {
							if (const auto defaultMode = core::DefaultAggressionFromKeywords(profIt->second)) {
								model->aggression = *defaultMode;
								logger::info("[KeywordApi] Summon 0x{:08X} initial aggression set to {} by keyword.",
									id, core::ToString(*defaultMode));
							}
						}
					}
				}
			}
			std::erase_if(s_defaultsApplied, [&](core::EntityID a_id) { return !registry.IsRegistered(a_id); });

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
					const auto profIt = profiles.find(sid);
					const core::SummonKeywordProfile* profile = profIt != profiles.end() ? &profIt->second : nullptr;
					const auto srcIt = sourceEffects.find(sid);
					const RE::EffectSetting* sourceEffect = srcIt != sourceEffects.end() ? srcIt->second : nullptr;

					if (profile && !core::ShouldAcceptCommand(*profile, cmd.type, cmd.order, cmd.priority, cmd.aggression)) {
						logger::debug("[KeywordApi] Summon 0x{:08X} rejected command {} (order {}) due to keywords.",
							sid, core::ToString(cmd.type), core::ToString(cmd.order));
						continue;
					}

					if (model) {
						const auto tagged = taggedSummons.find(sid);
						// Assist is a Tactical focus command. SCR chooses from the same
						// in-zone candidates each tick and never replaces a legal current target.
						if (cmd.priority == core::CommandPriority::Tactical &&
						    cmd.type == core::CommandType::SetFocusTarget &&
						    tagged != taggedSummons.end() &&
						    core::UsesCombatRadius(radius::Settings(), *model, tagged->second)) {
							continue;
						}
						cmdQueue.ApplyToModel(cmd, *model, &ctx.GetStateMachine());
					}

					ExecuteCommandOnActor(a_player, summonActor, model, cmd, profile, sourceEffect);
				}
				if (cmd.type == core::CommandType::CommandAction) {
					actions::ReportBatch();
				}
			}
			actions::Tick(a_timestamp);

			// 5. Per-tick order execution and continuous AI enforcement
			const auto& settings = ctx.GetSettings().Get();

			const bool radiusCollision = !orderedSummons.empty() && radius::UsesCollisionBypass(!taggedSummons.empty());
			const bool scanRadiusEnemies = std::ranges::any_of(taggedSummons, [&](const auto& entry) {
				const auto* model = registry.GetModel(entry.first);
				return model && core::UsesCombatRadius(radius::Settings(), *model, entry.second);
			});
			if (!worldScanned && (radiusCollision || scanRadiusEnemies)) {
				world = keywords::ScanWorld(a_player, true);
				worldScanned = true;
			}

			// Merge conjuration source-MGEF threat flags onto world snapshot so threat keywords
			// placed on a conjuration MGEF work identically to keywords on NPC_/RACE/active MGEF.
			if (!profiles.empty()) {
				std::unordered_map<core::EntityID, std::size_t> worldIndex;
				worldIndex.reserve(world.size());
				for (std::size_t idx = 0; idx < world.size(); ++idx) {
					worldIndex.emplace(world[idx].id, idx);
				}
				for (const auto& [id, prof] : profiles) {
					const auto threatBits = prof.flags & core::kKwThreatMask;
					if (threatBits == core::kKwNone) {
						continue;
					}
					if (const auto wit = worldIndex.find(id); wit != worldIndex.end()) {
						world[wit->second].flags |= threatBits;
					} else if (const auto ait = liveActors.find(id); ait != liveActors.end() && ait->second) {
						worldIndex.emplace(id, world.size());
						world.push_back({ ait->second, id, ownerId, threatBits });
					}
				}
			}

			// Publish thread-safe FriendlyFire keyword overrides (world + summon source-MGEF).
			{
				std::unordered_map<core::EntityID, core::KeywordFlags> ffFlags;
				for (const auto& wa : world) {
					if ((wa.flags & core::kKwFriendlyFireMask) != 0) {
						ffFlags[wa.id] |= (wa.flags & core::kKwFriendlyFireMask);
					}
				}
				for (const auto& [id, prof] : profiles) {
					if ((prof.flags & core::kKwFriendlyFireMask) != 0) {
						ffFlags[id] |= (prof.flags & core::kKwFriendlyFireMask);
					}
				}
				keywords::PublishFriendlyFireFlags(ffFlags);
			}

			radius::PublishEnemies(*a_player, scanRadiusEnemies ? std::span(world) : std::span<const keywords::WorldActor>{});

			// Pass-through table for the movement-avoidance and collision-filter seats:
			// the owner and its live summons (system groups read from their controllers).
			{
				std::vector<passthrough::Member> members;
				members.reserve(orderedSummons.size() + 1);
				members.push_back({ core::PassThroughParty{ ownerId, core::InvalidEntityID }, a_player });
				for (const auto& [id, actor] : orderedSummons) {
					core::PassThroughParty party{ id, ownerId };
					if (radiusCollision) {
						party.flags |= core::kPassThroughAll;
					}
					if (const auto profIt = profiles.find(id); profIt != profiles.end()) {
						party.flags |= static_cast<std::uint8_t>(profIt->second.flags & core::kKwPassThroughMask);
					}
					members.push_back({ std::move(party), actor });
				}
				passthrough::Publish(members, world, settings.collision);
				combat::PublishUntargetable(world);
			}

			// PoE2-style aggression radius around the owner for engine-chosen fights.
			{
				const auto& ownerPosition = a_player->GetPosition();
				combat::PublishAggressionArea({ ownerPosition.x, ownerPosition.y, ownerPosition.z }, settings.combat.aggressionRadius);
			}

			// Stealth state for SCF_HoldFireInStealth.
			const bool playerSneaking = a_player->IsSneaking();
			bool playerDetected = a_player->IsInCombat();
			if (playerSneaking && !playerDetected) {
				const bool anyHoldFire = std::ranges::any_of(profiles, [](const auto& a_entry) {
					return (a_entry.second.flags & core::kKwHoldFireInStealth) != 0;
				});
				if (anyHoldFire) {
					const auto isHostileTargetingUs = [&](RE::Actor* a_actor) -> bool {
						if (!a_actor || a_actor == a_player || !a_actor->IsInCombat() || !a_actor->IsHostileToActor(a_player)) {
							return false;
						}
						if (const auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get()) {
							const auto tid = target->GetFormID();
							return tid == ownerId || drivenSummons.contains(tid);
						}
						return false;
					};
					if (worldScanned) {
						for (const auto& wa : world) {
							if (isHostileTargetingUs(wa.actor)) {
								playerDetected = true;
								break;
							}
						}
					} else if (auto* lists = RE::ProcessLists::GetSingleton()) {
						for (const auto& handle : lists->highActorHandles) {
							if (const auto actor = handle.get(); isHostileTargetingUs(actor.get())) {
								playerDetected = true;
								break;
							}
						}
					}
				}
			}

			// Precompute per-sector formation counts (Default / Front / Flank / Back).
			std::array<std::size_t, 4> sectorTotals{};
			std::array<std::size_t, 4> sectorIndices{};
			for (const auto& [id, _] : orderedSummons) {
				const auto profIt = profiles.find(id);
				const auto sector = profIt != profiles.end()
					? core::FormationSectorFromKeywords(profIt->second)
					: core::FormationSector::Default;
				++sectorTotals[static_cast<std::size_t>(sector)];
			}

			for (std::size_t i = 0; i < orderedSummons.size(); ++i) {
				const auto summonId = orderedSummons[i].first;
				auto* summonActor = orderedSummons[i].second;
				auto* model = registry.GetModel(summonId);
				if (!model || !summonActor) {
					continue;
				}
				const auto profIt = profiles.find(summonId);
				const core::SummonKeywordProfile profile = profIt != profiles.end() ? profIt->second : core::SummonKeywordProfile{};

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

				const bool isExplicitAttack = (model->order == core::SummonOrder::Attack && model->currentTargetId != core::InvalidEntityID);
				combat::SummonCombatDirectives directives{};
				directives.alwaysNoCircling = profile.Has(core::kKwNoCircling);
				directives.ignoreAggressionRadius = profile.Has(core::kKwIgnoreCombatRadius);
				directives.specialtyFlags = profile.flags & core::kKwTargetingSpecialtyMask;
				directives.ownerId = ownerId;

				const bool keywordBlocksCombat =
					profile.Has(core::kKwNonCombat) ||
					(profile.Has(core::kKwHoldFireInStealth) && playerSneaking && !playerDetected && !isExplicitAttack);

				// Combat policy: may this summon fight at all this tick, and against whom?
				// The gate denies StartCombat for managed summons, sets the native
				// attack/cast deny bits and breaks an existing fight when forbidden.
				const auto tagged = taggedSummons.find(summonId);
				auto policy = core::BuildCombatPolicy(*model, model->currentTargetId != core::InvalidEntityID);
				if (tagged != taggedSummons.end() && !keywordBlocksCombat) {
					policy = radius::Update(*summonActor, *model, tagged->second, a_timestamp, directives);
				} else {
					policy = core::ApplyKeywordCombatPolicy(policy, profile, playerSneaking, playerDetected, isExplicitAttack);
					combat::UpdateForSummon(summonActor, policy, directives);
				}

				const auto effectiveSettings = core::ApplyKeywordSettingsOverride(settings, profile);
				const auto sector = core::FormationSectorFromKeywords(profile);
				const auto sectorIdx = static_cast<std::size_t>(sector);
				const std::size_t slotIndex = sectorIndices[sectorIdx]++;
				const std::size_t slotTotal = sectorTotals[sectorIdx];
				const bool stationary = profile.Has(core::kKwStationary);
				const bool allowCatchUp = !policy.radiusManaged && !profile.Has(core::kKwNoCatchUpWarp) && !stationary;

				// Movement execution for the current order (follow / return / move-to /
				// guard / attack approach). It also flips Return back to Follow on arrival.
				movement::UpdateForSummon(
					a_player,
					summonActor,
					*model,
					ctx.GetStateMachine(),
					effectiveSettings,
					slotIndex,
					slotTotal,
					a_timestamp,
					policy.allowCombat,
					allowCatchUp,
					sector,
					stationary);

				// Combat Leash enforcement (explicit Attack orders & stationary turrets are exempt).
				if (!policy.radiusManaged && !stationary &&
				    core::ShouldEnforceCombatLeash(*model, effectiveSettings.combat.enableCombatLeash, policy.allowCombat, summonActor->IsInCombat())) {
					const float distToPlayerSq = summonActor->GetPosition().GetSquaredDistance(a_player->GetPosition());
					const float leashRadius = effectiveSettings.combat.combatLeashRadius;
					if (distToPlayerSq > leashRadius * leashRadius) {
						if (effectiveSettings.combat.returnToOwnerWhenOutsideLeash) {
							logger::info("[UpdateHook] Summon 0x{:08X} outside combat leash radius ({:.1f} > {:.1f}), returning to owner",
								summonId, std::sqrt(distToPlayerSq), leashRadius);
							model->order = core::SummonOrder::Return;
							model->currentTargetId = core::InvalidEntityID;
							model->hasMoveTarget = false;
							ctx.GetStateMachine().TransitionTo(*model, core::SummonState::Return, "Exceeded combat leash");
							PublishPolicyAndBreakCombat(summonActor, model, true, &profile, ownerId);
						}
					}
				}
			}

			// Drop bookkeeping for summons that stopped being driven (dispel, death,
			// cell detach, displacement) so a recycled handle cannot inherit it.
			radius::FinishTick(taggedSummons);
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
					const auto srcIt = sourceEffects.find(id);
					const RE::EffectSetting* sourceEffect = srcIt != sourceEffects.end() ? srcIt->second : nullptr;
					if (actions::HasCommand(actor, sourceEffect)) {
						cooldowns.push_back({ id, actions::CooldownRemaining(id, a_timestamp), 0.0f });
					}
				}
				integrations::MinionPanel::PublishSnapshot(ownerId, a_timestamp, cooldowns);
			}

			// Stage 8: TrueHUD info bar on the FocusTarget. currentTargetId also holds targets
			// the AI picked itself, so only an Attack order (FocusTarget) counts as a focus.
			{
				core::EntityID focusId = core::InvalidEntityID;
				for (const auto& [id, actor] : orderedSummons) {
					const auto* model = registry.GetModel(id);
					if (model && model->order == core::SummonOrder::Attack && model->currentTargetId != core::InvalidEntityID) {
						focusId = model->currentTargetId;
						break;
					}
				}
				integrations::TrueHUDBackend::Sync(focusId);
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

				radius::ConsumeMenu();
				auto* ui = RE::UI::GetSingleton();
				if (ui && ui->GameIsPaused()) {
					return;
				}

				s_totalElapsed += a_delta;

				auto& ctx = core::FrameworkContext::GetSingleton();
				const bool enabled = ctx.GetSettings().Get().general.enabled;
				wolf::SetFrameworkEnabled(enabled);
				static bool wasEnabled = false;
				if (!enabled) {
					if (std::exchange(wasEnabled, false)) {
						attack_trace::Reset("framework_disabled");
						radius::Reset();
						combat::ClearRadiusPolicies();
						keywords::Reset();
						summonlimit::PublishFreeSlots(0);
						passthrough::Publish({}, {}, ctx.GetSettings().Get().collision);
					}
					return;
				}
				wasEnabled = true;

				attack_trace::Tick(ctx.GetRegistry());
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
		s_autoRegistered.clear();
		s_defaultsApplied.clear();
		attack_trace::Reset("session_reset");
		movement::ResetAll();
		radius::Reset();
		combat::ResetAll();
		keywords::Reset();
		summonlimit::PublishFreeSlots(0);
		passthrough::ResetAll();
		actions::Reset();
		persistence::Reset();
		integrations::MinionPanel::ClearSnapshot();
		integrations::TrueHUDBackend::ClearFocusTarget();
		core::FrameworkContext::GetSingleton().ResetRuntimeState();
	}
}
