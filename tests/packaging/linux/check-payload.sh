#!/bin/sh
# Checks a package payload (an extracted tar.gz, AppDir, deb or rpm tree):
#
#   tests/packaging/linux/check-payload.sh <dir> <max glibc>
#
#  - the newest GLIBC_ symbol version any ELF file needs is at most
#    <max glibc> (2.35 for legacy, the manifest's min_glibc);
#  - every RPATH and RUNPATH entry is $ORIGIN-relative, so no build path
#    ships;
#  - no file matches packaging/linux/appimage/host-libs.txt.
#
# Needs readelf and objdump (binutils).
set -eu

if [ $# -ne 2 ] || [ ! -d "$1" ]; then
    echo "usage: $0 <dir> <max glibc>" >&2
    exit 2
fi
dir=$1 max_glibc=$2
here=$(cd "$(dirname "$0")" && pwd)
host_libs=$here/../../../packaging/linux/appimage/host-libs.txt
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
status=0

find "$dir" -type f -print > "$work/files"
: > "$work/elves"
while IFS= read -r file; do
    readelf -h "$file" >/dev/null 2>&1 && printf '%s\n' "$file" >> "$work/elves"
done < "$work/files"
echo "$(wc -l < "$work/elves") ELF files in $dir"
[ -s "$work/elves" ] || { echo "no ELF files in $dir; is it the extracted payload?" >&2; exit 1; }

: > "$work/glibc"
while IFS= read -r file; do
    objdump -T "$file" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sed "s|^|$file |" >> "$work/glibc" || true
    readelf -d "$file" 2>/dev/null |
        sed -n 's/.*(\(RPATH\|RUNPATH\))[^[]*\[\(.*\)\]$/\2/p' | tr ':' '\n' |
        while IFS= read -r entry; do
            case $entry in
            '$ORIGIN' | '$ORIGIN/'* | '${ORIGIN}' | '${ORIGIN}/'*) ;;
            *) echo "FAIL: $file has the RPATH/RUNPATH entry $entry"; echo x >> "$work/bad" ;;
            esac
        done
done < "$work/elves"
[ -f "$work/bad" ] && status=1

newest=$(cut -d ' ' -f 2 "$work/glibc" | sed 's/GLIBC_//' | sort -uV | tail -n 1)
echo "newest GLIBC symbol version: ${newest:-none}"
if [ -n "$newest" ]; then
    top=$(printf '%s\n%s\n' "$newest" "$max_glibc" | sort -V | tail -n 1)
    if [ "$top" != "$max_glibc" ]; then
        echo "FAIL: GLIBC_$newest is above the floor $max_glibc, needed by:"
        grep " GLIBC_$newest\$" "$work/glibc" | cut -d ' ' -f 1 | sort -u | sed 's/^/  /'
        status=1
    fi
fi

grep -v '^[[:space:]]*\(#\|$\)' "$host_libs" > "$work/globs"
while IFS= read -r file; do
    name=$(basename "$file")
    while IFS= read -r glob; do
        # shellcheck disable=SC2254
        case $name in
        $glob) echo "FAIL: $file is a host library ($glob)"; status=1 ;;
        esac
    done < "$work/globs"
done < "$work/files"

[ $status -eq 0 ] && echo "payload check passed: $dir"
exit $status
