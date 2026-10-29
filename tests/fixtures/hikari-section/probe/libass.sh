#!/usr/bin/env bash
#   probe/libass.sh [out-dir]
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-}
[[ -n $out ]] && mkdir -p "$out"
frames() {
    ffmpeg -nostdin -v error -f lavfi -i color=c=gray:s=640x360:d=4:r=10 -vf "ass=$1" -f framemd5 - |
        grep -v '^#' | awk -F, '{print $1, $NF}'
}
failed=0
for pair in "plain plain-hikari" "fonts fonts-hikari"; do
    set -- $pair
    a=$(frames "$here/$1.ass") b=$(frames "$here/$2.ass")
    drawn=$(sort -u -k2 <<<"$a" | wc -l)
    if [[ -n $a && $a == "$b" ]]; then
        echo "SAME $1.ass $2.ass ($(wc -l <<<"$a") frames, $drawn distinct)"
    else
        echo "DIFFERENT $1.ass $2.ass"
        diff <(echo "$a") <(echo "$b") | head
        failed=1
    fi
    if [[ -n $out ]]; then
        for f in "$1" "$2"; do
            ffmpeg -nostdin -v error -y -f lavfi -i color=c=gray:s=640x360:d=4:r=10 -vf "ass=$here/$f.ass" \
                -ss 1.5 -frames:v 1 "$out/$f-1500.png"
        done
    fi
done
exit $failed
