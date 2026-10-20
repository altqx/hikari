#!/bin/bash
# body.sh THEME OUT.png: the current app (WS3's build) in THEME at 1280x800 on
# this Xvfb (run under vx11); the mockup draws its title bar over the menu row.
set -u
theme=$1 out=$2
APP=${APP:-/home/altq/.t3/worktrees/hikari/w6-ws3/out/build/ws3/src/app/hikarisub}
h=$(mktemp -d)
export HOME=$h XDG_CONFIG_HOME=$h/.config XDG_DATA_HOME=$h/.local/share XDG_CACHE_HOME=$h/.cache XDG_RUNTIME_DIR=$h/run
mkdir -p "$XDG_CONFIG_HOME/HikariSub/HikariSub" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cp "$(dirname "$0")/fixture.ass" "$h/Episode 03.ass"
cp "$(dirname "$0")/station.mkv" "$h/Episode 03.mkv"
printf '[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\nVideo: %s\r\nPosition: 15500\r\nFFMS2: 1\r\nSubtitles: %s\r\nActive: 6\r\nScroll: 0\r\nEditor: 1\r\n' \
    "$h/Episode 03.mkv" "$h/Episode 03.ass" | sed '1s/^/\xef\xbb\xbf/' > "$XDG_CONFIG_HOME/HikariSub/HikariSub/LastSession.txt"
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nsession.restore=2\neditor.spellchecker=false\n' "$theme" \
    > "$XDG_CONFIG_HOME/HikariSub/HikariSub/hikari.ini"
"$APP" > "$h/app.log" 2>&1 &
pid=$!
sleep ${WAIT:-9}
import -window root "$out"
if [ -n "${MENU_OUT:-}" ]; then   # the real layout menu, opened from the status bar's switcher
    xdotool mousemove 1230 782 click 1; sleep 1.5
    import -window root "$MENU_OUT"
fi
kill $pid; wait $pid 2>/dev/null
tail -5 "$h/app.log"
