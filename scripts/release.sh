#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Release pipeline (macOS): builds the game in its own Release build directory (preset "deploy",
# build/deploy), assembles a standalone deploy/QtCraft.app with macdeployqt (Qt frameworks, QML
# modules, platform, image format and multimedia plugins with the FFmpeg libraries), adds the
# dedicated server (Contents/MacOS/qtcraft-headless, on the same frameworks) and the example mods,
# signs it ad hoc, checks the result and packs it as .zip and .dmg next to it.
#
# Checks (any failure makes the script exit with 1):
#   - every Mach-O file links only to the bundle (@rpath, @executable_path, @loader_path) or to the
#     system, and has no rpath or install name pointing at the build or Qt directory (otool)
#   - required parts are present (Info.plist keys, icon, qt.conf, frameworks, plugins, QML modules,
#     FFmpeg libraries, example mod) and the signature verifies (codesign --verify --strict)
#   - the game and the dedicated server are universal (arm64 and x86_64)
#   - with gamepad support (QC_GAMEPAD, decision E-540): SDL3 is linked statically into the game for
#     both architectures, the bundle contains no SDL library and carries the licence texts of SDL,
#     HIDAPI and SDL's libm (THIRD_PARTY.md)
#   - with TLS (QC_TLS, decision E-872): the bundled OpenSSL (libssl.3.dylib, libcrypto.3.dylib) is
#     in Contents/Frameworks for both architectures, Qt's OpenSSL backend in PlugIns/tls, and the
#     bundle carries OpenSSL's licence text (Apache 2.0)
#   - the bundle carries the game's LICENSE, THIRD_PARTY.md and CREDITS.md, the licence of Pixelify
#     Sans, FFmpeg's LGPL 2.1 text, a Qt notice and, with open-source Qt, Qt's LGPL 3 and GPL 3 texts
#     (Contents/Resources/licenses); a local-only package carries NOT-FOR-DISTRIBUTION.txt
#   - the dedicated server of a copy starts (--server --port 0 --bind 127.0.0.1, temporary data
#     directory), reports the certificate fingerprint (with TLS) and "Listening on port", loads
#     only libraries from the copy or the system
#     (DYLD_PRINT_LIBRARIES) and stops with exit code 0 on SIGTERM
#   - a copy of the bundle in a temporary directory starts a world and saves a screenshot, loads
#     only libraries from inside the copy or the system (DYLD_PRINT_LIBRARIES), opens the audio
#     output (muted through settings.ini), installs the example mod switched off on the first start;
#     a second start with the mod switched on and German texts loads both; a third start goes
#     through Launch Services (open) like a double click; a fourth runs the Intel half under
#     Rosetta (arch -x86_64) when Rosetta is installed; all starts open the audio output and keep
#     QML and pipeline caches in the temporary directory, not in ~/Library/Caches
# Every run opens on the built-in display in the background (--screen builtin --background): no
# focus, no captured mouse, sound muted. Temporary data lives in a directory below
# QTCRAFT_RELEASE_TMP (default: $TMPDIR) and is removed when all checks pass.
#
# Usage: scripts/release.sh            (QT_DIR overrides the newest ~/Qt/6.8.*/macos; QTCRAFT_RELEASE_DMG=0
#                                       skips the disk image; QTCRAFT_RELEASE_PRESET overrides the
#                                       preset "deploy"; QTCRAFT_RELEASE_BUILD_LIMIT the build's
#                                       seconds (1800); QTCRAFT_RELEASE_PROGRESS=1 prints the build's
#                                       progress once a minute; QTCRAFT_RELEASE_GUI=0 skips the starts of
#                                       the game and keeps the static and dedicated-server checks,
#                                       for machines without a display such as GitHub Actions;
#                                       QTCRAFT_RELEASE_OPEN_SOURCE_QT=1 declares QT_DIR an
#                                       open-source Qt, without it the package is local-only)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
QT_DIR=${QT_DIR:-$(ls -d "$HOME"/Qt/6.8.*/macos 2>/dev/null | sort -V | tail -n 1)}
PRESET=${QTCRAFT_RELEASE_PRESET:-deploy}
# Seconds the build may take (the hosted CI runners need far longer than a development Mac).
BUILD_LIMIT=${QTCRAFT_RELEASE_BUILD_LIMIT:-1800}
GUI_RUNS=${QTCRAFT_RELEASE_GUI:-1}
case "$GUI_RUNS" in
0 | 1) ;;
*) echo "QTCRAFT_RELEASE_GUI must be 0 or 1, not '$GUI_RUNS'" >&2; exit 2 ;;
esac
BUILD_DIR="$PROJECT_DIR/build/$PRESET"
DEPLOY_DIR="$PROJECT_DIR/deploy"
APP="$DEPLOY_DIR/QtCraft.app"
VERSION=$(sed -n 's/^project(QtCraft VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
# CFBundleVersion: UTC build time as year.monthday.hourminute (e.g. 2026.1009.1130). It grows with
# every build on any branch; a commit count would shrink after a squash merge.
BUILD_NUMBER=$(date -u +%Y.%m%d.%H%M)
PACKAGE_BASE="$DEPLOY_DIR/QtCraft-$VERSION-macOS"
PACKAGE=$PACKAGE_BASE
TMP_ROOT=${QTCRAFT_RELEASE_TMP:-${TMPDIR:-/tmp}}
mkdir -p "$TMP_ROOT" || exit 2
WORK=$(mktemp -d "$TMP_ROOT/qtcraft-release.XXXXXX") || exit 2
failures=""

limited() {
    perl -e 'alarm shift; exec @ARGV or die' "$@"
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

# Mach-O files of a bundle (executables, dylibs, framework binaries), one per line.
macho_files() {
    find "$1" -type f ! -name '*.qml' ! -name '*.json' ! -name '*.png' ! -name '*.qmltypes' ! -name '*.plist' -print |
        while IFS= read -r file; do
            case "$(file -b "$file")" in
            Mach-O*) echo "$file" ;;
            esac
        done
}

echo "== QtCraft $VERSION (build $BUILD_NUMBER), Qt from $QT_DIR"
[ -x "$QT_DIR/bin/macdeployqt" ] || abort "macdeployqt not found in $QT_DIR/bin"

# Packages for others come only from the release workflow, built against open-source Qt (decision
# E-932). A package is for local tests only unless QTCRAFT_RELEASE_OPEN_SOURCE_QT=1 says that QT_DIR
# is an open-source Qt (.github/workflows/release.yml sets it): its name ends in -local-only, the
# bundle carries NOT-FOR-DISTRIBUTION.txt and a Qt notice without the LGPL. A Qt installation with
# commercial licence files (the online installer puts them into <Qt>/Licenses or <Qt>/licenseInfo.txt)
# keeps it local-only even then: whoever builds with a commercially licensed Qt still gets a working
# package, but only for their own use.
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
if [ "$LOCAL_ONLY" -eq 1 ]; then
    PACKAGE="$PACKAGE-local-only"
    [ "$OPEN_SOURCE_QT" = 1 ] && echo "WARNING: QTCRAFT_RELEASE_OPEN_SOURCE_QT=1 is ignored for a Qt with commercial licence files."
    echo "WARNING: local-only package: not for distribution; packages for others come from the release"
    echo "WARNING: workflow with open-source Qt (QTCRAFT_RELEASE_OPEN_SOURCE_QT=1, decision E-932)."
fi

echo "== build (preset $PRESET, $BUILD_DIR)"
"$PROJECT_DIR/scripts/ensure-user-presets.sh" || abort "CMakeUserPresets.json missing and not created"
# The universal SDL3 for gamepads comes from its own cache entry, built once (decision E-540).
limited 1200 "$PROJECT_DIR/scripts/build-sdl.sh" --universal >"$WORK/build-sdl.txt" 2>"$WORK/build-sdl.log" ||
    abort "SDL3 build failed, see $WORK/build-sdl.log"
SDL_PREFIX=$(tail -1 "$WORK/build-sdl.txt")
# The universal OpenSSL for TLS likewise (decision E-872); configure links the game against it.
limited 1200 "$PROJECT_DIR/scripts/build-openssl.sh" >"$WORK/build-openssl.txt" 2>"$WORK/build-openssl.log" ||
    abort "OpenSSL build failed, see $WORK/build-openssl.log"
OPENSSL_PREFIX=$(tail -1 "$WORK/build-openssl.txt")
# A release compiles without ccache (E-890): its shared cache hashes paths relative to the main
# checkout, so file names in a cached object could point into another worktree.
limited 300 cmake --preset "$PRESET" -DQC_BUILD_NUMBER="$BUILD_NUMBER" -DQC_CCACHE=OFF >"$WORK/configure.log" 2>&1 || abort "configure failed"
# The package carries the program alone (macdeployqt copies Qt, not project libraries): the project
# libraries must be static (QC_SHARED_LIBS is for debug and sanitizer trees only, E-890).
grep -q '^QC_SHARED_LIBS:BOOL=ON$' "$BUILD_DIR/CMakeCache.txt" &&
    abort "build/$PRESET has QC_SHARED_LIBS=ON; a release needs static project libraries (reconfigure with -DQC_SHARED_LIBS=OFF)"
# QTCRAFT_RELEASE_PROGRESS=1 (the release workflow) prints the build's last line once a minute, so
# a long build on a hosted runner shows that it moves.
PROGRESS_PID=""
if [ "${QTCRAFT_RELEASE_PROGRESS:-0}" = 1 ]; then
    (while sleep 60; do tail -n 1 "$WORK/build.log" 2>/dev/null | cut -c1-120; done) &
    PROGRESS_PID=$!
fi
if ! limited "$BUILD_LIMIT" cmake --build --preset "$PRESET" --target qtcraft qtcraft-headless >"$WORK/build.log" 2>&1; then
    [ -n "$PROGRESS_PID" ] && kill "$PROGRESS_PID" 2>/dev/null
    tail -n 40 "$WORK/build.log"
    abort "build failed or took longer than $BUILD_LIMIT s, see $WORK/build.log"
fi
[ -n "$PROGRESS_PID" ] && kill "$PROGRESS_PID" 2>/dev/null

echo "== assemble $APP"
mkdir -p "$DEPLOY_DIR" || abort "cannot create $DEPLOY_DIR"
# Only the results of earlier runs of this script are replaced.
rm -rf "$APP" "$PACKAGE_BASE.zip" "$PACKAGE_BASE.dmg" "$PACKAGE_BASE-local-only.zip" "$PACKAGE_BASE-local-only.dmg"
ditto "$BUILD_DIR/src/app/qtcraft.app" "$APP" || abort "cannot copy the app bundle"
# The dedicated server runs from the bundle on the game's frameworks (docs/Multiplayer.md).
SERVER="$APP/Contents/MacOS/qtcraft-headless"
ditto "$BUILD_DIR/src/headless/qtcraft-headless" "$SERVER" || abort "cannot copy the dedicated server"
mkdir -p "$APP/Contents/Resources/mods" || abort "cannot create the mods folder"
for mod in "$PROJECT_DIR"/mods/*/; do
    [ -f "$mod/pack.json" ] || continue
    rsync -a --exclude '.*' "$mod" "$APP/Contents/Resources/mods/$(basename "$mod")/" || abort "cannot copy $mod"
done

echo "== macdeployqt"
limited 600 "$QT_DIR/bin/macdeployqt" "$APP" -qmldir="$PROJECT_DIR/src/app/qml" -executable="$SERVER" -verbose=1 >"$WORK/macdeployqt.log" 2>&1 ||
    abort "macdeployqt failed, see $WORK/macdeployqt.log"
# SQL drivers come with QtSql (pulled in by QtQuick.LocalStorage); the game keeps no data in SQL.
# The Mimer, ODBC and PostgreSQL drivers need client libraries that are not part of Qt (macdeployqt
# reports them as missing), so they are left out; SQLite needs nothing else and stays.
grep "ERROR" "$WORK/macdeployqt.log" | grep -v -e 'libmimerapi\.dylib' -e 'libiodbc\.2\.dylib' -e 'libpq\.5\.dylib' >"$WORK/macdeployqt-errors.txt"
if [ -s "$WORK/macdeployqt-errors.txt" ]; then
    cat "$WORK/macdeployqt-errors.txt"
    abort "macdeployqt reported errors"
fi
for driver in libqsqlmimer libqsqlodbc libqsqlpsql; do
    rm -f "$APP/Contents/PlugIns/sqldrivers/$driver.dylib"
done
GAMEPAD=$(sed -n 's/^QC_GAMEPAD:BOOL=//p' "$BUILD_DIR/CMakeCache.txt")
if [ "$GAMEPAD" = ON ]; then
    LICENSES="$APP/Contents/Resources/licenses"
    mkdir -p "$LICENSES" && cp "$SDL_PREFIX/LICENSE.txt" "$LICENSES/SDL3-LICENSE.txt" &&
        cp "$SDL_PREFIX/HIDAPI-LICENSE.txt" "$LICENSES/HIDAPI-LICENSE.txt" &&
        cp "$SDL_PREFIX/LIBM-NOTICE.txt" "$LICENSES/SDL3-LIBM-NOTICE.txt" ||
        abort "cannot copy the licence texts of SDL3"
fi
TLS=$(sed -n 's/^QC_TLS:BOOL=//p' "$BUILD_DIR/CMakeCache.txt")
if [ "$TLS" = ON ]; then
    mkdir -p "$APP/Contents/Resources/licenses" && cp "$OPENSSL_PREFIX/LICENSE.txt" "$APP/Contents/Resources/licenses/OpenSSL-LICENSE.txt" ||
        abort "cannot copy the licence text of OpenSSL"
fi
# The logo's pixel font is built into the game's resources; its licence (SIL OFL 1.1) asks that
# every copy carries the licence text.
mkdir -p "$APP/Contents/Resources/licenses" &&
    cp "$PROJECT_DIR/src/app/fonts/OFL.txt" "$APP/Contents/Resources/licenses/PixelifySans-OFL.txt" ||
    abort "cannot copy the licence text of Pixelify Sans"
# The game's own licence and the lists of third-party parts, next to the licence texts.
for file in LICENSE THIRD_PARTY.md CREDITS.md; do
    cp "$PROJECT_DIR/$file" "$APP/Contents/Resources/licenses/QtCraft-$file" || abort "cannot copy $file"
done
# Qt Multimedia's FFmpeg libraries are under the LGPL 2.1 or later; Qt (frameworks, plugins, QML
# modules) is used under the LGPL 3, which builds on the GPL 3, when it is an open-source Qt
# (THIRD_PARTY.md). The LGPL texts come from licenses-third-party/ in the repository; the notice
# names the versions and where their exact sources are (decision E-932).
LICENSES="$APP/Contents/Resources/licenses"
cp "$PROJECT_DIR/licenses-third-party/LGPL-2.1-or-later.txt" "$LICENSES/FFmpeg-LGPL-2.1.txt" ||
    abort "cannot copy the licence text of FFmpeg"
QT_VERSION=$("$QT_DIR/bin/qtpaths" --query QT_VERSION 2>/dev/null)
[ -n "$QT_VERSION" ] || abort "cannot read the Qt version from $QT_DIR/bin/qtpaths"
QT_SERIES=${QT_VERSION%.*}
FFMPEG_LIBRARIES=$(cd "$APP/Contents/Frameworks" && ls libav*.dylib libsw*.dylib 2>/dev/null | tr '\n' ' ')
# The FFmpeg tag Qt builds from is named in Qt's provisioning script (n7.1 for Qt 6.8.3).
FFMPEG_SCRIPT="coin/provisioning/common/unix/install-ffmpeg.sh of the repository qt/qt5 at the tag v$QT_VERSION"
case "$QT_VERSION" in
6.8.3) FFMPEG_TAG="FFmpeg n7.1 (named in $FFMPEG_SCRIPT)" FFMPEG_URL_TAG=n7.1 ;;
*) FFMPEG_TAG="the FFmpeg tag named in $FFMPEG_SCRIPT" FFMPEG_URL_TAG="<tag>" ;;
esac
if [ "$LOCAL_ONLY" -eq 0 ]; then
    cp "$PROJECT_DIR/licenses-third-party/LGPL-3.0-only.txt" "$LICENSES/Qt-LGPL-3.0.txt" &&
        cp "$PROJECT_DIR/LICENSE" "$LICENSES/Qt-GPL-3.0.txt" ||
        abort "cannot copy the licence texts of Qt"
    cat >"$LICENSES/Qt-NOTICE.txt" <<EOF || abort "cannot write the Qt notice"
Qt $QT_VERSION

This bundle contains the frameworks, plugins and QML modules of Qt $QT_VERSION (The Qt Company Ltd.
and other contributors) as separate, unmodified files in Contents/Frameworks, Contents/PlugIns and
Contents/Resources/qml. They are used under the GNU Lesser General Public License version 3
(Qt-LGPL-3.0.txt, which builds on Qt-GPL-3.0.txt); you may replace them with your own build of the
same Qt version. Note that macdeployqt rewrote their install names to @rpath and the bundle is
signed: after replacing a framework, sign the bundle again (codesign --force --deep -s - QtCraft.app).
Qt contains code of other projects (for example libpng, libjpeg, PCRE2, HarfBuzz, double-conversion,
TinyCBOR); their licences and notices are listed in the software bills of materials in the folder
Qt-SBOM, where present, and in the Qt documentation ("Licenses Used in Qt").

Qt Multimedia plays sound through FFmpeg. The FFmpeg libraries in Contents/Frameworks
($FFMPEG_LIBRARIES) are the unmodified LGPL build that comes with Qt $QT_VERSION; FFmpeg is
licensed under the GNU Lesser General Public License version 2.1 or later (FFmpeg-LGPL-2.1.txt).
You may replace them with your own build of the same FFmpeg version (sign again as above).

Corresponding source code:
- Qt $QT_VERSION: https://download.qt.io/archive/qt/$QT_SERIES/$QT_VERSION/single/qt-everywhere-src-$QT_VERSION.tar.xz
  (the same folder also has a .zip and md5sums.txt).
- FFmpeg: $FFMPEG_TAG,
  https://github.com/FFmpeg/FFmpeg/archive/refs/tags/$FFMPEG_URL_TAG.tar.gz (also from ffmpeg.org); the build
  options are in that script.
The QtCraft maintainers also send these sources on request.
EOF
else
    cat >"$LICENSES/Qt-NOTICE.txt" <<EOF || abort "cannot write the Qt notice"
Qt $QT_VERSION (local tests only: not declared as open-source Qt)

This bundle was built for local tests with the Qt installation $QT_DIR, which is not declared as
open-source Qt. Its Qt frameworks, plugins and QML modules are not passed on under the GNU LGPL by
this bundle; do not distribute it (NOT-FOR-DISTRIBUTION.txt). Packages for others come from the
release workflow, built against open-source Qt.

The FFmpeg libraries in Contents/Frameworks ($FFMPEG_LIBRARIES) are licensed under the GNU Lesser
General Public License version 2.1 or later (FFmpeg-LGPL-2.1.txt); source: $FFMPEG_TAG.
EOF
    printf '%s\n' "This QtCraft bundle was built with a Qt that is not declared as open-source Qt ($QT_DIR), for local tests only." \
        "Do not pass it on: packages for others come from the release workflow with open-source Qt." \
        >"$APP/Contents/Resources/NOT-FOR-DISTRIBUTION.txt" || abort "cannot write the local-only notice"
fi
# Qt's software bills of materials for the modules in the bundle, when the Qt installation has them.
for module in qtbase qtdeclarative qtmultimedia qtimageformats qtsvg; do
    sbom="$QT_DIR/sbom/$module-$QT_VERSION.spdx"
    [ -f "$sbom" ] || continue
    mkdir -p "$APP/Contents/Resources/licenses/Qt-SBOM" && cp "$sbom" "$APP/Contents/Resources/licenses/Qt-SBOM/" ||
        abort "cannot copy $sbom"
done

echo "== ad-hoc signature (inside out)"
# Loose libraries and plugins first, then each framework as a whole, then the app.
macho_files "$APP/Contents" | grep -v '\.framework/' >"$WORK/loose-binaries.txt"
while IFS= read -r binary; do
    [ "$binary" = "$APP/Contents/MacOS/qtcraft" ] && continue
    codesign --force --sign - --timestamp=none "$binary" >>"$WORK/codesign.log" 2>&1 || abort "cannot sign $binary"
done <"$WORK/loose-binaries.txt"
for framework in "$APP"/Contents/Frameworks/*.framework; do
    [ -d "$framework" ] || continue
    codesign --force --sign - --timestamp=none "$framework" >>"$WORK/codesign.log" 2>&1 || abort "cannot sign $framework"
done
codesign --force --sign - --timestamp=none "$APP" >>"$WORK/codesign.log" 2>&1 || abort "cannot sign the app"

echo "== checks"
if codesign --verify --deep --strict "$APP" >"$WORK/verify.log" 2>&1; then
    ok "signature verifies"
else
    problem "signature does not verify ($WORK/verify.log)"
fi

# Links and rpaths: nothing may point at the build tree, the source tree or a Qt installation.
macho_files "$APP" >"$WORK/binaries.txt"
binaries=$(wc -l <"$WORK/binaries.txt" | tr -d ' ')
bad_links=0
while IFS= read -r binary; do
    otool -L "$binary" | grep '^[[:space:]]' | sed 's/^[[:space:]]*//; s/ (compatibility.*//' | sort -u >"$WORK/links.txt"
    while IFS= read -r link; do
        case "$link" in
        @rpath/* | @executable_path/* | @loader_path/* | /System/Library/* | /usr/lib/*) ;;
        *)
            problem "$binary links to $link"
            bad_links=$((bad_links + 1))
            ;;
        esac
    done <"$WORK/links.txt"
    otool -l "$binary" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' >"$WORK/rpaths.txt"
    while IFS= read -r rpath; do
        case "$rpath" in
        @executable_path | @loader_path | @executable_path/* | @loader_path/*) ;;
        *)
            problem "$binary has rpath $rpath"
            bad_links=$((bad_links + 1))
            ;;
        esac
    done <"$WORK/rpaths.txt"
done <"$WORK/binaries.txt"
for executable in "$APP/Contents/MacOS/qtcraft" "$SERVER"; do
    if strings -a "$executable" | grep -q -e "$BUILD_DIR" -e "$QT_DIR/lib"; then
        problem "$(basename "$executable") contains a path into the build or Qt directory"
        bad_links=$((bad_links + 1))
    fi
done
[ "$bad_links" -eq 0 ] && ok "otool: $binaries Mach-O files link only into the bundle and the system"

# Universal executables: Apple silicon and Intel Macs (Qt and FFmpeg come universal as well).
for executable in "$APP/Contents/MacOS/qtcraft" "$SERVER"; do
    architectures=$(lipo -archs "$executable" 2>/dev/null)
    if echo " $architectures " | grep -q ' arm64 ' && echo " $architectures " | grep -q ' x86_64 '; then
        ok "$(basename "$executable") for $architectures"
    else
        problem "$(basename "$executable") is not universal ($architectures)"
    fi
done

# SDL3 is linked into the game itself (static, both halves), never shipped as a library. The
# executable is stripped, so the check looks for a text of SDL's gamepad code instead of a symbol.
if [ "$GAMEPAD" = ON ]; then
    for arch in arm64 x86_64; do
        if lipo -thin "$arch" "$APP/Contents/MacOS/qtcraft" -output "$WORK/qtcraft-$arch" 2>/dev/null &&
            grep -q -a 'SDL_GAMECONTROLLERCONFIG' "$WORK/qtcraft-$arch"; then
            ok "SDL3 linked statically ($arch)"
        else
            problem "SDL3 is not linked into the $arch game"
        fi
    done
    sdl_files=$(find "$APP" -iname '*SDL*' ! -path "$APP/Contents/Resources/licenses/SDL3-*.txt")
    if [ -n "$sdl_files" ]; then
        problem "the bundle contains SDL files: $(echo "$sdl_files" | head -3)"
    else
        ok "no SDL library in the bundle"
    fi
    for licence in SDL3-LICENSE.txt HIDAPI-LICENSE.txt SDL3-LIBM-NOTICE.txt; do
        [ -s "$APP/Contents/Resources/licenses/$licence" ] && ok "contains $licence" || problem "missing $licence"
    done
else
    ok "built without gamepad support (QC_GAMEPAD=$GAMEPAD), no SDL checks"
fi
if [ "$LOCAL_ONLY" -eq 0 ]; then
    QT_TEXTS="Qt-LGPL-3.0.txt Qt-GPL-3.0.txt"
else
    QT_TEXTS=""
    [ -s "$APP/Contents/Resources/NOT-FOR-DISTRIBUTION.txt" ] && ok "local-only: contains NOT-FOR-DISTRIBUTION.txt" ||
        problem "missing NOT-FOR-DISTRIBUTION.txt"
fi
for licence in PixelifySans-OFL.txt QtCraft-LICENSE QtCraft-THIRD_PARTY.md QtCraft-CREDITS.md $QT_TEXTS \
    Qt-NOTICE.txt FFmpeg-LGPL-2.1.txt; do
    [ -s "$APP/Contents/Resources/licenses/$licence" ] && ok "contains $licence" || problem "missing $licence"
done

# OpenSSL comes as two libraries next to the Qt frameworks; Qt's backend opens libssl by name and
# finds it loaded by the executables (QcTlsBackend). Both halves, like the executables.
if [ "$TLS" = ON ]; then
    for part in Frameworks/libssl.3.dylib Frameworks/libcrypto.3.dylib PlugIns/tls/libqopensslbackend.dylib; do
        architectures=$(lipo -archs "$APP/Contents/$part" 2>/dev/null)
        if echo " $architectures " | grep -q ' arm64 ' && echo " $architectures " | grep -q ' x86_64 '; then
            ok "contains $part ($architectures)"
        else
            problem "missing or not universal: $part ($architectures)"
        fi
    done
    [ -s "$APP/Contents/Resources/licenses/OpenSSL-LICENSE.txt" ] && ok "contains OpenSSL-LICENSE.txt" || problem "missing OpenSSL-LICENSE.txt"
else
    problem "built without TLS (QC_TLS=$TLS): a release must encrypt multiplayer connections"
fi

plist="$APP/Contents/Info.plist"
for key in CFBundleIdentifier CFBundleShortVersionString CFBundleVersion CFBundleIconFile LSMinimumSystemVersion \
    LSApplicationCategoryType NSHumanReadableCopyright NSHighResolutionCapable; do
    value=$(plutil -extract "$key" raw -o - "$plist" 2>/dev/null)
    if [ -n "$value" ]; then
        ok "Info.plist $key = $value"
    else
        problem "Info.plist has no $key"
    fi
done
[ "$(plutil -extract CFBundleShortVersionString raw -o - "$plist" 2>/dev/null)" = "$VERSION" ] || problem "CFBundleShortVersionString is not $VERSION"
[ "$(plutil -extract CFBundleVersion raw -o - "$plist" 2>/dev/null)" = "$BUILD_NUMBER" ] || problem "CFBundleVersion is not $BUILD_NUMBER"

for part in Contents/MacOS/qtcraft-headless Contents/Resources/QtCraft.icns Contents/Resources/qt.conf \
    Contents/Frameworks/QtCore.framework Contents/Frameworks/QtGui.framework Contents/Frameworks/QtQuick.framework \
    Contents/Frameworks/QtQml.framework Contents/Frameworks/QtQuickControls2.framework Contents/Frameworks/QtMultimedia.framework \
    Contents/PlugIns/platforms/libqcocoa.dylib Contents/PlugIns/imageformats/libqjpeg.dylib \
    Contents/PlugIns/multimedia/libffmpegmediaplugin.dylib Contents/Resources/qml/QtQuick/Controls \
    Contents/Resources/qml/QtQuick/Layouts Contents/Resources/mods/rubies/pack.json; do
    if [ -e "$APP/$part" ]; then
        ok "contains $part"
    else
        problem "missing $part"
    fi
done
for library in libavcodec libavformat libavutil libswresample libswscale; do
    if ls "$APP/Contents/Frameworks/$library".*dylib >/dev/null 2>&1; then
        ok "contains FFmpeg $library"
    else
        problem "missing FFmpeg $library"
    fi
done

# Runs the copy of the bundle with a clean environment (no Qt paths from the shell). Perl sets
# DYLD_PRINT_LIBRARIES itself: the system strips DYLD_ variables when it starts env or perl (and
# arch, so runs under Rosetta print no library list). Caches stay in the temporary directory
# ($CACHE_ENV), not in the player's ~/Library/Caches.
# Usage: [RUN_ARCH=x86_64] run_copy <name> <data dir> <args...>
run_copy() {
    name=$1
    data=$2
    shift 2
    if [ -n "${RUN_ARCH:-}" ]; then
        set -- arch "-$RUN_ARCH" "$RUN_APP/Contents/MacOS/qtcraft" --screen builtin --background "$@"
    else
        set -- "$RUN_APP/Contents/MacOS/qtcraft" --screen builtin --background "$@"
    fi
    # shellcheck disable=SC2086
    env -i HOME="$HOME" USER="${USER:-}" PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" $CACHE_ENV \
        QTCRAFT_SCREEN=builtin QTCRAFT_BACKGROUND=1 QTCRAFT_AUDIO=1 QTCRAFT_DATA_DIR="$data" \
        QTCRAFT_SCREENSHOT="$WORK/$name.png" QTCRAFT_SCREENSHOT_FRAMES=90 \
        perl -e '$ENV{DYLD_PRINT_LIBRARIES} = 1; alarm shift; exec @ARGV or die' 180 "$@" \
        >"$WORK/$name.log" 2>&1
}

# The log of run $1 names the audio output (sound is mixed, settings.ini mutes it).
check_audio() {
    if grep -q "Audio output: " "$WORK/$1.log"; then
        ok "$1: $(grep -o 'Audio output: .*' "$WORK/$1.log" | head -1)"
    else
        problem "$1: no audio output in the log"
    fi
}

# Screenshot exists and is not a blank image (a flat PNG of this size is a few KiB).
check_screenshot() {
    size=$(stat -f %z "$1" 2>/dev/null || echo 0)
    if [ "$size" -gt 50000 ]; then
        ok "screenshot $(basename "$1") ($((size / 1024)) KiB)"
    else
        problem "screenshot $1 missing or blank ($size bytes)"
    fi
}

# Every image dyld loaded (log of a run with DYLD_PRINT_LIBRARIES) comes from the copy or the system.
check_bundle_libraries() {
    log=$1
    real_app=$(cd "$RUN_APP" && pwd -P)
    sed -n 's/^dyld\[[0-9]*\]: \(<[^>]*> \)\{0,1\}//p' "$log" | grep '^/' | sort -u >"$log.libraries"
    loaded=$(wc -l <"$log.libraries" | tr -d ' ')
    outside=$(grep -v -e "^$real_app/" -e "^$RUN_APP/" -e '^/System/' -e '^/usr/lib/' "$log.libraries")
    if [ "$loaded" -eq 0 ]; then
        problem "no library list in $log"
    elif [ -n "$outside" ]; then
        problem "libraries loaded from outside the bundle: $(echo "$outside" | tr '\n' ' ')"
    else
        ok "$(basename "$log" .log): $loaded loaded images, all from the bundle copy or the system"
    fi
}

# The game's libraries, and its multimedia backend from the bundle.
check_loaded_libraries() {
    log=$1
    check_bundle_libraries "$log"
    if grep -q -e "^$real_app/Contents/PlugIns/multimedia/libffmpegmediaplugin.dylib" -e "^$RUN_APP/Contents/PlugIns/multimedia/libffmpegmediaplugin.dylib" "$log.libraries" &&
        grep -q "/Contents/Frameworks/libavcodec" "$log.libraries"; then
        ok "FFmpeg multimedia backend loaded from the bundle"
    else
        problem "FFmpeg multimedia backend not loaded from the bundle"
    fi
}

echo "== start a copy in $WORK/copy"
RUN_APP="$WORK/copy/QtCraft.app"
# QML byte code and the graphics pipeline cache would otherwise go to ~/Library/Caches/QtCraft.
mkdir -p "$WORK/cache/qml"
CACHE_ENV="QML_DISK_CACHE_PATH=$WORK/cache/qml QSG_RHI_DISABLE_DISK_CACHE=1"
touch "$WORK/started"
mkdir -p "$WORK/copy" && ditto "$APP" "$RUN_APP" || abort "cannot copy the bundle"
DATA="$WORK/data"
mkdir -p "$DATA" && printf '[audio]\nmuted=true\n' >"$DATA/settings.ini"

# The starts of the game need a display; without one only the static and server checks run.
if [ "$GUI_RUNS" = 1 ]; then
    if run_copy world "$DATA" --play --seed 42 --look 45,-20; then
        ok "world run exited normally"
    else
        problem "world run failed (log: $WORK/world.log)"
    fi
    check_screenshot "$WORK/world.png"
    check_loaded_libraries "$WORK/world.log"
    check_audio world
    if grep -q "Installed bundled mods (switched off) in .*: rubies" "$WORK/world.log" && [ -f "$DATA/mods/rubies/pack.json" ] &&
        grep -q '"rubies"' "$DATA/mods/mods.json" && grep -q "Mods loaded: none" "$WORK/world.log"; then
        ok "example mod installed switched off in the data directory"
    else
        problem "example mod not installed switched off"
    fi

    # Second start: the player switches the mod on and plays in German.
    printf '{"disabled": []}\n' >"$DATA/mods/mods.json"
    if run_copy mod "$DATA" --play --seed 42 --look 45,-20 --language de --overlay inventory --chat "/give rubies:ruby_pickaxe 1" --chat "/give rubies:ruby_block 8"; then
        ok "run with the example mod exited normally"
    else
        problem "run with the example mod failed (log: $WORK/mod.log)"
    fi
    check_screenshot "$WORK/mod.png"
    if grep -q "Mods loaded: rubies " "$WORK/mod.log" && ! grep -q "Content:" "$WORK/mod.log"; then
        ok "$(grep -o 'Mods loaded: .*' "$WORK/mod.log" | head -1), no content warnings"
    else
        problem "example mod not loaded cleanly (log: $WORK/mod.log)"
    fi
    if grep -q "UI language de.*: translation loaded" "$WORK/mod.log"; then
        ok "German translation loaded"
    else
        problem "German translation not loaded"
    fi

    # Third start through Launch Services, as a double click does (main menu, no world).
    LS_DATA="$WORK/data-ls"
    mkdir -p "$LS_DATA" && printf '[audio]\nmuted=true\n' >"$LS_DATA/settings.ini"
    if limited 120 open -g -n -W --stdout "$WORK/open.log" --stderr "$WORK/open.log" \
        --env QTCRAFT_DATA_DIR="$LS_DATA" --env QTCRAFT_SCREENSHOT="$WORK/open.png" --env QTCRAFT_SCREENSHOT_FRAMES=60 \
        --env QTCRAFT_SCREEN=builtin --env QTCRAFT_BACKGROUND=1 --env QTCRAFT_AUDIO=1 \
        --env QML_DISK_CACHE_PATH="$WORK/cache/qml" --env QSG_RHI_DISABLE_DISK_CACHE=1 "$RUN_APP" --args --screen builtin --background; then
        ok "start through Launch Services exited normally"
    else
        problem "start through Launch Services failed (log: $WORK/open.log)"
        # open itself was stopped by the time limit; end the copy it started.
        # Anchored: the dedicated server qtcraft-headless next to it is no match.
        for pid in $(pgrep -f "^$RUN_APP/Contents/MacOS/qtcraft( |\$)"); do
            kill "$pid"
        done
    fi
    check_screenshot "$WORK/open.png"
    check_audio open
else
    echo "skip the starts of the game: QTCRAFT_RELEASE_GUI=0"
fi

# The dedicated server of the copy: starts on a free port of the loopback interface, says so,
# loads only libraries from the copy or the system, and saves and stops on SIGTERM with exit code 0.
# Perl sets DYLD_PRINT_LIBRARIES (see run_copy) and execs the server, which keeps perl's pid.
SERVER_DATA="$WORK/data-server"
mkdir -p "$SERVER_DATA"
env -i HOME="$HOME" USER="${USER:-}" PATH=/usr/bin:/bin TMPDIR="${TMPDIR:-/tmp}" \
    perl -e '$ENV{DYLD_PRINT_LIBRARIES} = 1; exec @ARGV or die' \
    "$RUN_APP/Contents/MacOS/qtcraft-headless" --server --port 0 --bind 127.0.0.1 --data-dir "$SERVER_DATA" \
    >"$WORK/server.log" 2>&1 &
server_pid=$!
# An interrupted or failing script does not leave the server running.
trap 'kill -KILL "$server_pid" 2>/dev/null' EXIT
trap 'kill -KILL "$server_pid" 2>/dev/null; exit 1' INT TERM HUP

# Whether the server process still runs: an ended child stays a zombie until it is waited for, and
# kill -0 succeeds on a zombie.
server_running() {
    state=$(ps -o stat= -p "$server_pid" 2>/dev/null | tr -d ' ')
    [ -n "$state" ] && [ "${state#Z}" = "$state" ]
}

# Waits up to $1 half-seconds while the server runs and `$2` (a command) fails.
wait_for_server() {
    waited=0
    while [ "$waited" -lt "$1" ] && server_running && ! eval "$2"; do
        perl -e 'select(undef, undef, undef, 0.5)'
        waited=$((waited + 1))
    done
}

wait_for_server 120 'grep -q "Listening on port" "$WORK/server.log"'
if grep -q "Certificate fingerprint (SHA-256): " "$WORK/server.log"; then
    ok "dedicated server: TLS certificate made"
else
    problem "dedicated server shows no certificate fingerprint (log: $WORK/server.log)"
fi
if grep -q "Listening on port" "$WORK/server.log"; then
    ok "dedicated server: $(grep -o 'Listening on port [0-9]*' "$WORK/server.log" | head -1)"
else
    problem "dedicated server did not start listening (log: $WORK/server.log)"
fi
kill -TERM "$server_pid" 2>/dev/null
# The server saves the world before it ends; give it at most 60 s.
wait_for_server 120 false
if server_running; then
    kill -KILL "$server_pid"
    problem "dedicated server did not stop on SIGTERM (log: $WORK/server.log)"
fi
wait "$server_pid"
server_status=$?
trap - EXIT INT TERM HUP
if [ "$server_status" -eq 0 ] && grep -q "server stopped" "$WORK/server.log"; then
    ok "dedicated server stopped on SIGTERM with exit code 0"
else
    problem "dedicated server ended with exit code $server_status (log: $WORK/server.log)"
fi
check_bundle_libraries "$WORK/server.log"

# Fourth start: the Intel half of the universal app under Rosetta, when it is installed.
if [ "$GUI_RUNS" = 1 ] && arch -x86_64 /usr/bin/true 2>/dev/null; then
    X86_DATA="$WORK/data-x86_64"
    mkdir -p "$X86_DATA" && printf '[audio]\nmuted=true\n' >"$X86_DATA/settings.ini"
    if RUN_ARCH=x86_64 run_copy x86_64 "$X86_DATA" --play --seed 42 --look 45,-20; then
        ok "world run as x86_64 (Rosetta) exited normally"
    else
        problem "world run as x86_64 (Rosetta) failed (log: $WORK/x86_64.log)"
    fi
    check_screenshot "$WORK/x86_64.png"
    check_audio x86_64
else
    echo "skip x86_64 start: Rosetta is not installed or QTCRAFT_RELEASE_GUI=0"
fi

# Nothing of the runs went to the player's caches.
if [ -d "$HOME/Library/Caches/QtCraft" ] && [ -n "$(find "$HOME/Library/Caches/QtCraft" -newer "$WORK/started" -print | head -1)" ]; then
    problem "the runs wrote to ~/Library/Caches/QtCraft"
else
    ok "no files written to ~/Library/Caches/QtCraft"
fi

echo "== packages"
limited 300 ditto -c -k --sequesterRsrc --keepParent "$APP" "$PACKAGE.zip" || abort "cannot create the zip"
ok "$(basename "$PACKAGE.zip") ($(du -h "$PACKAGE.zip" | cut -f1))"
if [ "${QTCRAFT_RELEASE_DMG:-1}" = "1" ]; then
    mkdir -p "$WORK/dmg" && ditto "$APP" "$WORK/dmg/QtCraft.app" && ln -s /Applications "$WORK/dmg/Applications" ||
        abort "cannot stage the disk image"
    limited 600 hdiutil create -quiet -volname "QtCraft $VERSION" -srcfolder "$WORK/dmg" -ov -format UDZO "$PACKAGE.dmg" ||
        abort "cannot create the disk image"
    if limited 300 hdiutil verify -quiet "$PACKAGE.dmg"; then
        ok "$(basename "$PACKAGE.dmg") ($(du -h "$PACKAGE.dmg" | cut -f1))"
    else
        problem "disk image does not verify"
    fi
fi

echo "== result"
echo "bundle:  $APP ($(du -sh "$APP" | cut -f1))"
if [ -n "$failures" ]; then
    echo "RELEASE CHECKS FAILED:$failures"
    echo "logs and screenshots: $WORK"
    exit 1
fi
if [ "$GUI_RUNS" = 1 ]; then
    cp "$WORK/world.png" "$DEPLOY_DIR/release-check-world.png"
    cp "$WORK/mod.png" "$DEPLOY_DIR/release-check-mod-de.png"
fi
rm -rf "$WORK"
if [ "$GUI_RUNS" = 1 ]; then
    echo "RELEASE OK (screenshots of the checks in $DEPLOY_DIR)"
else
    echo "RELEASE OK WITHOUT STARTS OF THE GAME (QTCRAFT_RELEASE_GUI=0): try the package once before publishing it"
fi
