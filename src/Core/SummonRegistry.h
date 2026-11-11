#pragma once

#include "Core/EntityID.h"
#include "Core/SummonModel.h"

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

	// A negative remaining duration means "no timer at all": the summon is not measured
	// against SCF's clock (Dead Thrall, a Papyrus registration without an engine effect).
	constexpr float kPermanentDuration{ -1.0f };

	// Duration policy for a record refreshed from an engine active effect.
	//
	// A duration of 0 is not "expires now": a conjuration effect without duration is a
	// permanent one, and Dead Thrall - the vanilla permanent reanimate - reports no timer.
	// Such an effect keeps kPermanentDuration, so nothing can read the record as a summon
	// about to expire. A positive duration becomes the remaining time, clamped at zero:
	// the engine ends the effect itself, the registry only reports what is left.
	[[nodiscard]] inline float RemainingDurationFromEffect(float a_effectDuration, float a_effectElapsed) noexcept
	{
		if (!(a_effectDuration > 0.0f)) {
			return kPermanentDuration;
		}
		const float remaining = a_effectDuration - a_effectElapsed;
		return remaining > 0.0f ? remaining : 0.0f;
	}

	struct SummonRecord
	{
		EntityID summonId{ InvalidEntityID };
		EntityID ownerId{ InvalidEntityID };
		SummonType type{ SummonType::SummonCreature };
		SummonLifecycleState lifecycle{ SummonLifecycleState::Active };

		float remainingDuration{ kPermanentDuration }; // Negative = permanent
		float registrationTimestamp{ 0.0f };
		float deathTimestamp{ 0.0f };     // Time when actor died or effect dispelled

		bool isDead{ false };
		bool isSuspended{ false };

		// Registered through the Papyrus API (SCF.RegisterSummon): the script, not the
		// engine's commanded-actor roster, owns the record's lifetime. The tactical sync
		// therefore never drops it for "not in the roster"; it goes away on
		// UnregisterSummon, on the actor's death, or when the form itself is gone.
		bool explicitRegistration{ false };

		SummonModel model{};
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

		// a_explicitRegistration marks a record whose lifetime SCF owns instead of the
		// engine roster (Papyrus registration). A roster refresh never clears the flag: the
		// sync re-registers every tracked summon on every tick and would otherwise wipe it.
		//
		// The owner's conjuration limit applies to roster-backed summons only: a
		// roster-independent record (see IsRosterIndependent) never displaces one and is
		// never displaced itself, so registering a scripted summon cannot cost the player a
		// conjuration slot and a full roster cannot silently drop it.
		bool RegisterSummon(
			EntityID a_ownerId,
			EntityID a_summonId,
			SummonType a_type,
			float a_duration,
			float a_timestamp,
			EntityID* a_outDisplaced = nullptr,
			bool a_explicitRegistration = false);

		bool UnregisterSummon(EntityID a_summonId, const char* a_reason);

		// True when the engine's commanded-actor roster is not authoritative for this
		// record's lifetime: an explicitly registered summon (released by UnregisterSummon,
		// death or a vanished form) or a reanimated corpse (released when the reanimate
		// effect ends and the engine stops reporting the owner as its commanding actor).
		//
		// Such a record also does not occupy a conjuration slot: it never displaces a
		// roster-backed summon to make room, and it is never displaced itself - nothing but
		// its own signal would release it, so a displacement would leak a dead record.
		[[nodiscard]] bool IsRosterIndependent(EntityID a_summonId) const noexcept;

		// The records the tactical sync releases: active summons of a_ownerId that the
		// engine roster no longer lists and whose lifetime the roster owns. Explicitly
		// registered summons and reanimated corpses are never returned (see
		// IsRosterIndependent). Sorted ascending, so the release order is stable.
		[[nodiscard]] std::vector<EntityID> GetOrphanedSummons(
			EntityID a_ownerId,
			const std::vector<EntityID>& a_engineRoster) const;

		void NotifyCellDetach(EntityID a_summonId);

		void NotifyCellAttach(EntityID a_summonId);

		void NotifyDeath(EntityID a_summonId, float a_timestamp = 0.0f);

		void NotifyEffectRemoved(EntityID a_ownerId, EntityID a_summonId, float a_timestamp = 0.0f);

		[[nodiscard]] const SummonRecord* FindRecord(EntityID a_summonId) const noexcept;
		[[nodiscard]] SummonRecord* FindRecord(EntityID a_summonId) noexcept;

		[[nodiscard]] SummonModel* GetModel(EntityID a_summonId) noexcept;
		[[nodiscard]] const SummonModel* GetModel(EntityID a_summonId) const noexcept;

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

		void ClearStaleRecords(float a_currentTimestamp = 0.0f, float a_gracePeriod = 0.0f);

		void Reset();

	private:
		[[nodiscard]] EntityID FindOldestActiveSummon(EntityID a_ownerId) const noexcept;

		std::unordered_map<EntityID, SummonRecord> _records;
		std::unordered_map<EntityID, std::size_t> _ownerMaxSummons;
		std::size_t _defaultMaxSummonsPerOwner{ 1 };
		LogCallback _logCallback{ nullptr };
	};
}
