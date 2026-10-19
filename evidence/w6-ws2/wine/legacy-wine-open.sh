#!/usr/bin/env bash
# Under vx11: open $1 (copied into $2) in legacy HikariSub under Wine; screenshot; leave pid in $2/pid.
set -u
in="$1"; out="$2"; mkdir -p "$out"
f="$out/$(basename "$in")"; cp "$in" "$f"; cp "$f" "$out/before-$(basename "$in")"
export WINEPREFIX=/tmp/ws2-wine/prefix WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
wine /tmp/ws2-wine/app/HikariSub_x64/HikariSub.exe "Z:${f//\//\\}" > "$out/hikarisub.log" 2>&1 &
pid=$!; echo $pid > "$out/pid"
for i in $(seq 1 90); do
  n=$(xdotool search --onlyvisible --name "." 2>/dev/null | wc -l); [ "$n" -gt 0 ] && break; sleep 1
done
sleep 10
xdotool search --onlyvisible --name "." getwindowname %@ > "$out/windows.txt" 2>/dev/null
import -window root "$out/opened.png"
