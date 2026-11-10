#include "check.h"
#include "Core/CombatPolicy.h"
#include "Core/Formation.h"
#include "Core/MovementIntent.h"
#include "Core/Settings.h"

#include <cmath>
#include <numbers>

using namespace scf::core;

namespace
{
	constexpr float kPi = static_cast<float>(std::numbers::pi);

	SettingsData SettingsWithDistances(float a_followMin, float a_followMax)
	{
		SettingsData data{};
		data.movement.followMinDistance = a_followMin;
		data.movement.followMaxDistance = a_followMax;
		return data;
	}

	SummonModel OrderModel(SummonOrder a_order, AggressionMode a_aggression = AggressionMode::Defensive)
	{
		SummonModel model{};
		model.id = 0x10000;
		model.ownerId = 0x14;
		model.order = a_order;
		model.aggression = a_aggression;
		return model;
	}

	// Movement tests below are about geometry and order rules; combat is permitted
	// unless a test asks otherwise.
	MovementIntent IntentWithCombat(
		const SummonModel& a_model,
		bool a_inCombat,
		const SettingsData& a_settings,
		const Vec3& a_owner,
		const Vec3& a_summon,
		float a_yaw,
		std::size_t a_slot,
		std::size_t a_total)
	{
		return BuildMovementIntent(a_model, a_inCombat, true, a_settings, a_owner, a_summon, a_yaw, a_slot, a_total);
	}
}

TEST(TestFormationSlotOffsetsSitOnRadius)
{
	for (std::size_t total = 1; total <= 6; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto offset = FormationSlotOffsetLocal(slot, total, 200.0f);
			CHECK_NEAR(offset.Length(), 200.0f, 0.01f);
			CHECK_NEAR(offset.z, 0.0f, 0.001f);
		}
	}
}

TEST(TestFormationSlotsAreDistinct)
{
	for (std::size_t total = 2; total <= 6; ++total) {
		for (std::size_t a = 0; a < total; ++a) {
			for (std::size_t b = a + 1; b < total; ++b) {
				const auto first = FormationSlotOffsetLocal(a, total, 200.0f);
				const auto second = FormationSlotOffsetLocal(b, total, 200.0f);
				CHECK(first.DistanceTo(second) > 50.0f);
			}
		}
	}
}

TEST(TestFormationDefaultSlotIsBehindOwner)
{
	const auto single = FormationSlotOffsetLocal(0, 1, 200.0f);
	CHECK(single.y < 0.0f);

	const auto trioRear = FormationSlotOffsetLocal(2, 3, 200.0f);
	CHECK(trioRear.y < -190.0f);
	CHECK_NEAR(trioRear.x, 0.0f, 0.01f);
}

TEST(TestFormationTwoSlotsAreMirrored)
{
	const auto left = FormationSlotOffsetLocal(0, 2, 150.0f);
	const auto right = FormationSlotOffsetLocal(1, 2, 150.0f);
	CHECK(left.x < 0.0f);
	CHECK(right.x > 0.0f);
	CHECK_NEAR(left.x, -right.x, 0.01f);
	CHECK_NEAR(left.y, right.y, 0.01f);
}

TEST(TestOwnerLocalWorldOffsetRoundTrip)
{
	const float yaws[] = { 0.0f, 0.7f, kPi / 2.0f, -2.2f, kPi };
	const Vec3 locals[] = {
		{ 0.0f, 100.0f, 0.0f },
		{ -120.0f, -80.0f, 0.0f },
		{ 35.0f, -250.0f, 15.0f }
	};

	for (const float yaw : yaws) {
		for (const auto& local : locals) {
			const auto world = OwnerLocalToWorldOffset(local, yaw);
			const auto back = WorldToOwnerLocalOffset(world, yaw);
			CHECK_NEAR(back.x, local.x, 0.01f);
			CHECK_NEAR(back.y, local.y, 0.01f);
			CHECK_NEAR(back.z, local.z, 0.01f);
		}
	}
}

TEST(TestOwnerWorldPointRoundTrip)
{
	const Vec3 ownerPosition{ 1200.0f, -450.0f, 76.0f };
	const float yaw = 1.1f;
	const Vec3 destination{ -800.0f, 640.0f, 12.0f };

	const auto local = WorldPointToOwnerLocal(destination, ownerPosition, yaw);
	const auto back = OwnerLocalToWorldPoint(local, ownerPosition, yaw);

	CHECK_NEAR(back.x, destination.x, 0.05f);
	CHECK_NEAR(back.y, destination.y, 0.05f);
	CHECK_NEAR(back.z, destination.z, 0.05f);
}

TEST(TestOwnerLocalWorldOffsetMatchesSquadConvention)
{
	const float yawDeg = 35.0f;
	const float yawRad = yawDeg * kPi / 180.0f;
	const float bearingDeg = -130.0f;
	const float bearingRad = bearingDeg * kPi / 180.0f;
	const float radius = 150.0f;

	const Vec3 local{ std::sin(bearingRad) * radius, std::cos(bearingRad) * radius, 0.0f };
	const auto world = OwnerLocalToWorldOffset(local, yawRad);

	const float heading = yawRad + bearingRad;
	CHECK_NEAR(world.x, std::sin(heading) * radius, 0.01f);
	CHECK_NEAR(world.y, std::cos(heading) * radius, 0.01f);
}

TEST(TestFacingOffsetDegreesCardinalDirections)
{
	const Vec3 actor{ 0.0f, 0.0f, 0.0f };
	CHECK_NEAR(FacingOffsetDegrees(actor, { 0.0f, 500.0f, 0.0f }, 0.0f), 0.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { 500.0f, 0.0f, 0.0f }, 0.0f), 90.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { 0.0f, -500.0f, 0.0f }, 0.0f), 180.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { -500.0f, 0.0f, 0.0f }, 0.0f), -90.0f, 0.1f);
}

TEST(TestFacingOffsetDegreesIsRelativeToOwnerHeading)
{
	const float yaw = kPi / 2.0f;
	const Vec3 actor{ 0.0f, 0.0f, 0.0f };
	CHECK_NEAR(FacingOffsetDegrees(actor, { 500.0f, 0.0f, 0.0f }, yaw), 0.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { 0.0f, 500.0f, 0.0f }, yaw), -90.0f, 0.1f);
}

TEST(TestFacingOffsetDegreesDegenerateIsZero)
{
	CHECK_NEAR(FacingOffsetDegrees({ 10.0f, 10.0f, 0.0f }, { 10.0f, 10.0f, 0.0f }, 1.0f), 0.0f, 0.001f);
}

TEST(TestMovementIntentGuardHoldsPositionEvenInCombat)
{
	auto model = OrderModel(SummonOrder::WaitGuard);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = IntentWithCombat(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::HoldPosition);
	CHECK(intent.lockTranslation == true);
	CHECK_NEAR(intent.localOffset.Length(), 0.0f, 0.001f);
}

TEST(TestMovementIntentAttackOverridesEveryMovementOrder)
{
	auto model = OrderModel(SummonOrder::Attack);
	model.currentTargetId = 0xBEEF;
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const auto intent = IntentWithCombat(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(intent.mode == MovementMode::None);
	CHECK(intent.lockTranslation == false);

	const auto fresh = IntentWithCombat(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(fresh.mode == MovementMode::None);
}

TEST(TestMovementIntentAttackFallsBackToFollowWhenCombatForbidden)
{
	// Passive aggression: the CombatPolicy forbids combat, so an Attack order must not
	// leave the summon standing with no movement authority (it keeps following).
	auto model = OrderModel(SummonOrder::Attack, AggressionMode::Passive);
	model.currentTargetId = 0xBEEF;
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const auto intent = BuildMovementIntent(model, true, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(intent.mode == MovementMode::FollowSlot);
}

TEST(TestMovementIntentFollowKeepsSlotWhenCombatForbidden)
{
	// Passive / Return / MoveTo policy: even if the engine still reports combat, the
	// formation must win (otherwise a passive summon stands and lets the engine drag it).
	auto model = OrderModel(SummonOrder::Follow);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const auto forbidden = BuildMovementIntent(model, true, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(forbidden.mode == MovementMode::FollowSlot);

	const auto permitted = IntentWithCombat(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(permitted.mode == MovementMode::None);
}

TEST(TestMovementIntentReturnRunsToOwner)
{
	auto model = OrderModel(SummonOrder::Return);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = IntentWithCombat(model, false, settings, { 0, 0, 0 }, { 600.0f, 0.0f, 0.0f }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::ReturnToOwner);
	CHECK(intent.lockTranslation == false);
	CHECK_NEAR(intent.localOffset.Length(), 0.0f, 0.001f);
	CHECK(intent.followRadius >= 150.0f);
	CHECK(intent.catchUpRadius > intent.followRadius);
	CHECK_NEAR(intent.facingOffsetDegrees, -90.0f, 0.5f);
}

TEST(TestMovementIntentFollowUsesFormationSlot)
{
	auto model = OrderModel(SummonOrder::Follow);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const Vec3 owner{ 0.0f, 0.0f, 0.0f };
	const auto probe = IntentWithCombat(model, false, settings, owner, owner, 0.0f, 0, 1);
	CHECK(probe.mode == MovementMode::FollowSlot);
	CHECK(probe.lockTranslation == false);
	CHECK_NEAR(probe.localOffset.Length(), 200.0f, 0.01f);
	CHECK(probe.followRadius > 0.0f);
	CHECK(probe.catchUpRadius > probe.followRadius);

	const auto slotWorld = OwnerLocalToWorldPoint(probe.localOffset, owner, 0.0f);
	const auto intent = IntentWithCombat(model, false, settings, owner, slotWorld, 0.0f, 0, 1);
	CHECK_NEAR(intent.facingOffsetDegrees, 0.0f, 0.5f);
}

TEST(TestMovementIntentFollowFacesTravelWhenAwayFromSlot)
{
	auto model = OrderModel(SummonOrder::Follow);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const Vec3 owner{ 0.0f, 0.0f, 0.0f };
	const Vec3 summon{ 400.0f, 0.0f, 0.0f };
	const auto intent = IntentWithCombat(model, false, settings, owner, summon, 0.0f, 0, 1);

	const auto slotWorld = OwnerLocalToWorldPoint(intent.localOffset, owner, 0.0f);
	CHECK(std::abs(intent.facingOffsetDegrees) > 60.0f);
	const float expected = std::atan2(slotWorld.x - summon.x, slotWorld.y - summon.y) * 180.0f / kPi;
	float delta = expected - intent.facingOffsetDegrees;
	while (delta > 180.0f) delta -= 360.0f;
	while (delta < -180.0f) delta += 360.0f;
	CHECK_NEAR(delta, 0.0f, 0.5f);
}

TEST(TestMovementIntentFollowYieldsToCombat)
{
	auto model = OrderModel(SummonOrder::Follow);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const auto inCombat = IntentWithCombat(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(inCombat.mode == MovementMode::None);

	model.currentTargetId = 0xDEAD;
	const auto withStaleTarget = IntentWithCombat(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(withStaleTarget.mode == MovementMode::FollowSlot);
}

TEST(TestMovementIntentMoveToKeepsWorldDestination)
{
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = true;
	model.moveTargetPosition = { -700.0f, 900.0f, 40.0f };

	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const Vec3 firstOwner{ 100.0f, 100.0f, 0.0f };
	const auto firstIntent = IntentWithCombat(model, false, settings, firstOwner, firstOwner, 0.3f, 0, 1);
	CHECK(firstIntent.mode == MovementMode::MoveToPoint);
	CHECK(firstIntent.localOffset.Length() > 0.0f);

	const auto firstWorld = OwnerLocalToWorldPoint(firstIntent.localOffset, firstOwner, 0.3f);
	CHECK_NEAR(firstWorld.x, model.moveTargetPosition.x, 0.05f);
	CHECK_NEAR(firstWorld.y, model.moveTargetPosition.y, 0.05f);

	const Vec3 secondOwner{ 500.0f, -300.0f, 0.0f };
	const auto secondIntent = IntentWithCombat(model, false, settings, secondOwner, secondOwner, 2.4f, 0, 1);
	const auto secondWorld = OwnerLocalToWorldPoint(secondIntent.localOffset, secondOwner, 2.4f);
	CHECK_NEAR(secondWorld.x, model.moveTargetPosition.x, 0.05f);
	CHECK_NEAR(secondWorld.y, model.moveTargetPosition.y, 0.05f);
}

TEST(TestMovementIntentMoveToIgnoresStaleDestination)
{
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = true;
	model.moveTargetPosition = { 70000.0f, -40000.0f, 0.0f };

	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = IntentWithCombat(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::FollowSlot);
}

TEST(TestMovementIntentMoveToWithoutDestinationFallsBackToFollow)
{
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = false;

	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = IntentWithCombat(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::FollowSlot);
}

TEST(TestMoveToArrivalRadiusIsHorizontal)
{
	const Vec3 summon{ 100.0f, 100.0f, 0.0f };
	CHECK(IsWithinArrivalRadius(summon, { 240.0f, 100.0f, 0.0f }));
	CHECK(!IsWithinArrivalRadius(summon, { 300.0f, 100.0f, 0.0f }));
	CHECK(IsWithinArrivalRadius(summon, { 240.0f, 100.0f, 900.0f }));
}

// Given an order that keeps the summon away from the player (MoveTo point, Attack, Guard)
// When the movement driver runs
// Then the vanilla FollowTo leash is released; Follow / Return / a MoveTo without a
// destination keep it.
TEST(TestFollowLeashReleasedOnlyForAwayOrders)
{
	SummonModel model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = true;
	CHECK(ReleasesFollowLeash(model));
	model.hasMoveTarget = false;
	CHECK(!ReleasesFollowLeash(model));

	CHECK(ReleasesFollowLeash(OrderModel(SummonOrder::Attack)));
	CHECK(ReleasesFollowLeash(OrderModel(SummonOrder::WaitGuard)));
	CHECK(!ReleasesFollowLeash(OrderModel(SummonOrder::Follow)));
	CHECK(!ReleasesFollowLeash(OrderModel(SummonOrder::Return)));
}

// Given the summon's running package as the field showed it (runtime FormID 0xFF......,
// type 18, FollowTo's two radius floats, no templateParent)
// Then it is recognised as the follow package; a plugin package, another type or a
// different input count is not.
TEST(TestSummonFollowPackageShape)
{
	CHECK(IsSummonFollowPackageShape(18, 0xFF0019FAu, 2));
	CHECK(!IsSummonFollowPackageShape(18, 0x00025E6Du, 2));
	CHECK(!IsSummonFollowPackageShape(1, 0xFF0019FAu, 2));
	CHECK(!IsSummonFollowPackageShape(18, 0xFF0019FAu, 3));
	CHECK(!IsSummonFollowPackageShape(18, 0xFF0019FAu, 0));
}

// ---------------------------------------------------------------------------
// CombatPolicy: the "no second behaviour pattern" contract.
// ---------------------------------------------------------------------------

TEST(TestCombatPolicyPassiveNeverFights)
{
	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	(void)settings;

	for (const auto order : { SummonOrder::Follow, SummonOrder::WaitGuard, SummonOrder::Return, SummonOrder::MoveTo, SummonOrder::Attack }) {
		auto model = OrderModel(order, AggressionMode::Passive);
		model.currentTargetId = 0x1234;
		model.hasMoveTarget = true;

		const auto policy = BuildCombatPolicy(model, true);
		CHECK(policy.allowCombat == false);
		CHECK(policy.disableAttacks == true);
		CHECK(policy.restrictToTarget == false);
	}
}

TEST(TestCombatPolicyReturnForbidsCombatUntilArrival)
{
	auto model = OrderModel(SummonOrder::Return);
	model.currentTargetId = 0x1234;

	const auto policy = BuildCombatPolicy(model, true);
	CHECK(policy.allowCombat == false);
	CHECK(policy.disableAttacks == true);
}

TEST(TestCombatPolicyMoveToForbidsCombatWhileMoving)
{
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = true;

	CHECK(BuildCombatPolicy(model, false).allowCombat == false);
	CHECK(BuildCombatPolicy(model, false).disableAttacks == true);

	// No destination stored: the order falls back to following, so combat is allowed.
	model.hasMoveTarget = false;
	CHECK(BuildCombatPolicy(model, false).allowCombat == true);
}

TEST(TestCombatPolicyAttackRestrictsToFocusedTarget)
{
	auto model = OrderModel(SummonOrder::Attack);
	model.currentTargetId = 0xABC;

	const auto policy = BuildCombatPolicy(model, true);
	CHECK(policy.allowCombat == true);
	CHECK(policy.restrictToTarget == true);
	CHECK(policy.target == 0xABC);
	CHECK(policy.disableAttacks == false);

	// No live target: combat stays forbidden until the order reverts.
	const auto noTarget = BuildCombatPolicy(model, false);
	CHECK(noTarget.allowCombat == false);
	CHECK(noTarget.disableAttacks == true);
}

TEST(TestCombatPolicyFollowAndGuardAllowCombat)
{
	auto follow = OrderModel(SummonOrder::Follow);
	CHECK(BuildCombatPolicy(follow, false).allowCombat == true);
	CHECK(BuildCombatPolicy(follow, false).restrictToTarget == false);

	auto guard = OrderModel(SummonOrder::WaitGuard);
	CHECK(BuildCombatPolicy(guard, false).allowCombat == true);

	auto aggressive = OrderModel(SummonOrder::Follow, AggressionMode::Aggressive);
	CHECK(BuildCombatPolicy(aggressive, false).allowCombat == true);
}
