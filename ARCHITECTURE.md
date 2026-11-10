# Архитектура Summon Control Framework (SCF)

Документ описывает архитектурные принципы, структуру кодовой базы и технические обоснования принятых решений.

---

## 1. Главная философия: Decoupled Core Architecture

### Проблема традиционной разработки SKSE-плагинов
В большинстве модификаций для Skyrim логика искусственного интеллекта и состояний пишется вперемешку с вызовами Creation Engine (`RE::Actor*`, `TESObjectREFR`, функции Havok и виртуальной машины Papyrus). Это приводит к четырем фундаментальным проблемам:
1. **Невозможность автоматического тестирования:** любой тест требует сборки мода, запуска `SkyrimSE.exe`, загрузки сохранения и воспроизведения ситуации вручную.
2. **Use-After-Free и краши памяти:** движок Skyrim асинхронно выгружает 3D-модели, очищает ячейки (`Cell Attach/Detach`) и удаляет объекты. Прямое хранение указателей приводит к мгновенным вылетам при доступе к инвалидированной памяти.
3. **Гонки потоков (Race Conditions):** движок выполняет физику, анимации и скрипты в разных потоках. Прямые манипуляции с актором из неподтвержденного контекста разрушают внутренние инварианты движка.
4. **Хрупкость при обновлении рантайма:** смена версии игры ломает смещения полей в структурах классов.

### Наше решение: разделение на 3 изолированных слоя

```text
┌────────────────────────────────────────────────────────┐
│                      Core & AI                         │
│  (Чистый C++23: StateMachine, TargetSelector, Models)  │
│  - Не знает о Skyrim, SKSE, Havok или Win32             │
│  - Оперирует EntityID, Vec3 и чистыми структурами     │
│  - 100% покрывается автономными Unit-тестами          │
└──────────────────────────┬─────────────────────────────┘
                           │ Команды и снимки мира
┌──────────────────────────▼─────────────────────────────┐
│                   Engine Adapters                      │
│  (Связующий слой: считывание мира, исполнение команд)  │
│  - ActorHandle <-> EntityID                            │
│  - Передача задач в главный поток (TaskInterface)     │
└──────────────────────────┬─────────────────────────────┘
                           │ Вызовы CommonLibSSE-NG
┌──────────────────────────▼─────────────────────────────┐
│                 Skyrim / SKSE Runtime                  │
│  - Hook points, Havok collision filters, Event sinks   │
└────────────────────────────────────────────────────────┘
```

---

## 2. Модель сущностей и идентификация (`EntityID`)

* **Файл:** [src/Core/EntityID.h](file:///D:/projects/Summon_Control_Framework/src/Core/EntityID.h), [src/Core/SummonModel.h](file:///D:/projects/Summon_Control_Framework/src/Core/SummonModel.h)

### Почему `EntityID` вместо `RE::Actor*`:
1. `EntityID` (`uint32_t`) — тривиально копируемый, детерминированный тип.
2. Он не зависит от валидности виртуальных таблиц или расположения объекта в куче Skyrim.
3. В адаптере движка (`Engine`) `EntityID` ассоциируется с безопасным `BSPointerHandle<Actor>` (`ActorHandle`). Разрешение хэндла в живой объект происходит **только в момент тика** и **только в проверенном потоке**, с мгновенным отсечением выгруженных или удаленных сущностей.

---

## 3. Две разные State Machine: Gameplay FSM vs Animation Graph

* **Файл:** [src/Core/StateMachine.h](file:///D:/projects/Summon_Control_Framework/src/Core/StateMachine.h), [src/Core/StateMachine.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/StateMachine.cpp)

В плане [Summon_Control_Framework_PLAN.md](file:///D:/projects/Summon_Control_Framework/Summon_Control_Framework_PLAN.md) четко зафиксировано правило: **не смешивать логику принятия решений и анимационный пайплайн движка**.

1. **Gameplay FSM (наш чистый C++):**
   * Решает **намерение** существа: `Idle` -> `FollowOwner` -> `Guard` -> `AcquireTarget` -> `Approach` -> `Engage` -> `Return` -> `Despawn`.
   * Определяет, когда атаковать, кого защищать, когда прекратить погоню и вернуться к игроку.
2. **Havok Behavior Graph (ванильный движок):**
   * Отвечает за **исполнение**: поиск пути по NavMesh, проигрывание шагов, анимации замаха оружием, кадры попадания (hit frames) и реакцию на ошеломление (stagger).
   * Мы не вмешиваемся в анимационный граф без доказанной необходимости.

### Ключевые механизмы устойчивости FSM:
* **Анти-дребезг (Debounce / Hysteresis):** таймер `targetLostTime`. Если враг на мгновение пропал из поля зрения или скрылся за укрытием, FSM не бросает бой сиюсекундно, а ожидает таймаут `targetLostDebounceTime` (по умолчанию 1.5 сек). Это полностью устраняет «нервное» дергание ИИ между бегом к игроку и возвратом в бой.
* **Разрыв поводка (`Leash Break`):** если игрок на спринте удаляется на дистанцию более `returnDistanceThreshold` (2500 единиц), призыв принудительно сбрасывает бой и переходит в состояние `Return`, предотвращая застревание существа в бесконечной драке на другом конце локации.
* **Приоритет прямых приказов:** команда `Return` или `WaitGuard` от игрока моментально прерывает автоматическое преследование.

---

## 4. Тактический селектор целей (`TargetSelector`)

* **Файл:** [src/AI/TargetSelector.h](file:///D:/projects/Summon_Control_Framework/src/AI/TargetSelector.h), [src/AI/TargetSelector.cpp](file:///D:/projects/Summon_Control_Framework/src/AI/TargetSelector.cpp)

### Архитектура скоринга вместо жестких веток `if-else`
Вместо запутанного дерева условий `TargetSelector` использует двухфазную модель:
1. **Жесткая фильтрация пригодности (Qualification):**
   * Исключение мертвых (`isDead`) и отключенных (`isDisabled`) акторов.
   * **Абсолютная защита от дружественного огня:** кандидат отсекается, если это владелец (`ownerId`), сам призыв (`id`) или союзник (`isOwnerOrAlly`).
   * Проверка режима агрессии (`Passive` не пропускает никого; `Defensive` требует наличия факта угрозы; `Aggressive` допускает всех врагов в радиусе).
2. **Динамический скоринг (Scoring):**
   * `Базовый вес`: чем ближе цель к призыву, тем выше балл.
   * `Приоритет защиты владельца (+1000)`: кандидат, непосредственно наносящий урон игроку, получает наивысший приоритет.
   * `Приоритет самообороны (+500)`: кандидат, атакующий призыва.
   * **«Липкая цель» / Гистерезис смены цели (`currentTargetStickyBonus = +400`):** цель, которую призыв уже атакует, получает значительный бонус. Новый противник не перехватит внимание призыва, если он не подошел существенно ближе или не начал бить игрока. Это исключает хаотичное метание призыва между двумя рядом стоящими врагами.
   * `Добивание раненых (+200 * (1 - HP%))`: поощрение устранения целей с низким здоровьем для скорейшего снижения входящего урона.

### Приоритет Focus Target
Если игрок явно указывает цель (через прицел или хоткей):
* Фокус-цель валидируется на безопасность (нельзя натравить призыва на игрока или союзника).
* Если цель валидна — она выбирается немедленно в обход скоринга.
* Если цель недопустима — плагин логирует причину отказа и безопасно переходит к автоматическому скорингу доступных врагов.

---

## 5. Наблюдаемость и логирование (Observability)

* **Файл:** [src/Plugin/RuntimeGate.cpp](file:///D:/projects/Summon_Control_Framework/src/Plugin/RuntimeGate.cpp), [src/main.cpp](file:///D:/projects/Summon_Control_Framework/src/main.cpp)

* Ядро не привязано к конкретной библиотеке логирования: и `StateMachine`, и `TargetSelector` принимают абстрактный функциональный callback `LogCallback`.
* В юнит-тестах callback собирает события в `std::vector` для assert-проверок.
* В SKSE плагине callback передает структурированные сообщения в `spdlog` (`Documents\My Games\Skyrim Special Edition\SKSE\SummonControlFramework.log`).
* Каждое изменение состояния логируется с указанием причины: `[FSM] Entity 1: FollowOwner -> AcquireTarget (Reason: Target detected)`.

---

## 6. Система сборки: почему xmake

* **Файл:** [xmake.lua](file:///D:/projects/Summon_Control_Framework/xmake.lua)

1. **Скорость и легкость:** xmake не требует огромных вспомогательных файлов, легко управляет C++23 и флагами MSVC.
2. **Интеграция с CommonLibSSE-NG:** правило `commonlibsse-ng.plugin` автоматически генерирует бинарные ресурсы версий Windows (`.rc`) и структуру экспорта плагина `SKSEPlugin_Version`.
3. **Двойная цель:**
   * `SummonControlFramework` — компилирует DLL плагина SKSE с включением PCH и CommonLib.
   * `scf_tests` — компилирует автономный консольный бинарник тестов ядра без тяжелых движковых библиотек. Сборка и запуск тестов занимают ~1 секунду.
4. **Гибкий поиск зависимостей:** автоматический поиск CommonLib через переменную `COMMONLIBSSE_NG_PATH`, локальный путь `D:/Games/Issen/CommonLibSSE-NG` или подмодуль `extern/CommonLibSSE-NG`.

---

## 7. Координация отряда и арбитраж слотов атак (`SquadCoordinator`)

* **Файл:** [src/AI/SquadCoordinator.h](file:///D:/projects/Summon_Control_Framework/src/AI/SquadCoordinator.h), [src/AI/SquadCoordinator.cpp](file:///D:/projects/Summon_Control_Framework/src/AI/SquadCoordinator.cpp)

### Проблема «свалки» (Dogpiling):
В стандартном Skyrim все призванные существа независимо выбирают одну и ту же ближайшую цель и устремляются в одну точку. В результате существа расталкивают друг друга, застревают в геометрии и блокируют игрока.

### Решение:
1. **Квотирование слотов атак:**
   * Каждая цель имеет ограниченную емкость слотов ближнего боя (`maxMeleeSlotsPerTarget`, по умолчанию 2) и дальнего боя (`maxRangedSlotsPerTarget`, по умолчанию 3).
   * Если лимит ближнего боя исчерпан, следующие бойцы ближнего боя перенаправляются на альтернативные цели или удерживают строй.
2. **Симметричные формации:**
   * Вычисление веерных смещений отряда с учетом угла рыскания (Yaw) игрока: 1 призыв (левый фланг), 2 призыва (левый и правый арьергард), 3-4 призыва (фланговые «рога» и тыловые якоря).
   * Непересекающиеся позиции предотвращают наложение физических капсул.

---

## 8. Реестр призывов и управление жизненным циклом (`SummonRegistry`)\

* **Файл:** [src/Core/SummonRegistry.h](file:///D:/projects/Summon_Control_Framework/src/Core/SummonRegistry.h), [src/Core/SummonRegistry.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/SummonRegistry.cpp)

1. **Идемпотентная регистрация:** повторный каст или обновление длительности существующего призыва не порождает дубликатов в отряде.
2. **Состояние `Suspended` при выгрузке ячейки:** при переходе через двери ячейка выгружается (`TESCellAttachDetachEvent(detach)`), но существо не удаляется из игры. Реестр переводит запись в `Suspended` и восстанавливает `Active` при загрузке ячейки, защищая от потери контроля.
3. **Вытеснение старейшего призыва при превышении лимита:** при призыве существа сверх лимита владельца реестр вычисляет старейший активный призыв, маркирует его `Displaced` и возвращает `EntityID` для инициирования dispel в движке.

---

## 9. Очередь команд и приоритеты (`CommandQueue`)

* **Файл:** [src/Core/CommandQueue.h](file:///D:/projects/Summon_Control_Framework/src/Core/CommandQueue.h), [src/Core/CommandQueue.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/CommandQueue.cpp)

1. **Иерархия приоритетов (Strict Priority Hierarchy):**
   * `Lifecycle` (0): смерть, принудительный dispel, вытеснение (высший приоритет).
   * `PlayerDirect` (1): хоткеи игрока (`Return`, `FocusTarget`, `WaitGuard`).
   * `ModeChange` (2): смена режима агрессии (`Passive`, `Defensive`, `Aggressive`).
   * `Tactical` (3): автоматические решения ИИ (низший приоритет).
2. **Коалесценция и подавление осцилляций:** быстрые нажатия клавиш игроком объединяются in-place, исключая бесполезную очередь устаревших приказов.
3. **Безопасность TTL (Time-to-Live):** команды старше 5 секунд автоматически аннулируются, исключая запоздалое выполнение после паузы или загрузочных экранов.
4. **Потокобезопасность:** защищена `std::mutex` для безопасной передачи из потока ввода в главный поток тика.

---

## 10. Планировщик обновлений и квантование времени (`UpdateScheduler`)

* **Файл:** [src/Core/UpdateScheduler.h](file:///D:/projects/Summon_Control_Framework/src/Core/UpdateScheduler.h), [src/Core/UpdateScheduler.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/UpdateScheduler.cpp)

1. **Отвязка тактовой частоты ИИ от FPS рендера:** тактическая частота зафиксирована на 15 Гц (~66.7 мс) вместо 60-144 Гц рендера, экономя такты CPU.
2. **Аккумулятор с защитой от дрейфа и спайков:** дробные остатки дельты сохраняются; спайки времени свыше 2 интервалов усекаются, защищая от лавинных догоняющих расчетов.
3. **Time-Slicing отряда:** расчеты поиска целей для нескольких существ распределяются по соседним тикам по алгоритму чередования (round-robin).
4. **Немедленное пробуждение (Priority Immediate Wakeup):** экстренные приказы игрока вызывают `RequestImmediateTick()`, инициируя расчет на ближайшем кадре.

---

## 11. Управление конфигурацией (`Settings`)

* **Файл:** [src/Core/Settings.h](file:///D:/projects/Summon_Control_Framework/src/Core/Settings.h), [src/Core/Settings.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/Settings.cpp)

1. **Чистый C++ INI-парсер без внешних зависимостей:** сохраняет нулевой оверхед и независимость от сторонних библиотек.
2. **Двойная схема именования:** поддерживает как ванильные префиксы Skyrim (`bEnabled`, `sLogLevel`, `uMaxSummonsPerOwner`, `fUpdateFrequencyHz`), так и строчные ключи без префиксов.
3. **Санитизация и клампинг границ:** `ValidateAndSanitize()` защищает от некорректных значений из пользовательских INI, предотвращая зацикливания и деление на ноль.

---

## 12. Центральный контекст рантайма и интеграция движка

* **Файл:**
  * [src/Core/FrameworkContext.h](file:///D:/projects/Summon_Control_Framework/src/Core/FrameworkContext.h) / [FrameworkContext.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/FrameworkContext.cpp)
  * [src/Core/ConsoleHandler.h](file:///D:/projects/Summon_Control_Framework/src/Core/ConsoleHandler.h) / [ConsoleHandler.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/ConsoleHandler.cpp)
  * [src/Plugin/ConsoleCommands.h](file:///D:/projects/Summon_Control_Framework/src/Plugin/ConsoleCommands.h) / [ConsoleCommands.cpp](file:///D:/projects/Summon_Control_Framework/src/Plugin/ConsoleCommands.cpp)
  * [src/Plugin/Messaging.h](file:///D:/projects/Summon_Control_Framework/src/Plugin/Messaging.h) / [Messaging.cpp](file:///D:/projects/Summon_Control_Framework/src/Plugin/Messaging.cpp)

1. **`FrameworkContext`:** объединяет все подсистемы (`Settings`, `SummonRegistry`, `CommandQueue`, `UpdateScheduler`, `StateMachine`, `SquadCoordinator`), связывает их логгеры в единый поток и предоставляет централизованный метод `ResetRuntimeState()` для очистки при загрузке сохранений.
2. **`ConsoleHandler`:** чистое C++ ядро обработки консольных команд (`scf status`, `scf registry`, `scf reload`, `scf help`), полностью тестируемое офлайн без запуска игры.
3. **`ConsoleCommands`:** плагин-адаптер, подключающий консольную команду `scf` к таблице консольных функций Skyrim через `SCRIPT_FUNCTION::LocateConsoleCommand` и выводящий результаты в `RE::ConsoleLog`.
4. **`Messaging`:** диспетчер сообщений SKSE (`kDataLoaded`, `kPreLoadGame`, `kPostLoadGame`, `kNewGame`), отвечающий за инициализацию INI-файла и своевременную очистку временных реестров.

---

## 13. Адаптация событий Creation Engine (`EventDispatcher` и `EventAdapter`)

* **Файл:**
  * [src/Core/EventDispatcher.h](file:///D:/projects/Summon_Control_Framework/src/Core/EventDispatcher.h) / [EventDispatcher.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/EventDispatcher.cpp)
  * [src/Engine/EventAdapter.h](file:///D:/projects/Summon_Control_Framework/src/Engine/EventAdapter.h) / [EventAdapter.cpp](file:///D:/projects/Summon_Control_Framework/src/Engine/EventAdapter.cpp)

1. **`EventDispatcher` (Core):**
   * Чистая логика обработки событий Creation Engine без включения заголовочных файлов движка.
   * `HandleActiveEffect`: регистрирует призыв при наложении эффекта, исключает баффы на себя (`caster == target`), вызывает callback вытеснения при превышении лимита, переводит в `Dispatched` при снятии эффекта.
   * `HandleDeath`: переводит погибшего актора в статус `Dead`, безопасно игнорируя нелетальные события.
   * `HandleCellAttachDetach`: реализует двухфазный переход `Active <-> Suspended` при загрузке и выгрузке ячейки.
2. **`EventAdapter` (Engine):**
   * Реализует `BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>`, `BSTEventSink<RE::TESDeathEvent>` и `BSTEventSink<RE::TESCellAttachDetachEvent>`.
   * Извлекает `FormID` из `NiPointer<TESObjectREFR>` с проверкой на `nullptr`.
   * Регистрирует приемники событий через `RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<T>()` в момент `kDataLoaded`.

---

## 14. Классификация и фильтрация коллизий (`CollisionPairClassifier`)

* **Файл:**
  * [src/Collision/CollisionPairClassifier.h](file:///D:/projects/Summon_Control_Framework/src/Collision/CollisionPairClassifier.h)
  * [src/Collision/CollisionPairClassifier.cpp](file:///D:/projects/Summon_Control_Framework/src/Collision/CollisionPairClassifier.cpp)

### Проблема блокировки движения и «застревания в дверях»:
В стандартном Skyrim коллизии персонажей (`bhkCharacterController` / Havok) жестко сталкивают игрока и его собственных призванных существ. В узких проходах, подземельях и дверных проемах крупный призыв (например, Огненный атронах или Грозовой атронах) полностью блокирует движение игрока, вызывая физический клинч (jitter и push).

### Архитектура и матрица решений классификатора:
`CollisionPairClassifier` реализует изолированную логику оценки пары сущностей:

| Пара сущностей | Режим обхода включен | Результат | Обоснование |
|---|---|---|---|
| **Игрок ↔ Собственный призыв** | Да | `Passthrough` (игнорировать коллизию) | Игрок свободно проходит сквозь призыва в дверных проемах |
| **Игрок ↔ Чужой призыв** | Да | `Default` (стандартное столкновение) | Сохраняется физическое взаимодействие с призванными врагами |
| **Призыв A ↔ Призыв B (один владелец)** | `bEnableSummonToSummonBypass=true` | `Passthrough` | Призывы отряда не расталкивают друг друга в узких коридорах |
| **Призыв A ↔ Призыв B (разные владельцы)** | Любое | `Default` | Боевой контакт между существами разных магов |
| **Призыв ↔ Враг** | Любое | `Default` | Сохраняется расчет попаданий оружия и физических коллизий в бою |
| **Не-призыв ↔ Любая сущность** | Любое | `Default` | Мир, окружение, статическая геометрия и NPC не модифицируются |

### Защитные механизмы (Safety Gates):
1. **Глобальный выключатель (`bEnableCollisionBypass = false`):** мгновенно возвращает движок к 100% ванильной физике без накладных расходов.
2. **Селективный выключатель `bEnableSummonToSummonBypass`:** позволяет раздельно настраивать прохождение игрока сквозь призывов и призывов сквозь друг друга.
3. **Безопасность геометрии:** классификатор принимает решение об отключении коллизии **только** для пар живых сущностей с зарегистрированным владением. Коллизия со стенами, полами и препятствиями мира никогда не затрагивается.
