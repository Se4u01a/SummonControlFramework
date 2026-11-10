#pragma once

#include "Core/Settings.h"
#include "Core/SummonModel.h"
#include "Core/Types.h"

#include <cstddef>
#include <cstdint>

namespace scf::core
{
	// How the engine is asked to move one summon. Pure data: the engine driver
	// (src/Engine/SummonMovement.cpp) turns it into Actor::SetDontMove /
	// Actor::KeepOffsetFromActor / Actor::ClearKeepOffsetFromActor calls.
	enum class MovementMode : std::uint8_t
	{
		None,          // Combat AI or the actor's own package drives locomotion
		FollowSlot,    // Hold the squad formation slot around the owner
		ReturnToOwner, // Run to the owner until inside followRadius
		MoveToPoint,   // Walk to SummonModel::moveTargetPosition
		HoldPosition   // Stand still (guard / wait): goal parked, translation locked
	};

	constexpr const char* ToString(MovementMode a_mode) noexcept
	{
		switch (a_mode) {
		case MovementMode::None:          return "None";
		case MovementMode::FollowSlot:    return "FollowSlot";
		case MovementMode::ReturnToOwner: return "ReturnToOwner";
		case MovementMode::MoveToPoint:   return "MoveToPoint";
		case MovementMode::HoldPosition:  return "HoldPosition";
		default:                          return "Unknown";
		}
	}

	struct MovementIntent
	{
		MovementMode mode{ MovementMode::None };

		// Owner body space (+X right, +Y forward, +Z up). Used by FollowSlot,
		// ReturnToOwner (always zero) and MoveToPoint.
		Vec3 localOffset{};

		float followRadius{ 0.0f };   // Inside this radius the actor stops moving
		float catchUpRadius{ 0.0f };  // Outside this radius the actor runs

		// Facing offset (degrees) relative to the owner's heading. This is the value
		// KeepOffsetFromActor takes in its angle-Z argument: with 0 the actor faces
		// where the owner faces (which makes it moonwalk whenever the goal lies to the
		// side or behind). The driver sets it to the direction of travel while moving
		// and to 0 while standing in formation.
		float facingOffsetDegrees{ 0.0f };

		bool lockTranslation{ false }; // Actor::SetDontMove(true)
	};

	// Arrival tolerance of a MoveTo order, in world units.
	inline constexpr float kMoveToArrivalRadius = 150.0f;

	// A MoveTo destination farther than this from the owner is treated as stale (the
	// player changed cell / teleported while the order was live) and is abandoned.
	inline constexpr float kMaxMoveToDistance = 8000.0f;

	// Closer than this the actor is considered "on the goal": it keeps the owner's
	// heading instead of following a noisy direction-of-travel.
	inline constexpr float kFacingMinDistance = 100.0f;

	// Slowest allowed follow radius, so a summon never grinds against the owner.
	inline constexpr float kMinFollowRadius = 40.0f;

	// Translates the summon's current order into a movement intent.
	//
	// Rules, highest priority first:
	//   * Attack (explicit FocusTarget) wins over any movement order: the combat AI
	//     drives the summon.
	//   * WaitGuard holds its ground, even in combat.
	//   * Return runs to the owner and hands back to Follow once inside followMinDistance.
	//   * MoveTo walks to the stored destination (owner-relative, recomputed every tick
	//     so a moving owner does not drag the destination along); a destination beyond
	//     kMaxMoveToDistance is ignored as stale.
	//   * Follow keeps the formation slot, but only while the summon is NOT fighting:
	//     an engaged summon must be driven by the combat AI, not dragged back by us.
	//     The engine combat flag is the only authority; a stale model target (a focus
	//     command whose enemy disengaged) must not freeze the follow order.
	MovementIntent BuildMovementIntent(
		const SummonModel& a_model,
		bool a_inCombat,
		const SettingsData& a_settings,
		const Vec3& a_ownerPosition,
		const Vec3& a_summonPosition,
		float a_ownerYawRadians,
		std::size_t a_slotIndex,
		std::size_t a_squadSize) noexcept;

	// True when the actor is close enough to a destination to consider the order done.
	bool IsWithinArrivalRadius(const Vec3& a_actorPosition, const Vec3& a_destination, float a_radius = kMoveToArrivalRadius) noexcept;
}
