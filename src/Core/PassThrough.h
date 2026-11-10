#pragma once

#include "Core/EntityID.h"
#include "Core/Settings.h"

#include <cstdint>
#include <vector>

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
	// its living body bones - Biped (8) and BipedNoCC (33). Skyrim.esm's COLL matrix
	// (checked 2026-11-11) lets the controller collide with BipedNoCC but NOT with Biped,
	// so in vanilla only 30/30 and 30/33 ever reach the seat enabled; 8 is kept as a
	// harmless guard for edited layer tables. Anything without
	// a character controller on one side (weapons, projectiles, spells, the world, dead
	// bodies) is never touched, so hits and ragdolls stay vanilla.
	bool IsPassThroughLayerPair(std::uint32_t a_layerA, std::uint32_t a_layerB) noexcept;

	// Which ray hits between pass-through partners are dropped (the ray's filter info
	// carries the caster's system group): only a character-controller ray (layer 30, a
	// controller's own ground/step probe) against the partner's controller (30) - the
	// body such a probe would stand a wolf on. Crosshair / activation (ItemPicker 40),
	// camera, LOS and pick rays keep hitting own summons, so they stay targetable.
	bool IsPassThroughRayPair(std::uint32_t a_rayLayer, std::uint32_t a_targetLayer) noexcept;

	// One published row of the pass-through table: who, and the system group of its
	// character controller (0 = no controller loaded yet).
	struct PassThroughRow
	{
		EntityID id{ InvalidEntityID };
		std::uint32_t systemGroup{ 0 };
	};

	// Havok asks the collision filter only when two bodies START overlapping; a pair that
	// was let in earlier (summon spawned against another before SCF published it, its
	// controller had no group yet, the bypass was off) keeps colliding for as long as the
	// two keep overlapping - a wolf running in the pack can climb onto another one
	// (field report 2026-11-11). The engine therefore asks Havok to re-filter the
	// existing pairs of every actor whose answer may have changed since the last table:
	//   * every actor of both tables when a CollisionSettings switch changed;
	//   * an actor new in the table, or whose system group changed (group != 0);
	//   * an actor that left the table (its pairs fall back to vanilla).
	// Result: unique ids, current-table order first, then the removed ones.
	std::vector<EntityID> RowsToRefilter(const std::vector<PassThroughRow>& a_previous, const CollisionSettings& a_previousSettings,
		const std::vector<PassThroughRow>& a_current, const CollisionSettings& a_currentSettings);
}
