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
	// PoE2 stage M1: following with three paces.
	//
	// The recall teleport that used to live here (Catch-Up Warp) belongs to stage 7 and
	// has its own decision module, Core/CatchUp.h; this file only decides how fast a
	// following summon closes the distance.
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
		float walkSpeed{ 100.0f };         // inside the formation
		float runSpeed{ 160.0f };          // lagging
		float sprintSpeed{ 230.0f };       // far behind
		float runAtDistance{ 450.0f };     // lag beyond this is a run
		float sprintAtDistance{ 1200.0f }; // lag beyond this is a sprint
	};

	struct FollowMotion
	{
		FollowPace pace{ FollowPace::Walk };
		float speed{ 100.0f };
	};

	// Follow tunables derived from the configuration the driver already holds: the "run"
	// threshold is MovementSettings::followMaxDistance (the distance at which the
	// keep-offset primitive itself starts running), the sprint threshold is
	// MovementSettings::sprintDistance and the three speeds are the configured ones. The
	// sprint threshold is never allowed to fall inside the run band: a misconfigured
	// sprint distance disables sprinting instead of turning every runner into a sprinter.
	FollowTuning FollowTuningFrom(const SettingsData& a_settings) noexcept;

	// Decides the pace of a following summon from its lag.
	//
	// `a_lagDistance` is the horizontal distance left to the follow goal (the formation
	// slot, or the owner while returning).
	//
	// Rules: pace by exclusive thresholds - above sprintAtDistance -> Sprint, above
	// runAtDistance -> Run, otherwise Walk. A summon exactly at a threshold keeps the
	// slower pace. A sprint threshold that is not above the run threshold disables
	// sprinting (Run is then the fastest pace) rather than collapsing the run band.
	FollowMotion DecideFollowMotion(float a_lagDistance, const FollowTuning& a_tuning) noexcept;

	struct MovementIntent
	{
		MovementMode mode{ MovementMode::None };

		// Owner body space (+X right, +Y forward, +Z up). Used by FollowSlot,
		// ReturnToOwner (always zero) and MoveToPoint.
		Vec3 localOffset{};

		float followRadius{ 0.0f };   // Inside this radius the actor stops moving
		float catchUpRadius{ 0.0f };  // Outside this radius the actor runs (must stay > followRadius)

		// Facing offset (degrees) passed to Actor::KeepOffsetFromActor (SE +603990).
		// IMPORTANT (Ghidra-verified on MovementPlannerAgentKeepOffset::PlanMovement at
		// SkyrimSE.exe+6FFDA0): the engine adds a_angle to the SUMMON's OWN current
		// orientation every frame (`desiredYaw = summonYaw + angle.z`), NOT to the
		// anchor's heading. Any non-zero angle injects a perpetual spin torque that makes
		// non-strafing quadrupeds (wolves) run in continuous circles. Keep this 0.0f.
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

	// Minimum gap between followRadius and catchUpRadius so PlanMovement's
	// `(dist - followRadius) / (catchUpRadius - followRadius)` never divides by zero.
	inline constexpr float kMinCatchUpMargin = 10.0f;

	// Minimum time a Return order holds combat-forbidden state before flipping back to
	// Follow once the summon is out of combat and within followMinDistance of the owner.
	// Prevents casting Follow near an enemy from flipping Return -> Follow on tick 1 and
	// immediately re-entering combat on tick 2.
	inline constexpr float kReturnMinDisengageSeconds = 2.5f;

	// Converts degrees to radians for Actor::KeepOffsetFromActor (native function expects radians).
	float DegreesToRadians(float a_degrees) noexcept;

	// Shortest unsigned angular difference between two angles in degrees, in [0, 180].
	// Prevents wrap-around jitter across the +/-180 degree seam.
	float AngularDifferenceDegrees(float a_lhsDegrees, float a_rhsDegrees) noexcept;

	// Guarantees catchUpRadius is strictly larger than followRadius so native
	// MovementPlannerAgentKeepOffset::PlanMovement (+6FFDA0) never divides by zero.
	float SafeKeepOffsetCatchUpRadius(float a_followRadius, float a_catchUpRadius) noexcept;

	// True only when a Return recall may safely hand back to Follow: the summon is out
	// of combat, back within owner range, and the disengage hold window has elapsed.
	bool ShouldCompleteReturn(
		float a_distanceToOwner,
		float a_followMinDistance,
		bool a_inCombat,
		float a_elapsedInReturn,
		float a_minDisengageSeconds = kReturnMinDisengageSeconds) noexcept;

	// Progress-based stall guard for explicit SummonOrder::Attack orders.
	// Replaces the blind wall-clock grace timeout so a summon charging a distant target
	// is never aborted mid-run while closing distance or fighting.
	struct AttackProgressState
	{
		EntityID targetId{ InvalidEntityID };
		float startedAt{ 0.0f };
		float lastProgressAt{ 0.0f };
		float bestDistance{ 0.0f };
		bool initialized{ false };
	};

	struct AttackAbortConfig
	{
		// Minimum horizontal distance reduction (world units) that counts as forward
		// progress toward the target and resets the stall clock.
		float progressStep{ 64.0f };

		// Seconds without entering combat AND without making forward progress before
		// the target is treated as unreachable and the order reverts to Follow.
		float stallTimeoutSeconds{ 30.0f };
	};

	void ResetAttackProgress(AttackProgressState& a_state) noexcept;

	// Observes the current Attack tick and returns true only when the Attack order must
	// finish (target dead/gone, OR not in combat and stalled without closing distance for
	// stallTimeoutSeconds).
	bool ObserveAndShouldAbortAttack(
		AttackProgressState& a_state,
		EntityID a_targetId,
		bool a_targetAlive,
		bool a_inCombat,
		float a_distanceToTarget,
		float a_now,
		const AttackAbortConfig& a_config = {}) noexcept;

	// Translates the summon's current order into a movement intent.
	//
	// Rules, highest priority first:
	//   * Attack (explicit FocusTarget) wins over any movement order: the combat AI
	//     drives the summon.
	//   * WaitGuard holds its ground, even in combat.
	//   * Return runs to the owner and hands back to Follow once disengaged and inside
	//     followMinDistance.
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

	// Keep charging the same wolf focus until melee handoff. After handoff the
	// wider restart radius lets the combat AI finish a bite on a moving target.
	inline constexpr float kWolfMeleeHandoff = 150.0f;
	inline bool ShouldChargeWolfFocus(float distance, bool chargingSameFocus) noexcept
	{
		return distance > (chargingSameFocus ? kWolfMeleeHandoff : 225.0f);
	}

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
