#include "check.h"
#include "Core/WolfManeuver.h"

TEST(wolf_maneuver_filter_follows_the_current_attack_order)
{
	using scf::core::BlocksWolfManeuver;
	CHECK(BlocksWolfManeuver(true, 23, 23));
	CHECK(!BlocksWolfManeuver(false, 23, 23));
	CHECK(!BlocksWolfManeuver(true, 0, 0));
	CHECK(!BlocksWolfManeuver(true, 42, 23));
	CHECK(BlocksWolfManeuver(true, 42, 42));
}

TEST(wolf_maneuver_exit_matches_the_rejected_node_and_thread)
{
	scf::core::WolfManeuverFrame frame{};
	int circle{}, surround{}, wolf{}, dog{};
	CHECK(frame.Begin(&circle, &wolf));
	CHECK(!frame.Begin(&surround, &dog));
	CHECK(!frame.Finish(&circle, &dog));
	CHECK(!frame.Finish(&surround, &wolf));
	CHECK(frame.Finish(&circle, &wolf));
	CHECK(!frame.Finish(&circle, &wolf));
	CHECK(frame.Begin(&surround, &dog));
	CHECK(frame.Finish(&surround, &dog));
}
