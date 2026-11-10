#include "check.h"
#include "Core/Assist.h"

using namespace scf::core;

namespace
{
	constexpr EntityID kEnemy = 0x000F31C4;
	constexpr EntityID kOtherEnemy = 0x000F31C0;

	SummonModel Model(SummonOrder a_order, AggressionMode a_aggression = AggressionMode::Defensive)
	{
		SummonModel model{};
		model.id = 0xFF000001;
		model.ownerId = 0x14;
		model.order = a_order;
		model.aggression = a_aggression;
		return model;
	}
}

// Given a following summon
// When the player hits an enemy
// Then the summon focuses that enemy
TEST(TestAssistFollowJoinsPlayersFight)
{
	CHECK(ShouldAssistPlayerHit(Model(SummonOrder::Follow), kEnemy));
	CHECK(ShouldAssistPlayerHit(Model(SummonOrder::Follow, AggressionMode::Aggressive), kEnemy));
}

// Given a summon already attacking, a different target switches it; the same one is not re-issued.
TEST(TestAssistAttackSwitchesOnlyOnNewTarget)
{
	SummonModel model = Model(SummonOrder::Attack);
	model.currentTargetId = kEnemy;
	CHECK(!ShouldAssistPlayerHit(model, kEnemy));
	CHECK(ShouldAssistPlayerHit(model, kOtherEnemy));
}

// Given Passive or an explicit movement / hold order, the assist never overrides it.
TEST(TestAssistRespectsPassiveAndExplicitOrders)
{
	CHECK(!ShouldAssistPlayerHit(Model(SummonOrder::Follow, AggressionMode::Passive), kEnemy));
	CHECK(!ShouldAssistPlayerHit(Model(SummonOrder::Return), kEnemy));
	CHECK(!ShouldAssistPlayerHit(Model(SummonOrder::MoveTo), kEnemy));
	CHECK(!ShouldAssistPlayerHit(Model(SummonOrder::WaitGuard), kEnemy));
	CHECK(!ShouldAssistPlayerHit(Model(SummonOrder::Follow), InvalidEntityID));
}

// Given an aggression radius around the owner, only nearby enemies may be engaged; 0 disables it.
TEST(TestAggressionRadiusAroundOwner)
{
	const Vec3 owner{ 0.0f, 0.0f, 0.0f };
	CHECK(IsWithinAggressionRadius(owner, { 1000.0f, 0.0f, 0.0f }, 1500.0f));
	CHECK(IsWithinAggressionRadius(owner, { 1000.0f, 0.0f, 5000.0f }, 1500.0f));  // horizontal only
	CHECK(!IsWithinAggressionRadius(owner, { 1200.0f, 1200.0f, 0.0f }, 1500.0f));
	CHECK(IsWithinAggressionRadius(owner, { 90000.0f, 0.0f, 0.0f }, 0.0f));
}
