# Testing Guide: Summon Control Framework (SCF)

Этот документ фиксирует статус проверок, тестовые наборы и инструкции для валидации как офлайн (Unit-тесты без Skyrim), так и в игре.

---

## 1. Automated Unit Tests (`scf_tests.exe`)

Все тесты выполняются за **~2 миллисекунды** автономным исполняемым файлом `build\windows\x64\releasedbg\scf_tests.exe`.
Текущий результат: **116/116 тестов успешно пройдено (0 failures)**.

### Запуск тестов
```powershell
xmake run scf_tests
```

### Suite A: FSM State Machine (`StateMachine`) — 8 tests
* `TestInitialStateAndFollow`: Проверяет инициализацию FSM в состоянии `Idle` и переход в `FollowOwner` при наличии владельца.
* `TestWaitGuardOrder`: Проверяет выполнение прямого приказа игрока `WaitGuard` и переход в состояние `Guard`.
* `TestPassiveModeIgnoresEnemies`: Проверяет, что в режиме `Passive` появление врага не приводит к выходу из следования (`FollowOwner`).
* `TestAggressiveModeEngagesTarget`: Проверяет обнаружение врага в режиме `Aggressive` и корректный цикл `AcquireTarget` -> `Approach` -> `Engage`.
* `TestTargetLostDebounce`: Проверяет гистерезис потери цели: FSM не бросает цель мгновенно при потере прямой видимости, удерживая бой до истечения таймаута 1.5 сек.
* `TestLeashBreakForcesReturn`: Проверяет «разрыв поводка»: удаление игрока дальше 2500 единиц принудительно прерывает бой и переводит существо в `Return`.
* `TestExplicitReturnOrder`: Проверяет, что прямой приказ `Return` прерывает любую фазу боя.
* `TestLogCallbackRecordsTransitions`: Проверяет наблюдаемость: все переходы состояний корректно фиксируются через `LogCallback`.

### Suite B: Target Selection (`TargetSelector`) — 7 tests
* `TestTargetSelectorPassive`: Проверяет полный отказ от выбора целей в режиме `Passive`.
* `TestTargetSelectorFriendlyFireGuard`: Гарантирует абсолютную защиту от дружественного огня: призыв никогда не выбирает владельца, себя или союзников отряда.
* `TestTargetSelectorAttackingOwnerPriority`: Проверяет приоритет угрозы: враг, бьющий игрока (+1000 очков), выбирается приоритетнее более близкого нейтрального врага.
* `TestTargetSelectorStickyTargetBonus`: Проверяет гистерезис цели (+400 очков): призыв не скачет между двумя одинаково близкими врагами.
* `TestTargetSelectorFocusTargetOverride`: Проверяет приказ `FocusTarget`: явно указанная игроком цель выбирается мгновенно в обход скоринга.
* `TestTargetSelectorDeadOrDisabledIgnored`: Проверяет отсечение недопустимых кандидатов: мертвые и отключенные акторы отфильтровываются.
* `TestTargetSelectorDefensiveRequiresThreat`: Проверяет режим `Defensive`: бой инициируется только при наличии доказанной угрозы игроку или призыву.

### Suite C: Squad Coordination (`SquadCoordinator`) — 5 tests
* `TestFormationOffsetsDistinct`: Проверяет генерацию непересекающихся веерных смещений отряда позади владельца с учетом его угла поворота (Yaw).
* `TestTargetDistributionAcrossMultipleEnemies`: Проверяет распределение целей: при наличии нескольких врагов существа отряда не сбиваются в кучу, а атакуют разных противников.
* `TestMeleeSlotLimitPreventsDogpiling`: Проверяет квотирование слотов ближнего боя (Anti-dogpiling): при исчерпании лимита (2) третий боец ближнего боя удерживает позицию.
* `TestRangedAndMeleeIndependentSlots`: Проверяет независимость лимитов: стрелки занимают дальние слоты (до 3), не блокируя бойцов ближнего боя.
* `TestSquadCoordinatorLogging`: Проверяет диагностическое логирование захвата и освобождения боевых слотов.

### Suite D: Registry & Lifecycle (`SummonRegistry`) — 6 tests
* `TestIdempotentRegistration`: Проверяет устойчивость к дублирующим событиям движка: повторная регистрация обновляет метаданные, не создавая клонов.
* `TestCellDetachSuspendsWithoutDeleting`: Проверяет двухфазный жизненный цикл: выгрузка ячейки (`Cell Detach`) переводит существо в `Suspended`, а повторная загрузка возвращает в `Active`.
* `TestReCastExceedingLimitDisplacesOldest`: Проверяет вытеснение при превышении лимита призывов владельца (вытесняется самый старый активный призыв с выдачей команды на dispel).
* `TestDeathAndDispelLifecycle`: Проверяет события смерти (`Dead`) и снятия эффекта (`Dispatched`), а также сборку мусора `ClearStaleRecords()`.
* `TestResetClearsAll`: Проверяет очистку реестра при перезагрузке сохранения или начале новой игры.
* `TestRegistryLogging`: Проверяет логирование всех операций жизненного цикла.

### Suite E: Command Queue & Priority (`CommandQueue`) — 6 tests
* `TestPriorityArbitrationOrder`: Проверяет строгую иерархию обработки: `Lifecycle` (0) -> `PlayerDirect` (1) -> `ModeChange` (2) -> `Tactical` (3).
* `TestCommandCoalescing`: Проверяет слияние команд: быстрый спам хоткеев игрока перезаписывает аргументы in-place, защищая от переполнения очереди.
* `TestTTLExpiration`: Проверяет автоматическое аннулирование устаревших команд по истечении Time-to-Live (5.0 сек).
* `TestDrainForSummonIncludesBroadcast`: Проверяет выборку команд: получение приказов конкретным существом включает как персональные, так и широковещательные приказы отряду.
* `TestApplyToModelWithFSM`: Проверяет интеграцию: применение приказов из очереди непосредственно к `SummonModel` и `StateMachine`.
* `TestThreadSafeConcurrentEnqueue`: Стресс-тест многопоточности: 8 параллельных потоков по 100 команд без состояния гонки и повреждения памяти.

### Suite F: Update Scheduler & Time-Slicing (`UpdateScheduler`) — 6 tests
* `TestIntervalCalculationAndClamping`: Проверяет корректность расчета тика (15 Гц = 66.7 мс) и клампинг частоты в безопасные границы [1, 60] Гц.
* `TestPeriodicTickAccurate`: Проверяет защиту от дрейфа времени: сохранение остатка дельты между кадрами.
* `TestImmediateTickOverride`: Проверяет немедленное пробуждение планировщика при получении экстренных команд игрока.
* `TestTimeSlicingInterleavedCadence`: Проверяет алгоритм чередования (Round-robin): расчеты для существ отряда распределяются по разным тикам.
* `TestResetZeroesState`: Проверяет сброс состояния планировщика при смене игровых сессий.
* `TestSchedulerLogging`: Проверяет диагностические логи изменения частоты, внеочередных тиков и сброса.

### Suite G: Configuration & Settings (`Settings`) — 6 tests
* `settings_default_values`: Проверяет соответствие всех параметров конфигурации спецификации мода по умолчанию.
* `settings_load_from_string_prefixed`: Проверяет парсинг стандартных INI-ключей Skyrim (`bEnabled`, `sLogLevel`, `uMaxSummonsPerOwner`, `bEnableCollisionBypass` и др.).
* `settings_load_from_string_unprefixed_and_comments`: Проверяет парсинг строчных ключей без префиксов и корректное удаление комментариев (`;` и `#`).
* `settings_validation_and_clamping`: Проверяет защиту от некорректных значений в INI (клампинг лимитов [1, 32], частоты [1, 60], дистанций поводка).
* `settings_serialization_roundtrip`: Проверяет сериализацию в INI-строку и обратный парсинг без потери данных.
* `settings_file_save_and_load`: Проверяет сохранение, загрузку с диска и обработку отсутствующих файлов.

### Suite H: Console Diagnostics & Context (`ConsoleHandler` & `FrameworkContext`) — 6 tests
* `console_command_help`: Проверяет справочные подсказки команды `scf help`.
* `console_command_status`: Проверяет отчет `scf status` (состояние рантайма, частота планировщика, режим time-slicing, флаги коллизий, число команд в очереди).
* `console_command_registry_empty_and_populated`: Проверяет форматированный вывод реестра `scf registry` с выводом ID, владельца, состояния и времени жизни.
* `console_command_reload`: Проверяет горячую перезагрузку INI через консоль `scf reload`.
* `console_command_unknown`: Проверяет вывод подсказок при неверных подкомандах.
* `framework_context_reset`: Проверяет `FrameworkContext::ResetRuntimeState()` с одновременным сбросом всех подсистем.

### Suite I: Engine Event Dispatching (`EventDispatcher`) — 4 tests
* `event_dispatcher_active_effect_apply_and_remove`: Проверяет регистрацию в `Active` при наложении эффекта призыва, отсечение селф-баффов (`caster == target`) и снятие эффекта (`Dispatched`).
* `event_dispatcher_recast_displacement`: Проверяет вызов коллбэка вытеснения старейшего призыва при превышении лимита владельца.
* `event_dispatcher_death_event`: Проверяет фиксацию смерти актора (`Dead`) и игнорирование нелетальных событий.
* `event_dispatcher_cell_attach_detach_lifecycle`: Проверяет переход `Active <-> Suspended` при смене ячеек.

### Suite J: Collision Pair Classification (`CollisionPairClassifier`) — 7 tests
* `collision_player_own_summon_passthrough`: Проверяет свободное прохождение игрока сквозь собственного призванного актора (`Passthrough`), устраняя блокировку проходов.
* `collision_player_other_owner_summon_collide`: Проверяет стандартное столкновение игрока с призывами других персонажей (`Default`).
* `collision_allied_summons_passthrough`: Проверяет сквозное прохождение двух призывов одного владельца при включенном `bEnableSummonToSummonBypass`.
* `collision_summon_and_enemy_collide`: Гарантирует сохранение физических столкновений и хитбоксов в бою против врагов (`Default`).
* `collision_non_summons_default`: Гарантирует, что сущности без флага призыва никогда не модифицируются классификатором (защита статиков, стен и полов).
* `collision_global_bypass_disabled`: Проверяет мгновенный возврат к ванильному поведению при выключении `bEnableCollisionBypass = false` (Safety Gate).
* `collision_classifier_logging`: Проверяет корректность вызовов диагностического логгера при оценке пар коллизий.

### Suite L: Stealth Coordination (`StealthContext` & AI) — 7 tests
* `stealth_undetected_defensive_suppresses_unaware_enemy`: Проверяет подавление атаки в режиме Defensive при скрытности игрока (`playerSneaking = true`, `playerDetected = false`) против ненастороженного врага.
* `stealth_detected_engages_threat`: Проверяет, что при срыве скрытности (`playerDetected = true`) сумон немедленно вступает в бой.
* `stealth_undetected_enemy_attacks_engages`: Проверяет реакцию на прямую атаку: враг, нанесший удар по игроку или сумону, атакуется немедленно даже при скрытности игрока.
* `stealth_undetected_explicit_focus_target_overrides`: Проверяет принудительный приказ: указание FocusTarget принудительно переводит сумона в атаку без срыва стелса игрока.
* `stealth_undetected_aggressive_mode_engages`: Проверяет режим Aggressive: игнорирует скрытность и атакует любого врага в радиусе обнаружения.
* `stealth_state_machine_holds_follow_when_stealthing`: Проверяет FSM: сумон остается в состоянии `FollowOwner` при скрытном перемещении мимо врагов.
* `stealth_state_machine_engages_on_explicit_order`: Проверяет FSM: приказ FocusTarget переводит сумона в `AcquireTarget` -> `Approach` -> `Engage`.

### Suite K: Friendly Fire & Attack Filtering (`FriendlyFireSettings` & `RelationshipClassifier`) — 5 tests
* `friendly_fire_settings_default_values`: Проверяет соответствие параметров FriendlyFire спецификации по умолчанию (`bEnableSummonProtection = true`, `bMeleePassThrough = true`, `bProjectilePassThrough = true` и др.).
* `friendly_fire_settings_parse_ini_and_serialization`: Проверяет парсинг секции `[FriendlyFire]` в INI и round-trip сериализацию без потери данных.
* `relationship_classifier_source_classification`: Проверяет классификацию атакующих: игрок -> `Player`, спутник -> `PlayerAlly`, сумон игрока -> `PlayerOwnedSummon`, сумон спутника -> `AlliedSummon`, враг -> `Hostile`, неизвестный -> `Unknown` с защитой от циклов цепочки командования (глубина 4).
* `relationship_classifier_target_protection`: Проверяет селектор защиты целей в зависимости от настроек (`bProtectPlayerOwnedSummons`, `bProtectAlliedSummons`).
* `relationship_classifier_evaluate_attack_matrix`: Проверяет матрицу решений: защита атак ближнего боя, стрел, магии, криков от игрока и союзников, при безусловном пропуске атак от врагов и нейтральных сущностей.

### Suite M: Formation Geometry, Facing, Movement Intent & Combat Policy (`Formation`, `MovementIntent`, `CombatPolicy`) — 29 tests
* `TestFormationSlotOffsetsSitOnRadius`: Проверяет, что смещение любого слота строя лежит ровно на заданном радиусе (50..1000) и не имеет вертикального сдвига.
* `TestFormationSlotsAreDistinct`: Проверяет непересечение слотов отряда: для 2..6 существ позиции разнесены более чем на 50 единиц.
* `TestFormationDefaultSlotIsBehindOwner`: Проверяет, что одиночный сумон и замыкающий тройки стоят позади владельца.
* `TestFormationTwoSlotsAreMirrored`: Проверяет зеркальность двух слотов (левый/правый) относительно оси владельца.
* `TestOwnerLocalWorldOffsetRoundTrip`: Проверяет обратимость перевода смещения из body space владельца в мир и обратно на нескольких углах поворота.
* `TestOwnerWorldPointRoundTrip`: Проверяет обратимость перевода абсолютной мировой точки в offset владельца и обратно.
* `TestOwnerLocalWorldOffsetMatchesSquadConvention`: Фиксирует соглашение `SquadCoordinator` (мир = yaw владельца + пеленг слота, sin -> X, cos -> Y), чтобы общий модуль не изменил поведение строя.
* `TestFacingOffsetDegreesCardinalDirections`: Проверяет угол поворота на цель для четырёх направлений (вперёд 0, вправо +90, назад 180, влево -90) — это значение уходит в `afOffsetAngleZ`, чтобы сумон не «ходил задом».
* `TestFacingOffsetDegreesIsRelativeToOwnerHeading`: Проверяет, что угол считается относительно текущего heading владельца (при yaw 90° цель на восток = 0°, на север = -90°).
* `TestFacingOffsetDegreesDegenerateIsZero`: Проверяет вырожденный случай (цель совпала с позицией актора) — угол 0, без «дрожания» ориентации.
* `TestMovementIntentGuardHoldsPositionEvenInCombat`: Проверяет, что приказ `WaitGuard` даёт `HoldPosition` с блокировкой транслокации даже в бою.
* `TestMovementIntentAttackOverridesEveryMovementOrder`: Проверяет, что явный `FocusTarget` (`Attack`) отменяет любой приказ движения: примитивы не применяются, локомоция у боевого AI.
* `TestMovementIntentReturnRunsToOwner`: Проверяет `ReturnToOwner` с нулевым смещением, корректными радиусами (follow < catchUp) и разворотом лицом к игроку.
* `TestMovementIntentFollowUsesFormationSlot`: Проверяет `FollowSlot` с дистанцией строя `fFollowMinDistance` и нулевым facing на самом слоте.
* `TestMovementIntentFollowFacesTravelWhenAwayFromSlot`: Проверяет, что смещённый от слота сумон смотрит по направлению к слоту, а не туда же, куда игрок.
* `TestMovementIntentFollowYieldsToCombat`: Проверяет, что в бою `Follow` не навязывает движение (локомоция остаётся боевому AI), а устаревшая цель модели не блокирует приказ.
* `TestMovementIntentAttackFallsBackToFollowWhenCombatForbidden`: Проверяет, что при запрещённом бое (`Passive`) приказ `Attack` не оставляет суммона без управления движением — он продолжает следовать.
* `TestMovementIntentFollowKeepsSlotWhenCombatForbidden`: Проверяет, что при `Passive`/`Return`/`MoveTo` формация выигрывает даже если движок ещё сообщает бой (иначе суммон стоит, пока его утаскивает AI).
* `TestCombatPolicyPassiveNeverFights`: Проверяет, что `Passive` запрещает бой при любом приказе (Follow/Guard/Return/MoveTo/Attack) и включает нативный запрет атак/кастов.
* `TestCombatPolicyReturnForbidsCombatUntilArrival`: Проверяет, что `Return` запрещает бой до прибытия к игроку.
* `TestCombatPolicyMoveToForbidsCombatWhileMoving`: Проверяет, что `MoveTo` с точкой запрещает бой, а без точки (откат в Follow) — разрешает.
* `TestCombatPolicyAttackRestrictsToFocusedTarget`: Проверяет, что `Attack` разрешает бой ровно с назначенной целью, а без живой цели запрещает.
* `TestCombatPolicyFollowAndGuardAllowCombat`: Проверяет, что `Follow`/`WaitGuard`/`Aggressive` разрешают бой (управление целью — за движком/агрессией).
* `TestMovementIntentMoveToKeepsWorldDestination`: Ключевой тест: при смещении и повороте владельца пересчитанный offset всё равно указывает на ту же мировую точку.
* `TestMovementIntentMoveToIgnoresStaleDestination`: Проверяет отмену `MoveTo` с точкой дальше 8000 единиц (осталась в другой ячейке после телепорта) — иначе получались абсурдные offset-цели из полевого лога.
* `TestMovementIntentMoveToWithoutDestinationFallsBackToFollow`: Проверяет безопасный откат `MoveTo` без сохранённой точки в обычное следование.
* `TestSummonFollowPackageShape`: пакет следования призыва опознаётся по форме (FormID `0xFF…`, тип 18, два float-входа) — так он выглядел в поле без `templateParent`; пакеты плагинов, другие типы и другое число входов не опознаются.
* `TestFollowLeashReleasedOnlyForAwayOrders`: поводок ванильного `FollowTo` отпускается только для `MoveTo` с точкой, `Attack` и `WaitGuard`; `Follow`/`Return`/`MoveTo` без точки его сохраняют.
* `TestMoveToArrivalRadiusIsHorizontal`: Проверяет горизонтальный критерий прибытия (перепад высоты не удерживает приказ в движении).

### Suite O: Pass-Through (`PassThrough`) — 5 tests
Файл `tests/test_pass_through.cpp`, правило `core::ShouldPassThrough` для seat'ов обхода в движении и фильтра коллизий тел.
* `TestPassThroughSameOwnerSummons`: два призыва игрока игнорируют друг друга (в обе стороны).
* `TestPassThroughSummonAndOwner`: призыв и его владелец игнорируют друг друга.
* `TestPassThroughDifferentOwnersCollide`: призыв игрока и призыв/сам NPC-маг продолжают обходить и толкать друг друга.
* `TestPassThroughUnknownOrSelf`: неизвестный SCF актор и пара «сам с собой» — ванильное поведение.
* `TestPassThroughSettingsGates`: `bEnableCollisionBypass=false` выключает всё, `bEnableSummonToSummonBypass=false` — только пары призывов.

### Suite N: Target Pin (`TargetPin`) — 7 tests
Файл `tests/test_target_pin.cpp`, правило `core::ChooseSelectorAnswer`, которым seat селектора `CombatController` заменяет выбор цели движка на `FocusTarget`.
* `TestTargetPinReplacesEnginePick`: фокус — удерживаемая цель группы, движок выбрал другого → ответ = фокус.
* `TestTargetPinWithoutPinKeepsEnginePick`: без фокуса выбор движка не меняется.
* `TestTargetPinNeverRevivesAFight`: движок не держит цели (ответ 0) → 0, пин не начинает и не оживляет бой.
* `TestTargetPinAbsentFromGroupKeepsEnginePick`: фокус ещё не цель группы → выбор движка.
* `TestTargetPinLostTargetKeepsEnginePick`: фокус помечен `kTargetLost` → выбор движка.
* `TestTargetPinDeadTargetKeepsEnginePick`: фокус мёртв/выгружен → выбор движка.
* `TestTargetPinAgreeingPickIsUnchanged`: движок уже выбрал фокус → без изменений.

---

## 2. Static / offline (passing)

* **Compiler:** MSVC 14.44 (Visual Studio 2022 Build Tools) с `/std:c++latest` (C++23).
* **Warning level:** `/W4` (`allextra`), `/permissive-`, `/Zc:preprocessor`. Чистая сборка с нулевым количеством ворнингов в коде мода.
* **Precompiled Header (PCH):** [src/PCH.h](file:///D:/projects/Summon_Control_Framework/src/PCH.h) с предкомпиляцией `RE/Skyrim.h`, `SKSE/SKSE.h` и `spdlog`.
* **Multi-runtime Configuration:**
  * CommonLibSSE-NG собрана с `ENABLE_SKYRIM_SE=1` и `ENABLE_SKYRIM_AE=1`.
  * Архитектура `windows|x64`.
  * Address Library режим (`SKSE::VersionIndependence::AddressLibrary`).
* **Развертывание бинарников (Dual-Deployment):**
  * Основная сборка: `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\SummonControlFramework.dll` (~647 KB).
  * Тестовая сборка: `D:\Games\stb\[RE2CE TEST] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\SummonControlFramework.dll` (~647 KB).
  * PDB: `SummonControlFramework.pdb` (полные отладочные символы развернуты в обе сборки).
  * INI: `SummonControlFramework.ini` (конфигурация мода со всеми секциями, включая `[FriendlyFire]`).
  * Профиль MO2: активирован в `D:\Games\stb\[STB] Mod Organizer\profiles\SBORKA\modlist.txt` как `+Summon Control Framework`.

---

## 3. In game (traces in `SummonControlFramework.log`)

### Verified
| Component | Evidence |
|---|---|
| Plugin build output | `SummonControlFramework.dll` generated (~647 KB, with `.pdb`) |
| Core unit tests | 116/116 passing via `scf_tests.exe` (< 2 ms) |
| MO2 deployment | Deployed to `mods\Summon Control Framework` in both MO2 instances and enabled |

### Verified In Gameplay (Skyrim SE 1.5.97.0 via MO2)
| Feature | In-Game Observation & Evidence | Status |
|---|---|:---:|
| Plugin load & Gate | SKSE loads `SummonControlFramework.dll` cleanly, `RuntimeGate` validates SE 1.5.97.0 | PASS |
| Console command `scf` | Executing `scf status` and `scf registry` prints tracked summons and FSM data | PASS |
| Summon Registration | `UpdateHook` on `PlayerCharacter::Update` automatically synchronizes `commandedActors` | PASS |
| Body Passthrough | Player character walks through summoned familiar without doorway clipping or pushback | PASS |
| Cell Transitions | Summons transition between locations without memory corruption (`Active <-> Suspended`) | PASS |
| Auto Garbage Collection | Dead / dispelled summons cleaned up with 3s grace period for ash-pile/death fadeout | PASS |
| Multi-Summon Limit | User verified in gameplay with Twin Souls perk (`player.addperk 000581DE`) that 2 summons function concurrently, body passthrough works for both, and both are tracked cleanly | PASS |

---

## 4. Acceptance Criteria: Friendly Fire Protection & Attack Filtering (FF-01)

Specification reference: [Friendly_Fire_Protection_Addendum.md](file:///D:/projects/Summon_Control_Framework/Friendly_Fire_Protection_Addendum.md).

### Suite FF-1: Защита от урона (Damage Protection)
* [ ] **FF-1.1 (Player Melee):** Обычные и силовые атаки игрока оружием ближнего боя не наносят урон зарегистрированным призывам игрока.
* [ ] **FF-1.2 (Player Sweep):** Размашистые атаки (двуручное оружие, перк Sweep) не повреждают призыва при попадании в зону поражения.
* [ ] **FF-1.3 (Player Projectiles):** Стрелы и болты арбалета игрока не наносят урон при попадании в защищенного призыва.
* [ ] **FF-1.4 (Player Allies):** Атаки спутников и союзных NPC не наносят урон призывам игрока при `bFilterPlayerAllies = true`.
* [ ] **FF-1.5 (Summon-to-Summon):** Призывы одного владельца или союзной цепочки не наносят взаимный урон в бою.
* [ ] **FF-1.6 (Hostile Integrity):** Враждебные NPC и вражеские призывы продолжают наносить полный стандартный урон.
* [ ] **FF-1.7 (Beneficial Magic):** Заклинания лечения, благословения и баффы не блокируются защитным фильтром.
* [ ] **FF-1.8 (Magic Protection):** Прямые враждебные заклинания игрока, концентрация (Flames/Sparks), взрывы AoE, крики и зачарования оружия не наносят урон при включенных флагах `bBlock*Damage`.

### Suite FF-2: Физическое прохождение атак насквозь (Attack Pass-Through)
* [ ] **FF-2.1 (Precision Melee Pass-Through):** При установленном моде Precision атака оружием сквозь призыва игрока возвращает `bIgnoreHit = true`: оружие без задержки и хитстопа пролетает сквозь призыва и разрубает врага позади него.
* [ ] **FF-2.2 (Sweeping Multi-Target):** Размашистый удар проходит сквозь призыва и успешно поражает нескольких врагов в радиусе замаха.
* [ ] **FF-2.3 (Arrow Pass-Through):** Стрела игрока пролетает сквозь призыва игрока, не застревая в его теле, и поражает противника за ним.
* [ ] **FF-2.4 (Bolt Pass-Through):** Болт арбалета пролетает сквозь тело союзного призыва во врага.
* [ ] **FF-2.5 (Magic Projectile Pass-Through):** Магические снаряды (Огненная стрела, Ледяной шип) не детонируют на союзном призыве при включенном режиме сквозного полета.
* [ ] **FF-2.6 (AoE Exclusion):** Взрыв шара огня или огненной ловушки не повреждает союзных призывов в эпицентре, но наносит полный урон всем врагам в радиусе.

### Suite FF-3: Регрессия, стабильность и совместимость
* [ ] **FF-3.1 (Global Killswitch):** При `bEnableSummonProtection = false` игра на 100% возвращается к стандартному ванильному расчету урона и попаданий.
* [ ] **FF-3.2 (Independent Pass-Through):** Отключение `bMeleePassThrough` оставляет `Damage Protection` активным (урон блокируется, но меч останавливается).
* [ ] **FF-3.3 (Enemy Combat Loop):** Атаки игрока по истинным врагам (`Hostile`) никогда ошибочно не фильтруются.
* [ ] **FF-3.4 (No Infinite Event Recursion):** Подавление попаданий не вызывает каскадных циклов `TESHitEvent` или утечек памяти.
* [ ] **FF-3.5 (Precision Compatibility):** Отсутствие конфликтов и взаимных блокировок с `Precision.dll` и сопутствующими боевыми модами (MCO/True Directional Movement).

---

## 5. Внешние зависимости и окружение тестирования (Dependency & Fallback Matrix)

Для корректной работы всех подсистем и проведения полного цикла тестирования используется следующая матрица зависимостей:

### 5.1. Обязательные зависимости рантайма (Hard Requirements)
Без этих компонентов `SummonControlFramework.dll` не будет загружен SKSE:
1. **Skyrim Special Edition (1.5.97.0)** или **Skyrim Anniversary Edition (1.6.x)**.
2. **SKSE64** (v2.0.20 для SE 1.5.97, v2.2.6+ для AE).
3. **Address Library for SKSE Plugins** (`version-1-5-97-0.bin`).

### 5.2. Опциональные зависимости и архитектурная деградация (Soft Requirements & Fallback)

| Компонент / Мод | Затрагиваемый функционал | Поведение при ОТСУТСТВИИ зависимости (Fallback) | Поведение при НАЛИЧИИ зависимости |
|---|---|---|---|
| **[Precision](https://www.nexusmods.com/skyrimspecialedition/mods/72347) (v2.0.4+)** | **Melee Pass-Through** (прохождение холодного оружия) | `PrecisionBackend` деактивируется. Работает `HitHook` на функции `ProcessHit`: урон обнуляется (`damage = 0`), агро/stagger подавляются, но ванильный хитбокс рейкаста вызывает хитстоп/остановку замаха на теле сумона. | Активируется `PreHitCallback` с `bIgnoreHit = true`. Оружие физически со свистом пролетает сквозь тело сумона без задержек и хитстопа, поражая противника позади него. |
| **Behavior Data Injector** | Интеграция Precision | Precision не может перехватывать события кадров анимации атаки. | Precision полноценно перехватывает траектории коллизий оружия. |
| **Перк «Парные души» (`000581DE`)** | Мультипризыв (2+ существ) | Ванильный движок имеет лимит `commandedActors = 1`. При касте 2-го сумона 1-й мгновенно рассеивается. | Игрок может одновременно призывать 2 и более существ. SCF регистрирует и координирует отряд целиком. |
| **SSE Display Tweaks** | Режим окна без рамки и физика | Эксклюзивный полноэкранный режим с риском зависания при Alt-Tab; при FPS > 60 физика Havok может сбоить. | Настоящее окно без рамки (Borderless Fullscreen), мгновенный Alt-Tab, адаптивный физический тик. |
| **Console Font Fix & ConsolePlusPlus** | Диагностика через консоль | При русской раскладке консоль выводит квадраты `□□□`, невозможно вставить текст через Ctrl+V. | Корректный ввод на русском/английском, поддержка вставки команд через Ctrl+V, история команд по стрелкам. |

### 5.3. Окружения тестирования
* **Основная тестовая сборка:** `D:\Games\stb\[STB] Mod Organizer` (профиль `SBORKA`). Включает Precision, боевые моды, настроенную физику и графику. Используется для тестов сквозного удара холодным оружием (Melee Pass-Through) и комплексных боёв.
* **Чистый тестовый профиль:** `D:\Games\stb\[RE2CE TEST] Mod Organizer` (профиль `RE2CE`). Минимальный набор модов без посторонних скриптов. Использовался для верификации базового прохождения акторов, смены локаций и перка парных призывов.
* **Автоматическое развертывание:** `xmake.lua` собирает DLL и автоматически копирует её в обе сборки MO2.

---

## 6. Приемочные тесты: Приказы игрока, Хоткеи, FLICK Menu и Скрытность (Stage 3, 5 & 8)

### 6.1. Хоткеи управления отрядом на цифровом блоке (Numpad)
* **[ ] TEST-ORDER-1 (Focus Target - Numpad 1):** При наведении перекрестия на враждебного актора и нажатии `Numpad 1` (`0x4F`) появляется сообщение `"SCF: Target Focused -> <Имя>"`, все сумоны фокусируют атаку на выбранной цели.
* **[ ] TEST-ORDER-2 (Return - Numpad 2):** Нажатие `Numpad 2` (`0x50`) выводит `"SCF: Order -> Return to Owner"`, все сумоны немедленно прекращают бой и бегут к игроку.
* **[ ] TEST-ORDER-3 (Toggle Aggression - Numpad 3):** Нажатие `Numpad 3` (`0x51`) циклически переключает режимы: `Passive` -> `Defensive` -> `Aggressive` с выводом сообщения на HUD.
* **[ ] TEST-ORDER-4 (Move To Point - Numpad 4):** Нажатие `Numpad 4` (`0x4B`) направляет сумонов в точку поверхности под перекрестием прицела (`"SCF: Move To Point (X, Y, Z)"`). Достигнув точки, сумоны переходят в режим охраны позиции (`Guard`).
* **[ ] TEST-ORDER-5 (Wait / Guard - Numpad 5):** Нажатие `Numpad 5` (`0x4C`) приказывает отряду удерживать текущую позицию (`"SCF: Order -> Guard Position"`).
* **[ ] TEST-ORDER-6 (Follow - Numpad 6):** Нажатие `Numpad 6` (`0x4D`) приказывает отряду следовать за игроком в боевом строю (`"SCF: Order -> Follow Owner"`).
* **[ ] TEST-ORDER-7 (Movement log evidence):** В `SummonControlFramework.log` после нажатия хоткея должны появляться строки `[Movement] Summon 0x... <Mode> keep-offset (...)`, `[Movement] Summon 0x... SetDontMove(true|false)` и (для `Return`) `[Movement] Summon 0x... reached owner (...) -> Follow`. Отсутствие строки `keep-offset` означает, что приказ не дошёл до движка.
* **[ ] TEST-ORDER-8 (Guard in place):** После `Numpad 5` сумон стоит на месте, не бежит за игроком даже когда игрок уходит; при этом он должен поворачиваться и наносить удары по подошедшему врагу (проверка того, что `SetDontMove(true)` не блокирует атаки). Нажатие `Numpad 6` снимает блокировку.
* **[ ] TEST-ORDER-9 (Catch-up warp):** Уйти от отряда дальше `fCatchUpDistance` (3500) и нажать `Numpad 2`: в логе должно появиться `catch-up warp applied (beyond catch-up distance ...)`, а сумон — оказаться за спиной игрока. Внутри одной ячейки.
* **[ ] TEST-ORDER-10 (No backwards walking):** Сумон должен бежать/идти головой вперёд при любом приказе (Follow из позиции перед игроком, Return, MoveTo с точкой позади/сбоку). В логе у `keep-offset` появляется поле `facing=...`, отличное от 0, когда цель сбоку/позади.
* **[ ] TEST-ORDER-11 (Focus overrides guard / move-to):** Поставить отряд в `Guard` (Numpad 5) или отправить `MoveTo` (Numpad 4), затем навестись на врага и нажать `Numpad 1`. В логе: `[Movement] ... Attack` не появляется как keep-offset (приказ `Attack` даёт `None`), но должен появиться `SetDontMove(false)` (если был guard), сумон бросается на цель; при смерти цели — `attack order finished (...) -> Follow`.
* **[ ] TEST-ORDER-12 (MoveTo aim sources):** Нажатие `Numpad 4` при прицеле на землю пишет в лог `MoveTo destination from aim projection`, при прицеле на актора/ссылку — `from crosshair reference`, при попадании пика в объект — `from crosshair collision point`. Точка назначения при этом совпадает с тем, куда смотрит прицел.
* **[ ] TEST-ORDER-13 (Crosshair diagnostics):** При промахе Focus (надпись `No target under crosshair`) в логе появляется строка `[Crosshair] FocusTarget: target=0x... targetActor=0x... ref=0x... point=(... , ... , ...)` — по ней видно, какое из полей прицела пустое.
* **[ ] TEST-ORDER-16 (M0 seat):** при загрузке в логе `[Leash] FollowTo creation seat installed at 0x...`; при новом призыве — `FollowTo package 0x... created for 0x...`; при `Numpad 4` — `follow leash RELEASED ... (recognised by created|shape)`. Строка `is not its follow package` с числом записей — пришлите целиком.
* **[ ] TEST-ORDER-15 (MoveTo without the follow pull):** `Numpad 4` в точку в 700–2000 ед.: суммон бежит до точки без остановок и разворотов к игроку, даже если игрок идёт в другую сторону; в логе `[Leash] ... follow leash RELEASED ... min 200 -> 100000` и затем `arrived at MoveTo point ... -> Guard`. После `Numpad 6` — `follow leash restored`.
* **[ ] TEST-ORDER-14 (Stale MoveTo cancelled):** Отправить отряд в `MoveTo`, затем быстро сменить ячейку (дверь/быстрое перемещение/`coc`): в логе должно появиться `MoveTo destination is ... units from the owner (limit 8000); abandoned -> Follow`, а сумон не должен бежать к старой точке.

### 6.2. Внутриигровое меню FLICK Menu (FUCK.dll — клавиша F7)
* **[ ] TEST-FLICK-1 (Меню настроек):** Нажать `F7` для открытия оверлея FLICK. В категории `Gameplay` выбрать вкладку **Summon Control Framework**.
* **[ ] TEST-FLICK-2 (Интерактивный ребинд):** Кликнуть на поле любого хоткея (отобразится `Press any key...`), нажать желаемую клавишу клавиатуры или кнопку мыши. Клавиша должна мгновенно забиндиться и отобразить новое имя.
* **[ ] TEST-FLICK-3 (Сохранение в INI):** Нажать кнопку `Save to INI`. Проверить, что настройки записались в `Data/SKSE/Plugins/SummonControlFramework.ini`.

### 6.3. Консольные приказы (`scf order` и алиас `sfc order`)
* `scf order return` — приказ отряду вернуться к игроку.
* `scf order follow` — возобновить следование за игроком.
* `scf order guard` (или `wait`, `stay`) — занять позицию охраны.
* `scf order moveto [x y z]` — переместить отряд в 3D-точку или точку под перекрестием.
* `scf order passive` / `defensive` / `aggressive` — переключение режима агрессии.
* `scf order focus <FormID>` — указать цель по FormID (шестнадцатеричный, например `0001234F`).
* `sfc order return` — проверка отказоустойчивости алиаса `sfc`.
* `scf help` / `scf order help` — вывод развернутого руководства по всем приказам.

### 6.4. Координация скрытности (Stealth Coordination)
* **[ ] TEST-STEALTH-1 (Sneak Hold Fire):** Игрок приседает в скрытность (`Ctrl`), сумон в режиме `Defensive`. При приближении к врагу (индикатор скрытности закрыт `[   ]`) сумон спокойно следует за игроком и **не агрится** на спокойного врага.
* **[ ] TEST-STEALTH-2 (Sneak Detected Engage):** Если враг замечает игрока (индикатор открывается `[ ОБНАРУЖЕНО ]`), сумон немедленно бросается в бой.
* **[ ] TEST-STEALTH-3 (Sneak Focus Target):** Находясь в скрытности, игрок нажимает `Numpad 1` на врага: сумон бесшумно срывается вперед и атакует указанную цель.

### 6.5. Полный контроль боя (`CombatGate`)

Проверка в логе: при загрузке должна быть строка `[CombatGate] StartCombat denial seat installed on 10 site(s) (1 tail call(s)) of 0x...; function entry untouched.`. Если вместо неё `did not resolve`/`no direct call sites`/`disabled` — остаётся только второй слой (биты + StopCombat), `Passive`/`Return` всё равно работают.

* **[ ] TEST-COMBAT-1 (Passive absolute):** Переключить режим на `Passive` (Numpad 3 до `SCF: Aggression Mode -> Passive`). Суммон не атакует ни при каких обстоятельствах: не входит в бой, не машет, не кастует, даже если враг бьёт его первым. В логе: `[CombatGate] attacks/casts disabled=true` и, при попытках движка, `StartCombat refused ... (combat forbidden)`.
* **[ ] TEST-COMBAT-2 (Passive blocks Focus):** Нажать `Numpad 1` в `Passive`: HUD `SCF: Passive mode - attack suppressed`, приказа нет, суммон продолжает следовать. (Приказ `FocusTarget` не должен доходить до движка.)
* **[ ] TEST-COMBAT-3 (Return exits combat):** В бою нажать `Numpad 2`: суммон сразу выходит из боя (`[CombatGate] Summon ... combat broken by policy`), бежит к игроку, **не вступая** в новые драки по дороге; только добежав (в логе `reached owner (...) -> Follow`) снова может воевать.
* **[ ] TEST-COMBAT-4 (Focus engages):** Навестись на врага и нажать `Numpad 1`: в логе `StartCombat(0x...) -> true (dist ..., hostile ..., inCombat yes)`, суммон подходит (`AttackApproach keep-offset ...`) и атакует. Если `StartCombat -> false` — пришлите строку целиком.
* **[ ] TEST-COMBAT-5 (Only the focused target):** После приказа `Numpad 1` суммон не переключается на других врагов, рядом стоящих: `StartCombat refused ... (focus-target-only)` для чужих целей.
* **[ ] TEST-COMBAT-6 (Attack ends):** Когда цель умирает, приказ завершается сам: `attack order finished (target gone) -> Follow`, суммон возвращается в строй.
* **[ ] TEST-PASS-1 (Seats installed and running):** при загрузке — две строки `[PassThrough] ... hooked`; при движении отряда — `movement avoidance seat OBSERVED running` и `collision filter seat OBSERVED a CharController pair`; во время движения раз в 10 с — `last 10 s: N avoidance checks and M body collisions skipped`. Нули при плотном строе означают, что seat не срабатывает — пришлите лог.
* **[ ] TEST-PASS-2 (Run through each other):** 3–4 волка, `Return` (Numpad 2) или `MoveTo` (Numpad 4) так, чтобы путь одного проходил через другого: призывы бегут по прямой сквозь друг друга, не огибают, не тормозят и не толкаются.
* **[ ] TEST-PASS-3 (Enemies unchanged):** призывы по-прежнему обходят и упираются во врагов и посторонних NPC; бой и удары по врагам не изменились.
* **[ ] TEST-COMBAT-12 (Focus on a far target):** Навестись на врага в 1500–2500 ед. и нажать `Numpad 1`: в логе `[Leash] ... follow leash RELEASED`, `AttackApproach keep-offset ... target=focus`; суммон бежит прямо к врагу (не возвращается к игроку), по дороге `re-engage StartCombat(...) -> true` и атакует. Приказ не должен сбрасываться (`combat never started`) раньше 15 с.
* **[ ] TEST-COMBAT-8 (Target pin seat runs):** В первом же бою в логе должна появиться строка `[TargetPin] selector seat OBSERVED running`. После `Numpad 1` — `[TargetPin] Summon 0x... pinned to focus 0x...` и затем `engine picked handle ..., answered with focus ...`. Если вместо этого раз в 2 с идёт `not pinned this update (... membership absent)`, фокус не попал в цели боевой группы — пришлите эти строки и соседние `add-target StartCombat`.
* **[ ] TEST-COMBAT-9 (Focus among several enemies):** В бою с 2–3 врагами навестись на дальнего и нажать `Numpad 1`: суммон бросает текущего противника и дерётся с назначенным до его смерти, не переключаясь, даже если другие бьют его.
* **[ ] TEST-COMBAT-10 (Ranged summon keeps range):** С атронахом (огненным/грозовым) дать `Numpad 1` по врагу на расстоянии: атронах стреляет с дистанции, а не бежит вплотную (keep-offset `AttackApproach` снимается, как только движок воюет с фокусом).
* **[ ] TEST-COMBAT-11 (Cell border outdoors):** Снаружи пройти через границу ячейки с Follow/Return/Focus: приказы не замирают, фокус по врагу в соседней ячейке не сбрасывается сам (`focus target ... is gone` не появляется, пока враг жив и рядом).
* **[ ] TEST-COMBAT-7 (MoveTo does not fight):** Отправить отряд в `MoveTo` мимо врага: пока идёт переход, суммон не вступает в бой (биты включены), после прибытия (`-> Guard`) снова может обороняться.

