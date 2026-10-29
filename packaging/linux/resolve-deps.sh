#!/bin/sh
# Turns the host libraries a package tree needs into package dependencies, one "<kind><TAB><dependency>" per line.
#
#   packaging/linux/resolve-deps.sh <deb|rpm|arch> <map file> <min glibc> <app tree>
#
# The map (packaging/linux/deps-<family>.map) has "<soname> <dependency>" lines, then after "# runtime" the
# run-time-only "<kind> <dependency>" lines; "@glibc" is the glibc floor. For rpm an unmapped soname becomes
# "<soname>()(64bit)"; for deb and arch it is an error.
#
# Same bytes on the main and legacy branches.
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

# The deb and arch maps carry their own libstdc++ floor.
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

awk '!seen[$0]++' "$work/deps"
