#pragma once

#include "Core/EntityID.h"
#include "Core/SummonRegistry.h"
#include "FriendlyFire/FriendlyFireSettings.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace scf::friendly_fire
{
	enum class AttackSourceKind : std::uint8_t
	{
		Player,             // Player Character (default 0x14)
		PlayerAlly,         // Follower / companion / faction ally of player
		PlayerOwnedSummon,  // Summon registered to Player
		AlliedSummon,       // Summon registered to PlayerAlly
		Neutral,            // Neutral non-hostile actor
		Hostile,            // Hostile enemy
		Unknown             // Could not resolve (FAIL-SAFE: NEVER filter Unknown!)
	};

	enum class AttackKind : std::uint8_t
	{
		Melee,
		Sweep,
		Arrow,
		PhysicalProjectile,
		MagicProjectile,
		Concentration,
		AreaEffect,
		Shout,
		Enchantment
	};

	constexpr const char* ToString(AttackSourceKind a_kind) noexcept
	{
		switch (a_kind) {
		case AttackSourceKind::Player:            return "Player";
		case AttackSourceKind::PlayerAlly:        return "PlayerAlly";
		case AttackSourceKind::PlayerOwnedSummon: return "PlayerOwnedSummon";
		case AttackSourceKind::AlliedSummon:      return "AlliedSummon";
		case AttackSourceKind::Neutral:           return "Neutral";
		case AttackSourceKind::Hostile:           return "Hostile";
		case AttackSourceKind::Unknown:           return "Unknown";
		default:                                  return "Invalid";
		}
	}

	constexpr const char* ToString(AttackKind a_kind) noexcept
	{
		switch (a_kind) {
		case AttackKind::Melee:              return "Melee";
		case AttackKind::Sweep:              return "Sweep";
		case AttackKind::Arrow:              return "Arrow";
		case AttackKind::PhysicalProjectile: return "PhysicalProjectile";
		case AttackKind::MagicProjectile:    return "MagicProjectile";
		case AttackKind::Concentration:      return "Concentration";
		case AttackKind::AreaEffect:         return "AreaEffect";
		case AttackKind::Shout:              return "Shout";
		case AttackKind::Enchantment:        return "Enchantment";
		default:                             return "Invalid";
		}
	}

	struct FilterDecision
	{
		bool blockDamage{ false };
		bool passThrough{ false };
		// Stop the victim from treating a friendly attacker as an aggressor (the engine's
		// AIProcess aggressor bit). Independent of the damage toggles: a hit may keep its
		// damage and still not start a fight.
		bool suppressAggression{ false };
		// Refuse a killmove / paired "finishing" animation on a protected summon.
		bool suppressKillmove{ false };
		// Suppress the victim's stagger / hit reaction for this hit.
		bool suppressHitReaction{ false };
	};

	class RelationshipClassifier
	{
	public:
		using IsAllyCallback = std::function<bool(core::EntityID a_actorId)>;
		using GetCommandingActorCallback = std::function<core::EntityID(core::EntityID a_actorId)>;

		// Maximum depth for command chain traversal to prevent cycles/hangs
		static constexpr std::size_t MaxCommandChainDepth = 4;

		[[nodiscard]] static AttackSourceKind ClassifySource(
			core::EntityID a_sourceId,
			const core::SummonRegistry& a_registry,
			core::EntityID a_playerId = 0x14,
			IsAllyCallback a_isAllyCb = nullptr,
			GetCommandingActorCallback a_cmdActorCb = nullptr) noexcept;

		[[nodiscard]] static bool IsProtectedTarget(
			core::EntityID a_targetId,
			const core::SummonRegistry& a_registry,
			const FriendlyFireSettings& a_settings,
			core::EntityID a_playerId = 0x14,
			IsAllyCallback a_isAllyCb = nullptr) noexcept;

		// Kind of a magic effect application (MagicTarget::AddTarget): an explosion / area
		// target is AreaEffect, then concentration, enchantment, shout; anything else is a
		// MagicProjectile (aimed / touch / self delivered spell).
		[[nodiscard]] static AttackKind ClassifyMagicAttack(
			bool a_areaTarget,
			bool a_concentration,
			bool a_enchantment,
			bool a_shout) noexcept;

		[[nodiscard]] static FilterDecision EvaluateAttack(
			AttackSourceKind a_sourceKind,
			AttackKind a_attackKind,
			bool a_isTargetProtected,
			const FriendlyFireSettings& a_settings) noexcept;

		// Killmove admission (bSuppressFriendlyKillmoves). A killmove is not a damage
		// kind: it is a paired animation the engine starts on a valid victim, so the
		// answer must not depend on the damage toggles. Refused only for a source SCF
		// already filters and a target that is a protected summon; hostile, neutral and
		// unknown sources are never refused.
		[[nodiscard]] static bool ShouldSuppressKillmove(
			AttackSourceKind a_sourceKind,
			bool a_isTargetProtected,
			const FriendlyFireSettings& a_settings) noexcept;

		// Hit reaction / stagger suppression (bSuppressFriendlyHitReactions). Same gate
		// as the killmove rule and also independent of the damage toggles: a protected
		// summon can still take the damage and not flinch.
		[[nodiscard]] static bool ShouldSuppressHitReaction(
			AttackSourceKind a_sourceKind,
			bool a_isTargetProtected,
			const FriendlyFireSettings& a_settings) noexcept;
	};
}
