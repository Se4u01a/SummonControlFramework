#include "Core/SummonRegistry.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace scf::core
{
	void SummonRegistry::SetOwnerMaxSummons(EntityID a_ownerId, std::size_t a_max)
	{
		_ownerMaxSummons[a_ownerId] = a_max;
		if (_logCallback) {
			char logBuf[256];
			std::snprintf(logBuf, sizeof(logBuf),
				"[Registry] Set max summons for owner %u to %zu", a_ownerId, a_max);
			_logCallback(logBuf);
		}
	}

	std::size_t SummonRegistry::GetOwnerMaxSummons(EntityID a_ownerId) const noexcept
	{
		const auto it = _ownerMaxSummons.find(a_ownerId);
		return (it != _ownerMaxSummons.end()) ? it->second : _defaultMaxSummonsPerOwner;
	}

	EntityID SummonRegistry::FindOldestActiveSummon(EntityID a_ownerId) const noexcept
	{
		EntityID oldestId = InvalidEntityID;
		float oldestTime = std::numeric_limits<float>::infinity();

		for (const auto& [id, record] : _records) {
			if (record.ownerId == a_ownerId &&
			    !record.isDead &&
			    record.lifecycle == SummonLifecycleState::Active) {
				if (record.registrationTimestamp < oldestTime) {
					oldestTime = record.registrationTimestamp;
					oldestId = id;
				}
			}
		}

		return oldestId;
	}

	bool SummonRegistry::RegisterSummon(
		EntityID a_ownerId,
		EntityID a_summonId,
		SummonType a_type,
		float a_duration,
		float a_timestamp,
		EntityID* a_outDisplaced)
	{
		if (a_summonId == InvalidEntityID || a_ownerId == InvalidEntityID) {
			return false;
		}

		char logBuf[256];

		// 1. Idempotent check: if already registered, update and restore Active state
		auto it = _records.find(a_summonId);
		if (it != _records.end()) {
			it->second.ownerId = a_ownerId;
			it->second.type = a_type;
			it->second.remainingDuration = a_duration;
			it->second.lifecycle = SummonLifecycleState::Active;
			it->second.isSuspended = false;
			it->second.isDead = false;
			it->second.deathTimestamp = 0.0f;
			it->second.model.id = a_summonId;
			it->second.model.ownerId = a_ownerId;

			// No log line here: this path runs for every tracked summon on every
			// tactical tick (15 Hz), and the refresh is not an event. Duration and
			// lifecycle are reported by `scf registry` instead.
			return true;
		}

		// 2. Capacity check for owner limit: displace oldest active summon if full
		const auto activeSummons = GetActiveSummonsForOwner(a_ownerId);
		const std::size_t limit = GetOwnerMaxSummons(a_ownerId);

		if (activeSummons.size() >= limit) {
			const EntityID oldestId = FindOldestActiveSummon(a_ownerId);
			if (oldestId != InvalidEntityID) {
				auto oldestIt = _records.find(oldestId);
				if (oldestIt != _records.end()) {
					oldestIt->second.lifecycle = SummonLifecycleState::Displaced;
					oldestIt->second.deathTimestamp = a_timestamp;
				}

				if (a_outDisplaced) {
					*a_outDisplaced = oldestId;
				}

				if (_logCallback) {
					std::snprintf(logBuf, sizeof(logBuf),
						"[Registry] Summon limit reached for owner %u (%zu/%zu). Displacing oldest summon %u",
						a_ownerId, activeSummons.size(), limit, oldestId);
					_logCallback(logBuf);
				}
			}
		}

		// 3. Register new record
		SummonRecord record;
		record.summonId = a_summonId;
		record.ownerId = a_ownerId;
		record.type = a_type;
		record.lifecycle = SummonLifecycleState::Active;
		record.remainingDuration = a_duration;
		record.registrationTimestamp = a_timestamp;
		record.deathTimestamp = 0.0f;
		record.isDead = false;
		record.isSuspended = false;
		record.model.id = a_summonId;
		record.model.ownerId = a_ownerId;

		_records[a_summonId] = record;

		if (_logCallback) {
			std::snprintf(logBuf, sizeof(logBuf),
				"[Registry] Registered summon %u for owner %u (%s, duration: %.1fs, active: %zu/%zu)",
				a_summonId, a_ownerId, ToString(a_type), a_duration,
				GetActiveSummonsForOwner(a_ownerId).size(), limit);
			_logCallback(logBuf);
		}

		return true;
	}

	bool SummonRegistry::UnregisterSummon(EntityID a_summonId, const char* a_reason)
	{
		auto it = _records.find(a_summonId);
		if (it == _records.end()) {
			return false;
		}

		if (_logCallback) {
			char logBuf[256];
			std::snprintf(logBuf, sizeof(logBuf),
				"[Registry] Unregistered summon %u (Reason: %s)",
				a_summonId, a_reason ? a_reason : "Unspecified");
			_logCallback(logBuf);
		}

		_records.erase(it);
		return true;
	}

	void SummonRegistry::NotifyCellDetach(EntityID a_summonId)
	{
		auto it = _records.find(a_summonId);
		if (it != _records.end()) {
			it->second.lifecycle = SummonLifecycleState::Suspended;
			it->second.isSuspended = true;

			if (_logCallback) {
				char logBuf[256];
				std::snprintf(logBuf, sizeof(logBuf),
					"[Registry] Cell detach: summon %u suspended", a_summonId);
				_logCallback(logBuf);
			}
		}
	}

	void SummonRegistry::NotifyCellAttach(EntityID a_summonId)
	{
		auto it = _records.find(a_summonId);
		if (it != _records.end()) {
			it->second.lifecycle = SummonLifecycleState::Active;
			it->second.isSuspended = false;

			if (_logCallback) {
				char logBuf[256];
				std::snprintf(logBuf, sizeof(logBuf),
					"[Registry] Cell attach: summon %u resumed", a_summonId);
				_logCallback(logBuf);
			}
		}
	}

	void SummonRegistry::NotifyDeath(EntityID a_summonId, float a_timestamp)
	{
		auto it = _records.find(a_summonId);
		if (it != _records.end()) {
			it->second.lifecycle = SummonLifecycleState::Dead;
			it->second.isDead = true;
			it->second.deathTimestamp = a_timestamp;

			if (_logCallback) {
				char logBuf[256];
				std::snprintf(logBuf, sizeof(logBuf),
					"[Registry] Death event: summon %u marked dead at %.2fs", a_summonId, a_timestamp);
				_logCallback(logBuf);
			}
		}
	}

	void SummonRegistry::NotifyEffectRemoved(EntityID a_ownerId, EntityID a_summonId, float a_timestamp)
	{
		auto it = _records.find(a_summonId);
		if (it != _records.end()) {
			it->second.lifecycle = SummonLifecycleState::Dispatched;
			it->second.isDead = true;
			it->second.deathTimestamp = a_timestamp;

			if (_logCallback) {
				char logBuf[256];
				std::snprintf(logBuf, sizeof(logBuf),
					"[Registry] Summon %u magic effect expired/removed for owner %u",
					a_summonId, a_ownerId);
				_logCallback(logBuf);
			}
		}
	}

	const SummonRecord* SummonRegistry::FindRecord(EntityID a_summonId) const noexcept
	{
		const auto it = _records.find(a_summonId);
		return (it != _records.end()) ? &it->second : nullptr;
	}

	SummonRecord* SummonRegistry::FindRecord(EntityID a_summonId) noexcept
	{
		auto it = _records.find(a_summonId);
		return (it != _records.end()) ? &it->second : nullptr;
	}

	SummonModel* SummonRegistry::GetModel(EntityID a_summonId) noexcept
	{
		auto* rec = FindRecord(a_summonId);
		return rec ? &rec->model : nullptr;
	}

	const SummonModel* SummonRegistry::GetModel(EntityID a_summonId) const noexcept
	{
		const auto* rec = FindRecord(a_summonId);
		return rec ? &rec->model : nullptr;
	}

	std::vector<EntityID> SummonRegistry::GetActiveSummonsForOwner(EntityID a_ownerId) const
	{
		std::vector<EntityID> result;
		for (const auto& [id, record] : _records) {
			if (record.ownerId == a_ownerId &&
			    !record.isDead &&
			    record.lifecycle == SummonLifecycleState::Active) {
				result.push_back(id);
			}
		}
		return result;
	}

	EntityID SummonRegistry::GetOwnerForSummon(EntityID a_summonId) const noexcept
	{
		const auto it = _records.find(a_summonId);
		return (it != _records.end()) ? it->second.ownerId : InvalidEntityID;
	}

	bool SummonRegistry::IsRegistered(EntityID a_summonId) const noexcept
	{
		return _records.find(a_summonId) != _records.end();
	}

	bool SummonRegistry::IsActive(EntityID a_summonId) const noexcept
	{
		const auto it = _records.find(a_summonId);
		return (it != _records.end()) &&
		       !it->second.isDead &&
		       it->second.lifecycle == SummonLifecycleState::Active;
	}

	std::size_t SummonRegistry::GetTotalCount() const noexcept
	{
		return _records.size();
	}

	std::size_t SummonRegistry::GetActiveCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& [id, record] : _records) {
			if (!record.isDead && record.lifecycle == SummonLifecycleState::Active) {
				++count;
			}
		}
		return count;
	}

	void SummonRegistry::ClearStaleRecords(float a_currentTimestamp, float a_gracePeriod)
	{
		std::erase_if(_records, [=](const auto& pair) {
			const auto& record = pair.second;
			if (record.isDead ||
			    record.lifecycle == SummonLifecycleState::Dispatched ||
			    record.lifecycle == SummonLifecycleState::Displaced) {
				if (a_gracePeriod <= 0.0f || a_currentTimestamp <= 0.0f) {
					return true;
				}
				return (a_currentTimestamp - record.deathTimestamp) >= a_gracePeriod;
			}
			return false;
		});
	}

	void SummonRegistry::Reset()
	{
		_records.clear();
		if (_logCallback) {
			_logCallback("[Registry] Registry reset: all records cleared");
		}
	}
}
