#include "Engine/PassThrough.h"
#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/C/CFilter.h>
#include <RE/H/hkpCollidable.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <vector>

namespace scf::engine::passthrough
{
	namespace
	{
		constexpr std::uint32_t kCharControllerLayer = 30;  // COL_LAYER::kCharController
		constexpr std::size_t kCollidableFilterInfo = 0x2C;  // hkpCollidable broadPhaseHandle.collisionFilterInfo
		static_assert(offsetof(RE::hkpCollidable, broadPhaseHandle) + offsetof(RE::hkpTypedBroadPhaseHandle, collisionFilterInfo) == kCollidableFilterInfo);

		constexpr std::size_t kShouldAvoidSlot = 4;
		constexpr std::size_t kIsCollisionEnabledSlot = 1;

		struct Entry
		{
			core::PassThroughParty party;
			std::uint32_t systemGroup{ 0 };  // 0 = unknown (no character controller)
		};

		std::shared_mutex s_tableLock;
		std::vector<Entry> s_table;
		core::CollisionSettings s_settings{};
		std::atomic<bool> s_hasSummons{ false };

		std::atomic<std::uint64_t> s_avoidSkipped{ 0 };
		std::atomic<std::uint64_t> s_collisionSkipped{ 0 };
		std::atomic<std::uint64_t> s_bonesSkipped{ 0 };
		std::atomic<bool> s_bonesObserved{ false };
		std::uint64_t s_lastBonesReported{ 0 };
		std::atomic<bool> s_avoidObserved{ false };
		std::atomic<bool> s_filterObserved{ false };
		std::uint64_t s_lastAvoidReported{ 0 };
		std::uint64_t s_lastCollisionReported{ 0 };
		std::chrono::steady_clock::time_point s_lastReport{};

		const Entry* FindById(RE::FormID a_id)
		{
			for (const auto& entry : s_table) {
				if (entry.party.id == a_id) {
					return &entry;
				}
			}
			return nullptr;
		}

		const Entry* FindByGroup(std::uint32_t a_group)
		{
			if (a_group == 0) {
				return nullptr;
			}
			for (const auto& entry : s_table) {
				if (entry.systemGroup == a_group) {
					return &entry;
				}
			}
			return nullptr;
		}

		// ---------------------------------------------------------------- seat 1
		struct ShouldAvoidHook
		{
			static bool thunk(void* a_this, RE::FormID a_self, RE::FormID a_other)
			{
				if (!s_avoidObserved.exchange(true, std::memory_order_relaxed)) {
					logger::info("[PassThrough] movement avoidance seat OBSERVED running (first ShouldAvoid call).");
				}
				if (s_hasSummons.load(std::memory_order_relaxed)) {
					bool pass = false;
					{
						std::shared_lock lock(s_tableLock);
						const auto* self = FindById(a_self);
						const auto* other = self ? FindById(a_other) : nullptr;
						pass = other && core::ShouldPassThrough(self->party, other->party, s_settings);
					}
					if (pass) {
						s_avoidSkipped.fetch_add(1, std::memory_order_relaxed);
						return false;
					}
				}
				return func(a_this, a_self, a_other);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---------------------------------------------------------------- seat 2
		struct CollisionFilterHook
		{
			static std::uint8_t* thunk(void* a_this, std::uint8_t* a_out, const RE::hkpCollidable* a_lhs, const RE::hkpCollidable* a_rhs)
			{
				std::uint8_t* result = func(a_this, a_out, a_lhs, a_rhs);
				if (!result || *result == 0 || !a_lhs || !a_rhs || !s_hasSummons.load(std::memory_order_relaxed)) {
					return result;
				}

				const auto infoL = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a_lhs) + kCollidableFilterInfo);
				const auto infoR = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a_rhs) + kCollidableFilterInfo);
				const auto layerL = infoL & 0x7F;
				const auto layerR = infoR & 0x7F;
				if (!core::IsPassThroughLayerPair(layerL, layerR)) {
					return result;
				}
				// Controller vs controller, or controller vs the other actor's body bones
				// (the bones share the actor's system group with its controller; standing
				// on another wolf is a controller landing on those bones).
				const bool bonesPair = layerL != kCharControllerLayer || layerR != kCharControllerLayer;
				if (!s_filterObserved.exchange(true, std::memory_order_relaxed)) {
					logger::info("[PassThrough] collision filter seat OBSERVED a CharController pair.");
				}

				bool pass = false;
				{
					std::shared_lock lock(s_tableLock);
					const auto* lhs = FindByGroup(infoL >> 16);
					const auto* rhs = lhs ? FindByGroup(infoR >> 16) : nullptr;
					pass = rhs && core::ShouldPassThrough(lhs->party, rhs->party, s_settings);
				}
				if (pass) {
					*result = 0;
					if (bonesPair) {
						s_bonesSkipped.fetch_add(1, std::memory_order_relaxed);
						if (!s_bonesObserved.exchange(true, std::memory_order_relaxed)) {
							logger::info("[PassThrough] first controller<->body-bones pair disabled (layers {} / {}): a summon can no longer stand on another.",
								layerL, layerR);
						}
					} else {
						s_collisionSkipped.fetch_add(1, std::memory_order_relaxed);
					}
				}
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Refuse a slot that holds a different function of SkyrimSE.exe (wrong id /
		// wrong runtime); accept the verified function or another plugin's hook on it
		// (outside the game image), which keeps the same ABI and is chained.
		bool SlotLooksRight(std::uintptr_t a_vtable, std::size_t a_slot, std::uintptr_t a_expected, const char* a_name)
		{
			const auto current = *reinterpret_cast<const std::uintptr_t*>(a_vtable + a_slot * sizeof(void*));
			if (current == a_expected) {
				return true;
			}
			const auto& module = REL::Module::get();
			const auto imageBegin = module.base();
			const auto imageEnd = imageBegin + module.segment(REL::Segment::textx).offset() + module.segment(REL::Segment::textx).size();
			if (current < imageBegin || current >= imageEnd) {
				logger::warn("[PassThrough] {} slot already hooked by another plugin (0x{:X}); chaining.", a_name, current);
				return true;
			}
			logger::error("[PassThrough] {} slot holds 0x{:X}, expected 0x{:X}; seat refused.", a_name, current, a_expected);
			return false;
		}
	}

	bool Install()
	{
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_5_97) {
			logger::info("[PassThrough] seats verified on SE 1.5.97 only; not installed on this runtime.");
			return false;
		}

		bool any = false;

		REL::Relocation<std::uintptr_t> avoidVtable{ RE::VTABLE_MovementAvoidanceManager[0] };
		if (SlotLooksRight(avoidVtable.address(), kShouldAvoidSlot, REL::ID(40659).address(), "MovementAvoidanceManager::ShouldAvoid")) {
			ShouldAvoidHook::func = avoidVtable.write_vfunc(kShouldAvoidSlot, ShouldAvoidHook::thunk);
			logger::info("[PassThrough] MovementAvoidanceManager::ShouldAvoid (slot {}) hooked, chaining (original 0x{:X}).",
				kShouldAvoidSlot, ShouldAvoidHook::func.address());
			any = true;
		}

		REL::Relocation<std::uintptr_t> filterVtable{ RE::VTABLE_bhkCollisionFilter[1] };
		if (SlotLooksRight(filterVtable.address(), kIsCollisionEnabledSlot, REL::ID(76676).address(), "bhkCollisionFilter::IsCollisionEnabled")) {
			CollisionFilterHook::func = filterVtable.write_vfunc(kIsCollisionEnabledSlot, CollisionFilterHook::thunk);
			logger::info("[PassThrough] bhkCollisionFilter::IsCollisionEnabled(collidable, collidable) (slot {}) hooked, chaining (original 0x{:X}).",
				kIsCollisionEnabledSlot, CollisionFilterHook::func.address());
			any = true;
		}
		return any;
	}

	void Publish(const std::vector<Member>& a_members, const core::CollisionSettings& a_settings)
	{
		std::vector<Entry> table;
		table.reserve(a_members.size());
		bool hasSummons = false;
		for (const auto& member : a_members) {
			Entry entry{ member.party, 0 };
			if (member.actor) {
				RE::CFilter info{};
				member.actor->GetCollisionFilterInfo(info);
				entry.systemGroup = info.GetSystemGroup();
			}
			hasSummons = hasSummons || member.party.IsSummon();
			table.push_back(entry);
		}

		{
			std::unique_lock lock(s_tableLock);
			s_table = std::move(table);
			s_settings = a_settings;
		}
		s_hasSummons.store(hasSummons, std::memory_order_relaxed);

		// A summary every 10 s while something was let through, so a field log shows the
		// seats acting without a line per physics step.
		const auto now = std::chrono::steady_clock::now();
		if (now - s_lastReport >= std::chrono::seconds(10)) {
			const auto avoid = s_avoidSkipped.load(std::memory_order_relaxed);
			const auto collision = s_collisionSkipped.load(std::memory_order_relaxed);
			const auto bones = s_bonesSkipped.load(std::memory_order_relaxed);
			if (avoid != s_lastAvoidReported || collision != s_lastCollisionReported || bones != s_lastBonesReported) {
				logger::info("[PassThrough] last 10 s: {} avoidance checks, {} controller collisions and {} controller<->bones collisions skipped between own summons/owner (totals {} / {} / {}).",
					avoid - s_lastAvoidReported, collision - s_lastCollisionReported, bones - s_lastBonesReported, avoid, collision, bones);
				s_lastAvoidReported = avoid;
				s_lastCollisionReported = collision;
				s_lastBonesReported = bones;
			}
			s_lastReport = now;
		}
	}

	void ResetAll()
	{
		{
			std::unique_lock lock(s_tableLock);
			s_table.clear();
		}
		s_hasSummons.store(false, std::memory_order_relaxed);
	}
}
