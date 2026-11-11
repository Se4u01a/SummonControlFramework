#include "Core/Formation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace scf::core
{
	namespace
	{
		constexpr float DegToRad(float a_degrees) noexcept
		{
			return a_degrees * (static_cast<float>(std::numbers::pi) / 180.0f);
		}

		std::size_t NormaliseSize(std::size_t a_squadSize) noexcept
		{
			return a_squadSize == 0 ? 1 : a_squadSize;
		}

		// A layout with outerRingCapacity = 0 would divide by zero; treat it as "one
		// slot per ring", which degrades to a single-file line behind the owner.
		std::size_t RingCapacity(const FormationLayout& a_layout) noexcept
		{
			return a_layout.outerRingCapacity == 0 ? 1 : a_layout.outerRingCapacity;
		}

		// Bearing table of the original single-arc layout. Sums of 1..4 summons are the
		// hand-tuned historical positions every caller and field test knows; 5 and more
		// are equispaced across the rear arc (90..270 degrees).
		float ArcBearingDeg(std::size_t a_indexInRing, std::size_t a_slotsInRing) noexcept
		{
			if (a_slotsInRing <= 1) {
				return -140.0f; // Single summon: left-rear, the historical squad default
			}

			const std::size_t index = a_indexInRing % a_slotsInRing;

			switch (a_slotsInRing) {
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
				const float step = 180.0f / static_cast<float>(a_slotsInRing + 1);
				return 90.0f + step * static_cast<float>(index + 1);
			}
		}

		std::size_t SlotsInRing(std::size_t a_ring, std::size_t a_total, std::size_t a_capacity) noexcept
		{
			const std::size_t taken = a_ring * a_capacity;
			if (taken >= a_total) {
				return 0;
			}
			return std::min(a_capacity, a_total - taken);
		}

		// A ring is a rear arc of its own: the outer ring uses the historical table, the
		// inner rings are plain equispaced arcs (the hand-tuned flanks would put an
		// inner-ring summon in front of the player, where the camera is).
		float RingBearingDeg(std::size_t a_ring, std::size_t a_indexInRing, std::size_t a_slotsInRing) noexcept
		{
			if (a_ring == 0) {
				return ArcBearingDeg(a_indexInRing, a_slotsInRing);
			}
			if (a_slotsInRing <= 1) {
				return 180.0f; // Straight behind
			}
			const float step = 180.0f / static_cast<float>(a_slotsInRing + 1);
			return 90.0f + step * static_cast<float>(a_indexInRing + 1);
		}

		// Radius of a ring as a fraction of the requested formation radius.
		float RingRadiusScale(std::size_t a_ring, const FormationLayout& a_layout) noexcept
		{
			const float scale = std::clamp(a_layout.innerRingScale, 0.1f, 1.0f);
			float factor = 1.0f;
			for (std::size_t ring = 0; ring < a_ring; ++ring) {
				factor *= scale;
			}
			return factor;
		}

		Vec3 RawSlotOffset(
			std::size_t a_slotIndex,
			std::size_t a_total,
			std::size_t a_capacity,
			const FormationLayout& a_layout,
			float a_radius) noexcept
		{
			const std::size_t index = a_slotIndex % a_total;
			const std::size_t ring = index / a_capacity;
			const std::size_t inRing = index % a_capacity;
			const std::size_t slotsInRing = SlotsInRing(ring, a_total, a_capacity);

			const float bearing = DegToRad(RingBearingDeg(ring, inRing, slotsInRing));
			const float radius = a_radius * RingRadiusScale(ring, a_layout);
			return { std::sin(bearing) * radius, std::cos(bearing) * radius, 0.0f };
		}

		// Smallest distance between any two slots the layout produces at this radius.
		// Scale-linear in the radius, so the expansion below can solve for the factor
		// directly, but the loop also keeps the answer exact for any ring count.
		float RawMinSlotDistance(
			std::size_t a_total,
			std::size_t a_capacity,
			const FormationLayout& a_layout,
			float a_radius) noexcept
		{
			if (a_total <= 1) {
				return 0.0f;
			}

			float minimum = std::numeric_limits<float>::max();
			for (std::size_t first = 0; first < a_total; ++first) {
				const auto firstOffset = RawSlotOffset(first, a_total, a_capacity, a_layout, a_radius);
				for (std::size_t second = first + 1; second < a_total; ++second) {
					const auto secondOffset = RawSlotOffset(second, a_total, a_capacity, a_layout, a_radius);
					minimum = std::min(minimum, firstOffset.DistanceTo(secondOffset));
				}
			}
			return minimum;
		}

		// Uniform expansion that keeps every pair of slots at least minSlotDistance
		// apart. Uniform scaling preserves the bearings and ring ratios, so the squad
		// keeps its shape and only stands farther out.
		float SlotExpansionFactor(
			std::size_t a_total,
			std::size_t a_capacity,
			const FormationLayout& a_layout,
			float a_radius) noexcept
		{
			const float maxExpansion = std::max(1.0f, a_layout.maxExpansion);
			if (a_layout.minSlotDistance <= 0.0f || a_total <= 1 || a_radius <= 0.0f) {
				return 1.0f;
			}

			const float spacing = RawMinSlotDistance(a_total, a_capacity, a_layout, a_radius);
			if (spacing <= 0.0f || spacing >= a_layout.minSlotDistance) {
				return 1.0f;
			}
			return std::min(a_layout.minSlotDistance / spacing, maxExpansion);
		}
	}

	float FormationSlotBearingDeg(std::size_t a_slotIndex, std::size_t a_squadSize) noexcept
	{
		return FormationSlotBearingDeg(a_slotIndex, a_squadSize, FormationLayout{});
	}

	float FormationSlotBearingDeg(std::size_t a_slotIndex, std::size_t a_squadSize, const FormationLayout& a_layout) noexcept
	{
		const std::size_t total = NormaliseSize(a_squadSize);
		const std::size_t capacity = RingCapacity(a_layout);
		const std::size_t index = a_slotIndex % total;
		const std::size_t ring = index / capacity;
		return RingBearingDeg(ring, index % capacity, SlotsInRing(ring, total, capacity));
	}

	std::size_t FormationRingCount(std::size_t a_squadSize, const FormationLayout& a_layout) noexcept
	{
		const std::size_t total = NormaliseSize(a_squadSize);
		const std::size_t capacity = RingCapacity(a_layout);
		return (total + capacity - 1) / capacity;
	}

	std::size_t FormationSlotRing(std::size_t a_slotIndex, std::size_t a_squadSize, const FormationLayout& a_layout) noexcept
	{
		const std::size_t total = NormaliseSize(a_squadSize);
		const std::size_t capacity = RingCapacity(a_layout);
		return (a_slotIndex % total) / capacity;
	}

	float FormationRingRadiusScale(std::size_t a_ring, const FormationLayout& a_layout) noexcept
	{
		return RingRadiusScale(a_ring, a_layout);
	}

	Vec3 FormationSlotOffsetLocal(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius) noexcept
	{
		return FormationSlotOffsetLocal(a_slotIndex, a_squadSize, a_radius, FormationLayout{});
	}

	Vec3 FormationSlotOffsetLocal(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout) noexcept
	{
		const std::size_t total = NormaliseSize(a_squadSize);
		const std::size_t capacity = RingCapacity(a_layout);
		const float radius = std::max(0.0f, a_radius);
		const float expansion = SlotExpansionFactor(total, capacity, a_layout, radius);
		return RawSlotOffset(a_slotIndex, total, capacity, a_layout, radius * expansion);
	}

	float FormationSlotRadius(std::size_t a_slotIndex, std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout) noexcept
	{
		return FormationSlotOffsetLocal(a_slotIndex, a_squadSize, a_radius, a_layout).Length();
	}

	float FormationMinSlotDistance(std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout) noexcept
	{
		const std::size_t total = NormaliseSize(a_squadSize);
		if (a_squadSize <= 1) {
			return 0.0f;
		}

		const std::size_t capacity = RingCapacity(a_layout);
		const float radius = std::max(0.0f, a_radius);
		const float expansion = SlotExpansionFactor(total, capacity, a_layout, radius);
		return RawMinSlotDistance(total, capacity, a_layout, radius * expansion);
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
