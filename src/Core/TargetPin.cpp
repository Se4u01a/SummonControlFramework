#include "Core/TargetPin.h"

namespace scf::core
{
	std::uint32_t ChooseSelectorAnswer(
		std::uint32_t a_engineAnswer,
		std::uint32_t a_pinnedHandle,
		bool a_pinnedAlive,
		PinMembership a_membership) noexcept
	{
		if (a_pinnedHandle == 0 || a_engineAnswer == 0) {
			return a_engineAnswer;
		}
		if (!a_pinnedAlive || a_membership != PinMembership::Held) {
			return a_engineAnswer;
		}
		return a_pinnedHandle;
	}

	const char* ToString(PinMembership a_membership) noexcept
	{
		switch (a_membership) {
		case PinMembership::Absent:
			return "absent";
		case PinMembership::Held:
			return "held";
		case PinMembership::Lost:
			return "lost";
		}
		return "unknown";
	}
}
