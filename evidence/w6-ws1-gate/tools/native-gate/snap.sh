#!/bin/bash
# snap.sh DIR NAME: screenshot + window list (+ sway tree) + AT-SPI windows and focus.
d=$1; n=$2; mkdir -p "$d"
if [ -n "${SWAYSOCK:-}" ]; then
    grim "$d/$n.png" 2>/dev/null
    python3 "$GATE_DIR/swaytree.py" > "$d/$n.windows.txt"
elif [ -n "${WAYLAND_DISPLAY:-}" ] && [ -n "${GATE_SHOT_CMD:-}" ]; then
    $GATE_SHOT_CMD "$d/$n.png"
elif [ -n "${DISPLAY:-}" ]; then
    import -window root "$d/$n.png" 2>/dev/null
    for w in $(xdotool search --onlyvisible --class hikarisub 2>/dev/null); do
        echo "$w $(xdotool getwindowname $w) $(xdotool getwindowgeometry $w | tr '\n' ' ')"
    done > "$d/$n.windows.txt"
fi
python3 "$GATE_DIR/atspi_tool.py" windows 2>/dev/null | grep -v "<> " >> "$d/$n.windows.txt"
python3 "$GATE_DIR/atspi_tool.py" focus 2>/dev/null >> "$d/$n.windows.txt"
cat "$d/$n.windows.txt"
