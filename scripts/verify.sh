#!/bin/sh
# Stage verification with two modes.
#   --full (default): debug build + tests, ASan/UBSan and TSan builds + tests, release build, static analysis
#                     of the whole project and time-limited smoke runs of the real game with screenshots.
#   --quick:          debug build, debug tests (optionally only those matching --tests) and static analysis
#                     of the files changed against dev (committed on this branch or not yet committed).
# --sanitizers asan|tsan limits --full to that sanitizer configuration, or adds it to --quick.
# Usage: scripts/verify.sh [--quick|--full] [--tests <regex>] [--sanitizers asan|tsan] [screenshot.png]

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2

MODE=full
TEST_REGEX=""
SANITIZERS="asan tsan"
SANITIZERS_GIVEN=0
SCREENSHOT="${TMPDIR:-/tmp}/qtcraft-smoke.png"
BASE_BRANCH=${QC_VERIFY_BASE:-dev}

usage() {
    echo "usage: scripts/verify.sh [--quick|--full] [--tests <regex>] [--sanitizers asan|tsan] [screenshot.png]" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
    --quick) MODE=quick ;;
    --full) MODE=full ;;
    --tests)
        [ $# -ge 2 ] || usage
        TEST_REGEX=$2
        shift
        ;;
    --sanitizers)
        [ $# -ge 2 ] || usage
        case "$2" in
        asan | tsan) SANITIZERS=$2 ;;
        *) usage ;;
        esac
        SANITIZERS_GIVEN=1
        shift
        ;;
    -*) usage ;;
    *) SCREENSHOT=$1 ;;
    esac
    shift
done

if [ "$MODE" = quick ] && [ "$SANITIZERS_GIVEN" -eq 0 ]; then
    SANITIZERS=""
fi

failures=""
timings=""
total_start=$(date +%s)

step() {
    name=$1
    shift
    echo "== $name"
    started=$(date +%s)
    if ! "$@"; then
        failures="$failures $name"
    fi
    seconds=$(($(date +%s) - started))
    echo "-- $name: ${seconds}s"
    timings="$timings
$(printf '%-24s %6ss' "$name" "$seconds")"
}

limited() {
    perl -e 'alarm shift; exec @ARGV or die' "$@"
}

run_tests() {
    preset=$1
    if [ -n "$TEST_REGEX" ]; then
        limited 1200 ctest --preset "$preset" -R "$TEST_REGEX"
    else
        limited 1200 ctest --preset "$preset"
    fi
}

build_and_test() {
    preset=$1
    step "configure-$preset" limited 300 cmake --preset "$preset"
    step "build-$preset" limited 1200 cmake --build --preset "$preset"
    step "test-$preset" run_tests "$preset"
}

# Source files changed against the base branch, committed or not, that still exist.
changed_files() {
    {
        git diff --name-only "$BASE_BRANCH...HEAD"
        git diff --name-only HEAD
        git ls-files --others --exclude-standard
    } | sort -u | grep -E '^(src|tests)/.*\.(cpp|h|qml|mm)$' | while read -r file; do
        [ -f "$file" ] && echo "$PROJECT_DIR/$file"
    done
}

static_analysis_changed() {
    files=$(changed_files)
    if [ -z "$files" ]; then
        echo "no changed source files against $BASE_BRANCH"
        return 0
    fi
    # shellcheck disable=SC2086
    scripts/static-analysis.sh "$PROJECT_DIR/build/debug" $files
}

build_and_test debug
for preset in $SANITIZERS; do
    build_and_test "$preset"
done

if [ "$MODE" = quick ]; then
    step "static-analysis-changed" static_analysis_changed
else
    # Release build: catches warnings that only appear without assertions.
    step "configure-release" limited 300 cmake --preset release
    step "build-release" limited 1200 cmake --build --preset release

    step "static-analysis" scripts/static-analysis.sh "$PROJECT_DIR/build/debug"

    rm -f "$SCREENSHOT"
    # Smoke runs keep their settings and worlds in the build tree, never in the player's data.
    SMOKE_DATA="$PROJECT_DIR/build/smoke-data"
    # Smoke runs open on the built-in display in the background (no focus, no captured mouse) and stay silent.
    step "smoke-run" env QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$SMOKE_DATA" QTCRAFT_SCREENSHOT="$SCREENSHOT" QTCRAFT_SCREENSHOT_FRAMES=60 \
        perl -e 'alarm shift; exec @ARGV or die' 120 build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft --play --seed 42 --look 45,-20 --no-audio
    step "smoke-screenshot" test -s "$SCREENSHOT"

    # The renderer and UI code only runs in the app, so it also gets a sanitizer smoke run.
    case " $SANITIZERS " in
    *" asan "*)
        rm -f "$SCREENSHOT.asan.png"
        step "smoke-run-asan" env QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$SMOKE_DATA" QTCRAFT_SCREENSHOT="$SCREENSHOT.asan.png" QTCRAFT_SCREENSHOT_FRAMES=60 \
            perl -e 'alarm shift; exec @ARGV or die' 180 build/asan/src/app/qtcraft.app/Contents/MacOS/qtcraft --play --seed 42 --look 45,-20 --no-audio
        step "smoke-screenshot-asan" test -s "$SCREENSHOT.asan.png"
        ;;
    esac
fi

echo "== timings ($MODE)$timings"
printf '%-24s %6ss\n' "total" "$(($(date +%s) - total_start))"

if [ -n "$failures" ]; then
    echo "FAILED:$failures"
    exit 1
fi
if [ "$MODE" = quick ]; then
    echo "ALL GREEN (quick)"
else
    echo "ALL GREEN (screenshot: $SCREENSHOT)"
fi
