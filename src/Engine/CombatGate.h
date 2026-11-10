#pragma once

#include "Core/CombatPolicy.h"
#include "Core/EntityID.h"

#include <cstdint>
#include <unordered_set>

namespace RE
{
	class Actor;
}

namespace scf::engine::combat
{
	// The combat denial seat: the single place where the framework decides whether a
	// managed summon may enter or keep a fight at all.
	//
	// Two layers:
	//   1. A hook on `Actor::StartCombat` (RELOCATION_ID(37608, 38561), SE 1.5.97 ->
	//      SkyrimSE.exe+6251B0, verified against the CommonLib binding and the unpacked
	//      image prologue). For a managed summon, combat is refused unless the published
	//      CombatPolicy allows it, and an explicit FocusTarget restricts it to that one
	//      actor. This is what stops the engine's own AI from fighting on its side.
	//   2. `Actor::BOOL_FLAGS` kAttackingDisabled | kCastingDisabled while combat is not
	//      allowed (the wholesale native deny APMF documents), plus an immediate
	//      StopCombat when the policy flips.
	//
	// Install() is called once at kDataLoaded. UpdateForSummon()/RetainOnly()/ResetAll()
	// are main-thread only; ShouldDenyStartCombat() runs on whatever thread calls
	// StartCombat and only touches the mutex-protected policy table.

	bool Install();

	// Publish and enforce the policy for one summon (main thread, each tactical tick).
	void UpdateForSummon(RE::Actor* a_summon, const core::CombatPolicy& a_policy);

	// Drop bookkeeping for summons that left the engine roster.
	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons);

	// kPreLoadGame / kNewGame.
	void ResetAll();

	// True when `a_self` is a managed summon whose current policy forbids starting
	// combat with `a_target` (or with anything at all). ANY THREAD — called from the
	// StartCombat hook.
	bool ShouldDenyStartCombat(RE::FormID a_self, RE::Actor* a_target) noexcept;
}
