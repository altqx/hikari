#!/usr/bin/env bash
# Under vx11: open $1 (copy in $2) in legacy HikariSub, append " x" to line 1's text, Ctrl+S, kill.
set -u
/tmp/ws2-tools/legacy-wine-open.sh "$1" "$2"
xdotool mousemove 1000 297 click 1; sleep 1; xdotool key End; xdotool type --delay 100 " x"; sleep 1
import -window root "$2/edited.png"
xdotool key ctrl+s; sleep 4
import -window root "$2/saved.png"
xdotool search --onlyvisible --name "." getwindowname %@ > "$2/windows-after.txt" 2>/dev/null
kill $(cat "$2/pid") 2>/dev/null
WINEPREFIX=/tmp/ws2-wine/prefix wineserver -k 2>/dev/null
f="$2/$(basename "$1")"; cmp -s "$f" "$2/before-$(basename "$1")" && echo "UNCHANGED" || echo "CHANGED"
