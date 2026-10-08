# QtCraft

A Minecraft-like voxel sandbox built with C++23 and Qt 6.8: procedurally generated worlds, mining and building, crafting, creatures, day and night, saved worlds, data-only mods and a JSON control interface for agents and tests. Everything runs through a local server and a versioned binary protocol, so a network transport can be added later without touching the game logic.

![Day view at view distance 8](docs/screenshots/08-day-view-distance-8.png)

## Build

Requirements: CMake 3.25+, a C++23 compiler (Apple Clang 21 tested), Qt 6.8 with Quick, QuickControls2, ShaderTools, LinguistTools and Test. The presets look for Qt in `~/Qt/6.8.3/macos`; pass `-DCMAKE_PREFIX_PATH=<qt dir>` for another location.

```sh
cmake --preset release
cmake --build --preset release
```

Presets: `debug`, `release`, `asan` (Address + UndefinedBehavior sanitizers), `tsan` (Thread sanitizer). Build directories are `build/<preset>`.

## Play

```sh
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft
```

The main menu lists saved worlds, creates new ones (name and optional seed), and opens the settings and the mod list.

| Input | Action |
|-------|--------|
| Mouse | Look around (click into the window to capture the mouse) |
| W A S D, Space, Shift, Ctrl | Move, jump, sneak, sprint |
| Left mouse / F | Mine; left click on a creature attacks it |
| Right mouse / R | Place the held block, eat held food, use a crafting table |
| 1–9, mouse wheel | Select a hotbar slot |
| E | Inventory with 2 × 2 crafting grid and recipe list |
| T or Enter | Chat and commands (`/help`) |
| Esc | Pause menu (settings, save, quit to menu) |

Settings (view distance, field of view, mouse sensitivity, invert mouse, debug overlay, language) are stored in `settings.ini` in the data directory. The UI is available in English and German.

## Mods

Mods are data-only content packs in `<data directory>/mods` (macOS: `~/Library/Application Support/QtCraft/QtCraft/mods`). Try the example mod in this repository:

```sh
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft --mods-dir mods
```

See [docs/Modding.md](docs/Modding.md) for the format and the boundary between core and mods.

## Control interface and headless mode

`qtcraft-headless` plays the game without a window and takes JSON-line commands on stdin (or from `--script file.jsonl`):

```sh
printf '%s\n' '{"cmd":"waitUntilReady"}' '{"cmd":"move","forward":1,"ticks":40}' '{"cmd":"status"}' '{"cmd":"quit"}' \
  | build/release/src/headless/qtcraft-headless --seed 42
```

All commands are described in [docs/Steuerschnittstelle.md](docs/Steuerschnittstelle.md).

## Tests and checks

```sh
ctest --preset debug
scripts/verify.sh
```

`scripts/verify.sh` builds debug, ASan/UBSan, TSan and release, runs all tests in each sanitizer build, runs clang-tidy, clazy and qmllint, and takes smoke screenshots. Set `QTCRAFT_DATA_DIR` to keep runs away from your real worlds; the tests always use `build/<preset>/test-data`.

Useful hooks: `--play [--seed N] [--world NAME]`, `--look yaw,pitch`, `--spawn x,z`, `--chat TEXT`, `--overlay inventory|pause|settings|chat`, `--view-distance N`, `--language de`, `--mods-dir DIR`, `--no-mods`, `QTCRAFT_SCREENSHOT=file.png`, `QTCRAFT_BENCHMARK=seconds`. The full list is in [docs/Architektur.md](docs/Architektur.md).

## Documentation

- [docs/Architektur.md](docs/Architektur.md) – modules, threads, protocol, content, persistence, gameplay rules, options
- [docs/Steuerschnittstelle.md](docs/Steuerschnittstelle.md) – control interface
- [docs/Modding.md](docs/Modding.md), [docs/Mod-Sicherheit.md](docs/Mod-Sicherheit.md) – mods and their security (German)
- [docs/Entscheidungen.md](docs/Entscheidungen.md), [docs/FORTSCHRITT.md](docs/FORTSCHRITT.md), [docs/Abschlussbericht.md](docs/Abschlussbericht.md) – decisions, progress and final report (German)
- [docs/ASSETS.md](docs/ASSETS.md) – textures (all procedural)
