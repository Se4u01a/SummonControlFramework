#include "check.h"
#include "Core/UpdateScheduler.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace scf::core;

TEST(TestIntervalCalculationAndClamping)
{
	UpdateScheduler scheduler(15.0f);
	CHECK_NEAR(scheduler.GetTickInterval(), 1.0f / 15.0f, 0.001f);

	// Test high frequency clamping (cap at 30 Hz / 0.0333s)
	scheduler.SetUpdateFrequency(200.0f);
	CHECK_NEAR(scheduler.GetTickInterval(), 0.0333f, 0.001f);

	// Test low frequency clamping (floor at 5 Hz / 0.2000s)
	scheduler.SetUpdateFrequency(1.0f);
	CHECK_NEAR(scheduler.GetTickInterval(), 0.2000f, 0.001f);
}

TEST(TestPeriodicTickAccurate)
{
	// 10 Hz scheduler -> interval = 0.10s
	UpdateScheduler scheduler(10.0f);
	CHECK_NEAR(scheduler.GetTickInterval(), 0.10f, 0.001f);

	// Simulate 60 FPS (dt = 0.01667s per frame)
	constexpr float frameDt = 0.01667f;

	int tickCount = 0;
	// Run 12 frames (~0.20s total, should produce exactly 2 ticks)
	for (int i = 0; i < 12; ++i) {
		if (scheduler.ShouldTick(frameDt)) {
			++tickCount;
		}
	}

	CHECK(tickCount == 2);
	CHECK(scheduler.GetTickCount() == 2);
	CHECK_NEAR(scheduler.GetTotalElapsedTime(), 12 * frameDt, 0.001f);
}

TEST(TestImmediateTickOverride)
{
	UpdateScheduler scheduler(10.0f); // 0.10s interval

	// After only 1 ms, regular tick must not fire
	CHECK(!scheduler.ShouldTick(0.001f));

	// Player command arrives -> requests immediate wakeup
	scheduler.RequestImmediateTick();

	// Next frame with 1 ms MUST fire immediately
	CHECK(scheduler.ShouldTick(0.001f));
	CHECK(scheduler.GetTickCount() == 1);

	// Next frame without immediate flag returns to normal periodic waiting
	CHECK(!scheduler.ShouldTick(0.001f));
}

TEST(TestTimeSlicingInterleavedCadence)
{
	UpdateScheduler scheduler(10.0f);
	constexpr std::size_t squadSize = 2;

	// Force tick 1 (tickCount = 1)
	scheduler.RequestImmediateTick();
	CHECK(scheduler.ShouldTick(0.01f));
	CHECK(scheduler.GetTickCount() == 1);

	// At tick 1: (index % 2 == 1 % 2) -> Member 1 updates, Member 0 waits
	CHECK(!scheduler.ShouldUpdateEntity(0, squadSize));
	CHECK(scheduler.ShouldUpdateEntity(1, squadSize));

	// Force tick 2 (tickCount = 2)
	scheduler.RequestImmediateTick();
	CHECK(scheduler.ShouldTick(0.01f));
	CHECK(scheduler.GetTickCount() == 2);

	// At tick 2: (index % 2 == 2 % 2 == 0) -> Member 0 updates, Member 1 waits
	CHECK(scheduler.ShouldUpdateEntity(0, squadSize));
	CHECK(!scheduler.ShouldUpdateEntity(1, squadSize));
}

TEST(TestResetZeroesState)
{
	UpdateScheduler scheduler(10.0f);
	scheduler.ShouldTick(0.25f);
	CHECK(scheduler.GetTickCount() > 0);
	CHECK(scheduler.GetTotalElapsedTime() > 0.0f);

	scheduler.Reset();
	CHECK(scheduler.GetTickCount() == 0);
	CHECK(scheduler.GetTotalElapsedTime() == 0.0f);
	CHECK(scheduler.GetAccumulatedTime() == 0.0f);
}

TEST(TestSchedulerLogging)
{
	std::vector<std::string> logs;
	UpdateScheduler scheduler(10.0f, [&logs](const char* msg) {
		logs.emplace_back(msg);
	});

	logs.clear(); // Clear constructor log
	scheduler.SetUpdateFrequency(20.0f);
	scheduler.RequestImmediateTick();
	scheduler.ShouldTick(0.01f);
	scheduler.Reset();

	CHECK(logs.size() == 3);
	CHECK(logs[0].find("Frequency set to 20.0 Hz") != std::string::npos);
	CHECK(logs[1].find("Immediate tick executed via priority override") != std::string::npos);
	CHECK(logs[2].find("Scheduler reset") != std::string::npos);
}
