#!/usr/bin/env python3
"""SY1 #247 Style manager and Style editor mockup: per theme, the window, its states and its popups.

"Today" is a real screenshot of today's Style manager (the PK1 build, qt 58192af34 content) in that theme,
taken by body.sh under vx11, as are the SY2 colour popover and the SY3 font list, which SY1 keeps. Everything
else is drawn here from the theme layer's roles (src/ui/theme.cpp baseRoles, default accent), DG1's dialog
frame (DialogWindow.qml), PK1's kit (MessageBox, Popover, ChooseStyles, ToastStack) and IC1's icons.

usage: gen.py [TREE]"""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-sy1")
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

GLYPHS = {
    "check": '<path d="M3.5 8.5l3 3 6-7"/>',
}
REPO_ICONS = ["about", "notice-warning", "notice-error", "search", "tab-close", "window-close", "window-maximize",
              "add", "duplicate", "delete", "move-up", "move-down", "move-to-top", "move-to-bottom", "sort",
              "import", "panel-menu", "copy-to-ass", "copy-to-storage", "copy-to-all-ass", "clear", "undo",
              "chevron-down", "tag-bold", "tag-italic", "tag-underline", "tag-strikeout", "settings", "filter",
              "fonts-from-subtitles", "edit", "styles"]


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
# DG1's frame and PK1's controls


def titlebar(title, resizable=False):
    btns = ((f'<span class="wb">{ic("window-maximize")}</span>' if resizable else "")
            + f'<span class="wb">{ic("window-close")}</span>')
    return f'<div class="dtb"><span class="dtitle">{title}</span><span class="wbs">{btns}</span></div>'


def window(title, body, foot, width, resizable=False, style="", body_cls="", foot_cls=""):
    return (f'<div class="dlg" style="width:{width}px;{style}">{titlebar(title, resizable)}'
            f'<div class="dbody {body_cls}">{body}</div><div class="dfoot {foot_cls}">{foot}</div></div>')


def btn(text, primary=False, focus=False, lead=False, danger=False, small=False, flat=False, off=False, icon=None):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" lead" if lead else "") + (
        " dng" if danger else "") + (" sm" if small else "") + (" flat" if flat else "") + (" off" if off else "")
    lead_icon = ic(icon, 16) if icon else ""
    return f'<span class="{cls}">{lead_icon}{text}</span>'


def qb(icon, tip="", on=False, off=False, focus=False):
    cls = "qb" + (" on" if on else "") + (" off" if off else "") + (" ring" if focus else "")
    return f'<span class="{cls}" title="{tip}">{ic(icon, 16)}</span>'


def box(on):
    mark = ('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
            'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
    return f'<span class="box{" on" if on else ""}">{mark if on else ""}</span>'


def checkbox(text, on, focus=False):
    return f'<div class="cb{" ring" if focus else ""}">{box(on)}<span class="cbt">{text}</span></div>'


def field(text, width=None, placeholder=False, focus=False, icon=None, trail=None, unit=None, mixed=False,
          bad=False, off=False, cls=""):
    w = f"width:{width}px;" if width else ""
    c = ("fld" + (" ph" if placeholder else "") + (" focus" if focus else "") + (" mixed" if mixed else "")
         + (" bad" if bad else "") + (" off" if off else "") + (f" {cls}" if cls else ""))
    lead = ic(icon, 16, "fic") if icon else ""
    tail = f'<span class="ftail">{trail}</span>' if trail else ""
    u = f'<span class="unit">{unit}</span>' if unit else ""
    return f'<span class="{c}" style="{w}">{lead}<span class="ftext">{text}</span>{u}{tail}</span>'


def combo(text, width=None, **kw):
    return field(text, width, trail=ic("chevron-down", 14), **kw)


def popover(body, width, style="", cls=""):
    return f'<div class="pop {cls}" style="width:{width}px;{style}">{body}</div>'


# ---------------------------------------------------------------------------------------------
# The example Styles: name, font, size, primary, outline, bold, italic, lines using it

FILE_STYLES = [
    ("Default", "Noto Sans", 72, "#FFFFFF", "#000000", False, False, 214),
    ("Default - Italic", "Noto Sans", 72, "#FFFFFF", "#000000", False, True, 37),
    ("Flashback", "Noto Sans", 72, "#FFF4E8", "#103050", False, True, 22),
    ("Note", "Noto Sans", 44, "#FFFFFF", "#1B3A66", False, False, 6),
    ("Sign - Station", "Noto Serif", 64, "#F4F0E2", "#2B2B2B", True, False, 2),
    ("Sign - Title", "Noto Serif", 90, "#FFE6A8", "#3A1E10", True, False, 1),
    ("OP Romaji", "Noto Sans", 52, "#FFD7F0", "#5A1048", True, False, 24),
    ("OP English", "Noto Sans", 46, "#FFFFFF", "#5A1048", False, False, 24),
    ("Credits", "Noto Sans", 38, "#E8E8E8", "#000000", False, False, 0),
]
CATALOG_STYLES = [
    ("Default", "Noto Sans", 72, "#FFFFFF", "#000000"),
    ("Default - Italic", "Noto Sans", 72, "#FFFFFF", "#000000"),
    ("Note", "Noto Sans", 44, "#FFFFFF", "#1B3A66"),
    ("Sign", "Noto Serif", 60, "#FFFFFF", "#102040"),
    ("Song romaji", "Noto Sans", 52, "#FFD7F0", "#5A1048"),
    ("Song English", "Noto Sans", 46, "#FFFFFF", "#5A1048"),
    ("Title", "Noto Serif", 90, "#FFE6A8", "#3A1E10"),
    ("Credits", "Noto Sans", 38, "#E8E8E8", "#000000"),
]


def swatch(primary, outline):
    return f'<span class="sw" style="background:{primary};border-color:{outline}"></span>'


def style_row(name, primary, outline, count=None, selected=False, dirty=False, focus=False, new=False):
    cls = "srow" + (" sel" if selected else "") + (" ring" if focus else "")
    mark = '<span class="dirty" title="Changes not applied"></span>' if dirty else ""
    cnt = (f'<span class="cnt{" zero" if count == 0 else ""}" title="Lines using it">{count}</span>'
           if count is not None else "")
    nm = f'<span class="sname{" newname" if new else ""}">{name}</span>'
    return f'<div class="{cls}">{swatch(primary, outline)}{nm}{mark}{cnt}</div>'


def strip(file_list, focus_more=False):
    return ('<div class="strip">'
            + qb("add", "New style") + qb("duplicate", "Duplicate") + qb("delete", "Delete")
            + '<span class="vsep"></span>' + qb("move-up", "Move up (Alt+Up)") + qb("move-down", "Move down (Alt+Down)")
            + '<span class="vsep"></span>' + qb("import", "Load from a file…")
            + '<span class="grow"></span>' + qb("panel-menu", "More", focus=focus_more) + '</div>')


def sidebar(sel_file=(4,), sel_cat=(), dirty_file=(4,), filter_text="", catalog="Default", cat_rows=None,
            file_rows=None, focus=None, new_row=None, hide_cat_count=False):
    cat = cat_rows if cat_rows is not None else CATALOG_STYLES
    files = file_rows if file_rows is not None else FILE_STYLES
    flt = field(filter_text or "Filter styles", None, placeholder=not filter_text, icon="search", cls="wide",
                focus=focus == "filter")
    cat_list = "".join(style_row(n, p, o, selected=i in sel_cat, focus=(focus == "cat" and i == (sel_cat or [0])[0]))
                       for i, (n, f, s, p, o) in enumerate(cat))
    file_list = "".join(style_row(n, p, o, c, selected=i in sel_file, dirty=i in dirty_file,
                                  focus=(focus == "file" and i == (sel_file or [0])[0]))
                        for i, (n, f, s, p, o, b, it, c) in enumerate(files))
    if new_row:
        file_list += style_row(new_row, "#FFFFFF", "#000000", 0, selected=True, dirty=True, new=True)
    to_file_on = bool(sel_cat)
    to_cat_on = bool(sel_file)
    return (f'<div class="side">{flt}'
            f'<div class="shead"><span class="stitle">Catalog</span>{combo(catalog, 150)}'
            f'{qb("add", "New catalog", on=focus == "newcat")}{qb("delete", "Delete catalog", off=catalog == "Default")}</div>'
            f'{strip(False)}<div class="slist">{cat_list}</div>'
            f'<div class="xfer">{btn("Add to file", small=True, icon="copy-to-ass", off=not to_file_on)}'
            f'{btn("Add to catalog", small=True, icon="copy-to-storage", off=not to_cat_on)}'
            f'{qb("copy-to-all-ass", "Add to all open files", off=not to_file_on)}</div>'
            f'<div class="shead"><span class="stitle">This file</span><span class="grow"></span></div>'
            f'{strip(True)}<div class="slist">{file_list}</div></div>')


# ---------------------------------------------------------------------------------------------
# The editor


def lab(text, changed=False):
    reset = f'<span class="rst" title="Put back the value it had when opened">{ic("undo", 14)}</span>' if changed else ""
    return f'<div class="lab"><span>{text}</span>{reset}</div>'


def cell(label, control, span=1, changed=False):
    return f'<div class="cell" style="grid-column:span {span}">{lab(label, changed)}{control}</div>'


def section(title, body, cols=4):
    return (f'<div class="sec"><div class="sect">{title}</div>'
            f'<div class="grid" style="grid-template-columns:repeat({cols},1fr)">{body}</div></div>')


def toggles(on, mixed=()):
    names = [("tag-bold", "Bold"), ("tag-italic", "Italic"), ("tag-underline", "Underline"),
             ("tag-strikeout", "Strikethrough")]
    return ('<span class="tg">' + "".join(
        f'<span class="tgb{" on" if n in on else ""}{" mix" if n in mixed else ""}" title="{n}">{ic(i, 16)}</span>'
        for i, n in names) + '</span>')


def align_grid(chosen, mixed=False):
    cells = ""
    for a in [7, 8, 9, 4, 5, 6, 1, 2, 3]:
        cells += f'<span class="ag{" on" if a == chosen and not mixed else ""}" title="{a}"><i></i></span>'
    return f'<span class="agrid{" mix" if mixed else ""}">{cells}</span>'


def colour_btn(hex_, alpha=0, mixed=False):
    if mixed:
        return '<span class="csw mixed" title="Multiple values"></span>'
    return (f'<span class="csw"><span class="cfill" style="background:{hex_};opacity:{1 - alpha / 255:.2f}"></span>'
            f'<span class="chex">{hex_}</span></span>')


def preview(text_style, sample="Podgląd", height=84, mixed=False):
    if mixed:
        inner = '<span class="pvmix">3 styles: the preview shows Sign - Station</span>'
        inner = f'<span class="pvtext" style="{text_style}">{sample}</span>' + inner
    else:
        inner = f'<span class="pvtext" style="{text_style}">{sample}</span>'
    return (f'<div class="pv" style="height:{height}px">{inner}</div>'
            f'{field(sample, None, cls="wide sample", icon="edit")}')


SIGN_PREVIEW = ("font-family:'Noto Serif',serif;font-weight:700;font-size:44px;color:#F4F0E2;"
                "-webkit-text-stroke:3px #2B2B2B;paint-order:stroke fill;letter-spacing:.5px;"
                "transform:scale(1.125,1.125)")


def editor(state="single", scroll=False, compact=False):
    """state: single (Sign - Station, Size changed), multi (3 Styles), rename, clash, catalog, new."""
    multi = state == "multi"
    name = {"single": "Sign - Station", "rename": "Sign - Station board", "clash": "Sign - Title",
            "catalog": "Sign", "new": "New style", "multi": "Multiple values"}[state]
    name_ctl = field(name, None, cls="wide", mixed=multi, off=multi, focus=state in ("rename", "clash", "new"),
                     bad=state == "clash")
    name_extra = ""
    if state == "rename":
        name_extra = f'<div class="under">{checkbox("Rename it in the 2 lines that use it", True)}</div>'
    if state == "clash":
        name_extra = ('<div class="under err">This file already has a style named “Sign - Title”.</div>')
    p = SIGN_PREVIEW if state != "catalog" else SIGN_PREVIEW.replace("#F4F0E2", "#FFFFFF").replace("#2B2B2B", "#102040")
    if state == "new":
        p = ("font-family:'Liberation Sans',Arial,sans-serif;font-size:40px;color:#FFFFFF;"
             "-webkit-text-stroke:3px #000000;paint-order:stroke fill")
    size_changed = state == "single"
    size = {"catalog": "60", "new": "20"}.get(state, "64")
    font = {"new": "Arial"}.get(state, "Noto Serif")
    font_body = (cell("Family", combo(font, None, cls="wide", mixed=False), 3)
                 + cell("Size", field(size, None, cls="wide", mixed=multi, focus=size_changed), 1, changed=size_changed)
                 + cell("Font catalog", '<span class="fcbar">' + combo("All fonts", None, cls="grow") + qb("add", "Add the font to a catalog")
                        + qb("settings", "Manage font catalogs") + qb("filter", "Show only fonts with the preview text’s characters")
                        + qb("fonts-from-subtitles", "Add fonts from subtitles") + '</span>', 2)
                 + cell("Emphasis", toggles({"Bold"} if state != "new" else set(), {"Bold"} if multi else ()), 1)
                 + cell("Text encoding", combo("1 - Default", None, cls="wide"), 1))
    if multi:
        font_body = (cell("Family", combo("Multiple values", None, cls="wide", mixed=True), 3)
                     + cell("Size", field("Multiple values", None, cls="wide", mixed=True), 1)
                     + cell("Font catalog", '<span class="fcbar">' + combo("All fonts", None, cls="grow") + qb("add", "Add")
                            + qb("settings", "Manage") + qb("filter", "Filter") + qb("fonts-from-subtitles", "From subtitles")
                            + '</span>', 2)
                     + cell("Emphasis", toggles({"Bold"}, {"Bold"}), 1)
                     + cell("Text encoding", combo("1 - Default", None, cls="wide"), 1))
    cols = [("Primary", "#F4F0E2" if state not in ("catalog", "new") else "#FFFFFF", 0),
            ("Secondary", "#FF0000" if state != "new" else "#0000FF", 0),
            ("Border", "#2B2B2B" if state not in ("catalog", "new") else ("#102040" if state == "catalog" else "#000000"), 0),
            ("Shadow", "#000000", 0)]
    colour_body = "".join(cell(n, '<span class="cpair">' + colour_btn(h, a, mixed=multi and n == "Primary")
                               + field("0", 46, cls="alpha") + '</span>') for n, h, a in cols)
    w = state not in ("catalog", "new")
    geo = (cell("Border", field("2", None, cls="wide", mixed=multi))
           + cell("Shadow", field("0", None, cls="wide"))
           + cell("Scale X", field("112.5" if w else "100", None, cls="wide", unit="%", mixed=multi))
           + cell("Scale Y", field("112.5" if w else "100", None, cls="wide", unit="%", mixed=multi))
           + cell("Angle", field("0", None, cls="wide", unit="°"))
           + cell("Spacing", field("0.5" if w else "0", None, cls="wide", mixed=multi))
           + cell("Border type", checkbox("Opaque box", False))
           + f'<div class="cell" style="grid-row:span 2;align-self:start">{lab("Alignment")}'
           f'{align_grid(7 if w else (8 if state == "catalog" else 2), mixed=multi)}</div>'
           + cell("Left margin", field("40" if state != "new" else "10", None, cls="wide"))
           + cell("Right margin", field("40" if state != "new" else "10", None, cls="wide"))
           + cell("Vertical margin", field("40" if state != "new" else "10", None, cls="wide", mixed=multi)))
    form = (section("Name", f'<div class="cell" style="grid-column:span 4">{name_ctl}{name_extra}</div>')
            + section("Font", font_body)
            + section("Colours and transparency", colour_body)
            + section("Shape and position", geo))
    bar = '<span class="vbar"></span>' if scroll else ""
    return (f'<div class="ed{" compact" if compact else ""}"><div class="pvbox">{preview(p, mixed=multi)}</div>'
            f'<div class="form">{form}{bar}</div></div>')


def footer(dirty=True, focus_ok=True):
    return (btn("Revert", lead=True, off=not dirty) + btn("OK", primary=True, focus=focus_ok)
            + btn("Cancel") + btn("Apply", off=not dirty))


def manager(width=980, height=760, side_kw=None, ed_state="single", dirty=True, scroll=False, compact=False,
            overlay=""):
    body = f'<div class="mgr" style="height:{height - 32 - 70}px">{sidebar(**(side_kw or {}))}{editor(ed_state, scroll, compact)}{overlay}</div>'
    return window("Style manager", body, footer(dirty), width, resizable=True, body_cls="flush",
                  foot_cls="lined", style=f"height:{height}px")


# ---------------------------------------------------------------------------------------------
# Sheets


def notes_table(rows):
    return ('<div class="notes"><table>' + "".join(f'<tr><td>{a}</td><td>{b}</td></tr>' for a, b in rows)
            + '</table></div>')


def sheet_window(key):
    th = THEMES[key]
    return "".join([
        f'<h1>SY1 Style manager and Style editor: the window ({th["name"]})</h1>',
        '<p class="note">One window on DG1’s frame (resizable, modeless, its size and place remembered), the '
        'Style editor always beside the lists: pick a style and edit it, with no editing mode and no second '
        'set of buttons. The styles, fonts and colours are examples; the font names render in this machine’s '
        'fonts.</p>',
        '<div class="row">',
        f'<div class="col">{manager()}'
        '<p class="cap" style="max-width:980px">“Sign - Station” is being edited: its Size went from 60 to 64, so '
        'the Size label shows a reset button, the row shows the unapplied mark and Revert and Apply are on. '
        'The preview follows every field (SY3); the swatches open SY2’s colour popover.</p></div>',
        '</div>',
        '<h2>Today (the PK1 build, for comparison)</h2><div class="row">',
        f'<div class="col"><div class="today"><img src="shots/today-manager-{th["code"]}.png" width="600"></div>'
        '<p class="cap">Not editing: two lists with ten-button columns and “Add to storage / ASS / all open ASS '
        'files” between them; OK and Close.</p></div>',
        f'<div class="col"><div class="today"><img src="shots/today-editor-{th["code"]}.png" width="600"></div>'
        '<p class="cap">After Edit: the editor opens at the side and the footer becomes OK, Cancel and Apply.</p></div>',
        '</div>',
        '<h2>What is where</h2>',
        notes_table([
            ("Lists", "The catalog above and this file’s styles below, as legacy (the copy icons point that way). "
             "One shared filter. Each row: the primary colour on the border colour, the name, and for the file the "
             "number of lines that use it (0 is muted). Click, Shift+click and Ctrl+click select; a selection in "
             "one list clears the other, so the editor and the copy buttons always act on one list."),
            ("List tools", "Icon-only, names on hover: New, Duplicate, Delete, Move up, Move down, Load from a "
             "file…, and ⋯ for Move to top, Move to bottom, Sort by name and (this file) Delete unused styles. "
             "“Edit” goes: the editor already shows the selection."),
            ("Copy", "“Add to file” (catalog → this file), “Add to catalog” (this file → catalog), and the icon "
             "for “Add to all open files”. Only the direction that has a selection is on. Clashes go to PK1’s "
             "clash review, as today."),
            ("Catalog", "The catalog choice in the Catalog heading, with New (a popover for the name) and Delete "
             "(a message box; off for Default)."),
            ("Editor", "Sections in MuseScore’s Properties form: a bold title, labels above the fields, 12 apart. "
             "Every Style field is here. Units sit in their fields (%, °). Bold, Italic, Underline and Strikethrough "
             "are icon toggles. The preview and its text stay pinned at the top while the form scrolls."),
            ("Buttons", "One closing row that never changes: OK applies and closes, Apply applies, Cancel (and Esc, "
             "and ×) drops what is not applied and closes; Revert, at the leading edge, drops it and stays. List "
             "actions (copy, delete, move) take effect at once, and in the file Ctrl+Z undoes them, as today."),
        ]),
    ])


def sheet_states(key):
    th = THEMES[key]
    cat_sel = manager(side_kw=dict(sel_file=(), sel_cat=(3,), dirty_file=()), ed_state="catalog", dirty=False,
                      height=760)
    return "".join([
        f'<h1>SY1: the editor’s states ({th["name"]})</h1>',
        '<div class="row">',
        f'<div class="col"><div class="crop" style="width:620px;height:556px">{editor("multi")}</div>'
        '<p class="cap" style="max-width:620px">Three styles selected (Sign - Station, Sign - Title and OP '
        'Romaji): the fields they share show their value, the others “Multiple values” in muted italics (the '
        'visual language’s mixed value), a mixed colour hatched. Apply writes only the fields that were changed, to all three. The name '
        'is off. This replaces the “Change all selected styles?” question.</p></div>',
        '<div class="col">'
        f'<div class="crop" style="width:620px;height:290px">{editor("rename")}</div>'
        '<p class="cap" style="max-width:620px">The name changed: a check box under it, on by default, renames '
        'the style in the lines that use it (it replaces the “Style name modified…” question). Not shown for a '
        'catalog style, nor when no line uses it.</p>'
        f'<div class="crop" style="width:620px;height:232px;margin-top:16px">{editor("clash")}</div>'
        '<p class="cap" style="max-width:620px">A name already taken: the field in the danger colour with the '
        'reason under it, and OK and Apply off until it is fixed (no message box after the fact). An empty name '
        'and a comma are refused the same way.</p></div>',
        '</div>',
        '<h2>A catalog style</h2>',
        f'<div class="row"><div class="col">{cat_sel}</div>'
        '<p class="cap" style="max-width:300px;margin-top:0">“Sign” in the Default catalog is selected: the '
        'editor edits the catalog’s copy (no line counts, no rename check box). “Add to file” is on and “Add to '
        'catalog” off. The catalog is saved when it changes and when the window closes, as today.</p></div>',
    ])


def sheet_small(key):
    th = THEMES[key]
    return "".join([
        f'<h1>SY1: on a small screen, and a new style ({th["name"]})</h1>',
        '<div class="row">',
        f'<div class="col">{manager(853, 533, side_kw=dict(), scroll=True, compact=True)}'
        '<p class="cap" style="max-width:853px">The first open is sized to its content and shrunk to the screen’s '
        'free area (DG1). At 1280 × 800 and 150 % that is 853 × 533: the lists share the height and scroll, the '
        'editor scrolls under its pinned preview with its scroll bar shown, nothing is cut off and no control '
        'shrinks below its text.</p></div>',
        '</div>',
        '<h2>New and Duplicate</h2>',
        '<div class="row">',
        f'<div class="col">{manager(side_kw=dict(sel_file=(), dirty_file=(), new_row="New style"), ed_state="new")}'
        '<p class="cap" style="max-width:980px">New adds a “New style” row at the end, selected, with legacy’s '
        'defaults and the name selected for typing; Duplicate adds “Copy of …” after the original. The row is '
        'only made by Apply or OK; Revert, Cancel or Esc drop it.</p></div>',
        '</div>',
    ])


# --- popups

def mb(title, main, detail, severity, foot, width=468, extra=""):
    glyph, cls = {"info": ("about", ""), "warning": ("notice-warning", "warn")}[severity]
    body = (f'<div class="mb">{ic(glyph, 32, "big " + cls)}<div class="mbtext"><div class="mbmain">{main}</div>'
            + (f'<div class="mbdetail">{detail}</div>' if detail else "") + extra + '</div></div>')
    return window(title, body, foot, width)


def more_menu():
    items = [("move-to-top", "Move to top", "Alt+Home"), ("move-to-bottom", "Move to bottom", "Alt+End"),
             ("sort", "Sort by name", ""), None, ("clear", "Delete unused styles", "")]
    rows = ""
    for it in items:
        if it is None:
            rows += '<div class="msep"></div>'
            continue
        i, t, k = it
        rows += (f'<div class="mi{" cur" if t == "Delete unused styles" else ""}"><span class="mchk"></span>{ic(i, 16)}'
                 f'<span class="mt">{t}</span><span class="mk">{k}</span></div>')
    return f'<div class="menu">{rows}</div>'


def toast():
    return ('<div class="toast">' + ic("about", 16) + '<div class="tt"><div class="ttitle">Deleted 2 unused styles'
            '</div><div class="ttext">Credits and Sign - Title. Ctrl+Z puts them back.</div>'
            f'<div class="tacts">{btn("Details", small=True, flat=True)}</div></div>'
            f'<span class="tx">{ic("tab-close", 14)}</span></div>')


def new_catalog_pop():
    return popover(f'<div class="frow"><span class="flabel">Name</span>{field("Episode project", 170, focus=True)}</div>'
                   f'<div class="pfoot">{btn("Create", primary=True, small=True)}</div>', 268)


def choose_from_file():
    names = [("Default", True), ("Sign", True), ("Sign - Bus", True), ("Song romaji", False), ("Song English", False),
             ("Title", False)]
    rows = "".join(f'<div class="crow">{box(on)}<span class="sname">{n}</span></div>' for n, on in names)
    body = (field("Search styles", None, placeholder=True, icon="search", cls="wide")
            + f'<div class="clist">{rows}</div>'
            f'<div class="cfoot"><span class="lnk2">Select all</span><span class="lnk2">None</span>'
            f'<span class="scount" style="margin-left:auto">3 of 6 selected</span></div>'
            f'<div class="pfoot">{btn("Load", primary=True, small=True)}</div>')
    return popover(body, 268)


def sheet_popups(key):
    th = THEMES[key]
    code = th["code"]
    return "".join([
        f'<h1>SY1: the popups, on PK1’s kit ({th["name"]})</h1>',
        '<p class="note">The six in-window dialogs of StyleManager.qml (createQuestion, message, multiQuestion, '
        'renameQuestion, newCatalog, deleteCatalogQuestion) and Main.qml’s cleanStylesMessage leave PK1’s allow-list: '
        'two become inline (the rename check box and the name error, previous sheet), the rest are below.</p>',
        '<h2>Message boxes</h2><div class="row">',
        f'<div class="col">{mb("Edit style", "This file has no style named “Sign - Bus”.", "Create it from Default and edit it?", "info", btn("Create", primary=True, focus=True) + btn("Cancel"))}'
        '<p class="cap">LE6’s Edit style on a line whose style is missing (today’s “Missing style”, Yes / No).</p></div>',
        f'<div class="col">{mb("Edit style", "Apply the changes to “Sign - Station”?", "Size and Spacing were changed.", "warning", btn("Discard", lead=True, danger=True) + btn("Apply", primary=True, focus=True) + btn("Cancel"))}'
        '<p class="cap">Picking another style, switching the file tab or Load while a change is not applied. '
        'Cancel stays on the edited style.</p></div>',
        f'<div class="col">{mb("Delete catalog", "Delete the catalog “Episode project”?", "Its 12 styles go with it. This can’t be undone.", "warning", btn("Delete", danger=True) + btn("Cancel", primary=True, focus=True))}'
        '<p class="cap">Today’s “Prompt”, Yes / No. Cancel is the default.</p></div>',
        '</div>',
        '<h2>Popovers, the ⋯ menu and the toast</h2><div class="row">',
        f'<div class="col"><div class="anchor">{combo("Default", 150)}{qb("add", "New catalog", on=True)}</div>'
        f'<div style="margin:4px 0 0 64px">{new_catalog_pop()}</div>'
        '<p class="cap" style="max-width:300px">New catalog: a popover at its button (a one-field form). Enter '
        'creates; a name in use is refused under the field.</p></div>',
        f'<div class="col"><div class="anchor">{strip(True, focus_more=True)}</div>'
        f'<div style="margin:2px 0 0 120px">{more_menu()}</div>'
        '<p class="cap" style="max-width:320px">The list’s ⋯ menu (this file’s; the catalog’s has no Delete unused '
        'styles). Move and Sort keep the selection.</p></div>',
        f'<div class="col"><div style="width:340px">{toast()}</div>'
        '<p class="cap" style="max-width:340px">Delete unused styles (the ⋯ menu, and the Subtitles menu command '
        'that does the same) reports in a toast, with the list in the Log, instead of “Status of deleted '
        'styles”. Ctrl+Z in the file undoes it.</p></div>',
        f'<div class="col"><div class="anchor">{qb("import", "Load from a file…", on=True)}</div>'
        f'<div style="margin:2px 0 0 0">{choose_from_file()}</div>'
        '<p class="cap" style="max-width:280px">Load from a file: the native file picker, then PK1’s Choose '
        'styles at the button, then the clash review, as today.</p></div>',
        '</div>',
        '<h2>Kept as they are (real screenshots of today’s build)</h2><div class="row">',
        f'<div class="col"><div class="today"><img src="shots/pop-{code}.png"></div>'
        '<p class="cap">SY2’s colour popover, opened from a swatch (right click: the eyedropper).</p></div>',
        f'<div class="col"><div class="today"><img src="shots/fonts-{code}.png"></div>'
        '<p class="cap" style="max-width:300px">SY3’s font list, each family in its own face. PU3 #305 later '
        'replaces the family field with its font picker window; SY1 does not touch it.</p></div>',
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
p.note{color:%(muted)s;font-size:12px;margin:0 0 12px;max-width:1280px;line-height:1.45}
.cap{font-size:11px;color:%(muted)s;margin:8px 0 0;max-width:520px;line-height:1.4}
.row{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap}
.col{display:flex;flex-direction:column}
.grow{flex:1}
.notes{background:%(raised)s;outline:1px solid %(line)s;padding:12px 16px;width:1280px;line-height:1.5}
.notes td{padding:3px 14px 3px 0;vertical-align:top}
.notes td:first-child{font-weight:650;white-space:nowrap}
.today{outline:1px solid %(line)s;position:relative;align-self:flex-start}
.today img{display:block}
.tlabel{position:absolute;left:0;top:-18px;font-size:11px;color:%(muted)s}

/* DG1 window */
.dlg{background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s);flex:none}
.dtb{display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtitle{flex:1;min-width:0;padding-left:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.wbs{display:flex;margin-left:auto;flex:none}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.dbody{padding:12px 24px 0;display:flex;flex-direction:column}
.dbody.flush{padding:0;flex:1;min-height:0}
.dfoot{display:flex;gap:8px;justify-content:flex-end;padding:24px;flex:none}
.dfoot.lined{padding:20px 24px;box-shadow:inset 0 1px %(line)s;height:70px}
.dfoot .lead{margin-right:auto}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-width:80px;height:30px;padding:0 10px;border-radius:1px;
   background:%(raised)s;border:1px solid %(line)s;color:%(text)s;white-space:nowrap;flex:none}
.b.sm{min-width:0;height:26px;padding:0 8px}
.b.flat{background:none;border-color:transparent;color:%(accent)s;padding:0 6px;height:24px}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.b.dng{color:%(danger)s;border-color:%(danger)s}
.b.off,.qb.off{opacity:.46}
.ring{outline:1px solid %(text)s;outline-offset:1px}
.qb{display:inline-flex;align-items:center;justify-content:center;width:24px;height:24px;border-radius:1px;color:%(text)s;flex:none;border:1px solid transparent}
.qb.on{background:%(select)s;border-color:%(accent)s}
.cb{display:flex;align-items:center;gap:8px;height:26px;white-space:nowrap}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.fld{display:inline-flex;align-items:center;gap:6px;height:28px;padding:0 8px;background:%(field)s;border:1px solid %(line)s;
     border-radius:1px;color:%(text)s;min-width:0}
.fld.wide{display:flex;width:100%%}
.fld.grow{flex:1;display:flex}
.fld.focus{border-color:%(focusc)s;box-shadow:inset 0 0 0 1px %(focusc)s}
.fld.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s}
.fld.ph .ftext{color:%(muted)s}
.fld.mixed .ftext{color:%(muted)s;font-style:italic}
.fld.off{opacity:.6}
.ftext{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.unit{color:%(muted)s;flex:none}
.fic,.ftail{color:%(muted)s}
.ftail{display:flex;margin-left:auto}
.frow{display:flex;align-items:center;gap:8px;margin:2px 0}
.flabel{width:44px;flex:none}
.warn,.warn .acc{color:%(warning)s}
.big{color:%(text)s}
.lnk2{color:%(accent)s}
.vsep{width:1px;height:16px;background:%(line)s;margin:0 4px;flex:none}

/* manager */
.mgr{display:flex;min-height:0;position:relative}
.side{width:300px;flex:none;background:%(bg)s;padding:12px;display:flex;flex-direction:column;gap:8px;box-shadow:inset -1px 0 %(line)s;min-height:0}
.shead{display:flex;align-items:center;gap:6px;margin-top:4px}
.stitle{font-weight:650;margin-right:4px}
.strip{display:flex;align-items:center;gap:2px}
.slist{flex:1;min-height:0;overflow:hidden;background:%(field)s;outline:1px solid %(line)s;padding:2px 0}
.srow{display:flex;align-items:center;gap:8px;height:24px;padding:0 10px 0 9px;box-shadow:inset 0 0 0 transparent}
.srow.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.srow.ring{outline-offset:-2px}
.sw{width:14px;height:14px;border:3px solid;border-radius:2px;flex:none;outline:1px solid %(line)s}
.sname{flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.newname{font-style:italic}
.dirty{width:7px;height:7px;border-radius:50%%;background:%(accent)s;flex:none}
.cnt{color:%(muted)s;font-size:11px;min-width:22px;text-align:right;font-variant-numeric:tabular-nums}
.cnt.zero{opacity:.6}
.xfer{display:flex;align-items:center;gap:6px}

/* editor */
.ed{flex:1;min-width:0;display:flex;flex-direction:column;min-height:0}
.pvbox{padding:12px 24px 10px;box-shadow:inset 0 -1px %(line)s;flex:none;display:flex;flex-direction:column;gap:6px}
.pv{position:relative;display:flex;align-items:center;justify-content:center;overflow:hidden;outline:1px solid %(line)s;
    background:repeating-conic-gradient(#5A5A5A 0 25%%,#6E6E6E 0 50%%) 0 0/16px 16px}
.pvtext{white-space:nowrap}
.pvmix{position:absolute;left:8px;bottom:6px;font-size:11px;color:#FFFFFF;background:rgba(0,0,0,.55);padding:1px 6px}
.fld.sample{height:26px}
.form{flex:1;min-height:0;overflow:hidden;padding:4px 24px 12px;position:relative}
.ed.compact .pvbox{padding:10px 24px 8px}
.sec{margin-top:12px}
.sect{font-weight:650;margin-bottom:6px}
.grid{display:grid;column-gap:10px;row-gap:8px;align-items:end}
.cpair{display:flex;gap:4px}
.cpair .csw{flex:1;min-width:0}
.fld.alpha{flex:none}
.cell{display:flex;flex-direction:column;gap:3px;min-width:0}
.lab{display:flex;align-items:center;height:18px;font-size:11px;color:%(muted)s;gap:4px}
.lab span:first-child{flex:1}
.rst{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;color:%(accent)s;border-radius:1px}
.under{margin-top:4px}
.under.err{color:%(danger)s;font-size:12px}
.vbar{position:absolute;right:4px;top:8px;width:6px;height:46%%;border-radius:3px;background:%(control)s;opacity:.8}
.fcbar{display:flex;align-items:center;gap:2px}
.tg{display:inline-flex;border:1px solid %(line)s;border-radius:1px;height:28px;align-self:flex-start}
.tgb{width:30px;display:flex;align-items:center;justify-content:center;background:%(raised)s;border-left:1px solid %(line)s;color:%(text)s}
.tgb:first-child{border-left:none}
.tgb.on{background:%(select)s;box-shadow:inset 0 -2px %(accent)s}
.tgb.mix{background:repeating-linear-gradient(135deg,%(select)s 0 4px,%(raised)s 4px 8px);box-shadow:none}
.agrid{display:grid;grid-template-columns:repeat(3,22px);grid-template-rows:repeat(3,18px);border:1px solid %(line)s;width:68px;background:%(field)s}
.ag{display:flex;align-items:center;justify-content:center}
.ag i{width:4px;height:4px;background:%(muted)s;border-radius:1px}
.ag.on{background:%(select)s;box-shadow:inset 0 0 0 1px %(accent)s}
.ag.on i{width:8px;height:8px;background:%(accent)s}
.agrid.mix .ag i{background:%(muted)s}
.csw{display:flex;align-items:center;height:28px;border:1px solid %(line)s;border-radius:1px;position:relative;overflow:hidden;
     background:repeating-conic-gradient(#BBBBBB 0 25%%,#FFFFFF 0 50%%) 0 0/10px 10px}
.cfill{position:absolute;inset:0}
.chex{position:relative;margin-left:auto;margin-right:2px;font:11px 'Liberation Mono',monospace;background:%(field)s;color:%(text)s;padding:1px 4px}
.csw.mixed{background:repeating-linear-gradient(135deg,%(muted)s 0 1px,%(field)s 1px 6px)}
.mixtxt{color:%(muted)s;font-style:italic;padding:0 8px;white-space:nowrap}
.crop{overflow:hidden;outline:1px solid %(line)s;background:%(panel)s;display:flex}
.crop .ed{width:100%%}

/* message box */
.mb{display:flex;gap:16px;align-items:flex-start;margin-top:8px}
.mbtext{display:flex;flex-direction:column;gap:6px;padding-top:4px;min-width:0}
.mbmain{font-size:14px;font-weight:650;line-height:1.4}
.mbdetail{color:%(muted)s;line-height:1.45}

/* popover, menu, toast */
.pop{position:relative;background:%(panel)s;outline:1px solid %(line)s;border-radius:4px;padding:12px;
     box-shadow:0 6px 22px rgba(0,0,0,%(shadow)s);color:%(text)s;z-index:2}
.pfoot{display:flex;gap:6px;justify-content:flex-end;margin-top:10px}
.anchor{display:flex;align-items:center;gap:4px;background:%(bg)s;padding:6px;outline:1px solid %(line)s;align-self:flex-start}
.menu{background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;box-shadow:0 10px 30px rgba(0,0,0,.33);width:250px}
.mi{display:flex;align-items:center;gap:6px;height:26px;padding:0 12px 0 4px}
.mi.cur{background:%(select)s}
.mchk{width:16px}
.mt{flex:1}
.mk{color:%(muted)s;font-size:12px}
.msep{height:1px;background:%(line)s;margin:4px 0}
.toast{display:flex;gap:10px;align-items:flex-start;padding:10px 8px 10px 12px;background:%(panel)s;outline:1px solid %(line)s;
       border-radius:4px;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);color:%(text)s}
.tt{flex:1;min-width:0;display:flex;flex-direction:column;gap:3px}
.ttitle{font-weight:650;line-height:1.35}
.ttext{color:%(muted)s;line-height:1.4}
.tacts{display:flex;gap:4px;margin:4px 0 0 -6px}
.tx{display:flex;align-items:center;justify-content:center;width:22px;height:22px;color:%(muted)s;flex:none}
.clist{margin-top:8px;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.crow{display:flex;align-items:center;gap:8px;height:22px;padding:0 8px}
.cfoot{display:flex;gap:12px;align-items:center;margin-top:10px}
.scount{color:%(muted)s;font-size:12px}
"""


def page(key, content):
    th = THEMES[key]
    css = CSS % dict(th, page=th["bg"], shadow=".55" if key in DARKISH else ".28",
                     focusc=th["accent"] if key not in HC else th["text"],
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')


def main():
    for key in THEMES:
        (OUT / f"sy1-{key}-1-window.html").write_text(page(key, sheet_window(key)))
        (OUT / f"sy1-{key}-2-states.html").write_text(page(key, sheet_states(key)))
        (OUT / f"sy1-{key}-3-small.html").write_text(page(key, sheet_small(key)))
        (OUT / f"sy1-{key}-4-popups.html").write_text(page(key, sheet_popups(key)))


main()
