# Setup: Windows real hardware, 2026-10-15

| Item | Value |
| --- | --- |
| Build under test | `hikarisub-qt-windows-payload` from run [38686574527](https://github.com/altqx/hikari/actions/runs/38686574527) ("Qt rewrite — Windows packages", success), evidence head `3e2b145d3de0f2bf7aad077ed610c07e9b390a8c` (`ci/evidence-H2`) |
| Run from | `C:\hw-test\payload\HikariSub.exe` (portable) |
| `HikariSub.exe` SHA-256 | `d3276860dc8108a3439b43baa0685f201a5802fb888ae5ff3760e81744c84a00` |
| `hikari-media-helper.exe` SHA-256 | `38a56586ddeb170ef4f8316ad2d3e1d6b493c24dac6ec1dfb89a91f4b3a11972` |
| Version the app shows | Help > About: "version 0.1.0-ci.29 (portable)"; **no commit is shown** ([about-version.png](about-version.png)) |
| Windows | Windows 11 Pro 26H2, build 26300.9457, x64 |
| Host | RUBY: AMD Ryzen 5 5600 (6 cores / 12 threads), 32 GiB DDR4 at 3000 MT/s, Gigabyte B450 AORUS ELITE; full record [docs/qt/perf/hosts/ruby.md](../../../docs/qt/perf/hosts/ruby.md) |
| GPU and driver | NVIDIA GeForce GTX 1070 Ti, driver 582.66 (WDDM 32.0.15.8266, 2026-06-09), 8 GiB |
| Monitor 1 | LM30-E330C, `\\.\DISPLAY1`, primary, 2560×1080 @ 144 Hz, 100 % |
| Monitor 2 | AOC 22E1W, `\\.\DISPLAY2`, right of the primary (origin 2560,93), 1920×1080 @ 60 Hz, 100 %; set to **150 %** for D3, back to 100 % for H3 |
| Audio outputs | Speakers (Razer BlackShark V2 X USB), USB headset; 22E1W (NVIDIA HD Audio), HDMI; inactive: LM30-E330C (NVIDIA HDA), Realtek Digital Output |
| Audio input | Microphone (USB Headphone Set) |
| Input languages | en-US, Thai, Korean (Microsoft IME), Japanese (Microsoft IME); Korean and Japanese added by the owner for E6 |
| Screen reader | NVDA 2026.2 portable (owner's copy, OneCore voice), restarted with `--log-level=12` for E6, quit before H3 |
| Profile | The Qt app's profile (`%LOCALAPPDATA%\HikariSub\HikariSub`) did not exist before this session (only the legacy app's `HikariSub\Legacy`); everything in it was made by these runs |

Captures are physical-pixel `CopyFromScreen` crops of the app's own windows; window geometry and DPI come from `GetWindowRect`/`GetDpiForWindow` read per-monitor DPI aware. Synthetic input, where used, is SendInput (real key and mouse events through the Windows input stack), never a Qt event.
