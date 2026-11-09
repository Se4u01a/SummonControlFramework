# Verification report (2026-11-10)

Three levels, kept strictly separate. Level 3 tracks in-game checks in Skyrim SE/AE.

---

## 1. Automated (passing)

Executable: `build\windows\x64\releasedbg\scf_tests.exe` — **15 cases, 0 failures, 0 failed checks**.  
Execution time: ~1-2 ms.

### Suite A: Gameplay FSM (`StateMachine`) — 8 tests
* `TestInitialStateAndFollow`: Verifies that a newly spawned summon in `Idle` with order `Follow` immediately transitions to `FollowOwner`.
* `TestWaitGuardOrder`: Verifies that an explicit `WaitGuard` order transitions the entity from `FollowOwner` into `Guard`.
* `TestPassiveModeIgnoresEnemies`: Verifies that under `AggressionMode::Passive`, even when a valid hostile target is detected 200 units away, the entity strictly remains in `FollowOwner` and clears any target ID.
* `TestAggressiveModeEngagesTarget`: Verifies the complete tactical progression under `Aggressive` mode:
  1. Target detected at 800u -> transitions to `AcquireTarget`.
  2. Next evaluation tick -> transitions to `Approach` (closing distance).
  3. Reaching melee range (<= 300u) -> transitions to `Engage`.
* `TestTargetLostDebounce`: Verifies anti-flicker hysteresis. When a target disappears behind cover:
  1. At `dt = 0.4s` (debounce limit 1.0s) -> remains in `Engage`.
  2. At `dt = 0.8s` -> still remains in `Engage`.
  3. At `dt = 1.1s` (> 1.0s limit) -> cleanly disengages and returns to `FollowOwner`.
* `TestLeashBreakForcesReturn`: Verifies leash safety constraint. When fighting a target at 100u, if the owner runs to 3000u (> 2500u threshold), the summon drops combat, clears target, and enters `Return`. When distance drops to 400u, transitions to `FollowOwner`.
* `TestExplicitReturnOrder`: Verifies that the player's direct command `Return` interrupts active combat immediately and restores `Follow` once within follow distance.
* `TestLogCallbackRecordsTransitions`: Verifies that transition callbacks fire with correct `EntityID`, `from`, `to`, and string reason.

### Suite B: Tactical Target Selection (`TargetSelector`) — 7 tests
* `TestTargetSelectorPassive`: Verifies zero target output when summon is set to `Passive`.
* `TestTargetSelectorFriendlyFireGuard`:
  1. Automatic selection: candidate list contains owner, a fellow summon, and an enemy. Selector selects the enemy, ignoring owner and ally.
  2. Explicit `FocusTarget`: player commands focus on the owner. Selector rejects the command, logs `"Explicit FocusTarget rejected: cannot target owner/ally"`, and falls back to the best enemy candidate.
* `TestTargetSelectorAttackingOwnerPriority`: Verifies threat weighting. Candidate A is close (200u) but passive; Candidate B is far (800u) but actively attacking the owner. Selector picks Candidate B to protect the player.
* `TestTargetSelectorStickyTargetBonus`: Verifies targeting hysteresis. When fighting Candidate A (500u), Candidate B appears closer (400u). Due to `currentTargetStickyBonus` (+400), summon continues attacking Candidate A, avoiding rapid target oscillation.
* `TestTargetSelectorFocusTargetOverride`: Verifies that a player-designated target overrides a closer automatic target.
* `TestTargetSelectorDeadOrDisabledIgnored`: Verifies dead (`isDead = true`) and disabled (`isDisabled = true`) candidates are filtered out.
* `TestTargetSelectorDefensiveRequiresThreat`: Verifies neutral/non-threatening NPCs are ignored under `Defensive` mode.

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
| Core unit tests | 15/15 passing via `scf_tests.exe` |

### Waiting for live Skyrim run
| Feature | Target verification |
|---|---|
| Plugin load | `SummonControlFramework v0.1.0 is loading...` in SKSE log |
| `RuntimeGate` check | Log lines verifying `Detected game version: 1.6.1170.0` |
| `kDataLoaded` receipt | Log line `Data loaded: SKSE message received, SCF FSM ready` |
| Summon registration | Log lines on casting `Conjure Familiar` / `Flame Atronach` |
| Friendly fire guard in game | Player weapon/spell hitting summon without hostility loop |
| Collision non-blocking | Player walking through summoned creature |
