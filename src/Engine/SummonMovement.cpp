#include "Engine/SummonMovement.h"
#include "Core/Formation.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/N/NiPoint3.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectREFR.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace scf::engine::movement
{
	namespace
	{
		// ------------------------------------------------------------------
		// Engine primitives.
		//
		// None of these three functions is declared by the CommonLibSSE-NG revision
		// this project builds against, so they are resolved through the Address
		// Library here. Ids and signature were checked against the unpacked 1.5.97
		// image and cross-checked with APMF's verified address table
		// (APMF/Docs/ADDRESS-TABLE-2026-09-15.md, rows MovementDeny.SetDontMove /
		// KeepOffsetFromActor / ClearKeepOffsetFromActor):
		//
		//   * id 36870 -> SkyrimSE.exe+603990. The function loads
		//     Actor::movementController (Actor+0x148, `BSTSmartPointer
		//     <MovementControllerNPC>`), RTTI-casts it to
		//     IMovementSetKeepOffsetFromActor (the type descriptor is present in the
		//     image) and calls that interface's slot 1 with the nine-field record
		//     {ActorHandle target, NiPoint3 offset, NiPoint3 angle, float catchUpRadius,
		//     float followRadius}. That record is exactly Papyrus
		//     Actor.KeepOffsetFromActor's argument list.
		//   * id 36871 -> SkyrimSE.exe+603AC0. Same interface, slot 0: clear.
		//   * id 36490 -> SkyrimSE.exe+5E3330. Actor::SetDontMove(bool).
		//
		// Offset semantics (CK wiki / Papyrus Index for KeepOffsetFromActor): the
		// offsets are in the TARGET's body space (+X right, +Y forward, +Z up);
		// afCatchUpRadius is the radius beyond which the actor runs to catch up, and
		// afFollowRadius is the radius inside which it will not move.
		// ------------------------------------------------------------------
		namespace native
		{
			void SetDontMove(RE::Actor* a_actor, bool a_dontMove)
			{
				using func_t = void (*)(RE::Actor*, bool);
				static REL::Relocation<func_t> func{ REL::RelocationID(36490, 37489) };
				func(a_actor, a_dontMove);
			}

			void KeepOffsetFromActor(
				RE::Actor* a_actor,
				const RE::ActorHandle& a_target,
				const RE::NiPoint3& a_offset,
				const RE::NiPoint3& a_angle,
				float a_catchUpRadius,
				float a_followRadius)
			{
				using func_t = void (*)(RE::Actor*, const RE::ActorHandle&, const RE::NiPoint3&, const RE::NiPoint3&, float, float);
				static REL::Relocation<func_t> func{ REL::RelocationID(36870, 37894) };
				func(a_actor, a_target, a_offset, a_angle, a_catchUpRadius, a_followRadius);
			}

			void ClearKeepOffsetFromActor(RE::Actor* a_actor)
			{
				using func_t = void (*)(RE::Actor*);
				static REL::Relocation<func_t> func{ REL::RelocationID(36871, 37895) };
				func(a_actor);
			}
		}

		// ------------------------------------------------------------------
		// Tunables. All are documented behaviour constants, not user settings:
		//   * a primitive is re-issued if the desired goal moved by more than
		//     kOffsetEpsilon, and re-asserted every kReassertSeconds so a movement
		//     goal the engine rewrote on its own is taken back (the vanilla
		//     summoned-creature follow may rewrite it).
		//   * Return gives up on walking and warps only when the summon is beyond
		//     Recovery.fCatchUpDistance or made no progress for kStuckSeconds.
		//   * MoveTo cancels (guard in place) after kMoveToTimeoutSeconds.
		// ------------------------------------------------------------------
		constexpr float kOffsetEpsilon = 8.0f;
		constexpr float kOffsetEpsilonSq = kOffsetEpsilon * kOffsetEpsilon;
		constexpr float kReassertSeconds = 2.0f;
		constexpr float kStuckSeconds = 3.0f;
		constexpr float kMoveToTimeoutSeconds = 45.0f;
		constexpr float kProgressEpsilon = 50.0f;

		// Radius the position of a "parked" summon is held inside (APMF's proven
		// full stand-still values: catchUp 20, follow 10).
		constexpr float kParkCatchUpRadius = 20.0f;
		constexpr float kParkFollowRadius = 10.0f;

		struct AppliedMovement
		{
			core::MovementMode mode{ core::MovementMode::None };
			core::Vec3 localOffset{};
			bool keepOffsetActive{ false };
			bool keepOffsetOnOwner{ true };
			bool translationLocked{ false };
			// SetDontMove is persistent on the actor and survives a save/load, so the
			// first tick after (re)discovery must push the desired value even when it
			// happens to equal the default `false`.
			bool translationKnown{ false };
			float modeEnteredAt{ 0.0f };
			float lastIssuedAt{ 0.0f };
			float bestDistance{ std::numeric_limits<float>::max() };
			float lastProgressAt{ 0.0f };
			bool catchUpWarped{ false };
		};

		std::unordered_map<core::EntityID, AppliedMovement> s_applied;

		core::Vec3 ToCore(const RE::NiPoint3& a_point) noexcept
		{
			return { a_point.x, a_point.y, a_point.z };
		}

		float HorizontalDistanceSq(const core::Vec3& a_lhs, const core::Vec3& a_rhs) noexcept
		{
			const float dx = a_lhs.x - a_rhs.x;
			const float dy = a_lhs.y - a_rhs.y;
			return dx * dx + dy * dy;
		}

		void ApplyTranslationLock(RE::Actor* a_summon, core::EntityID a_id, bool a_locked, AppliedMovement& a_state)
		{
			if (a_state.translationKnown && a_state.translationLocked == a_locked) {
				return;
			}

			native::SetDontMove(a_summon, a_locked);
			a_state.translationLocked = a_locked;
			a_state.translationKnown = true;
			logger::info("[Movement] Summon 0x{:08X} SetDontMove({})", a_id, a_locked);
		}

		void ClearKeepOffset(RE::Actor* a_summon, core::EntityID a_id, AppliedMovement& a_state)
		{
			if (!a_state.keepOffsetActive) {
				return;
			}

			native::ClearKeepOffsetFromActor(a_summon);
			a_state.keepOffsetActive = false;
			logger::info("[Movement] Summon 0x{:08X} keep-offset cleared ({})", a_id, core::ToString(a_state.mode));
		}

		void ApplyKeepOffset(
			RE::Actor* a_summon,
			const RE::ActorHandle& a_target,
			bool a_targetIsOwner,
			core::EntityID a_id,
			const core::MovementIntent& a_intent,
			AppliedMovement& a_state,
			float a_now)
		{
			const bool sameGoal = a_state.keepOffsetActive &&
			                      a_state.keepOffsetOnOwner == a_targetIsOwner &&
			                      a_state.localOffset.DistanceSquaredTo(a_intent.localOffset) <= kOffsetEpsilonSq;
			const bool reassert = a_state.keepOffsetActive && (a_now - a_state.lastIssuedAt) >= kReassertSeconds;

			if (sameGoal && !reassert) {
				return;
			}

			const RE::NiPoint3 offset{ a_intent.localOffset.x, a_intent.localOffset.y, a_intent.localOffset.z };
			const RE::NiPoint3 angles{ 0.0f, 0.0f, 0.0f };
			native::KeepOffsetFromActor(a_summon, a_target, offset, angles, a_intent.catchUpRadius, a_intent.followRadius);

			a_state.keepOffsetActive = true;
			a_state.keepOffsetOnOwner = a_targetIsOwner;
			a_state.localOffset = a_intent.localOffset;
			a_state.lastIssuedAt = a_now;

			if (!sameGoal) {
				logger::info("[Movement] Summon 0x{:08X} {} keep-offset ({:.0f}, {:.0f}, {:.0f}) follow={:.0f} catchUp={:.0f} target={}",
					a_id,
					core::ToString(a_intent.mode),
					a_intent.localOffset.x,
					a_intent.localOffset.y,
					a_intent.localOffset.z,
					a_intent.followRadius,
					a_intent.catchUpRadius,
					a_targetIsOwner ? "owner" : "self");
			} else {
				logger::debug("[Movement] Summon 0x{:08X} keep-offset re-asserted ({})", a_id, core::ToString(a_intent.mode));
			}
		}

		void TrackProgress(AppliedMovement& a_state, float a_distance, float a_now)
		{
			if (a_distance < a_state.bestDistance - kProgressEpsilon) {
				a_state.bestDistance = a_distance;
				a_state.lastProgressAt = a_now;
			}
		}

		bool WarpBehindOwner(RE::Actor* a_summon, RE::Actor* a_owner, float a_distance)
		{
			// A cross-cell recall cannot be done with a position write; the engine's own
			// summoned-creature snap keeps that case (fSummonedCreatureMaxFollowDist).
			if (!a_owner->GetParentCell() || a_summon->GetParentCell() != a_owner->GetParentCell()) {
				return false;
			}

			const float yaw = a_owner->GetAngleZ();
			const auto ownerPosition = a_owner->GetPosition();
			const RE::NiPoint3 destination{
				ownerPosition.x - std::sin(yaw) * a_distance,
				ownerPosition.y - std::cos(yaw) * a_distance,
				ownerPosition.z
			};

			a_summon->SetPosition(destination, true);
			a_summon->MoveToNearestNavmesh(0.0f);
			return true;
		}

		void TryCatchUpWarp(
			RE::Actor* a_summon,
			RE::Actor* a_owner,
			core::EntityID a_id,
			float a_distanceToOwner,
			const core::SettingsData& a_settings,
			AppliedMovement& a_state,
			float a_now)
		{
			if (!a_settings.recovery.enableCatchUpWarp || a_state.catchUpWarped) {
				return;
			}

			const bool tooFar = a_distanceToOwner >= a_settings.recovery.catchUpDistance;
			const bool stuck = (a_now - a_state.lastProgressAt) >= kStuckSeconds &&
			                   a_distanceToOwner > a_settings.movement.followMinDistance * 1.5f;
			if (!tooFar && !stuck) {
				return;
			}

			a_state.catchUpWarped = true;
			a_state.lastProgressAt = a_now;
			a_state.bestDistance = std::numeric_limits<float>::max();

			if (!WarpBehindOwner(a_summon, a_owner, a_settings.movement.followMinDistance)) {
				logger::warn("[Movement] Summon 0x{:08X} catch-up warp skipped: summon is not in the owner's cell (dist {:.0f})",
					a_id, a_distanceToOwner);
				return;
			}

			logger::info("[Movement] Summon 0x{:08X} catch-up warp applied ({}, dist was {:.0f}, threshold {:.0f})",
				a_id,
				tooFar ? "beyond catch-up distance" : "no walking progress",
				a_distanceToOwner,
				a_settings.recovery.catchUpDistance);
		}
	}

	void UpdateForSummon(
		RE::PlayerCharacter* a_owner,
		RE::Actor* a_summon,
		core::SummonModel& a_model,
		core::StateMachine& a_fsm,
		const core::SettingsData& a_settings,
		std::size_t a_slotIndex,
		std::size_t a_squadSize,
		float a_now)
	{
		if (!a_owner || !a_summon) {
			return;
		}

		const auto id = static_cast<core::EntityID>(a_summon->GetFormID());
		auto& state = s_applied[id];

		if (a_summon->IsDead()) {
			ApplyTranslationLock(a_summon, id, false, state);
			ClearKeepOffset(a_summon, id, state);
			return;
		}

		// A summon that is not in the owner's cell has a suspended process; issuing
		// movement primitives against it would address an unloaded reference.
		if (a_summon->GetParentCell() != a_owner->GetParentCell()) {
			return;
		}

		// A focus target that vanished must not keep the summon locked in "combat"
		// mode forever: that would suppress the follow order silently.
		if (a_model.currentTargetId != core::InvalidEntityID) {
			auto* focus = RE::TESForm::LookupByID<RE::Actor>(a_model.currentTargetId);
			if (!focus || focus->IsDead() || focus->GetParentCell() != a_summon->GetParentCell()) {
				logger::info("[Movement] Summon 0x{:08X} focus target 0x{:08X} is gone; resuming order",
					id, a_model.currentTargetId);
				a_model.currentTargetId = core::InvalidEntityID;
			}
		}

		const core::Vec3 summonPosition = ToCore(a_summon->GetPosition());
		const core::Vec3 ownerPosition = ToCore(a_owner->GetPosition());
		const float distanceToOwner = std::sqrt(HorizontalDistanceSq(summonPosition, ownerPosition));

		// 1. Order transitions that need engine distances.
		if (a_model.order == core::SummonOrder::Return) {
			TrackProgress(state, distanceToOwner, a_now);

			if (distanceToOwner <= a_settings.movement.followMinDistance) {
				a_model.order = core::SummonOrder::Follow;
				a_fsm.TransitionTo(a_model, core::SummonState::FollowOwner, "Returned to owner range");
				logger::info("[Movement] Summon 0x{:08X} reached owner ({:.0f}) -> Follow", id, distanceToOwner);
			} else {
				TryCatchUpWarp(a_summon, a_owner, id, distanceToOwner, a_settings, state, a_now);
			}
		} else if (a_model.order == core::SummonOrder::MoveTo && a_model.hasMoveTarget) {
			if (core::IsWithinArrivalRadius(summonPosition, a_model.moveTargetPosition)) {
				a_model.order = core::SummonOrder::WaitGuard;
				a_model.hasMoveTarget = false;
				a_fsm.TransitionTo(a_model, core::SummonState::Guard, "Arrived at move point");
				logger::info("[Movement] Summon 0x{:08X} arrived at MoveTo point ({:.0f}, {:.0f}, {:.0f}) -> Guard",
					id, a_model.moveTargetPosition.x, a_model.moveTargetPosition.y, a_model.moveTargetPosition.z);
			} else if (state.mode == core::MovementMode::MoveToPoint && (a_now - state.modeEnteredAt) >= kMoveToTimeoutSeconds) {
				a_model.order = core::SummonOrder::WaitGuard;
				a_model.hasMoveTarget = false;
				a_fsm.TransitionTo(a_model, core::SummonState::Guard, "MoveTo timed out");
				logger::warn("[Movement] Summon 0x{:08X} MoveTo point not reached in {:.0f}s -> Guard in place",
					id, kMoveToTimeoutSeconds);
			}
		}

		// 2. Translate the order into a movement intent.
		const auto intent = core::BuildMovementIntent(
			a_model,
			a_summon->IsInCombat(),
			a_settings,
			ownerPosition,
			a_owner->GetAngleZ(),
			a_slotIndex,
			a_squadSize);

		if (intent.mode != state.mode) {
			state.modeEnteredAt = a_now;
			state.bestDistance = std::numeric_limits<float>::max();
			state.lastProgressAt = a_now;
			state.catchUpWarped = false;
		}

		// 3. Apply it.
		switch (intent.mode) {
		case core::MovementMode::None:
			ApplyTranslationLock(a_summon, id, false, state);
			ClearKeepOffset(a_summon, id, state);
			break;

		case core::MovementMode::HoldPosition:
			{
				// Park the movement goal on the summon itself so the planner produces no
				// locomotion, and lock translation: a clean stand-still (APMF's proven
				// full movement block), while attacks still resolve in place.
				core::MovementIntent park = intent;
				park.followRadius = kParkFollowRadius;
				park.catchUpRadius = kParkCatchUpRadius;

				ApplyTranslationLock(a_summon, id, true, state);
				ApplyKeepOffset(a_summon, a_summon->GetHandle(), false, id, park, state, a_now);
			}
			break;

		default:
			ApplyTranslationLock(a_summon, id, false, state);
			ApplyKeepOffset(a_summon, a_owner->GetHandle(), true, id, intent, state, a_now);
			break;
		}

		state.mode = intent.mode;
	}

	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons)
	{
		std::erase_if(s_applied, [&a_liveSummons](const auto& a_entry) {
			return a_liveSummons.find(a_entry.first) == a_liveSummons.end();
		});
	}

	void ResetAll()
	{
		s_applied.clear();
		logger::info("[Movement] Movement state reset.");
	}
}
