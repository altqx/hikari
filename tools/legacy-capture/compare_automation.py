#!/usr/bin/env python3
"""Compare the legacy automation capture with the rewrite's (S3 -> L6, L2).

    compare_automation.py <legacy observations.json> <rewrite automation-capture-corpus.json>
                          [--macro-corpus <rewrite automation-capture-corpus-macro.json>]
                          [--dialogs <rewrite automation-capture-dialogs.json>]

The legacy side is the S3-automation case written by drive.py (Linux) or
drive_windows.py (Windows). The corpus (L6) compares the probe's load-time
record with the one the Lua helper test writes the same way
(LuaHelper.CaptureProbeCorpusRunsInThisHost); --macro-corpus compares the
record the probe's corpus macro wrote (legacy: the "corpus" step) with the
test's macro-time record. --dialogs compares each dialog case (L2) the legacy
app answered, or did not, with the rewrite's answer to the same macro and keys
(AutomationDialogTests::captureProbeDialogCasesAnswer). Differences are
printed as candidates for review: they are observations, not verdicts, and
none is fixed silently (A33-compat).
"""
import argparse
import json
import sys
from pathlib import Path


def legacy_probe(path):
    data = json.loads(Path(path).read_text())
    for case in data.get("cases", []):
        if case.get("route") != "automation":
            continue
        for obs in case.get("observations", []):
            if obs.get("part") == "probe":
                return data, obs
    sys.exit("no probe part in the legacy observations")


def legacy_corpus(path, when="load"):
    _, obs = legacy_probe(path)
    if when == "load":
        # The capture run while the host loaded the probe.
        for record in obs.get("load_time", []):
            if record.get("case") == "corpus" and "scripts" in record:
                return record
    for step in obs.get("steps", []):
        if step.get("case") == "corpus" and step.get("status") == "captured":
            return step["result"]
    sys.exit(f"no captured {when}-time corpus record in the legacy observations")


def summary(script):
    regs = [(r.get("kind"), (r.get("name") or {}).get("value")) for r in script.get("registrations", [])]
    return {
        "loaded": script.get("loaded"),
        "error": (script.get("error") or "").split("\n")[0],
        "script_name": (script.get("script_name") or {}).get("value"),
        "script_version": (script.get("script_version") or {}).get("value"),
        "registrations": regs,
    }


def compare_corpus(legacy, rewrite):
    rows = []
    for key in ("lua_version", "jit"):
        if legacy.get(key) != rewrite.get(key):
            rows.append(("host", key, legacy.get(key), rewrite.get(key)))
    for name in sorted(set(legacy.get("modules", {})) | set(rewrite.get("modules", {}))):
        l = legacy.get("modules", {}).get(name, {})
        r = rewrite.get("modules", {}).get(name, {})
        if (l.get("ok"), l.get("type")) != (r.get("ok"), r.get("type")):
            rows.append(("module", name, l, r))
    def stubs(record):
        # Error texts name each run's own probe path; compare outcome and type.
        out = dict(record.get("stubs") or {})
        call = out.pop("set_undo_point_call", {}) or {}
        out["set_undo_point_call"] = (call.get("ok"), (call.get("result") or {}).get("type"))
        return out
    if stubs(legacy) != stubs(rewrite):
        rows.append(("stubs", "C06", stubs(legacy), stubs(rewrite)))
    l_api, r_api = set(legacy.get("aegisub_api", [])), set(rewrite.get("aegisub_api", []))
    if l_api != r_api:
        rows.append(("aegisub api", "only legacy / only rewrite", sorted(l_api - r_api), sorted(r_api - l_api)))
    l_scripts = {s["file"]: summary(s) for s in legacy.get("scripts", [])}
    r_scripts = {s["file"]: summary(s) for s in rewrite.get("scripts", [])}
    for file in sorted(set(l_scripts) | set(r_scripts)):
        l, r = l_scripts.get(file), r_scripts.get(file)
        if l is None or r is None:
            rows.append(("script", file, l, r))
            continue
        for field in l:
            if l[field] != r[field]:
                rows.append(("script", f"{file} {field}", l[field], r[field]))
    return len(l_scripts), len(r_scripts), rows


def answer(result):
    """A dialog case's answer without the parts that name a run's own paths."""
    if result is None:
        return None
    out = {k: result.get(k) for k in ("ok", "button", "values", "extra") if k in result}
    if "error" in result:
        out["error"] = (result.get("error") or "").split(":")[-1].strip()
    return out


def compare_dialogs(legacy_obs, rewrite):
    rows = []
    r_cases = {c["case"]: c for c in rewrite.get("cases", [])}
    for step in legacy_obs.get("steps", []):
        name = step.get("case")
        if name == "corpus":
            continue
        r = r_cases.pop(name, None)
        if r is None:
            rows.append(("dialog", name, step.get("status"), None))
            continue
        if step.get("status") != "captured":
            # The legacy app gave no answer: what it did instead is the observation.
            legacy = {"status": step.get("status"), "responding_after": step.get("responding_after"),
                      "dialog_windows": step.get("dialog_windows")}
            rewrite = {"status": r.get("status"), "dialog_shown": r.get("dialog_shown")}
            # Keys that left the rewrite's dialog open, and how it was then closed.
            for key in ("dialog_open_after_keys", "focus_after_keys", "closed_by"):
                if key in r:
                    rewrite[key] = r[key]
            rewrite["answer"] = answer(r.get("result"))
            rows.append(("dialog", f"{name} (keys {step.get('keys')})", legacy, rewrite))
            continue
        l_answer, r_answer = answer(step.get("result")), answer(r.get("result"))
        if l_answer != r_answer:
            rows.append(("dialog", f"{name} (keys {step.get('keys')})", l_answer, r_answer))
    for name, r in r_cases.items():
        rows.append(("dialog", name, None, answer(r.get("result"))))
    return rows


def show(title, rows):
    print(title)
    for kind, what, l, r in rows:
        print(f"- {kind}: {what}\n    legacy:  {json.dumps(l, ensure_ascii=False)}\n    rewrite: {json.dumps(r, ensure_ascii=False)}")


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("legacy")
    ap.add_argument("rewrite_corpus")
    ap.add_argument("--macro-corpus")
    ap.add_argument("--dialogs")
    a = ap.parse_args()
    data, obs = legacy_probe(a.legacy)
    print(f"legacy {data.get('legacy_commit', '?')[:8]} on {data.get('host', '?')}")
    nl, nr, rows = compare_corpus(legacy_corpus(a.legacy, "load"), json.loads(Path(a.rewrite_corpus).read_text()))
    show(f"load-time corpus: {nl} legacy scripts, {nr} rewrite scripts, {len(rows)} candidate differences", rows)
    if a.macro_corpus:
        nl, nr, rows = compare_corpus(legacy_corpus(a.legacy, "macro"), json.loads(Path(a.macro_corpus).read_text()))
        show(f"macro-time corpus: {nl} legacy scripts, {nr} rewrite scripts, {len(rows)} candidate differences", rows)
    if a.dialogs:
        rows = compare_dialogs(obs, json.loads(Path(a.dialogs).read_text()))
        show(f"dialog cases: {len(rows)} candidate differences", rows)


if __name__ == "__main__":
    main()
