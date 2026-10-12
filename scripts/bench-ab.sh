#!/bin/sh
# Interleaved A/B benchmark of several builds (method: docs/Performance-Verlauf.md). Per round and
# scenario every build runs once, so the builds are measured at nearly the same moment. The order
# rotates per round: round r starts with build (r - 1) mod n of the given list, so with n rounds
# every build is first and last once (a fixed order put the last build of a round consistently
# higher, decision E-331). Not part of ctest; never run it in parallel with builds or tests.
# Usage: scripts/bench-ab.sh <result file> <rounds> <seconds> <label>=<app> [<label>=<app> ...]
#   <label> is a short name without "/" or blanks (e.g. a commit), <app> the qtcraft executable of a
#   release build (build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft of each checkout).
#   SCENARIOS="..." picks the scenarios (default below), VIEW the view distance (default 8).
#
# The script holds the benchmark lock ~/.cache/qtcraft/bench.lock (a directory with an owner file)
# for the whole series and removes it at the end, also on errors and interrupts. If someone else
# holds it, the script waits until it is gone; a lock left by a bench-ab.sh process that no longer
# runs is taken over.
#
# The result file gets a header line per series ("# series ...": date, rounds, seconds, view,
# scenarios, builds) and one line per run:
#   <round> <scenario> <label> pos=<k>/<n> order=<labels of the round> load=<before>/<after>
#   busy=<before>/<after> loaded=<0|1> BENCH ...
# load is the 1-minute load average before and after the run. busy lists foreign processes as
# "name:percent" pairs (or "-"): any process of 80 % CPU or more except the window server and
# kernel_task (UI and system), and every qtcraft process whatever its CPU (the game measured is
# not running while the samples are taken, so a qtcraft there belongs to another session). Before
# each run the script waits up to 3 minutes for a quiet machine (load at most 6, nothing busy);
# when it is still not quiet, it measures anyway. A run is marked loaded=1 when a sample before or
# after it shows a load above 6 or a busy process. Before the first round it rests 30 s more.
# A run that cannot be measured because the screen stays locked ends the series (exit 3).

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
usage() {
    echo "usage: $0 <result file> <rounds> <seconds> <label>=<app> [<label>=<app> ...]" >&2
    exit 2
}
[ $# -ge 4 ] || usage
RESULT=$1
ROUNDS=$2
SECONDS_PER_RUN=$3
shift 3

case "$ROUNDS" in
'' | *[!0-9]*) echo "rounds must be a positive number: $ROUNDS" >&2 && usage ;;
esac
case "$SECONDS_PER_RUN" in
'' | *[!0-9]*) echo "seconds must be a positive number: $SECONDS_PER_RUN" >&2 && usage ;;
esac
[ "$ROUNDS" -ge 1 ] && [ "$SECONDS_PER_RUN" -ge 1 ] || usage
LABELS=
for build in "$@"; do
    case "$build" in
    *=*) ;;
    *) echo "not <label>=<app>: $build" >&2 && usage ;;
    esac
    label=${build%%=*}
    app=${build#*=}
    case "$label" in
    '' | */* | *[[:space:]]* | *,*) echo "label must be non-empty, without \"/\", \",\" or blanks: '$label'" >&2 && usage ;;
    esac
    case " $LABELS " in
    *" $label "*) echo "label used twice: $label" >&2 && usage ;;
    esac
    [ -f "$app" ] && [ -x "$app" ] || { echo "not an executable file: $app" >&2 && exit 2; }
    LABELS="$LABELS $label"
done
BUILD_COUNT=$#

LOCK_DIR="$HOME/.cache/qtcraft/bench.lock"
LOCK_OWNER="bench-ab.sh pid=$$ dir=$PROJECT_DIR result=$RESULT"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-ab.XXXXXX") || exit 2
HAVE_LOCK=0

# Removes the lock if this script holds it; prints where the logs are.
cleanup() {
    if [ "$HAVE_LOCK" -eq 1 ] && [ "$(cat "$LOCK_DIR/owner" 2>/dev/null)" = "$LOCK_OWNER" ]; then
        rm -f "$LOCK_DIR/owner"
        rmdir "$LOCK_DIR" 2>/dev/null
    fi
    echo "logs: $WORK" >&2
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP

. "$PROJECT_DIR/scripts/bench-common.sh"

# Takes the benchmark lock, waiting while someone else holds it.
take_lock() {
    mkdir -p "$(dirname "$LOCK_DIR")" || exit 2
    waited=0
    while ! mkdir "$LOCK_DIR" 2>/dev/null; do
        owner=$(cat "$LOCK_DIR/owner" 2>/dev/null)
        pid=$(echo "$owner" | sed -n 's/^bench-ab\.sh pid=\([0-9]*\) .*/\1/p')
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
            echo "taking over the lock of a bench-ab.sh that no longer runs ($owner)" >&2
            echo "$LOCK_OWNER" >"$LOCK_DIR/owner"
            HAVE_LOCK=1
            return
        fi
        [ $((waited % 600)) -eq 0 ] && echo "benchmark lock held (${owner:-no owner file}); waiting" >&2
        sleep 30
        waited=$((waited + 30))
    done
    echo "$LOCK_OWNER" >"$LOCK_DIR/owner"
    HAVE_LOCK=1
}

# 1-minute load average.
load_now() {
    sysctl -n vm.loadavg | awk '{ print $2 }'
}

# Foreign processes as name:percent (blanks in names as "_"), or "-": 80 % CPU or more except the
# window server and kernel_task, and every qtcraft process.
busy_now() {
    busy=$(ps -Ao pcpu=,comm= | awk '{
        cpu = $1
        name = $0
        sub(/^ *[0-9.]+ +/, "", name)
        sub(".*/", "", name)
        gsub(/[ ,]/, "_", name)
        if (name ~ /^qtcraft/ || (cpu >= 80 && name != "WindowServer" && name != "kernel_task"))
            printf "%s:%d,", name, cpu
    }')
    busy=${busy%,}
    echo "${busy:-"-"}"
}

# True for a load of at most 6 and no busy process ($1 load, $2 busy).
quiet() {
    awk -v l="$1" 'BEGIN { exit !(l <= 6) }' && [ "$2" = "-" ]
}

# Waits up to 3 minutes for a quiet machine.
wait_quiet() {
    waited=0
    while ! quiet "$(load_now)" "$(busy_now)" && [ "$waited" -lt 180 ]; do
        sleep 10
        waited=$((waited + 10))
    done
    quiet "$(load_now)" "$(busy_now)" || echo "machine not quiet after 3 min (load $(load_now), busy $(busy_now)); measuring anyway" >&2
}

# Build number $1 (counted from 0) of the builds that follow it: sets label and app.
pick_build() {
    eval "build=\${$(($1 + 2))}"
    label=${build%%=*}
    app=${build#*=}
}

take_lock
echo "# series $(date '+%Y-%m-%d %H:%M:%S') rounds=$ROUNDS seconds=$SECONDS_PER_RUN view=${VIEW:-8} scenarios=${SCENARIOS:-default} builds=$*" >>"$RESULT"
wait_quiet
sleep 30

failed=0
round=1
while [ "$round" -le "$ROUNDS" ]; do
    first=$(((round - 1) % BUILD_COUNT))
    order=
    k=0
    while [ "$k" -lt "$BUILD_COUNT" ]; do
        pick_build $(((first + k) % BUILD_COUNT)) "$@"
        order="$order${order:+,}$label"
        k=$((k + 1))
    done
    for scenario in ${SCENARIOS:-stand sprint crowd light liquids glass showcase flat-crowd flat-liquids void-light armour-crowd}; do
        k=0
        while [ "$k" -lt "$BUILD_COUNT" ]; do
            pick_build $(((first + k) % BUILD_COUNT)) "$@"
            k=$((k + 1))
            wait_quiet
            load=$(load_now)
            busy=$(busy_now)
            log="$WORK/$round-$scenario-$label.log"
            line=$(run_game "$app" "$scenario" "$SECONDS_PER_RUN" "${VIEW:-8}" "$WORK/$round-$scenario-$label" "$log")
            status=$?
            load_after=$(load_now)
            busy_after=$(busy_now)
            rm -rf "$WORK/$round-$scenario-$label"
            if [ "$status" -ne 0 ]; then
                echo "$scenario $label round $round: screen locked, no measurement possible; series ended" >&2
                exit 3
            fi
            if [ -z "$line" ]; then
                echo "$scenario $label round $round produced no result (log: $log)" >&2
                failed=1
                continue
            fi
            loaded=0
            if ! quiet "$load" "$busy" || ! quiet "$load_after" "$busy_after"; then
                loaded=1
            fi
            echo "$round $scenario $label pos=$k/$BUILD_COUNT order=$order load=$load/$load_after busy=$busy/$busy_after loaded=$loaded $line" >>"$RESULT"
        done
    done
    round=$((round + 1))
done
exit $failed
