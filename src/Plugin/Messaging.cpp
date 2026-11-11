#include "Plugin/Messaging.h"
#include "Plugin/ConsoleCommands.h"
#include "Core/FrameworkContext.h"
#include "Engine/CombatGate.h"
#include "Engine/CommandActions.h"
#include "Engine/KeywordApi.h"
#include "Engine/FollowLeash.h"
#include "Engine/PassThrough.h"
#include "Engine/EventAdapter.h"
#include "Engine/InputHandler.h"
#include "Engine/UpdateHook.h"
#include "FriendlyFire/HitHook.h"
#include "FriendlyFire/MagicHook.h"
#include "FriendlyFire/ProjectileHook.h"
#include "Integrations/FlickMenu.h"
#include "Integrations/MinionPanel.h"
#include "Integrations/PrecisionBackend.h"
#include "Core/CommandActions.h"
#include "PCH.h"

#include <RE/G/GameSettingCollection.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESDataHandler.h>

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

	integrations::MinionPanelConfig ToPanelConfig(const core::MinionPanelSettings& a_settings)
	{
		integrations::MinionPanelConfig config{};
		config.enabled = a_settings.enabled;
		config.showHealthNumbers = a_settings.showHealthNumbers;
		config.showTarget = a_settings.showTarget;
		config.showCommand = a_settings.showCommand;
		config.showDuration = a_settings.showDuration;
		config.hideWhenEmpty = a_settings.hideWhenEmpty;
		config.posX = a_settings.posX;
		config.posY = a_settings.posY;
		config.width = a_settings.width;
		config.maxRows = a_settings.maxRows;
		return config;
	}

	// Settings that need the data handler: [Persistence] sBlacklistedSpells
	// ("Plugin.esp|0xID, ...") -> runtime FormIDs of SPEL records.
	void ApplyLoadedSettings()
	{
		auto& data = core::FrameworkContext::GetSingleton().GetSettings().GetMutable();
		auto& ids = data.persistence.blacklistedSpellIds;
		ids.clear();
		auto* handler = RE::TESDataHandler::GetSingleton();
		std::string_view list = data.persistenceBlacklist;
		while (!list.empty()) {
			const auto comma = list.find(',');
			const auto item = list.substr(0, comma);
			list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
			const auto key = core::ParseFormKey(item);
			if (!key) {
				if (item.find_first_not_of(" \t") != std::string_view::npos) {
					logger::warn("[Messaging] [Persistence] sBlacklistedSpells: '{}' is not Plugin.esp|0xID; ignored.", item);
				}
				continue;
			}
			auto* spell = handler ? handler->LookupForm<RE::SpellItem>(key->localId, key->plugin) : nullptr;
			if (!spell) {
				logger::warn("[Messaging] [Persistence] sBlacklistedSpells: {}|0x{:06X} is not a loaded spell; ignored.", key->plugin, key->localId);
				continue;
			}
			ids.push_back(spell->GetFormID());
		}
		logger::info("[Messaging] [Persistence] persistent summons {}, {} blacklisted spell(s).",
			data.persistence.persistentSummons ? "ON" : "off", ids.size());
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
			ApplyLoadedSettings();

			ConsoleCommands::Register();
			engine::EventAdapter::RegisterEventSinks();
			engine::InputHandler::Register();
			engine::actions::Load();
			engine::keywords::Load();
			engine::combat::Install();
			engine::leash::Install();
			engine::passthrough::Install();
			engine::update::Install();
			friendly_fire::HitHook::Install();
			friendly_fire::MagicHook::Install();
			friendly_fire::ProjectileHook::Install();
			integrations::PrecisionBackend::Initialize();
			integrations::FlickMenu::Install();
			integrations::MinionPanel::Install();
			integrations::MinionPanel::Configure(ToPanelConfig(core::FrameworkContext::GetSingleton().GetSettings().Get().minionPanel));
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
