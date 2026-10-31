#!/bin/bash
# run.sh <build dir> <runs> [pipewire|pulseaudio|default ...]: removes the playing sink under
# hikari_backends_general_player_tests in a container with its own PipeWire, WirePlumber and
# pipewire-pulse (AUDIO_SERVER=pulseaudio: a PulseAudio daemon instead). `default` leaves
# QT_AUDIO_BACKEND unset. Never touches the host's audio. Logs go to <build dir>/audio-removal.
set -euo pipefail
build=$(realpath "$1")
runs=$2
shift 2
backends=("${@:-pipewire pulseaudio}")
here=$(dirname "$(realpath "$0")")
sdk=$(sed -n 's/^HIKARI_SDK_DIR:[A-Z]*=//p' "$build/CMakeCache.txt")
work=$build/audio-removal
rm -rf "$work"
mkdir -p "$work"
ffmpeg -loglevel error -f lavfi -i testsrc=size=64x48:rate=10 -f lavfi -i sine=frequency=440:sample_rate=48000 \
    -t 60 -c:v mpeg4 -c:a pcm_s16le "$work/tone.mkv"
server=${AUDIO_SERVER:-pipewire}
case $server in
pipewire) packages="pipewire wireplumber pipewire-pulse" ;;
pulseaudio) packages=pulseaudio ;;
*) exit 2 ;;
esac
image=hikari-audio-removal-$server
docker build -q -t "$image" - >/dev/null <<DOCKERFILE
FROM archlinux:latest
RUN pacman -Syu --noconfirm $packages libpulse dbus libglvnd fontconfig freetype2 \
    libx11 libxrandr libxkbcommon krb5 brotli && pacman -Scc --noconfirm
DOCKERFILE
docker run --rm -v "$build:$build:ro" -v "$sdk:$sdk:ro" -v "$here:/audio-removal:ro" -v "$work:/work" \
    -e AUDIO_REMOVAL_MEDIA=/work/tone.mkv -e HIKARI_AUDIO_REMOVAL=/audio-removal/sinks.sh \
    -e QT_QPA_PLATFORM=offscreen -e BUILD="$build" -e RUNS="$runs" -e BACKENDS="${backends[*]}" -e SERVER="$server" \
    "$image" bash -c '
set -u
export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -m 700 -p $XDG_RUNTIME_DIR
eval "$(dbus-launch --sh-syntax)"
if [ "$SERVER" = pulseaudio ]; then
    pulseaudio --daemonize=no --exit-idle-time=-1 >/tmp/pulseaudio.log 2>&1 &
else
    pipewire >/tmp/pipewire.log 2>&1 &
    wireplumber >/tmp/wireplumber.log 2>&1 &
    pipewire-pulse >/tmp/pipewire-pulse.log 2>&1 &
fi
for i in $(seq 50); do pactl info >/dev/null 2>&1 && break; sleep 0.2; done
sinks() {
    pactl list short modules | awk "\$2 == \"module-null-sink\" { print \$1 }" | xargs -r -n1 pactl unload-module
    pactl load-module module-null-sink sink_name=hk_a >/dev/null
    pactl load-module module-null-sink sink_name=hk_b >/dev/null
    for i in $(seq 50); do pactl set-default-sink hk_a 2>/dev/null && [ "$(pactl get-default-sink)" = hk_a ] && break; sleep 0.1; done
}
test=$(find "$BUILD" -name hikari_backends_general_player_tests -type f -perm -u+x | head -1)
for backend in $BACKENDS; do
    for row in "system default" "picked device"; do
        passed=0
        for run in $(seq "$RUNS"); do
            sinks
            log=/work/$SERVER-$backend-${row// /-}-$run.log
            [ "$backend" = default ] && unset QT_AUDIO_BACKEND || export QT_AUDIO_BACKEND=$backend
            if timeout 90 "$test" "playbackFollowsTheRouteWhenItsDeviceGoesAway:$row" >"$log" 2>&1; then
                passed=$((passed + 1))
            else
                echo "--- $SERVER / $backend / $row / run $run failed (exit $?):"; grep -E "FAIL|Loc:|audio removal|QWARN|pw_" "$log" | head -20
            fi
        done
        echo "RESULT $SERVER / $backend / $row: $passed of $RUNS passed"
    done
done
'
