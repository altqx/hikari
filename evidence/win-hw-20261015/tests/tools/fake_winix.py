#!/usr/bin/env python3
"""A stand-in for the winix CLI's hot-plug commands (winix VmManager's
DeviceAdd, DeviceRemove and DeviceList): QEMU's peripherals and Winix's
records live in $FAKE_WINIX_STATE; each call is appended to $FAKE_WINIX_LOG."""
import json
import os
import sys

state_path = os.environ["FAKE_WINIX_STATE"]
with open(state_path) as f:
    state = json.load(f)
args = sys.argv[1:]
if args[:1] == ["--config"]:
    args = args[2:]
json_out = args[:1] == ["--json"]
if json_out:
    args = args[1:]


def log(line):
    with open(os.environ["FAKE_WINIX_LOG"], "a") as f:
        f.write(line + "\n")


def save():
    with open(state_path, "w") as f:
        json.dump(state, f)


def option(name):
    return args[args.index(name) + 1] if name in args else None


def plugged(dev):
    return any(p["name"] == dev for p in state["peripherals"])


if args[:3] == ["vm", "device", "list"]:
    print(json.dumps({"vm": "dev", "hotplugged": state["hotplugged"], "peripherals": state["peripherals"]}, indent=2))
elif args[:3] == ["vm", "device", "add"]:
    dev = option("--id")
    if option("--job"):
        log(f"add {dev} after {option('--marker')} of {option('--job')}")
        sys.exit(0)
    if plugged(dev):  # QMP device_add: "Duplicate device ID"
        print(f"qmp_error: Duplicate device ID '{dev}' for device", file=sys.stderr)
        sys.exit(1)
    state["peripherals"].append({"name": dev, "type": "child<usb-audio>"})
    state["hotplugged"] = [d for d in state["hotplugged"] if d["id"] != dev]
    state["hotplugged"].append({"id": dev, "driver": args[3], "properties": {}})
    save()
    log(f"add {dev}")
elif args[:3] == ["vm", "device", "remove"]:
    dev = args[3]
    if option("--job"):
        log(f"remove {dev} after {option('--marker')} of {option('--job')}")
        sys.exit(0)
    if not plugged(dev):  # QMP device_del fails before the record is touched
        print(f"qmp_error: Device '{dev}' not found", file=sys.stderr)
        sys.exit(1)
    state["peripherals"] = [p for p in state["peripherals"] if p["name"] != dev]
    state["hotplugged"] = [d for d in state["hotplugged"] if d["id"] != dev]
    save()
    log(f"remove {dev}")
elif args[:1] == ["run"] and json_out:
    devices = ",".join(p["name"] for p in state["peripherals"] if p["type"] == "child<usb-audio>")
    log(f"run {args[1]} with usb audio [{devices}]")
    print(json.dumps({"id": "job-fake"}))
elif args[:2] == ["job", "wait"]:
    log(f"wait {args[2]}")
else:
    print(f"fake winix: unexpected call {sys.argv[1:]}", file=sys.stderr)
    sys.exit(2)
