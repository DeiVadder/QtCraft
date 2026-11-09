# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Licence texts of the Linux AppImage (decision E-951), sourced by scripts/release-linux.sh: the same
# set as the macOS bundle and the Windows package (decision E-932), without OpenSSL (the AppImage
# takes OpenSSL from the system and ships none). Uses PROJECT_DIR, QT_DIR, SDL_PREFIX, LOCAL_ONLY
# and APPDIR of the release script.

# FFmpeg tag of Qt's prebuilt FFmpeg, named in Qt's provisioning script (checked for each Qt release
# the workflows use).
ffmpeg_tag() {
    case "$1" in
    6.8.3) echo n7.1 ;;
    6.12.0) echo n9.0.1 ;;
    *) echo "<tag>"; return 1 ;;
    esac
}

# Writes the Qt notice for open-source Qt $1 (version) into file $2; $3 lists the FFmpeg libraries.
write_qt_notice() {
    # A distributable package names the exact FFmpeg source: an unknown Qt version stops here.
    tag=$(ffmpeg_tag "$1") || { echo "no FFmpeg tag known for Qt $1 (release-licenses.sh, ffmpeg_tag)" >&2; return 1; }
    cat >"$2" <<EOF
Qt $1

This AppImage contains the libraries, plugins and QML modules of Qt $1 (The Qt Company Ltd. and
other contributors) as separate files in usr/lib, usr/plugins and usr/qml, unmodified apart from
the RUNPATH that linuxdeploy set to the AppImage's own folders. They are
used under the GNU Lesser General Public License version 3 (Qt-LGPL-3.0.txt, which builds on
Qt-GPL-3.0.txt); you may replace them with your own build of the same Qt version: unpack the
AppImage (--appimage-extract), replace the files in squashfs-root and start squashfs-root/AppRun
(appimagetool packs the folder into an AppImage again).
Qt contains code of other projects (for example libpng, libjpeg, PCRE2, HarfBuzz, double-conversion,
TinyCBOR); their licences and notices are listed in the software bills of materials in the folder
Qt-SBOM, where present, and in the Qt documentation ("Licenses Used in Qt").

Qt Multimedia plays sound through FFmpeg. The FFmpeg libraries in usr/lib ($3) are the LGPL build
that comes with Qt $1, unmodified apart from the RUNPATH set by linuxdeploy; FFmpeg is licensed under the GNU Lesser General Public License
version 2.1 or later (FFmpeg-LGPL-2.1.txt). You may replace them with your own build of the same
FFmpeg version in the same way.

Corresponding source code:
- Qt $1: https://download.qt.io/archive/qt/${1%.*}/$1/single/qt-everywhere-src-$1.tar.xz
  (the same folder also has a .zip and md5sums.txt).
- FFmpeg: FFmpeg $tag (named in coin/provisioning/common/unix/install-ffmpeg.sh of the repository
  qt/qt5 at the tag v$1), https://github.com/FFmpeg/FFmpeg/archive/refs/tags/$tag.tar.gz (also from
  ffmpeg.org); the build options are in that script.
The QtCraft maintainers also send these sources on request.
EOF
}

# Writes the notice of a local-only package for Qt $1 (version) into file $2 and the
# NOT-FOR-DISTRIBUTION.txt next to the licences folder $3; $4 lists the FFmpeg libraries.
write_local_only_notice() {
    cat >"$2" <<EOF || return 1
Qt $1 (local tests only: not declared as open-source Qt)

This AppImage was built for local tests with the Qt installation $QT_DIR, which is not declared as
open-source Qt. Its Qt libraries, plugins and QML modules are not passed on under the GNU LGPL by
this AppImage; do not distribute it (NOT-FOR-DISTRIBUTION.txt). Packages for others come from the
release workflow, built against open-source Qt.

The FFmpeg libraries in usr/lib ($4) are licensed under the GNU Lesser General Public License
version 2.1 or later (FFmpeg-LGPL-2.1.txt); source: FFmpeg $(ffmpeg_tag "$1").
EOF
    printf '%s\n' "This QtCraft AppImage was built with a Qt that is not declared as open-source Qt ($QT_DIR), for local tests only." \
        "Do not pass it on: packages for others come from the release workflow with open-source Qt." \
        >"$3/../NOT-FOR-DISTRIBUTION.txt"
}

# Licence file of the distribution package that installed library $1 (rpm or dpkg), if any.
package_licence() {
    file=""
    if command -v rpm >/dev/null 2>&1 && package=$(rpm -qf --qf '%{NAME}' "$1" 2>/dev/null); then
        file=$(rpm -qL "$package" 2>/dev/null | grep -i -E '/(LICEN[CS]E|COPYING)[^/]*$' | head -1)
    elif command -v dpkg >/dev/null 2>&1; then
        # dpkg knows a file under the path its package installed it to; with the merged /usr of
        # Ubuntu that may be /lib/... while ldconfig and readlink name /usr/lib/... (libbz2).
        real=$(readlink -f "$1")
        for candidate in "$1" "$real" "${real#/usr}"; do
            package=$(dpkg -S "$candidate" 2>/dev/null | head -1 | cut -d: -f1)
            [ -n "$package" ] && break
        done
        [ -n "$package" ] && file="/usr/share/doc/$package/copyright"
    fi
    [ -n "$file" ] && [ -s "$file" ] && echo "$file"
}

# Path of the system library named $1 (ldconfig), else of the link in WORK/compat.
system_library() {
    path=$(/sbin/ldconfig -p 2>/dev/null | sed -n "s|^[[:space:]]*$1 (.*=> \(.*\)\$|\1|p" | head -1)
    # A longer soname of the same library (Ubuntu's libbz2.so.1.0 for libbz2.so.1).
    [ -z "$path" ] && path=$(/sbin/ldconfig -p 2>/dev/null | sed -n "s|^[[:space:]]*$1\.[0-9.]* (.*=> \(.*\)\$|\1|p" | head -1)
    [ -z "$path" ] && [ -e "$WORK/compat/$1" ] && path=$(readlink -f "$WORK/compat/$1")
    echo "$path"
}

# Licence text for bundled library $1 (file name): FFmpeg's LGPL text, the licence of the system's ICU
# package for Qt's ICU (the same ICU licence; Qt's installation carries none), else the licence of
# the package the library was copied from. xcb-util-keysyms on Fedora ships no licence file: the
# xcb-util libraries share xcb-util's licence (X11), whose file stands in. Prints the source file.
bundled_licence_source() {
    case "$1" in
    libav*.so* | libsw*.so*) echo "$licenses/FFmpeg-LGPL-2.1.txt"; return ;;
    libicu*.so*) package_licence "$(/sbin/ldconfig -p 2>/dev/null | sed -n 's|.*libicuuc\.so\.[0-9]* (.*=> \(.*\)$|\1|p' | head -1)"; return ;;
    esac
    package_licence "$(system_library "$1")" && return
    case "$1" in
    libxcb-*) package_licence "$(system_library libxcb-util.so.1)" ;;
    *) return 1 ;;
    esac
}

# Writes the licence of every bundled library that is not part of Qt into the folder $1 and the list
# bundled-libraries.txt ("<library> <licence file>") that release-checks.sh compares with usr/lib.
write_bundled_licences() {
    licenses=$1
    : >"$licenses/bundled-libraries.txt" || return 1
    for path in "$APPDIR"/usr/lib/*.so*; do
        library=${path##*/}
        case "$library" in
        libQt6*) continue ;;
        esac
        source=$(bundled_licence_source "$library") || { echo "no licence found for $library" >&2; return 1; }
        case "$library" in
        libav*.so* | libsw*.so*) target=FFmpeg-LGPL-2.1.txt ;;
        libicu*) target=ICU-LICENSE.txt ;;
        *) target="${library%%.so*}-LICENSE.txt" ;;
        esac
        [ -s "$licenses/$target" ] || cp "$source" "$licenses/$target" || return 1
        echo "$library $target" >>"$licenses/bundled-libraries.txt"
    done
}

# Writes all licence texts into the folder $1 (usr/share/doc/qtcraft/licenses of the AppDir).
write_licenses() {
    licenses=$1
    mkdir -p "$licenses" || return 1
    for file in LICENSE THIRD_PARTY.md CREDITS.md; do
        cp "$PROJECT_DIR/$file" "$licenses/QtCraft-$file" || return 1
    done
    cp "$PROJECT_DIR/src/app/fonts/OFL.txt" "$licenses/PixelifySans-OFL.txt" &&
        cp "$PROJECT_DIR/LICENSES/CC-BY-4.0.txt" "$licenses/CC-BY-4.0.txt" &&
        cp "$SDL_PREFIX/LICENSE.txt" "$licenses/SDL3-LICENSE.txt" &&
        cp "$SDL_PREFIX/HIDAPI-LICENSE.txt" "$licenses/HIDAPI-LICENSE.txt" &&
        cp "$SDL_PREFIX/LIBM-NOTICE.txt" "$licenses/SDL3-LIBM-NOTICE.txt" &&
        cp "$PROJECT_DIR/licenses-third-party/LGPL-2.1-or-later.txt" "$licenses/FFmpeg-LGPL-2.1.txt" || return 1
    qt_version=$("$QT_DIR/bin/qtpaths" --query QT_VERSION 2>/dev/null)
    [ -n "$qt_version" ] || return 1
    ffmpeg_libraries=$(cd "$QT_DIR/lib" && ls libav*.so.* libsw*.so.* 2>/dev/null | grep -E '\.so\.[0-9]+$' | tr '\n' ' ')
    if [ "$LOCAL_ONLY" -eq 0 ]; then
        cp "$PROJECT_DIR/licenses-third-party/LGPL-3.0-only.txt" "$licenses/Qt-LGPL-3.0.txt" &&
            cp "$PROJECT_DIR/LICENSE" "$licenses/Qt-GPL-3.0.txt" &&
            write_qt_notice "$qt_version" "$licenses/Qt-NOTICE.txt" "$ffmpeg_libraries" || return 1
    else
        write_local_only_notice "$qt_version" "$licenses/Qt-NOTICE.txt" "$licenses" "$ffmpeg_libraries" || return 1
    fi
    # Qt's software bills of materials for the shipped modules, when the Qt installation has them.
    for module in qtbase qtdeclarative qtmultimedia qtimageformats qtsvg; do
        sbom="$QT_DIR/sbom/$module-$qt_version.spdx"
        [ -f "$sbom" ] || continue
        mkdir -p "$licenses/Qt-SBOM" && cp "$sbom" "$licenses/Qt-SBOM/" || return 1
    done
}
