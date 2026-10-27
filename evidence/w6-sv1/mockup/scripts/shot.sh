#!/usr/bin/env bash
# shot.sh <app> <scale> <outprefix> [tool-y]: run under vx11; launch the app with the clip + script,
# fill the screen, pick the rail tool at logical y (rail x 20), screenshot.
set -u
app=$1; scale=$2; out=$3; tooly=${4:-230}
home=~/.cache/sv1/run-$scale; rm -rf "$home"; mkdir -p "$home"/{config,data,cache}
export XDG_CONFIG_HOME=$home/config XDG_DATA_HOME=$home/data XDG_CACHE_HOME=$home/cache QT_SCALE_FACTOR=$scale
"$app" ~/.cache/sv1/media/sharp.ass ~/.cache/sv1/media/clip640.mkv >"$home/app.log" 2>&1 &
pid=$!
sleep 14
win=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | head -1)
read W H < <(xdotool getdisplaygeometry)
xdotool windowmove $win 0 0 windowsize $win $W $H
sleep 3
xdotool mousemove $((20*scale)) $((tooly*scale)) click 1
sleep 1
xdotool mousemove $((W-5)) $((H-5))
sleep 2
import -window root "${out}-panel.png"
if [ -n "${SHOT_FULLSCREEN:-}" ]; then
  xdotool mousemove $((20*scale)) $((113*scale)) click 1; sleep 1
  xdotool mousemove $((80*scale)) $((300*scale)) click --repeat 2 --delay 80 1; sleep 3
  import -window root "${out}-fs0.png"
  if [ -n "${FS_TOOL:-}" ]; then
    xdotool key "$FS_TOOL"; sleep 1
    xdotool mousemove $((W/2)) $((H-5)); sleep 2
    import -window root "${out}-full.png"
  fi
fi
kill $pid; wait $pid 2>/dev/null
