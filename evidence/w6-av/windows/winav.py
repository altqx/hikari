#!/usr/bin/env python3
"""AV1-AV3 (#238-#240) native observation on the Winix VM desktop, with the
D1 gate's helpers (tools/native-gate/windows/gate_windows.py).
  WINIX_OWNER=... WINIX_WORKSPACE=hikari-qt winav.py STEP...   (av1 av2 av3)"""
import json, sys, time, subprocess
from pathlib import Path
TREE = Path("/home/altq/.t3/worktrees/hikari/w6-av")
sys.path.insert(0, str(TREE / "tools/native-gate/windows"))
import gate_windows as G

OUT = Path("/tmp/w6av/winav/evidence")
OUT.mkdir(parents=True, exist_ok=True)
G.EVID = OUT
G.LOG = open(OUT / "steps.log", "a")
G.W = G.Winix(TREE / "winix.yaml", "dev")
log, uia, snap, keys = G.log, G.uia, G.snap, G.keys
NOTES = []


def note(*a):
    line = " ".join(str(x) for x in a)
    NOTES.append(line)
    log("NOTE", line)
    (OUT / "notes.txt").write_text("\n".join(NOTES) + "\n")


def el(st, ctype, name):
    found = G.find(st, ctype, name)
    return found[0] if found else None


def hover(e, dy=0):
    G.pointer([(e["x"] + e["w"] // 2, e["y"] + e["h"] // 2 + dy)], button="none")


def click(e):
    # UI Automation's Invoke (Toggle for a check box) on the element by name
    t = e["t"]
    st = uia(do=[{"action": "invoke", "name": e["n"], "type": t}], settle_ms=400)
    acts = st.get("actions") or []
    if not acts or not acts[0].get("matches") or acts[0].get("errors"):
        log("invoke", e["n"], t, "->", json.dumps(acts)[:300], "; pointer instead")
        G.pointer([(e["x"] + e["w"] // 2, e["y"] + e["h"] // 2)])


def button_named(st, prefix):
    for e in st["elements"]:
        if e["t"] in ("Button", "CheckBox") and e["n"].startswith(prefix) and not e["off"]:
            return e
    return None


def texts(st, prefix):
    return [e["n"] for e in st["elements"] if e["n"].startswith(prefix)]


def status_text(st):
    # the status bar's first field (a StaticText whose name is its text)
    bar = next((e for e in st["elements"] if e["t"] == "StatusBar"), None)
    if not bar:
        return None
    kids = [e for e in st["elements"] if e["p"] == bar["i"] and e["t"] == "Text"]
    return [k["n"] for k in kids]


def devices(name_part=None):
    r = G.W.call("vm", "devices")
    return r


def open_drift():
    G.fresh(G.LAYOUT["docs"] + "\\drift.ass")
    G.dismiss_notices()
    G.focus_main()
    if not G.menu_to("Open video…", opener="alt+v"):
        note("open_drift: no Open video… item")
    keys("return", 3)
    G.type_text(G.LAYOUT["docs"] + "\\av-drift.mkv")
    keys("return", 10)
    # the resolution question, if any
    uia(do=[{"action": "invoke", "name": "No", "type": "Button", "all": True}], settle_ms=500)
    G.focus_main()
    st = G.wait_for(lambda s: button_named(s, "Output device") is not None, timeout=60, step=2)
    if not button_named(st, "Output device"):
        snap("open-drift-no-strip")
        note("open_drift: buttons", [e["n"] for e in st["elements"] if e["t"] in ("Button", "CheckBox")][:80],
             "panes", [p["name"] for p in st["panels"]])
    return st


def step_av1():
    st = open_drift()
    b = button_named(st, "Output device")
    if not b:
        note("AV1: no Output device button", [e["n"] for e in st["elements"] if e["t"] == "Button"][:60])
        return
    hover(b)
    time.sleep(1.5)
    snap("av1-w-1-button-tooltip")
    click(b)
    time.sleep(1.2)
    _, st = snap("av1-w-2-menu-before")
    note("AV1 menu before hotplug:", st["popupMenuItems"])
    keys("escape", 0.5)
    # a USB audio device plugged in (QEMU usb-audio on the silent backend)
    r = subprocess.run(["winix", "--json", "--vm", "dev", "vm", "device", "add", "usb-audio", "--id", "av1usb"],
                       capture_output=True, text=True)
    note("hotplug add:", r.stdout.strip()[-300:], r.stderr.strip()[-200:])
    time.sleep(12)
    st = uia()
    b = button_named(st, "Output device")
    click(b)
    time.sleep(1.2)
    _, st = snap("av1-w-3-menu-with-usb")
    items = st["popupMenuItems"]
    note("AV1 menu after hotplug:", items)
    usb = next((n for n in items if "Audio Device" in n or "USB" in n), None)
    if not usb:
        note("AV1: no USB entry")
        keys("escape")
        return
    e = el(st, "MenuItem", usb)
    click(e)
    time.sleep(2)
    st = uia()
    b = button_named(st, "Output device")
    hover(b)
    time.sleep(1.5)
    _, st = snap("av1-w-4-chosen-tooltip")
    note("AV1 chosen", usb)
    # Settings > Audio shows it
    click(b)
    time.sleep(1.2)
    st = uia()
    e = el(st, "MenuItem", "Audio settings…")
    click(e)
    time.sleep(3)
    _, st = snap("av1-w-5-settings-audio-page")
    note("AV1 settings combos:", json.dumps(st["combos"], ensure_ascii=False)[:600])
    uia(do=[{"action": "invoke", "name": "Cancel", "type": "Button", "all": True}], settle_ms=800)
    G.focus_main()
    # plays (the video with its audio): space in the video... use Ctrl+P? the
    # video's play button by its name
    st = uia()
    play = button_named(st, "Play")
    note("AV1 play button:", play and play["n"])
    if play:
        click(play)
    time.sleep(4)
    _, st = snap("av1-w-6-playing-on-usb")
    note("AV1 status while playing:", status_text(st))
    # unplugged while playing
    r = subprocess.run(["winix", "--json", "--vm", "dev", "vm", "device", "remove", "av1usb"],
                       capture_output=True, text=True)
    note("hotplug remove:", r.stdout.strip()[-300:], r.stderr.strip()[-200:])
    seen = None
    for _ in range(12):
        time.sleep(0.6)
        st = uia()
        s = status_text(st) or []
        if any("is not connected" in x for x in s):
            seen = s
            break
    _, st = snap("av1-w-7-unplugged-notice")
    note("AV1 notice:", seen, "app alive:", bool(G.main_window()))
    b = button_named(st, "Output device")
    if b:
        hover(b)
        time.sleep(1.5)
        snap("av1-w-8-fallback-tooltip")
        click(b)
        time.sleep(1.2)
        _, st = snap("av1-w-9-menu-not-connected")
        note("AV1 menu after unplug:", st["popupMenuItems"])
        keys("escape")
    time.sleep(6)
    _, st = snap("av1-w-10-notice-gone")
    note("AV1 status after 6 s:", status_text(st), "still playing button:", [e["n"] for e in st["elements"] if e["t"] == "Button" and e["n"] in ("Play", "Pause")])


def step_av2():
    st = open_drift()
    maximize()
    st = uia()
    t = button_named(st, "Sync with video")
    note("AV2 toggle:", t and (t["t"], t["n"], t["val"]))
    play = button_named(st, "Play")
    G.park_pointer()
    click(play)
    t0 = time.monotonic()
    for at in (3, 15, 30, 45):
        time.sleep(max(0, at - (time.monotonic() - t0)))
        G.W.screenshot(OUT / f"av2-w-video-plays-{at}.png")
        note(f"AV2 shot at {time.monotonic() - t0:.1f}s after Play")
    st = uia()
    pause = button_named(st, "Pause")
    if pause:
        click(pause)
    time.sleep(1)
    # Off: the toggle, then the video plays again from the start
    st = uia()
    click(button_named(st, "Sync with video"))
    time.sleep(1)
    st = uia()
    note("AV2 toggle after click:", button_named(st, "Sync with video")["val"])
    keys("ctrl+home", 1)
    click(button_named(st, "Play"))
    time.sleep(5)
    G.W.screenshot(OUT / "av2-w-off-video-plays.png")
    st = uia()
    pause = button_named(st, "Pause")
    if pause:
        click(pause)
    G.app_kill()
    G.app_start(G.LAYOUT["docs"] + "\\drift.ass")
    time.sleep(3)
    G.dismiss_notices()
    maximize()
    st = uia()
    t = button_named(st, "Sync with video")
    snap("av2-w-off-after-restart")
    note("AV2 toggle after restart:", t and (t["n"], t["val"]))


def step_av3():
    G.fresh(G.LAYOUT["episode"])
    G.dismiss_notices()
    G.focus_main()
    ok = G.menu_to("Open dummy video", opener="alt+v")
    keys("return", 2.5)
    _, st = snap("av3-w-1-dialog")
    uia(do=[{"action": "invoke", "name": "OK", "type": "Button", "all": True}], settle_ms=500)
    time.sleep(4)
    G.focus_main()
    _, st = snap("av3-w-2-dummy-with-silence")
    note("AV3 dummy opened:", ok)
    # the Audio menu has no blank-audio command
    G.park_pointer()
    keys("alt+a", 0.8)
    _, st = snap("av3-w-3-audio-menu")
    note("AV3 Audio menu:", st["popupMenuItems"])
    keys("escape")


def hot(op, *a):
    r = subprocess.run(["winix", "--json", "--vm", "dev", "vm", "device", op, *a], capture_output=True, text=True)
    note("hotplug", op, r.stdout.strip()[-200:], r.stderr.strip()[-200:])


def maximize():
    G.focus_main_compositor()
    keys("win+up", 1.5)


def step_av1b():
    st = open_drift()
    maximize()
    hot("add", "usb-audio", "--id", "av1usb2")
    time.sleep(12)
    st = uia()
    b = button_named(st, "Output device")
    note("AV1b button before:", b and b["n"])
    click(b)
    time.sleep(1.2)
    _, st = snap("av1-w-b1-menu-plugged-in")
    items = st["popupMenuItems"]
    note("AV1b menu:", items)
    usb = next((n for n in items if "Audio Device)" in n and "High Definition" not in n and "not connected" not in n), None)
    click(el(st, "MenuItem", usb))
    keys("escape", 1)
    st = uia()
    note("AV1b button after choosing:", button_named(st, "Output device")["n"])
    click(button_named(st, "Play"))
    time.sleep(2)
    G.W.screenshot(OUT / "av1-w-b2-playing-on-usb.png")
    hot("remove", "av1usb2")
    for i in range(6):
        time.sleep(0.7)
        G.W.screenshot(OUT / f"av1-w-b3-unplugged-{i}.png")
    st = uia()
    note("AV1b after unplug: button", button_named(st, "Output device")["n"], "status", status_text(st),
         "play/pause", [e["n"] for e in st["elements"] if e["t"] == "Button" and e["n"] in ("Play", "Pause")])
    hot("add", "usb-audio", "--id", "av1usb3")
    time.sleep(12)
    st = uia()
    note("AV1b replugged: button", button_named(st, "Output device")["n"])
    click(button_named(st, "Output device"))
    time.sleep(1.2)
    _, st = snap("av1-w-b4-menu-replugged")
    note("AV1b menu replugged:", st["popupMenuItems"])
    keys("escape")
    hot("remove", "av1usb3")


SYNC = "Sync the video and audio cursors"


def step_av1c():
    open_drift()
    maximize()
    G.view_to("Audio and subs")
    keys("return", 2)
    st = G.wait_for(lambda s: button_named(s, "Output device") is not None, timeout=20, step=2)
    hot("add", "usb-audio", "--id", "av1usb4")
    time.sleep(12)
    st = uia()
    b = button_named(st, "Output device")
    note("AV1c button (system default) after plugging in:", b and b["n"])
    click(b)
    time.sleep(1.2)
    _, st = snap("av1-w-c1-menu-plugged-in")
    items = st["popupMenuItems"]
    note("AV1c menu:", items)
    usb = next((n for n in items if "Audio Device)" in n and "High Definition" not in n and "not connected" not in n), None)
    click(el(st, "MenuItem", usb))
    keys("escape", 1)
    st = uia()
    note("AV1c chosen:", usb, "button:", button_named(st, "Output device")["n"])
    click(button_named(st, "Play"))
    time.sleep(2)
    G.W.screenshot(OUT / "av1-w-c2-playing-on-usb.png")
    hot("remove", "av1usb4")
    t0 = time.monotonic()
    for i in range(5):
        time.sleep(0.5)
        G.W.screenshot(OUT / f"av1-w-c3-unplugged-{i}.png")
        note(f"AV1c shot {i} at {time.monotonic() - t0:.1f}s after the unplug")
    st = uia()
    note("AV1c after unplug: button", button_named(st, "Output device")["n"], "status", status_text(st),
         "play/pause", [e["n"] for e in st["elements"] if e["t"] == "Button" and e["n"] in ("Play", "Pause")])
    click(button_named(st, "Output device"))
    time.sleep(1.2)
    _, st = snap("av1-w-c4-menu-not-connected")
    note("AV1c menu after unplug:", st["popupMenuItems"])
    keys("escape")


def step_av2off():
    st = open_drift()
    t = button_named(st, SYNC)
    note("AV2 toggle:", t and (t["t"], t["n"], t["val"]))
    click(t)
    time.sleep(1)
    st = uia()
    G.park_pointer()
    click(button_named(st, "Play"))
    time.sleep(6)
    G.W.screenshot(OUT / "av2-w-off-video-plays.png")
    st = uia()
    pause = button_named(st, "Pause")
    if pause:
        click(pause)
    G.app_kill()
    G.app_start(G.LAYOUT["docs"] + "\\drift.ass")
    time.sleep(3)
    G.dismiss_notices()
    st = uia()
    _, st = snap("av2-w-off-after-restart")
    t = button_named(st, SYNC)
    note("AV2 toggle after restart:", t and (t["n"], t["val"]))


def step_av3b():
    G.fresh(G.LAYOUT["episode"])
    G.dismiss_notices()
    G.focus_main()
    G.menu_to("Open dummy video", opener="alt+v")
    keys("return", 2.5)
    uia(do=[{"action": "invoke", "name": "OK", "type": "Button", "all": True}], settle_ms=500)
    time.sleep(4)
    uia(do=[{"action": "invoke", "name": "Do not change", "type": "Button", "all": True}], settle_ms=500)
    G.focus_main()
    _, st = snap("av3-w-b1-dummy-with-silence")
    G.park_pointer()
    keys("alt+a", 1)
    _, st = snap("av3-w-b2-audio-menu")
    note("AV3 Audio menu:", st["popupMenuItems"])
    keys("escape")


def shorten():
    G.focus_main_compositor()
    keys("alt+space", 0.8)
    keys("s", 0.5)
    keys("down", 0.3)
    keys(*(["up"] * 10), delay=0.15)
    keys("return", 1)


def step_av1d():
    open_drift()
    shorten()
    G.W.screenshot(OUT / "av1-w-d0-shortened.png")
    hot("add", "usb-audio", "--id", "av1usb5")
    time.sleep(12)
    st = uia()
    b = button_named(st, "Output device")
    note("AV1d button after plugging in:", b and b["n"])
    click(b)
    time.sleep(1.2)
    _, st = snap("av1-w-d1-menu-plugged-in")
    items = st["popupMenuItems"]
    note("AV1d menu:", items)
    usb = next((n for n in items if "Audio Device)" in n and "High Definition" not in n and "not connected" not in n), None)
    click(el(st, "MenuItem", usb))
    keys("escape", 1)
    st = uia()
    note("AV1d chosen:", usb, "button:", button_named(st, "Output device")["n"])
    click(button_named(st, "Play"))
    time.sleep(1)
    G.W.screenshot(OUT / "av1-w-d2-playing-on-usb.png")
    hot("remove", "av1usb5")
    t0 = time.monotonic()
    for i in range(5):
        time.sleep(0.4)
        G.W.screenshot(OUT / f"av1-w-d3-unplugged-{i}.png")
        note(f"AV1d shot {i} at {time.monotonic() - t0:.1f}s after the unplug")
    st = uia()
    note("AV1d after unplug: button", button_named(st, "Output device")["n"], "status", status_text(st))


STEPS = {"av1d": step_av1d, "av1c": step_av1c, "av2off": step_av2off, "av3b": step_av3b, "av1b": step_av1b, "av1": step_av1, "av2": step_av2, "av3": step_av3}
if __name__ == "__main__":
    G.prepare()
    G.calibrate_pointer()
    log("pointer map", G.POINTER_MAP)
    for f in ("/tmp/w6av/native/av-drift.mkv", "/tmp/w6av/winav/drift.ass"):
        r = G.W.call("push", f, G.LAYOUT["docs"] + "\\" + Path(f).name)
        log("push", f, json.dumps(r)[:200])
    try:
        for s in sys.argv[1:]:
            log("--- winav", s)
            try:
                STEPS[s]()
            except Exception as e:
                import traceback
                note(f"{s} broke:", repr(e), traceback.format_exc()[-800:])
    finally:
        G.release()
        G.app_kill()
        if G.PROFILE["original"]:
            _, lines = G.W.task("gate-win-restore", env={"GATE_PROFILE_FROM": G.PROFILE["original"]})
            log("profile:", " | ".join(l for l in lines if l.startswith(("restored", "set aside"))))
