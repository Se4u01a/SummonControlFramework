#pragma once

#include "Core/CatchUp.h"
#include "Core/EntityID.h"
#include "Core/MovementIntent.h"
#include "Core/Settings.h"
#include "Core/StateMachine.h"
#include "Core/SummonModel.h"

#include <cstddef>
#include <string>
#include <unordered_set>

namespace RE
{
	class Actor;
	class PlayerCharacter;
}

namespace scf::engine::movement
{
	// Engine-facing order execution: turns the tactical order stored on a summon model
	// into movement primitives the Creation Engine understands, and reports arrival
	// back into the model / FSM.
	//
	// The primitives are the ones Papyrus exposes as Actor.SetDontMove,
	// Actor.KeepOffsetFromActor and Actor.ClearKeepOffsetFromActor. The CommonLibSSE-NG
	// revision pinned by this project binds none of the three, so they are declared
	// with Address Library ids in the .cpp; see the comment block there for what was
	// verified against the unpacked image.
	//
	// Follow pace and the catch-up recall sit on top of those primitives:
	//   * the pace (PoE2 stage M1) is an additive temporary SpeedMult modifier that this
	//     module owns and rolls back, decided by core::DecideFollowMotion;
	//   * the recall (stage 7) is the emergency teleport behind the owner, decided by
	//     core::DecideCatchUp / core::SelectLandingSpot (Core/CatchUp.h) and performed
	//     here as ONE position write plus one MoveToNearestNavmesh - never as smooth
	//     navigation and never as a repeated per-tick move.
	// So the thresholds, the hysteresis, the cooldown and the landing rules live in Core;
	// this file only collects the engine facts they need and executes the outcome.
	//
	// GAME THREAD ONLY: called from the PlayerCharacter::Update hook.
	void UpdateForSummon(
		RE::PlayerCharacter* a_owner,
		RE::Actor* a_summon,
		core::SummonModel& a_model,
		core::StateMachine& a_fsm,
		const core::SettingsData& a_settings,
		std::size_t a_slotIndex,
		std::size_t a_squadSize,
		float a_now,
		bool a_combatPermitted);

	// Forget bookkeeping for summons that are no longer engine-commanded (dispel,
	// death, cell detach / displacement). Keeps SetDontMove / keep-offset state from
	// being replayed onto a later actor with a recycled handle.
	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons);

	// kPreLoadGame / new game: drop every record.
	void ResetAll();

	// Catch-up statistics of this session (reset by ResetAll): the counters behind a
	// "scf status" line. Read-only for the caller.
	const core::CatchUpStats& CatchUpStatistics() noexcept;

	// The same counters as one log-safe line, e.g.
	//   catch-up: warps=2 triggers(distance=1, no-progress=1) OwnerInCombat=1 last=OwnerInCombat
	// The plugin's status command only has to print it.
	std::string DescribeCatchUpStatistics();
}
