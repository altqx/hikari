# Performance host: sapphire (Linux)

Recorded 2026-10-04T14:15:24+07:00 by `tools/perf/host-inventory.sh`. Machine-readable copy: `sapphire.json`.
Calibration: **uncalibrated**. No measurement tooling calibration, presentation traces or A/V loopback capture are bound to this host; its harness results are observations.

## Hardware

| Item | Value |
| --- | --- |
| CPU | AMD Ryzen 5 5600 6-Core Processor |
| Cores / threads | 6 physical / 12 logical (2 per core); max 4470.5078 MHz; boost enabled; L3 32 MiB (1 instance) |
| RAM | 31.26 GiB (swap 62.52 GiB); module speed and layout need root (dmidecode); not read |
| Storage (repository) | nvme0n1 HS-SSD-FUTURE 1024G nvme via /dev/mapper/root (btrfs) |
| Storage (build tree) | nvme0n1 HS-SSD-FUTURE 1024G nvme via /dev/mapper/root (btrfs) |
| Other disks | sda ST6000VX0003-1ZH110 5.5T HDD; sdb WDC WD5000AAKX-001CA0 465.8G HDD; zram0  31.3G SSD; nvme1n1 Seagate BarraCuda 510 SSD ZP250CM30001 232.9G SSD; nvme0n1 HS-SSD-FUTURE 1024G 953.9G SSD |
| GPU | 08:00.0 VGA compatible controller [0300]: NVIDIA Corporation GP104 [GeForce GTX 1070 Ti] [10de:1b82] (rev a1) [driver nvidia] |
| GPU driver | NVIDIA GeForce GTX 1070 Ti, 580.178.04, 8192 MiB, 180.00 W |
| OpenGL | NVIDIA GeForce GTX 1070 Ti/PCIe/SSE2, 4.6.0 NVIDIA 580.178.04 |
| Vulkan devices | NVIDIA GeForce GTX 1070 Ti |
| Displays | HDMI-A-1 AOC 22E1W 1920x1080 @ 60.00000 Hz, scale 1 |

## Software

| Item | Value |
| --- | --- |
| OS | Omarchy (build 4.0.0.r6713.ga85e29a), kernel 7.2.5-4-omarchy x86_64 |
| Session | wayland on Hyprland; compositor Hyprland v0.56.2 |
| Qt graphics | platform wayland;xcb; scene graph opengl (Qt default on Linux) |
| Audio server | PulseAudio (on PipeWire 1.6.9); default sink `alsa_output.usb-Razer_Razer_BlackShark_V2_X_USB_000000000001-00.analog-stereo` |
| Audio sinks | Razer BlackShark V2 X USB Analog Stereo (s24le 2ch 48000Hz); AOC 22E1W (HDA NVidia) (s32le 2ch 48000Hz); Starship/Matisse HD Audio Controller Digital Stereo (IEC958) (s32le 2ch 48000Hz) |
| Editor output | PortAudio, ALSA host API (the Linux build links libasound only) |
| General player | Qt Multimedia (FFmpeg backend; PipeWire or PulseAudio audio sink) |
| Power | profile performance; amd-pstate-epp (active); governors performance x12; EPP performance; boost 1; no battery or mains supply reported: a desktop on mains power |
| CPU temperature at inventory | +63.2°C |

## Pinned build

| Item | Value |
| --- | --- |
| Git revision | `bdf8ae522b43a1ed64328b0bb53664441e443f52` in `~/Work/hikari-qt` (0 modified tracked files when recorded; HEAD committed 2026-10-04T13:40:39+07:00) |
| Binaries built | hikarisub 2026-10-04T13:40:21+07:00; media helper 2026-10-04T13:29:22+07:00; hikari_core_perf 2026-10-04T11:51:10+07:00; hikari_ui_perf 2026-10-04T13:34:24+07:00 |
| Commits newer than the oldest binary | bdf8ae52 2026-10-04T13:40:39+07:00 A1 row: the in-use cache trim is the approved A1-trim-in-use; 69ae6195 2026-10-04T13:40:31+07:00 F1-win-charsets: decode the charsets wx had no converter for with ICU on Windows; record O1-destroyed-results and A1-trim-in-use under R3; 3121b555 2026-10-04T13:35:52+07:00 S3 capture: press and release a probe macro's menu item apart; 9ccfdb0d 2026-10-04T13:35:23+07:00 Select lines logs wxRegEx's invalid-expression and match errors; coverage for N6 on Windows; the F1-utf16-bom row states what legacy Linux did; 8e0283b0 2026-10-04T13:33:11+07:00 Merge integration stream: audio; e9e3bc80 2026-10-04T13:28:21+07:00 Merge integration stream: search; 530d6155 2026-10-04T13:26:03+07:00 Merge integration stream: wave-4 settings into the registry; c9565732 2026-10-04T13:24:27+07:00 Merge integration stream: legacy text files and charsets; 0561ed82 2026-10-04T13:19:27+07:00 N6 hot-plug test: say what the wait measured; 4d7f17b6 2026-10-04T13:18:47+07:00 Audio box: hand the video's index over, read blocks into a kept buffer (A1); c2018ae6 2026-10-04T13:17:10+07:00 N6: DirectSound's unplugged stream only stalls; record it per host API; a99b0596 2026-10-04T13:16:02+07:00 N6: real device loss and return on Windows through a hot-plugged USB audio device; 80094b63 2026-10-04T13:13:48+07:00 Search stream: regex match errors, wxDir listings per platform, dialog minors; db21f0a7 2026-10-04T12:59:43+07:00 Move the wave-4 interim INI keys into the settings registry; 3be9e157 2026-10-04T12:57:22+07:00 S3 capture: hover the probe row under the Automation label; 8e243ba6 2026-10-04T12:55:43+07:00 N6: a stream that stops being active without a stop is DeviceLost; the hot-plug test finds the returning device by what was missing; 0f653fd3 2026-10-04T12:51:59+07:00 N6 hot-plug task: match the name Windows gives the QEMU USB audio device; 909a0c31 2026-10-04T12:48:59+07:00 Legacy text files: iconv on Linux, wx's code pages on Windows, Rules.txt CR, paste through FileOpen; 68d6e57b 2026-10-04T12:40:30+07:00 S3 capture: close the app-drawn menu with a Grid click before each probe macro; 6bfd4c52 2026-10-04T12:38:28+07:00 N6: real device loss and return through a hot-plugged USB device; fix the MSVC-ambiguous SpellChecker calls in tests; 2d21c1a3 2026-10-04T12:31:13+07:00 N6: pick the test device by id too, and a WASAPI Winix task; c8db96c4 2026-10-04T12:16:59+07:00 Record the R3 extension and F1-utf16-bom approvals; ef73c9fe 2026-10-04T12:13:58+07:00 Merge A1: Audio; 94237b53 2026-10-04T12:09:47+07:00 Merge F1: Find and replace |
| Build tree | `~/Work/hikari-qt/out/build/ubuntu-x64-release` (Release) |
| Compiler | ~/.local/lib/kache/shims/c++ = GCC 16.2.1 20260810 |
| Qt | 6.11.2; Qt Multimedia FFmpeg: libavcodec.so.61.19.101,libavformat.so.61.7.103,libavutil.so.59.39.100 |
| FFmpeg (FFMS2 helper) | 7.1.2#5 |
| FFMS2 | 5.1.0-hikari-45d5f721 |
| libass | 0.17.5#102 (FreeType 2.14.3, HarfBuzz 14.5.0, FriBidi 1.0.17, Fontconfig 2.17.1#2) |
| PortAudio | 19.7#100 |
| Perf harness fingerprint | `AMD Ryzen 5 5600 6-Core Processor|12|Linux 7.2.5-4-omarchy|gcc 16.2.1 20260810` |

## Against the reference class

The accepted class is four physical cores, 8 GiB RAM, SSD, integrated graphics and a 60 Hz display (docs/qt/performance.md).

| Item | Class | This host | Matches |
| --- | --- | --- | --- |
| physicalCores | 4 | 6 | **no** |
| ramGiB | 8 | 31.26 | **no** |
| ssd | true | true | yes |
| integratedGraphics | true | false | **no** |
| refresh60Hz | 60 | 60 | yes |
