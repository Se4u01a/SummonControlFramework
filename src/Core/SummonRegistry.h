#pragma once

#include "Core/EntityID.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace scf::core
{
	enum class SummonLifecycleState : std::uint8_t
	{
		Active,     // In loaded cell, fully active
		Suspended,  // Temporarily cell-detached, reference kept
		Displaced,  // Marked for dispel due to summon limit
		Dead,       // Actor died
		Dispatched  // Magic effect expired or dispelled
	};

	constexpr const char* ToString(SummonLifecycleState a_state) noexcept
	{
		switch (a_state) {
		case SummonLifecycleState::Active:     return "Active";
		case SummonLifecycleState::Suspended:  return "Suspended";
		case SummonLifecycleState::Displaced:  return "Displaced";
		case SummonLifecycleState::Dead:       return "Dead";
		case SummonLifecycleState::Dispatched: return "Dispatched";
		default:                               return "Unknown";
		}
	}

	enum class SummonType : std::uint8_t
	{
		SummonCreature, // Standard vanilla summon (Atronach, Familiar, Dremora)
		Reanimate,      // Dead Thrall / Zombie (post-MVP)
		Scripted        // Custom mod summon via Papyrus
	};

	constexpr const char* ToString(SummonType a_type) noexcept
	{
		switch (a_type) {
		case SummonType::SummonCreature: return "SummonCreature";
		case SummonType::Reanimate:      return "Reanimate";
		case SummonType::Scripted:       return "Scripted";
		default:                         return "Unknown";
		}
	}

	struct SummonRecord
	{
		EntityID summonId{ InvalidEntityID };
		EntityID ownerId{ InvalidEntityID };
		SummonType type{ SummonType::SummonCreature };
		SummonLifecycleState lifecycle{ SummonLifecycleState::Active };

		float remainingDuration{ -1.0f }; // Negative = permanent
		float registrationTimestamp{ 0.0f };

		bool isDead{ false };
		bool isSuspended{ false };
	};

	class SummonRegistry
	{
	public:
		using LogCallback = std::function<void(const char* a_message)>;

		SummonRegistry() = default;
		explicit SummonRegistry(LogCallback a_logCb) : _logCallback(std::move(a_logCb)) {}

		void SetLogCallback(LogCallback a_logCb) noexcept
		{
			_logCallback = std::move(a_logCb);
		}

		void SetDefaultMaxSummonsPerOwner(std::size_t a_max) noexcept
		{
			_defaultMaxSummonsPerOwner = a_max;
		}

		void SetOwnerMaxSummons(EntityID a_ownerId, std::size_t a_max);

		[[nodiscard]] std::size_t GetOwnerMaxSummons(EntityID a_ownerId) const noexcept;

		bool RegisterSummon(
			EntityID a_ownerId,
			EntityID a_summonId,
			SummonType a_type,
			float a_duration,
			float a_timestamp,
			EntityID* a_outDisplaced = nullptr);

		bool UnregisterSummon(EntityID a_summonId, const char* a_reason);

		void NotifyCellDetach(EntityID a_summonId);

		void NotifyCellAttach(EntityID a_summonId);

		void NotifyDeath(EntityID a_summonId);

		void NotifyEffectRemoved(EntityID a_ownerId, EntityID a_summonId);

		[[nodiscard]] const SummonRecord* FindRecord(EntityID a_summonId) const noexcept;

		[[nodiscard]] std::vector<EntityID> GetActiveSummonsForOwner(EntityID a_ownerId) const;

		[[nodiscard]] EntityID GetOwnerForSummon(EntityID a_summonId) const noexcept;

		[[nodiscard]] bool IsRegistered(EntityID a_summonId) const noexcept;

		[[nodiscard]] bool IsActive(EntityID a_summonId) const noexcept;

		[[nodiscard]] std::size_t GetTotalCount() const noexcept;

		[[nodiscard]] std::size_t GetActiveCount() const noexcept;

		[[nodiscard]] const std::unordered_map<EntityID, SummonRecord>& GetAllRecords() const noexcept
		{
			return _records;
		}

		void ClearStaleRecords();

		void Reset();

	private:
		[[nodiscard]] EntityID FindOldestActiveSummon(EntityID a_ownerId) const noexcept;

		std::unordered_map<EntityID, SummonRecord> _records;
		std::unordered_map<EntityID, std::size_t> _ownerMaxSummons;
		std::size_t _defaultMaxSummonsPerOwner{ 1 };
		LogCallback _logCallback{ nullptr };
	};
}
