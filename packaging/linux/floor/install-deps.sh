#!/bin/sh
# Installs everything the legacy floor build needs inside the ubuntu:22.04
# container named in ./image: the toolchain (g++-12; jammy's default g++ is 11),
# the -dev packages of the host-side and bundled libraries, the tools that
# build FFmpeg and FFMS2, and the runtime pieces the AppImage bundles (the
# GStreamer plugins, the GTK im-modules, the gdk-pixbuf loaders, the icons).
#
# Run as root in the container: packaging/linux/floor/install-deps.sh
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential g++-12 gcc-12 cmake ninja-build pkg-config gettext git \
    ca-certificates curl xz-utils file python3 nasm autoconf automake libtool zsync \
    binutils zstd patchelf \
    libgtk-3-dev libass-dev libhunspell-dev libuchardet-dev \
    libcurl4-openssl-dev libicu-dev \
    libboost-filesystem-dev libboost-locale-dev libboost-regex-dev \
    libboost-system-dev libboost-thread-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgl1-mesa-dev libglu1-mesa-dev libfontconfig-dev \
    libdav1d-dev zlib1g-dev hunspell-en-us \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-libav gstreamer1.0-pulseaudio gstreamer1.0-alsa \
    ibus-gtk3 fcitx5-frontend-gtk3 librsvg2-common \
    adwaita-icon-theme hicolor-icon-theme \
    libgdk-pixbuf2.0-bin libgtk-3-bin libglib2.0-bin \
    desktop-file-utils appstream libxml2-utils
# The build scripts select gcc-12 and g++-12 through CC and CXX.
g++-12 --version | sed -n 1p
