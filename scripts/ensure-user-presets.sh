#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Makes sure the checkout has CMakeUserPresets.json: the visible presets (debug, release, profile, ...)
# with the Qt and Ninja paths of this machine, not in git (decision E-417). When the file is missing
# (a new worktree, a fresh clone), it copies the one of the main checkout if that has one, otherwise
# CMakeUserPresets.example.json. Every script that calls `cmake --preset` runs this first; in a new
# worktree run it (or any of those scripts) before a plain `cmake --preset`.
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
