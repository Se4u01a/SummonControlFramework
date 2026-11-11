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
		AttackApproach,// Explicit FocusTarget: close in on the focused actor
		HoldPosition   // Stand still (guard / wait): goal parked, translation locked
	};

	constexpr const char* ToString(MovementMode a_mode) noexcept
	{
		switch (a_mode) {
		case MovementMode::None:           return "None";
		case MovementMode::FollowSlot:     return "FollowSlot";
		case MovementMode::ReturnToOwner:  return "ReturnToOwner";
		case MovementMode::MoveToPoint:    return "MoveToPoint";
		case MovementMode::AttackApproach: return "AttackApproach";
		case MovementMode::HoldPosition:   return "HoldPosition";
		default:                           return "Unknown";
		}
	}

	// ------------------------------------------------------------------
	// PoE2 stage M1: following with three paces and a recall teleport.
	// ------------------------------------------------------------------

	// How fast a following summon closes the distance to its follow goal (its formation
	// slot, or the owner while returning).
	enum class FollowPace : std::uint8_t
	{
		Walk,   // inside the formation: the actor's own pace
		Run,    // lagging behind the goal by more than FollowTuning::runAtDistance
		Sprint  // far behind: lagging by more than FollowTuning::sprintAtDistance
	};

	constexpr const char* ToString(FollowPace a_pace) noexcept
	{
		switch (a_pace) {
		case FollowPace::Walk:   return "Walk";
		case FollowPace::Run:    return "Run";
		case FollowPace::Sprint: return "Sprint";
		default:                 return "Unknown";
		}
	}

	// Tunables of the follow race: pure data, passed by argument, so Core never reads an
	// INI. The engine driver builds them from the settings object it already receives
	// (FollowTuningFrom) and applies FollowMotion::speed through the actor's SpeedMult
	// actor value, where 100 is the actor's own normal movement pace.
	struct FollowTuning
	{
		float walkSpeed{ 100.0f };       // inside the formation
		float runSpeed{ 160.0f };        // lagging
		float sprintSpeed{ 230.0f };     // far behind
		float runAtDistance{ 450.0f };   // lag beyond this is a run
		float sprintAtDistance{ 1200.0f }; // lag beyond this is a sprint
		float catchUpDistance{ 3500.0f };  // owner distance beyond which the summon is recalled
		float stuckSeconds{ 3.0f };      // no progress for this long while it should move
		float stuckMinDistance{ 300.0f }; // ...and it still has this much lag to close
		bool enableCatchUpWarp{ true };  // operator switch (Recovery.bEnableCatchUpWarp)
	};

	struct FollowMotion
	{
		FollowPace pace{ FollowPace::Walk };
		float speed{ 100.0f };
		bool teleport{ false };            // recall behind the owner this tick
		bool teleportByDistance{ false };  // ...because it is beyond catchUpDistance
		bool teleportByStuck{ false };     // ...because it made no progress
	};

	// Follow tunables derived from the configuration the driver already holds: the "run"
	// threshold is MovementSettings::followMaxDistance (the distance at which the
	// keep-offset primitive itself starts running), the recall distance is
	// RecoverySettings::catchUpDistance, and the recall switch is
	// RecoverySettings::enableCatchUpWarp. The sprint threshold, the three speeds and the
	// stuck timings keep the documented defaults above.
	FollowTuning FollowTuningFrom(const SettingsData& a_settings) noexcept;

	// Decides the pace of a following summon and whether it must be recalled behind the
	// owner.
	//
	// `a_lagDistance` is the horizontal distance left to the follow goal (the formation
	// slot, or the owner while returning); `a_distanceToOwner` the horizontal distance to
	// the owner; `a_secondsWithoutProgress` how long the lag has not decreased while the
	// summon was supposed to close it.
	//
	// Rules:
	//   * Pace, exclusive thresholds: above sprintAtDistance -> Sprint, above
	//     runAtDistance -> Run, otherwise Walk. A summon exactly at a threshold keeps the
	//     slower pace. A sprint threshold that is not above the run threshold disables
	//     sprinting (Run is then the fastest pace) rather than collapsing the run band.
	//   * A recall is a teleport, so it needs all of: the catch-up switch on, the order is
	//     Follow or Return (the two orders that bring the summon back to the owner), and
	//     the engine is not fighting (a warp would drop the engagement; combat AI owns
	//     locomotion then). MoveTo, Attack and WaitGuard never warp.
	//   * Recall triggers when the summon is at or beyond catchUpDistance from the owner,
	//     or when it made no progress for stuckSeconds while still lagging more than
	//     stuckMinDistance behind its goal (a wall, a ledge, a closed door).
	FollowMotion DecideFollowMotion(
		SummonOrder a_order,
		bool a_inCombat,
		bool a_combatPermitted,
		float a_lagDistance,
		float a_distanceToOwner,
		float a_secondsWithoutProgress,
		const FollowTuning& a_tuning) noexcept;

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
	//     `a_combatPermitted` is the CombatPolicy answer for this tick: when the policy
	//     forbids combat (Passive, Return, MoveTo) the summon keeps following even if the
	//     engine still reports it in combat.
	//     The engine combat flag is the only authority; a stale model target (a focus
	//     command whose enemy disengaged) must not freeze the follow order.
	MovementIntent BuildMovementIntent(
		const SummonModel& a_model,
		bool a_inCombat,
		bool a_combatPermitted,
		const SettingsData& a_settings,
		const Vec3& a_ownerPosition,
		const Vec3& a_summonPosition,
		float a_ownerYawRadians,
		std::size_t a_slotIndex,
		std::size_t a_squadSize) noexcept;

	// True when the actor is close enough to a destination to consider the order done.
	bool IsWithinArrivalRadius(const Vec3& a_actorPosition, const Vec3& a_destination, float a_radius = kMoveToArrivalRadius) noexcept;

	// True for orders that need the summon away from the player, during which the
	// vanilla FollowTo package's "stay within Min Radius of the caster" pull is released
	// (src/Engine/FollowLeash.cpp): MoveTo with a destination, Attack, WaitGuard.
	// Follow and Return keep the vanilla follow (it agrees with them).
	bool ReleasesFollowLeash(const SummonModel& a_model) noexcept;

	// Shape of the engine's summon follow package when its creation was not observed:
	// a runtime-created form (FormID 0xFF......) of package type kPackage (18) whose data
	// carries exactly two float inputs (FollowTo's Min/Max Radius).
	bool IsSummonFollowPackageShape(std::uint8_t a_packageType, std::uint32_t a_formId, std::uint32_t a_floatInputs) noexcept;
}
