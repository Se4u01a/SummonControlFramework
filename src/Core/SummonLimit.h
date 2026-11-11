#pragma once

#include <cstddef>
#include <cstdint>

namespace scf::core
{
	// PoE2 stage M5 - how many summons the player may command at once.
	//
	// Engine (SE 1.5.97, verified 2026-11-11): id 38993 (+683D70) adds a commanded actor to
	// the caster's MiddleHighProcessData::commandedActors. It starts from the INI setting
	// iMaxSummonedCreatures (id 506252, default 1), lets the caster's perks modify it through
	// entry point 68 kModCommandedActorLimit (the only BGSEntryPoint::HandleEntryPoint call
	// with ecx = 0x44, at +683E11), rounds it, and dispels the oldest commanded actors until
	// the new one fits. SCF hooks that one call and, for the player only, raises the result.

	// The player's limit after SCF: the perk-computed value, raised to `a_configured` when
	// that is set (0 = vanilla). Never lowers what the perks give; a non-finite perk value
	// is treated as 1.
	float ApplyPlayerSummonLimit(float a_perkValue, std::uint32_t a_configured) noexcept;

	// How many summons SCF tracks for an owner: the configured cap, but never fewer than
	// the summons the engine actually keeps for that owner (its roster is the authority;
	// SCF must not "displace" a summon the engine still commands).
	std::size_t TrackingCapacity(std::size_t a_configured, std::size_t a_engineRosterSize) noexcept;
}
