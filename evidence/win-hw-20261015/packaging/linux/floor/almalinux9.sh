#!/bin/sh
# The Qt rewrite's Linux build floor (installers spec 5.1): AlmaLinux 9
# (glibc 2.34) with gcc-toolset-14. gcc-toolset links the C++ runtime parts
# newer than the system libstdc++ statically, so the binaries need only the
# system GLIBCXX_3.4.29, the same as the Qt SDK, and no libstdc++ is bundled.
#
# Run as root inside almalinux:9 (CI pins the image by digest). It installs
# the declared host prerequisites, a pinned CMake and the packaging tools,
# then prints the environment line to source:
#
#   packaging/linux/floor/almalinux9.sh && . /etc/profile.d/hikari-floor.sh
set -eu

dnf -y -q install dnf-plugins-core epel-release >/dev/null
dnf config-manager --set-enabled crb
dnf -y -q makecache >/dev/null

# Build tools (docs/qt/build.md), the compiler, and what vcpkg's ports need:
# OpenSSL's Configure wants the perl modules, autotools ports want
# autoconf-archive, FFmpeg wants nasm. (curl-minimal, already present,
# provides curl.)
pkgs="gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils
      gcc-toolset-14-libatomic-devel gcc-toolset-14-libquadmath-devel
      git make ninja-build pkgconf-pkg-config autoconf autoconf-archive automake libtool
      nasm python3 tar gzip zip unzip xz bzip2 patch diffutils findutils which file
      perl perl-IPC-Cmd perl-FindBin perl-File-Compare perl-File-Copy perl-Time-Piece perl-lib
      patchelf binutils zsync desktop-file-utils hunspell-en-US"
# The system libraries the official Qt binaries and the app link against
# (Qt Multimedia needs libpulse; PortAudio needs ALSA).
pkgs="$pkgs alsa-lib-devel pulseaudio-libs pulseaudio-libs-devel
      mesa-libGL-devel mesa-libEGL-devel libglvnd-devel libxkbcommon-devel libxkbcommon-x11
      wayland-devel fontconfig freetype dbus-libs
      libX11-devel libXext libXrandr libdrm
      xcb-util-cursor xcb-util-wm xcb-util-keysyms xcb-util-renderutil xcb-util-image
      libxcb libSM libICE"
# shellcheck disable=SC2086
dnf -y -q install $pkgs >/dev/null

# CMake 3.31 (EL9 ships 3.26; the project needs 3.28), pinned by the
# SHA-256 that Kitware publishes (cmake-3.31.8-SHA-256.txt).
CMAKE_VERSION=3.31.8
CMAKE_SHA256=630615d8e98ac33eba7fbe472626dff5c899c85af3c024585ae109166a6909d0
cmake_tgz=/tmp/cmake-$CMAKE_VERSION.tar.gz
curl -fsSL -o "$cmake_tgz" \
    "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz"
echo "$CMAKE_SHA256  $cmake_tgz" | sha256sum -c - >/dev/null
rm -rf /opt/cmake && mkdir -p /opt/cmake
tar -xzf "$cmake_tgz" -C /opt/cmake --strip-components=1
rm -f "$cmake_tgz"

# Autoconf 2.72: EL9 ships 2.69, and vcpkg's gperf (a host tool of
# fontconfig) requires 2.70 or later. Installed under /opt/autoconf, ahead
# of the system one on PATH.
AUTOCONF_VERSION=2.72
AUTOCONF_SHA256=ba885c1319578d6c94d46e9b0dceb4014caafe2490e437a0dbca3f270a223f5a
ac_txz=/tmp/autoconf-$AUTOCONF_VERSION.tar.xz
curl -fsSL --retry 3 -o "$ac_txz" "https://ftpmirror.gnu.org/autoconf/autoconf-$AUTOCONF_VERSION.tar.xz" ||
    curl -fsSL --retry 3 -o "$ac_txz" "https://ftp.gnu.org/gnu/autoconf/autoconf-$AUTOCONF_VERSION.tar.xz"
echo "$AUTOCONF_SHA256  $ac_txz" | sha256sum -c - >/dev/null
rm -rf /tmp/autoconf-src && mkdir -p /tmp/autoconf-src
tar -xJf "$ac_txz" -C /tmp/autoconf-src --strip-components=1
(cd /tmp/autoconf-src && ./configure -q --prefix=/opt/autoconf && make -s -j"$(nproc)" && make -s install) >/dev/null
rm -rf /tmp/autoconf-src "$ac_txz"

cat > /etc/profile.d/hikari-floor.sh <<'EOF'
# gcc-toolset-14 first, then the pinned CMake and Autoconf.
. /opt/rh/gcc-toolset-14/enable
export PATH=/opt/cmake/bin:/opt/autoconf/bin:$PATH
export CC=gcc CXX=g++
EOF

. /etc/profile.d/hikari-floor.sh
{ grep -E '^PRETTY_NAME=' /etc/os-release; ldd --version | head -1; g++ --version | head -1
  cmake --version | head -1; autoconf --version | head -1; ninja --version; nasm -v; patchelf --version; } >&2
