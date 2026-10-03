#!/usr/bin/env python3
"""Compare the legacy automation capture with the rewrite's (S3 -> L6).

    compare_automation.py <legacy observations.json> <rewrite automation-capture-corpus.json>

The legacy side is the "corpus" step of the S3-automation case written by
drive.py; the rewrite side is the same probe's JSON written by the Lua helper
test (LuaHelper.CaptureProbeCorpusRunsInThisHost). Differences are printed as
candidates for review: they are observations, not verdicts, and none is fixed
silently (A33-compat).
"""
import json
import sys
from pathlib import Path


def legacy_corpus(path):
    data = json.loads(Path(path).read_text())
    for case in data.get("cases", []):
        if case.get("route") != "automation":
            continue
        for obs in case.get("observations", []):
            # The capture run while the host loaded the probe, else the hotkey step.
            for record in obs.get("load_time", []):
                if record.get("case") == "corpus" and "scripts" in record:
                    return record
            for step in obs.get("steps", []):
                if step.get("case") == "corpus" and step.get("status") == "captured":
                    return step["result"]
    sys.exit("no captured corpus step in the legacy observations")


def summary(script):
    regs = [(r.get("kind"), (r.get("name") or {}).get("value")) for r in script.get("registrations", [])]
    return {
        "loaded": script.get("loaded"),
        "error": (script.get("error") or "").split("\n")[0],
        "script_name": (script.get("script_name") or {}).get("value"),
        "script_version": (script.get("script_version") or {}).get("value"),
        "registrations": regs,
    }


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    legacy = legacy_corpus(sys.argv[1])
    rewrite = json.loads(Path(sys.argv[2]).read_text())
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
    print(f"{len(l_scripts)} legacy scripts, {len(r_scripts)} rewrite scripts, {len(rows)} candidate differences")
    for kind, what, l, r in rows:
        print(f"- {kind}: {what}\n    legacy:  {json.dumps(l, ensure_ascii=False)}\n    rewrite: {json.dumps(r, ensure_ascii=False)}")


if __name__ == "__main__":
    main()
