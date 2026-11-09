# Verification report (2026-11-10)

Three levels, kept strictly separate. Level 3 tracks in-game checks in Skyrim SE/AE.

---

## 1. Automated (passing)

Executable: `build\windows\x64\releasedbg\scf_tests.exe` — **20 cases, 0 failures, 0 failed checks**.  
Execution time: ~1-2 ms.

### Suite A: Gameplay FSM (`StateMachine`) — 8 tests
* `TestInitialStateAndFollow`: Newly spawned summon in `Idle` with order `Follow` immediately transitions to `FollowOwner`.
* `TestWaitGuardOrder`: Explicit `WaitGuard` order transitions the entity from `FollowOwner` into `Guard`.
* `TestPassiveModeIgnoresEnemies`: Under `AggressionMode::Passive`, even when a valid hostile target is detected 200u away, the entity strictly remains in `FollowOwner` and clears target ID.
* `TestAggressiveModeEngagesTarget`: Progression under `Aggressive` mode: Target detected (800u) -> `AcquireTarget` -> `Approach` -> melee range (<= 300u) -> `Engage`.
* `TestTargetLostDebounce`: Anti-flicker hysteresis: at `dt = 0.4s` and `0.8s` entity stays in `Engage`; at `1.1s` (> 1.0s limit) cleanly disengages to `FollowOwner`.
* `TestLeashBreakForcesReturn`: When fighting at 100u, if owner runs to 3000u (> 2500u threshold), summon drops combat and enters `Return`. Restores `FollowOwner` when back within 400u.
* `TestExplicitReturnOrder`: Player's direct `Return` order immediately interrupts combat and restores `Follow` once within follow range.
* `TestLogCallbackRecordsTransitions`: Transition callbacks fire with correct `EntityID`, `from`, `to`, and string reason.

### Suite B: Tactical Target Selection (`TargetSelector`) — 7 tests
* `TestTargetSelectorPassive`: Zero target output when summon is set to `Passive`.
* `TestTargetSelectorFriendlyFireGuard`: Automatic selection skips owner and fellow summons. Explicit `FocusTarget` commanded on the owner is rejected with log `"Explicit FocusTarget rejected: cannot target owner/ally"` and safely falls back to hostile candidates.
* `TestTargetSelectorAttackingOwnerPriority`: Candidate actively attacking owner (800u) is prioritized over a passive enemy closer (200u).
* `TestTargetSelectorStickyTargetBonus`: Fighting Candidate A (500u); Candidate B appears at 400u. Due to `currentTargetStickyBonus` (+400), summon continues attacking A, preventing target oscillation.
* `TestTargetSelectorFocusTargetOverride`: Player-designated target overrides closer automatic target.
* `TestTargetSelectorDeadOrDisabledIgnored`: Dead and disabled candidates are filtered out.
* `TestTargetSelectorDefensiveRequiresThreat`: Neutral/non-threatening NPCs are ignored under `Defensive` mode.

### Suite C: Squad Coordination (`SquadCoordinator`) — 5 tests
* `TestFormationOffsetsDistinct`: Verifies that a squad of 2 summons receives distinct, non-overlapping formation offsets around the owner (left-rear and right-rear).
* `TestTargetDistributionAcrossMultipleEnemies`: With 2 melee summons and 2 enemies, when `maxMeleeSlotsPerTarget = 1`, Summon 1 claims Enemy 1, and Summon 2 automatically distributes to Enemy 2 without dogpiling.
* `TestMeleeSlotLimitPreventsDogpiling`: With 3 melee summons and only 1 enemy (`maxMeleeSlotsPerTarget = 2`), first 2 summons claim the 2 slots, while the 3rd summon holds in formation rather than crowding the same point.
* `TestRangedAndMeleeIndependentSlots`: Verifies that melee and ranged attack slots are tracked independently on the same target.
* `TestSquadCoordinatorLogging`: Verifies that slot assignments are logged with summon ID, combat role, target ID, and current slot claim counts.

---

## 2. Static / offline (passing)

* **Compiler:** MSVC 14.44 (Visual Studio 2022 Build Tools) with standard `/std:c++latest` (C++23).
* **Warning level:** `/W4` (`allextra`), `/permissive-`, `/Zc:preprocessor`. Clean compilation with zero warnings in project code.
* **Precompiled Header (PCH):** [src/PCH.h](file:///D:/projects/Summon_Control_Framework/src/PCH.h) precompiles `RE/Skyrim.h`, `SKSE/SKSE.h`, and `spdlog`.
* **Multi-runtime Configuration:**
  * CommonLibSSE-NG compiled with `ENABLE_SKYRIM_SE=1` and `ENABLE_SKYRIM_AE=1`.
  * Target platform `windows|x64`.
  * Address Library runtime compatibility mode enabled (`SKSE::VersionIndependence::AddressLibrary`).
  * `SKSEPluginInfo` resource generated automatically via `commonlibsse-ng.plugin` rule.
* **Header Independence:** Core and AI modules compile in pure isolation without linking `commonlibsse-ng.lib` in the test target.

---

## 3. In game (traces in `SummonControlFramework.log`)

### Verified
| Component | Evidence |
|---|---|
| Plugin build output | `SummonControlFramework.dll` generated (496 KB, with `.pdb`) |
| Core unit tests | 20/20 passing via `scf_tests.exe` |

### Waiting for live Skyrim run
| Feature | Target verification |
|---|---|
| Plugin load | `SummonControlFramework v0.1.0 is loading...` in SKSE log |
| `RuntimeGate` check | Log lines verifying `Detected game version: 1.6.1170.0` |
| `kDataLoaded` receipt | Log line `Data loaded: SKSE message received, SCF FSM ready` |
| Summon registration | Log lines on casting `Conjure Familiar` / `Flame Atronach` |
| Friendly fire guard in game | Player weapon/spell hitting summon without hostility loop |
| Collision non-blocking | Player walking through summoned creature |
| Squad formation | Multiple summons moving in fan/flank formation behind player |
