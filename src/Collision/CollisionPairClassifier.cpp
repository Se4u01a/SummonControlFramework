#include "Collision/CollisionPairClassifier.h"

#include <cstdio>

namespace scf::collision
{
	CollisionResult CollisionPairClassifier::ClassifyPair(
		core::EntityID a_entityA,
		core::EntityID a_entityB,
		const core::SummonRegistry& a_registry,
		const core::CollisionSettings& a_settings,
		core::EntityID a_playerEntityId) const noexcept
	{
		// Invalid entities or self-collision
		if (a_entityA == core::InvalidEntityID || a_entityB == core::InvalidEntityID || a_entityA == a_entityB) {
			return CollisionResult::Default;
		}

		if (!a_settings.enableCollisionBypass) {
			return CollisionResult::Default;
		}

		const bool aIsSummon = a_registry.IsActive(a_entityA);
		const bool bIsSummon = a_registry.IsActive(a_entityB);

		// If neither entity is an active summon, we do not interfere with engine collision
		if (!aIsSummon && !bIsSummon) {
			return CollisionResult::Default;
		}

		char logBuf[256];

		// Case 1: Player <-> Own Summon
		const bool aIsPlayer = (a_entityA == a_playerEntityId);
		const bool bIsPlayer = (a_entityB == a_playerEntityId);

		if (aIsPlayer && bIsSummon) {
			const auto* rec = a_registry.FindRecord(a_entityB);
			if (rec && rec->ownerId == a_playerEntityId) {
				if (_logCallback) {
					std::snprintf(logBuf, sizeof(logBuf),
						"[Collision] PassThrough: Player 0x%08X <-> Own Summon 0x%08X",
						a_playerEntityId, a_entityB);
					_logCallback(logBuf);
				}
				return CollisionResult::PassThrough;
			}
		} else if (bIsPlayer && aIsSummon) {
			const auto* rec = a_registry.FindRecord(a_entityA);
			if (rec && rec->ownerId == a_playerEntityId) {
				if (_logCallback) {
					std::snprintf(logBuf, sizeof(logBuf),
						"[Collision] PassThrough: Player 0x%08X <-> Own Summon 0x%08X",
						a_playerEntityId, a_entityA);
					_logCallback(logBuf);
				}
				return CollisionResult::PassThrough;
			}
		}

		// Case 2: Summon A <-> Summon B of the same owner
		if (aIsSummon && bIsSummon && a_settings.enableSummonToSummonBypass) {
			const auto* recA = a_registry.FindRecord(a_entityA);
			const auto* recB = a_registry.FindRecord(a_entityB);

			if (recA && recB && recA->ownerId == recB->ownerId && recA->ownerId != core::InvalidEntityID) {
				if (_logCallback) {
					std::snprintf(logBuf, sizeof(logBuf),
						"[Collision] PassThrough: Allied Summons 0x%08X <-> 0x%08X (Owner: 0x%08X)",
						a_entityA, a_entityB, recA->ownerId);
					_logCallback(logBuf);
				}
				return CollisionResult::PassThrough;
			}
		}

		// Case 3: Summon <-> Enemy or Unrelated entity -> maintain physical collision
		return CollisionResult::Collide;
	}
}
