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

	// Garbage collect stale records (immediate when no timestamps given)
	registry.ClearStaleRecords();
	CHECK(registry.GetTotalCount() == 0);
}

TEST(TestGracePeriodGarbageCollection)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 201, SummonType::SummonCreature, 60.0f, 1.0f);

	// Dies at timestamp 10.0s
	registry.NotifyDeath(201, 10.0f);

	// At 11.0s (1s elapsed, grace period 3.0s): should NOT be cleared yet
	registry.ClearStaleRecords(11.0f, 3.0f);
	CHECK(registry.GetTotalCount() == 1);
	CHECK(!registry.IsActive(201));

	// At 12.9s: still kept
	registry.ClearStaleRecords(12.9f, 3.0f);
	CHECK(registry.GetTotalCount() == 1);

	// At 13.1s (>= 3.0s elapsed): should be pruned
	registry.ClearStaleRecords(13.1f, 3.0f);
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

// Stage 9: a conjuration effect without duration is permanent, not "expires now"
// (Dead Thrall reports no timer), and an effect that ran past its duration reads 0.
TEST(TestRemainingDurationFromEffectPolicy)
{
	CHECK(RemainingDurationFromEffect(0.0f, 0.0f) == kPermanentDuration);
	CHECK(RemainingDurationFromEffect(-1.0f, 0.0f) == kPermanentDuration);
	CHECK(RemainingDurationFromEffect(0.0f, 12.0f) == kPermanentDuration);
	CHECK_NEAR(RemainingDurationFromEffect(60.0f, 10.0f), 50.0f, 0.0001);
	CHECK(RemainingDurationFromEffect(60.0f, 60.0f) == 0.0f);
	CHECK(RemainingDurationFromEffect(60.0f, 90.0f) == 0.0f);
	CHECK(RemainingDurationFromEffect(kPermanentDuration, 0.0f) == kPermanentDuration);
}

// The tactical sync re-registers every tracked summon on every tick with the default
// (roster) flag; that refresh must not revoke a Papyrus registration.
TEST(ExplicitRegistrationSurvivesRosterRefresh)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 501, SummonType::Scripted, kPermanentDuration, 1.0f, nullptr, true);

	CHECK(registry.FindRecord(501)->explicitRegistration);
	CHECK(registry.IsRosterIndependent(501));

	// Next tactical tick: the engine roster now reports the same actor as a commanded one.
	CHECK(registry.RegisterSummon(10, 501, SummonType::SummonCreature, 30.0f, 2.0f));
	CHECK(registry.FindRecord(501)->explicitRegistration);
	CHECK(registry.IsRosterIndependent(501));
	CHECK(registry.GetTotalCount() == 1);
}

// A reanimated corpse is released by the end of its reanimate effect, not by the roster.
TEST(ReanimateIsRosterIndependent)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 601, SummonType::Reanimate, kPermanentDuration, 1.0f);

	CHECK(registry.IsRosterIndependent(601));
	CHECK(!registry.IsRosterIndependent(602));
	CHECK(registry.GetOrphanedSummons(10, {}).empty());
}

// The tactical sync releases only the records the roster owns: a Papyrus registration, a
// thrall and a dead record are somebody else's to release.
TEST(OrphanedSummonsSkipRosterIndependentAndDead)
{
	SummonRegistry registry;
	registry.SetOwnerMaxSummons(10, 10);  // the conjuration cap is not what this test is about
	registry.SetOwnerMaxSummons(20, 10);
	registry.RegisterSummon(10, 701, SummonType::SummonCreature, 60.0f, 1.0f);  // in the roster
	registry.RegisterSummon(10, 702, SummonType::SummonCreature, 60.0f, 2.0f);  // roster lost it
	registry.RegisterSummon(10, 703, SummonType::Scripted, kPermanentDuration, 3.0f, nullptr, true);
	registry.RegisterSummon(10, 704, SummonType::Reanimate, kPermanentDuration, 4.0f);
	registry.RegisterSummon(10, 705, SummonType::SummonCreature, 60.0f, 5.0f);
	registry.NotifyDeath(705);
	registry.RegisterSummon(20, 706, SummonType::SummonCreature, 60.0f, 6.0f);  // another owner

	const auto orphaned = registry.GetOrphanedSummons(10, { 701 });
	CHECK(orphaned.size() == 1);
	if (orphaned.size() == 1) {
		CHECK(orphaned[0] == 702);
	}
}

// The release order must not depend on unordered_map iteration order.
TEST(OrphanedSummonsAreSortedAscending)
{
	SummonRegistry registry;
	registry.SetOwnerMaxSummons(10, 10);
	registry.RegisterSummon(10, 805, SummonType::SummonCreature, 60.0f, 1.0f);
	registry.RegisterSummon(10, 803, SummonType::SummonCreature, 60.0f, 2.0f);
	registry.RegisterSummon(10, 804, SummonType::SummonCreature, 60.0f, 3.0f);

	const auto orphaned = registry.GetOrphanedSummons(10, {});
	CHECK(orphaned.size() == 3);
	if (orphaned.size() == 3) {
		CHECK(orphaned[0] == 803);
		CHECK(orphaned[1] == 804);
		CHECK(orphaned[2] == 805);
	}
}

// A scripted summon is not a conjuration slot: registering it while the player is at the
// limit must not dispel a vanilla summon.
TEST(ExplicitRegistrationDoesNotConsumeSummonSlot)
{
	SummonRegistry registry;
	registry.SetOwnerMaxSummons(10, 1);
	registry.RegisterSummon(10, 801, SummonType::SummonCreature, 60.0f, 1.0f);

	EntityID displaced = InvalidEntityID;
	registry.RegisterSummon(10, 901, SummonType::Scripted, kPermanentDuration, 2.0f, &displaced, true);

	CHECK(displaced == InvalidEntityID);
	CHECK(registry.IsActive(801));
	CHECK(registry.IsActive(901));
	CHECK(registry.GetActiveSummonsForOwner(10).size() == 2);
}

// A raised corpse does not cost the player a conjuration slot either.
TEST(ReanimateDoesNotDisplaceVanillaSummon)
{
	SummonRegistry registry;
	registry.SetOwnerMaxSummons(10, 1);
	registry.RegisterSummon(10, 811, SummonType::SummonCreature, 60.0f, 1.0f);

	EntityID displaced = InvalidEntityID;
	registry.RegisterSummon(10, 911, SummonType::Reanimate, kPermanentDuration, 2.0f, &displaced);

	CHECK(displaced == InvalidEntityID);
	CHECK(registry.IsActive(811));
	CHECK(registry.IsActive(911));
}

// The cap still governs roster-backed summons, and it never picks a roster-independent
// record as the one to displace - even when that record is the oldest.
TEST(LimitDisplacesOldestRosterBackedOnly)
{
	SummonRegistry registry;
	registry.SetOwnerMaxSummons(10, 1);
	registry.RegisterSummon(10, 921, SummonType::Scripted, kPermanentDuration, 0.5f, nullptr, true);
	registry.RegisterSummon(10, 821, SummonType::SummonCreature, 60.0f, 1.0f);

	EntityID displaced = InvalidEntityID;
	registry.RegisterSummon(10, 822, SummonType::SummonCreature, 60.0f, 2.0f, &displaced);

	CHECK(displaced == 821);
	CHECK(registry.FindRecord(821)->lifecycle == SummonLifecycleState::Displaced);
	CHECK(registry.IsActive(822));
	CHECK(registry.IsActive(921));
}

// UnregisterSummon is the script's release signal: the record leaves the registry at once.
TEST(UnregisterReleasesExplicitRegistration)
{
	SummonRegistry registry;
	registry.RegisterSummon(10, 931, SummonType::Scripted, kPermanentDuration, 1.0f, nullptr, true);

	CHECK(registry.UnregisterSummon(931, "Papyrus SCF.UnregisterSummon"));
	CHECK(!registry.IsRegistered(931));
	CHECK(registry.GetTotalCount() == 0);
}
