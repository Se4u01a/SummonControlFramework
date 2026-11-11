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
	// Keyword API flags (src/Core/KeywordApi.h names the keywords): read on the main
	// thread from the actor (base, race) or an active magic effect.
	enum PassThroughFlag : std::uint8_t
	{
		kPassThroughFriends = 1 << 0,  // SCF_PassThroughFriends: through everyone of its own side
		kPassThroughAll = 1 << 1,      // SCF_PassThroughAll: through every actor
		kUntargetable = 1 << 2         // SCF_Untargetable: hostile actors neither target nor block it
	};

	// A party is any actor in the published table. SCF's own rule uses ownerId (a
	// managed summon: its owner; the owner itself and every other actor: Invalid). The
	// keyword rules use leaderId - the head of the actor's side: a summon's commanding
	// actor's leader, the player for the player's teammates, else the actor itself - and
	// hostileIds, filled for an untargetable party with the actors hostile to it.
	struct PassThroughParty
	{
		EntityID id{ InvalidEntityID };
		EntityID ownerId{ InvalidEntityID };
		EntityID leaderId{ InvalidEntityID };
		std::uint8_t flags{ 0 };
		std::vector<EntityID> hostileIds;

		[[nodiscard]] bool IsSummon() const noexcept { return ownerId != InvalidEntityID; }
		[[nodiscard]] bool Has(PassThroughFlag a_flag) const noexcept { return (flags & a_flag) != 0; }
	};

	// One side is untargetable and the other is hostile to it.
	bool IsUntargetablePair(const PassThroughParty& a_lhs, const PassThroughParty& a_rhs) noexcept;

	// Rules, first match wins (never for the same actor or an invalid id):
	//   Keyword API (independent of the INI switches):
	//   * either side SCF_PassThroughAll                         -> passes.
	//   * either side SCF_Untargetable and the pair is hostile   -> passes.
	//   * either side SCF_PassThroughFriends and same leader     -> passes.
	//   SCF summons (CollisionSettings, INI [Collision]):
	//   * bEnableCollisionBypass = false            -> no SCF pass-through.
	//   * summon <-> summon of the same owner       -> passes, if bEnableSummonToSummonBypass.
	//   * summon <-> its own owner                  -> passes.
	//   * everything else -> vanilla.
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

	// One published row of the pass-through table: who, the system group of its
	// character controller (0 = no controller loaded yet), whether any rule can apply to
	// it (an SCF summon or owner, a keyword flag) and a signature of everything a rule
	// reads (flags, leader, hostile set) - a change re-filters it.
	struct PassThroughRow
	{
		EntityID id{ InvalidEntityID };
		std::uint32_t systemGroup{ 0 };
		bool relevant{ true };
		std::uint64_t signature{ 0 };
	};

	// Signature of a party's rule inputs (order-independent over hostileIds).
	std::uint64_t PassThroughSignature(const PassThroughParty& a_party) noexcept;

	// Havok asks the collision filter only when two bodies START overlapping; a pair that
	// was let in earlier (summon spawned against another before SCF published it, its
	// controller had no group yet, the bypass was off) keeps colliding for as long as the
	// two keep overlapping - a wolf running in the pack can climb onto another one
	// (field report 2026-11-11). The engine therefore asks Havok to re-filter the
	// existing pairs of every actor whose answer may have changed since the last table:
	//   * every relevant actor of both tables when a CollisionSettings switch changed;
	//   * a relevant actor new in the table, or whose system group or signature changed,
	//     or that just became relevant (group != 0);
	//   * an actor that was relevant and left the table or stopped being relevant (its
	//     pairs fall back to vanilla).
	// Plain actors (no rule can apply) are never re-filtered: the keyword table holds
	// every loaded actor, and only the ones a rule names need Havok's attention.
	// Result: unique ids, current-table order first, then the removed ones.
	std::vector<EntityID> RowsToRefilter(const std::vector<PassThroughRow>& a_previous, const CollisionSettings& a_previousSettings,
		const std::vector<PassThroughRow>& a_current, const CollisionSettings& a_currentSettings);
}
