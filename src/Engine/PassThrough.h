#pragma once

#include "Core/PassThrough.h"
#include "Core/Settings.h"
#include "Engine/KeywordApi.h"

#include <vector>

namespace RE
{
	class Actor;
}

namespace scf::engine::passthrough
{
	// Summons of one owner run straight through each other (and through their owner):
	// they neither plan/steer around each other nor block each other's bodies.
	//
	// Two seats, both verified on the unpacked SkyrimSE 1.5.97 image (2026-11-11) and
	// installed only on that runtime:
	//
	//   1. MOVEMENT AVOIDANCE. MovementAvoidanceManager (vtable id 262446 -> +1668D28)
	//      slot 4 (id 40659, +6EBFD0) is `bool ShouldAvoid(this, FormID self, FormID other)`
	//      (vanilla: true, except "other is the player" asks whether self detects him).
	//      Every avoidance decision asks it FIRST and stops on false:
	//        * id 90637 (+112D140), called by the local actor avoider
	//          MovementAgentActorAvoider (+11099C5, +1109C7F, +110A04F);
	//        * id 90636 (+112CFF0), the MovementActorAvoidanceParameters check that also
	//          carries BSPathingRequest's avoidance parameters.
	//      Both call `[vtable+0x20]` on the IMovementAvoidanceManager singleton with
	//      edx = self, r8d = other. The seat answers false for pass-through pairs.
	//
	//   2. BODY COLLISION. bhkCollisionFilter's hkpCollidableCollidableFilter sub-vtable
	//      (VTABLE_bhkCollisionFilter[1], id 289509 -> +179E3B0) slot 1 (id 76676,
	//      +DD6780). REAL ABI: `hkBool* IsCollisionEnabled(this, hkBool* out,
	//      const hkpCollidable* a, const hkpCollidable* b)` - a hidden return slot
	//      (CommonLib declares `bool (a, b)`, which is wrong). It reads each collidable's
	//      filter info at +0x2C. The seat clears *out for two CharController-layer (30)
	//      collidables whose system groups belong to a pass-through pair. The groups are
	//      published from the main thread (Actor::GetCollisionFilterInfo, id 36559: the
	//      character controller's filter info), so the physics thread only compares two
	//      integers from the collidables it was handed.
	//
	//   3. EXISTING PAIRS. Havok asks seat 2 only when two bodies start overlapping, so
	//      after each table swap Publish() re-filters the controller bodies of actors whose
	//      answer may have changed (core::RowsToRefilter) through bhkWorld::
	//      UpdateCollisionFilter (id 76091), the call vanilla NiAVObject::SetCollisionLayer
	//      ends in. Details in the .cpp.
	//
	// Threads: Publish() is main-thread; both seats run on AI / physics worker threads
	// and only read the published table under a shared lock.

	bool Install();

	struct Member
	{
		core::PassThroughParty party;
		RE::Actor* actor{ nullptr };  // main thread only, for the filter-info read
	};

	// Main thread, every tactical tick: the owner and its live summons (SCF rule), plus
	// every loaded actor with its keyword flags and leader (keyword API; empty while no
	// mod defines the keywords). An SCF_Untargetable actor's hostile set is computed
	// here (IsHostileToActor in either direction).
	void Publish(const std::vector<Member>& a_members, const std::vector<keywords::WorldActor>& a_world, const core::CollisionSettings& a_settings);

	void ResetAll();
}
