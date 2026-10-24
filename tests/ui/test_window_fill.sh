#!/bin/sh
# The main menu fills large windows (decision E-875): the real game renders the menu offscreen in
# windows of 1280 x 720 up to 3840 x 2160 (large monitors in fullscreen at 100 % display scaling),
# and the screenshot hook logs the geometry of the side bar and of the page column
# (QTCRAFT_LOG_ITEMS). The page is as wide as the window's factor makes a large screen
# (Ui.screen.l, 960 px at 1.0) or as the room beside the side bar allows, and it stands in the
# middle of that room, never at its left edge with the rest empty. Any other result, a missing line
# or a QML warning fails the test.
# Usage: test_window_fill.sh <qtcraft executable> <data directory>
set -u
APP=$1
DATA=$2
rm -rf "$DATA"
mkdir -p "$DATA" || exit 2
failed=0

# number <line> <field>: the value after the word <field> in an "Item" line.
number() {
    echo "$1" | sed -n "s/.* $2 \([0-9.-]*\).*/\1/p"
}

# check <name> <width>x<height> <uiScale or empty> <expected UI factor of the screens>
check() {
    name=$1
    size=$2
    dir="$DATA/$name"
    mkdir -p "$dir"
    printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$dir/settings.ini"
    [ -n "$3" ] && printf '[General]\nuiScale=%s\n' "$3" >>"$dir/settings.ini"
    QTCRAFT_WINDOW_SIZE=$size QTCRAFT_LOG_ITEMS=menuSidebar,menuPages QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 \
        QTCRAFT_DATA_DIR="$dir" QTCRAFT_SCREENSHOT="$dir/$name.png" QTCRAFT_SCREENSHOT_FRAMES=20 \
        perl -e 'alarm shift; exec @ARGV or die' 60 "$APP" --no-audio --no-mods --screen builtin --background >"$dir/$name.log" 2>&1
    status=$?
    bar=$(grep 'Item menuSidebar: x' "$dir/$name.log" | tail -1)
    page=$(grep 'Item menuPages: x' "$dir/$name.log" | tail -1)
    if [ "$status" -ne 0 ] || [ -z "$bar" ] || [ -z "$page" ]; then
        echo "$name: game failed (exit $status) or logged no geometry:"
        tail -20 "$dir/$name.log"
        failed=1
        return
    fi
    if grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$dir/$name.log"; then
        echo "$name: QML reported problems"
        failed=1
    fi

    window=$(echo "$page" | sed -n 's/.* in the window \([0-9.]*\)x.*/\1/p')
    # The room beside the side bar, the page's gaps to its left and right edges and its share.
    verdict=$(awk -v window="$window" -v barX="$(number "$bar" x)" -v barW="$(number "$bar" width)" -v x="$(number "$page" x)" \
        -v w="$(number "$page" width)" -v factor="$4" -v want="${size%x*}" 'BEGIN {
            room = window - barX - barW
            left = x - barX - barW
            right = window - x - w
            # Ui.screen.l and the margins at the factor (Ui.px rounds to whole pixels).
            expected = room - 2 * 32 * factor
            if (960 * factor < expected) expected = 960 * factor
            if (window != want) { printf "window %s wide instead of %s", window, want; exit }
            if (left - right > 1 || right - left > 1) { printf "page off centre: %.0f px left, %.0f px right", left, right; exit }
            if (w < expected - 2 || w > expected + 2) { printf "page %.0f px wide instead of %.0f (factor %s)", w, expected, factor; exit }
            printf "ok"
        }')
    if [ "$verdict" = ok ]; then
        echo "$name: page fills the window ($page)"
    else
        echo "$name: $verdict ($bar; $page)"
        failed=1
    fi
}

check default-1280 1280x720 "" 1
# A MacBook's screen in logical pixels, where the screens were designed: factor 1.
check default-1512 1512x982 "" 1
check default-1920 1920x1080 "" 1.125
check default-2560 2560x1440 "" 1.5
check default-3840 3840x2160 "" 2.125
# Ultrawide: the height decides (1.5); the page stands centred in the wide room.
check default-3440 3440x1440 "" 1.5
# Auto from an older settings file is 100 %; 75 % of the window's 1.5 is 1.125.
check auto-2560 2560x1440 auto 1.5
check fixed-75-2560 2560x1440 75 1.125
exit $failed
