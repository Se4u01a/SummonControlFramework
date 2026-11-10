#include "Engine/CombatGate.h"
#include "Core/Assist.h"
#include "Core/TargetPin.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/B/BSAtomic.h>
#include <RE/C/CombatController.h>
#include <RE/C/CombatGroup.h>

#include <atomic>
#include <chrono>
#include <cstddef>
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

			// Target pin (FocusTarget): the handle the selector seat answers with.
			// Resolved on the main thread; the seat never looks up a form.
			RE::ActorHandle pinnedHandle{};
			bool            pinAppliedLogged{ false };
			std::chrono::steady_clock::time_point lastPinDeclineLog{};
		};

		// Shared with the StartCombat call sites and the selector seat (any thread).
		std::mutex                                 s_mutex;
		std::unordered_map<RE::FormID, Permission> s_permissions;

		// Aggression area (owner position + radius), guarded by s_mutex.
		core::Vec3 s_aggressionCentre{};
		float s_aggressionRadius{ 0.0f };

		// Number of published pins: the selector seat runs for EVERY actor in combat,
		// so it returns before touching the mutex when no summon has a FocusTarget.
		std::atomic<std::uint32_t> s_pinCount{ 0 };
		std::atomic<bool>          s_selectorSeatObserved{ false };

		// CombatTargetSelector{Standard,Fixed} vtable slot 6:
		//   ActorHandle* SelectTarget(CombatTargetSelector* this, ActorHandle* out)
		// VERIFIED on the unpacked 1.5.97 image (vtable_slot.py / disassembly, 2026-11-10):
		//   * Standard vtable id 265605 -> +1690A98, slot 6 -> +7B5E50 (stores the chosen
		//     handle at [rdx] and returns rdx; reads the controller through [this+0x10],
		//     then the controller's combatGroup through [controller+0x00]);
		//   * Fixed vtable id 265607 -> +1690AF8, slot 6 -> +7B6920
		//     (`mov eax,[rcx+0x18]; mov [rdx],eax; mov rax,rdx; ret`);
		//   * the sole caller CombatController::UpdateTarget (id 32489 -> +4FE300) loops
		//     over the active selectors, `call [rax+0x30]` with rdx = a stack slot, takes
		//     the first non-zero dword and, if it differs from controller+0x2C, calls
		//     CombatController::SetTarget (id 32488 -> +4FDFE0).
		// The same facts are in APMF's VERIFIED-ADDRESSES table (its ch.20 TargetPin).
		// CommonLib declares the selector classes only by name, so +0x10 is a raw offset;
		// it is guarded by a per-call vtable identity check.
		constexpr std::size_t kSelectTargetSlot = 0x06;
		constexpr std::size_t kSelectorControllerOffset = 0x10;

		std::uintptr_t s_vtSelectorStandard{ 0 };
		std::uintptr_t s_vtSelectorFixed{ 0 };

		static_assert(offsetof(RE::CombatController, combatGroup) == 0x00);
		static_assert(offsetof(RE::CombatController, attackerHandle) == 0x28);
		static_assert(offsetof(RE::CombatController, targetHandle) == 0x2C);
		static_assert(offsetof(RE::CombatGroup, targets) == 0x08);
		static_assert(offsetof(RE::CombatGroup, lock) == 0x160);
		static_assert(sizeof(RE::CombatTarget) == 0xA8);
		static_assert(offsetof(RE::CombatTarget, targetHandle) == 0x00);
		static_assert(sizeof(RE::ActorHandle) == sizeof(std::uint32_t));

		core::PinMembership GroupMembership(RE::CombatGroup* a_group, const RE::ActorHandle& a_handle)
		{
			RE::BSReadLockGuard guard(a_group->lock);
			for (const auto& entry : a_group->targets) {
				if (entry.targetHandle == a_handle) {
					return entry.flags.any(RE::CombatTarget::Flags::kTargetLost) ? core::PinMembership::Lost :
					                                                              core::PinMembership::Held;
				}
			}
			return core::PinMembership::Absent;
		}

		// Runs on the BSJobs combat workers, inside Actor::UpdateCombat, for one actor.
		void AnswerSelection(void* a_selector, std::uint32_t* a_out, std::uintptr_t a_expectedVtable) noexcept
		{
			try {
				if (!s_selectorSeatObserved.exchange(true, std::memory_order_relaxed)) {
					logger::info("[TargetPin] selector seat OBSERVED running (first CombatTargetSelector::SelectTarget call).");
				}
				if (s_pinCount.load(std::memory_order_relaxed) == 0 || !a_selector || !a_out) {
					return;
				}
				if (*reinterpret_cast<const std::uintptr_t*>(a_selector) != a_expectedVtable) {
					return;
				}

				auto* controller = *reinterpret_cast<RE::CombatController* const*>(
					reinterpret_cast<std::uintptr_t>(a_selector) + kSelectorControllerOffset);
				if (!controller) {
					return;
				}
				const auto attackerPtr = controller->attackerHandle.get();
				const RE::Actor* attacker = attackerPtr.get();
				if (!attacker) {
					return;
				}
				const auto self = attacker->GetFormID();

				RE::ActorHandle pinned{};
				{
					std::lock_guard lock(s_mutex);
					const auto it = s_permissions.find(self);
					if (it == s_permissions.end() || !it->second.allowCombat || !it->second.restrictToTarget) {
						return;
					}
					pinned = it->second.pinnedHandle;
				}
				if (!pinned) {
					return;
				}

				const std::uint32_t engineAnswer = *a_out;
				const auto targetPtr = pinned.get();
				const RE::Actor* target = targetPtr.get();
				const bool alive = target && !target->IsDead() && !target->IsDisabled() && target->Is3DLoaded();
				auto* group = controller->combatGroup;
				const auto membership = (alive && group) ? GroupMembership(group, pinned) : core::PinMembership::Absent;

				const std::uint32_t answer = core::ChooseSelectorAnswer(engineAnswer, pinned.native_handle(), alive, membership);
				if (answer != engineAnswer) {
					*a_out = answer;
				}

				// Diagnostics (rate-limited): did the pin take, and if not, why.
				std::lock_guard lock(s_mutex);
				const auto it = s_permissions.find(self);
				if (it == s_permissions.end()) {
					return;
				}
				auto& permission = it->second;
				if (answer == pinned.native_handle() && engineAnswer != 0) {
					if (!permission.pinAppliedLogged) {
						permission.pinAppliedLogged = true;
						logger::info("[TargetPin] Summon 0x{:08X}: engine picked handle 0x{:X}, answered with focus 0x{:08X}{}",
							self, engineAnswer, static_cast<RE::FormID>(permission.target),
							engineAnswer == answer ? " (engine already agreed)" : "");
					}
				} else {
					const auto now = std::chrono::steady_clock::now();
					if (now - permission.lastPinDeclineLog >= std::chrono::seconds(2)) {
						permission.lastPinDeclineLog = now;
						logger::info("[TargetPin] Summon 0x{:08X}: focus 0x{:08X} not pinned this update (engine answer 0x{:X}, target {}, group {}, membership {})",
							self, static_cast<RE::FormID>(permission.target), engineAnswer,
							alive ? "alive" : "gone", group ? "yes" : "none", core::ToString(membership));
					}
				}
			} catch (...) {
				// A seat must never throw into engine code; the engine's own answer stands.
			}
		}

		struct SelectStandardHook
		{
			static std::uint32_t* thunk(void* a_self, std::uint32_t* a_out)
			{
				std::uint32_t* result = func(a_self, a_out);
				AnswerSelection(a_self, result, s_vtSelectorStandard);
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct SelectFixedHook
		{
			static std::uint32_t* thunk(void* a_self, std::uint32_t* a_out)
			{
				std::uint32_t* result = func(a_self, a_out);
				AnswerSelection(a_self, result, s_vtSelectorFixed);
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void InstallTargetPin()
		{
			REL::Relocation<std::uintptr_t> standard{ RE::VTABLE_CombatTargetSelectorStandard[0] };
			REL::Relocation<std::uintptr_t> fixed{ RE::VTABLE_CombatTargetSelectorFixed[0] };
			if (!standard.address() || !fixed.address()) {
				logger::error("[TargetPin] CombatTargetSelector vtables did not resolve; FocusTarget pin disabled.");
				return;
			}
			s_vtSelectorStandard = standard.address();
			s_vtSelectorFixed = fixed.address();
			SelectStandardHook::func = standard.write_vfunc(kSelectTargetSlot, SelectStandardHook::thunk);
			SelectFixedHook::func = fixed.write_vfunc(kSelectTargetSlot, SelectFixedHook::thunk);
			logger::info("[TargetPin] CombatTargetSelector{{Standard,Fixed}}::SelectTarget (slot {}) hooked, chaining (originals 0x{:X} / 0x{:X}).",
				kSelectTargetSlot, SelectStandardHook::func.address(), SelectFixedHook::func.address());
		}

		void RecountPins()
		{
			std::uint32_t count = 0;
			for (const auto& [_, permission] : s_permissions) {
				count += (permission.restrictToTarget && permission.pinnedHandle) ? 1 : 0;
			}
			s_pinCount.store(count, std::memory_order_relaxed);
		}

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
			const char* reason = "";
			if (!permission.allowCombat) {
				deny = true;
				reason = "combat forbidden (passive/return/move)";
			} else if (permission.restrictToTarget) {
				deny = targetId != static_cast<RE::FormID>(permission.target);
				reason = "focus-target-only";
			} else if (a_target && s_aggressionRadius > 0.0f) {
				// PoE2-style aggression radius (stage M2): an engine-chosen fight only
				// against actors near the owner. Explicit focus orders never come here.
				const auto& position = a_target->GetPosition();
				deny = !core::IsWithinAggressionRadius(s_aggressionCentre, { position.x, position.y, position.z }, s_aggressionRadius);
				reason = "outside aggression radius";
			}

			if (deny) {
				// Rate-limited so a refused fight cannot flood the log (the AI retries).
				const auto now = std::chrono::steady_clock::now();
				if (now - permission.lastDenyLog >= std::chrono::seconds(2)) {
					permission.lastDenyLog = now;
					logger::info("[CombatGate] StartCombat refused: summon 0x{:08X} target 0x{:08X} ({})",
						a_self, targetId, reason);
				}
			}
			return deny;
		} catch (...) {
			// A seat must never throw into engine code; fail open (let the engine fight).
			return false;
		}
	}

	void PublishAggressionArea(const core::Vec3& a_ownerPosition, float a_radius)
	{
		std::lock_guard lock(s_mutex);
		s_aggressionCentre = a_ownerPosition;
		s_aggressionRadius = a_radius;
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

		// The target pin is independent of the StartCombat seat: a FocusTarget needs both
		// (entry adds the target to the group, the pin makes the selector choose it).
		InstallTargetPin();
		return s_installed;
	}

	void BreakCombat(RE::Actor* a_actor)
	{
		if (!a_actor || !a_actor->IsInCombat()) {
			return;
		}
		// StopCombat is the engine's own teardown (it releases the CombatController).
		// The controller's members are NOT written here: the combat workers own them
		// (CombatController::UpdateTarget rewrites targetHandle every combat update) and
		// a main-thread write into a controller being torn down is a race, not a fix.
		a_actor->StopCombat();
		a_actor->GetActorRuntimeData().currentCombatTarget.reset();
	}

	void UpdateForSummon(RE::Actor* a_summon, const core::CombatPolicy& a_policy)
	{
		if (!a_summon) {
			return;
		}

		const auto id = a_summon->GetFormID();

		// The pinned handle is resolved here, on the main thread, so the selector seat
		// never has to look a form up.
		RE::ActorHandle pinnedHandle{};
		if (a_policy.allowCombat && a_policy.restrictToTarget && a_policy.target != core::InvalidEntityID) {
			if (auto* target = RE::TESForm::LookupByID<RE::Actor>(a_policy.target)) {
				pinnedHandle = target->GetHandle();
			}
		}

		// 1. Publish the policy for the StartCombat seat and the target pin.
		{
			std::lock_guard lock(s_mutex);
			auto& permission = s_permissions[id];
			const bool pinChanged = permission.pinnedHandle != pinnedHandle;
			permission.allowCombat = a_policy.allowCombat;
			permission.restrictToTarget = a_policy.restrictToTarget;
			permission.target = a_policy.target;
			if (pinChanged) {
				permission.pinnedHandle = pinnedHandle;
				permission.pinAppliedLogged = false;
				if (pinnedHandle) {
					logger::info("[TargetPin] Summon 0x{:08X} pinned to focus 0x{:08X}", id, static_cast<RE::FormID>(a_policy.target));
				} else {
					logger::info("[TargetPin] Summon 0x{:08X} pin released", id);
				}
				RecountPins();
			}
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
				BreakCombat(a_summon);
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
			RecountPins();
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
			RecountPins();
		}
		s_applied.clear();
		logger::info("[CombatGate] Combat policy state reset.");
	}
}
