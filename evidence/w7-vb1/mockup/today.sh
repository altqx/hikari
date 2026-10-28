#!/bin/bash
# today.sh THEME W H OUT: today's HikariSub (AS1's build of main) in THEME at WxH on this Xvfb (run under vx11),
# with fixture.ass and a dummy video, restored through LastSession.txt. Writes OUT/window.png.
set -u
theme=$1 w=$2 h=$3 out=$4
APP=${APP:-/home/altq/.t3/worktrees/hikari/w7-as1/out/build/as1/src/app/hikarisub}
VIDEO=${VIDEO:-?dummy:23.976:4000:1920:1080:47:163:161:c}
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out"
hh=$(cd "$out" && pwd)/home; rm -rf "$hh"; mkdir -p "$hh"
export HOME=$hh XDG_CONFIG_HOME=$hh/.config XDG_DATA_HOME=$hh/.local/share XDG_CACHE_HOME=$hh/.cache XDG_RUNTIME_DIR=$hh/run
mkdir -p "$XDG_CONFIG_HOME/HikariSub/HikariSub" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cp "$here/fixture.ass" "$hh/Episode 03.ass"
printf '[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\nVideo: %s\r\nPosition: 1000\r\nFFMS2: 1\r\nSubtitles: %s\r\nActive: 0\r\nScroll: 0\r\nEditor: 1\r\n' \
    "$VIDEO" "$hh/Episode 03.ass" | sed '1s/^/\xef\xbb\xbf/' > "$XDG_CONFIG_HOME/HikariSub/HikariSub/LastSession.txt"
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nappearance.titleBar=custom\nsession.restore=2\neditor.spellchecker=false\nupdates.autoCheck=false\nanalytics.enabled=false\n' "$theme" \
    > "$XDG_CONFIG_HOME/HikariSub/HikariSub/hikari.ini"
"$APP" > "$hh/app.log" 2>&1 &
echo $! > "$out/app.pid"
sleep "${WAIT:-10}"
wid=$(xdotool search --pid "$(cat "$out/app.pid")" --onlyvisible 2>/dev/null | tail -1)
echo "$wid" > "$out/wid"
[ -n "$wid" ] && { xdotool windowmove "$wid" 0 0; xdotool windowsize "$wid" "$w" "$h"; sleep 1.5; }
import -window root -crop "${w}x${h}+0+0" +repage "$out/window.png"
