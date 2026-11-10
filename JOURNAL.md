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


---

## 2026-11-10 — FLICK Menu Interactive Binding, Numpad Controls, MoveTo Point Order & Console Overhaul

Address user feedback regarding hotkey accessibility, in-game menu binding via FLICK (`FUCK.dll`), new point navigation order, and console argument parsing.

* **Console Commands Multi-token Fix & Resilience:**
  * Fixed Skyrim console compilation issue where `scf order return` was rejected due to single `SCRIPT_PARAMETER`. Expanded `g_scfParams` to 4 parameters (`kChar`) and updated `ExecuteSCFCommand` to iterate through all `StringChunk` objects via `chunk->AsString()->GetNext()`.
  * Added resilient console alias `sfc` (bound via `BetaComment`) so common typos (`sfc order return` or `sfc status`) work identically.
  * Overhauled command reference: typing `scf help` or `scf order` (without arguments) now outputs a complete, clear instruction set of all available squad orders.
* **Squad Orders — "Move To Point" (`SummonOrder::MoveTo`):**
  * Extended `SummonOrder::MoveTo` and added `Vec3 targetPosition` to `Command` and `SummonModel`.
  * Integrated 3D crosshair raycast collision hit (`CrosshairPickData::collisionPoint[0]`) to resolve the exact ground/surface coordinates where the player aims.
  * In `StateMachine::Update()`, `MoveTo` directs summons into `Approach` toward destination coordinates, transitioning smoothly to `Guard` upon arrival (within 150 units).
  * Added console support: `scf order moveto [x y z]` and `scf order moveto` (uses crosshair raycast).
* **Hotkeys Migrated to Numpad:**
  * Replaced letter hotkeys with standard DirectX scan codes on the numeric keypad:
    * `Numpad 1` (`0x4F`): Focus Target under crosshair
    * `Numpad 2` (`0x50`): Return to owner
    * `Numpad 3` (`0x51`): Toggle aggression mode (`Passive` -> `Defensive` -> `Aggressive`)
    * `Numpad 4` (`0x4B`): Move To Point under crosshair
    * `Numpad 5` (`0x4C`): Wait / Guard position
    * `Numpad 6` (`0x4D`): Follow owner in formation
* **FLICK Menu Integration (`FUCK.dll`):**
  * Copied `FUCK_API.h` (v7) and ImGui type headers to `src/third_party/fuck` and `src/third_party/imgui`.
  * Created `src/Integrations/FlickMenu.h/.cpp` implementing `FUCK::ITool` under category `Gameplay`.
  * Dynamic discovery of `FUCK.dll` via `GetModuleHandleW` without hard link dependencies.
  * Implemented interactive hotkey binding (`FUCK::Hotkey` and `FUCK::StartBinding` on the input thread via `OnAsyncInput`). Clicking any hotkey slot prompts "Press any key..." and binds the pressed keyboard or mouse button in real time.
  * Implemented tuning controls for combat thresholds, friendly fire protections, collision bypass, and INI file persistence ("Save to INI", "Reload INI", "Restore Defaults").
* **Verification & Automated Testing:**
  * All 75 automated unit tests pass cleanly in `scf_tests.exe`.
  * `SummonControlFramework.dll` compiled and deployed cleanly to both `[STB] Mod Organizer` and `[RE2CE TEST] Mod Organizer`.

---

## 2026-11-10 — Target Dummy Combat Pathing Workaround (User Solution for MoveTo & Return)

Implemented the navigation workaround proposed directly by the user: using Skyrim's native combat AI system to drive physical running/sprinting towards a targeted position without teleports or synthetic package failures.

* **Target Dummy Navigation Workaround (`SummonOrder::MoveTo`):**
  * Spawning runtime AI packages without an ESP/ESL form record caused Skyrim's summoned creatures to ignore `kTravel` run-once packages due to high-priority follow quest overrides.
  * Workaround implementation in `src/Engine/UpdateHook.cpp`:
    1. An invisible, immortal ghost target dummy actor is spawned at the crosshair 3D raycast point (`dest`).
    2. The dummy is initialized with `SetAlpha(0.0f)`, `SpeedMult = 0.0f` (stationary beacon), `99999 HP`, and snapped to the nearest navmesh vertex (`MoveToNearestNavmesh()`).
    3. The summon is ordered to engage combat with the dummy (`StartCombat(dummyActor)` and `combatController->targetHandle = dummyActor`).
    4. Skyrim's combat navigation engine takes over immediately, forcing the summon to run/sprint at full speed across any terrain/navmesh directly to the beacon.
* **Proximity Arrival & Guard Transition:**
  * When the summon reaches proximity (`dist <= 180.0f` from the destination dummy, or on 25s timeout):
    1. Summon disengages combat (`StopCombat()` and target handles cleared).
    2. Dummy actor is disabled and marked for deletion (`dummy->Disable()`, `dummy->SetDelete(true)`).
    3. Summon `SpeedMult` is set to `0.0f` to prevent it from wandering or walking back to the player, cleanly holding its guard position at the target point.
    4. FSM state transitions to `Guard`.
* **Return Order (`SummonOrder::Return`):**
  * Cleans up any active dummy beacon immediately.
  * Restores `SpeedMult` to `100.0f` on the summon.
  * Disengages combat on the summon.
  * With combat cleared and speed restored, the summon's native follow package smoothly navigates it back to the player.
  * Once within follow range (`dist <= followMinDistance`), the order transitions to `Follow`.
* **Verification & Build:**
  * All 75 automated unit tests pass cleanly in `scf_tests.exe`.
  * `SummonControlFramework.dll` compiled and deployed to both MO2 plugin folders.

---

## 2026-11-10 — Order Execution Rework: `SummonMovement` (KeepOffsetFromActor) instead of combat dummies

Diagnosis of the "binds do nothing" report and a new physical execution layer for squad orders.

* **Field evidence (STB, profile SBORKA, `SummonControlFramework.log` 11:27–11:28):**
  * Input plumbing is healthy: every Numpad press arrives (`[InputHandler] SCF: Order -> ...`), the command is popped, and `[Execution]` runs for the summon.
  * The summons did not obey anyway. The only thing `Return` did was `StopCombat()` + `EvaluatePackage()`, which for a summoned creature creates no movement intent — a no-op in practice.
  * The dummy-beacon build before that did walk the summon to a point (combat AI pathing), but left it fighting an invisible immortal dummy; the 11:28 Return presses (9 in a row) could not disengage it, so the approach was reverted to the "[Pending R&D]" MoveTo in the working tree.
* **Root cause:** SCF had orders (Core/FSM), but no engine-side execution of "where the body should go". `SetRunOncePackage(nullptr)` / runtime packages / dummy actors were all attempts to borrow an intent the engine owns. `Actor::KeepOffsetFromActor` is the engine's own intent primitive (it is what a follower's movement goal uses).
* **Implemented — new layer `src/Engine/SummonMovement.{h,cpp}` + pure `Core/Formation` + `Core/MovementIntent`:**
  * `Actor::KeepOffsetFromActor` (`RELOCATION_ID(36870, 37894)`), `ClearKeepOffsetFromActor` (`36871, 37895`), `SetDontMove` (`36490, 37489`). Not bound by the pinned CommonLibSSE-NG; ids/signature cross-checked against the unpacked 1.5.97 image (`addrlib.py`: `36870 -> +603990`, `36871 -> +603AC0`; disassembly shows `Actor+0x148 movementController` RTTI-cast to `IMovementSetKeepOffsetFromActor`, slot 1, nine-field record) and against APMF's verified address table.
  * Offset semantics from CK Wiki / Papyrus Index: target **body space** (+X right, +Y forward), `catchUpRadius` = run-to-catch-up radius, `followRadius` = stand-still radius, no teleport.
  * Order → intent matrix: `Follow` → formation slot keep-offset (suppressed while the engine reports the summon in combat), `Return` → keep-offset at the owner and flip to `Follow` on arrival, `MoveTo` → owner-relative offset of the world point recomputed every tick, `WaitGuard` → goal parked on the summon itself + `SetDontMove(true)`, in-combat → no primitives.
  * Formation slot table moved into `Core::Formation` and is now shared with `SquadCoordinator` (single source of truth); command broadcast/iteration order is sorted by FormID so slots stay stable between ticks.
  * Emergency catch-up warp: only beyond `[Recovery] fCatchUpDistance` or after 3 s without 50 units of progress, once per `Return` order, same-cell only, always logged.
  * Primitive bookkeeping per summon: re-issue only on change (8-unit epsilon), keep-offset re-asserted every 2 s (the vanilla summoned-creature follow may rewrite it), `SetDontMove(false)` forced on first discovery because the flag survives save/load, state dropped via `RetainOnly` / `ResetAll`.
  * `FocusTarget` keeps `StartCombat`; `SetRunOncePackage(nullptr)` + `EvaluatePackage` removed from Return/Guard/Follow because their effect was unverified and they competed with the new intent.
  * Registry per-tick `[Registry] Updated existing summon` info line removed: it printed once per summon per 15 Hz tick and drowned the log (~132 KB per session).
  * FLICK menu: `Formation Distance`, `Catch-Up Run Distance`, `Combat Leash`, `Combat Leash Radius`, `Catch-Up Warp` + `Catch-Up Warp Distance` replace the misleading `Follow Distance` binding to `combat.followDistanceThreshold`.
  * Mouse-bound hotkeys now actually work: `InputHandler` used to drop `kMouse` events and `FlickMenu` used to refuse keys `>= FUCK::Keys::kMBBase` (256), so a rebind to a mouse button was silently ignored while ARCHITECTURE §18.1 claimed mouse support. The input sink now maps mouse buttons into the same number space (`GetIDCode() + SKSE::InputMap::kMacro_MouseButtonOffset`) and the menu accepts them.
* **Tests:** 14 new pure unit tests in `tests/test_movement_intent.cpp` (formation geometry, body-space round trips, intent matrix, MoveTo world-destination invariance, arrival radius). `scf_tests.exe`: **89/89 passing** (was 75/75).
* **Build:** `SummonControlFramework.dll` compiled with zero warnings and auto-deployed to both MO2 instances.
* **NOT verified in game yet:** whether a *summoned creature* honours `KeepOffsetFromActor` (the vanilla summoned-creature follow may fight it), whether `SetDontMove(true)` still allows in-place attacks, and the warp thresholds. The user tests in game; the log now has `[Movement]` lines that say what was applied and why.

---

## 2026-11-10 — Field diagnosis of the movement layer (backwards wolf, Focus / MoveTo)

First in-game session with `SummonMovement` (STB/SBORKA, log 13:31–13:38). The user reported: the wolf walks backwards, Focus Target (Numpad 1) and Move To Point (Numpad 4) do not work, the rest works.

* **What the log proved:**
  * Keys arrive and execute: 25 × Return, 22 × MoveTo, 41 × Focus presses all reached `[Execution]`.
  * `Follow`, `Return` and `Guard` are real: `KeepOffsetFromActor` moves a summoned wolf, `reached owner (199) -> Follow` fired, `SetDontMove(true)` freezes it in place, and the 3 s stuck-warp fired twice (878 and 459 units) when walking made no progress. So the primitive choice is correct.
  * **Backwards walking = facing lock.** `KeepOffsetFromActor` also holds the actor's *rotation*: with the offset-angle at 0 it faces where the player faces. Whenever the goal lay behind or beside the player (formation slot, a MoveTo point the player had walked past) the wolf moonwalked. The axis convention is confirmed by the log: while the player spun, the local X/Y of a fixed world point swung while Z stayed constant, so X/Y are horizontal and Z is vertical.
  * **Focus was executed but ignored.** `SetFocusTarget` ran `StartCombat` (e.g. 0x000F31C0 "Воин Серебряной руки"), but the model order was still `MoveTo`, and the intent matrix only suppressed movement for `Follow` — so the keep-offset goal kept dragging the wolf away from the fight until the 45 s MoveTo timeout. The same applied to a parked `Guard`.
  * **MoveTo always used the 500-units-ahead fallback.** Every order began with a local offset of exactly `(0, 500, 0)`: `CrosshairPickData::collisionPoint` is never filled when the crosshair is over terrain (the interaction pick fills it for references only).
  * **Focus only resolved `target`.** 33 of 41 presses logged `No target under crosshair`; the 8 successes were enemies close enough for the interaction pick. `CrosshairPickData::targetActor` (the aim actor) is never read by CommonLib's `GetActiveTarget()`.
  * After a teleport/cell change with a live MoveTo order the owner-relative offset became absurd (60404, -42954, ...) because the stored destination was 74k units away — it kept the wolf chasing a stale point until the 45 s timeout.
* **Fixes:**
  * `core::FacingOffsetDegrees` (Owner body-space geometry + tests) computes the direction of travel relative to the owner's heading; it is passed as the offset-angle Z so the actor faces where it walks. Standing on the goal resets it to 0 (formation look) within 100 units. Re-issued when the offset or facing changes (8 units / 10°).
  * `SummonOrder::Attack`: `CommandQueue::ApplyToModel` turns an explicit FocusTarget into it (and drops any move destination); the intent matrix returns `None` for it, so it clears `SetDontMove` and lets the combat AI own locomotion. It reverts to `Follow` when the target dies/disappears or combat never starts within 2 s.
  * `MoveTo` destinations farther than 8000 units from the owner are abandoned (`abandoned -> Follow`), which kills the stale-point chases after a teleport.
  * `InputHandler` resolves the crosshair actor from `targetActor` first, then `target`; MoveTo tries `collisionPoint`, then the crosshair reference position, then an **aim projection** (player yaw + `|Actor::GetAimAngle()|`, distance `110 / tan(pitch)` clamped to [200, 2500]) so aiming at the ground produces a nearby point instead of "500 straight ahead". Every press logs its source (`MoveTo destination from ...`), and a failed Focus dumps `[Crosshair] target=... targetActor=... point=...`.
* **Tests:** 20 pure unit tests in `tests/test_movement_intent.cpp` (6 new: facing cardinals, facing relative to owner heading, degenerate facing, Attack outranking movement, follow-facing while away from the slot, stale MoveTo). `scf_tests.exe`: **95/95 passing**.
* **Build:** clean, auto-deployed to both MO2 instances.
* **Pending:** re-test in game (TEST-ORDER-1/4/8/10/11/12/13/14 in TESTING.md). The remaining unknown is whether the offset-angle really controls facing on this runtime — if the wolf now turns correctly but goes to a rotated spot, the angle rotates the offset instead and the facing has to be handled differently (logged facing values make that diagnosable from one session).

---

## 2026-11-10 — Full combat control: `CombatGate` seat, `CombatPolicy`, AttackApproach

Second field session (STB/SBORKA, log 19:32–19:35) after the facing / aim fixes. Report: Move To Point works, Focus Target does not; the summon still behaves as if it were running two behaviour patterns at once. The player's requirement is explicit: the summon must abandon its own AI patterns, live by the FSM states, never attack in `Passive` under any circumstances, and on `Return` it must leave combat and only re-enter after reaching the player.

* **What the log proved:**
  * `FollowSlot` now logs a real `facing=` value (e.g. -167, then the travel direction), and Move To Point works from the aim projection (`aimAngle=0.05 rad → 2020 units`, `0.17 → 646`) — both previous defects fixed.
  * Focus *was* executed: `[Execution] Summon ... switching focus to 0x000F31C0`, `SetDontMove(false)`, `keep-offset cleared (HoldPosition)`, FSM `Guard -> AcquireTarget`, and no `attack order finished` reversion — i.e. `StartCombat` apparently left the summon "in combat", yet it did not attack. The vanilla commanded-actor logic kept the behaviour.
  * The crosshair collided with terrain: `collisionPoint` holds uninitialised garbage (≈FLT_MAX) when the interaction pick has no reference, so `IsSaneWorldPoint` rejected it; that is why the aim projection fallback is needed. Focus presses still failed 12 times for every success — the interaction pick is not reliable for far actors.
* **Research decision (Combat_Override_Package_List_Research.md §13):** CK `Combat Override Package List` + `HoldPosition` is a *soft* leash: it keeps the combat AI and Combat Style, cannot forbid fighting in `Passive`, cannot force a break on `Return`, cannot restrict to one target, and needs an ESP/ESL with packages, a quest and an alias pool. Rejected for SCF; the document keeps it as the description of a discarded alternative.
* **Implemented — combat denial at the engine seat:**
  * `src/Engine/CombatGate.{h,cpp}`: a hook on `Actor::StartCombat` (`RELOCATION_ID(37608, 38561)`, SE 1.5.97 → `SkyrimSE.exe+6251B0`, `write_branch<5>` through the SKSE trampoline; prologue `mov r11, rsp; push rsi; push rdi; ...` verified relocatable). For a registered summon it returns `false` — the same answer the engine gives for a dead self — unless the published `CombatPolicy` allows the fight. A focus order is restricted to exactly one actor. Thunk is thread-safe (mutex-protected table, log throttled to one refusal per 2 s per summon); the main thread publishes policies.
  * Second layer: `Actor::BOOL_FLAGS::kAttackingDisabled | kCastingDisabled` while combat is forbidden (the native wholesale deny APMF documents), plus an immediate `StopCombat` + cleared `currentCombatTarget`/`CombatController` when the policy flips.
  * `src/Core/CombatPolicy.{h,cpp}`: `Passive` never fights (even `FocusTarget`), `Return` and `MoveTo` forbid combat until the order ends, `Attack` restricts to the focused target, `Guard`/`Follow` allow it.
  * `BuildMovementIntent(..., a_combatPermitted, ...)`: with combat forbidden, `Follow` keeps the formation (previously it stood still while the engine dragged it), and a forbidden `Attack` falls through to the follow rules.
  * `MovementMode::AttackApproach`: outside 150 units the summon is driven to the focused actor by keep-offset (facing the target), inside it the goal is released to the combat AI; `StartCombat` is retried once a second through the same seat.
  * Focus resolution: `targetActor` → `target` → **aim-cone fallback** (closest hostile actor within 25° and 2500 units, `IsHostileToActor(player)`); in `Passive` the order is refused with `SCF: Passive mode - attack suppressed` instead of a false confirmation.
  * `[Execution] ... StartCombat(0x...) -> true|false (dist ..., hostile ..., inCombat ...)` and `[CombatGate] StartCombat refused ...` lines make the next field session diagnostic instead of guesswork.
* **Tests:** 7 new pure cases (CombatPolicy matrix + movement under forbidden combat); `tests/test_movement_intent.cpp` now 27 cases, `scf_tests.exe` **102/102 passing**.
* **Build:** clean, auto-deployed to both MO2 instances.
* **Not verified in game.** Open question: whether refusing `StartCombat` is enough to stop the vanilla commanded-actor combat, or whether the summon will still try (the `AttackApproach` assist guarantees the charge for a focused target). The next session's `[CombatGate]`/`[Execution] StartCombat` lines decide the follow-up.

---

## 2026-11-10 — Crash postmortem: `StartCombat` entry hook replaced by call-site patches

The first field run of the `CombatGate` ended in an access violation ~75 seconds into the session (log `Crashlogs/2026-11-10-19-53-29.log`). Recorded here because the mistake is one this project could repeat.

* **Symptom.**`EXCEPTION_ACCESS_VIOLATION at 0x7FF778072E40`, on a `BSJobs__JobThread` (non-render-safe AI) thread, inside the probable stack `FUN_140688270 (+0x688493) <- ActorProcess::unk_Assault <- ActorKnowledge <- StandardDetectionListener`. Our own log had 11 lines earlier:
  `[CombatGate] StartCombat denial seat installed at 0x7FF720B051B0 (original 0x7FF778072E40).`
  The "original" we called was exactly the fault address. The crash caller `0x68848E` is one of the ten engine call sites of `Actor::StartCombat` (it calls `StartCombat` with `mov r8,r15; mov rdx,rsi; mov rcx,rdi; call 0x6251B0`).
* **Root cause.** In this CommonLibSSE-NG revision, `Trampoline::write_branch<N>` is a **site patcher, not a trampoline factory**: `write_5branch` overwrites the site with `E8/E9 rel32` pointing at a near stub (`jmp [rip]; dq dst`), and the private `write_branch` returns `func = site + N + *disp` — the *previous branch target computed from the overwritten bytes*. On a call site that is the old callee (correct, and why `HitHook`'s `write_call<5>` has always worked); on a **function entry** those bytes are a prologue (`mov r11, rsp; push rsi; ...`), so the "original" is garbage. Patching `StartCombat`'s entry and calling that garbage crashed the first AI thread that started a fight.
* **Fix.** `CombatGate::Install` no longer touches the function entry. It scans the `.text` segment for direct branches whose rel32 resolves exactly to `Actor::StartCombat` and patches each one: 9 `E8` call sites via `write_call<5>` and the one `E9` argument-shuffling wrapper at `0x64776C` (`mov rax,r8; mov rcx,rdx; mov rdx,rax; mov r8,r9; jmp StartCombat`) via `write_branch<5>`, because that site must stay a jump (it is a tail call and has no fall-through epilogue). The thunk calls the real function by its verified address (the entry is unpatched, so there is no recursion). Offline verification against the unpacked 1.5.97 image: exactly 10 sites (9 calls + 1 tail call), matching APMF's "ten StartCombat callers" research, and the crash's caller `0x68848E` is one of them.
* **Hardening.** `SKSE::Init` trampoline raised from 64 to 1024 bytes: every patched site needs a 14-byte stub in that pool (one per unique destination, shared by all sites).
* **Checked.** Build clean, deployed; `scf_tests` 102/102. The install line now reads `StartCombat denial seat installed on 10 site(s) (1 tail call(s)) of 0x...; function entry untouched.` — the next session's log confirms it on the real image.
* **Rule learned.** In this fork `write_branch`/`write_call` return the previous *branch target*, never a callable copy of the original. Only call-site patches keep the original callable; a function-entry hook needs a real trampoline (copied prologue + jump back), which this fork does not provide.

---

## 2026-11-10 — FocusTarget через `CombatController`: пин цели в селекторах, без записей в контроллер

Ревизия всего, что сделано по контролю боя, по подсказке пользователя «combatController может помочь для управления поведением сумонов». Полевого лога после исправления краша нет (последний `SummonControlFramework.log` — сессия краша 19:53), поэтому всё ниже — анализ кода и дизассемблера, **не проверено в игре**.

* **Корневая причина «Focus исполняется, но игнорируется» (подтверждено дизассемблером 1.5.97):** `Actor::UpdateCombat` на каждом боевом тике вызывает `CombatController::UpdateTarget` (id 32489 → `+4FE300`). Тот обходит `targetSelectors`, для активных вызывает слот 6 (`call [rax+0x30]`, `rdx` = слот на стеке), берёт первый ненулевой ответ и при отличии от `controller+0x2C` вызывает `CombatController::SetTarget` (id 32488 → `+4FDFE0`). Значит запись `cc->targetHandle` / `currentCombatTarget` из главного потока живёт до следующего боевого тика, а сама запись — гонка с BSJobs-потоками, которые держат контроллер. Слот 6 селекторов: `Standard` (vtable id 265605 → `+1690A98`, функция `+7B5E50`, читает контроллер через `[this+0x10]`, группу через `[controller+0]`), `Fixed` (265607 → `+1690AF8`, `+7B6920`: `mov eax,[rcx+0x18]; mov [rdx],eax; mov rax,rdx; ret`). Совпадает с таблицей APMF (`APMF/Docs/VERIFIED-ADDRESSES.md`, ch.20 Target Pin) — APMF использовали как справочник, код написан заново под SCF.
* **Сделано:**
  * `CombatGate`: chaining-хук слота 6 обоих селекторов. Для призыва с фокусом ответ заменяется фокусом, если движок держит цель, фокус жив/загружен и является целью группы без `kTargetLost` (чтение под read-lock группы). Правило вынесено в чистый `core::ChooseSelectorAnswer` (`src/Core/TargetPin.*`), 7 unit-тестов. Быстрый путь — атомарный счётчик пинов (seat вызывается для каждого воюющего NPC игры). Разрешение `ActorHandle` фокуса — только в главном потоке.
  * Записи `CombatController::targetHandle`/`startedCombat` из главного потока удалены отовсюду (Focus, Return, Guard, Follow, Passive, leash, смена политики); разрыв боя — `combat::BreakCombat` (`StopCombat` + сброс `currentCombatTarget`).
  * `AttackApproach` (keep-offset к фокусу) работает только пока движок не воюет с фокусом; во время боя с ним движением владеет боевой AI — устраняет конкуренцию двух целей движения и не тащит дальнобойных атронахов в ближний бой. Если суммон воюет с другим, раз в секунду `StartCombat(focus)` добавляет фокус в цели группы (так он становится пригодным для пина).
  * `ShareLoadedSpace` вместо `GetParentCell() ==` (фокус и гейт движения): снаружи сосед по загруженной сетке — нормальная ситуация, старое сравнение сбрасывало фокус и замораживало приказы.
* **Отклонено:** писать цель в контроллер чаще (каждый кадр) — та же гонка и всё равно проигрывает `UpdateTarget`; хук `UpdateCombat` с перезаписью после тика — APMF отказался от этого как от маскирующего пояса, а источник выбора (селектор) закрывает задачу целиком.
* **Проверено:** полная пересборка DLL без предупреждений в нашем коде (единственное — в стороннем minhook), `scf_tests` 109/109, DLL разложена в обе MO2-сборки.
* **Ревью незакоммиченных правок FF** (`HitHook`, `ProjectileHook`, `PrecisionBackend` — были в рабочем дереве до этой задачи): слоты `AddImpact`/`HandleHits` (0xBD/0xBE) у `FlameProjectile`/`ConeProjectile`/`BeamProjectile` существуют, у `Cone` собственный `HandleHits` с тем же контрактом коллектора (`[rdx+0x10]` данные, `[rdx+0x18]` размер, шаг 0x40). Ошибок не найдено; правки оставлены незакоммиченными как чужие (AGENTS.md §9).
* **Осталось:** полевой тест TEST-COMBAT-1…11. Главный вопрос первой сессии — появится ли `[TargetPin] selector seat OBSERVED running` и `answered with focus` (APMF помечает тот же seat как «не наблюдался в поле»).

---

## 2026-11-11 — Полевой лог 23:44–23:53: «поводок» FollowTo, подход к Focus от цели

Отчёт пользователя: по Numpad 1 призывы не нападают; по Numpad 4 не бегут к точке, а всё время останавливаются и смотрят на игрока, «как будто их тянет ко мне». Дополнительно запрошено: призывы не должны учитывать друг друга в движении (проходить насквозь) — внесено в PLAN.md как TODO (этап 2).

* **Что доказал лог:**
  * Сборка с пином цели загрузилась: `StartCombat denial seat installed on 10 site(s)`, `selector seat OBSERVED running`. Пин работает: второй Focus — `StartCombat -> true`, `answered with focus (engine already agreed)`.
  * Ни один MoveTo за сессию не дошёл до `arrived at MoveTo point` (точки в 358–1719 ед., 10–30 с на каждую).
  * Первый Focus на 2142 ед.: `StartCombat -> false` (движок не начинает бой с необнаруженной/далёкой целью), подход keep-offset'ом не дал прогресса (2140 → 2075 → 2085 ед.), через 5 с `combat never started -> Follow`. Второй Focus: бой с фокусом начался, но волк так и не сократил 1300–1600 ед.
* **Корневая причина (дизассемблер 1.5.97 + Skyrim.esm):** GMST `fSummonedCreatureMinFollowDist` читает только функция id 36410 (`+5DBA80`); она строит призыву runtime-пакет — экземпляр шаблона `"FollowTo"` (`+43D460`, строка `+160D9F8`; PACK `00025E6D`: `Min Radius` uid 4, `Max Radius:` uid 5) — с Min=200. Этот пакет постоянно возвращает призыва в 200 ед. от игрока и спорит с нашим keep-offset. Follow/Return «подтверждались» в том числе потому, что их направление совпадает с ванильным следованием. Дополнительно: keep-offset подхода к Focus был задан относительно игрока и смещался с каждым его шагом.
* **Проверено попутно:** `Actor::KeepOffsetFromActor` (36870) вызывается в движке только из обёртки Papyrus (id 53897), ванильный ИИ его не перезаписывает — значит, конкурент именно пакет, а не чужой keep-offset.
* **Сделано:** `src/Engine/FollowLeash.{h,cpp}` — на `MoveTo`/`Attack`/`WaitGuard` радиусы экземпляра `FollowTo` = 100000, на `Follow`/`Return` — значения GMST (только если их ставил SCF); каждая запись проверяется по vtable `BGSPackageDataFloat` (значение по `+0x10`, проверено по Assign/LoadBuffer). Правило в ядре — `core::ReleasesFollowLeash` + тест. Подход к Focus якорится на самой цели, боевому AI отдаётся только ближе 400 ед.; ожидание входа в бой 15 с.
* **Не сделано / риски:** не проверено, перечитывает ли процедура FollowTo радиусы на лету (если нет — эффект появится только после переоценки пакета; лог `[Leash]` покажет, что запись произошла). MoveTo по-прежнему якорится на игроке (статичного актора-якоря в точке нет); при отпущенном поводке смещение цели ограничено одним тиком.
* **Проверки:** сборка без предупреждений, `scf_tests` 110/110, DLL в обеих MO2-сборках. В игре не проверено.

---

## 2026-11-11 — Поле 00:08–00:13: поводок не отпущен; анализ миньонов PoE2

* **Факт:** все строки `[Leash]` за сессию — `running package 0xFF… (type 18, template 0x00000000) is not a FollowTo instance ... not released`. Runtime-пакет призыва не хранит `templateParent`, поэтому проверка из `FollowLeash` (по `templateParent == 00025E6D`) никогда не проходила, и исправление от 2026-11-11 фактически не действовало. Пользователь подтверждает: волки по-прежнему прерывают движение к точке `MoveTo` (за сессию одно `arrived at MoveTo point`).
* **Focus:** `add-target StartCombat(...) -> true` и `answered with focus` для всех 4 волков — вход в бой с назначенной целью и пин работают.
* **Не исправлялось в этой записи** (по запросу пользователя — только отметить и спланировать). Следующий шаг — этап M0: опознавать пакет по факту создания (call-site у `+5DD030` внутри id 36410) или по форме.
* **Анализ PoE2:** механики миньонов (постоянные, авто-возрождение ~7 с, плотное следование с телепортом, радиус агрессии, командные навыки, фокус по цели игрока, большие армии, панель) сопоставлены с SCF; вывод — большая часть реализуема как Skyrim-версия, ограничение — размер армии и навмеш-локомоция, блокер — полное владение движением (M0). План: `PoE2_Minion_Parity_Plan.md`.

---

## 2026-11-11 — M0: опознание пакета следования призыва по факту создания и по форме

* **Причина:** поле 00:08 показало `templateParent = 0` у runtime-пакета призыва, и отпуск поводка не срабатывал (запись выше).
* **Исследование (1.5.97):** id 36422 (`+5DD030`, создаёт пакет из шаблона и кладёт его актору) имеет ровно два прямых вызова — `+405622` и `+5DBB04` внутри id 36410 (= 36410 + 0x84). Аргументы на этом сайте: `rcx` призыв, `rdx` владелец, `r8` 0, `r9` локация, на стеке Min (`xmm7` из `fSummonedCreatureMinFollowDist`), Max (`xmm6` из `fSummonedCreatureMaxFollowDist`), 0, 1; результат — `TESPackage*`.
* **Сделано:** seat `write_call<5>` на `+5DBB04` (только SE 1.5.97, проверка `E8` + цели перед патчем; AE-образа на машине нет — там seat не ставится, это записано в логе). Thunk запоминает созданный пакет и, если призыв под `MoveTo`/`Attack`/`WaitGuard`, создаёт его сразу с радиусами 100000. Запасное опознание по форме `core::IsSummonFollowPackageShape` + тест. Лог указывает, как опознан пакет.
* **Риск/открытый вопрос:** перечитывает ли процедура FollowTo радиусы на лету. Если в поле появится `RELEASED`, но прерывания останутся — следующий шаг: пересоздание пакета после отпуска (seat создаст новый уже отпущенным).
* **Проверки:** сборка без предупреждений, `scf_tests` 111/111, DLL в обеих MO2-сборках. В игре не проверено.

---

## 2026-11-11 — Призывы проходят сквозь друг друга: seat'ы обхода в движении и фильтра коллизий

Запрос пользователя: призывы бегут к цели сквозь друг друга, не пытаясь обходить по навмешу. Механики PoE2/M0 отложены до конца по решению пользователя.

* **Находка:** `CollisionPairClassifier` нигде не подключён к движку (используется только в тестах). Подтверждённое в поле прохождение игрока сквозь призывов, скорее всего, даёт мод «Disable Follower Collision» из профиля SBORKA; призывы друг с другом SCF никогда не разводил.
* **Исследование (1.5.97, дизассемблер):**
  * Нашёл в RTTI `MovementAgentActorAvoider`, `MovementAvoidanceManager`, `MovementPathManagerAgentAvoidBox`, `MovementActorAvoidanceParameters` (последний есть в `BSPathingRequest` на `+0xD8`).
  * Синглтон `IMovementAvoidanceManager` (`+2F45418`) используют: ActorAvoider (id 89716, 89723), id 90636 и id 90637. Обе последние функции первым делом вызывают слот 4 менеджера (`[vtable+0x20]`, `edx` = self, `r8d` = other) и при `false` отвечают «не обходить»; дальше — режимы параметров (все / никого / список) и сравнение приоритетов. Слот 4 (id 40659) в ванили всегда `true`, кроме случая «other — игрок» (тогда — обнаруживает ли self игрока).
  * Фильтр Havok: `bhkCollisionFilter` sub-vtable `[1]` слот 1 (id 76676) — `hkBool* (this, hkBool* out, a, b)`, читает filter info коллайдеров по `+0x2C` и зовёт `+DD6A80`. Объявление CommonLib неверно по ABI.
  * `Actor::GetCollisionFilterInfo` (id 36559) берёт filter info контроллера персонажа (`currentProcess` → controller → vfunc 8).
* **Сделано:** `core::ShouldPassThrough` + 5 тестов; `src/Engine/PassThrough.{h,cpp}` — два seat'а с проверкой слота перед `write_vfunc`, таблица «владелец + призывы + system group» из `UpdateHook` под `shared_mutex`, сводка в логе раз в 10 с. Только SE 1.5.97 (AE-образа нет).
* **Риски / не проверено:** уникальна ли system group контроллера на актора (если нет — пара просто не опознается, поведение ванильное); участвует ли фильтр collidable-collidable в столкновениях прокси персонажей — покажут счётчики `body collisions skipped`; путь по навмешу мог дополнительно учитывать avoid box'ы (`MovementPathManagerAgentAvoidBox`) не через этот слот — тогда в поле обход сохранится частично, следующий шаг — этот агент.
* **Проверки:** сборка без предупреждений, `scf_tests` 116/116, DLL в обеих MO2-сборках. В игре не проверено.
