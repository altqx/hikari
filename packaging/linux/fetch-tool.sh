#!/bin/sh
# Downloads a tool pinned in packaging/linux/tools.lock into <dir> (reusing a file with the pinned hash), checks its
# SHA-256 and prints its path.
#
#   packaging/linux/fetch-tool.sh <appimagetool|type2-runtime|nfpm> <dir>
#   packaging/linux/fetch-tool.sh --image <name>     (prints the image reference)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
lock=$here/tools.lock

if [ "${1-}" = --image ]; then
    [ $# -eq 2 ] || { echo "usage: $0 --image <name>" >&2; exit 2; }
    ref=$(awk -v n="$2" '$1 == "image" && $2 == n { print $3 }' "$lock")
    [ -n "$ref" ] || { echo "no image $2 in $lock" >&2; exit 1; }
    echo "$ref"
    exit 0
fi
[ $# -eq 2 ] || { echo "usage: $0 <tool> <dir>" >&2; exit 2; }
name=$1 dir=$2
line=$(awk -v n="$name" '$1 == n && $1 != "image" { print $2, $3, $4 }' "$lock")
[ -n "$line" ] || { echo "no tool $name in $lock" >&2; exit 1; }
set -- $line
version=$1 sha256=$2 url=$3
file=$dir/$(basename "$url")
mkdir -p "$dir"
if [ ! -f "$file" ] || ! echo "$sha256  $file" | sha256sum -c --status; then
    curl -fsSL --retry 3 -o "$file.part" "$url"
    if ! echo "$sha256  $file.part" | sha256sum -c --status; then
        rm -f "$file.part"
        echo "$name $version: SHA-256 mismatch for $url" >&2
        exit 1
    fi
    mv "$file.part" "$file"
fi

case $name in
appimagetool | type2-runtime)
    chmod 0755 "$file"
    echo "$file" ;;
nfpm)
    out=$dir/nfpm-$version
    if [ ! -x "$out/nfpm" ]; then
        mkdir -p "$out"
        tar -xzf "$file" -C "$out" nfpm
    fi
    echo "$out/nfpm" ;;
*)
    echo "$file" ;;
esac
