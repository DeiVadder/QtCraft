#!/bin/sh
# Stage verification with two modes.
#   --full (default): debug build + tests, ASan/UBSan and TSan builds + tests, release build, static analysis
#                     of the whole project and time-limited smoke runs of the real game with screenshots
#                     (rendered offscreen; QTCRAFT_SHOT_MODE=window takes the real window).
#   --quick:          debug build, debug tests (optionally only those matching --tests) and static analysis
#                     of the files changed against dev (committed on this branch or not yet committed).
# --sanitizers asan|tsan limits --full to that sanitizer configuration, or adds it to --quick.
# --tsan-concurrent: --quick plus a TSan build that runs only the tests labelled `concurrency`
#                    (tests/CMakeLists.txt, about a third of the TSan time); the level for work during
#                    a stage. Before the merge the full run checks every test under TSan. With
#                    --changed its TSan step also runs every other test whose own inputs changed
#                    (relinked, or a new or edited test file on the branch); a change of runtime data
#                    alone selects only the label there (decision E-412).
# --changed:         with --quick or --tsan-concurrent (alone it means --quick --changed): every test
#                    step runs only the tests whose inputs changed since they last passed in that build
#                    directory: executables, files named on their command line or in REQUIRED_FILES,
#                    and runtime data (content, translations, shaders, test data, CMake files; a change
#                    there runs all), compared by content, so a branch switch and back selects nothing. scripts/verify-tests.py selects and keeps the
#                    stamps in build/<preset>/.verify-test-stamps.json (decision E-405). Every run
#                    records the tests that passed, so a run without --changed also renews the stamps.
# Every mode first checks the source lists of the CMake files (scripts/check-cmake-lists.py, E-417)
# and that every project include names its header by its path below src/ or tests/ and keeps the
# layers of src/ (scripts/fix-includes.py --check, E-418, E-419).
# After the debug test step every mode compares each test's time with its TIMEOUT and COST and
# warns about limits that would be too tight under TSan (scripts/verify-limits.py, E-412); the
# warnings are repeated at the end and do not fail the run.
# On macOS the script runs under `caffeinate -i` (it starts itself once more), so the machine does
# not sleep during a run; arguments and exit code pass through.
# Every test step runs ctest in parallel (decision E-380): QC_TEST_JOBS jobs, by default the number
# of cores minus two. It runs in two phases (decision E-550, scripts/verify-phases.sh): first every
# test except those labelled `timing` (tests/timing.cmake), then the timing tests with
# QC_TIMING_JOBS jobs (default 2) once the 1-min load average is below QC_QUIET_LOAD (default the
# number of cores; at most QC_QUIET_WAIT seconds of waiting, default 600, then they run anyway).
# The timing phase holds the quiet lock ~/.cache/qtcraft/quiet.lock (scripts/quiet-lock.sh); while
# another run holds it, every configure, build, test and analysis step waits first (at most
# QC_QUIET_PAUSE_MAX seconds, default 2700). Agents call scripts/wait-quiet.sh before a bare build.
# --defer-timing: skips the timing phase and names the skipped tests; they keep their old stamps, so
#                    a later --changed run selects them. For agents working in parallel; the
#                    central run before the merge runs them. The script runs at normal priority; work beside it (the next stage in its own
# worktree) builds with `nice -n 10` and runs no tests. It refuses to start while the benchmark lock
# ~/.cache/qtcraft/bench.lock exists.
# Usage: scripts/verify.sh [--quick|--full|--tsan-concurrent] [--changed] [--defer-timing] [--tests <regex>] [--sanitizers asan|tsan] [screenshot.png]

set -u

if [ -z "${QC_VERIFY_CAFFEINATED:-}" ] && command -v caffeinate >/dev/null 2>&1; then
    QC_VERIFY_CAFFEINATED=1 exec caffeinate -i /bin/sh "$0" "$@"
fi

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2

# The local presets take the Qt and Ninja paths of this machine from CMakeUserPresets.json (not in
# git, decision E-417); a new worktree gets a copy of the main checkout's file or of the example.
scripts/ensure-user-presets.sh || exit 2

MODE=full
MODE_GIVEN=""
TEST_REGEX=""
TSAN_LABEL=""
CHANGED=0
DEFER_TIMING=0
SANITIZERS="asan tsan"
SANITIZERS_GIVEN=0
SCREENSHOT="${TMPDIR:-/tmp}/qtcraft-smoke.png"
BASE_BRANCH=${QC_VERIFY_BASE:-dev}
BENCH_LOCK="$HOME/.cache/qtcraft/bench.lock"

cores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
TEST_JOBS=${QC_TEST_JOBS:-$((cores - 2))}
case "$TEST_JOBS" in
'' | *[!0-9]*) echo "QC_TEST_JOBS must be a positive number, not '$TEST_JOBS'" >&2 && exit 2 ;;
esac
[ "$TEST_JOBS" -ge 1 ] || TEST_JOBS=1
. "$PROJECT_DIR/scripts/verify-phases.sh"

usage() {
    echo "usage: scripts/verify.sh [--quick|--full|--tsan-concurrent] [--changed] [--defer-timing] [--tests <regex>] [--sanitizers asan|tsan] [screenshot.png]" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
    --quick)
        MODE=quick
        MODE_GIVEN="$MODE_GIVEN quick"
        ;;
    --full)
        MODE=full
        MODE_GIVEN="$MODE_GIVEN full"
        ;;
    --tsan-concurrent)
        MODE=quick
        MODE_GIVEN="$MODE_GIVEN tsan-concurrent"
        TSAN_LABEL=concurrency
        ;;
    --changed)
        CHANGED=1
        ;;
    --defer-timing)
        DEFER_TIMING=1
        ;;
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

# --tsan-concurrent is a level of its own: it neither combines with --full nor with other sanitizers.
if [ -n "$TSAN_LABEL" ]; then
    case "$MODE_GIVEN" in
    *full*) usage ;;
    esac
    if [ "$SANITIZERS_GIVEN" -eq 1 ] && [ "$SANITIZERS" != tsan ]; then
        usage
    fi
    SANITIZERS=tsan
    SANITIZERS_GIVEN=1
fi

# --changed belongs to the levels during a stage; the full run before the merge runs every test.
if [ "$CHANGED" -eq 1 ]; then
    case "$MODE_GIVEN" in
    *full*) usage ;;
    esac
    MODE=quick
fi

if [ "$MODE" = quick ] && [ "$SANITIZERS_GIVEN" -eq 0 ]; then
    SANITIZERS=""
fi

if [ -e "$BENCH_LOCK" ]; then
    echo "benchmark lock $BENCH_LOCK exists ($(cat "$BENCH_LOCK/owner" 2>/dev/null)); no verify run while it is held" >&2
    exit 2
fi
echo "== ctest runs with $TEST_JOBS parallel jobs (QC_TEST_JOBS), timing tests with $TIMING_JOBS (QC_TIMING_JOBS)"

failures=""
timings=""
limit_warnings=""
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

# Prints the ctest regex of the tests to run with --changed (empty: none); fails when the selection
# cannot be made, and the caller then runs every test.
# A TSan label with --changed widens to every test whose own inputs changed (--broad-label).
select_changed() {
    preset=$1
    label=$2
    set -- select --build-dir "build/$preset" --changed
    if [ -n "$TEST_REGEX" ]; then
        set -- "$@" --regex "$TEST_REGEX"
    fi
    if [ -n "$label" ]; then
        set -- "$@" --broad-label "$label"
    fi
    python3 scripts/verify-tests.py "$@"
}

run_tests() {
    preset=$1
    label=""
    if [ "$preset" = tsan ] && [ -n "$TSAN_LABEL" ]; then
        label=$TSAN_LABEL
    fi
    # The serial TSan suite took about 40 minutes (stage 43); an hour per phase leaves room for a
    # machine that is busy beside the run. The positional parameters collect the ctest filters.
    set --
    rm -f "build/$preset/.verify-test-pending.json"

    selection=""
    if [ "$CHANGED" -eq 1 ] && selection=$(select_changed "$preset" "$label"); then
        if [ -z "$selection" ]; then
            echo "no test of build/$preset changed since it last passed: 0 tests run"
            return 0
        fi
        set -- "$@" -R "$selection"
    else
        [ "$CHANGED" -eq 1 ] && echo "test selection failed, running every test" >&2
        python3 scripts/verify-tests.py select --build-dir "build/$preset"
        if [ -n "$TEST_REGEX" ]; then
            set -- "$@" -R "$TEST_REGEX"
        fi
        if [ -n "$label" ]; then
            set -- "$@" -L "$label"
        fi
    fi

    # The JUnit reports of both phases tell which tests passed; only those get new stamps.
    junit="$PROJECT_DIR/build/$preset/Testing/verify-junit.xml"
    junit_timing="$PROJECT_DIR/build/$preset/Testing/verify-junit-timing.xml"
    rm -f "$junit" "$junit_timing"
    run_main_phase "$preset" "$junit" "$@"
    status=$?
    run_timing_phase "$preset" "$junit_timing" "$@" || status=1
    python3 scripts/verify-tests.py record --build-dir "build/$preset" --junit "$junit" --junit "$junit_timing"
    if [ "$preset" = debug ]; then
        check_limits "$junit" "$junit_timing"
    fi
    return $status
}

# Warns about tests whose TIMEOUT or COST does not fit their debug time (E-412); never fails.
check_limits() {
    found=$(python3 scripts/verify-limits.py --build-dir build/debug --junit "$1" --junit "$2")
    [ -n "$found" ] || return 0
    echo "$found"
    limit_warnings="$limit_warnings
$found"
}

build_and_test() {
    preset=$1
    quiet_pause "configure-$preset"
    step "configure-$preset" limited 300 cmake --preset "$preset"
    quiet_pause "build-$preset"
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

# Source lists of the CMake files sorted and complete (decision E-417); every mode, before the build.
step "check-cmake-lists" sh -c "python3 scripts/check-cmake-lists.py --self-test && python3 scripts/check-cmake-lists.py"
# Includes with the header's path below its include root and within the layers of src/ (decision
# E-418, E-419); every mode, before the build.
step "check-includes" sh -c "python3 scripts/fix-includes.py --self-test && python3 scripts/fix-includes.py --check"
build_and_test debug
for preset in $SANITIZERS; do
    build_and_test "$preset"
done

if [ "$MODE" = quick ]; then
    quiet_pause "static-analysis-changed"
    step "static-analysis-changed" static_analysis_changed
else
    # Release build: catches warnings that only appear without assertions.
    quiet_pause "configure-release"
    step "configure-release" limited 300 cmake --preset release
    quiet_pause "build-release"
    step "build-release" limited 1200 cmake --build --preset release

    quiet_pause "static-analysis"
    step "static-analysis" scripts/static-analysis.sh "$PROJECT_DIR/build/debug"

    # Smoke runs render offscreen (E-229); QTCRAFT_SHOT_MODE=window takes the real window, which
    # needs a shown screen: keep the display awake and wait while the screen is locked.
    . "$PROJECT_DIR/scripts/screen-guard.sh"
    keep_awake
    if shot_needs_screen; then
        step "screen-unlocked" wait_for_screen
    fi

    rm -f "$SCREENSHOT"
    # Smoke runs keep their settings and worlds in the build tree, never in the player's data.
    SMOKE_DATA="$PROJECT_DIR/build/smoke-data"
    # Window smoke runs open on the built-in display in the background (no focus, no captured mouse); all stay silent.
    step "smoke-run" env $(shot_env) QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$SMOKE_DATA" QTCRAFT_SCREENSHOT="$SCREENSHOT" QTCRAFT_SCREENSHOT_FRAMES=60 \
        perl -e 'alarm shift; exec @ARGV or die' 120 build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft --play --seed 42 --look 45,-20 --no-audio
    step "smoke-screenshot" test -s "$SCREENSHOT"

    # The renderer and UI code only runs in the app, so it also gets a sanitizer smoke run.
    case " $SANITIZERS " in
    *" asan "*)
        rm -f "$SCREENSHOT.asan.png"
        step "smoke-run-asan" env $(shot_env) QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$SMOKE_DATA" QTCRAFT_SCREENSHOT="$SCREENSHOT.asan.png" QTCRAFT_SCREENSHOT_FRAMES=60 \
            perl -e 'alarm shift; exec @ARGV or die' 180 build/asan/src/app/qtcraft.app/Contents/MacOS/qtcraft --play --seed 42 --look 45,-20 --no-audio
        step "smoke-screenshot-asan" test -s "$SCREENSHOT.asan.png"
        ;;
    esac
fi

echo "== timings ($MODE)$timings"
if [ -n "$deferred_timing" ]; then
    echo "== timing tests deferred (--defer-timing; not run, not stamped, the next --changed run selects them)$deferred_timing"
fi
if [ -n "$limit_warnings" ]; then
    echo "== test limits too tight for TSan (warning only; set TIMEOUT/COST in tests/CMakeLists.txt or lighten the test)$limit_warnings"
fi
printf '%-24s %6ss\n' "total" "$(($(date +%s) - total_start))"

if [ -n "$failures" ]; then
    echo "FAILED:$failures"
    exit 1
fi
selected=""
if [ "$CHANGED" -eq 1 ]; then
    selected=", changed tests only"
fi
if [ -n "$deferred_timing" ]; then
    selected="$selected, timing tests deferred"
fi
if [ -n "$TSAN_LABEL" ] && [ "$CHANGED" -eq 1 ]; then
    echo "ALL GREEN (quick$selected; TSan on label $TSAN_LABEL and changed tests)"
elif [ -n "$TSAN_LABEL" ]; then
    echo "ALL GREEN (quick, TSan on label $TSAN_LABEL$selected)"
elif [ "$MODE" = quick ]; then
    echo "ALL GREEN (quick$selected)"
else
    echo "ALL GREEN (screenshot: $SCREENSHOT)"
fi
