#!/bin/bash
# KWin session: kwin_wayland on its virtual backend with a private session bus.
# Input comes through KWin's EIS (libei) remote-desktop D-Bus entry point.
export GATE_SESSION=kwin
"$(dirname "$0")/stop.sh" kwin
. /home/altq/Work/hikari-wt/D1-gate/tools/native-gate/env.sh
dbus-daemon --session --address=unix:path=$XDG_RUNTIME_DIR/bus --fork
export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
export KWIN_WAYLAND_NO_PERMISSION_CHECKS=1
# ScreenShot2 is restricted to executables whose desktop entry names it.
mkdir -p "$XDG_DATA_HOME/applications"
printf '[Desktop Entry]\nType=Application\nName=gate shot\nExec=%s\nX-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\nNoDisplay=true\n' \
    "$(readlink -f /usr/bin/python3)" > "$XDG_DATA_HOME/applications/gate-shot.desktop"
kwin_wayland --virtual --width ${KWIN_WIDTH:-1600} --height ${KWIN_HEIGHT:-1000} \
    --output-count ${KWIN_OUTPUTS:-1} --scale ${KWIN_SCALE:-1} --no-lockscreen --no-global-shortcuts \
    --socket wayland-gate > /tmp/kwin.log 2>&1 &
for i in $(seq 1 100); do [ -e $XDG_RUNTIME_DIR/wayland-gate ] && break; sleep 0.1; done
echo "export WAYLAND_DISPLAY=wayland-gate QT_QPA_PLATFORM=wayland" > $XDG_RUNTIME_DIR/display.env
sleep 2
