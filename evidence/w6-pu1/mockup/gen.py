#!/usr/bin/env python3
"""PU1 #303 mockup: the file and session popups on PK1's kit, per theme.

Pages: close (Unsaved changes and the message boxes), autosaves (the merged Autosaves window), home (the
startup cards on SH2's Home page) and bars (the inline bars on the main window and a Document).

The main window and Home page behind the bars and cards are real screenshots of today's HikariSub (the PK1
build, qt ce0c6740e content) in that theme, taken by body.sh under vx11 (shots/). Everything else is drawn
with PK1's mockup helpers (kit.py, from evidence/w6-pk1-mockup) from the theme layer's roles."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import kit  # noqa: E402
from kit import THEMES, btn, checkbox, combo, field, ic, message_box, popover, segmented, window  # noqa: E402

OUT = pathlib.Path(__file__).resolve().parent


def bar(severity, text, actions, closable=True, extra=""):
    glyph, cls = kit.SEVERITY[severity]
    close = f'<span class="tx" title="Close">{ic("tab-close", 14)}</span>' if closable else ""
    return f'<div class="ibar">{ic(glyph, 16, cls)}<span class="ibt">{text}</span>{extra}{actions}{close}</div>'


def sbtn(text, primary=False, danger=False):
    return btn(text, small=True, primary=primary, danger=danger)


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


# ---------------------------------------------------------------------------------------------
# Page 1: closing and saving


DOCS = [
    ("Episode 03.ass", "~/Subs/Station", "Save"),
    ("Episode 04.ass", "~/Subs/Station", "Save"),
    ("Signs.ass", "~/Subs/Station/typesetting", "Don’t save"),
    ("Untitled 2", None, "Save"),
    ("Karaoke OP.ass", "~/Subs/Station/songs", "Save"),
]


def unsaved_window():
    rows = ""
    for i, (name, folder, choice) in enumerate(DOCS):
        where = (f'<span class="upath">{folder}</span>' if folder else
                 f'<span class="upath none">Not saved yet</span>')
        pick = (f'<span class="b sm flat" style="margin-left:auto">{ic("save-as", 14)}&nbsp;Choose file…</span>'
                if not folder else "")
        rows += (f'<div class="urow"><div class="udoc"><span class="uname">{name}</span>'
                 f'<span class="uwhere">{where}{pick}</span></div>'
                 f'{segmented(["Save", "Don’t save"], choice, small=True)}</div>')
    body = ('<div class="mbmain" style="margin-top:6px">Save the changes to 5 documents before quitting?</div>'
            '<div class="mbdetail">Changes you don’t save are lost.</div>'
            f'<div class="ulist">{rows}<span class="sbar" style="top:4px;height:150px"></span></div>'
            '<div class="usum">Saves 4 · Discards 1</div>')
    return window("Quit", body, btn("Quit without saving", lead=True, danger=True)
                  + btn("Save and quit", primary=True, focus=True) + btn("Cancel"), 640, resizable=True)


def page_close(key):
    th = THEMES[key]
    one = message_box("Close tab", "Save the changes to “Signs.ass” before closing?",
                      "Changes you don’t save are lost.", "warning",
                      btn("Don’t save", lead=True, danger=True) + btn("Save", primary=True, focus=True) + btn("Cancel"))
    readonly = message_box("Save", "“Signs.ass” is read-only.",
                           "Save it under another name, or make the file writable and save again.", "warning",
                           btn("Save as…", primary=True, focus=True) + btn("Cancel"))
    mkv = message_box("Load subtitles from MKV", "Save the changes to “Signs.ass” first?",
                      "The subtitles from Episode 03.mkv replace this tab’s.", "warning",
                      btn("Load without saving", lead=True, danger=True) + btn("Save and load", primary=True, focus=True)
                      + btn("Cancel"))
    close_all = message_box("Close all tabs", "Close all 6 tabs?",
                            "You are asked about unsaved changes next.", "info",
                            btn("Close all tabs") + btn("Cancel", primary=True, focus=True))
    with_video = message_box("Open video", "Open “Episode 03.ass” with the video?",
                             "It is next to Episode 03.mkv and has the same name.", "info",
                             btn("Open both", primary=True, focus=True) + btn("Video only"))
    pickers = "".join(f'<tr><td>{a}</td><td>{b}</td><td>{c}</td><td>{d}</td></tr>' for a, b, c, d in [
        ("File › Open subtitles", "Open subtitles", "Subtitles (*.ass *.ssa *.srt *.sub *.txt *.mpl), Videos with "
         "embedded subtitles (*.mkv *.ogm), All files", "Subtitles folder"),
        ("Save as, Save of an Untitled Document, Choose file… above", "Save “Signs.ass” as",
         "The Document’s format first (ASS subtitles (*.ass), …), All files", "The Document’s folder, else the "
         "subtitles folder"),
        ("File › Load session…", "Load session", "Sessions (*.kls), All files", "Sessions folder"),
        ("File › Save session…", "Save session", "Sessions (*.kls)", "Sessions folder"),
        ("Relink… on a session entry", "Find “Episode 03.mkv”", "The entry’s kind (Video …, Subtitles …, Audio …, "
         "Keyframes …), All files", "The missing file’s folder when it exists, else that kind’s folder"),
    ])
    return "".join([
        f'<h1>PU1: closing and saving ({th["name"]})</h1>',
        '<h2>Unsaved changes (a window on DG1’s frame, modal to the main window)</h2>',
        '<div class="row"><div class="col">', unsaved_window(), '</div>',
        cap('One row per Document with unsaved changes, in tab order: the tab name, its folder, and a Save / '
            'Don’t save switch (Save to start). More than five rows scroll; the window can be made taller. An '
            'Untitled Document has no folder: Choose file… picks one now, or Save and quit asks for it then. The '
            'line under the list counts what the button will do.<br><br>'
            'The title and the buttons name the command that asked: Quit (Save and quit, Quit without saving), '
            'Close all tabs (Save and close, Close without saving), Load session and Load last session (Save and '
            'load, Load without saving). The one that loses work sits at the leading edge in the danger colour, '
            'never the default. Esc and × are Cancel. A save that fails keeps the window open with the error '
            'under its row.', 600),
        '</div>',
        '<h2>One Document: a plain message box</h2>',
        '<div class="row"><div class="col">', one,
        cap('Close tab, New, Open into this tab and dropped files ask about one Document, so the message box '
            'asks. Untitled: Save is Save as….'), '</div>',
        '<div class="col">', mkv,
        cap('Load subtitles from MKV (MatroskaSubtitles saveQuestion): today Yes / No / Cancel under '
            '“Confirmation”.'), '</div></div>',
        '<h2>Read-only file</h2>',
        '<div class="row"><div class="col">', readonly,
        cap('Save onto a read-only file (Main.qml readOnlyWarning; Save session’s readOnlyNotice says '
            '“session file” and is titled Save session). Today an OK-only “Warning” whose OK opens the Save '
            'dialog; Cancel now leaves it unsaved.'), '</div></div>',
        '<h2>Two tab questions no card names (open question 8)</h2>',
        '<div class="row"><div class="col">', close_all,
        cap('Close all tabs (TabCommands closeAllPrompt, “Prompt”, Yes / No with No the default).'), '</div>',
        '<div class="col">', with_video,
        cap('Opening a video with same-named subtitles beside it (subtitlesPrompt, “Confirmation”, Yes / No).'),
        '</div></div>',
        '<h2>File pickers stay native</h2>',
        '<div class="notes"><table><tr class="th"><td style="width:300px">Picker</td><td>Title</td><td>Filters</td><td>Starts in, '
        'and remembers</td></tr>', pickers, '</table></div>',
    ])


# ---------------------------------------------------------------------------------------------
# Page 2: Autosaves

BUNDLES = [
    ("Episode 03.ass", "~/Subs/Station/Episode 03.ass", "Today 14:02", "3 versions", False),
    ("Untitled 1", "Never saved", "Today 09:40", "1 version", False),
    ("Signs.ass", "~/Subs/Station/typesetting/Signs.ass", "Yesterday 22:15", "2 versions", False),
    ("Episode 02.ass", "~/Subs/Station/Episode 02.ass", "14/09/2026", "4 versions", False),
    ("Episode 01.ass", "C:\\Subs\\Station\\Episode 01.ass", "02/08/2026", "6 versions", True),
]


def autosave_rows(cur=0):
    out = ""
    for i, (name, path, when, versions, legacy) in enumerate(BUNDLES):
        tag = '<span class="tag">Previous version</span>' if legacy else ""
        delete = (f'<span class="qb dim" title="Read only">{ic("delete", 16)}</span>' if legacy else
                  f'<span class="qb" title="Delete">{ic("delete", 16)}</span>')
        out += (f'<div class="arow{" cur" if i == cur else ""}"><div class="udoc"><span class="uname">{name}{tag}</span>'
                f'<span class="upath{" none" if path == "Never saved" else ""}">{path}</span></div>'
                f'<span class="awhen">{when}</span>{combo(versions, 116)}'
                f'{btn("Open copy", small=True)}{delete}</div>')
    return out


def autosaves_window(with_popover=False):
    body = (f'<div class="frow" style="margin-top:4px">{field("Search autosaves", 300, placeholder=True, icon="search")}'
            f'<span class="flabel" style="width:auto;margin-left:8px">Saved</span>{combo("Any time", 150)}</div>'
            '<div class="ahead"><span class="udoc">Document</span><span class="awhen">Saved</span>'
            '<span style="width:116px">Version</span><span style="width:120px"></span></div>'
            f'<div class="alist">{autosave_rows()}</div>'
            '<div class="mbdetail" style="margin-top:8px">Each opens as a new unsaved copy; the original file '
            'is not changed. Autosaves of the previous version are read only.</div>')
    lead = '<span class="b lead on">Delete older than…</span>' if with_popover else btn("Delete older than…", lead=True)
    w = window("Autosaves", body, lead + btn("Close", primary=not with_popover, focus=not with_popover), 780,
               resizable=True)
    if not with_popover:
        return w
    cal = kit.calendar().replace("A week ago", "A week ago").replace(
        '<span class="lnk2" style="margin-left:auto">Today</span>', '<span class="lnk2" style="margin-left:auto">'
        'Everything</span>')
    pop = popover(f'<div class="ptitle">Delete autosaves saved before</div>'
                  f'{field("21/09/2026", 244, trail=ic("calendar", 16), focus=True)}<div style="height:8px"></div>{cal}'
                  f'<div class="pfoot">{btn("Delete 2 autosaves", small=True, danger=True)}</div>', 268,
                  "position:absolute;left:24px;bottom:62px")
    return f'<div style="position:relative">{w}{pop}</div>'


def page_autosaves(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU1: Autosaves ({th["name"]})</h1>',
        '<p class="note">File › Open auto save and File › Remove temporary files open this one window (a window on '
        'DG1’s frame, modeless, resizable). It lists every autosave bundle, this version’s and the previous '
        'version’s (legacy’s Subs folder), newest first.</p>',
        '<div class="row"><div class="col">', autosaves_window(), '</div>',
        cap('Search matches the name and the path as you type (every word, as legacy’s “All words”). Saved '
            'filters by date with the shared date filter: Any time, Today, Last 7 days, Last 30 days, Before a '
            'date…. Version picks one of the bundle’s autosaves, the latest first. Open copy opens it as a new '
            'unsaved copy and closes the window. The bin deletes the bundle at once; the previous version’s are '
            'read only, so theirs is disabled.', 420),
        '</div>',
        '<h2>Delete older than… (a popover with the date picker)</h2>',
        '<div class="row"><div class="col">', autosaves_window(True), '</div>',
        cap('Picking a date counts what goes; the button says it and deletes in one step (no second '
            'question). “Everything” replaces Remove all auto save files. Previous-version autosaves are never '
            'deleted. Remove temporary files opens the window with this popover open.', 420),
        '</div>',
    ])


# ---------------------------------------------------------------------------------------------
# Page 3: Home


def home_backdrop(th, card_html, card_height):
    shot = f"shots/home-{th['code']}.png"
    top = 104 + card_height + 16
    return (f'<div class="app" style="background-image:url({shot});height:560px;overflow:hidden">'
            f'<div class="cover" style="top:100px;height:{top - 100}px"></div>'
            f'<div class="homecards" style="top:104px">{card_html}</div>'
            f'<div class="slice" style="top:{top}px;height:{700 - top}px;background-image:url({shot});'
            f'background-position:0 -166px"></div></div>')


def crash_card():
    return (f'<div class="hcard warnc">{ic("notice-warning", 32, "big warn")}<div class="hctext">'
            '<div class="mbmain">HikariSub did not close properly</div>'
            '<div class="mbdetail">The last session had 2 documents: Episode 03.ass and Signs.ass. Unsaved work in '
            '1 of them was autosaved.</div>'
            f'<div class="hcacts">{btn("Restore session", primary=True, focus=True)}{btn("Autosaves…")}</div></div>'
            f'<span class="tx" title="Dismiss">{ic("tab-close", 14)}</span></div>')


def ask_card():
    return (f'<div class="hcard">{ic("last-session", 32, "big")}<div class="hctext">'
            '<div class="mbmain">Restore the last session?</div>'
            '<div class="mbdetail">2 documents · Episode 03.ass, Signs.ass</div>'
            f'<div class="hcacts">{btn("Restore", primary=True, focus=True)}'
            f'<span style="margin-left:8px">{checkbox("Restore it at every start", False)}</span></div></div></div>')


def page_home(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU1: the startup prompts on the Home page ({th["name"]})</h1>',
        '<p class="note">Nothing blocks the start any more: the two questions that opened before the main window '
        '(Load last session?, The program crashed…) become cards at the top of Home, where SH2’s Last session '
        'row is. The card stays until it is answered, dismissed or a Document opens; then it goes (with a '
        'Document open it is the bar on the next page). Files that another start hands over open at once.</p>',
        '<h2>After a crash (Home)</h2>',
        home_backdrop(th, crash_card(), 112),
        cap('Replaces crashPrompt and the Open auto save window that opened by itself. Restore session loads the '
            'Session as File › Load last session does; Autosaves… opens the Autosaves window. × dismisses; Home '
            'then shows the ordinary Last session row. Without autosaved work the second sentence and Autosaves… '
            'are left out.', 900),
        '<h2>On start: Ask (Home)</h2>',
        home_backdrop(th, ask_card(), 104),
        cap('Replaces askPrompt (“Prompt”, Load last session? Yes / No). The Last session row becomes the '
            'question, with Restore the default. “Restore it at every start” sets On start to Restore (Settings '
            '› General), so the question goes. Ignoring it is “No”: legacy’s No went on to the crash check, '
            'which a crash card would show instead.', 900),
    ])


# ---------------------------------------------------------------------------------------------
# Page 4: bars


def doc_backdrop(th, bars_html, height):
    shot = f"shots/doc-{th['code']}.png"
    top = 64 + height
    return (f'<div class="app" style="background-image:url({shot})">'
            f'<div class="bars" style="top:64px">{bars_html}</div>'
            f'<div class="slice" style="top:{top}px;height:{778 - top}px;background-image:url({shot});'
            f'background-position:0 -64px"></div></div>')


def progress(pct, width=180):
    return (f'<span class="prog" style="width:{width}px"><span style="width:{pct}%"></span></span>'
            '<span class="pmeta">42% · 0:03</span>')


def page_bars(key):
    th = THEMES[key]
    start = bar("warning", "HikariSub did not close properly. The last session had 2 documents; unsaved work in 1 "
                "of them was autosaved.", sbtn("Restore session") + sbtn("Autosaves…"))
    changed = bar("warning", "<b>Signs.ass</b> was changed by another program. Reloading drops the changes you "
                  "made here.", sbtn("Reload") + sbtn("Compare") + sbtn("Keep mine"), closable=False)
    missing = bar("warning", "<b>Station.mkv</b>, this tab’s video, was not found.",
                  sbtn("Retry") + sbtn("Relink…") + sbtn("Remove"))
    mkv = bar("info", "Loading subtitles from <b>Episode 03.mkv</b>", sbtn("Cancel"), closable=False,
              extra=progress(42))
    return "".join([
        f'<h1>PU1: bars on the main window and on a Document ({th["name"]})</h1>',
        '<p class="note">PK1’s inline bar, under the Document tabs where the update-ready bar is. A bar about '
        'the window comes first; a bar about a Document shows only while its tab is the current one, under the '
        'window’s. None takes the keyboard.</p>',
        '<h2>Started with a file after a crash, and the file changed on disk</h2>',
        doc_backdrop(th, start + changed, 76),
        '<div class="row" style="margin-top:8px">',
        cap('<b>Startup bar</b> (window): the Home card’s offer when a file was opened from the command line, so '
            'Home never shows. Restore session replaces the open tabs after the usual Unsaved changes question, '
            'as File › Load last session does. With On start: Ask it reads “Restore the last session (2 '
            'documents)?” with Restore.', 600),
        cap('<b>Changed on disk</b> (Signs.ass): replaces the Reloading modal (“Subtitles were modified by '
            'another program. Reload?” Yes / No) that opened when the window was activated. Reload reads the file '
            'again; Keep mine closes the bar (saving then writes over the file); Compare opens the file as it is '
            'on disk in a tab beside it and turns on Subtitle comparison, the bar staying. Without unsaved '
            'changes the second sentence is left out and the bar is information.', 600),
        '</div>',
        '<h2>A session file not found, and loading subtitles from an MKV</h2>',
        doc_backdrop(th, mkv + missing, 76),
        '<div class="row" style="margin-top:8px">',
        cap('<b>Loading from an MKV</b> (window): replaces the modal progress dialog (“Loading subtitles from '
            'Matroska.”, the gauge, Time elapsed, Cancel). The loaded subtitles replace the tab’s when they are '
            'complete. The track chooser before it is already PK1’s list chooser; “The file does not contain any '
            'subtitle tracks.” becomes a toast.', 600),
        cap('<b>Not found</b> (the current tab, Signs.ass): replaces the Session entries not restored window. One bar '
            'per entry the tab could not restore (subtitles, video, audio, keyframes): Retry, Relink… (the native '
            'picker, titled with the file’s name) and Remove. The tab keeps its other files.', 600),
        '</div>',
    ])


# ---------------------------------------------------------------------------------------------

EXTRA = """
.notes tr.th td{font-weight:650;color:%(muted)s}
.notes td:first-child{font-weight:400}
.ulist{position:relative;margin-top:12px;outline:1px solid %(line)s;background:%(field)s;height:236px;overflow:hidden}
.urow,.arow{display:flex;align-items:center;gap:12px;padding:6px 14px 6px 10px;min-height:46px}
.urow+.urow,.arow+.arow{box-shadow:inset 0 1px %(line)s}
.urow.cur,.arow.cur{background:%(select)s}
.udoc{flex:1;min-width:0;display:flex;flex-direction:column;gap:2px}
.uname{font-weight:650;display:flex;align-items:center;gap:8px}
.uwhere{display:flex;align-items:center;gap:8px}
.upath{color:%(muted)s;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.upath.none{font-style:italic}
.usum{margin-top:8px;color:%(muted)s;font-size:12px}
.ahead{display:flex;gap:12px;align-items:center;padding:0 14px 0 10px;height:26px;margin-top:10px;color:%(muted)s;
       font-size:12px;background:%(raised)s;outline:1px solid %(line)s}
.alist{outline:1px solid %(line)s;background:%(field)s;height:236px;overflow:hidden}
.awhen{width:110px;flex:none;color:%(text)s}
.ahead .awhen{color:%(muted)s}
.tag{font-weight:400;font-size:11px;color:%(muted)s;border:1px solid %(line)s;border-radius:3px;padding:0 5px}
.qb.dim{color:%(disabled)s}
.cover{position:absolute;left:0;width:1280px;background:%(bgshot)s}
.slice{position:absolute;left:0;width:1280px;background-repeat:no-repeat}
.homecards{position:absolute;left:80px;width:1120px;display:flex;flex-direction:column;gap:8px}
.hcard{display:flex;gap:16px;align-items:flex-start;padding:16px 10px 16px 16px;background:%(panel)s;
       outline:1px solid %(line)s}
.hcard.warnc{box-shadow:inset 3px 0 %(warning)s}
.hctext{flex:1;display:flex;flex-direction:column;gap:6px}
.hcacts{display:flex;gap:8px;align-items:center;margin-top:8px}
.bars{position:absolute;left:0;width:1280px;display:flex;flex-direction:column}
.ibar .b.sm{height:24px}
.ibar b{font-weight:650}
.prog{display:inline-block;height:6px;background:%(field)s;outline:1px solid %(line)s;flex:none}
.prog span{display:block;height:100%%;background:%(accent)s}
.pmeta{color:%(muted)s;font-size:12px;width:80px}
"""


def page(key, content):
    th = THEMES[key]
    base = kit.page(key, content)
    extra = EXTRA % dict(th, bgshot=th["bg"])
    return base.replace("</style>", extra + "</style>", 1)


def main():
    for key in THEMES:
        (OUT / f"pu1-{key}-1-close.html").write_text(page(key, page_close(key)))
        (OUT / f"pu1-{key}-2-autosaves.html").write_text(page(key, page_autosaves(key)))
        (OUT / f"pu1-{key}-3-home.html").write_text(page(key, page_home(key)))
        (OUT / f"pu1-{key}-4-bars.html").write_text(page(key, page_bars(key)))


main()
