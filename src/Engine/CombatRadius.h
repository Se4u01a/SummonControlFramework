#pragma once

#include "Core/CombatRadius.h"
#include "Engine/CombatGate.h"
#include "Engine/KeywordApi.h"

#include <span>
#include <unordered_map>

namespace RE
{
	class Actor;
	class EffectSetting;
}

namespace scf::engine::radius
{
	void Load();
	void ConsumeMenu();
	void BeginTick();
	float Resolve(RE::Actor& a_summon, const RE::EffectSetting* a_source = nullptr);
	bool UsesCollisionBypass(bool a_hasTaggedSummon);
	const core::CombatRadiusSettings& Settings();
	void PublishEnemies(RE::Actor& a_player, std::span<const keywords::WorldActor> a_world);
	core::CombatPolicy Update(
		RE::Actor& a_summon,
		const core::SummonModel& a_model,
		float a_radius,
		float a_now,
		const combat::SummonCombatDirectives& a_directives = {});
	void FinishTick(const std::unordered_map<core::EntityID, float>& a_tagged);
	void Reset();
}
