# Engineering Journal

## 2026-11-10 — Initial setup and build system (xmake + CommonLibSSE-NG)

Decisions, deviations, and findings during project initialization.

* **Adopting xmake instead of CMake + vcpkg.**
  * *Reason:* IssenFramework and ReactiveDodgeFramework in `D:\Games\Issen` proved the effectiveness of `xmake` for SKSE development. It compiles in parallel out of the box, provides immediate precompiled header support, and allows declaring a lightweight independent testing binary (`scf_tests`) alongside the main SKSE DLL with zero overhead.
  * *CommonLibSSE-NG path resolution:* Configured [xmake.lua](file:///D:/projects/Summon_Control_Framework/xmake.lua) with a dynamic fallback sequence: `os.getenv("COMMONLIBSSE_NG_PATH")` -> `D:/Games/Issen/CommonLibSSE-NG` -> `extern/CommonLibSSE-NG`. This allows development to proceed without duplicating repository submodules while supporting CI and external checkouts.
* **Multi-target configuration:**
  * Enabled `skyrim_se = true`, `skyrim_ae = true`, `skse_xbyak = true`, `skyrim_vr = false`.
  * `commonlibsse-ng.plugin` rule was integrated to generate both `commonlibsse-ng-plugin.cpp` (with `SKSEPluginInfo` declaration) and `commonlib-plugin.rc` (Windows DLL version resources).
* **First build hurdle (MSVC consteval formatting):**
  * In `RuntimeGate.cpp`, initial code attempted `logger::info("Game executable: {}", module.filename());`. In CommonLibSSE-NG, `module.filename()` returns `std::wstring_view`. Under MSVC's C++23 `<format>` consteval parser, mixing wide string views into narrow format strings triggers `error C2039: 'parse': is not a member of std::formatter`.
  * *Fix:* Stripped wide string formatting and logged `module.version().string(".")`, which returns a standard UTF-8 `std::string`. Build succeeded immediately (`spent 1.8s`).

---

## 2026-11-10 — Pure C++ Core and Gameplay FSM implementation

Architectural implementation of the decision layer.

* **Decoupled Core Architecture:**
  * To adhere strictly to PLAN.md section 3.1, `src/Core/` was built completely free of `RE/Skyrim.h` or SKSE includes.
  * `EntityID` is a lightweight `uint32_t`. This prevents any reliance on raw `RE::Actor*` memory addresses that can be invalidated by cell changes, memory purges, or actor resets in Skyrim.
* **Separation of intention from execution:**
  * Gameplay FSM (`StateMachine`) only evaluates tactical intentions (`SummonState::FollowOwner`, `SummonState::Engage`, etc.). It does not attempt to calculate NavMesh paths or play animations, leaving those to the engine's native AI and Havok Behavior Graph.
* **Hysteresis and Leash constraints:**
  * Introduced `targetLostDebounceTime` (1.5s default). If a target ducks behind cover or LOS is broken for a fraction of a second, the summon remains engaged rather than oscillating between combat and follow states.
  * Added `returnDistanceThreshold` (2500 units leash). When the player sprints away, the summon unconditionally drops pursuit and returns to the owner, preventing lost summons.
* **Self-contained test harness:**
  * Ported the minimal, macro-based `check.h` pattern from IssenFramework. It requires no third-party packages, compiles in milliseconds, and provides clear line-numbered failure output with `CHECK` and `CHECK_NEAR`. 8 test cases written and passed.

---

## 2026-11-10 — TargetSelector & Threat Scoring

Implementation of tactical target evaluation and friendly-fire protection.

* **Strict Friendly-Fire Guard:**
  * PLAN.md section 6.4 mandates that a summon must never attack the owner or squad allies.
  * `TargetSelector` enforces this at the root qualification stage: any candidate matching `summon.ownerId`, `summon.id`, or marked `isOwnerOrAlly` is rejected immediately.
  * Even if the player explicitly issues a `FocusTarget` order on a friendly NPC or the player character, the order is rejected with an explicit diagnostic log (`"Explicit FocusTarget rejected: cannot target owner/ally"`), and the selector safely falls back to automatic scoring of hostile targets.
* **Sticky Target Hysteresis:**
  * In crowded combat, two enemies at 350 and 360 units would cause an naive AI to switch targets back and forth every frame as distances fluctuate.
  * Added `currentTargetStickyBonus = +400.0f`. The current target is kept unless another candidate is significantly more dangerous (e.g. actively attacking the owner) or much closer.
* **Review discovery during test execution:**
  * When writing `TestTargetSelectorFriendlyFireGuard`, a rejection log verification check initially failed.
  * *Root cause:* When an explicit `FocusTarget` was rejected, the selector logged the rejection reason, but then proceeded to step 3 (automatic selection) and logged a second time (`"Best scored candidate selected"`), overwriting a single-string log capture in the test.
  * *Refinement:* Adjusted the test harness to maintain a full history vector `std::vector<std::string> logHistory`, confirming that both the rejection event and the subsequent fallback selection are accurately recorded.
* **Test suite status:**
  * Total test count expanded to 15 cases across both suites (8 FSM + 7 TargetSelector). All 15 pass cleanly in ~1 ms.

---

## 2026-11-10 — SquadCoordinator & Attack Slot Arbitration

Implementation of Stage 6: multi-summon coordination, formation management, and attack slots.

* **Anti-Dogpiling with Attack Slots:**
  * Vanilla Skyrim NPC AI assigns combat targets independently, causing multiple summons to charge into the exact same point against a single enemy, resulting in pushing, physics jitter, and clipping.
  * `SquadCoordinator` implements attack slot arbitration: each candidate target has a limited number of melee engagement slots (`maxMeleeSlotsPerTarget`, default 2) and ranged slots (`maxRangedSlotsPerTarget`, default 3).
  * If Target A's melee slots are full, additional melee summons automatically distribute to alternative eligible targets (Target B), or hold in formation / flanking stance instead of crowding.
* **Symmetrical Formation Offsets:**
  * Implemented `CalculateFormationOffsets` based on squad count (1-4) and owner yaw angle.
  * Summons are arrayed across the rear-to-flank arc:
    * 1 summon: left-rear (-140°)
    * 2 summons: left-rear (-130°) and right-rear (+130°)
    * 3 summons: flanks (-120°, +120°) and straight behind (+180°)
    * 4 summons: flank horns (-70°, +70°) and rear anchors (-145°, +145°)
  * Offsets use standard Skyrim coordinate conversion (yaw rotation with sin = X, cos = Y), verified with non-overlapping distance assertions.
* **Independent Role Handling:**
  * `CombatRole` (`Melee`, `Ranged`, `Support`): ranged summons (e.g. Flame Atronachs) evaluate ranged slots independently from melee summons (e.g. Dremora Lords), enabling combined-arms behavior.
* **Verification & Tests:**
  * 5 new test cases added in `tests/test_squad_coordinator.cpp`, bringing total automated coverage to 20 tests.
  * All 20 tests pass cleanly with 0 failures.

---

## 2026-11-10 — SummonRegistry & Lifecycle State Tracking

Implementation of Stage 1: robust summon lifecycle tracking and capacity management.

* **Idempotent Registration & Dispel Tracking:**
  * Spells and active effects in Skyrim can fire duplicate events or refresh durations while the summon actor is alive.
  * `SummonRegistry::RegisterSummon` is strictly idempotent: re-registering an existing `EntityID` updates duration and restores `Active` status without creating duplicate entries or corrupting squad lists.
* **Preventing Premature Deletion on Cell Detach:**
  * PLAN.md sections 1.4 and 4.3 emphasize that passing through doors or cell transitions causes `TESCellAttachDetachEvent(detach)`. Deleting records on detach causes lost summons when the cell loads.
  * Introduced `SummonLifecycleState::Suspended`: detach marks the entity suspended without deleting its metadata. Re-entering cell attach cleanly restores `Active` status.
* **Capacity Limits & Oldest-Summon Displacement:**
  * When a player recasts a summon beyond their perk limit (e.g. 1 by default, 2 with Twin Souls), Skyrim's game engine dispels the oldest summon.
  * `SummonRegistry` mimics this contract: it identifies the oldest active summon by `registrationTimestamp`, marks it `Displaced`, and outputs its `EntityID` so the engine adapter can initiate the in-game dispel/kill.
* **Verification & Tests:**
  * 6 new test cases added in `tests/test_summon_registry.cpp`: idempotent registration, cell detach suspension, recast limit displacement, death/dispel lifecycle, stale record sweeping, and registry reset.
  * Total automated test count expanded to **26 test cases**, all passing with 0 failures.

---

## 2026-11-10 — CommandQueue & Priority Arbitration

Implementation of Section 6.3 & Thread Model: thread-safe command queue with strict priority hierarchy.

* **Explicit Priority Ordering (PLAN.md 6.3):**
  * In combat, direct player intervention (e.g. commanding `Return` or a specific `FocusTarget`) must override automatic tactical positioning without delay, while safety events (`Lifecycle`, `Dispel`) must take precedence over everything.
  * `CommandQueue` implements four priority tiers: `Lifecycle` (0) > `PlayerDirect` (1) > `ModeChange` (2) > `Tactical` (3).
  * Dequeuing strictly extracts higher-priority actions first regardless of enqueue arrival order.
* **Command Coalescing & Anti-Oscillation:**
  * If a player mashes commands or rapidly toggles orders (e.g. `Follow` -> `WaitGuard`) during a single frame, identical priority commands for the same summon are coalesced in-place, preventing rapid state oscillation.
* **Time-to-Live (TTL) Safety:**
  * Commands include a `ttl` field (default 5.0s). If the game pauses, opens an inventory menu, or enters a loading screen, stale queued commands expire automatically rather than executing out-of-context later.
* **Thread Safety:**
  * Input handlers (`BSInputDeviceManager`) and background callbacks push commands under `std::mutex` synchronization, ensuring safe delivery to the engine main tick thread.
* **Verification & Tests:**
  * 6 new test cases added in `tests/test_command_priority.cpp`, including priority sorting, coalescing, TTL expiration, broadcast draining, FSM integration, and a concurrent 4-thread stress test.
  * Total automated test count expanded to **32 test cases**, all passing with 0 failures.

---

## 2026-11-10 — UpdateScheduler & Tick Interleaving

Implementation of PLAN.md 1062: decoupled AI tick rate and squad time-slicing.

* **Decoupled Tick Rate (15 Hz vs Render FPS):**
  * Running spatial distance evaluations, target scoring, and formation calculations every render frame at 60/144 Hz wastes CPU budget without improving perceived reaction time.
  * `UpdateScheduler` sets a tactical rate (default 15 Hz / ~66.7 ms), clamped between 30 Hz (0.0333s cap) and 5 Hz (0.2000s floor).
* **Anti-Drift Accumulator & Spike Protection:**
  * Rather than discarding delta time, `_accumulatedTime` subtracts `interval`, preserving fractional remainders to eliminate timing drift over hours of gameplay.
  * If a massive frame spike occurs (e.g. cell load or script stall > 2x interval), accumulated time is clamped to avoid a rapid catch-up spiral-of-death.
* **Time-Sliced Squad Interleaving:**
  * In multi-summon squads, running full targeting simultaneously on the same frame produces micro-stutters.
  * `ShouldUpdateEntity` round-robins evaluation across consecutive ticks (`(index % total) == (tick % total)`), distributing CPU load across frames.
* **Priority Immediate Wake-up:**
  * When `CommandQueue` receives an urgent `PlayerDirect` or `Lifecycle` command, `RequestImmediateTick()` forces evaluation on the very next frame regardless of timer accumulator.
* **Review discovery:**
  * Constructor called `SetUpdateFrequency(10.0f)` which produced a log entry before test logs were cleared. Test adjusted to clear constructor log before assertions.
* **Verification & Tests:**
  * 6 new test cases added in `tests/test_update_scheduler.cpp`: interval clamping, 60 FPS periodic simulation, immediate wakeup override, round-robin time slicing, state reset, and logging.
  * Total automated test count expanded to **38 test cases**, all passing with 0 failures.

---

## 2026-11-10 — Settings Manager & INI Configuration

Implementation of core configuration parsing, validation, and serialization.

* **Pure C++ INI Parser without External Dependencies:**
  * Avoiding bloated external parser libraries keeps compile times sub-second and eliminates linking headaches between the main SKSE DLL and the offline unit test binary.
  * `Settings` implements resilient line-by-line parsing with support for both standard Skyrim-style prefixes (`bEnabled`, `sLogLevel`, `uMaxSummonsPerOwner`, `fUpdateFrequencyHz`, etc.) and clean unprefixed keys.
  * Strips comments delimited by `;` or `#` and trims whitespace seamlessly.
* **Defensive Clamping & Sanitization:**
  * User-edited INI files are prone to typos, negative numbers, or extreme values.
  * `ValidateAndSanitize()` runs automatically after loading:
    * Clamps `maxSummonsPerOwner` to [1, 32].
    * Validates `logLevel` against known strings ("trace", "debug", "info", "warn", "error", "critical"), falling back safely to "info".
    * Enforces `updateFrequencyHz` in [1.0, 60.0] and `commandTTL` in [0.1, 60.0].
    * Enforces leash integrity: ensures `returnDistanceThreshold > followDistanceThreshold` to prevent instant leash-break loops.
    * Clamps attack slots and formation spread radii.
* **Verification & Tests:**
  * 6 new test cases added in `tests/test_settings.cpp`: default values verification, prefixed parsing, unprefixed parsing with inline comments, defensive clamping, round-trip serialization, and disk file persistence.
  * Total automated test count expanded to **44 test cases**, all passing with 0 failures.

---

## 2026-11-10 — FrameworkContext, Console Commands & SKSE Messaging

Implementation of Stage 0 infrastructure: unified runtime state locator, in-game console commands, and SKSE message dispatching.

* **Architectural Decoupling of Console Handling:**
  * Rather than embedding console formatting inside Skyrim-dependent SKSE hooks, created `ConsoleHandler` in `src/Core/`: pure C++ parsing and formatting for commands (`scf status`, `scf registry`, `scf reload`, `scf help`).
  * `ConsoleCommands` in `src/Plugin/` serves as the engine adapter, registering the `scf` command in Skyrim's `SCRIPT_FUNCTION` table and outputting to both `RE::ConsoleLog` and the log file.
* **Unified Framework Context (`FrameworkContext`):**
  * Centralizes lifecycle management for `Settings`, `SummonRegistry`, `CommandQueue`, `UpdateScheduler`, `StateMachine`, and `SquadCoordinator`.
  * Wires all individual component logging callbacks into a unified sink.
  * Provides `ResetRuntimeState()` called on `kPreLoadGame` and `kNewGame` to prevent ghost summons from lingering across save-load transitions.
* **SKSE Messaging Dispatcher (`Messaging`):**
  * Listens to SKSE message bus:
    * `kDataLoaded`: automatically loads `Data/SKSE/Plugins/SummonControlFramework.ini` or saves defaults if absent; registers `scf` console command.
    * `kPreLoadGame` / `kNewGame`: purges runtime registries.
    * `kPostLoadGame` / `kPostLoad`: logs readiness.
* **Verification & Tests:**
  * 6 new test cases added in `tests/test_console_commands.cpp`: help command, status report, empty and populated registry dumps, live INI reload, unknown command fallback, and runtime state reset.
  * Total automated test count expanded to **50 test cases**, all passing with 0 failures.

---

## 2026-11-10 — Engine Event Adapters (`EventDispatcher` & `EventAdapter`)

Implementation of Stage 1: Wiring Skyrim's `BSTEventSink` into `SummonRegistry`.

* **Separation of Engine Sinks and Dispatch Logic:**
  * To preserve the pure decoupled core, `EventDispatcher` resides in `src/Core/` with zero `#include <RE/Skyrim.h>`, processing lightweight `ActiveEffectEvent`, `DeathEvent`, and `CellAttachDetachEvent`.
  * `EventAdapter` in `src/Engine/` implements `RE::BSTEventSink<T>` for the three target events, safely resolving `FormID` from `NiPointer<TESObjectREFR>` and forwarding to `EventDispatcher`.
* **Displacement Callback Integration:**
  * When a recast event exceeds the owner's limit, `EventDispatcher` fires a `DisplaceCallback` with the oldest summon ID, enabling the engine adapter to initiate in-game dispel.
* **Self-Buff & Non-Fatal Event Filtering:**
  * Self-buffs where `caster == target` are rejected immediately.
  * Non-fatal events (`isDead = false`) and events from untracked entities are safely ignored without corrupting registry state.
* **Verification & Tests:**
  * 4 new test cases added in `tests/test_event_dispatcher.cpp`: effect apply & remove, recast limit displacement, death events, and cell attach/detach lifecycle transitions.
  * Total automated test count expanded to **54 test cases**, all passing with 0 failures.

---

## 2026-11-10 — CollisionPairClassifier & Collision Bypass Subsystem

Implementation of Stage 2 (Collision PoC): decoupled collision pair matrix arbitration and runtime safety gates.

* **Decoupled Classification Matrix (PLAN.md Section 5):**
  * Built `CollisionPairClassifier` in `src/Collision/` to evaluate pair interaction without coupling to Havok headers or raw pointers.
  * Matrix rules:
    * `Player <-> Own Summon`: `Passthrough` (eliminates door blocking and narrow-corridor congestion).
    * `Player <-> Other Owner's Summon`: `Default` (maintains standard collision against enemy summons).
    * `Summon A <-> Summon B (Same Owner)`: `Passthrough` if `bEnableSummonToSummonBypass` is true; otherwise `Default`.
    * `Summon A <-> Summon B (Different Owners)`: `Default` (maintains combat collision).
    * `Summon <-> Enemy`: `Default` (weapon hit detection and projectile collision preserved).
    * `Non-Summons / Environment`: `Default` (actors will never fall through floors or walk through walls).
* **Runtime Safety Gates & INI Integration:**
  * Added `CollisionSettings` to `Settings.h` with `bEnableCollisionBypass` and `bEnableSummonToSummonBypass`.
  * If `bEnableCollisionBypass` is false, `CollisionPairClassifier::ClassifyPair` instantly returns `Default` with zero overhead.
  * Integrated with `scf status` console command to display active collision bypass status.
* **Verification & Tests:**
  * Added 7 unit tests in `tests/test_collision_pair_classifier.cpp` covering all collision combinations, toggles, and logging.
  * Total automated test count expanded to **61 test cases**, all passing cleanly with 0 failures in ~2 ms.

---

## 2026-11-10 — Fix: In-Game Summon Registration via Player Update Hook & commandedActors

Discovery and resolution of in-game summon registration failure.

* **Investigation of User Report:**
  * User tested in Mod Organizer (`SBORKA` profile) and reported `scf registry` returning `0 registered summons` even while a summoned spirit wolf/familiar was standing next to the player character (`Geone`).
* **Root Causes Identified:**
  1. **Engine Event Disconnect:** In Skyrim Creation Engine, `TESActiveEffectApplyRemoveEvent` for conjuration spells fires with `caster == target == Player` (as seen in the screenshot where auras and spirit buffs target `Geone`), because `SummonCreatureEffect` attaches to the caster to maintain the spell duration. The summoned actor reference (`Actor*`) is NOT passed as `target` in this event. Consequently, `EventDispatcher::HandleActiveEffect` discarded the event as a self-buff.
  2. **Missing Game Loop Hook:** The engine update hook had not yet been wired into the game loop, so `UpdateScheduler::ShouldTick` was never executing during live gameplay.
* **Solution Implemented:**
  1. **`UpdateHook` (PlayerCharacter::Update):**
     * Hooked `PlayerCharacter::Update` at virtual table index `0xAD` on `RE::VTABLE_PlayerCharacter[0]` (following the battle-tested pattern from `ReactiveDodgeFramework`).
     * Runs synchronously in the engine main thread at 15 Hz without thread-safety risks.
  2. **Direct Engine State Query (`commandedActors`):**
     * Reads `player->GetActorRuntimeData().currentProcess->middleHigh->commandedActors`.
     * Iterates over engine-managed `ActorHandle` entries, resolves live `Actor*` pointers, checks `!IsDead()`, reads remaining duration from `activeEffect`, and registers valid summons in `SummonRegistry`.
     * Automatically sweeps and removes dispelled summons using `registry.NotifyEffectRemoved`.
  3. **Event Sink Accelerated Trigger:**
     * Added immediate `update::SyncPlayerSummons()` call inside `ActiveEffectEventSink::ProcessEvent` so that summons are recorded instantly when spells finish casting.
* **Verification:**
  * Rebuilt `SummonControlFramework.dll` (~607 KB) and confirmed clean compilation with 0 warnings.
  * All 61 unit tests continue to pass in `scf_tests.exe`.
  * Deployed fresh DLL and PDB to `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\`.

---

## 2026-11-10 — Grace Period Garbage Collection & Console Output Filtering

Resolution of stale summon retention in scf registry after death or dispelling.

* **Grace Period Garbage Collection (SummonRegistry & UpdateHook):**
  * When a summon dies or is dispelled, keeping its metadata temporarily is essential so that other subsystems (SquadCoordinator, CommandQueue, TargetSelector) can safely unbind slots, drop orders, and handle the engine's 3-second death animation/ash-pile shader.
  * Added deathTimestamp tracking to SummonRecord.
  * SummonRegistry::ClearStaleRecords(timestamp, gracePeriod) now retains dead/dispatched records during a configurable grace period (default 3.0s), and automatically purges them once the fadeout completes.
  * In UpdateHook.cpp, ClearStaleRecords runs on every 15 Hz tick with the elapsed game time, ensuring zero manual intervention.
* **Console Formatting Improvements (ConsoleHandler):**
  * scf registry displays active summons clearly with duration and ownership.
  * Any dying/fading summons currently undergoing the 3-second ash-pile transition are displayed distinctly as (fading in ash-pile), avoiding confusion.
* **Verification:**
  * Added TestGracePeriodGarbageCollection in `tests/test_summon_registry.cpp`.
  * Test suite expanded to **62/62 unit tests passing**.
  * Deployed fresh DLL and PDB to `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\`.

---

## 2026-11-10 — In-Game Verification, Multi-Summon Mechanics, and Workstream FF-01 Integration

Comprehensive milestone following user playtesting in `[STB] Mod Organizer` and integration of Addendum FF-01.

* **In-Game Playtesting Results:**
  * **Body Passthrough Confirmed:** User confirmed that the player character moves freely through summoned creatures. Physical body collision blocking, doorway bottlenecks, and camera pushing are successfully eliminated.
  * **Cell Transitions Confirmed:** Entering and exiting interior/exterior cells correctly transfers summoned creatures without CTDs, broken references, or registry corruption (`Active <-> Suspended` transition logic validated in live engine).
  * **1-Summon Limit Resolution:** User noted that recasting displaces the first summon immediately. Analysis confirmed this is Skyrim engine's hardcoded default limit (`commandedActors limit = 1`). To test 2 simultaneous summons, the character requires the vanilla *Twin Souls* perk (`player.addperk 000581DE`).
* **Feasibility Analysis: Friendly Fire Protection & Attack Filtering:**
  * **Damage Protection (100% Viable):** Hooking the damage calculation pipeline (`Actor::AdjustDamage` / `ProcessHit`) allows clamping damage to 0 and suppressing hostility reaction loops when aggressor is player or ally.
  * **True Melee Pass-Through (High Viability):** Mod environment audit identified `Precision.dll` (v2.0.4) active in `D:\Games\stb\[STB] Mod Organizer\mods\Precision\`. Integrating `PrecisionAPI.h` and returning `bIgnoreHit = true` in `PreHitCallback` allows weapon swings to physically pass through friendly summons without hitstop and strike enemies behind them.
  * **True Projectile Pass-Through (Viable):** Filtering projectile collidables (`COL_LAYER_ARROW`, `COL_LAYER_MAGIC`) in Havok (`bhkCollisionFilter`) prevents collision generation between player/ally projectiles and registered summons.
* **Full Documentation & Roadmap Synchronization:**
  * **`PLAN.md`:** Expanded Stage 5 into 5 detailed sub-stages (`FF-Stage A` through `FF-Stage E`) based on `Friendly_Fire_Protection_Addendum.md`. Updated DoD with damage protection, attack pass-through, and regression criteria.
  * **`ARCHITECTURE.md`:** Added Section 16 detailing the architectural separation between Actor body collision (`CollisionPairClassifier`) and combat hit filtering (`AttackFilterManager`), the `RelationshipClassifier` model (`AttackSourceKind`), Precision backend mechanics, and INI configuration schemas.
  * **`STATUS.md`:** Formally recorded in-game verified milestones and activated Workstream FF-01 roadmap tasks.
  * **`TESTING.md`:** Added Section 4 with comprehensive acceptance test suites FF-1 (Damage Protection), FF-2 (Attack Pass-Through), and FF-3 (Compatibility & Regression).

---

## 2026-11-10 — Full Implementation of FF-01 (Friendly Fire, Precision Melee Pass-Through, Projectile Hook)

Implemented and deployed the complete friendly fire and combat pass-through subsystem.

* **Settings & Configuration (`FriendlyFireSettings`):**
  * Created `src/FriendlyFire/FriendlyFireSettings.h`.
  * Added `friendlyFire` member to `SettingsData` with support for `bEnableSummonProtection`, `bProtectPlayerOwnedSummons`, `bBlock*Damage`, `b*PassThrough`, etc.
  * Added INI parsing and round-trip serialization in `src/Core/Settings.cpp`.
* **Relationship Classification & Matrix Evaluation (`RelationshipClassifier`):**
  * Created `src/FriendlyFire/RelationshipClassifier.h/.cpp`.
  * Classified sources: `Player`, `PlayerAlly`, `PlayerOwnedSummon`, `AlliedSummon`, `Neutral`, `Hostile`, `Unknown`.
  * Depth-guarded commanding actor chain traversal (`MaxCommandChainDepth = 4`) with loop detection to prevent cycles.
  * Evaluates attack kinds (`Melee`, `Sweep`, `Arrow`, `PhysicalProjectile`, `MagicProjectile`, `AreaEffect`, etc.).
  * Fail-safe safety gate: unknown, neutral, and hostile sources are never filtered.
  * Added 5 new unit tests in `tests/test_friendly_fire.cpp` (all 67/67 tests passing).
* **Precision Melee Pass-Through (`PrecisionBackend`):**
  * Copied `PrecisionAPI.h` and created `src/Integrations/PrecisionBackend.h/.cpp`.
  * Dynamically connects to `Precision.dll` and installs `PreHitCallback`.
  * When a friendly melee swing touches a protected summon, returns `bIgnoreHit = true`. Precision physically ignores the hit, avoids hitstop, and lets the weapon slice through to hit enemies behind!
* **Engine Damage Hook (`HitHook`):**
  * Created `src/FriendlyFire/HitHook.h/.cpp`.
  * Intercepts call to `Actor::ProcessHit` (ID 37673 at offset 0x3C0).
  * Nullifies damage, stagger, and pushback, dropping friendly hits before damage calculation or hostility loops can trigger.
* **Engine Projectile Pass-Through (`ProjectileHook`):**
  * Created `src/FriendlyFire/ProjectileHook.h/.cpp`.
  * Hooks virtual functions `AddImpact` (vfunc 189) and `HandleHits` (vfunc 190) on `ArrowProjectile` and `MissileProjectile`.
  * Skips impact registration on friendly summons and removes physics contact points from `hkpAllCdPointCollector`, allowing arrows, bolts, and spells to fly directly through summons into targets behind them!
* **Build & Deployment:**
  * Clean compilation of `SummonControlFramework.dll` (647 KB) and automatic deployment to `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\`.

---

## 2026-11-10 — Clean Profile Verification ([RE2CE TEST]), Twin Summons In-Game, and Formalized Dependency Matrix

Testing iteration in minimal environment and documentation of system dependencies and graceful degradation.

* **Testing in `[RE2CE TEST] Mod Organizer` (Profile `RE2CE`):**
  * Switched testing to clean Skyrim environment to isolate SCF mechanics from complex third-party mod interactions.
  * Resolved environment issues: fixed Cyrillic console squares (`□□□`) via `fonts_console.swf` and configured borderless display tweaks.
  * User confirmed:
    * Twin summons (`player.addperk 000581DE` Twin Souls) work simultaneously in gameplay and are tracked cleanly by SCF.
    * Physical body passthrough functions seamlessly across multiple concurrent summons.
    * Cell transitions (`Active <-> Suspended`) cleanly preserve both summons without crashes or memory leakage.
* **Dual-Deployment Build Automation (`xmake.lua`):**
  * Updated `xmake.lua` to deploy built DLL and PDB files automatically to both `D:\Games\stb\[STB] Mod Organizer` and `D:\Games\stb\[RE2CE TEST] Mod Organizer`.
* **Return to `[STB] Mod Organizer` (Profile `SBORKA`):**
  * Baseline clean tests completed. Transitioning primary testing environment back to `[STB] Mod Organizer`, where `Precision.dll` is installed and active, enabling full live combat verification of Melee Pass-Through (`PreHitCallback` / `bIgnoreHit = true`) and projectile traversal in complex combat scenarios.
* **Formal Dependency & Fallback Matrix Documented:**
  * Synchronized all project documentation (`README.md`, `ARCHITECTURE.md`, `STATUS.md`, `TESTING.md`) with explicit separation of:
    1. **Hard Requirements:** Skyrim SE/AE, SKSE64, Address Library (critical for plugin load).
    2. **Soft Requirements & Feature Fallback:**
       - `Precision` + `Behavior Data Injector`: Full melee pass-through without hitstop vs fallback to `HitHook` damage nullification (`damage = 0`).
       - `Twin Souls` perk / multi-summon mods: Support for >1 concurrent summon vs vanilla engine cap of 1.
       - `SSE Display Tweaks`: Borderless fullscreen and high-FPS Havok physics stability.
       - `Console Font Fix` / `ConsolePlusPlus`: Console readability and Ctrl+V command pasting.


---

## 2026-11-10 — Stealth Coordination & Player Orders/Input Subsystem (Stage 3 & 5)

Implementation of tactical stealth behavior and live player input controls (Hotkeys & Console Orders).

* **Stealth Coordination (`StealthContext`):**
  * Created `src/Core/StealthContext.h` with `playerSneaking`, `playerDetected`, and `explicitAttackOrder`.
  * Embedded `StealthContext` into `DecisionContext.h`.
  * **Target Selector Integration:** In `TargetSelector.cpp`, added rule: when `stealth.playerSneaking && !stealth.playerDetected` and aggression mode is `Defensive`, summons suppress attacks against unaware enemies (`candidate.isAttackingOwner == false && candidate.isAttackingSummon == false`). This prevents summons from giving away player position during infiltration.
  * **FSM Integration:** In `StateMachine.cpp`, updated `CanEngageCombat` to hold `FollowOwner` during sneak unless an explicit focus target or active combat engagement occurs.
  * **Unit Tests:** Created `tests/test_stealth.cpp` with 7 test cases covering stealth suppression, detected transition, active threat defense, explicit focus target override, aggressive mode override, and FSM follow/engage states.
* **Engine Input & Crosshair Targeting (`InputHandler`):**
  * Created `src/Engine/InputHandler.h` and `src/Engine/InputHandler.cpp`.
  * Registered as `RE::BSTEventSink<RE::InputEvent*>` on `RE::BSInputDeviceManager`.
  * Resolves crosshair target via `RE::CrosshairPickData::GetSingleton()` (`targetActor[0]` and fallback `target[0]`), extracting `Actor*` via `ref->As<RE::Actor>()`. Validates that the target is alive, not the player, and not a registered summon.
  * Configurable DirectInput scan codes:
    * `0x22` (`G`): Focus Target under crosshair.
    * `0x23` (`H`): Immediate Return to player.
    * `0x24` (`J`): Cycle aggression mode (`Passive` -> `Defensive` -> `Aggressive`).
  * Real-time HUD feedback via `RE::SendHUDMessage::ShowHUDMessage`.
  * Registered `engine::InputHandler::Register()` in `src/Plugin/Messaging.cpp` on `kDataLoaded`.
* **Console Orders Processor (`ConsoleHandler`):**
  * Added `scf order <return | follow | guard | passive | defensive | aggressive | focus <FormID>>` in `src/Core/ConsoleHandler.cpp`.
  * Added unit test suite in `tests/test_console_commands.cpp`.
* **Verification & Build:**
  * All 75 automated unit tests pass in < 2 ms (`scf_tests.exe`).
  * `SummonControlFramework.dll` compiled and deployed cleanly to both `[STB] Mod Organizer` and `[RE2CE TEST] Mod Organizer`.
