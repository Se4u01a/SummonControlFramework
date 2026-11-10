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

## 8. Реестр призывов и управление жизненным циклом (`SummonRegistry`)

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

---

## 15. Синхронизация с игровым циклом (`UpdateHook` и `MiddleHighProcessData`)

* **Файл:**
  * [src/Engine/UpdateHook.h](file:///D:/projects/Summon_Control_Framework/src/Engine/UpdateHook.h)
  * [src/Engine/UpdateHook.cpp](file:///D:/projects/Summon_Control_Framework/src/Engine/UpdateHook.cpp)

### Архитектурный разрыв между событиями магии и реальными акторами:
В Creation Engine при касте заклинаний школы Колдовства (`Conjure Familiar`, `Conjure Flame Atronach` и др.) событие `TESActiveEffectApplyRemoveEvent` генерируется для самого заклинателя (`caster == target == Player`), так как эффект призыва (`SummonCreatureEffect`) накладывается на кастера для удержания связи и таймера. Созданный в мире физический актор (`Actor*`) **не является** целью этого события.

### Решение через прямой опрос и хук главного цикла:
1. **Хук `PlayerCharacter::Update` (vtable index `0xAD`):** перехватывает обновление игрока в каждом кадре рендера, не создавая сторонних потоков.
2. **Тик тактической частоты (15 Гц):** планировщик `UpdateScheduler::ShouldTick(delta)` фильтрует вызовы, выполняя ресурсоемкую синхронизацию только с оптимальной тактической частотой (~66.7 мс).
3. **Прямое чтение `MiddleHighProcessData::commandedActors`:**
   * Внутренний массив движка `player->GetActorRuntimeData().currentProcess->middleHigh->commandedActors` содержит реальные хэндлы всех активных призванных существ (`ActorHandle`).
   * Извлекает `FormID` актора, проверяет факт жизни (`!IsDead()`), считывает остаток длительности из `activeEffect->duration - activeEffect->elapsedSeconds`.
   * Выполняет регистрацию в `SummonRegistry`.
   * Автоматически очищает исчезнувших или рассеянных движком существ через `NotifyEffectRemoved`.
4. **Мгновенный триггер при событиях:** при поступлении `TESActiveEffectApplyRemoveEvent` адаптер немедленно инициирует `update::SyncPlayerSummons()`, исключая задержку отображения при касте.
5. **Исполнение приказов движения:** на каждом тактическом тике после дренажа очереди команд вызывается `movement::UpdateForSummon(...)`, который переводит `SummonModel::order` в движковые примитивы `KeepOffsetFromActor` / `SetDontMove` (см. §20).

---

## 16. Защита от дружественного огня и фильтрация атак (Friendly Fire & Attack Filtering)

* **Дополнение к спецификации:** [Friendly_Fire_Protection_Addendum.md](file:///D:/projects/Summon_Control_Framework/Friendly_Fire_Protection_Addendum.md) (Workstream FF-01)
* **Директории подсистемы:** src/FriendlyFire/, src/Integrations/

### 16.1. Разделение ответственности: Физика тел vs Боевые хитбоксы

Фундаментальный архитектурный принцип SCF: **не смешивать CollisionPairClassifier и AttackFilterManager**.

| Критерий | CollisionPairClassifier (Физика тел) | AttackFilterManager (Боевая фильтрация) |
|---|---|---|
| **Уровень движка** | Havok Character Controller (hkCharacterController) | Хитбоксы оружия, Havok Raycast, Precision, Projectiles, Magic |
| **Цель** | Устранить блокировку проходов («застревание в дверях») | Защитить призывы от ударов игрока и союзников, пропустить атаку во врага |
| **Область действия** | Прогулки, бег, навигация в тесных коридорах | Удары мечом, размашистые атаки, стрелы, болты, магия, крики |
| **Опасность ошибки** | Проваливание сквозь геометрию мира | Игнорирование ударов врагами, отключение боевой реакции |

### 16.2. Двухуровневая модель: Damage Protection vs Attack Pass-Through

В архитектуре FF-01 зафиксировано жесткое разграничение понятий:

1. **Damage Protection (MVP-уровень):**
   * Перехватывает этап применения урона (Actor::AdjustDamage, ProcessHit, ApplyDamage).
   * Обнуляет урон (damage = 0) и подавляет агрессию/враждебность (HostilityFilter).
   * При этом снаряд или меч могут визуально коснуться хитбокса призыва (отмена урона **не означает** продолжение движения).
2. **Attack Pass-Through (Целевой уровень):**
   * Призыв полностью исключается из кандидатов на столкновение до завершения удара/снаряда.
   * Меч (через Precision IgnoreHit) физически пролетает сквозь призыва и поражает врага позади него.
   * Стрелы и физические снаряды (через Havok collision filter) не вызывают impact на призыве и продолжают баллистическую траекторию.

### 16.3. Модель отношений: RelationshipClassifier и AttackSourceKind

Классификатор не полагается на примитивный IsHostileToActor(), а вычисляет строгий статус источника атаки:

`cpp
enum class AttackSourceKind : std::uint8_t
{
    Player,             // Игрок (0x14)
    PlayerAlly,         // Союзный NPC (компаньон, спутник, член фракции игрока)
    PlayerOwnedSummon,  // Призыв игрока (зарегистрирован в SummonRegistry)
    AlliedSummon,       // Призыв спутника/союзника
    Neutral,            // Нейтральный актор
    Hostile,            // Враг
    Unknown             // Не удалось разрешить (Safety Gate: не блокировать по умолчанию!)
};
`

#### Защитные барьеры классификатора:
* **Защита от циклов:** При обходе цепочки владельцев (GetCommandingActor()) глубина ограничена константой (макс. 4 уровня) с детекцией зацикливаний.
* **Принцип безопасного отказа (Fail-Safe):** Источник статуса Unknown **никогда** не фильтруется как союзник. Лучше допустить один случайный удар по призыву, чем сделать настоящего врага неуязвимым для атак.

### 16.4. Структура подсистемы и адаптеры

`	ext
src/FriendlyFire/
├── RelationshipClassifier.h/.cpp  // Чистая классификация AttackSourceKind и проверка целей
├── ProtectionPolicy.h/.cpp        // Матрица решений (ShouldFilterFriendlyAttack)
├── FriendlyFireSettings.h/.cpp    // Структуры конфигурации INI
├── AttackFilterManager.h/.cpp     // Маршрутизатор проверок
├── MeleeFilter.h/.cpp             // Ванильный melee и sweeping attacks
├── ProjectileFilter.h/.cpp        // Фильтрация стрел/болтов и физических снарядов
├── MagicFilter.h/.cpp             // Прямые заклинания, concentration, staves, shouts
├── ExplosionFilter.h/.cpp         // Исключение призыва из списков поражения AoE
├── HostilityFilter.h/.cpp         // Подавление дружественных петель враждебности
└── HitReactionFilter.h/.cpp       // Контроль stagger, recoil и killmoves

src/Integrations/
└── PrecisionBackend.h/.cpp        // Интеграция с PrecisionAPI (PreHitCallback)
`

### 16.5. Интеграция с Precision (PrecisionAPI.h)

Для сборок с установленным плагином Precision.dll (например, STB):
* В фазе SKSE Messaging (kPostLoad / kDataLoaded) плагин запрашивает интерфейс Precision:
  `cpp
  auto* precisionAPI = reinterpret_cast<PrecisionAPI::IVPrecision1*>(
      a_msg->Dispatch(PrecisionAPI::RequestPluginAPI));
  `
* Регистрируется легковесный PreHitCallback:
  `cpp
  precisionAPI->AddPreHitCallback(SKSE::GetPluginHandle(), [](const PrecisionAPI::HitInfo& a_hitInfo) {
      if (ShouldFilterFriendlyMelee(a_hitInfo.attacker, a_hitInfo.target)) {
          PrecisionAPI::PreHitCallbackReturn ret{};
          ret.bIgnoreHit = true; // Оружие физически летит дальше сквозь хитбокс!
          return ret;
      }
      return PrecisionAPI::PreHitCallbackReturn{};
  });
  `

### 16.6. Конфигурация INI ([FriendlyFire])

`ini
[FriendlyFire]
bEnableSummonProtection=true          ; Главный выключатель подсистемы
bProtectPlayerOwnedSummons=true       ; Защищать призывы игрока
bProtectAllyOwnedSummons=false        ; Защищать призывы спутников

bFilterPlayerAttacks=true             ; Фильтровать атаки игрока
bFilterPlayerAllies=true              ; Фильтровать атаки спутников
bFilterPlayerOwnedSummons=true        ; Фильтровать атаки других призывов игрока
bFilterAlliedSummons=true             ; Фильтровать атаки призывов спутников

; Защита от урона (Damage Protection)
bBlockMeleeDamage=true
bBlockArrowDamage=true
bBlockPhysicalProjectileDamage=true
bBlockMagicProjectileDamage=true
bBlockConcentrationDamage=true
bBlockAreaMagicDamage=true
bBlockShoutDamage=true
bBlockEnchantmentDamage=true

; Прохождение насквозь (Attack Pass-Through, экспериментально)
bMeleePassThrough=true
bArrowPassThrough=true
bPhysicalProjectilePassThrough=false
bMagicProjectilePassThrough=false

; Подавление побочных эффектов
bSuppressFriendlyAggression=true      ; Защита от combat-петли агро
bSuppressFriendlyKillmoves=true       ; Запрет добиваний союзных призывов
bSuppressFriendlyHitReactions=false   ; Stagger/recoil (выключено до тестов стабильности)
`

### 16.7. Матрица внешних зависимостей и архитектурная деградация (Graceful Degradation)

Фреймворк спроектирован так, чтобы отсутствие внешних модов не приводило к сбоям или повреждению сохранений:

| Компонент / Зависимость | Уровень | Механизм интеграции | Что происходит при отсутствии зависимости | Полнофункциональный режим |
|---|---|---|---|---|
| **SKSE64 + Address Library** | Обязательный (Hard) | ABI & Hook Injection | Плагин не запустится движком. | Базовый рантайм плагина. |
| **Precision (v2.0.4+)** | Опциональный (Soft) | Динамический зонд `GetProcAddress("RequestPluginAPI")` | `PrecisionBackend` деактивируется. Работает базовый `HitHook` на функции `ProcessHit`: урон обнуляется (`damage = 0`), агро глушится, но лезвие оружия останавливается на ванильном хитбоксе (хитстоп). | Активируется `PreHitCallback` с `bIgnoreHit = true`. Оружие физически со свистом пролетает сквозь тело сумона без задержек и хитстопа, поражая врага позади него. |
| **Behavior Data Injector** | Опциональный (Soft) | Зависимость Precision | Precision не может перехватывать события кадров анимации атаки. | Полноценная работа Precision. |
| **Перк «Парные души» (`000581DE`)** | Опциональный (Игровой) | Vanilla Engine Perk / Multi-summon mods | Движок Skyrim ванильно имеет лимит `commandedActors = 1`. При вызове второго сумона движок принудительно рассеивает первого. | Игрок может одновременно призывать 2 и более существ. SCF регистрирует и координирует отряд целиком. |
| **SSE Display Tweaks** | Рекомендуемый (Display) | SKSE Plugin | Игра может работать в эксклюзивном полноэкранном режиме с риском зависания при Alt-Tab; при FPS > 60 физика Havok может работать с артефактами. | Идеальный полноэкранный режим без рамки (Borderless Fullscreen), плавный Alt-Tab, адаптивный физический тик. |


---

## 17. Архитектура координации скрытности (Stealth Coordination)

### 17.1. Проблема несогласованности скрытности в ванильном Skyrim
В ванильной игре призванные существа не имеют контекста скрытности игрока. Если игрок крадется мимо группы врагов, сумон в ванильном режиме мгновенно бросается на первого попавшегося противника, демаскируя игрока и срывая стелс.

### 17.2. Модель StealthContext
Для изоляции логики скрытности в чистом C++ Core введена структура StealthContext ([src/Core/StealthContext.h](file:///D:/projects/Summon_Control_Framework/src/Core/StealthContext.h)):

```cpp
struct StealthContext {
    bool playerSneaking{false};      // Игрок находится в режиме скрытности
    bool playerDetected{false};      // Игрок обнаружен врагами (глаз открыт)
    bool explicitAttackOrder{false}; // Игрок отдал прямой приказ атаковать (FocusTarget)
};
```

### 17.3. Матрица поведения при скрытности
В подсистемах TargetSelector и StateMachine действуют строгие правила:

| Режим агрессии | Игрок крадется | Игрок обнаружен | Враг спокоен (Unaware) | Враг атакует | Прямой приказ (FocusTarget) | Решение сумона |
|---|---|---|---|---|---|---|
| Defensive | Да | Нет | Да | Нет | Нет | **Hold Fire** (Следовать за игроком, не атаковать) |
| Defensive | Да | Нет | Нет | Да | Нет | **Engage** (Защитить игрока от активной угрозы) |
| Defensive | Да | Да | Любой | Любой | Нет | **Engage** (Стелс сорван, бой разрешен) |
| Defensive | Да | Нет | Любой | Любой | Да | **Engage** (Приказ игрока имеет высший приоритет) |
| Aggressive | Да | Нет | Да | Нет | Нет | **Engage** (Режим Aggressive отключает скрытность) |
| Passive | Любой | Любой | Любой | Любой | Любой | **Passive** (Полный отказ от боя) |

### 17.4. Реализация в TargetSelector и StateMachine
- `TargetSelector::IsCandidateValid` проверяет `stealth`: если `playerSneaking && !playerDetected` и режим `Defensive`, ненастороженные враги, не атакующие игрока или сумона, отфильтровываются как недопустимые цели.
- `StateMachine::CanEngageCombat` удерживает состояние `FollowOwner` при скрытности игрока, пока не зафиксирована реальная боевая тревога или прямой приказ игрока.

---

## 18. Подсистема управления игроком и захват прицелом (Player Input & Controls)

### 18.1. Обработка ввода через RE::BSInputDeviceManager
Для надежного перехвата клавиш без задержек и конфликтов с Papyrus реализован InputHandler ([src/Engine/InputHandler.h](file:///D:/projects/Summon_Control_Framework/src/Engine/InputHandler.h)), зарегистрированный как sink интерфейса `RE::BSTEventSink<RE::InputEvent*>`:
- Перехватывает `RE::ButtonEvent` устройств `kKeyboard` и `kMouse`.
- Реагирует только на фазу нажатия (`event->IsDown()`).
- Клавиши клавиатуры сравниваются по DirectX scan-коду, кнопки мыши — по коду кнопки со смещением `SKSE::InputMap::kMacro_MouseButtonOffset` (256), то есть в том же пространстве номеров, в котором их выдаёт FLICK при переназначении. Один и тот же номер хоткея работает для обоих устройств.
- Не блокирует ввод других модов (возвращает `BSEventNotifyControl::kContinue`).

### 18.2. Захват цели прицелом (CrosshairPickData)
`InputHandler` читает `RE::CrosshairPickData` напрямую, потому что методы CommonLib (`GetActiveTarget()`) отдают только поле `target`. Полевой лог показал, что `target` (интерактивный, «activate»-пик) заполняется далеко не всегда, а `targetActor` — это актор под прицелом, который нужен для Focus Target. Поэтому порядок разрешения цели:
1. `targetActor` — актор под прицелом; далее `target` — интерактивная ссылка. Оба разрешаются в живой `RE::TESObjectREFR*` и безопасно преобразуются через `ref->As<RE::Actor>()`.
2. Валидация: отсекаются нулевые указатели, мертвые акторы (`actor->IsDead()`), сам игрок (`actor == player`) и призывы игрока.
3. Формируется высокоприоритетная команда `SetFocusTarget`, выполняется мгновенный такт, выводится HUD-подтверждение.
4. Если цель не найдена, в лог пишется одна диагностическая строка `[Crosshair] target=... targetActor=... point=...` — чтобы по полю было видно, какое из полей пустое.

Для **Move To Point** источник точки выбирается по приоритету:
1. `collisionPoint` — точка попадания рейкаста прицела (заполняется, когда пик попал в ссылку);
2. позиция ссылки/актора под прицелом;
3. **проекция прицела на землю**: направление берётся из `Actor::GetAngleZ()` игрока, а дистанция — из модуля `Actor::GetAimAngle()` (`kEyeHeight / tan(|pitch|)`, клампится в [200, 2500]); это работает, когда прицел наведён на землю (для неё `collisionPoint` не заполняется). Источник всегда пишется в лог (`MoveTo destination from ...`).

### 18.3. Хоткеи по умолчанию на цифровом блоке (Numpad)
По умолчанию хоткеи перенесены на цифровой блок (Numpad) для исключения конфликтов с популярными боевыми модами:
- **Numpad 1** (`0x4F`) — **Focus Target**: назначить цель под перекрестием прицела для всех сумонов.
- **Numpad 2** (`0x50`) — **Return**: экстренный возврат всех сумонов к игроку.
- **Numpad 3** (`0x51`) — **Toggle Aggression**: циклическое переключение режима агрессии (`Passive` -> `Defensive` -> `Aggressive`) с выводом на HUD.
- **Numpad 4** (`0x4B`) — **Move To Point**: приказ отряду переместиться в 3D-точку поверхности под перекрестием прицела (считывается через `CrosshairPickData::collisionPoint`) и занять оборону.
- **Numpad 5** (`0x4C`) — **Wait / Guard**: занять позицию охраны на месте.
- **Numpad 6** (`0x4D`) — **Follow**: возобновить следование за игроком в боевом строю.

### 18.4. Консольные приказы (scf order и алиас sfc)
Команда `scf order` поддерживает многосоставные параметры (благодаря `g_scfParams` и итерации по цепочке `StringChunk` движка) и отказоустойчива к опечаткам (зарегистрирован синоним `sfc`):
- `scf order return` — приказ возврата отряду.
- `scf order follow` — приказ следования.
- `scf order guard` (или `wait`, `stay`) — приказ занять оборону на позиции.
- `scf order moveto [x y z]` — переместиться в указанную точку или точку под прицелом.
- `scf order passive` — перевод отряда в пассивный режим.
- `scf order defensive` — перевод отряда в оборонительный режим.
- `scf order aggressive` — перевод отряда в агрессивный режим.
- `scf order focus <FormID>` — ручное указание цели по FormID в шестнадцатеричном формате (например `scf order focus 0001234F`).
- `scf help` / `scf order help` — вывод развернутой подсказки по всем приказам.

---

## 19. Интеграция с внутриигровым оверлеем FLICK Menu (FUCK.dll)

### 19.1. Архитектурная изоляция интерфейса
Интеграция с FLICK ([src/Integrations/FlickMenu.h](file:///D:/projects/Summon_Control_Framework/src/Integrations/FlickMenu.h)) построена на базе динамического C-ABI:
- Плагин SCF не компилирует реализацию ImGui внутрь своего бинарника, используя исключительно C-ABI функции экспорта `FUCK.dll` через [src/third_party/fuck/FUCK_API.h](file:///D:/projects/Summon_Control_Framework/src/third_party/fuck/FUCK_API.h).
- При отсутствии `FUCK.dll` в сборке игра запускается штатно: фреймворк логирует информационное сообщение и сохраняет полную работоспособность через INI и консоль.

### 19.2. Интерактивное переназначение клавиш (Binding UI)
По нажатию горячей клавиши оверлея (F7 по умолчанию):
1. Открывается вкладка **Summon Control Framework** в категории `Gameplay`.
2. Пользователь может кликнуть на любую из 6 строчек хоткеев: включается режим захвата ввода (`FUCK::StartBinding`).
3. На потоке ввода `OnAsyncInput` перехватывает нажатие любой клавиши клавиатуры или мыши и сохраняет скан-код в настройки `Settings`.
4. Вкладка предоставляет слайдеры дистанций (`Formation Distance`, `Catch-Up Run Distance`, `Return Leash Distance`, `Combat Leash Radius`, `Catch-Up Warp Distance`), переключатели дружественного огня, боевого поводка и аварийного warp, а также чекбоксы сквозного прохождения атак.
5. Кнопка **Save to INI** фиксирует сделанные изменения в файл `SummonControlFramework.ini`.

---

## 20. Исполнение приказов движения (`SummonMovement`)

* **Файлы:**
  * [src/Core/MovementIntent.h](file:///D:/projects/Summon_Control_Framework/src/Core/MovementIntent.h), [src/Core/MovementIntent.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/MovementIntent.cpp) — чистая трансляция приказа в намерение движения.
  * [src/Core/Formation.h](file:///D:/projects/Summon_Control_Framework/src/Core/Formation.h), [src/Core/Formation.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/Formation.cpp) — геометрия строя в системе координат владельца (общая для `SquadCoordinator` и слоя движения).
  * [src/Engine/SummonMovement.h](file:///D:/projects/Summon_Control_Framework/src/Engine/SummonMovement.h), [src/Engine/SummonMovement.cpp](file:///D:/projects/Summon_Control_Framework/src/Engine/SummonMovement.cpp) — единственное место, где вызываются движковые примитивы движения.

### 20.1. Проблема, которую решает слой
`Command` меняет только модель (`SummonModel::order`), а `StateMachine` — тактическое состояние. Сами по себе они не двигают актора. До появления слоя исполнение приказов сводилось к `StopCombat()` + `EvaluatePackage()`, что для призванного существа является **наблюдаемым no-op**: бой сбрасывается, но нового намерения движения никто не задаёт. Попытки использовать боевой AI (невидимый бессмертный «манекен» + `StartCombat`) двигали существо к точке, но оставляли его в бою с неуязвимой целью и не давали вернуться к игроку.

`SummonMovement` каждый тактический тик (15 Гц) переводит `order` в конкретное намерение и доводит его до Creation Engine, не создавая сущностей в мире, не подменяя AI-пакеты и не добавляя новых хуков.

### 20.2. Движковые примитивы и проверенные адреса
Все три функции в закреплённой ревизии CommonLibSSE-NG отсутствуют, поэтому объявлены в `SummonMovement.cpp` через Address Library:

| Функция | ID (SE, AE) | Проверка 1.5.97 |
|---|---|---|
| `Actor::SetDontMove(bool)` | `36490`, `37489` | `id 36490 -> SkyrimSE.exe+5E3330` (`addrlib.py`), совпадает со строкой `MovementDeny.SetDontMove` в `APMF/Docs/ADDRESS-TABLE-2026-09-15.md` |
| `Actor::KeepOffsetFromActor(const ActorHandle&, const NiPoint3& /*offset*/, const NiPoint3& /*angle*/, float /*catchUpRadius*/, float /*followRadius*/)` | `36870`, `37894` | `id 36870 -> SkyrimSE.exe+603990`; дизассемблер: функция читает `Actor::movementController` (`Actor+0x148`, `BSTSmartPointer<MovementControllerNPC>`), делает RTTI-cast к `IMovementSetKeepOffsetFromActor` (дескриптор типа присутствует в образе) и вызывает её слот 1 с записью `{ActorHandle target, NiPoint3 offset, NiPoint3 angle, float catchUp, float follow}` — это в точности список аргументов Papyrus `Actor.KeepOffsetFromActor` |
| `Actor::ClearKeepOffsetFromActor()` | `36871`, `37895` | `id 36871 -> SkyrimSE.exe+603AC0`, тот же интерфейс, слот 0 |

Семантика смещения (CK Wiki / Papyrus Index): смещения задаются в **body space цели** (`+X` — вправо от цели, `+Y` — вперёд, `+Z` — вверх); `afCatchUpRadius` — радиус, за пределами которого актор **бежит** догонять; `afFollowRadius` — радиус, внутри которого актор **не двигается**. Телепортации в этой функции нет. Третий аргумент-угол (`afOffsetAngleZ`, градусы) — это **угол поворота актора относительно цели**: при 0 актор смотрит туда же, куда владелец, из-за чего при движении вбок/назад он «идёт задом». Драйвер поэтому передаёт туда направление движения (см. 20.3).

Полевой лог подтвердил и систему осей: при вращении игрока локальные X/Y меняются, а Z остаётся постоянным — значит X/Y горизонтальные, Z вертикальная.

### 20.3. Матрица приказ → намерение
| `SummonModel::order` | Намерение | Действие |
|---|---|---|
| `Attack` (явный `FocusTarget`) | `None` (или `AttackApproach`) | Наивысший приоритет: снимает любую удерживающую/движущую команду, `SetDontMove(false)`, боем владеет `CombatGate` (только назначенная цель). Пока движок ещё не дерётся с фокусом и цель дальше `kAttackApproachRange`, суммон подводится keep-offset'ом (`AttackApproach`); когда боевой AI воюет с фокусом (его удерживает пин цели, §21.7), движением владеет он. Когда цель мертва/исчезла или бой так и не начался за 5 с — приказ возвращается в `Follow` |
| `WaitGuard` | `HoldPosition` | цель движения «паркуется» на самом призыве и включается `SetDontMove(true)` — чистая стойка на месте (боевые атаки в упор при этом работают) |
| `Return` | `ReturnToOwner` | `KeepOffsetFromActor(owner, 0,0,0)` + лицом к игроку; по достижении `fFollowMinDistance` приказ переключается на `Follow` |
| `MoveTo` | `MoveToPoint` | мировая точка цели пересчитывается в offset владельца **каждый тик**, поэтому владелец может двигаться и поворачиваться, а цель остаётся на месте; актор смотрит на цель. Точка дальше `kMaxMoveToDistance` (8000) считается устаревшей (смена ячейки, телепорт) и приказ отменяется в `Follow` |
| `Follow` | `FollowSlot` (только вне боя) | `KeepOffsetFromActor(owner, слот строя)`; лицом по направлению к слоту, а на самом слоте — туда же, куда владелец |
| бой (`Actor::IsInCombat()`) | `None` | примитивы не применяются: движением управляет боевой AI |

Слот строя берётся из `core::FormationSlotOffsetLocal` — той же таблицы, что использует `SquadCoordinator`; порядок существ — возрастание `FormID`, чтобы слоты не перетасовывались между тиками.

**Ориентация (исправлено после полевого теста).** `KeepOffsetFromActor` держит не только позицию, но и **поворот** актора: угол 0 = смотреть как владелец. Из-за этого волк «ходил задом», когда цель оказывалась сбоку или позади. Теперь `core::FacingOffsetDegrees` считает направление на цель движения в системе координат владельца, и это значение уходит в угол Z; когда актор уже стоит на точке (ближе `kFacingMinDistance` = 100), угол обнуляется, чтобы в строю смотреть туда же, куда игрок.

### 20.4. Аварийный catch-up warp
`Return` не телепортирует существо в нормальном режиме. Warp применяется только в двух случаях и всегда логируется:
1. расстояние до игрока превысило `[Recovery] fCatchUpDistance` (по умолчанию 3500);
2. существо не приблизилось ни на 50 единиц за 3 секунды (застряло).

Warp = позиция за спиной игрока на `fFollowMinDistance` + `MoveToNearestNavmesh()`; между разными ячейками он не выполняется (эту ситуацию закрывает ванильный snap призыва, порог которого `PatchGameSettings` выставляет из `fCatchUpDistance`). Один warp на один приказ `Return`.

### 20.5. Учёт состояния примитивов
`SetDontMove` и keep-offset — это **состояние на акторе**, поэтому драйвер хранит `AppliedMovement` на каждого призыва:
* примитивы вызываются только при смене намерения (смещение сравнивается с допуском 8 единиц, угол поворота — с допуском 10°);
* для движущихся режимов keep-offset повторно подтверждается раз в 2 секунды — на случай, если движок перезаписал цель своей ванильной логикой следования призыва;
* `SetDontMove(false)` принудительно отправляется на первом тике после (пере)обнаружения призыва, потому что флаг переживает загрузку сохранения;
* записи удаляются через `movement::RetainOnly` для призывов, покинувших `commandedActors` (dispel, смерть, detach ячейки, вытеснение), и через `movement::ResetAll()` на `kPreLoadGame` / `kNewGame`.

### 20.6. Потокобезопасность
Слой вызывается из того же места, что и остальная синхронизация: хук `PlayerCharacter::Update` (vfunc `0xAD`), главный поток, тактический тик 15 Гц. Новых хуков и потоков нет; все вызовы движка идут с игрового потока. Кэш состояния — обычный `std::unordered_map`, доступный только из этого потока.

### 20.7. Ограничения и допущения
* `SetDontMove(true)` блокирует **перемещение**; предполагается, что атаки в упор при этом продолжают работать (проверяется в игре). Если нет — для `HoldPosition` останется только парковка цели движения (без блокировки транслокации), это локальное изменение.
* Warp между ячейками не реализован намеренно: позиционная запись в чужой ячейке некорректна, а ванильный snap уже покрывает этот случай.
* Движение исполняется только пока призыв в той же ячейке, что и игрок: у призыва в другой ячейке процесс приостановлен, и движковые вызовы к нему не применяются.
* `FocusTarget`/агрессия исполняются через `StartCombat`/`StopCombat`, но только с разрешения `CombatGate` (раздел 21); слой движения уступает бою дорогу лишь тогда, когда политика бой разрешает.

---

## 21. Полный контроль боя (`CombatGate` и `CombatPolicy`)

* **Файлы:**
  * [src/Core/CombatPolicy.h](file:///D:/projects/Summon_Control_Framework/src/Core/CombatPolicy.h), [src/Core/CombatPolicy.cpp](file:///D:/projects/Summon_Control_Framework/src/Core/CombatPolicy.cpp) — чистые правила «можно ли этому призыву воевать и с кем».
  * [src/Engine/CombatGate.h](file:///D:/projects/Summon_Control_Framework/src/Engine/CombatGate.h), [src/Engine/CombatGate.cpp](file:///D:/projects/Summon_Control_Framework/src/Engine/CombatGate.cpp) — место отказа `Actor::StartCombat`, нативные биты и разрыв боя.
* **Исследование альтернативы:** [Combat_Override_Package_List_Research.md](file:///D:/projects/Summon_Control_Framework/Combat_Override_Package_List_Research.md), раздел 13 — почему CK `Combat Override Package List` + `HoldPosition` отклонён (мягкий leash, требует ESP/ESL, не умеет `Passive`/`Return`/одну цель).

### 21.1. Зачем
Полевой лог показал «два паттерна поведения»: приказ исполнялся, но ванильный AI призыва продолжал сам выбирать бой и втягивать актора в драку (в `Passive` `StopCombat()` вызывался на каждом такте, и движок тут же входил в бой снова; `FocusTarget` запускал `StartCombat`, но суммон не атаковал, оставаясь в собственной commanded-actor логике). Требование игрока — «суммон полностью отказывается от своих паттернов и живёт по состояниям FSM» — выполняется только запретом боя на уровне движка.

### 21.2. Seat `Actor::StartCombat` (патч точек вызова, НЕ входа функции)
| Что | Значение |
|---|---|
| Функция | `bool Actor::StartCombat(Actor* target, CombatGroup* group)` |
| ID | `RELOCATION_ID(37608, 38561)` (SE 1.5.97 → `SkyrimSE.exe+6251B0`) |
| Точки вызова | скан `.text` по `E8`/`E9` с rel32, точно указывающим на `StartCombat`: **10** на 1.5.97 (9 × `call`, 1 × `jmp`-обёртка `0x64776C`, которая переставляет аргументы). Совпадает с исследованием APMF («ten StartCombat callers») |
| Установка | каждая точка переписывается на ближний стаб трамплина, который прыгает в наш thunk: `E8` → `write_call<5>`, `E9` → `write_branch<5>`. Вход самой `StartCombat` **не трогается** |
| Оригинал | thunk вызывает реальную функцию по её проверенному адресу (вход не патчен → рекурсии нет) |
| Отказ | `return false` — ровно то, что движок сам возвращает для мёртвого `self`; вызывающий AI обрабатывает `false` |
| Фильтр | только зарегистрированные призывы; остальные акторы игры проходят без изменений |

**Почему не «хук на вход».** В этой ревизии CommonLibSSE-NG `Trampoline::write_branch<N>` не создаёт вызываемую копию функции: он патчит сайт на ближний стаб и **возвращает предыдущую цель перехода**, вычисленную из перезаписанных байт. На входе функции это мусор — 2026-11-10 игра упала именно на нём (`EXCEPTION_ACCESS_VIOLATION` на адресе, который установка залогировала как «original»). Патч точек вызова — единственный поддерживаемый этой ревизией способ сохранить «оригинал» вызываемым.

Thunk читает `CombatPolicy`, опубликованную главным потоком (мьютекс-таблица `FormID -> {allowCombat, restrictToTarget, target}`), поэтому вызов безопасен с любого потока (боевой поток движка). Отказы логируются с ограничением «не чаще раза в 2 секунды на суммона».

### 21.3. Нативные биты и разрыв боя
Пока политика запрещает бой, `CombatGate` выставляет `Actor::BOOL_FLAGS::kAttackingDisabled | kCastingDisabled` (нативный wholesale-deny, документированный APMF в `Docs/ALLOWANCE-TEMPLATE.md`) и снимает их, когда бой снова разрешён. Это страховка на случай, если бой уже идёт или `StartCombat` пришёл не через наш seat. При смене на «нельзя» немедленно вызывается `combat::BreakCombat` (`StopCombat()` + сброс `currentCombatTarget` актора; члены `CombatController` не пишутся — ими владеют боевые потоки, см. 21.7).

### 21.4. Матрица политики
| Состояние модели | `allowCombat` | `restrictToTarget` | `disableAttacks` | Поведение |
|---|---|---|---|---|
| `Passive` (любой приказ) | нет | — | да | не атакует ни при каких обстоятельствах, даже по `FocusTarget` |
| `Return` | нет | — | да | выходит из боя и бежит к игроку; бой снова возможен только после прибытия (`Follow`) |
| `MoveTo` с точкой | нет | — | да | идёт в точку, не отвлекаясь на бой; после прибытия — `Guard` |
| `Attack` + живая цель | да | да | нет | воюет только с назначенной целью, остальные `StartCombat` отклоняются |
| `Attack` без цели | нет | — | да | ждёт отката в `Follow` |
| `WaitGuard` | да | нет | нет | обороняется на месте (движение заблокировано) |
| `Follow` | да | нет | нет | бой по режиму агрессии, движение — за `SummonMovement` |

### 21.5. Подход к назначенной цели (`AttackApproach`)
Пока движок ещё не воюет с фокусом (не в бою или бьёт другую цель), при `Attack` и дистанции до цели больше `kAttackApproachRange` (150) драйвер движения каждый тик ставит keep-offset на мировую точку цели (offset относительно владельца, лицом к цели) — как `MoveTo`. В пределах 150 цель движения снимается, и удары выполняет боевой AI. Если движок сбросил бой, `StartCombat` повторяется не чаще раза в секунду (seat разрешает ровно эту цель).

### 21.6. Потокобезопасность и деградация
* `UpdateForSummon`/`RetainOnly`/`ResetAll` — только главный поток.
* `ShouldDenyStartCombat` — любой поток, только таблица под мьютексом, без обращений к движку.
* Если seat не установился (не нашлось точек вызова / не разрешился ID), `Install()` пишет ошибку и работает только второй слой (биты + `StopCombat`): `Passive` и `Return` остаются жёсткими, но движок может повторно входить в бой.
* `Passive`/`Return`/`MoveTo` также учитываются в `BuildMovementIntent`: при запрещённом бое `Follow` не уступает бой движку (иначе суммон стоял бы и ждал, пока его утащат).

### 21.7. Пин цели `FocusTarget` через селекторы `CombatController` (2026-11-10)
**Почему запись `targetHandle` не работала.** Цель дерущегося актора движок выбирает заново на каждом `Actor::UpdateCombat`: `CombatController::UpdateTarget` (SE id 32489 → `+4FE300`, единственный вызывающий — `UpdateCombat`) опрашивает активные `CombatTargetSelector` через слот 6 (`call [rax+0x30]`), берёт первый ненулевой ответ и, если он отличается от `controller+0x2C`, вызывает `CombatController::SetTarget` (id 32488 → `+4FDFE0`), который пишет `targetHandle` и `currentCombatTarget` актора. Наша запись этих полей из главного потока перетиралась на следующем боевом тике (и была гонкой с BSJobs-потоками) — поэтому Focus «исполнялся», но суммон дрался с тем, кого выбрал сам.

| Что | Значение (проверено по распакованному 1.5.97, `vtable_slot.py` + дизассемблер) |
|---|---|
| Seat | `write_vfunc` слот **6** `SelectTarget(this, ActorHandle* out) -> out` у `CombatTargetSelectorStandard` (vtable id 265605 → `+1690A98`, функция `+7B5E50`) и `CombatTargetSelectorFixed` (id 265607 → `+1690AF8`, функция `+7B6920`), цепочкой: сначала оригинал, потом наше решение |
| Чтение | селектор `+0x10` = `CombatController*` (сырое смещение, защищено проверкой идентичности vtable на каждом вызове), `controller->attackerHandle` → FormID призыва, таблица политик под мьютексом, `combatGroup->targets` под собственным read-lock группы |
| Правило | `core::ChooseSelectorAnswer` (`src/Core/TargetPin.{h,cpp}`): ответ заменяется на фокус, только если движок вообще держит цель (ответ ≠ 0 — пин никогда не начинает и не оживляет бой), фокус жив/загружен и является целью группы без флага `kTargetLost`. Иначе остаётся выбор движка |
| Вход в бой | `StartCombat(focus)` при приказе (новый `CombatController`/`CombatGroup`, фокус — первая цель группы); если суммон уже воюет с другим, раз в секунду повторный `StartCombat(focus)` добавляет фокус в цели группы |
| Быстрый путь | атомарный счётчик пинов: при отсутствии фокуса seat возвращается до мьютекса (он вызывается для каждого воюющего NPC) |
| Диагностика | `[TargetPin] selector seat OBSERVED running` (первый вызов за сессию), `pinned to focus`, `engine picked handle ..., answered with focus ...`, `not pinned this update (... membership absent/lost)` раз в 2 с |

Тот же механизм описан и проверен в APMF (ch.20 Target Pin) — адреса совпадают с его таблицей `VERIFIED-ADDRESSES`.

**Движение при `Attack`.** Пока боевой AI дерётся именно с фокусом (`IsInCombat` и `currentCombatTarget == focus`), локомоцией, дистанцией и выбором атак владеет он (дальнобойный/кастер держит дистанцию). `AttackApproach` (keep-offset к цели) работает только в промежутке, пока движок ещё не воюет с фокусом — иначе две цели движения конфликтуют.

**Члены `CombatController` из главного потока больше не пишутся.** Разрыв боя — `combat::BreakCombat` = `StopCombat()` + сброс `currentCombatTarget` актора; записи `targetHandle`/`startedCombat` удалены отовсюду (`Return`, `Guard`, `Follow`, `Passive`, leash, смена политики).

### 21.9. Поводок ванильного следования (`FollowLeash`, 2026-11-11)
* **Факт движка (1.5.97):** функция id 36410 (`+5DBA80`) — единственный читатель GMST `fSummonedCreatureMinFollowDist`/`MaxFollowDist` — строит призыву runtime-пакет через `+5DD030` → `+43D460`: экземпляр шаблона по имени `"FollowTo"` (строка `+160D9F8`; в Skyrim.esm это PACK `00025E6D`, входы `Target to Follow` uid 2, `Min Radius` uid 4, `Max Radius:` uid 5, `Location` uid 8, `Bool` uid 6) с радиусами из GMST, и кладёт его актору (`PutCreatedPackage`, vfunc 0xDF). Значение `BGSPackageDataFloat` (vtable id 252945 → `+1605F40`) — float по `+0x10` (Assign slot 3, LoadBuffer slot 8).
* **Следствие:** собственный пакет призыва всё время тянет его в радиус Min от игрока. Follow/Return это не замечали (направление совпадает), а MoveTo и подход к Focus-цели срывались — «останавливается и смотрит на меня».
* **Решение:** `engine::leash::UpdateForSummon` каждый тактический тик: если текущий пакет призыва — экземпляр `FollowTo` (проверка `templateParent`, каждая запись проверяется по vtable `BGSPackageDataFloat` перед чтением `+0x10`), то при `core::ReleasesFollowLeash(model)` (`MoveTo` с точкой, `Attack`, `WaitGuard`) Min/Max = 100000, иначе — возврат текущих значений GMST, но только если радиусы ставил SCF. Новый экземпляр пакета (движок пересоздаёт его) получает отпуск на следующем тике. Лог: `[Leash] ... follow leash RELEASED|restored`, а при отсутствии экземпляра — `is not a FollowTo instance` с типом пакета и шаблоном.
* **Опознание пакета (M0, 2026-11-11):** поле показало, что у экземпляра `templateParent = 0`. Поэтому пакет опознаётся (1) по факту создания — seat на call-site `id 36410 + 0x84` (`+5DBB04`, `call` id 36422 `+5DD030`; `rcx` призыв, `rdx` владелец, `[rsp+0x20]` Min, `[rsp+0x28]` Max, возвращает `TESPackage*`), только SE 1.5.97, с проверкой байтов перед патчем; thunk запоминает пакет (указатель только сравнивается) и создаёт его сразу отпущенным, если призыв под «удалённым» приказом; (2) по шаблону `FollowTo`; (3) по форме — `core::IsSummonFollowPackageShape` (FormID `0xFF…`, тип 18, ровно два float-входа). Таблица seat'а общая с главным потоком под мьютексом.
* **Подход к Focus-цели:** keep-offset якорится на самой цели (offset 0 в её системе координат, follow 150, catch-up 300), а не на игроке: цель относительно игрока смещалась при каждом его шаге/повороте. Боевому AI движение отдаётся, только когда он воюет с фокусом **и** ближе 400 ед. (бой с далёкой/необнаруженной целью — это поведение поиска, а не подход). Ожидание входа в бой — 15 с.

### 21.8. «Одно загруженное пространство» вместо «одна ячейка»
`engine::ShareLoadedSpace` (`src/Engine/ActorSpace.h`): та же ячейка, либо две загруженные внешние ячейки одного worldspace. Сравнение `GetParentCell() ==` снаружи ошибочно: сетка загруженных ячеек шире одной, и суммон, идущий за игроком через границу ячейки, или враг в соседней ячейке полностью загружены и обрабатываются. Старое сравнение сбрасывало фокус, как только враг стоял через границу, и останавливало все приказы движения, пока суммон отставал на ячейку. Аварийный warp по-прежнему только внутри одной ячейки (запись позиции не меняет родительскую ячейку).


