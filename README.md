# Summon Control Framework (SCF)

[![Build and Test](https://img.shields.io/badge/build-xmake-blue.svg)](https://xmake.io)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-purple.svg)]()
[![Skyrim](https://img.shields.io/badge/Skyrim-SE%201.5.97%20%7C%20AE%201.6.x-brightgreen.svg)]()

SKSE-фреймворк для **Skyrim Special Edition / Anniversary Edition**, обеспечивающий полный контроль над призванными существами: 3 быстрых заклинания приказов, защита от дружественного огня со сквозным пролётом оружия/стрел/магии, бесконфликтные коллизии, встроенная боевая зона **Summon Combat Radius**, **Zero-Master Keyword API v2**, постоянные призывы с авто-возрождением, панель миньонов и **Papyrus API**.

### Документация (`docs/`)
* [Карта проекта (`docs/PROJECT_MAP.md`)](docs/PROJECT_MAP.md) — слои, главный тик и ответственность каждого файла
* [Руководство по Keyword API v2 (`docs/KEYWORDS.md`)](docs/KEYWORDS.md) — полный справочник всех `SCF_*` кейвордов, примеров KID/SPID и приоритетов
* [Архитектура (`docs/ARCHITECTURE.md`)](docs/ARCHITECTURE.md) — внутреннее устройство, хуки, контракты и ограничения
* [План разработки (`docs/PLAN.md`)](docs/PLAN.md) — дорожная карта и статусы этапов
* [Текущий статус (`docs/STATUS.md`)](docs/STATUS.md) — что проверено автотестами и что подтверждено в игре
* [Тестирование (`docs/TESTING.md`)](docs/TESTING.md) — команды автотестов и полевые тест-кейсы
* [Инженерный журнал (`docs/JOURNAL.md`)](docs/JOURNAL.md) — хронология исследований и решений

---

## 📦 Установка из релиза

1. Установите обязательные зависимости: **SKSE64** и **Address Library for SKSE Plugins** под вашу версию Skyrim.
2. Установите архив `SummonControlFramework-v*.zip` через **Mod Organizer 2** или **Vortex** (как обычный мод).
3. Включите плагин **`SummonControlFramework.esp`** (имеет флаг **ESL** — не занимает слот ESP).
4. Запустите игру: 3 заклинания приказов будут выданы игроку автоматически при загрузке сохранения или старте новой игры (`[Spells] bAutoGrantCommandSpells = true`).
5. Для настройки в игре откройте меню **FLICK** (по умолчанию `F7` → категория `Gameplay`), если установлен `FUCK.dll`, или редактируйте `Data/SKSE/Plugins/SummonControlFramework.ini`.

> **Отладочные символы:** отдельный архив `SummonControlFramework-v*-pdb.zip` содержит `.pdb` для читаемых стектрейсов в Crash Logger и требуется только для диагностики.

---

## ✨ Основные возможности

### 1. Управление отрядом: 3 заклинания с быстрым кастом
Приказы призывам отдаются через 3 заклинания для рук (`Either Hand`, `0` маны, быстрый каст `0.1` с, школа Колдовства, без прокачки навыка) из ESL-плагина `SummonControlFramework.esp` — удобно использовать через Wheeler, Spell Hotbar или избранное:

| Заклинание | EditorID | Действие |
|---|---|---|
| **Приказ: Держать позицию** | `SCF_CmdHoldPosition` | Призывы прерывают бой и удерживают текущую позицию (`WaitGuard`). |
| **Приказ: Следовать** | `SCF_CmdFollow` | Призывы прерывают бой, возвращаются к игроку и переходят в следование (`Return -> Follow`). |
| **Приказ: В атаку** | `SCF_CmdAttackTarget` | При допустимом попадании цель вступает в бой с игроком, а призывы фокусируются строго на ней (действует и на нейтральную цель). |

* Также доступны консольные команды: `scf status`, `scf registry`, `scf reload`, `scf order <return | follow | guard | moveto [x y z] | passive | defensive | aggressive | focus <FormID>>`, `scf help` (поддерживается алиас `sfc`).
* Пересборка плагина из `Skyrim.esm`: `python tools/make_command_spells_esp.py <Skyrim.esm> data/SummonControlFramework.esp`.

### 2. Защита от дружественного огня и сквозные атаки (Friendly Fire & Pass-Through)
* **Ближний бой:** с установленным **Precision** оружие физически пролетает сквозь союзных призывов без хитстопа (`bIgnoreHit = true`) и бьёт врага за ними; без Precision `HitHook` обнуляет урон, стаггер и агро.
* **Снаряды:** `ProjectileHook` пропускает стрелы, болты и магические снаряды сквозь союзных призывов без детонации.
* **Магия и AoE:** `MagicHook` (`Actor::AddTarget`) защищает призывов от дружественных прямых заклинаний, концентрации, криков, зачарований и AoE-взрывов (фаерболы), не мешая лечению и баффам.
* **Добивания и реакции:** `KillmoveHook` и `HitReactionHook` блокируют добивания и стаггер по своим призывам.
* **Бесконфликтные коллизии (`PassThrough`):** игрок и призывы одного владельца не блокируют друг друга в дверях и коридорах (`bhkCollisionFilter` + `MovementAvoidanceManager`).

### 3. Summon Combat Radius (встроенный SCR v7.2)
Автоматическая боевая зона работает у призыва в режиме следования, если у него есть один из кейвордов `aaMZkw_SummonCombatArea200`, `aaMZkw_SummonCombatArea500` или `aaMZkw_SummonCombatArea1000` (либо параметрический `SCF_CombatRadius_<N>`). Добавьте кейворд на базу `NPC_`, `RACE` или `MGEF` призыва через xEdit / Creation Kit / KID. Сам `SummonControlFramework.esp` содержит базовые кейворды и оставляет их назначение модам и патчам; если загружен оригинальный мод SCR, SCF распознает его кейворды.

* Новые цели выбираются внутри радиуса `R` от игрока; текущая цель удерживается до `R + 75`. Явный приказ «В атаку» действует и за пределами зоны.
* Конфигурация: `Data/SKSE/Plugins/SummonCombatRadius.ini` (секция `[Keywords]` заменяет встроенные радиусы). В опциональном **SKSE Menu Framework** доступны панели **Summon Combat Radius → Status / Settings** с кнопкой **Reload INI**.
* `[Collision] enabled` включает проход собственных призывов сквозь других персонажей через фильтр `PassThrough` SCF.

### 4. Постоянные призывы, лимит армии и панель миньонов
* **Постоянные призывы (`Persistence`):** продление действия призыва, авто-возрождение после смерти, опциональный резерв макс. маны и сохранение состояния в SKSE co-save.
* **Лимит армии (`SummonLimit`):** настраиваемый бонус к лимиту призывов, режим `bUnlimitedSummons` и поддержка кейворда `SCF_FreeSlot`.
* **Панель миньонов (`MinionPanel`):** опциональный HUD-оверлей (`FUCK.dll`, переключение по клавише `uMinionPanelToggleKey`) со списком активных/возрождающихся призывов.

---

## 🔑 Keyword API v2 для других модов

SCF поддерживает **Zero-Master Keyword API v2** — декларативное управление поведением суммонов, заклинаний призыва и произвольных актёров с помощью стандартных записей `KYWD` (`SCF_*`) **без зависимости от `SummonControlFramework.esp`**. Подробный справочник всех 36 флагов, 10 алиасов, 4 параметрических числовых кейвордов, матрицы приоритетов и примеров KID/SPID вынесен в **[`docs/KEYWORDS.md`](docs/KEYWORDS.md)**.

Краткий обзор категорий:
* **Коллизии (World):** `SCF_PassThroughFriends`, `SCF_PassThroughAll`.
* **Угроза и выбор цели врагами (World):** `SCF_Untargetable`, `SCF_HighThreat` (`SCF_TauntPriority`), `SCF_LowThreat`.
* **Дружественный огонь (World):** `SCF_FriendlyFireProtected`, `SCF_AllowFriendlyFire`.
* **Регистрация, лимит и персистентность:** `SCF_AutoRegister`, `SCF_Unmanaged` (`SCF_IgnoreSummon`), `SCF_FreeSlot` (`SCF_IgnoreLimit`), `SCF_Persistent`, `SCF_NoPersistence`.
* **Боевой режим, приказы и скрытность:** `SCF_NonCombat` (`SCF_AlwaysPassive`), `SCF_DefaultPassive`/`Defensive`/`Aggressive`, `SCF_IgnoreOrders`/`Hold`/`Follow`/`AttackOrder`, `SCF_NoAssist`, `SCF_NoCircling` (`SCF_DirectAssault`), `SCF_HoldFireInStealth`, `SCF_IgnoreCombatRadius`.
* **Специализации авто-выбора цели:** `SCF_Bodyguard`, `SCF_FinishLowHP`, `SCF_HuntCasters`, `SCF_HuntRanged`.
* **Движение, турели, сектора строя и M3-команды:** `SCF_Stationary` (`SCF_Turret`), `SCF_NoCatchUpWarp`, `SCF_NoCombatLeash`, `SCF_FormationFront`/`Flank`/`Back` (`SCF_RoleTank`/`Melee`/`Ranged`/`Support`), `SCF_CmdRush`, `SCF_CmdDetonate`.
* **Параметрические числовые кейворды:** `SCF_CombatRadius_<N>`, `SCF_Leash_<N>`, `SCF_FollowDist_<N>`, `SCF_SpeedMult_<N>`.

* Кейворды читаются с `NPC_`, `RACE`, активных `MGEF` и исходного `MGEF` заклинания призыва; регистр не важен; нулевой CPU-оверхед, если кейворды не объявлены.
* Проверочный ESP со всеми 40 кейвордами и заклинаниями: `python tools/make_keyword_test_esp.py <Skyrim.esm> SCF_KeywordApiTest.esp`.

---

## 🐍 Papyrus API для других модов

Если призыв вашего мода движок не числит среди призванных существ игрока (скриптовое существо, кастомная нежить), зарегистрируйте его явно — SCF начнёт вести его как обычный призыв (для бескриптовой альтернативы см. `SCF_AutoRegister` в [`docs/KEYWORDS.md`](docs/KEYWORDS.md)).

Контракт поставляется в `scripts/Source/SCF.psc` (класс `SCF`, глобальные нативы):

```papyrus
bool Function RegisterSummon(Actor akSummon, Actor akOwner = None) global native
bool Function UnregisterSummon(Actor akSummon) global native
bool Function IsRegistered(Actor akSummon) global native
Actor[] Function GetSummons(Actor akOwner = None) global native
int Function GetSummonCount(Actor akOwner = None) global native
```

* Владелец — **только игрок** (`akOwner = None` подразумевает игрока).
* Скриптовая регистрация **не занимает слот лимита призывов** и не вытесняется лимитом; снимать её при смерти/диспелле (`UnregisterSummon`) должен вызывающий скрипт.
* Нативы вызываются из обычных функций Papyrus (не из tasklet-контекста).

---

## 🧩 Зависимости и матрица совместимости

Фреймворк спроектирован по принципу **Graceful Fallback**: отсутствие опциональных модов не вызывает вылетов.

### Обязательные требования (Hard Requirements)
* **Skyrim Special Edition 1.5.97** (основной проверенный рантайм) или **Anniversary Edition 1.6.x** (поддерживается через Address Library; отдельные SE-специфичные seat'ы имеют безопасный fallback).
* **SKSE64** под вашу версию игры.
* **Address Library for SKSE Plugins**.

### Опциональные интеграции (Feature-Dependent)
| Компонент / Мод | Затрагиваемый функционал | Без этого компонента | С этим компонентом |
|---|---|---|---|
| **[Precision](https://www.nexusmods.com/skyrimspecialedition/mods/72347)** (v2.0.4+) | **Melee Pass-Through** | `HitHook` обнуляет урон и агро, но удар останавливается на хитбоксе призыва. | `PrecisionBackend` (`bIgnoreHit = true`): оружие физически пролетает сквозь призыва без хитстопа и бьёт врага за ним. |
| **FLICK / FUCK.dll** (v7 API) | Внутриигровое меню настроек и **Панель миньонов** | Настройки читаются из `SummonControlFramework.ini`. | Интерактивное меню по `F7` (`Gameplay`), управление заклинаниями и оверлей панели миньонов. |
| **SKSE Menu Framework** | Панели **Summon Combat Radius** | Настройки SCR читаются из `SummonCombatRadius.ini`. | Живые панели **Summon Combat Radius → Status / Settings** и **Reload INI**. |
| **TrueHUD** | Индикаторы HUD | Базовая работа без TrueHUD-индикаторов. | Поддержка опционального бэкенда TrueHUD. |
| **Перк «Парные души»** (`000581DE`) или `[SummonLimit]` SCF | Отряд из 2+ существ | Ванильный лимит = 1 существо (если не включён лимит SCF). | Координация отряда из 2+ призывов. |

---

## 🛠 Сборка из исходников и автотесты

### Требования к окружению разработчика
* **ОС:** Windows 10/11 x64
* **Компилятор:** Visual Studio 2022 (MSVC v143+, C++23)
* **Система сборки:** [xmake](https://xmake.io) 3.0.8+
* **CommonLibSSE-NG:** ветка `ng` (переменная окружения `COMMONLIBSSE_NG_PATH`, либо `extern/CommonLibSSE-NG`)

### Сборка и запуск тестов
```powershell
# Конфигурация (отладочные символы + оптимизация)
xmake f -m releasedbg -y

# Сборка DLL плагина (SE + AE) и автономного раннера тестов
xmake build -y

# Запуск автономных юнит-тестов ядра (без запуска Skyrim)
xmake run scf_tests
```

Выходные артефакты:
* `build\windows\x64\releasedbg\SummonControlFramework.dll`
* `build\windows\x64\releasedbg\SummonControlFramework.pdb`
* `build\windows\x64\releasedbg\scf_tests.exe`

---

## 📂 Структура репозитория

```text
Summon_Control_Framework/
├── src/
│   ├── Core/               # Чистое C++23 ядро (без заголовков Skyrim)
│   ├── AI/                 # TargetSelector, SquadCoordinator
│   ├── Collision/          # CollisionPairClassifier
│   ├── FriendlyFire/       # RelationshipClassifier + Hit/Magic/Projectile/Killmove/HitReaction hooks
│   ├── Engine/             # UpdateHook, CombatGate, CombatRadius, SummonMovement, PassThrough, KeywordApi, Persistence, SummonLimit, CommandSpells и др.
│   ├── Integrations/       # Precision, FLICK/FUCK, MinionPanel, CombatRadiusMenu, TrueHUD, Papyrus
│   ├── Plugin/             # RuntimeGate, Messaging, ConsoleCommands
│   └── main.cpp            # SKSEPluginLoad
├── data/                   # SummonControlFramework.esp (ESL)
├── scripts/Source/         # Papyrus-контракт SCF.psc
├── tools/                  # Генераторы ESP и консольная тестовая сцена
├── tests/                  # Автономные юнит-тесты ядра (266 тестов)
├── docs/                   # PROJECT_MAP, KEYWORDS, ARCHITECTURE, PLAN, STATUS, TESTING, JOURNAL
├── AGENTS.md               # Инженерные стандарты
└── xmake.lua               # Конфигурация сборки
```

Подробное описание каждого файла см. в [`docs/PROJECT_MAP.md`](docs/PROJECT_MAP.md).
