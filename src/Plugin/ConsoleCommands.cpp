#include "Plugin/ConsoleCommands.h"
#include "Core/ConsoleHandler.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

namespace scf::plugin
{
	namespace
	{
		bool ExecuteSCFCommand(
			const RE::SCRIPT_PARAMETER* /*a_paramInfo*/,
			RE::SCRIPT_FUNCTION::ScriptData* a_scriptData,
			RE::TESObjectREFR* /*a_thisObj*/,
			RE::TESObjectREFR* /*a_containingObj*/,
			RE::Script* /*a_scriptObj*/,
			RE::ScriptLocals* /*a_locals*/,
			double& /*a_result*/,
			std::uint32_t& /*a_opcodeOffsetPtr*/)
		{
			std::string argStr;
			if (a_scriptData && a_scriptData->numParams > 0) {
				auto* chunk = a_scriptData->GetChunk();
				for (std::uint16_t i = 0; i < a_scriptData->numParams && chunk; ++i) {
					if (auto* strChunk = chunk->AsString()) {
						std::string s = strChunk->GetString();
						if (!s.empty()) {
							if (!argStr.empty()) {
								argStr += " ";
							}
							argStr += s;
						}
						chunk = strChunk->GetNext();
					} else {
						break;
					}
				}
			}

			auto outputCallback = [](const std::string& line) {
				logger::info("{}", line);
				if (auto* console = RE::ConsoleLog::GetSingleton()) {
					console->Print("%s", line.c_str());
				}
			};

			core::ConsoleHandler::Execute(argStr, core::FrameworkContext::GetSingleton(), outputCallback);
			return true;
		}

		RE::SCRIPT_PARAMETER g_scfParams[] = {
			{ "Subcommand", RE::SCRIPT_PARAM_TYPE::kChar, true },
			{ "Param1", RE::SCRIPT_PARAM_TYPE::kChar, true },
			{ "Param2", RE::SCRIPT_PARAM_TYPE::kChar, true },
			{ "Param3", RE::SCRIPT_PARAM_TYPE::kChar, true }
		};
	}

	bool ConsoleCommands::Register()
	{
		bool registered = false;

		// 1. Register primary command 'scf'
		auto* cmdSCF = RE::SCRIPT_FUNCTION::LocateConsoleCommand("TestSeenData");
		if (cmdSCF) {
			cmdSCF->functionName = "scf";
			cmdSCF->shortName = "scf";
			cmdSCF->helpString = "Summon Control Framework: scf [status|registry|order|reload|help]";
			cmdSCF->executeFunction = &ExecuteSCFCommand;
			cmdSCF->SetParameters(g_scfParams);
			logger::info("[ConsoleCommands] Registered console command 'scf'.");
			registered = true;
		}

		// 2. Register alias 'sfc' for user typo resilience
		auto* cmdSFC = RE::SCRIPT_FUNCTION::LocateConsoleCommand("BetaComment");
		if (cmdSFC) {
			cmdSFC->functionName = "sfc";
			cmdSFC->shortName = "sfc";
			cmdSFC->helpString = "Summon Control Framework (alias): sfc [status|registry|order|reload|help]";
			cmdSFC->executeFunction = &ExecuteSCFCommand;
			cmdSFC->SetParameters(g_scfParams);
			logger::info("[ConsoleCommands] Registered console alias 'sfc'.");
			registered = true;
		}

		if (!registered) {
			logger::warn("[ConsoleCommands] Could not locate console command entries to bind 'scf'/'sfc'.");
		}

		return registered;
	}
}
