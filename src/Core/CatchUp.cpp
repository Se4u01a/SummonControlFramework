#include "Core/CatchUp.h"
#include "Core/MovementIntent.h"

#include <algorithm>
#include <cmath>

namespace scf::core
{
	namespace
	{
		// The distance trigger re-arms only once the owner is back inside this radius.
		// Clamped so a misconfigured ratio cannot invert or collapse the band.
		float RearmDistance(const CatchUpTuning& a_tuning) noexcept
		{
			const float ratio = std::clamp(a_tuning.rearmRatio, 0.05f, 0.95f);
			return std::max(0.0f, a_tuning.catchUpDistance) * ratio;
		}

		constexpr float kDegToRadians = 0.017453292519943295f;

		// A candidate closer than this to one already in the list is the same spot.
		constexpr float kDuplicateDistance = 25.0f;

		Vec3 LocalOffsetAt(float a_bearingDegrees, float a_radius) noexcept
		{
			const float bearing = a_bearingDegrees * kDegToRadians;
			return Vec3{ std::sin(bearing) * a_radius, std::cos(bearing) * a_radius, 0.0f };
		}
	}

	CatchUpTuning CatchUpTuningFrom(const SettingsData& a_settings) noexcept
	{
		CatchUpTuning tuning{};
		const auto& recovery = a_settings.recovery;

		tuning.enableWarp = recovery.enableCatchUpWarp;
		tuning.stuckSeconds = recovery.stuckWarpSeconds;
		tuning.stuckMinDistance = recovery.stuckWarpMinDistance;
		tuning.cooldownSeconds = recovery.catchUpCooldownSeconds;
		tuning.allowWhileSummonInCombat = recovery.catchUpWarpSummonCombat;
		tuning.allowWhileOwnerInCombat = recovery.catchUpWarpOwnerCombat;

		// 0 keeps the documented default: the distance trigger has no "off" value, the
		// operator switch above is the way to turn the recall off.
		if (recovery.catchUpDistance > 0.0f) {
			tuning.catchUpDistance = recovery.catchUpDistance;
		}

		// A recall at or inside the sprint band would make the sprint pace dead code: the
		// summon would be teleported before it could ever use it.
		const FollowTuning follow = FollowTuningFrom(a_settings);
		tuning.catchUpDistance = std::max(tuning.catchUpDistance, follow.sprintAtDistance);
		return tuning;
	}

	void ObserveCatchUp(CatchUpMemory& a_memory, const CatchUpObservation& a_observation, const CatchUpTuning& a_tuning) noexcept
	{
		const float lag = std::max(0.0f, a_observation.lagDistance);
		const float holdRadius = std::max(0.0f, a_observation.holdRadius);
		const float epsilon = std::max(0.0f, a_tuning.progressEpsilon);

		// Progress: real travel, holding the slot, or a measured closing of the gap. A
		// summon that trails a sprinting player without closing the gap travels fast and
		// counts as progressing too - otherwise a normal chase would end in a teleport.
		const bool progress = a_observation.isMoving || lag <= holdRadius || lag < a_memory.bestLag - epsilon;
		if (progress) {
			a_memory.bestLag = std::min(a_memory.bestLag, lag);
			a_memory.lastProgressAt = a_observation.now;
			a_memory.awaitingProgress = false;
		}

		// Hysteresis: the distance trigger stays disarmed until the summon is genuinely
		// back near its owner, so a recall that did not help is not repeated every tick.
		if (a_observation.distanceToOwner <= RearmDistance(a_tuning)) {
			a_memory.distanceArmed = true;
		}
	}

	CatchUpDecision DecideCatchUp(
		const CatchUpContext& a_context,
		const CatchUpObservation& a_observation,
		const CatchUpMemory& a_memory,
		const CatchUpTuning& a_tuning) noexcept
	{
		CatchUpDecision decision{};

		// 1. Do the measurements call for a recall at all? A summon that is merely
		//    walking along is not an event, so it is not reported as a refusal either.
		const float secondsWithoutProgress = SecondsWithoutProgress(a_memory, a_observation.now);
		const bool rawDistance = a_tuning.catchUpDistance > 0.0f &&
		                         a_observation.distanceToOwner >= a_tuning.catchUpDistance;
		const bool rawStuck = a_tuning.stuckSeconds > 0.0f &&
		                      secondsWithoutProgress >= a_tuning.stuckSeconds &&
		                      a_observation.lagDistance > a_tuning.stuckMinDistance;
		if (!rawDistance && !rawStuck) {
			return decision;
		}

		// From here on the measurements HAVE called for a recall, so the trigger is
		// reported even if a gate below refuses it: the statistics then say what the race
		// looked like, not only what was performed.
		decision.trigger = rawDistance ? CatchUpTrigger::Distance : CatchUpTrigger::NoProgress;

		// 2. Only the two orders that bring the summon home may recall it. MoveTo, Attack
		//    and Guard are deliberately left to walk (or fight) where they were sent.
		if (a_context.order != SummonOrder::Follow && a_context.order != SummonOrder::Return) {
			decision.refusal = CatchUpRefusal::OrderNotFollowOrReturn;
			return decision;
		}

		// 3. The operator switch.
		if (!a_tuning.enableWarp) {
			decision.refusal = CatchUpRefusal::Disabled;
			return decision;
		}

		// 4. Combat owns locomotion. The pair (engine combat flag, CombatPolicy answer) is
		//    the same one the follow order uses: a passive summon may still be recalled
		//    while the engine still reports it in combat.
		if (a_context.summonInCombat && a_context.combatPermitted && !a_tuning.allowWhileSummonInCombat) {
			decision.refusal = CatchUpRefusal::SummonInCombat;
			return decision;
		}
		if (a_context.ownerInCombat && !a_tuning.allowWhileOwnerInCombat) {
			decision.refusal = CatchUpRefusal::OwnerInCombat;
			return decision;
		}

		// 5. A recall is a position write inside ONE loaded space. Crossing an interior /
		//    exterior boundary or a worldspace is the engine's own summon snap's job.
		if (!a_context.sameSpace) {
			decision.refusal = CatchUpRefusal::DifferentSpace;
			return decision;
		}

		// 6. There must be ground next to the owner to land on.
		if (a_context.ownerAirborne) {
			decision.refusal = CatchUpRefusal::OwnerAirborne;
			return decision;
		}
		if (a_context.ownerSwimming) {
			decision.refusal = CatchUpRefusal::OwnerSwimming;
			return decision;
		}
		if (a_context.ownerMounted) {
			decision.refusal = CatchUpRefusal::OwnerMounted;
			return decision;
		}

		// 7. Hysteresis before cooldown: it is the more informative answer ("the summon is
		//    still far but has not been back near the owner yet").
		const bool armedByDistance = rawDistance && a_memory.distanceArmed;
		const bool armedByStuck = rawStuck && !a_memory.awaitingProgress;
		if (armedByStuck && !armedByDistance) {
			// The distance gate is closed but the stuck one is not: that is what the recall
			// is actually about.
			decision.trigger = CatchUpTrigger::NoProgress;
		}
		if (!armedByDistance && !armedByStuck) {
			decision.refusal = a_memory.awaitingProgress ? CatchUpRefusal::AwaitingProgress : CatchUpRefusal::NotArmed;
			return decision;
		}

		if (a_tuning.cooldownSeconds > 0.0f && (a_observation.now - a_memory.lastAttemptAt) < a_tuning.cooldownSeconds) {
			decision.refusal = CatchUpRefusal::Cooldown;
			return decision;
		}

		decision.warp = true;
		return decision;
	}

	void MarkCatchUpWarped(CatchUpMemory& a_memory, float a_now) noexcept
	{
		a_memory.lastWarpAt = a_now;
		a_memory.lastAttemptAt = a_now;
		a_memory.lastProgressAt = a_now;
		a_memory.bestLag = 1.0e9f;
		a_memory.awaitingProgress = true;
		a_memory.distanceArmed = false;
	}

	void MarkCatchUpAttempt(CatchUpMemory& a_memory, float a_now) noexcept
	{
		a_memory.lastAttemptAt = a_now;
	}

	void ResetCatchUpProgress(CatchUpMemory& a_memory, float a_now) noexcept
	{
		a_memory.bestLag = 1.0e9f;
		a_memory.lastProgressAt = a_now;
		a_memory.awaitingProgress = false;
		a_memory.distanceArmed = true;
	}

	std::size_t BuildLandingCandidates(
		const Vec3& a_ownerPosition,
		float a_ownerYawRadians,
		std::size_t a_slotIndex,
		std::size_t a_squadSize,
		const FormationLayout& a_formation,
		const CatchUpTuning& a_tuning,
		LandingCandidate* a_out,
		std::size_t a_capacity) noexcept
	{
		if (!a_out || a_capacity == 0) {
			return 0;
		}

		std::size_t count = 0;

		const auto tryPush = [&](const Vec3& a_position, float a_bearing) {
			if (count >= a_capacity) {
				return;
			}
			for (std::size_t i = 0; i < count; ++i) {
				if (a_out[i].position.DistanceSquaredTo(a_position) <= kDuplicateDistance * kDuplicateDistance) {
					return;
				}
			}
			a_out[count].position = a_position;
			a_out[count].bearingDegrees = a_bearing;
			++count;
		};

		const auto pushLocal = [&](const Vec3& a_localOffset, float a_bearing) {
			tryPush(OwnerLocalToWorldPoint(a_localOffset, a_ownerPosition, a_ownerYawRadians), a_bearing);
		};

		// 1. The formation slot the follow rules were aiming for, at the landing radius.
		if (a_squadSize > 0) {
			const Vec3 slot = FormationSlotOffsetLocal(a_slotIndex, a_squadSize, a_tuning.landingDistance, a_formation);
			const float bearingDegrees = std::atan2(slot.x, slot.y) / kDegToRadians;
			pushLocal(slot, bearingDegrees);
		}

		// 2./3. Straight behind, at the landing distance and farther back: a door frame or a
		// narrow corridor can leave the closest spot occupied while the next one is clear.
		pushLocal(LocalOffsetAt(180.0f, a_tuning.landingDistance), 180.0f);
		pushLocal(LocalOffsetAt(180.0f, a_tuning.landingDistance * 1.8f), 180.0f);

		// 4. The rear flanks, 5. the sides.
		pushLocal(LocalOffsetAt(135.0f, a_tuning.landingDistance), 135.0f);
		pushLocal(LocalOffsetAt(-135.0f, a_tuning.landingDistance), -135.0f);
		pushLocal(LocalOffsetAt(90.0f, a_tuning.landingDistance), 90.0f);
		pushLocal(LocalOffsetAt(-90.0f, a_tuning.landingDistance), -90.0f);

		return count;
	}

	bool IsLandingSpotUsable(const LandingObservation& a_observation, const CatchUpTuning& a_tuning) noexcept
	{
		if (!a_observation.hasSpace || !a_observation.groundFound) {
			return false;
		}
		if (a_observation.blocked) {
			return false;
		}
		if (a_observation.groundRise > a_tuning.landingMaxRise) {
			return false;
		}
		if (a_observation.groundRise < -a_tuning.landingMaxDrop) {
			return false;
		}
		if (a_observation.waterRise < a_tuning.landingWaterClearance) {
			return false;
		}
		if (a_observation.nearestActor < a_tuning.landingClearance) {
			return false;
		}
		return true;
	}

	LandingSelection SelectLandingSpot(
		const LandingCandidate* a_candidates,
		const LandingObservation* a_observations,
		std::size_t a_count,
		const CatchUpTuning& a_tuning) noexcept
	{
		LandingSelection selection{};
		if (!a_candidates || !a_observations) {
			return selection;
		}

		for (std::size_t i = 0; i < a_count; ++i) {
			if (!IsLandingSpotUsable(a_observations[i], a_tuning)) {
				continue;
			}
			selection.found = true;
			selection.index = i;
			selection.position = a_candidates[i].position;
			selection.position.z += a_observations[i].groundRise;
			selection.refusal = CatchUpRefusal::None;
			return selection;
		}
		return selection;
	}

	void RecordCatchUpTrigger(CatchUpStats& a_stats, CatchUpTrigger a_trigger) noexcept
	{
		switch (a_trigger) {
		case CatchUpTrigger::Distance:
			++a_stats.distanceTriggers;
			break;
		case CatchUpTrigger::NoProgress:
			++a_stats.stuckTriggers;
			break;
		default:
			break;
		}
		if (a_trigger != CatchUpTrigger::None) {
			a_stats.lastTrigger = a_trigger;
		}
	}

	void RecordCatchUpWarp(CatchUpStats& a_stats, CatchUpTrigger a_trigger, float a_now) noexcept
	{
		++a_stats.warps;
		a_stats.lastWarpAt = a_now;
		if (a_trigger != CatchUpTrigger::None) {
			a_stats.lastTrigger = a_trigger;
		}
	}

	void RecordCatchUpRefusal(CatchUpStats& a_stats, CatchUpRefusal a_refusal) noexcept
	{
		const auto index = static_cast<std::size_t>(a_refusal);
		if (index < kCatchUpRefusalCount) {
			++a_stats.refusals[index];
		}
		a_stats.lastRefusal = a_refusal;
	}

	void ResetCatchUpStats(CatchUpStats& a_stats) noexcept
	{
		a_stats = CatchUpStats{};
	}

	std::string FormatCatchUpStats(const CatchUpStats& a_stats)
	{
		std::string text = "catch-up: warps=" + std::to_string(a_stats.warps) +
		                   " triggers(distance=" + std::to_string(a_stats.distanceTriggers) +
		                   ", no-progress=" + std::to_string(a_stats.stuckTriggers) + ")";

		for (std::size_t i = 1; i < kCatchUpRefusalCount; ++i) {
			if (a_stats.refusals[i] == 0) {
				continue;
			}
			text += ' ';
			text += ToString(static_cast<CatchUpRefusal>(i));
			text += '=';
			text += std::to_string(a_stats.refusals[i]);
		}

		if (a_stats.lastRefusal != CatchUpRefusal::None) {
			text += " last=";
			text += ToString(a_stats.lastRefusal);
		}
		return text;
	}
}
