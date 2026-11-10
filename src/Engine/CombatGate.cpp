#include "Engine/CombatGate.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/C/CombatController.h>
#include <RE/C/CombatGroup.h>

#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace scf::engine::combat
{
	namespace
	{
		// Actor::StartCombat(Actor* target, CombatGroup* group) -> bool.
		// VERIFIED 1.5.97: id 37608 -> SkyrimSE.exe+6251B0 (addrlib.py; the same id
		// CommonLib's Actor.cpp binds).
		//
		// HOW THE SEAT IS INSTALLED, AND HOW IT IS *NOT* (field crash 2026-11-10):
		// this CommonLibSSE-NG revision's `Trampoline::write_branch<N>` does NOT build a
		// callable copy of the original function. It patches the site with E8/E9 to a near
		// stub and RETURNS THE PREVIOUS BRANCH TARGET computed from the overwritten bytes
		// (`write_5branch` + the private `write_branch` in src/SKSE/Trampoline.cpp). Patching
		// StartCombat's function ENTRY with it and calling the returned value jumps to
		// garbage: the game died at 0x7FF778072E40, exactly the value the install line
		// logged as "original".
		//
		// The seat therefore patches the DIRECT CALL SITES of StartCombat instead. Each
		// site is a normal `E8 rel32` call, so our thunk is entered with an ordinary call
		// frame and returns the bool to the engine; the thunk forwards to the real function
		// by its verified address (the entry itself is untouched, so no recursion).
		// The scan is a byte scan for `E8` whose rel32 resolves exactly to StartCombat:
		// 9 clean sites on 1.5.97 (matches the crash stack's caller 0x68848E and APMF's
		// "ten StartCombat callers" research), and only sites that still point at the real
		// function are patched (a site another mod already rerouted is logged and skipped).
		using StartCombat_t = bool (*)(RE::Actor*, RE::Actor*, RE::CombatGroup*);

		StartCombat_t s_originalStartCombat{ nullptr };
		bool           s_installed{ false };

		struct Permission
		{
			bool           allowCombat{ true };
			bool           restrictToTarget{ false };
			core::EntityID target{ core::InvalidEntityID };
			std::chrono::steady_clock::time_point lastDenyLog{};
		};

		// Shared with the StartCombat call sites (any thread).
		std::mutex                                 s_mutex;
		std::unordered_map<RE::FormID, Permission> s_permissions;

		// Main-thread only.
		struct AppliedGate
		{
			bool attacksDisabled{ false };
			bool known{ false };
			bool combatBreakLogged{ false };
		};
		std::unordered_map<RE::FormID, AppliedGate> s_applied;

		bool StartCombatGateThunk(RE::Actor* a_self, RE::Actor* a_target, RE::CombatGroup* a_group)
		{
			if (!s_originalStartCombat) {
				return false;
			}
			if (a_self && ShouldDenyStartCombat(a_self->GetFormID(), a_target)) {
				return false;
			}
			return s_originalStartCombat(a_self, a_target, a_group);
		}
	}

	bool ShouldDenyStartCombat(RE::FormID a_self, RE::Actor* a_target) noexcept
	{
		try {
			std::lock_guard lock(s_mutex);

			const auto it = s_permissions.find(a_self);
			if (it == s_permissions.end()) {
				return false; // not a managed summon
			}

			auto& permission = it->second;
			const auto targetId = a_target ? a_target->GetFormID() : static_cast<RE::FormID>(0);

			bool deny = false;
			if (!permission.allowCombat) {
				deny = true;
			} else if (permission.restrictToTarget) {
				deny = targetId != static_cast<RE::FormID>(permission.target);
			}

			if (deny) {
				// Rate-limited so a refused fight cannot flood the log (the AI retries).
				const auto now = std::chrono::steady_clock::now();
				if (now - permission.lastDenyLog >= std::chrono::seconds(2)) {
					permission.lastDenyLog = now;
					logger::info("[CombatGate] StartCombat refused: summon 0x{:08X} target 0x{:08X} ({} -> {})",
						a_self,
						targetId,
						permission.allowCombat ? "focus-target-only" : "combat forbidden",
						permission.restrictToTarget ? "policy" : "passive/return/move");
				}
			}
			return deny;
		} catch (...) {
			// A seat must never throw into engine code; fail open (let the engine fight).
			return false;
		}
	}

	bool Install()
	{
		if (s_installed) {
			return true;
		}

		// The real function is never patched, so this address stays callable.
		REL::Relocation<std::uintptr_t> function{ REL::RelocationID(37608, 38561) };
		if (!function.address()) {
			logger::error("[CombatGate] Actor::StartCombat did not resolve; combat denial disabled.");
			return false;
		}
		s_originalStartCombat = reinterpret_cast<StartCombat_t>(function.address());

		const auto text = REL::Module::get().segment(REL::Segment::textx);
		if (!text.address() || text.size() < 8) {
			logger::error("[CombatGate] .text segment is not available; combat denial disabled.");
			return false;
		}

		const auto* bytes = text.pointer<std::uint8_t>();
		struct Site
		{
			std::size_t offset;
			bool        tailCall; // E9: the caller tail-jumps, so it must stay a jmp
		};
		std::vector<Site> sites;

		// Collect first: patching changes the bytes the scan reads.
		for (std::size_t i = 0; i + 5 <= text.size(); ++i) {
			const auto opcode = bytes[i];
			if (opcode != 0xE8 && opcode != 0xE9) {
				continue;
			}

			std::int32_t displacement = 0;
			std::memcpy(&displacement, bytes + i + 1, sizeof(displacement));
			const auto previous = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(text.address() + i + 5) + displacement);
			if (previous == function.address()) {
				sites.push_back({ i, opcode == 0xE9 });
			}
		}

		if (sites.empty()) {
			logger::error("[CombatGate] no direct call sites of Actor::StartCombat found; combat denial disabled.");
			return false;
		}

		std::size_t patched = 0;
		std::size_t tailCalls = 0;
		for (const auto& site : sites) {
			REL::Relocation<std::uintptr_t> callSite{ text.address() + site.offset };
			const auto previous = site.tailCall ? callSite.write_branch<5>(&StartCombatGateThunk)
			                                    : callSite.write_call<5>(&StartCombatGateThunk);
			if (previous != function.address()) {
				logger::warn("[CombatGate] StartCombat site 0x{:X} changed between scan and patch (previous 0x{:X}); chained.",
					text.address() + site.offset, previous);
			}
			++patched;
			tailCalls += site.tailCall ? 1 : 0;
		}

		s_installed = patched > 0;
		if (s_installed) {
			logger::info("[CombatGate] StartCombat denial seat installed on {} site(s) ({} tail call(s)) of 0x{:X}; function entry untouched.",
				patched, tailCalls, function.address());
		} else {
			logger::error("[CombatGate] no StartCombat call site could be patched; combat denial disabled.");
		}
		return s_installed;
	}

	void UpdateForSummon(RE::Actor* a_summon, const core::CombatPolicy& a_policy)
	{
		if (!a_summon) {
			return;
		}

		const auto id = a_summon->GetFormID();

		// 1. Publish the policy for the StartCombat seat.
		{
			std::lock_guard lock(s_mutex);
			auto& permission = s_permissions[id];
			permission.allowCombat = a_policy.allowCombat;
			permission.restrictToTarget = a_policy.restrictToTarget;
			permission.target = a_policy.target;
		}

		// 2. Native attack/cast deny bits while combat is forbidden.
		auto& applied = s_applied[id];
		if (!applied.known || applied.attacksDisabled != a_policy.disableAttacks) {
			a_summon->GetActorRuntimeData().boolFlags.set(
				a_policy.disableAttacks,
				RE::Actor::BOOL_FLAGS::kAttackingDisabled,
				RE::Actor::BOOL_FLAGS::kCastingDisabled);
			applied.attacksDisabled = a_policy.disableAttacks;
			applied.known = true;
			logger::info("[CombatGate] Summon 0x{:08X} attacks/casts disabled={}", id, a_policy.disableAttacks);
		}

		// 3. Break an existing fight immediately; the seat keeps new ones from starting.
		if (!a_policy.allowCombat) {
			if (a_summon->IsInCombat()) {
				a_summon->StopCombat();
				a_summon->GetActorRuntimeData().currentCombatTarget.reset();
				if (auto* controller = a_summon->GetActorRuntimeData().combatController) {
					controller->targetHandle.reset();
					controller->startedCombat = false;
				}
				if (!applied.combatBreakLogged) {
					applied.combatBreakLogged = true;
					logger::info("[CombatGate] Summon 0x{:08X} combat broken by policy", id);
				}
			}
		} else if (applied.combatBreakLogged) {
			applied.combatBreakLogged = false;
		}
	}

	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons)
	{
		{
			std::lock_guard lock(s_mutex);
			std::erase_if(s_permissions, [&a_liveSummons](const auto& a_entry) {
				return a_liveSummons.find(static_cast<core::EntityID>(a_entry.first)) == a_liveSummons.end();
			});
		}
		std::erase_if(s_applied, [&a_liveSummons](const auto& a_entry) {
			return a_liveSummons.find(static_cast<core::EntityID>(a_entry.first)) == a_liveSummons.end();
		});
	}

	void ResetAll()
	{
		{
			std::lock_guard lock(s_mutex);
			s_permissions.clear();
		}
		s_applied.clear();
		logger::info("[CombatGate] Combat policy state reset.");
	}
}
