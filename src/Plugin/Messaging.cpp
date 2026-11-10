#include "Plugin/Messaging.h"
#include "Plugin/ConsoleCommands.h"
#include "Core/FrameworkContext.h"
#include "Engine/EventAdapter.h"
#include "Engine/InputHandler.h"
#include "Engine/UpdateHook.h"
#include "FriendlyFire/HitHook.h"
#include "FriendlyFire/ProjectileHook.h"
#include "Integrations/FlickMenu.h"
#include "Integrations/PrecisionBackend.h"
#include "PCH.h"

#include <filesystem>

namespace scf::plugin
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("[Messaging] SKSE kDataLoaded received: Initializing framework components.");
			{
				const std::string iniPath = "Data/SKSE/Plugins/SummonControlFramework.ini";
				auto& settings = core::FrameworkContext::GetSingleton().GetSettings();
				if (!std::filesystem::exists(iniPath)) {
					logger::info("[Messaging] INI not found at {}, creating default configuration.", iniPath);
					settings.SaveToFile(iniPath);
				} else {
					settings.LoadFromFile(iniPath);
					logger::info("[Messaging] Configuration successfully loaded from {}.", iniPath);
				}
			}

			ConsoleCommands::Register();
			engine::EventAdapter::RegisterEventSinks();
			engine::InputHandler::Register();
			engine::update::Install();
			friendly_fire::HitHook::Install();
			friendly_fire::ProjectileHook::Install();
			integrations::PrecisionBackend::Initialize();
			integrations::FlickMenu::Install();
			logger::info("[Messaging] Framework initialization complete.");
			break;

		case SKSE::MessagingInterface::kPreLoadGame:
			logger::info("[Messaging] SKSE kPreLoadGame: Resetting ephemeral runtime state for save load.");
			engine::update::Reset();
			break;

		case SKSE::MessagingInterface::kPostLoadGame:
			logger::info("[Messaging] SKSE kPostLoadGame: Save loaded successfully.");
			break;

		case SKSE::MessagingInterface::kNewGame:
			logger::info("[Messaging] SKSE kNewGame: Initializing clean runtime state for new game.");
			engine::update::Reset();
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
