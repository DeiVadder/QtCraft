#!/bin/sh
# Screenshots of the same showcase scenes with each shipped texture pack (stage 28, decision E-303):
# the game's 16 x 16 textures, QtCraft HD at 32x and 64x and the image pack Storybook, plus the
# settings page. Rendered offscreen (no window, decision E-229), silent, with a temporary data folder
# that is removed at the end. Writes docs/screenshots/28-<scene>-<pack>.png.
# Usage: scripts/texturepack-shots.sh [out-dir] [build-dir]
set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$PROJECT_DIR/docs/screenshots}
BUILD=${2:-$PROJECT_DIR/build/debug}
APP="$BUILD/src/app/qtcraft.app/Contents/MacOS/qtcraft"
DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-packshots.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
failed=0

# shoot <name> <packs> <arguments...>
shoot() {
    name=$1
    packs=$2
    shift 2
    rm -f "$OUT/$name.png"
    env QTCRAFT_OFFSCREEN=1 QTCRAFT_TEXTURE_PACKS="$packs" QTCRAFT_SCREEN=builtin QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" \
        QTCRAFT_SCREENSHOT="$OUT/$name.png" QTCRAFT_SCREENSHOT_FRAMES="${SHOT_FRAMES:-400}" \
        perl -e 'alarm shift; exec @ARGV or die' 180 "$APP" --showcase --no-audio --no-mods --screen builtin --background "$@" \
        >"$DATA/$name.log" 2>&1
    if [ -s "$OUT/$name.png" ]; then
        echo "$name: $(grep -o 'Texture atlas:.*' "$DATA/$name.log" | tail -1)"
    else
        echo "$name: no screenshot ($(tail -3 "$DATA/$name.log"))" >&2
        failed=1
    fi
}

for pack in 16:none 32:builtin:qtcraft_hd_32 64:builtin:qtcraft_hd_64 storybook:builtin:storybook_32; do
    label=${pack%%:*}
    ids=${pack#*:}
    shoot "28-blocks-$label" "$ids" --spawn 64,-127 --look 0,-9.7
    shoot "28-landscape-$label" "$ids" --spawn 17,-21 --look 69.6,-20.6
    shoot "28-inventory-$label" "$ids" --spawn 118,-17 --look 0,-9.7 --chat "@300 /equip diamond" --overlay "@800 inventory" \
        --preview side=front,time=0
done
shoot "28-creatures-16" none --spawn 68,-17 --look 0,-8.8
shoot "28-creatures-64" builtin:qtcraft_hd_64 --spawn 68,-17 --look 0,-8.8
shoot "28-settings-texture-packs" builtin:storybook_32,builtin:qtcraft_hd_32 --spawn 64,-127 --look 0,-9.7 --overlay "@800 texturepacks"
exit "$failed"
