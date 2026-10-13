#!/bin/sh
# Runs build.sh in the floor container on a workstation with docker:
#
#   packaging/linux/floor/run-local.sh <out dir> [jobs]
#
# Builds the image from ./Dockerfile (tag hikarisub-floor:local), mounts the
# source tree, the repository's git directory (a worktree's .git file points
# there) and <out dir>, and hands the results back to the calling user.
set -eu

if [ $# -lt 1 ]; then
    echo "usage: $0 <out dir> [jobs]" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../.." && pwd)
out=$(mkdir -p "$1" && cd "$1" && pwd)
jobs=${2:-$(nproc)}
common_git=$(git -C "$src" rev-parse --path-format=absolute --git-common-dir)

docker build -q --build-arg BASE="$(cat "$here/image")" -t hikarisub-floor:local "$here" >/dev/null
docker run --rm \
    -v "$src:$src" -v "$common_git:$common_git:ro" -v "$out:$out" \
    hikarisub-floor:local \
    sh -c 'rc=0; "$1/packaging/linux/floor/build.sh" "$2" "$3" || rc=$?; chown -R "$4" "$2" "$1/HikariSub/gitparams.h" 2>/dev/null; exit $rc' \
    sh "$src" "$out" "$jobs" "$(id -u):$(id -g)"
