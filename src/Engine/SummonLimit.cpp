#include "Engine/SummonLimit.h"
#include "Core/SummonLimit.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/P/PlayerCharacter.h>

#include <atomic>
#include <cstring>

namespace scf::engine::summonlimit
{
	namespace
	{
		constexpr std::uintptr_t kAddCommandedActorId = 38993;  // +683D70
		constexpr std::uintptr_t kMovEcxOffset = 0x9C;          // +683E0C: B9 44 00 00 00
		constexpr std::uintptr_t kCallOffset = 0xA1;            // +683E11: E8 rel32 -> +32ECE0
		constexpr std::uint32_t kEntryPointCommandedActorLimit = 68;

		using HandleEntryPoint_t = void (*)(std::uint32_t, RE::Actor*, void*, float*);
		HandleEntryPoint_t s_original{ nullptr };
		std::atomic<std::uint32_t> s_playerLimit{ 0 };
		std::atomic<bool> s_loggedRaise{ false };
		bool s_installed{ false };

		void LimitThunk(std::uint32_t a_entryPoint, RE::Actor* a_owner, void* a_spell, float* a_value)
		{
			s_original(a_entryPoint, a_owner, a_spell, a_value);
			if (a_entryPoint != kEntryPointCommandedActorLimit || !a_value || !a_owner || !a_owner->IsPlayerRef()) {
				return;
			}
			const auto configured = s_playerLimit.load(std::memory_order_relaxed);
			const float perk = *a_value;
			const float raised = core::ApplyPlayerSummonLimit(perk, configured);
			if (raised != perk) {
				*a_value = raised;
				if (!s_loggedRaise.exchange(true, std::memory_order_relaxed)) {
					logger::info("[SummonLimit] player summon limit raised: perks give {:.0f}, uPlayerSummonLimit {} -> {:.0f}.",
						perk, configured, raised);
				}
			}
		}
	}

	bool Install()
	{
		if (s_installed) {
			return true;
		}
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_5_97) {
			logger::info("[SummonLimit] seat verified on SE 1.5.97 only; the engine summon limit stays vanilla on this runtime.");
			return false;
		}

		const auto function = REL::ID(kAddCommandedActorId).address();
		const auto callee = REL::ID(23073).address();  // BGSEntryPoint::HandleEntryPoint
		const auto* mov = reinterpret_cast<const std::uint8_t*>(function + kMovEcxOffset);
		const auto* call = reinterpret_cast<const std::uint8_t*>(function + kCallOffset);
		std::int32_t displacement = 0;
		std::memcpy(&displacement, call + 1, sizeof(displacement));
		const auto target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(function + kCallOffset + 5) + displacement);
		constexpr std::uint8_t kMovEcx68[] = { 0xB9, 0x44, 0x00, 0x00, 0x00 };
		if (std::memcmp(mov, kMovEcx68, sizeof(kMovEcx68)) != 0 || call[0] != 0xE8 || target != callee) {
			logger::error("[SummonLimit] entry-point-68 call at 0x{:X} is not `mov ecx,0x44; call 0x{:X}` (opcode 0x{:02X}, target 0x{:X}); seat refused.",
				function + kCallOffset, callee, call[0], target);
			return false;
		}

		s_original = reinterpret_cast<HandleEntryPoint_t>(callee);
		REL::Relocation<std::uintptr_t> site{ function + kCallOffset };
		site.write_call<5>(&LimitThunk);
		s_installed = true;
		logger::info("[SummonLimit] commanded-actor limit seat installed at 0x{:X} (HandleEntryPoint 68).", function + kCallOffset);
		return true;
	}

	void Publish(std::uint32_t a_playerLimit) noexcept
	{
		const auto previous = s_playerLimit.exchange(a_playerLimit, std::memory_order_relaxed);
		if (previous != a_playerLimit) {
			s_loggedRaise.store(false, std::memory_order_relaxed);
			logger::info("[SummonLimit] uPlayerSummonLimit = {} ({}).", a_playerLimit,
				a_playerLimit == 0 ? "vanilla perks decide" : "player limit raised to at least this");
		}
	}
}
