# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Progress of the package build for scripts/release.sh and scripts/release-linux.sh (sourced). The
# build writes into a log; this shows how far it is from Ninja's "[done/total]" lines.
#   - in a terminal: one line, updated every 5 s ("build 398/1322 (30 %)")
#   - QTCRAFT_RELEASE_PROGRESS=1 (the release workflow): a line once a minute, so a long build on a
#     hosted runner shows that it moves
#   - otherwise (output into a file or a pipe): nothing
# QTCRAFT_RELEASE_PROGRESS=0 turns it off in a terminal too.

BUILD_PROGRESS_PID=""

# Prints the build's state from log $1: "build <done>/<total> (<percent> %)", or the last line when
# no Ninja counter is there yet (configure, CMake's own steps).
build_progress_line() {
    counter=$(grep -o '^\[[0-9]*/[0-9]*\]' "$1" 2>/dev/null | tail -n 1 | tr -d '[]')
    if [ -n "$counter" ]; then
        done_steps=${counter%/*}
        total_steps=${counter#*/}
        echo "build $done_steps/$total_steps ($((done_steps * 100 / total_steps)) %)"
    else
        tail -n 1 "$1" 2>/dev/null | cut -c1-100
    fi
}

# Starts showing the progress of the build that writes log $1.
start_build_progress() {
    if [ -t 1 ] && [ "${QTCRAFT_RELEASE_PROGRESS:-}" != 0 ]; then
        (while sleep 5; do printf '\r\033[K%s' "$(build_progress_line "$1")"; done) &
        BUILD_PROGRESS_PID=$!
    elif [ "${QTCRAFT_RELEASE_PROGRESS:-0}" = 1 ]; then
        (while sleep 60; do build_progress_line "$1"; done) &
        BUILD_PROGRESS_PID=$!
    fi
}

# Stops the progress output (and ends the terminal's progress line).
stop_build_progress() {
    [ -n "$BUILD_PROGRESS_PID" ] || return 0
    kill "$BUILD_PROGRESS_PID" 2>/dev/null
    BUILD_PROGRESS_PID=""
    [ -t 1 ] && printf '\r\033[K'
    return 0
}
