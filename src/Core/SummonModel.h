#pragma once

#include "Core/EntityID.h"
#include "Core/Types.h"

namespace scf::core
{
	struct SummonModel
	{
		EntityID id{ InvalidEntityID };
		EntityID ownerId{ InvalidEntityID };
		EntityID currentTargetId{ InvalidEntityID };

		SummonState state{ SummonState::Idle };
		AggressionMode aggression{ AggressionMode::Defensive };
		SummonOrder order{ SummonOrder::Follow };

		Vec3 lastPosition{};
		float stateTime{ 0.0f };
		float targetLostTime{ 0.0f };

		bool isReanimated{ false };
		bool playerSneaking{ false };
		bool playerDetected{ false };
	};
}
