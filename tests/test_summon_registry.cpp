#include "check.h"
#include "Core/SummonRegistry.h"

#include <string>
#include <vector>

using namespace scf::core;

TEST(TestIdempotentRegistration)
{
	SummonRegistry registry;

	const bool first = registry.RegisterSummon(10, 101, SummonType::SummonCreature, 60.0f, 1.0f);
	CHECK(first);
	CHECK(registry.GetTotalCount() == 1);
	CHECK(registry.IsActive(101));

	// Re-register same summon ID (e.g. refreshed duration or duplicate event)
	const bool second = registry.RegisterSummon(10, 101, SummonType::SummonCreature, 120.0f, 2.0f);
	CHECK(second);
	CHECK(registry.GetTotalCount() == 1); // No duplicate entry!
	CHECK(registry.FindRecord(101)->remainingDuration == 120.0f);
}

TEST(TestCellDetachSuspendsWithoutDeleting)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 101, SummonType::SummonCreature, 60.0f, 1.0f);

	CHECK(registry.GetActiveSummonsForOwner(10).size() == 1);
	CHECK(registry.IsActive(101));

	// 1. Cell detach: player walks through a loading door
	registry.NotifyCellDetach(101);

	// Record MUST NOT be deleted; it is kept suspended
	CHECK(registry.IsRegistered(101));
	CHECK(!registry.IsActive(101));
	CHECK(registry.FindRecord(101)->lifecycle == SummonLifecycleState::Suspended);
	CHECK(registry.GetActiveSummonsForOwner(10).empty());

	// 2. Cell attach: reference loaded in destination cell
	registry.NotifyCellAttach(101);

	CHECK(registry.IsActive(101));
	CHECK(registry.FindRecord(101)->lifecycle == SummonLifecycleState::Active);
	CHECK(registry.GetActiveSummonsForOwner(10).size() == 1);
}

TEST(TestReCastExceedingLimitDisplacesOldest)
{
	SummonRegistry registry;
	registry.SetOwnerMaxSummons(10, 2); // e.g. Twin Souls perk: max 2 summons

	EntityID displaced = InvalidEntityID;

	// Spawn 1 at t=1.0s
	registry.RegisterSummon(10, 101, SummonType::SummonCreature, 60.0f, 1.0f, &displaced);
	CHECK(displaced == InvalidEntityID);

	// Spawn 2 at t=2.0s
	registry.RegisterSummon(10, 102, SummonType::SummonCreature, 60.0f, 2.0f, &displaced);
	CHECK(displaced == InvalidEntityID);
	CHECK(registry.GetActiveSummonsForOwner(10).size() == 2);

	// Spawn 3 at t=3.0s -> Exceeds limit (2)! Oldest (101) must be displaced!
	displaced = InvalidEntityID;
	registry.RegisterSummon(10, 103, SummonType::SummonCreature, 60.0f, 3.0f, &displaced);
	CHECK(displaced == 101);

	// 101 is now Displaced; 102 and 103 are Active
	CHECK(registry.FindRecord(101)->lifecycle == SummonLifecycleState::Displaced);
	CHECK(registry.IsActive(102));
	CHECK(registry.IsActive(103));

	const auto active = registry.GetActiveSummonsForOwner(10);
	CHECK(active.size() == 2);
	CHECK(active[0] == 102);
	CHECK(active[1] == 103);
}

TEST(TestDeathAndDispelLifecycle)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 201, SummonType::SummonCreature, 60.0f, 1.0f);
	registry.RegisterSummon(10, 202, SummonType::SummonCreature, 60.0f, 1.0f);

	// 201 dies
	registry.NotifyDeath(201);
	CHECK(!registry.IsActive(201));
	CHECK(registry.FindRecord(201)->isDead);

	// 202 magic effect removed/dispelled
	registry.NotifyEffectRemoved(10, 202);
	CHECK(!registry.IsActive(202));
	CHECK(registry.FindRecord(202)->lifecycle == SummonLifecycleState::Dispatched);

	CHECK(registry.GetActiveSummonsForOwner(10).empty());
	CHECK(registry.GetTotalCount() == 2);

	// Garbage collect stale records
	registry.ClearStaleRecords();
	CHECK(registry.GetTotalCount() == 0);
}

TEST(TestResetClearsAll)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 301, SummonType::SummonCreature, 60.0f, 1.0f);
	registry.RegisterSummon(10, 302, SummonType::SummonCreature, 60.0f, 1.0f);

	CHECK(registry.GetTotalCount() == 2);

	registry.Reset();
	CHECK(registry.GetTotalCount() == 0);
	CHECK(registry.GetActiveCount() == 0);
}

TEST(TestRegistryLogging)
{
	std::vector<std::string> logs;
	SummonRegistry registry([&logs](const char* msg) {
		logs.emplace_back(msg);
	});

	registry.RegisterSummon(10, 401, SummonType::SummonCreature, 60.0f, 1.0f);
	registry.NotifyCellDetach(401);
	registry.NotifyCellAttach(401);
	registry.NotifyDeath(401);

	CHECK(logs.size() == 4);
	CHECK(logs[0].find("Registered summon 401") != std::string::npos);
	CHECK(logs[1].find("Cell detach: summon 401 suspended") != std::string::npos);
	CHECK(logs[2].find("Cell attach: summon 401 resumed") != std::string::npos);
	CHECK(logs[3].find("Death event: summon 401 marked dead") != std::string::npos);
}
