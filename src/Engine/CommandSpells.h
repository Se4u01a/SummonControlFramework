#pragma once

#include <cstddef>
#include <cstdint>

namespace scf::engine::command_spells
{
	// Main thread (kDataLoaded): resolve the 3 command spells + AttackTarget effect
	// from SummonControlFramework.esp and register the TESSpellCastEvent /
	// TESMagicEffectApplyEvent sinks.
	void Load();

	// True once all 3 SPEL records and the aimed MGEF record have been resolved.
	[[nodiscard]] bool IsLoaded() noexcept;

	// True if `a_formId` matches any of SCF's own command spells or command magic effects.
	// Used by EventAdapter's HitEventSink so casting SCF_CmdAttackTarget never double-fires
	// the passive Assist path alongside MagicEffectApplySink.
	[[nodiscard]] bool IsCommandSpell(std::uint32_t a_formId) noexcept;

	// Main thread (kPostLoadGame / kNewGame): grant the 3 command spells if
	// [Spells] bAutoGrantCommandSpells is enabled.
	void SyncPlayerSpells();

	// Main thread: idempotently add / remove the 3 command spells on the player.
	// Returns the number of spells added / removed.
	std::size_t GrantSpellsToPlayer();
	std::size_t RemoveSpellsFromPlayer();

	// Thread-safe wrappers for UI threads (e.g. FLICK ImGui overlay): schedule
	// grant / remove onto Skyrim's main game thread via SKSE TaskInterface.
	void QueueGrantSpellsToPlayer();
	void QueueRemoveSpellsFromPlayer();
}
