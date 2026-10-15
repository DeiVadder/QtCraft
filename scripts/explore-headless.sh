#!/bin/sh
# The long exploration flight of the benchmarks (scenario "explore", stage 31.3) without a window:
# qtcraft-headless with its bot in creative mode flies east at 20 m/s at y 200 (a teleport of 5 m
# every 5 ticks) and puts a 3 x 3 glass platform at y 70 below itself every 20 ticks, so every chunk
# passed is changed. Prints one line per STATS interval: seconds, process memory and its peak (MB),
# loaded chunks on the server, creatures, items, tick mean and max of the interval (ms); then a
# summary line EXPLORE with the averages. Server and bot share the process (as in single player).
# Usage: scripts/explore-headless.sh <qtcraft-headless> [seconds] [view distance] [stats ms]
# The data directory is a temporary folder, removed at the end. Respect the benchmark lock
# (~/.cache/qtcraft/bench.lock) like every measurement. It runs in real time (the bot waits in client
# ticks); for the cost of the server's ticks use QTCRAFT_EXPLORE_MEASURE=300 tests/test_explore_measure,
# which flies the same way as fast as the server ticks, or SCENARIOS=explore scripts/bench-headless.sh
# for an A/B of builds (docs/Performance.md).

set -eu

APP=${1:?usage: explore-headless.sh <qtcraft-headless> [seconds] [view] [stats ms]}
SECONDS_TOTAL=${2:-300}
VIEW=${3:-8}
STATS_MS=${4:-5000}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-explore.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

SCRIPT="$WORK/flight.jsonl"
{
    echo '{"cmd":"waitUntilReady"}'
    echo '{"cmd":"chat","text":"/gamemode creative"}'
    echo '{"cmd":"chat","text":"/time set day"}'
    step=0
    steps=$((SECONDS_TOTAL * 4))
    while [ "$step" -lt "$steps" ]; do
        if [ $((step % 4)) -eq 0 ]; then
            echo '{"cmd":"chat","text":"/fill ~-1 70 ~-1 ~1 70 ~1 glass"}'
        fi
        echo '{"cmd":"chat","text":"/tp ~5 200 ~"}'
        echo '{"cmd":"wait","ticks":5}'
        step=$((step + 1))
    done
} >"$SCRIPT"

"$APP" --data-dir "$WORK/data" --seed 42 --view-distance "$VIEW" --script "$SCRIPT" --stats "$STATS_MS" \
    --time-limit $((SECONDS_TOTAL * 3 + 120)) >"$WORK/out.log" 2>&1 || {
    echo "explore-headless: the run failed (last lines of its log follow)" >&2
    tail -20 "$WORK/out.log" >&2
    exit 1
}

grep -o 'STATS {.*' "$WORK/out.log" | sed 's/^STATS //' | python3 -c '
import json, sys
rows = []
for n, line in enumerate(sys.stdin):
    data = json.loads(line)
    client, server = data["client"], data["server"]
    rows.append((n, client["memoryMb"], client["peakMemoryMb"], server["loadedChunks"], server["mobs"], server["items"],
                 server["tickMs"]["mean"], server["tickMs"]["max"]))
interval = float(sys.argv[1]) / 1000.0
print("seconds memoryMb peakMb serverChunks mobs items tickMean tickMax")
for n, memory, peak, chunks, mobs, items, mean, worst in rows:
    print(f"{n * interval:.0f} {memory} {peak} {chunks} {mobs} {items} {mean:.3f} {worst:.3f}")
if rows:
    means = [r[6] for r in rows]
    print(f"EXPLORE samples={len(rows)} lastMemoryMb={rows[-1][1]} peakMb={max(r[2] for r in rows)} "
          f"lastServerChunks={rows[-1][3]} maxServerChunks={max(r[3] for r in rows)} lastMobs={rows[-1][4]} "
          f"tickMean={sum(means) / len(means):.3f} tickMax={max(r[7] for r in rows):.3f}")
' "$STATS_MS"
