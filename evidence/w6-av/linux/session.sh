#!/usr/bin/env bash
# Runs inside vx11: a private PipeWire (no hardware), two null sinks, HikariSub
# with a fresh profile. Writes env.sh for the driver; ends when $N/stop exists.
set -u
N=/tmp/w6av/native
APP=${APP:?}
export XDG_RUNTIME_DIR=$N/run; rm -rf "$XDG_RUNTIME_DIR"; mkdir -m 700 -p "$XDG_RUNTIME_DIR"
export PIPEWIRE_RUNTIME_DIR=$XDG_RUNTIME_DIR PULSE_RUNTIME_PATH=$XDG_RUNTIME_DIR/pulse
export XDG_CONFIG_HOME=$N/cfg
export XDG_DATA_HOME=$N/data XDG_STATE_HOME=$N/state XDG_CACHE_HOME=$N/cache
mkdir -p "$XDG_DATA_HOME" "$XDG_STATE_HOME" "$XDG_CACHE_HOME"
rm -f $N/stop
pipewire > $N/pipewire.log 2>&1 &
sleep 1
wireplumber > $N/wireplumber.log 2>&1 &
pipewire-pulse > $N/pipewire-pulse.log 2>&1 &
sleep 2
pactl load-module module-null-sink sink_name=studio sink_properties=device.description=Studio_Monitors > $N/studio.module
pactl load-module module-null-sink sink_name=headset sink_properties=device.description=USB_Headset > $N/headset.module
pactl set-default-sink studio
cat > $N/env.sh <<EOS
export DISPLAY=$DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR PIPEWIRE_RUNTIME_DIR=$XDG_RUNTIME_DIR PULSE_RUNTIME_PATH=$XDG_RUNTIME_DIR/pulse
export DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS
EOS
"$APP" "$@" > $N/app.log 2>&1 &
echo $! > $N/app.pid
until [ -e $N/stop ]; do sleep 1; done
kill $(cat $N/app.pid) 2>/dev/null; sleep 2
kill %1 %2 %3 2>/dev/null
