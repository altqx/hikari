#!/bin/sh
# Lists every DT_NEEDED soname of the ELF files in a package tree that the
# tree does not provide itself, one per line, sorted: the libraries the host
# must have. The packaging maps them to package dependencies
# (packaging/linux/deps-{deb,rpm,arch}.map).
#
#   tools/release/host-sonames.sh <app tree>
#
# Needs readelf (binutils). This file has the same bytes on the legacy (main)
# and Qt (qt) branches.
set -eu

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
    echo "usage: $0 <app tree>" >&2
    exit 2
fi
tree=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

# What the tree provides: the file name of every file and symlink.
find "$tree" \( -type f -o -type l \) -exec basename {} \; | LC_ALL=C sort -u > "$work/provided"

: > "$work/needed"
find "$tree" -type f -print | while IFS= read -r file; do
    # Only ELF objects; readelf -h fails on anything else.
    readelf -h "$file" >/dev/null 2>&1 || continue
    readelf -d "$file" 2>/dev/null |
        sed -n 's/.*(NEEDED)[^[]*\[\(.*\)\]$/\1/p' >> "$work/needed"
done
LC_ALL=C sort -u "$work/needed" -o "$work/needed"
LC_ALL=C comm -23 "$work/needed" "$work/provided"
