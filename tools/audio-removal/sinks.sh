#!/bin/bash
# sinks.sh media | pick | remove <device id> | heard <device id>, inside the container run.sh starts.
# pick prints the device to choose; remove prints the playing streams' names.
set -eu
case $1 in
media)
    echo "$AUDIO_REMOVAL_MEDIA"
    ;;
pick)
    pactl get-default-sink
    ;;
remove)
    module=$(pactl list short modules | awk -v name="sink_name=$2" '$2 == "module-null-sink" && index($0, name) { print $1 }')
    [ -n "$module" ] || { echo "no null sink for device id $2" >&2; pactl list short sinks >&2; exit 1; }
    pactl list sink-inputs | sed -n 's/^\s*media.name = //p' | tr '\n' ' '
    pactl unload-module "$module"
    ;;
heard)
    timeout 3 parec -d "$2.monitor" --raw --format=s16le --channels=1 --rate=8000 --latency-msec=20 2>/dev/null |
        head -c 8000 | od -An -v -td2 -w2 | awk '{ s += $1 * $1; n++ } END { exit !(n == 4000 && s / n > 10000) }'
    ;;
*)
    exit 2
    ;;
esac
