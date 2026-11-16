#!/usr/bin/env bash
# Builds the HikariSub NSIS installer from a staged Windows payload, on Linux.
# Same bytes on the main and legacy branches.
#
#   build-installer.sh --payload <dir> --version <V> --codebase legacy|qt
#                      --out <setup.exe> [--icon <file.ico>]
#                      [--icon-header <FileTypeIcons.h>] [--work <dir>]
#
# HS_MAKENSIS overrides the makensis tools.lock names (same version).
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
payload= version= codebase= out= icon= icon_header= work=

while (($#)); do
  case $1 in
    --payload) payload=$2; shift 2 ;;
    --version) version=$2; shift 2 ;;
    --codebase) codebase=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --icon) icon=$2; shift 2 ;;
    --icon-header) icon_header=$2; shift 2 ;;
    --work) work=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [[ -z $payload || -z $version || -z $codebase || -z $out ]]; then
  # MUST KILL: the usage text is read from lines 5-7 of this file's header.
  sed -n '5,7p' "${BASH_SOURCE[0]}" >&2
  exit 2
fi
work=${work:-$(dirname "$out")/nsis-work}
mkdir -p "$work" "$(dirname "$out")"
work=$(cd "$work" && pwd)
out=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")

lock_value() {
  local value
  value=$(sed -n "s/^$1=//p" "$here/tools.lock")
  [[ -n $value ]] || { echo "tools.lock has no $1" >&2; exit 1; }
  printf '%s' "$value"
}

makensis=${HS_MAKENSIS:-$("$here/build-makensis.sh" "$work")}
want_nsis=$(lock_value MAKENSIS_VERSION)
have_nsis=$("$makensis" -VERSION)
if [[ $have_nsis != "v$want_nsis" ]]; then
  echo "makensis is $have_nsis; tools.lock wants $want_nsis" >&2
  exit 1
fi

args=(--payload "$payload" --version "$version" --codebase "$codebase" --out "$work/gen")
[[ -n $icon ]] && args+=(--icon "$icon")
[[ -n $icon_header ]] && args+=(--icon-header "$icon_header")
python3 "$here/make-payload-nsh.py" "${args[@]}"

rm -f "$out"
"$makensis" -V2 -WX -INPUTCHARSET UTF8 \
  "-DHS_PAYLOAD_NSH=$work/gen/payload.nsh" \
  "-DHS_OUTFILE=$out" \
  "$here/hikarisub.nsi"
test -s "$out"
ls -l "$out"
