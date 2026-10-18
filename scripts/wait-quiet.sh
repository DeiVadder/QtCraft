#!/bin/sh
# Waits until no timing phase and no benchmark holds the machine (decision E-550): returns once
# neither the quiet lock ~/.cache/qtcraft/quiet.lock (held by another live process; a stale one is
# removed) nor the benchmark lock ~/.cache/qtcraft/bench.lock (held by a live bench script) exists.
# Agents call it before every bare `cmake --build` and test run (verify.sh waits by itself), e.g.
#   scripts/wait-quiet.sh && nice -n 10 cmake --build --preset debug
# Waits at most QC_QUIET_PAUSE_MAX seconds (default 2700), checking every QC_QUIET_POLL seconds
# (default 10); says once what it waits for. Exit 0 when free, 1 when a lock is still held after the
# bound (then go on and name it in the report).
# Usage: scripts/wait-quiet.sh

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BENCH_LOCK="$HOME/.cache/qtcraft/bench.lock"
. "$PROJECT_DIR/scripts/quiet-lock.sh"

# Prints the owner of the benchmark lock while a live bench script holds it, nothing otherwise. A
# lock left by a bench script that no longer runs counts as free (the next bench script takes it
# over); one without owner file counts as held.
bench_lock_owner() {
    [ -d "$BENCH_LOCK" ] || return 0
    owner=$(cat "$BENCH_LOCK/owner" 2>/dev/null)
    pid=$(echo "$owner" | sed -n 's/^bench-[a-z]*\.sh pid=\([0-9]*\) .*/\1/p')
    if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi
    echo "${owner:-no owner file}"
}

# What holds the machine right now, or nothing.
holder() {
    quiet=$(quiet_lock_foreign)
    if [ -n "$quiet" ]; then
        echo "quiet lock (timing phase): $quiet"
        return 0
    fi
    bench=$(bench_lock_owner)
    [ -z "$bench" ] || echo "benchmark lock: $bench"
}

held=$(holder)
[ -n "$held" ] || exit 0
echo "wait-quiet: $held; waiting (at most ${QUIET_PAUSE_MAX}s, QC_QUIET_PAUSE_MAX)" >&2
waited=0
while [ -n "$held" ]; do
    if [ "$waited" -ge "$QUIET_PAUSE_MAX" ]; then
        echo "wait-quiet: still held after ${waited}s ($held)" >&2
        exit 1
    fi
    sleep "$QUIET_POLL"
    waited=$((waited + QUIET_POLL))
    held=$(holder)
done
echo "wait-quiet: free after ${waited}s" >&2
exit 0
