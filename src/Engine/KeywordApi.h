#pragma once

#include "Core/EntityID.h"

#include <cstdint>
#include <vector>

namespace RE
{
	class Actor;
	class PlayerCharacter;
}

namespace scf::engine::keywords
{
	// Keyword API (see core/KeywordApi.h for the names and README "Keyword API").
	// Load() runs at kDataLoaded: every BGSKeyword whose EditorID is one of the API names
	// (any plugin, several records per name allowed) is remembered per flag. No such
	// keyword in the load order -> Active() is false and nothing is scanned.
	void Load();
	[[nodiscard]] bool Active() noexcept;

	// Main thread. Flags of an actor: keyword on its base (TESObjectREFR::HasKeyword
	// through the base form), on its race, or on the base effect of an active magic
	// effect (Actor::HasMagicEffectWithKeyword, id 19220: GetMagicTarget()->
	// MagicTarget::HasMagicEffectWithKeyword).
	[[nodiscard]] std::uint8_t FlagsOf(RE::Actor* a_actor);

	// Main thread. Head of the actor's side: the player for the player and the player's
	// teammates, a commanded actor's (summon's) commander's leader, else the actor.
	[[nodiscard]] core::EntityID LeaderOf(RE::Actor* a_actor);

	// One loaded actor as the keyword rules see it this tick.
	struct WorldActor
	{
		RE::Actor* actor{ nullptr };
		core::EntityID id{ core::InvalidEntityID };
		core::EntityID leader{ core::InvalidEntityID };
		std::uint8_t flags{ 0 };
	};

	// Main thread, once per tactical tick: the player and every live, 3D-loaded actor of
	// the high process list with its flags and leader. Empty while the API is idle.
	[[nodiscard]] std::vector<WorldActor> ScanWorld(RE::PlayerCharacter* a_player);
}
