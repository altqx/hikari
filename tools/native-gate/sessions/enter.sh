#!/bin/bash
# enter.sh SESSION CMD...: run CMD inside a running session's environment
# (its private HOME/XDG tree, session bus and display).
export GATE_SESSION="$1"; shift
. /home/altq/Work/hikari-wt/D1-gate/tools/native-gate/env.sh
export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
[ -f "$XDG_RUNTIME_DIR/display.env" ] && . "$XDG_RUNTIME_DIR/display.env"
if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
    w=$(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | head -1)
    [ -n "$w" ] && export WAYLAND_DISPLAY="$w"
fi
s=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -1)
[ -n "$s" ] && export SWAYSOCK="$s"
exec "$@"
