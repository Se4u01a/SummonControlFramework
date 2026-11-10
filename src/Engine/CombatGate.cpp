#include "Engine/CombatGate.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/C/CombatController.h>
#include <RE/C/CombatGroup.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace scf::engine::combat
{
	namespace
	{
		// Actor::StartCombat(Actor* target, CombatGroup* group) -> bool.
		// VERIFIED 1.5.97: id 37608 -> SkyrimSE.exe+6251B0 (addrlib.py; the same id
		// CommonLib's Actor.cpp binds). The function starts with a relocatable
		// `mov r11, rsp; push rsi; push rdi; ...` prologue, so a 5-byte branch through the
		// SKSE trampoline is safe. The AI enters combat through this function; refusing
		// here (returning false, exactly like the engine does for a dead self) keeps the
		// actor out of combat without touching the combat controller.
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

		// Shared with the StartCombat seat (any thread).
		std::mutex                                s_mutex;
		std::unordered_map<RE::FormID, Permission> s_permissions;

		// Main-thread only.
		struct AppliedGate
		{
			bool attacksDisabled{ false };
			bool known{ false };
			bool combatBreakLogged{ false };
		};
		std::unordered_map<RE::FormID, AppliedGate> s_applied;

		bool StartCombatThunk(RE::Actor* a_self, RE::Actor* a_target, RE::CombatGroup* a_group)
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

		REL::Relocation<std::uintptr_t> function{ REL::RelocationID(37608, 38561) };
		if (!function.address()) {
			logger::error("[CombatGate] Actor::StartCombat did not resolve; combat denial disabled.");
			return false;
		}

		const auto trampoline = function.write_branch<5>(&StartCombatThunk);
		s_originalStartCombat = reinterpret_cast<StartCombat_t>(trampoline);
		s_installed = s_originalStartCombat != nullptr;

		if (s_installed) {
			logger::info("[CombatGate] StartCombat denial seat installed at 0x{:X} (original 0x{:X}).",
				function.address(), reinterpret_cast<std::uintptr_t>(s_originalStartCombat));
		} else {
			logger::error("[CombatGate] Trampoline for Actor::StartCombat failed; combat denial disabled.");
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
