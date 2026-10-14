#!/bin/sh
# Builds the legacy Linux release packages in the floor container
# (packaging/linux/floor/Dockerfile, ubuntu:22.04 + g++-12):
#
#   packaging/linux/floor/build.sh <out dir> [jobs]
#
# 1. wxWidgets 3.3.3 from the submodule, into <out>/wx/prefix;
# 2. FFmpeg 7.1, the FFMS2 fork and the libass fork, into <out>/media
#    (build-media.sh);
# 3. the app, in <out>/app, against both;
# 4. the packages, in <out>/app/dist:
#      hikarisub-linux-x86_64.tar.gz          (portable; renamed at release)
#      HikariSub-<V>-linux-x86_64.AppImage and .zsync
#      hikarisub-<V>-linux-amd64.deb, hikarisub-<V>-linux-x86_64.rpm
#
# Steps 1 and 2 are skipped when <out> already holds their result (CI
# restores both from its cache). Run from anywhere; the source tree is the
# one this script is in. To run it in docker on a workstation, see run-local.sh.
set -eu

if [ $# -lt 1 ]; then
    echo "usage: $0 <out dir> [jobs]" >&2
    exit 2
fi
out=$(mkdir -p "$1" && cd "$1" && pwd)
jobs=${2:-$(nproc)}
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../.." && pwd)
CC=gcc-12 CXX=g++-12
export CC CXX
git config --global --add safe.directory '*' 2>/dev/null || true

wx_config=$out/wx/prefix/bin/wx-config
if [ -x "$wx_config" ] && "$wx_config" --version >/dev/null 2>&1; then
    echo "wxWidgets $("$wx_config" --version) is in $out/wx/prefix"
else
    # wx's own copies of libjpeg, libpng, libtiff and PCRE2, as on Windows:
    # jammy's sonames (libjpeg.so.8, libtiff.so.5, libpcre2-32) are not on
    # every target (Debian has libjpeg.so.62, Ubuntu 24.04 libtiff.so.6), and
    # GTK's own copies would make them host libraries by the closure rule.
    HIKARISUB_WX_BUILD_DIR=$out/wx/build HIKARISUB_WX_PREFIX=$out/wx/prefix \
        HIKARISUB_WX_JOBS=$jobs \
        HIKARISUB_WX_CMAKE_ARGS="-DwxUSE_LIBJPEG=builtin -DwxUSE_LIBPNG=builtin -DwxUSE_LIBTIFF=builtin -DwxUSE_REGEX=builtin" \
        "$src/Thirdparty/build-wxwidgets-linux.sh"
fi

libass_pin=$(git -C "$src" rev-parse HEAD:Thirdparty/libass)
if [ -f "$out/media/lib/pkgconfig/ffms2.pc" ] && ls "$out"/media/.ffmpeg-* >/dev/null 2>&1 &&
   [ -f "$out/media/.libass-$libass_pin" ]; then
    echo "FFmpeg, FFMS2 and libass are in $out/media"
else
    "$here/build-media.sh" "$out/media" "$out/downloads" "$jobs"
fi

tools=$out/tools
appimagetool=$("$src/packaging/linux/fetch-tool.sh" appimagetool "$tools")
runtime=$("$src/packaging/linux/fetch-tool.sh" type2-runtime "$tools")
nfpm=$("$src/packaging/linux/fetch-tool.sh" nfpm "$tools")

cmake -S "$src" -B "$out/app" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHIKARISUB_WX_CONFIG="$wx_config" \
    -DHIKARISUB_MEDIA_PREFIX="$out/media" \
    -DHIKARISUB_APPIMAGETOOL="$appimagetool" \
    -DHIKARISUB_APPIMAGE_RUNTIME="$runtime" \
    -DHIKARISUB_NFPM="$nfpm"
cmake --build "$out/app" -j "$jobs"
cmake --build "$out/app" --target hikarisub_appimage
cmake --build "$out/app" --target hikarisub_native_packages
ls -l "$out/app/dist"

# Test helpers, never released (tests/packaging/linux): gst-check, built here
# so it runs on every host of the matrix, and a deb and rpm of the same tree
# one patch version up, for the upgrade test.
test_dir=$out/test
rm -rf "$test_dir"
mkdir -p "$test_dir/upgrade"
# shellcheck disable=SC2046
"$CC" -O2 -o "$test_dir/gst-check" "$src/tests/packaging/linux/gst-check.c" \
    $(pkg-config --cflags --libs gstreamer-1.0)
version=$(sed -n 's/^#define[[:space:]]*VersionHikariSub[[:space:]]*"\(.*\)".*/\1/p' "$src/HikariSub/VersionHikariSub.h")
next=$(echo "$version" | awk -F '[.-]' '{ printf "%d.%d.%d-test.1", $1, $2, $3 + 1 }')
cmake -D SOURCE_DIR="$src" -D BINARY_DIR="$out/app" \
    -D PACKAGE_ROOT="$out/app/package-stage/hikarisub-linux-x86_64" \
    -D VERSION="$next" -D NFPM="$nfpm" -D OUTPUT_DIR="$test_dir/upgrade" \
    -D MIN_GLIBC=2.35 -P "$src/cmake/PackageLinuxNative.cmake" >/dev/null
ls -l "$test_dir" "$test_dir/upgrade"
