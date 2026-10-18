# The quiet lock ~/.cache/qtcraft/quiet.lock (sourced by scripts/verify-phases.sh and
# scripts/wait-quiet.sh, not run; decision E-550). A verify run holds it during the timing phase of
# a test step (waiting for a quiet machine, then the `timing` tests); while another live process
# holds it, verify runs and agents pause before every build and test step. Like the benchmark lock
# it is a directory, created atomically with mkdir; its file `owner` reads
# "pid=<pid> dir=<worktree> tool=<script>". A lock whose owner process no longer runs is stale and
# is removed by whoever finds it; a lock without an owner file for longer than a minute as well.
# QC_QUIET_PAUSE_MAX (seconds, default 2700) bounds every wait for it, QC_QUIET_POLL (seconds,
# default 10) is the interval of the checks. The holder exports QC_QUIET_LOCK_PID, so its own
# children never wait for it.

QUIET_LOCK_DIR="$HOME/.cache/qtcraft/quiet.lock"
QUIET_PAUSE_MAX=${QC_QUIET_PAUSE_MAX:-2700}
QUIET_POLL=${QC_QUIET_POLL:-10}
QUIET_LOCK_HELD=0

for value in "$QUIET_PAUSE_MAX" "$QUIET_POLL"; do
    case "$value" in
    '' | *[!0-9]*) echo "QC_QUIET_PAUSE_MAX and QC_QUIET_POLL must be whole numbers, not '$value'" >&2 && exit 2 ;;
    esac
done
[ "$QUIET_POLL" -ge 1 ] || QUIET_POLL=1

# Prints the owner line of the quiet lock, or nothing when it is free. Removes a stale lock.
quiet_lock_owner() {
    [ -d "$QUIET_LOCK_DIR" ] || return 0
    owner=$(cat "$QUIET_LOCK_DIR/owner" 2>/dev/null)
    if [ -z "$owner" ]; then
        # Between mkdir and writing the owner file; stale only if that takes longer than a minute.
        if [ -n "$(find "$QUIET_LOCK_DIR" -maxdepth 0 -mmin +1 2>/dev/null)" ]; then
            echo "removing the quiet lock without owner file" >&2
            rm -rf "$QUIET_LOCK_DIR"
            return 0
        fi
        echo "pid=? (being taken)"
        return 0
    fi

    pid=$(echo "$owner" | sed -n 's/^pid=\([0-9]*\) .*/\1/p')
    if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
        echo "removing the stale quiet lock of a process that no longer runs ($owner)" >&2
        rm -rf "$QUIET_LOCK_DIR"
        return 0
    fi
    echo "$owner"
}

# Prints the owner line when another live process holds the quiet lock (not this process or the
# holder that started it), nothing otherwise.
quiet_lock_foreign() {
    owner=$(quiet_lock_owner)
    [ -n "$owner" ] || return 0
    case "$owner" in
    "pid=$$ "* | "pid=${QC_QUIET_LOCK_PID:-none} "*) return 0 ;;
    esac
    echo "$owner"
}

# Waits while another live process holds the quiet lock, at most QUIET_PAUSE_MAX seconds; says once
# what it waits for. Argument: what is about to start. Returns 1 after the bound, 0 otherwise.
quiet_pause() {
    owner=$(quiet_lock_foreign)
    [ -n "$owner" ] || return 0
    echo "quiet lock held by a timing phase ($owner); pausing before $1 (at most ${QUIET_PAUSE_MAX}s, QC_QUIET_PAUSE_MAX)" >&2
    paused=0
    while [ -n "$owner" ]; do
        if [ "$paused" -ge "$QUIET_PAUSE_MAX" ]; then
            echo "quiet lock still held after ${paused}s ($owner); going on with $1" >&2
            return 1
        fi
        sleep "$QUIET_POLL"
        paused=$((paused + QUIET_POLL))
        owner=$(quiet_lock_foreign)
    done
    echo "quiet lock free after ${paused}s; going on with $1" >&2
    return 0
}

# Takes the quiet lock, waiting (bounded) while another timing phase holds it. Without the lock
# after the bound, it goes on without it and says so.
quiet_lock_take() {
    mkdir -p "$(dirname "$QUIET_LOCK_DIR")" || return 0
    waited=0
    while ! mkdir "$QUIET_LOCK_DIR" 2>/dev/null; do
        owner=$(quiet_lock_foreign)
        if [ -z "$owner" ] && [ -d "$QUIET_LOCK_DIR" ]; then
            # Held by this process already.
            return 0
        fi
        if [ -n "$owner" ]; then
            if [ "$waited" -ge "$QUIET_PAUSE_MAX" ]; then
                echo "timing phase: quiet lock still held after ${waited}s ($owner); running without it" >&2
                return 0
            fi
            [ "$waited" -eq 0 ] && echo "timing phase: another timing phase holds the quiet lock ($owner); waiting (at most ${QUIET_PAUSE_MAX}s)" >&2
            sleep "$QUIET_POLL"
            waited=$((waited + QUIET_POLL))
        fi
    done
    echo "pid=$$ dir=$PROJECT_DIR tool=$(basename "$0")" >"$QUIET_LOCK_DIR/owner"
    QUIET_LOCK_HELD=1
    QC_QUIET_LOCK_PID=$$
    export QC_QUIET_LOCK_PID
}

# Removes the quiet lock if this process holds it.
quiet_lock_release() {
    [ "$QUIET_LOCK_HELD" -eq 1 ] || return 0
    case "$(cat "$QUIET_LOCK_DIR/owner" 2>/dev/null)" in
    "pid=$$ "*) rm -rf "$QUIET_LOCK_DIR" ;;
    esac
    QUIET_LOCK_HELD=0
}
