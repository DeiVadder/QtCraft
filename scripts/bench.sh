#!/bin/sh
# Per-stage performance check: runs fixed scenarios several times in the release build at view
# distance 8 and prints the median of each value. Not part of ctest; never run it in parallel with
# builds or tests, the machine load would distort the numbers.
# Usage: scripts/bench.sh [runs] [seconds]   (SCENARIOS="liquids" runs a subset)
#
# Scenarios: see scripts/bench-common.sh. VIEW=12 measures another view distance, KEYS="..." other
# values of the BENCH line (among them the server tick phases tick_<phase>_avg_ms and _max_ms,
# decision E-225), DEBUG_DISPLAY=off|compact|full the level of the debug display.

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
RUNS=${1:-3}
SECONDS_PER_RUN=${2:-20}
APP=build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft
WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-bench.XXXXXX") || exit 2

. "$PROJECT_DIR/scripts/bench-common.sh"

limited 300 cmake --preset release >/dev/null 2>&1 || exit 1
limited 1200 cmake --build --preset release >/dev/null 2>&1 || exit 1

run_scenario() {
    run_game "$APP" "$1" "$SECONDS_PER_RUN" "${VIEW:-8}" "$WORK/$1-$2" "$WORK/$1-$2.log"
}

failed=0
KEYS=${KEYS:-"frame_avg_ms frame_p99_ms gui_cpu_avg_ms render_cpu_avg_ms gpu_avg_ms tick_avg_ms tick_worst_ms tick_late_avg_ms tick_late_p99_ms tick_late_max_ms ticks_skipped peak_rss_mb entities change_mesh_avg_ms change_mesh_max_ms mesh_backlog_max light_job_avg_ms"}
printf '%-8s' scenario
for key in $KEYS; do
    printf ' %18s' "$key"
done
printf '\n'
for scenario in ${SCENARIOS:-stand sprint crowd light liquids showcase flat-crowd flat-liquids}; do
    results="$WORK/$scenario.results"
    : >"$results"
    i=1
    while [ "$i" -le "$RUNS" ]; do
        line=$(run_scenario "$scenario" "$i")
        if [ -z "$line" ]; then
            echo "$scenario run $i produced no result (log: $WORK/$scenario-$i.log)" >&2
            failed=1
        else
            echo "$line" >>"$results"
        fi
        i=$((i + 1))
    done
    printf '%-8s' "$scenario"
    for key in $KEYS; do
        printf ' %18s' "$(value "$key" <"$results" | median)"
    done
    printf '\n'
done
echo "raw logs: $WORK"
exit $failed
