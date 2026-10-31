#!/usr/bin/env python3
"""Windows native gate: HikariSub on the Winix VM desktop through `winix ui`.

WINIX_OWNER=<lease owner> gate_windows.py [--dpi] [--config winix.yaml] [--vm dev] [STEP...]

Guest tasks (gate.ps1, uia.ps1, display.ps1, nvda.ps1) do what winix does not; Qt ignores the APPDATA variables `--fresh-profile` sets.
"""
import argparse
import base64
import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EVID = Path(os.environ.get("GATE_EVIDENCE_WIN") or ROOT / "out" / "native-gate-evidence" / "windows")
PANELS = ["Video", "Audio", "Line editor", "Grid", "Reference", "Timing", "Search"]
MAIN = "HikariSub"
# Windows maps the absolute HID tablet to the primary monitor only.
POINTER_MAP = "primary"

W = None
LAYOUT = {}
APP = {"pid": None, "log": None}
PROFILE = {"original": None}
results = {}
LOG = None


def log(*a):
    line = " ".join(str(x) for x in a)
    print(line, flush=True)
    LOG.write(time.strftime("%H:%M:%S ") + line + "\n")
    LOG.flush()


def verdict(item, status, detail, evidence=()):
    results[item] = {"status": status, "detail": detail, "evidence": list(evidence),
                     "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
    (EVID / "results.json").write_text(json.dumps(results, indent=1, ensure_ascii=False))
    log(f"== {item}: {status} - {detail}")


class Winix:
    def __init__(self, config, vm):
        self.config = Path(config).resolve()
        self.vm = vm

    def lines(self, *args, timeout=900):
        cmd = ["winix", "--json", "--vm", self.vm, "--config", str(self.config), *args]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        out = []
        for line in p.stdout.splitlines():
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
        if not out:
            out.append({"error": "unparsed", "message": (p.stdout[-500:] + p.stderr[-500:]).strip()})
        return out

    def call(self, *args, timeout=900):
        return self.lines(*args, timeout=timeout)[-1]

    def ui(self, action, **args):
        return self.call("ui", action, "--args", json.dumps(args))

    def batch(self, steps):
        return self.call("ui", "batch", "--args", json.dumps(steps), "--continue-on-error")

    def screenshot(self, path, monitor=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        return self.call("ui", "screenshot", "--output", str(path), *(["--monitor", str(monitor)] if monitor is not None else []))

    def task(self, name, env=None, timeout=3600):
        args = ["run", name, "--wait"]
        for k, v in (env or {}).items():
            args += ["--env", f"{k}={v}"]
        job = next((j for j in reversed(self.lines(*args, timeout=timeout)) if j.get("type") == "job" or j.get("state")), {})
        if not job.get("id"):
            raise RuntimeError(f"task {name} did not start: {job}")
        # `job logs --all` pages the log: one JSON record per 64 KiB.
        text = "".join(r.get("text", "") for r in self.lines("job", "logs", job["id"], "--all") if isinstance(r, dict))
        lines = [l[6:] if l.startswith(("[out] ", "[err] ")) else l for l in text.splitlines()]
        return job, lines

    def pull(self, guest, host):
        return self.call("pull", guest, str(host))


def line_value(lines, prefix):
    return next((l[len(prefix):] for l in lines if l.startswith(prefix)), None)


def sections(lines):
    out, current = {}, None
    for line in lines:
        if line.startswith("== "):
            current = line[3:].strip()
            out[current] = []
        elif current:
            out[current].append(line)
    return out


def keys(*seq, delay=0.35):
    run = []

    def flush():
        if run:
            r = W.ui("keys", keys=" ".join(run), delayMs=int(delay * 1000))
            if isinstance(r, dict) and r.get("error"):
                log("keys", run, "->", r)
            time.sleep(delay)
            run.clear()
    for item in seq:
        if isinstance(item, (int, float)):
            flush()
            time.sleep(item)
        else:
            run.append(item.upper())
    flush()


def combo(*names):
    keys("+".join(names), delay=0)


def type_text(text):
    W.ui("text", text=text)


def pointer(path, **kw):
    r = W.ui("pointer", path=";".join(f"{x},{y}" for x, y in path), map=POINTER_MAP, **kw)
    if isinstance(r, dict) and r.get("error"):
        log("pointer", path, kw, "->", r)
    return r


def park_pointer():
    """Park the cursor: a menu that opens under it takes its current item from the hover."""
    m = next((x for x in monitors() if x["primary"]), None)
    if m:
        W.ui("pointer", path=f"{m['x'] + m['w'] - 30},{m['y'] + m['h'] - 20}", button="none", map="primary")


def release():
    return W.ui("pointer", release=True)


def double_click(x, y):
    """winix has no double-click at a point: two clicks must land within the 500 ms double-click time."""
    pointer([(x, y)])
    t1 = time.monotonic()
    pointer([(x, y)])
    return round(time.monotonic() - t1, 2)


def double_click_until(x, y, done, title=None):
    gaps = []
    for _ in range(3):
        gaps.append(double_click(x, y))
        time.sleep(1.5)
        st = wait_for(done, timeout=4)
        if done(st):
            return st, "tablet", gaps
        log("tablet double-click at", x, y, "had no effect (gap", gaps[-1], "s); again")
    if title:
        aid, name, window = title
        r = W.ui("doubleclick", pid=APP["pid"], automationId=aid, name=name, window=window)
        log("ui doubleclick", name, "->", r)
        time.sleep(1.5)
        st = wait_for(done, timeout=4)
        if done(st):
            return st, "ui doubleclick (SendInput)", gaps
    return uia(), None, gaps


def monitors():
    m = W.ui("monitors") or {}
    return [{"device": x["deviceName"], "primary": x["primary"], "x": x["bounds"]["x"], "y": x["bounds"]["y"],
             "w": x["bounds"]["width"], "h": x["bounds"]["height"], "dpi": x["dpi"]} for x in m.get("monitors") or []]


def displays(mode=None, scale2=None, second=None):
    env = {"GATE_DISPLAY": mode or "status"}
    if second:
        env["GATE_WIDTH2"], env["GATE_HEIGHT2"] = second
    if scale2:
        env["GATE_SCALE2"] = scale2
    _, lines = W.task("gate-win-display", env=env)
    raw = line_value(lines, "DISPLAY_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def frame_id(name):
    return MAIN if name == MAIN or name.endswith(" - " + MAIN) else name


def panel_id(name):
    if name.startswith("Editing:") or name == "No document open":
        return "Grid"
    return name.split(":")[0]


def el_path(els, e):
    parts = []
    while e is not None:
        parts.append(f"{e['t']}:'{e['n']}'")
        e = els[e["p"]] if e["p"] >= 0 else None
    return " > ".join(reversed(parts))


def ancestors(els, e):
    while e["p"] >= 0:
        e = els[e["p"]]
        yield e


def guest_uia(do=None, settle_ms=None):
    env = {}
    if do:
        env["GATE_UIA_DO"] = json.dumps(do)
    if settle_ms is not None:
        env["GATE_UIA_SETTLE_MS"] = settle_ms
    try:
        _, lines = W.task("gate-win-uia", env=env, timeout=600)
        raw = line_value(lines, "UIA_JSON_B64 ")
        return json.loads(base64.b64decode(raw).decode("utf-8")) if raw else {"error": lines[-10:]}
    except Exception as e:
        return {"error": repr(e)}


def flatten(node, parent, depth, win, els):
    if node.get("separateWindow"):
        return
    e = node["element"]
    b = e.get("bounds") or {}
    t = e.get("controlType") or ""
    n = e.get("name") or ""
    i = len(els)
    els.append({"i": i, "p": parent, "d": depth, "win": win, "hw": e.get("hwnd") or 0, "t": t, "n": n,
                "id": e.get("automationId") or "", "x": b.get("x", 0), "y": b.get("y", 0), "w": b.get("width", 0),
                "h": b.get("height", 0), "en": bool(e.get("enabled")), "val": e.get("value"),
                # winix reports no IsOffscreen: an element without area counts as off screen.
                "off": not b.get("width") or not b.get("height")})
    for c in node.get("children") or []:
        flatten(c, i, depth + 1, win, els)


def uia(do=None, settle_ms=None, dpi=False):
    pressed = guest_uia(do, settle_ms) if do or dpi else {}
    st = {"frames": [], "windows": [], "elements": [], "panels": [], "focus": None, "focusPath": None,
          "texts": {}, "labels": [], "popupMenuItems": [], "videoLabels": [], "combos": {},
          "actions": pressed.get("actions") or [], "windowInfo": pressed.get("windowDpi") or [],
          "cursor": None, "foreground": None}
    pid = APP["pid"]
    if not pid:
        st["error"] = "no app running"
        return st
    steps = [{"action": "windows", "pid": pid}, {"action": "state"}]
    steps += [{"action": "tree", "pid": pid, "window": i, "depth": 12, "limit": 1000} for i in range(6)]
    for attempt in range(3):
        r = W.batch(steps)
        res = r.get("steps") or []
        # winix: a window changing under the walk (a menu opening) can fail a step.
        if len(res) > 2 and res[1].get("ok") and res[2].get("ok"):
            break
        log("ui batch incomplete, again:", json.dumps([t.get("error") for t in res[:3]])[:300])
        time.sleep(0.5)
    if len(res) < 2 or not res[0].get("ok"):
        st["error"] = json.dumps(r)[:500]
        return st
    windows = res[0]["result"]
    state = res[1].get("result") or {}
    st["windows"] = windows
    st["cursor"] = state.get("cursor")
    st["foreground"] = state.get("foreground")
    fg = (state.get("foreground") or {}).get("hwnd")
    dpis = {w["hwnd"]: w["dpi"] for w in pressed.get("windowDpi") or []}
    els = []
    # Out-of-range window indexes fail by design; anything else is kept.
    st["errors"] = [t for t in res[1:] if not t.get("ok") and "out of range" not in (t.get("error") or "")]
    for t in res[2:]:
        if t.get("ok"):
            node = t["result"]
            flatten(node, -1, 0, node["element"].get("name") or "", els)
    st["elements"] = els
    st["frames"] = [{"name": w["title"], "type": "Window", "x": w["bounds"]["x"], "y": w["bounds"]["y"],
                     "w": w["bounds"]["width"], "h": w["bounds"]["height"], "hwnd": w["hwnd"], "dpi": dpis.get(w["hwnd"]),
                     "active": w["hwnd"] == fg, "id": frame_id(w["title"])}
                    for w in windows if w["title"] and w.get("visible", True) and not w.get("minimized")]
    for e in els:
        if e["off"]:
            continue
        if e["t"] == "Pane" and e["n"]:
            st["panels"].append({"name": e["n"], "id": panel_id(e["n"]), "frame": frame_id(e["win"]),
                                 "x": e["x"], "y": e["y"], "w": e["w"], "h": e["h"]})
        if e["t"] == "Edit" and e["n"] in ("Line text", "Start", "End"):
            st["texts"][e["n"]] = e["val"]
        if e["t"] == "Text" and e["n"] and (e["n"].startswith(("Editing", "No editing")) or e["n"] == "Video times"):
            st["labels"].append(e["n"])
        if e["t"] == "MenuItem" and not any(a["t"] in ("MenuBar", "TitleBar") for a in ancestors(els, e)):
            st["popupMenuItems"].append(e["n"])
        if e["t"] == "Text" and any(a["t"] == "Pane" and panel_id(a["n"]) == "Video" for a in ancestors(els, e)):
            st["videoLabels"].append((e["n"], e["val"]))
        if e["t"] == "ComboBox":
            kids = [c for c in els if c["p"] == e["i"]]
            # Qt Quick's ComboBox reports an empty Value; its text is the child Edit's.
            st["combos"][e["n"]] = e["val"] or next((c["val"] or c["n"] for c in kids if c["t"] in ("Text", "Edit")), None)
    focused = state.get("focused")
    if focused and focused.get("pid") == pid:
        chain = [a for a in reversed(state.get("ancestors") or []) if a.get("pid") == pid] + [focused]
        st["focusPath"] = " > ".join(f"{a['controlType']}:'{a.get('name') or ''}'" for a in chain)
        st["focus"] = f"[{focused['controlType']}] {focused.get('name') or ''!r} in {(state.get('foreground') or {}).get('title')!r}"
    else:
        st["focus"] = f"not in the app: {json.dumps(focused)[:200]}"
    return st


def frames(st):
    return {f["id"]: f for f in st["frames"]}


def active_frames(st):
    return [f["id"] for f in st["frames"] if f.get("active")]


def panel(st, pid):
    return next((p for p in st["panels"] if p["id"] == pid), None)


def panel_frame(st, pid):
    p = panel(st, pid)
    return p["frame"] if p else None


def find(st, ctype, name):
    els = st.get("elements") or []
    return [dict(e, path=el_path(els, e)) for e in els
            if e["t"] == ctype and e["n"] == name and not e["off"]]


def wait_for(pred, timeout=8.0, step=0.4):
    end = time.time() + timeout
    while True:
        st = uia()
        if pred(st) or time.time() > end:
            return st
        time.sleep(step)


def panel_of(path):
    found = None
    for part in (path or "").split(" > "):
        m = re.match(r"(\w+):'(.*)'$", part)
        if m and m.group(1) == "Pane" and panel_id(m.group(2)) in PANELS:
            found = panel_id(m.group(2))
    return found


def snap(name, extra=None, monitor=None):
    png = EVID / f"{name}.png"
    W.screenshot(png, monitor)
    st = uia()
    with open(EVID / f"{name}.txt", "w") as f:
        f.write("# windows of the app (winix ui windows --pid)\n" + json.dumps(st["windows"], indent=1, ensure_ascii=False)
                + "\n# UI Automation (winix ui batch: state, tree per window)\n" + json.dumps(st, indent=1, ensure_ascii=False) + "\n")
        if extra:
            f.write(extra + "\n")
    return [f"{name}.png", f"{name}.txt"], st


def prepare():
    job, lines = W.task("gate-win-prepare")
    raw = line_value(lines, "GATE_LAYOUT ")
    if job.get("state") != "succeeded" or not raw:
        raise SystemExit("gate-win-prepare failed:\n" + "\n".join(lines[-30:]))
    LAYOUT.update(json.loads(raw))
    log("layout", json.dumps({k: v for k, v in LAYOUT.items() if k != "path"}))
    if not LAYOUT.get("app"):
        raise SystemExit("no hikarisub.exe under out/build/windows-x64-release in the VM (run the build task)")


def main_window():
    if not APP["pid"]:
        return None
    listed = W.ui("windows", pid=APP["pid"])
    for w in listed if isinstance(listed, list) else []:
        if frame_id(w.get("title") or "") == MAIN:
            return w
    return None


def app_start(doc=None, env=None):
    r = W.ui("launch", exe=LAYOUT["app"], arguments=[doc] if doc else [], cwd=LAYOUT["docs"], noConsole=True,
             env={"PATH": LAYOUT["qt_bin"] + ";" + LAYOUT["path"], "QT_FORCE_STDERR_LOGGING": "1",
                  "HIKARISUB_USER_DIR": LAYOUT["profile"]["user"], **(env or {})})
    APP["pid"], APP["log"] = r.get("pid"), r.get("log")
    log("launched", r)
    deadline = time.time() + 90
    while time.time() < deadline:
        main = main_window()
        if main:
            time.sleep(2)
            focus_main_compositor()
            return main
        time.sleep(2)
    hidden = W.ui("windows", pid=APP["pid"], includeHidden=True)
    log("app_start: no main window after 90 s; its windows, hidden ones included:",
        json.dumps([(w.get("title"), w.get("visible")) for w in hidden] if isinstance(hidden, list) else hidden)[:600])
    return None


def app_kill():
    if APP["pid"]:
        r = W.ui("kill", pid=APP["pid"])
        if r.get("error") and "pid" not in r:
            log("kill", APP["pid"], "->", r)
        APP["pid"] = None


def app_status():
    _, lines = W.task("gate-win-status")
    s = sections(lines)
    applog = []
    if APP["log"]:
        local = EVID / ".winix" / "app.log"
        local.parent.mkdir(parents=True, exist_ok=True)
        W.pull(APP["log"], local)
        if local.exists():
            applog = local.read_text(errors="replace").splitlines()[-40:]
    return {"alive": "ALIVE yes" in lines, "layout_files": [l for l in s.get("layout files", []) if l.strip()],
            "app_log": applog}


def fresh(doc=None):
    app_kill()
    job, lines = W.task("gate-win-fresh")
    for l in lines:
        if l.startswith("set aside"):
            log(l)
            if not PROFILE["original"]:
                PROFILE["original"] = l.split(" -> ", 1)[1].rsplit("\\", 1)[0]
    if job.get("state") != "succeeded":
        raise RuntimeError("gate-win-fresh failed (the profile was not set aside): " + " | ".join(lines[-6:]))
    app_start(doc)
    return uia()


def dismiss_notices():
    st = uia(do=[{"action": "invoke", "name": "OK", "type": "Button", "all": True}], settle_ms=500)
    others = [f["id"] for f in st["frames"] if f["id"] != MAIN]
    if others:
        log("windows besides the main one after the notices:", others)


def set_focus(name):
    r = W.ui("setfocus", pid=APP["pid"], name=name)
    if r.get("error"):
        log("setfocus", name, "->", r)
    time.sleep(0.5)
    return r


def focus_main_compositor():
    main = main_window()
    if not main:
        return False
    W.ui("focus", pid=APP["pid"], window=f"hwnd:{main['hwnd']}")
    time.sleep(0.8)
    return True


def focus_main():
    for _ in range(6):
        st = uia()
        main = frames(st).get(MAIN)
        if main and main.get("active") and st["focusPath"] and panel_of(st["focusPath"]) \
                and not st["focusPath"].startswith("Window:'Line editor'") \
                and "> Window:'Line editor'" not in st["focusPath"]:
            return True
        combo("f6")
        time.sleep(0.8)
    focus_main_compositor()
    return False


def open_view():
    park_pointer()
    for _ in range(3):
        keys("alt+w", 0.6)
        st = uia()
        if any("Move panel" in (n or "") for n in st["popupMenuItems"]):
            return True
        keys("escape", 0.3)
    log("open_view: the View menu did not open")
    return False


SUBMENU = {"key": "right", "checked": False}


def menu_to(name, opener=None):
    """Down skips disabled items: count Downs among the enabled items UI Automation lists."""
    if opener is None:
        if not open_view():
            return False
    else:
        park_pointer()
        keys(opener, 0.6)
    st = uia()
    els = st.get("elements") or []
    items = [e for e in els if e["t"] == "MenuItem" and not e["off"]
             and not any(a["t"] in ("MenuBar", "TitleBar") for a in ancestors(els, e))]
    enabled = [e["n"] for e in items if e["en"]]
    if name not in enabled:
        log(f"menu_to: {name!r} is not an enabled item of {[(e['n'], e['en']) for e in items]}")
        return False
    keys(*(["down"] * (enabled.index(name) + 1)), delay=0.3)
    return True


def view_to(name):
    return menu_to(name)


def panel_menu(name, item):
    view_to("Panels")
    k = SUBMENU["key"]
    seq = [k, 0.3] + ["down"] * PANELS.index(name) + [k, 0.3]
    seq += ["down"] * ["Show", "Hide", "Float", "Dock"].index(item) + ["return"]
    keys(*seq)
    time.sleep(1.2)


def close_window(title):
    W.ui("close", pid=APP["pid"], window=title)
    time.sleep(0.5)


def float_panel(name, tag):
    panel_menu(name, "Float")
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if name in frames(st):
        if not SUBMENU["checked"]:
            SUBMENU["checked"] = True
            verdict("submenu-right-arrow", "observed",
                    f"Right on View > Panels opens its submenu and Right on {name} opens the panel's: "
                    f"View > Panels > {name} > Float with Right and Return floated it", [])
        return "menu" if SUBMENU["key"] == "right" else "menu-return"
    if not SUBMENU["checked"]:
        SUBMENU["checked"] = True
        ev, _ = snap(f"{tag}-submenu-right")
        keys("escape", "escape", "escape", "escape")
        focus_main_compositor()
        SUBMENU["key"] = "return"
        panel_menu(name, "Float")
        st = wait_for(lambda s: name in frames(s), timeout=4)
        if name in frames(st):
            verdict("submenu-right-arrow", "failed",
                    "Right on View > Panels (focused item) does not open its submenu, so View > Panels > "
                    f"{name} > Float with Right did nothing; Return opens the submenus and floats it. The keys "
                    "reach the menu (Down moves the focus); `ui keys` sends Right with its scan code and the "
                    "extended-key flag. The rest of the run opens submenus with Return", ev)
            return "menu-return"
        SUBMENU["key"] = "right"
    log(f"{tag}: View > Panels > {name} > Float did not float it; using View > Move panel…")
    keys("escape", "escape", "escape", "escape")
    focus_main_compositor()
    view_to("Move panel…")
    keys("return", 1.2, "home", *(["down"] * PANELS.index(name)), "tab", "home",
         *(["down"] * 5), "tab", "space", 1.5)
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if "Move panel" in frames(st):
        close_window("Move panel")
    return "move-panel" if name in frames(st) else None


def f6_walk(n=5, key="f6"):
    seq = []
    for _ in range(n):
        combo(*key.split("+"))
        time.sleep(0.8)
        st = uia()
        seq.append({"focus": st["focus"], "path": st["focusPath"], "active": active_frames(st)})
    return seq


def image_diff(a, b):
    r = subprocess.run(["magick", "compare", "-metric", "AE", "-fuzz", "2%", str(EVID / a), str(EVID / b), "null:"],
                       capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0]))
    except (ValueError, IndexError):
        return r.stderr.strip()


def step_default():
    fresh()
    ev, st = snap("default-arrangement")
    fr = frames(st)
    docked = {p["id"]: p["frame"] for p in st["panels"] if p["id"] in ("Video", "Audio", "Line editor", "Grid")}
    ok = len(fr) == 1 and MAIN in fr and len(docked) == 4 and all(v == MAIN for v in docked.values())
    pos = {p["id"]: (p["x"], p["y"]) for p in st["panels"]}
    shape = (ok and pos["Video"][0] < pos["Audio"][0] and pos["Audio"][1] < pos["Line editor"][1]
             and pos["Grid"][1] > pos["Video"][1])
    verdict("default-arrangement", "observed" if shape else "failed",
            f"one window ({list(fr)}); panels {docked}; Video left, Audio over Line editor, Grid below: {bool(shape)}", ev)


def step_keyboard_float_dock():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    set_focus("Line text")
    keys("end")
    type_text(" draft")
    time.sleep(0.8)
    keys("f6", 0.5)
    st = uia()
    before = st["texts"].get("Line text")
    labels_before = st["labels"]
    ev0, _ = snap("kbd-0-draft")
    how = float_panel("Line editor", "kbd")
    st = wait_for(lambda s: "Line editor" in frames(s))
    ev1, st = snap("kbd-1-floated")
    floated = "Line editor" in frames(st) and panel_frame(st, "Line editor") == "Line editor"
    text_float = st["texts"].get("Line text")
    verdict("keyboard-float", "observed" if how in ("menu", "menu-return") and floated else "failed",
            f"View > Panels > Line editor > Float from the keyboard: own window={floated} (floated by {how})", ev0 + ev1)
    if how == "move-panel":
        verdict("keyboard-float-move-panel", "observed" if floated else "failed",
                f"View > Move panel… Place: Float (fallback after the View > Panels path failed): own window {floated}", ev1)
    if not floated:
        verdict("keyboard-dock", "not-observable", "the Line editor could not be floated from the keyboard")
        return
    verdict("draft-kept-across-float",
            "observed" if text_float == before and (before or "").endswith(" draft") else "failed",
            f"Line text before {before!r}, floating {text_float!r}; status {labels_before} -> {st['labels']}", ev0 + ev1)
    focus_main()
    panel_menu("Line editor", "Dock")
    st = wait_for(lambda s: "Line editor" not in frames(s))
    if "Line editor" in frames(st):
        log("Dock at the 4th position did not dock; trying the 3rd (disabled items skipped)")
        keys("escape", "escape", "escape")
        focus_main()
        view_to("Panels")
        k = SUBMENU["key"]
        keys(k, 0.3, "down", "down", k, 0.3, "down", "down", "return")
        st = wait_for(lambda s: "Line editor" not in frames(s))
    ev2, st = snap("kbd-2-docked")
    docked = "Line editor" not in frames(st) and panel_frame(st, "Line editor") == MAIN
    verdict("keyboard-dock", "observed" if docked else "failed",
            f"View > Panels > Line editor > Dock from the keyboard: back in the main window={docked}", ev2)
    text_dock = st["texts"].get("Line text")
    verdict("draft-kept-across-dock", "observed" if docked and text_dock == before else "failed",
            f"Line text after docking {text_dock!r}; status {st['labels']}", ev2)


def step_f6_floating():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    how = float_panel("Line editor", "f6")
    ev0, st = snap("f6-0-after-float")
    after_float = {"active": active_frames(st), "focus": st["focusPath"]}
    walk_a = f6_walk(4)
    ev1, _ = snap("f6-1-after-f6-from-float")
    focus_main_compositor()
    st = uia()
    start_b = {"active": active_frames(st), "focus": st["focusPath"]}
    walk_b = f6_walk(5)
    back = f6_walk(3, "shift+f6")
    ev2, st = snap("f6-2-walk-from-main")
    (EVID / "f6-floating.json").write_text(json.dumps(
        {"floated_by": how, "after_float": after_float, "f6_after_float": walk_a, "main_focused": start_b,
         "f6_from_main": walk_b, "shift_f6": back}, indent=1, ensure_ascii=False))
    reached_a = [panel_of(w["path"]) for w in walk_a]
    reached_b = [panel_of(w["path"]) for w in walk_b]
    float_active_b = any("Line editor" in w["active"] for w in walk_b)
    verdict("focus-after-float", "observed" if after_float["focus"] else "failed",
            f"floated by {how}; then active windows {after_float['active']}, focused object {after_float['focus']}", ev0)
    verdict("f6-from-floating-panel", "observed" if any(x and x != "Line editor" for x in reached_a) else "failed",
            f"F6 x4 right after Float reached {reached_a} (active {[w['active'] for w in walk_a]})", ev1)
    verdict("f6-into-floating-panel", "observed" if "Line editor" in reached_b and float_active_b else "failed",
            f"main window brought to the foreground with `ui focus` (focus {start_b['focus']}); F6 x5 reached {reached_b} "
            f"(active {[w['active'] for w in walk_b]}); Shift+F6 x3 {[panel_of(w['path']) for w in back]}",
            ev2 + ["f6-floating.json"])
    for _ in range(6):
        if panel_of(uia()["focusPath"]) == "Line editor":
            break
        combo("f6")
        time.sleep(0.8)
    in_float = panel_of(uia()["focusPath"]) == "Line editor"
    combo("ctrl", "shift", "h")
    st = wait_for(lambda s: any(f["name"].startswith("History") for f in s["frames"]))
    ev3, st = snap("ctrl-shift-h-from-floating")
    hist = any(f["name"].startswith("History") for f in st["frames"])
    verdict("shortcut-from-floating-panel", ("observed" if hist else "failed") if in_float else "not-observable",
            f"focus in the floating Line editor: {in_float}; Ctrl+Shift+H opened History: {hist}", ev3)
    keys("escape")


def window_combos(st, frame):
    els = st.get("elements") or []
    return {e["n"]: e["val"] or next((c["val"] or c["n"] for c in els if c["p"] == e["i"] and c["t"] in ("Text", "Edit")), None)
            for e in els if e["t"] == "ComboBox" and not e["off"] and frame_id(e["win"]) == frame}


def step_move_panel():
    fresh()
    view_to("Move panel…")
    keys("return", 1.2)
    st = wait_for(lambda s: "Move panel" in frames(s))
    combos_open = window_combos(st, "Move panel")
    ev0, st = snap("move-panel-0-open", extra="# combo boxes\n" + json.dumps(combos_open, ensure_ascii=False))
    if "Move panel" not in frames(st):
        verdict("keyboard-move-panel", "failed", "View > Move panel… did not open the placement window", ev0)
        return
    verdict("move-panel-defaults", "observed" if combos_open and all(v for v in combos_open.values()) else "failed",
            f"placement window on open (Grid focused): {combos_open}", ev0)
    keys("up", "up", "tab", "down", "tab", *(["up"] * 6), *(["down"] * 3), 0.3)
    combos_set = window_combos(uia(), "Move panel")
    keys("tab", "space", 1.5)
    ev1, st = snap("move-panel-1-left-of-grid", extra="# combo boxes before Move\n" + json.dumps(combos_set, ensure_ascii=False))
    pos = {p["id"]: p for p in st["panels"]}
    a, g = pos.get("Audio"), pos.get("Grid")
    ah = next((t for t in find(st, "TitleBar", "Audio") if frame_id(t["win"]) == MAIN), None)
    gh = next((t for t in find(st, "TabItem", "Grid") if frame_id(t["win"]) == MAIN), None)
    ok = a and g and a["frame"] == g["frame"] == MAIN and a["x"] < g["x"] and ah and gh and abs(ah["y"] - gh["y"]) < 3
    verdict("keyboard-move-panel", "observed" if ok else "failed",
            f"Move panel ({combos_set}) -> Audio {a and (a['x'], a['y'], a['w'], a['h'])} header "
            f"{ah and (ah['x'], ah['y'], ah['w'], ah['h'])}, Grid {g and (g['x'], g['y'], g['w'], g['h'])} header "
            f"{gh and (gh['x'], gh['y'], gh['w'], gh['h'])}; focus {st['focusPath']}", ev0 + ev1)
    close_window("Move panel")


def title_point(st, pid, frame_name, dx=120):
    tb = next((t for t in st.get("elements") or [] if t["t"] == "TitleBar" and not t["off"]
               and panel_id(t["n"]) == pid and frame_id(t["win"]) == frame_name), None)
    if tb:
        return tb["x"] + dx, tb["y"] + tb["h"] // 2, "UIA TitleBar"
    p = next((q for q in st["panels"] if q["id"] == pid and q["frame"] == frame_name), None)
    btn = next((b for n in (f"Float {pid}", f"Dock {pid}") for b in find(st, "Button", n)
                if frame_id(b["win"]) == frame_name), None)
    if btn and p:
        return p["x"] + dx, btn["y"] + btn["h"] // 2, "title-bar button row"
    f = frames(st).get(frame_name)
    if f:
        return f["x"] + dx, f["y"] + 12, "window caption"
    return None


def title_ref(st, pid, frame_name):
    tb = next((t for t in st.get("elements") or [] if t["t"] == "TitleBar" and not t["off"]
               and panel_id(t["n"]) == pid and frame_id(t["win"]) == frame_name), None)
    f = frames(st).get(frame_name)
    return (tb["id"], tb["n"], f"hwnd:{f['hwnd']}") if tb and f else None


def calibrate_pointer():
    global POINTER_MAP
    mons = monitors()
    points = []
    for m in mons:
        for fx, fy in ((0.25, 0.25), (0.75, 0.75)):
            x, y = m["x"] + int(m["w"] * fx), m["y"] + int(m["h"] * fy)
            for mapping in ("virtual", "primary"):
                r = W.ui("calibrate", point=f"{x},{y}", map=mapping)
                off = r.get("offset") or {}
                points.append({"monitor": m["device"], "map": mapping, "target": [x, y], "cursor": r.get("actual"),
                               "off": abs(off.get("x", 9999)) + abs(off.get("y", 9999))})
    hits = {mp: [p for p in points if p["map"] == mp and p["off"] <= 4] for mp in ("virtual", "primary")}
    POINTER_MAP = max(hits, key=lambda mp: len(hits[mp]))
    reachable = {p["monitor"] for p in hits[POINTER_MAP]}
    unreachable = [m["device"] for m in mons if m["device"] not in reachable]
    (EVID / "pointer-calibration.json").write_text(json.dumps(
        {"monitors": mons, "points": points, "map": POINTER_MAP, "unreachable": unreachable}, indent=1))
    return mons, points, reachable, unreachable


def step_pointer():
    fresh()
    # The 1280x800 main window is larger than the 1024x768 primary, and the tablet reaches the primary only.
    focus_main_compositor()
    keys("win+up", 1.0)
    mons, points, reachable, unreachable = calibrate_pointer()
    used = [p for p in points if p["map"] == POINTER_MAP and p["monitor"] in reachable]
    off = [p["off"] for p in used]
    log("tablet calibration:", POINTER_MAP, off, "unreachable", unreachable)
    verdict("pointer-calibration", "observed" if off and max(off) <= 4 else "failed",
            f"`ui calibrate` with the tablet mapped over the {POINTER_MAP} area: the guest cursor landed {off} px "
            f"(|dx|+|dy|) from the targets on {sorted(reachable)}; not reachable with either mapping: {unreachable} "
            "(Windows maps the absolute HID pointer to the primary monitor) (pointer-calibration.json)",
            ["pointer-calibration.json"])
    st = uia()
    btn = next((b for b in find(st, "Button", "Audio options") if frame_id(b["win"]) == MAIN), None)
    if not btn:
        verdict("pointer-menu-undock", "not-observable", "no 'Audio options' button (the header's ⋯) found")
        return
    tx, ty = btn["x"] + btn["w"] // 2, btn["y"] + btn["h"] // 2
    pointer([(tx, ty)])
    st = wait_for(lambda s: "Undock" in s["popupMenuItems"], timeout=4)
    ev0, st = snap("ptr-1-menu-open")
    items = menu_items(st)
    undock = next((e for e in find(st, "MenuItem", "Undock")), None)
    if undock:
        pointer([(undock["x"] + undock["w"] // 2, undock["y"] + undock["h"] // 2)])
        time.sleep(1.2)
    st = wait_for(lambda s: "Audio" in frames(s))
    ev, st = snap("ptr-1-undocked")
    verdict("pointer-menu-undock", "observed" if undock and "Audio" in frames(st) else "failed",
            f"tablet click on the Audio header's ⋯ ('Audio options' at {tx},{ty}) opened {items}; a click on "
            f"Undock: own window={'Audio' in frames(st)}", ev0 + ev + ["pointer-calibration.json"])
    pt = title_point(st, "Video", MAIN)
    dfloat, redock, ev1, ev2, how1, how2, gaps = False, False, [], [], None, None, []
    if pt:
        vx, vy, _ = pt
        st, how1, g1 = double_click_until(vx, vy, lambda s: "Video" in frames(s), title_ref(st, "Video", MAIN))
        gaps.append(g1)
        ev1, st = snap("ptr-2-dblclick-float")
        dfloat = "Video" in frames(st)
    if dfloat:
        pt2 = title_point(st, "Video", "Video")
        if pt2:
            fx, fy, where = pt2
            log("floating Video title at", fx, fy, where)
            st, how2, g2 = double_click_until(fx, fy, lambda s: "Video" not in frames(s), title_ref(st, "Video", "Video"))
            gaps.append(g2)
            ev2, st = snap("ptr-3-dblclick-redock")
            redock = "Video" not in frames(st)
    verdict("pointer-dblclick-float-redock", "observed" if dfloat and redock else "failed",
            f"double-click on the docked Video title at {pt}: floated={dfloat} (by {how1}); double-click its "
            f"floating title: redocked={redock} (by {how2}); tablet attempts, seconds between the two clicks' "
            f"calls: {gaps}", ev1 + ev2)
    st = uia()
    src = title_point(st, "Audio", "Audio", dx=30)
    grid = panel(st, "Grid")
    if not src or not grid or grid["frame"] != MAIN:
        verdict("pointer-drag-dock", "not-observable", f"could not locate the floating Audio window ({src}) or the Grid")
        return
    sx, sy, where = src
    gx, gy = grid["x"] + grid["w"] // 2, grid["y"] + grid["h"] // 2 - 15
    try:
        pointer([(sx, sy)], hold=True)
        time.sleep(0.25)
        pointer([(sx, sy), (gx + 3, gy + 3), (gx, gy)], button="none", steps=30, delayMs=40)
        time.sleep(0.7)
        ev3, _ = snap("ptr-4-drag-over-grid")
    finally:
        release()
        time.sleep(1.5)
        pointer([(gx + 5, gy + 5)], button="none")
    ev4, st = snap("ptr-5-dropped")
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    verdict("pointer-drag-dock", "observed" if docked else "failed",
            f"tablet drag of floating Audio by its title ({where}) from {sx},{sy} to the Grid centre {gx},{gy} and "
            f"release: docked={docked}, windows {list(frames(st))} (drop highlight: ptr-4-drag-over-grid.png)", ev3 + ev4)
    pointer_tab_dblclick()
    pointer_floating_move_resize()


def menu_items(st):
    out = []
    for e in st.get("elements") or []:
        if e["t"] == "MenuItem" and not e["off"] and e["n"] in st["popupMenuItems"]:
            out.append(e["n"] + ("" if e["en"] else " (disabled)"))
    return out


def pointer_tab_dblclick():
    st = uia()
    tab = next((t for t in find(st, "TabItem", "Grid") if frame_id(t["win"]) == MAIN), None)
    if not tab:
        verdict("pointer-dblclick-tab", "not-observable", "no 'Grid' tab in the main window")
        return
    x, y = tab["x"] + 18, tab["y"] + tab["h"] // 2
    st, how1, g1 = double_click_until(x, y, lambda s: "Grid" in frames(s))
    ev1, st = snap("ptr-6-dblclick-tab-float")
    floated = "Grid" in frames(st)
    how2, g2, docked, ev2 = None, [], False, []
    if floated:
        tab = next((t for t in find(st, "TabItem", "Grid") if frame_id(t["win"]) == "Grid"), None)
        if tab:
            st, how2, g2 = double_click_until(tab["x"] + 18, tab["y"] + tab["h"] // 2, lambda s: "Grid" not in frames(s))
        ev2, st = snap("ptr-7-dblclick-tab-dock")
        docked = "Grid" not in frames(st) and panel_frame(st, "Grid") == MAIN
    verdict("pointer-dblclick-tab", "observed" if floated and docked else "failed",
            f"double-click on the Grid's tab at {x},{y}: floated={floated} (by {how1}); on its floating tab: "
            f"docked={docked} (by {how2}); tablet gaps {g1} {g2}", ev1 + ev2)


def shadow_check(png, f, mons):
    from PIL import Image
    ox = min(m["x"] for m in mons)
    oy = min(m["y"] for m in mons)
    img = Image.open(EVID / png).convert("RGB")
    x0, y0, x1, y1 = f["x"] - ox, f["y"] - oy, f["x"] - ox + f["w"] - 1, f["y"] - oy + f["h"] - 1

    def mean(points):
        px = [img.getpixel(p) for p in points if 0 <= p[0] < img.width and 0 <= p[1] < img.height]
        return round(sum(sum(c) / 3 for c in px) / len(px), 1) if px else None
    ys = range(y0 + 20, y1 - 20, 7)
    outside = mean([(x0 - 3, y) for y in ys] + [(x1 + 3, y) for y in ys])
    outer = mean([(x0, y) for y in ys] + [(x1, y) for y in ys])
    inner = mean([(x0 + 7, y) for y in ys] + [(x1 - 7, y) for y in ys])
    frame = mean([(x0 + 8, y) for y in ys] + [(x1 - 8, y) for y in ys])
    crop = png.replace(".png", "-shadow.png")
    img.crop((max(0, x0 - 24), max(0, y0 - 24), min(img.width, x1 + 25), min(img.height, y0 + 120))).save(EVID / crop)
    ok = (outside is not None and outer is not None and abs(outer - outside) <= 12 and outer > 8
          and inner is not None and inner <= outside)
    return ok, {"background": outside, "outermost ring": outer, "ring by the frame": inner, "frame line": frame}, crop


def pointer_floating_move_resize():
    placed = narrow_main()
    log("main window on the left:", placed)
    try:
        time.sleep(1.0)
        mons = monitors()
        focus_main_compositor()
        float_panel("Audio", "move")
        st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
        st = uia(dpi=True)
        fa = frames(st).get("Audio")
        if not fa:
            for item in ("floating-borderless", "floating-shadow", "floating-move", "floating-free-part-docks",
                         "floating-resize"):
                verdict(item, "not-observable", "Audio did not float")
            return
        styles = next((w for w in st["windowInfo"] if w["hwnd"] == fa["hwnd"]), {})
        style, ex = styles.get("style", 0), styles.get("exStyle", 0)
        caption, thick, tool = (style & 0x00C00000) == 0x00C00000, bool(style & 0x00040000), bool(ex & 0x80)
        system_bar = [e["path"] for e in st["elements"] if e["win"] == "Audio" and e["t"] == "TitleBar"
                      and panel_id(e["n"]) != "Audio"]
        ev0, st = snap("ptr-8-floating", extra="# window styles\n" + json.dumps(styles))
        verdict("floating-borderless", "observed" if not caption and not thick and tool and not system_bar else "failed",
                f"floating Audio window style {style:#x} (WS_CAPTION {caption}, WS_THICKFRAME {thick}), ex style "
                f"{ex:#x} (WS_EX_TOOLWINDOW {tool}); system title bars in UIA: {system_bar}", ev0)
        src = title_point(st, "Audio", "Audio", dx=30)
        main = frames(st).get(MAIN)
        prim = next(m for m in mons if m["primary"])
        left = (main["x"] + main["w"] if main else prim["x"] + 480) + 30
        sx, sy, where = src
        tx, ty = left + (sx - fa["x"]), prim["y"] + 40 + (sy - fa["y"])
        try:
            pointer([(sx, sy)], hold=True)
            time.sleep(0.25)
            pointer([(sx, sy), (tx + 3, ty + 3), (tx, ty)], button="none", steps=25, delayMs=40)
            time.sleep(0.5)
        finally:
            release()
            time.sleep(1.2)
        st = uia()
        fb = frames(st).get("Audio")
        want = (fa["x"] + tx - sx, fa["y"] + ty - sy)
        moved = bool(fb) and abs(fb["x"] - want[0]) <= 6 and abs(fb["y"] - want[1]) <= 6
        ev1, st = snap("ptr-9-floating-moved")
        if fb:
            ok, shades, crop = shadow_check(ev1[0], fb, mons)
            verdict("floating-shadow", "observed" if ok else "failed",
                    f"over the desktop, mean grey left and right of the frame {shades}: the background shows "
                    f"through the outer ring (not black) and the ring by the frame is darker ({crop})", ev1 + [crop])
        verdict("floating-move", "observed" if moved else "failed",
                f"tablet drag of the floating Audio header ({where}) from {sx},{sy} to {tx},{ty} over the desktop: "
                f"window {fa['x']},{fa['y']} -> {fb and (fb['x'], fb['y'])} (expected {want}), still floating "
                f"{bool(fb)}", ev1)
        if not fb:
            verdict("floating-resize", "not-observable", "the floating Audio window is gone after the move")
            return
        btn = next((b for b in find(st, "Button", "Audio options") if frame_id(b["win"]) == "Audio"), None)
        grid = panel(st, "Grid")
        if btn and grid and grid["frame"] == MAIN:
            sx, sy = btn["x"] - 24, btn["y"] + btn["h"] // 2
            mw = frames(st).get(MAIN) or {"x": grid["x"], "w": grid["w"]}
            vx0, vx1 = max(grid["x"], mw["x"]), min(grid["x"] + grid["w"], mw["x"] + mw["w"])
            gx, gy = (vx0 + vx1) // 2, grid["y"] + grid["h"] // 2 - 15
            try:
                pointer([(sx, sy)], hold=True)
                time.sleep(0.25)
                pointer([(sx, sy), (gx + 3, gy + 3), (gx, gy)], button="none", steps=30, delayMs=40)
                time.sleep(0.7)
                evh, _ = snap("ptr-9-free-part-over-grid")
            finally:
                release()
                time.sleep(1.5)
                pointer([(gx + 5, gy + 5)], button="none")
            ev, st = snap("ptr-9-free-part-docked")
            docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
            verdict("floating-free-part-docks", "observed" if docked else "failed",
                    f"tablet drag of the floating Audio header right of its title (just left of its ⋯) from "
                    f"{sx},{sy} to the Grid centre {gx},{gy} and release: docked={docked}, windows {list(frames(st))} "
                    f"(drop highlight: ptr-9-free-part-over-grid.png)", evh + ev)
            if docked:
                focus_main_compositor()
                float_panel("Audio", "move")
                st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
            fb = frames(st).get("Audio") or fb
        else:
            verdict("floating-free-part-docks", "not-observable",
                    f"the floating Audio header's ⋯ ({bool(btn)}) or the docked Grid ({grid}) is not in UIA")
        btn = next((b for b in find(st, "Button", "Audio options") if frame_id(b["win"]) == "Audio"), None)
        dock_item, ev2 = None, []
        if btn:
            pointer([(btn["x"] + btn["w"] // 2, btn["y"] + btn["h"] // 2)])
            st = wait_for(lambda s: "Dock" in s["popupMenuItems"], timeout=4)
            ev2, st = snap("ptr-9-floating-menu")
            dock_item = next(iter(find(st, "MenuItem", "Dock")), None)
            keys("escape", 0.5)
        below = bool(btn and dock_item) and btn["y"] + btn["h"] - 4 <= dock_item["y"] <= btn["y"] + btn["h"] + 80 \
            and abs(dock_item["x"] - btn["x"]) <= 320
        verdict("floating-menu-position", "observed" if below else "failed",
                f"tablet click on the floating Audio's ⋯ {btn and (btn['x'], btn['y'], btn['w'], btn['h'])}: its "
                f"menu's Dock item at {dock_item and (dock_item['x'], dock_item['y'])} (expected just below the "
                f"button); menu {menu_items(st) if btn else None}", ev2)
        shots, out = [], []
        top_x = max(fb["x"] + 30, min(fb["x"] + fb["w"] // 2, prim["x"] + prim["w"] - 20))
        for edge, (px, py), (dx, dy) in (("left", (fb["x"] + 4, fb["y"] + fb["h"] // 2), (-90, 0)),
                                         ("top", (top_x, fb["y"] + 4), (0, -30))):
            before = frames(uia()).get("Audio")
            try:
                pointer([(px, py)], hold=True)
                time.sleep(0.3)
                pointer([(px, py), (px + dx, py + dy)], button="none", steps=15, delayMs=40)
                time.sleep(0.4)
            finally:
                release()
                time.sleep(1.2)
            after = frames(uia()).get("Audio")
            out.append({"edge": edge, "pressed": [px, py], "by": [dx, dy],
                        "before": before and [before["x"], before["y"], before["w"], before["h"]],
                        "after": after and [after["x"], after["y"], after["w"], after["h"]]})
            ev, _ = snap(f"ptr-10-resized-{edge}")
            shots += ev
        grew = [abs((o["after"][2] - o["before"][2]) - abs(o["by"][0])) <= 6
                and abs((o["after"][3] - o["before"][3]) - abs(o["by"][1])) <= 6 if o["before"] and o["after"] else False
                for o in out]
        verdict("floating-resize", "observed" if all(grew) else "failed",
                f"tablet drags on the shadow edges of the floating Audio window: {out}", shots)
    finally:
        wide_main()


def step_video():
    if not LAYOUT.get("ep1_ass"):
        verdict("video-float-redock", "error", "no cfr.mkv media fixture in the VM's build tree (run the test task once)")
        return
    fresh(LAYOUT["ep1_ass"])
    dismiss_notices()
    uia(do=[{"action": "invoke", "name": "Load associated", "type": "Button", "all": True}])
    time.sleep(5)
    focus_main()
    shots = []

    def state(tag):
        ev, st = snap(f"video-{tag}")
        shots.append(ev[0])
        return ev, " | ".join(f"{n!r} {t!r}" for n, t in st["videoLabels"])

    def step_frames():
        uia(do=[{"action": "invoke", "name": "Next frame", "type": "Button", "all": True, "repeat": 3}], settle_ms=1500)

    ev0, l0 = state("0-docked")
    step_frames()
    ev1, l1 = state("1-docked-stepped")
    how = float_panel("Video", "video")
    time.sleep(2)
    ev2, l2 = state("2-floating")
    step_frames()
    ev3, l3 = state("3-floating-stepped")
    focus_main()
    panel_menu("Video", "Dock")
    st = wait_for(lambda s: "Video" not in frames(s), timeout=4)
    if "Video" in frames(st):
        keys("escape", "escape", "escape", "escape")
        focus_main_compositor()
        view_to("Panels")
        k = SUBMENU["key"]
        keys(k, 0.3, k, 0.3, "down", "down", "return")
        wait_for(lambda s: "Video" not in frames(s), timeout=4)
    time.sleep(2)
    ev4, l4 = state("4-redocked")
    step_frames()
    ev5, l5 = state("5-redocked-stepped")
    diffs = {"docked step": image_diff(shots[0], shots[1]), "floating step": image_diff(shots[2], shots[3]),
             "redocked step": image_diff(shots[4], shots[5])}
    applog = "\n".join(app_status()["app_log"])
    (EVID / "video-labels.txt").write_text(
        "\n".join(f"{k}: {v}" for k, v in [("0 docked", l0), ("1 docked stepped", l1), ("2 floating", l2),
                                            ("3 floating stepped", l3), ("4 redocked", l4), ("5 redocked stepped", l5)])
        + f"\nfloated by: {how}\npixel differences: {diffs}\n\n# app log\n{applog}\n")
    opened = "No video open" not in l0
    changed = all(isinstance(v, int) and v > 50 for v in diffs.values())
    verdict("video-float-redock", "observed" if opened and changed and how else "failed",
            f"video opened: {opened}; floated by {how}; screen pixels changed by 3 frame steps: {diffs}; "
            f"labels docked {l1!r} floating {l3!r} redocked {l5!r}",
            ev0 + ev1 + ev2 + ev3 + ev4 + ev5 + ["video-labels.txt"])


def step_persistence():
    fresh()
    float_panel("Audio", "persist")
    ev0, st = snap("persist-0-before-exit")
    focus_main_compositor()
    keys("alt+f4")
    time.sleep(2.5)
    status = app_status()
    gone = not status["alive"]
    if not gone:
        log("Alt+F4 did not end the app; ending it after the 5 s layout check")
        time.sleep(6)
        app_kill()
        status = app_status()
    cfg = "\n".join(status["layout_files"])
    app_start()
    st = wait_for(lambda s: "Audio" in frames(s))
    ev1, st = snap("persist-1-after-restart", extra="# layout files\n" + cfg)
    floating = "Audio" in frames(st)
    docked = {p["id"]: p["frame"] for p in st["panels"] if p["id"] in ("Video", "Line editor", "Grid")}
    in_main = len(docked) == 3 and all(v == MAIN for v in docked.values())
    ok = floating and in_main
    verdict("layout-persistence", "observed" if ok else "failed",
            f"main window closed with Alt+F4 (process ended: {gone}); layout files: {cfg.strip()!r}; "
            f"after restart Audio floating: {floating}; docked in the main window: {docked}", ev0 + ev1)


def fullscreen(mode, width=None):
    env = {"GATE_FULLSCREEN": mode}
    if width:
        env["GATE_LEFT_WIDTH"] = width
    _, lines = W.task("gate-win-fullscreen", env=env)
    raw = line_value(lines, "FULLSCREEN_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def narrow_main(hidden=("Video", "Line editor")):
    for name in hidden:
        panel_menu(name, "Hide")
    return fullscreen("left", width=480)


def wide_main(hidden=("Video", "Line editor")):
    fullscreen("off")
    for name in hidden:
        panel_menu(name, "Show")


def step_fullscreen():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    float_panel("Line editor", "fullscreen")
    # Windows has no fullscreen request for another app's window: a borderless window over the monitor counts.
    fs = fullscreen("on")
    try:
        time.sleep(1.5)
        ev0, st0 = snap("fullscreen-0-main-fullscreen", extra="# FULLSCREEN_RESULT\n" + json.dumps(fs))
        focus_main()
        walk = f6_walk(5)
        ev1, st = snap("fullscreen-1-after-f6")
    finally:
        restored = fullscreen("off")
    reached = [panel_of(w["path"]) for w in walk]
    float_active = any("Line editor" in w["active"] for w in walk)
    (EVID / "fullscreen-f6.json").write_text(json.dumps({"fullscreen": fs, "walk": walk, "restored": restored},
                                                        indent=1, ensure_ascii=False))
    main_reached = [panel_of(w["path"]) for w in walk if w["active"] == [MAIN] and panel_of(w["path"])]
    verdict("fullscreen-coexistence", "observed" if "Line editor" in reached and float_active and main_reached
            and not fs.get("caption", True) else "failed",
            f"main window borderless over the monitor ({fs}) with Line editor floating (windows "
            f"{list(frames(st0))}); F6 reached {reached}, main-window panels {main_reached}, "
            f"floating window active at some step: {float_active} (see screenshots: is the floating panel visible "
            f"above the fullscreen window?)", ev0 + ev1 + ["fullscreen-f6.json"])


def step_menu_from_text():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    set_focus("Line text")
    st = uia()
    before = st["texts"].get("Line text")
    keys("alt+v", 1.0)
    ev, st = snap("menu-from-text")
    after = st["texts"].get("Line text")
    menu_open = bool(st["popupMenuItems"])
    keys("escape")
    verdict("menu-mnemonic-from-text-field", "observed" if menu_open and after == before else "failed",
            f"Alt+V in Line text: a menu open={menu_open} ({st['popupMenuItems'][:6]}); text {before!r} -> {after!r}", ev)


def step_test_executables():
    app_kill()
    job, lines = W.task("gate-win-tests", timeout=3000)
    text = "\n".join(lines)
    (EVID / "test-executables.txt").write_text(f"# winix job {job.get('id')} ({job.get('state')})\n{text}\n")
    exits = re.findall(r"^### (\S+) .*\(exit (.+)\)$", text, re.M)
    ok = len(exits) == 6 and all(code == "0" for _, code in exits)
    summary = "; ".join(re.findall(r"Totals: [^,]+, [^,]+, [^,]+", text))
    skips = re.findall(r"SKIP\s*:.*", text)
    known = ("the compositor places windows", "a synthetic drag between windows needs the offscreen platform",
             "the window follows a declined system move only where the platform declines it",
             "X11 only (the .xvfb run)", "HIKARI_SURFACE_SHOT_DIR is not set",
             "the screen's scale changes through the offscreen platform",
             "screens come and go through the offscreen platform's configuration")
    unexpected = [k for k in skips if not any(reason in k for reason in known)]
    log("tests", exits, summary, skips)
    verdict("test-executables", "observed" if ok and not unexpected else "failed",
            f"exits {exits}; {summary}; skips: {skips}", ["test-executables.txt"])


def step_nvda():
    if not LAYOUT.get("nvda_exe"):
        job, lines = W.task("nvda-setup", timeout=1500)
        raw = line_value(lines, "NVDA_LAYOUT ")
        (EVID / "nvda-setup.txt").write_text("\n".join(lines) + "\n")
        if not raw:
            verdict("nvda", "error", "nvda-setup did not create the portable copy (nvda-setup.txt)", ["nvda-setup.txt"])
            return
        LAYOUT["nvda_exe"] = json.loads(raw)["exe"]
    fresh(LAYOUT["episode"])
    dismiss_notices()
    nvda_log = LAYOUT["nvda_dir"] + "\\nvda-" + time.strftime("%Y%m%d-%H%M%S") + ".log"
    W.ui("launch", exe=LAYOUT["nvda_exe"], arguments=[
        "--minimal", "--replace", "--disable-addons", "--log-level=12", f"--log-file={nvda_log}",
        f"--config-path={LAYOUT['nvda_config']}"], cwd=LAYOUT["nvda_dir"])
    time.sleep(10)
    header_how, ev, st = None, [], {"frames": []}
    try:
        focus_main_compositor()
        f6_walk(4)
        view_to("Panels")
        k = SUBMENU["key"]
        keys(k, 0.6, "down", 0.6, "down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, "return", 2.0)
        f6_walk(3)
        ev, st = snap("nvda-0-after-float")
        focus_main_compositor()
        header_how, _ = focus_header_control("TabItem", "Grid", "Grid")
        keys("right", 0.8, "space", 1.2, "down", 0.8)
        ev += snap("nvda-1-header-menu")[0]
        keys("escape", 0.8)
        time.sleep(2)
    finally:
        job, lines = W.task("nvda-output", env={"GATE_NVDA_LOG": nvda_log})
    s = sections(lines)
    speech = [l.strip() for l in s.get("speech", []) if l.strip()]
    (EVID / "nvda-speech.txt").write_text("\n".join(speech) + "\n")
    (EVID / "nvda-output.txt").write_text("\n".join(lines) + "\n")
    W.pull(LAYOUT["nvda_dir"] + "\\nvda-last.log", EVID / "nvda.log")
    got_log = (EVID / "nvda.log").exists()
    synth = s.get("synth", [])
    silent = any("silence" in l for l in synth)
    spoke = [p for p in ("Video", "Audio", "Line editor", "Grid", "Panels", "Float", "Grid options", "Move panel",
                         "Undock") if any(p in l for l in speech)]
    evidence = ["nvda-speech.txt", "nvda-output.txt"] + (["nvda.log"] if got_log else []) + ev
    header_spoken = all(any(p in l for l in speech) for p in ("Grid options", "Move panel"))
    verdict("nvda-header-menu", "observed" if header_spoken else "failed",
            f"NVDA spoke the ⋯ button's name and the menu: {header_spoken} ('Grid options', 'Move panel')",
            ["nvda-speech.txt"])
    verdict("nvda", "observed" if speech and silent else "failed",
            f"{len(speech)} speech lines; panel/menu names spoken: {spoke}; synthesizer lines {synth[:3]} "
            f"(silent: {silent}); floating windows after the menu {list(frames(st))}; the Grid tab reached by "
            f"{header_how}, then Right and Space on its ⋯ (nvda-speech.txt; full log nvda.log)",
            evidence)
    verdict("nvda-float-from-menu", "observed" if "Line editor" in frames(st) else "failed",
            f"with NVDA running, View > Panels > Line editor > Float from the keyboard: windows {list(frames(st))}", ev)


def inside(f, m):
    cx, cy = f["x"] + f["w"] // 2, f["y"] + f["h"] // 2
    return m["x"] <= cx < m["x"] + m["w"] and m["y"] <= cy < m["y"] + m["h"]


def head_shot(name, monitor):
    """One monitor's pixels; the winix worker is per-monitor DPI aware."""
    out = EVID / f"{name}.png"
    W.screenshot(out, monitor)
    return [out.name] if out.exists() else []


def step_outputs():
    d = displays("extend")
    mons = d.get("monitors") or []
    if len(mons) < 2:
        reason = f"only {len(mons)} monitor(s) on the guest desktop after extending ({d.get('steps')})"
        for item in ("mixed-dpi", "monitor-removal", "monitor-removal-show-focus"):
            verdict(item, "not-observable", reason)
        return
    fresh(LAYOUT["episode"])
    dismiss_notices()
    try:
        how = float_panel("Audio", "outputs")
        st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
        before = frames(st).get("Audio")
        # Windows offers 150 % only on a larger mode (about 1920x1200 and up).
        d2 = displays("extend", scale2=150, second=(1920, 1200))
        mons = d2.get("monitors") or mons
        prim = next(m for m in mons if m["primary"])
        sec = next(m for m in mons if not m["primary"])
        st = uia()
        moved_by = None
        src = title_point(st, "Audio", "Audio", dx=30)
        if src and before:
            sx, sy, _ = src
            tx, ty = sec["x"] + sec["w"] // 3, sec["y"] + 120
            cal = W.ui("calibrate", point=f"{tx},{ty}", map="virtual")
            off = cal.get("offset") or {}
            if abs(off.get("x", 9999)) + abs(off.get("y", 9999)) <= 4:
                try:
                    W.ui("pointer", path=f"{sx},{sy}", map="virtual", hold=True)
                    time.sleep(0.25)
                    W.ui("pointer", path=f"{sx},{sy};{tx},{ty}", map="virtual", button="none", steps=30, delayMs=40)
                    time.sleep(0.5)
                finally:
                    release()
                    time.sleep(1.5)
                moved_by = f"tablet drag of its title bar from {sx},{sy} to {tx},{ty}"
            else:
                # The tablet reaches the primary only, and a borderless tool window takes no Win+Shift+Right.
                placed = fullscreen("left", width=480)
                time.sleep(1)
                st = uia()
                src = title_point(st, "Audio", "Audio", dx=20)
                ex, ey = prim["x"] + prim["w"] - 3, prim["y"] + 120
                if src:
                    sx, sy, _ = src
                    try:
                        pointer([(sx, sy)], hold=True)
                        time.sleep(0.25)
                        pointer([(sx, sy), (ex - 3, ey + 3), (ex, ey)], button="none", steps=25, delayMs=40)
                        time.sleep(0.5)
                    finally:
                        release()
                        time.sleep(1.5)
                moved_by = (f"a tablet drag of its title from {src and src[:2]} to the primary's right edge {ex},{ey}, "
                            f"the main window on the left ({placed}); the tablet does not reach the second monitor: "
                            f"`ui calibrate` {tx},{ty} landed at {cal.get('actual')}")
        st = wait_for(lambda s: "Audio" in frames(s) and inside(frames(s)["Audio"], sec), timeout=6)
        st = uia(dpi=True)
        fa = frames(st).get("Audio")
        ev0, st = snap("outputs-0-on-scale2", extra="# DISPLAY_RESULT\n" + json.dumps(d2))
        ev0 += head_shot("outputs-0-monitor2", 1) + head_shot("outputs-0-monitor1", 0)
        on2 = bool(fa) and inside(fa, sec)
        ratio = round(fa["w"] / before["w"], 2) if fa and before and before["w"] else None
        verdict("mixed-dpi", "observed" if on2 and sec.get("dpi") == 144 and fa.get("dpi") == 144 and prim.get("dpi") == 96
                else "failed",
                f"floating Audio (floated by {how}) moved by {moved_by} onto {sec['device']} at {sec['dpi']} dpi "
                f"(primary {prim['device']} at {prim['dpi']}): on it={on2}, window {fa and (fa['x'], fa['y'], fa['w'], fa['h'])}, "
                f"GetDpiForWindow {fa and fa.get('dpi')} (96 would mean bitmap-scaled by Windows), width x{ratio} of "
                f"its 100 % width {before and before['w']}; second monitor {sec['w']}x{sec['h']}, scale call {d2.get('scale2')} "
                "(outputs-0-monitor2.png: that monitor's pixels)", ev0)
        d3 = displays("detach")
        left = d3.get("monitors") or []
        st = wait_for(lambda s: "Audio" in frames(s) and len(left) == 1 and inside(frames(s)["Audio"], left[0]), timeout=8)
        ev1, st = snap("outputs-1-after-removal", extra="# DISPLAY_RESULT\n" + json.dumps(d3))
        st = uia(dpi=True)
        fa = frames(st).get("Audio")
        back = len(left) == 1 and bool(fa) and inside(fa, left[0])
        verdict("monitor-removal", "observed" if back else "failed",
                f"second monitor removed ({d3.get('steps')}; monitors now {[(m['device'], m['x'], m['y'], m['w'], m['h']) for m in left]}): "
                f"floating Audio present={bool(fa)}, window {fa and (fa['x'], fa['y'], fa['w'], fa['h'])} dpi {fa and fa.get('dpi')}, "
                f"on the remaining monitor={back}", ev1)
        focus_main_compositor()
        panel_menu("Audio", "Show")
        time.sleep(1)
        ev2, st = snap("outputs-2-show-after-removal")
        focused_in = panel_of(st["focusPath"])
        verdict("monitor-removal-show-focus", "observed" if focused_in == "Audio" else "failed",
                f"then View > Panels > Audio > Show gives the focus to {focused_in!r} (active windows "
                f"{active_frames(st)}; focus {st['focusPath']})", ev2)
    finally:
        fullscreen("off")
        r = displays("extend", scale2=100)
        ok = len(r.get("monitors") or []) == 2 and all(m["dpi"] == 96 and m["w"] == 1024 and m["h"] == 768
                                                      for m in r["monitors"])
        (EVID / "outputs-restored.txt").write_text(json.dumps(r, indent=1))
        verdict("outputs-restored", "observed" if ok else "failed",
                f"both monitors back at 1024x768 (vm.displays), 100 %, extended: {[(m['device'], m['x'], m['w'], m['h'], m['dpi']) for m in r.get('monitors') or []]}",
                ["outputs-restored.txt"])


def dpi_set(percent):
    _, lines = W.task("gate-win-dpi", env={"GATE_DPI_PERCENT": percent})
    raw = line_value(lines, "DPI_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def step_dpi():
    """Windows offers 150 % only from about 1920x1200 and stops a 1024x768 monitor at 125 %."""
    fresh(LAYOUT["episode"])
    dismiss_notices()
    how = float_panel("Line editor", "dpi")
    ev0, st0 = snap("dpi-0-at-100")
    res = dpi_set(150)
    try:
        after = res.get("after")
        if not isinstance(after, int) or after <= 100:
            verdict("dpi-scale-change", "not-observable",
                    f"the guest did not change its scale without signing out: {res}", ev0)
            return
        time.sleep(3)
        ev1, st = snap(f"dpi-1-at-{after}", extra="# DPI_RESULT\n" + json.dumps(res))
        kept, how_kept = floating_kept(st0, st, "Line editor", after / 100)
        verdict("dpi-floating-keeps-size", "observed" if kept else "failed",
                f"floating Line editor after 100 -> {after} %: {how_kept} (dpi-1-at-{after}.png)", ev0 + ev1)
        set_focus("Line text")
        keys("end")
        type_text(" hidpi")
        time.sleep(0.8)
        txt = uia()["texts"].get("Line text")
        both = MAIN in frames(st) and "Line editor" in frames(st)
        verdict("dpi-scale-change", "observed" if both and txt and txt.endswith(" hidpi") else "failed",
                f"scale 100 -> {res.get('after')} % live ({res}); floating Line editor (floated by {how}) and main "
                f"window still shown: {both}; typing there gives {txt!r} (dpi-1-at-{after}*.png)"
                + ("" if after == 150 else f"; 150 % asked, the guest took {after} % (the most it offers at this "
                                           "resolution)"),
                ev0 + ev1)
    finally:
        back = dpi_set(100)
        time.sleep(3)
        ev2, st2 = snap("dpi-2-restored", extra="# DPI_RESULT\n" + json.dumps(back))
        verdict("dpi-restored", "observed" if back.get("after") == 100 else "failed",
                f"scale restored: {back}", ev2)
        if back.get("after") == 100 and isinstance(res.get("after"), int) and res["after"] > 100:
            kept, how_kept = floating_kept(st0, st2, "Line editor", 1.0)
            verdict("dpi-floating-keeps-size-back", "observed" if kept else "failed",
                    f"floating Line editor after {res['after']} -> 100 %: {how_kept} (dpi-2-restored.png)", ev0 + ev2)


def floating_kept(st0, st1, name, ratio):
    """Sizes, not places: after a live scale change Qt's UI Automation places some elements by the other monitor's origin."""
    f0, f1 = frames(st0).get(name), frames(st1).get(name)
    if not f0 or not f1:
        return False, f"window {name!r} at 100 %: {bool(f0)}, after: {bool(f1)}"
    want = (round(f0["w"] * ratio), round(f0["h"] * ratio))
    sized = abs(f1["w"] - want[0]) <= 3 and abs(f1["h"] - want[1]) <= 3
    els = [e for e in st1.get("elements") or [] if e["win"] == name and not e["off"]]
    panes = [e for e in els if e["t"] == "Pane" and panel_id(e["n"]) == panel_id(name)]
    pane = panes[0] if panes else None
    bars = [e for e in els if e["t"] == "TitleBar" and e["n"] == name]
    bar = bars[0] if bars else None
    high = (pane["h"] if pane else 0) + (bar["h"] if bar else 0)
    wide = max(pane["w"] if pane else 0, bar["w"] if bar else 0)
    whole = bool(pane) and wide <= f1["w"] + 1 and high <= f1["h"] + 1
    placed = bool(pane) and (pane["x"] >= f1["x"] and pane["y"] >= f1["y"]
                             and pane["x"] + pane["w"] <= f1["x"] + f1["w"] + 1
                             and pane["y"] + pane["h"] <= f1["y"] + f1["h"] + 1)
    return sized and whole, (f"window {f0['w']}x{f0['h']} -> {f1['w']}x{f1['h']} (kept would be {want[0]}x{want[1]}): "
                             f"{sized}; header and panel {wide}x{high} within the window's {f1['w']}x{f1['h']}: "
                             f"{whole}; UI Automation places the panel at "
                             f"{pane and (pane['x'], pane['y'], pane['w'], pane['h'])}, the window at "
                             f"{(f1['x'], f1['y'], f1['w'], f1['h'])} (inside by place: {placed})")


def title_bar_shade(png, f, mons):
    from PIL import Image
    ox = min(m["x"] for m in mons)
    oy = min(m["y"] for m in mons)
    img = Image.open(EVID / png).convert("RGB")
    x0, y0 = max(f["x"], 0) - ox, max(f["y"], 0) - oy
    x1 = min(f["x"] + f["w"], img.width + ox) - ox
    px = [img.getpixel((x, y)) for x in range(x0 + 40, max(x0 + 41, x1 - 160), 9) for y in range(y0 + 2, y0 + 20, 3)
          if 0 <= x < img.width and 0 <= y < img.height]
    return round(sum(sum(c) / 3 for c in px) / len(px), 1) if px else None


def system_theme(light):
    _, lines = W.task("gate-win-system-theme", env={"GATE_APPS_LIGHT": light})
    return line_value(lines, "APPS_LIGHT="), line_value(lines, "APPS_LIGHT_NOW=")


def step_dark():
    original, _ = system_theme("status")
    mons = monitors()
    WS_CAPTION = 0x00C00000

    def check(tag, item, dark, what):
        time.sleep(2)
        ev, _ = snap(f"titlebar-{tag}")
        st = uia(dpi=True)
        framed = [w for w in st.get("windowInfo") or [] if (w.get("style") or 0) & WS_CAPTION == WS_CAPTION]
        flags = {w.get("name"): w.get("darkTitleBar") for w in framed}
        main = frames(st).get(MAIN)
        shade = title_bar_shade(ev[0], main, mons) if main else None
        on_screen = shade is not None and (shade < 90 if dark else shade > 170)
        ok = bool(framed) and all(v == (1 if dark else 0) for v in flags.values()) and on_screen
        verdict(item, "observed" if ok else "failed",
                f"{what}: title bars {'dark' if dark else 'light'} expected; DWMWA_USE_IMMERSIVE_DARK_MODE per framed "
                f"window {flags}; the main window's title bar on screen grey {shade} ({ev[0]})", ev)

    def start(theme, follow):
        app_kill()
        job, lines = W.task("gate-win-fresh")
        for l in lines:
            if l.startswith("set aside"):
                log(l)
                if not PROFILE["original"]:
                    PROFILE["original"] = l.split(" -> ", 1)[1].rsplit("\\", 1)[0]
        if job.get("state") != "succeeded":
            raise RuntimeError("gate-win-fresh failed (the profile was not set aside): " + " | ".join(lines[-6:]))
        _, lines = W.task("gate-win-theme", env={"GATE_THEME": theme, "GATE_FOLLOW": follow})
        log("theme:", line_value(lines, "THEME_RESULT "))
        app_start(LAYOUT["episode"])
        dismiss_notices()

    try:
        log("system apps light before:", original, "now:", system_theme("1"))
        start("dark", "false")
        check("0-dark", "titlebar-dark-chosen", True, "Dark chosen (follow the system off), the system light")
        start("light", "false")
        check("1-light", "titlebar-light-chosen", False, "Light chosen, the system light")
        start("dark", "true")
        check("2-follow-light", "titlebar-follow-system", False, "following the system, the system light")
        log("system dark:", system_theme("0"))
        check("3-follow-dark-live", "titlebar-follow-system-live", True,
              "following the system, the system switched to dark while HikariSub runs")
        log("system light:", system_theme("1"))
        check("4-follow-light-live", "titlebar-follow-system-live-back", False,
              "following the system, the system switched back to light")
    finally:
        app_kill()
        if original in ("0", "1"):
            log("system theme restored:", system_theme(original))


def step_a11y():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    st = uia()
    els = st.get("elements") or []
    tree = "\n".join("  " * e["d"] + f"[{e['t']}] {e['n']!r} id={e['id']!r} @{e['x']},{e['y']} {e['w']}x{e['h']}"
                     + (" off" if e["off"] else "") + ("" if e["en"] else " disabled")
                     + (f" val={e['val']!r}" if e["val"] is not None else "") + (f" hwnd={e['hw']}" if e["hw"] else "")
                     + f"  <{e['win']}>" for e in els)
    (EVID / "a11y-tree.txt").write_text(tree + "\n")
    table = find(st, "Table", "Subtitle lines") or find(st, "DataGrid", "Subtitle lines")
    grid_panel = [p for p in st["panels"] if p["id"] == "Grid"]
    in_grid = bool(table) and panel_of(table[0]["path"]) == "Grid"
    verdict("grid-accessible", "observed" if in_grid and grid_panel else "failed",
            f"table 'Subtitle lines': {[t['path'] for t in table[:1]]}; Grid panel: {grid_panel[:1]}", ["a11y-tree.txt"])
    headers = {n: [e["path"] for e in find(st, "TitleBar", n)] for n in ("Video", "Audio", "Line editor")}
    grid_tab = [e["path"] for e in find(st, "TabItem", "Grid")]
    buttons = {n: bool(find(st, "Button", f"{n} options")) for n in ("Video", "Audio", "Line editor", "Grid")}
    old = [n for n in ("Float Audio", "Close Audio", "Float Grid", "Close Grid") if find(st, "Button", n)]
    repeats = [el_path(els, e) for e in els if e["t"] == "Text" and e["n"] in ("Video", "Audio", "Line editor", "Grid")
               and any(a["t"] in ("TitleBar", "Tab", "TabItem") for a in ancestors(els, e))]
    (EVID / "a11y-headers.txt").write_text(json.dumps({"title bars": headers, "grid tab": grid_tab, "menu buttons": buttons,
                                                       "old float/close buttons": old, "repeated titles": repeats},
                                                      indent=1, ensure_ascii=False))
    verdict("headers-accessible",
            "observed" if all(headers.values()) and grid_tab and all(buttons.values()) and not old and not repeats
            else "failed",
            f"title bars { {n: bool(v) for n, v in headers.items()} }; Grid tab {grid_tab[:1]}; ⋯ buttons {buttons}; "
            f"float/close buttons left over: {old}; titles repeated as text: {repeats}", ["a11y-headers.txt", "a11y-tree.txt"])
    st = uia(do=[{"action": "invoke", "name": "Audio options", "type": "Button", "after": 1000}])
    by_invoke = bool(st["actions"]) and st["actions"][0].get("done") == ["Invoke"]
    items = menu_items(st)
    ev0, _ = snap("a11y-0-audio-menu")
    st = uia(do=[{"action": "invoke", "name": "Undock", "type": "MenuItem"}])
    undock = st["actions"]
    st = wait_for(lambda s: "Audio" in frames(s))
    floated = "Audio" in frames(st)
    ev1, st = snap("a11y-1-undocked-by-uia")
    st = uia(do=[{"action": "invoke", "name": "Audio options", "type": "Button", "window": "Audio", "after": 1000}])
    items_floating = menu_items(st)
    uia(do=[{"action": "invoke", "name": "Dock", "type": "MenuItem"}])
    st = wait_for(lambda s: "Audio" not in frames(s))
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    want = ["Move panel…", "Undock", "Close"]
    verdict("panel-menu-accessible",
            "observed" if by_invoke and items == want and floated and "Dock" in items_floating and docked else "failed",
            f"'Audio options' pressed through Invoke: {by_invoke}; its menu {items} (expected {want}); 'Undock' "
            f"invoked ({undock}) -> own window={floated}; the floating panel's menu {items_floating}; 'Dock' "
            f"invoked -> docked={docked}", ev0 + ev1)
    focus_main()
    panel_menu("Timing", "Show")
    time.sleep(1)
    st = uia()
    tabs = {n: find(st, "TabItem", n) for n in ("Line editor", "Shift times")}
    lists = [el_path(st["elements"], e) for e in st["elements"] if e["t"] == "Tab" and e["n"] == "Panels" and not e["off"]]
    tab_button = bool(find(st, "Button", "Shift times options"))
    ev2, st = snap("a11y-2-tabs")
    uia(do=[{"action": "invoke", "name": "Shift times options", "type": "Button", "after": 1000},
            {"action": "invoke", "name": "Undock", "type": "MenuItem"}])
    st = wait_for(lambda s: "Shift times" in frames(s))
    tab_floated = "Shift times" in frames(st)
    ev3, st = snap("a11y-3-undock-timing-tab")
    (EVID / "a11y-tabs.txt").write_text(json.dumps(
        {"tabs": {n: [h["path"] for h in hits] for n, hits in tabs.items()}, "tab lists": lists,
         "menu button": tab_button}, indent=1, ensure_ascii=False))
    verdict("tabs-accessible",
            "observed" if all(tabs.values()) and lists and tab_button and tab_floated else "failed",
            f"tab items { {n: bool(h) for n, h in tabs.items()} } in a tab list 'Panels' ({len(lists)}); the selected "
            f"tab's 'Shift times options': {tab_button}; its menu's Undock invoked -> own window={tab_floated}",
            ev2 + ev3 + ["a11y-tabs.txt"])


def focus_header_control(ctype, name, panel_name, tries=12):
    for _ in range(6):
        if panel_of(uia()["focusPath"]) == panel_name:
            break
        combo("f6")
        time.sleep(0.6)
    want = f"{ctype}:'{name}'"
    for i in range(tries):
        st = uia()
        if (st["focusPath"] or "").endswith(want):
            return f"F6, Shift+Tab x{i}", st["focusPath"]
        combo("shift", "tab")
        time.sleep(0.4)
    uia(do=[{"action": "focus", "name": name, "type": ctype}])
    st = uia()
    return ("UIA SetFocus" if (st["focusPath"] or "").endswith(want) else None), st["focusPath"]


def step_header():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    park_pointer()
    focus_main_compositor()
    how, path = focus_header_control("TabItem", "Grid", "Grid")
    keys("right", 0.4)
    on_button = (uia()["focusPath"] or "").endswith("Button:'Grid options'")
    keys("space", 0.8)
    st = uia()
    items = menu_items(st)
    ev0, _ = snap("header-0-grid-menu-by-keys")
    keys("escape", 0.5)
    back = uia()["focusPath"]
    keys("left", 0.3, "shift+f10", 0.8)
    items_f10 = menu_items(uia())
    ev1, _ = snap("header-1-shift-f10")
    keys("escape", 0.5)
    want = ["Move panel…", "Undock", "Close"]
    verdict("header-menu-keyboard",
            "observed" if how and on_button and [i.split(" (")[0] for i in items] == want and items_f10 else "failed",
            f"the Grid tab focused by {how} ({path}); Right -> on 'Grid options'={on_button}; Space opened {items}; "
            f"Escape left the focus at {back}; Left, Shift+F10 opened {items_f10}", ev0 + ev1)
    how2, path2 = focus_header_control("Button", "Audio options", "Audio")
    keys("return", 0.8)
    items_audio = menu_items(uia())
    keys("down", "down", "return", 1.5)
    st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
    floated = "Audio" in frames(st)
    ev2, st = snap("header-2-audio-undocked-by-keys")
    docked, how3 = False, None
    if floated:
        how3, _ = focus_header_control("Button", "Audio options", "Audio")
        keys("return", 0.8, "down", "down", "return", 1.5)
        st = wait_for(lambda s: "Audio" not in frames(s), timeout=4)
        docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    ev3, st = snap("header-3-audio-docked-by-keys")
    verdict("header-menu-undock-dock-keyboard", "observed" if how2 and floated and docked else "failed",
            f"'Audio options' focused by {how2} ({path2}); Return opened {items_audio}; Down Down Return: own "
            f"window={floated}; in the floating window ('Audio options' by {how3}) Return Down Down Return: "
            f"docked={docked}; focus {st['focusPath']}", ev2 + ev3)


CORE = ("Video", "Audio", "Line editor", "Grid")
PRESETS = ("Editing", "Timing", "Translation", "Typesetting")
RETIRED_VIEW_ITEMS = ("All", "Video and subs", "Audio and subs", "Only video", "Only subtitles")
RETIRED_EDITOR_ITEM = "Enable / Disable editor"


def core_panels(st):
    return {p["id"]: (p["x"], p["y"], p["w"], p["h"]) for p in st["panels"] if p["id"] in CORE and p["frame"] == MAIN}


def popup_items(st):
    els = st.get("elements") or []
    return [e for e in els if e["t"] == "MenuItem" and not e["off"]
            and not any(a["t"] in ("MenuBar", "TitleBar") for a in ancestors(els, e))]


def layout_menu(st):
    items = popup_items(st)
    parent = next((e["p"] for e in items if e["n"] in PRESETS), None)
    return [e for e in items if parent is not None and e["p"] == parent]


def layout_to(name):
    if not menu_to("Layout"):
        return False
    keys(SUBMENU["key"], 0.6)
    items = layout_menu(uia())
    enabled = [e["n"] for e in items if e["en"]]
    if name not in enabled:
        log(f"layout_to: {name!r} is not an enabled item of {[(e['n'], e['en']) for e in items]}")
        return False
    if enabled.index(name):
        keys(*(["down"] * enabled.index(name)), delay=0.3)
    return True


def layout_items():
    items = {}
    if menu_to("Layout"):
        keys(SUBMENU["key"], 0.6)
        items = {e["n"]: e["en"] for e in layout_menu(uia())}
    keys("escape", "escape", "escape", 0.4)
    return items


def menu_names(opener):
    if opener == "view":
        open_view()
    else:
        park_pointer()
        keys(opener, 0.6)
    names = [e["n"] for e in popup_items(uia())]
    keys("escape", "escape", 0.4)
    return names


def switcher_label(st=None):
    st = st or uia()
    return next((e["n"] for e in st.get("elements") or [] if not e["off"] and (e["n"] or "").startswith("Layout: ")),
                None)


def step_layouts():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    focus_main()
    ev, st = snap("layouts-0-editing")
    evidence = list(ev)
    base = core_panels(st)
    view = menu_names("view")
    subtitles = menu_names("alt+s")
    first = layout_items()
    switch0 = switcher_label()
    focus_main()
    keys("ctrl+e", 1.5)
    after_e = core_panels(uia())
    gone = not set(RETIRED_VIEW_ITEMS) & set(view) and RETIRED_EDITOR_ITEM not in subtitles
    listed = all(first.get(p) for p in PRESETS) and first.get("Save layout as…") and first.get("Manage layouts…") is False
    verdict("layouts-menus", "observed" if gone and listed and "Layout" in view and switch0 == "Layout: Editing"
            else "failed",
            f"View: {view}; Subtitles has the editor switch: {RETIRED_EDITOR_ITEM in subtitles}; View > Layout "
            f"(enabled): {first}; switcher: {switch0!r}", evidence)
    verdict("layouts-editor-key-retired", "observed" if base and set(after_e) == set(base) else "failed",
            f"Ctrl+E: panels before {sorted(base)}, after {sorted(after_e)}", evidence)

    panel_menu("Audio", "Hide")
    st = wait_for(lambda s: "Audio" not in core_panels(s), timeout=5)
    saved = set(core_panels(st))
    reached = layout_to("Save layout as…")
    keys("return", 1.2)
    type_text("no audio")
    time.sleep(0.5)
    keys("return", 1.5)
    switch1 = switcher_label()
    ev, st = snap("layouts-1-saved")
    evidence += ev
    reached &= layout_to("Timing")
    keys("return", 1.5)
    st = wait_for(lambda s: set(core_panels(s)) != saved, timeout=5)
    timing, switch2 = set(core_panels(st)), switcher_label(st)
    ev, st = snap("layouts-2-timing")
    evidence += ev
    reached &= layout_to("no audio")
    keys("return", 1.5)
    st = wait_for(lambda s: set(core_panels(s)) == saved, timeout=5)
    back, switch3 = set(core_panels(st)), switcher_label(st)
    ev, st = snap("layouts-3-saved-applied")
    evidence += ev
    rows = [f"saved {sorted(saved)} as 'no audio' (switcher {switch1!r})",
            f"Timing: {sorted(timing)} ({switch2!r})", f"no audio again: {sorted(back)} ({switch3!r})"]
    verdict("layouts-save-apply", "observed" if reached and "Audio" not in saved and timing != saved
            and back == saved and switch1 == switch3 == "Layout: no audio" and switch2 == "Layout: Timing"
            else "failed", "from the keyboard: " + "; ".join(rows), evidence)

    focus_main_compositor()
    keys("alt+f4")
    time.sleep(2.5)
    status = app_status()
    if status["alive"]:
        time.sleep(6)
        app_kill()
        status = app_status()
    cfg = "\n".join(status["layout_files"])
    app_start()
    dismiss_notices()
    focus_main()
    restarted = layout_items()
    ev, _ = snap("layouts-4-restarted", extra="# layout files\n" + cfg)
    evidence += ev
    verdict("layouts-persist", "observed" if restarted.get("no audio") and "layouts.json" in cfg else "failed",
            f"after a restart View > Layout: {restarted}; files: {cfg.strip()!r}", evidence)

    reached = layout_to("Manage layouts…")
    keys("return", 1.2)
    ev, _ = snap("layouts-5-manage")
    evidence += ev
    keys("f2", 0.4, "ctrl+a", 0.3)
    type_text("quiet")
    time.sleep(0.5)
    keys("return", 1.0, "escape", 0.8)
    renamed, switch4 = layout_items(), switcher_label()
    reached &= layout_to("Manage layouts…")
    keys("return", 1.2, "delete", 1.2)
    deleted, switch5 = layout_items(), switcher_label()
    ev, _ = snap("layouts-6-deleted")
    evidence += ev
    (EVID / "layouts.txt").write_text(
        f"View: {view}\nSubtitles: {subtitles}\nfirst: {first}\n" + "\n".join(rows)
        + f"\nrestarted: {restarted}\nrenamed: {renamed} ({switch4!r})\ndeleted: {deleted} ({switch5!r})\n")
    evidence.append("layouts.txt")
    verdict("layouts-rename-delete", "observed" if reached and renamed.get("quiet") and "no audio" not in renamed
            and switch4 == "Layout: quiet" and "quiet" not in deleted and deleted.get("Manage layouts…") is False
            and switch5 == "Layout: Editing" else "failed",
            f"renamed: {renamed} ({switch4!r}); deleted: {deleted} ({switch5!r})", evidence)


def tab_arrangement(st):
    fr = frames(st)
    out = {}
    for p in st["panels"]:
        if p["id"] not in PANELS or p["id"] in out:
            continue
        f = fr.get(p["frame"], {})
        entry = {"window": "main" if p["frame"] == MAIN else "floating",
                 "rect": (p["x"] - f.get("x", 0), p["y"] - f.get("y", 0), p["w"], p["h"])}
        if p["frame"] != MAIN:
            entry["frame"] = (f.get("x"), f.get("y"), f.get("w"), f.get("h"))
        out[p["id"]] = entry
    return out


def tab_differences(before, after, tolerance=2):
    diffs = []
    for pid in sorted(set(before) | set(after)):
        a, b = before.get(pid), after.get(pid)
        if a is None or b is None:
            diffs.append(f"{pid}: {'shown' if a else 'not shown'} before, {'shown' if b else 'not shown'} after")
        elif a["window"] != b["window"]:
            diffs.append(f"{pid}: {a['window']} before, {b['window']} after")
        else:
            for key in ("rect", "frame"):
                if key in a and not all(x is not None and y is not None and abs(x - y) <= tolerance
                                        for x, y in zip(a[key], b.get(key, ()))):
                    diffs.append(f"{pid}: {key} {a[key]} before, {b.get(key)} after")
    return diffs


def step_tabs():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    float_panel("Audio", "tabs")
    st = wait_for(lambda s: "Audio" in frames(s))
    time.sleep(1)
    ev0, st = snap("tabs-0-first-tab")
    first = tab_arrangement(st)
    focus_main()
    keys("ctrl+t", 1.5)
    ev1, st = snap("tabs-1-new-tab")
    copied = tab_differences(first, tab_arrangement(st))
    panel_menu("Audio", "Dock")
    float_panel("Line editor", "tabs")
    st = wait_for(lambda s: "Line editor" in frames(s) and "Audio" not in frames(s))
    time.sleep(1)
    ev2, st = snap("tabs-2-second-tab-own")
    second = tab_arrangement(st)
    focus_main()
    keys("ctrl+pgup", 1.5)
    st = wait_for(lambda s: "Audio" in frames(s) and "Line editor" not in frames(s))
    time.sleep(1)
    ev3, st = snap("tabs-3-first-tab-again")
    to_first = tab_differences(first, tab_arrangement(st))
    focus_main()
    keys("ctrl+pgdn", 1.5)
    st = wait_for(lambda s: "Line editor" in frames(s) and "Audio" not in frames(s))
    time.sleep(1)
    ev4, st = snap("tabs-4-second-tab-again")
    to_second = tab_differences(second, tab_arrangement(st))
    distinct = bool(tab_differences(first, second))
    verdict("tab-new-copies-arrangement", "observed" if not copied else "failed",
            "Ctrl+T: the new tab shows the first tab's arrangement, Audio floating"
            + (f"; differences: {copied}" if copied else ""), ev0 + ev1)
    verdict("tab-switch-floating", "observed" if distinct and not to_first and not to_second else "failed",
            f"tabs differ ({distinct}); Ctrl+PgUp back to the first tab: same windows, rectangles and floating "
            f"windows {not to_first}" + (f" {to_first}" if to_first else "") + f"; Ctrl+PgDn to the second: "
            f"{not to_second}" + (f" {to_second}" if to_second else ""), ev0 + ev2 + ev3 + ev4)


def fs_frames(st, mons):
    full = main = None
    for f in st["frames"]:
        if f["id"] != MAIN:
            continue
        if any((f["x"], f["y"], f["w"], f["h"]) == (m["x"], m["y"], m["w"], m["h"]) for m in mons):
            full = f
        else:
            main = f
    return full, main


def fs_value(st, ctype, name):
    return next((e["val"] for e in st.get("elements") or [] if e["t"] == ctype and e["n"] == name and not e["off"]),
                None)


def fs_has(st, ctype, name):
    return bool(find(st, ctype, name))


def step_video_fullscreen():
    if not LAYOUT.get("ep1_ass"):
        verdict("videofs-enter-leave", "error", "no cfr.mkv media fixture in the VM's build tree (run the test task once)")
        return
    d = displays("extend")
    mons = monitors()
    fresh(LAYOUT["ep1_ass"])
    dismiss_notices()
    uia(do=[{"action": "invoke", "name": "Load associated", "type": "Button", "all": True}])
    time.sleep(5)
    focus_main()
    for _ in range(8):
        if panel_of(uia()["focusPath"]) == "Video":
            break
        combo("f6")
        time.sleep(0.8)
    st0 = uia()
    in_video = panel_of(st0["focusPath"]) == "Video"
    _, main0 = fs_frames(st0, mons)
    m0 = next((i for i, m in enumerate(mons) if main0 and inside(main0, m)), 0)
    keys("f")
    st1 = wait_for(lambda s: fs_frames(s, mons)[0] is not None and fs_frames(s, mons)[0]["active"], timeout=8)
    full1, _ = fs_frames(st1, mons)
    ev1, st1 = snap("videofs-1-fullscreen", monitor=m0, extra=f"# monitors\n{json.dumps(mons)}\n# displays\n{json.dumps(d)[:2000]}")
    on1 = full1 is not None and inside(full1, mons[m0])
    keys("space")
    stp = wait_for(lambda s: fs_has(s, "Button", "Pause"), timeout=5)
    playing = fs_has(stp, "Button", "Pause")
    time.sleep(0.6)
    keys("space")
    sts = wait_for(lambda s: fs_has(s, "Button", "Play"), timeout=5)
    paused = fs_has(sts, "Button", "Play")
    # UI Automation shows no slider value or label text of Qt Quick's here: compare the monitor's pixels.
    time.sleep(1.0)
    shot_a = head_shot("videofs-1b-paused", m0)
    # Left: a slow guest can play the two-second clip to its last frame, where Right has nowhere to go.
    keys("left")
    time.sleep(1.5)
    shot_b = head_shot("videofs-1c-stepped", m0)
    diff = image_diff(shot_a[0], shot_b[0]) if shot_a and shot_b else None
    keys("escape")
    st2 = wait_for(lambda s: fs_frames(s, mons)[0] is None and fs_frames(s, mons)[1] is not None
                   and fs_frames(s, mons)[1]["active"] and panel_of(s["focusPath"]) == "Video", timeout=8)
    ev2, st2 = snap("videofs-2-left")
    full2, main2 = fs_frames(st2, mons)
    keys4 = ("x", "y", "w", "h")
    same = main0 is not None and main2 is not None and all(main0[k] == main2[k] for k in keys4)
    focus_back = panel_of(st2["focusPath"]) == "Video"
    stepped = isinstance(diff, int) and diff > 50
    verdict("videofs-enter-leave", "observed" if in_video and on1 and full1["active"] and full2 is None and main2
            and main2["active"] and same and focus_back else "failed",
            f"F in the Video panel (focus there: {in_video}): fullscreen window {full1} on monitor {m0} {mons[m0]}: {on1}; "
            f"Esc: fullscreen gone {full2 is None}, main window active {main2 and main2['active']}, geometry before "
            f"{main0 and {k: main0[k] for k in keys4}} after {main2 and {k: main2[k] for k in keys4}} same {same}; focus "
            f"back in the Video panel: {focus_back} ({st2['focusPath']})", ev1 + ev2)
    verdict("videofs-transport-keys", "observed" if playing and paused and stepped else "failed",
            f"in fullscreen Space played (Pause shown: {playing}) and paused (Play shown again: {paused}); Left "
            f"changed {diff} pixels of the fullscreen monitor (videofs-1b-paused, videofs-1c-stepped)",
            ev1 + shot_a + shot_b)

    def second_monitor(tag, scale):
        mons2 = monitors()
        if len(mons2) < 2:
            verdict(f"videofs-monitor-choice{tag}", "not-observable", f"only {len(mons2)} monitor(s) on the guest desktop")
            return
        second = next(i for i, m in enumerate(mons2) if not m["primary"])
        focus_main()
        for _ in range(8):
            if panel_of(uia()["focusPath"]) == "Video":
                break
            combo("f6")
            time.sleep(0.8)
        _, main3a = fs_frames(uia(), mons2)
        park_pointer()
        keys("apps")
        stm = wait_for(lambda s: "Open in full screen on monitor 2" in s["popupMenuItems"], timeout=4)
        offered = "Open in full screen on monitor 2" in stm["popupMenuItems"]
        if offered:
            uia(do=[{"action": "invoke", "name": "Open in full screen on monitor 2", "type": "MenuItem"}], settle_ms=1500)
        else:
            keys("escape")
        st3 = wait_for(lambda s: fs_frames(s, mons2)[0] is not None and fs_frames(s, mons2)[0]["active"], timeout=8)
        st3 = uia(dpi=True)
        full3, main3 = fs_frames(st3, mons2)
        ev3, _ = snap(f"videofs-3-monitor2{tag}", monitor=second,
                      extra=f"# monitors\n{json.dumps(mons2)}\n# popup menu items\n{json.dumps(stm['popupMenuItems'])}")
        on2 = full3 is not None and (full3["x"], full3["y"], full3["w"], full3["h"]) == \
            (mons2[second]["x"], mons2[second]["y"], mons2[second]["w"], mons2[second]["h"])
        dpi_ok = full3 is not None and full3.get("dpi") == mons2[second]["dpi"]
        dock_hidden = not any(p["id"] == "Video" for p in st3["panels"])
        keys("escape")
        st4 = wait_for(lambda s: fs_frames(s, mons2)[0] is None and any(p["id"] == "Video" for p in s["panels"])
                       and panel_of(s["focusPath"]) == "Video", timeout=8)
        ev4, st4 = snap(f"videofs-4-left-monitor2{tag}")
        full4, main4 = fs_frames(st4, mons2)
        dock_back = any(p["id"] == "Video" for p in st4["panels"])
        same4 = main3a is not None and main4 is not None and all(main3a[k] == main4[k] for k in keys4)
        focus4 = panel_of(st4["focusPath"]) == "Video"
        verdict(f"videofs-monitor-choice{tag}", "observed" if offered and on2 and full3["active"] and dpi_ok and dock_hidden
                and full4 is None and dock_back and same4 and focus4 else "failed",
                f"monitors {mons2}; the menu key offered 'Open in full screen on monitor 2': {offered}; fullscreen window "
                f"{full3} covering monitor {second} ({scale} %): {on2}, GetDpiForWindow {full3 and full3.get('dpi')} = the "
                f"monitor's {mons2[second]['dpi']}: {dpi_ok}; docked Video hidden meanwhile: {dock_hidden}; Esc: fullscreen "
                f"gone {full4 is None}, docked Video back {dock_back}, main geometry unchanged {same4}, focus back in the "
                f"Video panel {focus4}", ev3 + ev4)

    second_monitor("", 100)
    # Windows offers 150 % only from about 1920x1200.
    d2 = displays("extend", scale2=150, second=(1920, 1200))
    log("displays at 150 %:", json.dumps(d2)[:600])
    second_monitor("-150", 150)
    app_kill()
    displays("extend", scale2=100)


def nvda_start():
    if not LAYOUT.get("nvda_exe"):
        job, lines = W.task("nvda-setup", timeout=1500)
        raw = line_value(lines, "NVDA_LAYOUT ")
        (EVID / "nvda-setup.txt").write_text("\n".join(lines) + "\n")
        if not raw:
            return None
        LAYOUT["nvda_exe"] = json.loads(raw)["exe"]
    nvda_log = LAYOUT["nvda_dir"] + "\\nvda-" + time.strftime("%Y%m%d-%H%M%S") + ".log"
    W.ui("launch", exe=LAYOUT["nvda_exe"], arguments=[
        "--minimal", "--replace", "--disable-addons", "--log-level=12", f"--log-file={nvda_log}",
        f"--config-path={LAYOUT['nvda_config']}"], cwd=LAYOUT["nvda_dir"])
    time.sleep(10)
    return nvda_log


def nvda_stop(nvda_log, tag):
    job, lines = W.task("nvda-output", env={"GATE_NVDA_LOG": nvda_log})
    s = sections(lines)
    speech = [l.strip() for l in s.get("speech", []) if l.strip()]
    (EVID / f"{tag}-nvda-speech.txt").write_text("\n".join(speech) + "\n")
    (EVID / f"{tag}-nvda-output.txt").write_text("\n".join(lines) + "\n")
    W.pull(LAYOUT["nvda_dir"] + "\\nvda-last.log", EVID / f"{tag}-nvda.log")
    silent = any("silence" in l for l in s.get("synth", []))
    return speech, silent, [f"{tag}-nvda-speech.txt", f"{tag}-nvda-output.txt", f"{tag}-nvda.log"]


def step_snap():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    placed = narrow_main()
    log("main window on the left:", placed)
    try:
        time.sleep(1.0)
        mons = monitors()
        prim = next(m for m in mons if m["primary"])
        focus_main_compositor()
        float_panel("Audio", "snap")
        st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
        if "Audio" not in frames(st):
            verdict("floating-snap", "not-observable", "Audio did not float")
            return
        out, shots = [], []
        # Top first: after the left drag the window is mostly off the monitor, where the tablet cannot press it.
        for edge, target in (("top", (prim["x"] + prim["w"] // 2, prim["y"] + 1)),
                             ("left", (prim["x"] + 1, prim["y"] + prim["h"] // 2))):
            st = uia()
            before = frames(st).get("Audio")
            btn = next((b for b in find(st, "Button", "Audio options") if frame_id(b["win"]) == "Audio"), None)
            if not before or not btn:
                out.append({"edge": edge, "error": "no floating Audio or its ⋯ in UIA"})
                continue
            # The tablet reaches the primary monitor only: no further right than its edge.
            sx, sy = min(btn["x"] - 24, prim["x"] + prim["w"] - 8), btn["y"] + btn["h"] // 2
            mid = (max(prim["x"] + 520, sx - 60), min(prim["y"] + prim["h"] - 200, sy + 60))
            try:
                pointer([(sx, sy)], hold=True)
                time.sleep(0.25)
                pointer([(sx, sy), mid], button="none", steps=20, delayMs=40)
                pointer([mid, target], button="none", steps=30, delayMs=40)
                time.sleep(1.5)  # Snap would show its preview while the pointer rests at the edge
                held = frames(uia()).get("Audio")
                ev, _ = snap(f"snap-{edge}-held")
                shots += ev
            finally:
                release()
                time.sleep(2.0)
            st = uia(dpi=True)
            after = frames(st).get("Audio")
            ev, st = snap(f"snap-{edge}-released")
            shots += ev
            half = bool(after) and abs(after["x"] - prim["x"]) <= 12 and abs(after["w"] - prim["w"] // 2) <= 24 \
                and after["h"] >= prim["h"] - 90
            full = bool(after) and after["w"] >= prim["w"] - 24 and after["h"] >= prim["h"] - 90
            want = (before["x"] + target[0] - sx, before["y"] + target[1] - sy)
            follows = bool(held) and abs(held["x"] - want[0]) <= 24 and abs(held["y"] - want[1]) <= 24
            out.append({"edge": edge, "pressed": [sx, sy], "to": list(target),
                        "before": before and [before["x"], before["y"], before["w"], before["h"]],
                        "held": held and [held["x"], held["y"], held["w"], held["h"]],
                        "expected while held": list(want), "follows the pointer": follows,
                        "after": after and [after["x"], after["y"], after["w"], after["h"]],
                        "snapped": "left half" if edge == "left" and half else "maximized" if edge == "top" and full
                        else "no", "still floating": bool(after),
                        "size kept": bool(after) and (after["w"], after["h"]) == (before["w"], before["h"])})
            if after:
                fullscreen("left", width=480)
        (EVID / "snap.json").write_text(json.dumps({"monitors": mons, "drags": out}, indent=1))
        floating = all(o.get("still floating") for o in out)
        follows = all(o.get("follows the pointer") for o in out)
        unsnapped = all(o.get("snapped") == "no" and o.get("size kept") for o in out)
        taken = [o["edge"] + ": " + o.get("snapped", "?") for o in out]
        ok = out and floating and follows and unsnapped and not any("error" in o for o in out)
        verdict("floating-snap", "observed" if ok else "failed",
                f"the floating Audio window's header right of its title dragged to the primary monitor's left and "
                f"top edges and held there 1.5 s: the engine's drag (the window followed the pointer: {follows}); "
                f"Snap took it {taken} (expected no: not offered on Windows since 2026-10-15); size kept and still a "
                f"floating panel after each: {unsnapped and floating} (snap.json: window rectangles before, while "
                f"held and after)", ["snap.json"] + shots)
    finally:
        wide_main()


def step_tags():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    nvda_log = nvda_start()
    if not nvda_log:
        verdict("nvda-tag-list", "error", "nvda-setup did not create the portable copy (nvda-setup.txt)", ["nvda-setup.txt"])
        return
    ev, st, typed = [], {"texts": {}}, None
    try:
        focus_main_compositor()
        st0 = uia(do=[{"action": "invoke", "name": "Hide tags", "type": "CheckBox"}], settle_ms=800)
        set_focus("Line text")
        keys("home", 0.5)
        type_text("{\\")
        time.sleep(1.5)
        ev, st = snap("tags-0-list-open")
        typed = st["texts"].get("Line text")
        keys("down", 1.2, "down", 1.2)
        ev += snap("tags-1-down-down")[0]
        st1 = uia()
        keys("escape", 0.8)
        ev += snap("tags-2-escape")[0]
    finally:
        speech, silent, nev = nvda_stop(nvda_log, "tags")
    rows = [e for e in st.get("elements") or [] if e["t"] in ("ListItem", "DataItem") and not e["off"]]
    toggled = any(a.get("matches") and a.get("done") and not a.get("errors") for a in st0.get("actions") or [])
    list_open = {"alpha", "be", "blur"} <= {e["n"] for e in rows if e["t"] == "ListItem"}
    announced = [l for l in speech if re.search(r"\\+[a-z0-9]+, [^,']+(?:, [^,']+)*, \d+ of \d+", l)]
    focus_kept = "Line text" in (st1.get("focus") or "")
    (EVID / "tags.json").write_text(json.dumps({"hide-tags pressed": st0.get("actions"), "text after typing": typed,
                                                "focus with the list open": st1.get("focus"),
                                                "list rows in UIA": [(e["t"], e["n"]) for e in rows][:40],
                                                "announcements": announced}, indent=1, ensure_ascii=False))
    verdict("tag-list-raw-view", "observed" if toggled and list_open and typed and typed.startswith("{\\") else "failed",
            f"with Hide tags pressed through UIA ({st0.get('actions')}), typing {{\\ at the start left the Line text "
            f"{typed!r}, and the tag list {'opened' if list_open else 'did not open'}", ["tags.json"] + ev)
    verdict("tag-list-focus-kept", "observed" if list_open and focus_kept else "failed",
            f"with the list {'open' if list_open else 'not open'} and Down pressed twice the focus is "
            f"{st1.get('focus')!r}", ["tags.json"] + ev)
    verdict("nvda-tag-list", "observed" if list_open and any("Edge blur" in l for l in announced) and silent else "failed",
            f"NVDA spoke the tag list's selection: {announced[:6]} ({len(speech)} speech lines, silent synthesizer "
            f"{silent})", ["tags.json"] + nev + ev)


def step_statusbar():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    nvda_log = nvda_start()
    if not nvda_log:
        verdict("nvda-status-bar", "error", "nvda-setup did not create the portable copy (nvda-setup.txt)", ["nvda-setup.txt"])
        return
    try:
        focus_main_compositor()
        set_focus("Line text")
        time.sleep(1.0)
        ev, st = snap("statusbar-0")
        keys("insert+end", 2.5)
        keys("insert+end", 2.5)  # twice: NVDA spells it on a double press, but each press speaks it
        ev += snap("statusbar-1-read")[0]
    finally:
        speech, silent, nev = nvda_stop(nvda_log, "statusbar")
    els = st.get("elements") or []
    bars = [e for e in els if e["t"] == "StatusBar" and not e["off"]]
    fields = [(c["t"], c["n"], c["val"]) for b in bars for c in els
              if any(a["i"] == b["i"] for a in ancestors(els, c)) and not c["off"]]
    (EVID / "statusbar.json").write_text(json.dumps({"status bars": [el_path(els, b) for b in bars],
                                                     "fields": fields}, indent=1, ensure_ascii=False))
    verdict("uia-status-bar", "observed" if bars and fields else "failed",
            f"UIA StatusBar elements {[el_path(els, b) for b in bars]} with {len(fields)} named descendants: "
            f"{fields[:12]}", ["statusbar.json"] + ev)
    none = [l for l in speech if "no status bar" in l.lower()]
    read = [l for l in speech if "status bar" in l.lower() and l not in none] or \
        [l for l in speech if any(n and n in l for _, n, _ in fields if n and len(n) > 3)]
    verdict("nvda-status-bar", "observed" if read and not none and silent else "failed",
            f"NVDA+End (read status bar): {read[:6]}; 'no status bar' answers {none[:2]} ({len(speech)} speech lines, "
            f"silent synthesizer {silent})", ["statusbar.json"] + nev + ev)


def new_elements(before, st, frame=None):
    seen = {(e["win"], el_path(before.get("elements") or [], e)) for e in before.get("elements") or []}
    els = st.get("elements") or []
    return [dict(e, path=el_path(els, e)) for e in els if not e["off"] and (e["win"], el_path(els, e)) not in seen
            and (frame is None or frame_id(e["win"]) == frame)]


def step_dialogs():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    named = ("Button", "CheckBox", "RadioButton", "SplitButton", "ComboBox", "Edit", "Slider", "Spinner", "TabItem",
             "ListItem", "MenuItem", "Hyperlink")
    found, ev = {}, []
    for title, opener in (("Style manager", ("ctrl+m",)), ("Time shift", ("ctrl+i",)),
                          ("Find and replace", ("ctrl+h",)), ("Settings", None)):
        focus_main_compositor()
        set_focus("Subtitle lines")
        before = uia()
        if opener:
            keys(*opener, 2.0)
        else:
            ok = menu_to("Settings", opener="alt+f")
            if ok:
                keys("return", 2.0)
        st = uia()
        new = new_elements(before, st)
        tag = re.sub(r"\W+", "-", title.lower())
        ev += snap(f"dialogs-{tag}")[0]
        controls = [e for e in new if e["t"] in named]
        unnamed = [e["path"] + f" id={e['id']!r}" for e in controls if not (e["n"] or "").strip()]
        found[title] = {"opened": bool(new), "windows": sorted({e["win"] for e in new}),
                        "controls": [(e["t"], e["n"]) for e in controls], "unnamed": unnamed}
        keys("escape", 1.0)
        for f in [f for f in uia()["frames"] if f["id"] not in (MAIN,) and f["id"] not in PANELS]:
            close_window(f["name"])
    (EVID / "dialogs.json").write_text(json.dumps(found, indent=1, ensure_ascii=False))
    opened = [t for t, v in found.items() if v["opened"]]
    unnamed = {t: v["unnamed"] for t, v in found.items() if v["unnamed"]}
    verdict("uia-names-outside-main", "observed" if opened and not unnamed else "failed",
            f"opened {opened} (not opened: {[t for t in found if t not in opened]}); controls with no UIA name: "
            f"{unnamed or 'none'}; counts { {t: len(v['controls']) for t, v in found.items()} } (dialogs.json)",
            ["dialogs.json"] + ev)


def step_reference():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    nvda_log = nvda_start()
    if not nvda_log:
        verdict("nvda-reference-tray", "error", "nvda-setup did not create the portable copy", ["nvda-setup.txt"])
        return
    ev, st = [], {}
    try:
        focus_main_compositor()
        keys("ctrl+t", 1.5, "ctrl+pgup", 1.5)
        set_focus("Subtitle lines")
        keys("home", 0.5, "ctrl+q", 2.0)
        ev, st = snap("reference-0-tray")
        set_focus("Reference lines (protected, read-only)")
        time.sleep(1.0)
        for _ in range(4):
            keys("shift+tab", 0.9)
        ev += snap("reference-1-tabbed")[0]
    finally:
        speech, silent, nev = nvda_stop(nvda_log, "reference")
    want = ("Follow the editing line", "Show nearest line", "Previous match", "Next match",
            "Reference lines (protected, read-only)")
    els = st.get("elements") or []
    status = [e["n"] for e in els if re.match(r"(Match \d+ of \d+|No matching line|Independent navigation)$", e["n"] or "")]
    present = {n: bool([e for e in els if e["n"] == n and not e["off"]]) for n in want
               if n != "Show nearest line" or "No matching line" in status}
    spoken = [n for n in want + tuple(status) if any(n in l for l in speech)]
    (EVID / "reference.json").write_text(json.dumps({"present": present, "status": status, "spoken": spoken},
                                                    indent=1, ensure_ascii=False))
    verdict("uia-reference-tray", "observed" if all(present.values()) and status else "failed",
            f"tray controls in UIA {present}; status {status}", ["reference.json"] + ev)
    verdict("nvda-reference-tray", "observed" if spoken and silent else "failed",
            f"NVDA spoke {spoken} ({len(speech)} speech lines, silent synthesizer {silent})",
            ["reference.json"] + nev + ev)


THEME_PANEL = {"light": "#F9FAFB", "dark": "#20262D", "highContrastWhite": "#FFFFFF", "highContrastBlack": "#080808"}
TITLE_CONTROLS = ("Window menu", "Application menu", "Minimise", "Maximise", "Restore", "Close", "More menus")


def title_bar_start(choice="auto", theme="dark"):
    app_kill()
    job, lines = W.task("gate-win-fresh")
    for l in lines:
        if l.startswith("set aside"):
            log(l)
            if not PROFILE["original"]:
                PROFILE["original"] = l.split(" -> ", 1)[1].rsplit("\\", 1)[0]
    if job.get("state") != "succeeded":
        raise RuntimeError("gate-win-fresh failed (the profile was not set aside): " + " | ".join(lines[-6:]))
    _, lines = W.task("gate-win-theme", env={"GATE_THEME": theme, "GATE_FOLLOW": "false", "GATE_TITLE_BAR": choice})
    log("theme:", line_value(lines, "THEME_RESULT "))
    app_start()
    time.sleep(1.5)


def title_bar_controls(st=None):
    st = st or uia()
    out = {}
    els = st.get("elements") or []
    for e in els:
        if e["off"] or frame_id(e["win"]) != MAIN:
            continue
        # Windows' own caption buttons (the system's title bar) are under its TitleBar "TitleBar".
        if any(a["t"] == "TitleBar" and a["id"] == "TitleBar" for a in ancestors(els, e)):
            continue
        if e["n"] in TITLE_CONTROLS and e["n"] not in out:
            out[e["n"]] = e
        if (e["n"] or "").startswith("Layout: ") and "switcher" not in out:
            out["switcher"] = e
    return out


def place_on_primary():
    """A fresh 1280x800 window reaches past the 1024-pixel primary, where the tablet does not reach."""
    return fullscreen("left", width=960)


def covers(r, m):
    """Windows puts a maximised window's frame outside the monitor, so R covers M's width."""
    return r is not None and r[0] <= m["x"] and r[0] + r[2] >= m["x"] + m["w"]


def free_point(ctl):
    m = ctl.get("Minimise") or ctl.get("Maximise") or ctl.get("Restore")
    return (m["x"] - 16, m["y"] + m["h"] // 2) if m else None


def main_rect(st=None):
    f = frames(st or uia()).get(MAIN)
    return (f["x"], f["y"], f["w"], f["h"]) if f else None


def colour_at(png, x, y, mons):
    from PIL import Image
    ox, oy = min(m["x"] for m in mons), min(m["y"] for m in mons)
    img = Image.open(EVID / png).convert("RGB")
    try:
        return "#%02X%02X%02X" % img.getpixel((x - ox, y - oy))
    except IndexError:
        return None


def changed_pixels(a, b, box, mons):
    from PIL import Image, ImageChops
    ox, oy = min(m["x"] for m in mons), min(m["y"] for m in mons)
    crop = (box[0] - ox, box[1] - oy, box[2] - ox, box[3] - oy)
    ia = Image.open(EVID / a).convert("L").crop(crop)
    ib = Image.open(EVID / b).convert("L").crop(crop)
    return sum(1 for v in ImageChops.difference(ia, ib).getdata() if v > 13)


def centre(e):
    return e["x"] + e["w"] // 2, e["y"] + e["h"] // 2


def step_title_bar():
    mons = monitors()
    prim = next(m for m in mons if m["primary"])

    title_bar_start("auto")
    st = uia(dpi=True)
    ctl = title_bar_controls(st)
    ev0, _ = snap("titlebar-0-default")
    main = frames(st).get(MAIN)
    names = {n: n in ctl for n in ("Window menu", "Application menu", "Minimise", "Maximise", "Close")}
    verdict("titlebar-default", "observed" if all(names.values()) and ctl.get("switcher") else "failed",
            f"Windows: HikariSub's title bar by default: {names}, switcher {ctl.get('switcher', {}).get('n')!r}", ev0)
    icon = ctl.get("Window menu")
    top_gap = icon["y"] - main["y"] if icon and main else None
    info = next((w for w in st.get("windowInfo") or [] if w.get("hwnd") == (main or {}).get("hwnd")), {})
    verdict("titlebar-frameless", "observed" if top_gap is not None and 0 <= top_gap <= 2 else "failed",
            f"the window menu button {top_gap} px below the window's top (no caption of Windows' above the bar); "
            f"window style {hex(info.get('style') or 0)} (the caption styles kept for Snap and the system menu)", ev0)
    named = sorted({e["n"] for e in st.get("elements") or [] if frame_id(e["win"]) == MAIN and not e["off"]
                    and e["t"] in ("Button", "MenuItem", "MenuBar", "TitleBar", "SplitButton")
                    and main and e["y"] < main["y"] + 34})
    unnamed = [e["t"] for e in st.get("elements") or [] if frame_id(e["win"]) == MAIN and not e["off"] and not e["n"]
               and e["t"] in ("Button", "MenuItem", "SplitButton") and main and e["y"] < main["y"] + 34]
    verdict("titlebar-names", "observed" if not unnamed and all(names.values()) else "failed",
            f"UI Automation names in the title bar: {named}; controls with no name {unnamed}", ev0)

    log("placed on the primary:", place_on_primary())
    time.sleep(1.0)
    focus_main_compositor()
    st = uia()
    ctl = title_bar_controls(st)
    r0 = main_rect(st)
    fp = free_point(ctl)
    if r0 and fp:
        sx, sy = fp
        try:
            pointer([(sx, sy)], hold=True)
            time.sleep(0.25)
            pointer([(sx, sy), (sx + 40, sy + 30)], button="none", steps=20, delayMs=30)
            time.sleep(0.4)
        finally:
            release()
            time.sleep(1.0)
        r1 = main_rect()
        ev, _ = snap("titlebar-1-moved")
        moved = r1 and abs(r1[0] - r0[0] - 40) <= 8 and abs(r1[1] - r0[1] - 30) <= 8 and r1[2:] == r0[2:]
        verdict("titlebar-move", "observed" if moved else "failed",
                f"free part {fp} dragged by (+40,+30): window {r0} -> {r1}", ev)
    else:
        verdict("titlebar-move", "failed", f"no main window or free part ({r0}, {fp})")

    st = uia()
    ctl = title_bar_controls(st)
    r0 = main_rect(st)
    fp = free_point(ctl)
    st, how, gaps = double_click_until(*fp, done=lambda s: "Restore" in title_bar_controls(s))
    rmax = main_rect(st)
    ev, _ = snap("titlebar-2-double-click-maximised")
    work_ok = covers(rmax, prim)
    ctl = title_bar_controls(st)
    fp2 = free_point(ctl)
    st2, how2, _ = double_click_until(*fp2, done=lambda s: "Maximise" in title_bar_controls(s)) if fp2 else (uia(), None, [])
    rback = main_rect(st2)
    verdict("titlebar-double-click", "observed" if how and how2 and work_ok and rback == r0 else "failed",
            f"double click on the free part ({how}, gaps {gaps}): window {r0} -> {rmax} (the primary's width "
            f"{prim['w']}, covered: {work_ok}); again ({how2}): {rback}", ev)

    # HTMAXBUTTON: Windows' own press, handed to the QML button.
    ctl = title_bar_controls(st2)
    mb = ctl.get("Maximise")
    if mb:
        pointer([centre(mb)])
        st = wait_for(lambda s: "Restore" in title_bar_controls(s), timeout=4)
        rmax = main_rect(st)
        ev, _ = snap("titlebar-3-maximise-button")
        rb = title_bar_controls(st).get("Restore")
        if rb:
            pointer([centre(rb)])
        st = wait_for(lambda s: "Maximise" in title_bar_controls(s), timeout=4)
        rback = main_rect(st)
        verdict("titlebar-maximise-button", "observed" if rb and covers(rmax, prim) and rback == r0
                else "failed", f"Maximise clicked: {r0} -> {rmax}, Restore shown {bool(rb)}; Restore clicked: {rback}", ev)
    else:
        verdict("titlebar-maximise-button", "failed", f"no Maximise ({sorted(ctl)})")

    park_pointer()
    time.sleep(0.8)
    ctl = title_bar_controls()
    mb = ctl.get("Maximise")
    if mb:
        ev_a, _ = snap("titlebar-4-before-hover")
        pointer([centre(mb)], button="none")
        time.sleep(2.0)
        ev_b, st = snap("titlebar-4-snap-layouts")
        box = (max(mb["x"] - 260, prim["x"]), mb["y"] + mb["h"] + 4,
               min(mb["x"] + mb["w"] + 60, prim["x"] + prim["w"]), min(mb["y"] + mb["h"] + 200, prim["y"] + prim["h"]))
        changed = changed_pixels(ev_a[0], ev_b[0], box, mons)
        hovered = colour_at(ev_b[0], mb["x"] + 4, mb["y"] + 4, mons)
        plain = colour_at(ev_a[0], mb["x"] + 4, mb["y"] + 4, mons)
        park_pointer()
        keys("escape", 0.5)
        verdict("titlebar-snap-layouts", "observed" if changed > 4000 and hovered != plain else "failed",
                f"pointer resting on Maximise 2 s: {changed} pixels changed under it in {box} (Windows 11's snap "
                f"layouts flyout); the button's background {plain} -> {hovered} (its hover, from WM_NCMOUSEMOVE)",
                ev_a + ev_b)
    else:
        verdict("titlebar-snap-layouts", "failed", f"no Maximise ({sorted(ctl)})")

    st = uia()
    r0 = main_rect(st)
    fp = free_point(title_bar_controls(st))
    try:
        pointer([fp], hold=True)
        time.sleep(0.25)
        pointer([fp, (fp[0], prim["y"] + 40), (fp[0], prim["y"] + 1)], button="none", steps=25, delayMs=40)
        time.sleep(1.5)
    finally:
        release()
        time.sleep(1.5)
    st = uia()
    rtop = main_rect(st)
    ev_t, _ = snap("titlebar-5-aero-snap-top")
    top_max = "Restore" in title_bar_controls(st)
    focus_main_compositor()
    combo("win", "down")
    time.sleep(1.2)
    rrest = main_rect()
    combo("win", "left")
    time.sleep(1.5)
    keys("escape", 0.5)  # Snap Assist offers the other windows for the right half
    st = uia()
    rleft = main_rect(st)
    ev_l, _ = snap("titlebar-5-aero-snap-left")
    half = rleft is not None and rrest is not None and abs(rleft[0] - prim["x"]) <= 10 \
        and abs(rleft[1] - prim["y"]) <= 10 and rleft[3] >= prim["h"] - 60 \
        and (abs(rleft[2] - prim["w"] // 2) <= 24 or prim["w"] // 2 < rleft[2] < rrest[2])
    verdict("titlebar-aero-snap", "observed" if top_max and half else "failed",
            f"free part dragged to the primary's top edge: window {r0} -> {rtop}, maximised (Restore shown) "
            f"{top_max}; Win+Left: {rrest} -> {rleft}, the left half of {prim['w']}x{prim['h']} (or the "
            f"minimum width): {half}", ev_t + ev_l)
    combo("win", "right")
    time.sleep(1.2)
    keys("escape", 0.5)

    title_bar_start("custom")
    log("placed on the primary:", place_on_primary())
    time.sleep(1.0)
    st = uia()
    r0 = main_rect(st)
    if r0:
        cx, cy = r0[0] + r0[2] - 3, r0[1] + r0[3] - 3
        try:
            pointer([(cx, cy)], hold=True)
            time.sleep(0.25)
            pointer([(cx, cy), (cx - 120, cy - 80)], button="none", steps=20, delayMs=30)
            time.sleep(0.4)
        finally:
            release()
            time.sleep(1.0)
        r1 = main_rect()
        ev, _ = snap("titlebar-6-resized")
        resized = r1 is not None and abs(r1[2] - (r0[2] - 120)) <= 8 and abs(r1[3] - (r0[3] - 80)) <= 8 \
            and r1[:2] == r0[:2]
        verdict("titlebar-resize", "observed" if resized else "failed",
                f"bottom-right corner dragged by (-120,-80): {r0} -> {r1}", ev)
    else:
        verdict("titlebar-resize", "failed", "no main window")

    def system_menu(open_it, tag):
        open_it()
        time.sleep(1.0)
        st = uia()
        items = st["popupMenuItems"]
        ev, _ = snap(f"titlebar-7-system-menu-{tag}")
        keys("escape", 0.6)
        return items, ev
    ctl = title_bar_controls()
    fp = free_point(ctl)
    icon = ctl.get("Window menu")
    shown, shots = {}, []
    for tag, opener in (("right-click", lambda: pointer([fp], button="right")),
                        ("icon", lambda: pointer([centre(icon)]) if icon else None),
                        ("alt-space", lambda: (focus_main_compositor(), combo("alt", "space")))):
        items, ev = system_menu(opener, tag)
        shown[tag] = items
        shots += ev
    want = {"Move", "Size", "Minimize", "Close"}
    ok = all(want <= {i.replace("&", "") for i in shown[k]} for k in ("right-click", "icon"))
    kept = bool(main_window())
    verdict("titlebar-window-menu", "observed" if ok and kept else "failed",
            f"Windows' system menu (Move, Size, Minimize, Close expected) from a right click on the free part and a "
            f"click on the icon, each closed with Escape: {shown}; the window still open after the Escapes: {kept}. "
            f"Alt+Space is HikariSub's own (legacy's Play binding), so it opens the menu only where unbound "
            f"({shown['alt-space']})", shots)

    focus_main_compositor()
    park_pointer()
    keys("alt", 0.6, "down", 1.0)
    file_items = uia()["popupMenuItems"]
    ev7, _ = snap("titlebar-8-alt-down")
    keys("escape", 0.4, "escape", 0.4)
    keys("f10", 0.6, "right", 0.5, "down", 1.0)
    edit_items = uia()["popupMenuItems"]
    ev8, _ = snap("titlebar-8-f10-right-down")
    keys("escape", 0.4, "escape", 0.4)
    keys("alt+e", 1.0)
    alt_e = uia()["popupMenuItems"]
    keys("escape", 0.4)
    file_ok = any(n.startswith("Open") for n in file_items)
    edit_ok = any(n.startswith("Undo") for n in edit_items) and any(n.startswith("Undo") for n in alt_e)
    verdict("titlebar-keyboard", "observed" if file_ok and edit_ok else "failed",
            f"Alt, Down: {file_items[:4]} (File); F10, Right, Down: {edit_items[:4]} (Edit); Alt+E: {alt_e[:4]}",
            ev7 + ev8)

    ctl = title_bar_controls()
    mn = ctl.get("Minimise")
    if mn:
        pointer([centre(mn)])
        time.sleep(1.5)
        listed = W.ui("windows", pid=APP["pid"])
        mainw = next((w for w in listed if isinstance(listed, list) and frame_id(w.get("title") or "") == MAIN), {})
        ev, _ = snap("titlebar-9-minimised")
        verdict("titlebar-minimise", "observed" if mainw.get("minimized") else "failed",
                f"Minimise: the main window {json.dumps({k: mainw.get(k) for k in ('title', 'minimized', 'visible')})}", ev)
    else:
        verdict("titlebar-minimise", "failed", f"no Minimise ({sorted(ctl)})")
    title_bar_start("custom")
    place_on_primary()
    cl = title_bar_controls().get("Close")
    if cl:
        pointer([centre(cl)])
        time.sleep(3)
        alive = app_status()["alive"]
        verdict("titlebar-close", "observed" if not alive else "failed", f"Close: HikariSub still running {alive}")
        APP["pid"] = None if not alive else APP["pid"]
    else:
        verdict("titlebar-close", "failed", "no Close")

    title_bar_start("system")
    st = uia(dpi=True)
    ctl = title_bar_controls(st)
    main = frames(st).get(MAIN)
    menus = ctl.get("Application menu")
    gap = menus["y"] - main["y"] if menus and main else None
    ev, _ = snap("titlebar-10-system")
    buttons = [n for n in ("Window menu", "Minimise", "Maximise", "Close") if n in ctl]
    verdict("titlebar-system", "observed" if menus and ctl.get("switcher") and not buttons and gap and gap >= 20
            else "failed",
            f"menus {bool(menus)}, switcher {ctl.get('switcher', {}).get('n')!r}, HikariSub's window buttons {buttons}; "
            f"the menus {gap} px below the window's top (Windows' caption above them)", ev)

    shots, wrong = [], []
    for theme, colour in THEME_PANEL.items():
        title_bar_start("custom", theme)
        mons = monitors()
        ctl = title_bar_controls()
        fp, cl = free_point(ctl), ctl.get("Close")
        ev, _ = snap(f"titlebar-11-theme-{theme}")
        got = colour_at(ev[0], fp[0], fp[1] - 10, mons) if fp else None
        hover = None
        if cl:
            pointer([centre(cl)], button="none")
            time.sleep(0.8)
            evh, _ = snap(f"titlebar-11-theme-{theme}-close-hover")
            ev += evh
            hover = colour_at(evh[0], cl["x"] + 4, cl["y"] + 4, mons)
            park_pointer()
        shots += ev
        red = bool(hover) and int(hover[1:3], 16) > 150 and int(hover[3:5], 16) < 90 and int(hover[5:7], 16) < 90
        if got != colour or red or not hover:
            wrong.append(f"{theme}: bar {got} (expected {colour}), Close hovered {hover}")
    verdict("titlebar-themes", "observed" if not wrong else "failed",
            "the title bar in each theme's panel colour, Close's hover in the theme's (no red)"
            + (f": {wrong}" if wrong else ""), shots)

    d = displays("extend", scale2=150, second=(1920, 1200))
    mons2 = d.get("monitors") or []
    try:
        if len(mons2) < 2:
            verdict("titlebar-mixed-dpi", "not-observable", f"one monitor after extending ({d.get('steps')})")
            return
        sec = next(m for m in mons2 if not m["primary"])
        title_bar_start("custom")
        focus_main_compositor()
        combo("win", "shift", "right")
        time.sleep(2)
        st = wait_for(lambda s: MAIN in frames(s) and inside(frames(s)[MAIN], sec), timeout=6)
        st = uia(dpi=True)
        fm = frames(st).get(MAIN)
        ctl = title_bar_controls(st)
        ev, _ = snap("titlebar-12-scale150", extra="# DISPLAY_RESULT\n" + json.dumps(d))
        ev += head_shot("titlebar-12-monitor2", 1)
        bar = ctl.get("Close")
        on2 = bool(fm) and inside(fm, sec)
        sized = bool(bar) and abs(bar["h"] - 48) <= 2 and abs(bar["w"] - 69) <= 2
        verdict("titlebar-mixed-dpi", "observed" if on2 and fm.get("dpi") == 144 and sized else "failed",
                f"main window {fm and (fm['x'], fm['y'], fm['w'], fm['h'])} on {sec['device']} at {sec['dpi']} dpi: "
                f"{on2}, GetDpiForWindow {fm and fm.get('dpi')}; Close {bar and (bar['w'], bar['h'])} px (46x32 at 150 %: "
                f"69x48) ({'titlebar-12-monitor2.png'})", ev)
        combo("win", "shift", "left")
        time.sleep(1.5)
    finally:
        app_kill()
        r = displays("extend", scale2=100)
        log("displays restored:", [(m["device"], m["w"], m["h"], m["dpi"]) for m in r.get("monitors") or []])


WS_CAPTION, WS_SYSMENU, WS_THICKFRAME, WS_MINIMIZEBOX, WS_MAXIMIZEBOX = 0xC00000, 0x80000, 0x40000, 0x20000, 0x10000
DIALOG_TITLES = {"About": "About HikariSub", "Credits": "Credits"}


def help_item(name):
    focus_main_compositor()
    park_pointer()
    keys("alt+h", 0.8)
    st = uia()
    els = st.get("elements") or []
    items = [e["n"].replace("&", "") for e in els if e["t"] == "MenuItem" and not e["off"] and e["en"]
             and not any(a["t"] in ("MenuBar", "TitleBar") for a in ancestors(els, e))]
    at = next((i for i, n in enumerate(items) if n.startswith(name)), None)
    if at is None:
        log(f"help_item: no {name!r} in {items}")
        keys("escape", 0.4)
        return None
    keys(*(["down"] * (at + 1)), "return", delay=0.3)
    st = wait_for(lambda s: DIALOG_TITLES[name] in frames(s), timeout=6)
    return frames(st).get(DIALOG_TITLES[name])


def dialog_controls(st, title):
    els = st.get("elements") or []
    return {e["n"]: e for e in els if e["win"] == title and not e["off"] and e["n"]
            and not any(a["t"] == "TitleBar" and a["id"] == "TitleBar" for a in ancestors(els, e))}


def caption_buttons(st, title):
    els = st.get("elements") or []
    return sorted({e["n"] for e in els if e["win"] == title and not e["off"] and e["t"] == "Button"
                   and any(a["t"] == "TitleBar" and a["id"] == "TitleBar" for a in ancestors(els, e))})


def window_style(st, f):
    info = next((w for w in st.get("windowInfo") or [] if w.get("hwnd") == (f or {}).get("hwnd")), {})
    return info.get("style") or 0


def rect(f):
    return (f["x"], f["y"], f["w"], f["h"]) if f else None


def step_dialog_frame():
    mons = monitors()
    title_bar_start("auto")
    place_on_primary()
    time.sleep(1.0)
    st = uia()
    main = frames(st).get(MAIN)
    about = help_item("About")
    st = uia(dpi=True)
    about = frames(st).get(DIALOG_TITLES["About"])
    ev0, _ = snap("dialogs-0-about")
    if not about or not main:
        verdict("dialog-frame-window", "failed", f"About did not open as a window of its own (main {rect(main)})", ev0)
        return
    ctl = dialog_controls(st, about["name"] if "name" in about else DIALOG_TITLES["About"])
    style = window_style(st, about)
    has = {n: n in ctl for n in ("Close", "Minimise", "Maximise")}
    style_ok = bool(style & WS_CAPTION == WS_CAPTION and style & WS_SYSMENU) and not style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME)
    top_gap = None
    if ctl.get("Close"):
        top_gap = ctl["Close"]["y"] - about["y"]
    verdict("dialog-frame-window", "observed" if has["Close"] and not has["Minimise"] and not has["Maximise"]
            and style_ok and top_gap is not None and 0 <= top_gap <= 2 else "failed",
            f"About {rect(about)} beside the main window {rect(main)}: its own buttons {has} (Close only on a "
            f"fixed-size dialog), Close {top_gap} px below the window's top (no caption of Windows' above the bar); "
            f"window style {hex(style)} (WS_CAPTION and WS_SYSMENU kept, no WS_MINIMIZEBOX, WS_MAXIMIZEBOX or "
            f"WS_THICKFRAME: {style_ok})", ev0)
    cx = main["x"] + main["w"] // 2 - (about["x"] + about["w"] // 2)
    cy = main["y"] + main["h"] // 2 + 20 - (about["y"] + about["h"] // 2)
    verdict("dialog-frame-centred", "observed" if abs(cx) <= 10 and abs(cy) <= 10 else "failed",
            f"About's centre off the main window's centre + (0, 20) by ({cx}, {cy})", ev0)

    fp = (ctl["Close"]["x"] - 40, ctl["Close"]["y"] + ctl["Close"]["h"] // 2) if ctl.get("Close") else None
    items, enabled = [], set()
    if fp:
        pointer([fp], button="right")
        time.sleep(1.0)
        ev1, st = snap("dialogs-1-system-menu")
        items = [i.replace("&", "") for i in st["popupMenuItems"]]
        # Windows greys what the window's style rules out; the items stay listed.
        enabled = {e["n"].replace("&", "") for e in st.get("elements") or []
                   if e["t"] == "MenuItem" and not e["off"] and e["en"] and e["n"].replace("&", "") in items}
        keys("escape", 0.6)
    else:
        ev1 = []
    sizing = {"Restore", "Size", "Minimize", "Maximize"}
    verdict("dialog-frame-system-menu", "observed" if {"Move", "Close"} <= enabled and not enabled & sizing
            else "failed",
            f"Windows' system menu from a right click on the title bar: {items}, enabled {sorted(enabled)} (Move and "
            f"Close; Restore, Size, Minimize and Maximize greyed on a fixed-size dialog)", ev1)

    W.ui("focus", pid=APP["pid"], window=f"hwnd:{about['hwnd']}")
    time.sleep(0.6)
    r0 = rect(frames(uia()).get(DIALOG_TITLES["About"]))
    combo("win", "up")
    time.sleep(1.2)
    st = uia()
    r1 = rect(frames(st).get(DIALOG_TITLES["About"]))
    ev2, _ = snap("dialogs-2-win-up")
    verdict("dialog-frame-no-maximise", "observed" if r0 and r1 and r1[2:] == r0[2:] else "failed",
            f"Win+Up on About: {r0} -> {r1} (its size kept)", ev2)

    st = uia()
    about = frames(st).get(DIALOG_TITLES["About"])
    ctl = dialog_controls(st, DIALOG_TITLES["About"])
    moved = reopened = None
    if about and ctl.get("Close"):
        sx, sy = ctl["Close"]["x"] - 40, ctl["Close"]["y"] + ctl["Close"]["h"] // 2
        try:
            pointer([(sx, sy)], hold=True)
            time.sleep(0.25)
            pointer([(sx, sy), (sx - 60, sy + 40)], button="none", steps=20, delayMs=30)
            time.sleep(0.4)
        finally:
            release()
            time.sleep(1.0)
        moved = rect(frames(uia()).get(DIALOG_TITLES["About"]))
        keys("escape", 1.0)
        after = uia()
        gone = DIALOG_TITLES["About"] not in frames(after)
        fg = (after.get("foreground") or {}).get("title") or ""
        back = active_frames(after)
        verdict("dialog-frame-focus-back",
                "observed" if gone and back and "HikariSub" in fg and fg not in DIALOG_TITLES.values() else "failed",
                f"Esc on About: the foreground window {fg!r}, active {back}, focus {after.get('focus')}")
        help_item("About")
        time.sleep(0.8)
        reopened = rect(frames(uia()).get(DIALOG_TITLES["About"]))
        ev3, _ = snap("dialogs-3-moved-reopened")
        ok = moved and abs(moved[0] - about["x"] + 60) <= 8 and abs(moved[1] - about["y"] - 40) <= 8 \
            and reopened == moved and gone
        verdict("dialog-frame-move-remembered", "observed" if ok else "failed",
                f"title bar dragged by (-60,+40): {rect(about)} -> {moved}; Esc closed it: {gone}; "
                f"opened again at {reopened}", ev3)
    else:
        verdict("dialog-frame-move-remembered", "failed", "no About or no Close")

    credits = help_item("Credits")
    st = uia()
    both = DIALOG_TITLES["About"] in frames(st) and DIALOG_TITLES["Credits"] in frames(st)
    ev4, _ = snap("dialogs-4-about-and-credits")
    verdict("dialog-frame-modeless", "observed" if both else "failed",
            f"About and Credits open at once: {both} (Credits {rect(credits)})", ev4)
    for t in DIALOG_TITLES.values():
        if t in frames(uia()):
            close_window(t)

    title_bar_start("system")
    place_on_primary()
    time.sleep(1.0)
    help_item("About")
    st = uia(dpi=True)
    about = frames(st).get(DIALOG_TITLES["About"])
    ev5, _ = snap("dialogs-5-system-title-bar")
    ctl = dialog_controls(st, DIALOG_TITLES["About"])
    caps = caption_buttons(st, DIALOG_TITLES["About"])
    verdict("dialog-frame-system", "observed" if about and "Close" in caps and "Minimise" not in ctl
            and not any(n in ctl for n in ("Close", "Maximise")) else "failed",
            f"About with the system's title bar: Windows' caption buttons {caps}, HikariSub's drawn buttons "
            f"{[n for n in ('Close', 'Maximise', 'Minimise') if n in ctl]}", ev5)

    shots, wrong = [], []
    for theme, colour in THEME_PANEL.items():
        title_bar_start("custom", theme)
        place_on_primary()
        time.sleep(1.0)
        help_item("About")
        time.sleep(0.8)
        st = uia()
        about = frames(st).get(DIALOG_TITLES["About"])
        ev, _ = snap(f"dialogs-6-theme-{theme}")
        shots += ev
        ctl = dialog_controls(st, DIALOG_TITLES["About"])
        got = colour_at(ev[0], ctl["Close"]["x"] - 40, ctl["Close"]["y"] + 4, mons) if ctl.get("Close") else None
        if got != colour:
            wrong.append(f"{theme}: title bar {got} (expected {colour})")
    verdict("dialog-frame-themes", "observed" if not wrong else "failed",
            "About's title bar in each theme's panel colour" + (f": {wrong}" if wrong else ""), shots)
    app_kill()


def grid_cell(st):
    return (st.get("focusPath") or "").rsplit(" > ", 1)[-1]


def step_grid_speech():
    app_kill()
    user = LAYOUT["scratch"] + "\\grid-user-" + time.strftime("%Y%m%d-%H%M%S")
    W.task("cmd", env={"CMD": f"New-Item -ItemType Directory -Force '{user}' | Out-Null; "
                              f"Set-Content -Encoding ascii '{user}\\settings.toml' "
                              f"@('version = 1', '', '[grid]', 'foldGenerated = true')"})
    app_start(LAYOUT["root"] + "\\tests\\backends\\karaoke-presets\\05-bounce.out.ass",
              env={"HIKARISUB_USER_DIR": user})
    dismiss_notices()
    nvda_log = nvda_start()
    if not nvda_log:
        verdict("nvda-grid", "error", "nvda-setup did not create the portable copy (nvda-setup.txt)", ["nvda-setup.txt"])
        return
    ev, cells, menu = [], [], {}
    try:
        focus_main_compositor()
        for _ in range(6):
            if panel_of(uia()["focusPath"]) == "Grid":
                break
            combo("f6")
            time.sleep(0.6)
        keys("end", 1.5)
        ev0, st = snap("grid-0-folded")
        cells.append(grid_cell(st))
        keys("right", 2.0)
        ev1, st = snap("grid-1-unfolded")
        cells.append(grid_cell(st))
        keys("left", 2.0)
        cells.append(grid_cell(uia()))
        ev = ev0 + ev1
        how, _ = focus_header_control("TabItem", "Grid", "Grid")
        keys("right", 0.8)
        menu["button"] = uia()["focusPath"]
        keys("space", 1.5)
        ev2, st = snap("grid-2-menu-open")
        menu["items"] = st["popupMenuItems"]
        keys("escape", 1.5)
        menu["after"] = uia()["focusPath"]
        ev += ev2
    finally:
        speech, silent, nev = nvda_stop(nvda_log, "grid")
    (EVID / "grid.json").write_text(json.dumps({"cells": cells, "menu": menu}, indent=1, ensure_ascii=False))
    menu_at = next((i for i, l in enumerate(speech) if "'Grid options'" in l), len(speech))

    def states_spoken(lines):
        return [m.group(1) for l in lines for m in re.finditer(r"'(expanded|collapsed)'", l)]
    run, menu_said = states_spoken(speech[:menu_at]), states_spoken(speech[menu_at:])
    def toggled(said):
        return "expanded" in said and "collapsed" in said[said.index("expanded"):]
    rows = [l for l in speech if re.search(r"'row \d+'", l)]
    verdict("nvda-grid", "observed" if toggled(run) and toggled(menu_said) and silent else "failed",
            f"NVDA on the run's cell: {run}; on the options button: {menu_said}; row speech {rows[:3]} "
            f"({len(speech)} speech lines, silent synthesizer {silent}); focused cells {cells}; menu {menu}",
            ["grid.json"] + nev + ev)


STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "outputs": step_outputs, "nvda": step_nvda, "menutext": step_menu_from_text,
         "tests": step_test_executables, "a11y": step_a11y, "header": step_header, "dpi": step_dpi, "dark": step_dark,
         "layouts": step_layouts, "tabs": step_tabs, "videofs": step_video_fullscreen,
         "snap": step_snap, "tags": step_tags, "statusbar": step_statusbar, "dialogs": step_dialogs,
         "reference": step_reference, "titlebar": step_title_bar,
         "dialogframe": step_dialog_frame, "gridspeech": step_grid_speech}


def main():
    global W, LOG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="*", help=f"steps (default: all but dpi): {', '.join(STEPS)}")
    ap.add_argument("--config", default=str(ROOT / "winix.yaml"))
    ap.add_argument("--vm", default="dev")
    ap.add_argument("--dpi", action="store_true", help="allow the dpi step (changes the guest's display scale "
                                                       "to 150 %% and back to 100 %%)")
    a = ap.parse_args()
    wanted = a.steps or [s for s in STEPS if s != "dpi" or a.dpi]
    unknown = [s for s in wanted if s not in STEPS]
    if unknown:
        raise SystemExit(f"unknown steps {unknown}; steps: {', '.join(STEPS)}")
    if "dpi" in wanted and not a.dpi:
        raise SystemExit("the dpi step changes the guest's display scale; pass --dpi to run it")
    EVID.mkdir(parents=True, exist_ok=True)
    results.update(json.loads((EVID / "results.json").read_text()) if (EVID / "results.json").exists() else {})
    LOG = open(EVID / "steps.log", "a")
    W = Winix(a.config, a.vm)
    ready = W.call("vm", "ready", a.vm) or {}
    if not ready.get("desktopReady") or ready.get("activeJob"):
        raise SystemExit(f"the VM desktop is not ready or busy: {json.dumps(ready)[:300]}")
    prepare()
    try:
        for name in wanted:
            log(f"--- windows: {name}")
            try:
                STEPS[name]()
            except Exception as e:
                import traceback
                traceback.print_exc()
                verdict(name, "error", f"harness error: {e!r}")
    finally:
        release()
        app_kill()
        if PROFILE["original"]:
            _, lines = W.task("gate-win-restore", env={"GATE_PROFILE_FROM": PROFILE["original"]})
            log("profile:", " | ".join(l for l in lines if l.startswith(("restored", "set aside"))))


if __name__ == "__main__":
    main()
