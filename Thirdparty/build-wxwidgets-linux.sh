#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
wx_source="$repo_root/Thirdparty/wxWidgets"
case "${1-}" in
    '') build_type=Release; default_build="$repo_root/build-wx-linux" ;;
    --debug) build_type=Debug; default_build="$repo_root/build-wx-linux-debug" ;;
    *) echo "usage: $0 [--debug]" >&2; exit 2 ;;
esac
wx_build="${HIKARISUB_WX_BUILD_DIR:-$default_build}"
wx_prefix="${HIKARISUB_WX_PREFIX:-$wx_build/prefix}"

if [[ ! -f "$wx_source/src/zlib/zlib.h" ]]; then
    echo 'wxWidgets submodules are missing; run git submodule update --init --recursive' >&2
    exit 1
fi

# HikariSub's fixes to wxGTK, applied to the submodule's working tree (see
# Thirdparty/PATCHES.md). A patch that is already applied is left alone, so
# the script can be run again on the same checkout.
for patch in "$repo_root"/Thirdparty/wxWidgets-patches/*.patch; do
    [[ -e "$patch" ]] || continue
    if git -C "$wx_source" apply --reverse --check "$patch" 2>/dev/null; then
        echo "wxWidgets patch already applied: ${patch##*/}"
    else
        git -C "$wx_source" apply "$patch"
        echo "wxWidgets patch applied: ${patch##*/}"
    fi
done

# Extra CMake options, word-split; the release floor build passes
# packaging/linux/floor's choice of wx's own image and regex libraries.
# shellcheck disable=SC2086
cmake -S "$wx_source" -B "$wx_build" -G Ninja \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_INSTALL_PREFIX="$wx_prefix" \
    -DwxBUILD_TOOLKIT=gtk3 \
    -DwxBUILD_SHARED=ON \
    -DwxBUILD_INSTALL=ON \
    -DwxBUILD_SAMPLES=OFF \
    -DwxBUILD_TESTS=OFF \
    -DwxBUILD_PRECOMP=OFF \
    -DwxUSE_MEDIACTRL=OFF \
    -DwxUSE_WEBVIEW=OFF \
    ${HIKARISUB_WX_CMAKE_ARGS-}
cmake --build "$wx_build" --parallel "${HIKARISUB_WX_JOBS:-4}"
cmake --install "$wx_build"
"$wx_prefix/bin/wx-config" --version
