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

	// Which collision-layer pairs the body seat may disable (Havok COL_LAYER numbers):
	// an actor's character controller (30) against the other actor's controller (30) or
	// its living body bones - Biped (8) and BipedNoCC (33). The bones are what a wolf
	// lands on when it jumps onto another one (field report 2026-11-11). Anything without
	// a character controller on one side (weapons, projectiles, spells, the world, dead
	// bodies) is never touched, so hits and ragdolls stay vanilla.
	bool IsPassThroughLayerPair(std::uint32_t a_layerA, std::uint32_t a_layerB) noexcept;
}
