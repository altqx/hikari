#!/bin/bash
# body.sh THEME OUTDIR (under vx11): today's Style manager (the PK1 build, qt 58192af34 content) in THEME,
# with fixture.ass open and Default.sty as the catalog. Writes OUTDIR/today-manager-THEME.png and, after
# a double click on the Document's first Style, OUTDIR/today-editor-THEME.png.
set -u
theme=$1 out=$2
APP=${APP:-/home/altq/.t3/worktrees/hikari/w6-dg1/out/build/dg1/src/app/hikarisub}
here=$(cd "$(dirname "$0")" && pwd)
h=$(mktemp -d)
export HOME=$h XDG_CONFIG_HOME=$h/.config XDG_DATA_HOME=$h/.local/share XDG_CACHE_HOME=$h/.cache XDG_RUNTIME_DIR=$h/run
cfg=$XDG_CONFIG_HOME/HikariSub/HikariSub
mkdir -p "$cfg/Catalog" "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
cp "$here/fixture.ass" "$h/Episode 03.ass"
cp "$here/Default.sty" "$cfg/Catalog/Default.sty"
printf '[registry]\nschema=1\n\n[profile]\nappearance.theme=%s\nappearance.followSystem=false\nappearance.titleBar=custom\nsession.restore=0\neditor.spellchecker=false\nupdates.autoCheck=false\n' "$theme" > "$cfg/hikari.ini"
"$APP" "$h/Episode 03.ass" > "$h/app.log" 2>&1 &
pid=$!
sleep "${WAIT:-9}"
wid=$(xdotool search --pid $pid --onlyvisible 2>/dev/null | tail -1)
[ -n "$wid" ] && { xdotool windowmove "$wid" 0 0; xdotool windowsize "$wid" 1280 800; sleep 1.5; }
xdotool key ctrl+m; sleep 2
sm=$(xdotool search --pid $pid --onlyvisible --name "Style manager" 2>/dev/null | tail -1)
if [ -n "$sm" ]; then
    xdotool windowmove "$sm" 0 0; sleep 1
    eval "$(xdotool getwindowgeometry --shell "$sm")"
    import -window root -crop ${WIDTH}x${HEIGHT}+0+0 +repage "$out/today-manager-$theme.png"
    # The Document's list sits in the lower half of the left column.
    xdotool mousemove ${EDIT_X:-60} ${EDIT_Y:-567} click --repeat 2 --delay 90 1; sleep 2
    import -window root -crop ${WIDTH}x${HEIGHT}+0+0 +repage "$out/today-editor-$theme.png"
    xdotool mousemove 485 268 click 1; sleep 2
    import -window root -crop ${WIDTH}x${HEIGHT}+0+0 +repage "$out/today-popover-$theme.png"
    xdotool key Escape; sleep 1
    xdotool mousemove 891 115 click 1; sleep 2
    import -window root -crop ${WIDTH}x${HEIGHT}+0+0 +repage "$out/today-fontlist-$theme.png"
    xdotool key Escape; sleep 0.5
    echo "manager ${WIDTH}x${HEIGHT}"
else
    import -window root -crop 1280x800+0+0 +repage "$out/today-fail-$theme.png"
    echo "no Style manager window"
fi
kill $pid; wait $pid 2>/dev/null
tail -3 "$h/app.log"
rm -rf "$h"
