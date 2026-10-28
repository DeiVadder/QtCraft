# The two ctest phases of a verify test step (sourced by scripts/verify.sh, not run; decision
# E-550). Phase 1 runs every selected test except those labelled `timing` with QC_TEST_JOBS jobs;
# phase 2 runs the selected `timing` tests (tests/timing.cmake) with QC_TIMING_JOBS jobs (default
# 2) once the machine is quiet: the 1-min load average below QC_QUIET_LOAD (default the number of
# cores), waiting at most QC_QUIET_WAIT seconds (default 600). Phase 2 holds the quiet lock
# (scripts/quiet-lock.sh) from the wait to its end, so other verify runs and agents pause their
# builds and tests meanwhile; phase 1 and the build steps of verify.sh pause while another run
# holds it (quiet_pause).
# A machine that stays loud defers phase 2 (decision E-913): the tests do not run, are listed as
# deferred with the last load and the wait, and the step stays green; QC_TIMING_UNDER_LOAD=1
# measures anyway. Loud is sticky for the run: after one such deferral, later phases wait only
# QC_QUIET_RECHECK seconds (default 60). A load above twice QC_QUIET_LOAD at the start of phase 2 is
# not the run's own agents, so phase 2 then takes no quiet lock and also waits only QC_QUIET_RECHECK.
# With --defer-timing (DEFER_TIMING=1) phase 2 only names the tests it skips. Deferred tests lose
# their stamp (record_tests passes `phase_deferred` to `verify-tests.py record --forget`), so the
# next `--changed` run selects them again.
# QC_TEST_MACHINE_LOAD replaces the measured 1-min load average (for tests of these phases, E-913).
# Expects from verify.sh: PROJECT_DIR, TEST_JOBS, DEFER_TIMING, cores and the function `limited`;
# collects the skipped tests in `deferred_timing` (one line per preset: names and reason) for the
# summary and in `deferred_names_<preset>` for the commands that run them later.

TIMING_LABEL='^timing$'
TIMING_JOBS=${QC_TIMING_JOBS:-2}
QUIET_LOAD=${QC_QUIET_LOAD:-$cores}
QUIET_WAIT=${QC_QUIET_WAIT:-600}
QUIET_RECHECK=${QC_QUIET_RECHECK:-60}
TIMING_UNDER_LOAD=${QC_TIMING_UNDER_LOAD:-0}
deferred_timing=""
deferred_names_debug=""
deferred_names_asan=""
deferred_names_tsan=""
phase_deferred=""
machine_loud=0
load_source=""
[ -n "${QC_TEST_MACHINE_LOAD:-}" ] && load_source=" (load from QC_TEST_MACHINE_LOAD)"

for value in "$TIMING_JOBS" "$QUIET_WAIT" "$QUIET_RECHECK"; do
    case "$value" in
    '' | *[!0-9]*) echo "QC_TIMING_JOBS, QC_QUIET_WAIT and QC_QUIET_RECHECK must be whole numbers, not '$value'" >&2 && exit 2 ;;
    esac
done
for value in "$QUIET_LOAD" "${QC_TEST_MACHINE_LOAD-0}"; do
    case "$value" in
    '' | *[!0-9.]* | *.*.*) echo "QC_QUIET_LOAD and QC_TEST_MACHINE_LOAD must be numbers, not '$value'" >&2 && exit 2 ;;
    esac
done
[ "$TIMING_JOBS" -ge 1 ] || TIMING_JOBS=1
case "$TIMING_UNDER_LOAD" in
0 | 1) ;;
*) echo "QC_TIMING_UNDER_LOAD must be 0 or 1, not '$TIMING_UNDER_LOAD'" >&2 && exit 2 ;;
esac

. "$PROJECT_DIR/scripts/quiet-lock.sh"
# The lock goes with the run, also when it is interrupted.
trap quiet_lock_release EXIT
trap 'exit 130' INT TERM HUP

# The 1-min load average of the machine (QC_TEST_MACHINE_LOAD instead, when set).
machine_load() {
    if [ -n "${QC_TEST_MACHINE_LOAD:-}" ]; then
        echo "$QC_TEST_MACHINE_LOAD"
    elif [ -r /proc/loadavg ]; then
        cut -d' ' -f1 /proc/loadavg
    else
        sysctl -n vm.loadavg | awk '{ print $2 }'
    fi
}

# Succeeds when load $1 is below $2 times QUIET_LOAD.
load_below() {
    awk -v load="$1" -v limit="$QUIET_LOAD" -v factor="$2" 'BEGIN { exit !(load < limit * factor) }'
}

# Waits until the load average is below QUIET_LOAD, at most $1 seconds; says how it ended.
# Fails when the machine stayed loud, unless QC_TIMING_UNDER_LOAD=1; `load` and `waited` keep the
# last values for the caller.
wait_for_quiet() {
    wait_limit=$1
    waited=0
    load=$(machine_load)
    while ! load_below "$load" 1; do
        if [ "$waited" -ge "$wait_limit" ] && [ "$TIMING_UNDER_LOAD" = 1 ]; then
            echo "timing phase: load $load$load_source still not below $QUIET_LOAD after ${waited}s, running anyway (QC_TIMING_UNDER_LOAD=1; results may suffer from the load)"
            return 0
        elif [ "$waited" -ge "$wait_limit" ]; then
            return 1
        fi
        [ "$waited" -eq 0 ] && echo "timing phase: load $load$load_source, waiting until it is below $QUIET_LOAD (QC_QUIET_LOAD, at most ${wait_limit}s)"
        sleep 10
        waited=$((waited + 10))
        load=$(machine_load)
    done
    echo "timing phase: load $load$load_source below $QUIET_LOAD after ${waited}s"
}

# Names of the `timing` tests of build/<preset> that the ctest filter arguments select.
timing_tests() {
    preset=$1
    shift
    ctest --test-dir "build/$preset" --show-only=json-v1 "$@" -L "$TIMING_LABEL" |
        python3 -c 'import json, sys; print(" ".join(t["name"] for t in json.load(sys.stdin).get("tests", [])))'
}

# Phase 1: every selected test except the timing tests. Arguments: preset, JUnit report, filters.
run_main_phase() {
    preset=$1
    report=$2
    shift 2
    quiet_pause "test phase 1 of $preset"
    echo "-- test phase 1: all but timing tests, $TEST_JOBS jobs"
    limited 3600 ctest --preset "$preset" --parallel "$TEST_JOBS" --no-tests=ignore "$@" \
        -LE "$TIMING_LABEL" --output-junit "$report"
}

# Notes the timing tests of a preset as deferred. Arguments: preset, names, reason.
defer_timing_tests() {
    echo "-- test phase 2: timing tests deferred: $3; not run: $2"
    phase_deferred=$2
    deferred_timing="$deferred_timing
$1: $2 [$3]"
    eval "known=\$deferred_names_$1"
    for name in $2; do
        case " $known " in
        *" $name "*) ;;
        *) known="${known:+$known }$name" ;;
        esac
    done
    eval "deferred_names_$1=\$known"
}

# Phase 2: the selected timing tests on a quiet machine, or their names when deferred.
# Arguments: preset, JUnit report, filters.
run_timing_phase() {
    preset=$1
    report=$2
    shift 2
    phase_deferred=""
    names=$(timing_tests "$preset" "$@")
    if [ -z "$names" ]; then
        echo "-- test phase 2: no timing test selected"
        return 0
    fi

    if [ "$DEFER_TIMING" -eq 1 ]; then
        defer_timing_tests "$preset" "$names" "--defer-timing"
        return 0
    fi

    echo "-- test phase 2: timing tests, $TIMING_JOBS jobs: $names"
    # Pausing the own agents cannot quiet a load far above the limit; a loud run waits briefly.
    wait_limit=$QUIET_WAIT
    if [ "$machine_loud" -eq 1 ]; then
        wait_limit=$QUIET_RECHECK
    fi
    start_load=$(machine_load)
    if [ "$TIMING_UNDER_LOAD" = 1 ] || load_below "$start_load" 2; then
        quiet_lock_take
    else
        echo "timing phase: load $start_load$load_source above twice $QUIET_LOAD, not taking the quiet lock"
        wait_limit=$QUIET_RECHECK
    fi
    if ! wait_for_quiet "$wait_limit"; then
        quiet_lock_release
        machine_loud=1
        defer_timing_tests "$preset" "$names" "machine not quiet (load $load$load_source after ${waited}s, limit $QUIET_LOAD; QC_TIMING_UNDER_LOAD=1 measures anyway)"
        return 0
    fi

    machine_loud=0
    limited 3600 ctest --preset "$preset" --parallel "$TIMING_JOBS" --no-tests=ignore "$@" \
        -L "$TIMING_LABEL" --output-junit "$report"
    # Its own name: sh has no local variables, and run_tests keeps phase 1's result in `status`.
    timing_status=$?
    quiet_lock_release
    return $timing_status
}

# Records the passed tests of a step in build/<preset> and drops the stamps of the timing tests the
# step deferred (E-913). Arguments: preset, JUnit report, optional second JUnit report.
record_tests() {
    # shellcheck disable=SC2086
    python3 scripts/verify-tests.py record --build-dir "build/$1" --junit "$2" ${3:+--junit "$3"} \
        ${phase_deferred:+--forget $phase_deferred}
}

# Part two of a two-part full run (E-890) when part one deferred debug timing tests: only those,
# against the existing build/debug, no build (E-913). Argument: the test names.
run_deferred_debug_timing() {
    python3 scripts/verify-tests.py select --build-dir build/debug
    junit_timing="$PROJECT_DIR/build/debug/Testing/verify-junit-timing.xml"
    rm -f "$junit_timing"
    regex=$(echo "$1" | sed 's/ /|/g')
    run_timing_phase debug "$junit_timing" -R "^($regex)\$"
    deferred_status=$?
    record_tests debug "$junit_timing"
    debug_deferred_names=$phase_deferred
    return $deferred_status
}
