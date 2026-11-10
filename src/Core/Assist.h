#pragma once

#include "Core/SummonModel.h"
#include "Core/Types.h"

namespace scf::core
{
	// PoE2-style minion targeting (PoE2_Minion_Parity_Plan.md, stage M2). Pure rules; the
	// engine side is the hit-event sink in src/Engine/EventAdapter.cpp (assist) and the
	// StartCombat seat in src/Engine/CombatGate.cpp (aggression radius).

	// "Attack what I attack": the player hit `a_targetId`, a hostile candidate the
	// engine layer has already checked (alive, not the player, not a friend). Should this
	// summon switch its focus to it?
	//   * never in Passive (it only fights on an explicit order) - and not on Return,
	//     MoveTo or Guard: those are explicit player orders that outrank the assist;
	//   * Follow: yes - this is the minion joining the player's fight;
	//   * Attack: yes if it is a different target (the player switched targets); the
	//     same target is not re-issued.
	bool ShouldAssistPlayerHit(const SummonModel& a_model, EntityID a_targetId) noexcept;

	// Aggression radius: in Follow / Guard (engine-chosen fights) a summon may only START
	// a fight with an actor within `a_radius` of its owner (horizontal distance). A
	// non-positive radius disables the rule. Explicit focus targets are not subject to it.
	bool IsWithinAggressionRadius(const Vec3& a_owner, const Vec3& a_target, float a_radius) noexcept;
}
