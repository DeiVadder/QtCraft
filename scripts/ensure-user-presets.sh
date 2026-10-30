#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Makes sure the checkout has CMakeUserPresets.json: the visible presets (debug, release, profile, ...)
# with the Qt and Ninja paths of this machine, not in git (decision E-417). When the file is missing
# (a new worktree, a fresh clone), it copies the one of the main checkout if that has one, otherwise
# CMakeUserPresets.example.json. Every script that calls `cmake --preset` runs this first; in a new
# worktree run it (or any of those scripts) before a plain `cmake --preset`.
# With ~/.config/qtcraft/build-root it puts build/ on another disk (see below).
# It also enables the project's git hooks (tools/githooks: pre-push structure checks) through
# core.hooksPath, unless that is already set to something else.
# Usage: scripts/ensure-user-presets.sh   (exit code 0: the file exists, 2: it could not be created)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd) || exit 2
TARGET="$PROJECT_DIR/CMakeUserPresets.json"

# The hooks path is relative, so every worktree uses the hooks of its own checkout.
HOOKS_PATH=$(git -C "$PROJECT_DIR" config --get core.hooksPath 2>/dev/null) || HOOKS_PATH=""
if [ -z "$HOOKS_PATH" ] && [ -d "$PROJECT_DIR/tools/githooks" ]; then
    git -C "$PROJECT_DIR" config core.hooksPath tools/githooks &&
        echo "note: enabled the project git hooks (git config core.hooksPath tools/githooks)" >&2
elif [ -n "$HOOKS_PATH" ] && [ "$HOOKS_PATH" != tools/githooks ]; then
    echo "note: core.hooksPath is $HOOKS_PATH, the project hooks in tools/githooks stay off" >&2
fi

# Build folders on another disk (a setting of this machine, not in git): when
# ~/.config/qtcraft/build-root names a folder, the checkout's build/ becomes a link to
# <that folder>/<checkout folder name>/build, so every script and preset keeps using build/<preset>.
# The visible presets of CMakeUserPresets.json then name that real folder as their binaryDir
# (<folder>/${sourceDirName}/build/${presetName}); moc writes relative include paths, which only
# resolve from the real folder, not through the link.
# A missing disk stops here instead of building into a dangling link. Removing a worktree leaves
# its folder there; delete <root>/<worktree name> with it.
BUILD_ROOT_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/qtcraft/build-root"
if [ -f "$BUILD_ROOT_FILE" ]; then
    BUILD_ROOT=$(sed -n '1s/[[:space:]]*$//p' "$BUILD_ROOT_FILE")
    LINK="$PROJECT_DIR/build"
    if [ -n "$BUILD_ROOT" ] && [ ! -d "$BUILD_ROOT" ]; then
        echo "error: the build root $BUILD_ROOT ($BUILD_ROOT_FILE) is not there (disk not connected?)" >&2
        exit 2
    fi
    if [ -n "$BUILD_ROOT" ] && [ -L "$LINK" ] && [ ! -d "$LINK" ]; then
        echo "error: build/ links to a missing folder ($(readlink "$LINK"))" >&2
        exit 2
    fi
    if [ -n "$BUILD_ROOT" ] && [ ! -e "$LINK" ] && [ ! -L "$LINK" ]; then
        DEST="$BUILD_ROOT/$(basename "$PROJECT_DIR")/build"
        mkdir -p "$DEST" && ln -s "$DEST" "$LINK" || exit 2
        echo "note: build/ links to $DEST ($BUILD_ROOT_FILE)" >&2
    fi
fi

[ -f "$TARGET" ] && exit 0

# The main checkout is the folder above the common git directory (the .git folder all worktrees share).
SOURCE="$PROJECT_DIR/CMakeUserPresets.example.json"
COMMON_DIR=$(git -C "$PROJECT_DIR" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || COMMON_DIR=""
if [ -n "$COMMON_DIR" ] && [ -f "$COMMON_DIR/../CMakeUserPresets.json" ]; then
    SOURCE=$(cd "$COMMON_DIR/.." && pwd)/CMakeUserPresets.json
fi

if [ ! -f "$SOURCE" ] || ! cp "$SOURCE" "$TARGET"; then
    echo "error: CMakeUserPresets.json is missing and could not be created from $SOURCE;" \
        "copy CMakeUserPresets.example.json to $TARGET and set the Qt path there (E-417)" >&2
    exit 2
fi
echo "created CMakeUserPresets.json from $SOURCE (check the Qt path there)" >&2
