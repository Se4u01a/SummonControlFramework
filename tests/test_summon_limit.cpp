#include "check.h"
#include "Core/SummonLimit.h"

#include <limits>

using namespace scf::core;

// Given the vanilla limit (1, or 2 with Twin Souls)
// When SCF's player limit is off (0), the perks decide; when set, it raises the limit
// Then a higher perk value is never lowered.
TEST(TestPlayerSummonLimit)
{
	CHECK(ApplyPlayerSummonLimit(1.0f, 0) == 1.0f);
	CHECK(ApplyPlayerSummonLimit(2.0f, 0) == 2.0f);
	CHECK(ApplyPlayerSummonLimit(1.0f, 6) == 6.0f);
	CHECK(ApplyPlayerSummonLimit(2.0f, 6) == 6.0f);
	CHECK(ApplyPlayerSummonLimit(8.0f, 6) == 8.0f);
	CHECK(ApplyPlayerSummonLimit(std::numeric_limits<float>::quiet_NaN(), 0) == 1.0f);
	CHECK(ApplyPlayerSummonLimit(std::numeric_limits<float>::infinity(), 4) == 4.0f);
}

// Given the engine keeps more summons than SCF's configured cap (another mod raised the
// engine limit), SCF tracks them all instead of "displacing" one each tick.
TEST(TestTrackingCapacityFollowsEngineRoster)
{
	CHECK(TrackingCapacity(2, 10) == 10);
	CHECK(TrackingCapacity(6, 3) == 6);
	CHECK(TrackingCapacity(0, 0) == 1);
}
