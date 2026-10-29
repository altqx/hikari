#!/bin/sh
# Fails when an ELF file needs glibc above --min-glibc or has a run path that is not $ORIGIN-relative, or a file
# matches a glob of packaging/linux/appimage/host-libs.txt (never bundled).
#
#   check-payload.sh --min-glibc <version> <dir>...
set -euf
LC_ALL=C
export LC_ALL

here=$(cd "$(dirname "$0")" && pwd)
host_list=$here/appimage/host-libs.txt
min_glibc=''
if [ "${1-}" = --min-glibc ]; then
    min_glibc=$2
    shift 2
fi
[ -n "$min_glibc" ] && [ $# -ge 1 ] || { echo "usage: $0 --min-glibc <version> <dir>..." >&2; exit 2; }

status=0
elves=$(mktemp)
trap 'rm -f "$elves"' EXIT
for dir in "$@"; do
    find "$dir" -type f | while IFS= read -r f; do
        if [ "$(od -An -tx1 -N4 "$f" 2>/dev/null | tr -d ' \n')" = 7f454c46 ]; then
            printf '%s\n' "$f"
        fi
    done
done > "$elves"
[ -s "$elves" ] || { echo "error: no ELF files under $*" >&2; exit 1; }

top=$(tr '\n' '\0' < "$elves" | xargs -0 objdump -T 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' |
    sort -uV | tail -1)
if [ -n "$top" ] && [ "$(printf '%s\n%s\n' "$top" "$min_glibc" | sort -V | tail -1)" != "$min_glibc" ]; then
    echo "error: the payload needs GLIBC_$top, above the floor $min_glibc:" >&2
    while IFS= read -r f; do
        objdump -T "$f" 2>/dev/null | grep -q "GLIBC_$top" && echo "  $f" >&2
    done < "$elves"
    status=1
fi
echo "glibc ceiling: GLIBC_${top:-none} (floor $min_glibc)"

while IFS= read -r f; do
    readelf -dW "$f" 2>/dev/null | sed -n 's/.*(R\(UN\)\{0,1\}PATH).*\[\(.*\)\]/\2/p' | tr ':' '\n' |
    while IFS= read -r entry; do
        case "$entry" in
            '$ORIGIN'|'$ORIGIN/'*|'${ORIGIN}'|'${ORIGIN}/'*) ;;
            *) echo "error: $f has the run path entry '$entry'" >&2; echo bad ;;
        esac
    done
done < "$elves" | grep -q bad && status=1

globs=$(grep -v '^[[:space:]]*#' "$host_list" | grep -v '^[[:space:]]*$')
for dir in "$@"; do
    find "$dir" \( -type f -o -type l \) -name '*.so*'
done | while IFS= read -r f; do
    name=${f##*/}
    for g in $globs; do
        # shellcheck disable=SC2254
        case "$name" in
            $g) echo "error: $f is on the host list ($g) and must not be bundled" >&2; echo bad ;;
        esac
    done
done | grep -q bad && status=1

[ $status -eq 0 ] && echo "payload checks passed: $*"
exit $status
