#include "AI/TargetSelector.h"

#include <algorithm>
#include <limits>

namespace scf::ai
{
	bool TargetSelector::IsCandidateValid(
		const core::SummonModel& a_summon,
		const TargetCandidate& a_candidate,
		const TargetSelectorConfig& a_config) const noexcept
	{
		// 1. Never target invalid, dead or disabled actors
		if (a_candidate.id == core::InvalidEntityID || a_candidate.isDead || a_candidate.isDisabled) {
			return false;
		}

		// 2. Friendly-fire guard: never target owner or own squad/allies
		if (a_candidate.id == a_summon.ownerId || a_candidate.id == a_summon.id || a_candidate.isOwnerOrAlly) {
			return false;
		}

		// 3. Distance check
		const float dist = a_summon.lastPosition.DistanceTo(a_candidate.position);
		if (dist > a_config.maxTargetRadius) {
			return false;
		}

		// 4. Aggression-specific qualification
		switch (a_summon.aggression) {
		case core::AggressionMode::Passive:
			return false;

		case core::AggressionMode::Defensive:
			// Only engage if candidate is an active threat to owner or summon
			return a_candidate.isAttackingOwner ||
			       a_candidate.isAttackingSummon ||
			       a_candidate.isHostileToOwner;

		case core::AggressionMode::Aggressive:
		case core::AggressionMode::FocusTarget:
			return a_candidate.isHostileToOwner ||
			       a_candidate.isHostileToSummon ||
			       a_candidate.isAttackingOwner ||
			       a_candidate.isAttackingSummon;

		default:
			return false;
		}
	}

	float TargetSelector::ScoreCandidate(
		const core::SummonModel& a_summon,
		const TargetCandidate& a_candidate,
		const TargetSelectorConfig& a_config) const noexcept
	{
		const float dist = a_summon.lastPosition.DistanceTo(a_candidate.position);

		// Base proximity score (closer targets have higher base score)
		float score = std::max(0.0f, a_config.maxTargetRadius - dist);

		// Critical priority: threats currently attacking the owner
		if (a_candidate.isAttackingOwner) {
			score += a_config.attackingOwnerBonus;
		}

		// Secondary priority: threats attacking this summon
		if (a_candidate.isAttackingSummon) {
			score += a_config.attackingSummonBonus;
		}

		// Sticky target hysteresis: reward staying on current target to avoid target oscillation
		if (a_candidate.id == a_summon.currentTargetId) {
			score += a_config.currentTargetStickyBonus;
		}

		// Target finishing bonus: slightly favor lower HP enemies to reduce incoming damage faster
		const float healthClamped = std::clamp(a_candidate.healthPercent, 0.0f, 1.0f);
		score += (1.0f - healthClamped) * a_config.lowHealthBonus;

		return score;
	}

	core::EntityID TargetSelector::SelectTarget(
		const core::SummonModel& a_summon,
		const std::vector<TargetCandidate>& a_candidates,
		core::EntityID a_explicitFocusTarget,
		const TargetSelectorConfig& a_config) const
	{
		// 1. Passive mode: no targets allowed
		if (a_summon.aggression == core::AggressionMode::Passive) {
			if (_logCallback) {
				_logCallback(a_summon.id, core::InvalidEntityID, "Passive mode: targeting disabled");
			}
			return core::InvalidEntityID;
		}

		// 2. Explicit FocusTarget priority: if player commanded a specific target, evaluate it first
		if (a_explicitFocusTarget != core::InvalidEntityID) {
			const auto it = std::find_if(a_candidates.begin(), a_candidates.end(),
				[a_explicitFocusTarget](const TargetCandidate& c) { return c.id == a_explicitFocusTarget; });

			if (it != a_candidates.end()) {
				// Validate focus target (must not be dead, disabled or friendly)
				if (it->isDead || it->isDisabled) {
					if (_logCallback) {
						_logCallback(a_summon.id, core::InvalidEntityID, "Explicit FocusTarget is dead or disabled");
					}
				} else if (it->id == a_summon.ownerId || it->isOwnerOrAlly) {
					if (_logCallback) {
						_logCallback(a_summon.id, core::InvalidEntityID, "Explicit FocusTarget rejected: cannot target owner/ally");
					}
				} else {
					if (_logCallback) {
						_logCallback(a_summon.id, a_explicitFocusTarget, "Explicit FocusTarget selected");
					}
					return a_explicitFocusTarget;
				}
			} else {
				if (_logCallback) {
					_logCallback(a_summon.id, core::InvalidEntityID, "Explicit FocusTarget not found in candidates");
				}
			}
		}

		// 3. Automatic candidate selection
		core::EntityID bestTarget = core::InvalidEntityID;
		float bestScore = -std::numeric_limits<float>::infinity();

		for (const auto& candidate : a_candidates) {
			if (!IsCandidateValid(a_summon, candidate, a_config)) {
				continue;
			}

			const float score = ScoreCandidate(a_summon, candidate, a_config);
			if (score > bestScore) {
				bestScore = score;
				bestTarget = candidate.id;
			}
		}

		if (_logCallback) {
			if (bestTarget != core::InvalidEntityID) {
				_logCallback(a_summon.id, bestTarget, "Best scored candidate selected");
			} else {
				_logCallback(a_summon.id, core::InvalidEntityID, "No valid target candidates found");
			}
		}

		return bestTarget;
	}
}
