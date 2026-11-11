#pragma once

#include "Core/CommandActions.h"

#include <cstdint>

namespace RE
{
	class Actor;
}

namespace scf::core
{
	struct SummonModel;
}

namespace scf::engine::actions
{
	// PoE2 stage M3 - "Command" actions by summon type (engine side).
	//
	// Catalog: Data/SKSE/Plugins/SummonControlFramework_Commands.ini (written with
	// core::DefaultCommandCatalogText() when missing, never overwritten). Forms are
	// resolved once at kDataLoaded through TESDataHandler::LookupForm; a summon matches
	// through ExtraLeveledCreature's original/template base, its own base and that base's
	// TPLT chain (baseTemplateForm), else the Default line.
	//
	// Actions (main thread, from UpdateHook's command drain):
	//   * Rush         - temporary-modifier SpeedMult bonus for fRushSeconds (speed is
	//                    refreshed by a +/-0.01 CarryWeight nudge), and the caller runs the
	//                    usual Focus path at the target. Refused in Passive.
	//   * CastAtTarget - MagicCaster(kInstant)::CastSpellImmediate(spell, target), blame =
	//                    the summon (Papyrus Spell.Cast does the same; aimed spells fly at
	//                    the target).
	//   * Detonate     - the same cast (at the target, or on itself for a Self spell) and
	//                    then KillImmediate, like the Dispel command.
	// Cooldown per summon (core::CommandCooldowns) on the scheduler clock.

	void Load();

	enum class Outcome
	{
		Skipped,
		Rush,
		Cast,
		Detonated
	};

	// a_target may be null (only a Self-delivery Detonate works without one).
	Outcome Execute(RE::Actor* a_summon, const core::SummonModel* a_model, RE::Actor* a_target, float a_now);

	// After one Command key press has been executed for every summon: HUD summary.
	void ReportBatch();

	// Minion panel (M6): whether the summon has a command action, and the seconds left
	// on its cooldown (0 = ready). Main thread.
	[[nodiscard]] bool HasCommand(RE::Actor* a_summon);
	[[nodiscard]] float CooldownRemaining(std::uint32_t a_summonId, float a_now);

	// Every tactical tick: end expired Rush speed bonuses.
	void Tick(float a_now);

	// Save load / new game: end every Rush bonus still applied, clear cooldowns.
	void Reset();
}
