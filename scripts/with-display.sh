#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs a command with a display server for the tests that render (decision E-949).
#
#   scripts/with-display.sh ctest --preset debug -j 3
#
# On Linux without DISPLAY and WAYLAND_DISPLAY (a VM over SSH, a CI runner) the command runs under
# xvfb-run: Qt's offscreen platform finds no OpenGL without an X server there, and the screenshot
# runs (QTCRAFT_OFFSCREEN=1) need a native platform; on Xvfb both render with Mesa's llvmpipe. Every
# other case (macOS, Windows, a Linux desktop) runs the command unchanged.
if [ "$(uname -s)" = Linux ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "with-display.sh: no display and no xvfb-run; the rendering tests will fail" \
            "(Fedora: dnf install xorg-x11-server-Xvfb, Debian/Ubuntu: apt install xvfb)" >&2
        exec "$@"
    fi
    exec xvfb-run -a -s "-screen 0 1920x1080x24" "$@"
fi
exec "$@"
