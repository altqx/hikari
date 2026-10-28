# Crash, freeze and issue reports

CR1 ([#452](https://github.com/altqx/hikari/issues/452)). Departure: CR1-crash-reports ([compatibility-decisions.md](compatibility-decisions.md)).

Nothing leaves the machine until you press **Send report** or **Open GitHub issue** (or turn on **Always send crash reports**). What would go is shown first, in full.

## What is recorded

- **A crash.** A signal handler (Linux) or an unhandled-exception filter (Windows) writes `crash-record.txt` to the cache folder. The handler is always installed.
- **A freeze.** A watchdog thread checks every second that the window still answers. After 10 seconds without an answer it records the main thread's stack in `hang-record.txt` (Linux: a signal to the main thread; Windows: the thread suspended for a moment and its stack walked). The record is rewritten every 5 seconds while the window stays stuck. If the window answers again, the record is removed and the freeze is offered as a notice.
- **The Log's last lines.** Every Log entry is scrubbed as it comes, and the last 200 lines (64 KB at most, each line cut at 1,000 characters) are kept in `reports/log-tail.txt`, so the next start has the Log of a run that crashed. The `reports` folder in the cache folder is readable by your account only. A freeze report on its way when the program closes is kept there (`crash-report-sending.json`) until an answer comes, and offered again at the next start.

A record holds the error (the signal, the Windows exception, or `APP_HANG` and how long), the version and up to 32 frames, each a module name and an offset. A module is named only when it is one of the program's files or the system's; any other module is `other`.

## The report

The start after a crash, or after the program was closed while frozen, turns the record into a report, kept in `reports/crash-report.json` until it is answered:

- the record;
- the system: version, channel, package, OS and its version, architecture, Qt version, screens and scale, the layout preset;
- a settings summary: ids and values from a fixed list (theme, language, renderer, player, autosave, usage data and so on), never a folder, font or name;
- the last 200 Log lines.

Before a Log line goes into a report:

| In the Log | In the report |
|---|---|
| a name of a file that is open (subtitles, video, audio, keyframes), or any other file name | `file1.ass`, `file2.mkv`… (the extension kept, numbered in order of first use) |
| a quoted font name | `‹font›` |
| quoted text, or the text of a Line of an open file | `‹text›` |
| a path | `‹folder›` |

`crash_report_tests` (`noSubtitleTextPathOrFileName`, `scrubberRules`) checks these rules.

## The windows

- **HikariSub closed unexpectedly** (or **HikariSub stopped responding**, for a freeze it was closed in) opens at the first start after it, over Home. Your work holds PU1's crash offer: Restore session and Autosaves…. A note (optional, up to 4,000 characters) goes in as typed. What the report contains expands to the whole report as it will be sent, with Copy.
- **Report a freeze**: the freeze notice's Report… opens the same report for a freeze the window came back from.
- **Send report** sends it once to PostHog (below). **Open GitHub issue** opens a new Bug issue with the same text, the note as What happened; a report too long for a link (about 8000 characters) goes to the clipboard and the Bug form opens with only its title filled in. If no browser opens, the issue goes to the clipboard and the window stays open. **Don't send**, Esc and × drop the record and send nothing.
- **Always send crash reports** (in the window, and in Settings > Updates and privacy > Privacy, shown only in builds with the PostHog key; off by default) sends the report, without a note, at the start after a crash and when the window comes back from a freeze. No window opens; a notice says it was sent, with Show report.
- A report that could not be sent stays for the next start. Nothing retries in the background.

**Help › Report an issue** (and About's Report an issue) is a form for a Bug, a Feature request or a Question. Its fields are the issue forms' in `.github/ISSUE_TEMPLATE` (bug.yml, feature.yml, question.yml), so **Open on GitHub** fills the matching form in the browser. System and settings always go; for a bug the Log excerpt and a screenshot of the window are ticked and can be unticked, and the subtitle file goes only when ticked. The Log excerpt and the subtitle file are the ones from when the form opened. A link cannot carry the screenshot or the file, so Finish on GitHub offers them to drag in: the screenshot (PNG) with Copy image and Show file, the subtitle file as a `.zip` with Show file. Both stay in the cache folder (`reports/issue`) until the form closes (Done, Esc or ×). **Copy** puts the issue on the clipboard as Markdown. The app sends nothing.

## What is sent

Send report posts to PostHog's `/batch/` with AN1's client ([analytics.md](analytics.md)):

- With usage data on, AN1 already sent the record alone as `$exception` at the start; Send report adds only `crash_report` (the note, the Log lines, the settings summary), linked to it by `report_id`. Turning usage data off drops what it had queued, so Send report then sends the record again.
- With usage data off (or `DO_NOT_TRACK`), Send report sends both, under a one-off random id: pressing it is consent for that one report. No install id is made or kept.

Only release builds carry the PostHog key; without it the window has no Send report, and Open GitHub issue still works.

## Symbols and symbolize.py

Release packages carry no debug info. Package CI builds with `HIKARI_DEBUG_SYMBOLS=ON` and keeps the symbols as release assets of their own:

- `hikarisub-symbols-<version>-linux-x86_64.tar.xz`: each ELF file's debug info, split out by `packaging/linux/make-packages.sh` (`<path>.debug`, and under `.build-id/`);
- `HikariSub-<version>-windows-x64-symbols.zip`: each module the build has a PDB for, beside its PDB.

`tools/crash/symbolize.py` turns a report's frames into functions and lines:

```
tools/crash/symbolize.py REPORT [--symbols DIR] [--version V]
```

REPORT is a crash record, a report as the window shows it or an issue carries it, or the `$exception` event from PostHog (JSON). Without `--symbols` it downloads the version's symbol asset with `gh` into `~/.cache/hikari-symbols/`. It uses `llvm-symbolizer` (both platforms; Windows modules need it), else `addr2line`.

Each package CI run crashes its payload on purpose (`HIKARISUB_CRASH_TEST=segv`) and checks that the record symbolizes with the symbols it made; the record and the output are in the run's evidence artifact.

## For developers

- `HIKARISUB_CRASH_TEST=segv` or `abort` crashes once the handler is in; `hang` freezes the window for 15 seconds, 3 seconds after it is up. The record's path goes to stderr.
- `HIKARISUB_HANG_WATCHDOG_MS=<ms>`: another freeze threshold; `0` turns the watchdog off.
- Code: `src/app/crash_record.cpp` (the record), `hang_watchdog.cpp`, `crash_report.cpp` (the report, the scrubber, the GitHub issue), `crash_reports.cpp` (what the windows do), `src/ui/CrashReportWindow.qml`, `src/ui/ReportIssueWindow.qml`.
- Tests: `crash_report_tests` (against a fake endpoint on the loopback, and `hikari_crash_probe` for real crashes and freezes).
