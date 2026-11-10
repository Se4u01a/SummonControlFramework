# Дополнение FF-01 — Friendly Fire Protection & Attack Filtering

> Дополнение к `PLAN.md` проекта **Summon Control Framework**.  
> Статус: техническое предложение / исследовательский workstream.  
> Назначение: защитить призывы игрока от случайных атак игрока и его союзников, а в идеале — сделать так, чтобы дружественные атаки проходили сквозь призывы и продолжали поражать цели за ними.  
> Актуальность сведений о сторонних проектах проверена: **10 ноября 2026 года**.

---

## FF-01.1. Цель и границы функциональности

Добавить опционально включаемую систему защиты призванных существ от friendly fire.

Система должна уметь работать в двух независимых режимах:

1. **Damage Protection** — атака не наносит защищённому призыву урон и не вызывает нежелательную враждебность. При этом визуальное попадание, hit reaction, остановка снаряда или другие последствия столкновения ещё могут происходить.
2. **Attack Pass-Through** — защищённый призыв не считается препятствием для соответствующей атаки. Меч, стрела или снаряд должны продолжать обработку траектории/попаданий и иметь возможность поразить врага за призывом.

Эти режимы нельзя считать одним и тем же. Отмена урона после регистрации попадания **не доказывает**, что атака прошла сквозь актёра и продолжила движение.

### Целевой результат

- Защищённые призывы не получают урон от игрока и выбранных категорий союзников.
- Дружественные атаки по возможности не останавливаются на защищённом призыве.
- Атака, прошедшая сквозь призыв, сохраняет возможность попасть во врага за ним.
- Вражеские атаки по-прежнему наносят призывам обычный урон.
- Лечение, усиления и прочие не-враждебные эффекты не блокируются без отдельной настройки.
- Функциональность можно независимо отключать через INI/MCM.

**Не обещать полное Attack Pass-Through до подтверждения в игре.** Блокирование урона — отдельная, более достижимая цель MVP; продолжение траектории атаки — отдельная исследовательская задача.

---

## FF-01.2. Матрица поведения

| Источник атаки | Цель | Damage Protection | Attack Pass-Through |
|---|---|---:|---:|
| Игрок | Призыв игрока | Да, если включено | Для melee/projectile — экспериментально |
| Союзный NPC | Призыв игрока | Да, если включено | Для melee/projectile — экспериментально |
| Другой призыв игрока | Призыв игрока | Да, если включено | Для melee/projectile — экспериментально |
| Призыв союзного NPC | Призыв игрока | Опционально | Опционально |
| Вражеский NPC | Призыв игрока | Нет | Нет |
| Вражеский призыв | Призыв игрока | Нет | Нет |
| Игрок / союзник | Вражеский NPC за призывом | Обычный урон | Атака должна продолжиться, если pass-through реализован |
| Враг | Игрок / союзник | Ванильное поведение | Ванильное поведение |

### Типы атак, которые следует учитывать

| Тип | Примеры | Что нужно проверить |
|---|---|---|
| Обычный melee | Одноручное/двуручное оружие, кинжалы | Отмена hit по призыву; продолжение sweep/trace до следующей цели |
| Sweeping attacks | Размашистые атаки, несколько целей | Не ломается ли регистрация следующих целей после пропуска призыва |
| Precision melee | Атаки через Precision hitboxes | Отдельная интеграция с Precision callback; поведение после `bIgnoreHit` |
| Стрелы и болты | Лук, арбалет | Не только отмена урона, но и продолжение полёта/impact search |
| Физические снаряды | Метательные и модовые projectile forms | Проверка collision/impact path конкретного типа снаряда |
| Магические снаряды | Fireball и другие projectile spells | Разделить физическое столкновение, применение Magic Effect и взрыв |
| Концентрационные заклинания | Flames, Sparks и аналоги | Фильтрация повторного урона на протяжении всего контакта |
| AoE / Explosion | Fireball explosion, Rune, area spells | Исключение защищённого актора из списка получателей эффекта; не путать с физическим pass-through |
| Shouts | Урон и эффекты криков | Проверить, проходят ли они через тот же magic/effect pipeline |
| Зачарования и посохи | Weapon enchantments, staves | Проверить источник эффекта и классификацию атакующего |
| Killmove / hit reaction | Добивания, stagger, recoil | Отдельная политика; не считать автоматически частью защиты от урона |

---

## FF-01.3. Классификация атакующего и защищённой цели

Не определять союзника только через `IsHostileToActor()`. Отношения между акторами, фракциями и цепочками командования могут быть сложнее одного булевого признака.

Ввести отдельный `RelationshipClassifier`, который отвечает на два независимых вопроса:

1. Является ли цель призывом, который SCF должен защищать?
2. Является ли источник атаки игроком или актёром из разрешённой союзной группы?

Концептуальная классификация источника:

```cpp
enum class AttackSourceKind
{
    Player,
    PlayerAlly,
    PlayerOwnedSummon,
    AlliedSummon,
    Neutral,
    Hostile,
    Unknown
};
```

Концептуальная политика:

```cpp
bool ShouldFilterFriendlyAttack(
    RE::Actor* attacker,
    RE::TESObjectREFR* target,
    AttackKind attackKind);
```

### Правила классификации

- Основной источник истины о призывах игрока — `SummonRegistry`.
- `IsCommandedActor()` и `GetCommandingActor()` можно использовать как признаки и для построения цепочек владельцев, но не считать единственным универсальным способом найти все модовые призывы.
- Для союзников проверять faction/team relationship и цепочки владения/командования.
- Поддержать вложенные цепочки: например, союзный follower → его summon.
- При обходе цепочки владельцев предусмотреть защиту от циклов и неограниченной глубины.
- Не считать `ActorTypeGhost` или одиночный keyword достаточным доказательством принадлежности к дружественному призыву.
- `Unknown` по умолчанию не должен автоматически считаться союзником: лучше не блокировать неизвестную атаку, чем случайно сделать врага безопасным для игрока.
- Классификация и решение о фильтрации должны выполняться отдельно от самого engine hook, насколько это позволяет конкретная точка перехвата.

### Защищённые цели

По умолчанию защищать только призывы, зарегистрированные как принадлежащие игроку. Отдельной настройкой разрешить распространить защиту на призывы союзных NPC/компаньонов.

---

## FF-01.4. Настройки INI / MCM

Ниже — **предлагаемая схема настроек**, а не существующие API/имена переменных. Имена можно изменить при реализации.

```ini
[FriendlyFire]
; Главный переключатель
bEnableSummonProtection=true

; Какие призывы считать защищёнными целями
bProtectPlayerOwnedSummons=true
bProtectAllyOwnedSummons=false

; От каких источников защищать
bFilterPlayerAttacks=true
bFilterPlayerAllies=true
bFilterPlayerOwnedSummons=true
bFilterAlliedSummons=true

; Защита от урона: не означает физическое прохождение атаки насквозь
bBlockMeleeDamage=true
bBlockArrowDamage=true
bBlockPhysicalProjectileDamage=true
bBlockMagicProjectileDamage=true
bBlockConcentrationDamage=true
bBlockAreaMagicDamage=true
bBlockShoutDamage=true
bBlockEnchantmentDamage=true

; Физическое/боевое прохождение насквозь — экспериментальные возможности
; По умолчанию выключены до прохождения соответствующих тестов
bMeleePassThrough=false
bArrowPassThrough=false
bPhysicalProjectilePassThrough=false
bMagicProjectilePassThrough=false

; Дополнительные последствия попаданий
bSuppressFriendlyAggression=true
bSuppressFriendlyKillmoves=true
bSuppressFriendlyHitReactions=false
```

### Семантика настроек

- `bEnableSummonProtection=false` отключает всю подсистему без удаления записей `SummonRegistry`.
- `bBlock*Damage` означает, что соответствующий эффект/урон не должен примениться к защищённой цели.
- `b*PassThrough` означает, что защищённая цель по возможности не должна завершать обработку атаки. Это более строгая функция, чем блокирование урона.
- `bBlockAreaMagicDamage` фильтрует защищённого актора как получателя AoE. Это не означает, что взрыв физически проходит через актёра.
- `bSuppressFriendlyAggression` предотвращает нежелательные переходы в combat/hostility, насколько это возможно в выбранном hook path.
- `bSuppressFriendlyHitReactions` оставить выключенным по умолчанию до проверки: подавление recoil/stagger может конфликтовать с анимациями, другими combat-модами и ожиданиями игрока.

Настройки должны быть доступны через INI на первом этапе. MCM можно добавить позднее, используя те же внутренние значения.

---

## FF-01.5. Архитектура подсистемы

Не помещать всю логику в `CollisionManager`. Физическая коллизия актёров, hit detection атаки и применение магического эффекта — разные уровни движка.

Предлагаемая структура:

```text
FriendlyFire/
├── RelationshipClassifier.h/.cpp
├── ProtectionPolicy.h/.cpp
├── FriendlyFireSettings.h/.cpp
├── AttackFilterManager.h/.cpp
├── MeleeFilter.h/.cpp
├── ProjectileFilter.h/.cpp
├── MagicFilter.h/.cpp
├── ExplosionFilter.h/.cpp
├── HostilityFilter.h/.cpp
├── HitReactionFilter.h/.cpp
└── Diagnostics.h/.cpp

Integrations/
├── PrecisionBackend.h/.cpp
├── NoFollowerAttackCollisionStudy.md
└── NOFFStudy.md
```

Назначение компонентов:

| Компонент | Ответственность |
|---|---|
| `RelationshipClassifier` | Классифицирует атакующего и защищённую цель; строит отношения по faction/command chain |
| `ProtectionPolicy` | Принимает решение, нужно ли фильтровать конкретную пару source → target для конкретного типа атаки |
| `FriendlyFireSettings` | Хранит и валидирует настройки INI/MCM |
| `AttackFilterManager` | Маршрутизирует проверки в конкретные adapters |
| `MeleeFilter` | Ванильный melee, sweeping attacks и Precision melee |
| `ProjectileFilter` | Стрелы, болты и физические projectile collision/impact |
| `MagicFilter` | Прямые spell effects, concentration, staves, enchantments и shouts |
| `ExplosionFilter` | Фильтрация получателей AoE/explosion effects |
| `HostilityFilter` | Предотвращает нежелательные combat/hostility side effects |
| `HitReactionFilter` | Отдельная экспериментальная обработка stagger, recoil и killmove |
| `Diagnostics` | Логирует source, target, attack kind, принятое решение и backend |

Все hot hooks должны выполнять только короткую проверку и не делать тяжёлую работу, выделение памяти или подробное логирование на каждый вызов. Решение лучше строить на заранее подготовленных/кэшированных данных, если lifetime и поток вызова это позволяют.

---

## FF-01.6. Существующие проекты и что из них изучать

### 1. No Follower Attack Collision Plus

- [Страница мода на Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/190215)
- Актуальная проверенная страница: версия **3.3**, обновление **22 октября 2026 года**.

По описанию и changelog:

- основан на No Follower Attack Collision;
- блокирует hit processing по дружественным целям;
- добавляет защиту от магического урона;
- заявляет поддержку Precision (добавлена в 3.1);
- поддерживает вложенные цепочки follower/summon ownership (3.2);
- предотвращает killmove по защищённому союзнику (3.3);
- имеет настройку отключения защиты вне боя.

**Как использовать в работе над SCF:**

1. Установить как baseline в отдельном тестовом профиле MO2.
2. Проверить защиту наших vanilla summons от игрока и союзных NPC.
3. Проверить стрелы, melee, Precision, magic damage, AoE, shouts, staves и concentration spells отдельно.
4. Посмотреть, останавливается ли физический projectile после контакта с защищённым summon и попадает ли в противника за ним.
5. Изучить исходники версии, соответствующей тестируемой версии мода, и определить, какие hooks/offsets и условия классификации используются.
6. Выяснить, есть ли подходящий публичный API. Не считать, что наличие готового SKSE-плагина автоматически означает наличие backend-интерфейса для SCF.
7. До копирования кода проверить актуальную лицензию, разрешения на заимствование и совместимость с выбранным CommonLib.

**Ограничение:** описание «skip hit functions» и поддержка Precision подтверждают подавление регистрации/обработки дружественных попаданий, но сами по себе не доказывают сквозное продолжение атаки до следующей цели. Это отдельный приёмочный тест.

### 2. Original No Follower Attack Collision

- [GitHub — LXIV-CXXVIII/No-Follower-Attack-Collision](https://github.com/LXIV-CXXVIII/No-Follower-Attack-Collision)
- [Nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/65037)

Изучать как более простой reference для пропуска hit processing по дружественным целям. В исходной версии описаны melee, sweeping attacks и arrows; магическая защита в оригинальном описании отсутствовала.

### 3. NOFF — No Offensive Friendly Fire

- [GitHub — feelixs/noff-skse](https://github.com/feelixs/noff-skse)
- [Nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/54527)

NOFF — особенно полезный reference по magic/effect path. README описывает:

- `MagicTarget::AddTarget` — применение magic effect к цели;
- `ValueModifierEffect::Update` — повторное применение эффекта концентрационного заклинания;
- `Explosion_ApplyEffects` — отдельный путь для взрывов;
- обработку enchantments, staves и shouts;
- отдельные hooks для подавления нежелательной hostility.

Ограничение самого NOFF: в README область действия описана как **player-cast spells**; NPC-cast spells не покрываются. Поэтому это не готовый backend для союзных NPC, а reference для исследования и расширения.

### 4. Precision

- [Precision source](https://github.com/ersh1/Precision)
- [PrecisionAPI.h](https://github.com/ersh1/Precision/blob/main/src/PrecisionAPI.h)

Для melee изучить:

- `PreHitCallback`;
- `PrecisionHitData`;
- `PreHitCallbackReturn::bIgnoreHit`;
- `AddPreHitCallback` и порядок вызова callback относительно hit function.

`bIgnoreHit = true` позволяет Precision проигнорировать конкретный hit. Однако публичная сигнатура сама по себе не гарантирует, что после игнорирования текущего hit Precision продолжит искать следующую цель за ним. Нужно проверить это в runtime на сценарии «защищённый summon перед вражеским NPC».

### 5. Accurate Projectile Collision

- [GitHub — SkyrimSE_AccurateProjectileCollision](https://github.com/Seb263/SkyrimSE_AccurateProjectileCollision)
- [Nexus page](https://www.nexusmods.com/skyrimspecialedition/mods/188551)

Проект расширяет collision/hitbox-механику Precision для стрел и магических снарядов, чтобы точнее определять попадания в конечности и части тела.

Использовать как reference по projectile hit detection и интеграции с Precision. Это **не готовое решение friendly pass-through**: задача проекта — точнее регистрировать попадания, а не пропускать дружественные цели.

---

## FF-01.7. Важные технические различия

### A. Melee damage suppression vs melee pass-through

Для vanilla melee и Precision melee могут существовать разные пути hit detection.

Проверить отдельно:

- блокируется ли только урон или весь hit processing;
- остаётся ли stagger/recoil;
- получает ли оружие/анимация реакцию на контакт;
- продолжает ли sweeping attack обрабатывать вторую цель;
- продолжает ли Precision проверять следующие collision candidates после `bIgnoreHit`.

Не считать один hook достаточным для vanilla и Precision.

### B. Projectile damage suppression vs projectile pass-through

Для стрел/болтов возможны разные результаты:

1. Урон заблокирован, но стрела застревает в призыве.
2. Hit event подавлен, но projectile всё равно получает impact и исчезает.
3. Collision с призывом полностью отфильтрован, projectile продолжает полёт.
4. Projectile проходит призыв, но система не регистрирует последующее попадание во врага.

Нужен именно результат №3 с корректным результатом №4. Поэтому исследовать collision filter, projectile impact handling и порядок target/impact resolution отдельно.

### C. Magic projectile vs AoE

Магический снаряд и его взрыв — не обязательно один hit path.

- Для projectile важны физическая collision и impact.
- Для explosion — список актёров, которым применяются эффекты.
- Для concentration — повторная обработка эффекта во времени.
- Для direct touch/target spells — путь применения Magic Effect.

Успешная защита от одного из этих типов не доказывает защиту от остальных.

### D. Не смешивать с Actor CollisionManager

`CollisionManager` из основного плана отвечает за столкновения тел актёров между собой и с окружением.

`FriendlyFire/AttackFilterManager` отвечает за атаки, hit processing и применение эффектов.

Они могут использовать общие низкоуровневые данные Havok/Precision, но должны оставаться отдельными подсистемами, чтобы не отключить столкновения призыва с врагами или окружением ради блокировки дружественного урона.

---

## FF-01.8. План реализации

Этот workstream расширяет существующий **Этап 5 — Friendly Fire и Stealth** основного `PLAN.md`. Не требуется менять общую нумерацию этапов.

### FF-Stage A — Baseline и классификация

- [ ] Установить No Follower Attack Collision Plus 3.3 в отдельном профиле тестирования.
- [ ] Составить матрицу тестов: player → summon, ally → summon, summon → summon, enemy → summon.
- [ ] Проверить vanilla melee, sweeping attacks, arrows, Precision melee, magic, AoE, concentration, shouts и enchantments.
- [ ] Зафиксировать, какие воздействия только не наносят урон, а какие физически проходят через цель.
- [ ] Изучить source code NFAC Plus и его механизм определения союзников/владельцев.
- [ ] Реализовать `RelationshipClassifier` и покрыть его чистыми unit-тестами.

**Результат:** воспроизводимая база сравнения и независимая классификация разрешённых источников атак/защищённых целей.

### FF-Stage B — Damage Protection MVP

- [ ] Добавить главный переключатель `bEnableSummonProtection`.
- [ ] Защищать player-owned summons от атак игрока.
- [ ] Добавить отдельную настройку для атак player allies.
- [ ] Добавить фильтрацию ванильных melee и arrows на подтверждённых hit-processing paths.
- [ ] Добавить Precision `PreHitCallback` integration, если Precision установлен.
- [ ] Исследовать и реализовать защиту от player-cast hostile magic по reference NOFF.
- [ ] Отдельно протестировать concentration, explosion/AoE, shouts, staves и enchantments.
- [ ] Не блокировать non-hostile magic effects без явной настройки.
- [ ] Предотвратить нежелательную hostility/агрессию без глобального подавления всех `TESHitEvent`.

**Результат:** защищённые призывы не получают дружественный урон в проверенных сценариях; неизвестные пути помечены как неподдерживаемые, а не считаются автоматически исправленными.

### FF-Stage C — True Melee Pass-Through

- [ ] Проверить, продолжает ли vanilla melee/sweep обработку следующих целей после пропуска дружественного актора.
- [ ] Проверить Precision: продолжает ли hit detection после `bIgnoreHit`.
- [ ] Если нет — исследовать фильтрацию attack collision candidates до hit processing.
- [ ] Убедиться, что один удар может поразить врага за призывом без двойного урона и повторных hit events.
- [ ] Проверить одноручное, двуручное оружие, dual wield, power attacks и sweeping attacks.

**Результат:** дружественный summon не блокирует melee hit detection, а противник за ним получает нормальное попадание.

### FF-Stage D — True Projectile Pass-Through

- [ ] Исследовать collision filtering для arrows/bolts и разных projectile forms.
- [ ] Проверить, можно ли исключить защищённого актора до impact, не уничтожая projectile.
- [ ] Проверить прохождение снаряда через summon и последующее попадание во врага.
- [ ] Отдельно проверить физические и магические projectile.
- [ ] Проверить projectile mods и Accurate Projectile Collision как дополнительный reference.
- [ ] Не менять глобальные collision layers без доказательства, что это не затронет врагов, окружение и другие моды.

**Результат:** стрелы/снаряды игрока и разрешённых союзников проходят через защищённые призывы и могут попадать в цели за ними.

### FF-Stage E — Reactions, compatibility и settings

- [ ] Решить отдельно, нужно ли подавлять hit reactions и stagger.
- [ ] Предотвратить killmove по защищённым призывам.
- [ ] Проверить friendly-fire цепочки между summons/followers союзников.
- [ ] Добавить все настройки в INI.
- [ ] При наличии MCM-инфраструктуры вывести основные настройки в меню.
- [ ] Проверить конфликты с Precision, NFAC Plus, NOFF, combat mods и projectile mods.
- [ ] Обеспечить безопасное отключение hooks/подсистемы при неподдерживаемом runtime.

**Результат:** настраиваемая и диагностируемая подсистема с понятной совместимостью и graceful fallback.

---

## FF-01.9. Приёмочные тесты

### Защита от урона

- [ ] При включённой защите обычный melee игрока не наносит урон player-owned summon.
- [ ] Sweeping/power attacks игрока не наносят урон защищённым призывам.
- [ ] Стрелы и болты игрока не наносят урон защищённым призывам.
- [ ] Разрешённые атаки союзных NPC не наносят урон защищённым призывам.
- [ ] Другие summons в разрешённой command chain не наносят урон защищённым призывам.
- [ ] Hostile NPC и hostile summons продолжают наносить обычный урон.
- [ ] Healing и non-hostile buffs продолжают работать, если отдельная настройка не говорит обратного.
- [ ] Защита от direct magic, concentration, AoE, shouts, staves и enchantments проверяется отдельными сценариями.

### Реальное прохождение насквозь

- [ ] Melee hit проходит сквозь защищённый призыв и попадает во врага позади него.
- [ ] Sweeping attack может корректно поразить несколько врагов, не останавливаясь на защищённом призыве.
- [ ] Стрела проходит через защищённый призыв и попадает во врага позади него.
- [ ] Болт арбалета проходит через защищённый призыв и попадает во врага позади него.
- [ ] Физический projectile продолжает полёт после контакта с защищённым призывом.
- [ ] Магический projectile не взрывается/не завершается на защищённом призыве, если включён соответствующий pass-through режим.
- [ ] Взрыв AoE не наносит урон защищённому призыву, но продолжает воздействовать на подходящих врагов в радиусе.

### Регрессия и совместимость

- [ ] Вражеские melee/projectile/magic атаки по призыву работают как раньше.
- [ ] Удары игрока по hostile NPC не фильтруются.
- [ ] `bEnableSummonProtection=false` полностью возвращает стандартную обработку атак.
- [ ] При выключенном `bMeleePassThrough`/`bArrowPassThrough` защита от урона может оставаться включённой независимо.
- [ ] Нет бесконечных hit events, двойного урона, повторного impact или застрявших projectile.
- [ ] Не ломаются ragdoll, смерть, killmove и переходы между cell.
- [ ] Не возникает конфликтов с Precision или другим установленным combat plugin.

---

## FF-01.10. Риски и ограничения

| Риск | Мера |
|---|---|
| Hit suppression не приводит к продолжению атаки | Проверять Damage Protection и Attack Pass-Through как разные критерии |
| Precision использует собственную melee hit detection | Отдельно подключить и протестировать Precision API; не полагаться только на vanilla hooks |
| `bIgnoreHit` может не продолжить поиск следующей цели | Проверить runtime; при необходимости фильтровать кандидатов до hit resolution |
| Стрела получает impact и исчезает даже без урона | Исследовать projectile collision до impact, а не только damage handler |
| Магические projectile и AoE идут разными путями | Раздельные adapters и отдельные тесты |
| NOFF не покрывает NPC-cast spells | Использовать как reference; расширять только после исследования источника NPC spell и его thread/context |
| Faction/ownership chain классифицируется неверно | Отдельный classifier, cycle/depth guard, Unknown = не фильтровать по умолчанию |
| Подавление hit reaction ломает combat animation | Держать настройку отдельной и выключенной до тестов |
| Hooks конфликтуют с NFAC Plus/NOFF/Precision | Для каждого hook фиксировать владельца, runtime, порядок и совместимость; не подключать дублирующие перехваты вслепую |
| Код стороннего мода имеет несовместимую лицензию | Изучать как reference; перед заимствованием проверять LICENSE и permissions |

---

## FF-01.11. Решения на текущий момент

1. Добавить Friendly Fire Protection как отдельную подсистему SCF, а не расширение `CollisionManager`.
2. Сделать главный переключатель и независимые параметры по источникам атак/типам урона.
3. Разделять `Damage Protection` и `Attack Pass-Through` во внутренней архитектуре, INI/MCM и тестах.
4. Использовать NFAC Plus 3.3 как функциональный baseline и важный reference, но не считать его готовым встроенным backend без подтверждённого публичного API.
5. Использовать NOFF как reference для magic/effect/hostility paths, помня, что его README ограничивает scope player-cast spells.
6. Использовать Precision API для integration с melee hit processing, но отдельно доказать, что пропуск текущей цели позволяет удару попасть в следующую.
7. Считать полное прохождение стрел и магических projectile через защищённые актёры исследовательской задачей до появления работающего прототипа.
8. Не менять глобальные Havok collision layers только ради блокировки дружественного урона: сначала исследовать attack/projectile-specific фильтрацию.

---

## FF-01.12. Источники

- [No Follower Attack Collision Plus — Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/190215) — версия 3.3; описание friendly hit suppression, magic protection, Precision support и nested command chains.
- [No Follower Attack Collision — GitHub](https://github.com/LXIV-CXXVIII/No-Follower-Attack-Collision) — исходный SKSE-плагин для пропуска hit processing по дружественным целям.
- [NOFF — No Offensive Friendly Fire — GitHub](https://github.com/feelixs/noff-skse) — исходники magic/effect/hostility filtering.
- [Precision — GitHub](https://github.com/ersh1/Precision) — исходники и API.
- [PrecisionAPI.h](https://github.com/ersh1/Precision/blob/main/src/PrecisionAPI.h) — `PreHitCallback`, `PrecisionHitData`, `bIgnoreHit`.
- [Accurate Projectile Collision — GitHub](https://github.com/Seb263/SkyrimSE_AccurateProjectileCollision) — дополнительный reference для projectile collision/hitboxes; не реализация friendly pass-through.
- [Accurate Projectile Collision — Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/188551) — описание и changelog версии 2.0.1.
