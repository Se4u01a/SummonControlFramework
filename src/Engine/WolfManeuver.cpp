#include "PCH.h"
#include "Core/WolfManeuver.h"
#include "Engine/CombatGate.h"
#include "Engine/WolfManeuver.h"

#include <RE/C/CombatBehaviorController.h>
#include <RE/C/CombatBehaviorThread.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>

namespace scf::engine::wolf
{
	bool UsesWolfSkeleton(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		const auto* race = a_actor->GetRace();
		const auto* base = a_actor->GetActorBase();
		const auto* skeleton = race && base ? race->skeletonModels[base->GetSex() == RE::SEX::kFemale ? 1 : 0].GetModel() : nullptr;
		if (!skeleton || !skeleton[0]) {
			return false;
		}
		std::string lower(skeleton);
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lower.find("canine") != std::string::npos || lower.find("wolf") != std::string::npos;
	}

	namespace
	{
		using EnterStep = void* (*)(RE::CombatBehaviorTreeNode*, RE::CombatBehaviorThread*);
		using ExitStep = void (*)(RE::CombatBehaviorTreeNode*, RE::CombatBehaviorThread*);
		struct Leaf
		{
			const char* name;
			REL::VariantID table;
			EnterStep enter{};
			ExitStep exit{};
		};
		// Strictly CloseMovement and FlankingMovement circling/retreating leaves.
		// Direct approach leaves (Advance, Chase) and RangedMovement leaves
		// (MaintainOptimalRange, Strafe, FallbackToRanged) remain untouched.
		Leaf leaves[]{
			{ "Circle", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorCircle_[0] },
			{ "CircleDistant", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorCircleDistant_[0] },
			{ "Surround", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorSurround_[0] },
			{ "Flank", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorFlank_[0] },
			{ "FlankDistant", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorFlankDistant_[0] },
			{ "Stalk", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorStalk_[0] },
			{ "Fallback", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorFallback_[0] },
			{ "Reposition", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorReposition_[0] },
			{ "Backoff", RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorBackoff_[0] },
			{ "WatchTarget", RE::VTABLE_CombatBehaviorTreeNodeObject1_CombatBehaviorWatchTarget_float_[0] }
		};
		EnterStep failEnter{};
		ExitStep failExit{};
		thread_local core::WolfManeuverFrame pending;
		std::atomic<bool> enabled{ true };

		template <std::size_t Index>
		void* Enter(RE::CombatBehaviorTreeNode* node, RE::CombatBehaviorThread* thread)
		{
			if (enabled.load(std::memory_order_relaxed) && combat::HasFocusedWolf() && !pending.node &&
			    thread && thread->controller) {
				const auto* controller = thread->controller->combatController;
				if (controller && combat::BlocksWolfManeuver(*controller, Index, leaves[Index].name) &&
				    pending.Begin(node, thread)) {
					failEnter(node, thread);
					return thread;
				}
			}
			return leaves[Index].enter(node, thread);
		}

		template <std::size_t Index>
		void Exit(RE::CombatBehaviorTreeNode* node, RE::CombatBehaviorThread* thread)
		{
			if (pending.Finish(node, thread)) {
				failExit(node, thread);
				return;
			}
			leaves[Index].exit(node, thread);
		}

		template <std::size_t Index>
		void Hook()
		{
			auto& leaf = leaves[Index];
			REL::Relocation<std::uintptr_t> vt{ leaf.table };
			const auto* steps = reinterpret_cast<const std::uintptr_t*>(vt.address());
			leaf.enter = reinterpret_cast<EnterStep>(steps[2]);
			leaf.exit = reinterpret_cast<ExitStep>(steps[3]);
			vt.write_vfunc(3, &Exit<Index>);
			vt.write_vfunc(2, &Enter<Index>);
		}
	}

	void SetFrameworkEnabled(bool a_enabled)
	{
		enabled.store(a_enabled, std::memory_order_relaxed);
	}

	void InstallManeuverHooks()
	{
		if (failEnter) {
			return;
		}
		const auto& module = REL::Module::get();
		const bool se = module.version() == SKSE::RUNTIME_SSE_1_5_97;
		if (!se && module.version() != REL::Version{ 1, 6, 1170, 0 }) {
			logger::warn("[WolfManeuver] Unsupported runtime, using native maneuvers.");
			return;
		}
		// Native Enter allocates a different-sized frame for each leaf. ForceFail
		// immediately ascends; its matching Exit releases its own four-byte frame.
		// Use only the verified original pair (APMF BehaviorTreeCatalog).
		const auto* fail = reinterpret_cast<const std::uintptr_t*>(
			RE::VTABLE_CombatBehaviorTreeNodeObject_CombatBehaviorForceFail_[0].address());
		if (fail[2] != module.base() + (se ? 0x8218A0 : 0x8B9D90) ||
		    fail[3] != module.base() + (se ? 0x821950 : 0x8B9E50)) {
			logger::warn("[WolfManeuver] ForceFail slots changed, using native maneuvers.");
			return;
		}
		failEnter = reinterpret_cast<EnterStep>(fail[2]);
		failExit = reinterpret_cast<ExitStep>(fail[3]);
		[]<std::size_t... I>(std::index_sequence<I...>) { (Hook<I>(), ...); }(
			std::make_index_sequence<std::size(leaves)>{});
		logger::info("[WolfManeuver] Installed focused-attack maneuver filter ({} leaves).", std::size(leaves));
	}
}
