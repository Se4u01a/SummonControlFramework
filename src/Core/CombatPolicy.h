#pragma once

#include "Core/SummonModel.h"
#include "Core/Types.h"

#include <cstdint>

namespace scf::core
{
	// What the Creation Engine is allowed to do with a summon's own combat, derived from
	// the player's current order and the aggression mode. Pure rules; the engine layer
	// (`src/Engine/CombatGate.cpp`) applies them through the StartCombat denial seat and
	// the native attack/cast disable bits.
	//
	// This is the "no second behaviour pattern" contract: while a summon is under a
	// movement or passive order the engine may not pick a fight on its own, and an
	// explicit FocusTarget is the only target it may fight.
	struct CombatPolicy
	{
		// The summon may enter / stay in combat at all.
		bool allowCombat{ true };

		// When set, only `target` may be fought (explicit FocusTarget). Any other
		// StartCombat against this summon is refused.
		bool restrictToTarget{ false };
		EntityID target{ InvalidEntityID };

		// Native Actor::BOOL_FLAGS kAttackingDisabled + kCastingDisabled. Used whenever
		// combat is not allowed so the actor cannot swing or cast even if combat slips
		// through the denial seat (Passive must never attack, Return must run home).
		bool disableAttacks{ false };
	};

	// `a_hasLiveTarget` is the engine-side fact that the model's currentTargetId still
	// resolves to a loaded, living actor in the summon's cell.
	//
	// Rules:
	//   * Passive (any order): never fight, attacks/casts disabled.
	//   * Return: no combat until the summon reaches the owner.
	//   * MoveTo with a destination: no combat until the point is reached (the command
	//     is a movement order, not an engagement).
	//   * Attack: combat allowed only against the focused target; without a live target
	//     combat is blocked until the order reverts to Follow.
	//   * Guard / Follow: combat allowed (the engine picks targets), movement stays under
	//     the movement driver's control.
	CombatPolicy BuildCombatPolicy(const SummonModel& a_model, bool a_hasLiveTarget) noexcept;
}
