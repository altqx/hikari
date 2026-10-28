#!/bin/bash
# GNOME compositor session: mutter 50 (GNOME's compositor; gnome-shell itself
# needs systemd-logind, which a container has not) headless on Wayland with
# virtual monitors and a private session bus. Input comes through mutter's
# RemoteDesktop + EIS (eidaemon.py mutter); screenshots through its
# ScreenCast API and PipeWire (mutter_shot.py).
export GATE_SESSION=mutter
"$(dirname "$0")/stop.sh" mutter
. "$(cd "$(dirname "$0")/.." && pwd)/env.sh"
dbus-daemon --session --address=unix:path=$XDG_RUNTIME_DIR/bus --fork
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
pipewire > /tmp/pipewire.log 2>&1 &
sleep 0.5
wireplumber > /tmp/wireplumber.log 2>&1 &
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=GNOME
mutter --headless --wayland --no-x11 --wayland-display wayland-gate \
    --virtual-monitor ${MUTTER_MON1:-1600x1000} ${MUTTER_MON2:+--virtual-monitor $MUTTER_MON2} \
    > /tmp/mutter.log 2>&1 &
for i in $(seq 1 200); do [ -e $XDG_RUNTIME_DIR/wayland-gate ] && break; sleep 0.1; done
echo "export WAYLAND_DISPLAY=wayland-gate QT_QPA_PLATFORM=wayland" > $XDG_RUNTIME_DIR/display.env
sleep 3
