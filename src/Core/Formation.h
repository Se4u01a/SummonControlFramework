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
	float FormationSlotBearingDeg(std::size_t a_slotIndex, std::size_t a_squadSize) noexcept;

	// Slot position relative to the owner, in owner body space.
	Vec3 FormationSlotOffsetLocal(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius) noexcept;

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
