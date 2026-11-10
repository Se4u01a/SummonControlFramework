#include "Engine/FollowLeash.h"
#include "PCH.h"

#include <RE/A/AIProcess.h>
#include <RE/A/Actor.h>
#include <RE/G/GameSettingCollection.h>
#include <RE/I/IPackageData.h>
#include <RE/T/TESCustomPackageData.h>
#include <RE/T/TESPackage.h>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace scf::engine::leash
{
	namespace
	{
		constexpr RE::FormID kFollowToTemplate = 0x00025E6D;  // Skyrim.esm PACK "FollowTo"
		constexpr std::int8_t kMinRadiusUid = 4;              // "Min Radius"
		constexpr std::int8_t kMaxRadiusUid = 5;              // "Max Radius:"
		constexpr std::size_t kFloatValueOffset = 0x10;       // BGSPackageDataFloat value
		constexpr float kReleasedRadius = 100000.0f;

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
		};

		// Locates the two float inputs of a FollowTo instance. Every entry is checked
		// against the BGSPackageDataFloat vtable before its +0x10 is touched.
		FollowRadii FindRadii(RE::TESPackage* a_package)
		{
			FollowRadii radii{};
			if (!a_package || !a_package->data) {
				return radii;
			}
			auto* custom = skyrim_cast<RE::TESCustomPackageData*>(a_package->data);
			if (!custom || !custom->templateParent || custom->templateParent->GetFormID() != kFollowToTemplate) {
				return radii;
			}

			static const std::uintptr_t floatVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_BGSPackageDataFloat[0] }.address();
			const auto& list = custom->data;
			if (!list.data) {
				return radii;
			}

			float* firstFloat = nullptr;
			float* secondFloat = nullptr;
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
				if (!firstFloat) {
					firstFloat = value;
				} else if (!secondFloat) {
					secondFloat = value;
				}
			}
			// The template lists Min before Max; use that order if the instance carries no uids.
			if (!radii.min && !radii.max && firstFloat && secondFloat) {
				radii.min = firstFloat;
				radii.max = secondFloat;
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
	}

	void UpdateForSummon(RE::Actor* a_summon, bool a_released)
	{
		if (!a_summon) {
			return;
		}
		const auto id = static_cast<core::EntityID>(a_summon->GetFormID());
		auto& state = s_states[id];

		auto* process = a_summon->GetActorRuntimeData().currentProcess;
		auto* package = process ? process->currentPackage.package : nullptr;
		const auto radii = FindRadii(package);
		if (!radii.min || !radii.max) {
			// Not running its FollowTo instance (combat, another package, a mod's own
			// summon AI). Nothing to release; say so once so a field log can tell.
			if (a_released && !state.reportedMissing) {
				state.reportedMissing = true;
				auto* custom = (package && package->data) ? skyrim_cast<RE::TESCustomPackageData*>(package->data) : nullptr;
				logger::info("[Leash] Summon 0x{:08X}: running package 0x{:08X} (type {}, template 0x{:08X}) is not a FollowTo instance with two radius floats; follow leash not released",
					id,
					package ? package->GetFormID() : 0u,
					package ? static_cast<int>(package->packData.packType.get()) : -1,
					(custom && custom->templateParent) ? custom->templateParent->GetFormID() : 0u);
			}
			return;
		}
		state.reportedMissing = false;

		// Restoring only ever undoes our own release: a FollowTo instance SCF never
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

		// Logged on a state flip or when the engine handed the summon a new FollowTo
		// instance (the release is re-applied to it), not on every tick.
		if (state.released != a_released || state.package != package->GetFormID()) {
			logger::info("[Leash] Summon 0x{:08X} follow leash {} on package 0x{:08X}: min {:.0f} -> {:.0f}, max {:.0f} -> {:.0f}",
				id, a_released ? "RELEASED" : "restored", package->GetFormID(), oldMin, wantMin, oldMax, wantMax);
		}
		state.released = a_released;
		state.package = package->GetFormID();
	}

	void RetainOnly(const std::unordered_set<core::EntityID>& a_liveSummons)
	{
		std::erase_if(s_states, [&a_liveSummons](const auto& a_entry) {
			return a_liveSummons.find(a_entry.first) == a_liveSummons.end();
		});
	}

	void ResetAll()
	{
		s_states.clear();
	}
}
