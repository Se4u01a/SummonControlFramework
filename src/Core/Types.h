#pragma once

#include <cmath>

namespace scf::core
{
	struct Vec3
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };

		constexpr Vec3() = default;
		constexpr Vec3(float a_x, float a_y, float a_z) : x(a_x), y(a_y), z(a_z) {}

		[[nodiscard]] float LengthSquared() const noexcept
		{
			return x * x + y * y + z * z;
		}

		[[nodiscard]] float Length() const noexcept
		{
			return std::sqrt(LengthSquared());
		}

		[[nodiscard]] float DistanceSquaredTo(const Vec3& a_rhs) const noexcept
		{
			const float dx = x - a_rhs.x;
			const float dy = y - a_rhs.y;
			const float dz = z - a_rhs.z;
			return dx * dx + dy * dy + dz * dz;
		}

		[[nodiscard]] float DistanceTo(const Vec3& a_rhs) const noexcept
		{
			return std::sqrt(DistanceSquaredTo(a_rhs));
		}
	};

	enum class SummonState : std::uint8_t
	{
		Idle,
		FollowOwner,
		Guard,
		AcquireTarget,
		Approach,
		Engage,
		Return,
		Despawn
	};

	enum class SummonOrder : std::uint8_t
	{
		Follow,
		WaitGuard,
		Attack,
		Return
	};

	enum class AggressionMode : std::uint8_t
	{
		Passive,
		Defensive,
		Aggressive,
		FocusTarget
	};

	constexpr const char* ToString(SummonState a_state) noexcept
	{
		switch (a_state) {
		case SummonState::Idle:          return "Idle";
		case SummonState::FollowOwner:   return "FollowOwner";
		case SummonState::Guard:         return "Guard";
		case SummonState::AcquireTarget: return "AcquireTarget";
		case SummonState::Approach:      return "Approach";
		case SummonState::Engage:        return "Engage";
		case SummonState::Return:        return "Return";
		case SummonState::Despawn:       return "Despawn";
		default:                         return "Unknown";
		}
	}

	constexpr const char* ToString(SummonOrder a_order) noexcept
	{
		switch (a_order) {
		case SummonOrder::Follow:    return "Follow";
		case SummonOrder::WaitGuard: return "WaitGuard";
		case SummonOrder::Attack:    return "Attack";
		case SummonOrder::Return:    return "Return";
		default:                     return "Unknown";
		}
	}

	constexpr const char* ToString(AggressionMode a_mode) noexcept
	{
		switch (a_mode) {
		case AggressionMode::Passive:     return "Passive";
		case AggressionMode::Defensive:   return "Defensive";
		case AggressionMode::Aggressive:  return "Aggressive";
		case AggressionMode::FocusTarget: return "FocusTarget";
		default:                          return "Unknown";
		}
	}
}
