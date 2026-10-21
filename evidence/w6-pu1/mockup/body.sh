#!/bin/bash
# body.sh THEME OUTDIR MODE: today's HikariSub (qt ce0c6740e content, the PK1 build) in THEME at 1280x800
# on this Xvfb (run under vx11). MODE doc: the main window with Episode 03 open (the backdrop the bars,
# notices and windows sit on); MODE home: the Home page with a last Session to restore and recent files.
set -u
theme=$1 out=$2 mode=${3:-doc}
APP=${APP:-/home/altq/.t3/worktrees/hikari/w6-dg1/out/build/dg1/src/app/hikarisub}
here=$(cd "$(dirname "$0")" && pwd)
h=$(mktemp -d)
export HOME=$h XDG_CONFIG_HOME=$h/.config XDG_DATA_HOME=$h/.local/share XDG_CACHE_HOME=$h/.cache XDG_RUNTIME_DIR=$h/run
cfg=$XDG_CONFIG_HOME/HikariSub/HikariSub
mkdir -p "$cfg" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
for n in "Episode 03" "Episode 04" "Signs"; do cp "$here/fixture.ass" "$h/$n.ass"; done
cp "$here/station.mkv" "$h/Episode 03.mkv"
printf '[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\nVideo: %s\r\nPosition: 15500\r\nFFMS2: 1\r\nSubtitles: %s\r\nActive: 6\r\nScroll: 0\r\nEditor: 1\r\nTab: 1\r\nSubtitles: %s\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n' \
    "$h/Episode 03.mkv" "$h/Episode 03.ass" "$h/Signs.ass" | sed '1s/^/\xef\xbb\xbf/' > "$cfg/LastSession.txt"
restore=2; [ "$mode" = home ] && restore=0
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nappearance.titleBar=custom\nsession.restore=%s\neditor.spellchecker=false\nupdates.autoCheck=false\n' "$theme" "$restore" \
    > "$cfg/hikari.ini"
if [ "$mode" = home ]; then
    # recent files: open each once so Home lists them
    "$APP" "$h/Signs.ass" > /dev/null 2>&1 & p=$!; sleep 5; kill $p; wait $p 2>/dev/null
    "$APP" "$h/Episode 04.ass" > /dev/null 2>&1 & p=$!; sleep 5; kill $p; wait $p 2>/dev/null
    printf '[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\nVideo: %s\r\nPosition: 15500\r\nFFMS2: 1\r\nSubtitles: %s\r\nActive: 6\r\nScroll: 0\r\nEditor: 1\r\nTab: 1\r\nSubtitles: %s\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n' \
        "$h/Episode 03.mkv" "$h/Episode 03.ass" "$h/Signs.ass" | sed '1s/^/\xef\xbb\xbf/' > "$cfg/LastSession.txt"
fi
"$APP" > "$h/app.log" 2>&1 &
pid=$!
sleep "${WAIT:-10}"
wid=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | tail -1)
[ -n "$wid" ] && { xdotool windowmove "$wid" 0 0; xdotool windowsize "$wid" 1280 800; sleep 1.5; }
import -window root -crop 1280x800+0+0 +repage "$out/$mode-$theme.png"
kill $pid; wait $pid 2>/dev/null
tail -3 "$h/app.log"
rm -rf "$h"
