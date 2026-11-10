# Summon Control Framework (SCF)

[![Build and Test](https://img.shields.io/badge/build-xmake-blue.svg)](https://xmake.io)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-purple.svg)]()
[![Skyrim](https://img.shields.io/badge/Skyrim-SE%20%7C%20AE-brightgreen.svg)]()

Фреймворк для Skyrim Special Edition / Anniversary Edition, расширяющий управление призванными существами (тактические режимы, выбор приоритетов боя, приказы и бесконфликтные коллизии отряда).

Подробный план разработки см. в [Summon_Control_Framework_PLAN.md](Summon_Control_Framework_PLAN.md).  
Архитектурные принципы и устройство см. в [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 🛠 Требования к окружению

* **ОС:** Windows 10/11 x64
* **Компилятор:** Visual Studio 2022 (MSVC v143+, C++23)
* **Система сборки:** [xmake](https://xmake.io) 3.0.8+ (`winget install xmake` или через официальный инсталлятор)
* **CommonLibSSE-NG:** Ветка `ng` (локальная копия в `D:\Games\Issen\CommonLibSSE-NG` или путь через переменную окружения `COMMONLIBSSE_NG_PATH`)
* **Целевая версия Skyrim:**
  * Steam AE 1.6.1170 (основной тестовый рантайм для PoC)
  * Skyrim SE 1.5.97 (поддерживается через Address Library)

---

## 🚀 Сборка

### 1. Настройка проекта
```powershell
# Конфигурация в режиме releasedbg (отладочные символы + оптимизация)
xmake f -m releasedbg -y
```

### 2. Компиляция
```powershell
xmake build -y
```
После первой сборки выходные файлы доступны по пути:
* DLL плагина: `build\windows\x64\releasedbg\SummonControlFramework.dll`
* Отладочные символы: `build\windows\x64\releasedbg\SummonControlFramework.pdb`
* Бинарник тестов: `build\windows\x64\releasedbg\scf_tests.exe`

### 3. Автоматическое копирование в папку мода MO2 / Vortex
При конфигурации можно указать каталог `deploy_dir`:
```powershell
xmake f -m releasedbg --deploy_dir="C:/Games/MO2/mods/SummonControlFramework/SKSE/Plugins"
xmake build -y
```
Бинарники будут автоматически копироваться в каталог мода после каждой успешной сборки.

---

## 🧪 Запуск автоматических тестов

Чистая логика принятия решений (`Core` и `AI`) компилируется в автономный исполняемый файл и выполняется без запуска игры:

```powershell
xmake run scf_tests
```

Тесты проверяют:
* Переходы состояний Gameplay FSM (`Follow`, `Guard`, `AcquireTarget`, `Approach`, `Engage`, `Return`).
* Защиту от дребезга при потере цели (Hysteresis / Debounce).
* Разрыв поводка при удалении от игрока (`Leash Break`).
* Приоритеты и режимы агрессии селектора целей (`Passive`, `Defensive`, `Aggressive`, `FocusTarget`).
* Защиту от дружественного огня по владельцу и союзным призывам.

---

---

## 🧩 Зависимости и матрица деградации функционала

Фреймворк спроектирован по принципу **Graceful Fallback**: отсутствие опциональных модификаций не приводит к вылетам, но влияет на физику и боевые возможности.

### 1. Обязательные требования (Hard Requirements)
Без них плагин не загрузится движком:
* **Skyrim Special Edition (1.5.97)** или **Anniversary Edition (1.6.x)**.
* **SKSE64** (версия под вашу сборку игры).
* **Address Library for SKSE Plugins** (ersion-1-5-97-0.bin или соответствующий версии игры).

### 2. Зависимости отдельных возможностей (Feature-Dependent Integrations)

| Компонент / Мод | Затрагиваемый функционал | Без этого компонента | С этим компонентом |
|---|---|---|---|
| **[Precision](https://www.nexusmods.com/skyrimspecialedition/mods/72347)** (v2.0.4+) | **Melee Pass-Through** (Сквозной пролет оружия сквозь сумонов) | Работает базовый HitHook: урон сумону обнуляется (damage = 0), агро подавляется, но ванильный хитбокс рейкаста вызывает хитстоп/остановку замаха на сумоне. | Активируется PrecisionBackend (IgnoreHit = true): лезвие меча со свистом физически пролетает сквозь тело сумона без хитстопа и поражает врага позади него. |
| **Behavior Data Injector** | Интеграция Precision | Precision не может перехватить поведение графа анимаций и кадры атаки. | Precision работает штатно. |
| **Перк «Парные души»** (player.addperk 000581DE) или мод на мультипризыв | **Отряд из 2+ существ** (maxSummonsPerOwner >= 2) | Ванильный движок Skyrim SE имеет жесткий лимит commandedActors = 1. При касте второго сумона первый мгновенно рассеивается. | Игрок может призвать и удерживать одновременно 2 и более существ; SCF корректно координирует отряд. |
| **SSE Display Tweaks** | Режим окна без рамки и физика при высоком FPS | Игра запускается в эксклюзивном полноэкранном режиме, Alt-Tab может подвешивать игру, физика при FPS > 60 может сбоить. | Идеальное окно без рамки нативное разрешению, мгновенный Alt-Tab, адаптивный физический шаг Havok. |
| **ConsolePlusPlus** | Удобство управления через консоль | Невозможно вставить команду из буфера обмена (Ctrl+V), нет истории команд. | Вставка команд через Ctrl+V, история команд по стрелкам ↑/↓. |
| **Console Font Fix** (onts_console.swf) | Читаемость консоли | При случайном переключении на русскую раскладку консоль выводит квадраты □□□. | Корректное отображение русских и английских букв. |

## 📂 Структура проекта

```text
Summon_Control_Framework/
├── src/
│   ├── Core/               # Чистая модель: EntityID, Types, SummonModel, StateMachine
│   ├── AI/                 # Тактический ИИ: TargetSelector
│   ├── Plugin/             # SKSE обвязка: RuntimeGate
│   ├── PCH.h               # Прекомпилированный заголовок
│   └── main.cpp            # SKSEPluginLoad и точка входа плагина
├── tests/
│   ├── check.h             # Легковесный тест-раннер
│   ├── main.cpp            # Точка входа тестов
│   ├── test_fsm.cpp        # Тесты конечного автомата
│   └── test_target_selector.cpp # Тесты селектора целей
├── ARCHITECTURE.md         # Описание архитектурных решений
├── Summon_Control_Framework_PLAN.md # Дорожная карта и план реализации
└── xmake.lua               # Конфигурация сборки
```
