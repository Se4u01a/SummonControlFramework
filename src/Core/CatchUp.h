#pragma once

#include "Core/Formation.h"
#include "Core/Settings.h"
#include "Core/Types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace scf::core
{
	// ------------------------------------------------------------------
	// Stage 7: the emergency catch-up warp.
	//
	// Following is a race the summon often loses: a door it cannot open, a ledge, a
	// closed gate, a player on a horse. When the follow rules have failed for long
	// enough, the summon is recalled behind its owner instead of being left behind for
	// good. A recall is a TELEPORT - a recovery, never a way of moving - so everything
	// that decides whether it may happen, when it may repeat, and where the summon may
	// land lives here as pure rules over facts collected by the engine driver. Only the
	// actor write itself (one SetPosition + MoveToNearestNavmesh) happens in
	// src/Engine/SummonMovement.cpp.
	//
	// Line of sight is deliberately NOT a criterion. A summon behind a wall with a
	// perfectly walkable path is not stuck, and one in plain sight of the owner can be
	// wedged in a door frame. The two measurements are the distance to the owner and
	// whether the summon actually closed it (progress); everything else below is a
	// safety gate around the teleport, not a way to detect it.
	// ------------------------------------------------------------------

	// Why the recall fires.
	enum class CatchUpTrigger : std::uint8_t
	{
		None,
		Distance,  // the owner is at or beyond CatchUpTuning::catchUpDistance
		NoProgress // the summon has not closed the gap for CatchUpTuning::stuckSeconds
	};

	constexpr const char* ToString(CatchUpTrigger a_trigger) noexcept
	{
		switch (a_trigger) {
		case CatchUpTrigger::None:       return "None";
		case CatchUpTrigger::Distance:   return "Distance";
		case CatchUpTrigger::NoProgress: return "NoProgress";
		default:                         return "Unknown";
		}
	}

	// Why a recall that the measurements called for did not happen. The same names are
	// counted in CatchUpStats, so a field report says WHICH kind of refusal it was.
	enum class CatchUpRefusal : std::uint8_t
	{
		None = 0,
		Disabled,             // [Recovery] bEnableCatchUpWarp is off
		OrderNotFollowOrReturn,
		SummonInCombat,       // a fighting summon is driven by the combat AI
		OwnerInCombat,        // the player is fighting: the fight owns the moment
		DifferentSpace,       // interior/exterior or worldspace mismatch, or an unloaded cell
		OwnerAirborne,        // nothing to land next to
		OwnerSwimming,
		OwnerMounted,
		Cooldown,             // a recall (or a refused attempt) just happened
		AwaitingProgress,     // the stuck trigger waits for real movement after a recall
		NotArmed,             // the distance trigger waits for the hysteresis re-arm
		NoSafeSpot,           // no landing candidate passed the engine checks
		Count
	};

	constexpr const char* ToString(CatchUpRefusal a_refusal) noexcept
	{
		switch (a_refusal) {
		case CatchUpRefusal::None:                  return "None";
		case CatchUpRefusal::Disabled:              return "Disabled";
		case CatchUpRefusal::OrderNotFollowOrReturn: return "OrderNotFollowOrReturn";
		case CatchUpRefusal::SummonInCombat:        return "SummonInCombat";
		case CatchUpRefusal::OwnerInCombat:         return "OwnerInCombat";
		case CatchUpRefusal::DifferentSpace:        return "DifferentSpace";
		case CatchUpRefusal::OwnerAirborne:         return "OwnerAirborne";
		case CatchUpRefusal::OwnerSwimming:         return "OwnerSwimming";
		case CatchUpRefusal::OwnerMounted:          return "OwnerMounted";
		case CatchUpRefusal::Cooldown:              return "Cooldown";
		case CatchUpRefusal::AwaitingProgress:      return "AwaitingProgress";
		case CatchUpRefusal::NotArmed:              return "NotArmed";
		case CatchUpRefusal::NoSafeSpot:            return "NoSafeSpot";
		default:                                    return "Unknown";
		}
	}

	inline constexpr std::size_t kCatchUpRefusalCount = static_cast<std::size_t>(CatchUpRefusal::Count);

	// Tunables of the recall. Everything is pure data passed by argument, so Core never
	// reads an INI; the driver builds them from the settings object it already receives
	// (CatchUpTuningFrom) or from the defaults below.
	//
	// The three keys that already exist in [Recovery] keep their names; every other
	// field is a documented default until the settings expose it (the INI key each one
	// is meant to get is named next to it).
	struct CatchUpTuning
	{
		// --- when ---
		bool enableWarp{ true };                // [Recovery] bEnableCatchUpWarp
		float catchUpDistance{ 3500.0f };       // [Recovery] fCatchUpDistance
		float stuckSeconds{ 3.0f };             // [Recovery] fStuckWarpSeconds
		float stuckMinDistance{ 300.0f };       // [Recovery] fStuckWarpMinDistance
		float progressEpsilon{ 50.0f };         // [Recovery] fCatchUpProgressEpsilon: lag shrink that counts as progress
		float cooldownSeconds{ 5.0f };          // [Recovery] fCatchUpCooldownSeconds: pause after every attempt
		float rearmRatio{ 0.5f };               // [Recovery] fCatchUpRearmRatio: the distance gate re-arms below catchUpDistance * ratio
		bool allowWhileSummonInCombat{ false }; // [Recovery] bCatchUpWarpSummonCombat: explicit override, off by default
		bool allowWhileOwnerInCombat{ false };  // [Recovery] bCatchUpWarpOwnerCombat: explicit override, off by default

		// --- where ---
		float landingDistance{ 200.0f };        // [Recovery] fCatchUpLandingDistance: how far behind the owner
		float landingClearance{ 120.0f };       // [Recovery] fCatchUpLandingClearance: no other actor / geometry this close
		float landingMaxRise{ 200.0f };         // [Recovery] fCatchUpLandingMaxRise: highest ground above the owner's feet
		float landingMaxDrop{ 600.0f };         // [Recovery] fCatchUpLandingMaxDrop: deepest ground below the owner's feet
		float landingWaterClearance{ 24.0f };   // [Recovery] fCatchUpWaterClearance: ground must sit this far above the water
	};

	// Recall tunables derived from the configuration the driver already holds: the
	// operator switch and the three thresholds come from [Recovery]. The recall is also
	// never allowed inside the sprint band - a recall at or below the sprint threshold
	// would make the sprint pace dead code - so the distance is raised to
	// FollowTuning::sprintAtDistance when the configuration puts it lower.
	CatchUpTuning CatchUpTuningFrom(const SettingsData& a_settings) noexcept;

	// The persistent per-summon memory of the race. The driver owns one of these per
	// summon; Core only reads and updates it through the functions below.
	struct CatchUpMemory
	{
		float lastWarpAt{ -1.0e9f };    // last performed recall
		float lastAttemptAt{ -1.0e9f }; // last attempt, performed OR refused (the cooldown anchor)
		float lastProgressAt{ 0.0f };   // the clock the stuck trigger reads
		float bestLag{ 1.0e9f };        // smallest lag seen since the last reset
		bool awaitingProgress{ false }; // a recall happened; only real progress re-arms the stuck trigger
		bool distanceArmed{ true };     // hysteresis gate of the distance trigger
	};

	// Convenience for the engine: how long the summon has been failing to close the gap.
	inline float SecondsWithoutProgress(const CatchUpMemory& a_memory, float a_now) noexcept
	{
		const float elapsed = a_now - a_memory.lastProgressAt;
		return elapsed > 0.0f ? elapsed : 0.0f;
	}

	struct CatchUpObservation
	{
		float now{ 0.0f };
		float lagDistance{ 0.0f };     // horizontal distance left to the follow goal (slot / owner)
		float distanceToOwner{ 0.0f }; // horizontal distance to the owner
		float holdRadius{ 0.0f };      // inside this radius the summon is holding its slot
		bool isMoving{ false };        // the actor actually travelled since the previous sample
	};

	// Updates the progress clock and the hysteresis gate. Called once per tick, before
	// DecideCatchUp.
	//
	// Progress is any of: the actor really moved, the lag grew no larger than the
	// hold radius (standing in the slot is not being stuck), or the lag shrank by at
	// least progressEpsilon since its best value. Without progress the clock keeps
	// running, which is the ONLY way the stuck trigger ever matures.
	void ObserveCatchUp(CatchUpMemory& a_memory, const CatchUpObservation& a_observation, const CatchUpTuning& a_tuning) noexcept;

	// The world state the safety gates read, as observed by the driver this tick. The
	// measurements (distance, lag, clock) come from CatchUpObservation.
	struct CatchUpContext
	{
		SummonOrder order{ SummonOrder::Follow };
		bool summonInCombat{ false };   // the engine combat flag
		bool combatPermitted{ true };   // the CombatPolicy answer for this tick
		bool ownerInCombat{ false };
		bool sameSpace{ true };         // both actors share one loaded cell / worldspace
		bool ownerAirborne{ false };    // flying (dragon mount, levitation, mid-jump wings)
		bool ownerSwimming{ false };
		bool ownerMounted{ false };
	};

	struct CatchUpDecision
	{
		bool warp{ false };
		// The measurement that called for the recall - set even when a gate below refuses
		// it, so the statistics report what the race looked like, not only what ran.
		CatchUpTrigger trigger{ CatchUpTrigger::None };
		CatchUpRefusal refusal{ CatchUpRefusal::None };
	};

	// Decides whether the summon must be recalled behind its owner this tick.
	//
	// A refusal is only reported when the measurements actually called for a recall
	// (the summon is far enough away, or has been stuck long enough); a summon that is
	// just walking along reports None and is not counted anywhere. That keeps the
	// statistics meaning "events", not "ticks".
	//
	// Order of the gates, highest priority first:
	//   1. Trigger: distance beyond catchUpDistance with the hysteresis gate armed, or
	//      no progress for stuckSeconds while more than stuckMinDistance behind.
	//   2. The order must be Follow or Return (the two that bring the summon home).
	//   3. The operator switch must be on.
	//   4. A summon the CombatPolicy lets fight is never teleported - the fight owns its
	//      locomotion - unless the explicit override is on. The same for the owner.
	//   5. Both actors must share one loaded space: a cross-cell recall is not a position
	//      write, and the vanilla summon snap owns that case.
	//   6. The owner must be on the ground: no landing spot can be trusted next to a
	//      flying, swimming or mounted player.
	//   7. The hysteresis gate must be armed, the cooldown must have elapsed, and after a
	//      recall the stuck trigger must have seen real progress again.
	CatchUpDecision DecideCatchUp(
		const CatchUpContext& a_context,
		const CatchUpObservation& a_observation,
		const CatchUpMemory& a_memory,
		const CatchUpTuning& a_tuning) noexcept;

	// A recall was performed: the cooldown starts, the progress clock and the best lag
	// are reset (the old gap is meaningless now), the hysteresis gate closes until the
	// summon is back inside the exit threshold, and the stuck trigger waits for proof
	// that the summon can move again.
	void MarkCatchUpWarped(CatchUpMemory& a_memory, float a_now) noexcept;

	// An attempt was made and refused (no safe spot, no havok world). The cooldown still
	// applies, so a hopeless spot is not probed on every tick.
	void MarkCatchUpAttempt(CatchUpMemory& a_memory, float a_now) noexcept;

	// A new movement mode started the measurement over. The warp cooldown survives: a
	// teleport that just happened must cool down whatever order switch came with it.
	void ResetCatchUpProgress(CatchUpMemory& a_memory, float a_now) noexcept;

	// ------------------------------------------------------------------
	// Landing spot.
	// ------------------------------------------------------------------

	// The candidate list is built once, before any engine query, so the order the warp
	// tries is a rule, not an accident of the driver's loop.
	struct LandingCandidate
	{
		Vec3 position{};            // world point at the owner's own height
		float bearingDegrees{ 0.0f }; // relative to the owner's heading; +/-180 is directly behind
	};

	// Upper bound of the candidate list (the formation slot, two behind, two rear
	// flanks and two sides, minus duplicates).
	inline constexpr std::size_t kMaxLandingCandidates = 8;

	// Builds the ordered list of spots a recall may use:
	//   1. the summon's own formation slot (it keeps the squad together, and it is where
	//      the follow rules were trying to take the actor anyway),
	//   2. straight behind the owner at the landing distance,
	//   3. straight behind, farther back (a narrow corridor),
	//   4. the two rear flanks (+/-135 degrees),
	//   5. the two sides (+/-90 degrees).
	// Duplicates (a three-summon squad's rear marker is "straight behind", which the plain
	// candidate would repeat) and points on top of the owner are dropped. Returns the
	// number of candidates written.
	std::size_t BuildLandingCandidates(
		const Vec3& a_ownerPosition,
		float a_ownerYawRadians,
		std::size_t a_slotIndex,
		std::size_t a_squadSize,
		const FormationLayout& a_formation,
		const CatchUpTuning& a_tuning,
		LandingCandidate* a_out,
		std::size_t a_capacity) noexcept;

	// What the engine measured about one candidate. Everything here is an observation;
	// the accept/reject rule is IsLandingSpotUsable.
	struct LandingObservation
	{
		bool hasSpace{ false };        // the point resolves in the owner's loaded cell / worldspace
		bool groundFound{ false };     // a downward probe found walkable ground there
		float groundRise{ 0.0f };      // that ground's height relative to the candidate's own Z
		bool blocked{ false };         // solid geometry inside the landing clearance, or a ledge
		float nearestActor{ 1.0e9f };  // horizontal distance to the nearest living actor (1e9 = none)
		float waterRise{ 1.0e9f };     // ground height above the local water surface (1e9 = dry)
	};

	// True when the summon may be placed on this candidate at all.
	bool IsLandingSpotUsable(const LandingObservation& a_observation, const CatchUpTuning& a_tuning) noexcept;

	struct LandingSelection
	{
		bool found{ false };
		std::size_t index{ 0 };
		Vec3 position{}; // the candidate with the probed ground height substituted
		CatchUpRefusal refusal{ CatchUpRefusal::NoSafeSpot };
	};

	// Picks the first usable candidate. When none is usable the recall does NOT happen:
	// there is no fallback spot that is "probably fine".
	LandingSelection SelectLandingSpot(
		const LandingCandidate* a_candidates,
		const LandingObservation* a_observations,
		std::size_t a_count,
		const CatchUpTuning& a_tuning) noexcept;

	// ------------------------------------------------------------------
	// Statistics (the "scf status" style summary).
	// ------------------------------------------------------------------

	struct CatchUpStats
	{
		// How often the measurements (distance / no progress) called for a recall - the
		// trigger of every performed OR refused attempt, counted once per event.
		std::uint32_t distanceTriggers{ 0 };
		std::uint32_t stuckTriggers{ 0 };
		std::uint32_t warps{ 0 };
		std::uint32_t refusals[kCatchUpRefusalCount]{};
		CatchUpRefusal lastRefusal{ CatchUpRefusal::None };
		CatchUpTrigger lastTrigger{ CatchUpTrigger::None };
		float lastWarpAt{ -1.0e9f };
	};

	void RecordCatchUpTrigger(CatchUpStats& a_stats, CatchUpTrigger a_trigger) noexcept;
	void RecordCatchUpWarp(CatchUpStats& a_stats, CatchUpTrigger a_trigger, float a_now) noexcept;
	void RecordCatchUpRefusal(CatchUpStats& a_stats, CatchUpRefusal a_refusal) noexcept;
	void ResetCatchUpStats(CatchUpStats& a_stats) noexcept;

	// One line, log-safe: the counters plus a non-zero refusal per cause. Format:
	//   catch-up: warps=2 triggers(distance=1, no-progress=2) Cooldown=1 NoSafeSpot=1 last=NoSafeSpot
	std::string FormatCatchUpStats(const CatchUpStats& a_stats);
}
