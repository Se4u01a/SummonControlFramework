#pragma once

#include "Core/EntityID.h"
#include "Core/Types.h"

namespace scf::core
{
	struct DecisionContext
	{
		float distanceToOwner{ 0.0f };
		bool hasOwner{ true };
		bool isOwnerInCombat{ false };

		bool hasTarget{ false };
		EntityID detectedTargetId{ InvalidEntityID };
		float distanceToTarget{ 0.0f };

		// Configurable thresholds
		float followDistanceThreshold{ 500.0f };
		float returnDistanceThreshold{ 2500.0f };
		float engageDistanceThreshold{ 300.0f };
		float targetLostDebounceTime{ 1.5f };
	};
}
