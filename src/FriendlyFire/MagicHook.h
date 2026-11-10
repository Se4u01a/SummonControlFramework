#pragma once

namespace scf::friendly_fire
{
	// Friendly magic damage filter at MagicTarget::AddTarget: the single entry through
	// which every magic effect is added to an actor - aimed / touch spells, concentration,
	// enchantments, shouts and EXPLOSIONS (the fireball that hits the ground next to a
	// summon applies its fire damage here with AddTargetData::areaTarget set).
	//
	// VERIFIED on the unpacked SkyrimSE 1.5.97 image (2026-11-11): Character's MagicTarget
	// sub-vtable is VTABLE_Character[3] (id 261400 -> +165E3C8); slot 1 is id 37020
	// (+60F240, `jmp +4F12C0`, id 32157 = the shared MagicTarget::AddTarget body:
	// rcx = the MagicTarget sub-object, rdx = AddTargetData*, returns bool).
	//
	// For a protected summon (commanded by the player, or by a player teammate when
	// bProtectAllyOwnedSummons) a HOSTILE / DETRIMENTAL effect whose caster is the player,
	// a player teammate or one of their summons is refused (`return false`, the same
	// answer the engine gives a resisted effect). Buffs and heals always apply; enemies'
	// spells always apply. Decision matrix: RelationshipClassifier::EvaluateAttack with
	// ClassifyMagicAttack's kind (bBlockAreaMagicDamage, bBlockMagicProjectileDamage, ...).
	class MagicHook
	{
	public:
		static bool Install() noexcept;
	};
}
