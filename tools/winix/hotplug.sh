#!/usr/bin/env bash
# N6 / M50-device with a real device that goes away: runs the audio-hotplug
# Winix task and, when the test prints HOTPLUG-READY, unplugs the QEMU USB
# audio device (backed by the VM's silent "none" audiodev, so nothing is ever
# heard); on HOTPLUG-LOST it plugs it back. Approved by the user on
# 2026-10-04 for the Winix VM only.
#   tools/winix/hotplug.sh [domain] [usb device id]
set -euo pipefail
domain=${1:-winix-dev}
dev=${2:-hikariusbaudio}
monitor() { virsh qemu-monitor-command "$domain" --hmp "$1"; }
plug() {
    monitor "info usb" | grep -q "ID: $dev" || monitor "device_add usb-audio,id=$dev,audiodev=audio1,bus=usb.0"
}
plug
job=$(winix --config winix.yaml --json run audio-hotplug | python3 -c 'import json,sys; print(json.load(sys.stdin)["id"])')
echo "job $job"
state=ready
while :; do
    logs=$(winix --config winix.yaml job logs "$job" 2>/dev/null || true)
    if [ "$state" = ready ] && grep -q "HOTPLUG-READY" <<<"$logs"; then
        echo "$(date +%T) unplugging $dev"; monitor "device_del $dev"; state=lost
    elif [ "$state" = lost ] && grep -q "HOTPLUG-LOST" <<<"$logs"; then
        sleep 3; echo "$(date +%T) plugging $dev back"; plug; state=back
    fi
    if winix --config winix.yaml job status "$job" | grep -q '"terminal": true'; then break; fi
    sleep 2
done
plug
winix --config winix.yaml job status "$job"
