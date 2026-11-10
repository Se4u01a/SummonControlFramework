#pragma once

#include "Core/EntityID.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scf::core
{
	// PoE2 stage M3: a "Command" key makes each summon perform the action of its type
	// (PoE2 "Command" skills). Pure data and rules; the engine adapter resolves forms,
	// casts spells and changes speed (src/Engine/CommandActions.cpp).
	enum class CommandAction : std::uint8_t
	{
		None,
		Rush,          // focus the aimed enemy and sprint at it (temporary SpeedMult bonus)
		CastAtTarget,  // the summon casts its spell at the aimed enemy
		Detonate       // the summon casts its spell (at the enemy, else on itself) and is dispelled
	};

	const char* ToString(CommandAction a_action) noexcept;
	std::optional<CommandAction> ParseCommandAction(std::string_view a_text) noexcept;

	// A record named the way INI files name forms: "Plugin.esp|0x00ABCD" (local id).
	struct FormKey
	{
		std::string plugin;
		std::uint32_t localId{ 0 };

		[[nodiscard]] bool IsValid() const noexcept { return !plugin.empty() && localId != 0; }
		// Plugin names compare case-insensitively, as the game's file lookup does.
		bool operator==(const FormKey& a_other) const noexcept;
	};

	std::optional<FormKey> ParseFormKey(std::string_view a_text);

	struct CommandEntry
	{
		CommandAction action{ CommandAction::None };
		std::optional<FormKey> spell;  // required by CastAtTarget / Detonate
		float cooldownSeconds{ 8.0f };
	};

	struct CommandCatalog
	{
		std::vector<std::pair<FormKey, CommandEntry>> byBase;  // summon base NPC (or a template of it)
		std::optional<CommandEntry> fallback;                  // "Default" line: every other summon
		float rushSpeedBonus{ 60.0f };                         // SpeedMult points added during a Rush
		float rushSeconds{ 3.0f };
		std::vector<std::string> warnings;                     // rejected lines, for the log
	};

	// Format (SummonControlFramework_Commands.ini):
	//   [General]
	//   fRushSpeedBonus = 60        ; 0..300
	//   fRushSeconds = 3            ; 0.5..15
	//   [Commands]
	//   Skyrim.esm|0x000204C0 = CastAtTarget, Skyrim.esm|0x0001C789, 10
	//   Default = Rush, , 8
	// Value: action, spell (empty for Rush), cooldown seconds (1..600, default 8).
	// Lines that do not parse are skipped and reported in `warnings`.
	CommandCatalog ParseCommandCatalog(std::string_view a_text);

	// The file written when none exists: vanilla conjurations plus a Rush default.
	std::string DefaultCommandCatalogText();

	// Per-summon cooldowns on the scheduler clock.
	class CommandCooldowns
	{
	public:
		[[nodiscard]] bool IsReady(EntityID a_summon, float a_now) const;
		[[nodiscard]] float Remaining(EntityID a_summon, float a_now) const;
		void Start(EntityID a_summon, float a_now, float a_cooldownSeconds);
		void Forget(EntityID a_summon);
		void Clear();

	private:
		std::unordered_map<EntityID, float> _readyAt;
	};
}
