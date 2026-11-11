#include "check.h"
#include "Core/Formation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

using namespace scf::core;

namespace
{
	constexpr float kPi = static_cast<float>(std::numbers::pi);

	// Bearings of the historical single-arc layout, slot by slot.
	const std::array<std::array<float, 4>, 4>& LegacyBearings()
	{
		static const std::array<std::array<float, 4>, 4> table{
			std::array<float, 4>{ -140.0f, 0.0f, 0.0f, 0.0f },  // 1 summon
			std::array<float, 4>{ -130.0f, 130.0f, 0.0f, 0.0f }, // 2 summons
			std::array<float, 4>{ -120.0f, 120.0f, 180.0f, 0.0f }, // 3 summons
			std::array<float, 4>{ -70.0f, 70.0f, -145.0f, 145.0f } // 4 summons
		};
		return table;
	}

	// Hand-computed body-space offsets at radius 200 for squads of 1..4.
	const std::array<std::array<float, 4>, 4>& LegacyOffsetX()
	{
		static const std::array<std::array<float, 4>, 4> table{
			std::array<float, 4>{ -128.5576f, 0.0f, 0.0f, 0.0f },
			std::array<float, 4>{ -153.2089f, 153.2089f, 0.0f, 0.0f },
			std::array<float, 4>{ -173.2051f, 173.2051f, 0.0f, 0.0f },
			std::array<float, 4>{ -187.9386f, 187.9386f, -114.7153f, 114.7153f }
		};
		return table;
	}

	const std::array<std::array<float, 4>, 4>& LegacyOffsetY()
	{
		static const std::array<std::array<float, 4>, 4> table{
			std::array<float, 4>{ -153.2089f, 0.0f, 0.0f, 0.0f },
			std::array<float, 4>{ -128.5576f, -128.5576f, 0.0f, 0.0f },
			std::array<float, 4>{ -100.0f, -100.0f, -200.0f, 0.0f },
			std::array<float, 4>{ 68.4040f, 68.4040f, -163.8304f, -163.8304f }
		};
		return table;
	}

	float HorizontalDistance(const Vec3& a_lhs, const Vec3& a_rhs)
	{
		const float dx = a_lhs.x - a_rhs.x;
		const float dy = a_lhs.y - a_rhs.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	// Smallest distance between any two slots of the squad, computed from the public
	// slot accessors only (independent of FormationMinSlotDistance).
	float MeasuredMinSlotDistance(std::size_t a_squadSize, float a_radius, const FormationLayout& a_layout)
	{
		float minimum = 1.0e9f;
		for (std::size_t first = 0; first < a_squadSize; ++first) {
			const auto firstOffset = FormationSlotOffsetLocal(first, a_squadSize, a_radius, a_layout);
			for (std::size_t second = first + 1; second < a_squadSize; ++second) {
				const auto secondOffset = FormationSlotOffsetLocal(second, a_squadSize, a_radius, a_layout);
				minimum = std::min(minimum, firstOffset.DistanceTo(secondOffset));
			}
		}
		return a_squadSize <= 1 ? 0.0f : minimum;
	}
}

// ---------------------------------------------------------------------------
// Regression: squads of 1..4 must stay exactly where they always were.
// ---------------------------------------------------------------------------

TEST(TestFormationLegacyBearingsForSmallSquads)
{
	for (std::size_t total = 1; total <= 4; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const float expected = LegacyBearings()[total - 1][slot];
			CHECK_NEAR(FormationSlotBearingDeg(slot, total), expected, 0.001f);
			// The ring layout must agree for a squad that fits on the outer ring.
			CHECK_NEAR(FormationSlotBearingDeg(slot, total, FormationLayout{}), expected, 0.001f);
		}
	}
}

TEST(TestFormationLegacyOffsetsForSmallSquads)
{
	for (std::size_t total = 1; total <= 4; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto offset = FormationSlotOffsetLocal(slot, total, 200.0f);
			CHECK_NEAR(offset.x, LegacyOffsetX()[total - 1][slot], 0.01f);
			CHECK_NEAR(offset.y, LegacyOffsetY()[total - 1][slot], 0.01f);
			CHECK_NEAR(offset.z, 0.0f, 0.001f);
		}
	}
}

TEST(TestFormationFiveAndSixKeepTheSingleArc)
{
	// The default layout must not introduce rings before the outer ring is full: the
	// 5- and 6-summon arcs are the layout players already have.
	for (std::size_t total : { std::size_t{ 5 }, std::size_t{ 6 } }) {
		CHECK(FormationRingCount(total) == 1);

		for (std::size_t slot = 0; slot < total; ++slot) {
			const float step = 180.0f / static_cast<float>(total + 1);
			const float expected = 90.0f + step * static_cast<float>(slot + 1);
			CHECK_NEAR(FormationSlotBearingDeg(slot, total), expected, 0.001f);
			CHECK_NEAR(FormationSlotOffsetLocal(slot, total, 200.0f).Length(), 200.0f, 0.01f);
		}
	}
}

// ---------------------------------------------------------------------------
// Army layout: 1..10 summons, rings, uniqueness and minimum spacing.
// ---------------------------------------------------------------------------

TEST(TestFormationArmiesOccupyUniqueSlots)
{
	for (std::size_t total = 1; total <= 10; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto offset = FormationSlotOffsetLocal(slot, total, 200.0f);
			CHECK(std::isfinite(offset.x) && std::isfinite(offset.y));
			CHECK_NEAR(offset.z, 0.0f, 0.001f);
			CHECK_NEAR(offset.Length(), FormationSlotRadius(slot, total, 200.0f, FormationLayout{}), 0.001f);
		}
	}

	// Default layout at the default formation distance: every pair at least the
	// configured minimum apart, and no expansion needed (radius stays 200).
	const FormationLayout layout{};
	for (std::size_t total = 2; total <= 10; ++total) {
		const float measured = MeasuredMinSlotDistance(total, 200.0f, layout);
		CHECK(measured >= layout.minSlotDistance - 0.01f);
		CHECK_NEAR(measured, FormationMinSlotDistance(total, 200.0f, layout), 0.01f);

		for (std::size_t slot = 0; slot < total; ++slot) {
			const bool onOuterRing = FormationSlotRing(slot, total, layout) == 0;
			CHECK_NEAR(FormationSlotRadius(slot, total, 200.0f, layout), onOuterRing ? 200.0f : 124.0f, 0.05f);
		}
	}

	// A lone summon has nothing to keep its distance from.
	CHECK_NEAR(FormationMinSlotDistance(1, 200.0f, layout), 0.0f, 0.001f);
}

TEST(TestFormationArmyUsesTwoRingsButOnlyFillsTheOuterOneFirst)
{
	for (std::size_t total = 7; total <= 10; ++total) {
		CHECK(FormationRingCount(total) == 2);

		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto ring = FormationSlotRing(slot, total);
			CHECK(ring == (slot < 6 ? 0u : 1u));
			CHECK_NEAR(FormationRingRadiusScale(ring), ring == 0 ? 1.0f : 0.62f, 0.001f);
		}
	}

	// Ten summons is a two-ring army: six on the outer arc, four inward.
	CHECK(FormationRingCount(6) == 1);
	CHECK(FormationRingCount(10) == 2);
	CHECK(FormationRingCount(0) == 1);
}

TEST(TestFormationArmyNeverStandsInFrontOfTheOwner)
{
	// From five summons up, every slot is a rear arc: nothing stands in front of the
	// owner, where the player's camera is. (The hand-tuned four-summon flanks do sit
	// slightly forward, and they are frozen by the regression tests above.)
	for (std::size_t total = 5; total <= 10; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto offset = FormationSlotOffsetLocal(slot, total, 200.0f);
			CHECK(offset.y <= 0.01f);
		}
	}
}

TEST(TestFormationSlotsStayStableWhileTheArmyGrows)
{
	// Growing the squad must not reshuffle the summons already in formation: slot 0..5
	// of a 6-summon squad keep their exact place in a 7..10-summon squad.
	for (std::size_t total = 7; total <= 10; ++total) {
		for (std::size_t slot = 0; slot < 6; ++slot) {
			const auto small = FormationSlotOffsetLocal(slot, 6, 200.0f);
			const auto large = FormationSlotOffsetLocal(slot, total, 200.0f);
			CHECK_NEAR(large.x, small.x, 0.001f);
			CHECK_NEAR(large.y, small.y, 0.001f);
		}
	}
}

TEST(TestFormationMinSlotDistanceIsEnforced)
{
	// Given a requested minimum spacing
	// When the formation radius cannot provide it
	// Then the layout expands uniformly (same shape) until it does.
	const std::array<std::array<float, 2>, 5> cases{
		std::array<float, 2>{ 50.0f, 56.0f },
		std::array<float, 2>{ 100.0f, 80.0f },
		std::array<float, 2>{ 200.0f, 80.0f },
		std::array<float, 2>{ 200.0f, 120.0f },
		std::array<float, 2>{ 300.0f, 150.0f }
	};

	for (const auto& oneCase : cases) {
		const float radius = oneCase[0];
		const FormationLayout layout{ 6, 0.62f, oneCase[1], 4.0f };

		for (std::size_t total = 2; total <= 10; ++total) {
			const float measured = MeasuredMinSlotDistance(total, radius, layout);
			CHECK(measured >= oneCase[1] - 0.01f);
			CHECK_NEAR(measured, FormationMinSlotDistance(total, radius, layout), 0.02f);

			// The expansion is uniform and bounded: nothing ends up farther than the
			// expansion budget allows.
			for (std::size_t slot = 0; slot < total; ++slot) {
				const float slotRadius = FormationSlotRadius(slot, total, radius, layout);
				CHECK(slotRadius <= radius * layout.maxExpansion + 0.05f);
				CHECK(slotRadius >= radius * 0.62f - 0.05f);
			}
		}
	}
}

TEST(TestFormationExpansionIsBoundedByMaxExpansion)
{
	// A pathological request (500 units between neighbours inside a 40-unit ring) must
	// stay inside the budget instead of blowing the squad up.
	const FormationLayout layout{ 6, 0.62f, 500.0f, 2.0f };
	for (std::size_t slot = 0; slot < 10; ++slot) {
		const float slotRadius = FormationSlotRadius(slot, 10, 40.0f, layout);
		CHECK(slotRadius <= 80.05f);
		CHECK(std::isfinite(slotRadius));
	}
}

TEST(TestFormationDisablingTheMinimumKeepsTheRawLayout)
{
	// minSlotDistance = 0 is "use the layout as computed": no expansion at any radius.
	const FormationLayout layout{ 6, 0.62f, 0.0f, 4.0f };
	for (std::size_t total = 1; total <= 10; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto raw = FormationSlotOffsetLocal(slot, total, 60.0f, layout);
			const auto ringScale = FormationRingRadiusScale(FormationSlotRing(slot, total, layout), layout);
			CHECK_NEAR(raw.Length(), 60.0f * ringScale, 0.01f);
		}
	}
}

TEST(TestFormationIsDeterministic)
{
	// Same inputs -> bit-identical outputs, regardless of evaluation order or how many
	// times the layout was asked for before.
	for (std::size_t total = 1; total <= 10; ++total) {
		std::array<Vec3, 10> firstPass{};
		for (std::size_t slot = 0; slot < total; ++slot) {
			firstPass[slot] = FormationSlotOffsetLocal(slot, total, 175.0f);
		}

		// Ask for everything again in reverse order.
		std::array<Vec3, 10> secondPass{};
		for (std::size_t slot = total; slot > 0; --slot) {
			secondPass[slot - 1] = FormationSlotOffsetLocal(slot - 1, total, 175.0f);
		}

		for (std::size_t slot = 0; slot < total; ++slot) {
			CHECK(firstPass[slot].x == secondPass[slot].x);
			CHECK(firstPass[slot].y == secondPass[slot].y);
			CHECK(firstPass[slot].z == secondPass[slot].z);
		}
	}
}

TEST(TestFormationDegenerateInputsStayFinite)
{
	// Size 0 behaves like a squad of one, exactly as the historical bearing table did.
	const auto single = FormationSlotOffsetLocal(0, 1, 200.0f);
	const auto empty = FormationSlotOffsetLocal(0, 0, 200.0f);
	CHECK_NEAR(empty.x, single.x, 0.001f);
	CHECK_NEAR(empty.y, single.y, 0.001f);
	CHECK(FormationRingCount(0) == 1);
	CHECK_NEAR(FormationMinSlotDistance(0, 200.0f), 0.0f, 0.001f);
	CHECK_NEAR(FormationMinSlotDistance(1, 200.0f), 0.0f, 0.001f);

	// A zero radius collapses the squad onto the owner instead of dividing by zero.
	for (std::size_t slot = 0; slot < 10; ++slot) {
		const auto offset = FormationSlotOffsetLocal(slot, 10, 0.0f);
		CHECK_NEAR(offset.Length(), 0.0f, 0.001f);
		CHECK(std::isfinite(offset.x) && std::isfinite(offset.y));
	}

	// A ring capacity of 0 degrades to one slot per ring, not a division by zero.
	const FormationLayout noCapacity{ 0, 0.62f, 50.0f, 4.0f };
	for (std::size_t slot = 0; slot < 4; ++slot) {
		const auto offset = FormationSlotOffsetLocal(slot, 4, 200.0f, noCapacity);
		CHECK(std::isfinite(offset.x) && std::isfinite(offset.y));
		CHECK(FormationSlotRing(slot, 4, noCapacity) == slot);
	}
	for (std::size_t first = 0; first < 4; ++first) {
		for (std::size_t second = first + 1; second < 4; ++second) {
			CHECK(FormationSlotOffsetLocal(first, 4, 200.0f, noCapacity)
			          .DistanceTo(FormationSlotOffsetLocal(second, 4, 200.0f, noCapacity)) > 1.0f);
		}
	}

	// Inner-ring scale is clamped, so a hostile value cannot collapse every ring onto
	// the outer one with duplicated bearings.
	const FormationLayout flatScale{ 6, 5.0f, 0.0f, 4.0f };
	for (std::size_t first = 0; first < 8; ++first) {
		for (std::size_t second = first + 1; second < 8; ++second) {
			CHECK(FormationSlotOffsetLocal(first, 8, 200.0f, flatScale)
			          .DistanceTo(FormationSlotOffsetLocal(second, 8, 200.0f, flatScale)) > 1.0f);
		}
	}
	CHECK_NEAR(FormationRingRadiusScale(1, flatScale), 1.0f, 0.001f);
	CHECK_NEAR(FormationRingRadiusScale(0, flatScale), 1.0f, 0.001f);
}

TEST(TestFormationWrapsSlotIndices)
{
	// Slot indices past the squad size wrap around (the historical modulo behaviour), so
	// a stale slot index never produces an empty or invalid goal.
	for (std::size_t total = 1; total <= 10; ++total) {
		for (std::size_t slot = 0; slot < total; ++slot) {
			const auto direct = FormationSlotOffsetLocal(slot, total, 200.0f);
			const auto wrapped = FormationSlotOffsetLocal(slot + total * 2, total, 200.0f);
			CHECK_NEAR(wrapped.x, direct.x, 0.001f);
			CHECK_NEAR(wrapped.y, direct.y, 0.001f);
		}
	}
}

TEST(TestFormationLargeSquadsStayFinite)
{
	// The registry allows up to 32 summons; beyond the 6..10 target the layout still has
	// to produce distinct, finite goals (spacing then depends on the formation radius).
	for (std::size_t total = 11; total <= 32; ++total) {
		CHECK(FormationRingCount(total) >= 2);
		for (std::size_t first = 0; first < total; ++first) {
			const auto offset = FormationSlotOffsetLocal(first, total, 200.0f);
			CHECK(std::isfinite(offset.x) && std::isfinite(offset.y));
			CHECK(offset.Length() <= 800.05f);
			for (std::size_t second = first + 1; second < total; ++second) {
				CHECK(offset.DistanceTo(FormationSlotOffsetLocal(second, total, 200.0f)) > 1.0f);
			}
		}
	}
}

TEST(TestFormationSlotGeometryIsYawIndependent)
{
	// The slot table is body space: rotating the owner must rotate the whole squad
	// rigidly around him, never change the distances between its members.
	const float yaws[] = { 0.0f, 0.7f, kPi / 2.0f, -2.2f, kPi };
	const Vec3 owner{ 1200.0f, -450.0f, 76.0f };

	for (const float yaw : yaws) {
		for (std::size_t total = 1; total <= 10; ++total) {
			for (std::size_t first = 0; first < total; ++first) {
				const auto local = FormationSlotOffsetLocal(first, total, 200.0f);
				const auto world = OwnerLocalToWorldPoint(local, owner, yaw);
				CHECK_NEAR(std::hypot(world.x - owner.x, world.y - owner.y), local.Length(), 0.02f);

				for (std::size_t second = first + 1; second < total; ++second) {
					const auto otherLocal = FormationSlotOffsetLocal(second, total, 200.0f);
					const auto otherWorld = OwnerLocalToWorldPoint(otherLocal, owner, yaw);
					CHECK_NEAR(HorizontalDistance(world, otherWorld), HorizontalDistance(local, otherLocal), 0.02f);
				}
			}
		}
	}
}
