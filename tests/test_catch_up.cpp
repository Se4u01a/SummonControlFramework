#include "check.h"
#include "Core/CatchUp.h"
#include "Core/Formation.h"
#include "Core/MovementIntent.h"
#include "Core/Settings.h"

#include <cmath>
#include <numbers>
#include <string>

using namespace scf::core;

namespace
{
	constexpr float kPi = static_cast<float>(std::numbers::pi);

	// The documented defaults, spelled out so a changed default fails loudly.
	CatchUpTuning CatchSettings()
	{
		CatchUpTuning tuning{};
		tuning.enableWarp = true;
		tuning.catchUpDistance = 3500.0f;
		tuning.stuckSeconds = 3.0f;
		tuning.stuckMinDistance = 300.0f;
		tuning.progressEpsilon = 50.0f;
		tuning.cooldownSeconds = 5.0f;
		tuning.rearmRatio = 0.5f;
		tuning.allowWhileSummonInCombat = false;
		tuning.allowWhileOwnerInCombat = false;
		tuning.landingDistance = 200.0f;
		tuning.landingClearance = 120.0f;
		tuning.landingMaxRise = 200.0f;
		tuning.landingMaxDrop = 600.0f;
		tuning.landingWaterClearance = 24.0f;
		return tuning;
	}

	// A following summon the driver just observed: the progress clock started at `a_now`,
	// nothing is pending and the distance trigger is armed.
	CatchUpMemory CalmMemory(float a_now)
	{
		CatchUpMemory memory{};
		memory.lastProgressAt = a_now;
		memory.bestLag = 200.0f;
		memory.distanceArmed = true;
		return memory;
	}

	CatchUpObservation ObservationAt(float a_now, float a_lag, float a_ownerDistance)
	{
		CatchUpObservation observation{};
		observation.now = a_now;
		observation.lagDistance = a_lag;
		observation.distanceToOwner = a_ownerDistance;
		observation.holdRadius = 100.0f;
		return observation;
	}

	CatchUpContext FollowingContext()
	{
		CatchUpContext context{};
		context.order = SummonOrder::Follow;
		context.combatPermitted = true;
		context.sameSpace = true;
		return context;
	}

	LandingCandidate CandidateBehind(float a_distance)
	{
		LandingCandidate candidate{};
		candidate.position = { 0.0f, -a_distance, 0.0f };
		candidate.bearingDegrees = 180.0f;
		return candidate;
	}

	LandingObservation GoodSpot()
	{
		LandingObservation observation{};
		observation.hasSpace = true;
		observation.groundFound = true;
		observation.groundRise = -10.0f;
		observation.nearestActor = 1000.0f;
		observation.waterRise = 1000.0f;
		return observation;
	}
}

// ---------------------------------------------------------------------------
// When the recall fires.
// ---------------------------------------------------------------------------

// Given a following summon far from its owner
// When it reaches the catch-up distance
// Then the recall fires exactly at the threshold, and not one unit before.
TEST(TestCatchUpTriggersBeyondCatchUpDistance)
{
	const auto tuning = CatchSettings();
	const auto memory = CalmMemory(100.0f);

	const auto below = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 3499.0f, 3499.0f), memory, tuning);
	CHECK(!below.warp);
	CHECK(below.refusal == CatchUpRefusal::None);

	const auto atThreshold = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 3500.0f, 3500.0f), memory, tuning);
	CHECK(atThreshold.warp);
	CHECK(atThreshold.trigger == CatchUpTrigger::Distance);
	CHECK(atThreshold.refusal == CatchUpRefusal::None);
}

// Given a following summon that must move but makes no progress
// When it has been stuck for the configured time, still lagging more than the minimum
// Then the recall fires exactly at the threshold, and not before.
TEST(TestCatchUpTriggersWhenStuck)
{
	const auto tuning = CatchSettings();
	auto memory = CalmMemory(97.01f);

	const auto before = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 800.0f, 800.0f), memory, tuning);
	CHECK(!before.warp);
	CHECK(before.refusal == CatchUpRefusal::None);

	memory.lastProgressAt = 97.0f;
	const auto atThreshold = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 800.0f, 800.0f), memory, tuning);
	CHECK(atThreshold.warp);
	CHECK(atThreshold.trigger == CatchUpTrigger::NoProgress);
}

// Given a summon that reports a long "no progress" time but has almost nothing to close
// When the recall is decided
// Then it is not recalled: standing in the slot is not being stuck.
TEST(TestCatchUpStuckRecallNeedsDistanceToClose)
{
	const auto tuning = CatchSettings();

	const auto inSlot = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 100.0f, 100.0f), CalmMemory(0.0f), tuning);
	CHECK(!inSlot.warp);
	CHECK(inSlot.refusal == CatchUpRefusal::None);

	// Exactly on the minimum does not count; one unit past it does (the lag is 600 so
	// only the stuck band can trigger - the owner distance is far below 3500).
	const auto atMinimum = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 300.0f, 800.0f), CalmMemory(0.0f), tuning);
	CHECK(!atMinimum.warp);

	const auto beyondMinimum = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 301.0f, 800.0f), CalmMemory(0.0f), tuning);
	CHECK(beyondMinimum.warp);
	CHECK(beyondMinimum.trigger == CatchUpTrigger::NoProgress);
}

// Given a summon that is merely walking along (recent progress, close enough)
// When the recall is decided
// Then nothing is reported at all: no warp and no refusal event.
TEST(TestCatchUpQuietSummonIsNotAnEvent)
{
	const auto tuning = CatchSettings();
	const auto decision = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 280.0f, 280.0f), CalmMemory(99.0f), tuning);
	CHECK(!decision.warp);
	CHECK(decision.refusal == CatchUpRefusal::None);
	CHECK(decision.trigger == CatchUpTrigger::None);
}

// ---------------------------------------------------------------------------
// Hysteresis, cooldown and the progress clock.
// ---------------------------------------------------------------------------

// Given a recall that just happened, so the distance trigger is disarmed
// When the summon is still far but has not been back near the owner since
// Then it is not recalled again, and the gate opens only inside the exit threshold.
TEST(TestCatchUpHysteresisNeedsRearm)
{
	const auto tuning = CatchSettings();
	auto memory = CalmMemory(100.0f);
	memory.distanceArmed = false;
	memory.lastAttemptAt = -1.0e9f;

	const auto farAgain = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 5000.0f, 5000.0f), memory, tuning);
	CHECK(!farAgain.warp);
	CHECK(farAgain.refusal == CatchUpRefusal::NotArmed);

	// The exit threshold is catchUpDistance * rearmRatio.
	auto justOutside = ObservationAt(101.0f, 1751.0f, 1751.0f);
	ObserveCatchUp(memory, justOutside, tuning);
	CHECK(!memory.distanceArmed);

	auto inside = ObservationAt(102.0f, 1700.0f, 1700.0f);
	ObserveCatchUp(memory, inside, tuning);
	CHECK(memory.distanceArmed);

	const auto armedAgain = DecideCatchUp(FollowingContext(), ObservationAt(103.0f, 5000.0f, 5000.0f), memory, tuning);
	CHECK(armedAgain.warp);
	CHECK(armedAgain.trigger == CatchUpTrigger::Distance);
}

// Given a recall (or a refused attempt) that just happened
// When the next trigger arrives before the cooldown elapsed
// Then nothing is teleported; after the cooldown the very next trigger warps.
TEST(TestCatchUpCooldownBlocksImmediateRepeat)
{
	const auto tuning = CatchSettings();
	auto memory = CalmMemory(0.0f);
	memory.lastAttemptAt = 0.0f;

	const auto during = DecideCatchUp(FollowingContext(), ObservationAt(4.9f, 5000.0f, 5000.0f), memory, tuning);
	CHECK(!during.warp);
	CHECK(during.refusal == CatchUpRefusal::Cooldown);

	const auto after = DecideCatchUp(FollowingContext(), ObservationAt(5.0f, 5000.0f, 5000.0f), memory, tuning);
	CHECK(after.warp);
}

// Given a recall that was performed
// When the memory is looked at
// Then the cooldown started, the progress clock and best lag were reset, the distance
// gate closed and the stuck trigger now waits for proof of movement.
TEST(TestCatchUpWarpResetsProgressAndClosesGates)
{
	auto memory = CalmMemory(10.0f);
	memory.bestLag = 80.0f;

	MarkCatchUpWarped(memory, 100.0f);
	CHECK_NEAR(memory.lastWarpAt, 100.0f, 0.001f);
	CHECK_NEAR(memory.lastAttemptAt, 100.0f, 0.001f);
	CHECK_NEAR(memory.lastProgressAt, 100.0f, 0.001f);
	CHECK(memory.bestLag > 1.0e8f);
	CHECK(memory.awaitingProgress);
	CHECK(!memory.distanceArmed);
	CHECK_NEAR(SecondsWithoutProgress(memory, 100.0f), 0.0f, 0.001f);

	const auto tuning = CatchSettings();

	// Two seconds after the recall the stuck clock has not refilled.
	const auto early = DecideCatchUp(FollowingContext(), ObservationAt(102.0f, 800.0f, 800.0f), memory, tuning);
	CHECK(!early.warp);
	CHECK(early.refusal == CatchUpRefusal::None);

	// Four seconds later the clock says "stuck", but the previous recall has not been
	// proven to work yet, so it is refused for that reason.
	const auto pending = DecideCatchUp(FollowingContext(), ObservationAt(104.0f, 800.0f, 800.0f), memory, tuning);
	CHECK(!pending.warp);
	CHECK(pending.refusal == CatchUpRefusal::AwaitingProgress);
}

// Given a recall that just happened
// When the summon really moves again and later gets stuck once more
// Then the stuck trigger is allowed again (after the cooldown).
TEST(TestCatchUpProgressRearmsStuckTrigger)
{
	const auto tuning = CatchSettings();
	auto memory = CalmMemory(0.0f);
	MarkCatchUpWarped(memory, 100.0f);

	// Real movement at 100.5 s clears the "awaiting progress" state.
	auto moving = ObservationAt(100.5f, 700.0f, 700.0f);
	moving.isMoving = true;
	ObserveCatchUp(memory, moving, tuning);
	CHECK(!memory.awaitingProgress);

	// Stuck again, but the recall cooldown (5 s from the warp at 100 s) is still running.
	const auto stillCooling = DecideCatchUp(FollowingContext(), ObservationAt(104.0f, 700.0f, 700.0f), memory, tuning);
	CHECK(!stillCooling.warp);
	CHECK(stillCooling.refusal == CatchUpRefusal::Cooldown);

	// 6.6 s after the recall the cooldown is over and the summon is stuck again.
	const auto later = DecideCatchUp(FollowingContext(), ObservationAt(106.6f, 700.0f, 700.0f), memory, tuning);
	CHECK(later.warp);
	CHECK(later.trigger == CatchUpTrigger::NoProgress);
}

// Given a summon that holds its slot, travels, or closes part of the gap
// When the observation runs
// Then every one of those counts as progress and restarts the stuck clock.
TEST(TestCatchUpObserveTracksProgress)
{
	const auto tuning = CatchSettings();

	CatchUpMemory memory{};
	memory.lastProgressAt = 0.0f;

	// First observation of a fresh record: the best lag is unknown, so this counts.
	ObserveCatchUp(memory, ObservationAt(10.0f, 500.0f, 500.0f), tuning);
	CHECK_NEAR(memory.lastProgressAt, 10.0f, 0.001f);
	CHECK_NEAR(memory.bestLag, 500.0f, 0.001f);

	// Nothing changed and the actor is not moving: the clock keeps running.
	ObserveCatchUp(memory, ObservationAt(11.5f, 500.0f, 500.0f), tuning);
	CHECK_NEAR(SecondsWithoutProgress(memory, 11.5f), 1.5f, 0.001f);

	// Travel is progress.
	auto moving = ObservationAt(12.0f, 500.0f, 500.0f);
	moving.isMoving = true;
	ObserveCatchUp(memory, moving, tuning);
	CHECK_NEAR(memory.lastProgressAt, 12.0f, 0.001f);

	// Closing the gap by more than the epsilon is progress too...
	ObserveCatchUp(memory, ObservationAt(13.0f, 440.0f, 440.0f), tuning);
	CHECK_NEAR(memory.lastProgressAt, 13.0f, 0.001f);
	CHECK_NEAR(memory.bestLag, 440.0f, 0.001f);

	// ...while a 10-unit improvement is not.
	ObserveCatchUp(memory, ObservationAt(14.0f, 430.0f, 430.0f), tuning);
	CHECK_NEAR(memory.lastProgressAt, 13.0f, 0.001f);

	// Holding the slot is not being stuck.
	ObserveCatchUp(memory, ObservationAt(15.0f, 50.0f, 50.0f), tuning);
	CHECK_NEAR(memory.lastProgressAt, 15.0f, 0.001f);
}

// Given a disarmed distance trigger
// When the owner distance drops to the exit threshold
// Then the gate re-arms, and it stays closed while outside it.
TEST(TestCatchUpObserveRearmsDistance)
{
	const auto tuning = CatchSettings();

	CatchUpMemory memory{};
	memory.lastProgressAt = 0.0f;
	memory.distanceArmed = false;

	ObserveCatchUp(memory, ObservationAt(1.0f, 2000.0f, 2000.0f), tuning);
	CHECK(!memory.distanceArmed);

	ObserveCatchUp(memory, ObservationAt(2.0f, 1740.0f, 1740.0f), tuning);
	CHECK(memory.distanceArmed);
}

// Given a new movement mode (or a fresh record)
// When the progress state is reset
// Then the clock and the gate start over, but the warp cooldown survives.
TEST(TestCatchUpProgressResetKeepsCooldown)
{
	CatchUpMemory memory{};
	MarkCatchUpWarped(memory, 50.0f);
	CHECK(!memory.distanceArmed);
	CHECK(memory.awaitingProgress);

	ResetCatchUpProgress(memory, 60.0f);
	CHECK(memory.distanceArmed);
	CHECK(!memory.awaitingProgress);
	CHECK_NEAR(memory.lastProgressAt, 60.0f, 0.001f);
	CHECK_NEAR(memory.lastAttemptAt, 50.0f, 0.001f);
	CHECK_NEAR(memory.lastWarpAt, 50.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Safety gates.
// ---------------------------------------------------------------------------

// Given an order that keeps the summon away from the player
// When the summon is far away and stuck
// Then it is never recalled: MoveTo, Attack and Guard keep walking.
TEST(TestCatchUpNeverRecallsForAwayOrders)
{
	const auto tuning = CatchSettings();
	const auto observation = ObservationAt(100.0f, 5000.0f, 9000.0f);
	const auto memory = CalmMemory(0.0f);

	for (const auto order : { SummonOrder::MoveTo, SummonOrder::Attack, SummonOrder::WaitGuard }) {
		auto context = FollowingContext();
		context.order = order;
		const auto decision = DecideCatchUp(context, observation, memory, tuning);
		CHECK(!decision.warp);
		CHECK(decision.refusal == CatchUpRefusal::OrderNotFollowOrReturn);
	}

	auto returning = FollowingContext();
	returning.order = SummonOrder::Return;
	const auto decision = DecideCatchUp(returning, observation, memory, tuning);
	CHECK(decision.warp);
	CHECK(decision.trigger == CatchUpTrigger::Distance);
}

// Given a summon the CombatPolicy lets fight
// When the recall conditions are met
// Then the fight keeps its locomotion; a passive summon (combat forbidden) may be
// recalled, and an explicit override permits even a fighting one.
TEST(TestCatchUpNeverRecallsInCombat)
{
	const auto tuning = CatchSettings();
	const auto observation = ObservationAt(100.0f, 5000.0f, 9000.0f);
	const auto memory = CalmMemory(0.0f);

	auto fighting = FollowingContext();
	fighting.summonInCombat = true;
	fighting.combatPermitted = true;
	const auto blocked = DecideCatchUp(fighting, observation, memory, tuning);
	CHECK(!blocked.warp);
	CHECK(blocked.refusal == CatchUpRefusal::SummonInCombat);

	auto passive = FollowingContext();
	passive.summonInCombat = true;
	passive.combatPermitted = false;
	CHECK(DecideCatchUp(passive, observation, memory, tuning).warp);

	auto overridden = tuning;
	overridden.allowWhileSummonInCombat = true;
	CHECK(DecideCatchUp(fighting, observation, memory, overridden).warp);
}

// Given a player who is fighting
// When the recall conditions are met
// Then the recall waits (unless the explicit override is on).
TEST(TestCatchUpNeverRecallsWhileOwnerFights)
{
	const auto tuning = CatchSettings();
	const auto observation = ObservationAt(100.0f, 5000.0f, 9000.0f);
	const auto memory = CalmMemory(0.0f);

	auto context = FollowingContext();
	context.ownerInCombat = true;
	const auto blocked = DecideCatchUp(context, observation, memory, tuning);
	CHECK(!blocked.warp);
	CHECK(blocked.refusal == CatchUpRefusal::OwnerInCombat);

	auto overridden = tuning;
	overridden.allowWhileOwnerInCombat = true;
	CHECK(DecideCatchUp(context, observation, memory, overridden).warp);
}

// Given the two actors in different loaded spaces (another cell, another worldspace, or
// an interior / exterior boundary)
// When the recall conditions are met
// Then no position write is attempted: the vanilla summon snap owns that case.
TEST(TestCatchUpRefusesAcrossSpace)
{
	const auto tuning = CatchSettings();
	const auto observation = ObservationAt(100.0f, 5000.0f, 9000.0f);
	const auto memory = CalmMemory(0.0f);

	auto context = FollowingContext();
	context.sameSpace = false;
	const auto decision = DecideCatchUp(context, observation, memory, tuning);
	CHECK(!decision.warp);
	CHECK(decision.refusal == CatchUpRefusal::DifferentSpace);
}

// Given a player who is in the air, swimming or on a horse
// When the recall conditions are met
// Then there is no trustworthy landing spot next to them and the recall waits.
TEST(TestCatchUpRefusesOwnerAirborneSwimmingMounted)
{
	const auto tuning = CatchSettings();
	const auto observation = ObservationAt(100.0f, 5000.0f, 9000.0f);
	const auto memory = CalmMemory(0.0f);

	const auto check = [&](bool CatchUpContext::*a_field, CatchUpRefusal a_expected) {
		auto context = FollowingContext();
		context.*a_field = true;
		const auto decision = DecideCatchUp(context, observation, memory, tuning);
		CHECK(!decision.warp);
		CHECK(decision.refusal == a_expected);
	};

	check(&CatchUpContext::ownerAirborne, CatchUpRefusal::OwnerAirborne);
	check(&CatchUpContext::ownerSwimming, CatchUpRefusal::OwnerSwimming);
	check(&CatchUpContext::ownerMounted, CatchUpRefusal::OwnerMounted);
}

// Given the operator switched the catch-up recall off
// When the recall conditions are met
// Then nothing is teleported and the refusal says why - while the trigger still reports
// what the race looked like (the statistics count that, not only performed warps).
TEST(TestCatchUpHonoursOperatorSwitch)
{
	auto tuning = CatchSettings();
	tuning.enableWarp = false;

	const auto decision = DecideCatchUp(FollowingContext(), ObservationAt(100.0f, 5000.0f, 9000.0f), CalmMemory(0.0f), tuning);
	CHECK(!decision.warp);
	CHECK(decision.refusal == CatchUpRefusal::Disabled);
	CHECK(decision.trigger == CatchUpTrigger::Distance);
}

// Given a refused attempt whose only armed measurement is the stuck clock
// When the decision is returned
// Then the trigger names the stuck clock, not the (closed) distance gate.
TEST(TestCatchUpTriggerNamesTheArmedMeasurement)
{
	const auto tuning = CatchSettings();
	auto memory = CalmMemory(0.0f);
	memory.distanceArmed = false; // hysteresis gate closed

	auto context = FollowingContext();
	context.ownerInCombat = true; // something refuses the recall

	// Only the distance measurement is raw here (the lag is inside the stuck band).
	const auto decision = DecideCatchUp(context, ObservationAt(100.0f, 200.0f, 5000.0f), memory, tuning);
	CHECK(!decision.warp);
	CHECK(decision.refusal == CatchUpRefusal::OwnerInCombat);
	CHECK(decision.trigger == CatchUpTrigger::Distance);

	// Now only the stuck measurement is raw (the owner is well inside the catch-up
	// distance), and it is the armed one, so it names the trigger.
	const auto near = DecideCatchUp(context, ObservationAt(100.0f, 800.0f, 800.0f), memory, tuning);
	CHECK(!near.warp);
	CHECK(near.refusal == CatchUpRefusal::OwnerInCombat);
	CHECK(near.trigger == CatchUpTrigger::NoProgress);
}

// Given the settings the driver already receives
// When the recall tunables are derived
// Then the operator switch and the three thresholds come from [Recovery], the rest from
// the documented defaults, and the recall distance never sits inside the sprint band.
TEST(TestCatchUpTuningFromSettings)
{
	auto settings = SettingsData{};
	settings.movement.followMinDistance = 200.0f;
	settings.movement.followMaxDistance = 450.0f;
	settings.recovery.enableCatchUpWarp = true;
	settings.recovery.catchUpDistance = 3500.0f;
	settings.recovery.stuckWarpSeconds = 3.0f;
	settings.recovery.stuckWarpMinDistance = 300.0f;

	const auto tuning = CatchUpTuningFrom(settings);
	CHECK(tuning.enableWarp);
	CHECK_NEAR(tuning.catchUpDistance, 3500.0f, 0.001f);
	CHECK_NEAR(tuning.stuckSeconds, 3.0f, 0.001f);
	CHECK_NEAR(tuning.stuckMinDistance, 300.0f, 0.001f);
	CHECK_NEAR(tuning.cooldownSeconds, 5.0f, 0.001f);
	CHECK_NEAR(tuning.landingDistance, 200.0f, 0.001f);
	CHECK(!tuning.allowWhileOwnerInCombat);
	CHECK(!tuning.allowWhileSummonInCombat);

	// A recall distance inside the sprint band is raised to it.
	auto narrow = settings;
	narrow.recovery.catchUpDistance = 500.0f;
	const auto narrowTuning = CatchUpTuningFrom(narrow);
	CHECK(narrowTuning.catchUpDistance >= FollowTuningFrom(narrow).sprintAtDistance);

	// 0 keeps the documented default; the operator switch is the way to turn it off.
	auto zero = settings;
	zero.recovery.catchUpDistance = 0.0f;
	CHECK_NEAR(CatchUpTuningFrom(zero).catchUpDistance, 3500.0f, 0.001f);

	auto disabled = settings;
	disabled.recovery.enableCatchUpWarp = false;
	CHECK(!CatchUpTuningFrom(disabled).enableWarp);
}

// ---------------------------------------------------------------------------
// Landing spot.
// ---------------------------------------------------------------------------

// Given an owner and a formation of one
// When the landing candidates are built
// Then the list starts behind the owner, every spot is behind or beside it, and the
// list contains no duplicate point.
TEST(TestCatchUpLandingCandidatesAreBehindAndDistinct)
{
	const auto tuning = CatchSettings();
	const Vec3 owner{ 100.0f, 200.0f, 30.0f };
	const float yaw = 0.0f;

	LandingCandidate candidates[kMaxLandingCandidates]{};
	const std::size_t count = BuildLandingCandidates(owner, yaw, 0, 1, FormationLayout{}, tuning, candidates, kMaxLandingCandidates);

	CHECK(count >= 5);
	CHECK(count <= kMaxLandingCandidates);

	const Vec3 forward{ std::sin(yaw), std::cos(yaw), 0.0f };
	for (std::size_t i = 0; i < count; ++i) {
		const Vec3 delta{ candidates[i].position.x - owner.x, candidates[i].position.y - owner.y, 0.0f };
		// Behind or beside: never in front of the owner.
		CHECK(delta.x * forward.x + delta.y * forward.y <= 0.5f);
		CHECK_NEAR(candidates[i].position.z, owner.z, 0.001f);
		CHECK(delta.Length() >= tuning.landingDistance * 0.9f);

		for (std::size_t j = i + 1; j < count; ++j) {
			CHECK(candidates[i].position.DistanceTo(candidates[j].position) > 25.0f);
		}
	}

	// The first spot of a one-summon squad is that summon's own formation slot (the
	// historical left-rear default), and the list also carries a "straight behind" spot.
	const Vec3 firstDelta{ candidates[0].position.x - owner.x, candidates[0].position.y - owner.y, 0.0f };
	CHECK(firstDelta.y < 0.0f);

	bool straightBehind = false;
	for (std::size_t i = 0; i < count; ++i) {
		const Vec3 delta{ candidates[i].position.x - owner.x, candidates[i].position.y - owner.y, 0.0f };
		if (std::abs(delta.x) <= 0.5f && std::abs(delta.y + tuning.landingDistance) <= 0.5f) {
			straightBehind = true;
		}
	}
	CHECK(straightBehind);
}

// Given a rotated owner
// When the candidates are built
// Then "behind" follows the owner's heading, not the world axes.
TEST(TestCatchUpLandingCandidatesFollowOwnerHeading)
{
	const auto tuning = CatchSettings();
	const Vec3 owner{ -400.0f, 700.0f, 12.0f };
	const float yaw = kPi / 2.0f;

	LandingCandidate candidates[kMaxLandingCandidates]{};
	const std::size_t count = BuildLandingCandidates(owner, yaw, 0, 1, FormationLayout{}, tuning, candidates, kMaxLandingCandidates);
	CHECK(count > 0);

	const Vec3 forward{ std::sin(yaw), std::cos(yaw), 0.0f };
	bool behindFound = false;
	for (std::size_t i = 0; i < count; ++i) {
		const Vec3 delta{ candidates[i].position.x - owner.x, candidates[i].position.y - owner.y, 0.0f };
		if (std::abs(delta.x * forward.x + delta.y * forward.y + tuning.landingDistance) <= 0.5f) {
			behindFound = true;
		}
	}
	CHECK(behindFound);
}

// Given a squad
// When the candidates are built
// Then the first one is the summon's own formation slot at the landing radius, so the
// recall drops it where the follow rules wanted it.
TEST(TestCatchUpLandingFirstCandidateIsFormationSlot)
{
	const auto tuning = CatchSettings();
	const Vec3 owner{ 50.0f, -20.0f, 0.0f };
	const float yaw = 0.4f;
	FormationLayout layout{};
	layout.outerRingCapacity = 6;

	for (std::size_t slot = 0; slot < 3; ++slot) {
		LandingCandidate candidates[kMaxLandingCandidates]{};
		const std::size_t count = BuildLandingCandidates(owner, yaw, slot, 3, layout, tuning, candidates, kMaxLandingCandidates);
		CHECK(count > 0);

		const Vec3 expected = OwnerLocalToWorldPoint(
			FormationSlotOffsetLocal(slot, 3, tuning.landingDistance, layout), owner, yaw);
		CHECK_NEAR(candidates[0].position.x, expected.x, 0.5f);
		CHECK_NEAR(candidates[0].position.y, expected.y, 0.5f);
	}
}

// Given an owner in a corridor
// When the list is truncated to the capacity the caller provides
// Then no more candidates are written than asked for.
TEST(TestCatchUpLandingCandidatesRespectCapacity)
{
	const auto tuning = CatchSettings();
	LandingCandidate candidates[kMaxLandingCandidates]{};
	for (std::size_t capacity = 1; capacity <= 3; ++capacity) {
		const std::size_t count = BuildLandingCandidates({ 0.0f, 0.0f, 0.0f }, 0.0f, 0, 1, FormationLayout{}, tuning, candidates, capacity);
		CHECK(count <= capacity);
	}
	CHECK(BuildLandingCandidates({ 0.0f, 0.0f, 0.0f }, 0.0f, 0, 1, FormationLayout{}, tuning, nullptr, 4) == 0);
}

// Given several candidates where the first is not on ground and the second is occupied
// When the spot is selected
// Then the first usable candidate wins and its probed ground height is used.
TEST(TestCatchUpSelectsFirstUsableSpot)
{
	const auto tuning = CatchSettings();
	LandingCandidate candidates[3]{ CandidateBehind(200.0f), CandidateBehind(360.0f), CandidateBehind(520.0f) };

	LandingObservation observations[3]{};
	observations[0] = GoodSpot();
	observations[0].groundFound = false;

	observations[1] = GoodSpot();
	observations[1].nearestActor = 50.0f;

	observations[2] = GoodSpot();
	observations[2].groundRise = -75.0f;

	const auto selection = SelectLandingSpot(candidates, observations, 3, tuning);
	CHECK(selection.found);
	CHECK(selection.index == 2);
	CHECK(selection.refusal == CatchUpRefusal::None);
	CHECK_NEAR(selection.position.x, candidates[2].position.x, 0.001f);
	CHECK_NEAR(selection.position.y, candidates[2].position.y, 0.001f);
	CHECK_NEAR(selection.position.z, candidates[2].position.z - 75.0f, 0.001f);
}

// Given every candidate failing a different safety check
// When each is tested
// Then it is refused.
TEST(TestCatchUpRejectsUnsafeSpots)
{
	const auto tuning = CatchSettings();

	const auto rejected = [&](const LandingObservation& a_observation) {
		return !IsLandingSpotUsable(a_observation, tuning);
	};

	CHECK(!rejected(GoodSpot()));

	auto noSpace = GoodSpot();
	noSpace.hasSpace = false;
	CHECK(rejected(noSpace));

	auto noGround = GoodSpot();
	noGround.groundFound = false;
	CHECK(rejected(noGround));

	auto blocked = GoodSpot();
	blocked.blocked = true;
	CHECK(rejected(blocked));

	auto tooHigh = GoodSpot();
	tooHigh.groundRise = tuning.landingMaxRise + 1.0f;
	CHECK(rejected(tooHigh));

	auto tooDeep = GoodSpot();
	tooDeep.groundRise = -tuning.landingMaxDrop - 1.0f;
	CHECK(rejected(tooDeep));

	auto underwater = GoodSpot();
	underwater.waterRise = tuning.landingWaterClearance - 1.0f;
	CHECK(rejected(underwater));

	auto occupied = GoodSpot();
	occupied.nearestActor = tuning.landingClearance - 1.0f;
	CHECK(rejected(occupied));

	// Exactly on the limits is allowed: the thresholds are inclusive.
	auto atLimits = GoodSpot();
	atLimits.groundRise = tuning.landingMaxRise;
	atLimits.nearestActor = tuning.landingClearance;
	atLimits.waterRise = tuning.landingWaterClearance;
	CHECK(!rejected(atLimits));
}

// Given no candidate that passes the checks
// When the recall would need a landing spot
// Then there is none: no "probably fine" fallback.
TEST(TestCatchUpRefusesWhenNoSpotIsSafe)
{
	const auto tuning = CatchSettings();
	LandingCandidate candidates[2]{ CandidateBehind(200.0f), CandidateBehind(360.0f) };
	LandingObservation observations[2]{};
	observations[0].hasSpace = false;
	observations[1] = GoodSpot();
	observations[1].blocked = true;

	const auto selection = SelectLandingSpot(candidates, observations, 2, tuning);
	CHECK(!selection.found);
	CHECK(selection.refusal == CatchUpRefusal::NoSafeSpot);
}

// ---------------------------------------------------------------------------
// Statistics.
// ---------------------------------------------------------------------------

// Given performed recalls and refused attempts
// When they are recorded
// Then the counters keep them per cause and the summary line names them.
TEST(TestCatchUpStatsCountersAndSummary)
{
	CatchUpStats stats{};
	RecordCatchUpTrigger(stats, CatchUpTrigger::Distance);
	RecordCatchUpTrigger(stats, CatchUpTrigger::NoProgress);
	RecordCatchUpTrigger(stats, CatchUpTrigger::None);
	RecordCatchUpWarp(stats, CatchUpTrigger::Distance, 42.0f);
	RecordCatchUpRefusal(stats, CatchUpRefusal::OwnerInCombat);
	RecordCatchUpRefusal(stats, CatchUpRefusal::OwnerInCombat);
	RecordCatchUpRefusal(stats, CatchUpRefusal::NoSafeSpot);

	CHECK(stats.distanceTriggers == 1);
	CHECK(stats.stuckTriggers == 1);
	CHECK(stats.warps == 1);
	CHECK_NEAR(stats.lastWarpAt, 42.0f, 0.001f);
	CHECK(stats.refusals[static_cast<std::size_t>(CatchUpRefusal::OwnerInCombat)] == 2);
	CHECK(stats.refusals[static_cast<std::size_t>(CatchUpRefusal::NoSafeSpot)] == 1);
	CHECK(stats.lastRefusal == CatchUpRefusal::NoSafeSpot);
	CHECK(stats.lastTrigger == CatchUpTrigger::Distance);

	const std::string summary = FormatCatchUpStats(stats);
	CHECK(summary.find("warps=1") != std::string::npos);
	CHECK(summary.find("triggers(distance=1, no-progress=1)") != std::string::npos);
	CHECK(summary.find("OwnerInCombat=2") != std::string::npos);
	CHECK(summary.find("NoSafeSpot=1") != std::string::npos);
	CHECK(summary.find("last=NoSafeSpot") != std::string::npos);
	CHECK(summary.find("Cooldown=") == std::string::npos);

	ResetCatchUpStats(stats);
	CHECK(stats.warps == 0);
	CHECK(stats.refusals[static_cast<std::size_t>(CatchUpRefusal::OwnerInCombat)] == 0);
	CHECK(stats.lastRefusal == CatchUpRefusal::None);
	CHECK(FormatCatchUpStats(stats) == "catch-up: warps=0 triggers(distance=0, no-progress=0)");
}
