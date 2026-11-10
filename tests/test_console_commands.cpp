#include "Core/ConsoleHandler.h"
#include "Core/FrameworkContext.h"
#include "check.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace scf::core;

TEST(console_command_help)
{
	auto& context = FrameworkContext::GetSingleton();
	std::vector<std::string> output;

	auto callback = [&output](const std::string& line) {
		output.push_back(line);
	};

	ConsoleHandler::Execute("", context, callback);
	CHECK(!output.empty());
	CHECK(output[0].find("Summon Control Framework") != std::string::npos);

	output.clear();
	ConsoleHandler::Execute("help", context, callback);
	CHECK(!output.empty());
	CHECK(output[0].find("Summon Control Framework") != std::string::npos);

	output.clear();
	ConsoleHandler::Execute("scf help", context, callback);
	CHECK(!output.empty());
	CHECK(output[0].find("Summon Control Framework") != std::string::npos);
}

TEST(console_command_status)
{
	auto& context = FrameworkContext::GetSingleton();
	std::vector<std::string> output;

	ConsoleHandler::Execute("status", context, [&output](const std::string& line) {
		output.push_back(line);
	});

	CHECK(!output.empty());
	CHECK(output[0].find("[SCF] Status:") != std::string::npos);

	bool foundLimits = false;
	bool foundScheduler = false;
	for (const auto& line : output) {
		if (line.find("Max summons per owner") != std::string::npos) {
			foundLimits = true;
		}
		if (line.find("Scheduler:") != std::string::npos) {
			foundScheduler = true;
		}
	}

	CHECK(foundLimits);
	CHECK(foundScheduler);
}

TEST(console_command_registry_empty_and_populated)
{
	auto& context = FrameworkContext::GetSingleton();
	context.ResetRuntimeState();

	std::vector<std::string> output;
	auto callback = [&output](const std::string& line) {
		output.push_back(line);
	};

	// 1. Empty registry
	ConsoleHandler::Execute("registry", context, callback);
	CHECK(!output.empty());
	CHECK(output[0].find("0 registered summons") != std::string::npos);

	// 2. Populated registry
	context.GetRegistry().RegisterSummon(0x14, 0x100, SummonType::SummonCreature, 60.0f, 0.0f);
	context.GetRegistry().RegisterSummon(0x14, 0x200, SummonType::SummonCreature, 120.0f, 0.0f);

	output.clear();
	ConsoleHandler::Execute("scf registry", context, callback);
	CHECK(output.size() >= 3);
	CHECK(output[0].find("2 registered summon(s)") != std::string::npos);

	bool found100 = false;
	bool found200 = false;
	for (const auto& line : output) {
		if (line.find("0x00000100") != std::string::npos) {
			found100 = true;
		}
		if (line.find("0x00000200") != std::string::npos) {
			found200 = true;
		}
	}
	CHECK(found100);
	CHECK(found200);
}

TEST(console_command_reload)
{
	auto& context = FrameworkContext::GetSingleton();
	std::vector<std::string> output;

	const std::string testIni = "test_console_reload.ini";
	{
		std::ofstream ofs(testIni);
		ofs << "[General]\n";
		ofs << "uMaxSummonsPerOwner = 4\n";
	}

	ConsoleHandler::Execute("reload", context, [&output](const std::string& line) {
		output.push_back(line);
	}, testIni);

	CHECK(!output.empty());
	CHECK(output[0].find("reloaded successfully") != std::string::npos);
	CHECK(context.GetSettings().Get().general.maxSummonsPerOwner == 4);

	// Failed reload (non-existent file)
	output.clear();
	ConsoleHandler::Execute("reload", context, [&output](const std::string& line) {
		output.push_back(line);
	}, "non_existent_file.ini");

	CHECK(!output.empty());
	CHECK(output[0].find("Failed to reload") != std::string::npos);

	std::remove(testIni.c_str());
}

TEST(console_command_order)
{
	auto& context = FrameworkContext::GetSingleton();
	context.ResetRuntimeState();
	std::vector<std::string> output;

	auto callback = [&output](const std::string& line) {
		output.push_back(line);
	};

	// 1. Order return
	ConsoleHandler::Execute("scf order return", context, callback);
	CHECK(!output.empty());
	CHECK(output.back().find("Return dispatched") != std::string::npos);
	CHECK(context.GetCommandQueue().GetPendingCount() == 1);

	Command cmd;
	CHECK(context.GetCommandQueue().PopNext(0.0f, cmd));
	CHECK(cmd.type == CommandType::SetOrder);
	CHECK(cmd.order == SummonOrder::Return);

	// 2. Order follow
	output.clear();
	ConsoleHandler::Execute("scf order follow", context, callback);
	CHECK(!output.empty());
	CHECK(output.back().find("Follow dispatched") != std::string::npos);
	CHECK(context.GetCommandQueue().PopNext(0.0f, cmd));
	CHECK(cmd.order == SummonOrder::Follow);

	// 3. Order guard
	output.clear();
	ConsoleHandler::Execute("scf order guard", context, callback);
	CHECK(!output.empty());
	CHECK(output.back().find("Guard position dispatched") != std::string::npos);
	CHECK(context.GetCommandQueue().PopNext(0.0f, cmd));
	CHECK(cmd.order == SummonOrder::WaitGuard);

	// 4. Order modes: passive, defensive, aggressive
	output.clear();
	ConsoleHandler::Execute("scf order passive", context, callback);
	CHECK(context.GetCommandQueue().PopNext(0.0f, cmd));
	CHECK(cmd.type == CommandType::SetAggression);
	CHECK(cmd.aggression == AggressionMode::Passive);

	output.clear();
	ConsoleHandler::Execute("scf order aggressive", context, callback);
	CHECK(context.GetCommandQueue().PopNext(0.0f, cmd));
	CHECK(cmd.aggression == AggressionMode::Aggressive);

	// 5. Order focus
	output.clear();
	ConsoleHandler::Execute("scf order focus 0x00012345", context, callback);
	CHECK(!output.empty());
	CHECK(output.back().find("0x00012345") != std::string::npos);
	CHECK(context.GetCommandQueue().PopNext(0.0f, cmd));
	CHECK(cmd.type == CommandType::SetFocusTarget);
	CHECK(cmd.focusTargetId == 0x00012345);
}

TEST(console_command_unknown)
{
	auto& context = FrameworkContext::GetSingleton();
	std::vector<std::string> output;

	ConsoleHandler::Execute("foobar", context, [&output](const std::string& line) {
		output.push_back(line);
	});

	CHECK(!output.empty());
	CHECK(output[0].find("Unknown command 'foobar'") != std::string::npos);
}

TEST(framework_context_reset)
{
	auto& context = FrameworkContext::GetSingleton();
	std::vector<std::string> logs;
	context.SetLogCallback([&logs](const char* msg) {
		logs.emplace_back(msg);
	});

	// Populate subsystems
	context.GetRegistry().RegisterSummon(0x14, 0x50, SummonType::SummonCreature, 30.0f, 0.0f);
	Command cmd;
	cmd.summonId = 0x50;
	context.GetCommandQueue().Enqueue(cmd);
	context.GetScheduler().RequestImmediateTick();

	CHECK(context.GetRegistry().GetAllRecords().size() == 1);
	CHECK(context.GetCommandQueue().GetPendingCount() == 1);

	// Reset
	context.ResetRuntimeState();

	CHECK(context.GetRegistry().GetAllRecords().empty());
	CHECK(context.GetCommandQueue().GetPendingCount() == 0);
	CHECK(!logs.empty());
}
