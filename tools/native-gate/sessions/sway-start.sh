#!/bin/bash
# sway session: headless wlroots backend with two outputs (the right one at
# scale 2), a virtual pointer fed through a FIFO, a private session bus.
export GATE_SESSION=sway
"$(dirname "$0")/stop.sh" sway
. "$(cd "$(dirname "$0")/.." && pwd)/env.sh"
dbus-daemon --session --address=unix:path=$XDG_RUNTIME_DIR/bus --fork
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
    sway -c "$GATE_DIR/sessions/sway.config" > /tmp/sway.log 2>&1 &
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.*.sock >/dev/null 2>&1 && break; sleep 0.1; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock) WAYLAND_DISPLAY=wayland-1
echo "export WAYLAND_DISPLAY=wayland-1 QT_QPA_PLATFORM=wayland" > $XDG_RUNTIME_DIR/display.env
[ -p $XDG_RUNTIME_DIR/vpointer.in ] || mkfifo $XDG_RUNTIME_DIR/vpointer.in
(sleep infinity > $XDG_RUNTIME_DIR/vpointer.in &)
/tmp/vpointer < $XDG_RUNTIME_DIR/vpointer.in > /tmp/vpointer.log 2>&1 &
# A keyboard that stays: every wtype run adds and removes its own virtual
# keyboard, and a seat without any keyboard drops the clients' keyboard focus.
nohup wtype -s 2147483647 > /dev/null 2>&1 &
swaymsg 'workspace 1; move workspace to output HEADLESS-1; focus output HEADLESS-1' >/dev/null
echo "extent 2400 1000" > $XDG_RUNTIME_DIR/vpointer.in
sleep 0.5
