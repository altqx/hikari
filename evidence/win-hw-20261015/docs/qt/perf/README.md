# Performance hosts and observations

This folder holds evidence for [H3 #126](https://github.com/altqx/hikari/issues/126) under the [performance contract](../performance.md).

- `hosts/<hostname>.md` and `.json`: a bound host's inventory, written by `tools/perf/host-inventory.sh`. It records CPU, cores and threads, RAM, storage, GPU and driver, displays, OS and kernel, compositor and session, graphics and audio backends and devices, power profile and governor, and the pinned build (git revision, compiler, Qt, FFmpeg, FFMS2, libass, PortAudio, the harness fingerprint). It also compares the host with the accepted reference class.
- `observations/<date>/`: one folder per run from `tools/perf/run-observations.sh`. `raw/` holds the harness reports and stdout, the contention samples (load average, CPU pressure, the busiest other processes every ~2 s), and the binary hashes. `summary.md`/`summary.json` come from `tools/perf/summarize-observations.py`.

## Bound hosts

| Platform | Host | Bound | Calibration |
| --- | --- | --- | --- |
| Linux | [sapphire](hosts/sapphire.md) (Ryzen 5 5600, GTX 1070 Ti, Hyprland/Wayland, PipeWire) | 2026-10-04, by the maintainer's decision on #126 | **Uncalibrated.** There are no presentation traces and no A/V loopback capture. Results are observations. |
| Windows | none | — | — |

sapphire exceeds the accepted reference class: 6 physical cores rather than 4, 31 GiB RAM rather than 8 GiB, and a discrete GPU rather than integrated graphics. The decision binds it anyway. A result there cannot show that a lower-class machine meets a gate.

The harness reports `calibrated: true` only for a fingerprint listed in `tests/perf/reference-hosts.json`. That list stays empty: binding records the host, while calibration needs measurement tooling that does not exist yet.

## 2026-10-04 observations

[Summary](observations/2026-10-04/summary.md). Two runs were taken. Both are **contaminated** under the rule below:

- run1: concurrent agent builds, at a load average of 6.4.
- run2: the idle Winix VM and agent-launched application instances, at a load average of 1.2–1.9 with others' median CPU at 29–33 %.

The binaries were copied from the shared build tree, with their hashes in `raw/*-run.json`. `hikari_core_perf` was built at 11:51 and `hikari_ui_perf` at 12:10. Their measured sources (`tests/perf`, `tests/support/perf`, ASS load, frame timeline, Grid model and paint) are identical at `c8db96c4`. A clean run needs the host idle, with no builds, no Winix VM and no other agents.

## Rules the scripts apply

- Repetitions follow the contract: five runs, at least 1,000 timed operations, 10 s warmup for W, and nearest-rank p95/p99/max per run, never pooled. The harness enforces the minimums.
- Every run records the load average and the other processes' CPU. It is **contaminated** when the 1-minute load average exceeds 1.5 at its start, or other processes use more than a median 25 % or a peak 100 % of a core while it runs. A contaminated run is still kept, and is labelled as contaminated.
- A workload that measures only part of an endpoint can make the endpoint **inconclusive**, or **fail** when the part alone exceeds the bound on a clean run. It never makes the endpoint pass.
- Large raw traces stay out of git. Today's raw files are a few hundred kilobytes.

## Running

```sh
tools/perf/host-inventory.sh                       # writes docs/qt/perf/hosts/<hostname>.{md,json}
tools/perf/run-observations.sh --label run1 --diagnostics
tools/perf/summarize-observations.py docs/qt/perf/observations/<date> --host-inventory docs/qt/perf/hosts/<hostname>.json
```

Both scripts default to `out/build/ubuntu-x64-release`; pass `--build-dir` for another tree. Stop builds and other heavy work first, and keep the machine on AC power with the recorded power profile.
