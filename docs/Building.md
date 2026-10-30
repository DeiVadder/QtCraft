# Building QtCraft

How to build, package, test and measure QtCraft. Playing: [Playing.md](Playing.md). The modules, threads and the full list of command-line options and test hooks are in [Architektur.md](Architektur.md); the working rules for contributors (and for the coding agents) are in [CLAUDE.md](../CLAUDE.md) (German).

## Requirements

CMake 3.25+, Ninja, a C++23 compiler (Apple Clang 21 tested on macOS, MSVC 2022 on Windows) and Qt 6.8 (any Qt 6.8 release; the releases are built with 6.8.3) with Quick, QuickControls2, Multimedia, ShaderTools, LinguistTools and Test. Git (the SDL submodule `3rdparty/SDL`) and Python 3 (`scripts/*.py`). For encrypted multiplayer the configure step builds OpenSSL once, which needs `curl` and `perl` (see below).

## Presets

The local presets (`debug`, `release`, `deploy`, `profile`, `asan`, `tsan`) live in `CMakeUserPresets.json`, which is not in git: copy `CMakeUserPresets.example.json` to it and set the Qt and Ninja paths of your machine in its hidden preset `local` (the example uses `~/Qt/6.8.3/macos`; any Qt 6.8 release works). `scripts/ensure-user-presets.sh` creates the file when it is missing (a copy of the main checkout's file in a worktree, otherwise of the example) and switches on the git hooks of `tools/githooks`; every script that calls `cmake --preset` runs it first.

```sh
cp CMakeUserPresets.example.json CMakeUserPresets.json   # once; adjust the paths
cmake --preset release
cmake --build --preset release
```

Presets: `debug`, `release` (RelWithDebInfo), `asan` (Address + UndefinedBehavior sanitizers), `tsan` (Thread sanitizer), `profile` and `deploy` (used by the scripts below); `ci-debug`, `ci-deploy` and `ci-release` are the CI's presets (Qt from `QT_ROOT_DIR`; `ci-release` only on Windows, for the Windows package: Release with PDB files). Build directories are `build/<preset>`. All own targets are compiled with warnings as errors and without exceptions.

The presets `debug`, `asan` and `tsan` build the project libraries as shared libraries (`QC_SHARED_LIBS`, decision E-890), so the several hundred test programs load one copy of each library instead of linking their own: on this machine a debug tree takes about 40 GB, an ASan tree about 31 GB and a TSan tree about 26 GB. The sanitizer presets compile with line tables only (`-gline-tables-only`). `release`, `deploy`, `profile`, the CI presets and every Windows build stay static. A program that loads the QML module `QtCraft` links `qc_app_qml`.

**ccache.** When `ccache` is installed, every preset compiles through it (`QC_CCACHE`, on by default; `-DQC_CCACHE=OFF` or a `CMAKE_CXX_COMPILER_LAUNCHER` of your own turns it off). On macOS: `brew install ccache`. Settings used for the project's worktrees (`ccache --set-config <key>=<value>`):

| Setting | Value | Why |
|---------|-------|-----|
| `max_size` | `20G` | bounded because sanitizer and debug trees already need much disk |
| `compression` | `true` | more objects in the same space |
| `sloppiness` | `pch_defines,time_macros,include_file_mtime,include_file_ctime` | headers that git rewrites on a branch switch still hit |
| `base_dir` | the main checkout (e.g. `~/src/QtCraft`) | absolute paths below it are hashed relative, so every worktree under `.claude/worktrees/` shares hits |
| `hash_dir` | `false` | the working directory is not part of the hash, same reason |

Side effect of `base_dir`/`hash_dir`: `__FILE__` becomes relative, and debug, line-table and sanitizer-report paths in a cached object may point to the worktree that compiled the file first (possibly deleted since); configure with `-DQC_CCACHE=OFF` for a debugging session. `scripts/release.sh` always configures with `-DQC_CCACHE=OFF` and refuses a release tree with `QC_SHARED_LIBS=ON`. `ccache -s` shows hits and misses (`ccache -z` resets the counters). An existing build tree picks ccache up at its next `cmake --preset` (one full rebuild, because the compile command changes).

## SDL3 and OpenSSL

The first configure step also builds two libraries once into `~/.cache/qtcraft`, shared by every preset and checkout:

- **SDL3 for gamepads**, statically linked (from the submodule `3rdparty/SDL`, `scripts/build-sdl.sh`, about a minute). `-DQC_GAMEPAD=OFF` builds without.
- **OpenSSL for encrypted multiplayer** (`scripts/build-openssl.sh`: downloads the release tarball named in `3rdparty/openssl-version.txt`, checks its SHA-256 and builds the shared libraries, universal on macOS, a few minutes; it needs `curl` and `perl`; on Windows `scripts\windows\build-openssl.ps1`, see below). Linux builds use the OpenSSL 3 of the system instead (Debian/Ubuntu: `libssl-dev`). `-DQC_OPENSSL_ROOT=<prefix>` uses another OpenSSL build of the same version, `-DQC_TLS=OFF` builds without encryption (plain TCP, cannot join servers with TLS; only for platforms without OpenSSL yet).

## Linux

Linux is set up as an experimental CI job whose failures will not fail the run (see [Continuous integration](#continuous-integration)); it has not run yet, and a Linux build has never been played or packaged.

## Windows

`qtcraft` and `qtcraft-headless` build natively on Windows 11 ARM64 with MSVC 2022 and Qt 6.8 (`msvc2022_arm64`), with and without gamepads (decision E-861), and as an x64 cross build inside an ARM64 VM (`vcvarsall arm64_amd64`, Qt 6.8 `msvc2022_64`). The x64 build is packaged as a standalone zip (below) and has been played on Windows PCs against a dedicated server on a Mac, including multiplayer over TLS. Not done yet: the tests under MSVC (with `/W4 /WX` many test programs do not build yet), an installer and code signing. A build machine needs the following.

**Qt Online Installer, Qt 6.8 (any 6.8 release; the releases are built with 6.8.3):**

- **MSVC 2022 64-bit.** This is the x64 kit, used for x64 PCs. It is also the kit to choose inside an ARM64 Windows VM on Apple silicon when the game is meant for x64 PCs: MSVC cross-compiles for x64 and the x64 Qt tools run emulated.
- **MSVC 2022 ARM64.** Only needed for a native ARM64 build, for example to play inside the VM. Since Qt 6.8 it brings its own ARM64 host tools (`moc`, `qsb`, `lrelease`).
- **Additional Libraries → Qt Multimedia.** Sound output, with the FFmpeg backend. (A kit built from the sources without FFmpeg has only the Windows Media backend.)
- **Additional Libraries → Qt Shader Tools.** The `qt_add_shaders` step that bakes the shaders for D3D11/D3D12, Vulkan, OpenGL and Metal.
- **Qt Debug Information Files.** Recommended: they let crash dumps (`.dmp`) resolve Qt frames.
- **Developer and Designer Tools → CMake and Ninja.**
- **Already in the base package:** Core, Gui, Network, Concurrent, Qml, Quick, QuickControls2, Test and the Linguist tools (`lupdate`/`lrelease`). They need no separate selection.
- **Not needed:** Qt Quick 3D, WebEngine, Qt 5 Compatibility, Qt Creator and the sources.

**Outside Qt:**

- Visual Studio 2022 or its Build Tools, workload "Desktop development with C++", including:
  - MSVC v143 x64/x86 build tools,
  - MSVC v143 ARM64 build tools (for an ARM64 VM),
  - a Windows 11 SDK.
- Git for Windows, with Git Bash: it fetches the SDL submodule, and `scripts/*.sh` run in Git Bash. The configure step finds its `bash.exe` in `%ProgramFiles%\Git\bin` (or set `QC_SDL_SHELL`) and builds SDL3 for gamepads once, like on macOS (a Debug build gets its own copy with the debug C runtime), into `%USERPROFILE%\.cache\qtcraft\sdl3` (`QC_SDL_CACHE` moves it). Without Git Bash, configure with `-DQC_GAMEPAD=OFF`.
- Python 3: `scripts/*.py`.
- Strawberry Perl (with its NASM in `C:\Strawberry\c\bin`) on `PATH`: the configure step builds the bundled OpenSSL once with `scripts\windows\build-openssl.ps1` (about 10 minutes, into `%USERPROFILE%\.cache\qtcraft\openssl`, `QC_OPENSSL_CACHE` moves it) and copies `libcrypto-3-x64.dll` and `libssl-3-x64.dll` next to the programs.
- Qt's OpenSSL TLS plugin `plugins\tls\qopensslbackend.dll` in the kit. The installer's kits have it; a kit built from the sources needs `-openssl-runtime -- -DOPENSSL_ROOT_DIR=<the OpenSSL cache entry>` at configure time, otherwise the game finds only the `schannel` backend and refuses multiplayer ("Qt's OpenSSL backend is missing"). For an existing kit the plugin alone can be built in a separate qtbase build folder with the same options plus `-no-feature-dtls -no-feature-ocsp` (so its imports match the kit's `Qt6Network.dll`) and `cmake --build . --target QTlsBackendOpenSSLPlugin`, then copied into the kit (decision E-872).

The repository checks out every text file with LF (`.gitattributes`), also with `core.autocrlf=true`: the content packs are hashed byte for byte when a client joins a server, and CRLF files would make a Windows client differ from a macOS server. A checkout made before `.gitattributes` existed is renewed with `git rm -r --cached . && git reset --hard`. **This discards every uncommitted change in the checkout:** commit your work first.

**Build (ARM64, as in the Parallels VM):** create `CMakeUserPresets.json` next to `CMakePresets.json` (not in git; the Windows counterpart of `CMakeUserPresets.example.json`):

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "local",
            "hidden": true,
            "cacheVariables": {
                "CMAKE_PREFIX_PATH": "C:/Qt/6.8.3/msvc2022_arm64",
                "CMAKE_MAKE_PROGRAM": "C:/Qt/Tools/Ninja/ninja.exe",
                "CMAKE_C_COMPILER": "cl",
                "CMAKE_CXX_COMPILER": "cl"
            }
        },
        { "name": "release", "inherits": ["qc-release", "local"] }
    ],
    "buildPresets": [
        { "name": "release", "inherits": "qc-build", "configurePreset": "release", "jobs": 4 }
    ]
}
```

Then, in `cmd`:

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" arm64
set PATH=C:\Qt\Tools\CMake\bin;C:\Qt\Tools\Ninja;%PATH%
cmake --preset release
cmake --build --preset release --target qtcraft qtcraft-headless
```

`qc-release` is RelWithDebInfo, so `build\release` holds `qtcraft.exe` and `qtcraft-headless.exe` with their `.pdb` files for the minidumps. Prefer the CMake and Ninja of `C:\Qt\Tools` over others on `PATH` (Strawberry Perl brings a MinGW toolchain). For an x64 build use the x64 kit (`C:/Qt/6.8.x/msvc2022_64`) and `vcvarsall.bat x64` (on an x64 PC) or `arm64_amd64` (cross build in an ARM64 VM). To start the programs from the build folder, put the kit's `bin` on `PATH` (`set PATH=C:\Qt\6.8.3\msvc2022_arm64\bin;%PATH%`). The preset limits the build to 4 jobs, the cores of the VM (`qc-build` asks for 16).

**Standalone x64 zip (local tests only, see “Who may pass a package on” below):** `scripts/vm-package-windows.sh [git ref]` runs `scripts/windows/package-windows.ps1` in the Parallels VM over SSH and brings back `deploy/QtCraft-<version>-<commit>-Windows-x64.zip` (`windeployqt`, app-local VC++ runtime, OpenSSL, `README-Windows.txt`), plus a zip with the PDB files; it checks the unpacked copy (among others a server start with TLS and a bot that joins it). Details (in German): [Playtest.md](Playtest.md), section 3.

## Release: a standalone macOS app

```sh
scripts/release.sh
```

One command builds a universal (Apple silicon and Intel) Release build in `build/deploy` and puts a standalone `deploy/QtCraft.app` next to `deploy/QtCraft-<version>-macOS.zip` and `.dmg` (the `deploy/` folder is not in git). The app runs by double click on any Mac with macOS 13.3 or later, without Qt, build directory or command line. Inside are the Qt frameworks, QML modules and plugins (platform, image formats, styles, multimedia with the FFmpeg backend), OpenSSL in `Contents/Frameworks` with its licence in `Contents/Resources/licenses`, the base content and translations (built into the program), the example mod, the icon, which the build draws from the game's own textures, and the dedicated server `QtCraft.app/Contents/MacOS/qtcraft-headless` (start it from Terminal, see [Multiplayer.md](Multiplayer.md)). Saves, settings and mods stay in `~/Library/Application Support/QtCraft/QtCraft`; on the first start the example mod is copied to its `mods` folder switched off.

Requirements: the build requirements above, Xcode command line tools (`codesign`, `otool`, `lipo`, `iconutil`, `hdiutil`) and `macdeployqt` from the Qt installation (`QT_DIR` overrides the default, the newest `~/Qt/6.8.*/macos`). The script checks its result and exits with 1 if any check fails: links and rpaths of every binary (`otool`), the required parts and Info.plist keys, the signature, that game and dedicated server are universal, OpenSSL and its licence, a start of the copy's dedicated server (listens on a free loopback port, stops with exit code 0 on SIGTERM), and starts of a copy of the game in a temporary folder (a world with screenshot and the list of loaded libraries, which must all come from the copy or the system, the audio output, the example mod and the German texts, a start through Launch Services like a double click, and on Macs with Rosetta a start of the Intel half). Every start opens the (muted) audio output and keeps its caches in the temporary folder. `CFBundleVersion` is the UTC build time (`2026.1009.1130`). The starts open on the built-in display in the background and are muted. `QTCRAFT_RELEASE_DMG=0` skips the disk image, `QTCRAFT_RELEASE_PRESET` builds with another preset than `deploy` and `QTCRAFT_RELEASE_GUI=0` skips the starts of the game (machines without a display, such as the release workflow). It takes a few minutes and is not part of `scripts/verify.sh`.

The app is signed ad hoc only; what that means for the first start on another Mac is described in [Playing.md](Playing.md#starting-the-game).

**Who may pass a package on (decision E-932):** packages for others – public releases and playtest builds – come only from the release workflow on GitHub, built against open-source Qt 6.8.3. A package is local-only unless the Qt is declared open source: `QTCRAFT_RELEASE_OPEN_SOURCE_QT=1` for `scripts/release.sh` (the release workflow sets it), `-OpenSourceQt` for `scripts/windows/package-windows.ps1`. If the Qt installation holds commercial licence files (`Licenses/*ENTERPRISE*` or `*COMMERCIAL*`, or `licenseInfo.txt`, two folders above the Qt kit), the package stays local-only even then, with a warning; building and playing with a commercially licensed Qt works as usual. A local-only package – every package built outside the release workflow – is for your own tests and is never passed on: its names end in `-local-only` (`QtCraft-<version>-macOS-local-only.zip`/`.dmg`, `QtCraft-<version>-<commit>-Windows-x64-local-only.zip`), it carries `NOT-FOR-DISTRIBUTION.txt` and a `Qt-NOTICE.txt` without the LGPL. Each run removes old packages of both names.

**Replacing Qt or FFmpeg in the macOS bundle (LGPL):** macdeployqt rewrites the install names of the frameworks and libraries to `@rpath`/`@executable_path`, and the bundle is signed. A replaced framework or FFmpeg library must keep its install name (`install_name_tool -id` if needed), and the bundle must be signed again: `codesign --force --deep -s - QtCraft.app`.

Before a release: check the OpenSSL security advisories (openssl-library.org, “Vulnerabilities”) for the line in `3rdparty/openssl-version.txt`; when a newer patch release of that line is out, raise the version and SHA-256 there (from the release's `.sha256` file, compared with the one on the OpenSSL website) and let `scripts/build-openssl.sh` build it (decision E-872).

## Control interface and headless mode

`qtcraft-headless` plays the game without a window and takes JSON-line commands on stdin (or from `--script file.jsonl`):

```sh
printf '%s\n' '{"cmd":"waitUntilReady"}' '{"cmd":"move","forward":1,"ticks":40}' '{"cmd":"status"}' '{"cmd":"quit"}' \
  | build/release/src/headless/qtcraft-headless --seed 42
```

All commands are described in [Steuerschnittstelle.md](Steuerschnittstelle.md). `qtcraft-headless --server` runs a dedicated server ([Multiplayer.md](Multiplayer.md)); `--console` runs a server console: every line of stdin is a server command (`help`, `op`, `whitelist`, `backup`, `tick`, `stop`, …; see the handbook chapter Commands). `qtcraft-headless --export-sounds DIR` writes every sound of the content (and one music piece) as WAV files. `qtcraft-headless --export-handbook DIR [--language de]` writes the base game's handbook as Markdown (add `--mods-dir DIR` to include mods); the base game's handbook is in [handbook/](handbook/index.md) (German: [handbook/de/](handbook/de/index.md)), and `scripts/export-handbook.sh` regenerates it and the data of the web handbook (`web/handbook`); tests keep both in sync with the content.

The API reference (C++ libraries and QML types, decision E-939) is generated with QDoc from the `/*! … */` comments in the headers and QML files: `cmake --build --preset debug --target docs` writes `build/debug/docs/api/index.html` and lists qdoc's warnings, with their places in `src/`, in `build/debug/docs/qdoc-warnings.txt`. The target exists when CMake finds `qdoc` (from the Qt in use, or `-DQC_QDOC=<path>`); links to Qt's own types need Qt's documentation index files (`-DQC_QT_DOCS=<Docs/Qt-version>`, found next to the Qt installation by default). `scripts/convert-doc-comments.py` turns `//` comments into QDoc comments and lists what is still undocumented (`--check`).

## Tests and checks

```sh
ctest --preset debug
scripts/verify.sh --quick --changed [--tests <regex>]   # while working, a few minutes
scripts/verify.sh --tsan-concurrent --changed           # plus TSan on the concurrent tests
scripts/verify.sh [--full] [--sanitizers asan|tsan]     # once before a merge
```

`scripts/verify.sh --full` (the default) builds debug, ASan/UBSan, TSan and release, runs all tests in each sanitizer build, checks the size limits (function and file lengths, decision E-760) and runs clang-tidy, clazy and qmllint over the whole project, and takes smoke screenshots. `--quick` builds and tests only debug (`--tests` limits the tests to a CTest regular expression) and analyses only the C++, Objective-C++ and QML files changed against `dev` (committed on the branch or not; a changed header is analysed through its `.cpp` of the same name, a header without one only gets the length check). `--changed` runs only the tests whose inputs changed since they last passed. `--tsan-concurrent` adds a TSan build that runs the tests labelled `concurrency`. `--sanitizers asan|tsan` limits `--full` to one sanitizer configuration, or adds it to `--quick` when threading code changed. Every step prints its duration, and a table of all durations ends the run. Tests run in parallel, each with its own data directory `build/<preset>/test-data/<name>`; the tests labelled `timing` run afterwards on a quiet machine, and when the load does not fall in time they are deferred (listed at the end with one command per preset that runs them later; `QC_TIMING_UNDER_LOAD=1` runs them anyway). `scripts/static-analysis.sh [build-dir] [file ...]` runs the analysis alone. Set `QTCRAFT_DATA_DIR` to keep manual runs away from your real worlds. The full set of options is in the header of `scripts/verify.sh`; the rules for parallel runs (slot broker `scripts/qc-slot.py`) are in [CLAUDE.md](../CLAUDE.md).

## Performance and profiling

```sh
scripts/bench.sh [runs] [seconds]        # median per scenario, release build, view distance 8 (VIEW=12 for others)
scripts/bench-ab.sh <file> <rounds> <seconds> <label>=<app>[:TEXTURE_PACKS=<ids>]...  # interleaved A/B of several builds (or settings per label), order rotated per round, holds the bench lock
scripts/bench-headless.sh                # server values (tick, phases, memory) without a window
scripts/perf-check.sh                    # fixed generous limits for stand, sprint and crowd; exit code 1 on a regression
scripts/profile.sh <tool> [scenario] [seconds] [view distance]
```

`profile.sh` builds the `profile` preset (`build/profile`, RelWithDebInfo with QML debugging) and runs one short benchmark under a tool: `breakdown` (frame cost per scenario without VSync: GUI and render thread CPU time, GPU time, uploads, entity meshing, meshing per section, memory, sections), `time` (Instruments Time Profiler plus a text report of the hottest functions per thread), `metal` (Metal System Trace), `alloc` (`heap` and `vmmap`), `leaks`, `sample`, `qml` (`qmlprofiler` plus a report of the slowest bindings), `sg` (scene graph timing log) and `visualize` (`QSG_VISUALIZE` screenshots). Scenarios: see `scripts/bench-common.sh` (among them `stand`, `walk`, `sprint`, `crowd`, `light`, `liquids`, `inventory`, `furnace`, `handbook`, `showcase`). Raw data goes to a temporary directory outside the repository. All scripted runs open the window on the built-in display and in the background (`QTCRAFT_SCREEN=builtin`, `QTCRAFT_BACKGROUND=1`): they never take the focus or capture the mouse; set `QTCRAFT_SCREEN` to another screen index or name to override. None of these is part of `ctest`; never run them while building or testing. Results and goals (German): [Performance.md](Performance.md), [Performance-Verlauf.md](Performance-Verlauf.md).

Useful hooks: `--play [--seed N] [--world NAME]`, `--world-type terrain|flat|void|showcase` (world types, also in the main menu; `--game-mode survival|creative` for the player of `--play`; `--flat-layers bedrock,59*stone,3*dirt,grass_block` for flat worlds), `--showcase` (the showcase world with every block, creature and item; screenshot tour `scripts/showcase-tour.sh`), `--look yaw,pitch`, `--spawn x,z`, `--chat TEXT` (`@<ms> TEXT` delays it), `--overlay inventory|creative|pause|settings|debug|character|chat|furnace|chest|handbook` (steps `@<ms> fly`, `@<ms> text:<objectName>=<text>`), `--debug-display off|compact|full`, `--debug-show chunkBorders,hitboxes,…`, `--stats <ms>` / `QTCRAFT_STATS=<ms>` (debug data as JSON lines `STATS {…}` in the log), `--handbook ENTRY|SEARCH`, `--camera first|behind|front|side`, `--pose walk|swing|swim|jump|sneak|death|fly|flysprint`, `--preview animation=walk,side=back,zoom=0.5,gaze=0.8:0.3,time=0` (fixes what the character previews show), `QTCRAFT_PREVIEW_ACTIVE=1` (character previews animate in the inactive window of a scripted run, for benchmarks), `--view-distance N`, `--screen builtin|INDEX|NAME`, `--background`, `--language de`, `--mods-dir DIR`, `--no-mods`, `--no-audio`, `QTCRAFT_AUDIO=0|1`, `QTCRAFT_SCREENSHOT=file.png` (silent unless `QTCRAFT_AUDIO=1`), `QTCRAFT_OFFSCREEN=1` (render without a shown window, independent of the display; scripts do this for screenshots unless `QTCRAFT_SHOT_MODE=window`), `QTCRAFT_BENCHMARK=seconds` (`QTCRAFT_BENCHMARK_SCENARIO=sprint|walk|stand|light|liquids`). The full list is in [Architektur.md](Architektur.md) (“Build options and test hooks”).

## Continuous integration

GitHub Actions (`.github/workflows/`), all against open-source Qt 6.8.3 installed by `install-qt-action` (presets `ci-debug`, `ci-deploy` and `ci-release`, Qt from `QT_ROOT_DIR`):

- **CI** – every push to `dev` or `main` and every pull request: Debug build and all tests on macOS. Linux and Windows jobs are set up too, as experimental jobs whose failures do not fail the run (not run yet).
- **Pages** – `web/` from `main`: a start page, the web handbook (`/handbook/`) and the API reference (`/api/`, generated with the `qdoc` of that Qt; decision E-939).
- **Release** – a tag `v<version>` matching `project(QtCraft VERSION …)`: on macOS the tests, then `scripts/release.sh` without the starts of the game; on Windows (`windows-2025`, x64) `scripts/windows/package-windows.ps1 -Ci` (preset `ci-release`, Release like the macOS package, with PDB files; SDL3 and OpenSSL in their own steps, each cached as soon as it is built; `windeployqt`, app-local VC++ runtime, OpenSSL, licence texts and the checks of the script, among them a dedicated server of the unpacked copy with TLS and a bot that joins it; the Windows tests stay in the experimental CI job). The macOS `.zip` and `.dmg` and `QtCraft-<version>-Windows-x64.zip` with its `-pdb.zip` go to a draft release, headed by the release notes `docs/release-notes/<version>.md`, that a maintainer publishes after trying the packages once. Started by hand, the same workflow builds the packages without a release (next section).

**World generator checksums on new platforms:** `test_generator_versions` compares each generator version with checksums recorded per platform (`<kernel>-<cpu>`, `tests/data/generator-versions.json`, decision E-922). A platform listed under `"unrecorded"` (today `darwin-x86_64`, `linux-arm64`, `linux-x86_64`, `winnt-arm64`, `winnt-x86_64`) skips the comparison and prints its values in the test log; any other platform without values fails once its version is frozen. To record a platform, copy the printed values into the file (or run the test with `QC_RECORD_GENERATOR_REFERENCE=<version>` on that platform) and remove it from `"unrecorded"`; this is allowed once also for frozen versions. The macOS jobs compare with the `darwin-arm64` values recorded on the development Mac. **If the runner's values differ** (another macOS rounds differently), the fix is a platform key with the Darwin major version, never new values for an existing key: the key becomes e.g. `darwin25-arm64` (from `QSysInfo::kernelVersion()`), the existing `darwin-arm64` entry is renamed to the development Mac's key with byte-identical values, the runner's key (e.g. `darwin24-arm64`) goes under `"unrecorded"` and is recorded once from the CI log (decision E-940).

The workflows have not run on GitHub yet; the first run is part of the publication (stage 39). The Windows job of the release workflow is untested until then (decision E-934).

### Making a release

1. On `dev`: set the version in `project(QtCraft VERSION …)` in `CMakeLists.txt`, write the release notes `docs/release-notes/<version>.md` (English; features by area, known limitations, notes on worlds), and freeze the world generator: `QcGeneratorVersion::lastFrozen` = `current` (decision E-922; from then on a change of that version's output needs a new version). At a milestone the benchmark runs first, then the full `scripts/verify.sh --release`.
2. Open a merge request from `dev` to `main`; a human reviews, approves and merges it.
3. Wait for a green CI run on `main` (the macOS job runs all tests, among them `test_generator_versions` against the recorded checksums). Only then tag the merge commit `v<version>` and push the tag. The release workflow checks that the tag matches the CMake version, builds both packages and creates a draft release whose text starts with `docs/release-notes/<version>.md` (without the file it warns and writes only the generated part: checks, downloads, licences and the sources of Qt and FFmpeg).
4. Try the packages from the draft on a Mac and a Windows PC, then publish the draft. The push to `main` also publishes the handbook and the API reference (Pages workflow).

Once, before the first public release: rewrite the commit e-mail of the history and restore the push URL (`git remote set-url --push origin https://github.com/DeiVadder/QtCraft.git`), make the repository public (`gh repo edit --visibility public`; Pages needs a public repository on a free account), set Settings → Pages → Source to “GitHub Actions”, and set the website of the repository (`gh repo edit --homepage https://deivadder.github.io/QtCraft/`).

### Playtest builds

Packages for others – playtest builds as well as releases – come only from the release workflow, built against open-source Qt 6.8.3 (decision E-932). Packages from `scripts/release.sh` or `scripts/vm-package-windows.sh` on a local machine are for local tests only (`…-local-only`, with `NOT-FOR-DISTRIBUTION.txt`; see “Who may pass a package on” above); the Windows job of the workflow runs `package-windows.ps1 -Ci -OpenSourceQt` (decision E-934).

1. On GitHub: Actions → Release → Run workflow, choose the branch (normally `dev`) and, optionally, the server address that `README-Windows.txt` names (empty: “the address the game master names”). Or from a terminal: `gh workflow run release.yml --ref dev -f playtest_server='[2001:db8::1]:25600'`.
2. Wait for the run (macOS and Windows jobs, roughly 30–60 minutes; the first Windows run also builds OpenSSL and SDL3 into the Actions cache). A run started by hand creates no release.
3. Download the artifacts on the run's page (or `gh run download <run-id> -n QtCraft-Windows-x64`): `QtCraft-Windows-x64` holds `QtCraft-<version>-Windows-x64.zip` (GitHub wraps every artifact in one more zip: unpack the download once, pass on the inner zip), `QtCraft-Windows-x64-pdb` the PDB files for crash dumps (keep them, do not pass them on), `QtCraft-macOS` the macOS `.zip` and `.dmg`. Artifacts expire after 90 days.
4. The game version and commit are in `README-Windows.txt` and in the log headers; the playtest server must be built from the same commit.
