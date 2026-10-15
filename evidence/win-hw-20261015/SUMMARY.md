# Windows real hardware at 3e2b145d3 (2026-10-15)

Branch `evidence/win-hw-20261015` (from `origin/qt` d8d90b078). Evidence only; no code.

## Setup

Package `hikarisub-qt-windows-payload` from run [38686574527](https://github.com/altqx/hikari/actions/runs/38686574527) (evidence head `3e2b145d3`), portable. `HikariSub.exe` SHA-256 `d3276860dc8108a3439b43baa0685f201a5802fb888ae5ff3760e81744c84a00`. About shows "0.1.0-ci.29 (portable)", **no commit**.
Host RUBY: Windows 11 Pro 26H2 build 26300.9457; Ryzen 5 5600; 32 GiB; GTX 1070 Ti, driver 582.66. Monitors: LM30-E330C 2560×1080 @ 144 Hz, 100 %, primary; AOC 22E1W 1920×1080 @ 60 Hz, 100 % (150 % for D3). Audio: Razer BlackShark V2 X USB headset; 22E1W HDMI.
Details: [setup/host.md](setup/host.md). The owner did the physical steps: IME install and typing, mouse drags, Win+P, unplugging, listening.

## E6 #188: tag popup with a real IME ([e6/results.md](e6/results.md))

Observed with the Microsoft Korean and Japanese IMEs. Keys went through the real IMEs (SendInput), and the owner made one Korean run on the physical keyboard (end state captured).
- **Observed:**
  - preedit inline under the open tag list (ㅎ/하/한; かんじ, 感じ);
  - the list does not commit the composition (field value unchanged while composing);
  - a Space commit gives `{\한 `, and the list then closes (no match);
  - Japanese Enter confirms 感じ without committing the Line;
  - Japanese Esc and Down go to the IME, and the list's selection is untouched;
  - NVDA announces the list with the Korean IME on ("\alpha, Transparency, 1 of 20") and the moved selection with the Japanese IME on ("\be, Edge blur, 2 of 20").
- **Needs a decision (Korean):**
  - K7: Enter during composition commits 한 **and** commits the Line and advances to a new Line (the Microsoft Korean IME passes the Enter on).
  - K8: Esc during composition removes the preedit and **reverts the whole Line draft**. Without a composition, Esc with the list open only closes the list.
- **Seen once, not reproduced:** J2, an early commit (`かんｊ`) when the IME's first prediction window took focus.
- **Missing:** the decision on K7/K8. The ledger note on completion fixtures is unchanged.

## D3 #207: dock and panel chrome on real monitors ([d3/results.md](d3/results.md))

- **Observed:**
  - free-part move onto the 150 % monitor;
  - the size kept in logical pixels (706×582 → 1059×873);
  - GetDpiForWindow 144, rendering crisp;
  - resize from the shadow at 150 %;
  - title drag back with the translucent panel and drop highlight, docked;
  - restart with the second monitor gone: the panel placed fully on the LM30 and usable.
- **Snap (D3-windows-snap, decision 50):** unchanged on real hardware. No Snap preview or snap at the right or top edge; the panel stays floating (Snap is on for the user).
- **Failed: live monitor removal.** The floating panel ended at (2387,107) 816×199, with only 173 px on the remaining screen (run 2). In run 1 it came back as an 800×672 window with the panel drawn in only its top-left two-thirds and the rest transparent. On restore it stays straddling both monitors.
- **Usability note:** the owner's first title drag did not dock. The recording shows the free part's system move, so the drag likely started right of the title.
- **Missing:** a fix and re-check for live monitor removal; D1 #152 and the sway/Hyprland gate (Linux).

## H2 #125: listening, Windows ([h2/results.md](h2/results.md))

Line audition in the app (A4) through PortAudio WASAPI/DirectSound, plus Qt Multimedia playback. `hikari_h2_audition` was not built: it needs the full vcpkg graph on this PC.
- **Yes:** A2–A8 (line, 500 ms before/after start and end, 44.1 kHz on the output, stop), A11b on both host APIs, and B0–B5, B7b, B8.
- **No (N6):**
  - the editor output keeps the device it first opened: an audition after the Windows default changed still played on the old device;
  - the app offers no device choice, only the host API; switching the host API reopens on the current default.
- **Recorded observations:**
  - A9/B6: neither path follows a default change during playback.
  - A11 WASAPI: the unplug stops the sound and shows "Audio player failed."
  - A11 DirectSound: Windows moves the stream to the new default.
  - B7: Qt Multimedia goes silent while the video keeps running, with 165 `AUDCLNT_E_DEVICE_INVALIDATED` lines in the log.
- **Not observable:** A1 (no device id or rate in the UI); A10 as written; the finished-event numbers.

## H3 #126: Windows host ([h3/results.md](h3/results.md), [host record](../../docs/qt/perf/hosts/ruby.md))

Host bound as `docs/qt/perf/hosts/ruby.md/.json` in this branch. Everything is **uncalibrated**; the host is outside the reference class.
- **Warm startup** to window plus "Line text": median 929 ms, p95 964 ms (n=20).
- **50,000-row open** to the Grid reporting 50,000 rows: median 1753 ms, p95 1795 ms (n=20). Generated fixture, not the pinned G.
- **10-min 1080p playback:** app private memory 550 → 603 MiB (+4.7 MiB/min, slowing); GPU memory ~179 MiB, flat; helpers flat.
- **Not run:**
  - Grid paint, frame lookup, step, seek, frame delivery, overlay: no Windows perf harness build.
  - Keystroke latency: no presentation or ETW traces.
  - Cold starts.
  - A/V: needs calibrated loopback capture (light sensor and audio into one capture device, ≤5 ms).

## Follow-ups

Two D3 fixes were pushed to `qt` after this session: `366189ea5` (live monitor removal) and `7812c180f` (whole header docks on Windows). Their status, CI results and what is left: [FOLLOW-UPS.md](FOLLOW-UPS.md).
