#pragma once

#include "Core/Types.h"

#include <chrono>

namespace scf::core
{
	using AttackTraceClock = std::chrono::steady_clock;

	struct AttackTraceOrigin
	{
		std::uint64_t id{};
		AttackTraceClock::time_point impactAt{};
		Vec3 impactTarget{};
	};

	struct AttackTracePath
	{
		Vec3 previous;
		float distance{};
		float forwardDistance{};

		void Observe(const Vec3& a_position, float a_yaw) noexcept
		{
			const float dx = a_position.x - previous.x;
			const float dy = a_position.y - previous.y;
			distance += std::hypot(dx, dy);
			forwardDistance += dx * std::sin(a_yaw) + dy * std::cos(a_yaw);
			previous = a_position;
		}
	};

	// A new weapon attack requires a change of phase after dispatch, with the
	// assigned actor selected by the engine. An attack already in progress can
	// finish without being counted as a new start.
	struct AttackTraceWeapon
	{
		bool ready{};

		bool Observe(bool a_atRest, bool a_startPhase, bool a_hasFocus) noexcept
		{
			if (a_atRest) {
				ready = true;
				return false;
			}
			const bool started = ready && a_startPhase && a_hasFocus;
			ready = false;
			return started;
		}
	};
}
