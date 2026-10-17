#!/bin/sh
# Makes sure the checkout has CMakeUserPresets.json: the visible presets (debug, release, profile, ...)
# with the Qt and Ninja paths of this machine, not in git (decision E-417). When the file is missing
# (a new worktree, a fresh clone), it copies the one of the main checkout if that has one, otherwise
# CMakeUserPresets.example.json. Every script that calls `cmake --preset` runs this first; in a new
# worktree run it (or any of those scripts) before a plain `cmake --preset`.
# Usage: scripts/ensure-user-presets.sh   (exit code 0: the file exists, 2: it could not be created)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd) || exit 2
TARGET="$PROJECT_DIR/CMakeUserPresets.json"
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
