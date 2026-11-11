#pragma once

#include "Core/Persistence.h"
#include "Core/Types.h"
#include "FriendlyFire/FriendlyFireSettings.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace scf::core
{
	struct GeneralSettings
	{
		bool enabled{ true };
		std::string logLevel{ "info" };
		std::size_t maxSummonsPerOwner{ 1 };       // SCF tracking floor; never below the engine roster
		std::uint32_t playerSummonLimit{ 0 };     // PoE2 M5: engine limit for the player, 0 = vanilla perks
	};

	struct CollisionSettings
	{
		bool enableCollisionBypass{ true };     // Player <-> own summon non-blocking
		bool enableSummonToSummonBypass{ true }; // Summon <-> summon (same owner) non-blocking
	};

	struct TimingSettings
	{
		float updateFrequencyHz{ 15.0f };
		bool enableTimeSlicing{ true };
		float commandTTL{ 5.0f };
	};

	struct MovementSettings
	{
		float followMinDistance{ 200.0f };
		float followMaxDistance{ 450.0f };

		// PoE2 stage M1: follow paces as SpeedMult values (100 = the actor's own pace).
		float followWalkSpeed{ 100.0f };
		float followRunSpeed{ 160.0f };
		float sprintSpeed{ 230.0f };
		float sprintDistance{ 1200.0f };  // lag beyond which a following summon sprints
	};

	struct CombatSettings
	{
		float maxTargetRadius{ 2000.0f };
		float followDistanceThreshold{ 500.0f };
		float returnDistanceThreshold{ 2500.0f };
		float engageDistanceThreshold{ 300.0f };
		float targetLostDebounceTime{ 1.5f };

		// Combat leash
		bool enableCombatLeash{ true };
		float combatLeashRadius{ 2000.0f };
		bool returnToOwnerWhenOutsideLeash{ true };

		// PoE2-style targeting (stage M2)
		bool assistPlayerAttacks{ true };  // summons focus whatever the player hits
		float aggressionRadius{ 1500.0f };  // engine-chosen fights only within this of the owner; 0 = off
	};

	struct RecoverySettings
	{
		bool enableCatchUpWarp{ true };
		float catchUpDistance{ 3500.0f };
		float stuckWarpSeconds{ 3.0f };        // M1: no progress this long -> recall behind the owner
		float stuckWarpMinDistance{ 300.0f };  // ...only while still lagging more than this
	};

	struct SquadSettings
	{
		std::size_t maxMeleeSlotsPerTarget{ 2 };
		std::size_t maxRangedSlotsPerTarget{ 3 };
		float formationSpreadRadius{ 160.0f };

		// PoE2 stage M5: formation rings for large squads (core::FormationLayout).
		std::size_t formationRingCapacity{ 6 };
		float formationInnerRingScale{ 0.62f };
		float formationMinSlotDistance{ 50.0f };
		float formationMaxExpansion{ 4.0f };
	};

	struct HotkeySettings
	{
		std::uint32_t focusTargetKey{ 0x4F };        // Numpad 1 (79)
		std::uint32_t commandReturnKey{ 0x50 };      // Numpad 2 (80)
		std::uint32_t toggleAggressionKey{ 0x51 };   // Numpad 3 (81)
		std::uint32_t moveToKey{ 0x4B };             // Numpad 4 (75)
		std::uint32_t waitGuardKey{ 0x4C };          // Numpad 5 (76)
		std::uint32_t followKey{ 0x4D };             // Numpad 6 (77)
		std::uint32_t commandActionKey{ 0x47 };      // Numpad 7 (71): PoE2 "Command" action by summon type
		std::uint32_t minionPanelToggleKey{ 0 };     // PoE2 M6 panel show/hide; 0 = unbound
	};

	// PoE2 stage M6: the minion HUD panel (integrations::MinionPanelConfig).
	struct MinionPanelSettings
	{
		bool enabled{ false };
		bool showHealthNumbers{ true };
		bool showTarget{ true };
		bool showCommand{ true };
		bool showDuration{ true };
		bool hideWhenEmpty{ false };
		float posX{ 40.0f };
		float posY{ 240.0f };
		float width{ 260.0f };
		std::size_t maxRows{ 32 };
	};

	struct SettingsData
	{
		GeneralSettings general{};
		CollisionSettings collision{};
		TimingSettings timing{};
		MovementSettings movement{};
		CombatSettings combat{};
		RecoverySettings recovery{};
		SquadSettings squad{};
		HotkeySettings hotkeys{};
		friendly_fire::FriendlyFireSettings friendlyFire{};
		PersistenceSettings persistence{};      // PoE2 M4 (blacklist ids resolved by the plugin)
		std::string persistenceBlacklist{};     // [Persistence] sBlacklistedSpells, "Plugin.esp|0xID, ..."
		MinionPanelSettings minionPanel{};
	};

	class Settings
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		Settings() = default;
		explicit Settings(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		[[nodiscard]] const SettingsData& Get() const noexcept
		{
			return _data;
		}

		[[nodiscard]] SettingsData& GetMutable() noexcept
		{
			return _data;
		}

		void ResetToDefaults() noexcept;

		bool LoadFromString(std::string_view a_iniContent);

		bool LoadFromFile(const std::string& a_filePath);

		bool SaveToFile(const std::string& a_filePath) const;

		[[nodiscard]] std::string Serialize() const;

		void ValidateAndSanitize() noexcept;

	private:
		SettingsData _data{};
		LogCallback _logCallback{ nullptr };
	};
}
