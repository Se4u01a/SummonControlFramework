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
* **Stealth Coordination Subsystem (`StealthContext`):**
  * Pure C++ `StealthContext` (`playerSneaking`, `playerDetected`, `explicitAttackOrder`) integrated cleanly into `DecisionContext`.
  * **Stealth-aware Target Scoring (`TargetSelector`):** when the player is sneaking and undetected (`playerSneaking && !playerDetected`) in `Defensive` mode, summons suppress attacks against unaware enemies to preserve player concealment.
  * **Explicit Overrides & Combat Reactions:** unprovoked attacks against the player/summon, explicit `FocusTarget` orders, or `Aggressive` mode override stealth suppression.
  * **FSM Sneak Preservation (`StateMachine`):** holds `FollowOwner` state during stealth without prematurely engaging distant unalerted enemies.
  * Covered by 7 comprehensive unit tests in `tests/test_stealth.cpp` (102/102 tests passing).
* **Physical Order Execution (`SummonMovement`):**
  * New engine layer `src/Engine/SummonMovement.{h,cpp}` executes the order stored on `SummonModel` every tactical tick (15 Hz, main thread via the existing `PlayerCharacter::Update` hook): no new hooks, threads, spawned actors or borrowed AI packages.
  * Engine primitives (not bound by the pinned CommonLibSSE-NG, resolved via Address Library and verified against the unpacked 1.5.97 image): `Actor::KeepOffsetFromActor` (`SE 36870 / AE 37894`), `Actor::ClearKeepOffsetFromActor` (`36871 / 37895`), `Actor::SetDontMove` (`36490 / 37489`).
  * Order → intent matrix: `Attack` (explicit FocusTarget) outranks every movement order, `Follow` holds the formation slot around the player (suppressed while fighting so combat AI keeps locomotion), `Return` runs to the player and flips to `Follow` on arrival, `MoveTo` walks to the world point (owner-relative offset recomputed every tick; a destination farther than 8000 units, e.g. left behind by a cell change, is abandoned), `WaitGuard` parks the movement goal on the summon and locks translation.
  * **Facing control:** `KeepOffsetFromActor` also holds the actor's rotation (angle 0 = face where the owner faces), which made the wolf walk backwards whenever its goal was behind or beside the player. `core::FacingOffsetDegrees` now feeds the direction of travel into the angle-Z argument; standing on the slot/point resets it to the owner's heading.
  * **Focus is not overridden by movement:** `CommandQueue::ApplyToModel` turns a `SetFocusTarget` into `SummonOrder::Attack` (drops any move destination), the intent matrix yields `None` for it, and the order reverts to `Follow` when the target dies/vanishes or combat never starts within 2 s.
  * Pure geometry/rules split: `src/Core/Formation.{h,cpp}` (slot table and body-space/facing math, shared with `SquadCoordinator`) and `src/Core/MovementIntent.{h,cpp}` (intent matrix, arrival radius, move-distance limit).
  * Emergency catch-up warp (only beyond `[Recovery] fCatchUpDistance` or after 3 s without progress, same-cell only, always logged; at most one warp per Return order).
  * Primitive state bookkeeping (`AppliedMovement`): re-issue only on change (8 units / 10°), keep-offset re-assert every 2 s, `SetDontMove(false)` forced on first discovery so a stale save/load flag cannot freeze a summon; state dropped on dispel/death/detach and on `kPreLoadGame`/`kNewGame`.
  * `SetRunOncePackage(nullptr)` / `EvaluatePackage()` removed from Return/Guard/Follow execution: they created no movement intent and competed with the new one.
  * Covered by 27 pure unit tests in `tests/test_movement_intent.cpp` (20 movement/formation/facing + 7 combat-policy; 102/102 tests passing).
  * **Field status:** the 2026-11-10 sessions confirmed that a summoned wolf *does* follow the keep-offset goal, that `Return` works (walking and the 3 s stuck-warp), that `Guard` freezes it in place, that the faced direction fixed the backwards walking, and that Move To Point now lands where the player aims. Focus Target was mechanically executed but ignored by the summon — addressed by the CombatGate (below).
* **Full Combat Control (`CombatGate` & `CombatPolicy`):**
  * New engine seat: the **10 direct call/jmp sites** of `Actor::StartCombat` (`RELOCATION_ID(37608, 38561)`, SE 1.5.97 → `SkyrimSE.exe+6251B0`) are patched to a thunk (9 × `write_call<5>` + the argument-shuffling `jmp` wrapper at `0x64776C` via `write_branch<5>`; the function entry itself is untouched). For a registered summon the call is refused (`return false`) unless its current `CombatPolicy` allows the fight; an explicit `FocusTarget` allows exactly one actor.
  * **Field crash fixed (2026-11-10):** the first seat patched `StartCombat`'s *entry* with `Trampoline::write_branch<5>` and called the value it returned as the original. In this CommonLibSSE-NG revision that function returns the *previous branch target* (garbage at a function entry), not a trampoline — the game died at that address on the AI job thread (`crash-2026-11-10-19-53-29.log`). The seat is now built from call-site patches, and the thunk calls the real function by its verified address.
  * Second layer: `Actor::BOOL_FLAGS::kAttackingDisabled | kCastingDisabled` while combat is forbidden (native wholesale deny), cleared when it is allowed again; an existing fight is broken immediately (`StopCombat` + cleared `currentCombatTarget` / `CombatController`).
  * `src/Core/CombatPolicy.{h,cpp}` — pure rules: `Passive` never fights (even on `FocusTarget`), `Return` and `MoveTo` forbid combat until the order ends, `Attack` restricts combat to the focused target, `Guard`/`Follow` allow it.
  * `BuildMovementIntent` now takes `a_combatPermitted`: with combat forbidden a `Follow` summon keeps its formation instead of standing still while the engine drags it.
  * `Attack` approach assist (`MovementMode::AttackApproach`): outside 150 units the summon is driven to the focused actor's world position by keep-offset (facing the target), inside it the goal is released to the combat AI; `StartCombat` is retried once per second if the engine dropped the fight.
  * Focus resolution: `targetActor` → `target` → aim-cone fallback (closest hostile actor within 25° of the player's facing, 2500 units); `Passive` refuses a Focus order with an explicit HUD message instead of a false confirmation.
  * Diagnostics: `StartCombat` result, target distance, hostility and combat flag are logged for every focus; a refused seat logs `[CombatGate] StartCombat refused`.
  * Covered by 7 new unit tests (CombatPolicy matrix + movement under forbidden combat).
  * **Field status:** build deployed, not yet re-tested in game.
* **Pass-through: summons no longer stand on each other — 2026-11-11:**
  * Field: summons passed through each other but could land on top of one another (their controller stood on the other's body bones, layer Biped 8 / BipedNoCC 33, which the seat did not cover).
  * The body seat now also disables controller (30) ↔ the other actor's Biped/BipedNoCC bones for the same pass-through pairs (`core::IsPassThroughLayerPair`, 1 test); separate counter in the 10 s summary. Field: still "micro-collisions", the bones counter stayed 0 (Skyrim.esm COLL: the controller never collides with Biped 8).
  * Follow-up: pairs Havok accepted before the table knew both actors keep colliding while they overlap. Every publish now re-filters the existing pairs of new / regrouped / removed actors through `bhkWorld::UpdateCollisionFilter` (id 76091) (`core::RowsToRefilter`, 3 tests — **125/125 passing**).
  * **Field status:** built and deployed, NOT tested in game (TEST-PASS-4).
* **PoE2 stage M2: attack what I attack + aggression radius — 2026-11-11:**
  * `TESHitEvent` sink: when the player hits an enemy, summons in `Follow` (or `Attack` on another target) get a FocusTarget on it; never in `Passive`, never over `Return`/`MoveTo`/`Guard` (`core::ShouldAssistPlayerHit`). INI `[Combat] bAssistPlayerAttacks`.
  * StartCombat seat: engine-chosen fights in `Follow`/`Guard` only against actors within `[Combat] fAggressionRadius` (1500, 0 = off) of the player (`core::IsWithinAggressionRadius`). Both in FLICK.
  * 4 new tests + settings checks — **121/121 passing**.
  * **Field status:** built and deployed, NOT tested in game (TEST-M2-1/2).
* **Friendly splash / magic damage filter (`MagicHook`) — 2026-11-11:**
  * Field: pass-through confirmed working by the user; a player fireball that hits the ground next to summons still damaged them (explosion effects are applied through `MagicTarget::AddTarget`, not `ProcessHit`).
  * Seat on `Character` MagicTarget vtable[3] slot 1 (SE id 37020 → `MagicTarget::AddTarget` id 32157): hostile/detrimental effects from the player, allies or their summons are refused on protected summons; buffs/heals and enemy spells unaffected. 1 new test — **117/117 passing**.
  * **Field status:** built and deployed, NOT tested in game (TEST-FF-AOE-1). Explosion knockback force is not covered.
* **Summons pass through each other (movement avoidance + bodies) — 2026-11-11:**
  * Finding: `CollisionPairClassifier` was never wired to an engine hook; the field-verified player-through-summon passthrough most likely came from the installed mod "Disable Follower Collision", not from SCF.
  * Seat 1 (SE 1.5.97): `MovementAvoidanceManager` slot 4 `ShouldAvoid(self, other)` (id 40659), asked first by both the local actor avoider and the path avoidance parameters — returns false for own summon↔summon and summon↔owner pairs.
  * Seat 2 (SE 1.5.97): `bhkCollisionFilter::IsCollisionEnabled(collidable, collidable)` (id 76676, real ABI with a hidden hkBool* return) — clears the result for two CharController collidables whose system groups belong to such a pair (groups published from `Actor::GetCollisionFilterInfo`).
  * Rule `core::ShouldPassThrough`, 5 tests — **116/116 passing**.
  * **Field status (2026-11-11 00:42):** works — the user confirmed summons run through each other; the log shows both seats running (`movement avoidance seat OBSERVED`, thousands of avoidance checks and hundreds of body collisions skipped per minute).
* **M0: follow package recognition without `templateParent` — 2026-11-11:**
  * The 00:08 session proved the follow-leash release never fired: the summon's runtime package (type 18) has `templateParent = 0`.
  * New creation seat (SE 1.5.97 only, byte-checked): call site `id 36410 + 0x84` → id 36422 is patched with `write_call<5>`; it records the package built for each summon and builds it already released under an away order. Fallback (AE, packages restored from a save): shape — runtime FormID `0xFF…`, type 18, exactly two float inputs (`core::IsSummonFollowPackageShape`, 1 test, **111/111 passing**).
  * **Field status:** built and deployed to both MO2 instances, NOT tested in game (TEST-ORDER-15/16). Open question: whether the FollowTo procedure re-reads the radii live.
* **Follow leash release + focus-anchored approach — 2026-11-11 (field log 23:44–23:53):**
  * Field report: Numpad 1 — summons do not attack; Numpad 4 — summons keep stopping and turning to the player instead of running to the point.
  * Log evidence: no MoveTo in the session ever logged `arrived at MoveTo point` (points 358–1719 units away, 10–30 s each); the first Focus at 2142 units got `StartCombat -> false` and was dropped after 5 s; the second Focus entered combat (`answered with focus (engine already agreed)`, so the target pin works) but the wolf never closed the 1300–1600 units.
  * Root cause (verified on the 1.5.97 image and Skyrim.esm): the summon's own runtime package is an instance of template `FollowTo` (00025E6D) built by id 36410 with `Min Radius = fSummonedCreatureMinFollowDist` (200); it keeps pulling the summon back to the player.
  * Fix: `src/Engine/FollowLeash.{h,cpp}` releases that instance's Min/Max Radius (100000) during `MoveTo`/`Attack`/`WaitGuard` and restores the GMST values on `Follow`/`Return`; the focus approach is anchored on the target actor itself and handed to the combat AI only within 400 units; attack grace 15 s. 1 new test — **110/110 passing**.
  * **Field status (session 2026-11-11 00:08):** NOT working — the release never fired: the summon's runtime package (type 18) has `templateParent = 0`, so it was never recognised as a FollowTo instance (`[Leash] ... template 0x00000000 ... not released`) and MoveTo is still interrupted. Focus did enter combat with the focus target. Next: stage M0 of `PoE2_Minion_Parity_Plan.md`.
* **Target Pin for FocusTarget (`CombatController` selectors) — 2026-11-10:**
  * Root cause of "Focus executed but ignored": the engine re-chooses a fighting actor's target on every `Actor::UpdateCombat` (`CombatController::UpdateTarget`, SE `+4FE300`, asks each `CombatTargetSelector` through vtable slot 6 and writes the first non-zero answer via `SetTarget`, `+4FDFE0`). SCF's main-thread writes to `CombatController::targetHandle` were overwritten on the next combat update (and raced the BSJobs combat workers).
  * New seat in `CombatGate`: chaining `write_vfunc` on slot 6 `SelectTarget` of `CombatTargetSelectorStandard` (SE vtable id 265605, function `+7B5E50`) and `CombatTargetSelectorFixed` (265607, `+7B6920`), verified on the unpacked 1.5.97 image. For a summon with a FocusTarget it answers with the focused actor when the engine holds a target, the focus is alive/loaded and it is a held (not `kTargetLost`) target of the summon's combat group (`core::ChooseSelectorAnswer`, `src/Core/TargetPin.{h,cpp}`).
  * Entry: `StartCombat(focus)` on the order; while the summon fights someone else, a `StartCombat(focus)` once per second adds the focus to its group's targets.
  * `AttackApproach` keep-offset only while the engine is not yet fighting the focus — the combat AI owns locomotion and range during the fight (ranged summons keep distance).
  * No `CombatController` member is written from the main thread any more: `combat::BreakCombat` = `StopCombat()` + reset of the actor's `currentCombatTarget`.
  * `engine::ShareLoadedSpace` replaces `GetParentCell() ==` for focus validity and movement gating: neighbouring loaded exterior cells of one worldspace count as the same space (the old test dropped focus across a cell border and froze orders while the summon trailed one cell behind).
  * Covered by 7 unit tests in `tests/test_target_pin.cpp` — **109/109 tests passing**.
  * **Field status (session 2026-11-10 23:44):** the selector seat runs (`selector seat OBSERVED running`) and answered with the focus once combat started; the summon still did not attack because the vanilla follow leash dragged it back (fixed above).
* **Player Orders & Input Subsystem (`InputHandler` & `ConsoleCommands`):**
  * `InputHandler` registered as sink on `RE::BSInputDeviceManager` capturing button events on keyboard and mouse.
  * **Crosshair Target Pick & Move-To-Point (`RE::CrosshairPickData`):**
    * Resolves alive target actors via `targetActor[0]` and `target[0]`.
    * Resolves exact 3D world collision point via `collisionPoint[0]` for point navigation.
  * **Default Hotkeys on Numpad (DirectX Scan Codes):**
    * `Numpad 1` (`0x4F`): **Focus Target** under crosshair with HUD confirmation.
    * `Numpad 2` (`0x50`): **Return** summons to player immediately.
    * `Numpad 3` (`0x51`): **Toggle Aggression** cycling `Passive` -> `Defensive` -> `Aggressive`.
    * `Numpad 4` (`0x4B`): **Move To Point** orders squad to navigate to crosshair position and hold guard.
    * `Numpad 5` (`0x4C`): **Wait / Guard** orders squad to hold ground.
    * `Numpad 6` (`0x4D`): **Follow** orders squad to resume follow formation.
  * **In-Game HUD Notifications:** top-left alerts via `RE::SendHUDMessage::ShowHUDMessage` for immediate player feedback.
  * **Console Command Processor:**
    * Fixed multi-token parsing: `g_scfParams` now supports 4 parameters with full `StringChunk` iteration.
    * Resilient prefix parsing: supports both `scf order` and `sfc order` typo alias.
    * Orders: `scf order <return | follow | guard | moveto [x y z] | passive | defensive | aggressive | focus <FormID>>`.
    * Comprehensive command help via `scf help` and `scf order help`.
* **FLICK Menu Integration (`FlickMenu`):**
  * Dynamic integration with `FUCK.dll` (v7 API) without hard ImGui linkage.
  * Accessible via in-game overlay hotkey (F7 by default) under the `Gameplay` category.
  * Real-time interactive keybinding for all squad orders (keyboard and mouse buttons).
  * Direct tuning of combat thresholds (follow distance, leash return, debounce time), Friendly Fire protections, and collision bypass with instant **Save to INI**.
* **Observability & Diagnostics:**
  * Pluggable functional callbacks capturing state changes, target scoring, registry events, command queues, scheduler updates, collision classifications, and squad slot claims with structured reasons.
  * In-game bridge forwarding events to `spdlog` in `Documents\\My Games\\Skyrim Special Edition\\SKSE\\SummonControlFramework.log`.
* **Automated Unit Tests (`scf_tests.exe`):**
  * Self-contained, zero-dependency test harness (`check.h`).
  * **125/125 test cases passing** (0 failures, execution time < 2 ms).
* **SKSE Plugin Infrastructure:**
  * Configured via `xmake.lua` targeting MSVC 2022 (C++23).
  * Multi-runtime support: Skyrim SE 1.5.97.0 (exact match) and Steam AE 1.6.1170 via Address Library (`commonlibsse-ng.plugin`).
  * `RuntimeGate` module logging runtime environment and validating executable version.
  * Dual-deployment active in `xmake.lua`:
    * `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\`
    * `D:\Games\stb\[RE2CE TEST] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\`
  * Mod active in `D:\Games\stb\[STB] Mod Organizer\profiles\SBORKA\modlist.txt` as `+Summon Control Framework`.

## In-Game Verified Functionality (Player Tested in MO2 / Skyrim SE 1.5.97)

* **Physical Body Passthrough Verified:** Player character freely passes through summoned creatures without physical body collision or doorway blocking. (2026-11-11: SCF had no engine hook for this at the time; the effect is most likely from the installed "Disable Follower Collision" mod.)
* **Cell Transition Resilience Verified:** Transitioning across load doors correctly preserves summons without crashes or broken registry state (`Active <-> Suspended` transition).
* **Live Summon Registration & Synchronization:** `UpdateHook` on `PlayerCharacter::Update` correctly polls `commandedActors` at 15 Hz and tracks active summons.
* **Automatic Garbage Collection Active:** Dispelled or killed summons cleanly transition with 3-second grace period for death/ash-pile fadeout before record purging.
* **Twin Summons In-Game Verified:** User verified in live gameplay with Twin Souls perk (`player.addperk 000581DE`) that 2 summons function concurrently, body passthrough works for both simultaneously, and both are tracked cleanly by Summon Control Framework.

## Implemented and Deployed to MO2: FF-01 (Friendly Fire Protection & Attack Filtering)

Specification reference: [Friendly_Fire_Protection_Addendum.md](file:///D:/projects/Summon_Control_Framework/Friendly_Fire_Protection_Addendum.md).

* **FF-Stage A (Relationship Classification & Matrix Policy):**
  * `src/FriendlyFire/RelationshipClassifier.h/.cpp`: classifies sources as `Player`, `PlayerAlly`, `PlayerOwnedSummon`, `AlliedSummon`, `Neutral`, `Hostile`, or `Unknown`.
  * Depth-guarded commanding actor traversal (`MaxCommandChainDepth = 4`) with cycle detection.
  * Fail-Safe Gate: `Unknown` and hostile sources are NEVER filtered, protecting combat integrity against true enemies.
  * 100% covered by 5 new unit tests in `tests/test_friendly_fire.cpp` (all 102/102 tests passing).
* **FF-Stage B & C (Damage Protection MVP & Precision Melee Pass-Through):**
  * `src/FriendlyFire/FriendlyFireSettings.h`: integrated with `Settings.h/.cpp` with full INI parsing, validation, and round-trip serialization.
  * `src/Integrations/PrecisionBackend.h/.cpp`: dynamically discovers `Precision.dll` (v2.0.4) and installs `PreHitCallback`. When the player swings through a registered summon, returns `bIgnoreHit = true`: weapon physically flies through without hitstop to strike enemies behind!
  * `src/FriendlyFire/HitHook.h/.cpp`: intercepts engine `ProcessHit` (ID 37673 at offset 0x3C0), nullifying damage, stagger, and pushback, dropping friendly hits before combat hostility loops can trigger.
* **FF-Stage D (True Projectile Pass-Through):**
  * `src/FriendlyFire/ProjectileHook.h/.cpp`: hooks `ArrowProjectile` (arrows & crossbow bolts) and `MissileProjectile` (spells) at `AddImpact` (vfunc 189) and `HandleHits` (vfunc 190).
  * Skips impact registration on friendly summons and compacts contact points, allowing projectiles to fly cleanly through summons into enemies behind.

## Active Playtesting Environment: [STB] Mod Organizer (Profile SBORKA)

Baseline clean testing in `[RE2CE TEST] Mod Organizer` is complete. The active testing environment has transitioned back to `D:\Games\stb\[STB] Mod Organizer` (Profile `SBORKA`), where Precision is installed and active.

1. **Melee pass-through test (with Precision):** Stand behind a summon with a sword/axe while an enemy is nearby. Swing at the enemy through the summon — sword physically slices through the summon (no hitstop, no damage) and strikes the enemy behind.
2. **Fallback test (without Precision):** If Precision is disabled, `HitHook` nullifies damage and hostility reaction, but the weapon swing stops on the summon's vanilla raycast hitbox.
3. **Projectile pass-through test:** Shoot an arrow, crossbow bolt, or spell missile through a summon — projectile passes cleanly through the summon and strikes the target behind.

## Known limitations

* Engine adapters (`ActorAdapter`, `CombatAdapter`) are currently decoupled; test execution uses synthetic `DecisionContext` snapshots until the `SummonRegistry` hook is active.
* Raycast / Line-of-sight checks currently depend on snapshot inputs and will be connected to engine raycasts in the adapter layer.
* `SummonMovement` is field-confirmed for `Follow` / `Return` / `Guard` / `MoveTo` (the keep-offset moves a summoned wolf, warp fires when stuck, `SetDontMove` holds it, the aim projection lands where the player aimed). `CombatGate` and the FocusTarget target pin are not yet re-tested after the call-site fix: the open questions are whether denying `Actor::StartCombat` is enough to stop the vanilla commanded-actor fighting, and whether the selector seat runs for summoned creatures (the first log line `[TargetPin] selector seat OBSERVED running` answers the latter).
* The `StartCombat` seat is a global hook: another plugin hooking the same function chains through the SKSE trampoline, but a mod that calls combat entry through a path this function does not cover could still start a fight for a summon. The native `kAttackingDisabled`/`kCastingDisabled` bits are the second layer for that case.
* Catch-up warp is same-cell only; cross-cell recall relies on the engine's vanilla summon snap (`fSummonedCreatureMaxFollowDist`, patched from `[Recovery] fCatchUpDistance`).
* A summon in a different cell than the player receives no movement orders (its AI process is suspended); the drift `MoveTo` destination is abandoned when the owner is more than 8000 units away.
* Focus Target is resolved from `targetActor`/`target` with an aim-cone fallback (25°, 2500 units); if both the crosshair data and the cone are empty, the diagnostic `[Crosshair]` line is the way to tell why.
* `SetDontMove` / the combat deny bits are actor state, not framework state: turning the framework off with `[General] bEnabled = false` (or disabling the plugin) does not release them. Use any other order (or reload the save) before disabling.
