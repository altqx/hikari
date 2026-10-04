#!/usr/bin/env python3
"""Summarizes performance observations (H3 #126) against docs/qt/performance.md.

    tools/perf/summarize-observations.py <date dir> [--host-inventory FILE]

<date dir> holds one folder per run from tools/perf/run-observations.sh. Writes
<date dir>/summary.json and summary.md: per-run statistics for every
workload, the CPU contention seen during each run, and a verdict for every
budget row. An uncalibrated host's results are observations; an implemented
workload that measures only part of an endpoint stays inconclusive.
"""

import argparse
import glob
import json
import os
import statistics
import sys

# Contention: another process's CPU (percent of one core, from top's
# one-second samples) or a load average beyond the workload's own thread.
OTHER_CPU_MEDIAN_LIMIT = 25.0
OTHER_CPU_PEAK_LIMIT = 100.0
LOAD1_START_LIMIT = 1.5

# Implemented workloads and what they measure against the contract.
WORKLOADS = {
    "core.ass_load.50k_lines": {
        "row": "G open → parsed, selectable visible grid, W",
        "boundMs": 2000, "percentile": "p95",
        "measures": "in-process parse of a generated 50,000-Line ASS script (no process start, file read, model, "
                    "grid or presentation)",
    },
    "core.frame_lookup.ntsc.x1000": {
        "row": None,
        "measures": "1,000 frame lookups on a 24000/1001 timeline per operation (a building block of exact "
                    "stepping; no budget row)",
    },
    "ui.grid_paint.50k_lines.1280x720": {
        "row": "G scroll, W: presented-frame interval",
        "boundMs": 20, "p99BoundMs": 34, "percentile": "p95",
        "measures": "CPU paint of one 1280×720 Grid frame into a QImage at scattered scroll positions over "
                    "50,000 Lines (offscreen; no scene graph, GPU or presented frames)",
    },
}

# Every budget row of docs/qt/performance.md and what covers it today.
ROWS = [
    ("Empty startup → input-ready workspace, C / W", "4 s / 1.5 s", "missing",
     "no harness workload (needs a cold-state method and 20 process launches)"),
    ("G open → parsed, selectable visible grid, C / W", "4 s / 2 s", "partial", "core.ass_load.50k_lines (parse only)"),
    ("G scroll, W: presented-frame interval p95 / p99", "20 / 34 ms", "partial",
     "ui.grid_paint.50k_lines.1280x720 (CPU paint only)"),
    ("G select/reveal; E keystroke/composition update → visible, W", "50 ms", "missing",
     "no workload; E fixture (10,000-character lines, IME) not packaged"),
    ("V exact indexed step, W", "100 ms", "missing", "no seek/frame-delivery workload; V fixture not packaged"),
    ("V seeded random seek → correct frame, W / M", "500 / 1,500 ms", "missing",
     "no seek workload; V fixture not packaged"),
    ("A waveform zoom → complete viewport, W / M", "50 / 250 ms", "missing", "no workload; A fixture not packaged"),
    ("A spectrum zoom → complete viewport, W / M", "100 / 500 ms", "missing", "no workload (spectrum not built yet)"),
    ("A/V offset p95 / max; drift (three 30-minute wired runs)", "40 / 80 ms; 20 ms", "needs-calibration",
     "needs calibrated audio-loopback/video capture (≤5 ms uncertainty); callback clocks cannot pass"),
    ("G+V+A active peak CPU / attributable GPU memory", "1.5 GiB / 256 MiB", "missing",
     "no workload; no GPU allocation tracking"),
    ("Memory growth over 30 open/edit/zoom/close cycles (final idle; fitted slope)",
     "≤32 MiB CPU / 16 MiB GPU; ≤0.5 / 0.25 MiB per cycle", "missing",
     "no cycle workload (helper resource growth included); Linux PSS sampling not implemented"),
]

ASKED_MISSING = [
    ("seek", "V exact indexed step and seeded random seek (W/M)"),
    ("frame delivery", "presented-frame timing for indexed and general playback; native presentation traces"),
    ("overlay rendering", "libass overlay render/composite timing per frame (part of seek/step/scroll endpoints)"),
    ("audio latency", "A/V offset and drift; needs calibrated loopback capture"),
    ("helper resource growth", "30-cycle memory growth for the application and its media/Lua helpers (PSS)"),
]


def ms(us):
    return round(us / 1000.0, 3)


def contention(run_dir, name, pid):
    samples = []
    path = os.path.join(run_dir, "raw", f"{name}-samples.jsonl")
    if os.path.exists(path):
        with open(path) as f:
            samples = [json.loads(line) for line in f if line.strip()]
    before = json.load(open(os.path.join(run_dir, "raw", f"{name}-before.json")))
    others = []
    for s in samples:
        others.append(sum(p["cpu"] for p in s["busiest"]
                          if p["pid"] != pid and p["command"] not in ("top",)))
    loads = [s["load1"] for s in samples] or [before["load1"]]
    temps = [s["cpuTempC"] for s in samples if s.get("cpuTempC") is not None]
    median_other = statistics.median(others) if others else 0.0
    peak_other = max(others) if others else 0.0
    reasons = []
    if before["load1"] > LOAD1_START_LIMIT:
        reasons.append(f"load average {before['load1']} at start (limit {LOAD1_START_LIMIT})")
    if median_other > OTHER_CPU_MEDIAN_LIMIT:
        reasons.append(f"other processes used a median {median_other:.0f}% CPU (limit {OTHER_CPU_MEDIAN_LIMIT:.0f}%)")
    if peak_other > OTHER_CPU_PEAK_LIMIT:
        reasons.append(f"other processes peaked at {peak_other:.0f}% CPU (limit {OTHER_CPU_PEAK_LIMIT:.0f}%)")
    busiest = {}
    for s in samples:
        for p in s["busiest"]:
            if p["pid"] != pid and p["command"] != "top":
                busiest[p["command"]] = max(busiest.get(p["command"], 0), p["cpu"])
    return {
        "samples": len(samples),
        "load1Start": before["load1"], "load1Min": min(loads), "load1Max": max(loads),
        "load1Mean": round(statistics.mean(loads), 2),
        "otherCpuMedian": round(median_other, 1), "otherCpuPeak": round(peak_other, 1),
        "busiestOthers": sorted(busiest.items(), key=lambda kv: -kv[1])[:5],
        "cpuTempC": [min(temps), max(temps)] if temps else None,
        "contaminated": bool(reasons), "reasons": reasons,
    }


def verdict(bench, spec, calibrated, contaminated):
    if spec.get("row") is None:
        return "diagnostic", "no budget row"
    worst95 = max(r["p95Ms"] for r in bench["runs"])
    worst99 = max(r["p99Ms"] for r in bench["runs"])
    bound = spec["boundMs"]
    within = worst95 <= bound and worst99 <= spec.get("p99BoundMs", 2 * bound)
    parts = [f"worst run p95 {worst95:.3f} ms, p99 {worst99:.3f} ms against {spec['row']} "
             f"p95 {bound} ms / p99 {spec.get('p99BoundMs', 2 * bound)} ms: component "
             f"{'within' if within else 'OVER'} the bound"]
    parts.append("measures only " + spec["measures"])
    if not calibrated:
        parts.append("uncalibrated host")
    if contaminated:
        parts.append("contaminated by other CPU work")
    # A component of an endpoint never passes the endpoint; a component that
    # alone exceeds the bound shows the endpoint would fail.
    return ("fail" if not within and not contaminated else "inconclusive"), "; ".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("date_dir")
    ap.add_argument("--host-inventory")
    args = ap.parse_args()
    runs = []
    for run_dir in sorted(d for d in glob.glob(os.path.join(args.date_dir, "*")) if os.path.isdir(os.path.join(d, "raw"))):
        for run_meta in sorted(glob.glob(os.path.join(run_dir, "raw", "*-run.json"))):
            meta = json.load(open(run_meta))
            report_path = os.path.join(run_dir, "raw", f"{meta['name']}-report.json")
            if not os.path.exists(report_path):
                continue
            report = json.load(open(report_path))
            cont = contention(run_dir, meta["name"], meta["pid"])
            for b in report["benchmarks"]:
                bench = {
                    "workload": b["name"], "binaryRun": meta["name"], "run": os.path.basename(run_dir),
                    "started": meta["started"], "ended": meta["ended"], "binarySha256": meta["sha256"],
                    "warmupMs": b["warmupMs"], "p95SpreadPercent": round(b["p95SpreadPercent"], 1),
                    "runs": [{"operations": r["operations"], "p50Ms": ms(r["p50Us"]), "p95Ms": ms(r["p95Us"]),
                              "p99Ms": ms(r["p99Us"]), "maxMs": ms(r["maxUs"]), "meanMs": ms(r["meanUs"])}
                             for r in b["runs"]],
                    "calibrated": report["calibrated"], "harnessNote": report["note"],
                    "host": report["host"], "contention": cont,
                }
                spec = WORKLOADS.get(b["name"], {"row": None, "measures": "unmapped workload"})
                bench["verdict"], bench["verdictDetail"] = verdict(bench, spec, report["calibrated"], cont["contaminated"])
                bench["label"] = "uncalibrated observation" if not report["calibrated"] else "calibrated measurement"
                runs.append(bench)
    if not runs:
        sys.exit("no runs found")

    diag = []
    for path in sorted(glob.glob(os.path.join(args.date_dir, "*", "raw", "diag-*.stderr"))):
        name = os.path.basename(path)[5:-7]
        lines = [l.strip() for l in open(path, errors="replace") if " ms" in l or "MiB" in l]
        before = json.load(open(path.replace(".stderr", "-before.json")))
        diag.append({"run": path.split(os.sep)[-3], "name": name, "load1Start": before["load1"], "lines": lines})

    inventory = json.load(open(args.host_inventory)) if args.host_inventory else None
    summary = {
        "card": "H3 #126", "contract": "docs/qt/performance.md",
        "label": "uncalibrated observation",
        "host": runs[0]["host"], "hostInventory": args.host_inventory,
        "contaminationRule": {
            "load1AtStartAbove": LOAD1_START_LIMIT, "otherCpuMedianAbovePercent": OTHER_CPU_MEDIAN_LIMIT,
            "otherCpuPeakAbovePercent": OTHER_CPU_PEAK_LIMIT,
            "note": "other processes' CPU from top's one-second samples every ~2 s, the workload's own pid excluded",
        },
        "workloadRuns": runs,
        "budgetRows": [{"row": r, "release": rel, "coverage": cov, "detail": det,
                        "verdict": "inconclusive" if cov != "partial" else None} for r, rel, cov, det in ROWS],
        "missingWorkloads": [{"asked": a, "needs": n} for a, n in ASKED_MISSING],
        "diagnostics": diag,
    }
    # A partial row takes the verdict of its latest clean run, else of its latest run.
    for row in summary["budgetRows"]:
        if row["coverage"] != "partial":
            continue
        name = row["detail"].split(" ")[0]
        cands = [r for r in runs if r["workload"] == name]
        clean = [r for r in cands if not r["contention"]["contaminated"]]
        pick = (clean or cands)[-1] if cands else None
        row["verdict"] = pick["verdict"] if pick else "inconclusive"
        row["basedOn"] = f"{pick['run']} ({'clean' if pick and not pick['contention']['contaminated'] else 'contaminated'})" if pick else None

    with open(os.path.join(args.date_dir, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1, ensure_ascii=False)

    out = []
    w = out.append
    h = summary["host"]
    w(f"# Performance observations, {os.path.basename(os.path.abspath(args.date_dir))}: {h['cpu']}")
    w("")
    w("**Uncalibrated observation.** This is H3 #126 on the bound Linux reference host. Its inventory is in "
      f"[`hosts/{inventory['host'] if inventory else '?'}.md`](../../hosts/{inventory['host'] if inventory else '?'}.md). "
      "The host is bound but not calibrated, and the harness reports `calibrated: false`. No result here is a budget pass. "
      "Gates that need calibrated A/V loopback capture stay inconclusive, and #126 stays open for them.")
    w("")
    w(f"Harness fingerprint: `{h['fingerprint']}`. Repetition rule (docs/qt/performance.md): five runs, at least 1,000 "
      "timed operations, 10 s warmup (W), nearest-rank p95/p99/max per run, never pooled.")
    w("")
    w("Contamination rule: a run is contaminated when the load average exceeds "
      f"{LOAD1_START_LIMIT} at its start, or other processes use more than a median {OTHER_CPU_MEDIAN_LIMIT:.0f}% "
      f"or a peak {OTHER_CPU_PEAK_LIMIT:.0f}% of a core during it. Both are sampled every ~2 s.")
    w("")
    w("## Workload runs")
    w("")
    w("| Run | Workload | Per-run p95 (ms) | Per-run p99 (ms) | Per-run max (ms) | Spread | Load 1-min (start / max) | Other CPU (median / peak) | Contention | Verdict |")
    w("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for r in runs:
        c = r["contention"]
        w(f"| {r['run']} | `{r['workload']}` | {' / '.join(f'{x['p95Ms']:.3f}' for x in r['runs'])} | "
          f"{' / '.join(f'{x['p99Ms']:.3f}' for x in r['runs'])} | {' / '.join(f'{x['maxMs']:.3f}' for x in r['runs'])} | "
          f"{r['p95SpreadPercent']}% | {c['load1Start']} / {c['load1Max']} | {c['otherCpuMedian']}% / {c['otherCpuPeak']}% | "
          f"{'**contaminated**' if c['contaminated'] else 'clean'} | {r['verdict']} ({r['label']}) |")
    w("")
    for r in runs:
        c = r["contention"]
        if c["contaminated"]:
            w(f"- {r['run']} `{r['workload']}`: contaminated by {'; '.join(c['reasons'])}. Busiest others: "
              + ", ".join(f"{k} {v:.0f}%" for k, v in c["busiestOthers"]) + ".")
    w("")
    w("## Budget rows")
    w("")
    w("| Row | Release bound | Coverage | Verdict | Detail |")
    w("| --- | --- | --- | --- | --- |")
    for row in summary["budgetRows"]:
        w(f"| {row['row']} | {row['release']} | {row['coverage']} | **{row['verdict']}** | {row['detail']}"
          + (f"; based on {row['basedOn']}" if row.get("basedOn") else "") + " |")
    w("")
    for r in runs:
        if r["verdictDetail"] != "no budget row" and r is [x for x in runs if x["workload"] == r["workload"]][-1]:
            w(f"- `{r['workload']}` ({r['run']}): {r['verdictDetail']}.")
    w("")
    w("## Workloads that do not exist yet")
    w("")
    for m in summary["missingWorkloads"]:
        w(f"- **{m['asked']}**: {m['needs']}.")
    if diag:
        w("")
        w("## Single-shot diagnostics (not harness workloads)")
        w("")
        w("These are timings that backend tests print once per run. They have no repetitions or percentiles and no budget verdict.")
        w("")
        for d in diag:
            w(f"- {d['run']} `{d['name']}` (load {d['load1Start']} at start): " + "; ".join(d["lines"]))
    with open(os.path.join(args.date_dir, "summary.md"), "w") as f:
        f.write("\n".join(out) + "\n")
    print(os.path.join(args.date_dir, "summary.md"))


if __name__ == "__main__":
    main()
