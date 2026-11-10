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
				auto* chunk = a_scriptData->GetStringChunk();
				if (chunk) {
					argStr = chunk->GetString();
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
			{ "Subcommand", RE::SCRIPT_PARAM_TYPE::kChar, true }
		};
	}

	bool ConsoleCommands::Register()
	{
		auto* cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand("TestSeenData");
		if (!cmd) {
			cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand("BetaComment");
		}

		if (!cmd) {
			logger::warn("[ConsoleCommands] Could not locate eligible console command entry to bind 'scf'.");
			return false;
		}

		cmd->functionName = "scf";
		cmd->shortName = "scf";
		cmd->helpString = "Summon Control Framework: scf [status|registry|reload|help]";
		cmd->executeFunction = &ExecuteSCFCommand;
		cmd->SetParameters(g_scfParams);

		logger::info("[ConsoleCommands] Registered console command 'scf'.");
		return true;
	}
}
