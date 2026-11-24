#include "Engine/CombatGate.h"
#include "Core/Assist.h"
#include "Core/KeywordApi.h"
#include "Core/TargetPin.h"
#include "Core/WolfManeuver.h"
#include "PCH.h"
#include "Engine/WolfManeuver.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/B/BSAtomic.h>
#include <RE/C/CombatController.h>
#include <RE/C/CombatGroup.h>
#include <RE/T/TESObjectWEAP.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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

		// Actor::CanAttackActor(Actor* target) -> bool.
		// VERIFIED 1.5.97 (Ghidra 2026-11-23): id 36532 (AE 37532) -> SkyrimSE.exe+5E7500
		// (matches CommonLibSSE-NG Actor.cpp:160). For commanded actors (kIsCommandedActor),
		// vanilla CanAttackActor refuses ANY target that is not already in combat with the
		// player or the summon (5 direct E8 callers: CombatManager::StartCombat +7A5ECB,
		// CombatGroup::CanAddTarget +76A799, CombatGroup upkeep +76AD2E, AIProcess +658807/
		// +658878). Because SCF_CmdAttackTarget is a non-hostile command spell, StartCombat
		// always returned false on distant/unalerted foes until the target entered combat.
		// Patching the direct call sites lets an explicit FocusTarget bypass only that
		// refusal while keeping the function entry untouched and leaving all other actors
		// and orders 100% vanilla.
		using CanAttackActor_t = bool (*)(RE::Actor*, RE::Actor*);

		StartCombat_t    s_originalStartCombat{ nullptr };
		CanAttackActor_t s_originalCanAttackActor{ nullptr };
		bool             s_installed{ false };

		struct Permission
		{
			bool           allowCombat{ true };
			bool           restrictToTarget{ false };
			bool           radiusManaged{ false };
			core::EntityID target{ core::InvalidEntityID };
			std::chrono::steady_clock::time_point lastDenyLog{};

			// Target pin (FocusTarget): the handle the selector seat answers with.
			// Resolved on the main thread; the seat never looks up a form.
			RE::ActorHandle pinnedHandle{};
			bool            focusedWolf{ false };
			std::uint32_t  loggedManeuvers{};
			bool            pinAppliedLogged{ false };
			bool            canAttackOverrideLogged{ false };
			std::chrono::steady_clock::time_point lastPinDeclineLog{};

			// Keyword API v2 directives.
			bool               alwaysNoCircling{ false };
			bool               ignoreAggressionRadius{ false };
			core::KeywordFlags specialtyFlags{ core::kKwNone };
			core::EntityID     ownerId{ core::InvalidEntityID };
		};

		// Shared with the StartCombat / CanAttackActor call sites and the selector seat (any thread).
		std::mutex                                 s_mutex;
		std::unordered_map<RE::FormID, Permission> s_permissions;

		// Keyword API threat sets (SCF_Untargetable, SCF_HighThreat/TauntPriority, SCF_LowThreat),
		// guarded by s_mutex; atomic counts let the selector seat skip the lock when empty.
		std::unordered_set<RE::FormID> s_untargetable;
		std::unordered_set<RE::FormID> s_highThreat;
		std::unordered_set<RE::FormID> s_lowThreat;
		std::atomic<std::uint32_t> s_untargetableCount{ 0 };
		std::atomic<std::uint32_t> s_threatCount{ 0 };
		std::atomic<std::uint32_t> s_specialtyCount{ 0 };
		std::chrono::steady_clock::time_point s_lastUntargetableDenyLog{};
		std::atomic<std::uint64_t> s_untargetableReplaced{ 0 };
		std::atomic<std::uint64_t> s_untargetableCleared{ 0 };
		std::uint64_t s_lastReplacedReported{ 0 };
		std::uint64_t s_lastClearedReported{ 0 };
		std::chrono::steady_clock::time_point s_lastUntargetableReport{};

		// Aggression area (owner position + radius), guarded by s_mutex.
		core::Vec3 s_aggressionCentre{};
		float s_aggressionRadius{ 0.0f };

		// Number of published pins: the selector and CanAttackActor seats run for EVERY
		// actor in combat, so they return before touching the mutex when no summon has a
		// FocusTarget.
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

		float HorizontalDist(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs) noexcept
		{
			return std::hypot(a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y);
		}

		float HorizontalDist(const RE::NiPoint3& a_lhs, const core::Vec3& a_rhs) noexcept
		{
			return std::hypot(a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y);
		}

		bool HasRangedWeaponEquipped(const RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			for (const bool left : { false, true }) {
				if (const auto* equipped = a_actor->GetEquippedObject(left)) {
					if (const auto* weapon = equipped->As<RE::TESObjectWEAP>()) {
						if (weapon->IsBow() || weapon->IsCrossbow() || weapon->IsStaff()) {
							return true;
						}
					}
				}
			}
			return false;
		}

		float ActorHealthFraction(RE::Actor* a_actor)
		{
			auto* avo = a_actor ? a_actor->AsActorValueOwner() : nullptr;
			if (!avo) {
				return 1.0f;
			}
			const float cur = avo->GetActorValue(RE::ActorValue::kHealth);
			const float max = a_actor->GetActorValueMax(RE::ActorValue::kHealth);
			if (!(max > 0.0f)) {
				return 1.0f;
			}
			return std::clamp(cur / max, 0.0f, 1.0f);
		}

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
				if ((s_pinCount.load(std::memory_order_acquire) == 0 &&
				     s_specialtyCount.load(std::memory_order_acquire) == 0) ||
				    !a_selector || !a_out) {
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
				bool radiusManaged = false;
				core::KeywordFlags specialtyFlags = core::kKwNone;
				core::EntityID ownerId = core::InvalidEntityID;
				core::Vec3 ownerPos{};
				{
					std::lock_guard lock(s_mutex);
					const auto it = s_permissions.find(self);
					if (it == s_permissions.end()) {
						return;
					}
					radiusManaged = it->second.radiusManaged;
					if (radiusManaged && (!it->second.allowCombat || !it->second.pinnedHandle ||
					    s_untargetable.contains(it->second.target))) {
						*a_out = 0;
						return;
					}
					if (!it->second.allowCombat) {
						return;
					}
					if (it->second.restrictToTarget) {
						pinned = it->second.pinnedHandle;
					} else if ((it->second.specialtyFlags & core::kKwTargetingSpecialtyMask) != 0) {
						specialtyFlags = it->second.specialtyFlags;
						ownerId = it->second.ownerId;
						ownerPos = s_aggressionCentre;
					} else {
						return;
					}
				}
				if (specialtyFlags != core::kKwNone) {
					auto* group = controller->combatGroup;
					if (!group || *a_out == 0) {
						return;
					}
					const auto summonPos = attacker->GetPosition();
					std::vector<core::SpecialtyCandidate> candidates;
					std::vector<RE::FormID> ids;
					{
						RE::BSReadLockGuard guard(group->lock);
						candidates.reserve(group->targets.size());
						ids.reserve(group->targets.size());
						for (const auto& entry : group->targets) {
							const auto targetPtr = entry.targetHandle.get();
							auto* target = targetPtr.get();
							core::SpecialtyCandidate c{};
							c.handle = entry.targetHandle.native_handle();
							c.lost = entry.flags.any(RE::CombatTarget::Flags::kTargetLost) ||
							         !target || target->IsDead() || target->IsDisabled() || !target->Is3DLoaded();
							if (!c.lost) {
								c.isCasting = target->IsCasting(nullptr);
								c.hasRangedWeapon = HasRangedWeaponEquipped(target);
								c.healthFraction = ActorHealthFraction(target);
								const auto targetTarget = target->GetActorRuntimeData().currentCombatTarget.get();
								c.attackingOwner = targetTarget && ownerId != core::InvalidEntityID &&
								                   targetTarget->GetFormID() == ownerId;
								const auto& tpos = target->GetPosition();
								c.distanceToOwner = HorizontalDist(tpos, ownerPos);
								c.distanceToSummon = HorizontalDist(tpos, summonPos);
							}
							candidates.push_back(c);
							ids.push_back(target ? target->GetFormID() : 0);
						}
					}
					if (s_untargetableCount.load(std::memory_order_acquire) != 0) {
						std::lock_guard lock(s_mutex);
						for (std::size_t i = 0; i < candidates.size(); ++i) {
							if (ids[i] != 0 && s_untargetable.contains(ids[i])) {
								candidates[i].lost = true;
							}
						}
					}
					const auto chosen = core::ChooseSummonTargetBySpecialty(*a_out, specialtyFlags, candidates);
					if (chosen != 0 && chosen != *a_out) {
						*a_out = chosen;
					}
					return;
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

				const std::uint32_t answer = radiusManaged ?
					core::ChooseRadiusSelectorAnswer(engineAnswer, pinned.native_handle(), alive, membership) :
					core::ChooseSelectorAnswer(engineAnswer, pinned.native_handle(), alive, membership);
				// Policy can change while GroupMembership reads the combat group.
				// Revalidate the pin before writing a selector result.
				std::lock_guard lock(s_mutex);
				const auto it = s_permissions.find(self);
				if (it == s_permissions.end() || it->second.pinnedHandle != pinned ||
				    it->second.radiusManaged != radiusManaged) {
					return;
				}
				auto& permission = it->second;
				if (answer != engineAnswer) {
					*a_out = answer;
				}
				if (answer == pinned.native_handle() && engineAnswer != 0) {
					if (!permission.pinAppliedLogged) {
						permission.pinAppliedLogged = true;
					if (permission.focusedWolf) {
						logger::info("[WolfManeuver] Summon 0x{:08X} focus 0x{:08X}: active combat style 0x{:08X}",
							self, permission.target, controller->combatStyle ? controller->combatStyle->GetFormID() : 0);
					}
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

		// Keyword API threat rules in ANY actor's target selection (same seat, before the pin):
		// SCF_Untargetable (avoid/clear), SCF_HighThreat/SCF_TauntPriority (draw aggro),
		// and SCF_LowThreat (deprioritize when normal targets exist).
		void AvoidUntargetable(void* a_selector, std::uint32_t* a_out, std::uintptr_t a_expectedVtable) noexcept
		{
			try {
				if (s_threatCount.load(std::memory_order_acquire) == 0 || !a_selector || !a_out || *a_out == 0) {
					return;
				}
				if (*reinterpret_cast<const std::uintptr_t*>(a_selector) != a_expectedVtable) {
					return;
				}
				auto* controller = *reinterpret_cast<RE::CombatController* const*>(
					reinterpret_cast<std::uintptr_t>(a_selector) + kSelectorControllerOffset);
				auto* group = controller ? controller->combatGroup : nullptr;
				if (!group) {
					return;
				}

				const auto attackerPtr = controller->attackerHandle.get();
				const RE::Actor* attacker = attackerPtr.get();
				const std::uint32_t engineAnswer = *a_out;
				std::vector<core::ThreatCandidate> candidates;
				std::vector<RE::FormID> ids;
				{
					RE::BSReadLockGuard guard(group->lock);
					candidates.reserve(group->targets.size());
					ids.reserve(group->targets.size());
					for (const auto& entry : group->targets) {
						const auto actor = entry.targetHandle.get();
						core::ThreatCandidate c{};
						c.handle = entry.targetHandle.native_handle();
						c.lost = entry.flags.any(RE::CombatTarget::Flags::kTargetLost) || (actor && actor->IsDead());
						c.distance = (attacker && actor) ? HorizontalDist(attacker->GetPosition(), actor->GetPosition()) : 0.0f;
						candidates.push_back(c);
						ids.push_back(actor ? actor->GetFormID() : 0);
					}
				}
				bool answerUntargetable = false;
				{
					std::lock_guard lock(s_mutex);
					for (std::size_t i = 0; i < candidates.size(); ++i) {
						if (ids[i] != 0) {
							candidates[i].untargetable = s_untargetable.contains(ids[i]);
							candidates[i].highThreat = s_highThreat.contains(ids[i]);
							candidates[i].lowThreat = s_lowThreat.contains(ids[i]);
						}
						answerUntargetable = answerUntargetable || (candidates[i].handle == engineAnswer && candidates[i].untargetable);
					}
				}
				const auto answer = core::ChooseEnemyTargetByThreat(engineAnswer, candidates);
				if (answer != engineAnswer) {
					*a_out = answer;
					if (answerUntargetable) {
						(answer != 0 ? s_untargetableReplaced : s_untargetableCleared).fetch_add(1, std::memory_order_relaxed);
					}
				}
			} catch (...) {
				// Never throw into engine code; the engine's answer stands.
			}
		}

		struct SelectStandardHook
		{
			static std::uint32_t* thunk(void* a_self, std::uint32_t* a_out)
			{
				std::uint32_t* result = func(a_self, a_out);
				AvoidUntargetable(a_self, result, s_vtSelectorStandard);
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
				AvoidUntargetable(a_self, result, s_vtSelectorFixed);
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

		std::atomic<bool> s_hasFocusedWolf{ false };

		void RecountPins()
		{
			std::uint32_t count = 0;
			std::uint32_t specialtyCount = 0;
			bool hasWolf = false;
			for (const auto& [_, permission] : s_permissions) {
				hasWolf |= (permission.focusedWolf || permission.alwaysNoCircling);
				count += (permission.radiusManaged || (permission.restrictToTarget && permission.pinnedHandle)) ? 1 : 0;
				specialtyCount += (permission.allowCombat && !permission.restrictToTarget && !permission.radiusManaged &&
				                   (permission.specialtyFlags & core::kKwTargetingSpecialtyMask) != 0) ? 1 : 0;
			}
			s_hasFocusedWolf.store(hasWolf, std::memory_order_release);
			s_pinCount.store(count, std::memory_order_release);
			s_specialtyCount.store(specialtyCount, std::memory_order_release);
		}

		// Main-thread only.
		struct AppliedGate
		{
			bool attacksDisabled{ false };
			bool known{ false };
			bool combatBreakLogged{ false };
			RE::ActorHandle radiusActor{};
			bool previousAttackDisabled{};
			bool previousCastDisabled{};
		};
		std::unordered_map<RE::FormID, AppliedGate> s_applied;

		void RestoreRadiusFlags(const AppliedGate& a_applied)
		{
			if (const auto actor = a_applied.radiusActor.get()) {
				using Flags = RE::Actor::BOOL_FLAGS;
				auto& flags = actor->GetActorRuntimeData().boolFlags;
				flags.set(a_applied.previousAttackDisabled, Flags::kAttackingDisabled);
				flags.set(a_applied.previousCastDisabled, Flags::kCastingDisabled);
			}
		}

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

		bool ShouldAllowExplicitFocusAttack(RE::FormID a_self, RE::Actor* a_target) noexcept
		{
			try {
				if (!a_target || a_target->IsPlayerRef() || a_target->IsDead() || a_target->IsDisabled() || a_target->IsDeleted()) {
					return false;
				}
				const RE::FormID targetId = a_target->GetFormID();
				if (targetId == 0 || targetId == a_self) {
					return false;
				}

				std::lock_guard lock(s_mutex);
				if (s_untargetableCount.load(std::memory_order_acquire) != 0 && s_untargetable.contains(targetId)) {
					return false;
				}
				const auto it = s_permissions.find(a_self);
				if (it == s_permissions.end()) {
					return false;
				}
				auto& permission = it->second;
				if (permission.radiusManaged || !permission.allowCombat || !permission.restrictToTarget ||
				    static_cast<RE::FormID>(permission.target) != targetId) {
					return false;
				}
				if (!permission.canAttackOverrideLogged) {
					permission.canAttackOverrideLogged = true;
					logger::info("[CombatGate] CanAttackActor permitted for explicit focus: summon 0x{:08X} -> target 0x{:08X}",
						a_self, targetId);
				}
				return true;
			} catch (...) {
				return false;
			}
		}

		bool CanAttackActorGateThunk(RE::Actor* a_self, RE::Actor* a_target)
		{
			if (!s_originalCanAttackActor) {
				return false;
			}
			if (a_self && a_target && a_self != a_target &&
			    s_pinCount.load(std::memory_order_acquire) != 0 &&
			    ShouldAllowExplicitFocusAttack(a_self->GetFormID(), a_target)) {
				return true;
			}
			return s_originalCanAttackActor(a_self, a_target);
		}

		struct CallSitePatchResult
		{
			std::size_t patched{ 0 };
			std::size_t tailCalls{ 0 };
		};

		CallSitePatchResult PatchDirectCallSites(std::uintptr_t a_functionAddress, std::uintptr_t a_thunkAddress, const char* a_label)
		{
			CallSitePatchResult result{};
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			if (!text.address() || text.size() < 8) {
				logger::error("[CombatGate] .text segment is not available; {} seat disabled.", a_label);
				return result;
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
				if (previous == a_functionAddress) {
					sites.push_back({ i, opcode == 0xE9 });
				}
			}

			if (sites.empty()) {
				logger::error("[CombatGate] no direct call sites of {} found; seat disabled.", a_label);
				return result;
			}

			for (const auto& site : sites) {
				REL::Relocation<std::uintptr_t> callSite{ text.address() + site.offset };
				const auto previous = site.tailCall ? callSite.write_branch<5>(a_thunkAddress)
				                                    : callSite.write_call<5>(a_thunkAddress);
				if (previous != a_functionAddress) {
					logger::warn("[CombatGate] {} site 0x{:X} changed between scan and patch (previous 0x{:X}); chained.",
						a_label, text.address() + site.offset, previous);
				}
				++result.patched;
				result.tailCalls += site.tailCall ? 1 : 0;
			}

			return result;
		}
	}

	bool ShouldDenyStartCombat(RE::FormID a_self, RE::Actor* a_target) noexcept
	{
		try {
			std::lock_guard lock(s_mutex);

			// Keyword API: nobody starts a fight against an SCF_Untargetable actor.
			if (a_target && s_untargetableCount.load(std::memory_order_acquire) != 0 &&
				a_target->GetFormID() != a_self && s_untargetable.contains(a_target->GetFormID())) {
				const auto now = std::chrono::steady_clock::now();
				if (now - s_lastUntargetableDenyLog >= std::chrono::seconds(2)) {
					s_lastUntargetableDenyLog = now;
					logger::info("[CombatGate] StartCombat refused: 0x{:08X} -> 0x{:08X} (target is SCF_Untargetable)",
						a_self, a_target->GetFormID());
				}
				return true;
			}

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
			} else if (a_target) {
				const auto& position = a_target->GetPosition();
				bool bodyguardThreat = false;
				if ((permission.specialtyFlags & core::kKwBodyguard) != 0) {
					const auto targetTarget = a_target->GetActorRuntimeData().currentCombatTarget.get();
					const auto ttId = targetTarget ? targetTarget->GetFormID() : static_cast<RE::FormID>(0);
					bodyguardThreat = (ttId != 0 && (ttId == permission.ownerId || ttId == a_self));
					const bool nearOwner = core::IsWithinAggressionRadius(s_aggressionCentre, { position.x, position.y, position.z }, core::kBodyguardAlertRadius);
					if (!bodyguardThreat && !nearOwner) {
						deny = true;
						reason = "bodyguard: target not threatening owner";
					}
				}
				if (!deny && !bodyguardThreat && !permission.ignoreAggressionRadius && s_aggressionRadius > 0.0f) {
					// PoE2-style aggression radius (stage M2): an engine-chosen fight only
					// against actors near the owner. Explicit focus orders and active threats
					// intercepted by SCF_Bodyguard are not blocked by the radius.
					deny = !core::IsWithinAggressionRadius(s_aggressionCentre, { position.x, position.y, position.z }, s_aggressionRadius);
					reason = "outside aggression radius";
				}
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

	bool HasFocusedWolf() noexcept
	{
		return s_hasFocusedWolf.load(std::memory_order_acquire);
	}

	bool BlocksWolfManeuver(const RE::CombatController& controller, std::size_t index, const char* name)
	{
		const auto actor = controller.attackerHandle.get();
		if (!actor) {
			return false;
		}
		std::lock_guard lock(s_mutex);
		const auto it = s_permissions.find(actor->GetFormID());
		if (it == s_permissions.end()) {
			return false;
		}
		auto& permission = it->second;
		if (!core::BlocksWolfManeuver(permission.focusedWolf, permission.pinnedHandle.native_handle(),
			controller.targetHandle.native_handle(), permission.alwaysNoCircling)) {
			return false;
		}
		const auto bit = 1u << index;
		if (!(permission.loggedManeuvers & bit)) {
			permission.loggedManeuvers |= bit;
			logger::info("[WolfManeuver] Summon 0x{:08X} focus 0x{:08X} style 0x{:08X}: skipped {}",
				actor->GetFormID(), permission.target, controller.combatStyle ? controller.combatStyle->GetFormID() : 0, name);
		}
		return true;
	}

	bool Install()
	{
		if (s_installed) {
			return true;
		}

		// 1. Actor::StartCombat denial seat (function entry untouched).
		REL::Relocation<std::uintptr_t> startCombatFn{ REL::RelocationID(37608, 38561) };
		if (!startCombatFn.address()) {
			logger::error("[CombatGate] Actor::StartCombat did not resolve; combat denial disabled.");
			return false;
		}
		s_originalStartCombat = reinterpret_cast<StartCombat_t>(startCombatFn.address());

		const auto startCombatSites = PatchDirectCallSites(
			startCombatFn.address(),
			reinterpret_cast<std::uintptr_t>(&StartCombatGateThunk),
			"Actor::StartCombat");

		s_installed = startCombatSites.patched > 0;
		if (s_installed) {
			logger::info("[CombatGate] StartCombat denial seat installed on {} site(s) ({} tail call(s)) of 0x{:X}; function entry untouched.",
				startCombatSites.patched, startCombatSites.tailCalls, startCombatFn.address());
		} else {
			logger::error("[CombatGate] no StartCombat call site could be patched; combat denial disabled.");
		}

		// 2. Actor::CanAttackActor seat for explicit Attack focus targets (function entry untouched).
		REL::Relocation<std::uintptr_t> canAttackFn{ REL::RelocationID(36532, 37532) };
		if (canAttackFn.address()) {
			s_originalCanAttackActor = reinterpret_cast<CanAttackActor_t>(canAttackFn.address());
			const auto canAttackSites = PatchDirectCallSites(
				canAttackFn.address(),
				reinterpret_cast<std::uintptr_t>(&CanAttackActorGateThunk),
				"Actor::CanAttackActor");
			if (canAttackSites.patched > 0) {
				logger::info("[CombatGate] CanAttackActor focus seat installed on {} site(s) ({} tail call(s)) of 0x{:X}; function entry untouched.",
					canAttackSites.patched, canAttackSites.tailCalls, canAttackFn.address());
			}
		} else {
			logger::warn("[CombatGate] Actor::CanAttackActor did not resolve; explicit Attack on non-combat targets may require proximity.");
		}

		// 3. The target pin is independent of the StartCombat seat: a FocusTarget needs both
		// (entry adds the target to the group, the pin makes the selector choose it).
		InstallTargetPin();
		wolf::InstallManeuverHooks();
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

	void PublishUntargetable(const std::vector<keywords::WorldActor>& a_world)
	{
		std::unordered_set<RE::FormID> untargetable;
		std::unordered_set<RE::FormID> highThreat;
		std::unordered_set<RE::FormID> lowThreat;
		for (const auto& world : a_world) {
			if (world.flags & core::kKwUntargetable) {
				untargetable.insert(world.id);
			}
			if (world.flags & core::kKwHighThreat) {
				highThreat.insert(world.id);
			}
			if (world.flags & core::kKwLowThreat) {
				lowThreat.insert(world.id);
			}
		}
		const auto totalThreat = static_cast<std::uint32_t>(untargetable.size() + highThreat.size() + lowThreat.size());
		{
			std::lock_guard lock(s_mutex);
			if (untargetable != s_untargetable) {
				logger::info("[CombatGate] SCF_Untargetable actors: {} (was {}).", untargetable.size(), s_untargetable.size());
			}
			if (highThreat != s_highThreat || lowThreat != s_lowThreat) {
				logger::info("[CombatGate] Threat keywords: highThreat={}, lowThreat={}.", highThreat.size(), lowThreat.size());
			}
			s_untargetable = untargetable;
			s_highThreat = std::move(highThreat);
			s_lowThreat = std::move(lowThreat);
		}
		s_untargetableCount.store(static_cast<std::uint32_t>(untargetable.size()), std::memory_order_release);
		s_threatCount.store(totalThreat, std::memory_order_release);
		if (untargetable.empty()) {
			return;
		}

		// An actor already fighting an untargetable one: the selector seat moves it to
		// another target on its next combat update; one with nothing else to fight leaves
		// combat (the StartCombat seat keeps it from re-engaging).
		for (const auto& world : a_world) {
			auto* actor = world.actor;
			if (!actor || actor->IsPlayerRef() || !actor->IsInCombat()) {
				continue;
			}
			const auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
			if (!target || !untargetable.contains(target->GetFormID())) {
				continue;
			}
			bool alternative = false;
			auto* controller = actor->GetActorRuntimeData().combatController;
			if (auto* group = controller ? controller->combatGroup : nullptr) {
				RE::BSReadLockGuard guard(group->lock);
				for (const auto& entry : group->targets) {
					const auto other = entry.targetHandle.get();
					if (other && !untargetable.contains(other->GetFormID()) && !other->IsDead() &&
						!entry.flags.any(RE::CombatTarget::Flags::kTargetLost)) {
						alternative = true;
						break;
					}
				}
			}
			if (!alternative) {
				logger::info("[CombatGate] 0x{:08X} ({}) leaves combat: its only target 0x{:08X} is SCF_Untargetable.",
					world.id, actor->GetName(), target->GetFormID());
				BreakCombat(actor);
			}
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - s_lastUntargetableReport >= std::chrono::seconds(10)) {
			const auto replaced = s_untargetableReplaced.load(std::memory_order_relaxed);
			const auto cleared = s_untargetableCleared.load(std::memory_order_relaxed);
			if (replaced != s_lastReplacedReported || cleared != s_lastClearedReported) {
				logger::info("[CombatGate] last 10 s: {} selector answers moved off an SCF_Untargetable actor, {} cleared (no other target).",
					replaced - s_lastReplacedReported, cleared - s_lastClearedReported);
				s_lastReplacedReported = replaced;
				s_lastClearedReported = cleared;
			}
			s_lastUntargetableReport = now;
		}
	}

	void UpdateForSummon(RE::Actor* a_summon, const core::CombatPolicy& a_policy, const SummonCombatDirectives& a_directives)
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

		// Suppress melee circling/flanking maneuvers for ANY focused summon under an
		// explicit Attack order (all hooked leaves belong to CloseMovement /
		// FlankingMovement, leaving Advance, Chase and RangedMovement untouched).
		const bool focusedWolf = pinnedHandle && !a_policy.radiusManaged;

		// 1. Publish the policy for the StartCombat / CanAttackActor seats and the target pin.
		{
			std::lock_guard lock(s_mutex);
			auto& permission = s_permissions[id];
			const bool pinChanged = permission.pinnedHandle != pinnedHandle || permission.radiusManaged != a_policy.radiusManaged;
			const bool directivesChanged = permission.alwaysNoCircling != a_directives.alwaysNoCircling ||
			                               permission.specialtyFlags != a_directives.specialtyFlags ||
			                               permission.allowCombat != a_policy.allowCombat ||
			                               permission.restrictToTarget != a_policy.restrictToTarget;
			permission.radiusManaged = a_policy.radiusManaged;
			permission.allowCombat = a_policy.allowCombat;
			permission.restrictToTarget = a_policy.restrictToTarget;
			permission.target = a_policy.target;
			permission.alwaysNoCircling = a_directives.alwaysNoCircling;
			permission.ignoreAggressionRadius = a_directives.ignoreAggressionRadius;
			permission.specialtyFlags = a_directives.specialtyFlags;
			permission.ownerId = a_directives.ownerId;
			if (pinChanged || permission.focusedWolf != focusedWolf) {
				permission.focusedWolf = focusedWolf;
				permission.loggedManeuvers = 0;
				permission.pinnedHandle = pinnedHandle;
				permission.pinAppliedLogged = false;
				permission.canAttackOverrideLogged = false;
				if (pinnedHandle) {
					logger::info("[TargetPin] Summon 0x{:08X} pinned to focus 0x{:08X}", id, static_cast<RE::FormID>(a_policy.target));
				} else {
					logger::info("[TargetPin] Summon 0x{:08X} pin released", id);
				}
				RecountPins();
			} else if (directivesChanged) {
				RecountPins();
			}
		}

		// 2. Native attack/cast deny bits while combat is forbidden.
		auto& applied = s_applied[id];
		if (applied.radiusActor && applied.radiusActor != a_summon->GetHandle()) {
			RestoreRadiusFlags(applied);
			applied = {};
		}

		if (a_policy.radiusManaged && !applied.radiusActor) {
			using Flags = RE::Actor::BOOL_FLAGS;
			const auto& flags = a_summon->GetActorRuntimeData().boolFlags;
			applied.radiusActor = a_summon->GetHandle();
			// If SCF already owned the denial, it is not an external baseline.
			applied.previousAttackDisabled = !applied.known && flags.any(Flags::kAttackingDisabled);
			applied.previousCastDisabled = !applied.known && flags.any(Flags::kCastingDisabled);
		} else if (!a_policy.radiusManaged && applied.radiusActor) {
			RestoreRadiusFlags(applied);
			applied = {};
			applied.known = !a_policy.disableAttacks;
		}
		if (!applied.known || applied.attacksDisabled != a_policy.disableAttacks) {
			if (a_policy.radiusManaged && !a_policy.disableAttacks) {
				RestoreRadiusFlags(applied);
			} else {
				a_summon->GetActorRuntimeData().boolFlags.set(
					a_policy.disableAttacks,
					RE::Actor::BOOL_FLAGS::kAttackingDisabled,
					RE::Actor::BOOL_FLAGS::kCastingDisabled);
			}
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
			if (a_liveSummons.contains(a_entry.first)) {
				return false;
			}
			RestoreRadiusFlags(a_entry.second);
			return true;
		});
	}

	void ClearRadiusPolicies()
	{
		{
			std::lock_guard lock(s_mutex);
			std::erase_if(s_permissions, [](const auto& entry) { return entry.second.radiusManaged; });
			RecountPins();
		}
		std::erase_if(s_applied, [](const auto& entry) {
			if (!entry.second.radiusActor) {
				return false;
			}
			RestoreRadiusFlags(entry.second);
			return true;
		});
	}

	void ResetAll()
	{
		{
			std::lock_guard lock(s_mutex);
			s_permissions.clear();
			RecountPins();
			s_untargetable.clear();
			s_highThreat.clear();
			s_lowThreat.clear();
		}
		s_untargetableCount.store(0, std::memory_order_relaxed);
		s_threatCount.store(0, std::memory_order_relaxed);
		s_specialtyCount.store(0, std::memory_order_relaxed);
		for (const auto& [_, applied] : s_applied) {
			RestoreRadiusFlags(applied);
		}
		s_applied.clear();
		logger::info("[CombatGate] Combat policy state reset.");
	}
}
