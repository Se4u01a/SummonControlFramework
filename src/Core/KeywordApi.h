#pragma once

#include "Core/CombatPolicy.h"
#include "Core/CommandActions.h"
#include "Core/CommandQueue.h"
#include "Core/Formation.h"
#include "Core/PassThrough.h"
#include "Core/Settings.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace scf::core
{
	// Keyword API v2 for modders and users (zero-master: define KYWD records with
	// these EditorIDs in ANY plugin or distribute via KID/SPID).
	//
	// Where SCF reads them:
	//   * World keywords (collision, threat, friendly fire, auto-register): actor base
	//     NPC_, RACE, or active magic effect MGEF on any loaded actor.
	//   * Summon keywords & parametric keywords: actor base NPC_, RACE, active magic
	//     effect MGEF on the summon, AND the conjuration spell's source MGEF
	//     (ActiveEffect::GetBaseObject()) on the owner.
	//
	// All EditorIDs compare case-insensitively.

	// --- Collision / PassThrough (World) ---
	inline constexpr std::string_view kKeywordPassThroughFriends = "SCF_PassThroughFriends";
	inline constexpr std::string_view kKeywordPassThroughAll     = "SCF_PassThroughAll";

	// --- Enemy Target Selection / Threat (World) ---
	inline constexpr std::string_view kKeywordUntargetable       = "SCF_Untargetable";
	inline constexpr std::string_view kKeywordHighThreat         = "SCF_HighThreat";
	inline constexpr std::string_view kKeywordTauntPriority      = "SCF_TauntPriority";
	inline constexpr std::string_view kKeywordLowThreat          = "SCF_LowThreat";

	// --- Friendly Fire (World) ---
	inline constexpr std::string_view kKeywordFriendlyFireProtected = "SCF_FriendlyFireProtected";
	inline constexpr std::string_view kKeywordAllowFriendlyFire     = "SCF_AllowFriendlyFire";

	// --- Lifecycle / Registration / Limit / Persistence ---
	inline constexpr std::string_view kKeywordAutoRegister       = "SCF_AutoRegister";
	inline constexpr std::string_view kKeywordUnmanaged          = "SCF_Unmanaged";
	inline constexpr std::string_view kKeywordIgnoreSummon       = "SCF_IgnoreSummon";
	inline constexpr std::string_view kKeywordFreeSlot           = "SCF_FreeSlot";
	inline constexpr std::string_view kKeywordIgnoreLimit        = "SCF_IgnoreLimit";
	inline constexpr std::string_view kKeywordPersistent         = "SCF_Persistent";
	inline constexpr std::string_view kKeywordNoPersistence      = "SCF_NoPersistence";

	// --- Combat Mode, Orders, Assist, Stealth, Specialty ---
	inline constexpr std::string_view kKeywordNonCombat          = "SCF_NonCombat";
	inline constexpr std::string_view kKeywordAlwaysPassive      = "SCF_AlwaysPassive";
	inline constexpr std::string_view kKeywordDefaultPassive     = "SCF_DefaultPassive";
	inline constexpr std::string_view kKeywordDefaultDefensive   = "SCF_DefaultDefensive";
	inline constexpr std::string_view kKeywordDefaultAggressive  = "SCF_DefaultAggressive";
	inline constexpr std::string_view kKeywordIgnoreOrders       = "SCF_IgnoreOrders";
	inline constexpr std::string_view kKeywordIgnoreHold         = "SCF_IgnoreHold";
	inline constexpr std::string_view kKeywordIgnoreFollow       = "SCF_IgnoreFollow";
	inline constexpr std::string_view kKeywordIgnoreAttackOrder  = "SCF_IgnoreAttackOrder";
	inline constexpr std::string_view kKeywordNoAssist           = "SCF_NoAssist";
	inline constexpr std::string_view kKeywordNoCircling         = "SCF_NoCircling";
	inline constexpr std::string_view kKeywordDirectAssault      = "SCF_DirectAssault";
	inline constexpr std::string_view kKeywordBodyguard          = "SCF_Bodyguard";
	inline constexpr std::string_view kKeywordHuntCasters        = "SCF_HuntCasters";
	inline constexpr std::string_view kKeywordHuntRanged         = "SCF_HuntRanged";
	inline constexpr std::string_view kKeywordFinishLowHP        = "SCF_FinishLowHP";
	inline constexpr std::string_view kKeywordHoldFireInStealth  = "SCF_HoldFireInStealth";
	inline constexpr std::string_view kKeywordIgnoreCombatRadius = "SCF_IgnoreCombatRadius";

	// --- Movement, Stance, Formation, Leash, Warp ---
	inline constexpr std::string_view kKeywordStationary         = "SCF_Stationary";
	inline constexpr std::string_view kKeywordTurret             = "SCF_Turret";
	inline constexpr std::string_view kKeywordNoCatchUpWarp      = "SCF_NoCatchUpWarp";
	inline constexpr std::string_view kKeywordNoCombatLeash      = "SCF_NoCombatLeash";
	inline constexpr std::string_view kKeywordFormationFront     = "SCF_FormationFront";
	inline constexpr std::string_view kKeywordRoleTank           = "SCF_RoleTank";
	inline constexpr std::string_view kKeywordFormationFlank     = "SCF_FormationFlank";
	inline constexpr std::string_view kKeywordRoleMelee          = "SCF_RoleMelee";
	inline constexpr std::string_view kKeywordFormationBack      = "SCF_FormationBack";
	inline constexpr std::string_view kKeywordRoleRanged         = "SCF_RoleRanged";
	inline constexpr std::string_view kKeywordRoleSupport        = "SCF_RoleSupport";

	// --- Command Actions (PoE2 M3) ---
	inline constexpr std::string_view kKeywordCmdRush            = "SCF_CmdRush";
	inline constexpr std::string_view kKeywordCmdDetonate        = "SCF_CmdDetonate";

	// --- Parametric Numeric Prefixes (SCF_<Param>_<N>) ---
	inline constexpr std::string_view kPrefixCombatRadius        = "SCF_CombatRadius_";
	inline constexpr std::string_view kPrefixLeash               = "SCF_Leash_";
	inline constexpr std::string_view kPrefixFollowDist          = "SCF_FollowDist_";
	inline constexpr std::string_view kPrefixSpeedMult           = "SCF_SpeedMult_";

	// 64-bit keyword flag bitmask. Lower 3 bits match PassThroughFlag exactly.
	using KeywordFlags = std::uint64_t;

	inline constexpr KeywordFlags kKwNone                  = 0ULL;
	inline constexpr KeywordFlags kKwPassThroughFriends    = 1ULL << 0;
	inline constexpr KeywordFlags kKwPassThroughAll        = 1ULL << 1;
	inline constexpr KeywordFlags kKwUntargetable          = 1ULL << 2;
	inline constexpr KeywordFlags kKwHighThreat            = 1ULL << 3;
	inline constexpr KeywordFlags kKwLowThreat             = 1ULL << 4;
	inline constexpr KeywordFlags kKwFriendlyFireProtected = 1ULL << 5;
	inline constexpr KeywordFlags kKwAllowFriendlyFire     = 1ULL << 6;
	inline constexpr KeywordFlags kKwAutoRegister          = 1ULL << 7;
	inline constexpr KeywordFlags kKwUnmanaged             = 1ULL << 8;
	inline constexpr KeywordFlags kKwFreeSlot              = 1ULL << 9;
	inline constexpr KeywordFlags kKwPersistent            = 1ULL << 10;
	inline constexpr KeywordFlags kKwNoPersistence         = 1ULL << 11;
	inline constexpr KeywordFlags kKwNonCombat             = 1ULL << 12;
	inline constexpr KeywordFlags kKwDefaultPassive        = 1ULL << 13;
	inline constexpr KeywordFlags kKwDefaultDefensive      = 1ULL << 14;
	inline constexpr KeywordFlags kKwDefaultAggressive     = 1ULL << 15;
	inline constexpr KeywordFlags kKwIgnoreOrders          = 1ULL << 16;
	inline constexpr KeywordFlags kKwIgnoreHold            = 1ULL << 17;
	inline constexpr KeywordFlags kKwIgnoreFollow          = 1ULL << 18;
	inline constexpr KeywordFlags kKwIgnoreAttackOrder     = 1ULL << 19;
	inline constexpr KeywordFlags kKwNoAssist              = 1ULL << 20;
	inline constexpr KeywordFlags kKwNoCircling            = 1ULL << 21;
	inline constexpr KeywordFlags kKwBodyguard             = 1ULL << 22;
	inline constexpr KeywordFlags kKwHuntCasters           = 1ULL << 23;
	inline constexpr KeywordFlags kKwHuntRanged            = 1ULL << 24;
	inline constexpr KeywordFlags kKwFinishLowHP           = 1ULL << 25;
	inline constexpr KeywordFlags kKwHoldFireInStealth     = 1ULL << 26;
	inline constexpr KeywordFlags kKwIgnoreCombatRadius    = 1ULL << 27;
	inline constexpr KeywordFlags kKwStationary            = 1ULL << 28;
	inline constexpr KeywordFlags kKwNoCatchUpWarp         = 1ULL << 29;
	inline constexpr KeywordFlags kKwNoCombatLeash         = 1ULL << 30;
	inline constexpr KeywordFlags kKwFormationFront        = 1ULL << 31;
	inline constexpr KeywordFlags kKwFormationFlank        = 1ULL << 32;
	inline constexpr KeywordFlags kKwFormationBack         = 1ULL << 33;
	inline constexpr KeywordFlags kKwCmdRush               = 1ULL << 34;
	inline constexpr KeywordFlags kKwCmdDetonate           = 1ULL << 35;

	static_assert(kKwPassThroughFriends == kPassThroughFriends, "PassThroughFlag bit 0 mismatch");
	static_assert(kKwPassThroughAll == kPassThroughAll, "PassThroughFlag bit 1 mismatch");
	static_assert(kKwUntargetable == kUntargetable, "PassThroughFlag bit 2 mismatch");

	inline constexpr KeywordFlags kKwPassThroughMask =
		kKwPassThroughFriends | kKwPassThroughAll | kKwUntargetable;

	inline constexpr KeywordFlags kKwThreatMask =
		kKwUntargetable | kKwHighThreat | kKwLowThreat;

	inline constexpr KeywordFlags kKwFriendlyFireMask =
		kKwFriendlyFireProtected | kKwAllowFriendlyFire;

	inline constexpr KeywordFlags kKwTargetingSpecialtyMask =
		kKwBodyguard | kKwHuntCasters | kKwHuntRanged | kKwFinishLowHP;

	// Flags that require scanning loaded world actors (not only player summons).
	inline constexpr KeywordFlags kKwWorldMask =
		kKwPassThroughMask | kKwThreatMask | kKwFriendlyFireMask | kKwAutoRegister;

	// Parametric keyword limits.
	inline constexpr float kMinKeywordCombatRadius = 50.0f;
	inline constexpr float kMaxKeywordCombatRadius = 10000.0f;
	inline constexpr float kMinKeywordLeash        = 100.0f;
	inline constexpr float kMaxKeywordLeash        = 20000.0f;
	inline constexpr float kMinKeywordFollowDist   = 60.0f;
	inline constexpr float kMaxKeywordFollowDist   = 2000.0f;
	inline constexpr float kMinKeywordSpeedMult    = 25.0f;
	inline constexpr float kMaxKeywordSpeedMult    = 400.0f;

	// Specialty thresholds.
	inline constexpr float kBodyguardAlertRadius   = 500.0f;
	inline constexpr float kFinishLowHpThreshold   = 0.35f;

	enum class KeywordParamKind : std::uint8_t
	{
		None = 0,
		CombatRadius,
		Leash,
		FollowDist,
		SpeedMult
	};

	struct ParsedKeyword
	{
		KeywordFlags flags{ kKwNone };
		KeywordParamKind paramKind{ KeywordParamKind::None };
		float paramValue{ 0.0f };

		[[nodiscard]] bool Recognized() const noexcept
		{
			return flags != kKwNone || paramKind != KeywordParamKind::None;
		}
	};

	// Aggregated keyword state for a single summon (from NPC_, RACE, active MGEFs,
	// and conjuration source MGEF).
	struct SummonKeywordProfile
	{
		KeywordFlags flags{ kKwNone };
		float combatRadius{ 0.0f };     // > 0: smallest positive SCF_CombatRadius_<N>
		float leashRadius{ 0.0f };      // > 0: smallest positive SCF_Leash_<N>
		float followDistance{ 0.0f };   // > 0: smallest positive SCF_FollowDist_<N>
		float speedMultPercent{ 0.0f }; // > 0: effective SCF_SpeedMult_<N> percent (100 = normal)

		[[nodiscard]] bool Has(KeywordFlags a_flag) const noexcept
		{
			return (flags & a_flag) != 0;
		}

		[[nodiscard]] bool Any() const noexcept
		{
			return flags != kKwNone ||
			       combatRadius > 0.0f ||
			       leashRadius > 0.0f ||
			       followDistance > 0.0f ||
			       speedMultPercent > 0.0f;
		}
	};

	// Parses any SCF_* keyword EditorID (flags, aliases, or parametric SCF_<Param>_<N>).
	ParsedKeyword ParseKeywordEditorId(std::string_view a_editorId) noexcept;

	// Backward-compatible helper: returns the lower-3-bit PassThroughFlag for a keyword EditorID.
	std::uint8_t FlagForKeywordEditorId(std::string_view a_editorId) noexcept;

	// True if any flag in `a_flags` requires world-actor scanning.
	[[nodiscard]] constexpr bool IsWorldKeywordFlags(KeywordFlags a_flags) noexcept
	{
		return (a_flags & kKwWorldMask) != 0;
	}

	// Merges one recognized keyword into a summon's aggregated profile.
	// Distance parameters use smallest positive wins; SpeedMult stacks multiplicatively
	// and clamps to [kMinKeywordSpeedMult, kMaxKeywordSpeedMult].
	void MergeKeywordIntoProfile(SummonKeywordProfile& a_profile, const ParsedKeyword& a_parsed) noexcept;

	// --- Enemy target selection (Untargetable / HighThreat / LowThreat) ---

	// One entry of a CombatGroup's target list, as the selector seat sees it (legacy v1).
	struct SelectorCandidate
	{
		std::uint32_t handle{ 0 };
		bool untargetable{ false };
		bool lost{ false };
	};

	// Backward-compatible v1 helper for SCF_Untargetable.
	std::uint32_t ChooseTargetAvoidingUntargetable(std::uint32_t a_engineAnswer, bool a_answerUntargetable,
		const std::vector<SelectorCandidate>& a_candidates) noexcept;

	struct ThreatCandidate
	{
		std::uint32_t handle{ 0 };
		bool untargetable{ false };
		bool highThreat{ false };
		bool lowThreat{ false };
		bool lost{ false };
		float distance{ 0.0f };
	};

	// Enemy target selection with Untargetable, HighThreat/TauntPriority, and LowThreat.
	// Rules:
	//   1. If any living, non-lost, non-untargetable candidate has HighThreat:
	//      keep a_engineAnswer if it is already one of them, else choose the closest HighThreat.
	//   2. Otherwise, if a_engineAnswer is untargetable:
	//      prefer the closest normal (!untargetable && !lowThreat && !lost) candidate;
	//      if none, fall back to the closest !untargetable && !lost candidate (even if lowThreat);
	//      else 0 (the main-thread sweep ends that fight).
	//   3. Otherwise, if a_engineAnswer is lowThreat:
	//      switch to the closest normal (!untargetable && !lowThreat && !lost) candidate if one exists;
	//      else keep a_engineAnswer.
	//   4. Otherwise keep a_engineAnswer.
	std::uint32_t ChooseEnemyTargetByThreat(
		std::uint32_t a_engineAnswer,
		const std::vector<ThreatCandidate>& a_candidates) noexcept;

	// --- Summon targeting specialty (Bodyguard / HuntCasters / HuntRanged / FinishLowHP) ---

	struct SpecialtyCandidate
	{
		std::uint32_t handle{ 0 };
		bool lost{ false };
		bool isCasting{ false };
		bool hasRangedWeapon{ false };
		float healthFraction{ 1.0f }; // [0..1], <= 0 means unknown/dead
		bool attackingOwner{ false };
		float distanceToOwner{ 0.0f };
		float distanceToSummon{ 0.0f };
	};

	// Rewrites a summon's automatic combat target when the summon carries targeting
	// specialty keywords and does NOT have an explicit FocusTarget pin.
	// Priority: Bodyguard > FinishLowHP > HuntCasters > HuntRanged.
	// Keeps a_engineAnswer when it already satisfies the active specialty (stickiness),
	// or when no candidate matches the specialty.
	std::uint32_t ChooseSummonTargetBySpecialty(
		std::uint32_t a_engineAnswer,
		KeywordFlags a_summonFlags,
		const std::vector<SpecialtyCandidate>& a_candidates) noexcept;

	// --- Command filtering, default mode, combat policy, settings & roles ---

	// Returns false if the summon's keywords reject the incoming command.
	// Lifecycle commands and Dispel are always accepted.
	bool ShouldAcceptCommand(
		const SummonKeywordProfile& a_profile,
		CommandType a_type,
		SummonOrder a_order,
		CommandPriority a_priority,
		AggressionMode a_aggression = AggressionMode::Defensive) noexcept;

	// Initial aggression mode applied once when a summon is first tracked by SCF.
	// Priority: NonCombat/DefaultPassive > DefaultDefensive > DefaultAggressive.
	std::optional<AggressionMode> DefaultAggressionFromKeywords(const SummonKeywordProfile& a_profile) noexcept;

	// Applies keyword combat restrictions (SCF_NonCombat, SCF_HoldFireInStealth) on top
	// of the base CombatPolicy computed from the summon's model.
	CombatPolicy ApplyKeywordCombatPolicy(
		CombatPolicy a_policy,
		const SummonKeywordProfile& a_profile,
		bool a_playerSneaking,
		bool a_playerDetected,
		bool a_isExplicitAttack) noexcept;

	// Returns a per-summon copy of SettingsData modified by parametric and movement/leash
	// keywords (SCF_FollowDist_<N>, SCF_Leash_<N>, SCF_NoCombatLeash, SCF_NoCatchUpWarp,
	// SCF_SpeedMult_<N>).
	SettingsData ApplyKeywordSettingsOverride(
		SettingsData a_settings,
		const SummonKeywordProfile& a_profile) noexcept;

	// Formation sector requested by keywords. Priority: Front > Flank > Back > Default.
	FormationSector FormationSectorFromKeywords(const SummonKeywordProfile& a_profile) noexcept;

	// Friendly fire override for any target actor:
	// AllowFriendlyFire wins over FriendlyFireProtected; if neither is set, returns a_baseProtected.
	bool ResolveFriendlyFireProtection(bool a_baseProtected, KeywordFlags a_targetFlags) noexcept;

	// Persistence override for a player summon:
	// If base eligibility is false (blacklisted, etc.), returns false.
	// Otherwise: NoPersistence -> false, Persistent -> true, else a_globalEnabled.
	bool ResolvePersistencePolicy(bool a_baseAllowed, bool a_globalEnabled, KeywordFlags a_summonFlags) noexcept;

	// PoE2 M3 command action override from keywords (CmdDetonate > CmdRush).
	std::optional<CommandAction> ResolveCommandActionOverride(const SummonKeywordProfile& a_profile) noexcept;

	// Combines SummonCombatRadius's radius with Keyword API v2's SCF_CombatRadius_<N>
	// and SCF_IgnoreCombatRadius. Returns 0 when combat radius is disabled/ignored.
	float EffectiveCombatRadius(float a_scrRadius, const SummonKeywordProfile& a_profile) noexcept;
}
