#!/bin/sh
# Builds OpenSSL once and installs it into a cache shared by every preset and every worktree
# (decision E-872): <cache>/<version>-<arch>-<config>, where <cache> is $QC_OPENSSL_CACHE or
# ~/.cache/qtcraft/openssl, <version> the first field of 3rdparty/openssl-version.txt, <arch>
# "universal" on macOS (arm64 and x86_64 in one library, for the build tree and the release bundle
# alike) and the machine (`uname -m`) elsewhere, and <config> the version of this script's build
# options (CONFIG_VERSION below; raised whenever the options change, so an entry built with other
# options is never reused). The source is the release tarball of that version, checked against the
# SHA-256 in the second field of the same file before it is unpacked; the download is kept in
# <cache>/src. Built are the shared libraries libcrypto and libssl only (no programs, tests, docs,
# engines or legacy provider, providers inside libcrypto), Release, without sanitizers; on macOS
# for macOS 13.3 like the game, with the install names @rpath/libssl.3.dylib and
# @rpath/libcrypto.3.dylib (libssl finds libcrypto next to itself). OPENSSLDIR points at a folder
# nobody can create (/var/empty/qtcraft-openssl), so no openssl.cnf of the system or of another
# program changes how the game's TLS behaves (the game also initialises OpenSSL without reading any
# configuration, OPENSSL_CONF included; QcTlsBackend). A finished entry has the file .complete and is never
# built again. The configure step of QtCraft runs this script itself on macOS when the entry is
# missing. Linux builds use the OpenSSL 3 of the system; there this script builds a prefix for
# QC_OPENSSL_ROOT only (cmake/QcTls.cmake). Windows has its own script, scripts/windows/build-openssl.ps1.
#
# Usage: scripts/build-openssl.sh [--print-prefix]
#   --print-prefix  only print the cache directory of this version and architecture and exit
# Environment: QC_OPENSSL_CACHE (cache root), QC_BUILD_JOBS (parallel jobs; otherwise all cores),
# QC_OPENSSL_WAIT_BENCH_LOCK=1 (wait while the benchmark lock ~/.cache/qtcraft/bench.lock exists;
# set by the configure step). Exit code 0: the prefix exists (printed last on stdout), 1: the
# download or build failed, 2: wrong use.

set -u

# Raise this when the Configure options below change: the cache key then names a new entry.
CONFIG_VERSION=c1

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd) || exit 2
PRINT_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --print-prefix) PRINT_ONLY=1 ;;
        *)
            echo "usage: scripts/build-openssl.sh [--print-prefix]" >&2
            exit 2
            ;;
    esac
    shift
done

read -r VERSION SHA256 <"$PROJECT_DIR/3rdparty/openssl-version.txt" || exit 2
case "$VERSION" in
    '' | *[!0-9.]*) echo "error: 3rdparty/openssl-version.txt names no version" >&2; exit 2 ;;
esac
[ ${#SHA256} -eq 64 ] || { echo "error: 3rdparty/openssl-version.txt has no SHA-256" >&2; exit 2; }

SYSTEM=$(uname -s)
ARCH=$(uname -m)
[ "$SYSTEM" = Darwin ] && ARCH=universal
KEY="$VERSION-$ARCH-$CONFIG_VERSION"
CACHE_ROOT=${QC_OPENSSL_CACHE:-"${XDG_CACHE_HOME:-$HOME/.cache}/qtcraft/openssl"}
PREFIX="$CACHE_ROOT/$KEY"
if [ "$PRINT_ONLY" -eq 1 ] || [ -f "$PREFIX/.complete" ]; then
    echo "$PREFIX"
    exit 0
fi

. "$PROJECT_DIR/scripts/cache-entry-lock.sh"
[ "${QC_OPENSSL_WAIT_BENCH_LOCK:-0}" = 1 ] && wait_for_bench_lock build-openssl

mkdir -p "$CACHE_ROOT/src" || exit 1
LOCK="$CACHE_ROOT/.lock-$KEY"
WORK=""
cache_lock_acquire
if [ -f "$PREFIX/.complete" ]; then
    echo "$PREFIX"
    exit 0
fi
# Leftovers of builds that were killed before their trap ran.
rm -rf "$CACHE_ROOT/.build-$KEY".* "$LOCK".stale.*
WORK=$(mktemp -d "$CACHE_ROOT/.build-$KEY.XXXXXX") || exit 1

# --- The source: the release tarball, kept once it matches the pinned SHA-256. ---
TARBALL="$CACHE_ROOT/src/openssl-$VERSION.tar.gz"
URL="https://github.com/openssl/openssl/releases/download/openssl-$VERSION/openssl-$VERSION.tar.gz"
# The first tool found; without one the check cannot run, and nothing unchecked is built.
if command -v shasum >/dev/null 2>&1; then
    sha256_of() { shasum -a 256 "$1" | cut -d' ' -f1; }
elif command -v sha256sum >/dev/null 2>&1; then
    sha256_of() { sha256sum "$1" | cut -d' ' -f1; }
else
    echo "error: neither shasum nor sha256sum found; the OpenSSL tarball cannot be checked" >&2
    exit 1
fi
if [ ! -f "$TARBALL" ] || [ "$(sha256_of "$TARBALL")" != "$SHA256" ]; then
    echo "build-openssl: downloading $URL" >&2
    curl -fsSL --retry 3 -o "$WORK/download.tar.gz" "$URL" || { echo "error: cannot download $URL" >&2; exit 1; }
    FOUND=$(sha256_of "$WORK/download.tar.gz")
    if [ "$FOUND" != "$SHA256" ]; then
        echo "error: openssl-$VERSION.tar.gz has SHA-256 $FOUND, 3rdparty/openssl-version.txt wants $SHA256" >&2
        exit 1
    fi
    mv "$WORK/download.tar.gz" "$TARBALL" || exit 1
fi

JOBS=${QC_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}
OPTIONS="shared no-tests no-docs no-apps no-engine no-module no-legacy no-ssl3 no-comp
    --openssldir=/var/empty/qtcraft-openssl --libdir=lib"

# Builds the libraries for Configure target $1 (empty: the machine's) and installs them into $2.
build_one() {
    source_dir="$WORK/source-${1:-native}"
    mkdir -p "$source_dir" && tar -xzf "$TARBALL" -C "$source_dir" --strip-components 1 || return 1
    log="$WORK/build-${1:-native}.log"
    echo "build-openssl: building OpenSSL $VERSION ${1:-native} ($CONFIG_VERSION)" >&2
    # shellcheck disable=SC2086
    (cd "$source_dir" && MACOSX_DEPLOYMENT_TARGET=13.3 perl ./Configure $1 $OPTIONS --prefix="$2" $EXTRA_FLAGS &&
        make -j "$JOBS" build_libs && make install_dev) >"$log" 2>&1 || { tail -40 "$log" >&2; return 1; }
}

INSTALL="$WORK/install"
if [ "$SYSTEM" = Darwin ]; then
    EXTRA_FLAGS="-mmacosx-version-min=13.3"
    build_one darwin64-arm64-cc "$WORK/arm64" && build_one darwin64-x86_64-cc "$WORK/x86_64" || exit 1
    # Both architectures have the same public headers (64 bit, little endian); a difference would
    # need a merged header and is refused.
    diff -r "$WORK/arm64/include" "$WORK/x86_64/include" >"$WORK/headers.diff" ||
        { cat "$WORK/headers.diff" >&2; echo "error: the headers of arm64 and x86_64 differ" >&2; exit 1; }
    mkdir -p "$INSTALL/lib" && cp -R "$WORK/arm64/include" "$INSTALL/include" || exit 1
    for library in libcrypto.3.dylib libssl.3.dylib; do
        lipo -create "$WORK/arm64/lib/$library" "$WORK/x86_64/lib/$library" -output "$INSTALL/lib/$library" || exit 1
        install_name_tool -id "@rpath/$library" "$INSTALL/lib/$library" 2>/dev/null || exit 1
        ln -s "$library" "$INSTALL/lib/${library%.3.dylib}.dylib" || exit 1
    done
    install_name_tool -change "$WORK/arm64/lib/libcrypto.3.dylib" @rpath/libcrypto.3.dylib \
        -change "$WORK/x86_64/lib/libcrypto.3.dylib" @rpath/libcrypto.3.dylib \
        -add_rpath @loader_path "$INSTALL/lib/libssl.3.dylib" 2>/dev/null || exit 1
    for library in libcrypto.3.dylib libssl.3.dylib; do
        codesign --force --sign - "$INSTALL/lib/$library" >/dev/null 2>&1 || exit 1
    done
    # otool's header lines (the file and its architectures) end in a colon and are skipped.
    if otool -L "$INSTALL/lib/libssl.3.dylib" "$INSTALL/lib/libcrypto.3.dylib" | grep -v ':$' | grep -q "$WORK"; then
        echo "error: libssl.3.dylib or libcrypto.3.dylib still names a path of the build folder" >&2
        exit 1
    fi
else
    EXTRA_FLAGS=""
    build_one "" "$INSTALL" || exit 1
    # Static libraries, pkg-config and CMake files of the build folder are not used.
    rm -f "$INSTALL"/lib/*.a
fi
rm -rf "$INSTALL/lib/pkgconfig" "$INSTALL/lib/cmake"

# The licence text the release bundle carries (THIRD_PARTY.md): Apache License 2.0.
cp "$WORK/source-$([ "$SYSTEM" = Darwin ] && echo darwin64-arm64-cc || echo native)/LICENSE.txt" "$INSTALL/LICENSE.txt" ||
    { echo "error: cannot copy the licence text of OpenSSL" >&2; exit 1; }
echo "$VERSION $ARCH $CONFIG_VERSION" >"$INSTALL/.complete"

# Moved into place in one step, so a reader never sees a half-installed entry. Checked again right
# before: a finished entry is never replaced.
if [ -f "$PREFIX/.complete" ]; then
    echo "$PREFIX"
    exit 0
fi
rm -rf "$PREFIX"
mv "$INSTALL" "$PREFIX" || exit 1
[ -f "$PREFIX/.complete" ] || { echo "error: $PREFIX is incomplete after the move" >&2; exit 1; }
echo "build-openssl: done" >&2
echo "$PREFIX"
