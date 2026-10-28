#!/bin/bash
# tools.sh THEME W H OUT: today.sh, then each rail family clicked in turn; OUT/tool-N.png is the Video panel.
set -u
theme=$1 w=$2 h=$3 out=$4
here=$(cd "$(dirname "$0")" && pwd)
"$here/today.sh" "$theme" "$w" "$h" "$out"
# Dismiss the user-folder toast.
xdotool mousemove 1248 655 click 1; sleep 0.5
for i in $(seq 0 11); do
    xdotool mousemove 19 $((119 + 29 * i)) click 1; sleep 0.8
    xdotool mousemove 330 $((h - 300)); sleep 0.6   # pointer over the video, away from tooltips
    import -window root -crop "${PANEL_W:-640}x$((h - 60))+0+60" +repage "$out/tool-$i.png"
done
kill "$(cat "$out/app.pid")"
