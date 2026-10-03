#!/usr/bin/env python3
"""Record what the legacy wx HikariSub does on the approved-departure fixtures.

Characterization harness, run by .github/workflows/legacy-capture.yml. It never
touches a user profile: every UI capture runs a fresh copy of the legacy Linux
package (its configuration lives next to the executable) on a private X server.

Routes (see plan.json):
  time      legacy SubsTime/Timebase code via the legacy_time_capture probe
  save-as   open a scratch copy in the real app, Save As to a new path, hash it;
            an optional "keys" script is typed first (editor commands)
  automation (plan "automation" section) the legacy Lua host: the bundled
            Autoload scripts at startup, then automation/capture-probe.lua's
            macros run through script hotkeys (dialog subset, then corpus)
Anything else is recorded as not captured, with the plan's reason.
"""
import argparse, hashlib, json, os, platform, shutil, subprocess, sys, tarfile, tempfile, time
from pathlib import Path


SUBTITLE_SUFFIXES = {".ass", ".ssa", ".srt", ".sub", ".txt", ".mpl"}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def time_cases(case, fixtures, probe):
    obs = []
    lines = []
    for name in case["inputs"]:
        data = json.loads((fixtures / "inputs" / name).read_text())
        if "legacy_representable_cases" in data:
            for c in data["legacy_representable_cases"]:
                l, r = int(c["lhs"]), int(c["rhs"])
                if l % 1000 or r % 1000:
                    obs.append({"input": c, "status": "not-representable-in-legacy-ms"})
                    continue
                lines.append((f"cmp {l // 1000} {r // 1000}", c))
        if data.get("lookup") == "frameAtOrAfter":
            for c in data["cases"]:
                vals = [c["anchor"], c["target"], *c["starts"]]
                if any(v % 1000 for v in vals):
                    obs.append({"input": c, "status": "not-representable-in-legacy-ms"})
                    continue
                ms = [v // 1000 for v in vals]
                lines.append(("offset " + " ".join(map(str, ms)), c))
    if not lines:
        return obs
    res = run([str(probe)], input="\n".join(l for l, _ in lines) + "\n", timeout=60)
    out = res.stdout.strip().splitlines()
    if res.returncode or len(out) != len(lines):
        return obs + [{"status": "probe-failed", "returncode": res.returncode, "stderr": res.stderr[-2000:]}]
    for (cmd, c), o in zip(lines, out):
        fields = dict(kv.split("=") for kv in o.split()[3:])
        obs.append({"input": c, "probe_command": cmd, "observed": {k: int(v) for k, v in fields.items()},
                    "status": "captured"})
    return obs


def x(cmd, display, timeout=30):
    # A timed-out wait is an observation (empty result), not a harness crash.
    try:
        return run(cmd, env={**os.environ, "DISPLAY": display}, timeout=timeout)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "", "timeout")


def save_as(case, inputs_dir, package, display, workdir, keys=None):
    obs = []
    for name in case["inputs"]:
        if Path(name).suffix.lower() not in SUBTITLE_SUFFIXES:
            continue  # descriptors (JSON) and Session templates are not files to open
        src = inputs_dir / name
        scratch = Path(tempfile.mkdtemp(prefix="cap-", dir=workdir))
        with tarfile.open(package) as t:
            t.extractall(scratch, filter="data")
        app = next(scratch.glob("*/hikarisub"))
        # The app keeps its single-instance lock under HOME, so it must exist.
        (scratch / "home").mkdir()
        # Load/save does not involve automation; bundled autoload scripts raise a
        # modal error in this environment, so this capture runs without them.
        autoload = app.parent / "Automation" / "automation" / "Autoload"
        removed = sorted(p.name for p in autoload.glob("*")) if autoload.is_dir() else []
        for p in autoload.glob("*"):
            if p.is_file():
                p.unlink()
        doc = scratch / "doc" / name
        doc.parent.mkdir()
        shutil.copyfile(src, doc)
        out = scratch / "doc" / ("saved-" + name)
        rec = {"input": name, "input_sha256": sha256(src), "status": "timeout",
               "autoload_scripts_removed": removed, "dismissed_popups": []}
        proc = subprocess.Popen([str(app), str(doc)], cwd=app.parent,
                                env={**os.environ, "DISPLAY": display, "HOME": str(scratch / "home")},
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            win = x(["xdotool", "search", "--sync", "--onlyvisible", "--name", "HikariSub v[0-9]"], display, 90)
            wid = win.stdout.split()[0] if win.stdout.split() else None
            if not wid:
                rec["status"] = "no-main-window"
                continue
            time.sleep(5)  # let the document load settle
            # Any other top-level window at this point is an unexpected popup.
            # Record its title and dismiss it so the capture can continue.
            listed = x(["xdotool", "search", "--onlyvisible", "--name", "."], display, 10).stdout.split()
            for other in listed:
                if other == wid:
                    continue
                title = x(["xdotool", "getwindowname", other], display, 5).stdout.strip()
                if not title or "HikariSub v" in title:
                    continue
                rec["dismissed_popups"].append(title)
                x(["xdotool", "windowactivate", "--sync", other], display, 10)
                x(["xdotool", "key", "Return"], display, 5)
                time.sleep(1)
            # Focus through the window manager, then click the grid's empty
            # area so keyboard focus is inside the frame that owns the hotkeys.
            x(["xdotool", "windowactivate", "--sync", wid], display)
            x(["xdotool", "mousemove", "--window", wid, "400", "550", "click", "1"], display)
            time.sleep(1)
            # Editor key script (route "keys"): typed into the focused window,
            # one key chord at a time, before Save As.
            for chord in keys or []:
                x(["xdotool", "key", "--clearmodifiers", chord], display)
                time.sleep(0.4)
            if keys:
                rec["keys"] = keys
                time.sleep(1)
            x(["xdotool", "key", "--clearmodifiers", "ctrl+shift+s"], display)
            dlg = x(["xdotool", "search", "--sync", "--onlyvisible", "--name", "Save subtitle file"], display, 30)
            if not dlg.stdout.split():
                rec["status"] = "no-save-dialog"
                continue
            time.sleep(1)
            x(["xdotool", "key", "--clearmodifiers", "ctrl+a"], display)
            x(["xdotool", "type", "--delay", "20", str(out)], display, 60)
            x(["xdotool", "key", "Return"], display)
            deadline = time.time() + 30
            saved = None
            while time.time() < deadline:
                cands = [p for p in out.parent.glob("saved-*")]
                if cands and cands[0].stat().st_size > 0:
                    time.sleep(1)
                    saved = cands[0]
                    break
                time.sleep(0.5)
            if saved:
                data = saved.read_bytes()
                rec.update(status="captured", output_name=saved.name, output_bytes=len(data),
                           output_sha256=hashlib.sha256(data).hexdigest(),
                           byte_identical=data == src.read_bytes())
                keep = workdir / "outputs" / case["id"]
                keep.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(saved, keep / saved.name)
        finally:
            if rec["status"] != "captured":
                shot = workdir / "screens" / f"{case['id']}-{name}.png"
                shot.parent.mkdir(parents=True, exist_ok=True)
                x(["import", "-window", "root", str(shot)], display)
                rec["screenshot"] = str(shot.relative_to(workdir))
            proc.kill()
            try:
                log, _ = proc.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                log = ""
            rec["app_log_tail"] = (log or "")[-1500:]
            obs.append(rec)
            shutil.rmtree(scratch, ignore_errors=True)  # keep only outputs and screenshots
    return obs


def unpack(package, workdir):
    scratch = Path(tempfile.mkdtemp(prefix="cap-", dir=workdir))
    with tarfile.open(package) as t:
        t.extractall(scratch, filter="data")
    app = next(scratch.glob("*/hikarisub"))
    (scratch / "home").mkdir()  # the single-instance lock lives under HOME
    return scratch, app


def popups(display, main_wid):
    """Visible top-level windows other than the main one: (id, title)."""
    found = []
    for other in x(["xdotool", "search", "--onlyvisible", "--name", "."], display, 10).stdout.split():
        if other == main_wid:
            continue
        title = x(["xdotool", "getwindowname", other], display, 5).stdout.strip()
        if title and "HikariSub v" not in title:
            found.append((other, title))
    return found


def screenshot(display, workdir, name):
    shot = workdir / "screens" / f"{name}.png"
    shot.parent.mkdir(parents=True, exist_ok=True)
    x(["import", "-window", "root", str(shot)], display)
    return str(shot.relative_to(workdir))


def finish(proc):
    proc.kill()
    try:
        log, _ = proc.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        log = ""
    return (log or "")[-3000:]


def automation(spec, package, display, workdir, probe_source):
    """S3: the legacy automation host, observed through the app itself."""
    obs = []
    doc_text = "[Script Info]\nScriptType: v4.00+\n\n[Events]\n" \
               "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" \
               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,probe\n"

    # 1. Startup with the bundled Autoload scripts as shipped: what the user sees.
    scratch, app = unpack(package, workdir)
    autoload = app.parent / "Automation" / "automation" / "Autoload"
    shipped = sorted(p.name for p in autoload.glob("*") if p.is_file())
    doc = scratch / "doc" / "probe.ass"
    doc.parent.mkdir()
    doc.write_text(doc_text)
    rec = {"part": "bundled-autoload-startup", "autoload_scripts": shipped, "status": "timeout", "popups": []}
    proc = subprocess.Popen([str(app), str(doc)], cwd=app.parent,
                            env={**os.environ, "DISPLAY": display, "HOME": str(scratch / "home")},
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        win = x(["xdotool", "search", "--sync", "--onlyvisible", "--name", "HikariSub v[0-9]"], display, 90)
        wid = win.stdout.split()[0] if win.stdout.split() else None
        if wid:
            time.sleep(8)  # autoload runs at startup
            rec["popups"] = [t for _, t in popups(display, wid)]
            rec["screenshot"] = screenshot(display, workdir, "automation-bundled-autoload")
            rec["status"] = "captured"
        else:
            rec["status"] = "no-main-window"
    finally:
        rec["app_log_tail"] = finish(proc)
        obs.append(rec)
        shutil.rmtree(scratch, ignore_errors=True)

    # 2. The probe: Autoload emptied (its scripts become the corpus), the
    # probe's macros bound to Ctrl+Shift+F<k+1> in the app's own Hotkeys.txt.
    scratch, app = unpack(package, workdir)
    autoload = app.parent / "Automation" / "automation" / "Autoload"
    corpus_dir = scratch / "corpus"
    corpus_dir.mkdir()
    for p in sorted(autoload.glob("*")):
        if p.is_file():
            shutil.copyfile(p, corpus_dir / p.name)
            p.unlink()
    corpus = sorted(corpus_dir.glob("*"))
    (scratch / "corpus.txt").write_text("".join(f"{c}\n" for c in corpus))
    probe = scratch / "probe" / "capture-probe.lua"
    probe.parent.mkdir()
    shutil.copyfile(probe_source, probe)
    config = app.parent / "Config"
    config.mkdir(exist_ok=True)
    # A header with a build number past the converter and more than ten
    # entries, so legacy LoadHkeys keeps this file as it is.
    lines = ["[HikariSub 0.0.1.9999]"] + [f"Script {probe}-{k}=Ctrl-Shift-F{k + 1}" for k in range(12)]
    (config / "Hotkeys.txt").write_text("\r\n".join(lines) + "\r\n")
    doc = scratch / "doc" / "probe.ass"
    doc.parent.mkdir()
    doc.write_text(doc_text)
    output = scratch / "capture.jsonl"
    output.touch()
    proc = subprocess.Popen([str(app), str(doc)], cwd=app.parent,
                            env={**os.environ, "DISPLAY": display, "HOME": str(scratch / "home"),
                                 "HIKARI_CAPTURE_OUT": str(output), "HIKARI_CAPTURE_CORPUS": str(scratch / "corpus.txt")},
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    probe_rec = {"part": "probe", "probe_sha256": sha256(probe_source), "corpus": [c.name for c in corpus],
                 "status": "timeout", "steps": []}
    try:
        win = x(["xdotool", "search", "--sync", "--onlyvisible", "--name", "HikariSub v[0-9]"], display, 90)
        wid = win.stdout.split()[0] if win.stdout.split() else None
        if not wid:
            probe_rec["status"] = "no-main-window"
            return obs
        time.sleep(5)
        probe_rec["startup_popups"] = [t for _, t in popups(display, wid)]
        for other, _ in popups(display, wid):
            x(["xdotool", "windowactivate", "--sync", other], display, 10)
            x(["xdotool", "key", "Return"], display, 5)
            time.sleep(1)
        for step in spec["steps"]:
            k = step["macro"]
            srec = {"case": step["case"], "macro": k, "keys": step.get("keys", []), "status": "timeout"}
            before = len(output.read_text().splitlines())
            x(["xdotool", "windowactivate", "--sync", wid], display)
            x(["xdotool", "mousemove", "--window", wid, "400", "550", "click", "1"], display)
            time.sleep(1)
            x(["xdotool", "key", "--clearmodifiers", f"ctrl+shift+F{k + 1}"], display)
            if step.get("keys") is not None:
                time.sleep(3)  # the dialog takes the focus
                srec["dialog_windows"] = [t for _, t in popups(display, wid)]
                for chord in step["keys"]:
                    if chord.startswith("type:"):
                        x(["xdotool", "type", "--delay", "50", chord[5:]], display, 30)
                    else:
                        x(["xdotool", "key", "--clearmodifiers", chord], display)
                    time.sleep(0.5)
            deadline = time.time() + step.get("timeout", 30)
            while time.time() < deadline:
                got = output.read_text().splitlines()
                if len(got) > before:
                    srec["result"] = json.loads(got[before])
                    srec["status"] = "captured"
                    break
                time.sleep(0.5)
            srec["windows_after"] = [t for _, t in popups(display, wid)]
            srec["main_title_after"] = x(["xdotool", "getwindowname", wid], display, 5).stdout.strip()
            if srec["status"] != "captured":
                srec["screenshot"] = screenshot(display, workdir, f"automation-{step['case']}")
                srec["popups"] = srec["windows_after"]
                for other, _ in popups(display, wid):  # leave a clean state for the next step
                    x(["xdotool", "windowactivate", "--sync", other], display, 10)
                    x(["xdotool", "key", "Escape"], display, 5)
                    time.sleep(1)
            probe_rec["steps"].append(srec)
        probe_rec["status"] = "captured" if all(s["status"] == "captured" for s in probe_rec["steps"]) else "incomplete"
    finally:
        probe_rec["app_log_tail"] = finish(proc)
        # Whether the app read the crafted hotkeys: its Config directory and
        # the Hotkeys.txt it left behind.
        probe_rec["config_files"] = sorted(p.name for p in config.glob("*")) if config.is_dir() else []
        hotkeys = config / "Hotkeys.txt"
        probe_rec["hotkeys_after"] = hotkeys.read_text(errors="replace")[:2000] if hotkeys.exists() else None
        probe_rec["capture_lines"] = output.read_text(errors="replace")[:20000] if output.exists() else None
        obs.append(probe_rec)
        shutil.rmtree(scratch, ignore_errors=True)
    return obs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True, type=Path)
    ap.add_argument("--plan", required=True, type=Path)
    ap.add_argument("--package", required=True, type=Path)
    ap.add_argument("--probe", required=True, type=Path)
    ap.add_argument("--legacy-commit", required=True)
    ap.add_argument("--display", default=":99")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    manifest = json.loads((a.fixtures / "manifest.json").read_text())
    # Inputs must still match the manifest: a changed fixture is not the same case.
    for i in manifest["inputs"]:
        if sha256(a.fixtures / i["path"]) != i["sha256"]:
            sys.exit(f"fixture changed: {i['path']}")
    plan = json.loads(a.plan.read_text())
    cases = {c["id"]: c for c in manifest["cases"]}
    workdir = a.out.parent
    result = {"schema": 1, "legacy_commit": a.legacy_commit, "package_sha256": sha256(a.package),
              "probe_sha256": sha256(a.probe), "host": platform.platform(),
              "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "cases": []}
    # Capture-only cases with their own inputs (hashes recorded, not checked).
    extra_dir = a.plan.parent / "inputs"
    for cid, route in plan.get("extra_cases", {}).items():
        cases[cid] = {"id": cid, "inputs": route["inputs"]}
        plan["routes"][cid] = route
    for cid, route in plan["routes"].items():
        case = cases[cid]
        entry = {"id": cid, "route": route["route"]}
        if route["route"] == "time":
            entry["observations"] = time_cases(case, a.fixtures, a.probe)
        elif route["route"] == "save-as":
            source = extra_dir if cid in plan.get("extra_cases", {}) else a.fixtures / "inputs"
            entry["observations"] = save_as(case, source, a.package, a.display, workdir, route.get("keys"))
        else:
            entry["status"] = "not-captured"
            entry["reason"] = route["reason"]
        result["cases"].append(entry)
    if "automation" in plan:
        auto = plan["automation"]
        probe_source = a.plan.parent / auto["probe"]
        result["cases"].append({"id": auto["id"], "route": "automation",
                                "observations": automation(auto, a.package, a.display, workdir, probe_source)})
    a.out.write_text(json.dumps(result, indent=1) + "\n")
    bad = [c["id"] for c in result["cases"] for o in c.get("observations", [])
           if o["status"] not in ("captured", "not-representable-in-legacy-ms")]
    print(json.dumps({"cases": len(result["cases"]), "incomplete": bad}))


if __name__ == "__main__":
    main()
