#!/bin/sh
# Builds the SDL3 submodule (3rdparty/SDL) once and installs it into a cache shared by every preset
# and every worktree (decision E-540): <cache>/<tag>-<arch>-<config>, where <cache> is $QC_SDL_CACHE
# or ~/.cache/qtcraft/sdl3, <tag> the line of 3rdparty/sdl3-tag.txt, <arch> the machine (`uname -m`),
# the architecture of --arch or "universal" (arm64 and x86_64, for the release bundle; on Windows
# the target of the MSVC environment, VSCMD_ARG_TGT_ARCH, e.g. arm64 or x64), and <config>
# the version of this script's build options (CONFIG_VERSION below; raised whenever the options
# change, so an entry built with other options is never reused). Only the joystick, gamepad, HIDAPI
# (without libusb), haptic and sensor parts are built (no audio, video, GPU, render, camera, dialog,
# tray), as a static library, Release, without sanitizers. A finished entry has the file .complete
# and is never built again. The configure step of QtCraft runs this script itself when SDL3 is
# missing, so it is rarely needed by hand.
#
# Usage: scripts/build-sdl.sh [--universal | --arch <arch>] [--debug-runtime] [--print-prefix]
#   --universal     build arm64 and x86_64 in one library (macOS)
#   --arch <arch>   build for this one architecture (macOS, e.g. x86_64 on an arm64 machine)
#   --debug-runtime MSVC: link against the debug C runtime (/MDd) like a Debug build of the game;
#                   SDL itself stays optimised, the entry gets the suffix "-mdd" (decision E-861)
#   --print-prefix  only print the cache directory of this tag and architecture and exit
# Environment: QC_SDL_CACHE (cache root), QC_SDL_MAKE_PROGRAM (path of ninja; otherwise ninja from
# PATH or ~/Qt/Tools/Ninja/ninja, else make), QC_SDL_WAIT_BENCH_LOCK=1 (wait while the benchmark
# lock ~/.cache/qtcraft/bench.lock exists; set by the configure step). Exit code 0: the prefix
# exists (printed last on stdout), 1: the build failed, 2: wrong use or missing submodule.

set -u

# Raise this when the CMake options below change: the cache key then names a new entry.
CONFIG_VERSION=c2

usage() {
    echo "usage: scripts/build-sdl.sh [--universal | --arch <arch>] [--debug-runtime] [--print-prefix]" >&2
    exit 2
}

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd) || exit 2
SDL_DIR="$PROJECT_DIR/3rdparty/SDL"
TAG=$(tr -d '[:space:]' <"$PROJECT_DIR/3rdparty/sdl3-tag.txt") || exit 2
if [ -z "$TAG" ]; then
    echo "error: 3rdparty/sdl3-tag.txt is empty" >&2
    exit 2
fi
UNIVERSAL=0
DEBUG_RUNTIME=0
PRINT_ONLY=0
ARCH=$(uname -m)
# Windows (Git Bash): uname names the architecture of the shell, which runs emulated as x86_64 on an
# ARM64 machine; the target of the MSVC environment (vcvarsall) is the one that counts.
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) [ -n "${VSCMD_ARG_TGT_ARCH:-}" ] && ARCH=$VSCMD_ARG_TGT_ARCH ;;
esac
EXPLICIT_ARCH=""
while [ $# -gt 0 ]; do
    case "$1" in
        --universal) UNIVERSAL=1 ;;
        --print-prefix) PRINT_ONLY=1 ;;
        --debug-runtime) DEBUG_RUNTIME=1 ;;
        --arch)
            [ $# -ge 2 ] || usage
            EXPLICIT_ARCH=$2
            shift
            ;;
        *) usage ;;
    esac
    shift
done
if [ -n "$EXPLICIT_ARCH" ]; then
    [ "$UNIVERSAL" -eq 1 ] && usage
    case "$EXPLICIT_ARCH" in
        *[!A-Za-z0-9_]*) echo "error: invalid architecture '$EXPLICIT_ARCH'" >&2; exit 2 ;;
    esac
    ARCH=$EXPLICIT_ARCH
fi
[ "$UNIVERSAL" -eq 1 ] && ARCH=universal

KEY="$TAG-$ARCH-$CONFIG_VERSION"
[ "$DEBUG_RUNTIME" -eq 1 ] && KEY="$KEY-mdd"
CACHE_ROOT=${QC_SDL_CACHE:-"${XDG_CACHE_HOME:-$HOME/.cache}/qtcraft/sdl3"}
# Windows: a path like C:/Users/... that CMake and the native tools read as well as Git Bash.
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*) CACHE_ROOT=$(cygpath -m "$CACHE_ROOT") ;;
esac
PREFIX="$CACHE_ROOT/$KEY"
if [ "$PRINT_ONLY" -eq 1 ]; then
    echo "$PREFIX"
    exit 0
fi
if [ -f "$PREFIX/.complete" ]; then
    echo "$PREFIX"
    exit 0
fi

# The submodule must be checked out at the tag of sdl3-tag.txt (compared through SDL_version.h, as
# a shallow checkout need not know the tag name).
if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
    echo "build-sdl: checking out the submodule 3rdparty/SDL" >&2
    git -C "$PROJECT_DIR" submodule update --init --depth 1 3rdparty/SDL >&2 || {
        echo "error: 3rdparty/SDL is missing; run: git submodule update --init 3rdparty/SDL" >&2
        exit 2
    }
fi
VERSION_H="$SDL_DIR/include/SDL3/SDL_version.h"
version_part() { sed -n "s/^#define SDL_$1_VERSION *\([0-9]*\).*/\1/p" "$VERSION_H"; }
FOUND="release-$(version_part MAJOR).$(version_part MINOR).$(version_part MICRO)"
if [ "$FOUND" != "$TAG" ]; then
    echo "error: 3rdparty/SDL is $FOUND, 3rdparty/sdl3-tag.txt wants $TAG; run: git submodule update 3rdparty/SDL" >&2
    exit 2
fi

# The configure step does not build while a benchmark measures (CLAUDE.md, benchmark lock).
. "$PROJECT_DIR/scripts/cache-entry-lock.sh"
[ "${QC_SDL_WAIT_BENCH_LOCK:-0}" = 1 ] && wait_for_bench_lock build-sdl

# One build per entry at a time: parallel configure steps (presets, worktrees) wait here.
mkdir -p "$CACHE_ROOT" || exit 1
LOCK="$CACHE_ROOT/.lock-$KEY"
WORK=""
cache_lock_acquire

if [ -f "$PREFIX/.complete" ]; then
    echo "$PREFIX"
    exit 0
fi
# Leftovers of builds that were killed before their trap ran.
rm -rf "$CACHE_ROOT/.build-$KEY".* "$LOCK".stale.*
WORK=$(mktemp -d "$CACHE_ROOT/.build-$KEY.XXXXXX") || exit 1

GENERATOR="Unix Makefiles"
MAKE_PROGRAM=${QC_SDL_MAKE_PROGRAM:-$(command -v ninja || true)}
[ -z "$MAKE_PROGRAM" ] && [ -x "$HOME/Qt/Tools/Ninja/ninja" ] && MAKE_PROGRAM="$HOME/Qt/Tools/Ninja/ninja"
case "$MAKE_PROGRAM" in
    *ninja*) GENERATOR=Ninja ;;
    *) MAKE_PROGRAM="" ;;
esac
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

set -- -S "$SDL_DIR" -B "$WORK/build" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$WORK/install" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_INSTALL=ON -DSDL_INSTALL_CPACK=OFF -DSDL_INSTALL_DOCS=OFF -DSDL_UNINSTALL=OFF \
    -DSDL_AUDIO=OFF -DSDL_VIDEO=OFF -DSDL_GPU=OFF -DSDL_RENDER=OFF -DSDL_CAMERA=OFF \
    -DSDL_DIALOG=OFF -DSDL_TRAY=OFF -DSDL_POWER=OFF \
    -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_HIDAPI=ON -DSDL_HIDAPI_LIBUSB=OFF -DSDL_SENSOR=ON \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
[ -n "$MAKE_PROGRAM" ] && set -- "$@" -DCMAKE_MAKE_PROGRAM="$MAKE_PROGRAM"
# A Debug build of the game links the debug C runtime of MSVC; a static SDL must use the same one.
[ "$DEBUG_RUNTIME" -eq 1 ] && set -- "$@" -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL
if [ "$(uname -s)" = Darwin ]; then
    # The same lowest macOS as the game (CMakeLists.txt), so linking gives no version warnings.
    set -- "$@" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3
    [ "$UNIVERSAL" -eq 1 ] && set -- "$@" "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
    [ -n "$EXPLICIT_ARCH" ] && set -- "$@" "-DCMAKE_OSX_ARCHITECTURES=$EXPLICIT_ARCH"
fi

echo "build-sdl: building SDL3 $TAG ($ARCH, $CONFIG_VERSION) into $PREFIX (once)" >&2
cmake "$@" >"$WORK/configure.log" 2>&1 || { cat "$WORK/configure.log" >&2; exit 1; }
cmake --build "$WORK/build" --parallel "$JOBS" >"$WORK/build.log" 2>&1 || { tail -50 "$WORK/build.log" >&2; exit 1; }
cmake --install "$WORK/build" >"$WORK/install.log" 2>&1 || { cat "$WORK/install.log" >&2; exit 1; }

# The licence texts the release bundle carries (THIRD_PARTY.md): SDL's zlib licence, HIDAPI under
# its BSD option, and the notice of the SunPro maths routines in src/libm.
LIBM_NOTICE=$(sed -n '/Copyright (C) 1993 by Sun Microsystems/,/is preserved/s/^ \* \{0,1\}//p' "$SDL_DIR/src/libm/s_sin.c")
if ! cp "$SDL_DIR/LICENSE.txt" "$WORK/install/LICENSE.txt" ||
    ! cp "$SDL_DIR/src/hidapi/LICENSE-bsd.txt" "$WORK/install/HIDAPI-LICENSE.txt" ||
    [ -z "$LIBM_NOTICE" ] || ! printf '%s\n' "$LIBM_NOTICE" >"$WORK/install/LIBM-NOTICE.txt"; then
    echo "error: cannot copy the licence texts of SDL3" >&2
    exit 1
fi
echo "$TAG $ARCH $CONFIG_VERSION" >"$WORK/install/.complete"

# Moved into place in one step, so a reader never sees a half-installed entry. Checked again right
# before: a finished entry is never replaced.
if [ -f "$PREFIX/.complete" ]; then
    echo "$PREFIX"
    exit 0
fi
rm -rf "$PREFIX"
mv "$WORK/install" "$PREFIX" || exit 1
[ -f "$PREFIX/.complete" ] || { echo "error: $PREFIX is incomplete after the move" >&2; exit 1; }
echo "build-sdl: done" >&2
echo "$PREFIX"
