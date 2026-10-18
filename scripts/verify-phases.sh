# The two ctest phases of a verify test step (sourced by scripts/verify.sh, not run; decision
# E-550). Phase 1 runs every selected test except those labelled `timing` with QC_TEST_JOBS jobs;
# phase 2 runs the selected `timing` tests (tests/timing.cmake) with QC_TIMING_JOBS jobs (default
# 2) once the machine is quiet: the 1-min load average below QC_QUIET_LOAD (default the number of
# cores), waiting at most QC_QUIET_WAIT seconds (default 600) and then running anyway. Phase 2 holds
# the quiet lock (scripts/quiet-lock.sh) from the wait to its end, so other verify runs and agents
# pause their builds and tests meanwhile; phase 1 and the build steps of verify.sh pause while
# another run holds it (quiet_pause). With --defer-timing
# (DEFER_TIMING=1) phase 2 only names the tests it skips; they get no stamp, so the next
# `--changed` run selects them again.
# Expects from verify.sh: PROJECT_DIR, TEST_JOBS, DEFER_TIMING, cores and the function `limited`;
# collects the skipped tests in `deferred_timing` for the summary.

TIMING_LABEL='^timing$'
TIMING_JOBS=${QC_TIMING_JOBS:-2}
QUIET_LOAD=${QC_QUIET_LOAD:-$cores}
QUIET_WAIT=${QC_QUIET_WAIT:-600}
deferred_timing=""

for value in "$TIMING_JOBS" "$QUIET_WAIT"; do
    case "$value" in
    '' | *[!0-9]*) echo "QC_TIMING_JOBS and QC_QUIET_WAIT must be whole numbers, not '$value'" >&2 && exit 2 ;;
    esac
done
case "$QUIET_LOAD" in
'' | *[!0-9.]*) echo "QC_QUIET_LOAD must be a number, not '$QUIET_LOAD'" >&2 && exit 2 ;;
esac
[ "$TIMING_JOBS" -ge 1 ] || TIMING_JOBS=1

. "$PROJECT_DIR/scripts/quiet-lock.sh"
# The lock goes with the run, also when it is interrupted.
trap quiet_lock_release EXIT
trap 'exit 130' INT TERM HUP

# The 1-min load average of the machine.
machine_load() {
    if [ -r /proc/loadavg ]; then
        cut -d' ' -f1 /proc/loadavg
    else
        sysctl -n vm.loadavg | awk '{ print $2 }'
    fi
}

# Waits until the load average is below QUIET_LOAD, at most QUIET_WAIT seconds; says how it ended.
wait_for_quiet() {
    waited=0
    load=$(machine_load)
    while ! awk -v load="$load" -v limit="$QUIET_LOAD" 'BEGIN { exit !(load < limit) }'; do
        if [ "$waited" -ge "$QUIET_WAIT" ]; then
            echo "timing phase: load $load still not below $QUIET_LOAD after ${waited}s, running anyway (results may suffer from the load)"
            return 0
        fi
        [ "$waited" -eq 0 ] && echo "timing phase: load $load, waiting until it is below $QUIET_LOAD (QC_QUIET_LOAD, at most ${QUIET_WAIT}s)"
        sleep 10
        waited=$((waited + 10))
        load=$(machine_load)
    done
    echo "timing phase: load $load below $QUIET_LOAD after ${waited}s"
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

# Phase 2: the selected timing tests on a quiet machine, or their names when deferred.
# Arguments: preset, JUnit report, filters.
run_timing_phase() {
    preset=$1
    report=$2
    shift 2
    names=$(timing_tests "$preset" "$@")
    if [ -z "$names" ]; then
        echo "-- test phase 2: no timing test selected"
        return 0
    fi

    if [ "$DEFER_TIMING" -eq 1 ]; then
        echo "-- test phase 2 deferred (--defer-timing), not run: $names"
        deferred_timing="$deferred_timing
$preset: $names"
        return 0
    fi

    echo "-- test phase 2: timing tests, $TIMING_JOBS jobs: $names"
    quiet_lock_take
    wait_for_quiet
    limited 3600 ctest --preset "$preset" --parallel "$TIMING_JOBS" --no-tests=ignore "$@" \
        -L "$TIMING_LABEL" --output-junit "$report"
    status=$?
    quiet_lock_release
    return $status
}
