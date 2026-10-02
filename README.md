# DarkFox

Internal-чит для **Counter-Strike 2** на C++20 / MSVC.

Внутренняя DLL (инжектится в `cs2.exe`), собственный оверлей на D3D11 + ImGui,
меню на собственном UI-фреймворке `xui`/`xdraw`.

> **Примечание.** Проект публикуется «как есть», в образовательных целях.
> Использование в онлайн-матчах нарушает Steam Subscriber Agreement и ведёт к
> блокировке аккаунта.

---

## Возможности

**Rage**
- Рейджбот по логике Neverlose: 4 метрики точки (`point_metrics`) + компаратор
  `is_better_point` с ранними возвратами.
- Мульти-точки (head / body / safe), автомат по урону, лимиты хитчасти.
- Penetration-скан: урон по задетой группе (`get_max_damage`), а не по `hit.damage`.
- Предикт (`core/systems/impl/prediction.cpp`): `SetupMove` / `ProcessMovement` /
  `FinishMove` + `CPlayer_MovementServices`.
- Backtrack, no-spread, double-tap.

**Legit**
- Сайлент-аим, RCS с чтением отдачи из `CCSPlayer_AimPunchServices`
  (через схему, а не сигнатуру — не ломается на апдейтах).
- Триггербот, авто-пистолет.

**ESP**
- Игроки: бокс, скелет, имя, оружие, HP/армор, флаги, дистанция, рей (упреждение).
- Мир: дропы, гранаты, снаряды, бомба, импакты с иконками, звуки.
- Кастомная отрисовка через `xdraw` (собственный векторный рендерер).

**Movement**
- BHOP, авто-стрейф, edge-jump, jump-bug, fast-ladder, авто-ник.

**Misc**
- Changer (скины, модели, агенты, граффити, музыка килл-стрика).
- Гранады (траектория, помощник броска), таймер бомбы, hitmarker, hit-sound.
- Reaction-функции, авто-закупка, spectator-лист, пинг/фейклаг.

**Прочее**
- Авто-дамп при краше: `MiniDumpWriteDump` + мгновенный текстовый лог.
- Оптимизация рендера: cull объектов, замена текстур на картон.
- Встроенный конфиг-профиль по умолчанию (`core/defaults.hpp`).

---

## Требования

| Компонент | Версия |
|---|---|
| Windows | x64, 10/11 |
| MSVC | 14.44+ (toolset `v145`) |
| Windows SDK | 10.0.26100.0 |
| C++ | C++20 |
| vcpkg | для `freetype` |

Сторонние библиотеки лежат в `external/` (vendored): `nlohmann/json`, `phnt`,
`zydis`, `lz4`, `xdraw`, `stb_image`, `xorstr`, `poly2d`, `bc7`, `stackwalker`,
`inline-syscall`.

---

## Сборка

### 1. Окружение

Тулчейн подключается вручную (загрузчик `vcvars64.bat` не используется):

```bat
rem .env_build.bat
set "DARKFOX_INT_DIR=D:\DarkFoxInt"
set "DARKFOX_VS_DIR=D:\BuildTools"
set "DARKFOX_SDK_DIR=C:\Program Files (x86)\Windows Kits\10"
```

Правь пути под свою машину, затем подключай в консоли: `.env_build.bat`.

### 2. Сборка DLL

```bash
python build_cl.py Development   # конфиг передаётся ПОЗИЦИОННО
python build_cl.py Ship
```

Полная сборка — 10–50 мин, PCH ~248 МБ, требуется 2–3 ГБ свободной RAM.

### 3. Быстрая проверка одного файла

```bash
python chk2.py core/features/combat/impl/rage.cpp
```

### 4. Лоадеры

```bash
cd loader && ./build.bat
```

На выходе — 4 EXE в `bin/` (GDI и xui/D3D11, обычный и dev).

---

## Структура

```
core/
├── features/          # rage, legit, esp, movement, misc, changer, world
├── hooks/             # детуры (render_view, parse_report_hit, …)
├── systems/           # prediction, tracing, schemas, config
├── rendering/         # оверлей, меню, виджеты
└── defaults.hpp       # профиль конфига по умолчанию
protection/
├── patterns.cpp       # сигнатуры (pattern-scan) по модулям игры
└── patterns.hpp
loader/                # 4 лоадера + сборка
utilities/             # memory, perf, diag, shared_data, xdraw-обвязка
external/              # вендоренные зависимости
```

### Сигнатуры

Паттерны в `protection/patterns.cpp` сканируются по `client.dll`, `engine2.dll`,
`filesystem_stdio.dll`, `materialsystem2.dll`.

Формат: `??` — один байт, `*` — rip-relative, `>` — цель вызова, `^` —
абсолютный указатель, `~` — разыменование, `+NN`/`-NN` — пост-смещение.

После апдейта игры прогоняй аудит:

```bash
python .workbuddy-ai/tools/check_patterns.py
```

---

## Лицензия

MIT — см. [LICENSE](LICENSE).
