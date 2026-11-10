#include "Core/MovementIntent.h"
#include "Core/Formation.h"

#include <algorithm>
#include <cmath>

namespace scf::core
{
	namespace
	{
		// Direction of travel, relative to the owner's heading, for a goal world point.
		// Zero while the actor is effectively standing on the goal (it then faces the
		// owner's heading, which is the formation look).
		float TravelFacingDegrees(const Vec3& a_from, const Vec3& a_to, float a_ownerYawRadians) noexcept
		{
			const float dx = a_to.x - a_from.x;
			const float dy = a_to.y - a_from.y;
			if (dx * dx + dy * dy < kFacingMinDistance * kFacingMinDistance) {
				return 0.0f;
			}
			return FacingOffsetDegrees(a_from, a_to, a_ownerYawRadians);
		}
	}

	MovementIntent BuildMovementIntent(
		const SummonModel& a_model,
		bool a_inCombat,
		bool a_combatPermitted,
		const SettingsData& a_settings,
		const Vec3& a_ownerPosition,
		const Vec3& a_summonPosition,
		float a_ownerYawRadians,
		std::size_t a_slotIndex,
		std::size_t a_squadSize) noexcept
	{
		const auto& movement = a_settings.movement;

		// 1. Attack (explicit FocusTarget): the player asked for a kill. It outranks
		//    every movement order, including a parked guard, so combat AI drives.
		//    When the CombatPolicy forbids combat (Passive), the order falls through to
		//    the follow rules instead: a passive summon keeps following and never charges.
		if (a_model.order == SummonOrder::Attack && a_combatPermitted) {
			return MovementIntent{};
		}

		// 2. Guard / wait: hold the current position, regardless of combat.
		if (a_model.order == SummonOrder::WaitGuard) {
			MovementIntent intent{};
			intent.mode = MovementMode::HoldPosition;
			intent.lockTranslation = true;
			return intent;
		}

		// 3. Return: run back to the owner, then the order flips to Follow.
		if (a_model.order == SummonOrder::Return) {
			MovementIntent intent{};
			const float followRadius = std::max(movement.followMinDistance, 150.0f);
			const float catchUpRadius = std::max(movement.followMaxDistance, followRadius * 2.0f);
			intent.mode = MovementMode::ReturnToOwner;
			intent.followRadius = followRadius;
			intent.catchUpRadius = catchUpRadius;
			intent.facingOffsetDegrees = TravelFacingDegrees(a_summonPosition, a_ownerPosition, a_ownerYawRadians);
			return intent;
		}

		// 4. MoveTo: the destination is a world point, but the primitive consumes an
		//    owner body-space offset, so the offset is recomputed from where the owner
		//    is standing right now. Recomputing every tick keeps the world destination
		//    fixed while the owner moves. A destination left over from another cell or
		//    a teleport (unreachable by walking) is ignored.
		if (a_model.order == SummonOrder::MoveTo && a_model.hasMoveTarget) {
			const float dx = a_model.moveTargetPosition.x - a_ownerPosition.x;
			const float dy = a_model.moveTargetPosition.y - a_ownerPosition.y;
			if (dx * dx + dy * dy <= kMaxMoveToDistance * kMaxMoveToDistance) {
				MovementIntent intent{};
				intent.mode = MovementMode::MoveToPoint;
				intent.localOffset = WorldPointToOwnerLocal(a_model.moveTargetPosition, a_ownerPosition, a_ownerYawRadians);
				intent.followRadius = kMoveToArrivalRadius;
				intent.catchUpRadius = std::max(movement.followMaxDistance, kMoveToArrivalRadius * 4.0f);
				intent.facingOffsetDegrees = TravelFacingDegrees(a_summonPosition, a_model.moveTargetPosition, a_ownerYawRadians);
				return intent;
			}
		}

		// 5. Follow: an engaged summon fights; the engine owns its locomotion then.
		//    Only the engine combat flag is authoritative here: a stale model target
		//    (e.g. a focus command whose enemy disengaged) must not freeze the order.
		//    When the CombatPolicy forbids combat (Passive / Return / MoveTo) the engine
		//    may still report combat for a moment; the formation must win anyway.
		if (a_inCombat && a_combatPermitted) {
			return MovementIntent{};
		}

		MovementIntent intent{};
		intent.mode = MovementMode::FollowSlot;
		intent.localOffset = FormationSlotOffsetLocal(a_slotIndex, a_squadSize, movement.followMinDistance);
		intent.followRadius = std::max(kMinFollowRadius, movement.followMinDistance * 0.5f);
		intent.catchUpRadius = std::max(movement.followMaxDistance, intent.followRadius * 2.0f);
		intent.facingOffsetDegrees = TravelFacingDegrees(
			a_summonPosition,
			OwnerLocalToWorldPoint(intent.localOffset, a_ownerPosition, a_ownerYawRadians),
			a_ownerYawRadians);
		return intent;
	}

	bool ReleasesFollowLeash(const SummonModel& a_model) noexcept
	{
		switch (a_model.order) {
		case SummonOrder::MoveTo:
			return a_model.hasMoveTarget;
		case SummonOrder::Attack:
		case SummonOrder::WaitGuard:
			return true;
		default:
			return false;
		}
	}

	bool IsWithinArrivalRadius(const Vec3& a_actorPosition, const Vec3& a_destination, float a_radius) noexcept
	{
		const float dx = a_actorPosition.x - a_destination.x;
		const float dy = a_actorPosition.y - a_destination.y;
		return (dx * dx + dy * dy) <= (a_radius * a_radius);
	}
}
