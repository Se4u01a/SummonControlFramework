#pragma once

namespace scf::friendly_fire
{
	// Friendly-killmove refusal (bSuppressFriendlyKillmoves): a protected summon is never
	// the victim of a killmove / paired finishing animation performed by the player, a
	// player ally or one of their summons. Enemies' killmoves on our summons are untouched.
	//
	// WHERE THE SEAT IS, AND WHAT WAS VERIFIED ON THE UNPACKED 1.5.97 IMAGE (2026-11-11,
	// local SkyrimSE.exe + Address Library SE + capstone disassembly, not in game):
	//
	//   ID 38048 -> +63D0F0, "begin killmove". Three facts pin it:
	//     1. Body: `mov rax,[rdx+8]` / `cmp byte [rax+0x1A],0x3E` (FormType 0x3E = ACHR,
	//        so [rdx+8] is an actor), then `mov rax,[rdi+0x10]` with the same check, so
	//        [rdx+0x10] is the second actor. rdx is the killmove request record; the seat
	//        reads the victim from +0x10 exactly as the callee does.
	//     2. At +0x19C it calls `SetKillMove([rdx+8 actor], nullptr)` and at +0x1A7
	//        `SetKillMove([rdx+0x10 actor], [rdx+8 actor])`, i.e. the performer enters the
	//        killmove with no killer and the victim records the performer as its killer.
	//     3. It writes the Actor fields CommonLib pins: [actor+0xF0] is the AIProcess,
	//        [actor+0x100] is `myKiller`, and [actor+0x1FC] |= 0x4000 is
	//        BOOL_FLAGS::kIsInKillMove (CommonLib Actor.h, *0x1FC).
	//
	//   ID 37659 -> +6280D0 is that `Actor::SetKillMove(Actor* a_killer)`:
	//     `or dword [rbx+0x1FC],0x4000` at +0x49, `mov [rbx+0x100],eax` at +0x6E, and the
	//     killmove camera call gated on `this == PlayerCharacter::GetSingleton()`.
	//   ID 37661 -> +628240 is the matching getter: `mov eax,[rcx+0x1FC]; shr eax,0xE;
	//     and al,1; ret` (exactly Actor::IsInKillMove()).
	//   ID 37660 -> +628190 is the matching teardown (`and dword [+0x1FC],0xFFFFBFFF`).
	//
	// ID 38048 has exactly FOUR direct call sites on 1.5.97 (+63AFA6, +63B14F, +63CB9B,
	// +63CC35); the seat patches those calls, not the function entry, because this
	// CommonLibSSE-NG revision's write_branch on an entry returns the overwritten prologue
	// as "the original" (field crash 2026-11-10, see Engine/CombatGate.cpp). Refusing the
	// call drops the whole killmove setup: no killmove flag, no myKiller, no killcam, no
	// paired animation start. Every refusal is logged once per summon.
	//
	// Only installed on SE 1.5.97: on any other runtime the plugin logs and leaves vanilla.
	class KillmoveHook
	{
	public:
		static bool Install() noexcept;
	};
}
