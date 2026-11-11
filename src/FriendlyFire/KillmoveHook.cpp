#include "FriendlyFire/KillmoveHook.h"
#include "FriendlyFire/ProtectionQuery.h"
#include "FriendlyFire/RelationshipClassifier.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/T/TESForm.h>

#include <cstring>
#include <vector>

namespace scf::friendly_fire
{
	namespace
	{
		// ---- verified 1.5.97 facts (see the header for the full write-up) ----
		constexpr std::uint32_t kKillmoveStartId = 38048;  // +63D0F0: begin killmove
		constexpr std::uint32_t kSetKillMoveId = 37659;    // +6280D0: Actor::SetKillMove
		constexpr std::uint32_t kIsInKillMoveId = 37661;   // +628240: Actor::IsInKillMove

		// The two `call SetKillMove` sites inside the killmove-start function, and where in
		// its request record the performer / victim actor pointers sit.
		constexpr std::uintptr_t kSetKillMoveCallOffsets[] = { 0x19C, 0x1A7 };
		constexpr std::size_t kAttackerOffset = 0x08;
		constexpr std::size_t kVictimOffset = 0x10;

		// Entry of +63D0F0: `mov r11,rsp; push rbp,rsi,rdi,r12,r13,r14,r15; sub rsp,0x50`.
		constexpr std::uint8_t kExpectedEntry[] = {
			0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
			0x48, 0x83, 0xEC, 0x50
		};

		// `or dword [rbx+0x1FC], 0x4000` at +0x49 of Actor::SetKillMove: the write that
		// makes the actor "in a killmove" (BOOL_FLAGS::kIsInKillMove), which is what proves
		// the identity of the function we refuse to call into.
		constexpr std::uintptr_t kEnterKillMoveOffset = 0x49;
		constexpr std::uint8_t kExpectedEnterKillMove[] = { 0x81, 0x8B, 0xFC, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00 };

		// `mov eax,[rcx+0x1FC]; shr eax,0xE; and al,1; ret` = Actor::IsInKillMove().
		constexpr std::uint8_t kExpectedIsInKillMove[] = { 0x8B, 0x81, 0xFC, 0x01, 0x00, 0x00, 0xC1, 0xE8, 0x0E, 0x24, 0x01, 0xC3 };

		using StartKillmove_t = void (*)(void* a_this, void* a_request, std::uint8_t a_flag);

		StartKillmove_t s_original{ nullptr };
		bool s_installed{ false };

		[[nodiscard]] bool BytesAre(std::uintptr_t a_address, const std::uint8_t* a_expected, std::size_t a_size) noexcept
		{
			return std::memcmp(reinterpret_cast<const void*>(a_address), a_expected, a_size) == 0;
		}

		// The actor pointer the engine itself reads at [request+offset] and accepts as an
		// actor (form type 0x3E = ACHR); nullptr for anything else, so a request record of
		// an unexpected shape can never be treated as a protected victim.
		[[nodiscard]] RE::Actor* ActorAt(void* a_request, std::size_t a_offset) noexcept
		{
			auto* form = *reinterpret_cast<RE::TESForm**>(static_cast<char*>(a_request) + a_offset);
			if (!form || !form->Is(RE::FormType::ActorCharacter)) {
				return nullptr;
			}
			return static_cast<RE::Actor*>(form);
		}

		[[nodiscard]] bool ShouldRefuse(void* a_request) noexcept
		{
			try {
				if (!a_request) {
					return false;
				}
				const auto& settings = core::FrameworkContext::GetSingleton().GetSettings().Get().friendlyFire;
				if (!settings.enabled || !settings.suppressFriendlyKillmoves) {
					return false;
				}

				auto* victim = ActorAt(a_request, kVictimOffset);
				if (!victim || !query::IsProtectedActor(victim, settings)) {
					return false;
				}

				// The aggressor's killmove is never refused: only what the friendly-fire
				// matrix already filters (player / ally / their summons).
				auto* attacker = ActorAt(a_request, kAttackerOffset);
				const auto sourceKind = query::ClassifySourceActor(
					*reinterpret_cast<RE::TESForm**>(static_cast<char*>(a_request) + kAttackerOffset));
				if (!RelationshipClassifier::ShouldSuppressKillmove(sourceKind, true, settings)) {
					return false;
				}

				logger::info("[KillmoveHook] Refused friendly killmove on protected summon 0x{:08X} ({}) by 0x{:08X} ({})",
					victim->GetFormID(), victim->GetName(),
					attacker ? attacker->GetFormID() : 0u, ToString(sourceKind));
				return true;
			} catch (...) {
				return false;  // never throw into engine code: the killmove proceeds
			}
		}

		void StartKillmoveThunk(void* a_this, void* a_request, std::uint8_t a_flag)
		{
			if (ShouldRefuse(a_request)) {
				return;
			}
			s_original(a_this, a_request, a_flag);
		}
	}

	bool KillmoveHook::Install() noexcept
	{
		if (s_installed) {
			return true;
		}
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_5_97) {
			logger::info("[KillmoveHook] seat verified on SE 1.5.97 only; friendly killmove suppression not installed on this runtime.");
			return false;
		}

		REL::Relocation<std::uintptr_t> begin{ REL::ID(kKillmoveStartId) };
		if (!begin.address()) {
			logger::error("[KillmoveHook] ID {} (begin killmove) did not resolve; killmove suppression disabled.", kKillmoveStartId);
			return false;
		}

		// 1. The ID must still name the function the seat was verified against.
		if (!BytesAre(begin.address(), kExpectedEntry, sizeof(kExpectedEntry))) {
			logger::error("[KillmoveHook] 0x{:X} does not start with the verified prologue; killmove suppression disabled.",
				begin.address());
			return false;
		}

		// 2. It must still be the killmove start: two calls to Actor::SetKillMove inside.
		const auto setKillMove = REL::ID(kSetKillMoveId).address();
		if (!setKillMove || !BytesAre(setKillMove + kEnterKillMoveOffset, kExpectedEnterKillMove, sizeof(kExpectedEnterKillMove))) {
			logger::error("[KillmoveHook] ID {} (Actor::SetKillMove) has no verified `or [this+0x1FC],0x4000`; killmove suppression disabled.",
				kSetKillMoveId);
			return false;
		}
		for (const auto offset : kSetKillMoveCallOffsets) {
			const auto site = begin.address() + offset;
			std::int32_t displacement = 0;
			std::memcpy(&displacement, reinterpret_cast<const void*>(site + 1), sizeof(displacement));
			const auto target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(site + 5) + displacement);
			const auto opcode = *reinterpret_cast<const std::uint8_t*>(site);
			if (opcode != 0xE8 || target != setKillMove) {
				logger::error("[KillmoveHook] 0x{:X} is not `call Actor::SetKillMove` (opcode 0x{:02X}, target 0x{:X}); killmove suppression disabled.",
					site, opcode, target);
				return false;
			}
		}

		const auto isInKillMove = REL::ID(kIsInKillMoveId).address();
		if (!isInKillMove || !BytesAre(isInKillMove, kExpectedIsInKillMove, sizeof(kExpectedIsInKillMove))) {
			logger::warn("[KillmoveHook] ID {} (Actor::IsInKillMove) does not match the verified getter; continuing on the start/end pair only.",
				kIsInKillMoveId);
		}

		// 3. Patch the direct call sites: the entry stays untouched, so forwarding to the
		// real function cannot recurse (same seat shape as Engine/CombatGate.cpp).
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		if (!text.address() || text.size() < 8) {
			logger::error("[KillmoveHook] .text segment is not available; killmove suppression disabled.");
			return false;
		}
		const auto* bytes = text.pointer<std::uint8_t>();
		struct Site
		{
			std::size_t offset;
			bool        tailCall;
		};
		std::vector<Site> sites;
		for (std::size_t i = 0; i + 5 <= text.size(); ++i) {
			const auto opcode = bytes[i];
			if (opcode != 0xE8 && opcode != 0xE9) {
				continue;
			}
			std::int32_t displacement = 0;
			std::memcpy(&displacement, bytes + i + 1, sizeof(displacement));
			const auto target = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(text.address() + i + 5) + displacement);
			if (target == begin.address()) {
				sites.push_back({ i, opcode == 0xE9 });
			}
		}
		// The scan is byte-level, so a stray rel32 match would patch unrelated code: accept
		// only the exact set verified on 1.5.97 (4 sites: +63AFA6, +63B14F, +63CB9B, +63CC35).
		constexpr std::size_t kVerifiedSiteCount = 4;
		if (sites.size() != kVerifiedSiteCount) {
			logger::error("[KillmoveHook] found {} direct call site(s) of 0x{:X}, verified {}; killmove suppression disabled.",
				sites.size(), begin.address(), kVerifiedSiteCount);
			return false;
		}

		s_original = reinterpret_cast<StartKillmove_t>(begin.address());
		std::size_t patched = 0;
		for (const auto& site : sites) {
			REL::Relocation<std::uintptr_t> callSite{ text.address() + site.offset };
			const auto previous = site.tailCall ? callSite.write_branch<5>(&StartKillmoveThunk) :
			                                      callSite.write_call<5>(&StartKillmoveThunk);
			if (previous != begin.address()) {
				logger::warn("[KillmoveHook] site 0x{:X} changed between scan and patch (previous 0x{:X}); chained.",
					text.address() + site.offset, previous);
			}
			++patched;
		}

		s_installed = patched > 0;
		logger::info("[KillmoveHook] killmove-refusal seat installed on {} call site(s) of begin-killmove 0x{:X} (Actor::SetKillMove 0x{:X}); entry untouched.",
			patched, begin.address(), setKillMove);
		return s_installed;
	}
}
