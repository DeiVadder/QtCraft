# Contributing to QtCraft

Thanks for your interest! This page sums up how to build, test and submit a change. The complete working rules (in German) are in [CLAUDE.md](CLAUDE.md), which is also what the coding agents read; how the game works inside is in [docs/HowItWorks.md](docs/HowItWorks.md) and [docs/Architektur.md](docs/Architektur.md).

## Build and test

Set up the presets and build as described in [docs/Building.md](docs/Building.md). While you work:

```sh
scripts/verify.sh --quick --changed [--tests <regex>]   # debug build, the tests your change affects, static analysis of changed files
scripts/verify.sh --tsan-concurrent --changed           # also when you touch threads, networking, the server host, meshing or textures
```

Before a change is merged, the full `scripts/verify.sh` runs once (all tests under ASan/UBSan and TSan, release build, static analysis, smoke screenshots). New tests use port 0, keep their files in a temporary directory or their own data directory, and poll for conditions instead of sleeping for fixed times.

Changed content or handbook texts: run `scripts/export-handbook.sh` and commit the regenerated `docs/handbook` (`web/handbook/data` is not committed; the Pages workflow writes it). New user-interface texts: see “Texte und Übersetzungen” in [CLAUDE.md](CLAUDE.md).

## Branches and review

- Never commit directly to `dev` or `main`. Every change goes on its own feature branch and into `dev` through a reviewed merge request; releases go from `dev` to `main` with a final review.
- Commit only after the tests are green.
- New features also extend the showcase world (`--showcase`), so they can be seen and checked there.

## Code rules (summary)

- C++23 and Qt 6.8. No exceptions (`-fno-exceptions`): errors go through `std::optional`, `std::expected` or `QcResult`. Warnings are errors.
- Casts only as named casts (`static_cast`, `reinterpret_cast`, `const_cast`), never `T(x)` or `(T)x`, also for enums.
- Size limits, counted in code lines without comments and blank lines: functions ≤ 60 lines (aim for 30) and ≤ 5 parameters; `.cpp`/`.qml` files ≤ 600 lines, headers ≤ 220, test sources ≤ 750. When a limit is reached, split by responsibility – never squeeze statements together.
- No output parameters: prefer functions that return a value (or `std::optional`, a struct with named fields); if an object must change, make it a method of that object.
- A member variable used in more than two places gets a getter and a setter, and every use except the constructor and the accessors goes through them.
- A fixed set of values is an enum (`Q_ENUM` for QML), not a string. Setters that reject a value log a warning with the value and the reason.
- Includes name the path from `src/` (`#include "common/blockrules/QcRails.h"`); the layers of `src/` are checked by `scripts/fix-includes.py --check`.
- Every source file starts with the SPDX header (`SPDX-FileCopyrightText: 2026 the QtCraft authors` and `SPDX-License-Identifier: GPL-3.0-or-later`); `python3 scripts/add-spdx.py <file>` adds it, `scripts/verify.sh` and the pre-push hook check it.
- Comments describe the current state, not the history. Larger design choices are recorded in `docs/Entscheidungen.md`.

## Content and naming

- The wiring material is **Signal Dust**, not “Redstone”. Comparisons with Minecraft are descriptive only.
- No Mojang names for anything the player sees (blocks, items, creatures, places, texts): the game uses its own names, for example the Shadewalker. Code comments and design notes may name the role model to describe a behaviour.
- No Minecraft (Mojang) code, textures, sounds or other assets. Textures, sounds and music are made by the project itself; third-party assets need a free licence and an entry in `docs/ASSETS.md` and `CREDITS.md`.

## Licence of contributions

QtCraft is licensed under the GNU General Public License version 3 or later (`LICENSE`). By submitting a change you agree that it is licensed under the same terms, and you confirm that you have the right to do so. Third-party code, fonts or assets need a GPL-compatible licence and an entry in `THIRD_PARTY.md` (code, fonts) or `docs/ASSETS.md` and `CREDITS.md` (assets); their licence texts go into `LICENSES/` (licences of files in the repository) or `licenses-third-party/` (texts only the release packages need) unless the component brings its own.

## Commit messages

English, describing what changed and why. No links to external sites and no tool attribution lines.
