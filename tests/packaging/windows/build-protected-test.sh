#!/usr/bin/env bash
# Builds protected-test.exe (protected-test.nsi) with the makensis of
# packaging/windows/tools.lock:
#
#   build-protected-test.sh <out.exe>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# The makensis of tools.lock, as build-installer.sh uses, or HS_MAKENSIS.
makensis=${HS_MAKENSIS:-$("$here/../../../packaging/windows/build-makensis.sh" "$(dirname "$1")/nsis-work")}
"$makensis" -V2 -WX -INPUTCHARSET UTF8 "-DHS_OUTFILE=$1" "$here/protected-test.nsi"
test -s "$1"
