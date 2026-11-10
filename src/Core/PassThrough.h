#pragma once

#include "Core/EntityID.h"
#include "Core/Settings.h"

#include <cstdint>

namespace scf::core
{
	// "Pass-through" pairs: two actors that must neither steer/path around each other
	// nor block each other physically. Pure rule, shared by the two engine seats in
	// src/Engine/PassThrough.cpp (movement avoidance and the Havok collision filter) and
	// the unit tests.
	//
	// A party is a managed summon (ownerId = its owner) or the owner itself
	// (ownerId = InvalidEntityID). Actors unknown to SCF are never parties.
	struct PassThroughParty
	{
		EntityID id{ InvalidEntityID };
		EntityID ownerId{ InvalidEntityID };

		[[nodiscard]] bool IsSummon() const noexcept { return ownerId != InvalidEntityID; }
	};

	// Rules (CollisionSettings, INI [Collision]):
	//   * bEnableCollisionBypass = false            -> nothing passes through (vanilla).
	//   * summon <-> summon of the same owner       -> passes, if bEnableSummonToSummonBypass.
	//   * summon <-> its own owner                  -> passes.
	//   * everything else (other owners' summons, enemies, NPCs, self) -> vanilla.
	bool ShouldPassThrough(const PassThroughParty& a_lhs, const PassThroughParty& a_rhs, const CollisionSettings& a_settings) noexcept;
}
