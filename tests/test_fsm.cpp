#include "check.h"
#include "Core/StateMachine.h"

#include <string>
#include <vector>

using namespace scf::core;

struct TransitionRecord
{
	EntityID id;
	SummonState from;
	SummonState to;
	std::string reason;
};

TEST(TestInitialStateAndFollow)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.order = SummonOrder::Follow;
	model.state = SummonState::Idle;

	DecisionContext ctx;
	ctx.distanceToOwner = 100.0f;
	ctx.hasTarget = false;

	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::FollowOwner);
}

TEST(TestWaitGuardOrder)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.order = SummonOrder::WaitGuard;
	model.state = SummonState::FollowOwner;

	DecisionContext ctx;
	ctx.distanceToOwner = 100.0f;

	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::Guard);
}

TEST(TestPassiveModeIgnoresEnemies)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.order = SummonOrder::Follow;
	model.state = SummonState::FollowOwner;
	model.aggression = AggressionMode::Passive;

	DecisionContext ctx;
	ctx.distanceToOwner = 100.0f;
	ctx.hasTarget = true;
	ctx.detectedTargetId = 42;
	ctx.distanceToTarget = 200.0f;

	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::FollowOwner);
	CHECK(model.currentTargetId == InvalidEntityID);
}

TEST(TestAggressiveModeEngagesTarget)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.order = SummonOrder::Follow;
	model.state = SummonState::FollowOwner;
	model.aggression = AggressionMode::Aggressive;

	DecisionContext ctx;
	ctx.distanceToOwner = 150.0f;
	ctx.hasTarget = true;
	ctx.detectedTargetId = 100;
	ctx.distanceToTarget = 800.0f; // farther than engage range (300)

	// 1. Detect -> AcquireTarget
	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::AcquireTarget);
	CHECK(model.currentTargetId == 100);

	// 2. Next tick: target > 300 -> Approach
	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::Approach);

	// 3. Move closer: target <= 300 -> Engage
	ctx.distanceToTarget = 250.0f;
	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::Engage);
}

TEST(TestTargetLostDebounce)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.state = SummonState::Engage;
	model.aggression = AggressionMode::Aggressive;
	model.currentTargetId = 100;

	DecisionContext ctx;
	ctx.distanceToOwner = 200.0f;
	ctx.followDistanceThreshold = 500.0f;
	ctx.targetLostDebounceTime = 1.0f;
	ctx.hasTarget = false; // Target lost!

	// Tick 0.4s: debounce not expired, should still stay in Engage
	fsm.Update(model, ctx, 0.4f);
	CHECK(model.state == SummonState::Engage);
	CHECK_NEAR(model.targetLostTime, 0.4f, 0.001f);

	// Another 0.4s (total 0.8s): still in Engage
	fsm.Update(model, ctx, 0.4f);
	CHECK(model.state == SummonState::Engage);

	// Another 0.3s (total 1.1s > 1.0s debounce): disengage!
	fsm.Update(model, ctx, 0.3f);
	CHECK(model.state == SummonState::FollowOwner);
	CHECK(model.currentTargetId == InvalidEntityID);
}

TEST(TestLeashBreakForcesReturn)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.state = SummonState::Engage;
	model.aggression = AggressionMode::Aggressive;
	model.currentTargetId = 100;

	DecisionContext ctx;
	ctx.returnDistanceThreshold = 2500.0f;
	ctx.followDistanceThreshold = 500.0f;
	ctx.hasTarget = true;
	ctx.distanceToTarget = 100.0f;
	ctx.distanceToOwner = 3000.0f; // Owner ran far away!

	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::Return);
	CHECK(model.currentTargetId == InvalidEntityID);

	// While returning, summon reaches owner range
	ctx.distanceToOwner = 400.0f;
	ctx.hasTarget = false;
	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::FollowOwner);
}

TEST(TestExplicitReturnOrder)
{
	StateMachine fsm;
	SummonModel model;
	model.id = 1;
	model.state = SummonState::Engage;
	model.order = SummonOrder::Return;

	DecisionContext ctx;
	ctx.distanceToOwner = 1200.0f;
	ctx.followDistanceThreshold = 400.0f;

	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::Return);

	// Arrives at owner
	ctx.distanceToOwner = 350.0f;
	fsm.Update(model, ctx, 0.1f);
	CHECK(model.state == SummonState::FollowOwner);
	CHECK(model.order == SummonOrder::Follow);
}

TEST(TestLogCallbackRecordsTransitions)
{
	std::vector<TransitionRecord> records;
	StateMachine fsm([&records](EntityID id, SummonState from, SummonState to, const char* reason) {
		records.push_back({ id, from, to, reason });
	});

	SummonModel model;
	model.id = 77;
	model.state = SummonState::Idle;
	model.order = SummonOrder::Follow;

	DecisionContext ctx;
	ctx.distanceToOwner = 50.0f;
	fsm.Update(model, ctx, 0.1f);

	CHECK(records.size() == 1);
	CHECK(records[0].id == 77);
	CHECK(records[0].from == SummonState::Idle);
	CHECK(records[0].to == SummonState::FollowOwner);
	CHECK(records[0].reason == "Order: Follow");
}
