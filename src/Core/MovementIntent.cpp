#include "Core/MovementIntent.h"
#include "Core/Formation.h"

#include <algorithm>

namespace scf::core
{
	MovementIntent BuildMovementIntent(
		const SummonModel& a_model,
		bool a_inCombat,
		const SettingsData& a_settings,
		const Vec3& a_ownerPosition,
		float a_ownerYawRadians,
		std::size_t a_slotIndex,
		std::size_t a_squadSize) noexcept
	{
		const auto& movement = a_settings.movement;

		// 1. Guard / wait: hold the current position, regardless of combat.
		if (a_model.order == SummonOrder::WaitGuard) {
			MovementIntent intent{};
			intent.mode = MovementMode::HoldPosition;
			intent.lockTranslation = true;
			return intent;
		}

		// 2. Return: run back to the owner, then the order flips to Follow.
		if (a_model.order == SummonOrder::Return) {
			MovementIntent intent{};
			const float followRadius = std::max(movement.followMinDistance, 150.0f);
			const float catchUpRadius = std::max(movement.followMaxDistance, followRadius * 2.0f);
			intent.mode = MovementMode::ReturnToOwner;
			intent.followRadius = followRadius;
			intent.catchUpRadius = catchUpRadius;
			return intent;
		}

		// 3. MoveTo: the destination is a world point, but the primitive consumes an
		//    owner body-space offset, so the offset is recomputed from where the owner
		//    is standing right now. Recomputing every tick keeps the world destination
		//    fixed while the owner moves.
		if (a_model.order == SummonOrder::MoveTo && a_model.hasMoveTarget) {
			MovementIntent intent{};
			intent.mode = MovementMode::MoveToPoint;
			intent.localOffset = WorldPointToOwnerLocal(a_model.moveTargetPosition, a_ownerPosition, a_ownerYawRadians);
			intent.followRadius = kMoveToArrivalRadius;
			intent.catchUpRadius = std::max(movement.followMaxDistance, kMoveToArrivalRadius * 4.0f);
			return intent;
		}

		// 4. Follow: an engaged summon fights; the engine owns its locomotion then.
		//    Only the engine combat flag is authoritative here: a stale model target
		//    (e.g. a focus command whose enemy disengaged) must not freeze the order.
		if (a_inCombat) {
			return MovementIntent{};
		}

		MovementIntent intent{};
		intent.mode = MovementMode::FollowSlot;
		intent.localOffset = FormationSlotOffsetLocal(a_slotIndex, a_squadSize, movement.followMinDistance);
		intent.followRadius = std::max(kMinFollowRadius, movement.followMinDistance * 0.5f);
		intent.catchUpRadius = std::max(movement.followMaxDistance, intent.followRadius * 2.0f);
		return intent;
	}

	bool IsWithinArrivalRadius(const Vec3& a_actorPosition, const Vec3& a_destination, float a_radius) noexcept
	{
		const float dx = a_actorPosition.x - a_destination.x;
		const float dy = a_actorPosition.y - a_destination.y;
		return (dx * dx + dy * dy) <= (a_radius * a_radius);
	}
}
