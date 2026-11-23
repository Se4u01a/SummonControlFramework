#include "check.h"
#include "Core/AttackTrace.h"

#include <numbers>

TEST(attack_trace_accumulates_horizontal_path_and_signed_forward_motion)
{
	scf::core::AttackTracePath path{ { 10.0f, 20.0f, 30.0f } };
	path.Observe({ 10.0f, 50.0f, 40.0f }, 0.0f);
	CHECK_NEAR(path.distance, 30.0f, 0.001f);
	CHECK_NEAR(path.forwardDistance, 30.0f, 0.001f);

	path.Observe({ 10.0f, 40.0f, 40.0f }, 0.0f);
	path.Observe({ 30.0f, 40.0f, 40.0f }, 0.0f);
	CHECK_NEAR(path.distance, 60.0f, 0.001f);
	CHECK_NEAR(path.forwardDistance, 20.0f, 0.001f);

	path.Observe({ 40.0f, 40.0f, 40.0f }, std::numbers::pi_v<float> / 2);
	path.Observe({ 40.0f, 40.0f, 70.0f }, 0.0f);
	CHECK_NEAR(path.distance, 70.0f, 0.001f);
	CHECK_NEAR(path.forwardDistance, 30.0f, 0.001f);
	CHECK_NEAR(path.previous.z, 70.0f, 0.001f);
}

TEST(attack_trace_weapon_requires_a_new_attack_at_the_focus)
{
	scf::core::AttackTraceWeapon weapon;
	CHECK(!weapon.Observe(false, true, true));
	CHECK(!weapon.Observe(true, false, true));
	CHECK(!weapon.Observe(false, true, false));
	CHECK(!weapon.Observe(false, true, true));
	CHECK(!weapon.Observe(true, false, false));
	CHECK(weapon.Observe(false, true, true));
	CHECK(!weapon.Observe(false, true, true));
}
