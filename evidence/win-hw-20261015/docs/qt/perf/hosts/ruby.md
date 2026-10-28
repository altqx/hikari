# Performance host: ruby (Windows)

Recorded 2026-10-15 by hand in PowerShell (CIM/WMI, `powercfg`, the MMDevices registry, `nvidia-smi`); there is no Windows port of `tools/perf/host-inventory.sh` yet. Machine-readable copy: `ruby.json`.
Calibration: **uncalibrated**. No measurement tooling calibration, presentation traces or A/V loopback capture are bound to this host; its results are observations.

This record is in the evidence branch `evidence/win-hw-20261015`, not on `qt`; binding it as the Windows reference host is the owner's decision.

## Hardware

| Item | Value |
| --- | --- |
| CPU | AMD Ryzen 5 5600 6-Core Processor |
| Cores / threads | 6 physical / 12 logical (2 per core); base 3501 MHz; L2 3 MiB, L3 32 MiB |
| Board | Gigabyte B450 AORUS ELITE |
| RAM | 31.92 GiB visible; 4 × 8 GiB DDR4 configured at 3000 MT/s (two kits: CMU16GX4M2C3200C16, and a CL17 DDR4-2400 pair) |
| Storage (system, repository, payload) | C: NTFS on Seagate BarraCuda 510 SSD ZP250CM30001 (NVMe) |
| Other disks | HS-SSD-FUTURE 1024G (NVMe SSD), ST6000VX0003-1ZH110 (SATA HDD 6 TB), WDC WD5000AAKX (SATA HDD 500 GB) |
| GPU | NVIDIA GeForce GTX 1070 Ti, 8 GiB |
| GPU driver | 582.66 (WDDM 32.0.15.8266, 2026-06-09) |
| Displays | LM30-E330C 2560×1080 @ 144 Hz, 100 %, primary; AOC 22E1W 1920×1080 @ 60 Hz, 100 % |

## Software

| Item | Value |
| --- | --- |
| OS | Windows 11 Pro 26H2, build 26300.9457, x64 |
| Qt graphics | platform `windows`; scene graph: Qt's default on Windows (D3D11 RHI), `QSG_RHI_BACKEND` not set |
| Audio | WASAPI. Active endpoints: Speakers (Razer BlackShark V2 X USB), 22E1W (NVIDIA HDA, HDMI) |
| Editor output | PortAudio, WASAPI by default or DirectSound (Options > Audio > Audio output) |
| General player | Qt Multimedia (FFmpeg backend 7.1.5, WASAPI sink) |
| Power | Balanced plan (`381b4222-…`), maximum processor state on AC 100 %; a desktop on mains power, no battery |

## Pinned build

| Item | Value |
| --- | --- |
| Package | `hikarisub-qt-windows-payload`, run 38686574527, portable, app version 0.1.0-ci.29 |
| Commit | `3e2b145d3de0f2bf7aad077ed610c07e9b390a8c` (`ci/evidence-H2`: qt `d8d90b078` plus W2 and decisions 49–53) |
| `HikariSub.exe` SHA-256 | `d3276860dc8108a3439b43baa0685f201a5802fb888ae5ff3760e81744c84a00` |
| Compiler | MSVC 2022 x64 (CI, `windows-x64-release`) |
| Qt | 6.11.2 (`msvc2022_64`) |
| FFmpeg (Qt Multimedia) | 7.1.5 (avcodec 61.19.101, avformat 61.7.103, avutil 59.39.100), shipped DLLs |
| FFmpeg (FFMS2 helper) | 7.1.2#5 (vcpkg override), static in the media helper |
| FFMS2 | 5.1.0-hikari-45d5f721 |
| libass | 0.17.5-altqx-4f5ea436 (API 0x1705020; HarfBuzz 14.5.0, FriBidi 1.0.17, DirectWrite font provider) |
| PortAudio | 19.7#100 |
| KDDockWidgets | 2.4.1#101 |
| LuaJIT | 2026-09-08#3 |
| vcpkg baseline | `ee6a47dac031930715bea2e6dfb9ad60caaa5d83` |
| Perf harness | not available for this build: `hikari_core_perf` and `hikari_ui_perf` are not in the package or any CI artifact for this head |

## Against the reference class

The accepted class is four physical cores, 8 GiB RAM, SSD, integrated graphics and a 60 Hz display (docs/qt/performance.md).

| Item | Class | This host | Matches |
| --- | --- | --- | --- |
| physicalCores | 4 | 6 | **no** |
| ramGiB | 8 | 31.92 | **no** |
| ssd | true | true | yes |
| integratedGraphics | true | false | **no** |
| refresh60Hz | 60 | 144 (primary), 60 | **no** (primary) |
