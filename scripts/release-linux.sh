#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Release pipeline (Linux, decision E-951): builds the game and the dedicated server in their own
# Release build directory (build/linux-release, without presets: the Qt comes from QT_DIR), packs
# both into one AppImage with linuxdeploy and its Qt plugin (Qt libraries, QML modules, platform,
# image format, TLS and multimedia plugins with Qt's FFmpeg libraries; SDL3 is linked statically),
# checks the result and writes deploy/QtCraft-<version>-Linux-<arch>.AppImage and the filled
# README-Linux.txt next to it (QtCraft-<version>-Linux-README.txt). OpenSSL 3 comes from the system
# (libssl.so.3, libcrypto.so.3), like glibc and the graphics drivers.
#
# The AppImage starts the game; with --headless as the first argument (or called through a link
# named qtcraft-headless) it starts the dedicated server (scripts/linux/AppRun).
#
# Checks (any failure makes the script exit with 1):
#   - every ELF file of the AppDir resolves its libraries (ldd) inside the AppDir or to the system
#     libraries of host_library_allowed (glibc, the C++ runtime, GL/EGL/Vulkan, X11/xcb, Wayland,
#     OpenSSL ...); no RUNPATH points at the build tree or the Qt installation
#   - required parts are present (desktop file, icon, qt.conf, platform plugins xcb and Wayland,
#     FFmpeg backend and libraries, TLS backend, Qt Quick Controls, example mod) and nothing that must
#     come from the system or the static link is bundled (OpenSSL, SDL, GStreamer)
#   - the licence texts (the game's LICENSE, THIRD_PARTY.md, CREDITS.md, Pixelify Sans, SDL3, HIDAPI,
#     SDL libm, FFmpeg's LGPL 2.1, a Qt notice and, with open-source Qt, Qt's LGPL 3 and GPL 3) are in
#     usr/share/doc/qtcraft/licenses; a local-only package carries NOT-FOR-DISTRIBUTION.txt
#   - the AppImage runs (--headless --help through its runtime; without FUSE with
#     APPIMAGE_EXTRACT_AND_RUN=1)
#   - the dedicated server of the extracted AppImage starts (--server --port 0 --bind 127.0.0.1,
#     temporary data directory), makes its TLS certificate, listens, maps only libraries of the
#     AppImage or the system, lets a bot of the AppImage join over TLS, and saves and stops on SIGTERM
#     with exit code 0
#   - with a display (QTCRAFT_RELEASE_GUI=1, the default; scripts/with-display.sh runs Xvfb when there
#     is none): the game of the extracted AppImage shows the main menu and, in a second start, a world,
#     each saved as a screenshot; it opens the audio output (muted through settings.ini), installs the
#     example mod switched off, and loads Qt and FFmpeg only from the AppImage (LD_DEBUG)
# Temporary data lives below QTCRAFT_RELEASE_TMP (default: $TMPDIR or /tmp) and is removed when all
# checks pass; the screenshots are kept in deploy/.
#
# linuxdeploy and linuxdeploy-plugin-qt are the official AppImages of the pinned releases below, the
# AppImage runtime the official type2-runtime of its pinned release (appimagetool inside linuxdeploy
# cannot follow GitHub's redirect to fetch it itself); all are downloaded once into
# ~/.cache/qtcraft/linuxdeploy (QTCRAFT_LINUXDEPLOY_DIR) and checked against their SHA-256.
#
# Usage: scripts/release-linux.sh     (QT_DIR: the Qt installation, else the one build/linux-release
#                                      was configured with, else the newest ~/Qt/6.*/gcc_64 or
#                                      gcc_arm64; QTCRAFT_RELEASE_BUILD_LIMIT the build's seconds
#                                      (3600); QC_BUILD_JOBS the build's jobs; QTCRAFT_RELEASE_PROGRESS=1
#                                      prints the build's progress once a minute; QTCRAFT_RELEASE_GUI=0
#                                      skips the starts of the game; QTCRAFT_RELEASE_OPEN_SOURCE_QT=1
#                                      declares QT_DIR an open-source Qt, without it the package is
#                                      local-only; QTCRAFT_RELEASE_COMMIT names the commit when there
#                                      is no git checkout)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
[ "$(uname -s)" = Linux ] || { echo "scripts/release-linux.sh runs on Linux only" >&2; exit 2; }
ARCH=$(uname -m)
BUILD_LIMIT=${QTCRAFT_RELEASE_BUILD_LIMIT:-3600}
GUI_RUNS=${QTCRAFT_RELEASE_GUI:-1}
case "$GUI_RUNS" in
0 | 1) ;;
*) echo "QTCRAFT_RELEASE_GUI must be 0 or 1, not '$GUI_RUNS'" >&2; exit 2 ;;
esac
BUILD_DIR="$PROJECT_DIR/build/linux-release"
if [ -z "${QT_DIR:-}" ]; then
    QT_DIR=$(sed -n 's#^Qt6_DIR:PATH=\(.*\)/lib/cmake/Qt6$#\1#p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null)
    [ -n "$QT_DIR" ] || QT_DIR=$(ls -d "$HOME"/Qt/6.*/gcc_64 "$HOME"/Qt/6.*/gcc_arm64 2>/dev/null | sort -V | tail -n 1)
fi
DEPLOY_DIR="$PROJECT_DIR/deploy"
VERSION=$(sed -n 's/^project(QtCraft VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
BUILD_NUMBER=$(date -u +%Y.%m%d.%H%M)
COMMIT=${QTCRAFT_RELEASE_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}
PACKAGE_BASE="$DEPLOY_DIR/QtCraft-$VERSION-Linux-$ARCH"
TMP_ROOT=${QTCRAFT_RELEASE_TMP:-${TMPDIR:-/tmp}}
mkdir -p "$TMP_ROOT" || exit 2
WORK=$(mktemp -d "$TMP_ROOT/qtcraft-release-linux.XXXXXX") || exit 2
APPDIR="$WORK/AppDir"
failures=""

# linuxdeploy and its Qt plugin: pinned releases and the SHA-256 of their AppImages per architecture.
LINUXDEPLOY_TAG=1-alpha-20251107-1
PLUGIN_QT_TAG=1-alpha-20250213-1
RUNTIME_TAG=20251108
TOOLS_DIR=${QTCRAFT_LINUXDEPLOY_DIR:-$HOME/.cache/qtcraft/linuxdeploy}/$LINUXDEPLOY_TAG-$PLUGIN_QT_TAG-$RUNTIME_TAG-$ARCH
case "$ARCH" in
x86_64)
    LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
    PLUGIN_QT_SHA256=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
    RUNTIME_SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
    ;;
aarch64)
    LINUXDEPLOY_SHA256=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
    PLUGIN_QT_SHA256=bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1
    RUNTIME_SHA256=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444
    ;;
*) echo "no pinned linuxdeploy for $ARCH" >&2; exit 2 ;;
esac

limited() {
    timeout --kill-after=10 "$@"
}

# Stops the pipeline at once (build and packaging steps).
abort() {
    echo "RELEASE FAILED: $* (logs: $WORK)"
    exit 1
}

# Records a failed check; the pipeline goes on so every problem is reported.
problem() {
    echo "FAIL $*"
    failures="$failures
$*"
}

ok() {
    echo "ok   $*"
}

# Downloads tool $1 from $2 into TOOLS_DIR unless it is there with SHA-256 $3.
fetch_tool() {
    target="$TOOLS_DIR/$1"
    if [ -x "$target" ] && echo "$3  $target" | sha256sum -c --status; then
        return 0
    fi
    mkdir -p "$TOOLS_DIR" || return 1
    limited 300 curl -fsSL -o "$target.part" "$2" || return 1
    if ! echo "$3  $target.part" | sha256sum -c --status; then
        echo "SHA-256 of $2 does not match the pinned value" >&2
        rm -f "$target.part"
        return 1
    fi
    chmod +x "$target.part" && mv "$target.part" "$target"
}

# Whether a library the AppImage takes from the system (path $1) is one it may take: glibc, the C++
# runtime, OpenSSL, the graphics stack (GL, EGL, Vulkan, DRM, X11/xcb with the session libraries SM and
# ICE, Wayland), fonts (fontconfig on Fedora reads its configuration with libxml2) and what those bring.
host_library_allowed() {
    case "$(basename "$1")" in
    ld-linux* | libc.so* | libm.so* | libdl.so* | libpthread.so* | librt.so* | libresolv.so* | libutil.so* | libmvec.so*) return 0 ;;
    libstdc++.so* | libgcc_s.so* | libgcc_s-*.so*) return 0 ;;
    libssl.so.3* | libcrypto.so.3*) return 0 ;;
    libGL.so* | libGLX.so* | libGLdispatch.so* | libOpenGL.so* | libEGL.so* | libgbm.so* | libdrm.so* | libvulkan.so*) return 0 ;;
    libX11.so* | libX11-xcb.so* | libSM.so* | libICE.so* | libuuid.so* | libxcb.so* | libxcb-glx.so* | libxcb-randr.so* | libxcb-render.so* | libxcb-shape.so* | libxcb-shm.so* | libxcb-sync.so* | libxcb-xfixes.so* | libxcb-xkb.so*) return 0 ;;
    libXau.so* | libXdmcp.so* | libXext.so* | libXrender.so* | libXrandr.so* | libXi.so* | libXfixes.so*) return 0 ;;
    libxkbcommon.so* | libxkbcommon-x11.so* | libwayland-*.so* | libffi.so*) return 0 ;;
    libfontconfig.so* | libfreetype.so* | libexpat.so* | libxml2.so* | liblzma.so* | libz.so* | libbz2.so* | libpng16.so* | libbrotli*.so* | libharfbuzz.so* | libglib-2.0.so* | libgobject-2.0.so* | libgmodule-2.0.so* | libgraphite2.so* | libpcre2-*.so* | libzstd.so*) return 0 ;;
    libasound.so* | libpulse.so* | libpulse-simple.so* | libpulse-mainloop-glib.so* | libsndfile.so* | libsystemd.so* | libcap.so* | libdbus-1.so*) return 0 ;;
    esac
    return 1
}

# Removes the libraries of AppDir $1/usr/lib that no executable, plugin or QML plugin needs, directly
# or through other libraries (the dependencies of the removed GStreamer backend); prints their names.
prune_libraries() {
    reach="$WORK/reach.txt"
    find "$1/usr/bin" "$1/usr/plugins" "$1/usr/qml" -type f -name '*.so*' -print >"$reach"
    find "$1/usr/bin" -type f -perm -u+x -print >>"$reach"
    count=0
    while [ "$(wc -l <"$reach")" -ne "$count" ]; do
        count=$(wc -l <"$reach")
        while IFS= read -r binary; do
            readelf -d "$binary" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p'
        done <"$reach" | sort -u | while IFS= read -r name; do
            [ -e "$1/usr/lib/$name" ] && echo "$1/usr/lib/$name"
        done >"$reach.libs"
        sort -u "$reach" "$reach.libs" -o "$reach"
    done
    for library in "$1"/usr/lib/*.so*; do
        grep -qxF "$library" "$reach" && continue
        echo "${library##*/}"
        rm -f "$library" || return 1
    done
}

echo "== QtCraft $VERSION ($ARCH, build $BUILD_NUMBER, commit $COMMIT), Qt from $QT_DIR"
[ -x "$QT_DIR/bin/qmake" ] || abort "qmake not found in $QT_DIR/bin"
for tool in cmake ninja curl file ldd readelf sha256sum timeout; do
    command -v "$tool" >/dev/null 2>&1 || abort "$tool not found"
done

# Packages for others come only from the release workflow, built against open-source Qt (decision
# E-932); the rules are those of scripts/release.sh.
OPEN_SOURCE_QT=${QTCRAFT_RELEASE_OPEN_SOURCE_QT:-0}
case "$OPEN_SOURCE_QT" in
0 | 1) ;;
*) abort "QTCRAFT_RELEASE_OPEN_SOURCE_QT must be 0 or 1, not '$OPEN_SOURCE_QT'" ;;
esac
qt_root=$(cd "$QT_DIR/../.." 2>/dev/null && pwd)
COMMERCIAL_QT=0
if [ -n "$qt_root" ] && { ls "$qt_root/Licenses" 2>/dev/null | grep -qi -e enterprise -e commercial ||
    grep -qi -e 'License type \[Enterprise' -e 'commercial' "$qt_root/licenseInfo.txt" 2>/dev/null; }; then
    COMMERCIAL_QT=1
    echo "WARNING: $qt_root holds commercial Qt licence files."
fi
LOCAL_ONLY=1
if [ "$OPEN_SOURCE_QT" = 1 ] && [ "$COMMERCIAL_QT" -eq 0 ]; then
    LOCAL_ONLY=0
fi
PACKAGE=$PACKAGE_BASE
if [ "$LOCAL_ONLY" -eq 1 ]; then
    PACKAGE="$PACKAGE-local-only"
    echo "WARNING: local-only package: not for distribution; packages for others come from the release"
    echo "WARNING: workflow with open-source Qt (QTCRAFT_RELEASE_OPEN_SOURCE_QT=1, decision E-932)."
fi
APPIMAGE="$PACKAGE.AppImage"
README="$DEPLOY_DIR/QtCraft-$VERSION-Linux-README.txt"

echo "== linuxdeploy $LINUXDEPLOY_TAG, linuxdeploy-plugin-qt $PLUGIN_QT_TAG, AppImage runtime $RUNTIME_TAG"
fetch_tool "linuxdeploy-$ARCH.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$ARCH.AppImage" \
    "$LINUXDEPLOY_SHA256" || abort "cannot get linuxdeploy"
fetch_tool "linuxdeploy-plugin-qt-$ARCH.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$PLUGIN_QT_TAG/linuxdeploy-plugin-qt-$ARCH.AppImage" \
    "$PLUGIN_QT_SHA256" || abort "cannot get linuxdeploy-plugin-qt"
fetch_tool "runtime-$ARCH" "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_TAG/runtime-$ARCH" \
    "$RUNTIME_SHA256" || abort "cannot get the AppImage runtime"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-$ARCH.AppImage"
# Without FUSE (containers, some CI runners) the AppImages unpack themselves into a temporary folder.
if [ ! -e /dev/fuse ] || { ! command -v fusermount >/dev/null 2>&1 && ! command -v fusermount3 >/dev/null 2>&1; }; then
    export APPIMAGE_EXTRACT_AND_RUN=1
    echo "no FUSE: AppImages run with APPIMAGE_EXTRACT_AND_RUN=1"
fi

echo "== build ($BUILD_DIR)"
# SDL3 for gamepads comes from its cache entry, built once (decision E-540).
limited 1200 "$PROJECT_DIR/scripts/build-sdl.sh" >"$WORK/build-sdl.txt" 2>"$WORK/build-sdl.log" ||
    abort "SDL3 build failed, see $WORK/build-sdl.log"
SDL_PREFIX=$(tail -1 "$WORK/build-sdl.txt")
# A release compiles without ccache (E-890) and with static project libraries.
limited 300 cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" -DQC_BUILD_NUMBER="$BUILD_NUMBER" -DQC_CCACHE=OFF -DQC_SHARED_LIBS=OFF \
    >"$WORK/configure.log" 2>&1 || abort "configure failed, see $WORK/configure.log"
grep -q '^QC_TLS:BOOL=ON$' "$BUILD_DIR/CMakeCache.txt" ||
    abort "the build has no TLS: a package must encrypt multiplayer connections"
grep -q '^QC_GAMEPAD:BOOL=ON$' "$BUILD_DIR/CMakeCache.txt" || abort "the build has no gamepad support (QC_GAMEPAD)"
PROGRESS_PID=""
if [ "${QTCRAFT_RELEASE_PROGRESS:-0}" = 1 ]; then
    (while sleep 60; do tail -n 1 "$WORK/build.log" 2>/dev/null | cut -c1-120; done) &
    PROGRESS_PID=$!
    trap 'kill "$PROGRESS_PID" 2>/dev/null' EXIT
    trap 'kill "$PROGRESS_PID" 2>/dev/null; exit 1' INT TERM HUP
fi
if ! limited "$BUILD_LIMIT" cmake --build "$BUILD_DIR" --target qtcraft qtcraft-headless -j "${QC_BUILD_JOBS:-$(nproc)}" >"$WORK/build.log" 2>&1; then
    [ -n "$PROGRESS_PID" ] && kill "$PROGRESS_PID" 2>/dev/null
trap - EXIT INT TERM HUP
    tail -n 40 "$WORK/build.log"
    abort "build failed or took longer than $BUILD_LIMIT s, see $WORK/build.log"
fi
[ -n "$PROGRESS_PID" ] && kill "$PROGRESS_PID" 2>/dev/null
trap - EXIT INT TERM HUP

echo "== assemble $APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" ||
    abort "cannot create the AppDir"
cp "$BUILD_DIR/src/app/qtcraft" "$BUILD_DIR/src/headless/qtcraft-headless" "$APPDIR/usr/bin/" ||
    abort "cannot copy the executables"
# The example mods sit next to the executables, where QcBundledMods looks for them.
for mod in "$PROJECT_DIR"/mods/*/; do
    [ -f "$mod/pack.json" ] || continue
    mkdir -p "$APPDIR/usr/bin/mods" && cp -R "$mod" "$APPDIR/usr/bin/mods/$(basename "$mod")" || abort "cannot copy $mod"
done
find "$APPDIR/usr/bin/mods" -name '.*' -exec rm -rf {} + 2>/dev/null
cp "$PROJECT_DIR/scripts/linux/qtcraft.desktop" "$APPDIR/usr/share/applications/" || abort "cannot copy the desktop file"
# The icon is drawn from the base game's procedural textures, as on macOS and Windows.
limited 120 "$BUILD_DIR/src/headless/qtcraft-headless" --export-icon "$WORK/QtCraft.iconset" >"$WORK/icon.log" 2>&1 &&
    cp "$WORK/QtCraft.iconset/icon_256x256.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/qtcraft.png" ||
    abort "cannot draw the icon, see $WORK/icon.log"

. "$PROJECT_DIR/scripts/linux/release-licenses.sh"
DOC="$APPDIR/usr/share/doc/qtcraft"
write_licenses "$DOC/licenses" || abort "cannot write the licence texts"
sed -e "s/@VERSION@/$VERSION/g" -e "s/@COMMIT@/$COMMIT/g" -e "s/@ARCH@/$ARCH/g" \
    "$PROJECT_DIR/scripts/linux/README-Linux.txt" >"$DOC/README-Linux.txt" || abort "cannot write README-Linux.txt"

echo "== linuxdeploy"
# First the libraries and plugins (no output yet), so that what the game must not carry can be
# removed before the AppImage is packed: the GStreamer backend (the game plays through FFmpeg) and
# the SQL drivers whose client libraries are not part of Qt. OpenSSL stays a system library.
export QMAKE="$QT_DIR/bin/qmake" QML_SOURCES_PATHS="$PROJECT_DIR/src/app/qml" EXTRA_PLATFORM_PLUGINS=libqwayland.so
# linuxdeploy's own strip cannot read the packed relocations (.relr.dyn) of current distributions
# and fails; the Release binaries are left as the build and the distribution made them.
export NO_STRIP=1
# Qt's prebuilt FFmpeg asks for libbz2.so.1.0, the Debian name of bzip2's library; Fedora and others
# name it libbz2.so.1 only. Then a link under the Debian name lets linuxdeploy find and bundle it.
mkdir -p "$WORK/compat"
if ! /sbin/ldconfig -p 2>/dev/null | grep -q 'libbz2\.so\.1\.0 '; then
    bz2=$(/sbin/ldconfig -p 2>/dev/null | sed -n 's/.*libbz2\.so\.1 .*=> \(.*\)$/\1/p' | head -1)
    [ -n "$bz2" ] && ln -sf "$bz2" "$WORK/compat/libbz2.so.1.0" && echo "libbz2.so.1.0 from $bz2"
fi
export LD_LIBRARY_PATH="$QT_DIR/lib:$WORK/compat"
# The Qt plugin of linuxdeploy knows the Wayland platform plugin only under its older names
# (libqwayland-egl.so, libqwayland-generic.so): the shell, graphics and decoration plugins Qt's
# libqwayland.so loads are copied here, and linuxdeploy deploys what they need.
WAYLAND_PLUGINS="wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client"
set --
for folder in $WAYLAND_PLUGINS; do
    mkdir -p "$APPDIR/usr/plugins" && cp -R "$QT_DIR/plugins/$folder" "$APPDIR/usr/plugins/" || abort "cannot copy the plugins $folder"
    set -- "$@" --deploy-deps-only "$APPDIR/usr/plugins/$folder"
done
limited 900 "$LINUXDEPLOY" --appdir "$APPDIR" "$@" --executable "$APPDIR/usr/bin/qtcraft" \
    --executable "$APPDIR/usr/bin/qtcraft-headless" --desktop-file "$APPDIR/usr/share/applications/qtcraft.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/qtcraft.png" --custom-apprun "$PROJECT_DIR/scripts/linux/AppRun" \
    --exclude-library 'libssl.so*' --exclude-library 'libcrypto.so*' --plugin qt >"$WORK/linuxdeploy.log" 2>&1 ||
    abort "linuxdeploy failed, see $WORK/linuxdeploy.log"
rm -f "$APPDIR"/usr/plugins/multimedia/libgstreamermediaplugin.so
for driver in libqsqlmimer libqsqlodbc libqsqlpsql; do
    rm -f "$APPDIR/usr/plugins/sqldrivers/$driver.so"
done
# Libraries every desktop system has, taken from the system and not shipped (decision E-951; their
# licences would otherwise have to travel with the AppImage): OpenSSL (the Qt plugin bundles it despite
# --exclude-library, Qt's FFmpeg stubs ask for it), the PulseAudio client with libsndfile and its
# codecs, glib, D-Bus, libsystemd, X11 and xcb core extensions, xkbcommon, Wayland, brotli, zstd and
# PCRE2. What stays is Qt with its ICU, Qt's FFmpeg, the xcb-util libraries Qt's xcb plugin needs
# (often missing on Wayland desktops) and libbz2 (Qt's FFmpeg asks for the Debian name
# libbz2.so.1.0, which Fedora and openSUSE lack).
for pattern in 'libssl.so*' 'libcrypto.so*' 'libpulse*.so*' 'libsndfile.so*' 'libasyncns.so*' 'libglib-2.0.so*' \
    'libgobject-2.0.so*' 'libgmodule-2.0.so*' 'libdbus-1.so*' 'libsystemd.so*' 'libXau.so*' 'libXext.so*' \
    'libXrandr.so*' 'libXrender.so*' 'libxcb-glx.so*' 'libxcb-randr.so*' 'libxcb-render.so*' 'libxcb-shape.so*' \
    'libxcb-shm.so*' 'libxcb-sync.so*' 'libxcb-xfixes.so*' 'libxcb-xkb.so*' 'libxkbcommon*.so*' 'libwayland-*.so*' \
    'libbrotli*.so*' 'libzstd.so*' 'libpcre2-*.so*'; do
    find "$APPDIR/usr/lib" -maxdepth 1 -name "$pattern" -delete
done
prune_libraries "$APPDIR" >"$WORK/pruned.txt" || abort "cannot prune the libraries"
echo "removed $(wc -l <"$WORK/pruned.txt" | tr -d ' ') libraries nothing needs any more (list: $WORK/pruned.txt)"
write_bundled_licences "$DOC/licenses" || abort "cannot write the licences of the bundled libraries"
echo "bundled libraries besides Qt: $(cut -d' ' -f1 "$DOC/licenses/bundled-libraries.txt" | tr '\n' ' ')"
mkdir -p "$DEPLOY_DIR" || abort "cannot create $DEPLOY_DIR"
rm -f "$PACKAGE_BASE.AppImage" "$PACKAGE_BASE-local-only.AppImage" "$README"
unset LD_LIBRARY_PATH QMAKE QML_SOURCES_PATHS EXTRA_PLATFORM_PLUGINS NO_STRIP
# Packed by the appimagetool inside linuxdeploy, called directly: linuxdeploy's output step would
# deploy the dependencies again and bring back what was just removed.
APPIMAGETOOL="$TOOLS_DIR/extracted/squashfs-root/plugins/linuxdeploy-plugin-appimage/usr/bin/appimagetool"
# The unpacked copy is used only while its stamp names the SHA-256 of the checked linuxdeploy.
if [ ! -x "$APPIMAGETOOL" ] || [ "$(cat "$TOOLS_DIR/extracted/sha256" 2>/dev/null)" != "$LINUXDEPLOY_SHA256" ]; then
    rm -rf "$TOOLS_DIR/extracted" && mkdir -p "$TOOLS_DIR/extracted" &&
        (cd "$TOOLS_DIR/extracted" && "$LINUXDEPLOY" --appimage-extract >/dev/null) &&
        echo "$LINUXDEPLOY_SHA256" >"$TOOLS_DIR/extracted/sha256" || abort "cannot unpack linuxdeploy"
fi
ARCH=$ARCH limited 900 "$APPIMAGETOOL" --no-appstream --runtime-file "$TOOLS_DIR/runtime-$ARCH" "$APPDIR" "$APPIMAGE" \
    >"$WORK/appimage.log" 2>&1 || abort "packing the AppImage failed, see $WORK/appimage.log"
cp "$DOC/README-Linux.txt" "$README" || abort "cannot copy README-Linux.txt"

. "$PROJECT_DIR/scripts/linux/release-checks.sh"
check_runtime
check_contents
check_links
check_server
if [ "$GUI_RUNS" = 1 ]; then
    check_game
else
    echo "skip the starts of the game: QTCRAFT_RELEASE_GUI=0"
fi

echo "== result"
echo "AppImage: $APPIMAGE ($(du -h "$APPIMAGE" | cut -f1))"
echo "README:   $README"
if [ -n "$failures" ]; then
    echo "RELEASE CHECKS FAILED:$failures"
    echo "logs and screenshots: $WORK"
    exit 1
fi
if [ "$GUI_RUNS" = 1 ]; then
    cp "$WORK/menu.png" "$DEPLOY_DIR/release-check-linux-menu.png"
    cp "$WORK/world.png" "$DEPLOY_DIR/release-check-linux-world.png"
fi
rm -rf "$WORK"
if [ "$GUI_RUNS" = 1 ]; then
    echo "RELEASE OK (screenshots of the checks in $DEPLOY_DIR)"
else
    echo "RELEASE OK WITHOUT STARTS OF THE GAME (QTCRAFT_RELEASE_GUI=0): try the package once before publishing it"
fi
