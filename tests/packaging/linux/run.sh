#!/bin/sh
# Runs one package test in a clean container of the test matrix (installers
# spec, section 8.1 and 8.2):
#
#   tests/packaging/linux/run.sh [options] <test> <image> <dist dir> <version>
#
#   <test>     appimage, deb, rpm, tar or aur
#   <image>    a test image name from packaging/linux/tools.lock
#              (ubuntu-22.04, ubuntu-24.04, debian-12, fedora, tumbleweed,
#              archlinux)
#   <dist dir> the release files: HikariSub-<V>-linux-x86_64.AppImage,
#              hikarisub-<V>-linux-amd64.deb, hikarisub-<V>-linux-x86_64.rpm,
#              hikarisub-<V>-linux-x86_64.tar.gz
#
# Options:
#   --gst-check <file>  gst-check built in the floor container (required
#                       until the app has --hikari-gst-check)
#   --aur <dir>         the rendered PKGBUILD and .SRCINFO (aur test)
#   --upgrade <dir>     newer test packages to upgrade to (deb, rpm tests)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
gst_check= aur= upgrade=
while [ $# -gt 0 ]; do
    case $1 in
    --gst-check) gst_check=$2; shift 2 ;;
    --aur) aur=$2; shift 2 ;;
    --upgrade) upgrade=$2; shift 2 ;;
    --) shift; break ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
done
if [ $# -ne 4 ]; then
    sed -n '2,/^set -eu/p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
fi
test=$1 image_name=$2 dist=$(cd "$3" && pwd) version=$4
image=$("$repo/packaging/linux/fetch-tool.sh" --image "$image_name")

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT INT TERM
cp "$here/in-container.sh" "$here/sample.mkv" "$here/sample-free.webm" "$repo/packaging/linux/appimage/host-libs.txt" "$stage/"
if [ -n "$gst_check" ]; then
    cp "$gst_check" "$stage/gst-check"
    chmod 0755 "$stage/gst-check"
fi
chmod -R a+rX "$stage"

set -- -v "$stage:/tests:ro" -v "$dist:/dist:ro"
[ -n "$aur" ] && set -- "$@" -v "$(cd "$aur" && pwd):/aur:ro"
[ -n "$upgrade" ] && set -- "$@" -v "$(cd "$upgrade" && pwd):/upgrade:ro"
echo "== $test on $image_name ($image)"
docker run --rm "$@" "$image" sh /tests/in-container.sh "$test" "$version"
