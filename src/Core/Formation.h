#pragma once

#include "Core/Types.h"

#include <cstddef>

namespace scf::core
{
	// Formation slot geometry shared by the tactical squad coordinator (AI) and the
	// engine-facing movement driver.
	//
	// Offsets are expressed in the OWNER's body space: +X is the owner's right,
	// +Y is forward, +Z is up. That is the convention Papyrus
	// Actor.KeepOffsetFromActor documents for its afOffsetX/Y/Z arguments, and it is
	// the space the engine primitive consumes at runtime.
	//
	// Bearings are degrees measured from "directly in front" towards the owner's
	// right; +/-180 is directly behind. The world conversion is the one the squad
	// coordinator already uses: world heading = owner yaw + bearing, sin -> X,
	// cos -> Y.
	//
	// Shape of the squad:
	//   * 1..4 summons keep the original hand-tuned arc (left-rear default, mirrored
	//     flanks, rear marker): they are the historical layout and must not move.
	//   * larger squads fill the OUTERMOST ring first and then further rings inward,
	//     so slot indices stay stable while the army grows (slot 3 of a squad of 6 has
	//     the same place in a squad of 10). Rings are concentric rear arcs, not a
	//     single widening fan: an arc spread over 6+ bodies stacks neighbours inside
	//     each other's bodies.
	struct FormationLayout
	{
		// Slots per ring. Every ring holds at most this many slots and the outermost
		// ring is filled first, so with the default 6 a squad of 7..12 uses two rings.
		std::size_t outerRingCapacity{ 6 };

		// Radius of ring `r` is `a_radius * innerRingScale^r` (ring 0 is outermost).
		// Clamped to [0.1, 1.0] so a misconfigured value cannot collapse the layout.
		float innerRingScale{ 0.62f };

		// Smallest distance allowed between any two slots, in world units (a summon's
		// footprint is roughly 50 units wide). When the requested radius cannot keep
		// neighbours that far apart, the whole layout is expanded uniformly - same
		// bearings, same ring ratios - until it can. 0 disables the guarantee and uses
		// the layout as computed.
		float minSlotDistance{ 50.0f };

		// Upper bound of that uniform expansion, as a multiple of the requested radius.
		// Guards against a tiny formation distance turning an army into a huge crowd
		// (and against a degenerate layout producing an infinite one).
		float maxExpansion{ 4.0f };
	};

	// Number of concentric rings a squad of `a_squadSize` occupies (>= 1).
	std::size_t FormationRingCount(std::size_t a_squadSize, const FormationLayout& a_layout = {}) noexcept;

	// Ring (0 = outermost) of a slot; out-of-range slot indices wrap around the squad
	// just like FormationSlotBearingDeg does.
	std::size_t FormationSlotRing(std::size_t a_slotIndex, std::size_t a_squadSize, const FormationLayout& a_layout = {}) noexcept;

	// Radius of `a_ring` as a fraction of the requested formation radius.
	float FormationRingRadiusScale(std::size_t a_ring, const FormationLayout& a_layout = {}) noexcept;

	// Slot bearing (degrees) in the ring layout, including the uniform expansion a
	// positive FormationLayout::minSlotDistance may cause.
	float FormationSlotBearingDeg(std::size_t a_slotIndex, std::size_t a_squadSize, const FormationLayout& a_layout) noexcept;
	float FormationSlotBearingDeg(std::size_t a_slotIndex, std::size_t a_squadSize) noexcept;

	// Slot position relative to the owner, in owner body space.
	Vec3 FormationSlotOffsetLocal(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout) noexcept;
	Vec3 FormationSlotOffsetLocal(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius) noexcept;

	// Distance from the owner to the slot (equals the slot offset length).
	float FormationSlotRadius(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout) noexcept;

	// Smallest distance between any two slots of the squad after the layout is built;
	// 0 for a squad of one. Callers can use it to log or to pick a formation radius.
	float FormationMinSlotDistance(std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout = {}) noexcept;

	// Body space <-> world space for an offset (a delta between two world points).
	Vec3 OwnerLocalToWorldOffset(const Vec3& a_localOffset, float a_ownerYawRadians) noexcept;
	Vec3 WorldToOwnerLocalOffset(const Vec3& a_worldOffset, float a_ownerYawRadians) noexcept;

	// Body space <-> world space for an absolute world point (round-trips exactly).
	Vec3 OwnerLocalToWorldPoint(const Vec3& a_localOffset, const Vec3& a_ownerPosition, float a_ownerYawRadians) noexcept;
	Vec3 WorldPointToOwnerLocal(const Vec3& a_worldPoint, const Vec3& a_ownerPosition, float a_ownerYawRadians) noexcept;

	// Heading (degrees, -180..180) an actor standing at a_from must face to move toward
	// a_to, expressed relative to the owner's heading. That is the value
	// Actor::KeepOffsetFromActor takes in its offset-angle Z argument ("the offset angle
	// from the target actor ... Z (degrees)"), so the actor faces where it walks instead
	// of moonwalking with the owner's heading.
	float FacingOffsetDegrees(const Vec3& a_from, const Vec3& a_to, float a_ownerYawRadians) noexcept;
}
