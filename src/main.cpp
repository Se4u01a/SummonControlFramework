#include "PCH.h"
#include "Core/FrameworkContext.h"
#include "Plugin/Messaging.h"
#include "Engine/Persistence.h"
#include "Plugin/RuntimeGate.h"

namespace
{
	void SetupLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "SummonControlFramework.log"sv;

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} v{} is loading...", plugin->GetName(), plugin->GetVersion());

	// The trampoline pool backs every 5-byte site patch (14-byte stub per unique target:
	// the HitHook thunk and the CombatGate StartCombat thunk), so keep real headroom
	// instead of the old 64 bytes.
	SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = 1024 });

	if (!scf::plugin::RuntimeGate::ValidateRuntime()) {
		logger::error("RuntimeGate validation failed, disabling plugin hooks.");
		return false;
	}

	// Wire unified logging from Core/AI subsystems to spdlog
	scf::core::FrameworkContext::GetSingleton().SetLogCallback([](const char* a_message) {
		logger::info("{}", a_message ? a_message : "");
	});

	scf::plugin::RegisterMessaging();

	// PoE2 M4: persistent summon slots and the Magicka reserve live in the SKSE co-save.
	scf::engine::persistence::Install();

	logger::info("{} has finished loading.", plugin->GetName());
	return true;
}
