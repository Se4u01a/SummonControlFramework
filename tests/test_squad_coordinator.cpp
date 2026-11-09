#include "check.h"
#include "AI/SquadCoordinator.h"
#include "AI/TargetSelector.h"

#include <string>
#include <vector>

using namespace scf;

TEST(TestFormationOffsetsDistinct)
{
	ai::SquadCoordinator coordinator;

	core::SummonModel model1;
	model1.id = 1;
	core::SummonModel model2;
	model2.id = 2;

	std::vector<ai::SquadMember> members{
		{ .model = &model1, .role = ai::CombatRole::Melee },
		{ .model = &model2, .role = ai::CombatRole::Melee }
	};

	coordinator.CalculateFormationOffsets(members, 0.0f, 150.0f);

	// Offsets must be distinct and non-zero
	const auto distBetweenMembers = members[0].formationOffset.DistanceTo(members[1].formationOffset);
	CHECK(distBetweenMembers > 50.0f);

	// Symmetrical X offsets (one left, one right)
	CHECK(members[0].formationOffset.x < 0.0f);
	CHECK(members[1].formationOffset.x > 0.0f);

	// Behind owner (negative Y when facing North/0 deg)
	CHECK(members[0].formationOffset.y < 0.0f);
	CHECK(members[1].formationOffset.y < 0.0f);
}

TEST(TestTargetDistributionAcrossMultipleEnemies)
{
	ai::SquadCoordinator coordinator;
	ai::TargetSelector selector;

	core::SummonModel model1;
	model1.id = 1;
	model1.ownerId = 10;
	model1.aggression = core::AggressionMode::Aggressive;
	model1.lastPosition = { 0, 0, 0 };

	core::SummonModel model2;
	model2.id = 2;
	model2.ownerId = 10;
	model2.aggression = core::AggressionMode::Aggressive;
	model2.lastPosition = { 0, 0, 0 };

	std::vector<ai::SquadMember> members{
		{ .model = &model1, .role = ai::CombatRole::Melee },
		{ .model = &model2, .role = ai::CombatRole::Melee }
	};

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 101, .position = { 300, 0, 0 }, .isHostileToOwner = true },
		{ .id = 102, .position = { 350, 0, 0 }, .isHostileToOwner = true }
	};

	ai::SquadConfig config;
	config.maxMeleeSlotsPerTarget = 1; // Strict: 1 melee attacker per target

	coordinator.CoordinateSquad(members, candidates, { 0, 0, 0 }, 0.0f, selector, config);

	// Member 1 takes Candidate 101 (closer)
	CHECK(members[0].assignedTarget == 101);
	CHECK(model1.currentTargetId == 101);

	// Member 2 sees 101 slots full and distributes to Candidate 102!
	CHECK(members[1].assignedTarget == 102);
	CHECK(model2.currentTargetId == 102);
}

TEST(TestMeleeSlotLimitPreventsDogpiling)
{
	ai::SquadCoordinator coordinator;
	ai::TargetSelector selector;

	core::SummonModel model1{ .id = 1, .ownerId = 10, .aggression = core::AggressionMode::Aggressive };
	core::SummonModel model2{ .id = 2, .ownerId = 10, .aggression = core::AggressionMode::Aggressive };
	core::SummonModel model3{ .id = 3, .ownerId = 10, .aggression = core::AggressionMode::Aggressive };

	std::vector<ai::SquadMember> members{
		{ .model = &model1, .role = ai::CombatRole::Melee },
		{ .model = &model2, .role = ai::CombatRole::Melee },
		{ .model = &model3, .role = ai::CombatRole::Melee }
	};

	// Only 1 single enemy available
	std::vector<ai::TargetCandidate> candidates{
		{ .id = 999, .position = { 400, 0, 0 }, .isHostileToOwner = true }
	};

	ai::SquadConfig config;
	config.maxMeleeSlotsPerTarget = 2; // Maximum 2 attackers can engage target 999

	coordinator.CoordinateSquad(members, candidates, { 0, 0, 0 }, 0.0f, selector, config);

	// First two summons claim the slots
	CHECK(members[0].assignedTarget == 999);
	CHECK(members[1].assignedTarget == 999);

	// Third summon cannot dogpile; holds in formation
	CHECK(members[2].assignedTarget == core::InvalidEntityID);
	CHECK(model3.currentTargetId == core::InvalidEntityID);
}

TEST(TestRangedAndMeleeIndependentSlots)
{
	ai::SquadCoordinator coordinator;
	ai::TargetSelector selector;

	core::SummonModel modelMelee{ .id = 1, .ownerId = 10, .aggression = core::AggressionMode::Aggressive };
	core::SummonModel modelRanged{ .id = 2, .ownerId = 10, .aggression = core::AggressionMode::Aggressive };

	std::vector<ai::SquadMember> members{
		{ .model = &modelMelee,  .role = ai::CombatRole::Melee },
		{ .model = &modelRanged, .role = ai::CombatRole::Ranged }
	};

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 500, .position = { 400, 0, 0 }, .isHostileToOwner = true }
	};

	ai::SquadConfig config;
	config.maxMeleeSlotsPerTarget = 1;
	config.maxRangedSlotsPerTarget = 1;

	coordinator.CoordinateSquad(members, candidates, { 0, 0, 0 }, 0.0f, selector, config);

	// Both can target 500 simultaneously because melee and ranged slots are tracked independently
	CHECK(members[0].assignedTarget == 500);
	CHECK(members[1].assignedTarget == 500);
}

TEST(TestSquadCoordinatorLogging)
{
	std::vector<std::string> logs;
	ai::SquadCoordinator coordinator([&logs](const char* msg) {
		logs.emplace_back(msg);
	});
	ai::TargetSelector selector;

	core::SummonModel model{ .id = 7, .ownerId = 10, .aggression = core::AggressionMode::Aggressive };
	std::vector<ai::SquadMember> members{
		{ .model = &model, .role = ai::CombatRole::Melee }
	};

	std::vector<ai::TargetCandidate> candidates{
		{ .id = 303, .position = { 200, 0, 0 }, .isHostileToOwner = true }
	};

	coordinator.CoordinateSquad(members, candidates, { 0, 0, 0 }, 0.0f, selector);

	CHECK(logs.size() == 1);
	CHECK(logs[0].find("Summon 7 (Melee) assigned to Target 303") != std::string::npos);
	CHECK(logs[0].find("Melee slot 1/2") != std::string::npos);
}
