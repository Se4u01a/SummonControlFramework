#pragma once

#include "Core/EntityID.h"
#include "Core/StealthContext.h"
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

		// Position targeting (MoveTo order)
		bool hasMoveTarget{ false };
		float distanceToMoveTarget{ 0.0f };

		// Stealth coordination
		StealthContext stealth{};

		// Configurable thresholds
		float followDistanceThreshold{ 500.0f };
		float returnDistanceThreshold{ 2500.0f };
		float engageDistanceThreshold{ 300.0f };
		float targetLostDebounceTime{ 1.5f };
	};
}
