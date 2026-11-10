#include "Core/ConsoleHandler.h"
#include "Core/FrameworkContext.h"
#include "check.h"

#include <filesystem>
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

	ConsoleHandler::Execute("help", context, callback);
	CHECK(!output.empty());
	CHECK(output[0].find("Summon Control Framework") != std::string::npos);

	output.clear();
	ConsoleHandler::Execute("", context, callback);
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

	// 2. Register summons
	auto& reg = context.GetRegistry();
	reg.RegisterSummon(0x14, 0x10001, SummonType::SummonCreature, 60.0f, 0.0f);
	reg.RegisterSummon(0x14, 0x10002, SummonType::SummonCreature, 120.0f, 0.0f);

	output.clear();
	ConsoleHandler::Execute("scf registry", context, callback);
	CHECK(output.size() >= 3);
	CHECK(output[0].find("2 registered summon(s)") != std::string::npos);

	bool foundSummon1 = false;
	bool foundSummon2 = false;
	for (const auto& line : output) {
		if (line.find("00010001") != std::string::npos) {
			foundSummon1 = true;
		}
		if (line.find("00010002") != std::string::npos) {
			foundSummon2 = true;
		}
	}
	CHECK(foundSummon1);
	CHECK(foundSummon2);

	// Clean up
	context.ResetRuntimeState();
}

TEST(console_command_reload)
{
	auto& context = FrameworkContext::GetSingleton();
	const std::string testIni = "scf_console_test.ini";

	Settings writer;
	writer.GetMutable().general.maxSummonsPerOwner = 4;
	writer.SaveToFile(testIni);

	std::vector<std::string> output;
	auto callback = [&output](const std::string& line) {
		output.push_back(line);
	};

	ConsoleHandler::Execute("reload", context, callback, testIni);
	CHECK(!output.empty());
	CHECK(output[0].find("reloaded successfully") != std::string::npos);
	CHECK(context.GetSettings().Get().general.maxSummonsPerOwner == 4);

	// Non-existent file
	output.clear();
	ConsoleHandler::Execute("scf reload", context, callback, "non_existent_file.ini");
	CHECK(!output.empty());
	CHECK(output[0].find("Failed to reload") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove(testIni, ec);
}

TEST(console_command_unknown)
{
	auto& context = FrameworkContext::GetSingleton();
	std::vector<std::string> output;

	ConsoleHandler::Execute("scf foobar", context, [&output](const std::string& line) {
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
