#!/usr/bin/env python3
"""PU7 #309 update, import and workspace popups mockup: per theme, four sheets.

1-updates: "A new version is available" (download and install; downloading; the AUR variant with its copyable
   command; the prerelease the AUR package does not carry; the release page only), the Installing update window
   and Settings > Updates on PF1's Settings window (#337).
2-import: "Import legacy settings" as a 3-step wizard (installation, review with the conflicts filter, import and
   Roll back) and the Roll back question.
3-workspace: Save layout as (new name, a name in use, a refused name), Manage layouts (rename in place, the
   delete question) and Move panel (Move and Resize sections).
4-hotkeys: Settings > Hotkeys with Import legacy hotkeys, its outcome toasts, and the hotkey capture's refusal
   and conflict (PK1's form, against the message box the card names).

Drawn with PK1's mockup kit (kit.py: the theme roles of src/ui/theme.cpp, DG1's frame, the popover frame, the
message box and the toast, IC1's icons from the tree) and PF1's Settings window (evidence/w6-pf1-mockup gen.py:
the 220-wide page list, sections and rows). Versions, paths, settings and keys are examples."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu7"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, box, field, combo, segmented, window, ic, message_box, toast  # noqa: E402

kit.REPO_ICONS += ["settings", "appearance", "editor", "settings-video", "settings-audio", "convert",
                   "script-properties", "automation", "import", "folder-open", "layout-save", "layout-manage",
                   "edit", "delete", "restore-default", "filter", "panel-move", "language", "link", "whats-new"]
# PF1's two new icons (#337), and a busy ring for the installing window.
kit.GLYPHS.update({
    "settings-grid": '<rect x="1.5" y="2.5" width="13" height="11" rx="1"/><path d="M1.5 5.5h13M5.5 2.5v11"/>'
                     '<g class="acc"><path d="M7.5 8.25h5M7.5 10.75h3"/></g>',
    "check-big": '<circle cx="8" cy="8" r="6.5"/><g class="acc"><path d="M5 8.25l2 2 4-4.5"/></g>',
    "chevron-up": '<path d="M4.5 9.5l3.5-3.5 3.5 3.5"/>',
})


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=520):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def link(text):
    return f'<span class="lnk2">{text}</span>'


def note_line(severity, text, extra=""):
    glyph, cls = kit.SEVERITY[severity]
    return f'<div class="nline">{ic(glyph, 16, cls)}<span>{text}</span>{extra}</div>'


def error(text):
    return f'<div class="ferr">{ic("notice-error", 14, "err")}<span>{text}</span></div>'


def tip(text, style):
    return f'<span class="tip" style="{style}">{text}</span>'


# ---------------------------------------------------------------------------------------------
# 1. Updates

NOTES = """<h4>New</h4><ul>
<li><b>Settings</b> is its own window, with ten pages and a search on Hotkeys.</li>
<li>Karaoke: split a syllable at the audio cursor, and time syllables by tapping.</li>
<li>Notices show as toasts at the bottom right; the <b>Log</b> panel keeps every one.</li>
</ul><h4>Fixed</h4><ul>
<li>The video no longer jumps a frame back after a seek past the last keyframe.</li>
<li>Saving a file with a BOM keeps it (<span class="mdl">#318</span>).</li>
</ul><p class="mdp">All changes: <span class="mdl">github.com/altqx/hikari/releases/tag/v0.1.0-beta.2</span></p>"""


def update_head(version="0.1.0-beta.2", have="0.1.0-beta.1", date="23 October 2026"):
    return (f'<div class="uhead">{ic("check-updates", 32)}<div class="col"><div class="uname">HikariSub {version}</div>'
            f'<div class="umuted">You have {have}. Released on {date}.</div></div></div>')


def notes_area(height=196, scroll=(4, 120), focus=False):
    top, h = scroll
    return (f'<div class="notes-area{" focus" if focus else ""}" style="height:{height}px"><div class="md">{NOTES}</div>'
            f'<span class="sbar" style="top:{top}px;height:{h}px"></span></div>')


def update_window(state="install"):
    body = update_head()
    if state == "aur-pre":
        body = update_head("0.2.0-beta.1", "0.1.2")
    body += notes_area(focus=False)
    lead = btn("Skip this version", lead=True)
    if state == "install":
        foot = lead + btn("Download and install", primary=True, focus=True) + btn("Remind me later")
    elif state == "downloading":
        body += (f'<div class="prog"><span class="bar"><span style="width:42%"></span></span>'
                 f'<span class="umuted">Downloading… 42 %</span></div>')
        foot = btn("Skip this version", lead=True).replace('class="b', 'class="b dis', 1) + btn(
            "Close", primary=True, focus=True)
    elif state == "page":
        foot = lead + btn("Open the download page", primary=True, focus=True) + btn("Remind me later")
    elif state == "aur":
        cmd = (f'<div class="aur"><div class="aurt">Update with your AUR helper</div>'
               f'<div class="umuted">You installed HikariSub from the AUR, so your package manager updates it.</div>'
               f'<div class="cmdrow">{field("yay -Syu hikarisub-stable-bin", None, mono=True).replace("class=\"fld", "class=\"fld wide", 1)}'
               f'{btn(ic("edit-copy") + "<span>Copy</span>", small=True)}</div></div>')
        body += cmd
        foot = lead + btn("Close", primary=True, focus=True) + btn("Remind me later")
    elif state == "aur-pre":
        body += note_line("warning", "The AUR package carries stable versions only. To try 0.2.0-beta.1, use the "
                          "AppImage from the release page.")
        foot = lead + btn("Open the release page", primary=True, focus=True) + btn("Remind me later")
    elif state == "requirement":
        body += note_line("warning", "This version needs Windows 10 version 1809 or later.")
        foot = lead + btn("Close", primary=True, focus=True) + btn("Remind me later")
    return window("A new version is available", body, foot, 600, resizable=True)


def installing_window():
    body = (f'<div class="inst"><span class="spin"></span><div class="col" style="gap:4px">'
            f'<div class="uname" style="font-size:14px">Installing HikariSub 0.1.0-beta.2…</div>'
            f'<div class="umuted">Enter your password when the system asks for it. HikariSub starts again when the '
            f'install is done.</div></div></div>')
    return (f'<div class="dlg" style="width:440px"><div class="dtb"><span class="dtitle">Install update</span></div>'
            f'<div class="dbody" style="padding-bottom:24px">{body}</div></div>')


# PF1's Settings window, with the Updates page after Hotkeys (MuseScore 4's order: Shortcuts, Update).
PAGES = [("general", "General", "settings"), ("appearance", "Appearance", "appearance"),
         ("editor", "Line editor", "editor"), ("grid", "Subtitle grid", "settings-grid"),
         ("video", "Video", "settings-video"), ("audio", "Audio", "settings-audio"),
         ("conversion", "Conversion", "convert"), ("properties", "Subtitle properties", "script-properties"),
         ("automation", "Automation", "automation"), ("hotkeys", "Hotkeys", "hotkeys"),
         ("updates", "Updates", "check-updates")]


def s_section(title, *rows, first=False):
    return (f'<div class="sec{" first" if first else ""}"><div class="stitle">{title}</div>'
            f'<div class="srows">{"".join(rows)}</div></div>')


def s_row(label, control, icon=None, note=None):
    slot = ic(icon) if icon else ""
    n = f'<div class="rnote">{note}</div>' if note else ""
    return (f'<div class="r"><span class="slot">{slot}</span><span class="rl">{label}</span>'
            f'<span class="rc">{control}</span></div>{n}')


def s_check(label, on=False, note=None, focus=False, new=False):
    n = f'<div class="rnote">{note}</div>' if note else ""
    mark = '<span class="newmark">moved here</span>' if new else ""
    return (f'<div class="r chk{" ring" if focus else ""}"><span class="slot">{box(on)}</span>'
            f'<span class="rl wrap">{label}{mark}</span></div>{n}')


def settings_window(current, body, height=560, extra=""):
    pages = "".join(f'<div class="pg{" cur" if k == current else ""}">{ic(i)}<span>{n}</span></div>'
                    for k, n, i in PAGES)
    foot = (f'{btn(ic("restore-default") + "<span>Reset page</span>" + ic("chevron-down", 14), lead=True)}'
            f'{btn("OK", primary=True)}{btn("Cancel")}{btn("Apply")}')
    return (f'<div class="dlg settings" style="width:880px;height:{height}px">{kit.titlebar("Settings", True)}'
            f'<div class="split"><div class="pages">{pages}</div><div class="pbody"><div class="form">{body}</div>'
            f'{extra}</div></div><div class="dfoot">{foot}</div></div>')


def updates_page(kind="install"):
    checking = [s_check("Check for updates automatically", True, new=True,
                        note="At most once a day, when HikariSub starts.")]
    if kind != "aur":
        checking.append(s_check("Stable versions only", True, new=True,
                                note="Off: prereleases (beta versions) are offered too."))
    checking.append(s_row("Skipped version", f'<span>0.1.0-beta.2</span>{btn("Offer it again", small=True)}',
                          note="Skip this version hides one version; a newer one is offered again."))
    secs = [s_section("Checking", *checking, first=True)]
    if kind == "install":
        secs.append(s_section("Installing", s_check(
            "Install updates automatically", True, new=True,
            note="Downloads the update in the background and installs it when you quit HikariSub.")))
    else:
        secs.append(s_section("Installing", '<div class="rnote" style="margin:0 0 0 24px">'
                    'You installed HikariSub from the AUR, so your package manager installs updates. '
                    'The beta or stable package you installed decides which versions you get.</div>'))
    return "".join(secs)


def updates_sheet(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU7 updates ({th["name"]})</h1>',
        '<p class="note">One window, “A new version is available”, on DG1’s frame: resizable (the notes grow), '
        'modeless, quiet when the automatic check opens it (it takes no keyboard). The release notes are SH3’s '
        'rendering (the What’s new Markdown: headings, lists, links that open the browser) in a scrolling area. '
        'Skip this version at the leading edge; the offer and Remind me later at the trailing one, in the '
        'platform’s order. The three update checkboxes leave it for Settings › Updates.</p>',
        '<h2>Download and install, and the download running</h2>',
        row(col(update_window("install"),
                "An installable kind (NSIS, AppImage, deb, rpm) with “Install updates automatically” on. "
                "Download and install is the default. Remind me later is legacy’s “Remind me in a week” "
                "(the next automatic check in seven days). Skip this version hides this version until a newer one "
                "comes: the automatic check stays quiet for it, Help › Check for updates still shows it. × and Esc "
                "close without either.", 600),
            col(update_window("downloading"),
                "The download in place of the button that started it. Skip is off while it runs. Close leaves "
                "it running; the update-ready bar takes over when it is done (unchanged).", 600)),
        '<h2>The AUR variant, and the offers without a download</h2>',
        row(col(update_window("aur"),
                "AUR installs: the same window, with “Update with your AUR helper” and the command in a copyable "
                "field (Copy puts it on the clipboard and says “Copied” for two seconds). The helper is the first of "
                "yay, paru found on PATH; yay when neither is. The package is the install’s (stable or beta). "
                "Close is the default.", 600),
            col(update_window("aur-pre"),
                "A prerelease the stable AUR package does not carry: the notes, the reason, and the release page "
                "(its AppImage).", 600)),
        row(col(update_window("page"),
                "A kind that does not install itself (the portable ZIP and tar.gz), or “Install updates "
                "automatically” off: the download page.", 600),
            col(update_window("requirement"),
                "A version this system cannot run: the requirement, no offer. Skip and Remind me later still "
                "work.", 600), top=24),
        '<h2>Installing update, and Settings › Updates</h2>',
        row(col(installing_window(),
                "After the main window closes, while the system’s password prompt and the package manager install "
                "a deb or rpm: DG1’s frame, fixed size, the translated title “Install update” (NT1’s title for this "
                "install’s result toast) in place of “HikariSub”. No Close: the wait ends with the package "
                "manager (as today); Alt+F4 and Esc do nothing.", 440),
            col(settings_window("updates", updates_page("install"), 560),
                "Settings › Updates, a page after Hotkeys on PF1’s window (#337). The three checkboxes, with "
                "a muted line under each saying what it does, and the skipped version with “Offer it again”. Staged "
                "with every other setting: OK or Apply writes them. The “moved here” tags are for this mockup "
                "only.", 860)),
        row(col(settings_window("updates", updates_page("aur"), 560),
                "The same page on an AUR install with a channel: no Stable versions only (the package decides) and "
                "no Install updates automatically.", 860), top=24),
    ])


# ---------------------------------------------------------------------------------------------
# 2. Import legacy settings

def stepper(at):
    names = ["Installation", "Review", "Import"]
    out = []
    for i, n in enumerate(names, 1):
        cls = "done" if i < at else "cur" if i == at else ""
        mark = (('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
                 'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
                if i < at else str(i))
        out.append(f'<span class="step {cls}"><span class="dot">{mark}</span><span>{n}</span></span>')
        if i < len(names):
            out.append(f'<span class="sline{" done" if i < at else ""}"></span>')
    return f'<div class="stepper">{"".join(out)}</div>'


ROOTS = [
    ("C:\\Users\\Kenta\\AppData\\Roaming\\HikariSub", "Config.txt, AudioConfig.txt, Hotkeys.txt, 3 style catalogs",
     "changed 14 September 2026"),
    ("D:\\Tools\\HikariSub 0.0.2 portable", "Config.txt, Hotkeys.txt", "changed 2 June 2026"),
]


def wizard(step, **kw):
    w = 780
    if step == 1:
        rows = "".join(
            f'<div class="rootrow{" cur" if i == 0 else ""}"><span class="radio{" on" if i == 0 else ""}"></span>'
            f'{ic("folder-open")}<div class="col" style="gap:2px;min-width:0"><span class="rp">{p}</span>'
            f'<span class="umuted">{f} · {d}</span></div></div>' for i, (p, f, d) in enumerate(ROOTS))
        body = (stepper(1)
                + '<div class="wtitle">Choose the legacy installation to import from</div>'
                + '<div class="umuted">HikariSub 0.0.x installations found on this computer. Nothing in them is '
                  'changed.</div>'
                + f'<div class="rootlist">{rows}</div>'
                + f'<div style="margin-top:8px">{btn(ic("folder-open") + "<span>Choose folder…</span>", small=True)}</div>'
                + ((f'<div class="frow" style="margin-top:14px"><span class="flabel" style="width:auto">Files that are '
                    f'not UTF-8 are read as</span>{combo("Windows-1252", 200)}</div>'
                    '<div class="rnote" style="margin:0">Shown only when a file is not UTF-8 and its encoding is not '
                    'certain.</div>') if not kw.get("rollback") else "")
                + (f'<div class="ibar inwin">{ic("about", 16)}<span class="ibt">The import of 22 October 2026, 10:14 '
                   f'is in effect.</span>{btn("Roll back…", small=True)}</div>' if kw.get("rollback") else ""))
        foot = btn("Next", primary=True, focus=True) + btn("Cancel")
        return window("Import legacy settings", body, foot, w, resizable=True)
    if step == 2:
        conflicts = kw.get("conflicts", True)
        # "All" is shown scrolled down to the records that are not conflicts.
        rows = REVIEW[3:] if not conflicts else [r for r in REVIEW if r[4] == "Conflict"]
        trs = ""
        for i, (on, name, legacy, now, status, sel) in enumerate(rows):
            chip = f'<span class="chip {status.lower()}">{status}</span>'
            dis = status not in ("Conflict",)
            b = box(on) if not dis else '<span class="box dis"></span>'
            cur = " cur" if (i == 1 and conflicts) or (i == 4 and not conflicts) else ""
            trs += (f'<div class="trow{cur}{" muted" if dis else ""}"><span class="tc0">{b}</span>'
                    f'<span class="tc1">{name}</span><span class="tc2">{legacy}</span><span class="tc3">{now}</span>'
                    f'<span class="tc4">{chip}</span></div>')
        seg = segmented(["Conflicts 14", "All 212"], "Conflicts 14" if conflicts else "All 212", small=True)
        tools = (f'<div class="rtools">{seg}{field("Search settings", 220, placeholder=True, icon="search")}'
                 f'<span class="rsum">{"9 of 14 chosen" if conflicts else "9 chosen"}</span></div>')
        detail = (DETAIL_CONFLICT if conflicts else DETAIL_UNRESOLVED)
        body = (stepper(2)
                + '<div class="wtitle">Review what to import</div>'
                + ('<div class="umuted">A conflict is a setting you have now that the legacy installation sets '
                   'differently. The proposed choice is ticked; untick what you want to keep.</div>' if conflicts else
                   '<div class="umuted">Every record read: the conflicts, and what is imported without a question, '
                   'left alone, or cannot be imported (with why).</div>')
                + tools
                + f'<div class="rtable"><div class="thead"><span class="tc0"></span><span class="tc1">Setting</span>'
                  f'<span class="tc2">Legacy</span><span class="tc3">Now</span><span class="tc4">Status</span></div>'
                  f'<div class="tbody">{trs}<span class="sbar" style="top:{4 if conflicts else 70}px;height:{90 if conflicts else 60}px"></span>'
                  f'</div></div>'
                + f'<div class="rdet">{detail}</div>'
                + f'<div class="rlinks">{link("Proposed choice")}{link("All conflicts")}{link("None")}</div>')
        foot = (btn("Back", lead=True) + btn("Import 9 settings", primary=True, focus=conflicts) + btn("Cancel"))
        return window("Import legacy settings", body, foot, w, resizable=True)
    # step 3
    kept = kw.get("kept", False)
    body = (stepper(3)
            + f'<div class="done3">{ic("check-big", 32)}<div class="col" style="gap:6px">'
              f'<div class="mbmain">9 settings imported.</div>'
              f'<div class="mbdetail">They take effect the next time HikariSub starts. Your current settings were '
              f'kept as a copy, so you can roll back to them.</div>'
            + ('<div class="mbdetail">2 settings you changed after the import keep your change: Theme, Audio '
               'volume.</div>' if kept else "")
            + '</div></div>')
    foot = btn("Roll back…", lead=True) + btn("Close", primary=True, focus=True)
    return window("Import legacy settings", body, foot, w, resizable=True)


# (ticked, setting, legacy, now, status, -)
REVIEW = [
    (True, "Audio delay", "120 ms", "0 ms", "Conflict", 0),
    (True, "Subtitle grid font", "Tahoma, 9", "Noto Sans, 10", "Conflict", 0),
    (False, "Video zoom at start", "75 %", "100 %", "Conflict", 0),
    (True, "Save every", "50 edits", "100 edits", "Conflict", 0),
    (True, "Hotkey: Save all", "Ctrl+Alt+S", "none", "Conflict", 0),
    (False, "Recent subtitles", "10 files (2 not found)", "4 files", "Conflict", 0),
    (True, "Theme", "Dark", "Dark", "Same", 0),
    (False, "Script hotkey: karaoke-templater-3", "Ctrl+K", "none", "Unresolved", 0),
    (False, "Audio spectrum", "on", "off", "Imported", 0),
    (False, "Use the DirectShow player", "on", "", "Not here", 0),
]
DETAIL_CONFLICT = ('<b>Subtitle grid font</b> · Config.txt › GRID_FONT_NAME = Tahoma, GRID_FONT_SIZE = 9 → '
                   'Settings › Subtitle grid › Font<br><span class="umuted">Proposed: the legacy value, because yours '
                   'is still the default.</span>')
DETAIL_UNRESOLVED = ('<b>Script hotkey: karaoke-templater-3</b> · Hotkeys.txt › Script karaoke-templater-3 = '
                     'Ctrl+K<br><span class="umuted">No loaded script has that file name and macro. It can be mapped '
                     'once the script is loaded (Settings › Hotkeys).</span>')


def rollback_box():
    return message_box("Roll back import", "Go back to the settings you had before the import of 22 October 2026?",
                       "2 settings you changed since will go back too: Theme, Audio volume. The legacy installation "
                       "is not changed.", "warning", btn("Roll back", danger=True) + btn("Cancel", primary=True, focus=True))


def import_sheet(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU7 Import legacy settings ({th["name"]})</h1>',
        '<p class="note">A window on DG1’s frame, resizable (780 × 560 to start, its place and size kept), modal '
        'to the main window as today. Three steps with a stepper at the top: choose the installation, review, '
        'import. Back and Next move between them; Cancel and × close without importing. What it does is O3’s '
        '(settings-import.md): nothing changes until Import, the import takes effect at the next start, and Roll '
        'back goes back to the settings before it.</p>',
        '<h2>Step 1: the installation</h2>',
        row(col(wizard(1),
                "The installations found (legacy’s Config folder: its files and when they last changed), the "
                "first chosen; Choose folder… adds one by hand (native picker). The encoding choice appears only "
                "when it matters. Next reads the chosen one (legacy’s “Read”).", 780),
            col(wizard(1, rollback=True),
                "Opened again while an import is in effect: an inline bar with Roll back… (the same question as "
                "step 3), so Roll back stays one click away.", 460)),
        '<h2>Step 2: the review, conflicts only and everything</h2>',
        row(col(wizard(2),
                "Opens on “Conflicts”: only the settings where the legacy value and yours differ, each with a tick "
                "(the proposed choice ticked). The details of the current row under the table: the legacy file and "
                "key, where it goes, and why it is proposed. Search filters by name, value and key. Proposed choice, "
                "All conflicts and None set the ticks of the rows shown. Import counts what it will write.", 780)),
        row(col(wizard(2, conflicts=False),
                "“All”: every record, with its status: Conflict, Same (nothing to do), Imported (no current value "
                "of yours: taken as it is), Unresolved (a path not found, a script not loaded: kept for later) and "
                "Not here (a Windows-only or retired setting). Only conflicts take a tick. The status replaces the "
                "eight-way “Show” list.", 780), top=24),
        '<h2>Step 3: imported, and Roll back</h2>',
        row(col(wizard(3),
                "The result, and Roll back… at the leading edge. Close is the default.", 780),
            col(rollback_box(),
                "PK1’s message box: the title names the command, Roll back in the danger colour, Cancel the "
                "default; the settings that would go back are named (legacy rollbackEdits).", 468)),
    ])


# ---------------------------------------------------------------------------------------------
# 3. Workspace: layouts and Move panel

def save_layout(state="new"):
    name = {"new": "Typesetting", "replace": "Timing", "bad": "Video"}[state]
    body = (f'<div class="frow"><span class="flabel">Name</span>{field(name, 260, focus=True)}</div>')
    if state == "replace":
        body += note_line("info", "Saving replaces the layout “Timing”.")
    elif state == "bad":
        body += error("“Video” is a preset’s name. Choose another.")
    label = "Replace" if state == "replace" else "Save"
    primary = btn(label, primary=True, focus=state != "bad")
    if state == "bad":
        primary = primary.replace('class="b pri', 'class="b pri dis', 1)
    return window("Save layout as", body, primary + btn("Cancel"), 380)


LAYOUTS = [("Timing", ""), ("Typesetting", ""), ("Karaoke, two monitors", ""),
           ("Old laptop", "made by a newer version of HikariSub")]


def manage_layouts(state="list"):
    rows = ""
    for i, (n, problem) in enumerate(LAYOUTS):
        cur = i == 1
        if cur and state == "rename":
            rows += (f'<div class="mrow cur">{field("Typesetting 1080p", None, focus=True).replace("class=\"fld", "class=\"fld wide", 1)}'
                     '</div>')
            continue
        acts = (f'<span class="macts"><span class="qb" title="Rename">{ic("edit")}</span>'
                f'<span class="qb" title="Delete">{ic("delete")}</span></span>') if cur else ""
        sub = f'<span class="umuted" style="font-size:12px">Cannot be used: {problem}</span>' if problem else ""
        rows += (f'<div class="mrow{" cur" if cur else ""}{" two" if problem else ""}"><div class="col" style="flex:1;min-width:0">'
                 f'<span{" class=dimt" if problem else ""}>{n}</span>{sub}</div>{acts}</div>')
    body = f'<div class="mlist">{rows}</div>'
    if state == "rename":
        body += '<div class="rnote" style="margin:6px 0 0">Enter renames, Esc puts the name back.</div>'
    elif state == "list":
        body += tip("Rename (F2)", "position:absolute;left:276px;top:118px")
    return (window("Manage layouts", body, btn("Close", primary=True, focus=state == "list"), 380,
                   style="position:relative"))


def delete_layout_box():
    return message_box("Delete layout", "Delete the layout “Typesetting”?",
                       "Tabs that use it keep their arrangement, under the preset it started from.", "warning",
                       btn("Delete", danger=True) + btn("Cancel", primary=True, focus=True), width=440)


def move_panel():
    body = (f'<div class="frow"><span class="flabel">Panel</span>{combo("Audio", 280)}</div>'
            f'<div class="msec"><div class="stitle">Move</div>'
            f'<div class="frow"><span class="flabel">Place</span>{combo("Below", 130)}'
            f'<span class="flabel" style="width:auto;margin-left:6px">next to</span>{combo("Video", 110)}</div>'
            f'<div class="frow" style="justify-content:flex-end">{btn("Move", small=True)}</div></div>'
            f'<div class="msec"><div class="stitle">Resize</div>'
            f'<div class="frow"><span class="flabel">Width</span>{spin("480", "px")}'
            f'<span class="flabel" style="width:auto;margin-left:14px">Height</span>{spin("220", "px")}</div>'
            f'<div class="frow" style="justify-content:flex-end">{btn("Resize", small=True)}</div></div>')
    return window("Move panel", body, btn("Close", primary=True), 404)


def spin(value, unit):
    return (f'<span class="fld spinf" style="width:104px"><span class="ftext num">{value}</span><span class="unit">{unit}</span>'
            f'<span class="arrows">{ic("chevron-up", 10)}{ic("chevron-down", 10)}</span></span>')


def workspace_sheet(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU7 layouts and Move panel ({th["name"]})</h1>',
        '<p class="note">All three are opened from menus (the layout switcher’s menu and View › Layout; the panel '
        'menu and View › Move panel), so by the small forms rule they are compact windows on DG1’s frame: fixed '
        'size, modeless, Esc closes. Save layout as and Manage layouts were in-window Controls dialogs; Move panel '
        'was a bare window.</p>',
        '<h2>Save layout as</h2>',
        row(col(save_layout("new"), "The current tab’s layout under a name; the field starts with the tab’s saved "
                                   "layout’s name, selected. Enter saves.", 380),
            col(save_layout("replace"), "A name in use (in any case): said under the field, and the button reads "
                                        "Replace.", 380),
            col(save_layout("bad"), "A preset’s name, an empty or too long one: the reason in the danger colour, the "
                                    "button off.", 380)),
        '<h2>Manage layouts</h2>',
        row(col(manage_layouts("list"),
                "The saved layouts in the order saved. The current row shows Rename and Delete as icon buttons "
                "(names on hover); F2 and Delete do the same from the keyboard, as today. A layout this version "
                "cannot use says why and can be deleted.", 380),
            col(manage_layouts("rename"),
                "Rename in place: the row becomes the name field (the separate Name field and Rename button go). "
                "A refused name says why under the row, as Save layout as does.", 380),
            col(delete_layout_box(),
                "Delete asks first now (a layout cannot be got back): PK1’s message box, Delete in the danger "
                "colour, Cancel the default.", 440)),
        '<h2>Move panel</h2>',
        row(col(move_panel(),
                "D1’s keyboard placement in two titled sections: Move (where, next to which panel; “next to” is off "
                "for Float) and Resize (the size in pixels, read from the panel when it is chosen). Each section has "
                "its own button, and the window stays open to move or resize again. The panel list follows the "
                "focused panel when opened from View › Move panel.", 404)),
    ])


# ---------------------------------------------------------------------------------------------
# 4. Hotkeys

HOTKEYS = [
    ("Global", None, None),
    ("Save", "Ctrl+S", ""), ("Save all", "", "sel"), ("Settings", "Ctrl+,", ""), ("Find", "Ctrl+F", ""),
    ("Automation", None, None),
    ("Script karaoke-templater-1", "Ctrl+Alt+K", "new"), ("Script fix-overlaps-1", "Ctrl+Alt+O", "new"),
    ("Script resample-1", "", ""),
    ("Audio", None, None),
    ("Play line", "R", ""), ("Commit", "G", ""), ("Next line", "F", ""),
]


def hotkeys_page():
    head = (f'<div class="htools">{combo("All", 160)}{field("Search hotkeys", 250, placeholder=True, icon="search")}'
            f'<span style="margin-left:auto">{btn(ic("import") + "<span>Import legacy hotkeys…</span>", small=True)}</span></div>')
    rows = ""
    for n, k, s in HOTKEYS:
        if k is None:
            rows += f'<div class="hgrp">{n}</div>'
        else:
            rows += (f'<div class="hrow2{" sel" if s == "sel" else ""}"><span>{n}</span><span class="keys">{k}</span>'
                     '</div>')
    table = f'<div class="htable2"><div class="hhead2"><span>Function</span><span>Hotkey</span></div>{rows}</div>'
    acts = ('<div class="hacts">' + btn("Map hotkey…", small=True) + btn("Restore default", small=True)
            + btn("Delete", small=True) + '</div>')
    return head + table + acts


def hotkey_form(state):
    keys = kit.keycaps(["Ctrl", "S"]) if state == "conflict" else kit.keycaps([], True)
    extra = ""
    if state == "refused":
        extra = (f'<div class="conf">{ic("notice-warning", 16, "warn")}<span>Alt+F4 closes the window, so it cannot '
                 'be a hotkey. Press other keys.</span></div>')
    if state == "conflict":
        extra = (f'<div class="conf">{ic("notice-warning", 16, "warn")}<span>Ctrl+S is already used by '
                 '<b>Save</b>.</span></div>'
                 f'<div class="pfoot wrap" style="margin-top:10px">{btn("Swap with Save", small=True, primary=True, focus=True)}'
                 f'{btn("Take it from Save", small=True)}{btn("Keep both", small=True)}</div>')
    inwin = f'<div class="frow"><span class="flabel" style="width:auto">In</span>{combo("Global hotkey", 260)}</div>'
    return keys + inwin + extra


def capture_window(state):
    return window("Keys for “Save all”", hotkey_form(state), btn("Clear", lead=True) + btn("Cancel"), 388)


def conflict_box():
    return message_box("Keys for “Save all”", "Ctrl+S is already used by Save.",
                       "Swap gives Save the keys Save all had (none).", "warning",
                       btn("Swap with Save", primary=True, focus=True) + btn("Take it from Save") + btn("Keep both")
                       + btn("Cancel"), width=520)


def refusal_box():
    return message_box("Keys for “Save all”", "Alt+F4 cannot be a hotkey.", "It closes the window.", "warning",
                       btn("Choose other keys", primary=True, focus=True) + btn("Cancel"), width=468)


def hotkeys_sheet(key):
    th = THEMES[key]
    stack = (toast("info", "Import legacy hotkeys", "Added 2 script hotkeys from Hotkeys.txt. 1 kept the keys you "
                   "gave it; 1 was skipped because its keys are taken.", btn("Details", small=True, flat=True))
             + toast("info", "Import legacy hotkeys", "Hotkeys.txt has no script hotkeys to add."))
    return "".join([
        f'<h1>PU7 hotkeys ({th["name"]})</h1>',
        '<p class="note">Import legacy hotkeys (AM1’s import of a legacy Hotkeys.txt’s script lines) leaves the '
        'Automation panel’s menu (AD3, #312, removes that entry) and becomes a button on Settings › Hotkeys, '
        'PF1’s page (#337), at the end of its tool row. The mapping windows were moved onto PK1’s hotkey capture '
        'already (PK1, #323): the popover from the Hotkeys page’s row, the compact window from Shift+click.</p>',
        '<h2>Settings › Hotkeys, and what the import says</h2>',
        row(col(settings_window("hotkeys", hotkeys_page(), 560),
                "Import legacy hotkeys… opens the native file picker (Hotkeys.txt). The script hotkeys it adds "
                "show in the list under Automation at once. A script hotkey you already have keeps its keys; one "
                "whose keys are taken is skipped (AM1’s rules). The two it added here are the first two under "
                "Automation.", 860),
            col(f'<div class="stack2">{stack}</div>',
                "The outcome as a toast (NT1’s rule: an outcome is a toast): what was added and skipped, the "
                "skipped ones under Details (in the Log). Today the import says nothing.", 360)),
        '<h2>The capture’s refusal and conflict: PK1’s form (recommended) or message boxes (the card’s text)</h2>',
        row(col(capture_window("refused"),
                "PK1’s capture (approved and on qt): a refused key is said on the form, and it keeps waiting for "
                "other keys.", 388),
            col(capture_window("conflict"),
                "Keys that are taken: the choice on the form, with buttons named for it (legacy’s Switch hotkeys / "
                "Delete hotkey / Set anyway). Pressing other keys starts again.", 388),
            col(conflict_box() + '<div style="height:20px"></div>' + refusal_box(),
                "The card’s “refusal and conflict questions become message boxes”, drawn for comparison: a box "
                "over the capture for each. It adds a step for every refused key, so I recommend keeping PK1’s "
                "form, and taking that line off the card.", 520)),
    ])


# ---------------------------------------------------------------------------------------------

EXTRA_CSS = """
body{width:1700px}
.lnk2{color:%(accent)s}
.dis{opacity:.45}
.b.dis{opacity:.45}
.umuted{color:%(muted)s;line-height:1.45}
.uhead{display:flex;gap:14px;align-items:center;margin-top:6px}
.uhead>.ic{color:%(text)s}
.uname{font-size:18px;font-weight:650}
.notes-area{position:relative;margin-top:14px;background:%(field)s;border:1px solid %(line)s;overflow:hidden;padding:4px 14px}
.notes-area.focus{border-color:%(focus)s}
.md{line-height:1.5}
.md h4{font-size:13px;font-weight:650;margin:8px 0 2px}
.md ul{margin:0 0 4px 18px}
.md li{margin:2px 0}
.mdp{margin-top:6px;color:%(muted)s}
.mdl{color:%(accent)s;text-decoration:underline}
.prog{display:flex;gap:12px;align-items:center;margin-top:12px}
.prog .bar{flex:1;height:6px;border-radius:3px;background:%(raised)s;outline:1px solid %(line)s;overflow:hidden}
.prog .bar span{display:block;height:100%%;background:%(accent)s}
.nline{display:flex;gap:8px;align-items:flex-start;margin-top:12px;line-height:1.45}
.nline>.ic{margin-top:1px}
.aur{margin-top:14px;display:flex;flex-direction:column;gap:4px}
.aurt{font-weight:650}
.cmdrow{display:flex;gap:8px;align-items:center;margin-top:4px}
.fld.mono .ftext{font:12px 'Liberation Mono',Consolas,monospace}
.inst{display:flex;gap:16px;align-items:flex-start;margin-top:12px}
.spin{width:28px;height:28px;border-radius:50%%;border:3px solid %(raised)s;border-top-color:%(accent)s;flex:none}
.tip{position:absolute;z-index:4;background:%(raised)s;color:%(text)s;outline:1px solid %(line)s;padding:4px 8px;
     font-size:12px;border-radius:2px;box-shadow:0 4px 12px rgba(0,0,0,.25);white-space:nowrap}
.ferr{display:flex;gap:6px;align-items:center;color:%(danger)s;margin:4px 0 0 64px;font-size:12px}
.ferr .ic{color:%(danger)s}

/* Settings (PF1) */
.settings .dfoot{padding:16px 24px;box-shadow:inset 0 1px %(line)s;margin-top:auto}
.settings .b{gap:6px}
.split{flex:1;display:flex;min-height:0;box-shadow:inset 0 1px %(line)s}
.pages{width:220px;flex:none;background:%(bg)s;padding:8px 0;box-shadow:inset -1px 0 %(line)s}
.pg{display:flex;align-items:center;gap:12px;height:36px;padding:0 16px;color:%(text)s}
.pg.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s;font-weight:650}
.pbody{position:relative;flex:1;min-width:0;overflow:hidden}
.form{padding:20px 28px 24px 24px}
.sec{padding-top:16px;margin-top:16px;box-shadow:inset 0 1px %(line)s}
.sec.first{padding-top:0;margin-top:0;box-shadow:none}
.stitle{font-weight:650;margin-bottom:10px}
.srows{display:flex;flex-direction:column;gap:8px}
.r{display:flex;align-items:center;gap:8px;min-height:28px}
.r.chk{min-height:22px}
.slot{width:16px;flex:none;display:flex;align-items:center;justify-content:center}
.rl{width:220px;flex:none;line-height:1.35}
.rl.wrap{width:auto;flex:1}
.rc{display:flex;align-items:center;gap:12px;flex:1;min-width:0}
.rnote{margin:-5px 0 0 24px;color:%(muted)s;font-size:12px;line-height:1.4}
.newmark{margin-left:10px;font-size:11px;color:%(accent)s;outline:1px dashed %(accent)s;padding:0 5px;border-radius:2px}
.htools{display:flex;gap:8px;margin-bottom:10px;align-items:center}
.htable2{outline:1px solid %(line)s;background:%(field)s}
.hhead2,.hrow2{display:grid;grid-template-columns:1fr 180px;padding:0 10px;height:26px;align-items:center}
.hhead2{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.hgrp{height:26px;display:flex;align-items:center;padding:0 10px;font-weight:650;color:%(muted)s;font-size:12px}
.hrow2{padding-left:22px}
.hrow2.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.hacts{display:flex;gap:8px;margin-top:10px}
.stack2{display:flex;flex-direction:column;gap:8px;width:360px;margin-top:300px}

/* wizard */
.stepper{display:flex;align-items:center;gap:10px;margin:6px 0 18px}
.step{display:flex;align-items:center;gap:8px;color:%(muted)s}
.step .dot{width:22px;height:22px;border-radius:50%%;display:flex;align-items:center;justify-content:center;
           border:1px solid %(control)s;font-size:12px;font-weight:650;color:%(muted)s;background:%(panel)s}
.step.cur{color:%(text)s;font-weight:650}
.step.cur .dot{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.step.done{color:%(text)s}
.step.done .dot{border-color:%(accent)s;color:%(accent)s}
.sline{flex:1;height:1px;background:%(line)s;max-width:120px}
.sline.done{background:%(accent)s}
.wtitle{font-size:14px;font-weight:650;margin-bottom:4px}
.rootlist{margin-top:12px;outline:1px solid %(line)s;background:%(field)s}
.rootrow{display:flex;gap:10px;align-items:center;padding:8px 10px}
.rootrow+.rootrow{box-shadow:inset 0 1px %(line)s}
.rootrow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.rp{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.radio{width:16px;height:16px;border-radius:50%%;border:1px solid %(control)s;background:%(field)s;flex:none;position:relative}
.radio.on{border-color:%(accent)s}
.radio.on:after{content:"";position:absolute;left:3px;top:3px;width:8px;height:8px;border-radius:50%%;background:%(accent)s}
.ibar.inwin{margin-top:16px;height:40px;box-shadow:none;outline:1px solid %(line)s}
.rtools{display:flex;gap:10px;align-items:center;margin-top:12px}
.rsum{margin-left:auto;color:%(muted)s}
.rtable{margin-top:8px;outline:1px solid %(line)s;background:%(field)s}
.thead,.trow{display:flex;align-items:center;gap:10px;padding:0 10px;height:28px}
.thead{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.tbody{position:relative;height:196px;overflow:hidden}
.trow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.trow.muted .tc1,.trow.muted .tc2,.trow.muted .tc3{color:%(muted)s}
.tc0{width:16px;flex:none;display:flex}
.tc1{flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tc2,.tc3{width:170px;flex:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tc4{width:90px;flex:none}
.box.dis{border-color:%(disabled)s;opacity:.5}
.chip{display:inline-flex;align-items:center;height:20px;padding:0 6px;border-radius:2px;font-size:11px;
      outline:1px solid %(line)s;color:%(muted)s;background:%(panel)s}
.chip.conflict{outline-color:%(accent)s;color:%(text)s;font-weight:650}
.chip.unresolved{outline-color:%(warning)s;color:%(warning)s}
.rdet{margin-top:8px;padding:8px 10px;background:%(raised)s;outline:1px solid %(line)s;line-height:1.5;font-size:12px}
.rlinks{display:flex;gap:16px;margin-top:10px}
.done3{display:flex;gap:16px;align-items:flex-start;margin-top:6px;min-height:150px}
.done3>.ic{color:%(text)s}

/* layouts */
.mlist{outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.mrow{display:flex;align-items:center;gap:8px;min-height:30px;padding:2px 6px 2px 10px}
.mrow.two{min-height:42px}
.mrow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.mrow .fld{height:26px}
.macts{display:flex;gap:2px}
.dimt{color:%(muted)s}
.msec{margin-top:14px;padding-top:12px;box-shadow:inset 0 1px %(line)s}
.msec .stitle{margin-bottom:4px}
.spinf{padding-right:2px}
.ftext.num{text-align:right;font-variant-numeric:tabular-nums}
.unit{color:%(muted)s}
.arrows{display:flex;flex-direction:column;color:%(muted)s;margin-left:2px}
.dlg .dtb .dtitle{padding-left:12px}
"""


def page(key, content):
    th = THEMES[key]
    html = kit.page(key, content)
    focus = th["accent"] if key not in kit.HC else th["text"]
    return html.replace("</style>", EXTRA_CSS % dict(th, focus=focus) + "</style>", 1)


def main():
    for key in THEMES:
        (HERE / f"pu7-{key}-1-updates.html").write_text(page(key, updates_sheet(key)))
        (HERE / f"pu7-{key}-2-import.html").write_text(page(key, import_sheet(key)))
        (HERE / f"pu7-{key}-3-workspace.html").write_text(page(key, workspace_sheet(key)))
        (HERE / f"pu7-{key}-4-hotkeys.html").write_text(page(key, hotkeys_sheet(key)))


main()
