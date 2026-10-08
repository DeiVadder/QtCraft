#!/bin/sh
# Per-stage performance check: runs fixed scenarios several times in the release build at view
# distance 8 and prints the median of each value. Not part of ctest; never run it in parallel with
# builds or tests, the machine load would distort the numbers.
# Usage: scripts/bench.sh [runs] [seconds]
#
# Scenarios:
#   stand  - standing still at the spawn
#   sprint - sprinting and jumping straight ahead, streaming new chunks
#   crowd  - standing among summoned mobs (humanoid zombies included) and dropped items
#   light  - standing on a cleared stone floor while torches appear and vanish around the player
#            four times a second (block light and remeshing in the background)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
RUNS=${1:-3}
SECONDS_PER_RUN=${2:-20}
APP=build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft
WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-bench.XXXXXX") || exit 2

limited() {
    perl -e 'alarm shift; exec @ARGV or die' "$@"
}

limited 300 cmake --preset release >/dev/null 2>&1 || exit 1
limited 1200 cmake --build --preset release >/dev/null 2>&1 || exit 1

CROWD_ITEMS="coal diamond gold_ingot iron_ingot stick raw_porkchop rotten_flesh slimeball wooden_sword stone_sword
iron_sword diamond_sword wooden_pickaxe stone_pickaxe iron_pickaxe diamond_pickaxe wooden_axe stone_axe iron_axe
diamond_axe wooden_shovel stone_shovel iron_shovel diamond_shovel"

run_scenario() {
    scenario=$1
    run=$2
    data="$WORK/$scenario-$run"
    log="$WORK/$scenario-$run.log"
    set -- --play --seed 42 --view-distance 8 --look 45,-10
    walk=sprint
    if [ "$scenario" != sprint ]; then
        walk=stand
    fi
    if [ "$scenario" = light ]; then
        walk=light
        set -- "$@" --chat "/time set night" --chat "/fill ~-11 ~-1 ~-11 ~11 ~-1 ~11 stone" --chat "/fill ~-11 ~ ~-11 ~11 ~4 ~11 air"
    fi
    if [ "$scenario" = crowd ]; then
        set -- "$@" --chat "/time set day" --chat "/summon qtcraft:pig 16" --chat "/summon qtcraft:pig 16" \
            --chat "/summon qtcraft:zombie 16" --chat "/summon qtcraft:slime 8"
        for item in $CROWD_ITEMS; do
            set -- "$@" --chat "/spawnitem qtcraft:$item 1"
        done
    fi
    # Sound is mixed as in a normal game but muted, so runs stay quiet.
    mkdir -p "$data"
    printf '[audio]\nmuted=true\n' >"$data/settings.ini"
    env QTCRAFT_DATA_DIR="$data" QTCRAFT_BENCHMARK="$SECONDS_PER_RUN" QTCRAFT_BENCHMARK_SCENARIO="$walk" \
        perl -e 'alarm shift; exec @ARGV or die' $((SECONDS_PER_RUN + 90)) "$APP" "$@" >"$log" 2>&1
    grep -o 'BENCH .*' "$log" | tail -1
}

# Median of the numbers on stdin.
median() {
    sort -n | awk '{ v[NR] = $1 } END { if (NR == 0) { print "-" } else if (NR % 2) { print v[(NR + 1) / 2] } else { print (v[NR / 2] + v[NR / 2 + 1]) / 2 } }'
}

value() {
    sed -n "s/.*$1=\([0-9.]*\).*/\1/p"
}

failed=0
KEYS="frame_avg_ms frame_p99_ms tick_avg_ms tick_worst_ms peak_rss_mb entities change_mesh_avg_ms change_mesh_max_ms mesh_backlog_max light_job_avg_ms"
printf '%-8s' scenario
for key in $KEYS; do
    printf ' %18s' "$key"
done
printf '\n'
for scenario in stand sprint crowd light; do
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
