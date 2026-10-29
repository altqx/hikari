#!/bin/sh
# Lists the DT_NEEDED sonames of a package tree's ELF files that the tree does not provide, sorted. Needs readelf.
#
#   tools/release/host-sonames.sh <app tree>
#
# Same bytes on the main and legacy branches.
set -eu

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
    echo "usage: $0 <app tree>" >&2
    exit 2
fi
tree=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

find "$tree" \( -type f -o -type l \) -exec basename {} \; | LC_ALL=C sort -u > "$work/provided"

: > "$work/needed"
find "$tree" -type f -print | while IFS= read -r file; do
    readelf -h "$file" >/dev/null 2>&1 || continue
    readelf -d "$file" 2>/dev/null |
        sed -n 's/.*(NEEDED)[^[]*\[\(.*\)\]$/\1/p' >> "$work/needed"
done
LC_ALL=C sort -u "$work/needed" -o "$work/needed"
LC_ALL=C comm -23 "$work/needed" "$work/provided"
