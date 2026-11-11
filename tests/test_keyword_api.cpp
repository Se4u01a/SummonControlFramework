#include "check.h"
#include "Core/KeywordApi.h"

#include <vector>

using namespace scf::core;

namespace
{
	constexpr EntityID kPlayer = 0x14;
	constexpr EntityID kFollower = 0x000B1234;
	constexpr EntityID kBandit = 0x000C0001;

	PassThroughParty Actor(EntityID a_id, EntityID a_leader, std::uint8_t a_flags = 0, std::vector<EntityID> a_hostile = {})
	{
		PassThroughParty party{};
		party.id = a_id;
		party.leaderId = a_leader;
		party.flags = a_flags;
		party.hostileIds = std::move(a_hostile);
		return party;
	}
}

// Given the three API EditorIDs in any letter case, each maps to its flag.
TEST(TestKeywordEditorIds)
{
	CHECK(FlagForKeywordEditorId("SCF_PassThroughFriends") == kPassThroughFriends);
	CHECK(FlagForKeywordEditorId("scf_passthroughall") == kPassThroughAll);
	CHECK(FlagForKeywordEditorId("SCF_UNTARGETABLE") == kUntargetable);
	CHECK(FlagForKeywordEditorId("SCF_PassThrough") == 0);
	CHECK(FlagForKeywordEditorId("") == 0);
}

// Given a follower's summon with SCF_PassThroughFriends
// When it meets the player, the follower, another of the player's side, or a bandit
// Then it passes through its own side only.
TEST(TestKeywordPassThroughFriends)
{
	const CollisionSettings settings{};
	const auto summon = Actor(0xFF000010, kPlayer, kPassThroughFriends);
	CHECK(ShouldPassThrough(summon, Actor(kPlayer, kPlayer), settings));
	CHECK(ShouldPassThrough(Actor(kFollower, kPlayer), summon, settings));
	CHECK(!ShouldPassThrough(summon, Actor(kBandit, kBandit), settings));

	// Without the keyword two members of the same side still collide (not SCF summons).
	CHECK(!ShouldPassThrough(Actor(0xFF000011, kPlayer), Actor(kFollower, kPlayer), settings));
	// An unknown leader never matches.
	CHECK(!ShouldPassThrough(Actor(0xFF000012, InvalidEntityID, kPassThroughFriends), Actor(0xFF000013, InvalidEntityID), settings));
}

// Given an actor with SCF_PassThroughAll, it passes through anyone - also with the
// SCF collision switches off (the keyword is an explicit request).
TEST(TestKeywordPassThroughAll)
{
	CollisionSettings off{};
	off.enableCollisionBypass = false;
	const auto ghost = Actor(0xFF000020, kPlayer, kPassThroughAll);
	CHECK(ShouldPassThrough(ghost, Actor(kBandit, kBandit), off));
	CHECK(ShouldPassThrough(Actor(kFollower, kPlayer), ghost, off));
	CHECK(!ShouldPassThrough(ghost, ghost, off));
}

// Given an untargetable summon, hostile actors pass through it; friends and neutrals
// still bump into it.
TEST(TestKeywordUntargetableHostilesPass)
{
	const CollisionSettings settings{};
	const auto decoy = Actor(0xFF000030, kPlayer, kUntargetable, { kBandit });
	CHECK(ShouldPassThrough(Actor(kBandit, kBandit), decoy, settings));
	CHECK(ShouldPassThrough(decoy, Actor(kBandit, kBandit), settings));
	CHECK(!ShouldPassThrough(decoy, Actor(kFollower, kPlayer), settings));
	CHECK(IsUntargetablePair(decoy, Actor(kBandit, kBandit)));
	CHECK(!IsUntargetablePair(decoy, Actor(0x000C0002, 0x000C0002)));
}

// Given an enemy whose selector picked an untargetable actor, it is answered with the
// next valid group target, or with none.
TEST(TestSelectorAvoidsUntargetable)
{
	const std::vector<SelectorCandidate> group{ { 0x100, true, false }, { 0x200, false, true }, { 0x300, false, false } };
	CHECK(ChooseTargetAvoidingUntargetable(0x100, true, group) == 0x300);
	CHECK(ChooseTargetAvoidingUntargetable(0x300, false, group) == 0x300);
	CHECK(ChooseTargetAvoidingUntargetable(0, false, group) == 0);

	const std::vector<SelectorCandidate> onlyDecoy{ { 0x100, true, false }, { 0x200, false, true } };
	CHECK(ChooseTargetAvoidingUntargetable(0x100, true, onlyDecoy) == 0);
}

// Given the keyword table holding every loaded actor, plain actors are never
// re-filtered; a flag appearing on one (magic effect applied) re-filters it, and losing
// it re-filters it once more.
TEST(TestRefilterOnlyRelevantRows)
{
	const CollisionSettings on{};
	const std::vector<PassThroughRow> before{ { kBandit, 5, false, 0 }, { kFollower, 6, false, 0 } };
	CHECK(RowsToRefilter({}, on, before, on).empty());

	const std::vector<PassThroughRow> flagged{ { kBandit, 5, false, 0 }, { kFollower, 6, true, 42 } };
	const auto gained = RowsToRefilter(before, on, flagged, on);
	CHECK(gained.size() == 1);
	CHECK(gained[0] == kFollower);

	const std::vector<PassThroughRow> hostileChanged{ { kBandit, 5, false, 0 }, { kFollower, 6, true, 43 } };
	CHECK(RowsToRefilter(flagged, on, hostileChanged, on).size() == 1);

	const auto lost = RowsToRefilter(flagged, on, before, on);
	CHECK(lost.size() == 1);
	CHECK(lost[0] == kFollower);
}

// Given the signature, it changes with flags, leader and the hostile set, but not with
// the order of the hostile set.
TEST(TestPassThroughSignature)
{
	const auto a = Actor(1, kPlayer, kUntargetable, { 10, 20 });
	const auto b = Actor(1, kPlayer, kUntargetable, { 20, 10 });
	CHECK(PassThroughSignature(a) == PassThroughSignature(b));
	CHECK(PassThroughSignature(a) != PassThroughSignature(Actor(1, kPlayer, kUntargetable, { 10 })));
	CHECK(PassThroughSignature(a) != PassThroughSignature(Actor(1, kPlayer, kPassThroughAll, { 10, 20 })));
	CHECK(PassThroughSignature(a) != PassThroughSignature(Actor(1, kFollower, kUntargetable, { 10, 20 })));
}
