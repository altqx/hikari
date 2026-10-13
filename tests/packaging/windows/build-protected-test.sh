#!/usr/bin/env bash
# Builds protected-test.exe (protected-test.nsi) with makensis:
#
#   build-protected-test.sh <out.exe>
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
makensis -V2 -WX -INPUTCHARSET UTF8 "-DHS_OUTFILE=$1" "$here/protected-test.nsi"
test -s "$1"
