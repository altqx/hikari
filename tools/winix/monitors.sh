#!/usr/bin/env bash
# D1 native gate (mixed DPI, monitor removal): gives the Winix VM two
# monitors, a QXL primary and a second QXL device (Windows' QXL-WDDM-DOD
# driver from the virtio-win guest tools drives one monitor per device), or
# puts the single VGA head back. Approved by the user on 2026-10-05 for the
# Winix VM only. The definition before the first change is kept next to the
# VM's files; `one` restores it.
#   tools/winix/monitors.sh two|one|status [vm name]
set -euo pipefail
mode=${1:?two, one or status}
vm=${2:-dev}
domain=winix-$vm
backup=$HOME/.local/share/winix/vms/$vm/domain.before-monitors.xml

videos() { virsh dumpxml --inactive "$domain" | grep -A1 '<video>' | grep '<model' || true; }
case $mode in
status) videos; exit 0 ;;
two | one) ;;
*) echo "unknown mode $mode" >&2; exit 2 ;;
esac

[ -f "$backup" ] || virsh dumpxml --inactive "$domain" >"$backup"
new=$(mktemp --suffix=.xml)
trap 'rm -f "$new"' EXIT
if [ "$mode" = one ]; then
    cp "$backup" "$new"
else
    python3 - "$backup" "$new" <<'EOF'
import sys
import xml.etree.ElementTree as ET
ET.register_namespace('qemu', 'http://libvirt.org/schemas/domain/qemu/1.0')
tree = ET.parse(sys.argv[1])
devices = tree.getroot().find('devices')
for v in devices.findall('video'):
    devices.remove(v)
def qxl(primary):
    video = ET.SubElement(devices, 'video')
    model = ET.SubElement(video, 'model', {'type': 'qxl', 'ram': '65536', 'vram': '65536',
                                           'vgamem': '16384', 'heads': '1'})
    if primary:
        model.set('primary', 'yes')
        ET.SubElement(video, 'address', {'type': 'pci', 'domain': '0x0000', 'bus': '0x00',
                                         'slot': '0x01', 'function': '0x0'})
qxl(True)
qxl(False)
tree.write(sys.argv[2], encoding='unicode')
EOF
fi

cd "$(dirname "$0")/../.."
winix --config winix.yaml vm down "$vm"
virsh define "$new"
winix --config winix.yaml vm up "$vm"
winix --config winix.yaml vm ready "$vm"
if [ "$mode" = two ]; then
    # The guest tools leave QXL on the basic display driver, which brings up no
    # second monitor: install qxldod from the pinned virtio-win ISO (copied
    # into the synced tree for the task, then removed).
    drivers=tools/winix/drivers
    rm -rf "$drivers" && mkdir -p "$drivers"
    trap 'rm -f "$new"; rm -rf "$drivers"' EXIT
    bsdtar -xf "$HOME/.local/share/winix/media/virtio-win.iso" -C "$drivers" qxldod/w10/amd64
    winix --config winix.yaml sync >/dev/null
    winix --config winix.yaml run qxl-driver --wait >/dev/null || true
    rm -rf "$drivers"
fi
winix --config winix.yaml run monitors --wait >/dev/null || true
videos
