#!/bin/bash
# Runs the package tests of installers spec 8.1 and 8.2 in docker, one fresh
# container per image and test group, as the CI workflow
# (.github/workflows/qt-package-linux.yml) does with its job matrix.
#
#   run-matrix.sh --dist <dir> --old <dir> --version <V> [--expect-version-switch] [<image>...]
#
# <dist> holds the release packages, <old> the test-only ones
# (make-test-packages.sh and an older Qt test version). Images default to
# the spec's matrix. Logs go to <dist>/../test-logs/.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../.." && pwd)
dist='' old='' version='' extra=()
while [ $# -gt 0 ]; do
    case "$1" in
        --dist) dist=$(cd "$2" && pwd); shift 2 ;;
        --old) old=$(cd "$2" && pwd); shift 2 ;;
        --version) version=$2; shift 2 ;;
        --expect-version-switch) extra+=(--expect-version-switch); shift ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
[ -n "$dist" ] && [ -n "$old" ] && [ -n "$version" ] || { echo "usage: see $0" >&2; exit 2; }
images=("$@")
[ ${#images[@]} -gt 0 ] || images=(ubuntu:22.04 ubuntu:24.04 debian:12 fedora:latest opensuse/tumbleweed archlinux:latest)
logs=$(dirname "$dist")/test-logs
mkdir -p "$logs"

groups_for() # <image>: the test groups, one container each
{
    case $1 in
        ubuntu:*) echo "appimage,tar native,helper,upgrade,remove" ;;
        debian:*|fedora:*|opensuse/*) echo "appimage native,helper,upgrade,remove" ;;
        archlinux:*) echo "appimage aur" ;;
        *) echo "unknown image $1" >&2; return 1 ;;
    esac
}

status=0
for image in "${images[@]}"; do
    for group in $(groups_for "$image"); do
        log=$logs/$(echo "$image" | tr ':/' '__')-${group//,/-}.log
        echo "== $image: $group"
        if docker run --rm -v "$src:/src:ro" -v "$dist:/dist:ro" -v "$old:/old:ro" "$image" \
            /src/tests/packaging/linux/container-test.sh --dist /dist --old /old --version "$version" \
            --tests "$group" "${extra[@]+"${extra[@]}"}" > "$log" 2>&1; then
            grep -E '^(PASS|SKIP)' "$log" | sed 's/^/   /'
            echo "   ok ($log)"
        else
            tail -30 "$log" | sed 's/^/   /'
            echo "   FAILED ($log)"
            status=1
        fi
    done
done
exit $status
