#!/bin/sh
# Builds FFmpeg 7.1 and FFMS2 (the Thirdparty/ffms2 fork) from source into one
# prefix, for the legacy floor build in ubuntu:22.04.
#
#   packaging/linux/floor/build-media.sh <prefix> <download dir> [jobs]
#
# Jammy's own FFmpeg 4.4 and FFMS2 2.40 cannot be used: the app compiles
# Thirdparty/Build/FFMS2/indexing_additional.cpp against the fork's private
# headers (FFMS2 5.1, FFmpeg 7.1 API), so the library it links must be the
# fork built against FFmpeg 7.1. CMakeLists.txt finds both through
# HIKARISUB_MEDIA_PREFIX=<prefix>.
#
# FFmpeg is built for decoding only, the way FFMS2 uses it: the native
# decoders and demuxers plus dav1d for AV1, no programs, no avdevice,
# avfilter or postproc, and no hardware (VA-API, VDPAU, Vulkan): FFMS2 never
# initialises a hardware device, so linking the drivers' libraries would only
# add host coupling. No library is autodetected, so the result does not depend
# on what else happens to be installed in the container.
set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 <prefix> <download dir> [jobs]" >&2
    exit 2
fi
prefix=$1
downloads=$2
jobs=${3:-$(nproc)}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
: "${CC:=gcc-12}" "${CXX:=g++-12}"
export CC CXX

set -- $(grep '^ffmpeg[[:space:]]' "$here/sources.lock")
ffmpeg_version=$2 ffmpeg_sha256=$3 ffmpeg_url=$4
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A prefix that already holds this FFmpeg (a resumed local build) keeps it.
ffmpeg_marker="$prefix/.ffmpeg-$ffmpeg_version-$ffmpeg_sha256"
if [ ! -f "$ffmpeg_marker" ]; then
    tarball="$downloads/ffmpeg-$ffmpeg_version.tar.xz"
    mkdir -p "$downloads"
    if [ ! -f "$tarball" ] || ! echo "$ffmpeg_sha256  $tarball" | sha256sum -c --status; then
        curl -fsSL --retry 3 -o "$tarball.part" "$ffmpeg_url"
        mv "$tarball.part" "$tarball"
    fi
    echo "$ffmpeg_sha256  $tarball" | sha256sum -c
    tar -xJf "$tarball" -C "$work"
    cd "$work/ffmpeg-$ffmpeg_version"
    ./configure \
        --prefix="$prefix" \
        --cc="$CC" --cxx="$CXX" \
        --enable-shared --disable-static --enable-pic \
        --enable-gpl \
        --disable-autodetect \
        --enable-zlib --enable-libdav1d \
        --disable-programs --disable-doc \
        --disable-avdevice --disable-avfilter --disable-postproc \
        --disable-encoders --disable-muxers \
        --disable-network \
        --disable-debug \
        --extra-ldflags='-Wl,--as-needed'
    make -j"$jobs"
    make install
    touch "$ffmpeg_marker"
fi

# The fork, from the submodule, so its headers and library agree.
cp -a "$repo/Thirdparty/ffms2" "$work/ffms2"
cd "$work/ffms2"
rm -f .git
PKG_CONFIG_PATH="$prefix/lib/pkgconfig" LDFLAGS="-L$prefix/lib" ./autogen.sh \
    --prefix="$prefix" --enable-shared --disable-static
make -j"$jobs"
make install
rm -f "$prefix"/lib/*.la

echo "FFmpeg $ffmpeg_version and FFMS2 $(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --modversion ffms2) installed in $prefix"
