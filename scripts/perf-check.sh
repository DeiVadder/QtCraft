#!/bin/sh
# Performance regression check with fixed, generous limits (see docs/Performance.md, "Ziele").
# Builds the release preset, runs stand, sprint and crowd once each at view distance 8 and fails
# if a value exceeds its limit. The limits are three to ten times the measured values, so the load
# of the machine does not make the check fail; a failure means a real regression worth profiling
# (scripts/profile.sh). Not part of ctest; never run it in parallel with builds or tests.
# Usage: scripts/perf-check.sh [seconds]

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
. "$PROJECT_DIR/scripts/bench-common.sh"
SECONDS_PER_RUN=${1:-10}
APP="$PROJECT_DIR/build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-perf-check.XXXXXX") || exit 2

limited 300 cmake --preset release >/dev/null 2>&1 || exit 1
limited 1200 cmake --build --preset release >/dev/null 2>&1 || exit 1

# key limit (upper bounds)
LIMITS="frame_avg_ms 16.7
frame_p99_ms 25
gui_cpu_avg_ms 4
render_cpu_avg_ms 6
gpu_avg_ms 6
tick_avg_ms 5
peak_rss_mb 600
mesh_job_avg_ms 5"

failed=0
for scenario in stand sprint crowd; do
    line=$(run_game "$APP" "$scenario" "$SECONDS_PER_RUN" 8 "$WORK/data-$scenario" "$WORK/$scenario.log")
    if [ -z "$line" ]; then
        echo "FAIL $scenario: no result (log: $WORK/$scenario.log)"
        failed=1
        continue
    fi
    echo "$LIMITS" | while read -r key limit; do
        measured=$(echo "$line" | value "$key")
        if [ -z "$measured" ]; then
            echo "FAIL $scenario: $key missing"
            exit 1
        fi
        if awk -v m="$measured" -v l="$limit" 'BEGIN { exit !(m > l) }'; then
            echo "FAIL $scenario: $key=$measured > $limit"
            exit 1
        fi
        echo "ok   $scenario: $key=$measured <= $limit"
    done || failed=1
done
echo "raw logs: $WORK"
if [ "$failed" -ne 0 ]; then
    echo "PERF CHECK FAILED"
    exit 1
fi
echo "PERF CHECK PASSED"
