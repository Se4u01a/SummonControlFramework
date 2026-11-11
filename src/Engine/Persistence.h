#pragma once

#include "Core/Persistence.h"

#include <vector>

namespace RE
{
	class Actor;
	class PlayerCharacter;
}

namespace SKSE
{
	class SerializationInterface;
}

namespace scf::core
{
	class SummonRegistry;
}

namespace scf::engine::persistence
{
	// PoE2 stage M4 adapter: persistent summons, their Magicka reserve and auto-revive.
	//
	// The rules live in src/Core/Persistence; this file is only the engine seam:
	//   * extends the conjuration ActiveEffect of a tracked summon while the mode is on;
	//   * re-casts the summon spell from the player after a death, with no cost and
	//     silently, then binds the new actor to the same slot (order and aggression are
	//     written back into the new actor's SummonModel);
	//   * applies the aggregate max-Magicka reserve as a permanent actor-value modifier;
	//   * persists slots and the applied reserve in the SKSE co-save.
	//
	// Scope guard: by default any magic effect with archetype kSummonCreature counts,
	// provided the effect's caster is the player and the spell is not blacklisted. That
	// includes a summon spell from a mod plugin (the usual "Wolf Spirit") and a vanilla
	// spell whose record is overridden by USSEP or a patch. `bPersistVanillaSpellsOnly`
	// narrows the set to spells whose ORIGINAL source file (TESForm::GetFile(0), not the
	// winning override) is Skyrim/Update/Dawnguard/HearthFires/Dragonborn. The decision
	// itself is the pure core::ShouldTrackSummonSpell. A staff/scroll cast with another
	// caster, a reanimation and anything blacklisted stay normal summons, with one debug
	// log line. A mod that re-implements summons with scripts can be excluded through the
	// blacklist; nothing is written into an untracked actor.

	// SKSEPlugin_Load (after SKSE::Init): registers the co-save callbacks. Safe to call
	// twice. If the serialization interface is unavailable, summons still work in-session
	// and a warning is logged.
	void Install();

	// UpdateHook, once per scheduler tick, AFTER the registry sync: the live summon actors
	// of `a_player` (the same list UpdateHook builds) plus the registry whose models are
	// used to save/restore the order and aggression.
	void Tick(
		RE::PlayerCharacter* a_player,
		float a_now,
		const std::vector<RE::Actor*>& a_liveSummons,
		core::SummonRegistry& a_registry,
		const core::PersistenceSettings& a_settings);

	// UpdateHook: a commanded actor is dead this tick. Idempotent, so calling it on every
	// tick while the corpse stays in commandedActors is fine. Also feeds the recent-death
	// rule for untracked summons.
	void OnSummonDied(core::EntityID a_summonId, float a_now);

	// UpdateHook: a tracked summon left commandedActors (dispel, effect removed,
	// displacement). Drops a living slot; a dead slot keeps waiting for its revive.
	void OnSummonRemoved(core::EntityID a_summonId);

	// True when the last applied settings had the persistent mode on.
	[[nodiscard]] bool IsEnabled() noexcept;

	// kPreLoadGame / kNewGame (from UpdateHook::Reset). Drops in-memory slots, timers and
	// pending restore. Does not touch actor values: the loaded actor carries its own
	// permanent modifier, and the co-save restores the amount that was applied.
	void Reset();

	// Co-save callbacks. Install() registers them; they are public so the caller can
	// register its own trampoline instead.
	bool Save(SKSE::SerializationInterface* a_intfc);
	bool Load(SKSE::SerializationInterface* a_intfc);
	void Revert(SKSE::SerializationInterface* a_intfc);
}
