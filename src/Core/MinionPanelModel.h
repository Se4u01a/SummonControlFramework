#pragma once

#include "Core/EntityID.h"
#include "Core/SummonRegistry.h"
#include "Core/Types.h"

#include <string>
#include <vector>

namespace scf::core
{
	// PoE2 stage M6 - the minion HUD panel: display rules only.
	//
	// This header is deliberately free of ImGui and of the game runtime: the engine
	// adapter (src/Integrations/MinionPanel.cpp) fills a MinionRow from the registry and
	// the live actors on the main thread, publishes the rows as a snapshot, and the FLICK
	// draw callback only formats them through these functions. Colours are plain floats so
	// the same rules run in the engine-free unit tests (tests/test_minion_panel.cpp).

	struct Rgba
	{
		float r{ 1.0f };
		float g{ 1.0f };
		float b{ 1.0f };
		float a{ 1.0f };
	};

	// One line of the panel: a summon the owner currently commands (or one that died
	// within the registry's grace window, so the panel can say who went down).
	struct MinionRow
	{
		EntityID summonId{ InvalidEntityID };
		std::string name;  // actor display name; the adapter substitutes "0x%08X" when unnamed
		SummonType type{ SummonType::SummonCreature };
		SummonLifecycleState lifecycle{ SummonLifecycleState::Active };
		bool alive{ true };

		bool hasHealth{ false };  // false when the actor could not be resolved at snapshot time
		float health{ 0.0f };
		float maxHealth{ 0.0f };

		SummonOrder order{ SummonOrder::Follow };
		SummonState state{ SummonState::Idle };
		AggressionMode aggression{ AggressionMode::Defensive };

		EntityID targetId{ InvalidEntityID };
		std::string targetName;

		bool hasCooldown{ false };  // false when the command-cooldown source is unavailable
		float cooldownRemaining{ 0.0f };
		float cooldownTotal{ 0.0f };

		float remainingDuration{ -1.0f };  // < 0: no timer (permanent summon)
	};

	// Health bar colour bands: at or below 30% is critical, at or below 60% is low.
	constexpr float kHealthCriticalFraction = 0.30f;
	constexpr float kHealthLowFraction = 0.60f;

	// How long a summon that is dead / dispelled / displaced stays in the panel. Mirrors
	// the registry's own stale-record grace (SummonRegistry::ClearStaleRecords), so a row
	// never outlives the record it was built from.
	constexpr float kDeadRowGraceSeconds = 3.0f;

	// Current / maximum as a 0..1 bar fraction. A non-positive maximum, or a pool that
	// has not been read yet, is an empty bar; anything beyond the maximum is clamped.
	[[nodiscard]] float NormalizeHealthFraction(float a_health, float a_maxHealth) noexcept;

	// Colour of the health bar and of the percentage next to the name.
	[[nodiscard]] Rgba HealthColor(float a_fraction, bool a_alive) noexcept;

	// "71%"
	[[nodiscard]] std::string FormatHealthPercent(float a_fraction);

	// "142 / 200", or just "142" when the maximum is unknown, or "?" when the pool was
	// never read.
	[[nodiscard]] std::string FormatHealthValue(const MinionRow& a_row);

	// "71% (142 / 200)" / "?" - the one line drawn next to the name.
	[[nodiscard]] std::string FormatHealthSummary(const MinionRow& a_row);

	// Seconds as "m:ss" ("0:07", "1:05"). Non-positive values read as "0:00".
	[[nodiscard]] std::string FormatTime(float a_seconds);

	// "Follow | FollowOwner | Defensive" - order, FSM state, aggression mode.
	[[nodiscard]] std::string FormatStatus(const MinionRow& a_row);

	// "Wolf (dead)" / "Wolf (suspended)" / "Wolf [Reanimate]" / "Wolf". An empty name
	// falls back to the FormID.
	[[nodiscard]] std::string FormatTitle(const MinionRow& a_row);

	// "Target: Bandit", "Target: 0x0001ABCD" when the target has no name, or an empty
	// string when there is no target.
	[[nodiscard]] std::string FormatTarget(const MinionRow& a_row);

	// "Cmd: ready", "Cmd: 3.4 s", or an empty string when the cooldown is not known.
	[[nodiscard]] std::string FormatCommand(const MinionRow& a_row);

	// "Duration: permanent" (no timer), "Duration: 2:14", "Duration: 0:00" when expiring.
	[[nodiscard]] std::string FormatDuration(const MinionRow& a_row);

	// "Minions: 2", "Minions: 1 alive, 1 down", "Minions: none".
	[[nodiscard]] std::string FormatHeader(const std::vector<MinionRow>& a_rows);

	// Whether a registry record belongs in the panel: an active summon always does, and a
	// record that left the roster only within a_gracePeriod of its death.
	[[nodiscard]] bool ShouldShowRecord(
		SummonLifecycleState a_lifecycle,
		bool a_isDead,
		float a_deathTimestamp,
		float a_now,
		float a_gracePeriod) noexcept;

	// Display order: living summons first, then the ones that went down; ascending FormID
	// within each group. The registry stores records in an unordered_map, so this is what
	// keeps rows from swapping places between ticks.
	void SortRowsForDisplay(std::vector<MinionRow>& a_rows);
}
