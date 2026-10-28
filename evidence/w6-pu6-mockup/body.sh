#!/bin/bash
# body.sh THEME OUTDIR: today's HikariSub (qt 7788b8a7c, the NT1 build) in THEME at W x 800 (W=1280 by
# default; CLICKS open the Log panel, whose place is
# the Spellchecker panel's) on this Xvfb (run under vx11). Writes
# OUTDIR/body-THEME.png: the main window the Spellchecker panel and the windows are drawn beside.
set -u
theme=$1 out=$2
APP=${APP:-/home/altq/.t3/worktrees/hikari/w6-nt1/out/build/nt1/src/app/hikarisub}
W=${W:-1280}
here=$(cd "$(dirname "$0")" && pwd)
h=$(mktemp -d)
export HOME=$h XDG_CONFIG_HOME=$h/.config XDG_DATA_HOME=$h/.local/share XDG_CACHE_HOME=$h/.cache XDG_RUNTIME_DIR=$h/run
mkdir -p "$XDG_CONFIG_HOME/HikariSub/HikariSub" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cp "$here/fixture.ass" "$h/Episode 03.ass"
cp "$here/station.mkv" "$h/Episode 03.mkv"
printf '[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\nVideo: %s\r\nPosition: 9600\r\nFFMS2: 1\r\nSubtitles: %s\r\nActive: 3\r\nScroll: 0\r\nEditor: 1\r\n' \
    "$h/Episode 03.mkv" "$h/Episode 03.ass" | sed '1s/^/\xef\xbb\xbf/' > "$XDG_CONFIG_HOME/HikariSub/HikariSub/LastSession.txt"
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nappearance.titleBar=custom\nsession.restore=2\neditor.spellchecker=false\nupdates.autoCheck=false\n' "$theme" \
    > "$XDG_CONFIG_HOME/HikariSub/HikariSub/hikari.ini"
"$APP" > "$h/app.log" 2>&1 &
pid=$!
sleep "${WAIT:-10}"
wid=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | tail -1)
[ -n "$wid" ] && { xdotool windowmove "$wid" 0 0; xdotool windowsize "$wid" "$W" 800; sleep 1.5; }
# CLICKS: "x,y x,y ..." pressed in turn (File, then Log: the Log panel takes the place along the bottom).
for c in ${CLICKS:-}; do xdotool mousemove ${c%,*} ${c#*,} click 1; sleep 1.2; done
[ -n "${CLICKS:-}" ] && xdotool mousemove 1279 400 && sleep 1
import -window root -crop ${W}x800+0+0 +repage "$out/body-$theme.png"
kill $pid; wait $pid 2>/dev/null
tail -3 "$h/app.log"
rm -rf "$h"
