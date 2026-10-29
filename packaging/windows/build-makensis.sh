#!/usr/bin/env bash
# Builds the makensis that tools.lock names, for Linux, and prints its path.
# Same bytes on the main and legacy branches.
#
#   build-makensis.sh <work dir>
#
# Distributions ship NSIS before 3.11, which has CVE-2025-43715. The compiler is built from source with scons (NSIS's
# POSIX route); stubs, plug-ins and headers come from the release zip. HS_NSIS_ZIP and HS_NSIS_SRC name local copies.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# MUST KILL: the usage text is read from line 5 of this file's header.
[[ $# -eq 1 ]] || { sed -n '5p' "${BASH_SOURCE[0]}" >&2; exit 2; }
mkdir -p "$1"
work=$(cd "$1" && pwd)

lock_value() {
  local value
  value=$(sed -n "s/^$1=//p" "$here/tools.lock")
  [[ -n $value ]] || { echo "tools.lock has no $1" >&2; exit 1; }
  printf '%s' "$value"
}

version=$(lock_value MAKENSIS_VERSION)
dest="$work/nsis-$version"
# makensis finds Stubs, Plugins and Include in the folder above its own, as in the release zip.
makensis="$dest/Bin/makensis"
if [[ -x $makensis && "$("$makensis" -VERSION 2>/dev/null)" == "v$version" ]]; then
  echo "$makensis"
  exit 0
fi

fetch() { # <url> <sha256> <file>
  if [[ ! -f $3 ]]; then
    curl -fsSL --retry 3 -o "$3.part" "$1"
    mv "$3.part" "$3"
  fi
  echo "$2  $3" | sha256sum -c --quiet - >&2
}
zip=${HS_NSIS_ZIP:-$work/nsis-$version.zip}
src=${HS_NSIS_SRC:-$work/nsis-$version-src.tar.bz2}
fetch "$(lock_value NSIS_ZIP_URL)" "$(lock_value NSIS_ZIP_SHA256)" "$zip"
fetch "$(lock_value NSIS_SRC_URL)" "$(lock_value NSIS_SRC_SHA256)" "$src"

rm -rf "$dest" "$work/nsis-unzip" "$work/nsis-src"
mkdir -p "$work/nsis-unzip" "$work/nsis-src"
unzip -q "$zip" -d "$work/nsis-unzip"
mv "$work/nsis-unzip/nsis-$version" "$dest"
rm -rf "$work/nsis-unzip"
tar -xjf "$src" -C "$work/nsis-src" --strip-components=1
(cd "$work/nsis-src" &&
  scons -Q SKIPSTUBS=all SKIPPLUGINS=all SKIPUTILS=all SKIPMISC=all NSIS_CONFIG_CONST_DATA_PATH=no \
    "VERSION=$version" "VER_MAJOR=${version%%.*}" "VER_MINOR=${version#*.}" \
    "PREFIX=$dest" install-compiler) >&2
rm -rf "$work/nsis-src"
mv "$dest/makensis" "$makensis"
[[ "$("$makensis" -VERSION)" == "v$version" ]] || { echo "the makensis built is not $version" >&2; exit 1; }
echo "$makensis"
