#!/bin/bash
# body.sh THEME OUTDIR: today's HikariSub (qt 1f6fe86f8 content, the DG1 build) in THEME at 1280x800 on this
# Xvfb (run under vx11). Writes OUTDIR/body-THEME.png (the main window, the backdrop the mockup's
# dialogs open over) and, with TODAY=1, OUTDIR/today-{about,credits}-THEME.png (today's in-window
# dialogs, opened from the Help menu by keyboard).
set -u
theme=$1 out=$2
APP=${APP:-/home/altq/.t3/worktrees/hikari/w6-dg1/out/build/dg1/src/app/hikarisub}
here=$(cd "$(dirname "$0")" && pwd)
h=$(mktemp -d)
export HOME=$h XDG_CONFIG_HOME=$h/.config XDG_DATA_HOME=$h/.local/share XDG_CACHE_HOME=$h/.cache XDG_RUNTIME_DIR=$h/run
mkdir -p "$XDG_CONFIG_HOME/HikariSub/HikariSub" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cp "$here/fixture.ass" "$h/Episode 03.ass"
cp "$here/station.mkv" "$h/Episode 03.mkv"
printf '[HikariSub v0.0.1]\r\n[Close session]\r\nTab: 0\r\nVideo: %s\r\nPosition: 15500\r\nFFMS2: 1\r\nSubtitles: %s\r\nActive: 6\r\nScroll: 0\r\nEditor: 1\r\n' \
    "$h/Episode 03.mkv" "$h/Episode 03.ass" | sed '1s/^/\xef\xbb\xbf/' > "$XDG_CONFIG_HOME/HikariSub/HikariSub/LastSession.txt"
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nappearance.titleBar=custom\nsession.restore=2\neditor.spellchecker=false\nupdates.autoCheck=false\n' "$theme" \
    > "$XDG_CONFIG_HOME/HikariSub/HikariSub/hikari.ini"
"$APP" > "$h/app.log" 2>&1 &
pid=$!
sleep "${WAIT:-10}"
wid=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | tail -1)
[ -n "$wid" ] && { xdotool windowmove "$wid" 0 0; xdotool windowsize "$wid" 1280 800; sleep 1.5; }
import -window root -crop 1280x800+0+0 +repage "$out/body-$theme.png"
if [ "${TODAY:-}" = 1 ]; then
    for item in about credits; do
        [ "$item" = about ] && n=${ABOUT_DOWNS:-4} || n=${CREDITS_DOWNS:-5}
        xdotool key alt+h; sleep 0.8
        for _ in $(seq 1 "$n"); do xdotool key Down; sleep 0.15; done
        [ -n "${MENU_SHOT:-}" ] && import -window root -crop 1280x800+0+0 +repage "$out/menu-$theme.png"
        xdotool key Return; sleep 1.2
        import -window root -crop 1280x800+0+0 +repage "$out/today-$item-$theme.png"
        xdotool key Escape; sleep 0.6
    done
fi
kill $pid; wait $pid 2>/dev/null
tail -3 "$h/app.log"
rm -rf "$h"
