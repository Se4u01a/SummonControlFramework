#pragma once

#include "Core/EntityID.h"
#include "Core/SummonModel.h"
#include "Core/Types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace scf::core
{
	class StateMachine;

	enum class CommandPriority : std::uint8_t
	{
		Lifecycle = 0,    // Highest: death, dispel, unbind
		PlayerDirect = 1, // Explicit player hotkey / command
		ModeChange = 2,   // Aggression mode toggle
		Tactical = 3      // Lowest: automated FSM decisions
	};

	constexpr const char* ToString(CommandPriority a_prio) noexcept
	{
		switch (a_prio) {
		case CommandPriority::Lifecycle:    return "Lifecycle";
		case CommandPriority::PlayerDirect: return "PlayerDirect";
		case CommandPriority::ModeChange:   return "ModeChange";
		case CommandPriority::Tactical:     return "Tactical";
		default:                            return "Unknown";
		}
	}

	enum class CommandType : std::uint8_t
	{
		SetOrder,
		SetAggression,
		SetFocusTarget,
		Dispel
	};

	constexpr const char* ToString(CommandType a_type) noexcept
	{
		switch (a_type) {
		case CommandType::SetOrder:       return "SetOrder";
		case CommandType::SetAggression:  return "SetAggression";
		case CommandType::SetFocusTarget: return "SetFocusTarget";
		case CommandType::Dispel:         return "Dispel";
		default:                          return "Unknown";
		}
	}

	struct Command
	{
		EntityID summonId{ InvalidEntityID }; // InvalidEntityID = broadcast to all owner's summons
		EntityID ownerId{ InvalidEntityID };
		CommandPriority priority{ CommandPriority::Tactical };
		CommandType type{ CommandType::SetOrder };

		SummonOrder order{ SummonOrder::Follow };
		AggressionMode aggression{ AggressionMode::Defensive };
		EntityID focusTargetId{ InvalidEntityID };
		Vec3 targetPosition{};

		float timestamp{ 0.0f };
		float ttl{ 5.0f }; // Time-to-live in seconds
	};

	class CommandQueue
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		CommandQueue() = default;
		explicit CommandQueue(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		void Enqueue(Command a_cmd);

		bool PopNext(float a_currentTimestamp, Command& a_outCmd);

		std::vector<Command> DrainForSummon(EntityID a_summonId, float a_currentTimestamp);

		void ApplyToModel(const Command& a_cmd, SummonModel& a_model, StateMachine* a_fsm = nullptr) const;

		[[nodiscard]] std::size_t GetPendingCount() const noexcept;

		void Clear();

	private:
		[[nodiscard]] bool IsExpired(const Command& a_cmd, float a_currentTimestamp) const noexcept;

		mutable std::mutex _mutex;
		std::vector<Command> _queue;
		LogCallback _logCallback{ nullptr };
	};
}
