#include "PCH.h"
#include "Core/StateMachine.h"
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

	void LogFSMTransition(scf::core::EntityID a_id, scf::core::SummonState a_from, scf::core::SummonState a_to, const char* a_reason)
	{
		logger::info("[FSM] Entity {}: {} -> {} (Reason: {})",
			a_id,
			scf::core::ToString(a_from),
			scf::core::ToString(a_to),
			a_reason ? a_reason : "none");
	}

	scf::core::StateMachine g_stateMachine(LogFSMTransition);

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("Data loaded: SKSE message received, SCF FSM ready.");
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} v{} is loading...", plugin->GetName(), plugin->GetVersion());

	SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = 64 });

	if (!scf::plugin::RuntimeGate::ValidateRuntime()) {
		logger::error("RuntimeGate validation failed, disabling plugin hooks.");
		return false;
	}

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

	logger::info("{} has finished loading.", plugin->GetName());
	return true;
}
