#include "Core/FrameworkContext.h"

namespace scf::core
{
	FrameworkContext& FrameworkContext::GetSingleton() noexcept
	{
		static FrameworkContext instance;
		return instance;
	}

	FrameworkContext::FrameworkContext()
	{
		_registry.SetDefaultMaxSummonsPerOwner(_settings.Get().general.maxSummonsPerOwner);
		PropagateLogCallbacks();
	}

	void FrameworkContext::SetLogCallback(LogCallback a_logCb) noexcept
	{
		_logCallback = std::move(a_logCb);
		PropagateLogCallbacks();
	}

	void FrameworkContext::PropagateLogCallbacks()
	{
		if (!_logCallback) {
			return;
		}

		_settings.SetLogCallback(_logCallback);
		_registry.SetLogCallback(_logCallback);
		_commandQueue.SetLogCallback(_logCallback);
		_scheduler.SetLogCallback(_logCallback);
		_coordinator.SetLogCallback(_logCallback);
		_fsm.SetLogCallback([this](EntityID id, SummonState from, SummonState to, const char* reason) {
			if (_logCallback) {
				char buf[256];
				std::snprintf(buf, sizeof(buf), "[FSM] Entity %u: %s -> %s (Reason: %s)",
					id, ToString(from), ToString(to), reason ? reason : "none");
				_logCallback(buf);
			}
		});
	}

	void FrameworkContext::ResetRuntimeState() noexcept
	{
		_registry.Reset();
		_registry.SetDefaultMaxSummonsPerOwner(_settings.Get().general.maxSummonsPerOwner);
		_commandQueue.Clear();
		_scheduler.Reset();

		if (_logCallback) {
			_logCallback("[FrameworkContext] Runtime state reset: registry, command queue, and scheduler cleared.");
		}
	}
}
