#!/bin/bash
# X11 session: Xvfb (one 3200x1000 screen; RandR monitors are set by the
# scenario) with openbox as the window manager and a private session bus.
export GATE_SESSION=x11
"$(dirname "$0")/stop.sh" x11
. "$(cd "$(dirname "$0")/.." && pwd)/env.sh"
dbus-daemon --session --address=unix:path=$XDG_RUNTIME_DIR/bus --fork
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
Xvfb :7 -screen 0 3200x1000x24 +extension RANDR -nolisten tcp > /tmp/xvfb.log 2>&1 &
for i in $(seq 1 50); do [ -e /tmp/.X11-unix/X7 ] && break; sleep 0.1; done
echo "export DISPLAY=:7 QT_QPA_PLATFORM=xcb" > $XDG_RUNTIME_DIR/display.env
export DISPLAY=:7
openbox --sm-disable > /tmp/openbox.log 2>&1 &
sleep 1
