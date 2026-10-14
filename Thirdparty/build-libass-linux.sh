#!/bin/sh
# Builds the Thirdparty/libass submodule (the altqx fork) as a shared library
# for the Linux build, which links and bundles it instead of the
# distribution's libass:
#
#   Thirdparty/build-libass-linux.sh [prefix] [jobs]
#
# The prefix defaults to build-libass-linux/prefix in the repository, where
# CMakeLists.txt looks for it; the release floor build passes its media
# prefix (packaging/linux/floor/build-media.sh). CC and the usual flags are
# honoured.
#
# libass is built the way the Windows project builds it: with its x86
# assembly and without libunibreak, so lines wrap at the same places on
# both. Linux finds fonts through fontconfig, and configure fails without
# it. Autotools rather than Meson, because the fork needs Meson 0.64 and the
# ubuntu:22.04 floor has 0.61; the shared library exports exactly
# libass/libass.sym either way.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
prefix=${1:-$repo/build-libass-linux/prefix}
jobs=${2:-$(nproc)}
src=$repo/Thirdparty/libass

if [ ! -f "$src/libass/ass.h" ]; then
    echo "Thirdparty/libass is empty; run git submodule update --init Thirdparty/libass" >&2
    exit 1
fi
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)

# autoreconf writes into the source tree, so build from a copy and leave the
# submodule's working tree clean.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -a "$src/." "$work/libass"
rm -rf "$work/libass/.git"
cd "$work/libass"
./autogen.sh >/dev/null
./configure --prefix="$prefix" --libdir="$prefix/lib" \
    --enable-shared --disable-static \
    --enable-asm --enable-fontconfig --disable-libunibreak \
    --disable-test --disable-compare --disable-profile --disable-fuzz
make -j"$jobs"
make install
rm -f "$prefix"/lib/libass.la

# The fork's API is what tells it from upstream: its version number does not.
grep -q '^#define LIBASS_HAS_FRAME_ATLAS' "$prefix/include/ass/ass.h" || {
    echo "$prefix/include/ass/ass.h is not the altqx libass fork's" >&2
    exit 1
}
echo "libass $(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --modversion libass) (altqx fork) installed in $prefix"
