# Testing Guide: Summon Control Framework (SCF)

Этот документ фиксирует статус проверок, тестовые наборы и инструкции для валидации как офлайн (Unit-тесты без Skyrim), так и в игре.

---

## 1. Automated Unit Tests (`scf_tests.exe`)

Все тесты выполняются за **~2 миллисекунды** автономным исполняемым файлом `build\windows\x64\releasedbg\scf_tests.exe`.
Текущий результат: **75/75 тестов успешно пройдено (0 failures)**.

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
| Core unit tests | 75/75 passing via `scf_tests.exe` (< 2 ms) |
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

## 6. Приемочные тесты: Приказы игрока, Хоткеи и Скрытность (Stage 3 & 5)

### 6.1. Хоткеи управления отрядом
* **[ ] TEST-ORDER-1 (Focus Target Hotkey - G):** При наведении перекрестия на враждебного актора и нажатии `G` появляется сообщение `"SCF: Focus Target Set"`, все сумоны переключаются на выбранную цель.
* **[ ] TEST-ORDER-2 (Focus Target Guard):** Нажатие `G` при наведении на дружественного NPC, сумона или пустую точку игнорируется (не сбрасывает текущие цели и не бьет своих).
* **[ ] TEST-ORDER-3 (Return Hotkey - H):** Нажатие `H` выводит `"SCF: Summon Return Ordered"`, все сумоны немедленно прекращают бой и бегут к игроку.
* **[ ] TEST-ORDER-4 (Toggle Aggression Hotkey - J):** Нажатие `J` циклически переключает режимы: `Passive` -> `Defensive` -> `Aggressive` с выводом сообщения на HUD.

### 6.2. Консольные приказы (`scf order`)
* `scf order return` — приказ отряду вернуться к игроку.
* `scf order follow` — возобновить следование за игроком.
* `scf order guard` — занять позицию охраны.
* `scf order passive` — перевести в пассивный режим (не атакуют).
* `scf order defensive` — перевести в оборонительный режим (атакуют только при угрозе).
* `scf order aggressive` — перевести в агрессивный режим (атакуют всех врагов).
* `scf order focus <FormID>` — указать цель по FormID.

### 6.3. Координация скрытности (Stealth Coordination)
* **[ ] TEST-STEALTH-1 (Sneak Hold Fire):** Игрок приседает в скрытность (`Ctrl`), сумон в режиме `Defensive`. При приближении к врагу (индикатор скрытности закрыт `[   ]`) сумон спокойно следует за игроком и **не агрится** на спокойного врага.
* **[ ] TEST-STEALTH-2 (Sneak Detected Engage):** Если враг замечает игрока (индикатор открывается `[ ОБНАРУЖЕНО ]`), сумон немедленно бросается в бой.
* **[ ] TEST-STEALTH-3 (Sneak Focus Target):** Находясь в скрытности, игрок нажимает `G` на врага: сумон бесшумно срывается вперед и атакует указанную цель.
