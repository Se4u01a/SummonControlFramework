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
