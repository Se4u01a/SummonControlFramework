#include "check.h"
#include "Core/MinionPanelModel.h"

#include <string>
#include <vector>

using namespace scf::core;

namespace
{
	MinionRow MakeRow(EntityID a_id = 0x0001ABCD, const char* a_name = "Wolf")
	{
		MinionRow row;
		row.summonId = a_id;
		row.name = a_name;
		return row;
	}
}

// Given pools at, beyond and outside the maximum
// When the bar fraction is computed
// Then it stays inside 0..1 and a missing maximum reads as empty.
TEST(TestMinionPanelHealthFractionStaysInRange)
{
	CHECK_NEAR(NormalizeHealthFraction(100.0f, 200.0f), 0.5, 1e-6);
	CHECK_NEAR(NormalizeHealthFraction(250.0f, 200.0f), 1.0, 1e-6);
	CHECK_NEAR(NormalizeHealthFraction(-5.0f, 200.0f), 0.0, 1e-6);
	CHECK_NEAR(NormalizeHealthFraction(10.0f, 0.0f), 0.0, 1e-6);
	CHECK_NEAR(NormalizeHealthFraction(0.0f, 200.0f), 0.0, 1e-6);
}

// Given a living or a dead summon
// When the bar colour is chosen
// Then death is grey and the living bands change at 30% and 60%.
TEST(TestMinionPanelHealthColorBands)
{
	const auto dead = HealthColor(1.0f, false);
	CHECK_NEAR(dead.r, 0.55, 1e-6);
	CHECK_NEAR(dead.g, 0.55, 1e-6);

	const auto criticalEdge = HealthColor(0.30f, true);  // 30% is still critical
	CHECK_NEAR(criticalEdge.r, 0.86, 1e-6);
	CHECK_NEAR(criticalEdge.g, 0.25, 1e-6);

	const auto low = HealthColor(0.31f, true);
	CHECK_NEAR(low.g, 0.76, 1e-6);

	const auto lowEdge = HealthColor(0.60f, true);
	CHECK_NEAR(lowEdge.g, 0.76, 1e-6);

	const auto healthy = HealthColor(0.61f, true);
	CHECK_NEAR(healthy.g, 0.80, 1e-6);

	CHECK_NEAR(HealthColor(0.0f, true).r, 0.86, 1e-6);
	CHECK_NEAR(HealthColor(5.0f, true).g, 0.80, 1e-6);
}

// Given a number of seconds
// When it is formatted for the panel
// Then it reads as minutes and two-digit seconds, and nothing negative is shown.
TEST(TestMinionPanelTimeFormatting)
{
	CHECK(FormatTime(-1.0f) == "0:00");
	CHECK(FormatTime(0.0f) == "0:00");
	CHECK(FormatTime(6.7f) == "0:07");
	CHECK(FormatTime(59.4f) == "0:59");
	CHECK(FormatTime(59.6f) == "1:00");
	CHECK(FormatTime(65.0f) == "1:05");
	CHECK(FormatTime(3599.0f) == "59:59");
	CHECK(FormatTime(3600.0f) == "60:00");
}

// Given a row whose health was read, unread, or whose maximum is unknown
// When the health line is formatted
// Then it shows percent and numbers, "?" for an unread pool, and numbers alone otherwise.
TEST(TestMinionPanelHealthSummaryText)
{
	auto row = MakeRow();
	CHECK(FormatHealthSummary(row) == "?");

	row.hasHealth = true;
	row.health = 142.4f;
	row.maxHealth = 200.0f;
	CHECK(FormatHealthPercent(0.712f) == "71%");
	CHECK(FormatHealthPercent(1.5f) == "100%");
	CHECK(FormatHealthPercent(-0.2f) == "0%");
	CHECK(FormatHealthValue(row) == "142 / 200");
	CHECK(FormatHealthSummary(row) == "71% (142 / 200)");

	row.maxHealth = 0.0f;
	CHECK(FormatHealthValue(row) == "142");
}

// Given an order, an FSM state and an aggression mode
// When the status line is formatted
// Then all three are named, in that order.
TEST(TestMinionPanelStatusLine)
{
	MinionRow row;
	CHECK(FormatStatus(row) == "Follow | Idle | Defensive");

	row.order = SummonOrder::MoveTo;
	row.state = SummonState::Approach;
	row.aggression = AggressionMode::Aggressive;
	CHECK(FormatStatus(row) == "MoveTo | Approach | Aggressive");

	row.state = SummonState::Engage;
	row.aggression = AggressionMode::FocusTarget;
	CHECK(FormatStatus(row) == "MoveTo | Engage | FocusTarget");
}

// Given a summon that died, was suspended, is a reanimate, or has no name
// When the title is formatted
// Then the state is spelled out next to the name, or the FormID stands in for it.
TEST(TestMinionPanelTitleMarksState)
{
	auto row = MakeRow(0x0001ABCD, "Wolf");
	CHECK(FormatTitle(row) == "Wolf");

	row.alive = false;
	row.lifecycle = SummonLifecycleState::Dead;
	CHECK(FormatTitle(row) == "Wolf (dead)");

	row.alive = true;
	row.lifecycle = SummonLifecycleState::Suspended;
	CHECK(FormatTitle(row) == "Wolf (suspended)");

	row.lifecycle = SummonLifecycleState::Active;
	row.type = SummonType::Reanimate;
	CHECK(FormatTitle(row) == "Wolf [Reanimate]");

	row.name.clear();
	row.type = SummonType::SummonCreature;
	CHECK(FormatTitle(row) == "0x0001ABCD");
}

// Given a summon with no target and one with a named or unnamed target
// When the target line is formatted
// Then it is omitted, or names the target with a FormID fallback.
TEST(TestMinionPanelTargetLine)
{
	auto row = MakeRow();
	CHECK(FormatTarget(row).empty());

	row.targetId = 0x0002BCDE;
	CHECK(FormatTarget(row) == "Target: 0x0002BCDE");

	row.targetName = "Bandit";
	CHECK(FormatTarget(row) == "Target: Bandit");
}

// Given a command cooldown that is unknown, ready, or running
// When the command line is formatted
// Then it is omitted, says "ready", or shows the remaining seconds.
TEST(TestMinionPanelCommandLine)
{
	auto row = MakeRow();
	CHECK(FormatCommand(row).empty());

	row.hasCooldown = true;
	CHECK(FormatCommand(row) == "Cmd: ready");

	row.cooldownRemaining = 3.44f;
	CHECK(FormatCommand(row) == "Cmd: 3.4 s");

	row.cooldownRemaining = 0.0f;
	CHECK(FormatCommand(row) == "Cmd: ready");
}

// Given a summon with no timer and one that expires
// When the duration line is formatted
// Then the permanent case is named and the other reads as a clock.
TEST(TestMinionPanelDurationLine)
{
	auto row = MakeRow();
	CHECK(FormatDuration(row) == "Duration: permanent");

	row.remainingDuration = 134.0f;
	CHECK(FormatDuration(row) == "Duration: 2:14");

	row.remainingDuration = 0.0f;
	CHECK(FormatDuration(row) == "Duration: 0:00");
}

// Given no summons, two living ones, and one living plus one down
// When the header is formatted
// Then it counts what is on screen.
TEST(TestMinionPanelHeaderCounts)
{
	std::vector<MinionRow> rows;
	CHECK(FormatHeader(rows) == "Minions: none");

	const auto wolf = MakeRow(0x00000001, "Wolf");
	const auto bear = MakeRow(0x00000002, "Bear");

	rows = { wolf, bear };
	CHECK(FormatHeader(rows) == "Minions: 2");

	auto down = bear;
	down.alive = false;
	down.lifecycle = SummonLifecycleState::Dead;
	rows = { wolf, down };
	CHECK(FormatHeader(rows) == "Minions: 1 alive, 1 down");
}

// Given records that are active, just died, died long ago, or carry no timestamp
// When the panel decides whether to show them
// Then only active records and recent deaths inside the grace window are shown.
TEST(TestMinionPanelShowsDeadOnlyInsideGraceWindow)
{
	// A living summon is always shown.
	CHECK(ShouldShowRecord(SummonLifecycleState::Active, false, 0.0f, 100.0f, kDeadRowGraceSeconds));

	// A summon that just died stays for the grace window, boundary included.
	CHECK(ShouldShowRecord(SummonLifecycleState::Dead, true, 10.0f, 12.0f, kDeadRowGraceSeconds));
	CHECK(ShouldShowRecord(SummonLifecycleState::Dead, true, 10.0f, 13.0f, kDeadRowGraceSeconds));
	CHECK(!ShouldShowRecord(SummonLifecycleState::Dead, true, 10.0f, 13.1f, kDeadRowGraceSeconds));

	// Displaced by the summon limit and dispelled summons are treated alike.
	CHECK(ShouldShowRecord(SummonLifecycleState::Displaced, true, 10.0f, 11.0f, kDeadRowGraceSeconds));
	CHECK(ShouldShowRecord(SummonLifecycleState::Dispatched, true, 10.0f, 11.0f, kDeadRowGraceSeconds));

	// No timestamp, or a timestamp from the future: nothing to count from.
	CHECK(!ShouldShowRecord(SummonLifecycleState::Dispatched, true, 0.0f, 12.0f, kDeadRowGraceSeconds));
	CHECK(!ShouldShowRecord(SummonLifecycleState::Dead, true, 20.0f, 10.0f, kDeadRowGraceSeconds));

	// A zero grace window means "never keep a dead row" (the registry's own policy).
	CHECK(!ShouldShowRecord(SummonLifecycleState::Dead, true, 10.0f, 10.0f, 0.0f));
}

// Given rows in registry (unordered) order, one of them down
// When the display order is built
// Then living summons come first, each group by ascending FormID.
TEST(TestMinionPanelSortsLivingFirstByFormID)
{
	auto downLow = MakeRow(0x00000010, "Frost Atronach");
	downLow.alive = false;
	downLow.lifecycle = SummonLifecycleState::Dead;
	const auto aliveHigh = MakeRow(0x00000020, "Wolf");
	const auto aliveLow = MakeRow(0x00000002, "Bear");

	std::vector<MinionRow> rows{ downLow, aliveHigh, aliveLow };
	SortRowsForDisplay(rows);

	CHECK(rows.size() == 3);
	CHECK(rows[0].summonId == 0x00000002);
	CHECK(rows[1].summonId == 0x00000020);
	CHECK(rows[2].summonId == 0x00000010);
	CHECK(!rows[2].alive);

	// Sorting an empty list is a no-op.
	std::vector<MinionRow> empty;
	SortRowsForDisplay(empty);
	CHECK(empty.empty());
}
