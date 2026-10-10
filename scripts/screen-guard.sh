# Shared by the scripts that start the real game (sourced, not run). A window that macOS does not
# show (display asleep, screen locked, window covered) renders no frames, so benchmarks, screenshots
# and smoke runs of such a moment are worthless. These helpers keep the display awake while the
# calling script runs and hold runs back while the screen is locked.

# Keeps display and system awake until the calling script ends (once per script).
keep_awake() {
    [ -n "${QC_KEEP_AWAKE_STARTED:-}" ] && return 0
    QC_KEEP_AWAKE_STARTED=1
    if command -v caffeinate >/dev/null 2>&1; then
        caffeinate -dimsu -w $$ >/dev/null 2>&1 &
    fi
}

# True while the console session is locked (lock screen, also after closing the lid).
screen_locked() {
    command -v ioreg >/dev/null 2>&1 || return 1
    ioreg -n Root -d1 | grep -Eq '"(IOConsoleLocked|CGSSessionScreenIsLocked)" = Yes'
}

# Waits until the screen is unlocked, polling every 30 s for at most $1 seconds (default 1800).
# Returns 1 when it is still locked after that.
wait_for_screen() {
    limit=${1:-1800}
    waited=0
    while screen_locked; do
        if [ "$waited" -ge "$limit" ]; then
            echo "screen still locked after ${waited} s" >&2
            return 1
        fi
        [ "$waited" -eq 0 ] && echo "screen locked: waiting until it is unlocked (game runs need a shown window)" >&2
        sleep 30
        waited=$((waited + 30))
    done
    return 0
}
