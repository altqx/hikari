#!/bin/bash
# today.sh THEME OUTDIR [W H] (under vx11): today's Line editor (the TM1 part-2 build: qt + RunStrip) in THEME
# with fixture.ass open on Line 3, window W x H. Writes OUTDIR/today-THEME-WxH.png (the whole window).
set -u
theme=$1 out=$2 W=${3:-1280} H=${4:-800}
APP=${APP:-/home/altq/.t3/worktrees/hikari/w6-tm1/out/build/tm1/src/app/hikarisub}
here=$(cd "$(dirname "$0")" && pwd)
h=$(mktemp -d -p "$here/out")
export HOME=$h XDG_CONFIG_HOME=$h/.config XDG_DATA_HOME=$h/.local/share XDG_CACHE_HOME=$h/.cache XDG_RUNTIME_DIR=$h/run
cfg=$XDG_CONFIG_HOME/HikariSub/HikariSub
mkdir -p "$cfg" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cp "$here/fixture.ass" "$h/Episode 03.ass"
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nappearance.titleBar=custom\nsession.restore=0\neditor.spellchecker=false\nupdates.autoCheck=false\n' "$theme" > "$cfg/hikari.ini"
"$APP" "$h/Episode 03.ass" > "$h/app.log" 2>&1 &
pid=$!
sleep "${WAIT:-9}"
wid=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | tail -1)
[ -n "$wid" ] && { xdotool windowmove "$wid" 0 0; xdotool windowsize "$wid" $W $H; sleep 1.5; }
# CLICK="x y": a click (a Grid row); DRAG="x1 y1 x2 y2": a separator drag; TAG: a file name suffix.
[ -n "${CLICK:-}" ] && { set -- $CLICK; xdotool mousemove $1 $2 click 1; sleep 1; }
[ -n "${DRAG:-}" ] && { set -- $DRAG; xdotool mousemove $1 $2 mousedown 1; sleep .3; xdotool mousemove $(( ($1+$3)/2 )) $(( ($2+$4)/2 )); sleep .3; xdotool mousemove $3 $4; sleep .3; xdotool mouseup 1; sleep 1; }
xdotool mousemove ${PARK:-5 795}
sleep 1
import -window root -crop ${W}x${H}+0+0 +repage "$out/today-$theme-${W}x$H${TAG:-}.png"
kill $pid; wait $pid 2>/dev/null
tail -3 "$h/app.log"
rm -rf "$h"
