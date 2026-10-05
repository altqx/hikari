#!/usr/bin/env bash
# N6 / M50-device with a real device that goes away: runs the audio-hotplug
# Winix task with a QEMU USB audio device on the VM's silent audiodev
# (vm: { audio: none }, so nothing is ever heard), unplugs it when the test
# prints HOTPLUG-READY and plugs it back after HOTPLUG-LOST. Approved by the
# user on 2026-10-04 for the Winix VM only.
#   tools/winix/hotplug.sh [usb device id] [task]
set -euo pipefail
cd "$(dirname "$0")/../.."
dev=${1:-hikariusbaudio}
task=${2:-audio-hotplug}
w() { winix --config winix.yaml "$@"; }

w vm device list | grep -q "\"$dev\"" || w vm device add usb-audio --id "$dev"
job=$(w --json run "$task" --label hotplug | python3 -c 'import json,sys; print(json.loads(sys.stdin.readline())["id"])')
echo "job $job"
w vm device remove "$dev" --job "$job" --marker HOTPLUG-READY
w vm device add usb-audio --id "$dev" --job "$job" --marker HOTPLUG-LOST --delay 3s
w job wait "$job"
