#pragma once

#include <cstdint>

namespace scf::core
{
	// A rejected movement node enters the native ForceFail frame. Only its
	// corresponding exit may pop that frame, even if the order changes meanwhile.
	struct WolfManeuverFrame
	{
		const void* node{};
		const void* thread{};

		bool Begin(const void* a_node, const void* a_thread) noexcept
		{
			if (node || !a_node || !a_thread) {
				return false;
			}
			node = a_node;
			thread = a_thread;
			return true;
		}

		bool Finish(const void* a_node, const void* a_thread) noexcept
		{
			if (!node || node != a_node || thread != a_thread) {
				return false;
			}
			*this = {};
			return true;
		}
	};

	inline bool BlocksWolfManeuver(bool a_focusedWolf, std::uint32_t a_pin, std::uint32_t a_target) noexcept
	{
		return a_focusedWolf && a_pin != 0 && a_pin == a_target;
	}
}
