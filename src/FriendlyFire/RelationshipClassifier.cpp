#include "FriendlyFire/RelationshipClassifier.h"

#include <array>

namespace scf::friendly_fire
{
	namespace
	{
		// The "is this attack source ours to filter at all" gate, shared by the damage
		// matrix and the reaction rules (killmove, hit reaction). Hostile, neutral and
		// unknown sources are NEVER filtered: an unrecognised attacker must keep every
		// effect it has in vanilla.
		[[nodiscard]] bool IsFilteredSource(AttackSourceKind a_sourceKind, const FriendlyFireSettings& a_settings) noexcept
		{
			switch (a_sourceKind) {
			case AttackSourceKind::Player:
				return a_settings.filterPlayerAttacks;
			case AttackSourceKind::PlayerAlly:
				return a_settings.filterPlayerAllies;
			case AttackSourceKind::PlayerOwnedSummon:
				return a_settings.filterPlayerOwnedSummons;
			case AttackSourceKind::AlliedSummon:
				return a_settings.filterAlliedSummons;
			case AttackSourceKind::Neutral:
			case AttackSourceKind::Hostile:
			case AttackSourceKind::Unknown:
			default:
				return false;
			}
		}
	}

	AttackSourceKind RelationshipClassifier::ClassifySource(
		core::EntityID a_sourceId,
		const core::SummonRegistry& a_registry,
		core::EntityID a_playerId,
		IsAllyCallback a_isAllyCb,
		GetCommandingActorCallback a_cmdActorCb) noexcept
	{
		if (a_sourceId == core::InvalidEntityID) {
			return AttackSourceKind::Unknown;
		}

		if (a_sourceId == a_playerId) {
			return AttackSourceKind::Player;
		}

		// 1. Check if directly registered in SCF SummonRegistry
		const auto* summonRecord = a_registry.FindRecord(a_sourceId);
		if (summonRecord) {
			if (summonRecord->ownerId == a_playerId) {
				return AttackSourceKind::PlayerOwnedSummon;
			}
			if (a_isAllyCb && a_isAllyCb(summonRecord->ownerId)) {
				return AttackSourceKind::AlliedSummon;
			}
		}

		// 2. Traverse engine commanding chain with loop & depth guard
		if (a_cmdActorCb) {
			std::array<core::EntityID, MaxCommandChainDepth> visited{};
			std::size_t visitedCount = 0;
			core::EntityID curr = a_sourceId;

			for (std::size_t depth = 0; depth < MaxCommandChainDepth; ++depth) {
				core::EntityID commander = a_cmdActorCb(curr);
				if (commander == core::InvalidEntityID || commander == curr) {
					break;
				}

				// Loop detection
				bool loopDetected = false;
				for (std::size_t i = 0; i < visitedCount; ++i) {
					if (visited[i] == commander) {
						loopDetected = true;
						break;
					}
				}
				if (loopDetected) {
					break;
				}
				visited[visitedCount++] = commander;

				if (commander == a_playerId) {
					return AttackSourceKind::PlayerOwnedSummon;
				}
				if (a_isAllyCb && a_isAllyCb(commander)) {
					return AttackSourceKind::AlliedSummon;
				}

				curr = commander;
			}
		}

		// 3. Direct ally check (followers, companions, faction teammates)
		if (a_isAllyCb && a_isAllyCb(a_sourceId)) {
			return AttackSourceKind::PlayerAlly;
		}

		// Default fail-safe
		return AttackSourceKind::Unknown;
	}

	bool RelationshipClassifier::IsProtectedTarget(
		core::EntityID a_targetId,
		const core::SummonRegistry& a_registry,
		const FriendlyFireSettings& a_settings,
		core::EntityID a_playerId,
		IsAllyCallback a_isAllyCb) noexcept
	{
		if (a_targetId == core::InvalidEntityID || !a_settings.enabled) {
			return false;
		}

		const auto* record = a_registry.FindRecord(a_targetId);
		if (!record) {
			return false;
		}

		if (record->ownerId == a_playerId) {
			return a_settings.protectPlayerOwnedSummons;
		}

		if (a_settings.protectAllyOwnedSummons && a_isAllyCb && a_isAllyCb(record->ownerId)) {
			return true;
		}

		return false;
	}

	AttackKind RelationshipClassifier::ClassifyMagicAttack(
		bool a_areaTarget,
		bool a_concentration,
		bool a_enchantment,
		bool a_shout) noexcept
	{
		if (a_areaTarget) {
			return AttackKind::AreaEffect;
		}
		if (a_concentration) {
			return AttackKind::Concentration;
		}
		if (a_enchantment) {
			return AttackKind::Enchantment;
		}
		if (a_shout) {
			return AttackKind::Shout;
		}
		return AttackKind::MagicProjectile;
	}

	FilterDecision RelationshipClassifier::EvaluateAttack(
		AttackSourceKind a_sourceKind,
		AttackKind a_attackKind,
		bool a_isTargetProtected,
		const FriendlyFireSettings& a_settings) noexcept
	{
		FilterDecision decision{};

		if (!a_settings.enabled || !a_isTargetProtected) {
			return decision;
		}

		// Verify whether the source is configured to be filtered
		const bool shouldFilterSource = IsFilteredSource(a_sourceKind, a_settings);

		if (!shouldFilterSource) {
			return decision;
		}

		// 1. Damage Blocking
		switch (a_attackKind) {
		case AttackKind::Melee:
		case AttackKind::Sweep:
			decision.blockDamage = a_settings.blockMeleeDamage;
			break;
		case AttackKind::Arrow:
			decision.blockDamage = a_settings.blockArrowDamage;
			break;
		case AttackKind::PhysicalProjectile:
			decision.blockDamage = a_settings.blockPhysicalProjectileDamage;
			break;
		case AttackKind::MagicProjectile:
			decision.blockDamage = a_settings.blockMagicProjectileDamage;
			break;
		case AttackKind::Concentration:
			decision.blockDamage = a_settings.blockConcentrationDamage;
			break;
		case AttackKind::AreaEffect:
			decision.blockDamage = a_settings.blockAreaMagicDamage;
			break;
		case AttackKind::Shout:
			decision.blockDamage = a_settings.blockShoutDamage;
			break;
		case AttackKind::Enchantment:
			decision.blockDamage = a_settings.blockEnchantmentDamage;
			break;
		}

		// 2. Attack Pass-Through
		switch (a_attackKind) {
		case AttackKind::Melee:
		case AttackKind::Sweep:
			decision.passThrough = a_settings.meleePassThrough;
			break;
		case AttackKind::Arrow:
			decision.passThrough = a_settings.arrowPassThrough;
			break;
		case AttackKind::PhysicalProjectile:
			decision.passThrough = a_settings.physicalProjectilePassThrough;
			break;
		case AttackKind::MagicProjectile:
			decision.passThrough = a_settings.magicProjectilePassThrough;
			break;
		default:
			decision.passThrough = false;
			break;
		}

		// 3. Hostility suppression
		decision.suppressAggression = a_settings.suppressFriendlyAggression;

		// 4. Reactions. Both are independent of the damage toggles above: a hit can keep
		// its damage and still not start a fight, flinch or open a killmove.
		decision.suppressKillmove = a_settings.suppressFriendlyKillmoves;
		decision.suppressHitReaction = a_settings.suppressFriendlyHitReactions;

		return decision;
	}

	bool RelationshipClassifier::ShouldSuppressKillmove(
		AttackSourceKind a_sourceKind,
		bool a_isTargetProtected,
		const FriendlyFireSettings& a_settings) noexcept
	{
		if (!a_settings.enabled || !a_settings.suppressFriendlyKillmoves || !a_isTargetProtected) {
			return false;
		}
		return IsFilteredSource(a_sourceKind, a_settings);
	}

	bool RelationshipClassifier::ShouldSuppressHitReaction(
		AttackSourceKind a_sourceKind,
		bool a_isTargetProtected,
		const FriendlyFireSettings& a_settings) noexcept
	{
		if (!a_settings.enabled || !a_settings.suppressFriendlyHitReactions || !a_isTargetProtected) {
			return false;
		}
		return IsFilteredSource(a_sourceKind, a_settings);
	}
}
