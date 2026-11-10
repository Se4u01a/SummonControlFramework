#include "Engine/FollowLeash.h"
#include "Core/MovementIntent.h"
#include "PCH.h"

#include <RE/A/AIProcess.h>
#include <RE/A/Actor.h>
#include <RE/G/GameSettingCollection.h>
#include <RE/I/IPackageData.h>
#include <RE/T/TESCustomPackageData.h>
#include <RE/T/TESPackage.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace scf::engine::leash
{
	namespace
	{
		constexpr RE::FormID kFollowToTemplate = 0x00025E6D;  // Skyrim.esm PACK "FollowTo"
		constexpr std::int8_t kMinRadiusUid = 4;              // "Min Radius"
		constexpr std::int8_t kMaxRadiusUid = 5;              // "Max Radius:"
		constexpr std::size_t kFloatValueOffset = 0x10;       // BGSPackageDataFloat value
		constexpr float kReleasedRadius = 100000.0f;

		// ------------------------------------------------------------------
		// Creation seat (SE 1.5.97 only).
		//
		// VERIFIED on the unpacked 1.5.97 image (2026-11-11):
		//   * id 36410 (+5DBA80) loads Min/Max from the two GMSTs into xmm7/xmm6 and at
		//     +5DBB04 (= id 36410 + 0x84) does `call +5DD030` (id 36422) with
		//     rcx = the summon, rdx = the commander, r8 = 0, r9 = a location, and on the
		//     stack [rsp+0x20] = Min, [rsp+0x28] = Max, [rsp+0x30] = 0, [rsp+0x38] = 1.
		//     id 36422 returns the created TESPackage* (36410 keeps it in rdi).
		//   * id 36422 has exactly two direct callers: that site and +405622.
		// The field log of 2026-11-11 00:08 proved that the created instance carries
		// templateParent = 0, so the package cannot be recognised by its template; the
		// seat records "summon -> created package" instead, and creates it with the
		// released radii when the summon is under an away order at creation time.
		// AE: no verified site (no AE image on this machine) -> the seat is not installed
		// and recognition falls back to the package's shape.
		// ------------------------------------------------------------------
		constexpr std::uint64_t kCreateCallOffsetSE = 0x84;
		using CreateFollow_t = RE::TESPackage* (*)(RE::Actor*, RE::Actor*, void*, void*, float, float, bool, bool);
		CreateFollow_t s_originalCreate{ nullptr };
		bool s_seatInstalled{ false };

		// Shared with the creation seat (whatever thread builds the package).
		std::mutex s_mutex;
		std::unordered_map<RE::FormID, const RE::TESPackage*> s_createdPackages;  // compared, never dereferenced
		std::unordered_set<RE::FormID> s_releasedSummons;

		RE::TESPackage* CreateFollowThunk(
			RE::Actor* a_summon,
			RE::Actor* a_commander,
			void* a_arg3,
			void* a_arg4,
			float a_minRadius,
			float a_maxRadius,
			bool a_arg7,
			bool a_arg8)
		{
			bool released = false;
			const RE::FormID id = a_summon ? a_summon->GetFormID() : 0;
			if (id != 0) {
				std::lock_guard lock(s_mutex);
				released = s_releasedSummons.contains(id);
			}

			RE::TESPackage* package = s_originalCreate(
				a_summon, a_commander, a_arg3, a_arg4,
				released ? kReleasedRadius : a_minRadius,
				released ? kReleasedRadius : a_maxRadius,
				a_arg7, a_arg8);

			if (id != 0 && package) {
				std::lock_guard lock(s_mutex);
				s_createdPackages[id] = package;
			}
			logger::info("[Leash] FollowTo package 0x{:08X} created for 0x{:08X} (min {:.0f}, max {:.0f}{})",
				package ? package->GetFormID() : 0u, id, a_minRadius, a_maxRadius,
				released ? ", created RELEASED" : "");
			return package;
		}

		struct LeashState
		{
			RE::FormID package{ 0 };
			bool released{ false };
			bool reportedMissing{ false };
		};
		std::unordered_map<core::EntityID, LeashState> s_states;

		struct FollowRadii
		{
			float* min{ nullptr };
			float* max{ nullptr };
			const char* recognisedBy{ "" };
		};

		// Locates the two float inputs of the summon's follow package. Every entry is
		// checked against the BGSPackageDataFloat vtable before its +0x10 is touched.
		FollowRadii FindRadii(RE::Actor* a_summon, RE::TESPackage* a_package)
		{
			FollowRadii radii{};
			if (!a_package || !a_package->data) {
				return radii;
			}
			auto* custom = skyrim_cast<RE::TESCustomPackageData*>(a_package->data);
			if (!custom || !custom->data.data) {
				return radii;
			}

			static const std::uintptr_t floatVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_BGSPackageDataFloat[0] }.address();
			const auto& list = custom->data;

			float* floats[2]{ nullptr, nullptr };
			std::uint32_t floatCount = 0;
			for (std::uint16_t i = 0; i < list.dataSize; ++i) {
				auto* entry = list.data[i];
				if (!entry || *reinterpret_cast<const std::uintptr_t*>(entry) != floatVtable) {
					continue;
				}
				auto* value = reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(entry) + kFloatValueOffset);
				const std::int8_t uid = list.uids ? list.uids[i] : static_cast<std::int8_t>(-1);
				if (uid == kMinRadiusUid) {
					radii.min = value;
				} else if (uid == kMaxRadiusUid) {
					radii.max = value;
				}
				if (floatCount < 2) {
					floats[floatCount] = value;
				}
				++floatCount;
			}

			// Which package is it? 1) the one the creation seat saw being built for this
			// summon; 2) a FollowTo instance by template; 3) the shape of the engine's
			// summon follow package (runtime kPackage with exactly the two radius floats).
			bool created = false;
			{
				std::lock_guard lock(s_mutex);
				const auto it = s_createdPackages.find(a_summon->GetFormID());
				created = it != s_createdPackages.end() && it->second == a_package;
			}
			const bool byTemplate = custom->templateParent && custom->templateParent->GetFormID() == kFollowToTemplate;
			const bool byShape = core::IsSummonFollowPackageShape(
				static_cast<std::uint8_t>(a_package->packData.packType.get()), a_package->GetFormID(), floatCount);

			if (!created && !byTemplate && !byShape) {
				return {};
			}
			radii.recognisedBy = created ? "created" : (byTemplate ? "template" : "shape");

			// The template lists Min before Max; use that order if the uids did not say.
			if ((!radii.min || !radii.max) && floatCount == 2) {
				radii.min = floats[0];
				radii.max = floats[1];
			}
			return radii;
		}

		float GmstOr(const char* a_name, float a_fallback)
		{
			if (auto* collection = RE::GameSettingCollection::GetSingleton()) {
				if (auto* setting = collection->GetSetting(a_name)) {
					return setting->GetFloat();
				}
			}
			return a_fallback;
		}

		void PublishReleased(RE::FormID a_id, bool a_released)
		{
			std::lock_guard lock(s_mutex);
			if (a_released) {
				s_releasedSummons.insert(a_id);
			} else {
				s_releasedSummons.erase(a_id);
			}
		}
	}

	bool Install()
	{
		if (s_seatInstalled) {
			return true;
		}
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_5_97) {
			logger::info("[Leash] FollowTo creation seat not installed: verified on SE 1.5.97 only; recognising the follow package by shape.");
			return false;
		}

		const std::uintptr_t site = REL::ID(36410).address() + kCreateCallOffsetSE;
		const std::uintptr_t callee = REL::ID(36422).address();
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);
		std::int32_t displacement = 0;
		std::memcpy(&displacement, bytes + 1, sizeof(displacement));
		const auto target = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(site + 5) + displacement);
		if (bytes[0] != 0xE8 || target != callee) {
			logger::error("[Leash] FollowTo creation site 0x{:X} is not `call 0x{:X}` (opcode 0x{:02X}, target 0x{:X}); seat refused, recognising by shape.",
				site, callee, bytes[0], target);
			return false;
		}

		s_originalCreate = reinterpret_cast<CreateFollow_t>(callee);
		REL::Relocation<std::uintptr_t> call{ site };
		call.write_call<5>(&CreateFollowThunk);
		s_seatInstalled = true;
		logger::info("[Leash] FollowTo creation seat installed at 0x{:X} (call 0x{:X}).", site, callee);
		return true;
	}

	void UpdateForSummon(RE::Actor* a_summon, bool a_released)
	{
		if (!a_summon) {
			return;
		}
		const auto id = static_cast<core::EntityID>(a_summon->GetFormID());
		auto& state = s_states[id];
		PublishReleased(a_summon->GetFormID(), a_released);

		auto* process = a_summon->GetActorRuntimeData().currentProcess;
		auto* package = process ? process->currentPackage.package : nullptr;
		const auto radii = FindRadii(a_summon, package);
		if (!radii.min || !radii.max) {
			// Not running its follow package (combat, another package, a mod's own summon
			// AI). Nothing to release; say so once so a field log can tell.
			if (a_released && !state.reportedMissing) {
				state.reportedMissing = true;
				auto* custom = (package && package->data) ? skyrim_cast<RE::TESCustomPackageData*>(package->data) : nullptr;
				logger::info("[Leash] Summon 0x{:08X}: running package 0x{:08X} (type {}, template 0x{:08X}, {} data entries) is not its follow package; follow leash not released",
					id,
					package ? package->GetFormID() : 0u,
					package ? static_cast<int>(package->packData.packType.get()) : -1,
					(custom && custom->templateParent) ? custom->templateParent->GetFormID() : 0u,
					custom ? custom->data.dataSize : 0);
			}
			return;
		}
		state.reportedMissing = false;

		// Restoring only ever undoes our own release: a follow package SCF never
		// touched (vanilla values, another mod's tuning) keeps whatever it has.
		if (!a_released && *radii.min != kReleasedRadius && *radii.max != kReleasedRadius) {
			state.released = false;
			state.package = package->GetFormID();
			return;
		}

		const float wantMin = a_released ? kReleasedRadius : GmstOr("fSummonedCreatureMinFollowDist", 200.0f);
		const float wantMax = a_released ? kReleasedRadius : GmstOr("fSummonedCreatureMaxFollowDist", 400.0f);
		const bool changed = *radii.min != wantMin || *radii.max != wantMax;
		if (!changed) {
			state.released = a_released;
			state.package = package->GetFormID();
			return;
		}

		const float oldMin = *radii.min;
		const float oldMax = *radii.max;
		*radii.min = wantMin;
		*radii.max = wantMax;

		// Logged on a state flip or when the engine handed the summon a new follow
		// package (the release is re-applied to it), not on every tick.
		if (state.released != a_released || state.package != package->GetFormID()) {
			logger::info("[Leash] Summon 0x{:08X} follow leash {} on package 0x{:08X} (recognised by {}): min {:.0f} -> {:.0f}, max {:.0f} -> {:.0f}",
				id, a_released ? "RELEASED" : "restored", package->GetFormID(), radii.recognisedBy, oldMin, wantMin, oldMax, wantMax);
		}
		state.released = a_released;
		state.package = package->GetFormID();
	}

	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons)
	{
		std::erase_if(s_states, [&a_liveSummons](const auto& a_entry) {
			return a_liveSummons.find(a_entry.first) == a_liveSummons.end();
		});
		std::lock_guard lock(s_mutex);
		// s_createdPackages is NOT pruned by the roster: the package is created before the
		// engine lists the summon in commandedActors. It holds one pointer per actor that
		// ever got a follow package (compared, never dereferenced) and is cleared on load.
		std::erase_if(s_releasedSummons, [&a_liveSummons](RE::FormID a_id) {
			return a_liveSummons.find(static_cast<core::EntityID>(a_id)) == a_liveSummons.end();
		});
	}

	void ResetAll()
	{
		s_states.clear();
		std::lock_guard lock(s_mutex);
		s_createdPackages.clear();
		s_releasedSummons.clear();
	}
}
