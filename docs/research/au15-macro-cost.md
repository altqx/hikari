# AU15: what a macro run costs on a big file

[#849](https://github.com/altqx/hikari/issues/849), 2026-11-11, main `f4e91e4f2`. Investigation only; no code changed.

## Method

- A throwaway gtest linked like `hikari_backends_lua_helper_tests` drove the real helper (`LuaScriptHost`). It timed each phase with `steady_clock`. The Lua macros stamped their own start and end with `clock_gettime(CLOCK_MONOTONIC)`, so the IPC on each side of the macro is split out.
- **Files:** real subtitle files, not synthetic ones.
  - **KFX:** the first 20,000 Lines of a 32,944-Line templater output (7.4 MB, ~300 characters of text per Line), plus the whole file.
  - **Typesetting:** a 57,117-Line typeset episode (55.9 MB; 46k drawings, 660 clips). A 500-Line spread of it was used for ASSFoundation.
- **Macros:** "noop", "edit one" (one Line's text), "read all" (read every Line) and "touch all" (change every Line's text).
- **Host and runs:** release build, Linux, host load 6–17 (other builds were running), so treat the numbers as ±30%. Ranges are over 2–3 runs.
- **Proxies:** "encode" and "decode" were measured in-process with `lua::encodeSnapshot`, `decodeSnapshot`, `encodeMacroResult` and `decodeMacroResult`. They stand in for the helper's side.

## Results

### 20k-Line KFX (snapshot 15.4 MB each way)

| Phase | Time |
|---|---|
| `snapshotForMacro` | 14–25 ms |
| encode snapshot / decode snapshot | 5–20 ms / 4–7 ms |
| send → macro starts (encode, IPC, helper decode, subs) | 27–85 ms |
| macro ends → result back (Lua → result, encode, IPC, decode) | 27–75 ms |
| **round trip, noop macro** | **70–160 ms** |
| macro body: read all / touch all | 28–31 ms / 43–50 ms |
| `applyMacroResult`, nothing changed | 7–8 ms |
| **`applyMacroResult`, edit one** | **1.58–1.85 s** |
| **`applyMacroResult`, touch all** | **1.90–2.10 s** |

### Bigger files (the macro can't run; app-side phases timed with a built result)

| File | Lines | Snapshot | snapshot + encode | apply, edit one | apply, touch all |
|---|---|---|---|---|---|
| whole KFX | 32,944 | 25.5 MB | 35–65 ms | 5.3–6.9 s | 5.7–5.8 s |
| typesetting | 57,117 | 113.5 MB | 85–230 ms | 14.5 s | 16.4 s |

### ASSFoundation (500 typesetting Lines, ~970 characters each, mostly drawings)

| Phase | Time |
|---|---|
| script load requiring DependencyControl, a-mo.LineCollection and ASSFoundation | 0.84–1.13 s |
| `LineCollection(subs, sel)` | 4–17 ms |
| `ASS:parse` on 499 Lines (1,010 sections) | 366–409 ms, **~0.8 ms a Line** |
| `data:commit()` and `replaceLines()` | +a few ms (noise) |

## What the numbers say

1. **Apply dominates, and it grows with the square of the Line count.**
   - Editing one Line costs 1.6–1.9 s at 20k Lines, 5–7 s at 33k and 14.5 s at 57k. That's 2.9× the Lines for 8.5× the time.
   - The cause is in `applyMacroResult` (`src/application/macro_transaction.cpp`): for **every** kept Line it calls `doc.lines()` and scans all Lines to find that Line's position and its predecessor's. That's O(n²) even when nothing moved.
   - A no-change result skips this (7 ms).
2. **The snapshot transfer is cheap.** A noop round trip moves 15.4 MB each way in 70–160 ms, under 10% of a one-Line apply.
3. **Big files can't run macros at all.**
   - `LuaScriptHost` keeps `HelperHost`'s default `maxOutstandingBytes` of 16 MB, so `run()` returns false once the snapshot is over 16 MB. The snapshot is about 2× the file size, so that's any file over ~8 MB, such as KFX past ~20k Lines.
   - A snapshot over `kMaxPayload` (64 MB) can't be framed even with that limit raised. The 57k-Line file needs 113.5 MB.
   - `AutomationShell::run` returns false on the refusal, and no user-facing message was found on that path (inferred; needs a repro).
4. **ASSFoundation's parse costs ~0.8 ms a Line on dense typesetting.** A typical sign selection (≤100 Lines) parses in under 0.1 s, less than the script's own 0.8–1.1 s load. A whole-file pass on 57k Lines would take ~45 s, but that isn't a common use.

## Go / no-go

- **NB11 (snapshot deltas): no-go for speed.** The transfer is a small part of the cost. Deltas would only matter for files whose snapshot can't fit in one frame (>64 MB). Halving the snapshot (it carries each Line's text and its raw ASS) covers most of those more simply.
- **ASSFoundation native backend: no-go.** Parsing is ~0.8 ms a Line; load time is the bigger cost for typical use, and AU2's compile cache already targets that. Revisit only if a real whole-file ASSF macro is reported slow.
- **Go, new cards:**
  - **Apply in linear time:** place kept Lines from one position map instead of rescanning per Line. Expected: 1.8 s → tens of ms at 20k Lines, and the 57k case from 14.5 s to well under a second.
  - **Big files must run macros:**
    - raise the script host's outstanding limit to `kMaxPayload`;
    - send each Line once (drop the duplicate raw text from the wire or rebuild it in the helper), which roughly halves the snapshot;
    - tell the user when a Document is still too large, instead of failing silently.
