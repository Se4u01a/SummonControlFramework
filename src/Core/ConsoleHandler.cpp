#include "Core/ConsoleHandler.h"
#include "Core/FrameworkContext.h"
#include "Core/Settings.h"
#include "Core/SummonRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace scf::core
{
	namespace
	{
		std::string ToLower(std::string_view a_str)
		{
			std::string result(a_str);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return result;
		}

		std::string Trim(std::string_view a_str)
		{
			const auto first = a_str.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return "";
			}
			const auto last = a_str.find_last_not_of(" \t\r\n");
			return std::string(a_str.substr(first, last - first + 1));
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

		// Strip leading "scf " or "sfc " if present
		if (cmd.rfind("scf ", 0) == 0) {
			cmd = Trim(cmd.substr(4));
		} else if (cmd.rfind("sfc ", 0) == 0) {
			cmd = Trim(cmd.substr(4));
		} else if (cmd == "scf" || cmd == "sfc") {
			cmd = "help";
		}

		const std::string subCmd = ToLower(cmd);

		if (subCmd.empty() || subCmd == "help") {
			a_output("[SCF] Summon Control Framework v0.1.0");
			a_output("[SCF] Available commands:");
			a_output("  scf status   - View framework status, active counts, and current settings");
			a_output("  scf registry - View all tracked summons, owners, and lifecycle states");
			a_output("  scf reload   - Reload configuration from INI file");
			a_output("  scf help     - Display this command reference");
			a_output("[SCF] Squad Orders (scf order <action>):");
			a_output("  scf order return      - Recall all summons immediately back to player");
			a_output("  scf order follow      - Resume following player in formation");
			a_output("  scf order guard       - Hold current position and guard area (also: wait, stay)");
			a_output("  scf order moveto      - Move summons to position: moveto <x y z> or moveto (crosshair)");
			a_output("  scf order passive     - Set aggression to Passive (never attack)");
			a_output("  scf order defensive   - Set aggression to Defensive (attack only active threats)");
			a_output("  scf order aggressive  - Set aggression to Aggressive (attack any enemy in sight)");
			a_output("  scf order focus <id>  - Focus fire on target by FormID (hex e.g. 0001234F)");
			return;
		}

		if (subCmd == "status") {
			const auto& s = a_context.GetSettings().Get();
			const std::size_t pending = a_context.GetCommandQueue().GetPendingCount();

			char buf[512];
			a_output("[SCF] Status:");

			std::snprintf(buf, sizeof(buf), "  Enabled: %s | Log level: %s | Max summons per owner: %zu",
				s.general.enabled ? "true" : "false",
				s.general.logLevel.c_str(),
				s.general.maxSummonsPerOwner);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Scheduler: %.1f Hz | TimeSlicing: %s | CommandTTL: %.1fs | PendingCommands: %zu",
				s.timing.updateFrequencyHz,
				s.timing.enableTimeSlicing ? "true" : "false",
				s.timing.commandTTL,
				pending);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Collision: Bypass=%s | SummonToSummon=%s",
				s.collision.enableCollisionBypass ? "true" : "false",
				s.collision.enableSummonToSummonBypass ? "true" : "false");
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  FriendlyFire: Protection=%s | MeleePassThrough=%s | ProjectilePassThrough=%s",
				s.friendlyFire.enabled ? "true" : "false",
				s.friendlyFire.meleePassThrough ? "true" : "false",
				s.friendlyFire.physicalProjectilePassThrough ? "true" : "false");
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Combat: FollowDist=%.0f | ReturnDist=%.0f | EngageDist=%.0f | MaxRadius=%.0f",
				s.combat.followDistanceThreshold,
				s.combat.returnDistanceThreshold,
				s.combat.engageDistanceThreshold,
				s.combat.maxTargetRadius);
			a_output(buf);

			std::snprintf(buf, sizeof(buf), "  Hotkeys (Numpad): Focus=0x%X | Return=0x%X | Aggression=0x%X | MoveTo=0x%X | Guard=0x%X | Follow=0x%X",
				s.hotkeys.focusTargetKey,
				s.hotkeys.commandReturnKey,
				s.hotkeys.toggleAggressionKey,
				s.hotkeys.moveToKey,
				s.hotkeys.waitGuardKey,
				s.hotkeys.followKey);
			a_output(buf);

			return;
		}

		if (subCmd == "registry") {
			const auto& registry = a_context.GetRegistry();
			const auto& records = registry.GetAllRecords();

			if (records.empty()) {
				a_output("[SCF] Registry is empty (0 registered summons).");
				return;
			}

			std::vector<const SummonRecord*> activeRecords;
			std::vector<const SummonRecord*> inactiveRecords;

			for (const auto& [id, rec] : records) {
				if (rec.lifecycle == SummonLifecycleState::Active || rec.lifecycle == SummonLifecycleState::Suspended) {
					activeRecords.push_back(&rec);
				} else {
					inactiveRecords.push_back(&rec);
				}
			}

			char buf[512];
			std::snprintf(buf, sizeof(buf), "[SCF] Registry: %zu registered summon(s) (%zu active, %zu inactive/purged):",
				records.size(), activeRecords.size(), inactiveRecords.size());
			a_output(buf);

			for (const auto* rec : activeRecords) {
				std::snprintf(buf, sizeof(buf), "  [Active] ID: 0x%08X | Owner: 0x%08X | Type: %s | State: %s | Rem: %.1fs",
					rec->summonId,
					rec->ownerId,
					ToString(rec->type),
					ToString(rec->lifecycle),
					rec->remainingDuration);
				a_output(buf);
			}

			for (const auto* rec : inactiveRecords) {
				std::snprintf(buf, sizeof(buf), "  [Inactive] ID: 0x%08X | Owner: 0x%08X | Type: %s | State: %s",
					rec->summonId,
					rec->ownerId,
					ToString(rec->type),
					ToString(rec->lifecycle));
				a_output(buf);
			}

			return;
		}

		if (subCmd.rfind("order", 0) == 0) {
			std::string orderArgs = cmd.size() > 5 ? Trim(cmd.substr(5)) : "";
			std::string lowerArgs = ToLower(orderArgs);

			if (lowerArgs.empty() || lowerArgs == "help") {
				a_output("[SCF] Squad Orders Reference:");
				a_output("  scf order return      - Recall all summons immediately back to player");
				a_output("  scf order follow      - Resume following player in formation");
				a_output("  scf order guard       - Hold current position and guard area (also: wait, stay)");
				a_output("  scf order moveto      - Move summons to position: moveto <x y z> or moveto");
				a_output("  scf order passive     - Set aggression to Passive (never attack)");
				a_output("  scf order defensive   - Set aggression to Defensive (attack only active threats)");
				a_output("  scf order aggressive  - Set aggression to Aggressive (attack any enemy in sight)");
				a_output("  scf order focus <id>  - Focus fire on target by FormID (hex e.g. 0001234F)");
				return;
			}

			if (lowerArgs == "return") {
				Command c{};
				c.type = CommandType::SetOrder;
				c.order = SummonOrder::Return;
				c.priority = CommandPriority::PlayerDirect;
				c.summonId = InvalidEntityID;
				a_context.GetCommandQueue().Enqueue(c);
				a_output("[SCF] Order: Return dispatched to all summons.");
				return;
			}
			if (lowerArgs == "follow") {
				Command c{};
				c.type = CommandType::SetOrder;
				c.order = SummonOrder::Follow;
				c.priority = CommandPriority::PlayerDirect;
				c.summonId = InvalidEntityID;
				a_context.GetCommandQueue().Enqueue(c);
				a_output("[SCF] Order: Follow dispatched to all summons.");
				return;
			}
			if (lowerArgs == "guard" || lowerArgs == "stay" || lowerArgs == "wait") {
				Command c{};
				c.type = CommandType::SetOrder;
				c.order = SummonOrder::WaitGuard;
				c.priority = CommandPriority::PlayerDirect;
				c.summonId = InvalidEntityID;
				a_context.GetCommandQueue().Enqueue(c);
				a_output("[SCF] Order: Guard position dispatched to all summons.");
				return;
			}
			if (lowerArgs.rfind("moveto", 0) == 0 || lowerArgs.rfind("move", 0) == 0 || lowerArgs.rfind("goto", 0) == 0) {
				std::string coords;
				if (lowerArgs.rfind("moveto", 0) == 0) {
					coords = Trim(orderArgs.substr(6));
				} else if (lowerArgs.rfind("goto", 0) == 0) {
					coords = Trim(orderArgs.substr(4));
				} else {
					coords = Trim(orderArgs.substr(4));
				}

				Vec3 targetPos{};
				bool hasCoords = false;
				if (!coords.empty()) {
					std::stringstream ss(coords);
					float x, y, z;
					if (ss >> x >> y >> z) {
						targetPos = { x, y, z };
						hasCoords = true;
					}
				}

				Command c{};
				c.type = CommandType::SetOrder;
				c.order = SummonOrder::MoveTo;
				c.priority = CommandPriority::PlayerDirect;
				c.summonId = InvalidEntityID;
				c.targetPosition = targetPos;
				a_context.GetCommandQueue().Enqueue(c);

				char buf[256];
				if (hasCoords) {
					std::snprintf(buf, sizeof(buf), "[SCF] Order: MoveTo (%.1f, %.1f, %.1f) dispatched to all summons.", targetPos.x, targetPos.y, targetPos.z);
				} else {
					std::snprintf(buf, sizeof(buf), "[SCF] Order: MoveTo crosshair point dispatched to all summons.");
				}
				a_output(buf);
				return;
			}
			if (lowerArgs == "passive") {
				Command c{};
				c.type = CommandType::SetAggression;
				c.aggression = AggressionMode::Passive;
				c.priority = CommandPriority::ModeChange;
				c.summonId = InvalidEntityID;
				a_context.GetCommandQueue().Enqueue(c);
				a_output("[SCF] Order: Aggression set to Passive.");
				return;
			}
			if (lowerArgs == "defensive") {
				Command c{};
				c.type = CommandType::SetAggression;
				c.aggression = AggressionMode::Defensive;
				c.priority = CommandPriority::ModeChange;
				c.summonId = InvalidEntityID;
				a_context.GetCommandQueue().Enqueue(c);
				a_output("[SCF] Order: Aggression set to Defensive.");
				return;
			}
			if (lowerArgs == "aggressive") {
				Command c{};
				c.type = CommandType::SetAggression;
				c.aggression = AggressionMode::Aggressive;
				c.priority = CommandPriority::ModeChange;
				c.summonId = InvalidEntityID;
				a_context.GetCommandQueue().Enqueue(c);
				a_output("[SCF] Order: Aggression set to Aggressive.");
				return;
			}
			if (lowerArgs.rfind("focus", 0) == 0) {
				std::string idStr = Trim(orderArgs.substr(5));
				EntityID targetId = InvalidEntityID;
				try {
					targetId = static_cast<EntityID>(std::stoul(idStr, nullptr, 0));
				} catch (...) {
					targetId = InvalidEntityID;
				}

				if (targetId != InvalidEntityID) {
					Command c{};
					c.type = CommandType::SetFocusTarget;
					c.focusTargetId = targetId;
					c.priority = CommandPriority::PlayerDirect;
					c.summonId = InvalidEntityID;
					a_context.GetCommandQueue().Enqueue(c);
					char buf[256];
					std::snprintf(buf, sizeof(buf), "[SCF] Order: FocusTarget (0x%08X) dispatched to squad.", targetId);
					a_output(buf);
				} else {
					a_output("[SCF] Usage: scf order focus <FormID>");
				}
				return;
			}

			a_output("[SCF] Unknown order subcommand. Available orders: return, follow, guard, moveto, passive, defensive, aggressive, focus <id>");
			return;
		}

		if (subCmd == "reload") {
			if (a_context.GetSettings().LoadFromFile(a_configFilePath)) {
				a_context.GetRegistry().SetDefaultMaxSummonsPerOwner(
					a_context.GetSettings().Get().general.maxSummonsPerOwner);
				char buf[256];
				std::snprintf(buf, sizeof(buf), "[SCF] Configuration reloaded successfully from '%s'.", a_configFilePath.c_str());
				a_output(buf);
			} else {
				char buf[256];
				std::snprintf(buf, sizeof(buf), "[SCF] Failed to reload configuration from '%s'.", a_configFilePath.c_str());
				a_output(buf);
			}
			return;
		}

		char buf[256];
		std::snprintf(buf, sizeof(buf), "[SCF] Unknown command '%s'. Type 'scf help' for available commands.", cmd.c_str());
		a_output(buf);
	}
}
