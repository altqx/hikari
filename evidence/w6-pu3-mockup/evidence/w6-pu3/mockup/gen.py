#!/usr/bin/env python3
"""PU3 #305 Style and font popups mockup: per theme, the font picker (from the Line editor and from the Style
editor), Manage font catalogs with its small forms, Add fonts from subtitles, and the catalog questions as PK1
message boxes, the clash review and toasts.

Drawn with PK1's mockup kit (kit.py: the theme roles of src/ui/theme.cpp, DG1's frame, the popover frame, the
message box and the toast). Font names are drawn in their own faces where this machine has them, as SY3's
FontFamilyRow draws them."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu3"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, box, checkbox, field, combo, segmented, popover, window, ic, toast, message_box  # noqa

kit.REPO_ICONS += ["add", "edit", "delete", "import", "refresh", "fonts-from-subtitles", "filter", "tag-bold",
                   "tag-italic", "tag-underline", "tag-strikeout", "tag-font", "panel-menu", "folder-open"]


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=520):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def qbtn(name, title, on=False, ring=False):
    return f'<span class="qb{" qon" if on else ""}{" ring" if ring else ""}" title="{title}">{ic(name)}</span>'


def chip(text, on=False, menu=True, lead=None, ring=False):
    return (f'<span class="chip{" on" if on else ""}{" ring" if ring else ""}">'
            + (ic(lead, 14) if lead else "") + f'<span>{text}</span>'
            + (ic("chevron-down", 14) if menu else "") + '</span>')


# ---------------------------------------------------------------------------------------------
# Fonts (families this machine has, so the mockup draws them in their faces)

FONTS = [
    ("Adwaita Sans", ""), ("Arial", "Dialogue"), ("C059", ""), ("Commie Sans", "Signs"), ("iA Writer Quattro S", ""),
    ("K2D", "Signs"), ("Leelawadee", ""), ("Liberation Sans", "Dialogue"), ("Liberation Serif", ""),
    ("Nimbus Sans Narrow", "Signs, Karaoke"), ("Noto Sans", "Dialogue"), ("Noto Sans CJK JP", "Karaoke"),
    ("Noto Serif", ""), ("P052", ""), ("Standard Symbols PS", ""), ("URW Bookman", ""), ("URW Gothic", "Karaoke"),
    ("Z003", "Signs"),
]
SYMBOL = {"Standard Symbols PS"}


def family_row(name, cats="", sel=False, focus=False, show_cats=True):
    face = "inherit" if name in SYMBOL else f"'{name}'"
    sample = (f'<span class="fsample" style="font-family:\'{name}\'">αβγδ ∑∫√</span>' if name in SYMBOL else "")
    tag = f'<span class="fcats">{cats}</span>' if cats and show_cats else ""
    return (f'<div class="frow2{" sel" if sel else ""}{" ring" if focus else ""}">'
            f'<span class="fname" style="font-family:{face}">{name}</span>{sample}{tag}</div>')


def family_list(names, chosen, height=286, show_cats=True, empty=None, focus=False):
    if empty:
        inner = empty
    else:
        inner = "".join(family_row(n, c, n == chosen, focus and n == chosen, show_cats) for n, c in names)
    return f'<div class="flist" style="height:{height}px">{inner}<span class="sbar" style="top:40px;height:90px"></span></div>'


FILES = {"Arial": "arial", "K2D": "K2D-Regular", "Noto Sans": "NotoSans-Regular"}


def preview(text, family, size=30, bold=False, warn=False, note=None, sub=None):
    style = (f"font-family:'{family}';font-size:{size}px;font-weight:{700 if bold else 400}")
    if note is None:
        note = f'Drawn by the subtitle renderer with <b>{family}{" Bold" if bold else ""}</b> ({(FILES.get(family, family.replace(" ", "")) + ("bd" if family == "Arial" else "")).replace("Regular", "Bold") if bold else FILES.get(family, family.replace(" ", ""))}.ttf)'
    glyph = ic("notice-warning", 14, "warn") if warn else ""
    return (f'<div class="pv"><div class="pvbox"><span class="pvtext" style="{style}">{text}</span></div>'
            f'<div class="pvnote{" warn" if warn else ""}">{glyph}<span>{note}</span></div>'
            + (f'<div class="pvsub">{sub}</div>' if sub else "") + '</div>')


LINE = "The station closes at midnight — run!"


def styles_row(b=False, i=False, u=False, s=False):
    return ('<span class="tgl">' + qbtn("tag-bold", "Bold", b) + qbtn("tag-italic", "Italic", i)
            + qbtn("tag-underline", "Underline", u) + qbtn("tag-strikeout", "Strikethrough", s) + '</span>')


def picker(search="", chosen="Arial", catalog="All fonts", covers=False, names=None, prev=None, title="Font selection",
           foot_extra=None, size="48", bold=True, empty=None, count="18 fonts", ring_search=True):
    names = FONTS if names is None else names
    top = ('<div class="ptop">'
           + field(search or "Search fonts", 236, placeholder=not search, focus=ring_search, icon="search")
           + chip(catalog, on=catalog != "All fonts", lead="styles")
           + chip("Has “ąęłńśźż”" if covers else "Characters", on=covers, lead="filter")
           + '<span class="grow1"></span>'
           + qbtn("add", "Add “Arial” to a catalog") + '</div>')
    side = ('<div class="pside">'
            f'<div class="lbl">Size</div>{field(size, 96)}'
            f'<div class="lbl" style="margin-top:12px">Style</div>{styles_row(b=bold)}</div>')
    body = (top + f'<div class="pmid">{family_list(names, chosen, empty=empty)}{side}</div>'
            f'<div class="pcount">{count}</div>'
            + (prev or preview(LINE, chosen, bold=bold, sub="Line 12, its selected text")))
    foot = (foot_extra if foot_extra is not None else btn("Manage catalogs…", lead=True)) + btn(
        "OK", primary=True) + btn("Cancel")
    return window(title, body, foot, 616, resizable=True)


def catalog_menu():
    items = [("All fonts", "312", False, True), ("Without catalog", "283", False, False), None,
             ("Dialogue", "4", False, False), ("Karaoke", "3", False, False), ("Signs", "4", False, False), None,
             ("Manage catalogs…", "", True, False)]
    rows = "".join('<div class="msep"></div>' if it is None else
                   f'<div class="mrow{" cur" if it[3] else ""}"><span class="mchk">'
                   f'{ic("check", 14) if it[3] else ""}</span><span class="mt">{it[0]}</span>'
                   f'<span class="mk">{it[1]}</span></div>' for it in items)
    return f'<div class="menu" style="width:230px">{rows}</div>'


def add_menu(empty=False):
    if empty:
        items = [("No catalogs yet", False, True), None, ("New catalog…", False, False),
                 ("Add fonts from subtitles…", False, False)]
    else:
        items = [("Dialogue", True, False), ("Karaoke", False, False), ("Signs", False, False), None,
                 ("New catalog…", False, False), ("Add fonts from subtitles…", False, False)]
    rows = "".join('<div class="msep"></div>' if it is None else
                   f'<div class="mrow{" dis" if it[2] else ""}"><span class="mchk">{ic("check", 14) if it[1] else ""}'
                   f'</span><span class="mt">{it[0]}</span></div>' for it in items)
    return f'<div class="menu" style="width:230px">{rows}</div>'


def covers_popover():
    body = ('<div class="ptitle">Only fonts that have these characters</div>'
            + field("ąęłńśźż", 260, focus=True)
            + '<div class="pfoot">' + btn("Clear", small=True, lead=True) + btn("Apply", primary=True, small=True)
            + '</div>')
    return popover(body, 288)


def picker_page(key):
    th = THEMES[key]
    gandhi = [("Gandhi Sans", "")]
    missing = ('<div class="fempty"><div>No installed font is called “Gandhi Sans”.</div>'
               '<div class="frow2 sel use">Use “Gandhi Sans” anyway</div></div>')
    return "".join([
        f'<h1>PU3 font picker ({th["name"]})</h1>',
        '<p class="note">One window on DG1’s frame replaces FontDialog.qml. The Line editor’s Font selection button '
        'and hotkey open it, modal to the main window, as today. Each change is applied to the Line at once (legacy '
        'FONT_CHANGED) and Cancel takes every change back. Fonts, catalogs and text are examples.</p>',
        row(col(picker(),
                "Search filters the list as you type (Ctrl+F; Up and Down move in the list while the field keeps the "
                "keyboard). Each family is drawn in its own face (SY3’s rows); its catalogs are muted at the right. "
                "The catalog is a chip (All fonts, Without catalog or one catalog) and the character filter is a "
                "second chip. The preview is the Line’s text, the selected part if there is one, drawn by the "
                "subtitle renderer, with the face it picked under it. Manage catalogs… opens that window on its own; "
                "it is never inside this one.", 616),
            col(f'<div class="anchor">{picker(catalog="Signs", chosen="K2D", names=[f for f in FONTS if "Signs" in f[1]], count="4 of 312 fonts", bold=False, ring_search=False)}'
                f'<div style="position:absolute;left:280px;top:84px">{catalog_menu()}</div></div>',
                "The catalog chip open: the catalogs with their sizes, then Manage catalogs…. A chosen catalog fills "
                "the chip; the count says what is hidden.", 616),
            top=8),
        row(col(f'<div class="anchor">{picker(covers=True, chosen="Noto Sans", count="9 of 312 fonts", ring_search=False, names=[f for f in FONTS if f[0] in ("Arial","Liberation Sans","Liberation Serif","Noto Sans","Noto Serif","C059","P052","URW Bookman","Adwaita Sans")])}'
                f'<div style="position:absolute;left:378px;top:84px">{covers_popover()}</div></div>',
                "The character chip (legacy’s Filter toggle and its “Save filter” text): a popover with the "
                "characters. Apply turns the filter on and keeps the text for the next time; Clear turns it off. The "
                "chip shows the characters while on.", 616),
            col(f'<div class="anchor">{picker(chosen="Arial", ring_search=False)}'
                f'<div style="position:absolute;right:20px;top:84px">{add_menu()}</div></div>',
                "+ adds the selected font to a catalog (legacy’s Add menu): a check per catalog, New catalog…, and "
                "Add fonts from subtitles…. With no catalog yet the menu says so and offers New catalog… (legacy "
                "wrote “click Manage first” to the log).", 616),
            top=28),
        row(col(picker(search="Gandhi Sans", names=gandhi, chosen=None, empty=missing, count="0 of 312 fonts",
                       prev=preview(LINE, "Arial", bold=True, warn=True,
                                    note="Gandhi Sans is not installed: the subtitle renderer falls back to <b>Arial "
                                         "Bold</b> (arialbd.ttf)", sub="Line 12, its selected text")),
                "A name that is not installed can still be used (a file made for another machine): the list says so "
                "and offers it. The preview shows the fallback, with the warning in words, not colour alone.", 616),
            col(picker(title="Font selection", chosen="Noto Sans", size="62", bold=False, ring_search=False,
                       prev=preview("Sample text: Ąę Ćż 0123", "Noto Sans", sub="The Style editor’s sample text"),
                       foot_extra=btn("Manage catalogs…", lead=True)),
                "Opened from the Style editor (SY1’s Font family field gets a “…” button beside it; the catalog bar "
                "under it goes, its catalog and filter now chips here). Modal to the Style manager. The preview is the "
                "editor’s sample text. OK writes the family, size and B/I/U/S into the editor’s fields, which "
                "the Style manager’s OK or Apply then saves; Cancel leaves them.", 616),
            top=28),
        style_editor_strip(),
    ])


def style_editor_strip():
    fld = (f'<div class="sefont"><div class="lbl">Font family</div><div class="serow">'
           f'{field("Noto Sans", 220, trail=ic("chevron-down", 14))}'
           f'<span class="b sm ring" title="Choose a font">…</span>'
           f'<span style="margin-left:14px" class="lbl2">Size</span>{field("62", 64)}{styles_row()}</div></div>')
    return ('<h2>In the Style editor (SY1)</h2>'
            + row(col(f'<div class="sepanel">{fld}</div>',
                      "SY1’s Font section after PU3: the family field stays (type a name, or pick from SY3’s list) "
                      "with “…” to open the picker; the Y6 catalog bar row under it is removed. The change to "
                      "StyleManager.qml waits for SY1’s PR #338 to merge.", 616), top=0))


# ---------------------------------------------------------------------------------------------
# Manage font catalogs

CATS = [("Dialogue", 4), ("Karaoke", 3), ("Signs", 4)]


def cat_sidebar(chosen="Signs", ring=None):
    rows = "".join(f'<div class="crow{" sel" if n == chosen else ""}"><span>{n}</span><span class="ccount">{c}</span>'
                   f'</div>' for n, c in CATS)
    strip = ('<div class="strip">'
             + qbtn("add", "New catalog", ring=ring == "new") + qbtn("edit", "Rename", ring=ring == "rename")
             + qbtn("delete", "Delete", ring=ring == "delete") + '<span class="sep"></span>'
             + qbtn("import", "Load from a file") + qbtn("fonts-from-subtitles", "Add fonts from subtitles")
             + '</div>')
    return f'<div class="cside"><div class="lbl">Catalogs</div>{strip}<div class="clist">{rows}</div></div>'


def member_row(name, on, cats, sel=False):
    face = "inherit" if name in SYMBOL else f"'{name}'"
    sample = "αβγδ ∑∫√" if name in SYMBOL else "The quick brown fox"
    return (f'<div class="mrow2{" sel" if sel else ""}">{box(on)}<span class="mname">{name}</span>'
            f'<span class="mcats">{cats or "—"}</span>'
            f'<span class="msample" style="font-family:{face if name not in SYMBOL else chr(39) + name + chr(39)}">'
            f'{sample}</span></div>')


def catalog_window(chosen="Signs", show="All fonts", sel="K2D", ring=None, width=880):
    rows = "".join(member_row(n, "Signs" in c, c, n == sel) for n, c in FONTS[:13])
    head = ('<div class="mhead"><span style="width:16px"></span><span class="mname">Font</span>'
            '<span class="mcats">Catalogs</span><span class="msample">Sample</span></div>')
    top = ('<div class="ptop">' + field("Search fonts", 220, placeholder=True, icon="search")
           + f'<span class="lbl2">Show</span>{segmented(["All fonts", "In “Signs”", "Not in a catalog"], show, small=True)}'
           + '<span class="grow1"></span>' + qbtn("refresh", "Refresh fonts") + '</div>')
    main = (f'<div class="cmain">{top}<div class="mtable">{head}{rows}'
            '<span class="sbar" style="top:60px;height:70px"></span></div>'
            + preview("The quick brown fox", "K2D", size=28,
                      note="Drawn by the subtitle renderer with <b>K2D</b> (K2D-Regular.ttf)") + '</div>')
    body = f'<div class="cwin">{cat_sidebar(chosen, ring)}{main}</div>'
    foot = (f'<span class="lead saved" title="Catalogs are saved automatically to FontCatalogs.txt in the Config '
            f'folder">{ic("check", 14)}Saved</span>' + btn("Close", primary=True))
    return window("Manage font catalogs", body, foot, width, resizable=True)


def name_popover(title, value, verb, error=None, ph=False):
    err = f'<div class="ferr">{ic("notice-warning", 14, "err")}<span>{error}</span></div>' if error else ""
    fld = field(value, 240, placeholder=ph, focus=True).replace('class="fld', 'class="fld' + (" bad" if error else ""), 1)
    body = (f'<div class="ptitle">{title}</div>{fld}{err}<div class="pfoot">'
            + btn(verb, primary=not error, small=True).replace('class="b', 'class="b' + (" off" if error else ""), 1)
            + btn("Cancel", small=True) + '</div>')
    return popover(body, 268)


def from_subtitles(all_files=False, replace=False):
    body = ('<div class="form">'
            f'<span class="lbl3">Catalog</span>{field("Signs", 260, trail=ic("chevron-down", 14), focus=True)}'
            f'<span></span><span class="hint">Choose a catalog or type a new name.</span>'
            f'<span class="lbl3">From</span><span>{segmented(["This file", "All open files"], "All open files" if all_files else "This file", small=True)}</span>'
            f'<span></span>{checkbox("Replace the catalog’s fonts", replace)}'
            f'<span></span><span class="hint">The fonts of every style and every \\fn tag in Episode 03.ass.</span>'
            '</div>')
    return window("Add fonts from subtitles", body, btn("Add fonts", primary=True) + btn("Cancel"), 420)


def catalogs_page(key):
    th = THEMES[key]
    return "".join([
        f'<h1>PU3 Manage font catalogs and Add fonts from subtitles ({th["name"]})</h1>',
        '<p class="note">Manage font catalogs is one modeless, resizable window of its own (only one; opening it again '
        'raises it), opened from the picker’s Manage catalogs… and the catalog chip, never inside the picker or the '
        'Style manager. Open pickers see its changes at once.</p>',
        row(col(catalog_window(),
                "The catalogs on the left with their sizes, one icon strip (New, Rename, Delete; Load from a file, "
                "Add fonts from subtitles). On the right every installed font: the check says whether it is in the "
                "chosen catalog (Space toggles every selected row), with its catalogs and a sample. This replaces "
                "legacy’s mark column and per-row catalog menu. Show narrows the list; Refresh fonts reads the fonts "
                "again. The preview is the renderer’s. Edits save themselves (legacy’s autosave): the footer says "
                "Saved, the file and folder in its tooltip, instead of a sentence.", 880),
            top=8),
        row(col(f'<div class="anchor" style="height:200px;width:300px"><div style="position:absolute;left:0;top:0">'
                f'{cat_sidebar(ring="new")}</div><div style="position:absolute;left:16px;top:66px">'
                f'{name_popover("New catalog", "Catalog name", "Create", ph=True)}</div></div>',
                "New: a popover at the button (PK1’s small-form rule).", 300),
            col(f'<div class="anchor" style="height:200px;width:300px"><div style="position:absolute;left:0;top:0">'
                f'{cat_sidebar(ring="rename")}</div><div style="position:absolute;left:16px;top:66px">'
                f'{name_popover("Rename “Signs”", "", "Rename", error="Enter a name for the catalog.")}</div></div>',
                "Rename with the name cleared: the error under the field and Rename off, instead of legacy’s “Info” "
                "box that then reopened the dialog.", 300),
            col(f'<div class="anchor" style="height:200px;width:300px"><div style="position:absolute;left:0;top:0">'
                f'{cat_sidebar(ring="new")}</div><div style="position:absolute;left:16px;top:66px">'
                f'{name_popover("New catalog", "signs", "Create", error="“Signs” already exists.")}</div></div>',
                "New with a name that exists (any case): said at once (legacy added nothing and said nothing).", 300),
            top=28),
        row(col(from_subtitles(),
                "Add fonts from subtitles: a compact DG1 window (it opens from a menu). The catalog is chosen or "
                "typed (a new name makes it). “From” replaces “Add fonts from all open subtitles”; “Replace the "
                "catalog’s fonts” replaces “Remove all contents of catalog”. The verb button replaces OK. The result "
                "is a toast and the picker shows that catalog.", 420),
            col(from_subtitles(all_files=True, replace=True).replace(
                "The fonts of every style and every \\fn tag in Episode 03.ass.",
                "The fonts of every style and every \\fn tag in the 3 open files. Signs’ 4 fonts are removed first."),
                "All open files, replacing: the hint says what will happen.", 420),
            top=28),
    ])


# ---------------------------------------------------------------------------------------------
# Questions and notices

LOADS = [("Signs", "4 here, 6 in the file (3 new)", "Merge", ""),
         ("Karaoke", "3 here, 3 in the file (1 new)", "Rename", "Karaoke (2)"),
         ("Dialogue", "4 here, 5 in the file (1 new)", "Skip", "")]


def load_review():
    rows = "".join(
        f'<div class="crow3"><span class="cname">{n}</span><span class="cdiff">{d}</span>'
        f'<span class="cact">{segmented(["Merge", "Replace", "Rename", "Skip"], a, small=True)}'
        + (field(r, 140, focus=True) if r else "") + '</span></div>' for n, d, a, r in LOADS)
    body = ('<div class="mbmain" style="margin-top:6px">3 of 5 catalogs in “catalogs-2025.txt” already exist.</div>'
            '<div class="mbdetail">The other 2 are added.</div>'
            f'<div class="allrow"><span>All:</span>{segmented(["Merge", "Replace", "Rename", "Skip"], None, small=True)}</div>'
            f'<div class="ctable"><div class="chead"><span class="cname">Catalog</span><span class="cdiff">'
            f'Fonts</span><span class="cact">Action</span></div>{rows}</div>')
    return window("Load font catalogs", body, btn("Load 4 catalogs", primary=True, focus=True) + btn("Cancel"),
                  780, resizable=True)


def questions_page(key):
    th = THEMES[key]
    delete = message_box("Delete font catalog", "Delete the catalog “Signs”?",
                         "Its 4 fonts stay installed and in their other catalogs.", "warning",
                         btn("Delete", danger=True) + btn("Cancel", primary=True, focus=True))
    clash = message_box("Rename font catalog", "A catalog named “Signs” already exists.",
                        "Merge adds the fonts of “Typesetting” to it. Replace deletes the old “Signs” first.",
                        "warning", btn("Replace", danger=True) + btn("Merge", primary=True, focus=True) + btn("Cancel"))
    stack = ('<div class="stack2">'
             + toast("info", "Added 7 fonts to “Signs”", "From the styles and \\fn tags of Episode 03.ass.")
             + toast("info", "Loaded 4 font catalogs", "From catalogs-2025.txt: 2 new, 1 merged, 1 renamed.")
             + toast("warning", "Cannot read “catalogs-2025.txt”", "The file is not a font catalog file.",
                     btn("Details", small=True, flat=True))
             + '</div>')
    return "".join([
        f'<h1>PU3 catalog questions and notices ({th["name"]})</h1>',
        '<p class="note">The catalog prompts (legacy “Prompt” and “Info”, Yes / No) become PK1 message boxes with '
        'verb buttons; loading a file whose catalogs clash uses the clash review. Platform order as on Windows '
        'and KDE; the default is filled, a destructive verb outlined in the danger colour and never the '
        'default.</p>',
        row(col(delete, "Delete in Manage font catalogs (legacy: “Are you sure you want to delete this catalog?”, "
                        "Yes / No). Cancel is the default.", 468),
            col(clash, "Rename to a name that exists (legacy “Prompt”: Merge / Delete / Cancel). Merge loses "
                       "nothing, so it is the default; Replace (legacy Delete) is outlined.", 468), top=8),
        row(col(load_review(),
                "Load from a file (legacy merged every clashing catalog silently): PK1’s clash review, one row per "
                "catalog that exists, with Merge (legacy’s, the default), Replace, Rename (a free name, editable) "
                "or Skip. Catalogs that would not change are left out and counted.", 780),
            col(stack, "Toasts for what needs no answer: what Add fonts from subtitles and Load did, and a file that "
                       "cannot be read (legacy said nothing).", 360), top=28),
    ])


EXTRA_CSS = """
.grow1{flex:1}
.anchor{position:relative}
.lbl{font-size:11px;color:%(muted)s;margin-bottom:4px}
.lbl2{color:%(muted)s;font-size:12px;margin:0 2px 0 6px}
.lbl3{color:%(text)s;align-self:center}
.hint{color:%(muted)s;font-size:12px;line-height:1.4}
.ptop{display:flex;align-items:center;gap:6px;margin-top:6px}
.chip{display:inline-flex;align-items:center;gap:5px;height:26px;padding:0 6px 0 8px;border:1px solid %(line)s;
      border-radius:13px;background:%(raised)s;color:%(text)s;white-space:nowrap}
.chip.on{background:%(select)s;border-color:%(accent)s;font-weight:650}
.chip .ic{color:%(muted)s}
.chip.on .ic{color:%(text)s}
.pmid{display:flex;gap:16px;margin-top:8px}
.pside{width:150px;flex:none}
.tgl{display:inline-flex;gap:2px}
.qb{border:1px solid transparent}
.qb.qon{background:%(select)s;border-color:%(accent)s}
.flist{position:relative;flex:1;overflow:hidden;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.frow2{display:flex;align-items:center;gap:12px;height:28px;padding:0 10px}
.frow2.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.frow2.use{margin-top:8px;color:%(text)s}
.fname{font-size:15px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fsample{font-size:15px;color:%(muted)s}
.fcats{margin-left:auto;color:%(muted)s;font-size:11px;white-space:nowrap}
.fempty{padding:12px 10px 0;color:%(muted)s}
.fempty .frow2{margin-left:-10px;margin-right:-10px}
.pcount{color:%(muted)s;font-size:11px;margin-top:4px}
.pv{margin-top:10px}
.pvbox{height:96px;display:flex;align-items:center;justify-content:center;outline:1px solid %(line)s;overflow:hidden;
       background-color:#5d6670;background-image:linear-gradient(45deg,#535b64 25%%,transparent 25%%,transparent 75%%,#535b64 75%%),
       linear-gradient(45deg,#535b64 25%%,transparent 25%%,transparent 75%%,#535b64 75%%);background-size:16px 16px;
       background-position:0 0,8px 8px}
.pvtext{color:#fff;-webkit-text-stroke:0;text-shadow:0 0 2px #000,0 0 2px #000,1px 1px 0 #000,-1px -1px 0 #000,1px -1px 0 #000,-1px 1px 0 #000,2px 2px 1px rgba(0,0,0,.6);white-space:nowrap}
.pvnote{display:flex;align-items:center;gap:6px;margin-top:6px;color:%(muted)s;font-size:12px}
.pvnote b{color:%(text)s;font-weight:650}
.pvnote.warn,.pvnote.warn b{color:%(warning)s}
.pvsub{color:%(muted)s;font-size:11px;margin-top:2px}
.menu{background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;box-shadow:0 10px 30px rgba(0,0,0,.33);z-index:3;position:relative}
.mrow{display:flex;align-items:center;height:26px;padding:0 12px 0 6px;gap:6px}
.mrow.cur{background:%(select)s}
.mrow.dis{color:%(disabled)s}
.mchk{width:16px;display:flex;justify-content:center}
.mt{flex:1}
.mk{color:%(muted)s;font-size:12px}
.msep{height:1px;background:%(line)s;margin:4px 0}
.sepanel{background:%(panel)s;outline:1px solid %(line)s;padding:14px 18px;width:616px}
.serow{display:flex;align-items:center;gap:6px}
.serow .b{min-width:30px;padding:0 8px}
.cwin{display:flex;gap:16px;height:520px}
.cside{width:200px;flex:none;display:flex;flex-direction:column}
.cside .strip{margin:0 0 6px}
.clist{flex:1;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.crow{display:flex;align-items:center;height:28px;padding:0 10px}
.crow.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s;font-weight:650}
.ccount{margin-left:auto;color:%(muted)s;font-size:12px;font-weight:400}
.strip{display:flex;align-items:center;gap:2px}
.sep{width:1px;height:18px;background:%(line)s;margin:0 4px}
.cmain{flex:1;display:flex;flex-direction:column;min-width:0}
.cmain .ptop{margin-top:0}
.mtable{position:relative;flex:1;outline:1px solid %(line)s;background:%(field)s;margin-top:8px;overflow:hidden}
.mhead,.mrow2{display:flex;align-items:center;gap:10px;height:28px;padding:0 10px}
.mhead{height:26px;background:%(raised)s;color:%(muted)s;font-size:12px;box-shadow:inset 0 -1px %(line)s}
.mrow2.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.mname{width:170px;flex:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.mcats{width:130px;flex:none;color:%(muted)s;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.msample{flex:1;font-size:15px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.saved{display:flex;align-items:center;gap:6px;color:%(muted)s}
.fld.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s}
.ferr{display:flex;align-items:center;gap:6px;margin-top:6px;color:%(danger)s;font-size:12px}
.b.off{opacity:.46}
.form{display:grid;grid-template-columns:64px 1fr;column-gap:10px;row-gap:8px;margin-top:8px;align-items:center}
.stack2{width:360px;display:flex;flex-direction:column;gap:8px}
.allrow{display:flex;align-items:center;gap:10px;margin-top:14px}
.ctable{margin-top:10px;outline:1px solid %(line)s;background:%(field)s}
.chead,.crow3{display:flex;align-items:center;gap:10px;padding:0 10px;min-height:38px}
.chead{min-height:28px;color:%(muted)s;font-size:12px;box-shadow:inset 0 -1px %(line)s;background:%(raised)s}
.crow3+.crow3{box-shadow:inset 0 1px %(line)s}
.crow3 .cname{font-weight:650}
.cname{width:90px;flex:none}
.cdiff{width:200px;flex:none;color:%(muted)s}
.cact{flex:1;display:flex;gap:8px;align-items:center}
"""


def page(key, content):
    th = THEMES[key]
    html = kit.page(key, content)
    extra = EXTRA_CSS % dict(th)
    return html.replace("</style>", extra + "</style>", 1)


def main():
    for key in THEMES:
        (HERE / f"pu3-{key}-1-picker.html").write_text(page(key, picker_page(key)))
        (HERE / f"pu3-{key}-2-catalogs.html").write_text(page(key, catalogs_page(key)))
        (HERE / f"pu3-{key}-3-questions.html").write_text(page(key, questions_page(key)))


main()
