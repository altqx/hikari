#!/usr/bin/env python3
"""PK1 #301 popup kit mockup: per theme, the four kinds (window, message box, popover, notice) and the
shared pieces (clash review, Choose styles, hotkey capture, list chooser, date picker).

The main window behind the notices is a real screenshot of today's HikariSub (the DG1 build, qt 1f6fe86f8)
in that theme, taken by body.sh under vx11. Everything else is drawn here from the theme layer's roles
(src/ui/theme.cpp baseRoles, default accent), DG1's dialog frame (DialogWindow.qml), the colour
popover's frame (ColourPopover.qml: padding 12, radius 4, a 1-pixel line, a shadow) and IC1's icons."""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu4")
OUT = pathlib.Path(__file__).resolve().parent

THEMES = {
    "light": dict(code="light", name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", press="#DDE2E7", warning="#8A5A00",
                  danger="#A51F31"),
    "dark": dict(code="dark", name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", press="#343E49", warning="#E0A030",
                 danger="#FFADAD"),
    "hcwhite": dict(code="highContrastWhite", name="High contrast white", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    press="#C9DAF8", warning="#6B4500", danger="#A00000"),
    "hcblack": dict(code="highContrastBlack", name="High contrast black", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    press="#253F60", warning="#FFD54A", danger="#FFADAD"),
}
HC = {"hcwhite", "hcblack"}
DARKISH = {"dark", "hcblack"}

# New glyphs in IC1's 16-unit, 1-wide stroke style (the accent group takes the severity colour).
GLYPHS = {
    "notice-error": '<path d="M5.5 1.5h5l4 4v5l-4 4h-5l-4-4v-5z"/><g class="acc"><path d="M8 4.75v4"/>'
                    '<circle cx="8" cy="11.25" r=".75" fill="currentColor" stroke="none"/></g>',
    "calendar": '<rect x="1.5" y="2.5" width="13" height="12" rx="1"/><path d="M1.5 6h13M5 1v3M11 1v3"/>'
                '<g class="acc"><path d="M4.5 9h1M7.5 9h1M10.5 9h1M4.5 11.5h1M7.5 11.5h1"/></g>',
    "chevron-down": '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/>',
    "chevron-right": '<path d="M6.5 4.5l3.5 3.5-3.5 3.5"/>',
    "chevron-left": '<path d="M9.5 4.5l-3.5 3.5 3.5 3.5"/>',
    "check": '<path d="M3.5 8.5l3 3 6-7"/>',
}
REPO_ICONS = ["about", "notice-warning", "search", "tab-close", "log", "styles", "hotkeys", "open-audio",
              "spellchecker", "remove-temporary", "check-updates", "window-close", "window-maximize", "edit-copy",
              "save", "menu-more"]


def icon_svg(name):
    svg = (TREE / "src/ui/icons" / f"{name}.svg").read_text()
    return re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip()).replace('<g id="accent">', '<g class="acc">')


def sprite():
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    syms = {**GLYPHS, **{n: icon_svg(n) for n in REPO_ICONS}}
    body = "".join(f'<symbol id="i-{n}" viewBox="0 0 16 16"><g {attrs}>{s}</g></symbol>' for n, s in syms.items())
    return f'<svg width="0" height="0" style="position:absolute"><defs>{body}</defs></svg>'


def ic(name, size=16, cls=""):
    return f'<svg class="ic {cls}" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


# ---------------------------------------------------------------------------------------------
# DG1's frame and controls


def titlebar(title, resizable=False):
    btns = ((f'<span class="wb">{ic("window-maximize")}</span>' if resizable else "")
            + f'<span class="wb">{ic("window-close")}</span>')
    return f'<div class="dtb"><span class="dtitle">{title}</span><span class="wbs">{btns}</span></div>'


def window(title, body, foot, width, resizable=False, style="", foot_cls=""):
    return (f'<div class="dlg" style="width:{width}px;{style}">{titlebar(title, resizable)}'
            f'<div class="dbody">{body}</div><div class="dfoot {foot_cls}">{foot}</div></div>')


def btn(text, primary=False, focus=False, lead=False, danger=False, small=False, flat=False):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" lead" if lead else "") + (
        " dng" if danger else "") + (" sm" if small else "") + (" flat" if flat else "")
    return f'<span class="{cls}">{text}</span>'


def box(on, partial=False):
    mark = ('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
            'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
    if partial:
        mark = '<span class="part"></span>'
    return f'<span class="box{" on" if on or partial else ""}">{mark if on or partial else ""}</span>'


def checkbox(text, on, focus=False, extra=""):
    return f'<div class="cb{" ring" if focus else ""}">{box(on)}<span class="cbt">{text}</span>{extra}</div>'


def field(text, width=None, placeholder=False, focus=False, icon=None, trail=None, mono=False):
    w = f"width:{width}px;" if width else ""
    cls = "fld" + (" ph" if placeholder else "") + (" focus" if focus else "") + (" mono" if mono else "")
    lead = ic(icon, 16, "fic") if icon else ""
    tail = f'<span class="ftail">{trail}</span>' if trail else ""
    return f'<span class="{cls}" style="{w}">{lead}<span class="ftext">{text}</span>{tail}</span>'


def combo(text, width=None):
    return field(text, width, trail=ic("chevron-down", 14))


def segmented(options, chosen, small=False):
    return ('<span class="seg' + (" sm" if small else "") + '">' + "".join(
        f'<span class="{"on" if o == chosen else ""}">{o}</span>' for o in options) + '</span>')


def popover(body, width, style="", cls=""):
    return f'<div class="pop {cls}" style="width:{width}px;{style}">{body}</div>'


# ---------------------------------------------------------------------------------------------
# Message box


SEVERITY = {"info": ("about", ""), "warning": ("notice-warning", "warn"), "error": ("notice-error", "err")}


def message_box(title, main, detail="", severity="info", foot="", width=468, extra="", details_open=None):
    glyph, cls = SEVERITY[severity]
    body = (f'<div class="mb">{ic(glyph, 32, "big " + cls)}<div class="mbtext"><div class="mbmain">{main}</div>'
            + (f'<div class="mbdetail">{detail}</div>' if detail else "") + extra + '</div></div>')
    if details_open is not None:
        body += (f'<div class="disc">{ic("chevron-down" if details_open else "chevron-right", 14)}Details</div>')
        if details_open:
            body += (f'<div class="dets"><pre>{details_open}</pre>'
                     f'<span class="qb" title="Copy details">{ic("edit-copy", 14)}</span></div>')
    return window(title, body, foot, width)


def message_boxes():
    return [
        (message_box("Delete style", "Delete the style “Song romaji”?",
                     "12 lines use it. They will use Default instead.", "warning",
                     btn("Delete", danger=True) + btn("Cancel", primary=True, focus=True)),
         "A destructive choice: the verb in the danger colour, and Cancel, the safe button, as the default "
         "(Enter). The title names the command, never “Confirmation”."),
        (message_box("Restore last session", "Open the files from the last session?",
                     "Episode 03.ass and Episode 03.mkv", "info",
                     btn("Open", primary=True, focus=True) + btn("Not now"),
                     extra=f'<div class="mbcheck">{checkbox("Don’t ask again", False)}</div>'),
         "A question legacy had a setting for (Settings › General › On start): “Don’t ask again” writes that "
         "setting. Settings › General › “Ask again” turns every hidden question back on."),
        (message_box("Open subtitles", "Cannot open “Episode 04.ass”.",
                     "The file is not UTF-8 and its encoding could not be detected.", "error",
                     btn("Choose encoding…", lead=True) + btn("OK", primary=True, focus=True),
                     details_open="uchardet: no confident guess (0.31)\nfirst bad byte at 0x1A2F (line 48)"),
         "An error that stops an action stays modal. The technical text sits under Details, collapsed until "
         "asked for, with a copy button."),
    ]


# ---------------------------------------------------------------------------------------------
# Notices


def toast(severity, title, text="", actions="", fade=False, hover=False):
    glyph, cls = SEVERITY[severity]
    return (f'<div class="toast{" hover" if hover else ""}">{ic(glyph, 16, cls)}<div class="tt">'
            f'<div class="ttitle">{title}</div>' + (f'<div class="ttext">{text}</div>' if text else "")
            + (f'<div class="tacts">{actions}</div>' if actions else "") + '</div>'
            f'<span class="tx" title="Dismiss">{ic("tab-close", 14)}</span></div>')


def inline_bar(severity, text, actions):
    glyph, cls = SEVERITY[severity]
    return (f'<div class="ibar">{ic(glyph, 16, cls)}<span class="ibt">{text}</span>{actions}'
            f'<span class="tx" title="Close">{ic("tab-close", 14)}</span></div>')


def overview(key):
    th = THEMES[key]
    stack = "".join([
        toast("info", "Saved a copy as Episode 03 (read-only copy).ass"),
        toast("warning", "The audio device stopped", "Playback is paused. Choose another output in "
              "Settings › Audio.", btn("Details", small=True, flat=True) + btn("Settings", small=True, flat=True)),
        toast("info", "No dictionary for Polish", "The spellchecker skips lines in Polish.",
              btn("Details", small=True, flat=True), hover=True),
    ])
    bar = inline_bar("warning", "Episode 03.ass is read-only. Saving writes a copy next to it.",
                     btn("Save as…", small=True) + btn("Make writable", small=True))
    return "".join([
        f'<h1>PK1 popup kit: notices over the main window ({th["name"]})</h1>',
        '<p class="note">Toasts stack at the bottom right of the main window, 12 inside it, above the status '
        'bar: three at most, the newest on top. An information toast fades after 5 seconds (hover pauses it; '
        'the bottom one here is under the pointer); a warning stays until its × is pressed. Details opens the '
        'Log. Every notice also goes to the Log. With “Show notices as toasts” off (Settings › General), they go '
        'only to the Log. While the video is full screen they show there, at the same corner.</p>',
        '<p class="note">An inline bar sits under the menus like the update-ready bar, for a state that lasts '
        'and has its own actions (the file is read-only, an update is ready). It stays until its × or its '
        'state ends. Data-loss errors and actions that stopped stay modal message boxes.</p>',
        f'<div class="app"><div class="barslot">{bar}</div><div class="stack">{stack}</div></div>',
    ])


# ---------------------------------------------------------------------------------------------
# Popover anatomy and the small forms rule


def kinds(key):
    th = THEMES[key]
    mbs = message_boxes()
    s = [f'<h1>PK1 popup kit: the four kinds ({th["name"]})</h1>',
         '<div class="notes"><table>',
         '<tr><td>Window</td><td>DG1’s frame: a top-level window, centred on its opener the first time, then '
         'where it was left; Esc closes it. Anything with more than three fields, or anything opened from a menu or '
         'hotkey that is not a small form.</td></tr>',
         '<tr><td>Message box</td><td>A question or notice that needs an answer: a window on DG1’s frame, modal '
         'to its parent window only, fixed size. Title: the command it comes from. A severity icon, one main '
         'line, an optional detail, buttons named for what they do, in the platform’s order.</td></tr>',
         '<tr><td>Popover</td><td>Anchored to the control that opened it, below it (above where there is no '
         'room). The colour popover’s frame. A press outside or the anchor again closes it and keeps the '
         'change; Esc closes it and puts the value before back. In a window of its own when the host window is '
         'too small for it.</td></tr>',
         '<tr><td>Notice</td><td>A toast or an inline bar (over the main window, next page). Never a message '
         'box with only OK.</td></tr>',
         '<tr><td>Small forms</td><td>Three fields or fewer: a popover from an on-screen control, a compact '
         'window from a menu or hotkey (the same content).</td></tr>',
         '</table></div>',
         '<h2>Message box</h2>',
         '<div class="row">']
    s += [f'<div class="col">{d}<p class="cap">{c}</p></div>' for d, c in mbs[:2]]
    s += ['</div><div class="row" style="margin-top:24px">',
          f'<div class="col">{mbs[2][0]}<p class="cap">{mbs[2][1]}</p></div>',
          f'<div class="col">{message_box("Close all tabs", "Save the changes to 3 files before closing them?", "Episode 03.ass, Episode 04.ass and Signs.ass", "warning", btn("Don’t save", lead=True, danger=True) + btn("Save all", primary=True, focus=True) + btn("Cancel"))}'
          '<p class="cap">Three answers: the one that loses work at the leading edge, in the danger colour. '
          'Windows and KDE order shown; GNOME and macOS put the default last.</p></div>',
          '</div>',
          '<h2>Popover: anatomy, and the small forms rule</h2>',
          '<div class="row">',
          popover_anatomy(),
          small_form_pair(),
          '</div>']
    return "".join(s)


def popover_anatomy():
    pop = popover(
        '<div class="ptitle">Rename layer</div>'
        f'<div class="frow"><span class="flabel">Name</span>{field("Signs – station", 196, focus=True)}</div>'
        f'<div class="pfoot">{btn("Rename", primary=True, small=True)}</div>', 268,
        "position:absolute;left:0;top:36px")
    return (f'<div class="col"><div class="anchorbox" style="height:200px;width:300px">'
            f'<span class="b sm on">Rename…</span>{pop}</div>'
            '<p class="cap" style="max-width:300px">A popover below its control (pressed while open): padding 12, '
            'the panel surface, radius 4, a 1-pixel line and the shadow. A title only when the control does not '
            'already say what it edits. Enter applies, Esc puts back.</p></div>')


def small_form_pair():
    form = (f'<div class="frow"><span class="flabel">Width</span>{field("1920", 90)}<span class="flabel" '
            f'style="width:auto;margin-left:10px">Height</span>{field("1080", 90)}</div>'
            f'<div class="frow">{checkbox("Keep the aspect ratio", True)}</div>')
    pop = popover(form + f'<div class="pfoot">{btn("Apply", primary=True, small=True)}</div>', 300)
    win = window("Change resolution", form, btn("Apply", primary=True, focus=True) + btn("Cancel"), 340)
    return (f'<div class="col"><div class="row" style="gap:18px;align-items:flex-start">'
            f'<div class="col"><span class="b sm on" style="align-self:flex-start">1920 × 1080</span>'
            f'<div style="margin-top:4px">{pop}</div></div>'
            f'<div class="col">{win}</div></div>'
            '<p class="cap" style="max-width:640px">The same small form both ways: as a popover from the '
            'resolution shown in a panel (left), and as a compact window from a menu command or hotkey (right). '
            '(An example form; the cards that own each popup apply the rule.)</p></div>')


# ---------------------------------------------------------------------------------------------
# Shared pieces

CLASHES = [
    ("Default", "Font, size and outline differ", "Replace", ""),
    ("Song romaji", "Colours differ", "Rename", "Song romaji (catalog)"),
    ("Sign", "Alignment differs", "Skip", ""),
]


def clash_review(key):
    rows = "".join(
        f'<div class="crow2"><span class="cname">{n}</span><span class="cdiff">{d}</span>'
        f'<span class="cact">{segmented(["Replace", "Rename", "Skip"], a, small=True)}'
        + (field(r, 168, focus=True) if r else "") + '</span></div>' for n, d, a, r in CLASHES)
    body = ('<div class="mbmain" style="margin-top:6px">3 of 8 styles already exist in Episode 03.ass.</div>'
            '<div class="mbdetail">The other 5 are added. Two more are the same as the ones in the file and are '
            'left as they are.</div>'
            f'<div class="allrow"><span>All:</span>{segmented(["Replace", "Rename", "Skip"], None, small=True)}</div>'
            f'<div class="ctable"><div class="chead"><span class="cname">Style</span><span class="cdiff">'
            f'Difference</span><span class="cact">Action</span></div>{rows}</div>')
    return window("Load styles from catalog", body, btn("Load 6 styles", primary=True, focus=True) + btn("Cancel"),
                  720, resizable=True)


STYLES = [("Default", 214, True), ("Sign", 9, True), ("Song romaji", 31, True), ("Song English", 31, False),
          ("Notes", 4, False), ("Title", 1, False), ("Flashback", 22, False), ("Kenta thoughts", 6, False),
          ("Station board", 3, False), ("Preview", 12, False), ("Credits", 18, False), ("Sign – blue", 2, False)]


def choose_styles(filtered=False):
    shown = [s for s in STYLES if "s" in s[0].lower()] if filtered else STYLES[:8]
    rows = "".join(f'<div class="srow{" cur" if i == 2 and not filtered else ""}">{box(on)}<span class="sname">'
                   f'{n}</span><span class="scount">{c}</span></div>' for i, (n, c, on) in enumerate(shown))
    body = (f'{field("s" if filtered else "Search styles", None, placeholder=not filtered, focus=filtered, icon="search").replace("class=\"fld", "class=\"fld wide", 1)}'
            f'<div class="slist">{rows}' + ('<span class="sbar" style="top:4px;height:150px"></span>'
                                            if not filtered else "") + '</div>'
            f'<div class="sfoot"><span class="lnk2">Select all</span><span class="lnk2">None</span>'
            f'<span class="scount" style="margin-left:auto">3 of 12 selected</span></div>')
    pop = popover(body, 260, "margin-top:4px")
    anchor = (f'<div class="frow"><span class="flabel">Styles</span>{field("Default,Sign,Song romaji", 190)}'
              f'<span class="b sm on">Choose…</span></div>')
    return f'<div class="col">{anchor}<div style="margin-left:192px">{pop}</div></div>'


def hotkey_rows(sel):
    rows = [("Save", "Ctrl+S"), ("Save as", "Ctrl+Shift+S"), ("Save all", ""), ("Close", "Ctrl+W")]
    return ('<div class="htable"><div class="hhead"><span>Command</span><span>Keys</span></div>' + "".join(
        f'<div class="hrow{" sel" if n == sel else ""}"><span>{n}</span><span class="keys">{k}</span></div>'
        for n, k in rows) + '</div>')


def keycaps(keys, waiting=False):
    if waiting:
        return '<div class="capture focus"><span class="wait">Press the keys…</span></div>'
    return ('<div class="capture focus">' + '<span class="plus">+</span>'.join(
        f'<span class="kc">{k}</span>' for k in keys) + '</div>')


def hotkey_capture(state):
    window_choice = f'<div class="frow"><span class="flabel">In</span>{combo("Every window", 180)}</div>'
    if state == "waiting":
        body = (f'<div class="ptitle">Keys for “Save all”</div>{keycaps([], True)}{window_choice}'
                f'<div class="pfoot">{btn("Clear", small=True, lead=True)}{btn("Cancel", small=True)}</div>')
    else:
        body = (f'<div class="ptitle">Keys for “Save all”</div>{keycaps(["Ctrl", "S"])}'
                f'<div class="conf">{ic("notice-warning", 16, "warn")}<span>Ctrl+S is already used by '
                '<b>Save</b>.</span></div>'
                f'<div class="pfoot wrap">{btn("Swap with Save", small=True, primary=True, focus=True)}'
                f'{btn("Take it from Save", small=True)}{btn("Keep both", small=True)}</div>')
    pop = popover(body, 312, "margin-top:2px")
    return f'<div class="col" style="width:340px">{hotkey_rows("Save all")}<div style="margin-left:24px">{pop}</div></div>'


def list_chooser_window():
    rows = [("1", "Japanese", "AAC 2.0 · default"), ("2", "English", "AAC 2.0"),
            ("3", "Japanese", "Commentary · Opus 2.0")]
    lst = "".join(f'<div class="lrow{" cur" if i == 0 else ""}"><span class="lnum">{n}</span><span>{a}</span>'
                  f'<span class="lmeta">{b}</span></div>' for i, (n, a, b) in enumerate(rows))
    body = ('<div class="mbmain" style="margin-top:4px">Episode 03.mkv has 3 audio tracks.</div>'
            f'<div class="llist">{lst}</div>')
    return window("Choose the audio track", body, btn("Open", primary=True, focus=True) + btn("Cancel"), 400)


def list_chooser_popover():
    words = ["station", "stations", "stating", "statin"]
    lst = "".join(f'<div class="lrow{" cur" if i == 0 else ""}"><span>{w}</span></div>' for i, w in enumerate(words))
    editor = ('<div class="editor"><span>We’ll meet again at the <span class="miss">statoin</span>,\\NI promise.'
              '</span></div>')
    pop = popover(f'<div class="llist pl">{lst}</div>', 180, "margin-top:2px")
    return f'<div class="col" style="width:400px">{editor}<div style="margin-left:150px">{pop}</div></div>'


def calendar():
    days = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"]
    cells = ['<span class="dh">' + d + '</span>' for d in days]
    lead = 1  # 1 September 2026 is a Tuesday
    cells += ['<span class="dd out">31</span>'] * lead
    for d in range(1, 31):
        cls = "dd" + (" sel" if d == 21 else "")
        cells.append(f'<span class="{cls}">{d}</span>')
    for d in range(1, (7 - (lead + 30) % 7) % 7 + 1):
        cells.append(f'<span class="dd out">{d}</span>')
    head = (f'<div class="calhead"><span class="qb">{ic("chevron-left", 14)}</span><span class="calm">September 2026'
            f'</span><span class="qb">{ic("chevron-right", 14)}</span></div>')
    return (head + f'<div class="cal">{"".join(cells)}</div>'
            f'<div class="sfoot"><span class="lnk2">A week ago</span><span class="lnk2">A month ago</span>'
            f'<span class="lnk2" style="margin-left:auto">Today</span></div>')


def date_picker():
    anchor = (f'<div class="frow"><span class="flabel" style="width:auto">Remove auto saves older than</span>'
              f'{field("21/09/2026", 128, trail=ic("calendar", 16), focus=True)}</div>')
    pop = popover(calendar(), 252, "margin-top:2px")
    return f'<div class="col">{anchor}<div style="margin-left:186px">{pop}</div></div>'


def pieces(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PK1 popup kit: the shared pieces ({th["name"]})</h1>',
        '<p class="note">Built once and used everywhere; the copies named under each one are replaced in this '
        'card. Style names, files, keys and dates are examples.</p>',
        '<h2>Clash review (a window on DG1’s frame)</h2>',
        f'<div class="row"><div class="col">{clash_review(key)}</div>'
        '<p class="cap" style="max-width:480px;margin-top:0">Replaces the chained “Yes to all / Yes / No / '
        'Cancel” questions of style copy and load, catalogs and shape names. One row per clash, each with its '
        'own action; “All” sets every row. Rename suggests a free name and lets it be edited. Clashes that are '
        'the same both sides are not listed. The button counts what will be written.</p></div>',
        '<h2>Choose styles (popover)</h2>',
        '<p class="note">Replaces the four copies: Find and replace, Select lines, Shift '
        'times and the Style manager’s “Choose styles”.</p>',
        '<div class="row">',
        f'<div class="col">{choose_styles()}<p class="cap">Search, scroll, Select all / None, the count. Each '
        'style shows how many lines use it. A tick changes the field at once; Esc puts the field back.</p></div>',
        f'<div class="col">{choose_styles(filtered=True)}<p class="cap">Typing filters the list (here “s”); '
        'Select all and None act on the rows shown.</p></div>',
        '</div>',
        '<h2>Hotkey capture (popover from the hotkey list)</h2>',
        '<p class="note">Replaces Main.qml’s hotkeyCapture (Automation hotkeys) and '
        'HotkeyMapping.qml (Settings › Hotkeys).</p>',
        '<div class="row">',
        f'<div class="col">{hotkey_capture("waiting")}<p class="cap">Waiting for keys. Esc cancels, so Esc '
        'alone cannot be a hotkey (as legacy). Clear removes the keys. “In” is the window the hotkey works '
        'in, where the command has a choice.</p></div>',
        f'<div class="col">{hotkey_capture("conflict")}<p class="cap">The keys are taken: the choice is made '
        'here, with buttons named for it (legacy’s “Switch hotkeys / Delete hotkey / Set anyway” warning). '
        'Pressing other keys starts again.</p></div>',
        '</div>',
        '<h2>List chooser</h2>',
        '<p class="note">Replaces the three 220 × 160 list dialogs: audio track, MKV '
        'subtitle track and fix suggestions.</p>',
        '<div class="row">',
        f'<div class="col">{list_chooser_window()}<p class="cap">As a compact window when nothing on screen '
        'asked for it (opening a file with several audio or subtitle tracks). Rows carry the track’s language '
        'and format; double-click or Enter opens.</p></div>',
        f'<div class="col">{list_chooser_popover()}<p class="cap">As a popover at the word for spelling '
        'suggestions. Enter or a click replaces the word.</p></div>',
        '</div>',
        '<h2>Date picker (a date field with a calendar popover)</h2>',
        '<div class="row">',
        f'<div class="col">{date_picker()}</div>',
        '<p class="cap" style="max-width:420px;margin-top:40px">The date is typed in the system’s short date '
        'format, or picked: arrows move the day, Page Up and Page Down the month. The week starts on the '
        'system’s first day. Shortcuts for the common choices. Replaces the day / month / year boxes in '
        'Remove temporary files.</p>',
        '</div>',
    ])


# ---------------------------------------------------------------------------------------------
# Page

CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(page)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:1328px}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
h1{font-size:18px;font-weight:600;margin:0 0 8px}
h2{font-size:13px;font-weight:650;margin:28px 0 10px}
p.note{color:%(muted)s;font-size:12px;margin:0 0 8px;max-width:1280px;line-height:1.45}
.cap{font-size:11px;color:%(muted)s;margin:8px 0 0;max-width:520px;line-height:1.4}
.row{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap}
.col{display:flex;flex-direction:column}
.notes{background:%(raised)s;outline:1px solid %(line)s;padding:12px 16px;width:1280px;line-height:1.5}
.notes td{padding:3px 14px 3px 0;vertical-align:top}
.notes td:first-child{font-weight:650;white-space:nowrap}

/* main window backdrop */
.app{position:relative;width:1280px;height:800px;background:url(%(body)s) 0 0 no-repeat;outline:1px solid %(line)s;margin-top:12px}
.barslot{position:absolute;left:0;top:64px;width:1280px}
.stack{position:absolute;right:12px;bottom:34px;width:360px;display:flex;flex-direction:column;gap:8px}

/* DG1 window */
.dlg{background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s)}
.dtb{display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtitle{flex:1;min-width:0;padding-left:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.wbs{display:flex;margin-left:auto;flex:none}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.dbody{padding:12px 24px 0;display:flex;flex-direction:column}
.dfoot{display:flex;gap:8px;justify-content:flex-end;padding:24px}
.dfoot .lead,.pfoot .lead{margin-right:auto}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;min-width:80px;height:30px;padding:0 10px;border-radius:1px;
   background:%(raised)s;border:1px solid %(line)s;color:%(text)s;white-space:nowrap;flex:none}
.b.sm{min-width:0;height:26px;padding:0 10px}
.b.on{background:%(select)s;border-color:%(accent)s}
.b.flat{background:none;border-color:transparent;color:%(accent)s;padding:0 6px;height:24px}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.b.dng{color:%(danger)s;border-color:%(danger)s}
.ring{outline:1px solid %(text)s;outline-offset:1px}
.qb{display:inline-flex;align-items:center;justify-content:center;width:22px;height:22px;border-radius:1px;color:%(text)s}
.cb{display:flex;align-items:center;gap:8px;height:24px}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.fld{display:inline-flex;align-items:center;gap:6px;height:28px;padding:0 8px;background:%(field)s;border:1px solid %(line)s;
     border-radius:1px;color:%(text)s;min-width:0}
.fld.wide{display:flex;width:100%%}
.fld.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.fld.ph .ftext{color:%(muted)s}
.ftext{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fic,.ftail{color:%(muted)s}
.ftail{display:flex;margin-left:auto}
.frow{display:flex;align-items:center;gap:8px;margin:6px 0}
.flabel{width:56px;color:%(text)s;flex:none}
.seg{display:inline-flex;border:1px solid %(line)s;border-radius:1px;overflow:hidden;flex:none}
.seg span{padding:0 10px;height:26px;display:flex;align-items:center;background:%(raised)s;border-left:1px solid %(line)s}
.seg span:first-child{border-left:none}
.seg span.on{background:%(select)s;color:%(text)s;box-shadow:inset 0 -2px %(accent)s;font-weight:650}
.seg.sm span{height:24px;padding:0 8px}
.sbar{position:absolute;right:3px;width:4px;border-radius:2px;background:%(control)s;opacity:.8}
.big{color:%(text)s}
.warn,.warn .acc{color:%(warning)s}
.err,.err .acc{color:%(danger)s}
.lnk2{color:%(accent)s;cursor:pointer}

/* message box */
.mb{display:flex;gap:16px;align-items:flex-start;margin-top:8px}
.mbtext{display:flex;flex-direction:column;gap:6px;padding-top:4px;min-width:0}
.mbmain{font-size:14px;font-weight:650;line-height:1.4}
.mbdetail{color:%(muted)s;line-height:1.45}
.mbcheck{margin-top:8px}
.disc{display:flex;align-items:center;gap:4px;margin:14px 0 0 48px;color:%(text)s}
.dets{position:relative;margin:6px 0 0 48px;background:%(field)s;outline:1px solid %(line)s;padding:8px 34px 8px 10px}
.dets pre{font:12px 'Liberation Mono',Consolas,monospace;color:%(text)s;white-space:pre-wrap}
.dets .qb{position:absolute;right:6px;top:6px}

/* notices */
.toast{display:flex;gap:10px;align-items:flex-start;padding:10px 8px 10px 12px;background:%(panel)s;outline:1px solid %(line)s;
       border-radius:4px;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);color:%(text)s}
.toast.hover{background:%(raised)s}
.toast>.ic{margin-top:1px}
.tt{flex:1;min-width:0;display:flex;flex-direction:column;gap:3px}
.ttitle{font-weight:650;line-height:1.35}
.ttext{color:%(muted)s;line-height:1.4}
.tacts{display:flex;gap:4px;margin:4px 0 0 -6px}
.tx{display:flex;align-items:center;justify-content:center;width:22px;height:22px;color:%(muted)s;flex:none}
.ibar{display:flex;align-items:center;gap:8px;height:38px;padding:0 6px 0 10px;background:%(raised)s;
      box-shadow:inset 0 -1px %(line)s,inset 0 1px %(line)s;color:%(text)s}
.ibt{flex:1}

/* popover */
.pop{position:relative;background:%(panel)s;outline:1px solid %(line)s;border-radius:4px;padding:12px;
     box-shadow:0 6px 22px rgba(0,0,0,%(shadow)s);color:%(text)s;z-index:2}
.ptitle{font-weight:650;margin-bottom:8px}
.pfoot{display:flex;gap:6px;justify-content:flex-end;margin-top:10px}
.pfoot.wrap{flex-wrap:wrap;justify-content:flex-start}
.anchorbox{position:relative}

/* clash review */
.allrow{display:flex;align-items:center;gap:10px;margin-top:14px}
.ctable{margin-top:10px;outline:1px solid %(line)s;background:%(field)s}
.chead,.crow2{display:flex;align-items:center;gap:10px;padding:0 10px;min-height:38px}
.chead{min-height:28px;color:%(muted)s;font-size:12px;box-shadow:inset 0 -1px %(line)s;background:%(raised)s}
.crow2+.crow2{box-shadow:inset 0 1px %(line)s}
.crow2 .cname{font-weight:650}
.cname{width:110px;flex:none}
.cdiff{width:170px;flex:none;color:%(muted)s}
.cact{flex:1;display:flex;gap:8px;align-items:center}

/* choose styles */
.slist{position:relative;margin-top:8px;height:186px;overflow:hidden;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.srow{display:flex;align-items:center;gap:8px;height:22px;padding:0 14px 0 8px}
.srow.cur{background:%(select)s}
.sname{flex:1}
.scount{color:%(muted)s;font-size:12px}
.sfoot{display:flex;gap:14px;align-items:center;margin-top:10px}

/* hotkeys */
.htable{outline:1px solid %(line)s;background:%(field)s}
.hhead,.hrow{display:grid;grid-template-columns:1fr 130px;padding:0 10px;height:26px;align-items:center}
.hhead{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.hrow.sel{background:%(select)s}
.keys{color:%(muted)s}
.capture{display:flex;align-items:center;justify-content:center;gap:6px;height:48px;background:%(field)s;
         border:1px solid %(line)s;border-radius:1px}
.capture.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.wait{color:%(muted)s}
.kc{display:inline-flex;align-items:center;height:28px;padding:0 10px;border:1px solid %(control)s;border-bottom-width:2px;
    border-radius:3px;background:%(raised)s;font-weight:650}
.plus{color:%(muted)s}
.conf{display:flex;gap:8px;align-items:flex-start;margin-top:10px;line-height:1.4}

/* lists */
.llist{margin-top:10px;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.llist.pl{margin-top:0}
.lrow{display:flex;gap:10px;align-items:center;height:28px;padding:0 10px}
.lrow.cur{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.lnum{color:%(muted)s;width:12px}
.lmeta{color:%(muted)s;margin-left:auto}
.editor{height:30px;display:flex;align-items:center;padding:0 10px;background:%(field)s;border:1px solid %(line)s}
.miss{text-decoration:underline wavy %(danger)s;text-underline-offset:3px;background:%(select)s}

/* calendar */
.calhead{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
.calm{font-weight:650}
.cal{display:grid;grid-template-columns:repeat(7,32px);row-gap:2px}
.dh{height:24px;display:flex;align-items:center;justify-content:center;color:%(muted)s;font-size:11px}
.dd{height:28px;display:flex;align-items:center;justify-content:center;border-radius:1px}
.dd.out{color:%(disabled)s}
.dd.sel{background:%(accent)s;color:%(onaccent)s;font-weight:650;outline:1px solid %(text)s;outline-offset:1px}
"""


def page(key, content):
    th = THEMES[key]
    css = CSS % dict(th, page=th["bg"], body=f"shots/body-{th['code']}.png",
                     shadow=".55" if key in DARKISH else ".28",
                     focus=th["accent"] if key not in HC else th["text"],
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')


def main():
    for key in THEMES:
        (OUT / f"pk1-{key}-kinds.html").write_text(page(key, kinds(key)))
        (OUT / f"pk1-{key}-notices.html").write_text(page(key, overview(key)))
        (OUT / f"pk1-{key}-pieces.html").write_text(page(key, pieces(key)))


