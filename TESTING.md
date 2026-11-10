# Testing Guide: Summon Control Framework (SCF)

Этот документ фиксирует статус проверок, тестовые наборы и инструкции для валидации как офлайн (Unit-тесты без Skyrim), так и в игре.

---

## 1. Automated Unit Tests (`scf_tests.exe`)

Все тесты выполняются за **~2 миллисекунды** автономным исполняемым файлом `build\windows\x64\releasedbg\scf_tests.exe`.
Текущий результат: **62/62 тестов успешно пройдено (0 failures)**.

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

---

## 2. Static / offline (passing)

* **Compiler:** MSVC 14.44 (Visual Studio 2022 Build Tools) с `/std:c++latest` (C++23).
* **Warning level:** `/W4` (`allextra`), `/permissive-`, `/Zc:preprocessor`. Чистая сборка с нулевым количеством ворнингов в коде мода.
* **Precompiled Header (PCH):** [src/PCH.h](file:///D:/projects/Summon_Control_Framework/src/PCH.h) с предкомпиляцией `RE/Skyrim.h`, `SKSE/SKSE.h` и `spdlog`.
* **Multi-runtime Configuration:**
  * CommonLibSSE-NG собрана с `ENABLE_SKYRIM_SE=1` и `ENABLE_SKYRIM_AE=1`.
  * Архитектура `windows|x64`.
  * Address Library режим (`SKSE::VersionIndependence::AddressLibrary`).
* **Развертывание бинарников:**
  * DLL: `D:\Games\stb\[STB] Mod Organizer\mods\Summon Control Framework\SKSE\Plugins\SummonControlFramework.dll` (~600 KB).
  * PDB: `SummonControlFramework.pdb` (полные отладочные символы).
  * INI: `SummonControlFramework.ini` (конфигурация мода со всеми секциями).
  * Профиль MO2: активирован в `D:\Games\stb\[STB] Mod Organizer\profiles\SBORKA\modlist.txt` как `+Summon Control Framework`.

---

## 3. In game (traces in `SummonControlFramework.log`)

### Verified
| Component | Evidence |
|---|---|
| Plugin build output | `SummonControlFramework.dll` generated (~600 KB, with `.pdb`) |
| Core unit tests | 61/61 passing via `scf_tests.exe` |
| MO2 deployment | Deployed to `mods\Summon Control Framework` and enabled in `SBORKA` |

### Ready for live Skyrim check (SE 1.5.97.0 via MO2)
| Feature | Target verification |
|---|---|
| Plugin load | `SummonControlFramework v0.1.0 is loading...` in SKSE log |
| `RuntimeGate` check | Log lines verifying `Detected game version: 1.5.97.0` |
| `kDataLoaded` receipt | Log line `Data loaded: SKSE message received, SCF FSM ready` |
| Console command `scf` | Executing `scf status`, `scf registry`, `scf reload` prints output in console |
| Event sinks registration | Log line verifying registration of `ActiveEffect`, `Death`, `CellAttachDetach` |
| Summon registration | Log lines on casting `Conjure Familiar` (`player.addspell 00021143`) |
| Twin Souls limit displacement | Recasting Familiar exceeding limit displaces oldest active summon |
| Collision bypass | In-game non-blocking pass-through between player and summoned familiar |
