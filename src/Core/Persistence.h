#pragma once

#include "Core/EntityID.h"
#include "Core/SummonModel.h"
#include "Core/Types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace scf::core
{
	// PoE2 stage M4: persistent summons, their Magicka reserve ("Spirit") and auto-revive.
	//
	// This header is the pure part: rules, slot lifecycle and the co-save codec. It never
	// touches the engine and never reads INI files - the caller builds the settings
	// snapshot (src/Core/Settings) and the engine adapter (src/Engine/Persistence) applies
	// the decisions with CommonLib.
	//
	// Scope guard: by default ANY summon-creature spell cast by the player is managed,
	// including a mod spell (the usual "Wolf Spirit" style summon) and a vanilla spell
	// whose record is overridden by a patch. `vanillaSpellsOnly` narrows that to spells
	// whose ORIGINAL source file is an official master. The blacklist always wins.
	//
	// PoE2 rules modelled here:
	//   * a persistent summon's conjuration effect does not expire while the mode is on;
	//   * a dead persistent summon is re-cast by its owner after `reviveDelaySeconds`,
	//     but a revive waits while another summon of the same owner died less than
	//     `recentDeathWindowSeconds` ago (the timer restarts on that death);
	//   * the same slot keeps the order and the aggression mode across the revive;
	//   * persistent summons reserve max Magicka, and the reserve is released when the
	//     slot is gone (dispel, mode off, revive given up) - not merely while it is dead.

	// Settings snapshot. Values come from the INI (report lists the keys); this module
	// clamps them, it does not parse them.
	struct PersistenceSettings
	{
		bool persistentSummons{ false };          // bPersistentSummons
		bool autoRevive{ true };                  // bAutoRevive
		bool vanillaSpellsOnly{ false };          // bPersistVanillaSpellsOnly
		float reviveDelaySeconds{ 7.0f };         // fReviveDelaySeconds      [1..120]
		float recentDeathWindowSeconds{ 10.0f };  // fRecentDeathWindowSeconds [0..300]; 0 = rule off
		float magickaReservePerSummon{ 50.0f };   // fMagickaReservePerSummon  [0..2000]
		float minMagickaFraction{ 0.1f };         // fMinMagickaFraction       [0..0.9]
		float persistentEffectSeconds{ 86400.0f };// fPersistentEffectSeconds  [60..604800]
		float effectExtendBelowSeconds{ 300.0f }; // fEffectExtendBelowSeconds [5..3600]
		float reviveCastTimeoutSeconds{ 6.0f };   // fReviveCastTimeoutSeconds [1..30]
		std::uint32_t maxReviveAttempts{ 3 };     // uMaxReviveAttempts        [1..10]
		std::size_t maxTrackedSlots{ 32 };        // uMaxTrackedSlots          [1..64]

		// Full runtime FormIDs of summon spells that must never be made persistent
		// (scripted summons, house rules, a spell a mod owns in a way that conflicts with
		// re-casting). Resolved from sBlacklistedSpells by the caller with the data
		// handler; 0 entries are ignored.
		std::vector<std::uint32_t> blacklistedSpellIds;

		// Clamped copy with finite floats. Never throws. `vanillaSpellsOnly` is a bool and
		// is carried over as-is (the defaulted operator== compares it too).
		[[nodiscard]] PersistenceSettings Sanitized() const noexcept;

		// True when the spell is not blacklisted (0 is never allowed).
		[[nodiscard]] bool IsSpellAllowed(std::uint32_t a_spellFormId) const noexcept;

		bool operator==(const PersistenceSettings&) const = default;
	};

	enum class PersistentSlotState : std::uint8_t
	{
		Alive,    // tracked and alive (actorId may still be Invalid right after a load)
		Dead,     // died, waiting for the revive timer / the recent-death rule
		Reviving  // the revive cast has been issued, waiting for the new actor
	};

	const char* ToString(PersistentSlotState a_state) noexcept;

	// The part of a SummonModel that must survive death and save/load.
	struct SummonIntent
	{
		SummonOrder order{ SummonOrder::Follow };
		AggressionMode aggression{ AggressionMode::Defensive };
		EntityID focusTargetId{ InvalidEntityID };

		bool operator==(const SummonIntent&) const = default;
	};

	struct PersistentSlot
	{
		EntityID slotId{ InvalidEntityID };   // internal handle, allocated by the state
		EntityID actorId{ InvalidEntityID };  // InvalidEntityID = restored slot not bound yet
		std::uint32_t spellFormId{ 0 };       // conjuration spell to re-cast on revive
		PersistentSlotState state{ PersistentSlotState::Alive };

		float diedAt{ 0.0f };
		float reviveAt{ 0.0f };
		float castAt{ 0.0f };
		std::uint32_t reviveAttempts{ 0 };

		// True between a revive / restore and the moment the engine writes the intent
		// into the new actor's SummonModel; intent capture is paused meanwhile.
		bool intentPending{ false };

		SummonIntent intent{};
	};

	// One slot as written to the co-save. Actor FormIDs are not stored: summoned actors
	// get temporary FormIDs that do not survive a load, so the live roster is matched by
	// spell instead (see PersistenceState::BindAliveUnbound).
	struct SavedSlot
	{
		std::uint32_t spellFormId{ 0 };
		PersistentSlotState state{ PersistentSlotState::Dead };
		float remainingReviveSeconds{ 0.0f };
		std::uint32_t reviveAttempts{ 0 };
		SummonIntent intent{};
	};

	struct SavedState
	{
		float reserveApplied{ 0.0f };
		std::vector<SavedSlot> slots;
	};

	// Co-save payload codec: little-endian, magic 'SCFP', version 1. DecodeState returns
	// std::nullopt for a foreign/short/newer payload; it never trusts the slot count.
	std::vector<std::uint8_t> EncodeState(const SavedState& a_state);
	std::optional<SavedState> DecodeState(const std::uint8_t* a_data, std::size_t a_size);
	std::optional<SavedState> DecodeState(const std::vector<std::uint8_t>& a_data);

	// True when a persistent effect is close enough to its end to be refreshed
	// (the adapter then writes duration = persistentEffectSeconds, elapsed = 0).
	[[nodiscard]] bool NeedsPersistentEffectRefresh(float a_remainingSeconds, const PersistenceSettings& a_settings) noexcept;

	// Scope guard shared with the adapter: may this summoned actor be managed as a
	// persistent summon?
	//   * a_isSummonCreature - the spell has an effect with archetype kSummonCreature;
	//   * a_castByPlayer     - the active effect's caster is the owner player;
	//   * a_originIsOfficialMaster - the ORIGINAL source file of the spell record
	//     (TESForm::GetFile(0), not the winning override) is Skyrim/Update/Dawnguard/
	//     HearthFires/Dragonborn. Consulted only when `vanillaSpellsOnly` is on: a mod
	//     summon spell (the usual "Wolf Spirit") and a vanilla spell overridden by a
	//     patch are both tracked by default;
	//   * a_blacklisted      - the spell FormID is in `blacklistedSpellIds`.
	// The blacklist and the caster check always win; the vanilla-only flag can only
	// narrow the set further.
	[[nodiscard]] bool ShouldTrackSummonSpell(
		bool a_isSummonCreature,
		bool a_castByPlayer,
		bool a_originIsOfficialMaster,
		bool a_blacklisted,
		const PersistenceSettings& a_settings) noexcept;

	// Reserve of max Magicka for `a_trackedSummons` persistent slots, given the baseline
	// max Magicka (before the reserve). Never pushes max Magicka below
	// `minMagickaFraction` of the baseline.
	[[nodiscard]] float ComputeMagickaReserve(std::size_t a_trackedSummons, float a_maxMagicka, const PersistenceSettings& a_settings) noexcept;

	// Slots, timers and the rules that decide when a dead persistent summon is re-cast.
	// Time is a caller-supplied monotonic clock in seconds (the scheduler clock); the state
	// stores relative countdowns in the co-save so a load can rebase everything.
	class PersistenceState
	{
	public:
		void SetSettings(const PersistenceSettings& a_settings);
		[[nodiscard]] const PersistenceSettings& GetSettings() const noexcept { return _settings; }

		[[nodiscard]] const std::vector<PersistentSlot>& GetSlots() const noexcept { return _slots; }
		[[nodiscard]] std::size_t GetSlotCount() const noexcept { return _slots.size(); }

		[[nodiscard]] PersistentSlot* FindSlot(EntityID a_slotId) noexcept;
		[[nodiscard]] const PersistentSlot* FindSlot(EntityID a_slotId) const noexcept;
		[[nodiscard]] PersistentSlot* FindByActor(EntityID a_actorId) noexcept;
		[[nodiscard]] const PersistentSlot* FindByActor(EntityID a_actorId) const noexcept;

		// Track a live summon as persistent. Idempotent: the same actor keeps its slot.
		// Returns nullptr when the slot cap is reached.
		PersistentSlot* TrackAlive(EntityID a_actorId, std::uint32_t a_spellFormId, float a_now);

		// Bind the first restored Alive slot with this spell that has no actor yet.
		bool BindAliveUnbound(std::uint32_t a_spellFormId, EntityID a_actorId, float a_now);

		// Bind a Reviving slot to the actor the revive cast produced; restores the intent.
		bool BindReviving(std::uint32_t a_spellFormId, EntityID a_actorId, float a_now);

		void Remove(EntityID a_slotId);
		void RemoveByActor(EntityID a_actorId);
		void Clear();

		// Remembers any summon death for the PoE2 "nobody died recently" rule. Works for
		// tracked and untracked summons, so the rule also honours non-persistent minions.
		// Returns true only for the first report of that summon: the adapter reports a
		// dead actor on every tick while it stays in commandedActors.
		bool RecordDeath(EntityID a_summonId, float a_now);

		// Alive -> Dead. Returns false when the slot does not exist or is already dead,
		// so a repeatedly reported death is harmless.
		bool MarkDead(EntityID a_actorId, float a_now);

		// Deaths of other summons inside the window; std::nullopt when none is recent.
		[[nodiscard]] std::optional<float> LatestOtherDeath(EntityID a_excludedSummonId, float a_now) const;

		[[nodiscard]] bool IsRevivePaused(EntityID a_slotId, float a_now) const;

		// Dead slots whose revive may be cast now. While another summon died recently the
		// slot's reviveAt is pushed to that death + the window (the timer restarts).
		std::vector<EntityID> CollectDueRevives(float a_now);

		void MarkReviveCast(EntityID a_slotId, float a_now);

		// Slot ids whose revive cast produced no actor in time. The caller then calls
		// MarkReviveFailed for each.
		std::vector<EntityID> ExpireReviveCasts(float a_now);

		// A failed revive: retry after the delay, or drop the slot after
		// maxReviveAttempts. Returns true when the slot was kept for another attempt.
		bool MarkReviveFailed(EntityID a_slotId, float a_now);

		// Number of slots that keep the Magicka reserve: every Dead slot and every Alive
		// slot bound to an actor. A restored slot that was never matched to an actor does
		// not reserve (the summon is most likely gone from the loaded save).
		[[nodiscard]] std::size_t ReservedSummonCount() const noexcept;

		[[nodiscard]] float GetAppliedReserve() const noexcept { return _appliedReserve; }
		void SetAppliedReserve(float a_reserve) noexcept;

		[[nodiscard]] float DesiredReserve(float a_maxMagicka) const noexcept;
		[[nodiscard]] float PendingReserveDelta(float a_maxMagicka) const noexcept;

		// Intent capture (Alive slots only, paused while intentPending).
		void UpdateIntentFromModel(EntityID a_actorId, const SummonModel& a_model);
		// Returns and clears the pending intent of the slot bound to the actor.
		std::optional<SummonIntent> TakePendingIntent(EntityID a_actorId);

		// Snapshot for the co-save. Dead slots store the remaining countdown; a Reviving
		// slot is stored as Dead with a fresh delay (the cast cannot be resumed after load).
		[[nodiscard]] SavedState Save(float a_now) const;

		// Replaces the state with a loaded snapshot, rebasing countdowns on `a_now`.
		// Applied reserve is taken from the snapshot (the loaded actor already carries
		// the permanent modifier, if it was saved).
		void Restore(const SavedState& a_saved, float a_now);

	private:
		void PruneDeaths(float a_now) noexcept;
		PersistentSlot& AppendSlot(std::uint32_t a_spellFormId);

		PersistenceSettings _settings{};
		std::vector<PersistentSlot> _slots;
		// summonId -> time of its last death, for the recent-death rule
		std::vector<std::pair<EntityID, float>> _deaths;
		std::uint32_t _nextSlotId{ 0 };
		float _appliedReserve{ 0.0f };
	};
}
