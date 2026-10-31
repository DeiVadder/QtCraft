# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks of the Linux AppImage (decision E-951), sourced by scripts/release-linux.sh after packing:
# contents, library links, the AppImage runtime, the dedicated server with a bot and the starts of
# the game. Uses APPIMAGE, WORK, PROJECT_DIR, BUILD_DIR, QT_DIR, LOCAL_ONLY and the functions ok,
# problem and host_library_allowed of the release script. Every check runs on the AppImage itself or
# on its extracted copy RUN, never on the AppDir it was packed from.

RUN="$WORK/extract/squashfs-root"
# Runs a command with a clean environment: no Qt paths of the shell, English UTF-8, and
# APPIMAGE_EXTRACT_AND_RUN when the machine has no FUSE. clean_env_exec replaces the calling (sub)shell,
# so a background start keeps the process id in $!; clean_env runs it in a subshell of its own.
clean_env_exec() {
    exec env -i HOME="$HOME" USER="${USER:-}" LANG=C.UTF-8 PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" \
        ${APPIMAGE_EXTRACT_AND_RUN:+APPIMAGE_EXTRACT_AND_RUN=1} "$@"
}

clean_env() {
    (clean_env_exec "$@")
}

# ELF files below $1, one per line.
elf_files() {
    find "$1" -type f -print | while IFS= read -r file; do
        case "$(file -b "$file")" in
        ELF*) echo "$file" ;;
        esac
    done
}

# The AppImage runs through its runtime: the dedicated server's help, once by --headless and once
# through a link named qtcraft-headless; then it unpacks itself into RUN for the other checks.
check_runtime() {
    echo "== AppImage runtime"
    if clean_env timeout 120 "$APPIMAGE" --headless --help >"$WORK/help.txt" 2>&1 && grep -q -e '--server' "$WORK/help.txt"; then
        ok "AppImage --headless --help"
    else
        problem "AppImage --headless --help failed (log: $WORK/help.txt)"
    fi
    ln -sf "$APPIMAGE" "$WORK/qtcraft-headless"
    if clean_env timeout 120 "$WORK/qtcraft-headless" --help >"$WORK/help-link.txt" 2>&1 && grep -q -e '--server' "$WORK/help-link.txt"; then
        ok "AppImage through a link named qtcraft-headless"
    else
        problem "AppImage through a link named qtcraft-headless failed (log: $WORK/help-link.txt)"
    fi
    mkdir -p "$WORK/extract" && (cd "$WORK/extract" && timeout 300 "$APPIMAGE" --appimage-extract >/dev/null 2>&1) ||
        abort "cannot extract the AppImage"
    RUN_REAL=$(cd "$RUN" && pwd -P)
}

check_contents() {
    echo "== contents"
    for part in AppRun qtcraft.desktop qtcraft.png usr/bin/qtcraft usr/bin/qtcraft-headless usr/bin/qt.conf \
        usr/bin/mods/rubies/pack.json usr/plugins/platforms/libqxcb.so usr/plugins/platforms/libqwayland.so usr/plugins/wayland-shell-integration/libxdg-shell.so \
        usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
        usr/plugins/multimedia/libffmpegmediaplugin.so usr/plugins/tls/libqopensslbackend.so \
        usr/plugins/imageformats/libqjpeg.so usr/qml/QtQuick/Controls usr/lib/libQt6Core.so.6 \
        usr/share/doc/qtcraft/README-Linux.txt; do
        [ -e "$RUN/$part" ] && ok "contains $part" || problem "missing $part"
    done
    for library in libavcodec libavformat libavutil libswresample libswscale; do
        found=$(cd "$RUN/usr/lib" && ls "$library".so.* 2>/dev/null | head -1)
        [ -n "$found" ] && ok "contains FFmpeg $found" || problem "missing FFmpeg $library"
    done
    # OpenSSL comes from the system; SDL3 is linked statically; the GStreamer backend is removed.
    unwanted=$(find "$RUN" \( -name 'libssl.so*' -o -name 'libcrypto.so*' -o -iname '*SDL*.so*' -o -name 'libgst*' \) -print)
    [ -z "$unwanted" ] && ok "no OpenSSL, SDL or GStreamer library bundled" || problem "bundled: $(echo "$unwanted" | tr '\n' ' ')"
    licenses="$RUN/usr/share/doc/qtcraft/licenses"
    qt_texts="Qt-NOTICE.txt"
    [ "$LOCAL_ONLY" -eq 0 ] && qt_texts="$qt_texts Qt-LGPL-3.0.txt Qt-GPL-3.0.txt"
    for licence in QtCraft-LICENSE QtCraft-THIRD_PARTY.md QtCraft-CREDITS.md PixelifySans-OFL.txt SDL3-LICENSE.txt \
        HIDAPI-LICENSE.txt SDL3-LIBM-NOTICE.txt FFmpeg-LGPL-2.1.txt $qt_texts; do
        [ -s "$licenses/$licence" ] && ok "contains $licence" || problem "missing $licence"
    done
    check_bundled_licences "$licenses"
    if [ "$LOCAL_ONLY" -eq 1 ]; then
        [ -s "$RUN/usr/share/doc/qtcraft/NOT-FOR-DISTRIBUTION.txt" ] && ok "local-only: contains NOT-FOR-DISTRIBUTION.txt" ||
            problem "local-only package without NOT-FOR-DISTRIBUTION.txt"
    fi
}

# Every library in usr/lib outside Qt is listed in bundled-libraries.txt with a licence text that is there.
check_bundled_licences() {
    missing=""
    for path in "$RUN"/usr/lib/*.so*; do
        library=${path##*/}
        case "$library" in
        libQt6*) continue ;;
        esac
        licence=$(sed -n "s/^$library //p" "$1/bundled-libraries.txt" 2>/dev/null)
        [ -n "$licence" ] && [ -s "$1/$licence" ] || missing="$missing $library"
    done
    [ -z "$missing" ] && ok "every bundled library outside Qt has its licence ($(wc -l <"$1/bundled-libraries.txt" | tr -d ' ') listed)" ||
        problem "bundled libraries without licence text:$missing"
}

# Every ELF file resolves its libraries inside the AppImage or to allowed system libraries, and no
# RUNPATH points at the build tree or the Qt installation.
check_links() {
    echo "== library links"
    elf_files "$RUN" >"$WORK/elf.txt"
    : >"$WORK/host-libraries.txt"
    missing=0
    bad_paths=0
    while IFS= read -r binary; do
        env -i PATH=/usr/bin:/bin ldd "$binary" >"$WORK/ldd.txt" 2>&1
        if grep -q 'not found' "$WORK/ldd.txt"; then
            problem "$binary: $(grep 'not found' "$WORK/ldd.txt" | tr -s ' \t' ' ' | tr '\n' ';')"
            missing=$((missing + 1))
        fi
        # Only the libraries the file names itself (NEEDED): what system libraries need in turn is
        # the system's business.
        readelf -d "$binary" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p' >"$WORK/needed.txt"
        sed -n 's/^[[:space:]]*\([^ ]*\) => \(\/[^ ]*\) .*/\1 \2/p' "$WORK/ldd.txt" | while read -r name path; do
            grep -qxF "$name" "$WORK/needed.txt" && echo "$path"
        done | grep -v -e "^$RUN/" -e "^$RUN_REAL/" >>"$WORK/host-libraries.txt"
        if readelf -d "$binary" 2>/dev/null | grep -E 'R(UN)?PATH' | grep -q -e "$BUILD_DIR" -e "$QT_DIR" -e "$PROJECT_DIR"; then
            problem "$binary has a RUNPATH into the build or Qt directory"
            bad_paths=$((bad_paths + 1))
        fi
    done <"$WORK/elf.txt"
    sort -u -o "$WORK/host-libraries.txt" "$WORK/host-libraries.txt"
    unexpected=""
    while IFS= read -r library; do
        host_library_allowed "$library" || unexpected="$unexpected $library"
    done <"$WORK/host-libraries.txt"
    if [ -n "$unexpected" ]; then
        problem "unexpected system libraries:$unexpected"
    else
        ok "$(wc -l <"$WORK/elf.txt" | tr -d ' ') ELF files; system libraries: $(sed 's#.*/##' "$WORK/host-libraries.txt" | tr '\n' ' ')"
    fi
    [ "$missing" -eq 0 ] && ok "every library resolves" || true
    [ "$bad_paths" -eq 0 ] && ok "no RUNPATH into the build or Qt directory" || true
}

# Libraries mapped by process $1 that come from neither the copy nor the allowed system libraries.
foreign_mappings() {
    awk '{ print $6 }' "/proc/$1/maps" 2>/dev/null | grep '\.so' | sort -u | while IFS= read -r library; do
        case "$library" in
        "$RUN"/* | "$RUN_REAL"/*) ;;
        *) host_library_allowed "$library" || echo "$library" ;;
        esac
    done
}

# Waits up to $1 half-seconds while process $2 runs and `$3` (a command) fails.
wait_while_running() {
    waited=0
    while [ "$waited" -lt "$1" ] && kill -0 "$2" 2>/dev/null && ! eval "$3"; do
        sleep 0.5
        waited=$((waited + 1))
    done
}

# A bot of the copy joins the server on port $1 over TLS and pins its certificate.
check_join() {
    data="$WORK/data-bot"
    mkdir -p "$data"
    clean_env timeout 90 "$RUN/AppRun" --headless --join "127.0.0.1:$1" --player-name PackageCheck \
        --data-dir "$data" --time-limit 60 </dev/null >"$WORK/bot.out" 2>"$WORK/bot.err"
    status=$?
    if [ "$status" -eq 0 ] && grep -q "joined '" "$WORK/bot.err"; then
        ok "a bot of the AppImage joined the server over TLS (port $1)"
    else
        problem "join bot exit code $status (log: $WORK/bot.err)"
    fi
    [ -f "$data/known-servers.json" ] && ok "join bot pinned the certificate (known-servers.json)" || problem "join bot has no known-servers.json"
}

# The dedicated server of the copy: TLS certificate, listening, libraries, a bot, SIGTERM.
check_server() {
    echo "== dedicated server"
    data="$WORK/data-server"
    mkdir -p "$data"
    clean_env_exec "$RUN/AppRun" --headless --server --port 0 --bind 127.0.0.1 --generator flat --data-dir "$data" \
        </dev/null >"$WORK/server.log" 2>&1 &
    server_pid=$!
    trap 'kill -KILL "$server_pid" 2>/dev/null' EXIT
    trap 'kill -KILL "$server_pid" 2>/dev/null; exit 1' INT TERM HUP
    wait_while_running 180 "$server_pid" 'grep -q "Listening on port" "$WORK/server.log"'
    grep -q "Certificate fingerprint (SHA-256): " "$WORK/server.log" && ok "dedicated server: TLS certificate made" ||
        problem "dedicated server shows no certificate fingerprint (log: $WORK/server.log)"
    tls=$(sed -n 's/.*TLS through \(OpenSSL [0-9][^ ]*\).*/\1/p' "$WORK/server.log" | head -1)
    [ -n "$tls" ] && ok "dedicated server: TLS through $tls of the system" || problem "dedicated server names no OpenSSL (log: $WORK/server.log)"
    port=$(sed -n 's/.*Listening on port \([0-9]*\).*/\1/p' "$WORK/server.log" | head -1)
    if [ -n "$port" ]; then
        ok "dedicated server: listening on port $port"
        [ "$(readlink -f "/proc/$server_pid/exe")" = "$RUN_REAL/usr/bin/qtcraft-headless" ] &&
            ok "AppRun keeps the process id (exec)" || problem "server pid $server_pid is $(readlink -f "/proc/$server_pid/exe")"
        foreign=$(foreign_mappings "$server_pid")
        [ -z "$foreign" ] && ok "dedicated server maps only libraries of the AppImage or allowed system libraries" ||
            problem "dedicated server maps $(echo "$foreign" | tr '\n' ' ')"
        check_join "$port"
    else
        problem "dedicated server did not start listening (log: $WORK/server.log)"
    fi
    kill -TERM "$server_pid" 2>/dev/null
    wait_while_running 120 "$server_pid" false
    if kill -0 "$server_pid" 2>/dev/null; then
        kill -KILL "$server_pid"
        problem "dedicated server did not stop on SIGTERM (log: $WORK/server.log)"
    fi
    wait "$server_pid"
    status=$?
    trap - EXIT INT TERM HUP
    [ "$status" -eq 0 ] && grep -q "server stopped" "$WORK/server.log" && ok "dedicated server stopped on SIGTERM with exit code 0" ||
        problem "dedicated server ended with exit code $status (log: $WORK/server.log)"
}

# Starts the game of the copy as run $1 with data directory $2 and arguments $3...: a clean
# environment plus the display, screenshot hook, muted audio, English texts, caches in WORK and
# LD_DEBUG=files for the list of loaded libraries. scripts/with-display.sh adds Xvfb without display.
run_game() {
    name=$1
    data=$2
    shift 2
    "$PROJECT_DIR/scripts/with-display.sh" sh -c 'exec env -i DISPLAY="${DISPLAY:-}" XAUTHORITY="${XAUTHORITY:-}" \
        WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" "$@"' sh \
        HOME="$HOME" USER="${USER:-}" LANG=C.UTF-8 PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" XDG_CACHE_HOME="$WORK/cache" \
        QML_DISK_CACHE_PATH="$WORK/cache/qml" QSG_RHI_DISABLE_DISK_CACHE=1 QTCRAFT_DATA_DIR="$data" \
        QTCRAFT_AUDIO=1 QTCRAFT_LANGUAGE=en QTCRAFT_SCREENSHOT="$WORK/$name.png" QTCRAFT_SCREENSHOT_FRAMES=90 \
        LD_DEBUG=files LD_DEBUG_OUTPUT="$WORK/ld-$name" timeout --kill-after=10 300 "$RUN/AppRun" "$@" >"$WORK/$name.log" 2>&1
}

# Screenshot $1 exists and is larger than $2 bytes: not a blank image (a flat PNG of this size is a
# few KiB; the main menu, mostly flat, about 50 KiB at 1280x720; a world several hundred).
check_screenshot() {
    size=$(stat -c %s "$1" 2>/dev/null || echo 0)
    [ "$size" -gt "$2" ] && ok "screenshot $(basename "$1") ($((size / 1024)) KiB)" || problem "screenshot $1 missing or blank ($size bytes)"
}

# Run $1 loaded Qt, its plugins and FFmpeg only from the copy, and nothing from the Qt installation.
check_game_libraries() {
    sed -n 's/.*calling init: \(\/.*\)$/\1/p' "$WORK/ld-$1".* 2>/dev/null | sort -u >"$WORK/$1.libraries"
    loaded=$(wc -l <"$WORK/$1.libraries" | tr -d ' ')
    outside=$(grep -e 'libQt6' -e '/libav' -e '/libsw' -e '/plugins/' -e "^$QT_DIR" "$WORK/$1.libraries" |
        grep -v -e "^$RUN/" -e "^$RUN_REAL/")
    if [ "$loaded" -eq 0 ]; then
        problem "$1: no library list (LD_DEBUG)"
    elif [ -n "$outside" ]; then
        problem "$1: Qt or FFmpeg loaded from outside the AppImage: $(echo "$outside" | tr '\n' ' ')"
    else
        ok "$1: $loaded loaded libraries, Qt and FFmpeg all from the AppImage"
    fi
    grep -q -e "^$RUN_REAL/usr/plugins/multimedia/libffmpegmediaplugin.so" -e "^$RUN/usr/plugins/multimedia/libffmpegmediaplugin.so" "$WORK/$1.libraries" &&
        grep -q '/lib/libavcodec' "$WORK/$1.libraries" && ok "$1: FFmpeg multimedia backend loaded from the AppImage" ||
        problem "$1: FFmpeg multimedia backend not loaded from the AppImage"
    grep -q 'Audio output: ' "$WORK/$1.log" && ok "$1: $(grep -o 'Audio output: .*' "$WORK/$1.log" | head -1)" ||
        problem "$1: no audio output in the log"
}

# The main menu (first start, which also installs the example mod) and a world (second start).
check_game() {
    echo "== starts of the game"
    data="$WORK/data"
    mkdir -p "$data" "$WORK/cache/qml" && printf '[audio]\nmuted=true\n' >"$data/settings.ini"
    run_game menu "$data" && ok "main menu run exited normally" || problem "main menu run failed (log: $WORK/menu.log)"
    check_screenshot "$WORK/menu.png" 20000
    check_game_libraries menu
    if grep -q "Installed bundled mods (switched off) in .*: rubies" "$WORK/menu.log" && [ -f "$data/mods/rubies/pack.json" ]; then
        ok "example mod installed switched off in the data directory"
    else
        problem "example mod not installed switched off"
    fi
    run_game world "$data" --play --seed 42 --look 45,-20 && ok "world run exited normally" || problem "world run failed (log: $WORK/world.log)"
    check_screenshot "$WORK/world.png" 100000
    check_game_libraries world
}
