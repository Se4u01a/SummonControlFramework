#include "Plugin/Messaging.h"
#include "Plugin/ConsoleCommands.h"
#include "Core/FrameworkContext.h"
#include "Engine/CombatGate.h"
#include "Engine/EventAdapter.h"
#include "Engine/InputHandler.h"
#include "Engine/UpdateHook.h"
#include "FriendlyFire/HitHook.h"
#include "FriendlyFire/ProjectileHook.h"
#include "Integrations/FlickMenu.h"
#include "Integrations/PrecisionBackend.h"
#include "PCH.h"

#include <RE/G/GameSettingCollection.h>

#include <filesystem>

namespace scf::plugin
{
	void PatchGameSettings()
	{
		auto* gmstCollection = RE::GameSettingCollection::GetSingleton();
		if (!gmstCollection) {
			logger::warn("[Messaging] GameSettingCollection singleton not available!");
			return;
		}

		const auto& s = core::FrameworkContext::GetSingleton().GetSettings().Get();

		// 1. Follow distance max snap leash
		if (auto* maxDist = gmstCollection->GetSetting("fSummonedCreatureMaxFollowDist")) {
			const float oldVal = maxDist->data.f;
			const float newVal = s.recovery.enableCatchUpWarp ? s.recovery.catchUpDistance : 1000000.0f;
			maxDist->data.f = newVal;
			logger::info("[Messaging] Patched GMST fSummonedCreatureMaxFollowDist: {:.1f} -> {:.1f}", oldVal, newVal);
		} else {
			logger::warn("[Messaging] Setting fSummonedCreatureMaxFollowDist not found in GameSettingCollection!");
		}

		// 2. Follow distance min
		if (auto* minDist = gmstCollection->GetSetting("fSummonedCreatureMinFollowDist")) {
			const float oldVal = minDist->data.f;
			const float newVal = s.movement.followMinDistance;
			minDist->data.f = newVal;
			logger::info("[Messaging] Patched GMST fSummonedCreatureMinFollowDist: {:.1f} -> {:.1f}", oldVal, newVal);
		} else {
			logger::warn("[Messaging] Setting fSummonedCreatureMinFollowDist not found in GameSettingCollection!");
		}
	}

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

			PatchGameSettings();

			ConsoleCommands::Register();
			engine::EventAdapter::RegisterEventSinks();
			engine::InputHandler::Register();
			engine::combat::Install();
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
			PatchGameSettings();
			break;

		case SKSE::MessagingInterface::kNewGame:
			logger::info("[Messaging] SKSE kNewGame: Initializing clean runtime state for new game.");
			engine::update::Reset();
			PatchGameSettings();
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
