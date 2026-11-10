#include "check.h"
#include "Core/PassThrough.h"

#include <vector>

using namespace scf::core;

namespace
{
	constexpr EntityID kPlayer = 0x14;
	constexpr EntityID kNpcMage = 0x000A1234;

	PassThroughParty Summon(EntityID a_id, EntityID a_owner = kPlayer) { return { a_id, a_owner }; }
	PassThroughParty Owner(EntityID a_id = kPlayer) { return { a_id, InvalidEntityID }; }

	CollisionSettings Defaults() { return {}; }
}

// Given two summons of the player
// When their movement planners or bodies meet
// Then they ignore each other (run straight through)
TEST(TestPassThroughSameOwnerSummons)
{
	CHECK(ShouldPassThrough(Summon(0xFF000001), Summon(0xFF000002), Defaults()));
	CHECK(ShouldPassThrough(Summon(0xFF000002), Summon(0xFF000001), Defaults()));
}

// Given a summon and its own owner, they ignore each other.
TEST(TestPassThroughSummonAndOwner)
{
	CHECK(ShouldPassThrough(Summon(0xFF000001), Owner(), Defaults()));
	CHECK(ShouldPassThrough(Owner(), Summon(0xFF000001), Defaults()));
}

// Given summons of different owners (the player's wolf and an NPC mage's atronach),
// they keep avoiding and blocking each other.
TEST(TestPassThroughDifferentOwnersCollide)
{
	CHECK(!ShouldPassThrough(Summon(0xFF000001), Summon(0xFF000009, kNpcMage), Defaults()));
	CHECK(!ShouldPassThrough(Summon(0xFF000001), Owner(kNpcMage), Defaults()));
}

// Given an actor SCF does not know (enemy, NPC) or the same actor twice, vanilla rules.
TEST(TestPassThroughUnknownOrSelf)
{
	CHECK(!ShouldPassThrough(Summon(0xFF000001), PassThroughParty{}, Defaults()));
	CHECK(!ShouldPassThrough(Summon(0xFF000001), Summon(0xFF000001), Defaults()));
	CHECK(!ShouldPassThrough(Owner(), Owner(), Defaults()));
}

// Given the INI switches, the bypass can be turned off as a whole or for summon pairs.
TEST(TestPassThroughSettingsGates)
{
	CollisionSettings off{};
	off.enableCollisionBypass = false;
	CHECK(!ShouldPassThrough(Summon(0xFF000001), Summon(0xFF000002), off));
	CHECK(!ShouldPassThrough(Summon(0xFF000001), Owner(), off));

	CollisionSettings noSummonPairs{};
	noSummonPairs.enableSummonToSummonBypass = false;
	CHECK(!ShouldPassThrough(Summon(0xFF000001), Summon(0xFF000002), noSummonPairs));
	CHECK(ShouldPassThrough(Summon(0xFF000001), Owner(), noSummonPairs));
}

// Given a wolf that jumps onto another wolf, its controller (30) meets the other's body
// bones (Biped 8 / BipedNoCC 33): that pair is disabled too, controller vs controller
// as before; hits (weapon 5, projectile 6, spell 7), the world and dead bodies (32) never.
TEST(TestPassThroughLayerPairs)
{
	CHECK(IsPassThroughLayerPair(30, 30));
	CHECK(IsPassThroughLayerPair(30, 8));
	CHECK(IsPassThroughLayerPair(8, 30));
	CHECK(IsPassThroughLayerPair(30, 33));
	CHECK(!IsPassThroughLayerPair(8, 8));
	CHECK(!IsPassThroughLayerPair(30, 32));
	CHECK(!IsPassThroughLayerPair(30, 5));
	CHECK(!IsPassThroughLayerPair(6, 8));
	CHECK(!IsPassThroughLayerPair(30, 1));
	CHECK(!IsPassThroughLayerPair(30, 13));
}

// Given a wolf whose pairs Havok already accepted before SCF knew it (spawned against
// another wolf, or its controller had no group yet)
// When the next table carries it with a system group
// Then its existing pairs are re-filtered; unchanged rows are left alone.
TEST(TestPassThroughRefilterNewOrRegrouped)
{
	const CollisionSettings on{};
	const std::vector<PassThroughRow> before{ { kPlayer, 7 }, { 0xFF000001, 9 }, { 0xFF000002, 0 } };
	const std::vector<PassThroughRow> after{ { kPlayer, 7 }, { 0xFF000001, 9 }, { 0xFF000002, 11 }, { 0xFF000003, 12 }, { 0xFF000004, 0 } };
	const auto ids = RowsToRefilter(before, on, after, on);
	CHECK(ids.size() == 2);
	CHECK(ids[0] == 0xFF000002);
	CHECK(ids[1] == 0xFF000003);

	CHECK(RowsToRefilter(after, on, after, on).empty());
}

// Given a summon dismissed (or no longer managed), its pairs return to vanilla:
// it is re-filtered once more after it left the table.
TEST(TestPassThroughRefilterRemoved)
{
	const CollisionSettings on{};
	const std::vector<PassThroughRow> before{ { kPlayer, 7 }, { 0xFF000001, 9 } };
	const std::vector<PassThroughRow> after{ { kPlayer, 7 } };
	const auto ids = RowsToRefilter(before, on, after, on);
	CHECK(ids.size() == 1);
	CHECK(ids[0] == 0xFF000001);
}

// Given the bypass switched off (or on) in the menu, every actor of both tables is
// re-filtered, each once.
TEST(TestPassThroughRefilterSettingsChange)
{
	CollisionSettings off{};
	off.enableCollisionBypass = false;
	const std::vector<PassThroughRow> rows{ { kPlayer, 7 }, { 0xFF000001, 9 } };
	const std::vector<PassThroughRow> fewer{ { kPlayer, 7 } };
	const auto ids = RowsToRefilter(rows, CollisionSettings{}, fewer, off);
	CHECK(ids.size() == 2);
	CHECK(ids[0] == kPlayer);
	CHECK(ids[1] == 0xFF000001);
}
