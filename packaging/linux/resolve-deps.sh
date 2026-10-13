#!/bin/sh
# Turns the host libraries a package tree needs into package dependencies.
#
#   packaging/linux/resolve-deps.sh <deb|rpm|arch> <map file> <min glibc> <app tree>
#
# Prints one dependency per line, prefixed by its kind and a tab:
#   depends<TAB>libgtk-3-0t64 | libgtk-3-0
#   recommends<TAB>gstreamer1.0-plugins-bad      (deb, rpm)
#   optdepends<TAB>gst-plugins-bad               (arch)
#
# The sonames come from tools/release/host-sonames.sh. The map file
# (packaging/linux/deps-<family>.map) has one "<soname> <dependency>" line per
# soname, and then, after a line that reads "# runtime", the run-time-only
# dependencies as "<kind> <dependency>" lines. The dependency "@glibc" stands
# for the glibc floor: deb "libc6 (>= <min glibc>)", rpm
# "glibc >= <min glibc>", arch "glibc". For rpm a soname without a map line
# becomes its own capability, "<soname>()(64bit)"; for deb and arch an
# unmapped soname is an error, so a new host library cannot slip through
# undeclared.
#
# This file has the same bytes on the legacy (main) and Qt (qt) branches.
set -eu

if [ $# -ne 4 ]; then
    echo "usage: $0 <deb|rpm|arch> <map file> <min glibc> <app tree>" >&2
    exit 2
fi
family=$1 map=$2 min_glibc=$3 tree=$4
here=$(cd "$(dirname "$0")" && pwd)
host_sonames=$here/../../tools/release/host-sonames.sh
case $family in
deb | rpm | arch) ;;
*) echo "unknown package family: $family" >&2; exit 2 ;;
esac
[ -f "$map" ] || { echo "no such map file: $map" >&2; exit 2; }

glibc_dep() {
    case $family in
    deb) echo "libc6 (>= $min_glibc)" ;;
    rpm) echo "glibc >= $min_glibc" ;;
    arch) echo "glibc" ;;
    esac
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
"$host_sonames" "$tree" > "$work/sonames"

# The soname part of the map, and the runtime part.
awk '/^# runtime[[:space:]]*$/ { exit } !/^[[:space:]]*(#|$)/' "$map" > "$work/sonames.map"
awk 'found && !/^[[:space:]]*(#|$)/ { print } /^# runtime[[:space:]]*$/ { found = 1 }' "$map" > "$work/runtime.map"

: > "$work/unmapped"
: > "$work/deps"
while IFS= read -r soname; do
    dep=$(awk -v s="$soname" '$1 == s { sub(/^[^[:space:]]+[[:space:]]+/, ""); print; exit }' "$work/sonames.map")
    if [ -z "$dep" ]; then
        if [ "$family" = rpm ]; then
            dep="$soname()(64bit)"
        else
            echo "$soname" >> "$work/unmapped"
            continue
        fi
    fi
    [ "$dep" = "@glibc" ] && dep=$(glibc_dep)
    printf 'depends\t%s\n' "$dep" >> "$work/deps"
done < "$work/sonames"

if [ -s "$work/unmapped" ]; then
    echo "error: no $family dependency for these host libraries; add them to $map:" >&2
    sed 's/^/  /' "$work/unmapped" >&2
    exit 1
fi

# The glibc floor always, and for rpm the newest libstdc++ symbol version the
# tree uses (the deb and arch maps carry their own libstdc++ floor).
printf 'depends\t%s\n' "$(glibc_dep)" >> "$work/deps"
if [ "$family" = rpm ] && grep -qx 'libstdc++.so.6' "$work/sonames"; then
    glibcxx=$(find "$tree" -type f -exec sh -c 'readelf -h "$1" >/dev/null 2>&1 && objdump -T "$1"' sh {} \; 2>/dev/null |
        grep -o 'GLIBCXX_[0-9][0-9.]*' | sort -uV | tail -n 1)
    [ -n "$glibcxx" ] && printf 'depends\tlibstdc++.so.6(%s)(64bit)\n' "$glibcxx" >> "$work/deps"
fi

while read -r kind dep; do
    case $kind in
    depends | recommends | optdepends) printf '%s\t%s\n' "$kind" "$dep" >> "$work/deps" ;;
    *) echo "bad runtime line in $map: $kind $dep" >&2; exit 1 ;;
    esac
done < "$work/runtime.map"

# Unique, first occurrence kept, so the output is stable.
awk '!seen[$0]++' "$work/deps"
