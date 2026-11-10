#include "Core/CombatPolicy.h"

namespace scf::core
{
	CombatPolicy BuildCombatPolicy(const SummonModel& a_model, bool a_hasLiveTarget) noexcept
	{
		// Passive is absolute: no combat, no attacks, no casts, whatever the order says.
		if (a_model.aggression == AggressionMode::Passive) {
			return { false, false, InvalidEntityID, true };
		}

		switch (a_model.order) {
		case SummonOrder::Return:
			// The player ordered a recall: break off and run home, fight only after arrival.
			return { false, false, InvalidEntityID, true };

		case SummonOrder::MoveTo:
			// A move order is a movement order; the summon re-arms on arrival (Guard).
			if (a_model.hasMoveTarget) {
				return { false, false, InvalidEntityID, true };
			}
			break;

		case SummonOrder::Attack:
			if (a_hasLiveTarget && a_model.currentTargetId != InvalidEntityID) {
				return { true, true, a_model.currentTargetId, false };
			}
			return { false, false, InvalidEntityID, true };

		case SummonOrder::WaitGuard:
		case SummonOrder::Follow:
		default:
			break;
		}

		return {};
	}
}
