#include "AI/TargetSelector.h"
#include "Core/DecisionContext.h"
#include "Core/StateMachine.h"
#include "Core/StealthContext.h"
#include "check.h"

#include <string>
#include <vector>

using namespace scf::core;
using namespace scf::ai;

namespace
{
	TargetCandidate MakeHostile(EntityID a_id, float a_x, float a_y)
	{
		TargetCandidate c{};
		c.id = a_id;
		c.position = Vec3{ a_x, a_y, 0.0f };
		c.isHostileToOwner = true;
		c.isHostileToSummon = true;
		return c;
	}
}

TEST(stealth_undetected_defensive_suppresses_unaware_enemy)
{
	std::vector<std::string> logs;
	TargetSelector selector([&logs](EntityID, EntityID, const char* reason) {
		logs.push_back(reason);
	});

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.aggression = AggressionMode::Defensive;
	summon.lastPosition = Vec3{ 0.0f, 0.0f, 0.0f };

	std::vector<TargetCandidate> candidates{
		MakeHostile(200, 300.0f, 0.0f)
	};

	StealthContext stealth{};
	stealth.playerSneaking = true;
	stealth.playerDetected = false;
	stealth.explicitAttackOrder = false;

	const EntityID chosen = selector.SelectTarget(summon, candidates, InvalidEntityID, {}, stealth);
	CHECK(chosen == InvalidEntityID);
	CHECK(!logs.empty());
	CHECK(logs.back() == "Stealth mode: player undetected, holding fire");
}

TEST(stealth_detected_engages_threat)
{
	TargetSelector selector{};

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.aggression = AggressionMode::Defensive;
	summon.lastPosition = Vec3{ 0.0f, 0.0f, 0.0f };

	std::vector<TargetCandidate> candidates{
		MakeHostile(200, 300.0f, 0.0f)
	};

	StealthContext stealth{};
	stealth.playerSneaking = true;
	stealth.playerDetected = true; // Player has been detected!
	stealth.explicitAttackOrder = false;

	const EntityID chosen = selector.SelectTarget(summon, candidates, InvalidEntityID, {}, stealth);
	CHECK(chosen == 200);
}

TEST(stealth_undetected_enemy_attacks_engages)
{
	TargetSelector selector{};

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.aggression = AggressionMode::Defensive;
	summon.lastPosition = Vec3{ 0.0f, 0.0f, 0.0f };

	auto candidate = MakeHostile(200, 300.0f, 0.0f);
	candidate.isAttackingOwner = true; // Active threat!

	std::vector<TargetCandidate> candidates{ candidate };

	StealthContext stealth{};
	stealth.playerSneaking = true;
	stealth.playerDetected = false;
	stealth.explicitAttackOrder = false;

	const EntityID chosen = selector.SelectTarget(summon, candidates, InvalidEntityID, {}, stealth);
	CHECK(chosen == 200);
}

TEST(stealth_undetected_explicit_focus_target_overrides)
{
	TargetSelector selector{};

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.aggression = AggressionMode::Defensive;
	summon.lastPosition = Vec3{ 0.0f, 0.0f, 0.0f };

	std::vector<TargetCandidate> candidates{
		MakeHostile(200, 400.0f, 0.0f)
	};

	StealthContext stealth{};
	stealth.playerSneaking = true;
	stealth.playerDetected = false;
	stealth.explicitAttackOrder = true;

	const EntityID chosen = selector.SelectTarget(summon, candidates, 200, {}, stealth);
	CHECK(chosen == 200);
}

TEST(stealth_undetected_aggressive_mode_engages)
{
	TargetSelector selector{};

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.aggression = AggressionMode::Aggressive; // Aggressive overrides stealth hold
	summon.lastPosition = Vec3{ 0.0f, 0.0f, 0.0f };

	std::vector<TargetCandidate> candidates{
		MakeHostile(200, 300.0f, 0.0f)
	};

	StealthContext stealth{};
	stealth.playerSneaking = true;
	stealth.playerDetected = false;
	stealth.explicitAttackOrder = false;

	const EntityID chosen = selector.SelectTarget(summon, candidates, InvalidEntityID, {}, stealth);
	CHECK(chosen == 200);
}

TEST(stealth_state_machine_holds_follow_when_stealthing)
{
	StateMachine fsm{};

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.state = SummonState::FollowOwner;
	summon.aggression = AggressionMode::Defensive;

	DecisionContext ctx{};
	ctx.hasOwner = true;
	ctx.distanceToOwner = 150.0f;
	ctx.hasTarget = true; // Enemy snapshot present in area
	ctx.detectedTargetId = 200;
	ctx.distanceToTarget = 300.0f;
	ctx.isOwnerInCombat = false;

	ctx.stealth.playerSneaking = true;
	ctx.stealth.playerDetected = false;
	ctx.stealth.explicitAttackOrder = false;

	fsm.Update(summon, ctx, 0.1f);
	CHECK(summon.state == SummonState::FollowOwner);
}

TEST(stealth_state_machine_engages_on_explicit_order)
{
	StateMachine fsm{};

	SummonModel summon{};
	summon.id = 100;
	summon.ownerId = 1;
	summon.state = SummonState::FollowOwner;
	summon.aggression = AggressionMode::Defensive;

	DecisionContext ctx{};
	ctx.hasOwner = true;
	ctx.distanceToOwner = 150.0f;
	ctx.hasTarget = true;
	ctx.detectedTargetId = 200;
	ctx.distanceToTarget = 200.0f;
	ctx.engageDistanceThreshold = 300.0f;
	ctx.isOwnerInCombat = false;

	ctx.stealth.playerSneaking = true;
	ctx.stealth.playerDetected = false;
	ctx.stealth.explicitAttackOrder = true; // Explicit attack commanded!

	fsm.Update(summon, ctx, 0.1f);
	CHECK(summon.state == SummonState::AcquireTarget);
}
