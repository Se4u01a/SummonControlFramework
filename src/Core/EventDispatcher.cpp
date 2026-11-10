#include "Core/EventDispatcher.h"

#include <cstdio>

namespace scf::core
{
	bool EventDispatcher::HandleActiveEffect(
		const ActiveEffectEvent& a_event,
		SummonRegistry& a_registry,
		float a_currentTimestamp)
	{
		char logBuf[256];

		if (a_event.targetId == InvalidEntityID || a_event.targetId == 0) {
			return false;
		}

		if (a_event.isApplied) {
			// Do not register if caster and target are identical (self-buff, not a summon)
			if (a_event.casterId == a_event.targetId) {
				return false;
			}

			EntityID displaced = InvalidEntityID;
			const bool registered = a_registry.RegisterSummon(
				a_event.casterId,
				a_event.targetId,
				SummonType::SummonCreature,
				-1.0f,
				a_currentTimestamp,
				&displaced);

			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[EventDispatcher] ActiveEffect applied: Caster 0x%08X -> Target 0x%08X (Registered: %s)",
					a_event.casterId, a_event.targetId, registered ? "true" : "false");
				_logCallback(logBuf);
			}

			if (displaced != InvalidEntityID && _displaceCallback) {
				if (_logCallback) {
					std::snprintf(logBuf, sizeof(logBuf),
						"[EventDispatcher] Displacement triggered for oldest summon 0x%08X", displaced);
					_logCallback(logBuf);
				}
				_displaceCallback(displaced);
			}

			return registered;
		} else {
			// Effect removed / dispelled
			if (!a_registry.IsRegistered(a_event.targetId)) {
				return false;
			}

			a_registry.NotifyEffectRemoved(a_event.casterId, a_event.targetId);

			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[EventDispatcher] ActiveEffect removed: Caster 0x%08X -> Target 0x%08X",
					a_event.casterId, a_event.targetId);
				_logCallback(logBuf);
			}

			return true;
		}
	}

	bool EventDispatcher::HandleDeath(
		const DeathEvent& a_event,
		SummonRegistry& a_registry)
	{
		if (a_event.dyingId == InvalidEntityID || !a_event.isDead) {
			return false;
		}

		if (!a_registry.IsRegistered(a_event.dyingId)) {
			return false;
		}

		a_registry.NotifyDeath(a_event.dyingId);

		if (_logCallback) {
			char logBuf[256];
			std::snprintf(logBuf, sizeof(logBuf),
				"[EventDispatcher] Death event processed for Summon 0x%08X (Killer: 0x%08X)",
				a_event.dyingId, a_event.killerId);
			_logCallback(logBuf);
		}

		return true;
	}

	bool EventDispatcher::HandleCellAttachDetach(
		const CellAttachDetachEvent& a_event,
		SummonRegistry& a_registry)
	{
		if (a_event.entityId == InvalidEntityID) {
			return false;
		}

		if (!a_registry.IsRegistered(a_event.entityId)) {
			return false;
		}

		char logBuf[256];

		if (a_event.isAttached) {
			a_registry.NotifyCellAttach(a_event.entityId);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[EventDispatcher] Cell attach processed for Summon 0x%08X (Restored to Active)",
					a_event.entityId);
				_logCallback(logBuf);
			}
		} else {
			a_registry.NotifyCellDetach(a_event.entityId);
			if (_logCallback) {
				std::snprintf(logBuf, sizeof(logBuf),
					"[EventDispatcher] Cell detach processed for Summon 0x%08X (Suspended)",
					a_event.entityId);
				_logCallback(logBuf);
			}
		}

		return true;
	}
}
