#pragma once

#include "Core/AttackTrace.h"
#include "Core/EntityID.h"

namespace RE
{
	class Actor;
}

namespace scf::core
{
	class SummonRegistry;
}

namespace scf::engine::attack_trace
{
	void Begin(RE::Actor& a_summon, RE::Actor& a_target, const core::AttackTraceOrigin& a_origin);
	void ObserveHit(core::EntityID a_summon, core::EntityID a_target, core::EntityID a_source);
	void ObserveCast(core::EntityID a_summon, core::EntityID a_spell);
	void Tick(core::SummonRegistry& a_registry);
	void Reset(const char* a_reason);
}
