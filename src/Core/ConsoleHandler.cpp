#include "Core/ConsoleHandler.h"
#include "Core/FrameworkContext.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace scf::core
{
	namespace
	{
		std::string Trim(std::string_view a_str)
		{
			const auto first = a_str.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return "";
			}
			const auto last = a_str.find_last_not_of(" \t\r\n");
			return std::string(a_str.substr(first, last - first + 1));
		}

		std::string ToLower(std::string_view a_str)
		{
			std::string result(a_str);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return result;
		}
	}

	void ConsoleHandler::Execute(
		std::string_view a_commandLine,
		FrameworkContext& a_context,
		OutputCallback a_output,
		const std::string& a_configFilePath)
	{
		if (!a_output) {
			return;
		}

		std::string cmd = Trim(a_commandLine);

		// Strip leading "scf " or "scf" prefix if present
		const std::string lower = ToLower(cmd);
		if (lower == "scf") {
			cmd.clear();
		} else if (lower.rfind("scf ", 0) == 0) {
			cmd = Trim(cmd.substr(4));
		}

		const std::string subCmd = ToLower(cmd);

		if (subCmd.empty() || subCmd == "help") {
			a_output("[SCF] Summon Control Framework v0.1.0");
			a_output("[SCF] Available commands:");
			a_output("  scf status   - View framework status, active counts, and current settings");
			a_output("  scf registry - View all tracked summons, owners, and lifecycle states");
			a_output("  scf reload   - Reload configuration from INI file");
			a_output("  scf help     - Display this command reference");
			return;
		}

		if (subCmd == "status") {
			const auto& s = a_context.GetSettings().Get();
			const std::size_t pending = a_context.GetCommandQueue().GetPendingCount();

			char buf[512];
			a_output("[SCF] Status:");

			std::snprintf(buf, sizeof(buf), "  Framework: %s (LogLevel: %s)",
				s.general.enabled ? "Enabled" : "Disabled",
				s.general.logLevel.c_str());
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Limits: Max summons per owner: %zu",
				s.general.maxSummonsPerOwner);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Scheduler: %.1f Hz (Time-slicing: %s, Command TTL: %.1fs)",
				s.timing.updateFrequencyHz,
				s.timing.enableTimeSlicing ? "Enabled" : "Disabled",
				s.timing.commandTTL);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Combat: Follow: %.1f, Return: %.1f, Target Radius: %.1f",
				s.combat.followDistanceThreshold,
				s.combat.returnDistanceThreshold,
				s.combat.maxTargetRadius);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Squad: Melee slots: %zu, Ranged slots: %zu, Spread: %.1f",
				s.squad.maxMeleeSlotsPerTarget,
				s.squad.maxRangedSlotsPerTarget,
				s.squad.formationSpreadRadius);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Pending commands in queue: %zu", pending);
			a_output(buf);
			return;
		}

		if (subCmd == "registry") {
			const auto& reg = a_context.GetRegistry();
			const auto& records = reg.GetAllRecords();

			if (records.empty()) {
				a_output("[SCF] Registry: 0 registered summons.");
				return;
			}

			char buf[256];
			std::snprintf(buf, sizeof(buf), "[SCF] Registry: %zu registered summon(s):", records.size());
			a_output(buf);

			for (const auto& [id, rec] : records) {
				std::snprintf(buf, sizeof(buf), "  - Summon 0x%08X | Owner: 0x%08X | State: %s | Duration: %.1fs",
					rec.summonId,
					rec.ownerId,
					ToString(rec.lifecycle),
					rec.remainingDuration);
				a_output(buf);
			}
			return;
		}

		if (subCmd == "reload") {
			if (a_context.GetSettings().LoadFromFile(a_configFilePath)) {
				char buf[256];
				std::snprintf(buf, sizeof(buf), "[SCF] Configuration reloaded successfully from '%s'.", a_configFilePath.c_str());
				a_output(buf);
			} else {
				char buf[256];
				std::snprintf(buf, sizeof(buf), "[SCF] Failed to reload configuration from '%s'. Preserved current settings.", a_configFilePath.c_str());
				a_output(buf);
			}
			return;
		}

		char buf[256];
		std::snprintf(buf, sizeof(buf), "[SCF] Unknown command '%s'. Type 'scf help' for available commands.", cmd.c_str());
		a_output(buf);
	}
}
