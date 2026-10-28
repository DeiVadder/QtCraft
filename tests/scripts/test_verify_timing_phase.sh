#!/bin/sh
# The timing phase of scripts/verify-phases.sh and the end of a run in scripts/verify-summary.sh
# (decisions E-550, E-890, E-913) with a fake load (QC_TEST_MACHINE_LOAD), stubbed ctest, python3
# and sleep, and the quiet lock in a temporary HOME: a machine that stays loud defers the timing
# tests and keeps the step green, loud is sticky for the run, a load far above the limit takes no
# quiet lock, QC_TIMING_UNDER_LOAD=1 measures anyway, a quiet machine measures, --defer-timing
# defers; deferred tests go to `record --forget`, the full-run stamp names deferred debug timing
# tests, the second part runs only those, and the commands and the final line name them per preset.
# Runs no build and no ctest.
# Usage: tests/scripts/test_verify_timing_phase.sh <path of scripts/verify-phases.sh>
set -u
SCRIPTS=$(cd "$(dirname "$1")" && pwd)
DIR=$(mktemp -d "${TMPDIR:-/tmp}/verify-timing-phase.XXXXXX") || exit 2
trap 'rm -rf "$DIR"' EXIT
failed=0

# Runs shell code in a subshell with both files sourced and the stubs in place; prints its output.
# Arguments: environment assignments (VAR=value ...) before `--`, then the code.
run() {
    (
        while [ "$1" != -- ]; do
            export "${1?}"
            shift
        done
        shift
        HOME="$DIR/home"
        PROJECT_DIR=$(dirname "$SCRIPTS")
        TEST_JOBS=2
        DEFER_TIMING=${DEFER:-0}
        cores=4
        limited() {
            echo "ctest ran: $*"
            return "${CTEST_STATUS:-0}"
        }
        . "$SCRIPTS/verify-phases.sh"
        . "$SCRIPTS/verify-summary.sh"
        timing_tests() {
            echo "test_slow test_clock"
        }
        sleep() {
            echo "slept, lock $([ -d "$QUIET_LOCK_DIR" ] && echo held || echo free)"
        }
        python3() {
            echo "python3 $*"
        }
        phase() {
            for preset in "$@"; do
                run_timing_phase "$preset" "$DIR/junit.xml" -R x
                echo "result=$? phase_deferred=[$phase_deferred]"
            done
            echo "lock=$([ -d "$QUIET_LOCK_DIR" ] && echo held || echo free)"
            echo "deferred:$deferred_timing"
        }
        eval "$1"
    ) 2>&1
}

# expect <label> <output> <text the output must contain>...
expect() {
    label=$1
    output=$2
    shift 2
    for text in "$@"; do
        case "$output" in
        *"$text"*) ;;
        *)
            echo "FAIL $label: missing '$text' in:" >&2
            echo "$output" >&2
            failed=1
            return
            ;;
        esac
    done
    echo "ok   $label"
}

# reject <label> <output> <text the output must not contain>
reject() {
    case "$2" in
    *"$3"*)
        echo "FAIL $1: unexpected '$3' in:" >&2
        echo "$2" >&2
        failed=1
        ;;
    *) echo "ok   $1" ;;
    esac
}

# Loud: above the limit 4, below twice the limit, so the phase takes the quiet lock.
loud="QC_TEST_MACHINE_LOAD=6.5 QC_QUIET_LOAD=4 QC_QUIET_WAIT=30 QC_QUIET_RECHECK=10"
far="QC_TEST_MACHINE_LOAD=150.5 QC_QUIET_LOAD=4 QC_QUIET_WAIT=30 QC_QUIET_RECHECK=10"

# shellcheck disable=SC2086
out=$(run $loud -- 'phase debug')
expect "loud machine defers" "$out" "load 6.5 (load from QC_TEST_MACHINE_LOAD), waiting until it is below 4 (QC_QUIET_LOAD, at most 30s)" \
    "slept, lock held" \
    "timing tests deferred: machine not quiet (load 6.5 (load from QC_TEST_MACHINE_LOAD) after 30s, limit 4;" \
    "not run: test_slow test_clock" "result=0 phase_deferred=[test_slow test_clock]" "lock=free" \
    "debug: test_slow test_clock [machine not quiet"
reject "loud machine runs no ctest" "$out" "ctest ran"

# shellcheck disable=SC2086
out=$(run $loud -- 'phase debug asan')
expect "loud is sticky: the second phase waits only QC_QUIET_RECHECK" "$out" "(QC_QUIET_LOAD, at most 30s)" \
    "(QC_QUIET_LOAD, at most 10s)" "after 10s, limit 4" "asan: test_slow test_clock [machine not quiet"

# shellcheck disable=SC2086
out=$(run $far -- 'phase debug')
expect "far above the limit: no quiet lock, short wait" "$out" "load 150.5 (load from QC_TEST_MACHINE_LOAD) above twice 4, not taking the quiet lock" \
    "(QC_QUIET_LOAD, at most 10s)" "slept, lock free" "result=0 phase_deferred=[test_slow test_clock]"
reject "far above the limit never holds the lock" "$out" "lock held"

# shellcheck disable=SC2086
out=$(run $far QC_TIMING_UNDER_LOAD=1 CTEST_STATUS=8 -- 'phase debug')
expect "override measures under load" "$out" "running anyway (QC_TIMING_UNDER_LOAD=1" "slept, lock held" \
    "ctest ran: 3600 ctest --preset debug --parallel 2 --no-tests=ignore -R x -L ^timing\$" \
    "result=8 phase_deferred=[]" "lock=free"

out=$(run QC_TEST_MACHINE_LOAD=1.5 QC_QUIET_LOAD=4 -- 'phase debug')
expect "quiet machine measures" "$out" "load 1.5 (load from QC_TEST_MACHINE_LOAD) below 4 after 0s" "ctest ran" \
    "result=0 phase_deferred=[]"

# shellcheck disable=SC2086
out=$(run $loud DEFER=1 -- 'phase debug')
expect "--defer-timing defers" "$out" "timing tests deferred: --defer-timing; not run: test_slow test_clock" \
    "result=0 phase_deferred=[test_slow test_clock]"
reject "--defer-timing waits for no load" "$out" "slept"

out=$(run QC_TIMING_UNDER_LOAD=yes -- 'phase debug')
expect "override must be 0 or 1" "$out" "QC_TIMING_UNDER_LOAD must be 0 or 1"
out=$(run QC_TEST_MACHINE_LOAD=high -- 'phase debug')
expect "fake load must be a number" "$out" "QC_QUIET_LOAD and QC_TEST_MACHINE_LOAD must be numbers, not 'high'"

# The stamps: passed reports recorded, deferred tests forgotten.
out=$(run -- 'phase_deferred="test_slow test_clock"; record_tests debug a.xml b.xml; phase_deferred=""; record_tests asan c.xml; echo end')
expect "record drops the stamps of deferred tests" "$out" \
    "python3 scripts/verify-tests.py record --build-dir build/debug --junit a.xml --junit b.xml --forget test_slow test_clock" \
    "python3 scripts/verify-tests.py record --build-dir build/asan --junit c.xml
end"

# The full-run stamp: the commit key on line 1, deferred debug timing tests on their own line.
stamp="$DIR/stamp"
out=$(run -- "write_full_stamp '$stamp' 'abc 123' 'test_slow test_clock'; echo key=[\$(full_stamp_key '$stamp')] deferred=[\$(full_stamp_deferred '$stamp')]; cat '$stamp'")
expect "stamp names deferred debug timing tests" "$out" "key=[abc 123] deferred=[test_slow test_clock]" \
    "abc 123
deferred-debug: test_slow test_clock"
out=$(run -- "write_full_stamp '$stamp' 'abc 123' ''; echo key=[\$(full_stamp_key '$stamp')] deferred=[\$(full_stamp_deferred '$stamp')]; wc -l <'$stamp'")
expect "stamp without deferred tests has one line" "$out" "key=[abc 123] deferred=[]" " 1"

# The second part of a full run: only the deferred debug timing tests, no build.
part_two='build_and_test() { echo "build_and_test $*"; }
step() { echo "step $1"; shift; "$@"; echo "step status $?"; }
SKIP_SHARED=1 SHARED_DEFERRED="test_slow test_clock"
run_debug_step; echo "debug_deferred=[$debug_deferred_names]"'
# shellcheck disable=SC2016
out=$(run QC_TEST_MACHINE_LOAD=1 QC_QUIET_LOAD=4 -- "$part_two")
expect "part two runs only the deferred debug timing tests" "$out" "step test-debug-timing" \
    "python3 scripts/verify-tests.py select --build-dir build/debug" \
    "ctest ran: 3600 ctest --preset debug --parallel 2 --no-tests=ignore -R ^(test_slow|test_clock)\$ -L ^timing\$" \
    "record --build-dir build/debug --junit $(dirname "$SCRIPTS")/build/debug/Testing/verify-junit-timing.xml" \
    "step status 0" "debug_deferred=[]"
reject "part two builds nothing" "$out" "build_and_test"
# shellcheck disable=SC2086
out=$(run $far -- "$part_two")
expect "part two deferred again: named for the stamp" "$out" "--forget test_slow test_clock" \
    "debug_deferred=[test_slow test_clock]"
out=$(run -- 'build_and_test() { echo "build_and_test $*"; }; SKIP_SHARED=0 SHARED_DEFERRED=""; run_debug_step
SKIP_SHARED=1; run_debug_step; echo done')
expect "part one builds and tests debug, part two without deferred tests nothing" "$out" "build_and_test debug
done"

# Commands per preset and the final line.
# shellcheck disable=SC2086
out=$(run $loud -- 'timing_tests() { [ "$preset" = asan ] && echo test_slow || echo "test_slow test_clock"; }
phase debug asan >/dev/null; deferred_timing_commands
MODE=full CHANGED=0 TSAN_LABEL="" SCREENSHOT=shot.png; all_green_line; MODE=quick; all_green_line')
expect "one command per preset, only its own tests" "$out" \
    "debug: scripts/verify.sh --quick --changed --tests '^(test_slow|test_clock)\$'" \
    "asan: scripts/verify.sh --quick --changed --sanitizers asan --tests '^(test_slow)\$'" \
    "ALL GREEN EXCEPT DEFERRED TIMING TESTS (screenshot: shot.png): the full verify is complete except for the timing tests debug: test_slow test_clock; asan: test_slow; run them later with the commands above" \
    "ALL GREEN (quick, timing tests deferred)"
reject "no tsan command without tsan deferrals" "$out" "tsan:"
out=$(run -- 'MODE=full CHANGED=0 TSAN_LABEL="" SCREENSHOT=shot.png; all_green_line')
expect "full run without deferrals" "$out" "ALL GREEN (screenshot: shot.png)"

exit $failed
