#include "Core/KeywordApi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace scf::core
{
	namespace
	{
		bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs) noexcept
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a_lhs.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a_lhs[i])) != std::tolower(static_cast<unsigned char>(a_rhs[i]))) {
					return false;
				}
			}
			return true;
		}

		bool StartsWithNoCase(std::string_view a_text, std::string_view a_prefix) noexcept
		{
			if (a_text.size() < a_prefix.size()) {
				return false;
			}
			return EqualsNoCase(a_text.substr(0, a_prefix.size()), a_prefix);
		}

		std::optional<std::uint32_t> ParsePositiveUint(std::string_view a_digits) noexcept
		{
			if (a_digits.empty() || a_digits.size() > 7) {
				return std::nullopt;
			}
			std::uint32_t value = 0;
			for (const char ch : a_digits) {
				if (ch < '0' || ch > '9') {
					return std::nullopt;
				}
				value = value * 10u + static_cast<std::uint32_t>(ch - '0');
			}
			if (value == 0u) {
				return std::nullopt;
			}
			return value;
		}

		struct FlagEntry
		{
			std::string_view editorId;
			KeywordFlags flag;
		};

		inline constexpr FlagEntry kExactKeywords[] = {
			// Collision / PassThrough
			{ kKeywordPassThroughFriends,    kKwPassThroughFriends },
			{ kKeywordPassThroughAll,        kKwPassThroughAll },

			// Threat / Enemy Target Selection
			{ kKeywordUntargetable,          kKwUntargetable },
			{ kKeywordHighThreat,            kKwHighThreat },
			{ kKeywordTauntPriority,         kKwHighThreat },
			{ kKeywordLowThreat,             kKwLowThreat },

			// Friendly Fire
			{ kKeywordFriendlyFireProtected, kKwFriendlyFireProtected },
			{ kKeywordAllowFriendlyFire,     kKwAllowFriendlyFire },

			// Lifecycle / Registration / Limit / Persistence
			{ kKeywordAutoRegister,          kKwAutoRegister },
			{ kKeywordUnmanaged,             kKwUnmanaged },
			{ kKeywordIgnoreSummon,          kKwUnmanaged },
			{ kKeywordFreeSlot,              kKwFreeSlot },
			{ kKeywordIgnoreLimit,           kKwFreeSlot },
			{ kKeywordPersistent,            kKwPersistent },
			{ kKeywordNoPersistence,         kKwNoPersistence },

			// Combat Mode, Orders, Assist, Stealth, Specialty
			{ kKeywordNonCombat,             kKwNonCombat },
			{ kKeywordAlwaysPassive,         kKwNonCombat },
			{ kKeywordDefaultPassive,        kKwDefaultPassive },
			{ kKeywordDefaultDefensive,      kKwDefaultDefensive },
			{ kKeywordDefaultAggressive,     kKwDefaultAggressive },
			{ kKeywordIgnoreOrders,          kKwIgnoreOrders },
			{ kKeywordIgnoreHold,            kKwIgnoreHold },
			{ kKeywordIgnoreFollow,          kKwIgnoreFollow },
			{ kKeywordIgnoreAttackOrder,     kKwIgnoreAttackOrder },
			{ kKeywordNoAssist,              kKwNoAssist },
			{ kKeywordNoCircling,            kKwNoCircling },
			{ kKeywordDirectAssault,         kKwNoCircling },
			{ kKeywordBodyguard,             kKwBodyguard },
			{ kKeywordHuntCasters,           kKwHuntCasters },
			{ kKeywordHuntRanged,            kKwHuntRanged },
			{ kKeywordFinishLowHP,           kKwFinishLowHP },
			{ kKeywordHoldFireInStealth,     kKwHoldFireInStealth },
			{ kKeywordIgnoreCombatRadius,    kKwIgnoreCombatRadius },

			// Movement, Stance, Formation, Leash, Warp
			{ kKeywordStationary,            kKwStationary },
			{ kKeywordTurret,                kKwStationary },
			{ kKeywordNoCatchUpWarp,         kKwNoCatchUpWarp },
			{ kKeywordNoCombatLeash,         kKwNoCombatLeash },
			{ kKeywordFormationFront,        kKwFormationFront },
			{ kKeywordRoleTank,              kKwFormationFront },
			{ kKeywordFormationFlank,        kKwFormationFlank },
			{ kKeywordRoleMelee,             kKwFormationFlank },
			{ kKeywordFormationBack,         kKwFormationBack },
			{ kKeywordRoleRanged,            kKwFormationBack },
			{ kKeywordRoleSupport,           kKwFormationBack },

			// Command Actions (PoE2 M3)
			{ kKeywordCmdRush,               kKwCmdRush },
			{ kKeywordCmdDetonate,           kKwCmdDetonate }
		};

		void MergeSmallestPositive(float& a_target, float a_value) noexcept
		{
			if (!(a_value > 0.0f)) {
				return;
			}
			if (a_target <= 0.0f || a_value < a_target) {
				a_target = a_value;
			}
		}

		const ThreatCandidate* FindThreat(const std::vector<ThreatCandidate>& a_candidates, std::uint32_t a_handle) noexcept
		{
			if (a_handle == 0) {
				return nullptr;
			}
			for (const auto& candidate : a_candidates) {
				if (candidate.handle == a_handle) {
					return &candidate;
				}
			}
			return nullptr;
		}
	}

	ParsedKeyword ParseKeywordEditorId(std::string_view a_editorId) noexcept
	{
		if (a_editorId.size() < 5 || !StartsWithNoCase(a_editorId, "SCF_")) {
			return {};
		}

		for (const auto& entry : kExactKeywords) {
			if (EqualsNoCase(a_editorId, entry.editorId)) {
				return ParsedKeyword{ entry.flag, KeywordParamKind::None, 0.0f };
			}
		}

		if (StartsWithNoCase(a_editorId, kPrefixCombatRadius)) {
			if (const auto val = ParsePositiveUint(a_editorId.substr(kPrefixCombatRadius.size()))) {
				const float clamped = std::clamp(static_cast<float>(*val), kMinKeywordCombatRadius, kMaxKeywordCombatRadius);
				return ParsedKeyword{ kKwNone, KeywordParamKind::CombatRadius, clamped };
			}
			return {};
		}

		if (StartsWithNoCase(a_editorId, kPrefixLeash)) {
			if (const auto val = ParsePositiveUint(a_editorId.substr(kPrefixLeash.size()))) {
				const float clamped = std::clamp(static_cast<float>(*val), kMinKeywordLeash, kMaxKeywordLeash);
				return ParsedKeyword{ kKwNone, KeywordParamKind::Leash, clamped };
			}
			return {};
		}

		if (StartsWithNoCase(a_editorId, kPrefixFollowDist)) {
			if (const auto val = ParsePositiveUint(a_editorId.substr(kPrefixFollowDist.size()))) {
				const float clamped = std::clamp(static_cast<float>(*val), kMinKeywordFollowDist, kMaxKeywordFollowDist);
				return ParsedKeyword{ kKwNone, KeywordParamKind::FollowDist, clamped };
			}
			return {};
		}

		if (StartsWithNoCase(a_editorId, kPrefixSpeedMult)) {
			if (const auto val = ParsePositiveUint(a_editorId.substr(kPrefixSpeedMult.size()))) {
				const float clamped = std::clamp(static_cast<float>(*val), kMinKeywordSpeedMult, kMaxKeywordSpeedMult);
				return ParsedKeyword{ kKwNone, KeywordParamKind::SpeedMult, clamped };
			}
			return {};
		}

		return {};
	}

	std::uint8_t FlagForKeywordEditorId(std::string_view a_editorId) noexcept
	{
		const auto parsed = ParseKeywordEditorId(a_editorId);
		return static_cast<std::uint8_t>(parsed.flags & kKwPassThroughMask);
	}

	void MergeKeywordIntoProfile(SummonKeywordProfile& a_profile, const ParsedKeyword& a_parsed) noexcept
	{
		a_profile.flags |= a_parsed.flags;

		switch (a_parsed.paramKind) {
		case KeywordParamKind::CombatRadius:
			MergeSmallestPositive(a_profile.combatRadius, a_parsed.paramValue);
			break;
		case KeywordParamKind::Leash:
			MergeSmallestPositive(a_profile.leashRadius, a_parsed.paramValue);
			break;
		case KeywordParamKind::FollowDist:
			MergeSmallestPositive(a_profile.followDistance, a_parsed.paramValue);
			break;
		case KeywordParamKind::SpeedMult:
			if (a_parsed.paramValue > 0.0f) {
				if (a_profile.speedMultPercent <= 0.0f) {
					a_profile.speedMultPercent = std::clamp(a_parsed.paramValue, kMinKeywordSpeedMult, kMaxKeywordSpeedMult);
				} else {
					const float combined = (a_profile.speedMultPercent * a_parsed.paramValue) / 100.0f;
					a_profile.speedMultPercent = std::clamp(combined, kMinKeywordSpeedMult, kMaxKeywordSpeedMult);
				}
			}
			break;
		case KeywordParamKind::None:
		default:
			break;
		}
	}

	std::uint32_t ChooseTargetAvoidingUntargetable(std::uint32_t a_engineAnswer, bool a_answerUntargetable,
		const std::vector<SelectorCandidate>& a_candidates) noexcept
	{
		if (a_engineAnswer == 0 || !a_answerUntargetable) {
			return a_engineAnswer;
		}
		for (const auto& candidate : a_candidates) {
			if (candidate.handle != 0 && candidate.handle != a_engineAnswer && !candidate.untargetable && !candidate.lost) {
				return candidate.handle;
			}
		}
		return 0;
	}

	std::uint32_t ChooseEnemyTargetByThreat(
		std::uint32_t a_engineAnswer,
		const std::vector<ThreatCandidate>& a_candidates) noexcept
	{
		if (a_engineAnswer == 0) {
			return 0;
		}

		// 1. HighThreat / TauntPriority: draw enemies toward the closest eligible tank,
		//    keeping the engine's answer if it already targets a valid HighThreat actor.
		std::uint32_t bestHighHandle = 0;
		float bestHighDist = std::numeric_limits<float>::infinity();
		bool answerIsValidHigh = false;

		for (const auto& candidate : a_candidates) {
			if (candidate.handle == 0 || candidate.lost || candidate.untargetable) {
				continue;
			}
			if (candidate.highThreat) {
				if (candidate.handle == a_engineAnswer) {
					answerIsValidHigh = true;
				}
				if (candidate.distance < bestHighDist) {
					bestHighDist = candidate.distance;
					bestHighHandle = candidate.handle;
				}
			}
		}

		if (answerIsValidHigh) {
			return a_engineAnswer;
		}
		if (bestHighHandle != 0) {
			return bestHighHandle;
		}

		// 2. Inspect engine's current answer.
		const auto* answerInfo = FindThreat(a_candidates, a_engineAnswer);
		if (!answerInfo) {
			return a_engineAnswer;
		}

		if (!answerInfo->untargetable && !answerInfo->lowThreat) {
			return a_engineAnswer;
		}

		// Find best normal target (!untargetable && !lowThreat && !lost) and best fallback
		// (!untargetable && !lost, which may be lowThreat).
		std::uint32_t bestNormalHandle = 0;
		float bestNormalDist = std::numeric_limits<float>::infinity();
		std::uint32_t bestFallbackHandle = 0;
		float bestFallbackDist = std::numeric_limits<float>::infinity();

		for (const auto& candidate : a_candidates) {
			if (candidate.handle == 0 || candidate.handle == a_engineAnswer || candidate.lost || candidate.untargetable) {
				continue;
			}
			if (!candidate.lowThreat) {
				if (candidate.distance < bestNormalDist) {
					bestNormalDist = candidate.distance;
					bestNormalHandle = candidate.handle;
				}
			} else {
				if (candidate.distance < bestFallbackDist) {
					bestFallbackDist = candidate.distance;
					bestFallbackHandle = candidate.handle;
				}
			}
		}

		if (answerInfo->untargetable) {
			if (bestNormalHandle != 0) {
				return bestNormalHandle;
			}
			return bestFallbackHandle; // 0 if every remaining candidate is untargetable
		}

		// answerInfo->lowThreat: yield to any normal target, else stay on the lowThreat actor.
		return bestNormalHandle != 0 ? bestNormalHandle : a_engineAnswer;
	}

	std::uint32_t ChooseSummonTargetBySpecialty(
		std::uint32_t a_engineAnswer,
		KeywordFlags a_summonFlags,
		const std::vector<SpecialtyCandidate>& a_candidates) noexcept
	{
		if (a_engineAnswer == 0 || (a_summonFlags & kKwTargetingSpecialtyMask) == 0 || a_candidates.empty()) {
			return a_engineAnswer;
		}

		// 1. Bodyguard: prioritize enemies actively attacking the owner, else threats near the owner.
		if ((a_summonFlags & kKwBodyguard) != 0) {
			std::uint32_t bestAttacker = 0;
			float bestAttackerDist = std::numeric_limits<float>::infinity();
			bool answerAttacksOwner = false;

			std::uint32_t bestNearOwner = 0;
			float bestNearDist = std::numeric_limits<float>::infinity();
			bool answerNearOwner = false;

			for (const auto& candidate : a_candidates) {
				if (candidate.handle == 0 || candidate.lost) {
					continue;
				}
				if (candidate.attackingOwner) {
					if (candidate.handle == a_engineAnswer) {
						answerAttacksOwner = true;
					}
					if (candidate.distanceToOwner < bestAttackerDist) {
						bestAttackerDist = candidate.distanceToOwner;
						bestAttacker = candidate.handle;
					}
				}
				if (candidate.distanceToOwner <= kBodyguardAlertRadius) {
					if (candidate.handle == a_engineAnswer) {
						answerNearOwner = true;
					}
					if (candidate.distanceToOwner < bestNearDist) {
						bestNearDist = candidate.distanceToOwner;
						bestNearOwner = candidate.handle;
					}
				}
			}

			if (answerAttacksOwner) {
				return a_engineAnswer;
			}
			if (bestAttacker != 0) {
				return bestAttacker;
			}
			if (answerNearOwner) {
				return a_engineAnswer;
			}
			if (bestNearOwner != 0) {
				return bestNearOwner;
			}
		}

		// 2. FinishLowHP: pick the lowest-HP target at or below kFinishLowHpThreshold.
		if ((a_summonFlags & kKwFinishLowHP) != 0) {
			std::uint32_t bestLowHp = 0;
			float lowestFraction = kFinishLowHpThreshold + 0.0001f;
			float answerFraction = 2.0f;

			for (const auto& candidate : a_candidates) {
				if (candidate.handle == 0 || candidate.lost) {
					continue;
				}
				if (candidate.healthFraction > 0.0f && candidate.healthFraction <= kFinishLowHpThreshold) {
					if (candidate.handle == a_engineAnswer) {
						answerFraction = candidate.healthFraction;
					}
					if (candidate.healthFraction < lowestFraction) {
						lowestFraction = candidate.healthFraction;
						bestLowHp = candidate.handle;
					}
				}
			}

			if (answerFraction <= kFinishLowHpThreshold && (answerFraction - lowestFraction) <= 0.05f) {
				return a_engineAnswer;
			}
			if (bestLowHp != 0) {
				return bestLowHp;
			}
		}

		// 3. HuntCasters: prioritize spellcasters.
		if ((a_summonFlags & kKwHuntCasters) != 0) {
			std::uint32_t bestCaster = 0;
			float bestDist = std::numeric_limits<float>::infinity();
			bool answerIsCaster = false;

			for (const auto& candidate : a_candidates) {
				if (candidate.handle == 0 || candidate.lost || !candidate.isCasting) {
					continue;
				}
				if (candidate.handle == a_engineAnswer) {
					answerIsCaster = true;
				}
				if (candidate.distanceToSummon < bestDist) {
					bestDist = candidate.distanceToSummon;
					bestCaster = candidate.handle;
				}
			}

			if (answerIsCaster) {
				return a_engineAnswer;
			}
			if (bestCaster != 0) {
				return bestCaster;
			}
		}

		// 4. HuntRanged: prioritize bow/crossbow/staff ranged attackers.
		if ((a_summonFlags & kKwHuntRanged) != 0) {
			std::uint32_t bestRanged = 0;
			float bestDist = std::numeric_limits<float>::infinity();
			bool answerIsRanged = false;

			for (const auto& candidate : a_candidates) {
				if (candidate.handle == 0 || candidate.lost || !candidate.hasRangedWeapon) {
					continue;
				}
				if (candidate.handle == a_engineAnswer) {
					answerIsRanged = true;
				}
				if (candidate.distanceToSummon < bestDist) {
					bestDist = candidate.distanceToSummon;
					bestRanged = candidate.handle;
				}
			}

			if (answerIsRanged) {
				return a_engineAnswer;
			}
			if (bestRanged != 0) {
				return bestRanged;
			}
		}

		return a_engineAnswer;
	}

	bool ShouldAcceptCommand(
		const SummonKeywordProfile& a_profile,
		CommandType a_type,
		SummonOrder a_order,
		CommandPriority a_priority,
		AggressionMode a_aggression) noexcept
	{
		if (a_priority == CommandPriority::Lifecycle || a_type == CommandType::Dispel) {
			return true;
		}

		if (a_profile.Has(kKwIgnoreOrders)) {
			return false;
		}

		if (a_profile.Has(kKwNonCombat)) {
			if (a_type == CommandType::SetFocusTarget || a_type == CommandType::CommandAction) {
				return false;
			}
			if (a_type == CommandType::SetOrder && a_order == SummonOrder::Attack) {
				return false;
			}
			if (a_type == CommandType::SetAggression && a_aggression != AggressionMode::Passive) {
				return false;
			}
		}

		if (a_profile.Has(kKwNoAssist) && a_priority == CommandPriority::Tactical) {
			if (a_type == CommandType::SetFocusTarget) {
				return false;
			}
			if (a_type == CommandType::SetOrder && a_order == SummonOrder::Attack) {
				return false;
			}
		}

		if (a_profile.Has(kKwIgnoreHold) && a_type == CommandType::SetOrder && a_order == SummonOrder::WaitGuard) {
			return false;
		}

		if (a_profile.Has(kKwIgnoreFollow) && a_type == CommandType::SetOrder &&
		    (a_order == SummonOrder::Follow || a_order == SummonOrder::Return)) {
			return false;
		}

		if (a_profile.Has(kKwIgnoreAttackOrder)) {
			if (a_type == CommandType::SetFocusTarget || a_type == CommandType::CommandAction) {
				return false;
			}
			if (a_type == CommandType::SetOrder && a_order == SummonOrder::Attack) {
				return false;
			}
		}

		if (a_profile.Has(kKwStationary) && a_type == CommandType::SetOrder &&
		    (a_order == SummonOrder::MoveTo || a_order == SummonOrder::Return)) {
			return false;
		}

		return true;
	}

	std::optional<AggressionMode> DefaultAggressionFromKeywords(const SummonKeywordProfile& a_profile) noexcept
	{
		if (a_profile.Has(kKwNonCombat) || a_profile.Has(kKwDefaultPassive)) {
			return AggressionMode::Passive;
		}
		if (a_profile.Has(kKwDefaultDefensive)) {
			return AggressionMode::Defensive;
		}
		if (a_profile.Has(kKwDefaultAggressive)) {
			return AggressionMode::Aggressive;
		}
		return std::nullopt;
	}

	CombatPolicy ApplyKeywordCombatPolicy(
		CombatPolicy a_policy,
		const SummonKeywordProfile& a_profile,
		bool a_playerSneaking,
		bool a_playerDetected,
		bool a_isExplicitAttack) noexcept
	{
		if (a_profile.Has(kKwNonCombat)) {
			a_policy.allowCombat = false;
			a_policy.restrictToTarget = false;
			a_policy.target = InvalidEntityID;
			a_policy.disableAttacks = true;
			a_policy.radiusManaged = false;
			return a_policy;
		}

		if (a_profile.Has(kKwHoldFireInStealth) && a_playerSneaking && !a_playerDetected && !a_isExplicitAttack) {
			a_policy.allowCombat = false;
			a_policy.restrictToTarget = false;
			a_policy.target = InvalidEntityID;
			a_policy.disableAttacks = true;
			a_policy.radiusManaged = false;
			return a_policy;
		}

		return a_policy;
	}

	SettingsData ApplyKeywordSettingsOverride(
		SettingsData a_settings,
		const SummonKeywordProfile& a_profile) noexcept
	{
		if (a_profile.followDistance > 0.0f) {
			const float origMin = a_settings.movement.followMinDistance;
			const float origMax = a_settings.movement.followMaxDistance;
			const float ratio = (origMin > 1.0f && origMax > origMin) ? (origMax / origMin) : 2.25f;
			a_settings.movement.followMinDistance = a_profile.followDistance;
			a_settings.movement.followMaxDistance = std::max(a_profile.followDistance + 50.0f, a_profile.followDistance * ratio);
		}

		if (a_profile.leashRadius > 0.0f) {
			a_settings.combat.combatLeashRadius = a_profile.leashRadius;
			a_settings.combat.enableCombatLeash = true;
		}

		if (a_profile.Has(kKwNoCombatLeash)) {
			a_settings.combat.enableCombatLeash = false;
		}

		if (a_profile.Has(kKwNoCatchUpWarp)) {
			a_settings.recovery.enableCatchUpWarp = false;
		}

		if (a_profile.speedMultPercent > 0.0f) {
			const float scale = a_profile.speedMultPercent / 100.0f;
			a_settings.movement.followWalkSpeed = std::clamp(a_settings.movement.followWalkSpeed * scale, 25.0f, 500.0f);
			a_settings.movement.followRunSpeed = std::clamp(a_settings.movement.followRunSpeed * scale, 25.0f, 500.0f);
			a_settings.movement.sprintSpeed = std::clamp(a_settings.movement.sprintSpeed * scale, 25.0f, 500.0f);
		}

		return a_settings;
	}

	FormationSector FormationSectorFromKeywords(const SummonKeywordProfile& a_profile) noexcept
	{
		if (a_profile.Has(kKwFormationFront)) {
			return FormationSector::Front;
		}
		if (a_profile.Has(kKwFormationFlank)) {
			return FormationSector::Flank;
		}
		if (a_profile.Has(kKwFormationBack)) {
			return FormationSector::Back;
		}
		return FormationSector::Default;
	}

	bool ResolveFriendlyFireProtection(bool a_baseProtected, KeywordFlags a_targetFlags) noexcept
	{
		if ((a_targetFlags & kKwAllowFriendlyFire) != 0) {
			return false;
		}
		if ((a_targetFlags & kKwFriendlyFireProtected) != 0) {
			return true;
		}
		return a_baseProtected;
	}

	bool ResolvePersistencePolicy(bool a_baseAllowed, bool a_globalEnabled, KeywordFlags a_summonFlags) noexcept
	{
		if (!a_baseAllowed) {
			return false;
		}
		if ((a_summonFlags & kKwNoPersistence) != 0) {
			return false;
		}
		if ((a_summonFlags & kKwPersistent) != 0) {
			return true;
		}
		return a_globalEnabled;
	}

	std::optional<CommandAction> ResolveCommandActionOverride(const SummonKeywordProfile& a_profile) noexcept
	{
		if (a_profile.Has(kKwCmdDetonate)) {
			return CommandAction::Detonate;
		}
		if (a_profile.Has(kKwCmdRush)) {
			return CommandAction::Rush;
		}
		return std::nullopt;
	}

	float EffectiveCombatRadius(float a_scrRadius, const SummonKeywordProfile& a_profile) noexcept
	{
		if (a_profile.Has(kKwIgnoreCombatRadius)) {
			return 0.0f;
		}
		const bool hasScr = a_scrRadius > 0.0f;
		const bool hasKw = a_profile.combatRadius > 0.0f;
		if (hasScr && hasKw) {
			return std::min(a_scrRadius, a_profile.combatRadius);
		}
		if (hasKw) {
			return a_profile.combatRadius;
		}
		return hasScr ? a_scrRadius : 0.0f;
	}
}
