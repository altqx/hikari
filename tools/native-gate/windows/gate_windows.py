#!/usr/bin/env python3
"""D1 native gate on Windows: HikariSub on the Winix VM desktop.

  WINIX_OWNER=<lease owner> gate_windows.py [--dpi] [--config winix.yaml] [--vm dev] [STEP...]

The Windows half of tools/native-gate/gate.py, with the same step names and
the same evidence layout under out/native-gate-evidence/windows/: one PNG and
one TXT (the app's windows and its UI Automation view as JSON) per
observation, steps.log, and results.json with a verdict per gate item:
observed, failed or not-observable (with the reason).

Everything goes through `winix ui` (winix 0.2.0):
  keys      `ui keys` (SendInput with scan codes, the extended flag on navigation keys)
  text      `ui text` (Unicode input)
  pointer   `ui pointer` / `ui calibrate` (the VM's USB HID tablet, host-side QMP)
  view      one `ui batch`: `windows --pid`, `state` (focus, its ancestors, the
            cursor) and `tree --pid --window N` for every window of the app
  focus     `ui focus --pid --window hwnd:N`
  app       `ui launch --exe --arg --env --cwd --no-console`, `ui kill`
  monitors  `ui monitors`, `ui screenshot [--monitor N]`
Guest tasks (gate.ps1, uia.ps1, display.ps1, nvda.ps1) remain for what winix
does not do: the profile (Qt ignores the APPDATA variables `--fresh-profile`
sets), pressing controls through UIA patterns only, GetDpiForWindow, the
borderless fullscreen and display changes, NVDA, the test executables. Task
parameters go through `run TASK --env K=V`. The VM definition is never changed.
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
# GATE_EVIDENCE_WIN: another evidence directory (a card's own run).
EVID = Path(os.environ.get("GATE_EVIDENCE_WIN") or ROOT / "out" / "native-gate-evidence" / "windows")
PANELS = ["Video", "Audio", "Line editor", "Grid", "Reference", "Timing", "Search"]
MAIN = "HikariSub"
# Windows maps the absolute HID tablet to the primary monitor only (step
# pointer measures it), so pointer coordinates map over the primary.
POINTER_MAP = "primary"

W = None        # Winix
LAYOUT = {}     # gate-win-prepare's GATE_LAYOUT
APP = {"pid": None, "log": None}
# The profile the first fresh() set aside: put back when the run ends.
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


# ---------------------------------------------------------------- Winix
class Winix:
    """`winix --json`; the lease owner comes from WINIX_OWNER."""

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
        """Runs a winix.yaml task to its end; returns (job record, output lines)."""
        args = ["run", name, "--wait"]
        for k, v in (env or {}).items():
            args += ["--env", f"{k}={v}"]
        job = next((j for j in reversed(self.lines(*args, timeout=timeout)) if j.get("type") == "job" or j.get("state")), {})
        if not job.get("id"):
            raise RuntimeError(f"task {name} did not start: {job}")
        text = (self.call("job", "logs", job["id"], "--all") or {}).get("text", "")
        lines = [l[6:] if l.startswith(("[out] ", "[err] ")) else l for l in text.splitlines()]
        return job, lines

    def pull(self, guest, host):
        return self.call("pull", guest, str(host))


def line_value(lines, prefix):
    return next((l[len(prefix):] for l in lines if l.startswith(prefix)), None)


def sections(lines):
    """"== name" sections of a task's output."""
    out, current = {}, None
    for line in lines:
        if line.startswith("== "):
            current = line[3:].strip()
            out[current] = []
        elif current:
            out[current].append(line)
    return out


# ---------------------------------------------------------------- input
def keys(*seq, delay=0.35):
    """Chords ("alt+w", "down") and pauses (seconds): each run of chords is
    one `ui keys` call, DELAY seconds between chords."""
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
    """The tablet along PATH ([(x, y), ...]): pressed at the first point and
    released at the last (button "none": moves only); hold=True keeps it down."""
    r = W.ui("pointer", path=";".join(f"{x},{y}" for x, y in path), map=POINTER_MAP, **kw)
    if isinstance(r, dict) and r.get("error"):
        log("pointer", path, kw, "->", r)
    return r


def park_pointer():
    """The tablet cursor to the primary monitor's bottom-right corner (the
    taskbar's clock): a menu that opens under the cursor takes its current item
    from the hover, which would turn the keyboard's Down and Right into other
    moves (Right then goes to the next menu of the bar)."""
    m = next((x for x in monitors() if x["primary"]), None)
    if m:
        W.ui("pointer", path=f"{m['x'] + m['w'] - 30},{m['y'] + m['h'] - 20}", button="none", map="primary")


def release():
    return W.ui("pointer", release=True)


def double_click(x, y):
    """Two tablet clicks, one `ui pointer` call each (winix has no double-click
    at a point): a double-click only when the second press comes within the
    double-click time (500 ms) of the first; a call takes about 0.3 s, more
    when the VM is busy. Returns the seconds between the two calls' ends."""
    pointer([(x, y)])
    t1 = time.monotonic()
    pointer([(x, y)])
    return round(time.monotonic() - t1, 2)


def double_click_until(x, y, done, title=None):
    """A tablet double-click at X,Y, again (up to three times) while DONE(view)
    is false; then, as a last resort, `ui doubleclick` on the UIA title bar
    TITLE ((automationId, name, window): SendInput mouse, not the tablet).
    Returns (view, how, gaps)."""
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
    """The guest's monitors (display.ps1): unchanged, or extended / detached
    (SECOND: the second monitor's (w, h)), and the second monitor's scale."""
    env = {"GATE_DISPLAY": mode or "status"}
    if second:
        env["GATE_WIDTH2"], env["GATE_HEIGHT2"] = second
    if scale2:
        env["GATE_SCALE2"] = scale2
    _, lines = W.task("gate-win-display", env=env)
    raw = line_value(lines, "DISPLAY_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


# ---------------------------------------------------------------- UI Automation
def frame_id(name):
    """The main window is titled "<document> - HikariSub" once a Document is open."""
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
    """uia.ps1: controls pressed through UIA patterns, and GetDpiForWindow."""
    env = {}
    if do:
        env["GATE_UIA_DO"] = json.dumps(do)
    if settle_ms is not None:
        env["GATE_UIA_SETTLE_MS"] = settle_ms
    try:
        _, lines = W.task("gate-win-uia", env=env, timeout=600)
        raw = line_value(lines, "UIA_JSON_B64 ")
        return json.loads(base64.b64decode(raw).decode("utf-8")) if raw else {"error": lines[-10:]}
    except Exception as e:  # keep the step going; the result says why it is empty
        return {"error": repr(e)}


def flatten(node, parent, depth, win, els):
    """A `ui tree` node into the element list; owned windows (walked on their
    own) are skipped where they appear under their owner."""
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
    """The app's UI Automation view, shaped like atspi_tool.py json (frames,
    panels, focus, focusPath, texts, labels) plus the raw elements: one
    `ui batch` (windows, state, a tree per window). DO presses controls first
    (uia.ps1); DPI adds GetDpiForWindow per window (uia.ps1)."""
    pressed = guest_uia(do, settle_ms) if do or dpi else {}
    st = {"frames": [], "windows": [], "elements": [], "panels": [], "focus": None, "focusPath": None,
          "texts": {}, "labels": [], "popupMenuItems": [], "videoLabels": [], "combos": {},
          "actions": pressed.get("actions") or [], "cursor": None, "foreground": None}
    pid = APP["pid"]
    if not pid:
        st["error"] = "no app running"
        return st
    steps = [{"action": "windows", "pid": pid}, {"action": "state"}]
    steps += [{"action": "tree", "pid": pid, "window": i, "depth": 12, "limit": 1000} for i in range(6)]
    for attempt in range(3):
        r = W.batch(steps)
        res = r.get("steps") or []
        # A window changing under the walk (a menu opening) can fail a step: once more.
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
    # Every titled window: the main window and the windows it owns (floating
    # panels, the placement window, dialogs). Nameless windows are popups.
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
    """The innermost panel on a focus path."""
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


# ---------------------------------------------------------------- app control
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


def app_start(doc=None):
    """HikariSub from the VM's build with the Qt SDK it links on PATH; its
    output goes to the winix launch log."""
    r = W.ui("launch", exe=LAYOUT["app"], arguments=[doc] if doc else [], cwd=LAYOUT["docs"], noConsole=True,
             env={"PATH": LAYOUT["qt_bin"] + ";" + LAYOUT["path"], "QT_FORCE_STDERR_LOGGING": "1"})
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
    """Opening a Document may raise the spelling notice; press its OK."""
    st = uia(do=[{"action": "invoke", "name": "OK", "type": "Button", "all": True}], settle_ms=500)
    others = [f["id"] for f in st["frames"] if f["id"] != MAIN]
    if others:
        log("windows besides the main one after the notices:", others)


def set_focus(name):
    """The keyboard focus onto the app's element NAME (`ui setfocus`, UIA SetFocus)."""
    r = W.ui("setfocus", pid=APP["pid"], name=name)
    if r.get("error"):
        log("setfocus", name, "->", r)
    time.sleep(0.5)
    return r


def focus_main_compositor():
    """The main window in the foreground (`ui focus`)."""
    main = main_window()
    if not main:
        return False
    W.ui("focus", pid=APP["pid"], window=f"hwnd:{main['hwnd']}")
    time.sleep(0.8)
    return True


def focus_main():
    """F6 until a panel of the main window has the focus (the shell activates
    the window of the panel it moves to); else `ui focus`."""
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
    """Alt+W opens the View menu ("Vie&w": Alt+V stays with legacy's &Video).
    Retried until the View menu (its "Move panel…" item) shows."""
    park_pointer()
    for _ in range(3):
        keys("alt+w", 0.6)
        st = uia()
        if any("Move panel" in (n or "") for n in st["popupMenuItems"]):
            return True
        keys("escape", 0.3)
    log("open_view: the View menu did not open")
    return False


# The key that opens a submenu: Right, as on Linux. Should Right not open
# View > Panels, float_panel() records that once and switches to Return.
SUBMENU = {"key": "right", "checked": False}


def panel_menu(name, item):
    """View > Panels > NAME > ITEM (Show, Hide, Float, Dock) from the keyboard."""
    open_view()
    k = SUBMENU["key"]
    seq = ["down", k, 0.3] + ["down"] * PANELS.index(name) + [k, 0.3]
    seq += ["down"] * ["Show", "Hide", "Float", "Dock"].index(item) + ["return"]
    keys(*seq)
    time.sleep(1.2)


def close_window(title):
    W.ui("close", pid=APP["pid"], window=title)
    time.sleep(0.5)


def float_panel(name, tag):
    """View > Panels > NAME > Float, or View > Move panel… with Place: Float."""
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
    open_view()
    keys("down", "down", "return", 1.2, "home", *(["down"] * PANELS.index(name)), "tab", "home",
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


# ---------------------------------------------------------------- steps
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
        open_view()
        k = SUBMENU["key"]
        keys("down", k, 0.3, "down", "down", k, 0.3, "down", "down", "return")
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


def step_move_panel():
    fresh()
    open_view()
    keys("down", "down", "return", 1.2)
    st = wait_for(lambda s: "Move panel" in frames(s))
    combos_open = st["combos"]
    ev0, st = snap("move-panel-0-open", extra="# combo boxes\n" + json.dumps(combos_open, ensure_ascii=False))
    if "Move panel" not in frames(st):
        verdict("keyboard-move-panel", "failed", "View > Move panel… did not open the placement window", ev0)
        return
    verdict("move-panel-defaults", "observed" if combos_open and all(v for v in combos_open.values()) else "failed",
            f"placement window on open (Grid focused): {combos_open}", ev0)
    # Panel: Grid -> Audio (Up x2); Place: Left of (Down); Next to: Grid (Up x6, Down x3); Move.
    keys("up", "up", "tab", "down", "tab", *(["up"] * 6), *(["down"] * 3), 0.3)
    combos_set = uia()["combos"]
    keys("tab", "space", 1.5)
    ev1, st = snap("move-panel-1-left-of-grid", extra="# combo boxes before Move\n" + json.dumps(combos_set, ensure_ascii=False))
    pos = {p["id"]: p for p in st["panels"]}
    a, g = pos.get("Audio"), pos.get("Grid")
    ok = a and g and a["frame"] == g["frame"] == MAIN and a["x"] < g["x"] and abs(a["y"] - g["y"]) < 3
    verdict("keyboard-move-panel", "observed" if ok else "failed",
            f"Move panel ({combos_set}) -> Audio {a and (a['x'], a['y'], a['w'], a['h'])}, "
            f"Grid {g and (g['x'], g['y'], g['w'], g['h'])}; focus {st['focusPath']}", ev0 + ev1)
    close_window("Move panel")


def title_point(st, pid, frame_name, dx=120):
    """A point on PID's title bar, DX px in from its left edge: the UIA
    TitleBar KDDockWidgets exposes (named after the panel), else the row of
    its Float/Dock button, else (neither found) the window's native caption."""
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
    """(automationId, name, window hwnd) of PID's UIA title bar in FRAME_NAME, for `ui doubleclick`."""
    tb = next((t for t in st.get("elements") or [] if t["t"] == "TitleBar" and not t["off"]
               and panel_id(t["n"]) == pid and frame_id(t["win"]) == frame_name), None)
    f = frames(st).get(frame_name)
    return (tb["id"], tb["n"], f"hwnd:{f['hwnd']}") if tb and f else None


def calibrate_pointer():
    """Where the guest cursor lands (`ui calibrate`) on two targets per
    monitor, with the tablet mapped over the whole desktop (virtual) and over
    the primary monitor: the mapping that hits the targets is used."""
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
    """Float button, double-click on title bars and drag-to-dock with real pointer input (the VM's HID tablet)."""
    fresh()
    # The main window opens 1280x800, larger than the 1024x768 primary, and
    # the tablet reaches the primary only: maximized, its title bars are there.
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
    btn = next((b for b in find(st, "Button", "Float Audio") if frame_id(b["win"]) == MAIN), None)
    audio = panel(st, "Audio")
    if btn:
        tx, ty, aim = btn["x"] + btn["w"] // 2, btn["y"] + btn["h"] // 2, "UIA bounds of 'Float Audio'"
    elif audio:
        tx, ty, aim = audio["x"] + audio["w"] - 22, audio["y"] - 16, "30 px from the Audio panel's right edge"
    else:
        verdict("pointer-float-button", "not-observable", "neither the 'Float Audio' button nor the Audio panel found")
        return
    pointer([(tx, ty)])
    time.sleep(1.2)
    st = wait_for(lambda s: "Audio" in frames(s))
    ev, st = snap("ptr-1-float-button")
    verdict("pointer-float-button", "observed" if "Audio" in frames(st) else "failed",
            f"tablet click on Audio's float button at {tx},{ty} ({aim}): own window={'Audio' in frames(st)}",
            ev + ["pointer-calibration.json"])
    # Double-click the docked Video title bar: floats; double-click its floating title bar: docks.
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
    # Drag the floating Audio panel by its title bar onto the Grid's centre.
    st = uia()
    src = title_point(st, "Audio", "Audio", dx=150)
    grid = panel(st, "Grid")
    if not src or not grid or grid["frame"] != MAIN:
        verdict("pointer-drag-dock", "not-observable", f"could not locate the floating Audio window ({src}) or the Grid")
        return
    sx, sy, where = src
    gx, gy = grid["x"] + grid["w"] // 2, grid["y"] + grid["h"] // 2 - 15
    try:
        # Pressed on the title, along to the Grid's centre in 30 steps, a small
        # wiggle there, and held while the drop indicators are captured.
        pointer([(sx, sy)], hold=True)
        time.sleep(0.25)
        pointer([(sx, sy), (gx + 3, gy + 3), (gx, gy)], button="none", steps=30, delayMs=40)
        time.sleep(0.7)
        ev3, _ = snap("ptr-4-drag-over-grid")  # the drop indicators, button still held
    finally:
        release()
        time.sleep(1.5)
        pointer([(gx + 5, gy + 5)], button="none")
    ev4, st = snap("ptr-5-dropped")
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    verdict("pointer-drag-dock", "observed" if docked else "failed",
            f"tablet drag of floating Audio by its title ({where}) from {sx},{sy} to the Grid centre {gx},{gy} and "
            f"release: docked={docked}, windows {list(frames(st))} (drop indicators: ptr-4-drag-over-grid.png)", ev3 + ev4)


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
        # The panel's own Next frame button, pressed through UI Automation.
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
        open_view()
        k = SUBMENU["key"]
        keys("down", k, 0.3, k, 0.3, "down", "down", "return")
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
    # Close the main window the way a user does: its title bar, then Alt+F4
    # (WM_CLOSE reaches onClosing, which saves the layout).
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
    ok = "Audio" in frames(st)
    verdict("layout-persistence", "observed" if ok else "failed",
            f"main window closed with Alt+F4 (process ended: {gone}); layout files: {cfg.strip()!r}; "
            f"after restart Audio floating: {ok}", ev0 + ev1)


def fullscreen(mode):
    _, lines = W.task("gate-win-fullscreen", env={"GATE_FULLSCREEN": mode})
    raw = line_value(lines, "FULLSCREEN_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def step_fullscreen():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    float_panel("Line editor", "fullscreen")
    # Windows has no compositor fullscreen request for another application's
    # window and HikariSub has no fullscreen command: the main window is made
    # borderless over the whole monitor (taskbar included), as a borderless
    # fullscreen tool does. Windows treats such a window as fullscreen.
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
    app_kill()  # the tests' windows need the desktop to themselves
    job, lines = W.task("gate-win-tests", timeout=3000)
    text = "\n".join(lines)
    (EVID / "test-executables.txt").write_text(f"# winix job {job.get('id')} ({job.get('state')})\n{text}\n")
    exits = re.findall(r"^### (\S+) .*\(exit (.+)\)$", text, re.M)
    ok = len(exits) == 4 and all(code == "0" for _, code in exits)
    summary = "; ".join(re.findall(r"Totals: [^,]+, [^,]+, [^,]+", text))
    skips = re.findall(r"SKIP\s*:.*", text)
    unexpected = [k for k in skips if "the compositor places windows" not in k]
    log("tests", exits, summary, skips)
    verdict("test-executables", "observed" if ok and not unexpected else "failed",
            f"exits {exits}; {summary}; skips: {skips}", ["test-executables.txt"])


def step_nvda():
    """NVDA (portable, "No speech" synthesizer, io log level) while F6 moves
    through the panels and a panel is floated from the View menu."""
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
    try:
        focus_main_compositor()
        f6_walk(4)
        open_view()
        k = SUBMENU["key"]
        keys("down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, "return", 2.0)
        f6_walk(3)
        ev, st = snap("nvda-0-after-float")
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
    spoke = [p for p in ("Video", "Audio", "Line editor", "Grid", "Panels", "Float") if any(p in l for l in speech)]
    evidence = ["nvda-speech.txt", "nvda-output.txt"] + (["nvda.log"] if got_log else []) + ev
    verdict("nvda", "observed" if speech else "failed",
            f"{len(speech)} speech lines; panel/menu names spoken: {spoke}; synthesizer lines {synth[:3]} "
            f"(silent: {silent}); floating windows after the menu {list(frames(st))} (nvda-speech.txt; full log nvda.log)",
            evidence)
    verdict("nvda-float-from-menu", "observed" if "Line editor" in frames(st) else "failed",
            f"with NVDA running, View > Panels > Line editor > Float from the keyboard: windows {list(frames(st))}", ev)


def inside(f, m):
    """Whether window F's centre lies on monitor M."""
    cx, cy = f["x"] + f["w"] // 2, f["y"] + f["h"] // 2
    return m["x"] <= cx < m["x"] + m["w"] and m["y"] <= cy < m["y"] + m["h"]


def head_shot(name, monitor):
    """One monitor's pixels (`ui screenshot --monitor N`; the worker is per-monitor DPI aware)."""
    out = EVID / f"{name}.png"
    W.screenshot(out, monitor)
    return [out.name] if out.exists() else []


def step_outputs():
    """Mixed DPI and monitor removal on the VM's two QXL monitors: a floating
    panel (Audio) dragged with the real pointer onto the second monitor at
    150 % (primary 100 %), then that monitor removed from the desktop."""
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
        src = title_point(st, "Audio", "Audio", dx=100)
        if src and before:
            sx, sy, _ = src
            tx, ty = sec["x"] + sec["w"] // 3, sec["y"] + 120
            # Can the tablet reach the second monitor (`ui calibrate` over the whole desktop)?
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
                # Windows maps the tablet to the primary only, so the window
                # moves the keyboard way: active (a click on its title), then
                # Win+Shift+Right.
                pointer([(sx, sy)])
                time.sleep(0.5)
                keys("win+shift+right", 1.5)
                moved_by = (f"Win+Shift+Right after a click on its title (the tablet does not reach the second "
                            f"monitor: `ui calibrate` {tx},{ty} landed at {cal.get('actual')})")
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
        # Monitor removal with the panel on the second monitor.
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
    """The one monitor's display scale changed to 150 % live (a guest setting)
    with a floating panel, then restored to 100 %."""
    fresh(LAYOUT["episode"])
    dismiss_notices()
    how = float_panel("Line editor", "dpi")
    ev0, st0 = snap("dpi-0-at-100")
    res = dpi_set(150)
    try:
        if res.get("after") != 150:
            verdict("dpi-scale-change", "not-observable",
                    f"the guest did not change its scale to 150 % without signing out: {res}", ev0)
            return
        time.sleep(3)
        ev1, st = snap("dpi-1-at-150", extra="# DPI_RESULT\n" + json.dumps(res))
        set_focus("Line text")
        keys("end")
        type_text(" hidpi")
        time.sleep(0.8)
        txt = uia()["texts"].get("Line text")
        both = MAIN in frames(st) and "Line editor" in frames(st)
        verdict("dpi-scale-change", "observed" if both and txt and txt.endswith(" hidpi") else "failed",
                f"scale 100 -> {res.get('after')} % live ({res}); floating Line editor (floated by {how}) and main "
                f"window still shown: {both}; typing there gives {txt!r} (dpi-1-at-150*.png)",
                ev0 + ev1)
    finally:
        back = dpi_set(100)
        time.sleep(3)
        ev2, st = snap("dpi-2-restored", extra="# DPI_RESULT\n" + json.dumps(back))
        verdict("dpi-restored", "observed" if back.get("after") == 100 else "failed",
                f"scale restored: {back}", ev2)


def step_a11y():
    """What a screen reader finds of the docking controls and the Grid through
    UI Automation; the buttons pressed through UIA float and dock a panel."""
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
    names = ["Float Audio", "Close Audio", "Float Video", "Close Video", "Float Line editor", "Close Line editor",
             "Float Grid", "Close Grid"]
    found = {n: bool(find(st, "Button", n)) for n in names}
    press = find(st, "Button", "Float Audio")
    st = uia(do=[{"action": "invoke", "name": "Float Audio", "type": "Button"}])
    out = st["actions"]
    by_invoke = bool(out) and out[0].get("done") == ["Invoke"]
    st = wait_for(lambda s: "Audio" in frames(s))
    floated = "Audio" in frames(st)
    ev0, st = snap("a11y-0-float-audio-by-uia")
    dock_btn = find(st, "Button", "Dock Audio")
    uia(do=[{"action": "invoke", "name": "Dock Audio", "type": "Button"}])
    st = wait_for(lambda s: "Audio" not in frames(s))
    docked = "Audio" not in frames(st)
    verdict("title-bar-buttons-accessible",
            "observed" if all(found.values()) and press and by_invoke and floated and dock_btn and docked
            else "failed",
            f"named buttons {found}; pressed through the Invoke pattern: {by_invoke}; invoked -> "
            f"own window={floated} ({out}); then 'Dock Audio' {[d['path'] for d in dock_btn[:1]]} -> docked={docked}",
            ["a11y-tree.txt"] + ev0)
    focus_main()
    panel_menu("Timing", "Show")
    time.sleep(1)
    st = uia()
    tabs = {n: find(st, "TabItem", n) for n in ("Line editor", "Timing")}
    tab_buttons = {n: bool(find(st, "Button", n)) for n in ("Float Timing", "Close Timing", "Float Line editor",
                                                            "Close Line editor")}
    ev1, st = snap("a11y-1-tabs")
    uia(do=[{"action": "invoke", "name": "Float Timing", "type": "Button"}])
    st = wait_for(lambda s: "Timing" in frames(s))
    tab_floated = "Timing" in frames(st)
    ev2, st = snap("a11y-2-float-timing-tab")
    (EVID / "a11y-tabs.txt").write_text(json.dumps(
        {"tabs": {n: [h["path"] for h in hits] for n, hits in tabs.items()}, "buttons": tab_buttons,
         }, indent=1, ensure_ascii=False))
    verdict("tabs-accessible",
            "observed" if all(tabs.values()) and all(tab_buttons.values()) and tab_floated else "failed",
            f"tab items { {n: bool(h) for n, h in tabs.items()} }; tab buttons {tab_buttons}; "
            f"invoked 'Float Timing' -> own window={tab_floated}", ev1 + ev2 + ["a11y-tabs.txt"])


# ---------------------------------------------------------------- V5 (#184)
def fs_frames(st, mons):
    """(fullscreen, main): the fullscreen video window carries the main
    window's title; it is the one covering a whole monitor."""
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
    """V5 (#184): F in the Video panel shows the video fullscreen on the main
    window's monitor; Space and Left work there; Esc leaves with the main
    window's geometry and the keyboard focus as they were; the context menu's
    "Open in full screen on monitor 2" puts it on the second monitor, at 100 %
    and at 150 % (mixed DPI: GetDpiForWindow), and Esc brings the docked video
    back. Real keys (SendInput); the menu items through UI Automation."""
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
    # UI Automation shows no slider value or label text of Qt Quick's here:
    # the monitor's pixels before and after Right (cfr.mkv's block moves).
    time.sleep(1.0)
    shot_a = head_shot("videofs-1b-paused", m0)
    # Left (GLOBAL_PREVIOUS_FRAME): a slow guest can play the two-second
    # clip to its last frame before the second Space, where Right has no
    # frame to go to.
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
    # Mixed DPI: the second monitor at 150 % (Windows offers it from about 1920x1200).
    d2 = displays("extend", scale2=150, second=(1920, 1200))
    log("displays at 150 %:", json.dumps(d2)[:600])
    second_monitor("-150", 150)
    app_kill()
    displays("extend", scale2=100)


STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "outputs": step_outputs, "nvda": step_nvda, "menutext": step_menu_from_text,
         "tests": step_test_executables, "a11y": step_a11y, "dpi": step_dpi,
         "videofs": step_video_fullscreen}


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
            except Exception as e:  # keep going: one broken step must not hide the others
                import traceback
                traceback.print_exc()
                verdict(name, "error", f"harness error: {e!r}")
    finally:
        release()  # never leave the left button held
        app_kill()
        if PROFILE["original"]:
            _, lines = W.task("gate-win-restore", env={"GATE_PROFILE_FROM": PROFILE["original"]})
            log("profile:", " | ".join(l for l in lines if l.startswith(("restored", "set aside"))))


if __name__ == "__main__":
    main()
