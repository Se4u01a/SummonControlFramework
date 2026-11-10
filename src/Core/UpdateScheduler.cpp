#include "Core/UpdateScheduler.h"

#include <cstdio>

namespace scf::core
{
	UpdateScheduler::UpdateScheduler(float a_frequencyHz, LogCallback a_logCb) :
		_logCallback(std::move(a_logCb))
	{
		SetUpdateFrequency(a_frequencyHz);
	}

	void UpdateScheduler::SetConfig(const SchedulerConfig& a_config) noexcept
	{
		_config = a_config;
		SetUpdateFrequency(_config.updateFrequencyHz);
	}

	float UpdateScheduler::GetTickInterval() const noexcept
	{
		if (_config.updateFrequencyHz <= 0.0f) {
			return _config.maxInterval;
		}

		const float rawInterval = 1.0f / _config.updateFrequencyHz;
		return std::clamp(rawInterval, _config.minInterval, _config.maxInterval);
	}

	void UpdateScheduler::SetUpdateFrequency(float a_frequencyHz) noexcept
	{
		_config.updateFrequencyHz = std::max(0.1f, a_frequencyHz);

		if (_logCallback) {
			char logBuf[128];
			std::snprintf(logBuf, sizeof(logBuf),
				"[Scheduler] Frequency set to %.1f Hz (tick interval: %.1f ms)",
				_config.updateFrequencyHz, GetTickInterval() * 1000.0f);
			_logCallback(logBuf);
		}
	}

	bool UpdateScheduler::ShouldTick(float a_dt)
	{
		if (a_dt <= 0.0f) {
			return false;
		}

		_totalElapsedTime += a_dt;
		_accumulatedTime += a_dt;

		// 1. Immediate priority wake-up override (e.g. urgent player order or lifecycle dispel)
		if (_immediateTickRequested) {
			_immediateTickRequested = false;
			++_tickCount;
			_accumulatedTime = 0.0f;

			if (_logCallback) {
				_logCallback("[Scheduler] Immediate tick executed via priority override");
			}
			return true;
		}

		// 2. Normal periodic interval evaluation
		const float interval = GetTickInterval();
		if (_accumulatedTime >= interval) {
			// Consume one interval step; preserve leftover fraction to avoid timing drift
			_accumulatedTime -= interval;
			// Prevent spiral of death if a huge frame spike occurs (e.g. 5+ seconds)
			if (_accumulatedTime > interval * 2.0f) {
				_accumulatedTime = 0.0f;
			}

			++_tickCount;
			return true;
		}

		return false;
	}

	void UpdateScheduler::RequestImmediateTick() noexcept
	{
		_immediateTickRequested = true;
	}

	bool UpdateScheduler::ShouldUpdateEntity(std::size_t a_entityIndex, std::size_t a_totalEntities) const noexcept
	{
		if (!_config.enableTimeSlicing || a_totalEntities <= 1) {
			return true;
		}

		// Distribute squad entity evaluations evenly across successive ticks
		return (a_entityIndex % a_totalEntities) == (_tickCount % a_totalEntities);
	}

	void UpdateScheduler::Reset() noexcept
	{
		_accumulatedTime = 0.0f;
		_totalElapsedTime = 0.0f;
		_tickCount = 0;
		_immediateTickRequested = false;

		if (_logCallback) {
			_logCallback("[Scheduler] Scheduler reset: timers and tick count zeroed");
		}
	}
}
