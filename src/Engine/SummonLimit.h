#pragma once

#include <cstdint>

namespace scf::engine::summonlimit
{
	// PoE2 stage M5: raise the number of summons the PLAYER may command at once.
	//
	// Seat (SE 1.5.97 only, bytes verified at install): the call at id 38993 + 0x9C
	// (+683E0C `mov ecx, 0x44` / +683E11 `call +32ECE0`) - BGSEntryPoint::HandleEntryPoint
	// (id 23073) for entry point 68 kModCommandedActorLimit, inside the function that adds
	// a commanded actor (id 38993, +683D70). Arguments: ecx = 68, rdx = the caster (perk
	// owner), r8 = the spell, r9 = float* holding iMaxSummonedCreatures (id 506252) that the
	// perks modify. The thunk calls the original and, when the caster is the player, raises
	// the float with core::ApplyPlayerSummonLimit. The engine then rounds it and dispels the
	// oldest summons beyond it exactly as in vanilla. NPC casters are untouched.
	bool Install();

	// Main thread: [General] uPlayerSummonLimit (0 = vanilla perks).
	void Publish(std::uint32_t a_playerLimit) noexcept;
}
