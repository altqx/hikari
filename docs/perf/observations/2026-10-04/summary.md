# Performance observations, 2026-10-04: AMD Ryzen 5 5600 6-Core Processor

**Uncalibrated observation.** This is H3 #126 on the bound Linux reference host. Its inventory is in [`hosts/sapphire.md`](../../hosts/sapphire.md). The host is bound but not calibrated, and the harness reports `calibrated: false`. No result here is a budget pass. Gates that need calibrated A/V loopback capture stay inconclusive, and #126 stays open for them.

Harness fingerprint: `AMD Ryzen 5 5600 6-Core Processor|12|Linux 7.2.5-4-omarchy|gcc 16.2.1 20260810`. Repetition rule (docs/qt/performance.md): five runs, at least 1,000 timed operations, 10 s warmup (W), nearest-rank p95/p99/max per run, never pooled.

Contamination rule: a run is contaminated when the load average exceeds 1.5 at its start, or other processes use more than a median 25% or a peak 100% of a core during it. Both are sampled every ~2 s.

## Workload runs

| Run | Workload | Per-run p95 (ms) | Per-run p99 (ms) | Per-run max (ms) | Spread | Load 1-min (start / max) | Other CPU (median / peak) | Contention | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| run1 | `core.ass_load.50k_lines` | 62.189 / 62.149 / 63.014 / 59.849 / 56.860 | 64.529 / 71.842 / 68.068 / 62.600 / 58.405 | 68.264 / 77.445 / 79.036 / 65.586 / 60.961 | 10.8% | 6.37 / 6.37 | 158.3% / 267.9% | **contaminated** | inconclusive (uncalibrated observation) |
| run1 | `core.frame_lookup.ntsc.x1000` | 0.010 / 0.010 / 0.010 / 0.010 / 0.019 | 0.013 / 0.010 / 0.016 / 0.018 / 0.020 | 0.015 / 0.013 / 0.019 / 0.019 / 0.025 | 85.6% | 6.37 / 6.37 | 158.3% / 267.9% | **contaminated** | diagnostic (uncalibrated observation) |
| run1 | `ui.grid_paint.50k_lines.1280x720` | 2.633 / 2.668 / 2.683 / 2.768 / 2.125 | 2.702 / 2.722 / 2.776 / 2.948 / 2.262 | 2.943 / 2.751 / 2.890 / 3.791 / 2.886 | 30.3% | 4.49 / 4.35 | 165.2% / 222.6% | **contaminated** | inconclusive (uncalibrated observation) |
| run2 | `core.ass_load.50k_lines` | 50.398 / 45.476 / 48.212 / 45.455 / 52.857 | 56.359 / 48.529 / 50.965 / 46.496 / 54.388 | 60.052 / 50.671 / 57.218 / 50.592 / 58.711 | 16.3% | 1.2 / 2.07 | 33.3% / 176.1% | **contaminated** | inconclusive (uncalibrated observation) |
| run2 | `core.frame_lookup.ntsc.x1000` | 0.010 / 0.010 / 0.010 / 0.020 / 0.015 | 0.010 / 0.010 / 0.010 / 0.020 / 0.019 | 0.015 / 0.014 / 0.014 / 0.020 / 0.019 | 93.9% | 1.2 / 2.07 | 33.3% / 176.1% | **contaminated** | diagnostic (uncalibrated observation) |
| run2 | `ui.grid_paint.50k_lines.1280x720` | 1.978 / 1.984 / 2.001 / 1.964 / 2.131 | 2.026 / 2.085 / 2.093 / 2.009 / 2.217 | 2.028 / 2.324 / 2.218 / 2.039 / 2.338 | 8.5% | 1.85 / 1.85 | 28.7% / 43.0% | **contaminated** | inconclusive (uncalibrated observation) |

- run1 `core.ass_load.50k_lines`: contaminated by load average 6.37 at start (limit 1.5); other processes used a median 158% CPU (limit 25%); other processes peaked at 268% CPU (limit 100%). Busiest others: qemu-system-x86 125%, hikarisub 103%, hikari_core_per 100%, helium 98%, dockerd 95%.
- run1 `core.frame_lookup.ntsc.x1000`: contaminated by load average 6.37 at start (limit 1.5); other processes used a median 158% CPU (limit 25%); other processes peaked at 268% CPU (limit 100%). Busiest others: qemu-system-x86 125%, hikarisub 103%, hikari_core_per 100%, helium 98%, dockerd 95%.
- run1 `ui.grid_paint.50k_lines.1280x720`: contaminated by load average 4.49 at start (limit 1.5); other processes used a median 165% CPU (limit 25%); other processes peaked at 223% CPU (limit 100%). Busiest others: hikari_core_per 100%, hikarisub 93%, helium 30%, qemu-system-x86 22%, 2.1.288 8%.
- run2 `core.ass_load.50k_lines`: contaminated by other processes used a median 33% CPU (limit 25%); other processes peaked at 176% CPU (limit 100%). Busiest others: hikarisub 142%, qemu-system-x86 61%, helium 54%, 2.1.288 32%, hikari-lua-help 19%.
- run2 `core.frame_lookup.ntsc.x1000`: contaminated by other processes used a median 33% CPU (limit 25%); other processes peaked at 176% CPU (limit 100%). Busiest others: hikarisub 142%, qemu-system-x86 61%, helium 54%, 2.1.288 32%, hikari-lua-help 19%.
- run2 `ui.grid_paint.50k_lines.1280x720`: contaminated by load average 1.85 at start (limit 1.5); other processes used a median 29% CPU (limit 25%). Busiest others: qemu-system-x86 23%, 2.1.288 10%, tailscaled 7%, kache 6%.

## Budget rows

| Row | Release bound | Coverage | Verdict | Detail |
| --- | --- | --- | --- | --- |
| Empty startup → input-ready workspace, C / W | 4 s / 1.5 s | missing | **inconclusive** | no harness workload (needs a cold-state method and 20 process launches) |
| G open → parsed, selectable visible grid, C / W | 4 s / 2 s | partial | **inconclusive** | core.ass_load.50k_lines (parse only); based on run2 (contaminated) |
| G scroll, W: presented-frame interval p95 / p99 | 20 / 34 ms | partial | **inconclusive** | ui.grid_paint.50k_lines.1280x720 (CPU paint only); based on run2 (contaminated) |
| G select/reveal; E keystroke/composition update → visible, W | 50 ms | missing | **inconclusive** | no workload; E fixture (10,000-character lines, IME) not packaged |
| V exact indexed step, W | 100 ms | missing | **inconclusive** | no seek/frame-delivery workload; V fixture not packaged |
| V seeded random seek → correct frame, W / M | 500 / 1,500 ms | missing | **inconclusive** | no seek workload; V fixture not packaged |
| A waveform zoom → complete viewport, W / M | 50 / 250 ms | missing | **inconclusive** | no workload; A fixture not packaged |
| A spectrum zoom → complete viewport, W / M | 100 / 500 ms | missing | **inconclusive** | no workload (spectrum not built yet) |
| A/V offset p95 / max; drift (three 30-minute wired runs) | 40 / 80 ms; 20 ms | needs-calibration | **inconclusive** | needs calibrated audio-loopback/video capture (≤5 ms uncertainty); callback clocks cannot pass |
| G+V+A active peak CPU / attributable GPU memory | 1.5 GiB / 256 MiB | missing | **inconclusive** | no workload; no GPU allocation tracking |
| Memory growth over 30 open/edit/zoom/close cycles (final idle; fitted slope) | ≤32 MiB CPU / 16 MiB GPU; ≤0.5 / 0.25 MiB per cycle | missing | **inconclusive** | no cycle workload (helper resource growth included); Linux PSS sampling not implemented |

- `core.ass_load.50k_lines` (run2): worst run p95 52.857 ms, p99 56.359 ms against G open → parsed, selectable visible grid, W p95 2000 ms / p99 4000 ms: component within the bound; measures only in-process parse of a generated 50,000-Line ASS script (no process start, file read, model, grid or presentation); uncalibrated host; contaminated by other CPU work.
- `ui.grid_paint.50k_lines.1280x720` (run2): worst run p95 2.131 ms, p99 2.217 ms against G scroll, W: presented-frame interval p95 20 ms / p99 34 ms: component within the bound; measures only CPU paint of one 1280×720 Grid frame into a QImage at scattered scroll positions over 50,000 Lines (offscreen; no scene graph, GPU or presented frames); uncalibrated host; contaminated by other CPU work.

## Workloads that do not exist yet

- **seek**: V exact indexed step and seeded random seek (W/M).
- **frame delivery**: presented-frame timing for indexed and general playback; native presentation traces.
- **overlay rendering**: libass overlay render/composite timing per frame (part of seek/step/scroll endpoints).
- **audio latency**: A/V offset and drift; needs calibrated loopback capture.
- **helper resource growth**: 30-cycle memory growth for the application and its media/Lua helpers (PSS).

## Single-shot diagnostics (not harness workloads)

These are timings that backend tests print once per run. They have no repetitions or percentiles and no budget verdict.

- run1 `indexed-source-1` (load 4.32 at start): helper startup to handshake: 8.5 ms; frame transfer: 48 frames, 14745600 bytes, 41.8 ms (0.87 ms per frame)
- run1 `indexed-source-2` (load 4.12 at start): helper startup to handshake: 1.7 ms; frame transfer: 48 frames, 14745600 bytes, 43.7 ms (0.91 ms per frame)
- run1 `indexed-source-3` (load 4.03 at start): helper startup to handshake: 1.9 ms; frame transfer: 48 frames, 14745600 bytes, 46.0 ms (0.96 ms per frame)
- run1 `indexed-source-4` (load 3.95 at start): helper startup to handshake: 2.0 ms; frame transfer: 48 frames, 14745600 bytes, 45.4 ms (0.95 ms per frame)
- run1 `indexed-source-5` (load 4.04 at start): helper startup to handshake: 1.8 ms; frame transfer: 48 frames, 14745600 bytes, 41.7 ms (0.87 ms per frame)
- run1 `line-audition-1` (load 4.14 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run1 `line-audition-2` (load 4.12 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run1 `line-audition-3` (load 4.03 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run1 `line-audition-4` (load 3.95 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run1 `line-audition-5` (load 4.03 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run2 `indexed-source-1` (load 1.73 at start): helper startup to handshake: 1.7 ms; frame transfer: 48 frames, 14745600 bytes, 41.5 ms (0.86 ms per frame)
- run2 `indexed-source-2` (load 1.59 at start): helper startup to handshake: 1.6 ms; frame transfer: 48 frames, 14745600 bytes, 40.8 ms (0.85 ms per frame)
- run2 `indexed-source-3` (load 1.46 at start): helper startup to handshake: 1.6 ms; frame transfer: 48 frames, 14745600 bytes, 44.8 ms (0.93 ms per frame)
- run2 `indexed-source-4` (load 1.43 at start): helper startup to handshake: 1.7 ms; frame transfer: 48 frames, 14745600 bytes, 42.8 ms (0.89 ms per frame)
- run2 `indexed-source-5` (load 1.31 at start): helper startup to handshake: 1.8 ms; frame transfer: 48 frames, 14745600 bytes, 40.6 ms (0.85 ms per frame)
- run2 `line-audition-1` (load 1.73 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run2 `line-audition-2` (load 1.46 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run2 `line-audition-3` (load 1.43 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run2 `line-audition-4` (load 1.31 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
- run2 `line-audition-5` (load 1.45 at start): tail after logical stop: 149.0 ms, latency 54.0 ms
