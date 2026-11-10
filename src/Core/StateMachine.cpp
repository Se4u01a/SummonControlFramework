#include "Core/StateMachine.h"

namespace scf::core
{
	void StateMachine::TransitionTo(SummonModel& a_model, SummonState a_newState, const char* a_reason)
	{
		if (a_model.state == a_newState) {
			return;
		}

		const auto oldState = a_model.state;
		a_model.state = a_newState;
		a_model.stateTime = 0.0f;
		a_model.targetLostTime = 0.0f;

		if (_logCallback) {
			_logCallback(a_model.id, oldState, a_newState, a_reason ? a_reason : "Unspecified");
		}
	}

	bool StateMachine::CanEngageCombat(const SummonModel& a_model, const DecisionContext& a_ctx) const noexcept
	{
		if (!a_ctx.hasTarget) {
			return false;
		}

		// Stealth coordination: if player is sneaking and undetected, do not engage unless explicit order or aggressive mode
		if (a_ctx.stealth.playerSneaking && !a_ctx.stealth.playerDetected) {
			if (!a_ctx.stealth.explicitAttackOrder && a_model.aggression != AggressionMode::Aggressive) {
				return false;
			}
		}

		switch (a_model.aggression) {
		case AggressionMode::Passive:
			return false;

		case AggressionMode::Defensive:
			return a_ctx.isOwnerInCombat ||
			       a_ctx.stealth.explicitAttackOrder ||
			       a_model.state == SummonState::Engage ||
			       a_model.state == SummonState::Approach;

		case AggressionMode::Aggressive:
		case AggressionMode::FocusTarget:
			return true;

		default:
			return false;
		}
	}

	void StateMachine::Update(SummonModel& a_model, const DecisionContext& a_ctx, float a_dt)
	{
		if (a_model.state == SummonState::Despawn) {
			return;
		}

		// 1. Check explicit Return command
		if (a_model.order == SummonOrder::Return) {
			if (a_model.state != SummonState::Return) {
				a_model.currentTargetId = InvalidEntityID;
				TransitionTo(a_model, SummonState::Return, "Order: Return");
			} else if (a_ctx.distanceToOwner <= a_ctx.followDistanceThreshold) {
				a_model.order = SummonOrder::Follow;
				TransitionTo(a_model, SummonState::FollowOwner, "Returned to owner range");
			}
			a_model.stateTime += a_dt;
			return;
		}

		// 2. Check explicit Wait/Guard command
		if (a_model.order == SummonOrder::WaitGuard) {
			if (a_model.state != SummonState::Guard &&
			    a_model.state != SummonState::AcquireTarget &&
			    a_model.state != SummonState::Approach &&
			    a_model.state != SummonState::Engage) {
				TransitionTo(a_model, SummonState::Guard, "Order: WaitGuard");
			}
		}

		// 3. Check leash constraint: if too far from owner, abort combat and return
		if (a_ctx.hasOwner && a_ctx.distanceToOwner > a_ctx.returnDistanceThreshold) {
			if (a_model.state != SummonState::Return) {
				a_model.currentTargetId = InvalidEntityID;
				TransitionTo(a_model, SummonState::Return, "Leash break: distance to owner exceeded threshold");
				a_model.stateTime += a_dt;
				return;
			}
		}

		// 4. Combat evaluation
		const bool combatAllowed = CanEngageCombat(a_model, a_ctx);

		if (combatAllowed) {
			a_model.currentTargetId = a_ctx.detectedTargetId;
			a_model.targetLostTime = 0.0f;

			switch (a_model.state) {
			case SummonState::Idle:
			case SummonState::FollowOwner:
			case SummonState::Guard:
				TransitionTo(a_model, SummonState::AcquireTarget, "Target detected");
				break;

			case SummonState::AcquireTarget:
				if (a_ctx.distanceToTarget <= a_ctx.engageDistanceThreshold) {
					TransitionTo(a_model, SummonState::Engage, "Target within engage distance");
				} else {
					TransitionTo(a_model, SummonState::Approach, "Approaching target");
				}
				break;

			case SummonState::Approach:
				if (a_ctx.distanceToTarget <= a_ctx.engageDistanceThreshold) {
					TransitionTo(a_model, SummonState::Engage, "Target reached engage distance");
				}
				break;

			case SummonState::Engage:
				// Slight hysteresis: require distance > 1.5x threshold to switch back to Approach
				if (a_ctx.distanceToTarget > a_ctx.engageDistanceThreshold * 1.5f) {
					TransitionTo(a_model, SummonState::Approach, "Target stepped out of engage range");
				}
				break;

			default:
				break;
			}
		} else {
			// No combat allowed or target lost
			const bool inCombatState = (a_model.state == SummonState::AcquireTarget ||
			                            a_model.state == SummonState::Approach ||
			                            a_model.state == SummonState::Engage);

			if (inCombatState) {
				if (a_model.aggression == AggressionMode::Passive) {
					a_model.currentTargetId = InvalidEntityID;
					TransitionTo(a_model, SummonState::FollowOwner, "Passive mode disengage");
				} else {
					// Apply debounce for target lost
					a_model.targetLostTime += a_dt;
					if (a_model.targetLostTime >= a_ctx.targetLostDebounceTime) {
						a_model.currentTargetId = InvalidEntityID;
						if (a_ctx.distanceToOwner > a_ctx.followDistanceThreshold) {
							TransitionTo(a_model, SummonState::Return, "Target lost debounce expired, returning to owner");
						} else {
							const auto fallbackState = (a_model.order == SummonOrder::WaitGuard)
							                               ? SummonState::Guard
							                               : SummonState::FollowOwner;
							TransitionTo(a_model, fallbackState, "Target lost debounce expired, target disengaged");
						}
					}
				}
			} else if (a_model.state == SummonState::Return) {
				if (a_ctx.distanceToOwner <= a_ctx.followDistanceThreshold) {
					const auto fallbackState = (a_model.order == SummonOrder::WaitGuard)
					                               ? SummonState::Guard
					                               : SummonState::FollowOwner;
					TransitionTo(a_model, fallbackState, "Returned within owner distance");
				}
			} else if (a_model.state == SummonState::Idle && a_model.order == SummonOrder::Follow) {
				TransitionTo(a_model, SummonState::FollowOwner, "Order: Follow");
			}
		}

		a_model.stateTime += a_dt;
	}
}
