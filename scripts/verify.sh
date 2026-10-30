#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Stage verification with two modes.
#   --full (default): debug build + tests, ASan/UBSan and TSan builds + tests, release build, static analysis
#                     and time-limited smoke runs of the real game with screenshots (rendered offscreen;
#                     QTCRAFT_SHOT_MODE=window takes the real window). Only at milestones (before a
#                     release or a playtest build). Against the last fully green full verify
#                     (build/.verify-full-last) TSan runs only the tests labelled `concurrency`
#                     and those whose inputs changed since they last passed a full-verify TSan step,
#                     and clang-tidy, clazy and qmllint only the files changed since that commit plus
#                     the direct includers of changed headers; ASan runs every test, the size checks
#                     cover every file (decision E-929, scripts/verify-full-select.sh). --all-tsan
#                     (QC_FULL_TSAN_ALL=1) and --full-analysis (QC_FULL_ANALYSIS=1) take everything;
#                     --release (the release milestone) implies both; --dry-run prints both
#                     selections and stops; --seed-green <commit> records a commit that passed a
#                     full verify before E-929 and stops. The summary names the selections.
#   --quick:          debug build, debug tests (optionally only those matching --tests) and static analysis
#                     of the files changed against dev (committed on this branch or not yet committed).
#                     Every static analysis checks the QDoc comments of the documented folders
#                     (scripts/doc-folders.txt, E-939); qdoc runs in the full one and when a header in
#                     those folders changed (scripts/static-analysis.sh).
# --sanitizers asan|tsan limits --full to that sanitizer configuration, or adds it to --quick.
#                    A full run in two parts (`--full --sanitizers asan`, then `--full --sanitizers
#                    tsan`) does the debug build and tests, the release build, the static analysis
#                    and the debug smoke run only once per commit: a full run that passed every step
#                    without a sanitizer on a clean tree writes the commit (and the hash of
#                    CMakeUserPresets.json) to build/debug/.verify-full-shared, and the next full run
#                    with --sanitizers on the same state skips those steps and says so (decision
#                    E-890); the cheap checks and the slot broker's self-test run in both parts.
#                    --defer-timing belongs to the quick levels and is refused with --full.
# --tsan-concurrent: --quick plus a TSan build that runs only the tests labelled `concurrency`
#                    (tests/CMakeLists.txt, about a third of the TSan time); the level for work during
#                    a stage. With
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
# layers of src/ (scripts/fix-includes.py --check, E-418, E-419), that every logging category is
# defined once (scripts/check-logging.py, E-680) and that no tracked file is over 2 MB unless allowed
# (scripts/check-large-files.sh, E-933).
# After the debug test step every mode compares each test's time with its TIMEOUT and COST and
# warns about limits that would be too tight under TSan (scripts/verify-limits.py, E-412); the
# warnings are repeated at the end and do not fail the run.
# On macOS the script runs under `caffeinate -i` (it starts itself once more), so the machine does
# not sleep during a run; arguments and exit code pass through.
# Build steps use QC_BUILD_JOBS parallel jobs (default: the build tool's own choice, all cores);
# agents working in parallel set QC_BUILD_JOBS=4 so several runs do not saturate the machine.
# Every test step runs ctest in parallel (decision E-380): QC_TEST_JOBS jobs, by default the number
# of cores minus two. It runs in two phases (decision E-550, scripts/verify-phases.sh): first every
# test except those labelled `timing` (tests/timing.cmake), then the timing tests with
# QC_TIMING_JOBS jobs (default 2) once the 1-min load average is below QC_QUIET_LOAD (default the
# number of cores; at most QC_QUIET_WAIT seconds of waiting, default 600). A machine that stays loud
# defers them (E-913): not run, stamps dropped, listed in the summary with one command per preset
# that runs them later, the step stays green; loud is sticky for the run (later steps wait only
# QC_QUIET_RECHECK seconds, default 60), and a load above twice the limit takes no quiet lock.
# A full run names deferred debug timing tests in .verify-full-shared (`deferred-debug:` line), and
# the second part runs just those against the existing build/debug; QC_TIMING_UNDER_LOAD=1 measures
# anyway.
# The timing phase holds the quiet lock ~/.cache/qtcraft/quiet.lock (scripts/quiet-lock.sh); while
# another run holds it, every configure, build, test and analysis step waits first (at most
# QC_QUIET_PAUSE_MAX seconds, default 2700). Agents call scripts/wait-quiet.sh before a bare build.
# --defer-timing: skips the timing phase and names the skipped tests; their stamps are dropped, so
#                    a later --changed run selects them. For agents working in parallel; the
#                    central run before the merge runs them.
# The script runs at normal priority; work beside it (the next package in its own worktree) builds
# with `nice -n 10` and runs no tests. It refuses to start while the benchmark lock
# ~/.cache/qtcraft/bench.lock is held by a live process.
# Slot broker (scripts/qc-slot.py, decision E-781): unless QC_SLOT=0 or the run already holds a heavy
# or bench slot (QC_SLOT_ID with QC_SLOT_KIND tsan, asan, verify or bench), the script runs itself
# once more under `qc-slot.py run`: kind tsan for
# --tsan-concurrent and --sanitizers tsan, asan for --sanitizers asan, verify otherwise (the full run
# asks for cores minus two jobs). These kinds never start with fewer than 4 jobs (E-892; the full run
# at least half its wish), so the run waits rather than starting starved. It waits until the broker grants the slot (no benchmark, jobs free
# within the budget) and gets QC_BUILD_JOBS and QC_TEST_JOBS from it (an own value only lowers them).
# QC_SLOT_TIMEOUT bounds the wait (seconds, default none). Without a reachable broker it goes on with
# the lock files as before.
# Usage: scripts/verify.sh [--quick|--full|--tsan-concurrent] [--changed] [--defer-timing] [--tests <regex>] [--sanitizers asan|tsan]
#                          [--all-tsan] [--full-analysis] [--release] [--dry-run] [--seed-green <commit>] [screenshot.png]

set -u

if [ -z "${QC_VERIFY_CAFFEINATED:-}" ] && command -v caffeinate >/dev/null 2>&1; then
    QC_VERIFY_CAFFEINATED=1 exec caffeinate -i /bin/sh "$0" "$@"
fi

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2

# The local presets take the Qt and Ninja paths of this machine from CMakeUserPresets.json (not in
# git, decision E-417); a new worktree gets a copy of the main checkout's file or of the example.
scripts/ensure-user-presets.sh || exit 2

# The arguments as one quoted word list, to run the script once more under the slot broker.
ORIGINAL_ARGS=""
for arg in "$@"; do
    ORIGINAL_ARGS="$ORIGINAL_ARGS '$(printf '%s' "$arg" | sed "s/'/'\\\\''/g")'"
done

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
FULL_STAMP="$PROJECT_DIR/build/debug/.verify-full-shared"
FULL_ONLY_GIVEN=0
DRY_RUN=0
SEED_COMMIT=""

cores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
TEST_JOBS=${QC_TEST_JOBS:-$((cores - 2))}
case "$TEST_JOBS" in
'' | *[!0-9]*) echo "QC_TEST_JOBS must be a positive number, not '$TEST_JOBS'" >&2 && exit 2 ;;
esac
[ "$TEST_JOBS" -ge 1 ] || TEST_JOBS=1
BUILD_JOBS_ARGS=""
if [ -n "${QC_BUILD_JOBS:-}" ]; then
    case "$QC_BUILD_JOBS" in
    *[!0-9]* | 0) echo "QC_BUILD_JOBS must be a positive number, not '$QC_BUILD_JOBS'" >&2 && exit 2 ;;
    esac
    BUILD_JOBS_ARGS="--parallel $QC_BUILD_JOBS"
fi
. "$PROJECT_DIR/scripts/verify-phases.sh"
. "$PROJECT_DIR/scripts/verify-summary.sh"
. "$PROJECT_DIR/scripts/verify-full-select.sh"

usage() {
    echo "usage: scripts/verify.sh [--quick|--full|--tsan-concurrent] [--changed] [--defer-timing] [--tests <regex>] [--sanitizers asan|tsan]" >&2
    echo "                         [--all-tsan] [--full-analysis] [--release] [--dry-run] [--seed-green <commit>] [screenshot.png]" >&2
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
    --all-tsan)
        FULL_TSAN_ALL=1
        FULL_ONLY_GIVEN=1
        ;;
    --full-analysis)
        FULL_ANALYSIS=1
        FULL_ONLY_GIVEN=1
        ;;
    --dry-run)
        DRY_RUN=1
        FULL_ONLY_GIVEN=1
        ;;
    --release)
        FULL_TSAN_ALL=1
        FULL_ANALYSIS=1
        FULL_ONLY_GIVEN=1
        ;;
    --seed-green)
        [ $# -ge 2 ] || usage
        SEED_COMMIT=$2
        FULL_ONLY_GIVEN=1
        shift
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

# --defer-timing leaves tests unrun; the full run before the merge runs every test.
if [ "$DEFER_TIMING" -eq 1 ] && [ "$MODE" = full ] && [ "$CHANGED" -eq 0 ]; then
    usage
fi

# --changed belongs to the levels during a stage; the full run makes its own selections (E-929).
if [ "$CHANGED" -eq 1 ]; then
    case "$MODE_GIVEN" in
    *full*) usage ;;
    esac
    MODE=quick
fi

if [ "$MODE" = quick ] && [ "$SANITIZERS_GIVEN" -eq 0 ]; then
    SANITIZERS=""
fi

# The selections of the full run (E-929) belong to it alone; the dry run needs no slot.
if [ "$FULL_ONLY_GIVEN" -eq 1 ] && [ "$MODE" != full ]; then
    usage
fi
if [ -n "$SEED_COMMIT" ]; then
    seed_full_green "$SEED_COMMIT"
    exit $?
fi
if [ "$DRY_RUN" -eq 1 ]; then
    full_dry_run
    exit $?
fi

# Under the slot broker: runs this script once more as the command of a slot (exit code 211 of the
# client: no broker reachable, go on without).
# A held heavy or bench slot covers this run; any other slot (build, test, light) does not.
case "${QC_SLOT_ID:+${QC_SLOT_KIND:-}}" in
tsan | asan | verify | bench) slot_covered=1 ;;
*) slot_covered=0 ;;
esac
if [ "${QC_SLOT:-1}" != 0 ] && [ "$slot_covered" -eq 0 ] && command -v python3 >/dev/null 2>&1; then
    slot_kind=verify
    slot_jobs=""
    if [ -n "$TSAN_LABEL" ] || [ "$SANITIZERS" = tsan ]; then
        slot_kind=tsan
    elif [ "$SANITIZERS" = asan ]; then
        slot_kind=asan
    elif [ "$MODE" = full ]; then
        slot_jobs="--jobs $((cores > 4 ? cores - 2 : 3))"
    fi
    eval "set -- $ORIGINAL_ARGS"
    # shellcheck disable=SC2086
    python3 scripts/qc-slot.py run --no-fallback --kind "$slot_kind" $slot_jobs --timeout "${QC_SLOT_TIMEOUT:-0}" \
        --who "verify $MODE $(basename "$PROJECT_DIR")" -- /bin/sh "$PROJECT_DIR/scripts/verify.sh" "$@"
    status=$?
    [ "$status" -eq 211 ] || exit "$status"
    echo "slot broker not reachable; going on with the lock files" >&2
fi

# A benchmark lock refuses the run while its owner process lives; a stale one (owner pid ended) or
# the lock of the benchmark slot this run belongs to (QC_BENCH_LOCK_PID) does not.
if [ -e "$BENCH_LOCK" ]; then
    bench_owner=$(cat "$BENCH_LOCK/owner" 2>/dev/null)
    bench_pid=$(echo "$bench_owner" | sed -n 's/.*pid=\([0-9]*\).*/\1/p')
    if [ -n "$bench_pid" ] && ! kill -0 "$bench_pid" 2>/dev/null; then
        echo "ignoring the stale benchmark lock of a process that no longer runs ($bench_owner)" >&2
    elif [ -n "$bench_pid" ] && { [ "$bench_pid" = "${QC_BENCH_LOCK_PID:-none}" ] || pid_is_ancestor "$bench_pid"; }; then
        :
    else
        echo "benchmark lock $BENCH_LOCK exists (${bench_owner:-no owner file}); no verify run while it is held" >&2
        exit 2
    fi
fi
echo "== ctest runs with $TEST_JOBS parallel jobs (QC_TEST_JOBS), timing tests with $TIMING_JOBS (QC_TIMING_JOBS)"

failures=""
timings=""
debug_deferred_names=""
limit_warnings=""
total_start=$(date +%s)

# The commit of a tree without changes (tracked or untracked) and the hash of the local presets
# (not in git), empty when the tree has changes.
clean_commit() {
    [ -z "$(git status --porcelain 2>/dev/null)" ] || return 0
    commit=$(git rev-parse HEAD 2>/dev/null) || return 0
    echo "$commit $(git hash-object CMakeUserPresets.json 2>/dev/null || echo none)"
}
# One part of a two-part full run skips the steps without a sanitizer when an earlier full run
# passed them on this same clean commit (decision E-890); debug timing tests that run deferred
# (E-913) it runs alone, without a build. The sanitizers that passed on it before count towards the
# stamp of a fully green commit (E-929).
START_COMMIT=$(clean_commit)
SKIP_SHARED=0
SHARED_DEFERRED=""
read_shared_passed
if [ "$MODE" = full ] && [ "$SANITIZERS_GIVEN" -eq 1 ] && [ -z "$TEST_REGEX" ] && [ -n "$START_COMMIT" ] &&
    [ "$(full_stamp_key "$FULL_STAMP")" = "$START_COMMIT" ]; then
    SKIP_SHARED=1
    SHARED_DEFERRED=$(full_stamp_deferred "$FULL_STAMP")
    echo "== commit $START_COMMIT already passed the steps without a sanitizer in an earlier full run ($FULL_STAMP):"
    echo "   skipping the debug build and tests, the release build, the static analysis and the debug smoke run"
    if [ -n "$SHARED_DEFERRED" ]; then
        echo "   except the debug timing tests it deferred, which run now: $SHARED_DEFERRED"
    fi
fi

# The step name has a variable of its own: sh has no local variables, and the commands of a step
# (the timing phase's loops over test names) may use `name`.
step() {
    step_name=$1
    shift
    echo "== $step_name"
    started=$(date +%s)
    if ! "$@"; then
        failures="$failures $step_name"
    fi
    seconds=$(($(date +%s) - started))
    echo "-- $step_name: ${seconds}s"
    timings="$timings
$(printf '%-24s %6ss' "$step_name" "$seconds")"
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
    # The TSan step of a full run renews the stamps of its own selection as well (E-929).
    record_also=""
    if [ "$MODE" = full ] && [ "$preset" = tsan ]; then
        record_also=$FULL_TSAN_STAMPS
    fi

    selection=""
    if [ "$CHANGED" -eq 1 ] && selection=$(select_changed "$preset" "$label"); then
        if [ -z "$selection" ]; then
            echo "no test of build/$preset changed since it last passed: 0 tests run"
            return 0
        fi
        set -- "$@" -R "$selection"
    elif [ -n "$record_also" ] && full_tsan_select; then
        note_tsan_selection "$(regex_count "$full_selection")"
        if [ -z "$full_selection" ]; then
            echo "no TSan test selected for the full run: 0 tests run"
            return 0
        fi
        set -- "$@" -R "$full_selection"
    else
        [ "$CHANGED" -eq 1 ] && echo "test selection failed, running every test" >&2
        [ -n "$record_also" ] && note_tsan_selection "" "$full_reason"
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
    # Deferred timing tests lose their stamp, so the next --changed run selects them (E-913).
    record_tests "$preset" "$junit" "$junit_timing"
    record_also=""
    if [ "$preset" = debug ]; then
        debug_deferred_names=$phase_deferred
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
    step "build-$preset" limited 1200 cmake --build --preset "$preset" $BUILD_JOBS_ARGS
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
        # The API documentation gate of the documented folders still holds (E-939, about 1 s).
        python3 scripts/convert-doc-comments.py --check --documented
        return
    fi
    # shellcheck disable=SC2086
    scripts/static-analysis.sh "$PROJECT_DIR/build/debug" $files
}

# Source lists of the CMake files sorted and complete (decision E-417); every mode, before the build.
step "check-cmake-lists" sh -c "python3 scripts/check-cmake-lists.py --self-test && python3 scripts/check-cmake-lists.py"
# Includes with the header's path below its include root and within the layers of src/ (decision
# E-418, E-419); every mode, before the build.
step "check-includes" sh -c "python3 scripts/fix-includes.py --self-test && python3 scripts/fix-includes.py --check"
# SPDX header (licence and copyright) in every source of the project (decision E-932); every mode,
# before the build.
step "check-spdx" sh -c "python3 scripts/add-spdx.py --self-test && python3 scripts/add-spdx.py --check"
# Logging categories each defined once, no logging without a category in src/ (decision E-680);
# every mode, before the build.
step "check-logging" sh -c "python3 scripts/check-logging.py --self-test && python3 scripts/check-logging.py"
# No tracked or staged file over 2 MB outside scripts/large-files-allowed.txt (decision E-933); every
# mode, before the build.
step "check-large-files" scripts/check-large-files.sh
# Samples of the function length check (E-760); the check itself runs in the static analysis.
step "check-function-length" python3 scripts/check-function-length.py --self-test
# Slot broker tests (E-781, about 15 s, own temporary broker each): in the full run, and in the quick
# levels when the broker or the scripts that use it changed against the base branch.
slot_broker_changed() {
    {
        git diff --name-only "$BASE_BRANCH...HEAD"
        git diff --name-only HEAD
        git ls-files --others --exclude-standard scripts
    } | grep -qE '^scripts/(qc-slot\.py|tests/test_qc_slot\.py|wait-quiet\.sh|quiet-lock\.sh|verify\.sh|bench-ab\.sh)$'
}
if [ "$MODE" = full ] || slot_broker_changed; then
    step "check-slot-broker" python3 scripts/tests/test_qc_slot.py
fi
run_debug_step
for preset in $SANITIZERS; do
    build_and_test "$preset"
done

if [ "$MODE" = quick ]; then
    quiet_pause "static-analysis-changed"
    step "static-analysis-changed" static_analysis_changed
else
    if [ "$SKIP_SHARED" -eq 0 ]; then
        # Release build: catches warnings that only appear without assertions.
        quiet_pause "configure-release"
        step "configure-release" limited 300 cmake --preset release
        quiet_pause "build-release"
        step "build-release" limited 1200 cmake --build --preset release $BUILD_JOBS_ARGS

        quiet_pause "static-analysis"
        step "static-analysis" full_static_analysis
    fi

    # Smoke runs render offscreen (E-229); QTCRAFT_SHOT_MODE=window takes the real window, which
    # needs a shown screen: keep the display awake and wait while the screen is locked.
    . "$PROJECT_DIR/scripts/screen-guard.sh"
    keep_awake
    if shot_needs_screen; then
        step "screen-unlocked" wait_for_screen
    fi

    # Smoke runs keep their settings and worlds in the build tree, never in the player's data.
    SMOKE_DATA="$PROJECT_DIR/build/smoke-data"
    if [ "$SKIP_SHARED" -eq 0 ]; then
        rm -f "$SCREENSHOT"
        # Window smoke runs open on the built-in display in the background (no focus, no captured mouse); all stay silent.
        step "smoke-run" env $(shot_env) QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$SMOKE_DATA" QTCRAFT_SCREENSHOT="$SCREENSHOT" QTCRAFT_SCREENSHOT_FRAMES=60 \
            perl -e 'alarm shift; exec @ARGV or die' 120 build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft --play --seed 42 --look 45,-20 --no-audio
        step "smoke-screenshot" test -s "$SCREENSHOT"
    fi

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
    echo "== timing tests deferred (not run, stamps dropped, the next --changed run selects them)$deferred_timing"
    echo "   run them later on a quiet machine, each only in the preset that deferred it:"
    deferred_timing_commands
fi
if [ -n "$limit_warnings" ]; then
    echo "== test limits too tight for TSan (warning only; set TIMEOUT/COST in tests/CMakeLists.txt or lighten the test)$limit_warnings"
fi
printf '%-24s %6ss\n' "total" "$(($(date +%s) - total_start))"

# The stamps of a full run and its selections (E-890, E-913, E-929; scripts/verify-full-select.sh).
finish_full_run

if [ -n "$failures" ]; then
    echo "FAILED:$failures"
    exit 1
fi
all_green_line
