#include "check.h"
#include "AI/TargetSelector.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace scf;

TEST(TestTargetSelectorPassive)
{
	ai::TargetSelector selector;
	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Passive;

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 100, .position = { 100, 0, 0 }, .isHostileToOwner = true }
	};

	const auto chosen = selector.SelectTarget(summon, candidates);
	CHECK(chosen == core::InvalidEntityID);
}

TEST(TestTargetSelectorFriendlyFireGuard)
{
	std::vector<std::string> logHistory;
	ai::TargetSelector selector([&logHistory](core::EntityID, core::EntityID, const char* reason) {
		logHistory.emplace_back(reason);
	});

	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Aggressive;

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 10, .position = { 50, 0, 0 }, .isOwnerOrAlly = true },        // Owner
		{ .id = 2,  .position = { 60, 0, 0 }, .isOwnerOrAlly = true },        // Fellow summon
		{ .id = 200, .position = { 500, 0, 0 }, .isHostileToOwner = true }     // Enemy
	};

	// 1. Automatic targeting should choose enemy 200, skipping owner (10) and ally (2)
	const auto chosenAuto = selector.SelectTarget(summon, candidates);
	CHECK(chosenAuto == 200);

	// 2. Explicit FocusTarget targeting owner MUST be rejected
	logHistory.clear();
	const auto chosenFocusOwner = selector.SelectTarget(summon, candidates, 10);
	CHECK(chosenFocusOwner == 200); // Focus rejected, falls back to best auto candidate

	const bool hasRejectionLog = std::any_of(logHistory.begin(), logHistory.end(), [](const std::string& msg) {
		return msg.find("cannot target owner/ally") != std::string::npos;
	});
	CHECK(hasRejectionLog);
}

TEST(TestTargetSelectorAttackingOwnerPriority)
{
	ai::TargetSelector selector;
	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Defensive;
	summon.lastPosition = { 0, 0, 0 };

	std::vector<ai::TargetCandidate> candidates{
		// Candidate 1: Close (distance 200), but not attacking owner
		{ .id = 101, .position = { 200, 0, 0 }, .isHostileToOwner = true, .isAttackingOwner = false },
		// Candidate 2: Farther (distance 800), but actively attacking owner
		{ .id = 102, .position = { 800, 0, 0 }, .isHostileToOwner = true, .isAttackingOwner = true }
	};

	const auto chosen = selector.SelectTarget(summon, candidates);
	CHECK(chosen == 102);
}

TEST(TestTargetSelectorStickyTargetBonus)
{
	ai::TargetSelector selector;
	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Aggressive;
	summon.lastPosition = { 0, 0, 0 };
	summon.currentTargetId = 101; // Already fighting 101

	std::vector<ai::TargetCandidate> candidates{
		// 101: Current target, distance 500
		{ .id = 101, .position = { 500, 0, 0 }, .isHostileToOwner = true },
		// 102: New enemy slightly closer, distance 400
		{ .id = 102, .position = { 400, 0, 0 }, .isHostileToOwner = true }
	};

	// Because of currentTargetStickyBonus (400), 101 should stay selected despite 102 being 100 units closer
	const auto chosen = selector.SelectTarget(summon, candidates);
	CHECK(chosen == 101);
}

TEST(TestTargetSelectorFocusTargetOverride)
{
	ai::TargetSelector selector;
	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Aggressive;
	summon.lastPosition = { 0, 0, 0 };

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 101, .position = { 100, 0, 0 }, .isHostileToOwner = true },
		{ .id = 202, .position = { 1200, 0, 0 }, .isHostileToOwner = true }
	};

	// Player commands focus on 202
	const auto chosen = selector.SelectTarget(summon, candidates, 202);
	CHECK(chosen == 202);
}

TEST(TestTargetSelectorDeadOrDisabledIgnored)
{
	ai::TargetSelector selector;
	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Aggressive;

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 101, .position = { 100, 0, 0 }, .isDead = true, .isHostileToOwner = true },
		{ .id = 102, .position = { 200, 0, 0 }, .isDisabled = true, .isHostileToOwner = true },
		{ .id = 103, .position = { 500, 0, 0 }, .isDead = false, .isDisabled = false, .isHostileToOwner = true }
	};

	const auto chosen = selector.SelectTarget(summon, candidates);
	CHECK(chosen == 103);
}

TEST(TestTargetSelectorDefensiveRequiresThreat)
{
	ai::TargetSelector selector;
	core::SummonModel summon;
	summon.id = 1;
	summon.ownerId = 10;
	summon.aggression = core::AggressionMode::Defensive;

	// Non-threatening neutral candidate
	std::vector<ai::TargetCandidate> candidates{
		{ .id = 101, .position = { 100, 0, 0 }, .isHostileToOwner = false, .isAttackingOwner = false }
	};

	const auto chosen = selector.SelectTarget(summon, candidates);
	CHECK(chosen == core::InvalidEntityID);
}
