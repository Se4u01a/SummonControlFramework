#include "Core/SummonLimit.h"

#include <algorithm>
#include <cmath>

namespace scf::core
{
	float ApplyPlayerSummonLimit(float a_perkValue, std::uint32_t a_configured) noexcept
	{
		const float perk = std::isfinite(a_perkValue) ? a_perkValue : 1.0f;
		if (a_configured == 0) {
			return perk;
		}
		return std::max(perk, static_cast<float>(a_configured));
	}

	std::size_t TrackingCapacity(std::size_t a_configured, std::size_t a_engineRosterSize) noexcept
	{
		return std::max<std::size_t>({ a_configured, a_engineRosterSize, 1 });
	}
}
