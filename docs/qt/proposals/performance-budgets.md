# Proposed performance and resource budgets

For [Set measurable performance and resource budgets for the rewrite](https://github.com/altqx/hikari/issues/36). **Unverified proposal for human review.** The painted-grid choice is settled; these proposed requirements govern its follow-up and the rewrite only after approval.

## Evidence and reference-machine choice

The [d99b429e grid sample](https://github.com/altqx/hikari/blob/d99b429e7a6e16107fa6d6bf80cc5fba04a6264d/HikariSub/prototypes/qml-grid/README.md) records Windows 11 build 26200, Ryzen 5 5600, GTX 1070 Ti, Qt/PySide6 6.11.2, Python 3.12.14, D3D11 and 1420×860 at DPR 1. Its [painted p95 interval of 17.31 ms](https://github.com/altqx/hikari/blob/d99b429e7a6e16107fa6d6bf80cc5fba04a6264d/HikariSub/prototypes/qml-grid/evidence/windows-d3d11.json) measures Python-delivered frame signals, **not GPU duration or input-to-photon latency**. Both renderers remained allocated; no comparative memory conclusion follows.

Reference options, neither selected:

* **Lower baseline, recommended:** four physical CPU cores, 8 GiB RAM, SSD, integrated graphics and a 60 Hz display. This is a proposed class, not verified hardware.
* **Current-machine class:** the recorded Ryzen/GTX machine; inventory RAM, storage, drivers and power settings before qualification. Those details were not recorded in the sample.

Apply the same gates to Windows and Linux on the chosen hardware or documented equivalents. Name OS/build, Linux compositor and Wayland/X11 session, graphics/audio backend and device. No Linux performance result exists here. A Windows pass cannot certify Linux or macOS.

## Fixtures and conditions

Pin build/compiler/Qt/dependencies, fonts, locale, theme, fixture hashes/generator seed and command sequence. Use release builds, AC power, recorded power profile, no concurrent workload, 1536×768 logical viewport at 100% scale and 60 Hz; repeat native interaction checks at 150%. Log background activity and thermal throttling rather than deleting outliers silently.

**C:** first process launch/open after reboot, empty application caches; document cold-state method. **W:** same fixture preloaded once, caches retained, fresh process for startup/open. **M:** required frame/tile absent from application cache, index/PCM available and OS cache warm. Report preparation/indexing separately; never hide it inside warm results.

Use 20 independent C/W startup/open samples. Other latency workloads use five repetitions, ten seconds of warmup for W only, and at least 1,000 operations total; report each run's nearest-rank p95/p99 and maximum, without pooling away a failing run. Record raw event/frame identities and dropped trace samples. Latency ends at the matching visible result: correlate native presentation traces or calibrated capture with input timestamps; submission/callback timestamps alone cannot claim end-to-end success. GPU time is a separate diagnostic.

Fixtures: **G**, pinned 50,000-row ASS generator plus hashed real-script cases up to 50,000 rows; mixed scripts/tags, long lines, raw/hidden tags, selection/filter/sort. **E**, G with 10,000-character lines and native Japanese composition, Arabic and Thai input. **V**, generated ten-minute 1080p H.264/B-frame CFR and VFR media, two-second maximum GOP, pinned frame identities and matching subtitles. **A**, one-hour 48 kHz stereo PCM with impulses/tones, plus 44.1 kHz resampling; fixture-only 2048-point FFT, rectangular window, 512-sample hop, pinned zoom sequence. Fixtures need packaging/verification.

## Proposed thresholds

Release thresholds would block acceptance on the chosen reference. Stretch targets are aspirational. Unless specified otherwise, values are p95; latency p99 must remain within twice the listed bound. Startup/open use p95 only.

| Operation / endpoint | Release | Stretch |
| --- | --- | --- |
| Empty startup → input-ready workspace, C / W | 4 s / 1.5 s | 2 s / 0.75 s |
| G open → parsed, selectable visible grid, C / W | 4 s / 2 s | 2 s / 1 s |
| G scroll, W: presented-frame interval p95 / p99 | 20 / 34 ms | 17 / 25 ms |
| G select/reveal; E keystroke/composition update → visible, W | 50 ms | 25 ms |
| V exact indexed step, W | 100 ms | 50 ms |
| V seeded random seek → correct frame, W / M | 500 / 1,500 ms | 250 / 750 ms |
| A waveform zoom → complete viewport, W / M | 50 / 250 ms | 25 / 125 ms |
| A spectrum zoom → complete viewport, W / M | 100 / 500 ms | 50 / 250 ms |
| A/V, three 30-minute wired runs: absolute offset p95 / maximum; drift between first/last ten-second means | 40 / 80 ms; 20 ms | 20 / 40 ms; 10 ms |
| G+V+A active peak CPU / attributable GPU memory | 1.5 GiB / 256 MiB | 1 GiB / 128 MiB |

IME latency starts when its event reaches the app; separately record candidate-window behavior. For A/V, loop V three times with synchronized flashes/impulses; use calibrated audio-loopback/video capture with ≤5 ms measurement uncertainty, otherwise inconclusive. Callback clocks alone cannot pass. Exclude two seconds after start/seek from drift; require reacquisition within 500 ms. Test both playback modes. Wrong frames, corrupted composition or underruns cannot pass by being fast. 4K/longer-GOP/HEVC/AV1 remain reported stress workloads, not dropped capabilities.

For memory, report Windows private bytes/Linux PSS separately, acknowledging differing accounting, plus GPU allocation tracking. After five priming open/edit/zoom/close cycles, measure 30 cycles with identical bounded cache policy and five seconds quiescence each. Release: final idle growth ≤32 MiB CPU/16 MiB GPU; fitted growth over the last ten cycles ≤0.5/0.25 MiB per cycle. Stretch: halve these bounds. Zero growth is not assumed; report disk-cache size/eviction separately.

Measurement boundaries follow completed [video](https://github.com/altqx/hikari/blob/26d7ac2a940c9f0edf2c6e65f5b1db05ea1a2507/docs/research/qtquick-video.md), [audio](https://github.com/altqx/hikari/blob/7e1aad3ba4f7ac217b9bb467a297035bc0639abd/docs/research/audio.md) and [testing](https://github.com/altqx/hikari/blob/ee59a12872f3d9e0ddeb203e5fa3c09566b158ce/docs/research/qml-testing.md) research. Native tools and corpus coverage still require implementation; nothing here was benchmarked.

**Two decisions:** accept these starting release/stretch budgets, or name adjustments? Use the lower baseline class (**recommended**) or current-machine class, then identify the actual Windows/Linux reference machines?
