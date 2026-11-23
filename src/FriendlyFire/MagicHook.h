#pragma once

namespace scf::friendly_fire
{
	// Friendly magic damage filter at MagicTarget::AddTarget: the single entry through
	// which every magic effect is added to an actor - aimed / touch spells, concentration,
	// enchantments, shouts and EXPLOSIONS (the fireball that hits the ground next to a
	// summon applies its fire damage here with AddTargetData::areaTarget set).
	//
	// VERIFIED on the unpacked SkyrimSE 1.5.97 image (2026-11-24, RTTI subobj_off = 0x98):
	// TESObjectREFR owns vtables [0..3] ([3] at +0x38 is IAnimationGraphManagerHolder);
	// Character's MagicTarget sub-vtable is VTABLE_Character[4] (SE id 261401 -> +165E468):
	//   slot 0: +6941D0 (id 39204, sub rcx, 0x98; jmp Character::~Character)
	//   slot 1: +633090 (id 37832, Actor::AddTarget -> calls base MagicTarget::AddTarget id 33742)
	//   slot 2: +619830 (id 37342, Actor::GetTargetStatsObject: lea rax, [rcx - 0x98]; ret)
	//   slot 3: +619B00 (id 37358, Actor::MagicTargetIsActor: mov al, 1; ret)
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
