#include "Core/CommandActions.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

namespace scf::core
{
	namespace
	{
		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return a_text.substr(first, last - first + 1);
		}

		std::string Lower(std::string_view a_text)
		{
			std::string result(a_text);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		std::optional<float> ParseNumber(std::string_view a_text)
		{
			const auto text = std::string(Trim(a_text));
			if (text.empty()) {
				return std::nullopt;
			}
			float value = 0.0f;
			const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (ec != std::errc{} || end != text.data() + text.size()) {
				return std::nullopt;
			}
			return value;
		}

		std::vector<std::string_view> Split(std::string_view a_text, char a_sep)
		{
			std::vector<std::string_view> parts;
			std::size_t start = 0;
			while (true) {
				const auto pos = a_text.find(a_sep, start);
				parts.push_back(Trim(a_text.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start)));
				if (pos == std::string_view::npos) {
					return parts;
				}
				start = pos + 1;
			}
		}

		std::optional<CommandEntry> ParseEntry(std::string_view a_value, std::string& a_error)
		{
			const auto parts = Split(a_value, ',');
			if (parts.empty() || parts.size() > 3) {
				a_error = "expected: action, spell, cooldown";
				return std::nullopt;
			}
			CommandEntry entry{};
			const auto action = ParseCommandAction(parts[0]);
			if (!action || *action == CommandAction::None) {
				a_error = "unknown action '" + std::string(parts[0]) + "'";
				return std::nullopt;
			}
			entry.action = *action;
			if (parts.size() > 1 && !parts[1].empty()) {
				entry.spell = ParseFormKey(parts[1]);
				if (!entry.spell) {
					a_error = "bad spell '" + std::string(parts[1]) + "'";
					return std::nullopt;
				}
			}
			if ((entry.action == CommandAction::CastAtTarget || entry.action == CommandAction::Detonate) && !entry.spell) {
				a_error = std::string(ToString(entry.action)) + " needs a spell";
				return std::nullopt;
			}
			if (parts.size() > 2 && !parts[2].empty()) {
				const auto cooldown = ParseNumber(parts[2]);
				if (!cooldown || *cooldown < 1.0f || *cooldown > 600.0f) {
					a_error = "cooldown must be 1..600 seconds";
					return std::nullopt;
				}
				entry.cooldownSeconds = *cooldown;
			}
			return entry;
		}
	}

	const char* ToString(CommandAction a_action) noexcept
	{
		switch (a_action) {
		case CommandAction::Rush:         return "Rush";
		case CommandAction::CastAtTarget: return "CastAtTarget";
		case CommandAction::Detonate:     return "Detonate";
		case CommandAction::None:
		default:                          return "None";
		}
	}

	std::optional<CommandAction> ParseCommandAction(std::string_view a_text) noexcept
	{
		const auto text = Lower(Trim(a_text));
		if (text == "rush") {
			return CommandAction::Rush;
		}
		if (text == "castattarget") {
			return CommandAction::CastAtTarget;
		}
		if (text == "detonate") {
			return CommandAction::Detonate;
		}
		if (text == "none") {
			return CommandAction::None;
		}
		return std::nullopt;
	}

	bool FormKey::operator==(const FormKey& a_other) const noexcept
	{
		return localId == a_other.localId && Lower(plugin) == Lower(a_other.plugin);
	}

	std::optional<FormKey> ParseFormKey(std::string_view a_text)
	{
		const auto text = Trim(a_text);
		const auto bar = text.find('|');
		if (bar == std::string_view::npos) {
			return std::nullopt;
		}
		FormKey key{};
		key.plugin = std::string(Trim(text.substr(0, bar)));
		auto id = Trim(text.substr(bar + 1));
		if (id.size() > 2 && id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) {
			id.remove_prefix(2);
		}
		if (key.plugin.empty() || id.empty()) {
			return std::nullopt;
		}
		const auto [end, ec] = std::from_chars(id.data(), id.data() + id.size(), key.localId, 16);
		if (ec != std::errc{} || end != id.data() + id.size() || key.localId == 0) {
			return std::nullopt;
		}
		return key;
	}

	CommandCatalog ParseCommandCatalog(std::string_view a_text)
	{
		CommandCatalog catalog{};
		std::string section;
		std::size_t lineNo = 0;
		std::size_t start = 0;
		while (start <= a_text.size()) {
			const auto end = a_text.find('\n', start);
			auto line = a_text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
			start = end == std::string_view::npos ? a_text.size() + 1 : end + 1;
			++lineNo;

			if (const auto comment = line.find_first_of(";#"); comment != std::string_view::npos) {
				line = line.substr(0, comment);
			}
			line = Trim(line);
			if (line.empty()) {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = Lower(Trim(line.substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			const auto warn = [&](const std::string& a_why) {
				char prefix[32];
				std::snprintf(prefix, sizeof(prefix), "line %zu: ", lineNo);
				catalog.warnings.push_back(prefix + a_why);
			};
			if (eq == std::string_view::npos) {
				warn("no '='");
				continue;
			}
			const auto key = Trim(line.substr(0, eq));
			const auto value = Trim(line.substr(eq + 1));

			if (section == "general") {
				const auto number = ParseNumber(value);
				const auto name = Lower(key);
				if (name == "frushspeedbonus" && number && *number >= 0.0f && *number <= 300.0f) {
					catalog.rushSpeedBonus = *number;
				} else if (name == "frushseconds" && number && *number >= 0.5f && *number <= 15.0f) {
					catalog.rushSeconds = *number;
				} else {
					warn("bad [General] value '" + std::string(key) + "'");
				}
				continue;
			}
			if (section != "commands") {
				warn("outside [General]/[Commands]");
				continue;
			}

			std::string error;
			const auto entry = ParseEntry(value, error);
			if (!entry) {
				warn(error);
				continue;
			}
			if (Lower(key) == "default") {
				catalog.fallback = *entry;
				continue;
			}
			const auto base = ParseFormKey(key);
			if (!base) {
				warn("bad summon form '" + std::string(key) + "'");
				continue;
			}
			const auto existing = std::find_if(catalog.byBase.begin(), catalog.byBase.end(), [&](const auto& a_pair) { return a_pair.first == *base; });
			if (existing != catalog.byBase.end()) {
				existing->second = *entry;  // last line wins
			} else {
				catalog.byBase.emplace_back(*base, *entry);
			}
		}
		return catalog;
	}

	std::string DefaultCommandCatalogText()
	{
		return R"(; Summon Control Framework - Command actions (PoE2-style "Command" key, Numpad 7 by default).
; Each line: <summon base NPC> = <action>, <spell>, <cooldown seconds>
;   Rush         - focus the aimed enemy and sprint at it (no spell)
;   CastAtTarget - the summon casts <spell> at the aimed enemy
;   Detonate     - the summon casts <spell> (at the enemy, else on itself) and is dispelled
; Forms are "Plugin.esp|0xLocalID". A summon also matches through its template chain.
; Default applies to every summon not listed. Delete this file to get the defaults back.

[General]
fRushSpeedBonus = 60
fRushSeconds = 3

[Commands]
; Flame Atronach (and the potent version): Fireball
Skyrim.esm|0x000204C0 = CastAtTarget, Skyrim.esm|0x0001C789, 10
Skyrim.esm|0x0004E940 = CastAtTarget, Skyrim.esm|0x0001C789, 10
; Storm Atronach (and the potent version): Lightning Bolt
Skyrim.esm|0x000204C2 = CastAtTarget, Skyrim.esm|0x0002DD29, 8
Skyrim.esm|0x0004E944 = CastAtTarget, Skyrim.esm|0x0002DD29, 8
; Frost Atronach (and the potent version): rush in
Skyrim.esm|0x000204C1 = Rush, , 8
Skyrim.esm|0x0004E943 = Rush, , 8
; Example of a sacrifice: a Flame Atronach that explodes into a Fireball
; Skyrim.esm|0x000204C0 = Detonate, Skyrim.esm|0x0001C789, 20
Default = Rush, , 8
)";
	}

	bool CommandCooldowns::IsReady(EntityID a_summon, float a_now) const
	{
		return Remaining(a_summon, a_now) <= 0.0f;
	}

	float CommandCooldowns::Remaining(EntityID a_summon, float a_now) const
	{
		const auto it = _readyAt.find(a_summon);
		return it == _readyAt.end() ? 0.0f : std::max(0.0f, it->second - a_now);
	}

	void CommandCooldowns::Start(EntityID a_summon, float a_now, float a_cooldownSeconds)
	{
		_readyAt[a_summon] = a_now + a_cooldownSeconds;
	}

	void CommandCooldowns::Forget(EntityID a_summon)
	{
		_readyAt.erase(a_summon);
	}

	void CommandCooldowns::Clear()
	{
		_readyAt.clear();
	}
}
