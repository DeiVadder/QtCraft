#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Regenerates the exports of the base game's handbook after a content change: docs/handbook (English
# Markdown) and docs/handbook/de (German Markdown), both checked in and compared with a fresh export
# by test_handbook, and web/handbook/data (the web handbook: every language, icon sheet, colours)
# for a local preview of web/handbook. That data is not in git: the Pages workflow writes it the
# same way before it publishes web/, and test_web_handbook checks the exporter.
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
