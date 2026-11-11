#include "FriendlyFire/HitReactionHook.h"
#include "PCH.h"

#include <RE/A/Actor.h>

#include <cstring>
#include <vector>

namespace scf::friendly_fire
{
	namespace
	{
		// ---- verified 1.5.97 facts (see the header for the full write-up) ----
		constexpr std::uint32_t kStaggerId = 36700;  // +5FA1B0: apply stagger to one actor

		// Entry of +5FA1B0: `mov rax,rsp; push rbp,rsi,rdi; lea rbp,[rax-0x5F]; sub rsp,0xC0`.
		constexpr std::uint8_t kExpectedEntry[] = {
			0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00
		};

		// `or dword [rbx+0xC4], 0x2000` at +0x1EF: the single writer of
		// ActorState2::staggered, i.e. the proof that this ID is the stagger application.
		constexpr std::uintptr_t kSetStaggeredOffset = 0x1EF;
		constexpr std::uint8_t kExpectedSetStaggered[] = { 0x81, 0x8B, 0xC4, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00 };

		using ApplyStagger_t = void (*)(RE::Actor* a_victim, float a_magnitude, RE::Actor* a_attacker);

		ApplyStagger_t s_original{ nullptr };
		bool s_installed{ false };

		// The hit currently being applied on this thread: set by HitHook for the duration of
		// the engine call, so the seat knows whose stagger is the reaction to a friendly hit.
		thread_local RE::FormID s_armedVictim{ 0 };

		[[nodiscard]] bool BytesAre(std::uintptr_t a_address, const std::uint8_t* a_expected, std::size_t a_size) noexcept
		{
			return std::memcmp(reinterpret_cast<const void*>(a_address), a_expected, a_size) == 0;
		}

		void ApplyStaggerThunk(RE::Actor* a_victim, float a_magnitude, RE::Actor* a_attacker)
		{
			if (a_victim && s_armedVictim != 0 && a_victim->GetFormID() == s_armedVictim) {
				s_armedVictim = 0;  // one hit suppresses at most one stagger
				logger::info("[HitReactionHook] Suppressed hit reaction / stagger of protected summon 0x{:08X} ({})",
					a_victim->GetFormID(), a_victim->GetName());
				return;
			}
			s_original(a_victim, a_magnitude, a_attacker);
		}
	}

	void HitReactionHook::ArmFor(std::uint32_t a_victimFormId) noexcept
	{
		s_armedVictim = a_victimFormId;
	}

	void HitReactionHook::Disarm() noexcept
	{
		s_armedVictim = 0;
	}

	bool HitReactionHook::Install() noexcept
	{
		if (s_installed) {
			return true;
		}
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_5_97) {
			logger::info("[HitReactionHook] seat verified on SE 1.5.97 only; friendly hit-reaction suppression not installed on this runtime.");
			return false;
		}

		REL::Relocation<std::uintptr_t> stagger{ REL::ID(kStaggerId) };
		if (!stagger.address()) {
			logger::error("[HitReactionHook] ID {} (apply stagger) did not resolve; hit-reaction suppression disabled.", kStaggerId);
			return false;
		}

		// 1. The ID must still name the function the seat was verified against.
		if (!BytesAre(stagger.address(), kExpectedEntry, sizeof(kExpectedEntry))) {
			logger::error("[HitReactionHook] 0x{:X} does not start with the verified prologue; hit-reaction suppression disabled.",
				stagger.address());
			return false;
		}

		// 2. It must still be the stagger application: the single `or [this+0xC4],0x2000`.
		if (!BytesAre(stagger.address() + kSetStaggeredOffset, kExpectedSetStaggered, sizeof(kExpectedSetStaggered))) {
			logger::error("[HitReactionHook] 0x{:X} has no verified `or [this+0xC4],0x2000`; hit-reaction suppression disabled.",
				stagger.address() + kSetStaggeredOffset);
			return false;
		}

		// 3. Patch the direct sites: the entry stays callable, so forwarding cannot recurse.
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		if (!text.address() || text.size() < 8) {
			logger::error("[HitReactionHook] .text segment is not available; hit-reaction suppression disabled.");
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
			if (target == stagger.address()) {
				sites.push_back({ i, opcode == 0xE9 });
			}
		}
		// The scan is byte-level, so a stray rel32 match would patch unrelated code: accept
		// only the exact set verified on 1.5.97 (5 x E8 + 1 x E9).
		constexpr std::size_t kVerifiedSiteCount = 6;
		if (sites.size() != kVerifiedSiteCount) {
			logger::error("[HitReactionHook] found {} direct site(s) of 0x{:X}, verified {}; hit-reaction suppression disabled.",
				sites.size(), stagger.address(), kVerifiedSiteCount);
			return false;
		}

		s_original = reinterpret_cast<ApplyStagger_t>(stagger.address());
		std::size_t patched = 0;
		std::size_t tailCalls = 0;
		for (const auto& site : sites) {
			REL::Relocation<std::uintptr_t> callSite{ text.address() + site.offset };
			const auto previous = site.tailCall ? callSite.write_branch<5>(&ApplyStaggerThunk) :
			                                      callSite.write_call<5>(&ApplyStaggerThunk);
			if (previous != stagger.address()) {
				logger::warn("[HitReactionHook] site 0x{:X} changed between scan and patch (previous 0x{:X}); chained.",
					text.address() + site.offset, previous);
			}
			++patched;
			tailCalls += site.tailCall ? 1 : 0;
		}

		s_installed = patched > 0;
		logger::info("[HitReactionHook] stagger seat installed on {} site(s) ({} tail call(s)) of 0x{:X}; entry untouched.",
			patched, tailCalls, stagger.address());
		return s_installed;
	}
}
