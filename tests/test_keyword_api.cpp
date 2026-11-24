#include "check.h"
#include "Core/Formation.h"
#include "Core/KeywordApi.h"
#include "Core/MovementIntent.h"
#include "Core/SummonRegistry.h"
#include "Core/WolfManeuver.h"

#include <cmath>
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

	bool Near(float a_lhs, float a_rhs, float a_eps = 1.0e-3f)
	{
		return std::abs(a_lhs - a_rhs) <= a_eps;
	}
}

// =========================================================================
// Keyword API v1 backward-compatibility tests
// =========================================================================

TEST(TestKeywordEditorIds)
{
	CHECK(FlagForKeywordEditorId("SCF_PassThroughFriends") == kPassThroughFriends);
	CHECK(FlagForKeywordEditorId("scf_passthroughall") == kPassThroughAll);
	CHECK(FlagForKeywordEditorId("SCF_UNTARGETABLE") == kUntargetable);
	CHECK(FlagForKeywordEditorId("SCF_PassThrough") == 0);
	CHECK(FlagForKeywordEditorId("") == 0);
	// Non-passthrough v2 keywords must NOT leak into the 3-bit v1 helper.
	CHECK(FlagForKeywordEditorId("SCF_HighThreat") == 0);
	CHECK(FlagForKeywordEditorId("SCF_Stationary") == 0);
}

TEST(TestKeywordPassThroughFriends)
{
	const CollisionSettings settings{};
	const auto summon = Actor(0xFF000010, kPlayer, kPassThroughFriends);
	CHECK(ShouldPassThrough(summon, Actor(kPlayer, kPlayer), settings));
	CHECK(ShouldPassThrough(Actor(kFollower, kPlayer), summon, settings));
	CHECK(!ShouldPassThrough(summon, Actor(kBandit, kBandit), settings));

	CHECK(!ShouldPassThrough(Actor(0xFF000011, kPlayer), Actor(kFollower, kPlayer), settings));
	CHECK(!ShouldPassThrough(Actor(0xFF000012, InvalidEntityID, kPassThroughFriends), Actor(0xFF000013, InvalidEntityID), settings));
}

TEST(TestKeywordPassThroughAll)
{
	CollisionSettings off{};
	off.enableCollisionBypass = false;
	const auto ghost = Actor(0xFF000020, kPlayer, kPassThroughAll);
	CHECK(ShouldPassThrough(ghost, Actor(kBandit, kBandit), off));
	CHECK(ShouldPassThrough(Actor(kFollower, kPlayer), ghost, off));
	CHECK(!ShouldPassThrough(ghost, ghost, off));
}

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

TEST(TestSelectorAvoidsUntargetable)
{
	const std::vector<SelectorCandidate> group{ { 0x100, true, false }, { 0x200, false, true }, { 0x300, false, false } };
	CHECK(ChooseTargetAvoidingUntargetable(0x100, true, group) == 0x300);
	CHECK(ChooseTargetAvoidingUntargetable(0x300, false, group) == 0x300);
	CHECK(ChooseTargetAvoidingUntargetable(0, false, group) == 0);

	const std::vector<SelectorCandidate> onlyDecoy{ { 0x100, true, false }, { 0x200, false, true } };
	CHECK(ChooseTargetAvoidingUntargetable(0x100, true, onlyDecoy) == 0);
}

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

TEST(TestPassThroughSignature)
{
	const auto a = Actor(1, kPlayer, kUntargetable, { 10, 20 });
	const auto b = Actor(1, kPlayer, kUntargetable, { 20, 10 });
	CHECK(PassThroughSignature(a) == PassThroughSignature(b));
	CHECK(PassThroughSignature(a) != PassThroughSignature(Actor(1, kPlayer, kUntargetable, { 10 })));
	CHECK(PassThroughSignature(a) != PassThroughSignature(Actor(1, kPlayer, kPassThroughAll, { 10, 20 })));
	CHECK(PassThroughSignature(a) != PassThroughSignature(Actor(1, kFollower, kUntargetable, { 10, 20 })));
}

// =========================================================================
// Keyword API v2: Parsing flags, aliases, and parametric numeric keywords
// =========================================================================

TEST(TestKeywordV2ParseFlagsAndAliases)
{
	CHECK(ParseKeywordEditorId("SCF_HighThreat").flags == kKwHighThreat);
	CHECK(ParseKeywordEditorId("scf_tauntpriority").flags == kKwHighThreat);
	CHECK(ParseKeywordEditorId("SCF_LowThreat").flags == kKwLowThreat);
	CHECK(ParseKeywordEditorId("SCF_FriendlyFireProtected").flags == kKwFriendlyFireProtected);
	CHECK(ParseKeywordEditorId("SCF_AllowFriendlyFire").flags == kKwAllowFriendlyFire);
	CHECK(ParseKeywordEditorId("SCF_AutoRegister").flags == kKwAutoRegister);
	CHECK(ParseKeywordEditorId("SCF_Unmanaged").flags == kKwUnmanaged);
	CHECK(ParseKeywordEditorId("SCF_IgnoreSummon").flags == kKwUnmanaged);
	CHECK(ParseKeywordEditorId("SCF_FreeSlot").flags == kKwFreeSlot);
	CHECK(ParseKeywordEditorId("SCF_IgnoreLimit").flags == kKwFreeSlot);
	CHECK(ParseKeywordEditorId("SCF_Persistent").flags == kKwPersistent);
	CHECK(ParseKeywordEditorId("SCF_NoPersistence").flags == kKwNoPersistence);
	CHECK(ParseKeywordEditorId("SCF_NonCombat").flags == kKwNonCombat);
	CHECK(ParseKeywordEditorId("SCF_AlwaysPassive").flags == kKwNonCombat);
	CHECK(ParseKeywordEditorId("SCF_DefaultPassive").flags == kKwDefaultPassive);
	CHECK(ParseKeywordEditorId("SCF_DefaultDefensive").flags == kKwDefaultDefensive);
	CHECK(ParseKeywordEditorId("SCF_DefaultAggressive").flags == kKwDefaultAggressive);
	CHECK(ParseKeywordEditorId("SCF_IgnoreOrders").flags == kKwIgnoreOrders);
	CHECK(ParseKeywordEditorId("SCF_IgnoreHold").flags == kKwIgnoreHold);
	CHECK(ParseKeywordEditorId("SCF_IgnoreFollow").flags == kKwIgnoreFollow);
	CHECK(ParseKeywordEditorId("SCF_IgnoreAttackOrder").flags == kKwIgnoreAttackOrder);
	CHECK(ParseKeywordEditorId("SCF_NoAssist").flags == kKwNoAssist);
	CHECK(ParseKeywordEditorId("SCF_NoCircling").flags == kKwNoCircling);
	CHECK(ParseKeywordEditorId("SCF_DirectAssault").flags == kKwNoCircling);
	CHECK(ParseKeywordEditorId("SCF_Bodyguard").flags == kKwBodyguard);
	CHECK(ParseKeywordEditorId("SCF_HuntCasters").flags == kKwHuntCasters);
	CHECK(ParseKeywordEditorId("SCF_HuntRanged").flags == kKwHuntRanged);
	CHECK(ParseKeywordEditorId("SCF_FinishLowHP").flags == kKwFinishLowHP);
	CHECK(ParseKeywordEditorId("SCF_HoldFireInStealth").flags == kKwHoldFireInStealth);
	CHECK(ParseKeywordEditorId("SCF_IgnoreCombatRadius").flags == kKwIgnoreCombatRadius);
	CHECK(ParseKeywordEditorId("SCF_Stationary").flags == kKwStationary);
	CHECK(ParseKeywordEditorId("SCF_Turret").flags == kKwStationary);
	CHECK(ParseKeywordEditorId("SCF_NoCatchUpWarp").flags == kKwNoCatchUpWarp);
	CHECK(ParseKeywordEditorId("SCF_NoCombatLeash").flags == kKwNoCombatLeash);
	CHECK(ParseKeywordEditorId("SCF_FormationFront").flags == kKwFormationFront);
	CHECK(ParseKeywordEditorId("SCF_RoleTank").flags == kKwFormationFront);
	CHECK(ParseKeywordEditorId("SCF_FormationFlank").flags == kKwFormationFlank);
	CHECK(ParseKeywordEditorId("SCF_RoleMelee").flags == kKwFormationFlank);
	CHECK(ParseKeywordEditorId("SCF_FormationBack").flags == kKwFormationBack);
	CHECK(ParseKeywordEditorId("SCF_RoleRanged").flags == kKwFormationBack);
	CHECK(ParseKeywordEditorId("SCF_RoleSupport").flags == kKwFormationBack);
	CHECK(ParseKeywordEditorId("SCF_CmdRush").flags == kKwCmdRush);
	CHECK(ParseKeywordEditorId("SCF_CmdDetonate").flags == kKwCmdDetonate);

	// World scope classification
	CHECK(IsWorldKeywordFlags(kKwPassThroughFriends));
	CHECK(IsWorldKeywordFlags(kKwHighThreat));
	CHECK(IsWorldKeywordFlags(kKwFriendlyFireProtected));
	CHECK(IsWorldKeywordFlags(kKwAutoRegister));
	CHECK(!IsWorldKeywordFlags(kKwStationary));
	CHECK(!IsWorldKeywordFlags(kKwFreeSlot));
	CHECK(!IsWorldKeywordFlags(kKwHuntCasters));
}

TEST(TestKeywordV2ParseParametricAndMerge)
{
	const auto r1 = ParseKeywordEditorId("SCF_CombatRadius_750");
	CHECK(r1.Recognized());
	CHECK(r1.paramKind == KeywordParamKind::CombatRadius);
	CHECK(Near(r1.paramValue, 750.0f));

	const auto rSmall = ParseKeywordEditorId("scf_combatradius_10");
	CHECK(rSmall.Recognized());
	CHECK(Near(rSmall.paramValue, kMinKeywordCombatRadius));

	const auto rHuge = ParseKeywordEditorId("SCF_CombatRadius_99999");
	CHECK(rHuge.Recognized());
	CHECK(Near(rHuge.paramValue, kMaxKeywordCombatRadius));

	// Reject malformed suffixes
	CHECK(!ParseKeywordEditorId("SCF_CombatRadius_").Recognized());
	CHECK(!ParseKeywordEditorId("SCF_CombatRadius_0").Recognized());
	CHECK(!ParseKeywordEditorId("SCF_CombatRadius_abc").Recognized());
	CHECK(!ParseKeywordEditorId("SCF_CombatRadius_500extra").Recognized());
	CHECK(!ParseKeywordEditorId("SCF_Leash_-100").Recognized());

	// Merge: smallest positive wins for distances; SpeedMult stacks multiplicatively and clamps
	SummonKeywordProfile prof{};
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_CombatRadius_900"));
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_CombatRadius_400"));
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_CombatRadius_600"));
	CHECK(Near(prof.combatRadius, 400.0f));

	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_Leash_1500"));
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_Leash_600"));
	CHECK(Near(prof.leashRadius, 600.0f));

	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_FollowDist_350"));
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_FollowDist_180"));
	CHECK(Near(prof.followDistance, 180.0f));

	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_SpeedMult_150"));
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_SpeedMult_120"));
	CHECK(Near(prof.speedMultPercent, 180.0f));

	// Clamp at max 400
	MergeKeywordIntoProfile(prof, ParseKeywordEditorId("SCF_SpeedMult_300"));
	CHECK(Near(prof.speedMultPercent, kMaxKeywordSpeedMult));
}

// =========================================================================
// Keyword API v2: Enemy Threat Selection (Untargetable / HighThreat / LowThreat)
// =========================================================================

TEST(TestKeywordV2EnemyThreatSelection)
{
	// HighThreat pulls aggro from a normal target to the closest HighThreat.
	const std::vector<ThreatCandidate> mixed{
		{ 0x10, false, false, false, false, 100.0f },  // normal (engine answer)
		{ 0x20, false, true,  false, false, 400.0f },  // far tank
		{ 0x30, false, true,  false, false, 200.0f },  // near tank
	};
	CHECK(ChooseEnemyTargetByThreat(0x10, mixed) == 0x30);
	// Stickiness: if engine already picked a valid HighThreat, keep it.
	CHECK(ChooseEnemyTargetByThreat(0x20, mixed) == 0x20);

	// Untargetable overrides HighThreat (an untargetable actor cannot draw aggro).
	const std::vector<ThreatCandidate> untargetableTank{
		{ 0x10, false, false, false, false, 100.0f },
		{ 0x20, true,  true,  false, false, 50.0f },
	};
	CHECK(ChooseEnemyTargetByThreat(0x20, untargetableTank) == 0x10);

	// LowThreat deprioritizes when a normal target is available, but stays fallback when alone.
	const std::vector<ThreatCandidate> lowAndNormal{
		{ 0x10, false, false, true,  false, 80.0f },
		{ 0x20, false, false, false, false, 300.0f },
	};
	CHECK(ChooseEnemyTargetByThreat(0x10, lowAndNormal) == 0x20);

	const std::vector<ThreatCandidate> onlyLow{
		{ 0x10, false, false, true, false, 80.0f },
	};
	CHECK(ChooseEnemyTargetByThreat(0x10, onlyLow) == 0x10);

	// Untargetable falls back to LowThreat before returning 0.
	const std::vector<ThreatCandidate> untargetableAndLow{
		{ 0x10, true,  false, false, false, 50.0f },
		{ 0x20, false, false, true,  false, 250.0f },
	};
	CHECK(ChooseEnemyTargetByThreat(0x10, untargetableAndLow) == 0x20);
}

// =========================================================================
// Keyword API v2: Summon Targeting Specialties
// =========================================================================

TEST(TestKeywordV2SummonSpecialties)
{
	const std::vector<SpecialtyCandidate> enemies{
		// handle, lost, casting, ranged, hpFrac, attackingOwner, distOwner, distSummon
		{ 0x01, false, false, false, 1.00f, false, 600.0f, 100.0f },  // melee brute
		{ 0x02, false, true,  false, 0.90f, false, 700.0f, 300.0f },  // caster
		{ 0x03, false, false, true,  0.80f, false, 800.0f, 250.0f },  // archer
		{ 0x04, false, false, false, 0.20f, false, 900.0f, 400.0f },  // low-HP finisher target
		{ 0x05, false, false, false, 0.95f, true,  150.0f, 350.0f },  // attacking owner
	};

	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwHuntCasters, enemies) == 0x02);
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwHuntRanged, enemies) == 0x03);
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwFinishLowHP, enemies) == 0x04);
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwBodyguard, enemies) == 0x05);

	// Priority: Bodyguard > FinishLowHP > HuntCasters > HuntRanged
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwBodyguard | kKwHuntCasters, enemies) == 0x05);
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwFinishLowHP | kKwHuntCasters, enemies) == 0x04);
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwHuntCasters | kKwHuntRanged, enemies) == 0x02);

	// Stickiness: keep engineAnswer if it already matches the active specialty.
	const std::vector<SpecialtyCandidate> twoCasters{
		{ 0x02, false, true, false, 0.90f, false, 700.0f, 500.0f },
		{ 0x06, false, true, false, 0.90f, false, 400.0f, 100.0f },
	};
	CHECK(ChooseSummonTargetBySpecialty(0x02, kKwHuntCasters, twoCasters) == 0x02);
	CHECK(ChooseSummonTargetBySpecialty(0x01, kKwHuntCasters, twoCasters) == 0x06);
}

// =========================================================================
// Keyword API v2: Command Filtering, Default Aggression & Combat Policy
// =========================================================================

TEST(TestKeywordV2CommandFilteringAndDefaults)
{
	SummonKeywordProfile ignoreAll{};
	ignoreAll.flags = kKwIgnoreOrders;
	CHECK(!ShouldAcceptCommand(ignoreAll, CommandType::SetOrder, SummonOrder::Follow, CommandPriority::PlayerDirect));
	CHECK(!ShouldAcceptCommand(ignoreAll, CommandType::SetFocusTarget, SummonOrder::Attack, CommandPriority::PlayerDirect));
	CHECK(!ShouldAcceptCommand(ignoreAll, CommandType::SetAggression, SummonOrder::Follow, CommandPriority::PlayerDirect, AggressionMode::Passive));
	CHECK(ShouldAcceptCommand(ignoreAll, CommandType::Dispel, SummonOrder::Follow, CommandPriority::PlayerDirect));

	SummonKeywordProfile selective{};
	selective.flags = kKwIgnoreHold | kKwNoAssist;
	CHECK(!ShouldAcceptCommand(selective, CommandType::SetOrder, SummonOrder::WaitGuard, CommandPriority::PlayerDirect));
	CHECK(ShouldAcceptCommand(selective, CommandType::SetOrder, SummonOrder::Follow, CommandPriority::PlayerDirect));
	// Tactical assist rejected, PlayerDirect attack accepted
	CHECK(!ShouldAcceptCommand(selective, CommandType::SetFocusTarget, SummonOrder::Attack, CommandPriority::Tactical));
	CHECK(ShouldAcceptCommand(selective, CommandType::SetFocusTarget, SummonOrder::Attack, CommandPriority::PlayerDirect));

	SummonKeywordProfile nonCombat{};
	nonCombat.flags = kKwNonCombat;
	CHECK(!ShouldAcceptCommand(nonCombat, CommandType::SetFocusTarget, SummonOrder::Attack, CommandPriority::PlayerDirect));
	CHECK(!ShouldAcceptCommand(nonCombat, CommandType::SetAggression, SummonOrder::Follow, CommandPriority::PlayerDirect, AggressionMode::Aggressive));
	CHECK(ShouldAcceptCommand(nonCombat, CommandType::SetOrder, SummonOrder::Follow, CommandPriority::PlayerDirect));
	CHECK(DefaultAggressionFromKeywords(nonCombat) == AggressionMode::Passive);

	// Default aggression precedence: DefaultPassive > DefaultDefensive > DefaultAggressive
	SummonKeywordProfile defAggr{};
	defAggr.flags = kKwDefaultAggressive;
	CHECK(DefaultAggressionFromKeywords(defAggr) == AggressionMode::Aggressive);
	defAggr.flags |= kKwDefaultDefensive;
	CHECK(DefaultAggressionFromKeywords(defAggr) == AggressionMode::Defensive);
	defAggr.flags |= kKwDefaultPassive;
	CHECK(DefaultAggressionFromKeywords(defAggr) == AggressionMode::Passive);

	// Stealth hold-fire & NonCombat policy enforcement
	CombatPolicy open{};
	open.allowCombat = true;
	open.disableAttacks = false;

	SummonKeywordProfile stealth{};
	stealth.flags = kKwHoldFireInStealth;
	const auto hidden = ApplyKeywordCombatPolicy(open, stealth, true, false, false);
	CHECK(!hidden.allowCombat && hidden.disableAttacks);
	const auto detected = ApplyKeywordCombatPolicy(open, stealth, true, true, false);
	CHECK(detected.allowCombat && !detected.disableAttacks);
	const auto explicitAttack = ApplyKeywordCombatPolicy(open, stealth, true, false, true);
	CHECK(explicitAttack.allowCombat && !explicitAttack.disableAttacks);

	const auto nonCombatPolicy = ApplyKeywordCombatPolicy(open, nonCombat, false, true, true);
	CHECK(!nonCombatPolicy.allowCombat && nonCombatPolicy.disableAttacks);
}

// =========================================================================
// Keyword API v2: Settings Override, Formation Sectors, Stationary & Wolf
// =========================================================================

TEST(TestKeywordV2SettingsFormationStationaryAndWolf)
{
	SettingsData base{};
	base.movement.followMinDistance = 150.0f;
	base.movement.followMaxDistance = 300.0f;
	base.movement.followWalkSpeed = 100.0f;
	base.movement.followRunSpeed = 150.0f;
	base.movement.sprintSpeed = 200.0f;
	base.combat.enableCombatLeash = true;
	base.combat.combatLeashRadius = 1200.0f;
	base.recovery.enableCatchUpWarp = true;

	SummonKeywordProfile prof{};
	prof.flags = kKwNoCombatLeash | kKwNoCatchUpWarp | kKwFormationFront;
	prof.followDistance = 400.0f;
	prof.speedMultPercent = 150.0f;

	const auto tuned = ApplyKeywordSettingsOverride(base, prof);
	CHECK(!tuned.combat.enableCombatLeash);
	CHECK(!tuned.recovery.enableCatchUpWarp);
	CHECK(Near(tuned.movement.followMinDistance, 400.0f));
	CHECK(tuned.movement.followMaxDistance > tuned.movement.followMinDistance);
	CHECK(Near(tuned.movement.followWalkSpeed, 150.0f));
	CHECK(Near(tuned.movement.followRunSpeed, 225.0f));
	CHECK(Near(tuned.movement.sprintSpeed, 300.0f));

	// Formation sectors: Front (+Y), Flank (|X| large, Y~0), Back (-Y)
	CHECK(FormationSectorFromKeywords(prof) == FormationSector::Front);
	const auto frontPos = FormationSlotOffsetLocalForSector(FormationSector::Front, 0, 1, 200.0f);
	const auto flankPos = FormationSlotOffsetLocalForSector(FormationSector::Flank, 0, 2, 200.0f);
	const auto backPos  = FormationSlotOffsetLocalForSector(FormationSector::Back,  0, 1, 200.0f);
	CHECK(frontPos.y > 100.0f);
	CHECK(std::abs(flankPos.x) > 100.0f);
	CHECK(backPos.y < -100.0f);

	// Stationary turret always holds position and releases vanilla follow leash
	SummonModel model{};
	model.order = SummonOrder::Follow;
	const auto stationaryIntent = BuildMovementIntent(
		model, false, true, base, { 0, 0, 0 }, { 500, 0, 0 }, 0.0f, 0, 1, FormationSector::Front, true);
	CHECK(stationaryIntent.mode == MovementMode::HoldPosition);
	CHECK(stationaryIntent.lockTranslation);
	CHECK(ReleasesFollowLeash(model, true));
	CHECK(!ReleasesFollowLeash(model, false));

	// Wolf maneuver alwaysBlock (SCF_NoCircling) works even without a FocusTarget pin
	CHECK(!BlocksWolfManeuver(false, 0, 0x1234, false));
	CHECK(BlocksWolfManeuver(false, 0, 0x1234, true));
	CHECK(!BlocksWolfManeuver(false, 0, 0, true));
}

// =========================================================================
// Keyword API v2: Friendly Fire, Persistence, Command Actions, Radius, FreeSlot
// =========================================================================

TEST(TestKeywordV2FriendlyFirePersistenceActionsRadiusAndFreeSlot)
{
	// FriendlyFire overrides (AllowFriendlyFire > FriendlyFireProtected > base)
	CHECK(ResolveFriendlyFireProtection(false, kKwFriendlyFireProtected));
	CHECK(!ResolveFriendlyFireProtection(true, kKwAllowFriendlyFire));
	CHECK(!ResolveFriendlyFireProtection(true, kKwFriendlyFireProtected | kKwAllowFriendlyFire));
	CHECK(ResolveFriendlyFireProtection(true, kKwNone));
	CHECK(!ResolveFriendlyFireProtection(false, kKwNone));

	// Persistence overrides (baseAllowed == false always loses; NoPersistence > Persistent > global)
	CHECK(ResolvePersistencePolicy(true, false, kKwPersistent));
	CHECK(!ResolvePersistencePolicy(true, true, kKwNoPersistence));
	CHECK(!ResolvePersistencePolicy(true, true, kKwPersistent | kKwNoPersistence));
	CHECK(!ResolvePersistencePolicy(false, true, kKwPersistent));
	CHECK(ResolvePersistencePolicy(true, true, kKwNone));
	CHECK(!ResolvePersistencePolicy(true, false, kKwNone));

	// Command action overrides (CmdDetonate > CmdRush)
	SummonKeywordProfile cmdProf{};
	CHECK(!ResolveCommandActionOverride(cmdProf).has_value());
	cmdProf.flags = kKwCmdRush;
	CHECK(ResolveCommandActionOverride(cmdProf) == CommandAction::Rush);
	cmdProf.flags |= kKwCmdDetonate;
	CHECK(ResolveCommandActionOverride(cmdProf) == CommandAction::Detonate);

	// EffectiveCombatRadius (IgnoreCombatRadius > smallest positive of SCR and SCF_CombatRadius_<N>)
	SummonKeywordProfile radProf{};
	CHECK(Near(EffectiveCombatRadius(500.0f, radProf), 500.0f));
	radProf.combatRadius = 300.0f;
	CHECK(Near(EffectiveCombatRadius(500.0f, radProf), 300.0f));
	CHECK(Near(EffectiveCombatRadius(200.0f, radProf), 200.0f));
	CHECK(Near(EffectiveCombatRadius(0.0f, radProf), 300.0f));
	radProf.flags = kKwIgnoreCombatRadius;
	CHECK(Near(EffectiveCombatRadius(500.0f, radProf), 0.0f));

	// SummonRegistry freeSlot: does not consume conjuration slots or get displaced
	SummonRegistry reg;
	reg.SetOwnerMaxSummons(kPlayer, 1);

	EntityID displaced = InvalidEntityID;
	reg.RegisterSummon(kPlayer, 0x101, SummonType::SummonCreature, 60.0f, 1.0f, &displaced, false, false);
	CHECK(displaced == InvalidEntityID);
	CHECK(reg.OccupiesConjurationSlot(0x101));

	// Register a free-slot summon: must NOT displace 0x101 and must NOT occupy a conjuration slot.
	displaced = InvalidEntityID;
	reg.RegisterSummon(kPlayer, 0x102, SummonType::SummonCreature, 60.0f, 2.0f, &displaced, false, true);
	CHECK(displaced == InvalidEntityID);
	CHECK(!reg.OccupiesConjurationSlot(0x102));
	CHECK(reg.GetActiveSummonsForOwner(kPlayer).size() == 2);

	// Register another normal summon: must displace 0x101 (NOT the free-slot 0x102).
	displaced = InvalidEntityID;
	reg.RegisterSummon(kPlayer, 0x103, SummonType::SummonCreature, 60.0f, 3.0f, &displaced, false, false);
	CHECK(displaced == 0x101);
	CHECK(reg.IsRegistered(0x102));
	CHECK(reg.IsRegistered(0x103));
	CHECK(reg.GetActiveSummonsForOwner(kPlayer).size() == 2);
}
