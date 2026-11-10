#include "Core/ConsoleHandler.h"
#include "Core/FrameworkContext.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

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

		// Strip leading "scf " if present
		if (cmd.rfind("scf ", 0) == 0) {
			cmd = Trim(cmd.substr(4));
		} else if (cmd == "scf") {
			cmd = "help";
		}

		const std::string subCmd = ToLower(cmd);

		if (subCmd.empty() || subCmd == "help") {
			a_output("[SCF] Summon Control Framework v0.1.0");
			a_output("[SCF] Available commands:");
			a_output("  scf status   - View framework status, active counts, and current settings");
			a_output("  scf registry - View all tracked summons, owners, and lifecycle states");
			a_output("  scf order <command> - Issue squad orders: follow, guard, return, passive, defensive, aggressive, focus <id>");
			a_output("  scf reload   - Reload configuration from INI file");
			a_output("  scf help     - Display this command reference");
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

			char buf[256];
			std::snprintf(buf, sizeof(buf), "[SCF] Registry: %zu registered summon(s):", records.size());
			a_output(buf);

			for (const auto* rec : activeRecords) {
				std::snprintf(buf, sizeof(buf), "  - Summon 0x%08X | Owner: 0x%08X | State: %s | Duration: %.1fs",
					rec->summonId,
					rec->ownerId,
					ToString(rec->lifecycle),
					rec->remainingDuration);
				a_output(buf);
			}

			for (const auto* rec : inactiveRecords) {
				std::snprintf(buf, sizeof(buf), "  - Summon 0x%08X | Owner: 0x%08X | State: %s (fading)",
					rec->summonId,
					rec->ownerId,
					ToString(rec->lifecycle));
				a_output(buf);
			}
			return;
		}

		if (subCmd.rfind("order", 0) == 0) {
			std::string orderArgs = Trim(cmd.substr(5));
			std::string lowerArgs = ToLower(orderArgs);

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

			a_output("[SCF] Usage: scf order <follow | guard | return | passive | defensive | aggressive | focus <FormID>>");
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
