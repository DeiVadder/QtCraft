#!/bin/sh
# Layout test of "Manage server" (decision E-863): the real game plays a flat world offscreen (its
# player is the host, so an operator) and opens the screen (--overlay admin); the pad's right
# shoulder walks to each section. In the smallest window (1024 x 640) and in 1280 x 800, in German
# (the longer texts) and at 1024 x 640 in English, the layout check must find no item outside the
# window and the accessibility check no control without a name; a QML warning fails the test too.
# One run per window and language walks through all sections: the checks run after each pad step
# (QTCRAFT_CHECK_PAD_STEPS; the pad then reports a step only after QTCRAFT_SCREENSHOT_FRAMES
# rendered frames and the screens being made, so each check sees its section as settled as the
# screenshot does, decision E-900) and once more before the screenshot of the last section.
# The pause menu must offer "Manage server" to the host, and its click must open the screen.
# Usage: test_admin_layout.sh <qtcraft executable> <data directory> [directory for the screenshots]
# The data directory (build/<preset>/test-data/<test>, E-380) is emptied first and keeps the logs.
set -u
APP=$1
DATA=$2
[ -n "$DATA" ] || exit 2
rm -rf "$DATA" && mkdir -p "$DATA" || exit 2
SHOTS=${3:-$DATA}
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
failed=0

# check <name> <size> <checks> [qtcraft options...]: one offscreen run in a world, whose log must
# hold <checks> layout and accessibility checks (one per pad step and the one of the screenshot).
check() {
    name=$1
    size=$2
    checks=$3
    shift 3
    QTCRAFT_WINDOW_SIZE=$size QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_ACCESSIBILITY_CHECK=1 QTCRAFT_CHECK_PAD_STEPS=1 QTCRAFT_MENU_WORLD_TYPE=flat \
        QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$SHOTS/admin-$name.png" QTCRAFT_SCREENSHOT_FRAMES=40 \
        perl -e 'alarm shift; exec @ARGV or die' 90 "$APP" --no-audio --no-mods --screen builtin --background \
        --play --world-type flat --view-distance 2 "$@" >"$DATA/$name.log" 2>&1
    status=$?

    if [ "$status" -ne 0 ] || [ ! -s "$SHOTS/admin-$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
    elif [ "$(grep -c "Layout check: 0 items outside the window $size" "$DATA/$name.log")" -ne "$checks" ] \
        || [ "$(grep -c 'Layout check:' "$DATA/$name.log")" -ne "$checks" ]; then
        echo "$name: items outside the window (or not $checks checks):"
        grep -E 'Outside the window|Layout check|padFocus' "$DATA/$name.log"
        failed=1
    elif [ "$(grep -c 'Accessibility check: 0 controls without a name' "$DATA/$name.log")" -ne "$checks" ] \
        || [ "$(grep -c 'Accessibility check:' "$DATA/$name.log")" -ne "$checks" ]; then
        echo "$name: controls without an accessible name or role:"
        grep -E 'No accessible name|Accessibility check' "$DATA/$name.log"
        failed=1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: |no such control' "$DATA/$name.log"; then
        echo "$name: QML reported problems or a step found no control"
        failed=1
    else
        echo "$name: inside the window, every control named"
    fi
}

# sections <name> <size> [options]: the screen walked through all its sections. The status of the
# server comes once a second of the server's time: the first pad step waits for two of them. The
# window without a screen has no focus of its own; the first press (up) shows the pad's focus (the
# overview), the first shoulder press after it only reaches the side bar, and each further one
# shows the next section (players, chat, commands, lists, settings, log, server). Each step is
# checked once it is reported, the server section again before the screenshot.
sections() {
    name=$1
    size=$2
    shift 2
    set -- "$@" --overlay admin --overlay "@2500 pad:dpadUp"
    presses=8
    while [ "$presses" -gt 0 ]; do
        set -- "$@" --overlay "@300 pad:rightShoulder"
        presses=$((presses - 1))
    done
    check "$name" "$size" 10 "$@"
}

sections de-1024 1024x640 --language de
sections de-1280 1280x800 --language de
sections en-1024 1024x640

# The pause menu offers the screen to the host; clicking the entry opens it (its back entry
# answers the later click and leads back; check() fails on a step that finds no control). The
# delays of pointer steps count from the world's start.
check pause-de 1024x640 1 --language de --overlay pause --overlay "@2500 click:manageServerButton" \
    --overlay "@3500 click:sidebarBack"
exit $failed
