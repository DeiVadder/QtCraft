#!/bin/sh
# Screenshot tour through the showcase world (stage 21, decision E-220): one screenshot from every
# lookout and one close by for every area, from fixed camera positions that the world's plan gives
# (qtcraft-headless --showcase-tour), plus the light demo at night. The same content gives the same
# pictures, so the tour serves as visual check after changes; every new feature extends the world.
# The debug display (stage 21b, E-225) has its own stops: the full display over the podiums of the
# building parts, and the visualisations (hitboxes, creature goals, chunk borders) at the creatures.
# The character preview (stage 22, E-231) has its stops at the character stand: the character
# screen and the inventory with their previews, and the player seen from the front in the world
# (the same look and pose as the preview, for comparison). The cape (stage 23, E-240) has its stops
# there too, with a caped look in their own settings: the character screen (walking from the side,
# the emblem close up from behind), the player seen from behind standing, walking, sprinting,
# falling and sneaking, from the side walking and sprinting (the swing), from the front while
# falling, and caped mannequins walking about. Armour
# (stage 23, E-234, E-238) has its stops at the armour sets: the player in iron armour from the
# front and the inventory with the armour slots, the preview and the HUD's armour bar; with the caped
# look the diamond set and the cape from behind, standing and walking (the cape over the chest plate).
# Renders offscreen (no window, independent of the display, decision E-229), silent, with its
# settings and world in a temporary directory that is removed at the end.
# Usage: scripts/showcase-tour.sh [out-dir] [build-dir]
#   QTCRAFT_SHOT_MODE=window  real window on the built-in display instead (no focus, no captured
#                        mouse), for checking the window itself; needs a shown, unlocked screen
#   TOUR_FILTER=<regex>  only stops whose name matches (e.g. 'demo-|creatures')
#   TOUR_FRAMES=<n>      frames after loading before the picture (liquids flow, sand falls; default 600)
set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$PROJECT_DIR/build/showcase-tour}
BUILD=${2:-$PROJECT_DIR/build/debug}
APP="$BUILD/src/app/qtcraft.app/Contents/MacOS/qtcraft"
HEADLESS="$BUILD/src/headless/qtcraft-headless"
FILTER=${TOUR_FILTER:-.}
FRAMES=${TOUR_FRAMES:-600}
. "$PROJECT_DIR/scripts/screen-guard.sh"

if [ ! -x "$APP" ] || [ ! -x "$HEADLESS" ]; then
    echo "build $BUILD first (qtcraft and qtcraft-headless)" >&2
    exit 2
fi

DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-tour.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
trap 'exit 130' INT TERM
mkdir -p "$OUT"
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
# The cape stops use their own settings: the "Traveller" preset with short hair, so the emblem shows.
CAPED="$DATA/caped"
mkdir -p "$CAPED"
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n[character]\noutfit=qtcraft:tunic\nhair=qtcraft:short_hair\nskin=#b97b56\nhairColor=#b23a24\nshirt=#2f8f4e\npants=#8a5a2b\ncape=qtcraft:green_cape\n' \
    >"$CAPED/settings.ini"

"$HEADLESS" --showcase-tour --no-mods >"$DATA/stops.txt" || exit 1

# One picture: name, column, view and extra arguments; SHOT_DATA picks other settings.
shoot() {
    name=$1
    shift 5
    rm -f "$OUT/$name.png"
    env $(shot_env) QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="${SHOT_DATA:-$DATA}" QTCRAFT_SCREENSHOT="$OUT/$name.png" QTCRAFT_SCREENSHOT_FRAMES="$FRAMES" \
        perl -e 'alarm shift; exec @ARGV or die' 180 "$APP" --showcase --no-audio --no-mods --screen builtin --background "$@" \
        >"$DATA/$name.log" 2>&1
    if [ -s "$OUT/$name.png" ]; then
        echo "$name: $OUT/$name.png"
    else
        echo "$name: no screenshot (log: $(tail -3 "$DATA/$name.log"))" >&2
        failed=1
    fi
}

# Keeps the computer awake; a window run also waits while the screen is locked.
keep_awake

failed=0
while read -r name x z yaw pitch; do
    shot_needs_screen && { wait_for_screen || exit 1; }
    echo "$name" | grep -Eq "$FILTER" || continue
    shoot "$name" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch"
    case "$name" in
    demo-light_row-near)
        shoot "$name-night" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --chat "/time set night"
        ;;
    blocks-parts)
        shoot "debug-display" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --debug-display full
        ;;
    demo-character_stand-near)
        shoot "character-preview" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --overlay character \
            --preview side=front,time=0
        shoot "character-preview-inventory" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --overlay inventory \
            --preview animation=walk,time=0.4
        shoot "character-ingame-front" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,0" --camera front
        export SHOT_DATA="$CAPED"
        shoot "cape-character-screen" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --overlay character \
            --preview animation=walk,turn=90,time=0.4
        shoot "cape-emblem" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --overlay character \
            --preview side=back,zoom=0.5,time=0
        for pose in stand walk sprint fall sneak; do
            shoot "cape-behind-$pose" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "180,-30" --camera behind --pose "$pose"
        done
        for pose in walk sprint; do
            shoot "cape-side-$pose" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "180,-5" --camera side --pose "$pose"
        done
        shoot "cape-front-fall" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "180,0" --camera front --pose fall
        shoot "cape-mannequins" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,-15" --chat "/summon caped_mannequin 6"
        unset SHOT_DATA
        ;;
    armour-near)
        shoot "armour-ingame-front" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,0" --camera front --chat "@300 /equip iron"
        shoot "armour-inventory" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --chat "@300 /equip diamond" \
            --overlay "@800 inventory" --preview side=front,time=0
        export SHOT_DATA="$CAPED"
        shoot "armour-cape-back" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,-20" --camera behind --pose stand \
            --chat "@300 /equip diamond"
        shoot "armour-cape-walk" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,-20" --camera behind --pose walk \
            --chat "@300 /equip diamond"
        unset SHOT_DATA
        ;;
    creatures-near)
        shoot "debug-visuals" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --debug-display compact \
            --debug-show hitboxes,mobGoals,chunkBorders
        ;;
    esac
done <"$DATA/stops.txt"

exit "$failed"
