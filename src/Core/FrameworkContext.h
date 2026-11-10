#pragma once

#include "AI/SquadCoordinator.h"
#include "Core/CommandQueue.h"
#include "Core/Settings.h"
#include "Core/StateMachine.h"
#include "Core/SummonRegistry.h"
#include "Core/UpdateScheduler.h"

#include <functional>
#include <string>

namespace scf::core
{
	class FrameworkContext
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		static FrameworkContext& GetSingleton() noexcept;

		void SetLogCallback(LogCallback a_logCb) noexcept;

		[[nodiscard]] Settings& GetSettings() noexcept { return _settings; }
		[[nodiscard]] const Settings& GetSettings() const noexcept { return _settings; }

		[[nodiscard]] SummonRegistry& GetRegistry() noexcept { return _registry; }
		[[nodiscard]] const SummonRegistry& GetRegistry() const noexcept { return _registry; }

		[[nodiscard]] CommandQueue& GetCommandQueue() noexcept { return _commandQueue; }
		[[nodiscard]] const CommandQueue& GetCommandQueue() const noexcept { return _commandQueue; }

		[[nodiscard]] UpdateScheduler& GetScheduler() noexcept { return _scheduler; }
		[[nodiscard]] const UpdateScheduler& GetScheduler() const noexcept { return _scheduler; }

		[[nodiscard]] StateMachine& GetStateMachine() noexcept { return _fsm; }
		[[nodiscard]] const StateMachine& GetStateMachine() const noexcept { return _fsm; }

		[[nodiscard]] ai::SquadCoordinator& GetCoordinator() noexcept { return _coordinator; }
		[[nodiscard]] const ai::SquadCoordinator& GetCoordinator() const noexcept { return _coordinator; }

		void ResetRuntimeState() noexcept;

	private:
		FrameworkContext();

		void PropagateLogCallbacks();

		Settings _settings;
		SummonRegistry _registry;
		CommandQueue _commandQueue;
		UpdateScheduler _scheduler;
		StateMachine _fsm;
		ai::SquadCoordinator _coordinator;

		LogCallback _logCallback{ nullptr };
	};
}
