#include "Core/MovementIntent.h"
#include "Core/Formation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace scf::core
{
	float DegreesToRadians(float a_degrees) noexcept
	{
		return a_degrees * (static_cast<float>(std::numbers::pi) / 180.0f);
	}

	float AngularDifferenceDegrees(float a_lhsDegrees, float a_rhsDegrees) noexcept
	{
		float diff = std::fmod(a_lhsDegrees - a_rhsDegrees, 360.0f);
		if (diff > 180.0f) {
			diff -= 360.0f;
		} else if (diff < -180.0f) {
			diff += 360.0f;
		}
		return std::abs(diff);
	}

	float SafeKeepOffsetCatchUpRadius(float a_followRadius, float a_catchUpRadius) noexcept
	{
		const float safeFollow = std::max(0.0f, a_followRadius);
		return std::max(a_catchUpRadius, safeFollow + kMinCatchUpMargin);
	}

	bool ShouldCompleteReturn(
		float a_distanceToOwner,
		float a_followMinDistance,
		bool a_inCombat,
		float a_elapsedInReturn,
		float a_minDisengageSeconds) noexcept
	{
		if (a_inCombat) {
			return false;
		}
		const float minHold = std::max(0.0f, a_minDisengageSeconds);
		if (a_elapsedInReturn < minHold) {
			return false;
		}
		return a_distanceToOwner <= std::max(0.0f, a_followMinDistance);
	}

	void ResetAttackProgress(AttackProgressState& a_state) noexcept
	{
		a_state = AttackProgressState{};
	}

	bool ObserveAndShouldAbortAttack(
		AttackProgressState& a_state,
		EntityID a_targetId,
		bool a_targetAlive,
		bool a_inCombat,
		float a_distanceToTarget,
		float a_now,
		const AttackAbortConfig& a_config) noexcept
	{
		if (!a_targetAlive || a_targetId == InvalidEntityID) {
			ResetAttackProgress(a_state);
			return true;
		}

		const float distance = std::max(0.0f, a_distanceToTarget);
		if (!a_state.initialized || a_state.targetId != a_targetId) {
			a_state.targetId = a_targetId;
			a_state.startedAt = a_now;
			a_state.lastProgressAt = a_now;
			a_state.bestDistance = distance;
			a_state.initialized = true;
			return false;
		}

		// Guard against non-monotonic clocks (save reload / reset).
		if (a_now < a_state.lastProgressAt) {
			a_state.lastProgressAt = a_now;
		}

		if (a_inCombat) {
			a_state.lastProgressAt = a_now;
			if (distance < a_state.bestDistance) {
				a_state.bestDistance = distance;
			}
			return false;
		}

		const float step = std::max(0.0f, a_config.progressStep);
		if (distance <= a_state.bestDistance - step) {
			a_state.bestDistance = distance;
			a_state.lastProgressAt = a_now;
			return false;
		}

		if (!(a_config.stallTimeoutSeconds > 0.0f)) {
			return false;
		}
		return (a_now - a_state.lastProgressAt) >= a_config.stallTimeoutSeconds;
	}

	FollowTuning FollowTuningFrom(const SettingsData& a_settings) noexcept
	{
		FollowTuning tuning{};
		const auto& movement = a_settings.movement;

		tuning.walkSpeed = movement.followWalkSpeed;
		tuning.runSpeed = movement.followRunSpeed;
		tuning.sprintSpeed = movement.sprintSpeed;
		tuning.sprintAtDistance = movement.sprintDistance;

		// The keep-offset primitive already runs the actor beyond its catch-up radius, and
		// that radius is followMaxDistance: the same distance is the "run" threshold.
		tuning.runAtDistance = std::max(movement.followMaxDistance, movement.followMinDistance);

		// Never let the configured follow distance reach past the sprint threshold: a
		// narrower formation must not turn "run" into "sprint" for free.
		constexpr float kSprintRunFactor = 2.0f;
		tuning.sprintAtDistance = std::max(tuning.sprintAtDistance, tuning.runAtDistance * kSprintRunFactor);
		return tuning;
	}

	FollowMotion DecideFollowMotion(float a_lagDistance, const FollowTuning& a_tuning) noexcept
	{
		FollowMotion motion{};

		// Pace from the lag. Thresholds are exclusive: a summon standing exactly on a
		// threshold keeps the slower pace. A sprint threshold that is not genuinely beyond
		// the run threshold disables sprinting instead of collapsing the run band: a broken
		// configuration must not make every runner sprint.
		const float runAt = std::max(0.0f, a_tuning.runAtDistance);
		if (a_tuning.sprintAtDistance > runAt && a_lagDistance > a_tuning.sprintAtDistance) {
			motion.pace = FollowPace::Sprint;
			motion.speed = a_tuning.sprintSpeed;
		} else if (a_lagDistance > runAt) {
			motion.pace = FollowPace::Run;
			motion.speed = a_tuning.runSpeed;
		} else {
			motion.pace = FollowPace::Walk;
			motion.speed = a_tuning.walkSpeed;
		}
		return motion;
	}

	MovementIntent BuildMovementIntent(
		const SummonModel& a_model,
		bool a_inCombat,
		bool a_combatPermitted,
		const SettingsData& a_settings,
		const Vec3& a_ownerPosition,
		const Vec3& /*a_summonPosition*/,
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

		// 3. Return: run back to the owner, then the order flips to Follow once disengaged.
		//    facingOffsetDegrees stays 0.0f: native KeepOffsetFromActor (+6FFDA0) adds
		//    angle.z to the summon's OWN yaw every frame, so any non-zero value spins it.
		if (a_model.order == SummonOrder::Return) {
			MovementIntent intent{};
			const float followRadius = std::max(movement.followMinDistance, 150.0f);
			const float catchUpRadius = SafeKeepOffsetCatchUpRadius(
				followRadius,
				std::max(movement.followMaxDistance, followRadius * 2.0f));
			intent.mode = MovementMode::ReturnToOwner;
			intent.followRadius = followRadius;
			intent.catchUpRadius = catchUpRadius;
			intent.facingOffsetDegrees = 0.0f;
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
				intent.catchUpRadius = SafeKeepOffsetCatchUpRadius(
					intent.followRadius,
					std::max(movement.followMaxDistance, kMoveToArrivalRadius * 4.0f));
				intent.facingOffsetDegrees = 0.0f;
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
		FormationLayout layout{};
		layout.outerRingCapacity = a_settings.squad.formationRingCapacity;
		layout.innerRingScale = a_settings.squad.formationInnerRingScale;
		layout.minSlotDistance = a_settings.squad.formationMinSlotDistance;
		layout.maxExpansion = a_settings.squad.formationMaxExpansion;
		intent.localOffset = FormationSlotOffsetLocal(a_slotIndex, a_squadSize, movement.followMinDistance, layout);
		intent.followRadius = std::max(kMinFollowRadius, movement.followMinDistance * 0.5f);
		intent.catchUpRadius = SafeKeepOffsetCatchUpRadius(
			intent.followRadius,
			std::max(movement.followMaxDistance, intent.followRadius * 2.0f));
		intent.facingOffsetDegrees = 0.0f;
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

	bool IsSummonFollowPackageShape(std::uint8_t a_packageType, std::uint32_t a_formId, std::uint32_t a_floatInputs) noexcept
	{
		constexpr std::uint8_t kPackageTypePackage = 18;
		return a_packageType == kPackageTypePackage && (a_formId >> 24) == 0xFF && a_floatInputs == 2;
	}

	bool IsWithinArrivalRadius(const Vec3& a_actorPosition, const Vec3& a_destination, float a_radius) noexcept
	{
		const float dx = a_actorPosition.x - a_destination.x;
		const float dy = a_actorPosition.y - a_destination.y;
		return (dx * dx + dy * dy) <= (a_radius * a_radius);
	}
}
