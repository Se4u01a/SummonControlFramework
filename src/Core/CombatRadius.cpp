#include "Core/CombatRadius.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <istream>
#include <limits>
#include <string_view>

namespace scf::core
{
	namespace
	{
		std::string_view Trim(std::string_view value)
		{
			const auto start = value.find_first_not_of(" \t\r");
			return start == value.npos ? std::string_view{} : value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
		}

		template<class T>
		T Number(std::string_view value, T fallback)
		{
			if (value.empty()) {
				return fallback;
			}
			T result{};
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
			return error == std::errc{} && end == value.data() + value.size() && std::isfinite(result) ? result : fallback;
		}

		bool Bool(std::string_view value, bool fallback)
		{
			if (value == "true" || value == "1" || value == "yes" || value == "on") {
				return true;
			}
			if (value == "false" || value == "0" || value == "no" || value == "off") {
				return false;
			}
			return fallback;
		}

		double DistanceSquared(const Vec3& a, const Vec3& b)
		{
			const double dx = static_cast<double>(a.x) - b.x;
			const double dy = static_cast<double>(a.y) - b.y;
			return dx * dx + dy * dy;
		}
	}

	CombatRadiusSettings ReadCombatRadiusSettings(std::istream& input)
	{
		CombatRadiusSettings settings;
		std::string section;
		bool customKeywords = false;
		for (std::string line; std::getline(input, line);) {
			std::string_view view = line;
			if (view.starts_with("\xEF\xBB\xBF")) {
				view.remove_prefix(3);
			}
			view = Trim(view.substr(0, view.find_first_of(";#")));
			if (view.starts_with('[') && view.ends_with(']')) {
				section = Trim(view.substr(1, view.size() - 2));
				if (section == "Keywords" && !customKeywords) {
					customKeywords = true;
					settings.keywords.clear();
				}
				continue;
			}
			const auto equals = view.find('=');
			if (equals == view.npos) {
				continue;
			}
			const auto key = Trim(view.substr(0, equals));
			const auto value = Trim(view.substr(equals + 1));
			if (section == "Keywords") {
				const float radius = Number(value, 0.0f);
				if (!key.empty() && radius > 0.0f &&
				    std::ranges::none_of(settings.keywords, [&](const auto& entry) { return entry.editorId == key; })) {
					settings.keywords.push_back({ std::string(key), radius });
				}
			} else if (section == "Main") {
				if (key == "enabled") settings.enabled = Bool(value, settings.enabled);
				if (key == "verbose") settings.verbose = Bool(value, settings.verbose);
				if (key == "tickMs") settings.tickMs = std::max(100, Number(value, settings.tickMs));
			} else if (section == "Combat") {
				if (key == "enabled") settings.combatEnabled = Bool(value, settings.combatEnabled);
				if (key == "hysteresis") settings.hysteresis = std::max(0.0f, Number(value, settings.hysteresis));
				if (key == "retargetCooldownMs") settings.retargetCooldownMs = std::clamp(Number(value, settings.retargetCooldownMs), 250, 60000);
			} else if (section == "Approach") {
				if (key == "enabled") settings.approachEnabled = Bool(value, settings.approachEnabled);
				if (key == "package") settings.approachPackage = value;
				if (key == "ringMin") settings.ringMin = Number(value, settings.ringMin);
				if (key == "ringMax") settings.ringMax = Number(value, settings.ringMax);
			} else if (section == "Collision" && key == "enabled") {
				settings.collisionEnabled = Bool(value, settings.collisionEnabled);
			} else if (section == "Menu" && key == "enabled") {
				settings.menuEnabled = Bool(value, settings.menuEnabled);
			}
		}
		if (settings.ringMin > settings.ringMax) {
			std::swap(settings.ringMin, settings.ringMax);
		}
		return settings;
	}

	bool UsesCombatRadius(const CombatRadiusSettings& settings, const SummonModel& model, float radius) noexcept
	{
		return settings.enabled && settings.combatEnabled && radius > 0.0f &&
			model.order == SummonOrder::Follow && model.aggression != AggressionMode::Passive;
	}

	RadiusDecision DecideCombatRadius(const Vec3& player, const Vec3& summon, float radius,
		float hysteresis, std::span<const RadiusEnemy> candidates, EntityID current, bool ready) noexcept
	{
		const double entry = radius;
		const double exit = entry + hysteresis;
		double bestDistance = std::numeric_limits<double>::infinity();
		EntityID best = InvalidEntityID;
		for (const auto& enemy : candidates) {
			const double ownerDistance = DistanceSquared(player, enemy.position);
			if (enemy.id == current && current != InvalidEntityID && ownerDistance <= exit * exit) {
				return { RadiusAction::Hold, current };
			}
			if (!(ownerDistance <= entry * entry)) {
				continue;
			}
			const double summonDistance = DistanceSquared(summon, enemy.position);
			if (summonDistance < bestDistance || (summonDistance == bestDistance && enemy.id < best)) {
				best = enemy.id;
				bestDistance = summonDistance;
			}
		}
		if (best == InvalidEntityID) {
			return { RadiusAction::Park };
		}
		return { ready ? RadiusAction::Start : RadiusAction::Wait, best };
	}

	CombatPolicy BuildRadiusPolicy(EntityID target) noexcept
	{
		const bool hasTarget = target != InvalidEntityID;
		return { hasTarget, hasTarget, target, !hasTarget, true };
	}
}
