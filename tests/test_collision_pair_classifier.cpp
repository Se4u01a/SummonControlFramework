#include "Collision/CollisionPairClassifier.h"
#include "Core/Settings.h"
#include "Core/SummonRegistry.h"
#include "check.h"

#include <string>
#include <vector>

using namespace scf::collision;
using namespace scf::core;

TEST(collision_player_own_summon_passthrough)
{
	CollisionPairClassifier classifier;
	SummonRegistry registry;
	CollisionSettings settings{ true, true };

	// Register player summon
	registry.RegisterSummon(0x14, 0x10001, SummonType::SummonCreature, 60.0f, 0.0f);

	// Case: Player as A, Summon as B
	const auto resAB = classifier.ClassifyPair(0x14, 0x10001, registry, settings, 0x14);
	CHECK(resAB == CollisionResult::PassThrough);

	// Case: Summon as A, Player as B (symmetrical)
	const auto resBA = classifier.ClassifyPair(0x10001, 0x14, registry, settings, 0x14);
	CHECK(resBA == CollisionResult::PassThrough);
}

TEST(collision_player_other_owner_summon_collide)
{
	CollisionPairClassifier classifier;
	SummonRegistry registry;
	CollisionSettings settings{ true, true };

	// Register enemy summon belonging to Enemy Caster 0x55555
	registry.RegisterSummon(0x55555, 0x20002, SummonType::SummonCreature, 60.0f, 0.0f);

	// Player and enemy summon must collide!
	const auto res = classifier.ClassifyPair(0x14, 0x20002, registry, settings, 0x14);
	CHECK(res == CollisionResult::Collide);
}

TEST(collision_allied_summons_passthrough)
{
	CollisionPairClassifier classifier;
	SummonRegistry registry;
	registry.SetDefaultMaxSummonsPerOwner(2);
	CollisionSettings settings{ true, true };

	// Register two summons for player
	registry.RegisterSummon(0x14, 0x10001, SummonType::SummonCreature, 60.0f, 0.0f);
	registry.RegisterSummon(0x14, 0x10002, SummonType::SummonCreature, 60.0f, 0.0f);

	// Two allied summons pass through each other to avoid doorway jamming
	const auto res = classifier.ClassifyPair(0x10001, 0x10002, registry, settings, 0x14);
	CHECK(res == CollisionResult::PassThrough);

	// If summon-to-summon bypass is disabled in settings:
	CollisionSettings noSquadBypass{ true, false };
	const auto resNoBypass = classifier.ClassifyPair(0x10001, 0x10002, registry, noSquadBypass, 0x14);
	CHECK(resNoBypass == CollisionResult::Collide);
}

TEST(collision_summon_and_enemy_collide)
{
	CollisionPairClassifier classifier;
	SummonRegistry registry;
	CollisionSettings settings{ true, true };

	registry.RegisterSummon(0x14, 0x10001, SummonType::SummonCreature, 60.0f, 0.0f);

	// Random enemy NPC (0x99999) not in registry
	const auto res = classifier.ClassifyPair(0x10001, 0x99999, registry, settings, 0x14);
	CHECK(res == CollisionResult::Collide);
}

TEST(collision_non_summons_default)
{
	CollisionPairClassifier classifier;
	SummonRegistry registry;
	CollisionSettings settings{ true, true };

	// Player and an NPC (neither is a summon) -> SCF must defer to engine
	const auto res = classifier.ClassifyPair(0x14, 0x77777, registry, settings, 0x14);
	CHECK(res == CollisionResult::Default);

	// Two random NPCs -> Default
	const auto resNPCs = classifier.ClassifyPair(0x88881, 0x88882, registry, settings, 0x14);
	CHECK(resNPCs == CollisionResult::Default);
}

TEST(collision_global_bypass_disabled)
{
	CollisionPairClassifier classifier;
	SummonRegistry registry;
	registry.RegisterSummon(0x14, 0x10001, SummonType::SummonCreature, 60.0f, 0.0f);

	// With collision bypass disabled entirely:
	CollisionSettings disabledSettings{ false, false };
	const auto res = classifier.ClassifyPair(0x14, 0x10001, registry, disabledSettings, 0x14);
	CHECK(res == CollisionResult::Default);
}

TEST(collision_classifier_logging)
{
	std::vector<std::string> logs;
	CollisionPairClassifier classifier([&logs](const char* msg) {
		logs.emplace_back(msg);
	});

	SummonRegistry registry;
	registry.RegisterSummon(0x14, 0x10001, SummonType::SummonCreature, 60.0f, 0.0f);
	CollisionSettings settings{ true, true };

	const auto res = classifier.ClassifyPair(0x14, 0x10001, registry, settings, 0x14);
	CHECK(res == CollisionResult::PassThrough);
	CHECK(!logs.empty());
	CHECK(logs[0].find("PassThrough: Player") != std::string::npos);
}
