#!/usr/bin/env bash
set -u
/tmp/ws2-tools/legacy-wine-open.sh "$1" "$2"
kill $(cat "$2/pid") 2>/dev/null
WINEPREFIX=/tmp/ws2-wine/prefix wineserver -k 2>/dev/null
