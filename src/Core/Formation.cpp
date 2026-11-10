#include "Core/Formation.h"

#include <cmath>
#include <numbers>

namespace scf::core
{
	namespace
	{
		constexpr float DegToRad(float a_degrees) noexcept
		{
			return a_degrees * (static_cast<float>(std::numbers::pi) / 180.0f);
		}
	}

	float FormationSlotBearingDeg(std::size_t a_slotIndex, std::size_t a_squadSize) noexcept
	{
		const std::size_t total = a_squadSize == 0 ? 1 : a_squadSize;
		const std::size_t index = a_slotIndex % total;

		if (total <= 1) {
			return -140.0f; // Single summon: left-rear, the historical squad default
		}

		switch (total) {
		case 2:
			return (index == 0) ? -130.0f : 130.0f;

		case 3:
			if (index == 0) return -120.0f;
			if (index == 1) return 120.0f;
			return 180.0f; // Straight behind

		case 4:
			if (index == 0) return -70.0f;   // Left flank
			if (index == 1) return 70.0f;    // Right flank
			if (index == 2) return -145.0f;  // Left rear
			return 145.0f;                   // Right rear

		default:
			// Equispaced across the rear arc (90..270 degrees)
			const float step = 180.0f / static_cast<float>(total + 1);
			return 90.0f + step * static_cast<float>(index + 1);
		}
	}

	Vec3 FormationSlotOffsetLocal(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius) noexcept
	{
		const float bearing = DegToRad(FormationSlotBearingDeg(a_slotIndex, a_squadSize));
		return { std::sin(bearing) * a_radius, std::cos(bearing) * a_radius, 0.0f };
	}

	Vec3 OwnerLocalToWorldOffset(const Vec3& a_localOffset, float a_ownerYawRadians) noexcept
	{
		const float yawSin = std::sin(a_ownerYawRadians);
		const float yawCos = std::cos(a_ownerYawRadians);

		// World forward = (sin yaw, cos yaw); world right = (cos yaw, -sin yaw).
		return {
			a_localOffset.x * yawCos + a_localOffset.y * yawSin,
			-a_localOffset.x * yawSin + a_localOffset.y * yawCos,
			a_localOffset.z
		};
	}

	Vec3 WorldToOwnerLocalOffset(const Vec3& a_worldOffset, float a_ownerYawRadians) noexcept
	{
		const float yawSin = std::sin(a_ownerYawRadians);
		const float yawCos = std::cos(a_ownerYawRadians);

		return {
			a_worldOffset.x * yawCos - a_worldOffset.y * yawSin,
			a_worldOffset.x * yawSin + a_worldOffset.y * yawCos,
			a_worldOffset.z
		};
	}

	Vec3 OwnerLocalToWorldPoint(const Vec3& a_localOffset, const Vec3& a_ownerPosition, float a_ownerYawRadians) noexcept
	{
		const auto worldOffset = OwnerLocalToWorldOffset(a_localOffset, a_ownerYawRadians);
		return { a_ownerPosition.x + worldOffset.x, a_ownerPosition.y + worldOffset.y, a_ownerPosition.z + worldOffset.z };
	}

	Vec3 WorldPointToOwnerLocal(const Vec3& a_worldPoint, const Vec3& a_ownerPosition, float a_ownerYawRadians) noexcept
	{
		const Vec3 worldOffset{ a_worldPoint.x - a_ownerPosition.x, a_worldPoint.y - a_ownerPosition.y, a_worldPoint.z - a_ownerPosition.z };
		return WorldToOwnerLocalOffset(worldOffset, a_ownerYawRadians);
	}

	float FacingOffsetDegrees(const Vec3& a_from, const Vec3& a_to, float a_ownerYawRadians) noexcept
	{
		const float dx = a_to.x - a_from.x;
		const float dy = a_to.y - a_from.y;
		if (dx * dx + dy * dy < 1.0f) {
			return 0.0f; // standing on the goal: keep the owner's heading
		}

		const float worldDeg = std::atan2(dx, dy) * (180.0f / static_cast<float>(std::numbers::pi));
		const float ownerDeg = a_ownerYawRadians * (180.0f / static_cast<float>(std::numbers::pi));

		float relative = worldDeg - ownerDeg;
		while (relative > 180.0f) {
			relative -= 360.0f;
		}
		while (relative < -180.0f) {
			relative += 360.0f;
		}
		return relative;
	}
}
