#!/bin/bash
# run.sh <outdir>, under an X server with no window manager: Aegisub 3.5.1 opens each fixture, runs fa3.lua, saves and quits.
set -u
here=$(cd "$(dirname "$0")" && pwd) out=$(mkdir -p "$1" && cd "$1" && pwd)
export HOME=$out/home FA3_OUT=$out
mkdir -p "$HOME/.aegisub/automation/autoload"
cp "$here/fa3.lua" "$HOME/.aegisub/automation/autoload/"
cp "$here/hotkey.json" "$here/config.json" "$HOME/.aegisub/"

run() {
    local input=$1 name=$2 edit=$3 w=
    export FA3_NAME=$name
    cp "$here/../$input" "$out/$name.ass"
    aegisub "$out/$name.ass" >"$out/$name.log" 2>&1 &
    local pid=$!
    for _ in $(seq 60); do
        w=$(xdotool search --pid $pid --onlyvisible --name "$name" 2>/dev/null | head -1)
        [ -n "$w" ] && break
        sleep 1
    done
    sleep 3
    [ -n "$w" ] && xdotool windowfocus --sync "$w"
    xdotool mousemove 500 600 click 1
    sleep 1
    xdotool key --clearmodifiers "ctrl+shift+$([ "$edit" = edit ] && echo F10 || echo F11)"
    sleep 3
    xdotool key --clearmodifiers ctrl+s
    sleep 3
    xdotool key --clearmodifiers ctrl+q
    sleep 3
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
}

run folds.ass folds.aegisub dump
run perspective.ass perspective.aegisub dump
run folds.ass folds-edited.aegisub edit
mv "$out/folds.aegisub.lua-extra.txt" "$out/folds.lua-extra.txt"
mv "$out/perspective.aegisub.lua-extra.txt" "$out/perspective.lua-extra.txt"
