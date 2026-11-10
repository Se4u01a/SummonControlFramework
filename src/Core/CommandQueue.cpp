#include "Core/CommandQueue.h"
#include "Core/StateMachine.h"

#include <algorithm>
#include <cstdio>

namespace scf::core
{
	bool CommandQueue::IsExpired(const Command& a_cmd, float a_currentTimestamp) const noexcept
	{
		return a_cmd.ttl > 0.0f && (a_currentTimestamp - a_cmd.timestamp) > a_cmd.ttl;
	}

	void CommandQueue::Enqueue(Command a_cmd)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		char logBuf[256];

		// Coalescing: if an unexecuted command with the same type and priority exists for this summon, update it
		auto it = std::find_if(_queue.begin(), _queue.end(), [&a_cmd](const Command& existing) {
			return existing.summonId == a_cmd.summonId &&
			       existing.type == a_cmd.type &&
			       existing.priority == a_cmd.priority;
		});

		if (it != _queue.end()) {
			*it = a_cmd;
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[CommandQueue] Coalesced pending command for Summon %u (Type: %s, Priority: %s)",
					a_cmd.summonId, ToString(a_cmd.type), ToString(a_cmd.priority));
				_logCallback(logBuf);
			}
			return;
		}

		_queue.push_back(a_cmd);

		if (_logCallback) {
			std::snprintf(logBuf, sizeof(logBuf),
				"[CommandQueue] Enqueued command for Summon %u (Type: %s, Priority: %s, Queue size: %zu)",
				a_cmd.summonId, ToString(a_cmd.type), ToString(a_cmd.priority), _queue.size());
			_logCallback(logBuf);
		}
	}

	bool CommandQueue::PopNext(float a_currentTimestamp, Command& a_outCmd)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		// 1. Purge expired commands
		std::erase_if(_queue, [this, a_currentTimestamp](const Command& cmd) {
			const bool exp = IsExpired(cmd, a_currentTimestamp);
			if (exp && _logCallback) {
				char logBuf[256];
				std::snprintf(logBuf, sizeof(logBuf),
					"[CommandQueue] Discarded expired command for Summon %u (Type: %s, Age: %.2fs)",
					cmd.summonId, ToString(cmd.type), (a_currentTimestamp - cmd.timestamp));
				_logCallback(logBuf);
			}
			return exp;
		});

		if (_queue.empty()) {
			return false;
		}

		// 2. Select command with highest priority (lowest numerical enum value)
		// If priorities match, select oldest timestamp (FIFO)
		auto bestIt = _queue.begin();
		for (auto it = _queue.begin() + 1; it != _queue.end(); ++it) {
			if (it->priority < bestIt->priority) {
				bestIt = it;
			} else if (it->priority == bestIt->priority && it->timestamp < bestIt->timestamp) {
				bestIt = it;
			}
		}

		a_outCmd = *bestIt;
		_queue.erase(bestIt);
		return true;
	}

	std::vector<Command> CommandQueue::DrainForSummon(EntityID a_summonId, float a_currentTimestamp)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		std::vector<Command> matched;

		// Extract matching commands while purging expired ones
		std::erase_if(_queue, [this, a_summonId, a_currentTimestamp, &matched](const Command& cmd) {
			if (IsExpired(cmd, a_currentTimestamp)) {
				return true;
			}

			// Matches target summon or broadcast (InvalidEntityID)
			if (cmd.summonId == a_summonId || cmd.summonId == InvalidEntityID) {
				matched.push_back(cmd);
				return true;
			}
			return false;
		});

		// Sort extracted commands by priority (highest priority first)
		std::stable_sort(matched.begin(), matched.end(), [](const Command& a, const Command& b) {
			return a.priority < b.priority;
		});

		return matched;
	}

	void CommandQueue::ApplyToModel(const Command& a_cmd, SummonModel& a_model, StateMachine* a_fsm) const
	{
		switch (a_cmd.type) {
		case CommandType::SetOrder:
			a_model.order = a_cmd.order;
			if (a_fsm && a_cmd.order == SummonOrder::Return) {
				a_fsm->TransitionTo(a_model, SummonState::Return, "Command: Return executed");
			} else if (a_fsm && a_cmd.order == SummonOrder::WaitGuard) {
				a_fsm->TransitionTo(a_model, SummonState::Guard, "Command: WaitGuard executed");
			}
			break;

		case CommandType::SetAggression:
			a_model.aggression = a_cmd.aggression;
			if (a_fsm && a_cmd.aggression == AggressionMode::Passive) {
				a_fsm->TransitionTo(a_model, SummonState::FollowOwner, "Command: Passive mode engaged");
			}
			break;

		case CommandType::SetFocusTarget:
			a_model.currentTargetId = a_cmd.focusTargetId;
			if (a_fsm && a_cmd.focusTargetId != InvalidEntityID) {
				a_fsm->TransitionTo(a_model, SummonState::AcquireTarget, "Command: FocusTarget acquired");
			}
			break;

		case CommandType::Dispel:
			a_model.state = SummonState::Despawn;
			a_model.currentTargetId = InvalidEntityID;
			if (a_fsm) {
				a_fsm->TransitionTo(a_model, SummonState::Despawn, "Command: Dispel executed");
			}
			break;
		}
	}

	std::size_t CommandQueue::GetPendingCount() const noexcept
	{
		std::lock_guard<std::mutex> lock(_mutex);
		return _queue.size();
	}

	void CommandQueue::Clear()
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_queue.clear();
	}
}
