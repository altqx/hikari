#!/usr/bin/env python3
"""CR1 #452 crash and issue reports mockup: per theme, three sheets.

1-crash: "HikariSub closed unexpectedly" at the start after a crash, with the report preview collapsed and
   expanded: the recovery (PU1's offer: Restore session, Autosaves…), the note, the report, and Send report,
   Open GitHub issue, Don't send and "Always send crash reports".
2-hang: the hang notice over the main window once it responds again, the same report for a freeze, the window
   on the next start after a freeze that ended with the app being closed, and the outcome toasts.
3-issue: Help > Report an issue as a form (Bug, Feature request, Question), what it attaches and its preview,
   and the Finish on GitHub page (the normal case and a report too long for a link).

Drawn with PK1's mockup kit (kit.py, as copied for PU2-PU7: the theme roles of src/ui/theme.cpp, DG1's frame, the
message box, the toast and IC1's icons from the tree). The main window behind the hang notice is AD3's screenshot
of today's HikariSub (qt 7788b8a7c). Versions, frames, settings and log lines are examples."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-cr1"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, box, field, segmented, window, ic, toast  # noqa: E402

kit.REPO_ICONS += ["report-issue", "last-session", "autosave", "frame-snapshot", "link", "help", "settings",
                   "show-in-folder", "open-subtitles"]
kit.GLYPHS.update({
    "check-big": '<circle cx="8" cy="8" r="6.5"/><g class="acc"><path d="M5 8.25l2 2 4-4.5"/></g>',
    "chevron-up": '<path d="M4.5 9.5l3.5-3.5 3.5 3.5"/>',
    "lock": '<rect x="3" y="7" width="10" height="7.5" rx="1"/><path d="M5 7V5a3 3 0 0 1 6 0v2"/>'
            '<g class="acc"><path d="M8 10v2"/></g>',
})


def cap(text, width=620):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=620):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def sect(title, extra=""):
    return f'<div class="sect"><span>{title}</span>{extra}</div>'


def area(text, height, placeholder=False, focus=False):
    cls = "ta" + (" ph" if placeholder else "") + (" focus" if focus else "")
    return f'<div class="{cls}" style="height:{height}px">{text}</div>'


def disclosure(label, summary, open_):
    return (f'<div class="disc2">{ic("chevron-down" if open_ else "chevron-right", 14)}<span class="dl">{label}</span>'
            f'<span class="dsum">{summary}</span></div>')


def checkbox(text, on, extra=""):
    return f'<div class="cb">{box(on)}<span class="cbt">{text}</span>{extra}</div>'


# ---------------------------------------------------------------------------------------------
# Report content

VERSION = "0.1.0-beta.1"
LINUX_FRAMES = [
    ("hikarisub", "0x1f3a2c"), ("hikarisub", "0x1f2e10"), ("hikarisub", "0x0e81f4"), ("libQt6Core.so.6", "0x2c41b8"),
    ("libQt6Core.so.6", "0x2c3f02"), ("libQt6Gui.so.6", "0x1a90c4"), ("libQt6Quick.so.6", "0x3b7710"),
    ("other", "0x4d2a0"), ("libc.so.6", "0x29d90"), ("hikarisub", "0x0a1e35"),
]
MORE_FRAMES = [
    ("hikarisub", "0x0a1c90"), ("libQt6Core.so.6", "0x1e8a44"), ("libQt6Gui.so.6", "0x2207d8"),
    ("libQt6Quick.so.6", "0x41c2e0"), ("libQt6Core.so.6", "0x2b9f16"), ("hikarisub", "0x09f7a2"),
    ("libc.so.6", "0x2a28b"), ("hikarisub", "0x09e215"),
]
HANG_FRAMES = [
    ("libc.so.6", "0x10b2fd"), ("hikarisub", "0x31c0a8"), ("hikarisub", "0x31b7e2"), ("hikarisub", "0x2f04c6"),
    ("hikarisub", "0x0e7a19"), ("libQt6Core.so.6", "0x2c41b8"), ("libQt6Core.so.6", "0x2c3f02"),
    ("libQt6Gui.so.6", "0x1a90c4"), ("libc.so.6", "0x29d90"), ("hikarisub", "0x0a1e35"),
]
SETTINGS = [
    ("theme", "dark"), ("layout", "typesetting"), ("language", "en"), ("video renderer", "libass"),
    ("audio player", "pipewire"), ("spellchecker", "on, en_US"), ("grammar check", "off"),
    ("autosave", "every 1 edit, keep 3"), ("automation scripts", "12 bundled, 2 user"), ("usage data", "off"),
]
LOG = [
    ("14:28:03", "Info", "Opened file1.ass (412 lines)"),
    ("14:28:04", "Info", "Opened file2.mkv: 1920×1080, 23.976 fps, H.264"),
    ("14:28:06", "Info", "Audio from file2.mkv: AAC 2.0, 48 kHz"),
    ("14:29:51", "Warning", "Font “‹font›” is not installed; using Liberation Sans"),
    ("14:31:10", "Info", "Automation: ran “Apply karaoke template” on 31 lines"),
    ("14:31:40", "Info", "Autosaved file1.ass"),
    ("14:32:07", "Info", "Visual tool: Perspective"),
]


def frames_text(frames, total):
    out = [f"  #{i:<2} {m:<18} +{o}" for i, (m, o) in enumerate(frames)]
    if total > len(frames):
        out.append(f"  … {total - len(frames)} more frames")
    return "\n".join(out)


def report_text(kind="crash", full=False):
    if kind == "crash-github":
        return ("<b>### What happened</b>\nI had just switched to the Perspective tool on a sign line.\n\n"
                "<b>### Steps to reproduce</b>\n_(HikariSub closed unexpectedly; no steps given)_\n\n"
                "<b>### Crash record</b>\n```\nSIGSEGV (signal) in " + VERSION + "\n"
                + "\n".join(f"#{i} {m} +{o}" for i, (m, o) in enumerate(LINUX_FRAMES[:4])) + "\n…\n```\n\n"
                "<b>### System</b>\nHikariSub " + VERSION + " (prerelease) · AppImage · Linux 6.12 · x86_64 · "
                "Qt 6.11.2\n\n<b>### Settings</b>\n" + ", ".join(f"{k}: {v}" for k, v in SETTINGS[:4])
                + ", …\n\n<b>### Log</b>\n&lt;details&gt;&lt;summary&gt;Last 200 lines&lt;/summary&gt;\n\n```\n…\n"
                + "\n".join(f"{t} {lv} {m}" for t, lv, m in LOG[4:]) + "\n```\n&lt;/details&gt;")
    if kind == "crash":
        head = (f"<b>Crash</b>\n  SIGSEGV (signal) in {VERSION}\n" + frames_text(LINUX_FRAMES if not full else LINUX_FRAMES + MORE_FRAMES, 18))
    else:
        head = (f"<b>Freeze</b>\n  The window did not respond for 12 s (watchdog at 10 s), {VERSION}\n"
                "  Main thread:\n" + frames_text(HANG_FRAMES[:9], 14))
    system = (f"<b>System</b>\n  HikariSub {VERSION} (prerelease) · AppImage\n"
              "  Linux 6.12 · x86_64 · Qt 6.11.2 · 1 screen at 1.5x")
    settings = "<b>Settings</b>\n" + "\n".join(f"  {k:<19}{v}" for k, v in (SETTINGS if full else SETTINGS[:6])) + (
        "" if full else "\n  … 4 more")
    log = ("<b>Log</b> (last 200 lines)\n  …\n"
           + "\n".join(f"  {t} {lv:<8}{m}" for t, lv, m in LOG))
    return "\n\n".join([head, system, settings, log])


# ---------------------------------------------------------------------------------------------
# 1. HikariSub closed unexpectedly


def recovery():
    return (sect("Your work")
            + '<div class="rec">' + ic("last-session", 16) + '<div class="rect">'
            '<div>The last session had 2 documents: Episode 03.ass, Signs.ass.</div>'
            '<div class="muted">Unsaved work in 1 document was autosaved.</div></div>'
            f'<div class="recb">{btn("Restore session", small=True)}{btn("Autosaves…", small=True)}</div></div>')


def report_section(kind, open_, note, summary):
    body = sect("Report", f'<span class="sectnote">{ic("lock", 14)}Nothing is sent until you press Send report</span>')
    body += area(note, 58, placeholder=not note.startswith("I "), focus=False)
    body += disclosure("What the report contains", summary, open_)
    if open_:
        body += (f'<div class="rbox"><pre>{report_text(kind)}</pre>'
                 f'<span class="qb" title="Copy the report">{ic("edit-copy", 14)}</span>'
                 '<span class="sbar" style="top:6px;height:96px"></span></div>'
                 '<div class="rnote">File names become file1, file2… Your subtitle file, its text and your folders are '
                 'never in a report.</div>')
    return body


def crash_window(open_, note=False):
    head = ('<div class="mb">' + ic("notice-error", 32, "big err") + '<div class="mbtext">'
            '<div class="mbmain">HikariSub crashed yesterday at 14:32.</div>'
            '<div class="mbdetail">A report helps find the cause. You can read all of it before it goes.</div>'
            '</div></div>')
    text = ("I had just switched to the Perspective tool on a sign line." if note
            else "What were you doing when it closed? (optional)")
    summary = "crash record, system, settings, last 200 log lines"
    body = head + recovery() + report_section("crash", open_, text, summary)
    foot = (f'<span class="lead">{checkbox("Always send crash reports", False)}</span>'
            + btn("Send report", primary=True, focus=True) + btn("Open GitHub issue") + btn("Don’t send"))
    return window("HikariSub closed unexpectedly", body, foot, 620)


def sheet_crash(key):
    th = THEMES[key]
    return "".join([
        f'<h1>CR1 crash report: the start after a crash ({th["name"]})</h1>',
        '<p class="note">The window opens over Home at the first start after a crash, before anything else asks. '
        'Your work is PU1’s crash offer, unchanged: Restore session reopens the last session, Autosaves… opens the '
        'autosave list. Closing the window keeps that offer on Home and in the startup bar, as today.</p>',
        '<p class="note">The report is built on this machine and shown in full. Send report sends it once, even '
        'with usage data off. Open GitHub issue opens a new Bug issue with the same text (and the note as What '
        'happened). Don’t send, Esc and × delete the record and send nothing.</p>',
        row(col(crash_window(False), "Collapsed: the summary line lists the four parts. The note is optional and "
                                     "goes in the report as typed."),
            col(crash_window(True, note=True),
                "Expanded: the whole report, scrollable, as it will be sent (crash record as module + offset, "
                "system, settings as ids, the last 200 lines of the Log). Copy puts it on the clipboard.")),
        '<h2>The whole report, as the box scrolls</h2>',
        row(col(f'<div class="rbox full"><pre>{report_text("crash", full=True)}</pre></div>',
                "Settings are ids and values from a fixed list (no folders, fonts or file names). Log lines keep "
                "their time and level; file names become file1, file2… in order of first use, a quoted font name "
                "becomes ‹font›, and quoted Line text becomes ‹text›.", 620),
            col(f'<div class="rbox full"><pre>{report_text("crash-github")}</pre></div>',
                "What Open GitHub issue puts in the Bug form’s fields (Markdown). The log goes in a collapsed "
                "&lt;details&gt; block.", 620)),
    ])


# ---------------------------------------------------------------------------------------------
# 2. Hang


def freeze_window(open_, after_close=False):
    if after_close:
        main = "HikariSub stopped responding yesterday at 14:32 and was closed."
        detail = "It froze for 10 seconds or more. A report shows where it was stuck."
        title = "HikariSub stopped responding"
        glyph = ic("notice-warning", 32, "big warn")
        extra = recovery()
    else:
        main = "HikariSub stopped responding for 12 seconds at 14:32."
        detail = "It works again. A report shows where it was stuck."
        title = "Report a freeze"
        glyph = ic("notice-warning", 32, "big warn")
        extra = ""
    head = (f'<div class="mb">{glyph}<div class="mbtext"><div class="mbmain">{main}</div>'
            f'<div class="mbdetail">{detail}</div></div></div>')
    body = head + extra + report_section("freeze", open_, "What were you doing when it froze? (optional)",
                                         "freeze record, system, settings, last 200 log lines")
    foot = (f'<span class="lead">{checkbox("Always send crash reports", False)}</span>'
            + btn("Send report", primary=True, focus=True) + btn("Open GitHub issue") + btn("Don’t send"))
    return window(title, body, foot, 620)


def sheet_hang(key):
    th = THEMES[key]
    hang = toast("warning", "HikariSub stopped responding for 12 seconds",
                 "It works again. Send a report of where it was stuck?",
                 btn("Report…", small=True, flat=True))
    sent = toast("info", "Crash report sent. Thank you.")
    always = toast("info", "HikariSub closed unexpectedly. A crash report was sent.",
                   "“Always send crash reports” is on.",
                   btn("Show report", small=True, flat=True) + btn("Settings", small=True, flat=True))
    failed = toast("warning", "The crash report could not be sent", "No connection. It is kept for the next start.",
                   btn("Try again", small=True, flat=True) + btn("Open GitHub issue", small=True, flat=True))
    return "".join([
        f'<h1>CR1 hang watchdog: the freeze notice and report ({th["name"]})</h1>',
        '<p class="note">A watchdog thread checks the window every second. After 10 s without an answer it records '
        'the main thread’s stack, as a crash does. Nothing can be drawn while the window is stuck. When it '
        'answers again, a warning toast stays until it is closed; Report… opens the report. The notice also goes '
        'to the Log.</p>',
        f'<div class="app"><div class="stack">{hang}</div></div>',
        row(col(freeze_window(True), "Report… from the notice: the same report, with the freeze record (how long, "
                                     "and the main thread’s frames) in place of the crash record."),
            col(freeze_window(False, after_close=True),
                "When the app was closed while frozen (killed, or closed from the desktop), the next start shows "
                "this window with the recovery, as after a crash."), top=24),
        '<h2>Outcomes</h2>',
        row(col(f'<div style="width:360px">{sent}</div>', "Send report succeeded (information toast, fades).", 360),
            col(f'<div style="width:360px">{failed}</div>', "Send report failed: the record stays for the next "
                "start; nothing is retried in the background.", 360),
            col(f'<div style="width:360px">{always}</div>', "With “Always send crash reports” on: no window, the "
                "report (without a note) goes at start; PU1’s offer handles recovery. Show report opens the window "
                "read-only.", 360)),
    ])


# ---------------------------------------------------------------------------------------------
# 3. Report an issue

KINDS = ["Bug", "Feature request", "Question"]


def thumb():
    return '<div class="thumb"></div>'


def issue_form(kind, filled=True, expanded=False):
    rows = [f'<div class="frow2"><span class="flabel2">Kind</span>{segmented(KINDS, kind, small=True)}</div>']
    if kind == "Bug":
        fields = [("Title", "The Perspective tool’s handles jump when the video is zoomed", 0),
                  ("What happened", "With the video zoomed to 200 %, dragging a corner handle of the Perspective "
                                    "tool moves it twice as far as the pointer.", 54),
                  ("Steps to reproduce", "1. Open a video and zoom to 200 %<br>2. Choose the Perspective tool on a "
                                         "line<br>3. Drag a corner handle", 70),
                  ("What you expected", "The handle stays under the pointer.", 36)]
        ph = ["A short summary", "What went wrong?", "1. …", "What should have happened?"]
    elif kind == "Feature request":
        fields = [("Title", "", 0), ("What you’d like", "", 70), ("What it would help with", "", 54)]
        ph = ["A short summary", "Describe the feature.", "What are you trying to do?"]
    else:
        fields = [("Title", "", 0), ("Your question", "", 108)]
        ph = ["A short summary", "Ask away. Name the panel or command it is about."]
    for (label, text, h), p in zip(fields, ph):
        val = text if filled and text else p
        control = (field(val, None, placeholder=not (filled and text)).replace('class="fld', 'class="fld wide', 1)
                   if h == 0 else area(val, h, placeholder=not (filled and text)))
        rows.append(f'<div class="frow2 top"><span class="flabel2">{label}</span><div class="fcol">{control}</div></div>')
    body = "".join(rows)
    att = sect("Attached")
    sys_open = expanded
    att += ('<div class="attrow">' + ic("about", 16) + '<div class="attm">'
            + disclosure("System and settings", "HikariSub 0.1.0-beta.1 · AppImage · Linux 6.12",
                         sys_open) + '</div><span class="muted sm">Always included</span></div>')
    if sys_open:
        sys_text = (f"HikariSub {VERSION} (prerelease) · AppImage\nLinux 6.12 · x86_64 · Qt 6.11.2 · 1 screen at "
                    "1.5x\n\n" + "\n".join(f"{k:<19}{v}" for k, v in SETTINGS[:5]) + "\n… 5 more")
        att += f'<div class="rbox in"><pre>{sys_text}</pre></div>'
    if kind == "Bug":
        att += ('<div class="attrow">' + box(True) + '<div class="attm">'
                + disclosure("Log excerpt", "last 200 lines, file names replaced", expanded) + '</div></div>')
        if expanded:
            log = "…\n" + "\n".join(f"{t} {lv:<8}{m}" for t, lv, m in LOG[2:])
            att += f'<div class="rbox in"><pre>{log}</pre><span class="sbar" style="top:6px;height:40px"></span></div>'
        att += ('<div class="attrow">' + box(True) + '<div class="attm"><span class="dl">Screenshot of the window'
                f'</span></div>{thumb()}</div>')
        att += ('<div class="attrow">' + box(False) + '<div class="attm"><span class="dl">Subtitle file</span>'
                '<span class="dsum">Episode 03.ass, as a .zip to drag into the issue</span></div></div>')
    body += att
    foot = (btn("Copy", lead=True) + btn("Open on GitHub", primary=True, focus=filled) + btn("Cancel"))
    return window("Report an issue", body, foot, 620, resizable=True)


def finish_page(too_long):
    steps = []
    if too_long:
        steps.append(("notice-warning", "warn", "The report is too long for a link, so it is on the clipboard.",
                      "GitHub opened an empty Bug form. Paste into What happened (Ctrl+V).",
                      btn("Copy again", small=True)))
    else:
        steps.append(("check-big", "", "The Bug form opened in your browser, filled in.",
                      "Check it there and press Create. You need a GitHub account.", ""))
    steps.append(("frame-snapshot", "", "Add the screenshot",
                  "Drag it into the description, or copy it and paste it there.",
                  btn("Copy image", small=True) + btn("Show file", small=True)))
    body = "".join(
        f'<div class="step">{ic(g, 20, c)}<div class="stt"><div class="sttitle">{t}</div><div class="muted">{d}</div>'
        f'{"<div class=stb>" + b + "</div>" if b else ""}</div></div>' for g, c, t, d, b in steps)
    body = '<div class="mbmain" style="margin:6px 0 4px">Finish on GitHub</div>' + body
    foot = btn("Open the form again", lead=True) + btn("Done", primary=True, focus=True)
    return window("Report an issue", body, foot, 520)


def sheet_issue(key):
    th = THEMES[key]
    return "".join([
        f'<h1>CR1 Help › Report an issue ({th["name"]})</h1>',
        '<p class="note">A window on DG1’s frame (Help › Report an issue and About’s Report an issue). The fields '
        'are the ones in the repository’s issue forms (.github/ISSUE_TEMPLATE/bug.yml, feature.yml, '
        'question.yml), so Open on GitHub fills the matching form. System and settings are always attached; the '
        'log excerpt and the screenshot are ticked by default for a bug and can be unticked; the subtitle file '
        'only when ticked. Copy puts the issue on the clipboard as Markdown. Nothing is sent from the app.</p>',
        row(col(issue_form("Bug"), "A bug, filled in. Each attachment opens to its preview."),
            col(issue_form("Bug", expanded=True), "The previews open: system and settings as ids, the log with "
                                                  "file names replaced, as they go into the issue.")),
        row(col(issue_form("Feature request", filled=False), "Feature request: the form’s two fields; only system "
                                                             "and settings are attached."),
            col(issue_form("Question", filled=False), "Question: one field."), top=24),
        '<h2>After Open on GitHub (the window stays open)</h2>',
        row(col(finish_page(False), "The screenshot can’t go in a link, so it is offered to drag or paste.", 520),
            col(finish_page(True), "Too long for a link (about 8 000 characters): the text goes to the clipboard "
                                   "and the blank Bug form opens.", 520)),
    ])


# ---------------------------------------------------------------------------------------------

CSS = """
.muted{color:%(muted)s}
.sm{font-size:12px}
.sect{display:flex;align-items:center;gap:8px;margin:18px 0 8px;font-weight:650;padding-bottom:4px;box-shadow:inset 0 -1px %(line)s}
.sectnote{display:flex;align-items:center;gap:5px;margin-left:auto;font-weight:400;font-size:12px;color:%(muted)s}
.rec{display:flex;gap:10px;align-items:flex-start}
.rec>.ic{margin-top:2px}
.rect{flex:1;display:flex;flex-direction:column;gap:3px;line-height:1.4}
.recb{display:flex;gap:6px;flex:none}
.ta{background:%(field)s;border:1px solid %(line)s;border-radius:1px;padding:5px 8px;line-height:1.4;overflow:hidden}
.ta.ph{color:%(muted)s}
.ta.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.disc2{display:flex;align-items:center;gap:5px;margin-top:10px;min-width:0}
.dl{white-space:nowrap}
.dsum{color:%(muted)s;font-size:12px;margin-left:6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.rbox{position:relative;margin-top:6px;background:%(field)s;outline:1px solid %(line)s;padding:8px 34px 8px 10px;
      height:237px;overflow:hidden}
.rbox.in{height:auto;margin:4px 0 2px 26px}
.rbox.full{height:auto;width:620px;padding-right:10px}
.rbox pre{font:12px/17px 'Liberation Mono',Consolas,monospace;color:%(text)s;white-space:pre}
.rbox pre b{font-weight:700;color:%(text)s}
.rbox .qb{position:absolute;right:6px;top:6px}
.rnote{font-size:12px;color:%(muted)s;margin-top:6px;line-height:1.4}
.frow2{display:flex;align-items:center;gap:10px;margin:6px 0}
.frow2.top{align-items:flex-start}
.frow2.top .flabel2{padding-top:6px}
.flabel2{width:150px;flex:none}
.fcol{flex:1;min-width:0}
.attrow{display:flex;align-items:center;gap:10px;min-height:30px;margin-top:2px}
.attm{flex:1;min-width:0;display:flex;align-items:center}
.attm .disc2{margin-top:0}
.thumb{width:96px;height:60px;background:url(%(body)s) 0 0/96px 60px no-repeat;outline:1px solid %(line)s;flex:none}
.step{display:flex;gap:12px;align-items:flex-start;padding:12px 0;box-shadow:inset 0 -1px %(line)s}
.step:last-child{box-shadow:none}
.step>.ic{margin-top:1px}
.stt{display:flex;flex-direction:column;gap:3px;line-height:1.4}
.sttitle{font-weight:650}
.stb{display:flex;gap:6px;margin-top:6px}
"""


def main():
    kit.CSS += CSS
    for key in THEMES:
        (HERE / f"cr1-{key}-1-crash.html").write_text(kit.page(key, sheet_crash(key)))
        (HERE / f"cr1-{key}-2-hang.html").write_text(kit.page(key, sheet_hang(key)))
        (HERE / f"cr1-{key}-3-issue.html").write_text(kit.page(key, sheet_issue(key)))


if __name__ == "__main__":
    main()
