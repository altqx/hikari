#!/usr/bin/env bash
# settings.sh <app> <out> <theme> [clicks...]: launch the app, run "x,y" clicks with 2 s pauses, screenshot after each.
set -u
app=$1; out=$2; theme=$3; shift 3
home=~/.cache/sv1/run-set; rm -rf "$home"; mkdir -p "$home"/{config,data,cache}/HikariSub/HikariSub
export XDG_CONFIG_HOME=$home/config XDG_DATA_HOME=$home/data XDG_CACHE_HOME=$home/cache
[ "$theme" != - ] && printf '[appearance]\ntheme=%s\nfollowSystem=false\n' "$theme" > "$home/config/HikariSub/HikariSub/hikari.ini"
"$app" >"$home/app.log" 2>&1 &
pid=$!
sleep 12
win=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | head -1)
xdotool windowmove $win 0 0 windowsize $win 1600 1000; sleep 2
i=0
for c in "$@"; do
  xdotool mousemove ${c%,*} ${c#*,} click 1; sleep 2
  i=$((i+1)); import -window root "$out-$i.png"
done
kill $pid; wait $pid 2>/dev/null
