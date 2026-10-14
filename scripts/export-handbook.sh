#!/bin/sh
# Regenerates every checked-in export of the base game's handbook after a content change:
# docs/handbook (English Markdown), docs/handbook/de (German Markdown) and web/handbook/data (the
# web handbook: every language, icon sheet, colours). test_handbook and test_web_handbook compare
# them with a fresh export and fail until this has run.
#
# Usage: scripts/export-handbook.sh [build dir]   (default build/debug; qtcraft-headless must be built)

set -eu

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${1:-"$PROJECT_DIR/build/debug"}
HEADLESS="$BUILD_DIR/src/headless/qtcraft-headless"
[ -x "$HEADLESS" ] || { echo "qtcraft-headless not found in $BUILD_DIR (build it first)" >&2; exit 2; }

cd "$PROJECT_DIR"
"$HEADLESS" --no-mods --export-handbook docs/handbook
"$HEADLESS" --no-mods --export-handbook docs/handbook/de --language de
"$HEADLESS" --no-mods --export-handbook-web web/handbook/data
