# Sourced by the scripts that build a third-party library once into a shared cache entry
# (scripts/build-sdl.sh, scripts/build-openssl.sh; decisions E-540, E-872): one build per entry at a
# time, parallel configure steps (presets, worktrees) wait. Not a program of its own.
#
# The lock is the directory $LOCK; its file `owner` holds "<pid> <start time of the process>", so a
# lock of a dead process, or of a process whose pid was reused since, counts as stale. A lock without
# owner (its holder died between mkdir and writing it) is stale after 60 s. A stale lock is taken over
# atomically: renamed to a name of our own first, so of several waiters only one removes it.
#
# The caller sets LOCK (path of the lock directory) and WORK (its scratch directory, may stay empty),
# then calls cache_lock_acquire; the EXIT trap removes WORK and releases the lock.

OWNER=""

process_start() { ps -o lstart= -p "$1" 2>/dev/null | tr -s ' '; }

# Whether the lock directory $1 is stale.
lock_is_stale() {
    owner=$(cat "$1/owner" 2>/dev/null || true)
    if [ -z "$owner" ]; then
        # No owner yet: stale only when the directory is older than 60 s.
        [ -d "$1" ] && [ -n "$(find "$1" -maxdepth 0 -mmin +1 2>/dev/null)" ]
        return
    fi
    started=$(process_start "${owner%% *}")
    if [ -z "$started" ] && [ -z "${owner#* }" ]; then
        # No start time (Git Bash: its ps has no -o lstart): stale when the process is gone.
        ! kill -0 "${owner%% *}" 2>/dev/null
        return
    fi
    [ "$started" != "${owner#* }" ]
}

take_over_stale_lock() {
    stale="$LOCK.stale.$$"
    mv "$LOCK" "$stale" 2>/dev/null || return 0
    if lock_is_stale "$stale"; then
        rm -rf "$stale"
        return 0
    fi

    # Another waiter took the stale lock over and made a fresh one between our check and the
    # rename: the renamed lock is live and is restored (unless a third one holds the lock by now;
    # the release of each holder removes only its own lock).
    if mkdir "$LOCK" 2>/dev/null; then
        cp "$stale/owner" "$LOCK/owner" 2>/dev/null
    fi
    rm -rf "$stale"
}

release_lock() {
    [ -n "$OWNER" ] && [ "$(cat "$LOCK/owner" 2>/dev/null)" = "$OWNER" ] && rm -rf "$LOCK"
    OWNER=""
}

cleanup() {
    [ -n "$WORK" ] && rm -rf "$WORK"
    release_lock
}

# Waits up to 15 min for the lock and takes it; exits with 1 when it stays held.
cache_lock_acquire() {
    trap cleanup EXIT
    trap 'exit 1' INT TERM ALRM HUP
    waited=0
    until mkdir "$LOCK" 2>/dev/null; do
        if lock_is_stale "$LOCK"; then
            take_over_stale_lock
            continue
        fi
        if [ "$waited" -ge 900 ]; then
            echo "error: $LOCK is held for 15 min ($(cat "$LOCK/owner" 2>/dev/null)); remove it if no build runs" >&2
            exit 1
        fi
        sleep 2
        waited=$((waited + 2))
    done
    OWNER="$$ $(process_start $$)"
    echo "$OWNER" >"$LOCK/owner"
}

# Waits while the benchmark lock ~/.cache/qtcraft/bench.lock exists (CLAUDE.md: the configure step
# does not build while a benchmark measures), at most 60 min; $1 names the script in the message.
wait_for_bench_lock() {
    bench_waited=0
    while [ -e "$HOME/.cache/qtcraft/bench.lock" ]; do
        if [ "$bench_waited" -ge 3600 ]; then
            echo "error: the benchmark lock ~/.cache/qtcraft/bench.lock exists for 60 min" >&2
            exit 1
        fi
        [ "$bench_waited" -eq 0 ] && echo "$1: waiting for the benchmark lock to go" >&2
        sleep 5
        bench_waited=$((bench_waited + 5))
    done
}
