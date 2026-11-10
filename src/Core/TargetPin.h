#pragma once

#include <cstdint>

namespace scf::core
{
	// Target pin: the rule that turns an explicit FocusTarget into the target the
	// engine's own combat AI actually fights.
	//
	// Engine background (verified on the unpacked 1.5.97 image, see
	// src/Engine/CombatGate.cpp): every Actor::UpdateCombat calls
	// CombatController::UpdateTarget, which asks each active CombatTargetSelector
	// (vtable slot 6, `SelectTarget(this, ActorHandle* out)`) for a target; the first
	// non-zero answer is written to CombatController::targetHandle and the actor's
	// currentCombatTarget through CombatController::SetTarget. A main-thread write to
	// those members is therefore overwritten on the next combat update — which is why a
	// FocusTarget used to "execute" but be ignored. The pin answers the selector instead.
	//
	// Pure decision, shared by the engine seat and the unit tests. Handles are the raw
	// 32-bit ActorHandle values (0 = none).

	// Where the pinned actor is in the summon's CombatGroup target list.
	enum class PinMembership : std::uint8_t
	{
		Absent,  // not (yet) one of the group's combat targets
		Held,    // a combat target the group still tracks
		Lost     // in the list, but flagged kTargetLost (the group cannot locate it)
	};

	// Returns the handle the selector must answer with.
	//
	//   * No pin (0)                     -> the engine's own answer.
	//   * Engine answer 0 (no target)    -> 0: the pin never starts or revives a fight;
	//                                       entering combat is StartCombat's job.
	//   * Pinned actor not alive/loaded  -> the engine's own answer.
	//   * Pin not a held group target    -> the engine's own answer (the pin only chooses
	//                                       among targets the engine itself tracks; the
	//                                       FocusTarget StartCombat adds it to the group).
	//   * Otherwise                      -> the pinned handle.
	std::uint32_t ChooseSelectorAnswer(
		std::uint32_t a_engineAnswer,
		std::uint32_t a_pinnedHandle,
		bool a_pinnedAlive,
		PinMembership a_membership) noexcept;

	const char* ToString(PinMembership a_membership) noexcept;
}
