#!/bin/bash
# Muxes a clip with each input timecodes file and saves mkvmerge's own v2 list as <name>.mkvmerge.txt.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for input in v1-gaps v1-aegisub v2-fractions v4-unsorted; do
    frames=40
    case $input in v2-*|v4-*) frames=$(grep -cvE '^\s*(#|$)' "$here/$input.txt") ;; esac
    ffmpeg -loglevel error -f lavfi -i testsrc=size=64x64:rate=25 -frames:v "$frames" -c:v libx264 -bf 0 -f h264 -y "$work/clip.h264"
    mkvmerge -q -o "$work/$input.mkv" --timestamps "0:$here/$input.txt" "$work/clip.h264"
    mkvextract -q "$work/$input.mkv" timestamps_v2 "0:$here/$input.mkvmerge.txt"
done
mkvmerge --version | head -1 > "$here/mkvmerge-version.txt"
