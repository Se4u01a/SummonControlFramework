#include "Plugin/Messaging.h"
#include "Plugin/ConsoleCommands.h"
#include "Core/FrameworkContext.h"
#include "PCH.h"

namespace scf::plugin
{
	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			logger::info("[Messaging] SKSE kPostLoad received.");
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("[Messaging] SKSE kDataLoaded received: Initializing framework...");
			{
				auto& settings = core::FrameworkContext::GetSingleton().GetSettings();
				const std::string iniPath = "Data/SKSE/Plugins/SummonControlFramework.ini";
				if (!settings.LoadFromFile(iniPath)) {
					logger::info("[Messaging] Configuration file not found at {}, saving default configuration.", iniPath);
					settings.SaveToFile(iniPath);
				} else {
					logger::info("[Messaging] Configuration successfully loaded from {}.", iniPath);
				}
			}

			ConsoleCommands::Register();
			logger::info("[Messaging] Framework initialization complete.");
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
			logger::info("[Messaging] SKSE kPreLoadGame: Resetting ephemeral runtime state for save load.");
			core::FrameworkContext::GetSingleton().ResetRuntimeState();
			break;

		case SKSE::MessagingInterface::kPostLoadGame:
			logger::info("[Messaging] SKSE kPostLoadGame: Save loaded successfully.");
			break;

		case SKSE::MessagingInterface::kNewGame:
			logger::info("[Messaging] SKSE kNewGame: Initializing clean runtime state for new game.");
			core::FrameworkContext::GetSingleton().ResetRuntimeState();
			break;

		default:
			break;
		}
	}

	void RegisterMessaging()
	{
		auto* messaging = SKSE::GetMessagingInterface();
		if (messaging) {
			messaging->RegisterListener(OnMessage);
			logger::info("[Messaging] Registered SKSE messaging listener.");
		} else {
			logger::error("[Messaging] Failed to acquire SKSE messaging interface!");
		}
	}
}
