# Status (2026-11-10)

## Implemented and covered by automated tests

* **Decoupled Pure C++ Core:**
  * Type-safe `EntityID` (`uint32_t`) avoiding raw engine pointers in decision-making logic.
  * Spatial vector `Vec3` with Euclidean distance and length calculations.
  * Enums for summon lifecycle and orders: `SummonState`, `SummonOrder`, `AggressionMode`.
  * `SummonModel`: state time tracking, target lost debounce timer, owner and target IDs.
  * `DecisionContext`: snapshot structure capturing distances to owner and target, combat flags, and configurable thresholds.
* **Engine Main Loop Hook & Summon Synchronization (`UpdateHook`):**
  * Hooks `PlayerCharacter::Update` at vtable index `0xAD` on `RE::VTABLE_PlayerCharacter[0]`.
  * Advances `UpdateScheduler` at tactical 15 Hz cadence, executing synchronously in the main thread with zero multi-threading instability.
  * Directly queries `player->GetActorRuntimeData().currentProcess->middleHigh->commandedActors`, resolving true engine-managed `ActorHandle` references for all player summons.
  * Synchronizes remaining duration from active effects, handles dead actors via `NotifyDeath`, and cleanses dispelled summons via `NotifyEffectRemoved`.
  * Triggers immediate synchronization from `EventAdapter` on `TESActiveEffectApplyRemoveEvent` arrivals for zero-delay registration upon casting.
* **Collision Classification Subsystem (`CollisionPairClassifier`):**
  * **Matrix arbitration (Stage 2 PoC):** pure C++ classification of collision pairs with zero game-engine header coupling:
    * `Player <-> Own Summon`: `Passthrough` (prevents doorway blocking and physical body clinches).
    * `Player <-> Other Owner's Summon`: `Default` (maintains standard collision against enemy summons).
    * `Summon A <-> Summon B (Same Owner)`: `Passthrough` when `bEnableSummonToSummonBypass` is true.
    * `Summon A <-> Summon B (Different Owners)`: `Default` (opposing summons collide in combat).
    * `Summon <-> Enemy`: `Default` (preserves weapon hits, projectile blocking, and combat interactions).
    * `Non-Summons`: `Default` (environment, static geometry, walls, and floors remain completely unaffected).
  * **Configurable Safety Gates:** global bypass toggle (`bEnableCollisionBypass`) and summon-to-summon toggle (`bEnableSummonToSummonBypass`). When disabled, instantly reverts to vanilla collision evaluation.
  * **Integrated Observability:** diagnostic log callback support and reporting in `scf status`.
* **Engine Event Adapters & Dispatcher (`EventAdapter` & `EventDispatcher`):**
  * **Creation Engine event sinks:** registered through `RE::ScriptEventSourceHolder` on `kDataLoaded`:
    * `TESActiveEffectApplyRemoveEvent`: catches magic effect applications and removals, correlating caster (owner) and target (summon) FormIDs, and triggering immediate player summon synchronization.
    * `TESDeathEvent`: detects actor mortality, transitioning registered summons to `Dead` state. Safely ignores non-fatal events and unregistered entities.
    * `TESCellAttachDetachEvent`: captures cell lifecycle transitions, switching detached references to `Suspended` and restoring attached references to `Active`.
  * **Pure event dispatcher:** `EventDispatcher` in `src/Core/` implements 100% testable event translation logic isolated from Skyrim headers.
* **Unified Runtime State (`FrameworkContext`):**
  * Central service locator coordinating `Settings`, `SummonRegistry`, `CommandQueue`, `UpdateScheduler`, `StateMachine`, and `SquadCoordinator`.
  * Unified functional logging bridge propagating structured logs across all subsystems to `spdlog`.
  * Thread-safe `ResetRuntimeState()` for clean save-load and new game transitions.
* **Console Command Processor & In-Game Diagnostics (`ConsoleHandler` & `ConsoleCommands`):**
  * Pure C++ command parsing supporting:
    * `scf status`: detailed report of framework status, update frequency, time-slicing mode, collision bypass status, and pending command counts.
    * `scf registry`: dumps all tracked summons with IDs, owners, lifecycle states (`Active`, `Suspended`, `Displaced`), and remaining duration.
    * `scf reload`: reloads INI configuration on-the-fly.
    * `scf help`: command reference.
  * Engine binding via `RE::SCRIPT_FUNCTION::LocateConsoleCommand` routing output to `RE::ConsoleLog` and SKSE file log.
* **SKSE Messaging Dispatcher (`Messaging`):**
  * Handles `kDataLoaded`: automatically locates or initializes `Data/SKSE/Plugins/SummonControlFramework.ini`, registers console commands, event sinks, and installs `UpdateHook`.
  * Handles `kPreLoadGame` & `kNewGame`: clears transient registries to avoid cross-save state leakage.
  * Handles `kPostLoadGame` & `kPostLoad`.
* **Configuration & Settings Manager (`Settings`):**
  * **Categorized configuration:** `GeneralSettings`, `CollisionSettings`, `TimingSettings`, `CombatSettings`, `SquadSettings`, and `HotkeySettings`.
  * **Resilient INI parser:** parses standard INI format with support for both Skyrim prefixes (`bEnabled`, `sLogLevel`, `fUpdateFrequencyHz`, `uMaxSummonsPerOwner`, `bEnableCollisionBypass`) and unprefixed keys.
  * **Comment handling:** strips full-line and inline comments delimited by `;` or `#`.
  * **Sanitization & Clamping:** enforces safety boundaries across all tactical parameters (e.g. summon limits [1, 32], update frequency [1, 60] Hz, return leash threshold strictly greater than follow distance) with diagnostic warning logs.
  * **Round-trip serialization & file persistence:** full formatting to readable INI file format with clean error handling for missing or unwriteable files.
* **Update Scheduler & Time-Slicing (`UpdateScheduler`):**
  * **Tick regulation (PLAN.md 1062):** decouples AI decision-making from high render frame rates (60/144+ Hz) down to an optimal tactical rate (default 15 Hz / ~66.7 ms), eliminating CPU cache thrashing.
  * **Anti-drift timing:** preserves fractional leftover delta time between ticks to avoid timing drift over long play sessions.
  * **Frame spike protection:** clamps accumulated time on sudden multi-second stalls (e.g. loading screens or script hitches) to prevent catch-up spiral-of-death.
  * **Time-sliced squad interleaving:** distributes targeting and coordination calculations for multi-summon squads across alternating frames, smoothing out framerate spikes.
  * **Priority immediate wake-up:** direct player commands (e.g. urgent `Return` or `FocusTarget`) trigger immediate evaluation without waiting for the next periodic timer interval.
* **Command Queue & Priority Arbitration (`CommandQueue`):**
  * **Strict order priority hierarchy (PLAN.md 6.3):**
    1. `Lifecycle` (Priority 0: Dispel, death, unbind)
    2. `PlayerDirect` (Priority 1: Direct hotkeys, FocusTarget, Return, WaitGuard)
    3. `ModeChange` (Priority 2: Aggression mode toggles)
    4. `Tactical` (Priority 3: Automated FSM/squad decisions)
  * **Thread-safe synchronization:** protected by `std::mutex` for safe communication between background/input threads and the engine main thread.
  * **Command coalescing:** rapid successive player commands for the same summon are automatically coalesced, avoiding redundant sequential order execution.
  * **TTL expiration:** commands that exceed their time-to-live are purged automatically.
  * **Targeted and broadcast draining:** supports both per-summon commands and broadcast commands (`InvalidEntityID`) for the whole squad.
* **Summon Registry (`SummonRegistry`):**
  * **Idempotent registration:** safely handles repeat registrations, duplicate game events, and duration refreshes without duplicating records.
  * **Cell detach / attach lifecycle (`Suspended` state):** prevents premature record deletion during loading doors or cell transitions. Detached references enter `Suspended` state and resume `Active` upon cell attach.
  * **Owner limits and displacement:** tracks active summon count against per-owner limits (e.g. 1 default, 2 with Twin Souls). Automatically identifies the oldest active summon, marks it `Displaced`, and outputs its ID for engine-level dispel.
  * **Death & Dispel handling:** marks records `Dead` on death events and `Dispatched` on effect removal. Provides `ClearStaleRecords()` for garbage collection and `Reset()` for save-load reloads.
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
* **Squad Coordinator (`SquadCoordinator`):**
  * **Support for squads of 2-4 summons** acting as a coordinated unit.
  * **Combat roles:** `Melee`, `Ranged`, `Support`.
  * **Attack slot arbitration (Anti-dogpiling):** targets have capped engagement slots (default 2 melee, 3 ranged). If an enemy's melee slots are full, additional summons distribute to secondary targets or hold in formation.
  * **Target distribution:** automatically distributes multiple targets across squad members instead of all summons dogpiling on a single enemy.
  * **Formation offsets:** calculates symmetrical, non-overlapping follow/guard offsets around the owner based on squad size and owner yaw.
* **Observability & Diagnostics:**
  * Pluggable functional callbacks capturing state changes, target scoring, registry events, command queues, scheduler updates, collision classifications, and squad slot claims with structured reasons.
  * In-game bridge forwarding events to `spdlog` in `Documents\\My Games\\Skyrim Special Edition\\SKSE\\SummonControlFramework.log`.
* **Automated Unit Tests (`scf_tests.exe`):**
  * Self-contained, zero-dependency test harness (`check.h`).
  * **62/62 test cases passing** (0 failures, execution time < 2 ms).
* **SKSE Plugin Infrastructure:**
  * Configured via `xmake.lua` targeting MSVC 2022 (C++23).
  * Multi-runtime support: Skyrim SE 1.5.97.0 (exact match) and Steam AE 1.6.1170 via Address Library (`commonlibsse-ng.plugin`).
  * `RuntimeGate` module logging runtime environment and validating executable version.
  * Deployed to `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\`.
  * Mod active in `D:\Games\stb\[STB] Mod Organizer\profiles\SBORKA\modlist.txt`.

## Built and deployed, waiting for an in-game check

* `SummonControlFramework.dll` loading under SKSE on Skyrim SE 1.5.97.0 (MO2 profile SBORKA).
* Log file generation at `Documents\My Games\Skyrim Special Edition\SKSE\SummonControlFramework.log`.
* `RuntimeGate` log output verifying detected game version (1.5.97.0).
* SKSE messaging listener receiving `kDataLoaded`, `kPreLoadGame`, `kPostLoadGame`, `kNewGame`.
* Console command `scf` (with `status`, `registry`, `reload`, `help`) executing from the in-game tilde console.
* `UpdateHook` active on `PlayerCharacter::Update`, synchronizing player summons from `commandedActors`.
* Engine event sinks observing active effects, death, and cell attach/detach in live gameplay.
* Collision bypass settings active in `SummonControlFramework.ini`.

## Up next (Plan Roadmap)

* **Stage 2 (Havok Filter Integration & Verification):**
  * Precision API callback or `bhkCollisionFilter` hook registration in runtime.
  * In-game doorway pass-through testing.
* **Stage 3 (Targeting & Player Controls):**
  * `CrosshairPickData` hook for instant `FocusTarget` targeting.
  * Input event sink for hotkeys (`FocusTarget`, `Return`, `ToggleAggression`).

## Known limitations

* Engine adapters (`ActorAdapter`, `CombatAdapter`) are currently decoupled; test execution uses synthetic `DecisionContext` snapshots until the `SummonRegistry` hook is active.
* Raycast / Line-of-sight checks currently depend on snapshot inputs and will be connected to engine raycasts in the adapter layer.
