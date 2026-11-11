#include "Core/MinionPanelModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scf::core
{
	namespace
	{
		std::string FormatFormId(EntityID a_id)
		{
			char buf[16]{};
			std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(a_id));
			return std::string(buf);
		}

		std::string FormatInteger(float a_value)
		{
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(a_value)));
			return std::string(buf);
		}

		float Clamp01(float a_value) noexcept
		{
			if (!(a_value > 0.0f)) {  // also catches NaN
				return 0.0f;
			}
			return a_value > 1.0f ? 1.0f : a_value;
		}
	}

	float NormalizeHealthFraction(float a_health, float a_maxHealth) noexcept
	{
		if (!(a_maxHealth > 0.0f) || !(a_health > 0.0f)) {
			return 0.0f;
		}
		return Clamp01(a_health / a_maxHealth);
	}

	Rgba HealthColor(float a_fraction, bool a_alive) noexcept
	{
		if (!a_alive) {
			// Grey, not red: a summon that is already gone is not losing health.
			return Rgba{ 0.55f, 0.55f, 0.60f, 1.0f };
		}
		const float fraction = Clamp01(a_fraction);
		if (fraction <= kHealthCriticalFraction) {
			return Rgba{ 0.86f, 0.25f, 0.20f, 1.0f };
		}
		if (fraction <= kHealthLowFraction) {
			return Rgba{ 0.93f, 0.76f, 0.22f, 1.0f };
		}
		return Rgba{ 0.35f, 0.80f, 0.36f, 1.0f };
	}

	std::string FormatHealthPercent(float a_fraction)
	{
		char buf[16]{};
		std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(Clamp01(a_fraction) * 100.0f)));
		return std::string(buf);
	}

	std::string FormatHealthValue(const MinionRow& a_row)
	{
		if (!a_row.hasHealth) {
			return "?";
		}
		if (!(a_row.maxHealth > 0.0f)) {
			return FormatInteger(a_row.health);
		}
		return FormatInteger(a_row.health) + " / " + FormatInteger(a_row.maxHealth);
	}

	std::string FormatHealthSummary(const MinionRow& a_row)
	{
		if (!a_row.hasHealth) {
			return "?";
		}
		return FormatHealthPercent(NormalizeHealthFraction(a_row.health, a_row.maxHealth)) +
		       " (" + FormatHealthValue(a_row) + ")";
	}

	std::string FormatTime(float a_seconds)
	{
		if (!(a_seconds > 0.0f)) {
			return "0:00";
		}
		const long total = std::lround(a_seconds);
		const long minutes = total / 60;
		const long seconds = total % 60;

		char buf[32]{};
		std::snprintf(buf, sizeof(buf), "%ld:%02ld", minutes, seconds);
		return std::string(buf);
	}

	std::string FormatStatus(const MinionRow& a_row)
	{
		return std::string(ToString(a_row.order)) + " | " +
		       ToString(a_row.state) + " | " +
		       ToString(a_row.aggression);
	}

	std::string FormatTitle(const MinionRow& a_row)
	{
		std::string title = a_row.name.empty() ? FormatFormId(a_row.summonId) : a_row.name;

		if (!a_row.alive) {
			title += " (dead)";
		} else if (a_row.lifecycle == SummonLifecycleState::Suspended) {
			title += " (suspended)";
		} else if (a_row.type != SummonType::SummonCreature) {
			title += " [" + std::string(ToString(a_row.type)) + "]";
		}
		return title;
	}

	std::string FormatTarget(const MinionRow& a_row)
	{
		if (a_row.targetId == InvalidEntityID) {
			return {};
		}
		return "Target: " + (a_row.targetName.empty() ? FormatFormId(a_row.targetId) : a_row.targetName);
	}

	std::string FormatCommand(const MinionRow& a_row)
	{
		if (!a_row.hasCooldown) {
			return {};
		}
		if (!(a_row.cooldownRemaining > 0.0f)) {
			return "Cmd: ready";
		}
		char buf[32]{};
		std::snprintf(buf, sizeof(buf), "Cmd: %.1f s", a_row.cooldownRemaining);
		return std::string(buf);
	}

	std::string FormatDuration(const MinionRow& a_row)
	{
		if (a_row.remainingDuration < 0.0f) {
			return "Duration: permanent";
		}
		return "Duration: " + FormatTime(a_row.remainingDuration);
	}

	std::string FormatHeader(const std::vector<MinionRow>& a_rows)
	{
		if (a_rows.empty()) {
			return "Minions: none";
		}

		const auto alive = std::count_if(a_rows.begin(), a_rows.end(),
			[](const MinionRow& a_row) { return a_row.alive; });
		const auto total = static_cast<long>(a_rows.size());
		const auto down = total - static_cast<long>(alive);

		char buf[64]{};
		if (down == 0) {
			std::snprintf(buf, sizeof(buf), "Minions: %ld", total);
		} else {
			std::snprintf(buf, sizeof(buf), "Minions: %ld alive, %ld down", static_cast<long>(alive), down);
		}
		return std::string(buf);
	}

	bool ShouldShowRecord(
		SummonLifecycleState a_lifecycle,
		bool a_isDead,
		float a_deathTimestamp,
		float a_now,
		float a_gracePeriod) noexcept
	{
		if (a_lifecycle == SummonLifecycleState::Active && !a_isDead) {
			return true;
		}
		if (!(a_gracePeriod > 0.0f)) {
			return false;  // same policy as SummonRegistry::ClearStaleRecords with no grace
		}
		if (!(a_deathTimestamp > 0.0f) || a_now < a_deathTimestamp) {
			return false;
		}
		return (a_now - a_deathTimestamp) <= a_gracePeriod;
	}

	void SortRowsForDisplay(std::vector<MinionRow>& a_rows)
	{
		std::stable_sort(a_rows.begin(), a_rows.end(), [](const MinionRow& a_lhs, const MinionRow& a_rhs) {
			if (a_lhs.alive != a_rhs.alive) {
				return a_lhs.alive;  // living summons first
			}
			return a_lhs.summonId < a_rhs.summonId;
		});
	}
}
