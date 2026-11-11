#pragma once

#include <cstdint>

namespace RE
{
	class Actor;
}

namespace scf::friendly_fire
{
	// Friendly hit-reaction suppression (bSuppressFriendlyHitReactions, default off).
	//
	// WHERE THE SEAT IS, AND WHAT WAS VERIFIED ON THE UNPACKED 1.5.97 IMAGE (2026-11-11,
	// local SkyrimSE.exe + Address Library SE + capstone disassembly, not in game):
	//
	//   ID 36700 -> +5FA1B0 is the engine's stagger application on one actor:
	//     `mov rax,rsp; push rbp,rsi,rdi; lea rbp,[rax-0x5F]; sub rsp,0xC0` (entry bytes
	//     checked at install), `mov rdi,r8` (attacker), `movaps xmm6,xmm1` (float
	//     magnitude), `mov rbx,rcx` (the victim), then `call [rax+0x380]` on the victim and
	//     `cmp qword [rbx+0xF0],0` (AIProcess present). At +0x1EF it executes
	//     `or dword [rbx+0xC4],0x2000`, and CommonLib pins that as ActorState2::staggered
	//     (Actor is at +0xB8 in SE, actorState2 at +0x0C inside it -> +0xC4, bit 13).
	//     It is the ONLY writer of that bit in .text: the other nine `[reg+0xC4]` sites
	//     with a 0x2000 mask clear it. Immediately after setting the bit it notifies the
	//     AIProcess (`[actor+0xF0]`).
	//
	//   ID 36700 has six direct sites on 1.5.97: five `E8` calls (+5634CF, +5A377B,
	//   +5A381B, +626BF8, +67D8C7) and one `E9` tail call (+5A3E1A), so the seat patches
	//   the sites, not the entry (write_branch on an entry returns the prologue, see
	//   Engine/CombatGate.cpp), and keeps the tail call a jump.
	//
	// THE ARM WINDOW. The attacker is what decides "friendly", and the engine usually
	// calls this with a null attacker (`xor r8d,r8d` at every observed site), so the
	// attack source cannot be read here. Instead the melee seat (HitHook, verified on the
	// ProcessHit call site) arms this hook around the synchronous engine call that applies
	// the hit: ArmFor(victim) -> forward the hit -> Disarm(). A stagger that happens
	// inside that window is the reaction to the friendly hit and is refused; anything
	// outside it (an enemy hit, an explosion knockback, a later attack) is untouched.
	// Without an armed window this hook changes nothing at all (fail-open).
	//
	// Only installed on SE 1.5.97: on any other runtime the plugin logs and leaves vanilla.
	class HitReactionHook
	{
	public:
		static bool Install() noexcept;

		// Arm the next stagger of a_victim on THIS thread. Paired with Disarm() around the
		// engine call that applies one hit; never left armed across frames.
		static void ArmFor(std::uint32_t a_victimFormId) noexcept;
		static void Disarm() noexcept;
	};
}
