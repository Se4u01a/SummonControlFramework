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
