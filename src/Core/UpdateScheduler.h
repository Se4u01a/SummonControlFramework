#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace scf::core
{
	struct SchedulerConfig
	{
		float updateFrequencyHz{ 15.0f }; // 15 Hz = ~66.7 ms interval
		float minInterval{ 0.0333f };     // 30 Hz cap
		float maxInterval{ 0.2000f };     // 5 Hz floor
		bool enableTimeSlicing{ true };   // Staggers squad member updates across ticks
	};

	class UpdateScheduler
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		UpdateScheduler() = default;
		explicit UpdateScheduler(float a_frequencyHz, LogCallback a_logCb = nullptr);

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		void SetConfig(const SchedulerConfig& a_config) noexcept;

		[[nodiscard]] const SchedulerConfig& GetConfig() const noexcept
		{
			return _config;
		}

		[[nodiscard]] float GetTickInterval() const noexcept;

		void SetUpdateFrequency(float a_frequencyHz) noexcept;

		[[nodiscard]] float GetUpdateFrequency() const noexcept
		{
			return _config.updateFrequencyHz;
		}

		bool ShouldTick(float a_dt);

		void RequestImmediateTick() noexcept;

		[[nodiscard]] bool ShouldUpdateEntity(std::size_t a_entityIndex, std::size_t a_totalEntities) const noexcept;

		void Reset() noexcept;

		[[nodiscard]] std::uint64_t GetTickCount() const noexcept
		{
			return _tickCount;
		}

		[[nodiscard]] float GetTotalElapsedTime() const noexcept
		{
			return _totalElapsedTime;
		}

		[[nodiscard]] float GetAccumulatedTime() const noexcept
		{
			return _accumulatedTime;
		}

	private:
		SchedulerConfig _config{};
		float _accumulatedTime{ 0.0f };
		float _totalElapsedTime{ 0.0f };
		std::uint64_t _tickCount{ 0 };
		bool _immediateTickRequested{ false };
		LogCallback _logCallback{ nullptr };
	};
}
