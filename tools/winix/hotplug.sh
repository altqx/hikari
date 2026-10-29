#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
dev=${1:-hikariusbaudio}
task=${2:-audio-hotplug}
w() { winix --config winix.yaml "$@"; }

# Winix's hot-plug record outlives a VM restart that drops the device, so ask QEMU.
plugged=$(w vm device list | python3 -c '
import json, sys
listing = json.load(sys.stdin)
print("yes" if any(p.get("name") == sys.argv[1] for p in listing.get("peripherals", [])) else "no")
' "$dev")
[[ $plugged == yes ]] || w vm device add usb-audio --id "$dev"
job=$(w --json run "$task" --label hotplug | python3 -c 'import json,sys; print(json.loads(sys.stdin.readline())["id"])')
echo "job $job"
w vm device remove "$dev" --job "$job" --marker HOTPLUG-READY
w vm device add usb-audio --id "$dev" --job "$job" --marker HOTPLUG-LOST --delay 3s
w job wait "$job"
