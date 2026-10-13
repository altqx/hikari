#!/usr/bin/env bash
# Build the HikariSub NSIS installer from a staged Windows payload, on Linux
# (installers spec, 6.1). The same file on the legacy and Qt branches.
#
#   build-installer.sh --payload <dir> --version <V> --codebase legacy|qt
#                      --out <setup.exe> [--icon <file.ico>]
#                      [--icon-header <FileTypeIcons.h>] [--work <dir>]
#
# Needs python3, curl, unzip, sha256sum and what build-makensis.sh needs:
# makensis is the one tools.lock names, built into <work>/nsis-<version>
# (or HS_MAKENSIS, which must be that version). StdUtils is downloaded into
# <work>/stdutils and checked against tools.lock; set HS_STDUTILS_ZIP to use
# a local copy of the same zip.
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

# The makensis of tools.lock (build-makensis.sh), or HS_MAKENSIS.
makensis=${HS_MAKENSIS:-$("$here/build-makensis.sh" "$work")}
want_nsis=$(lock_value MAKENSIS_VERSION)
have_nsis=$("$makensis" -VERSION)
if [[ $have_nsis != "v$want_nsis" ]]; then
  echo "makensis is $have_nsis; tools.lock wants $want_nsis" >&2
  exit 1
fi

stdutils_zip=${HS_STDUTILS_ZIP:-$work/$(basename "$(lock_value STDUTILS_URL)")}
if [[ ! -f $stdutils_zip ]]; then
  curl -fsSL --retry 3 -o "$stdutils_zip.part" "$(lock_value STDUTILS_URL)"
  mv "$stdutils_zip.part" "$stdutils_zip"
fi
echo "$(lock_value STDUTILS_SHA256)  $stdutils_zip" | sha256sum -c --quiet -
rm -rf "$work/stdutils"
unzip -q "$stdutils_zip" 'Include/*' 'Plugins/Unicode/*' -d "$work/stdutils"

args=(--payload "$payload" --version "$version" --codebase "$codebase" --out "$work/gen")
[[ -n $icon ]] && args+=(--icon "$icon")
[[ -n $icon_header ]] && args+=(--icon-header "$icon_header")
python3 "$here/make-payload-nsh.py" "${args[@]}"

rm -f "$out"
"$makensis" -V2 -WX -INPUTCHARSET UTF8 \
  "-DHS_PAYLOAD_NSH=$work/gen/payload.nsh" \
  "-DHS_STDUTILS=$work/stdutils" \
  "-DHS_OUTFILE=$out" \
  "$here/hikarisub.nsi"
test -s "$out"
ls -l "$out"
