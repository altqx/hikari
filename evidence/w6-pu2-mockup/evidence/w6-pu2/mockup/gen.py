#!/usr/bin/env python3
"""PU2 #304 Lines and edit popups mockup: per theme, the windows (History, Select lines, Translation matching
options, Copy / Paste columns, the Line group description popover), the Fix minor errors dock panel, and the
find/replace, translation mode and line group questions as PK1 message boxes and toasts.

Drawn with PK1's mockup kit (kit.py: the theme roles of src/ui/theme.cpp, DG1's frame, the popover frame, the
message box and the toast). The main window behind the toasts is a real screenshot of today's HikariSub
(qt ce0c6740e, this branch's base) in that theme, taken by body.sh under vx11."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu2"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, box, checkbox, field, combo, segmented, popover, window, ic, toast, message_box  # noqa

kit.REPO_ICONS += ["history", "select-lines", "multireplace", "add", "edit", "delete", "translation-shift",
                   "columns-copy", "columns-paste", "panel-menu", "find-replace", "find-in-files", "make-tree", "undo", "redo"]


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=520):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def labelled(label, control, width=72, top=False):
    al = "align-items:flex-start" if top else ""
    pad = "padding-top:5px" if top else ""
    return (f'<div class="frow" style="{al}"><span class="flabel" style="width:{width}px;{pad}">{label}</span>'
            f'{control}</div>')


def qbtn(name, title, on=False):
    return f'<span class="qb{" qon" if on else ""}" title="{title}">{ic(name)}</span>'


# ---------------------------------------------------------------------------------------------
# History


STEPS = ["Open subtitles", "Change text", "Change start time", "Shift times", "Join lines", "Change style",
         "Paste lines", "Change text", "Set as comment", "Delete lines", "Change end time", "Split line"]


def history():
    current, chosen = 8, 6
    rows = []
    for i, s in enumerate(STEPS):
        cls = "hst" + (" sel" if i == chosen else "") + (" undone" if i > current else "")
        tag = '<span class="tag">Current</span>' if i == current else ""
        rows.append(f'<div class="{cls}"><span class="hn">{i + 1}</span><span class="ht'
                    f'{" bold" if i == current else ""}">{s}</span>{tag}</div>')
    body = ('<div class="mbdetail" style="margin:2px 0 8px">12 steps. Steps after the current one can be '
            'redone.</div>'
            f'<div class="hlist">{"".join(rows)}</div>')
    return window("History", body, btn("Go to step", primary=True, focus=True) + btn("Close"), 380, resizable=True)


# ---------------------------------------------------------------------------------------------
# Select lines


def select_lines(choose_open=False):
    find = (f'{segmented(["With", "Without"], "With", small=True)}'
            f'{field("station", 214, focus=not choose_open, trail=ic("chevron-down", 14))}'
            f'<span class="b sm{" on" if choose_open else ""}" title="Choose styles" style="padding:0 5px">'
            f'{ic("styles")}</span>')
    body = "".join([
        labelled("Find", find),
        labelled("", checkbox("Match case", False) + '<span style="width:18px"></span>'
                 + checkbox("Regular expressions", False)),
        labelled("In", segmented(["Text", "Styles", "Actor", "Effect", "Start time", "End time"], "Text",
                                 small=True)),
        labelled("Lines", checkbox("Dialogue", True) + '<span style="width:18px"></span>'
                 + checkbox("Comments", False)),
        labelled("Selection", segmented(["Select", "Add to selection", "Deselect"], "Select", small=True)),
        labelled("Then", combo("Do nothing", 214)),
    ])
    foot = btn("Select in all tabs", lead=True) + btn("Select", primary=True) + btn("Close")
    return window("Select lines", body, foot, 500)


# ---------------------------------------------------------------------------------------------
# Translation matching options


def translation_matching():
    def cell(label, hint):
        return f'<div class="tmc">{btn(label, small=True)}<span class="tmh">{hint}</span></div>'
    body = (
        '<div class="help"><div><b>Original</b>: the text with the right timing. The pasted dialogue is matched '
        'to it, and it is deleted later.</div><div><b>Translation</b>: the text pasted in, moved line by line '
        'until each one sits beside its original.</div></div>'
        '<div class="tmgrid"><div class="tmhead">Original</div><div class="tmhead">Translation</div>'
        + cell("Add line", "A blank line before the selected one; the original moves down. Time the new line.")
        + cell("Add line", "A blank line before the selected one; the translation moves down.")
        + cell("Join lines", "Joins the selected line with the next one; the original moves up.")
        + cell("Join lines", "Joins the selected line with the next one; the translation moves up.")
        + cell("Delete line", "Deletes the selected line; the original moves up.")
        + cell("Delete line", "Deletes the selected line; the translation moves up.")
        + '</div>')
    return window("Translation matching options", body, btn("Close", primary=True), 560)


# ---------------------------------------------------------------------------------------------
# Copy / Paste columns


COLS = ["Layer", "Start time", "End time", "Actor", "Styles", "Left margin", "Right margin", "Vertical margin",
        "Effect"]


def columns(paste):
    texts = ["Text into original", "Text into translation"] if paste else ["Text", "Text without tags"]
    ticks = {"Start time", "End time", "Text into translation"} if paste else {"Actor", "Styles", "Text"}
    items = COLS + texts
    cells = "".join(f'<div>{checkbox(c, c in ticks)}</div>' for c in items)
    lead = ("Paste the clipboard into these columns of the 4 selected lines:" if paste
            else "Copy these columns of the 4 selected lines:")
    body = (f'<div class="mbdetail" style="color:inherit;margin:2px 0 8px">{lead}</div>'
            f'<div class="cgrid">{cells}</div>'
            '<div class="sfoot" style="margin-top:8px"><span class="lnk2">Select all</span>'
            '<span class="lnk2">None</span></div>')
    return window("Paste columns" if paste else "Copy columns", body,
                  btn("Paste" if paste else "Copy", primary=True, focus=True) + btn("Cancel"), 400)


# ---------------------------------------------------------------------------------------------
# Line group description (popover at the group's row)


GRID = [("1", "0:00:01.20", "Default", "Is this the last train tonight?", ""),
        ("2", "0:00:04.10", "Default", "I think so. We should hurry.", ""),
        ("", "", "", "Opening song", "group"),
        ("3", "0:01:30.00", "Song romaji", "Hashiridasu kono michi no saki", "member"),
        ("4", "0:01:34.20", "Song romaji", "Kimi no koe ga kikoeta", "member"),
        ("5", "0:01:38.80", "Song romaji", "Mada owaranai", "member"),
        ("6", "0:02:10.00", "Default", "That song again?", "")]


def grid_rows(selected_group):
    out = ['<div class="ghead"><span class="gn">#</span><span class="gt">Start</span><span class="gs">Styles'
           '</span><span class="gx">Text</span></div>']
    for n, t, s, x, kind in GRID:
        if kind == "group":
            out.append(f'<div class="grow grp{" sel" if selected_group else ""}"><span class="gn">'
                       f'{ic("chevron-down", 14)}</span><span class="gx"><b>{x}</b>'
                       '<span class="gcount">3 lines</span></span></div>')
        else:
            ind = ' style="padding-left:14px"' if kind == "member" else ""
            out.append(f'<div class="grow"><span class="gn">{n}</span><span class="gt">{t}</span>'
                       f'<span class="gs">{s}</span><span class="gx"{ind}>{x}</span></div>')
    return "".join(out)


def group_description():
    pop = popover(
        '<div class="ptitle">Line group description</div>'
        f'{field("Opening song – TV size", 268, focus=True)}'
        f'<div class="pfoot">{btn("Rename", primary=True, small=True)}</div>', 296,
        "position:absolute;left:46px;top:100px")
    return f'<div class="anchorbox" style="width:520px;height:270px"><div class="mgrid">{grid_rows(True)}</div>{pop}</div>'


# ---------------------------------------------------------------------------------------------
# Fix minor errors panel


RULES = [(True, "Double spaces", "  +", " "),
         (True, "Space before punctuation", " +([,.!?…])", r"\1"),
         (True, "Three dots", r"\.\.\.", "…"),
         (True, "Missing space after a comma", r",(\S)", r", \1"),
         (False, "Repeated word", r"\b(\w+) \1\b", r"\1"),
         (True, "Straight apostrophe", r"(\w)'(\w)", r"\1’\2"),
         (False, "Dash with spaces", r" - ", " – "),
         (True, "Space inside brackets", r"\( +| +\)", "")]


def dock(title, body, width, height, active=False):
    mark = '<span class="fmark"></span>' if active else ""
    return (f'<div class="dock" style="width:{width}px;height:{height}px"><div class="dkh"><span class="dkt">'
            f'{title}</span>{qbtn("panel-menu", "Panel menu")}{mark}</div><div class="dkb">{body}</div></div>')


def vis(t):
    return t.replace(" ", '<span class="vsp">·</span>')


def rules_list(chosen, rows=8):
    out = ['<div class="rhead"><span class="rc"></span><span class="rd">Rule</span><span class="rf">Find → '
           'replace</span></div>']
    for i, (on, d, f, r) in enumerate(RULES[:rows]):
        out.append(f'<div class="rrow{" sel" if i == chosen else ""}"><span class="rc">{box(on)}</span>'
                   f'<span class="rd">{d}</span><span class="rf mono">{vis(f)}<span class="arr">→</span>'
                   f'{vis(r) if r else "<i>(nothing)</i>"}</span></div>')
    return f'<div class="rlist">{"".join(out)}</div>'


def scope_rows(styles=True):
    s = labelled("Lines", combo("According to the selected styles" if styles else "All lines", 300), 56)
    if styles:
        s += labelled("", field("Default,Sign", 262) + f'<span class="b sm" title="Choose styles" '
                      f'style="padding:0 5px">{ic("styles")}</span>', 56)
    return s


def tools_strip():
    return (f'<div class="strip">{qbtn("add", "Add rule")}{qbtn("edit", "Update rule")}'
            f'{qbtn("delete", "Delete rule")}<span class="sep"></span>'
            '<span class="scount" style="margin-left:auto">6 of 8 rules on</span></div>')


def rule_editor(open_):
    head = (f'<div class="disc2">{ic("chevron-down" if open_ else "chevron-right", 14)}<b>Rule</b>'
            + ('' if open_ else '<span class="scount" style="margin-left:6px">Space before punctuation</span>')
            + '</div>')
    if not open_:
        return head
    return head + "".join([
        labelled("Name", field("Space before punctuation", 300), 56),
        labelled("Find", field(vis(" +([,.!?…])"), 300, mono=True), 56),
        labelled("Replace", field(r"\1", 300, mono=True), 56),
        labelled("Case", combo("Like the found text", 168) + checkbox("Match case", False), 56),
        labelled("In", combo("Text and tags", 168), 56),
    ])


def run_bar(found=False):
    return (f'<div class="runbar">{segmented(["This tab", "All tabs"], "This tab", small=True)}'
            f'<span style="flex:1"></span>{btn("Find", small=True, primary=not found)}'
            f'{btn("Replace all", small=True)}</div>')


RESULTS = [("header", r"D:\Subs\Show\Episode 03.ass", ""),
           ("Line 6", "Then let's go before the doors close .", " ."),
           ("Line 9", "And I always come back , don't I?", " ,"),
           ("Line 14", "Wait... the tickets!", "..."),
           ("header", r"D:\Subs\Show\Episode 04.ass", ""),
           ("Line 2", "Hello  again.", "  "),
           ("Line 31", "It's over ...", "..."),
           ("Line 40", "Let's go ( now )", "( ")]


def results_list():
    out = []
    for i, (a, b, m) in enumerate(RESULTS):
        p, n = b.find(m), len(m)
        if a == "header":
            out.append(f'<div class="xrow xh">{box(i != 0, partial=(i == 0))}{ic("chevron-down", 14)}<span class="xpath">{b}</span>'
                       '<span class="scount">3</span></div>')
        else:
            on = i != 3
            out.append(f'<div class="xrow{" cur" if i == 2 else ""}">{box(on)}<span class="xl">{a}</span>'
                       f'<span class="xt">{b[:p]}<mark>{vis(b[p:p + n])}</mark>{b[p + n:]}</span></div>')
    return f'<div class="xlist">{"".join(out)}</div>'


def misspell_rules():
    body = (scope_rows() + tools_strip() + rules_list(1) + rule_editor(True)
            + '<div style="flex:1"></div>' + run_bar())
    return dock("Fix minor errors", body, 420, 720, active=True)


def misspell_results():
    body = (scope_rows(False) + tools_strip() + rules_list(1, 5) + rule_editor(False) + run_bar(True)
            + '<div class="xsect"><div class="xhead"><b>Results</b><span class="scount">6 errors in 2 files'
              '</span><span style="flex:1"></span><span class="lnk2">All</span><span class="lnk2">None</span>'
              '</div>' + results_list()
            + f'<div class="runbar" style="margin-top:8px"><span class="scount">5 checked</span>'
              f'<span style="flex:1"></span>{btn("Replace checked", small=True, primary=True)}</div></div>')
    return dock("Fix minor errors", body, 420, 720)


# ---------------------------------------------------------------------------------------------
# Questions


def questions():
    return [
        (message_box("Find and replace", "Reached the end of the subtitles.", "Search again from the first line?",
                     "info", btn("Search from the start", primary=True, focus=True) + btn("Stop")),
         "Wrap (legacy “Confirmation”, Yes / No). Enter searches from the start; Esc stops."),
        (message_box("Find and replace", "Episode 03.ass has none of the chosen styles.",
                     "Searching only these styles would find nothing.", "warning",
                     btn("Search every style", primary=True, focus=True) + btn("Cancel")),
         "No chosen style exists (legacy Yes “Remove styles” / Cancel). Search every style empties the "
         "Styles field and searches."),
        (message_box("Find and replace", "Episode 03.ass has no style named “Notes” or “Sign – blue”.",
                     "The other chosen styles are there. Searching with the missing ones finds fewer lines.",
                     "warning",
                     btn("Search every style", lead=True) + btn("Drop the missing styles", primary=True, focus=True)
                     + btn("Keep them") + btn("Cancel"), width=640),
         "Some chosen styles are missing (legacy OK “Remove nonexistent styles” / Yes “Remove styles” / No "
         "“Ignore” / Cancel). Drop removes them from the Styles field; Keep searches as typed and is not "
         "asked again this search.", ),
        (message_box("Replace in subtitles", "Replace in every matching file in “D:\\Subs\\Show”?",
                     "The files are changed on disk; Undo cannot bring them back. A copy of each file goes to the "
                     "ReplaceBackup folder first.", "warning",
                     btn("Replace in the files", danger=True) + btn("Cancel", primary=True, focus=True)),
         "Replace in subtitles, on the In files tab (legacy “Info”, Yes / No): destructive, so Cancel is the default."),
        (message_box("Translation mode", "Turn off Translation mode?",
                     "The Original text of every Line is deleted. Each Line keeps its Translated text.", "warning",
                     btn("Turn off", danger=True) + btn("Keep it on", primary=True, focus=True)),
         "Subtitles › Translation mode, unticked (legacy “Confirmation”: “Are you sure…? Foreign language text "
         "will be deleted.”, Yes / No, No the default)."),
        (message_box("Translation mode", "Turn on Translation mode for this file?",
                     "Each Line keeps its text as the Original text and gets a Translated text beside it. The file "
                     "saves both until you turn the mode off.", "info",
                     btn("Turn on", primary=True, focus=True) + btn("Not now")),
         "The Translation preset’s question: the same words, now on the message box."),
        (message_box("Line group", "This would break the line group “Opening song”.",
                     "Nothing was changed. Remove the group (its description line is deleted) and try again, or "
                     "cancel.", "warning",
                     btn("Remove group", danger=True) + btn("Cancel", primary=True, focus=True)),
         "A command that would split a line group (G56). Without a description there is nothing to choose, "
         "so it becomes a warning toast (next to the toasts below)."),
    ]


def notices(key):
    th = THEMES[key]
    stack = "".join([
        toast("warning", "This would leave lines outside their line group", "Nothing was changed."),
        toast("info", "Replaced 12 times."),
        toast("info", "12 lines selected.", hover=True),
    ])
    stack2 = "".join([
        toast("warning", "The search folder does not exist", "D:\\Subs\\Shw"),
        toast("info", "Could not find “statoin”."),
    ])
    return (f'<div class="row" style="gap:20px"><div class="app"><div class="stack">{stack}</div></div></div>'
            f'<div class="row" style="margin-top:14px;gap:28px"><div class="stack2">{stack2}</div>'
            + cap("Left: the OK-only messages of find and replace (not found; invalid search folder, a warning that "
                  "stays until ×), Select lines’ count, Replace’s count and the line group refusal without a "
                  "description. They go through one call, so the switch to NT1’s toasts is one line; "
                  "until NT1 lands it is PK1’s toast stack. Above: as they stack over the main window (today’s "
                  f"build, {th['name']}).", 640) + '</div>')


# ---------------------------------------------------------------------------------------------
# Pages


KINDS = [
    ("History", "Edit › History, Ctrl+Shift+H", "Window, resizable, modeless", "Main.qml historyWindow"),
    ("Select lines", "Edit › Select lines", "Window, modeless; the count a toast; Choose styles popover",
     "SelectLinesDialog.qml dialog, result"),
    ("Fix minor errors + Search results", "Edit › Fix minor errors", "One dock panel",
     "MisspellReplacerDialog.qml dialog, results"),
    ("Copy columns / Paste columns", "Grid menu, Ctrl+Shift+C / V", "Compact window", "Main.qml columnsWindow"),
    ("Tree description", "Line group row › Change description", "Popover at the row",
     "Main.qml groupDescriptionDialog"),
    ("Line group", "A command that would break a group", "Message box; toast without a description",
     "Main.qml groupBreakDialog"),
    ("Translation matching options", "Grid menu › Dialogue shifting window", "Compact window, modeless",
     "Main.qml translationShiftWindow"),
    ("Find and replace questions (5 kinds)", "Find, Replace, Replace in subtitles", "4 message boxes + toasts",
     "SearchTool.qml question"),
    ("Translation mode off / on", "Subtitles › Translation mode, Translation preset", "Message boxes",
     "TranslationModeOffConfirm.qml, TranslationModeQuestion.qml"),
]


def page_windows(key):
    th = THEMES[key]
    table = "".join(f'<tr><td>{a}</td><td>{b}</td><td>{c}</td><td class="mono2">{d}</td></tr>' for a, b, c, d in KINDS)
    return "".join([
        f'<h1>PU2 Lines and edit popups: windows and small forms ({th["name"]})</h1>',
        '<div class="notes"><table><tr><td>Popup</td><td>Opened from</td><td>Kind</td><td>Today</td></tr>'
        f'{table}</table></div>',
        '<p class="note" style="margin-top:8px">Every window is on DG1’s frame (title = the command, Esc closes, '
        'first open centred on the main window, then where it was left). The five in-window popups leave '
        'PK1’s allow-list (7 lines). Lines, files, styles and rules are examples.</p>',
        row(col(select_lines(),
                "Select lines: a modeless window of its own (it can leave the main window). Short labels instead of "
                "five group boxes: field, case and regex; which field; Dialogue / Comments; the selection; then an "
                "action (the seven legacy actions in a list). The styles button opens PK1’s Choose styles "
                "popover. Select stays open and posts the count as a toast (legacy asked “Close / OK”). Enter in "
                "the field selects.", 500),
            col(history(),
                "History: a resizable window, modeless, so the grid stays usable. The current step is bold and "
                "marked; undone steps are muted. Go to step (Enter, or a double-click) jumps there and stays "
                "open; Close or Esc closes. Legacy’s Set / OK / Cancel and the count in the title go.", 380),
            top=8),
        row(col(translation_matching(),
                "Translation matching options: a compact modeless window, as today. The help that sat in the "
                "Description button’s tooltip is visible text, and each button says what it moves under it. "
                "Grid menu › Dialogue shifting window or its hotkey.", 560),
            col(columns(False), "Copy columns (Ctrl+Shift+C): a compact window, as it opens from the Grid "
                "menu or a hotkey. The verb button replaces OK; Select all / None added.", 400),
            top=28),
        row(col(columns(True), "Paste columns (Ctrl+Shift+V), in translation mode: the two Text rows go "
                "into the original or the translation.", 400),
            col(group_description(),
                "Tree description becomes a popover at the line group’s row (opened from its menu’s Change "
                "description), like PK1’s spelling suggestions at the word. Enter or Rename keeps it, Esc puts "
                "it back. “Add description” when the group has none (legacy “Set tree name” / “Change tree "
                "name”).", 520),
            top=28),
    ])


def page_panel(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU2 Lines and edit popups: the Fix minor errors panel ({th["name"]})</h1>',
        '<p class="note">Fix minor errors and its Search results become one dock panel (MuseScore’s panel '
        'chrome: the bold title and ⋯ only). Edit › Fix minor errors shows or hides it, as it did the window; '
        'the first time it docks at the right edge, full height, 420 wide, then stays where it is left in the '
        'layout. The hand-made draggable title bars and the per-platform placement go. The rules file and the '
        'engine are unchanged.</p>',
        row(col(misspell_rules(),
                "Editing rules. Lines: which Lines (styles only for “According to the selected styles”; its "
                "button opens Choose styles, which legacy left unbound). Icon strip: Add rule (from the fields), "
                "Update rule (the selected one), Delete rule. The Rule section holds the fields; the three case "
                "boxes become one Case list (Like the found text, Unchanged, Lower case, Upper case: the order "
                "the engine reads them), and Replace only in tags / text one In list.", 420),
            col(misspell_results(),
                "After Find: the Results section opens under the rules, a header per file (its box checks the "
                "file’s finds, its arrow folds them), the find marked in each Line. A double-click shows the "
                "Line. Replace checked works once per search, as legacy. This tab / All tabs once instead of "
                "four buttons; legacy’s two never-implemented buttons (Find error, Replace error) go.", 420),
            gap=40, top=8),
    ])


def page_questions(key):
    th = THEMES[key]
    qs = questions()
    cols = [col(d, c, 640 if i == 2 else 468) for i, (d, c) in enumerate(qs)]
    return "".join([
        f'<h1>PU2 Lines and edit popups: questions and notices ({th["name"]})</h1>',
        '<p class="note">The find/replace question (one in-window dialog with five personalities) becomes PK1 '
        'message boxes with verb buttons; its OK-only messages become toasts. The translation mode prompts and '
        'the line group refusal too. Platform order as on Windows and KDE; the default is filled, a destructive '
        'verb outlined in the danger colour and never the default.</p>',
        row(cols[0], cols[1], top=8),
        row(cols[2], top=24),
        row(cols[3], cols[4], top=24),
        row(cols[5], cols[6], top=24),
        '<h2>Toasts</h2>',
        notices(key),
    ])


EXTRA_CSS = """
.mono,.mono2{font-family:'Liberation Mono',Consolas,monospace}
.mono2{font-size:11px;color:%(muted)s}
.notes tr:first-child td{color:%(muted)s;font-weight:650;font-size:12px}
.notes td:first-child{font-weight:650}
.hlist{outline:1px solid %(line)s;background:%(field)s;padding:2px 0;height:300px;overflow:hidden}
.hst{display:flex;align-items:center;gap:10px;height:24px;padding:0 10px}
.hst.sel{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.hst.undone .ht,.hst.undone .hn{color:%(disabled)s}
.hn{width:18px;color:%(muted)s;text-align:right;font-size:12px}
.ht{flex:1}
.bold{font-weight:650}
.tag{font-size:11px;padding:1px 6px;border:1px solid %(accent)s;color:%(accent)s;border-radius:3px}
.help{background:%(raised)s;outline:1px solid %(line)s;padding:8px 10px;line-height:1.45;display:flex;
      flex-direction:column;gap:4px;margin:2px 0 12px}
.tmgrid{display:grid;grid-template-columns:1fr 1fr;column-gap:20px;row-gap:12px}
.tmhead{font-weight:650;color:%(muted)s;font-size:12px;box-shadow:inset 0 -1px %(line)s;padding-bottom:4px}
.tmc{display:flex;flex-direction:column;gap:4px;align-items:flex-start}
.tmc .b{min-width:96px}
.tmh{color:%(muted)s;font-size:12px;line-height:1.4}
.cgrid{display:grid;grid-template-columns:1fr 1fr;grid-auto-flow:column;grid-template-rows:repeat(6,auto);column-gap:16px;outline:1px solid %(line)s;background:%(field)s;
       padding:6px 10px}
.mgrid{outline:1px solid %(line)s;background:%(field)s;font-size:12px}
.ghead,.grow{display:flex;align-items:center;height:24px;padding:0 6px;gap:8px}
.ghead{background:%(raised)s;color:%(muted)s;box-shadow:inset 0 -1px %(line)s}
.grow.grp{background:%(raised)s}
.grow.sel{background:%(select)s;outline:1px solid %(accent)s;outline-offset:-1px}
.gn{width:22px;color:%(muted)s;display:flex}
.gt{width:78px}.gs{width:84px}.gx{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.gcount{color:%(muted)s;margin-left:8px;font-size:11px}
.dock{background:%(field)s;outline:1px solid %(line)s;display:flex;flex-direction:column}
.dkh{position:relative;height:35px;display:flex;align-items:center;padding:0 6px 0 12px;box-shadow:inset 0 -1px %(line)s;flex:none}
.dkt{font-weight:700;flex:1}
.fmark{position:absolute;right:0;top:0;border-top:8px solid %(accent)s;border-left:8px solid transparent}
.dkb{flex:1;display:flex;flex-direction:column;padding:8px 12px 12px;min-height:0}
.strip{display:flex;align-items:center;gap:2px;margin:6px 0 4px}
.qb.qon{background:%(select)s}
.sep{width:1px;height:18px;background:%(line)s;margin:0 4px}
.rlist{outline:1px solid %(line)s;background:%(field)s}
.rhead,.rrow{display:flex;align-items:center;gap:8px;height:26px;padding:0 8px}
.rhead{height:24px;background:%(raised)s;color:%(muted)s;font-size:12px;box-shadow:inset 0 -1px %(line)s}
.rrow.sel{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.rc{width:16px;flex:none}.rd{width:178px;flex:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.rf{flex:1;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:%(muted)s}
.vsp{color:%(disabled)s}
.arr{margin:0 6px;color:%(text)s;font-family:'Liberation Sans',Arial,sans-serif}
.disc2{display:flex;align-items:center;gap:4px;margin:12px 0 4px;height:22px}
.runbar{display:flex;align-items:center;gap:6px;margin-top:10px}
.xsect{flex:1;display:flex;flex-direction:column;margin-top:12px;box-shadow:inset 0 1px %(line)s;padding-top:10px;min-height:0}
.xhead{display:flex;align-items:center;gap:10px;margin-bottom:6px}
.xlist{flex:1;outline:1px solid %(line)s;background:%(field)s;padding:2px 0;overflow:hidden}
.xrow{display:flex;align-items:center;gap:8px;height:24px;padding:0 8px;font-size:12px}
.xrow.xh{background:%(raised)s;font-size:12px}
.xrow.cur{background:%(select)s}
.xpath{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-weight:650}
.xl{color:%(muted)s;width:46px;flex:none;margin-left:18px}
.xt{flex:1;white-space:pre;overflow:hidden;text-overflow:ellipsis}
mark .vsp{color:inherit}
mark{background:%(accent)s;color:%(onaccent)s;border-radius:1px}
.part{width:8px;height:2px;background:%(onaccent)s}
.stack2{width:360px;display:flex;flex-direction:column;gap:8px}
"""


def page(key, content):
    th = THEMES[key]
    html = kit.page(key, content)
    extra = EXTRA_CSS % dict(th)
    return html.replace("</style>", extra + "</style>", 1)


def main():
    for key in THEMES:
        (HERE / f"pu2-{key}-windows.html").write_text(page(key, page_windows(key)))
        (HERE / f"pu2-{key}-panel.html").write_text(page(key, page_panel(key)))
        (HERE / f"pu2-{key}-questions.html").write_text(page(key, page_questions(key)))


main()
