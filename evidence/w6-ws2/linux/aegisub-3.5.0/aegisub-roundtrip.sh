#!/usr/bin/env bash
# Under vx11: open $1 (a copy) in Aegisub, Ctrl+S, quit; screenshots and the saved file in $2.
set -u
in="$1"; out="$2"; mkdir -p "$out"
cp "$in" "$out/$(basename "$in")"; f="$out/$(basename "$in")"
cp "$f" "$out/before-$(basename "$in")"
GDK_BACKEND=x11 aegisub "$f" > "$out/aegisub.log" 2>&1 &
pid=$!
for i in $(seq 1 60); do
  wid=$(xdotool search --pid $pid --onlyvisible --name "Aegisub" 2>/dev/null | head -1)
  [ -n "$wid" ] && break; sleep 1
done
sleep 4
xdotool search --onlyvisible --name "." getwindowname %@ > "$out/windows.txt" 2>/dev/null
import -window root "$out/opened.png" 2>/dev/null
xdotool mousemove 107 128 click 1; sleep 1; import -window root "$out/edited.png"; xdotool mousemove 96 48 click 1; sleep 3
import -window root "$out/saved.png" 2>/dev/null
xdotool search --onlyvisible --name "." getwindowname %@ > "$out/windows-after.txt" 2>/dev/null
kill $pid 2>/dev/null; wait $pid 2>/dev/null
cmp -s "$f" "$out/before-$(basename "$in")" && echo "UNCHANGED" || echo "CHANGED"
