#include "check.h"
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

	SummonModel OrderModel(SummonOrder a_order)
	{
		SummonModel model{};
		model.id = 0x10000;
		model.ownerId = 0x14;
		model.order = a_order;
		return model;
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
	// Single summon and the third of a trio are rear slots (the flank slots of a
	// four-summon squad sit slightly forward by design, so this is asserted only for
	// the slots that are documented as rear positions).
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
	// SquadCoordinator historically produced world = (sin(yaw + bearing), cos(yaw + bearing))
	// for a bearing measured from "in front" towards the owner's right.
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
	// Owner facing north (yaw 0): goal straight ahead -> 0, right -> +90, behind -> 180.
	const Vec3 actor{ 0.0f, 0.0f, 0.0f };
	CHECK_NEAR(FacingOffsetDegrees(actor, { 0.0f, 500.0f, 0.0f }, 0.0f), 0.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { 500.0f, 0.0f, 0.0f }, 0.0f), 90.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { 0.0f, -500.0f, 0.0f }, 0.0f), 180.0f, 0.1f);
	CHECK_NEAR(FacingOffsetDegrees(actor, { -500.0f, 0.0f, 0.0f }, 0.0f), -90.0f, 0.1f);
}

TEST(TestFacingOffsetDegreesIsRelativeToOwnerHeading)
{
	// Owner facing east (yaw 90 degrees): a goal straight east is "ahead" (0 degrees),
	// a goal straight north is to the owner's left (-90).
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
	const auto intent = BuildMovementIntent(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::HoldPosition);
	CHECK(intent.lockTranslation == true);
	CHECK_NEAR(intent.localOffset.Length(), 0.0f, 0.001f);
}

TEST(TestMovementIntentAttackOverridesEveryMovementOrder)
{
	auto model = OrderModel(SummonOrder::Attack);
	model.currentTargetId = 0xBEEF;
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	// Even with a guard hold and an explicit combat target, Attack means "combat AI drives".
	const auto intent = BuildMovementIntent(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(intent.mode == MovementMode::None);
	CHECK(intent.lockTranslation == false);

	// ... and it is not turned into a follow order when not in combat yet (the engine
	// reports combat one tick later).
	const auto fresh = BuildMovementIntent(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(fresh.mode == MovementMode::None);
}

TEST(TestMovementIntentReturnRunsToOwner)
{
	auto model = OrderModel(SummonOrder::Return);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = BuildMovementIntent(model, false, settings, { 0, 0, 0 }, { 600.0f, 0.0f, 0.0f }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::ReturnToOwner);
	CHECK(intent.lockTranslation == false);
	CHECK_NEAR(intent.localOffset.Length(), 0.0f, 0.001f);
	CHECK(intent.followRadius >= 150.0f);
	CHECK(intent.catchUpRadius > intent.followRadius);
	// The summon stands east of the owner and must face west to walk home: -90 degrees.
	CHECK_NEAR(intent.facingOffsetDegrees, -90.0f, 0.5f);
}

TEST(TestMovementIntentFollowUsesFormationSlot)
{
	auto model = OrderModel(SummonOrder::Follow);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const Vec3 owner{ 0.0f, 0.0f, 0.0f };
	const auto probe = BuildMovementIntent(model, false, settings, owner, owner, 0.0f, 0, 1);
	CHECK(probe.mode == MovementMode::FollowSlot);
	CHECK(probe.lockTranslation == false);
	CHECK_NEAR(probe.localOffset.Length(), 200.0f, 0.01f);
	CHECK(probe.followRadius > 0.0f);
	CHECK(probe.catchUpRadius > probe.followRadius);

	// Standing on the slot itself: face where the owner faces (no moonwalking).
	const auto slotWorld = OwnerLocalToWorldPoint(probe.localOffset, owner, 0.0f);
	const auto intent = BuildMovementIntent(model, false, settings, owner, slotWorld, 0.0f, 0, 1);
	CHECK_NEAR(intent.facingOffsetDegrees, 0.0f, 0.5f);
}

TEST(TestMovementIntentFollowFacesTravelWhenAwayFromSlot)
{
	auto model = OrderModel(SummonOrder::Follow);
	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	// Slot for a single summon is left-rear of the owner at 200 units: offset
	// (-128.6, -153.2). A summon standing 400 units east of the owner must face
	// towards that slot, i.e. roughly west-south-west, not the owner's heading.
	const Vec3 owner{ 0.0f, 0.0f, 0.0f };
	const Vec3 summon{ 400.0f, 0.0f, 0.0f };
	const auto intent = BuildMovementIntent(model, false, settings, owner, summon, 0.0f, 0, 1);

	const auto slotWorld = OwnerLocalToWorldPoint(intent.localOffset, owner, 0.0f);
	CHECK(std::abs(intent.facingOffsetDegrees) > 60.0f);
	// Sanity: the facing points at the slot (world heading of the direction from the summon).
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

	const auto inCombat = BuildMovementIntent(model, true, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(inCombat.mode == MovementMode::None);

	// A model target that is no longer in combat must not freeze the follow order.
	model.currentTargetId = 0xDEAD;
	const auto withStaleTarget = BuildMovementIntent(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);
	CHECK(withStaleTarget.mode == MovementMode::FollowSlot);
}

TEST(TestMovementIntentMoveToKeepsWorldDestination)
{
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = true;
	model.moveTargetPosition = { -700.0f, 900.0f, 40.0f };

	const auto settings = SettingsWithDistances(200.0f, 450.0f);

	const Vec3 firstOwner{ 100.0f, 100.0f, 0.0f };
	const auto firstIntent = BuildMovementIntent(model, false, settings, firstOwner, firstOwner, 0.3f, 0, 1);
	CHECK(firstIntent.mode == MovementMode::MoveToPoint);
	CHECK(firstIntent.localOffset.Length() > 0.0f);

	const auto firstWorld = OwnerLocalToWorldPoint(firstIntent.localOffset, firstOwner, 0.3f);
	CHECK_NEAR(firstWorld.x, model.moveTargetPosition.x, 0.05f);
	CHECK_NEAR(firstWorld.y, model.moveTargetPosition.y, 0.05f);

	// The owner moved and turned: the recomputed offset must still name the same point.
	const Vec3 secondOwner{ 500.0f, -300.0f, 0.0f };
	const auto secondIntent = BuildMovementIntent(model, false, settings, secondOwner, secondOwner, 2.4f, 0, 1);
	const auto secondWorld = OwnerLocalToWorldPoint(secondIntent.localOffset, secondOwner, 2.4f);
	CHECK_NEAR(secondWorld.x, model.moveTargetPosition.x, 0.05f);
	CHECK_NEAR(secondWorld.y, model.moveTargetPosition.y, 0.05f);
}

TEST(TestMovementIntentMoveToIgnoresStaleDestination)
{
	// A destination left behind in another cell / by a teleport must not produce a
	// huge keep-offset goal; the order falls back to following the owner.
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = true;
	model.moveTargetPosition = { 70000.0f, -40000.0f, 0.0f };

	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = BuildMovementIntent(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::FollowSlot);
}

TEST(TestMovementIntentMoveToWithoutDestinationFallsBackToFollow)
{
	auto model = OrderModel(SummonOrder::MoveTo);
	model.hasMoveTarget = false;

	const auto settings = SettingsWithDistances(200.0f, 450.0f);
	const auto intent = BuildMovementIntent(model, false, settings, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f, 0, 1);

	CHECK(intent.mode == MovementMode::FollowSlot);
}

TEST(TestMoveToArrivalRadiusIsHorizontal)
{
	const Vec3 summon{ 100.0f, 100.0f, 0.0f };
	CHECK(IsWithinArrivalRadius(summon, { 240.0f, 100.0f, 0.0f }));
	CHECK(!IsWithinArrivalRadius(summon, { 300.0f, 100.0f, 0.0f }));
	// Height difference does not keep a summon "not arrived" on stairs or slopes.
	CHECK(IsWithinArrivalRadius(summon, { 240.0f, 100.0f, 900.0f }));
}
