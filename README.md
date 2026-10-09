# QtCraft

A Minecraft-like voxel sandbox built with C++23 and Qt 6.8: procedurally generated worlds, mining and building, crafting, smelting in furnaces, hunger, creatures, day and night, flowing water and lava, falling sand, plants, fireflies and mine lamps, synthesized sound and music, an in-game handbook generated from the content, saved worlds, data-only mods and a JSON control interface for agents and tests. Everything runs through a local server and a versioned binary protocol, so a network transport can be added later without touching the game logic.

![Day view at view distance 8](docs/screenshots/08-day-view-distance-8.png)

## Build

Requirements: CMake 3.25+, a C++23 compiler (Apple Clang 21 tested), Qt 6.8 with Quick, QuickControls2, Multimedia, ShaderTools, LinguistTools and Test. The presets look for Qt in `~/Qt/6.8.3/macos`; pass `-DCMAKE_PREFIX_PATH=<qt dir>` for another location.

```sh
cmake --preset release
cmake --build --preset release
```

Presets: `debug`, `release`, `asan` (Address + UndefinedBehavior sanitizers), `tsan` (Thread sanitizer). Build directories are `build/<preset>`.

## Play

```sh
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft
```

The main menu lists saved worlds, creates new ones (name, optional seed, whether the inventory is kept on death), and opens the settings, the character screen (outfit, hair, colours) and the mod list.

| Input | Action |
|-------|--------|
| Mouse | Look around (click into the window to capture the mouse) |
| W A S D, Space, Shift, Ctrl | Move, jump, sneak, sprint |
| Left mouse / F | Mine; left click on a creature attacks it |
| Right mouse / R | Place the held block (stairs, slabs, torches, doors … turn and attach by where you look and click), eat held food when hungry, open a crafting table, furnace or chest, open and close doors, trapdoors and gates (sneak to place against them); with an empty bucket take water or lava, with a full one pour it; with an empty glass bottle catch a firefly |
| Q, Ctrl+Q | Throw one item or the whole stack of the selected slot (walk over items to pick them up) |
| F5 | Switch the view: first person, third person behind, third person in front |
| 1–9, mouse wheel | Select a hotbar slot |
| E | Inventory with 2 × 2 crafting grid and recipe list |
| Ladders | Walk into the wall behind a ladder or jump to climb, sneak to hold on |
| Furnace screen | Something to smelt goes on top, fuel below, the result appears on the right; eat when the food bar right of the hearts runs low |
| Mine lamp | Clay (4, crafting table) → unfired lamp shell → furnace → fired shell + firefly in a bottle (fireflies glow over swamps at night) → lamp for floors, walls and ceilings |
| F1 | Handbook: every block, item, recipe, creature and biome with its values and links, plus chapters on playing (opens at the block in view; in the inventory, furnace and chests at the item under the mouse, or click the small ? on a slot) |
| T or Enter | Chat and commands (`/help`) |
| Esc | Pause menu (settings, save, quit to menu) |

Settings (view distance, field of view, mouse sensitivity, invert mouse, debug overlay, language, view mode, the character's look and the volumes of all sound, effects, music and ambience plus a mute switch) are stored in `settings.ini` in the data directory. The UI is available in English and German.

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

All commands are described in [docs/Steuerschnittstelle.md](docs/Steuerschnittstelle.md). `qtcraft-headless --export-sounds DIR` writes every sound of the content (and one music piece) as WAV files. `qtcraft-headless --export-handbook DIR [--language de]` writes the base game's handbook as Markdown (add `--mods-dir DIR` to include mods); the base game's handbook is in [docs/handbook/](docs/handbook/index.md) (German: [docs/handbook/de/](docs/handbook/de/index.md)) and a test keeps it in sync with the content.

## Tests and checks

```sh
ctest --preset debug
scripts/verify.sh
```

`scripts/verify.sh` builds debug, ASan/UBSan, TSan and release, runs all tests in each sanitizer build, runs clang-tidy, clazy and qmllint, and takes smoke screenshots. Set `QTCRAFT_DATA_DIR` to keep runs away from your real worlds; the tests always use `build/<preset>/test-data`.

## Performance and profiling

```sh
scripts/bench.sh [runs] [seconds]        # median per scenario, release build, view distance 8 (VIEW=12 for others)
scripts/perf-check.sh                    # fixed generous limits for stand, sprint and crowd; exit code 1 on a regression
scripts/profile.sh <tool> [scenario] [seconds] [view distance]
```

`profile.sh` builds the `profile` preset (`build/profile`, RelWithDebInfo with QML debugging) and runs one short benchmark under a tool: `breakdown` (frame cost per scenario without VSync: GUI and render thread CPU time, GPU time, uploads, entity meshing, meshing per section, memory, sections), `time` (Instruments Time Profiler plus a text report of the hottest functions per thread), `metal` (Metal System Trace), `alloc` (`heap` and `vmmap`), `leaks`, `sample`, `qml` (`qmlprofiler` plus a report of the slowest bindings), `sg` (scene graph timing log) and `visualize` (`QSG_VISUALIZE` screenshots). Scenarios: `stand`, `walk`, `sprint`, `crowd`, `light`, `liquids`, `inventory`, `furnace`, `handbook` (see `scripts/bench-common.sh`). Raw data goes to a temporary directory outside the repository. All scripted runs open the window on the built-in display and in the background (`QTCRAFT_SCREEN=builtin`, `QTCRAFT_BACKGROUND=1`): they never take the focus or capture the mouse; set `QTCRAFT_SCREEN` to another screen index or name to override. None of these is part of `ctest`; never run them while building or testing. Results and goals: [docs/Performance.md](docs/Performance.md).

Useful hooks: `--play [--seed N] [--world NAME]`, `--look yaw,pitch`, `--spawn x,z`, `--chat TEXT` (`@<ms> TEXT` delays it), `--overlay inventory|pause|settings|character|chat|furnace|chest|handbook`, `--handbook ENTRY|SEARCH`, `--camera first|behind|front`, `--pose walk|swing|swim|jump|sneak|death`, `--view-distance N`, `--screen builtin|INDEX|NAME`, `--background`, `--language de`, `--mods-dir DIR`, `--no-mods`, `--no-audio`, `QTCRAFT_AUDIO=0|1`, `QTCRAFT_SCREENSHOT=file.png` (silent unless `QTCRAFT_AUDIO=1`), `QTCRAFT_BENCHMARK=seconds` (`QTCRAFT_BENCHMARK_SCENARIO=sprint|walk|stand|light|liquids`). The full list is in [docs/Architektur.md](docs/Architektur.md).

## Documentation

- [docs/handbook/index.md](docs/handbook/index.md) – the game's handbook (generated)
- [docs/Architektur.md](docs/Architektur.md) – modules, threads, protocol, content, persistence, gameplay rules, options
- [docs/Steuerschnittstelle.md](docs/Steuerschnittstelle.md) – control interface
- [docs/Modding.md](docs/Modding.md), [docs/Mod-Sicherheit.md](docs/Mod-Sicherheit.md) – mods and their security (German)
- [docs/Performance.md](docs/Performance.md), [docs/Performance-Verlauf.md](docs/Performance-Verlauf.md) – frame cost, hotspots, memory, goals and the per-stage measurements (German)
- [docs/Entscheidungen.md](docs/Entscheidungen.md), [docs/FORTSCHRITT.md](docs/FORTSCHRITT.md), [docs/Abschlussbericht.md](docs/Abschlussbericht.md) – decisions, progress and final report (German)
- [docs/ASSETS.md](docs/ASSETS.md) – textures and sounds (all procedural)
