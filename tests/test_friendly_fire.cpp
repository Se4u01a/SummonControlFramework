#include "Core/Settings.h"
#include "Core/SummonRegistry.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "check.h"

using namespace scf;
using namespace scf::core;
using namespace scf::friendly_fire;

TEST(friendly_fire_settings_default_values)
{
	FriendlyFireSettings settings{};
	CHECK(settings.enabled);
	CHECK(settings.protectPlayerOwnedSummons);
	CHECK(!settings.protectAllyOwnedSummons);
	CHECK(settings.filterPlayerAttacks);
	CHECK(settings.filterPlayerAllies);
	CHECK(settings.filterPlayerOwnedSummons);
	CHECK(settings.filterAlliedSummons);

	// Damage block defaults
	CHECK(settings.blockMeleeDamage);
	CHECK(settings.blockArrowDamage);
	CHECK(settings.blockPhysicalProjectileDamage);
	CHECK(settings.blockMagicProjectileDamage);
	CHECK(settings.blockConcentrationDamage);
	CHECK(settings.blockAreaMagicDamage);
	CHECK(settings.blockShoutDamage);
	CHECK(settings.blockEnchantmentDamage);

	// Pass-through defaults
	CHECK(settings.meleePassThrough);
	CHECK(settings.arrowPassThrough);
	CHECK(!settings.physicalProjectilePassThrough);
	CHECK(!settings.magicProjectilePassThrough);

	// Suppression defaults
	CHECK(settings.suppressFriendlyAggression);
	CHECK(settings.suppressFriendlyKillmoves);
	CHECK(!settings.suppressFriendlyHitReactions);
}

TEST(friendly_fire_settings_parse_ini_and_serialization)
{
	Settings settings;
	const char* iniContent =
		"[FriendlyFire]\n"
		"bEnableSummonProtection = true\n"
		"bProtectPlayerOwnedSummons = true\n"
		"bProtectAllyOwnedSummons = true\n"
		"bFilterPlayerAttacks = true\n"
		"bFilterPlayerAllies = false\n"
		"bBlockMeleeDamage = true\n"
		"bBlockArrowDamage = false\n"
		"bMeleePassThrough = false\n"
		"bArrowPassThrough = true\n"
		"bSuppressFriendlyAggression = true\n";

	CHECK(settings.LoadFromString(iniContent));
	const auto& ff = settings.Get().friendlyFire;
	CHECK(ff.enabled);
	CHECK(ff.protectPlayerOwnedSummons);
	CHECK(ff.protectAllyOwnedSummons);
	CHECK(ff.filterPlayerAttacks);
	CHECK(!ff.filterPlayerAllies);
	CHECK(ff.blockMeleeDamage);
	CHECK(!ff.blockArrowDamage);
	CHECK(!ff.meleePassThrough);
	CHECK(ff.arrowPassThrough);

	// Round-trip serialization
	const std::string serialized = settings.Serialize();
	CHECK(serialized.find("[FriendlyFire]") != std::string::npos);
	CHECK(serialized.find("bProtectAllyOwnedSummons = true") != std::string::npos);
	CHECK(serialized.find("bFilterPlayerAllies = false") != std::string::npos);

	Settings reloaded;
	CHECK(reloaded.LoadFromString(serialized));
	CHECK(reloaded.Get().friendlyFire.protectAllyOwnedSummons);
	CHECK(!reloaded.Get().friendlyFire.filterPlayerAllies);
}

TEST(relationship_classifier_source_classification)
{
	SummonRegistry registry;
	const EntityID playerId = 0x14;
	const EntityID playerSummonId = 0x100;
	const EntityID allyId = 0x200;
	const EntityID allySummonId = 0x300;
	const EntityID hostileId = 0x666;

	registry.RegisterSummon(playerId, playerSummonId, SummonType::SummonCreature, 60.0f, 1.0f);
	registry.RegisterSummon(allyId, allySummonId, SummonType::SummonCreature, 60.0f, 1.0f);

	auto isAlly = [&](EntityID id) {
		return id == allyId;
	};

	// 1. Direct player
	CHECK(RelationshipClassifier::ClassifySource(playerId, registry, playerId, isAlly) == AttackSourceKind::Player);

	// 2. Player-owned summon from registry
	CHECK(RelationshipClassifier::ClassifySource(playerSummonId, registry, playerId, isAlly) == AttackSourceKind::PlayerOwnedSummon);

	// 3. Ally follower
	CHECK(RelationshipClassifier::ClassifySource(allyId, registry, playerId, isAlly) == AttackSourceKind::PlayerAlly);

	// 4. Allied summon from registry
	CHECK(RelationshipClassifier::ClassifySource(allySummonId, registry, playerId, isAlly) == AttackSourceKind::AlliedSummon);

	// 5. Hostile / Unknown
	CHECK(RelationshipClassifier::ClassifySource(hostileId, registry, playerId, isAlly) == AttackSourceKind::Unknown);
	CHECK(RelationshipClassifier::ClassifySource(InvalidEntityID, registry, playerId, isAlly) == AttackSourceKind::Unknown);

	// 6. Nested commanding chain (e.g. Modded summon of a follower's summon)
	const EntityID subSummonId = 0x400;
	auto cmdChain = [&](EntityID id) -> EntityID {
		if (id == subSummonId) return allySummonId;
		if (id == allySummonId) return allyId;
		return InvalidEntityID;
	};
	CHECK(RelationshipClassifier::ClassifySource(subSummonId, registry, playerId, isAlly, cmdChain) == AttackSourceKind::AlliedSummon);

	// 7. Loop guard: A -> B -> A must safely terminate and return Unknown without infinite loop
	const EntityID loopA = 0x801;
	const EntityID loopB = 0x802;
	auto loopChain = [&](EntityID id) -> EntityID {
		if (id == loopA) return loopB;
		if (id == loopB) return loopA;
		return InvalidEntityID;
	};
	CHECK(RelationshipClassifier::ClassifySource(loopA, registry, playerId, isAlly, loopChain) == AttackSourceKind::Unknown);
}

TEST(relationship_classifier_target_protection)
{
	SummonRegistry registry;
	FriendlyFireSettings settings{};
	const EntityID playerId = 0x14;
	const EntityID playerSummonId = 0x100;
	const EntityID allySummonId = 0x200;
	const EntityID enemySummonId = 0x300;
	const EntityID randomActorId = 0x999;

	registry.RegisterSummon(playerId, playerSummonId, SummonType::SummonCreature, 60.0f, 1.0f);
	registry.RegisterSummon(0x500, allySummonId, SummonType::SummonCreature, 60.0f, 1.0f); // 0x500 is ally
	registry.RegisterSummon(0x666, enemySummonId, SummonType::SummonCreature, 60.0f, 1.0f); // 0x666 is enemy

	auto isAlly = [](EntityID id) {
		return id == 0x500;
	};

	// Default settings: player summon is protected, ally summon is NOT protected
	CHECK(RelationshipClassifier::IsProtectedTarget(playerSummonId, registry, settings, playerId, isAlly));
	CHECK(!RelationshipClassifier::IsProtectedTarget(allySummonId, registry, settings, playerId, isAlly));
	CHECK(!RelationshipClassifier::IsProtectedTarget(enemySummonId, registry, settings, playerId, isAlly));
	CHECK(!RelationshipClassifier::IsProtectedTarget(randomActorId, registry, settings, playerId, isAlly));

	// Enable ally-owned summons protection
	settings.protectAllyOwnedSummons = true;
	CHECK(RelationshipClassifier::IsProtectedTarget(allySummonId, registry, settings, playerId, isAlly));
	CHECK(!RelationshipClassifier::IsProtectedTarget(enemySummonId, registry, settings, playerId, isAlly));

	// Global killswitch disabled
	settings.enabled = false;
	CHECK(!RelationshipClassifier::IsProtectedTarget(playerSummonId, registry, settings, playerId, isAlly));
}

TEST(relationship_classifier_evaluate_attack_matrix)
{
	FriendlyFireSettings settings{};

	// 1. Player attacking own summon with Melee
	{
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Player,
			AttackKind::Melee,
			true,
			settings);
		CHECK(dec.blockDamage);
		CHECK(dec.passThrough);
		CHECK(dec.suppressAggression);
	}

	// 2. Player attacking own summon with Arrow
	{
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Player,
			AttackKind::Arrow,
			true,
			settings);
		CHECK(dec.blockDamage);
		CHECK(dec.passThrough);
		CHECK(dec.suppressAggression);
	}

	// 3. Player attacking own summon with AreaEffect (AoE)
	{
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Player,
			AttackKind::AreaEffect,
			true,
			settings);
		CHECK(dec.blockDamage);
		CHECK(!dec.passThrough); // AoE blocks damage, does NOT have physical pass-through
		CHECK(dec.suppressAggression);
	}

	// 4. Hostile enemy attacking summon -> MUST NOT BE FILTERED!
	{
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Hostile,
			AttackKind::Melee,
			true,
			settings);
		CHECK(!dec.blockDamage);
		CHECK(!dec.passThrough);
		CHECK(!dec.suppressAggression);
	}

	// 5. Unknown source attacking summon -> FAIL-SAFE: MUST NOT BE FILTERED!
	{
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Unknown,
			AttackKind::Melee,
			true,
			settings);
		CHECK(!dec.blockDamage);
		CHECK(!dec.passThrough);
		CHECK(!dec.suppressAggression);
	}

	// 6. Player attacking hostile enemy (target is NOT protected)
	{
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Player,
			AttackKind::Melee,
			false,
			settings);
		CHECK(!dec.blockDamage);
		CHECK(!dec.passThrough);
		CHECK(!dec.suppressAggression);
	}

	// 7. Independent pass-through toggle: disable meleePassThrough
	{
		settings.meleePassThrough = false;
		auto dec = RelationshipClassifier::EvaluateAttack(
			AttackSourceKind::Player,
			AttackKind::Melee,
			true,
			settings);
		CHECK(dec.blockDamage);    // Damage is still blocked!
		CHECK(!dec.passThrough);   // But physical pass-through is disabled
		CHECK(dec.suppressAggression);
	}
}

// Given a magic effect applied to a protected summon
// When it comes from an explosion / area (a fireball hitting the ground next to it)
// Then it is classified AreaEffect, which bBlockAreaMagicDamage blocks for friendly casters
TEST(relationship_classifier_magic_attack_kind)
{
	CHECK(RelationshipClassifier::ClassifyMagicAttack(true, false, false, false) == AttackKind::AreaEffect);
	CHECK(RelationshipClassifier::ClassifyMagicAttack(true, true, true, true) == AttackKind::AreaEffect);
	CHECK(RelationshipClassifier::ClassifyMagicAttack(false, true, false, false) == AttackKind::Concentration);
	CHECK(RelationshipClassifier::ClassifyMagicAttack(false, false, true, false) == AttackKind::Enchantment);
	CHECK(RelationshipClassifier::ClassifyMagicAttack(false, false, false, true) == AttackKind::Shout);
	CHECK(RelationshipClassifier::ClassifyMagicAttack(false, false, false, false) == AttackKind::MagicProjectile);

	FriendlyFireSettings settings{};
	const auto area = RelationshipClassifier::EvaluateAttack(AttackSourceKind::Player, AttackKind::AreaEffect, true, settings);
	CHECK(area.blockDamage);
	const auto hostile = RelationshipClassifier::EvaluateAttack(AttackSourceKind::Hostile, AttackKind::AreaEffect, true, settings);
	CHECK(!hostile.blockDamage);
}
