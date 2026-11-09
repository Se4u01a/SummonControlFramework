# Status (2026-11-10)

## Implemented and covered by automated tests

* **Decoupled Pure C++ Core:**
  * Type-safe `EntityID` (`uint32_t`) avoiding raw engine pointers in decision-making logic.
  * Spatial vector `Vec3` with Euclidean distance and length calculations.
  * Enums for summon lifecycle and orders: `SummonState`, `SummonOrder`, `AggressionMode`.
  * `SummonModel`: state time tracking, target lost debounce timer, owner and target IDs.
  * `DecisionContext`: snapshot structure capturing distances to owner and target, combat flags, and configurable thresholds.
* **Gameplay FSM (`StateMachine`):**
  * States implemented: `Idle`, `FollowOwner`, `Guard`, `AcquireTarget`, `Approach`, `Engage`, `Return`, `Despawn`.
  * Direct orders processing (`Follow`, `WaitGuard`, `Return`) overriding automated combat.
  * **Target lost debounce (Hysteresis):** momentary line-of-sight loss or target hiding does not drop combat immediately; FSM awaits `targetLostDebounceTime` (1.5s) before disengaging.
  * **Leash break constraint:** if the player moves beyond `returnDistanceThreshold` (2500 units), the summon drops combat and immediately returns to the owner.
  * Approach vs Engage range separation: smooth transition to `Approach` if target steps out of melee range (> 1.5x threshold).
* **Target Selector (`TargetSelector`):**
  * **Strict friendly-fire guard:** absolute refusal to target owner, self, or squad allies under any circumstances.
  * Aggression policies: `Passive` (no combat), `Defensive` (only threats targeting owner or summon), `Aggressive` (all enemies within range).
  * Explicit `FocusTarget` command: player-designated target validated for life/safety and given top priority. Rejection of invalid focus targets safely falls back to automatic scoring.
  * Dynamic candidate scoring:
    * Threats attacking owner (+1000)
    * Threats attacking summon (+500)
    * **Sticky target hysteresis (+400):** current target retains a strong bonus, completely eliminating target-flicker between adjacent enemies.
    * Low-HP finishing bonus (+200 * (1 - HP%)).
* **Observability & Diagnostics:**
  * Pluggable functional `LogCallback` capturing state changes and targeting decisions with structured reasons (`Reason: ...`).
  * In-game bridge forwarding events to `spdlog` in `Documents\My Games\Skyrim Special Edition\SKSE\SummonControlFramework.log`.
* **Automated Unit Tests (`scf_tests.exe`):**
  * Self-contained, zero-dependency test harness (`check.h`).
  * 15/15 test cases passing (0 failures, execution time < 2 ms).
* **SKSE Plugin Infrastructure:**
  * Configured via `xmake.lua` targeting MSVC 2022 (C++23).
  * Multi-runtime support: Steam AE 1.6.1170 (primary) and SE 1.5.97 via Address Library (`commonlibsse-ng.plugin`).
  * `RuntimeGate` module logging runtime environment and validating executable version.
  * Xbyak trampoline support enabled (`skse_xbyak = true`).
  * Optional automatic deployment to MO2/Vortex mods folder via `--deploy_dir`.

## Built and deployed, waiting for an in-game check

* `SummonControlFramework.dll` loading under SKSE on Steam AE 1.6.1170.
* Log file generation at `Documents\My Games\Skyrim Special Edition\SKSE\SummonControlFramework.log`.
* `RuntimeGate` log output verifying detected game version.
* SKSE messaging listener receiving `kDataLoaded`.

## Up next (Plan Roadmap)

* **Stage 1 (SummonRegistry PoC):**
  * Subscribe to `TESActiveEffectApplyRemoveEvent` to detect summon spell cast and expiration.
  * Hook `TESDeathEvent` and `TESCellAttachDetachEvent` for lifecycle management.
  * Map `ActorHandle` to internal `EntityID`.
  * Add in-game console commands: `scf_status`, `scf_registry`.
* **Stage 2 (Collision PoC - Key Technical Risk):**
  * Passive observation hook for Havok collision filter callbacks.
  * Filter pair classification (`Player <-> Own Summon` non-blocking).

## Known limitations

* Engine adapters (`ActorAdapter`, `CombatAdapter`) are currently decoupled; test execution uses synthetic `DecisionContext` snapshots until the `SummonRegistry` hook is active.
* Raycast / Line-of-sight checks currently depend on snapshot inputs and will be connected to engine raycasts in the adapter layer.
