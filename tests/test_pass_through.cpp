#include "check.h"
#include "Core/PassThrough.h"

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
