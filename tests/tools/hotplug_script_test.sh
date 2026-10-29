#!/usr/bin/env bash
# A VM restart drops hot-plugged devices but Winix keeps their records.
#   hotplug_script_test.sh SOURCE_DIR WORK_DIR
set -uo pipefail
source_dir=$1
work=$2
rm -rf "$work" && mkdir -p "$work/bin"
cp "$source_dir/tests/tools/fake_winix.py" "$work/bin/winix"
chmod +x "$work/bin/winix"
export PATH="$work/bin:$PATH" FAKE_WINIX_STATE="$work/state.json" FAKE_WINIX_LOG="$work/calls.log"

base='{"name": "sound0", "type": "child<ich9-intel-hda>"}, {"name": "usb", "type": "child<qemu-xhci>"}'
record='{"id": "hikariusbaudio", "driver": "usb-audio", "properties": {}, "addedAt": "2026-10-05T00:57:05+00:00"}'
replug='remove hikariusbaudio after HOTPLUG-READY of job-fake
add hikariusbaudio after HOTPLUG-LOST of job-fake
wait job-fake'
failures=0

check() { # name, state json, expected calls
    printf '%s\n' "$2" > "$FAKE_WINIX_STATE"
    : > "$FAKE_WINIX_LOG"
    local out status
    out=$(bash "$source_dir/tools/winix/hotplug.sh" 2>&1); status=$?
    local calls; calls=$(cat "$FAKE_WINIX_LOG")
    if [[ $status -ne 0 || $calls != "$3" ]]; then
        printf 'FAIL %s (exit %d)\n--- expected calls\n%s\n--- calls\n%s\n--- output\n%s\n' "$1" "$status" "$3" "$calls" "$out"
        failures=$((failures + 1))
    else
        printf 'PASS %s\n' "$1"
    fi
}

check stale-record "{\"hotplugged\": [$record], \"peripherals\": [$base]}" "add hikariusbaudio
run audio-hotplug with usb audio [hikariusbaudio]
$replug"
check no-record "{\"hotplugged\": [], \"peripherals\": [$base]}" "add hikariusbaudio
run audio-hotplug with usb audio [hikariusbaudio]
$replug"
check plugged-in "{\"hotplugged\": [$record], \"peripherals\": [$base, {\"name\": \"hikariusbaudio\", \"type\": \"child<usb-audio>\"}]}" "run audio-hotplug with usb audio [hikariusbaudio]
$replug"

echo "$((3 - failures)) of 3 passed"
exit $((failures > 0))
