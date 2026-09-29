#!/usr/bin/env python3
"""Record what the legacy wx HikariSub does on the approved-departure fixtures.

Characterization harness, run by .github/workflows/legacy-capture.yml. It never
touches a user profile: every UI capture runs a fresh copy of the legacy Linux
package (its configuration lives next to the executable) on a private X server.

Routes (see plan.json):
  time      legacy SubsTime/Timebase code via the legacy_time_capture probe
  save-as   open a scratch copy in the real app, Save As to a new path, hash it
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


def save_as(case, fixtures, package, display, workdir):
    obs = []
    for name in case["inputs"]:
        if Path(name).suffix.lower() not in SUBTITLE_SUFFIXES:
            continue  # descriptors (JSON) and Session templates are not files to open
        src = fixtures / "inputs" / name
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
    for cid, route in plan["routes"].items():
        case = cases[cid]
        entry = {"id": cid, "route": route["route"]}
        if route["route"] == "time":
            entry["observations"] = time_cases(case, a.fixtures, a.probe)
        elif route["route"] == "save-as":
            entry["observations"] = save_as(case, a.fixtures, a.package, a.display, workdir)
        else:
            entry["status"] = "not-captured"
            entry["reason"] = route["reason"]
        result["cases"].append(entry)
    a.out.write_text(json.dumps(result, indent=1) + "\n")
    bad = [c["id"] for c in result["cases"] for o in c.get("observations", [])
           if o["status"] not in ("captured", "not-representable-in-legacy-ms")]
    print(json.dumps({"cases": len(result["cases"]), "incomplete": bad}))


if __name__ == "__main__":
    main()
