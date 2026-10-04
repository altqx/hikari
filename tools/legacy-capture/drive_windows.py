#!/usr/bin/env python3
"""S3 on Windows: the legacy automation capture driven on the Winix VM desktop.

    drive_windows.py --plan plan.json --out <dir>/observations.json [--config winix.yaml]

The Linux route (drive.py) never completes a dialog case: the legacy Linux
build deadlocks on any macro that shows a dialog. This route runs the legacy
Windows release (v0.0.1-rc.1, built from the baseline) in the VM instead.
winix.yaml's legacy-win-setup task downloads and checks the release zip and
prepares two copies (tools/legacy-capture/windows/setup.ps1); this script
then drives the interactive desktop through `winix ui`:

  1. the copy as shipped, launched on a scratch document: popups and a
     screenshot at startup with the bundled Autoload scripts;
  2. the probe copy (Autoload emptied into a corpus, capture-probe.lua and its
     .cfg installed, its macros bound to Ctrl+Shift+F1..F12 in Config): the
     load-time corpus capture, then each plan step's macro by its hotkey, the
     dialog's windows and a screenshot, the step's keys, and the probe's JSON
     line read back through the legacy-win-output task.

The probe reads its settings from capture-probe.cfg because a desktop launch
cannot set the environment. Desktop keys go to the foreground window, so the
main window is clicked (its status bar) before each hotkey. A step whose
macro never answers leaves the app as it is for the record (responding or
not, its thread states), then the app is ended and the next step starts a
fresh one. The observations have drive.py's shape (part "probe", steps with
case/status/dialog_windows/result/screenshot); they record what the old app
did and are not expectations for the rewrite.
"""
import argparse
import base64
import json
import subprocess
import time
from pathlib import Path

LEGACY_COMMIT = "20d647c4c769ab7f5d383cf3c1c33f03876a94e9"
MAIN_TITLE = "HikariSub v"


class Winix:
    def __init__(self, config):
        self.base = ["winix", "--json", "--config", str(config)]

    def call(self, *args, timeout=900):
        p = subprocess.run(self.base + list(args), capture_output=True, text=True, timeout=timeout)
        lines = p.stdout.strip().splitlines()
        try:
            return json.loads(lines[-1]) if lines else None
        except json.JSONDecodeError:
            return {"error": "unparsed", "message": p.stdout[-500:] + p.stderr[-500:]}

    def ui(self, action, **args):
        return self.call("ui", action, "--args", json.dumps(args))

    def screenshot(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        return self.call("ui", "screenshot", "--output", str(path))

    def task(self, name):
        """Runs a winix.yaml task to its end; returns (job record, output lines)."""
        job = None
        for _ in range(3):  # `run --json` occasionally answers without a job id
            job = self.call("run", name, "--wait")
            if job and job.get("id"):
                break
        if not job or not job.get("id"):
            raise RuntimeError(f"task {name} did not start: {job}")
        text, offset = "", 0
        while True:
            chunk = self.call("job", "logs", job["id"], "--offset", str(offset))
            text += chunk.get("text", "")
            offset = chunk.get("nextOffset", offset)
            if chunk.get("end", True):
                break
        lines = [l[6:] if l.startswith(("[out] ", "[err] ")) else l for l in text.splitlines()]
        return job, lines


def sections(lines):
    """legacy-win-output's "== name" sections."""
    out, current = {}, None
    for line in lines:
        if line.startswith("== "):
            current = line[3:].strip()
            out[current] = []
        elif current:
            out[current].append(line)
    return out


class Legacy:
    def __init__(self, winix, workdir, scratch):
        self.w = winix
        self.workdir = workdir
        self.scratch = scratch
        self.pid = None
        self.layout = None

    def setup(self):
        job, lines = self.w.task("legacy-win-setup")
        layout = next((l[len("LEGACY_LAYOUT "):] for l in lines if l.startswith("LEGACY_LAYOUT ")), None)
        if job.get("state") != "succeeded" or not layout:
            raise RuntimeError("legacy-win-setup failed:\n" + "\n".join(lines[-30:]))
        self.layout = json.loads(layout)
        self.layout["setup_job"] = job["id"]
        return self.layout

    def windows(self):
        """The legacy process's top-level windows (UI Automation), or None if listing failed."""
        listed = self.w.ui("windows")
        if not isinstance(listed, list):
            return None
        return [w for w in listed if w.get("pid") == self.pid]

    def launch(self, exe, doc):
        self.kill()
        started = self.w.ui("launch", exe=exe, arguments=[doc])
        self.pid = started.get("pid") if isinstance(started, dict) else None
        deadline = time.time() + 90
        while time.time() < deadline:
            mine = self.windows() or []
            main = [w for w in mine if MAIN_TITLE in (w.get("name") or "")]
            if main:
                self.main = main[0]
                return True
            time.sleep(2)
        return False

    def popups(self):
        mine = self.windows()
        if mine is None:
            return None
        return [w.get("name") for w in mine if MAIN_TITLE not in (w.get("name") or "")]

    def focus_main(self):
        # Desktop keys go to the foreground window: click the main window's
        # status bar (no command there) to bring it forward.
        b = self.main["bounds"]
        return self.w.ui("click", x=b["x"] + b["width"] // 2, y=b["y"] + b["height"] - 12)

    def output(self):
        _, lines = self.w.task("legacy-win-output")
        s = sections(lines)
        capture = base64.b64decode("".join(s.get("capture", []))).decode("utf-8") if s.get("capture") else ""
        processes = [l for l in s.get("processes", []) if l.strip()]
        return {
            "processes": processes,
            "responding": None if not processes else "responding=True" in processes[0],
            "config_files": [l for l in s.get("config", []) if l.strip()],
            "hotkeys": "\n".join(s.get("hotkeys", [])).strip(),
            "capture": [json.loads(l) for l in capture.splitlines() if l.strip()],
            "capture_text": capture,
        }

    def thread_states(self):
        _, lines = self.w.task("legacy-win-stacks")
        return [l for l in lines if l.startswith(("pid ", "thread ", "cdb", "no HikariSub"))]

    def dump(self):
        """A minidump of the app (thread stacks) through the legacy-win-dump task."""
        job, lines = self.w.task("legacy-win-dump")
        art = self.w.call("job", "artifacts", job["id"], "--output", str(self.scratch))
        return {"job": job["id"], "log": [l for l in lines if l.strip()], "artifact": (art or {}).get("path")}

    def kill(self):
        self.w.task("legacy-win-kill")
        self.pid = None

    def shot(self, name):
        path = self.workdir / "screens" / f"{name}.png"
        self.w.screenshot(path)
        return str(path.relative_to(self.workdir))


def send_keys(legacy, keys):
    sent = []
    for chord in keys:
        if chord.startswith("type:"):
            # Desktop keys are virtual-key chords; text goes into an edit through
            # UI Automation (its value is set, not typed key by key).
            target = None
            for w in legacy.windows() or []:
                if MAIN_TITLE in (w.get("name") or ""):
                    continue
                tree = legacy.w.ui("tree", pid=legacy.pid, depth=6)
                target = find_edit(tree)
                break
            if target:
                r = legacy.w.ui("type", pid=legacy.pid, automationId=target, text=chord[5:])
            else:
                r = {"error": "no-edit", "message": "no dialog edit with an automation id to type into"}
        else:
            r = legacy.w.ui("keys", keys=chord.upper())
        sent.append({"chord": chord, "reply": r})
        time.sleep(0.5)
    return sent


def find_edit(node):
    if not isinstance(node, dict):
        return None
    e = node.get("element", {})
    if e.get("controlType") == "Edit" and e.get("automationId"):
        return e["automationId"]
    for child in node.get("children", []):
        found = find_edit(child)
        if found:
            return found
    return None


def bundled_startup(legacy, layout):
    rec = {"part": "bundled-autoload-startup", "autoload_scripts": layout["bundled_autoload"],
           "status": "timeout", "popups": []}
    if not legacy.launch(layout["bundled_exe"], layout["bundled_doc"]):
        rec["status"] = "no-main-window"
        rec["screenshot"] = legacy.shot("automation-bundled-autoload")
        legacy.kill()
        return rec
    rec["main_window"] = legacy.main.get("name")
    time.sleep(8)  # autoload runs at startup
    rec["popups"] = legacy.popups()
    rec["screenshot"] = legacy.shot("automation-bundled-autoload")
    rec["status"] = "captured"
    legacy.kill()
    return rec


def probe(legacy, layout, spec, dump_first_hang):
    rec = {"part": "probe", "probe_sha256": layout["probe_sha256"], "corpus": layout["corpus"],
           "status": "timeout", "steps": [], "launches": []}

    def start(reason):
        ok = legacy.launch(layout["probe_exe"], layout["probe_doc"])
        launch = {"reason": reason, "pid": legacy.pid, "main_window": legacy.main.get("name") if ok else None}
        rec["launches"].append(launch)
        if not ok:
            return False
        time.sleep(8)  # Autoload, with the probe's load-time corpus capture
        launch["popups"] = legacy.popups()
        for _ in launch["popups"] or []:
            legacy.w.ui("keys", keys="RETURN")
            time.sleep(1)
        return True

    if not start("first"):
        rec["status"] = "no-main-window"
        return rec
    first = legacy.output()
    rec["startup_popups"] = rec["launches"][0].get("popups")
    rec["load_time"] = first["capture"]
    rec["automation_menu"] = None  # hotkeys reach the app on Windows; the menu is not used
    lines = len(first["capture"])
    hung = False
    for step in spec["steps"]:
        k = step["macro"]
        srec = {"case": step["case"], "macro": k, "keys": step.get("keys", []), "status": "timeout",
                "hotkey": f"Ctrl+Shift+F{k + 1}"}
        if hung:
            # The previous step left the app without an answer: a fresh one.
            if not start(f"before {step['case']}"):
                srec["status"] = "no-main-window"
                rec["steps"].append(srec)
                continue
            srec["fresh_launch"] = True
            lines = len(legacy.output()["capture"])  # the relaunch's own load-time line
            hung = False
        legacy.focus_main()
        time.sleep(0.5)
        srec["hotkey_reply"] = legacy.w.ui("keys", keys=f"CTRL+SHIFT+F{k + 1}")
        if step.get("keys") is not None:
            time.sleep(3)  # the dialog takes the focus
            srec["dialog_windows"] = legacy.popups()
            srec["dialog_screenshot"] = legacy.shot(f"automation-{step['case']}-dialog")
            srec["keys_sent"] = send_keys(legacy, step["keys"])
        deadline = time.time() + step.get("timeout", 30)
        state = None
        while time.time() < deadline:
            state = legacy.output()
            if len(state["capture"]) > lines:
                srec["result"] = state["capture"][lines]
                srec["status"] = "captured"
                lines = len(state["capture"])
                break
            time.sleep(2)
        srec["windows_after"] = legacy.popups()
        srec["responding_after"] = state["responding"] if state else None
        srec["process_after"] = state["processes"] if state else None
        if srec["status"] != "captured":
            srec["screenshot"] = legacy.shot(f"automation-{step['case']}")
            srec["thread_states"] = legacy.thread_states()
            if dump_first_hang and not rec.get("hang_dump"):
                rec["hang_dump"] = {"case": step["case"], **legacy.dump()}
            legacy.kill()
            hung = True
        rec["steps"].append(srec)
    final = legacy.output()
    rec["config_files"] = final["config_files"]
    rec["hotkeys_after"] = final["hotkeys"][:2000]
    rec["capture_lines"] = final["capture_text"][:20000]
    legacy.kill()
    rec["status"] = "captured" if all(s["status"] == "captured" for s in rec["steps"]) else "incomplete"
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--config", default="winix.yaml", type=Path)
    ap.add_argument("--vm", default="dev")
    ap.add_argument("--scratch", default="out/legacy-win", type=Path, help="job artifacts (the minidump) go here")
    ap.add_argument("--dump-first-hang", action="store_true",
                    help="keep a minidump of the first step that never answers (legacy-win-dump)")
    a = ap.parse_args()
    plan = json.loads(a.plan.read_text())
    spec = plan["automation"]
    workdir = a.out.parent
    workdir.mkdir(parents=True, exist_ok=True)
    w = Winix(a.config)
    ready = w.call("vm", "ready", a.vm) or {}
    if not ready.get("desktopReady") or ready.get("activeJob"):
        raise SystemExit(f"the VM desktop is not ready or busy: {json.dumps(ready)[:300]}")
    windows = (ready.get("toolchain") or {}).get("windows") or {}
    legacy = Legacy(w, workdir, a.scratch)
    layout = legacy.setup()
    result = {"schema": 1, "legacy_commit": LEGACY_COMMIT, "package": layout["zip"], "release": layout["tag"],
              "package_sha256": layout["zip_sha256"], "probe_sha256": layout["probe_sha256"],
              "host": f"windows (Winix VM {a.vm}, Windows build {windows.get('OsBuildNumber', '?')})",
              "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "cases": []}
    obs = [bundled_startup(legacy, layout), probe(legacy, layout, spec, a.dump_first_hang)]
    result["cases"].append({"id": spec["id"], "route": "automation", "observations": obs})
    a.out.write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n")
    steps = obs[1].get("steps", [])
    print(json.dumps({"captured": [s["case"] for s in steps if s["status"] == "captured"],
                      "not_captured": [s["case"] for s in steps if s["status"] != "captured"]}))


if __name__ == "__main__":
    main()
