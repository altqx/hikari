#!/bin/bash
# Hyprland has no headless backend and binds xdg_wm_base 6 (sway offers 5): nested in KWin's virtual backend.
# "hypr": Hyprland's IPC socket path must fit the 107 bytes of a Unix socket address.
export GATE_SESSION=hypr
"$(dirname "$0")/stop.sh" hypr
. "$(cd "$(dirname "$0")/.." && pwd)/env.sh"
dbus-daemon --session --address=unix:path=$XDG_RUNTIME_DIR/bus --fork
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
unset LIBGL_ALWAYS_SOFTWARE
QT_QUICK_BACKEND=software kwin_wayland --virtual --width 1600 --height 1000 --output-count 1 --scale 1 \
    --no-lockscreen --no-global-shortcuts --socket wayland-host > /tmp/hyprhost.log 2>&1 &
for i in $(seq 1 100); do [ -e $XDG_RUNTIME_DIR/wayland-host ] && break; sleep 0.1; done
sleep 1
WAYLAND_DISPLAY=wayland-host Hyprland -c "$GATE_DIR/sessions/hyprland.lua" > /tmp/hyprland.log 2>&1 &
for i in $(seq 1 100); do ls $XDG_RUNTIME_DIR/wayland-[0-9] >/dev/null 2>&1 && break; sleep 0.1; done
w=$(cd $XDG_RUNTIME_DIR && ls -d wayland-[0-9] | head -1)
sig=$(ls -t $XDG_RUNTIME_DIR/hypr | head -1)
echo "export WAYLAND_DISPLAY=$w QT_QPA_PLATFORM=wayland HYPRLAND_INSTANCE_SIGNATURE=$sig" > $XDG_RUNTIME_DIR/display.env
export WAYLAND_DISPLAY=$w
[ -p $XDG_RUNTIME_DIR/vpointer.in ] || mkfifo $XDG_RUNTIME_DIR/vpointer.in
(sleep infinity > $XDG_RUNTIME_DIR/vpointer.in &)
/tmp/vpointer < $XDG_RUNTIME_DIR/vpointer.in > /tmp/vpointer-hypr.log 2>&1 &
nohup wtype -s 2147483647 > /dev/null 2>&1 &
echo "extent 1600 1000" > $XDG_RUNTIME_DIR/vpointer.in
sleep 0.5
