#!/usr/bin/env python3
"""GR1 #364 Grammar check with Harper: mockup, one HTML sheet per theme.

Built on PF1's Settings mockup (evidence/w6-pf1-mockup gen.py: DG1's frame, PK1's controls and menu, the theme
roles of src/ui/theme.cpp) with the Line editor, the Spellchecker panel (PU6) and NT1's toast drawn from today's
QML. The content colours (spelling, syntax) are theme.cpp content(); `grammar` is the new colour proposed here."""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-gr1")
OUT = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else pathlib.Path(__file__).resolve().parent / "out")
OUT.mkdir(parents=True, exist_ok=True)

THEMES = {
    "light": dict(code="light", name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", warning="#8A5A00", danger="#A51F31"),
    "dark": dict(code="dark", name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", warning="#E0A030", danger="#FFADAD"),
    "hcwhite": dict(code="highContrastWhite", name="High contrast white", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    warning="#6B4500", danger="#A00000"),
    "hcblack": dict(code="highContrastBlack", name="High contrast black", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    warning="#FFD54A", danger="#FFADAD"),
}
HC = {"hcwhite", "hcblack"}
DARKISH = {"dark", "hcblack"}

# The three new icons, K1's rules: 16 grid, 1-wide stroke, round caps, one accent layer.
NEW = {
    # The Subtitle grid page: a table, its time column and an active row.
    "settings-grid": '<rect x="1.5" y="2.5" width="13" height="11" rx="1"/><path d="M1.5 5.5h13M5.5 2.5v11"/>'
                     '<g id="accent"><path d="M7.5 8.25h5M7.5 10.75h3"/></g>',
    # Notices: a toast with its text, another behind it.
    "notices": '<rect x="1.5" y="5.5" width="13" height="8" rx="1"/><path d="M4 8.5h6M4 10.75h4"/>'
               '<g id="accent"><path d="M3.5 3.5h9"/></g>',
}
GLYPHS = {
    "chevron-down": '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/>',
    "chevron-up": '<path d="M4.5 9.5l3.5-3.5 3.5 3.5"/>',
}
REPO = ["settings", "appearance", "editor", "settings-video", "settings-audio", "convert", "script-properties",
        "automation", "hotkeys", "language", "ui-font", "renderer", "video-seek", "audio-device", "audio-delay",
        "theme", "accent-colour", "ui-colours", "restore-default", "dictionary", "autosave", "fps", "styles",
        "resample", "zoom", "lead-in", "folder-open", "search", "filter", "karaoke", "audio-waveform",
        "window-close", "window-maximize", "notice-warning", "add", "edit", "move-up", "move-down", "remove",
        "tag-bold", "tag-italic", "tag-font", "media-play", "about",
        "spellchecker", "dictionary-add", "edit-copy", "edit-cut", "edit-paste", "delete", "tab-close", "check-updates"]


def icon_body(name):
    if name in NEW:
        return NEW[name].replace('<g id="accent">', '<g class="acc">')
    svg = (TREE / "src/ui/icons" / f"{name}.svg").read_text()
    return re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip()).replace('<g id="accent">', '<g class="acc">')


def sprite():
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    syms = {**GLYPHS, **{n: icon_body(n) for n in REPO + list(NEW)}}
    body = "".join(f'<symbol id="i-{n}" viewBox="0 0 16 16"><g {attrs}>{s}</g></symbol>' for n, s in syms.items())
    return f'<svg width="0" height="0" style="position:absolute"><defs>{body}</defs></svg>'


def ic(name, size=16, cls=""):
    return f'<svg class="ic {cls}" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


# ---------------------------------------------------------------------------------------------
# Controls (PK1's)


def btn(text, primary=False, focus=False, lead=False, danger=False, small=False, icon=None, menu=False, on=False):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" lead" if lead else "") + (
        " dng" if danger else "") + (" sm" if small else "") + (" on" if on else "")
    i = ic(icon) if icon else ""
    m = f'<span class="bsep"></span>{ic("chevron-down", 14)}' if menu else ""
    return f'<span class="{cls}">{i}<span>{text}</span>{m}</span>'


def box(on):
    mark = ('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
            'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
    return f'<span class="box{" on" if on else ""}">{mark if on else ""}</span>'


def field(text, width=None, placeholder=False, focus=False, icon=None, trail=None, mono=False, dis=False):
    w = f"width:{width}px;" if width else ""
    cls = "fld" + (" ph" if placeholder else "") + (" focus" if focus else "") + (" mono" if mono else "") + (
        " dis" if dis else "")
    lead = ic(icon, 16, "fic") if icon else ""
    tail = f'<span class="ftail">{trail}</span>' if trail else ""
    return f'<span class="{cls}" style="{w}">{lead}<span class="ftext">{text}</span>{tail}</span>'


def combo(text, width=220, dis=False):
    return field(text, width, trail=ic("chevron-down", 14), dis=dis)


def spin(value, unit="", width=96):
    u = f'<span class="unit">{unit}</span>' if unit else ""
    return (f'<span class="fld spin" style="width:{width}px"><span class="ftext num">{value}</span>{u}'
            f'<span class="arrows">{ic("chevron-up", 10)}{ic("chevron-down", 10)}</span></span>')


def segmented(options, chosen):
    return ('<span class="seg">' + "".join(
        f'<span class="{"on" if o == chosen else ""}">{o}</span>' for o in options) + '</span>')


# ---------------------------------------------------------------------------------------------
# The Settings window's form: sections of rows; every row's leading 16-wide slot holds the
# option's icon (primary options), the check box (checks) or nothing, so all texts share one edge.


def section(title, *rows, first=False):
    return (f'<div class="sec{" first" if first else ""}"><div class="stitle">{title}</div>'
            f'<div class="srows">{"".join(rows)}</div></div>')


def row(label, control, icon=None, note=None, wide=False):
    slot = ic(icon) if icon else ""
    n = f'<div class="rnote">{note}</div>' if note else ""
    return (f'<div class="r{" wide" if wide else ""}"><span class="slot">{slot}</span>'
            f'<span class="rl">{label}</span><span class="rc">{control}</span></div>{n}')


def check(label, on=False, note=None, focus=False):
    n = f'<div class="rnote">{note}</div>' if note else ""
    return (f'<div class="r chk{" ring" if focus else ""}"><span class="slot">{box(on)}</span>'
            f'<span class="rl wrap">{label}</span></div>{n}')


def block(html):
    return f'<div class="rblock">{html}</div>'




PAGES = [("general", "General", "settings"), ("appearance", "Appearance", "appearance"),
         ("editor", "Line editor", "editor"), ("spelling", "Spelling & grammar", "spellchecker"),
         ("grid", "Subtitle grid", "settings-grid"), ("video", "Video", "settings-video"),
         ("audio", "Audio", "settings-audio"), ("conversion", "Conversion", "convert"),
         ("properties", "Subtitle properties", "script-properties"), ("automation", "Automation", "automation"),
         ("hotkeys", "Hotkeys", "hotkeys"), ("updates", "Updates", "check-updates")]

def page_list(current):
    rows = "".join(
        f'<div class="pg{" cur" if k == current else ""}">{ic(i)}<span>{n}</span></div>' for k, n, i in PAGES)
    return f'<div class="pages">{rows}</div>'


def titlebar(title):
    return (f'<div class="dtb"><span class="dtitle">{title}</span><span class="wbs">'
            f'<span class="wb">{ic("window-maximize")}</span><span class="wb">{ic("window-close")}</span></span></div>')


def footer(menu_open=False):
    menu = ""
    if menu_open:
        menu = ('<div class="menu" style="left:24px;bottom:60px">'
                f'<div class="mi">{ic("restore-default")}<span>Reset this page</span></div>'
                f'<div class="mi hi"><span class="noic"></span><span>Reset all settings…</span></div></div>')
    return (f'<div class="dfoot">{menu}{btn("Reset page", lead=True, icon="restore-default", menu=True, on=menu_open)}'
            f'{btn("OK", primary=True)}{btn("Cancel")}{btn("Apply")}</div>')


def settings_window(current, body, menu_open=False, scroll=None, height=640):
    bar = ""
    if scroll:
        top, h = scroll
        bar = f'<span class="sbar" style="top:{top}px;height:{h}px"></span>'
    return (f'<div class="dlg settings" style="width:880px;height:{height}px">{titlebar("Settings")}'
            f'<div class="split">{page_list(current)}<div class="pbody">{bar}<div class="form">{body}</div></div></div>'
            f'{footer(menu_open)}</div>')


# ---------------------------------------------------------------------------------------------
# Pages



CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(bg)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:1860px}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
h1{font-size:18px;font-weight:600;margin:0 0 6px}
h2{font-size:13px;font-weight:650;margin:26px 0 10px}
p.note{color:%(muted)s;font-size:12px;margin:0 0 6px;max-width:1780px;line-height:1.45}
.cap{font-size:12px;color:%(muted)s;margin:8px 0 0;max-width:880px;line-height:1.45}
.row2{display:flex;gap:40px;align-items:flex-start}
.col{display:flex;flex-direction:column}

/* DG1 window */
.dlg{position:relative;background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s)}
.dtb{display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtitle{flex:1;min-width:0;padding-left:12px;white-space:nowrap}
.wbs{display:flex;margin-left:auto;flex:none}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.dbody{padding:12px 24px 0;display:flex;flex-direction:column}
.dfoot{position:relative;display:flex;gap:8px;justify-content:flex-end;padding:16px 24px;flex:none}
.settings .dfoot{box-shadow:inset 0 1px %(line)s}
.dfoot .lead{margin-right:auto}

/* the split: page list on the background, the page on the panel */
.split{flex:1;display:flex;min-height:0;box-shadow:inset 0 1px %(line)s}
.pages{width:220px;flex:none;background:%(bg)s;padding:8px 0;box-shadow:inset -1px 0 %(line)s}
.pg{display:flex;align-items:center;gap:12px;height:36px;padding:0 16px;color:%(text)s}
.pg.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s;font-weight:650}
.pbody{position:relative;flex:1;min-width:0;overflow:hidden}
.form{padding:20px 28px 24px 24px}
.sbar{position:absolute;right:3px;width:4px;border-radius:2px;background:%(control)s;opacity:.8}

/* sections and rows */
.sec{padding-top:16px;margin-top:16px;box-shadow:inset 0 1px %(line)s}
.sec.first{padding-top:0;margin-top:0;box-shadow:none}
.stitle{font-weight:650;margin-bottom:10px}
.srows{display:flex;flex-direction:column;gap:8px}
.r{display:flex;align-items:center;gap:8px;min-height:28px}
.r.chk{min-height:22px}
.slot{width:16px;flex:none;display:flex;align-items:center;justify-content:center}
.rl{width:220px;flex:none;line-height:1.35}
.rl.wrap{width:auto;flex:1}
.rc{display:flex;align-items:center;gap:8px;flex:1;min-width:0}
.rnote{margin:-5px 0 0 24px;color:%(muted)s;font-size:12px;line-height:1.4}
.rblock{margin-left:24px}
.ring{outline:1px solid %(focus)s;outline-offset:2px}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-width:80px;height:30px;padding:0 10px;border-radius:1px;
   background:%(raised)s;border:1px solid %(control)s;color:%(text)s;white-space:nowrap;flex:none}
.b.sm{min-width:0;height:28px}
.b.on{background:%(select)s;border-color:%(accent)s}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.b.dng{color:%(danger)s;border-color:%(danger)s;background:%(panel)s}
.b.ring{outline:1px solid %(focus)s;outline-offset:1px}
.bsep{width:1px;align-self:stretch;margin:5px 0 5px 4px;background:%(line)s}
.qb{display:inline-flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:1px;color:%(text)s}
.qbs{display:flex;gap:2px;margin-top:4px}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.fld{display:inline-flex;align-items:center;gap:6px;height:28px;padding:0 8px;background:%(field)s;border:1px solid %(line)s;
     border-radius:1px;color:%(text)s;min-width:0}
.fld.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.fld.ph .ftext{color:%(muted)s}
.ftext{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.ftext.num{text-align:right;font-variant-numeric:tabular-nums}
.fic,.ftail{color:%(muted)s}
.ftail{display:flex;margin-left:auto}
.spin{padding-right:2px}
.unit{color:%(muted)s}
.arrows{display:flex;flex-direction:column;color:%(muted)s;margin-left:2px}
.x{color:%(muted)s}
.seg{display:inline-flex;border:1px solid %(line)s;border-radius:1px;overflow:hidden;flex:none}
.seg span{padding:0 10px;height:26px;display:flex;align-items:center;background:%(raised)s;border-left:1px solid %(line)s}
.seg span:first-child{border-left:none}
.seg span.on{background:%(select)s;box-shadow:inset 0 -2px %(accent)s;font-weight:650}
.big{color:%(text)s}
.warn,.warn .acc{color:%(warning)s}

/* appearance */
.tcards{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;max-width:560px}
.tcard{height:64px;border:1px solid;border-radius:1px;padding:6px;display:flex}
.tcard.on{border-width:2px;padding:5px}
.tpan{position:relative;flex:1;border:1px solid;border-radius:1px;padding:4px 6px;display:flex;flex-direction:column;gap:3px}
.tpan b{font-size:11px;white-space:nowrap;overflow:hidden}
.tpan i{display:block;width:48px;height:3px;border-radius:1px}
.tacc{position:absolute;right:5px;bottom:5px;width:24px;height:12px;border-radius:1px}
.sws{display:flex;gap:8px}
.sw{width:22px;height:22px;border-radius:50%%;outline:1px solid %(line)s;flex:none}
.sw.on{outline:2px solid %(text)s;outline-offset:2px}
.sw.big{border-radius:1px;width:28px}

/* lists */
.llist{outline:1px solid %(line)s;background:%(field)s;padding:2px 0;max-width:520px}
.lrow{display:flex;gap:8px;align-items:center;height:28px;padding:0 8px}
.lrow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.lic{width:16px}
.ln{width:120px}
.lt{color:%(muted)s;font-family:'Liberation Mono',monospace;font-size:12px}

/* subtitle properties */
.ptable{display:grid;grid-template-columns:120px 300px 40px;gap:8px 10px;align-items:center}
.pth{color:%(muted)s;font-size:12px;text-align:center}
.pl{color:%(text)s}
.pc{display:flex;justify-content:center}

/* hotkeys */
.htools{display:flex;gap:8px;margin-bottom:10px}
.htable{outline:1px solid %(line)s;background:%(field)s}
.hhead,.hrow{display:grid;grid-template-columns:1fr 180px;padding:0 10px;height:26px;align-items:center}
.hhead{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.hgrp{height:26px;display:flex;align-items:center;padding:0 10px;font-weight:650;color:%(muted)s;font-size:12px}
.hrow{padding-left:22px}
.hrow.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.hrow.mod span{color:%(warning)s}
.keys{color:%(muted)s}
.hacts{display:flex;gap:8px;margin-top:10px}

/* menu (PK1: radius 5, the shadow) */
.menu{position:absolute;z-index:3;min-width:220px;background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;
      box-shadow:0 10px 30px rgba(0,0,0,.33)}
.mi{display:flex;align-items:center;gap:10px;height:28px;padding:0 14px 0 10px}
.mi.hi{background:%(select)s}
.noic{width:16px}

/* message box */
.mb{display:flex;gap:16px;align-items:flex-start;margin-top:8px}
.mbtext{display:flex;flex-direction:column;gap:6px;padding-top:4px;min-width:0}
.mbmain{font-size:14px;font-weight:650;line-height:1.4}
.mbdetail{color:%(muted)s;line-height:1.45}
.mbox .dfoot{padding:24px}

/* icons */
.igrp{margin-bottom:14px}
.ih{color:%(muted)s;font-size:12px;margin-bottom:6px}
.icells{display:flex;flex-wrap:wrap;gap:8px}
.icell{display:grid;grid-template-columns:16px 32px 1fr;grid-template-rows:32px auto auto;column-gap:10px;align-items:center;
       width:168px;padding:10px;background:%(panel)s;outline:1px solid %(line)s}
.icell.new{outline:2px solid %(accent)s}
.iname{grid-column:1 / -1;font-family:'Liberation Mono',monospace;font-size:12px;margin-top:6px}
.ilabel{grid-column:1 / -1;color:%(muted)s;font-size:12px}
"""



# ---------------------------------------------------------------------------------------------
# GR1: the theme's content colours (src/ui/theme.cpp content()): spelling's EDITOR_SPELLCHECKER
# background, the syntax roles for tags, and the proposed `grammar` underline colour.

CONTENT = {
    "light": dict(spell="#FF6968", brace="#7E3B00", tag="#787600", grammar="#1A56DB"),
    "dark": dict(spell="#940000", brace="#8791FD", tag="#00C3FF", grammar="#6EA8FE"),
    "hcwhite": dict(spell="#FF9C9C", brace="#7A3E00", tag="#005A9E", grammar="#0000C8"),
    "hcblack": dict(spell="#C00000", brace="#FFB060", tag="#60D0FF", grammar="#FFFF00"),
}

GROUPS = [  # harper-core/default_config.json at v3.0.0: label, rules, description
    ("Proper nouns", 26, "Names of places, organizations and products keep their standard capitalization."),
    ("Initialisms", 27, "Abbreviations and initialisms use the expected letter case and punctuation."),
    ("Closed compounds", 70, "Compound words written as one word instead of split apart."),
    ("Phrase corrections", 92, "Common phrase-level mistakes where a standard expression is preferred."),
    ("Capitalization and case", 11, "Sentence starts, titles, brands and other case-sensitive text."),
    ("Punctuation and spacing", 39, "Punctuation marks, apostrophes, quotation marks, commas and spacing."),
    ("Numbers and units", 28, "Numbers, dates, times, decades and units of measurement."),
    ("Pronouns and possession", 38, "Pronouns, contractions, possessives and their agreement."),
    ("Compounds and hyphenation", 18, "Multi-word compounds that should be open, closed or hyphenated."),
    ("Spelling and orthography", 72, "Misspellings and word forms spelled the wrong way."),
    ("Style and redundancy", 59, "Wordy, repeated or weak phrasing that can be tightened."),
    ("Grammar and agreement", 114, "Agreement, articles, prepositions, verb forms and sentence structure."),
    ("Word choice and usage", 286, "More standard or idiomatic wording for understandable phrases."),
    ("Regionalisms and dialect", 3, "Wording that may not fit the chosen English."),
    ("Miscellaneous", 48, "Rules that fit none of the groups above."),
]


def t(text):  # plain text
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def tag(raw):  # an override block in the syntax colours
    return f'<span class="br">{{</span><span class="tg">{t(raw)}</span><span class="br">}}</span>'


def g(text):  # a grammar finding
    return f'<span class="gm">{t(text)}</span>'


def s(text):  # a spelling mark (Hunspell)
    return f'<span class="spm">{t(text)}</span>'


LINE4_TAGS = (tag(r"\i1") + g("Their") + tag(r"\i0") + " going to the store " + s("tomorow") + ", I "
              + g("has") + " " + g("a") + " apple.")
LINE4_HIDDEN = g("Their") + " going to the store " + s("tomorow") + ", I " + g("has") + " " + g("a") + " apple."


def editor_field(html, focus=False, caret=None, label=None, muted=False, width=560, cls=""):
    lab = f'<span class="flab">{label}</span>' if label else ""
    c = '<span class="caret"></span>' if caret else ""
    return (f'<div class="lefield{" focus" if focus else ""}{" mut" if muted else ""} {cls}" style="width:{width}px">'
            f'{lab}<div class="letext">{html}{c}</div></div>')


def line_editor(html, menu="", note=None, width=600, translated=None):
    """The Line editor dock: its title bar, the tag strip, the counters and the Text field."""
    strip = "".join(f'<span class="qb">{ic(i)}</span>' for i in ["tag-bold", "tag-italic", "tag-font"])
    fields = (editor_field(html, focus=True, width=width - 16) if translated is None else
              editor_field(translated[0], muted=True, width=width - 16, label="Original")
              + editor_field(translated[1], focus=True, width=width - 16, label="Translated"))
    n = f'<p class="cap">{note}</p>' if note else ""
    return (f'<div class="dock" style="width:{width}px"><div class="dhead"><b>Line editor</b>'
            f'<span class="dmore">⋯</span></div><div class="dbodyx"><div class="qbs">{strip}</div>'
            f'<div class="cnt">Wraps: 47/43&nbsp;&nbsp;Characters per second: 14&lt;=15</div>{fields}{menu}</div></div>{n}')


def menu(items, left, top, width=300):
    rows = []
    for it in items:
        if it == "-":
            rows.append('<div class="msep"></div>')
            continue
        kind, text = it[0], it[1]
        icon = it[2] if len(it) > 2 else None
        hint = it[3] if len(it) > 3 else ""
        i = ic(icon) if icon else '<span class="noic"></span>'
        h = f'<span class="mhint">{hint}</span>' if hint else ""
        if kind == "head":
            rows.append(f'<div class="mhead">{ic("spellchecker", 16, "gcol")}<div><div class="mmsg">{text}</div>'
                        f'<div class="mgrp">{hint}</div></div></div>')
        elif kind == "sug":
            rows.append(f'<div class="mi sug"><span class="noic"></span><span>{text}</span></div>')
        elif kind == "check":
            rows.append(f'<div class="mi">{i}<span class="mt">{text}</span><span class="mchk">{box(True)}</span></div>')
        elif kind == "sub":
            rows.append(f'<div class="mi">{i}<span class="mt">{text}</span><span class="msub">›</span></div>')
        elif kind == "dis":
            rows.append(f'<div class="mi dis">{i}<span class="mt">{text}</span>{h}</div>')
        else:
            rows.append(f'<div class="mi{" hi" if kind == "hi" else ""}">{i}<span class="mt">{text}</span>{h}</div>')
    return f'<div class="menu" style="left:{left}px;top:{top}px;width:{width}px">{"".join(rows)}</div>'


COMMON = ["-", ("i", "Copy", "edit-copy"), ("i", "Cut", "edit-cut"), ("i", "Paste", "edit-paste"), "-",
          ("check", "Spellchecker", "spellchecker"), ("sub", "Installed languages", "dictionary")]


def grammar_menu():
    return menu([("head", "Use “an” before a word that starts with a vowel sound.", None, "Grammar · Miscellaneous"),
                 ("sug", "an"), "-",
                 ("hi", "Ignore this rule", None, "Turns off Miscellaneous"),
                 ] + COMMON + [("i", "Delete", "delete")], 260, 128, 360)


def word_menu():
    return menu([("head", "Did you mean to spell “tomorow” this way?", None, "Grammar · Spelling and orthography"),
                 ("sug", "tomorrow"), ("sug", "tomorrows"), "-",
                 ("i", "Ignore this rule", None, "Turns off Spelling and orthography"),
                 ("hi", "Add “tomorow” to dictionary", "dictionary-add"),
                 ] + COMMON[:4] + [("i", "Delete", "delete")], 160, 128, 400)


def spelling_menu():
    return menu([("sug", "tomorrow"), ("sug", "tomorrows"), "-"] + COMMON[1:] +
                [("hi", "Add word “tomorow” to dictionary", "dictionary-add"), ("i", "Delete", "delete")], 160, 128, 300)


def toast_after_ignore():
    acts = btn("Undo", small=True) + btn("Grammar settings…", small=True)
    return (f'<div class="toast" style="width:380px">{ic("about", 16)}<div class="tt">'
            f'<div class="ttitle">Miscellaneous turned off</div>'
            f'<div class="ttext">Grammar check no longer uses its 48 rules. Turn it back on in Settings › Spelling &amp; grammar.</div>'
            f'<div class="tacts">{acts}</div></div><span class="tx">{ic("tab-close", 14)}</span></div>')


# ---------------------------------------------------------------------------------------------
# The Spellchecker panel (PU6, SpellCheckerPanel.qml): the dock along the bottom.


def panel(state):
    seg = segmented(["Spelling", "Grammar"], "Grammar" if state != "spelling" else "Spelling")
    head = f'<div class="phead"><b>Spellchecker</b><span class="dmore">⋯</span></div>'
    if state in ("off",):
        body = (f'<div class="pempty">{seg}<div class="pemsg">Grammar check is off</div>'
                f'<div class="pemsub">Choose an English in Settings to check grammar with Harper.</div>'
                f'{btn("Grammar settings…")}</div>')
        return f'<div class="spanel">{head}<div class="pbody2">{body}</div></div>'
    if state == "done":
        body = (f'<div class="pempty">{seg}<div class="pemsg">No grammar findings in Episode 03.ass</div>'
                f'<div class="pemsub">842 lines checked.</div>{btn("Check again")}</div>')
        return f'<div class="spanel">{head}<div class="pbody2">{body}</div></div>'
    if state == "spelling":
        word = '<div class="pword"><span class="pw">tehm</span><span class="pl">Line 4</span></div>'
        ctx = '<div class="pctx">Hello <u>tehm</u> good text</div>'
        sugg = '<div class="plist"><div class="pli cur">text</div><div class="pli">them</div><div class="pli">term</div></div>'
        acts = (f'<div class="pacts">{btn("Replace", primary=True)}{btn("Replace all")}{btn("Ignore")}{btn("Ignore All")}'
                f'<span class="wide">{btn("Add to dictionary", icon="dictionary-add")}</span></div>')
        left = f'<div class="pleft">{seg}{word}{ctx}<div class="prep"><span>Replace with</span>{field("text", 260, focus=True)}</div></div>'
        return f'<div class="spanel">{head}<div class="pbody2 cols">{left}{sugg}{acts}</div></div>'
    walking = state == "walking"
    prog = ""
    if walking:
        prog = (f'<div class="pprog"><span class="pbar"><i style="width:37%"></i></span>'
                f'<span class="pptext">Checking line 312 of 842…</span>{btn("Cancel", small=True)}</div>')
    else:
        prog = '<div class="pprog done"><span class="pptext">Finding 1 of 7 · 842 lines checked</span></div>'
    word = ('<div class="pword"><span class="pmsg">Use “an” before a word that starts with a vowel sound.</span></div>'
            '<div class="pl2">Line 4 · Miscellaneous</div>')
    ctx = f'<div class="pctx">Their going to the store tomorow, I has {g("a")} apple.</div>'
    sugg = '<div class="plist"><div class="pli cur">an</div></div>'
    acts = (f'<div class="pacts">{btn("Replace", primary=True)}{btn("Ignore")}{btn("Ignore this rule")}'
            f'<span class="b dis2">{ic("dictionary-add")}<span>Add to dictionary</span></span></div>')
    left = (f'<div class="pleft">{seg}{prog}{word}{ctx}<div class="prep"><span>Replace with</span>'
            f'{field("an", 260, focus=True)}</div></div>')
    return f'<div class="spanel">{head}<div class="pbody2 cols">{left}{sugg}{acts}</div></div>'


# ---------------------------------------------------------------------------------------------
# Settings › Spelling & grammar (PF1's form)


def spelling_page(off=False):
    groups = "".join(
        f'<div class="r chk{" dis" if off else ""}"><span class="slot">{box(True)}</span>'
        f'<span class="rl wrap">{n}<span class="gcount">{c} rules</span><span class="gdesc">{d}</span></span></div>'
        for n, c, d in GROUPS)
    choice = "Off" if off else "English (US)"
    note = ("Harper checks the Text field (the Translated field in translation mode) after you stop typing. "
            "It runs as a separate program only while grammar check is on.")
    return "".join([
        section("Spelling",
                check("Check spelling", True),
                row("Dictionary", combo("English (United States)"), icon="dictionary"),
                check("Show suggestions on double-click", True), first=True),
        section("Grammar",
                row("Grammar check", combo(choice), icon="spellchecker", note=note),
                ('<div class="rnote">While spelling is checked, Harper leaves spelling to the dictionary above, '
                 'so a word is never marked twice.</div>' if not off else "")),
        section("Rule groups" + ('<span class="gdis"> · Choose an English above to switch them</span>' if off else ""),
                block(f'<div class="glist">{groups}</div>'),
                block(f'<div class="lic">Harper 3.0.0 by Automattic, Apache License 2.0. '
                      f'<span class="lnk">Licence notice</span> in Help › Credits.</div>')),
    ])


def settings_window2(current, body, scroll=None, height=980):
    bar = ""
    if scroll:
        top, h = scroll
        bar = f'<span class="sbar" style="top:{top}px;height:{h}px"></span>'
    return (f'<div class="dlg settings" style="width:880px;height:{height}px">{titlebar("Settings")}'
            f'<div class="split">{page_list(current)}<div class="pbody">{bar}<div class="form">{body}</div></div></div>'
            f'{footer()}</div>')


def combo_open():
    rows = "".join(f'<div class="mi{" hi" if o == "English (UK)" else ""}"><span class="noic">{"" if o != "Off" else ""}</span>'
                   f'<span class="mt">{o}</span></div>'
                   for o in ["Off", "English (US)", "English (UK)", "English (Canada)", "English (Australia)"])
    return f'<div class="menu" style="position:relative;width:220px">{rows}</div>'


# The mapping strip: what Harper gets for Lines with tags, \N, karaoke and drawings.
MAPPING = [
    ("Overrides", tag(r"\i1") + g("Their") + tag(r"\i0") + " going home.", "Their going home.",
     "Tags are taken out; the finding on “Their” is drawn back under the raw text."),
    (r"\N", "I " + g("has") + '<span class="tg">\\N</span>' + g("a") + " apple.", "I has a apple.",
     "A line break is a space for Harper; a finding across it underlines both rows."),
    ("Karaoke", tag(r"\k20") + "I " + tag(r"\k30") + g("has") + " " + tag(r"\k25") + g("a") + " " + tag(r"\k40") + "apple",
     "I has a apple", "Syllable tags are skipped like any override; each finding sits between them."),
    ("Drawing", tag(r"\p1") + '<span class="dr">m 0 0 l 100 0 100 100 0 100</span>' + tag(r"\p0"), "(nothing)",
     "Drawing commands are never checked."),
]


def mapping():
    rows = "".join(f'<div class="mrow"><span class="mk">{k}</span>{editor_field(raw, width=420)}'
                   f'<span class="marrow">→</span><span class="mharp">{t(h)}</span><span class="mnote">{n}</span></div>'
                   for k, raw, h, n in MAPPING)
    return f'<div class="map">{rows}</div>'


GR1_CSS = """
.dock{position:relative;background:%(panel)s;outline:1px solid %(line)s}
.dhead,.phead{display:flex;align-items:center;height:34px;padding:0 10px 0 12px;box-shadow:inset 0 -1px %(line)s}
.dmore{margin-left:auto;color:%(muted)s;font-weight:700}
.dbodyx{position:relative;padding:6px 8px 10px}
.cnt{color:%(text)s;margin:6px 0 6px;font-size:13px}
.lefield{position:relative;background:%(field)s;border:1px solid %(line)s;padding:5px 9px;font-size:13px;line-height:1.6;min-height:28px}
.lefield.focus{border-color:%(accent)s}
.lefield.mut{opacity:.8}
.lefield+.lefield{margin-top:6px}
.flab{position:absolute;right:6px;top:4px;font-size:11px;color:%(muted)s}
.caret{display:inline-block;width:1px;height:15px;background:%(text)s;vertical-align:-3px}
.br{color:%(brace)s}.tg{color:%(tag)s}
.dr{color:%(muted)s}
.gm{text-decoration:underline wavy %(grammar)s;text-decoration-thickness:1.5px;text-underline-offset:3px;text-decoration-skip-ink:none}
.spm{background:%(spell)s}
.gcol{color:%(grammar)s}
.mhead{display:flex;gap:10px;padding:6px 14px 8px 10px;align-items:flex-start}
.mmsg{line-height:1.4}
.mgrp{color:%(muted)s;font-size:12px;margin-top:2px}
.mi.sug{font-weight:650}
.mt{flex:1}
.mhint{color:%(muted)s;font-size:12px;margin-left:12px}
.mi.hi .mhint{color:%(text)s}
.mi.dis{color:%(disabled)s}
.mchk .box{width:14px;height:14px}
.msub{color:%(muted)s}
.msep{height:1px;background:%(line)s;margin:4px 0}
.spanel{background:%(panel)s;outline:1px solid %(line)s;width:1240px}
.pbody2{padding:12px}
.pbody2.cols{display:grid;grid-template-columns:540px 220px 1fr;gap:16px;align-items:start}
.pleft{display:flex;flex-direction:column;gap:8px}
.pword{display:flex;align-items:baseline;gap:8px;margin-top:4px}
.pw{font-size:20px;font-weight:650}
.pmsg{font-size:16px;font-weight:650;line-height:1.3}
.pl,.pl2{color:%(muted)s;font-size:12px}
.pctx{line-height:1.6}
.prep{display:flex;align-items:center;gap:8px}
.plist{background:%(field)s;outline:1px solid %(line)s;height:112px;margin-top:4px}
.pli{height:26px;display:flex;align-items:center;padding:0 6px}
.pli.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.pacts{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:4px}
.pacts .b{width:100%%}
.pacts .wide{grid-column:1 / -1}
.b.dis2{color:%(disabled)s;border-color:%(line)s;width:100%%}
.pprog{display:flex;align-items:center;gap:10px;height:28px}
.pprog.done .pptext{color:%(muted)s}
.pbar{width:180px;height:4px;background:%(raised)s;outline:1px solid %(line)s;position:relative}
.pbar i{position:absolute;left:0;top:0;bottom:0;background:%(accent)s}
.pptext{font-size:12px}
.pempty{display:flex;flex-direction:column;align-items:center;gap:8px;padding:10px 0 14px}
.pempty .seg{margin-bottom:6px}
.pemsg{font-weight:650}
.pemsub{color:%(muted)s}
.glist{display:grid;grid-template-columns:1fr 1fr;gap:6px 24px;max-width:600px}
.glist .r{align-items:flex-start}
.glist .r .slot{margin-top:2px}
.gcount{color:%(muted)s;font-size:12px;margin-left:6px}
.gdesc{display:block;color:%(muted)s;font-size:12px;line-height:1.35}
.r.dis{color:%(disabled)s}.r.dis .box,.r.dis .box.on{border-color:%(disabled)s;background:%(raised)s;color:%(disabled)s}
.gdis{font-weight:400;color:%(muted)s;font-size:12px}
.lic{margin-top:14px;color:%(muted)s;font-size:12px;width:600px}
.lnk{color:%(accent)s;text-decoration:underline}
.map{display:flex;flex-direction:column;gap:8px}
.mrow{display:grid;grid-template-columns:80px 420px 20px 160px 1fr;gap:10px;align-items:center}
.mk{color:%(muted)s;font-size:12px}
.marrow{color:%(muted)s;text-align:center}
.mharp{font-family:'Liberation Mono',monospace;font-size:12px}
.mnote{color:%(muted)s;font-size:12px;line-height:1.4}
.swatch{display:inline-block;width:40px;height:14px;vertical-align:-2px;margin-right:6px}
.legend{display:flex;gap:28px;align-items:center;margin-top:10px}
.legend span{display:flex;align-items:center}
.abs{position:relative}
.pleft .seg,.pempty .seg{align-self:flex-start}
.pempty .seg{align-self:center}
.toast{display:flex;gap:10px;align-items:flex-start;padding:10px 8px 10px 12px;background:%(panel)s;outline:1px solid %(line)s;
       border-radius:4px;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);color:%(text)s}
.toast>.ic{margin-top:1px}
.tt{flex:1;min-width:0;display:flex;flex-direction:column;gap:3px}
.ttitle{font-weight:650;line-height:1.35}
.ttext{color:%(muted)s;line-height:1.4}
.tacts{display:flex;gap:4px;margin:4px 0 0 -6px}
.tx{display:flex;align-items:center;justify-content:center;width:22px;height:22px;color:%(muted)s;flex:none}
.capt{font-size:12px;color:%(muted)s;margin:0 0 8px;max-width:600px;line-height:1.45;min-height:52px}
"""


def sheet(key):
    th = THEMES[key]
    cc = CONTENT[key]
    css = (CSS + GR1_CSS) % dict(th, **cc, shadow=".55" if key in DARKISH else ".28", focus=th["text"],
                                 tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    legend = (f'<div class="legend"><span><span class="spm" style="padding:0 6px">tomorow</span>&nbsp; spelling '
              f'(Hunspell, unchanged: the theme’s EDITOR_SPELLCHECKER background)</span>'
              f'<span>{g("has")}&nbsp; grammar (Harper: a wavy underline in the new “grammar” colour, '
              f'{cc["grammar"]})</span></div>')
    content = f"""
<h1>GR1 Grammar check with Harper: {th['name']}</h1>
<p class="note">Harper (harper-ls, a helper process over LSP on stdio) checks the Text field, or the Translated field
in translation mode, about 300 ms after typing stops. Override tags, <code>\\N</code> and drawings are taken out first
with the Line editor’s own projection (core::project, as TM1’s tag transfer uses), and each finding is drawn back under
the raw text. Findings are a wavy underline in a colour of their own; spelling keeps its background mark. Grammar check
is Off by default; while it is off no helper process runs. The Grid gets no marks.</p>

<h2>1 · The Line editor: Line 4 with tags shown (default) and with tags hidden</h2>
<div class="row2">
 <div class="col">{line_editor(LINE4_TAGS)}</div>
 <div class="col">{line_editor(LINE4_HIDDEN)}</div>
</div>
{legend}

<h2>2 · Right-click on a finding</h2>
<div class="row2">
 <div class="col abs" style="height:560px"><p class="capt">On a grammar finding the menu starts with Harper’s message and the rule’s group, then its suggestions
 (bold, as spelling suggestions are), then Ignore this rule, which turns the rule’s group off in Settings and says so.
 The usual items follow.</p>{line_editor(LINE4_TAGS, grammar_menu())}</div>
 <div class="col abs" style="height:560px"><p class="capt">With Check spelling off, Harper’s own spelling rule is on: its findings are grammar marks too, and
 “Add to dictionary” adds the word to the app’s dictionary file and to Harper’s.</p>{line_editor(LINE4_TAGS.replace(s("tomorow"), g("tomorow")), word_menu())}</div>
 <div class="col abs" style="height:560px"><p class="capt">On a spelling mark the menu is today’s (Hunspell’s suggestions, Add word). With Check spelling on,
 Harper’s spelling rule is off, so a word never gets both marks.</p>{line_editor(LINE4_TAGS, spelling_menu(), width=520)}</div>
</div>
<div class="row2" style="margin-top:12px">
 <div class="col">{toast_after_ignore()}
 <p class="cap">After Ignore this rule: an info toast (NT1) with Undo and a way to the settings.</p></div>
 <div class="col">{line_editor("", translated=(tag(r"\i1") + "Their going to the store." + tag(r"\i0"), g("Their") + " going to the store."), width=620)}
 <p class="cap">Translation mode: only the Translated field is checked (as spelling is).</p></div>
</div>

<h2>3 · The Spellchecker panel: the Grammar filter</h2>
<div class="col" style="gap:14px">
 {panel("walking")}
 <p class="cap" style="max-width:1240px">Grammar walks the whole file in the background from the active Line, a Line
 at a time, and shows the first finding as soon as there is one; the progress line has Cancel. The panel and the
 editor stay usable meanwhile. Replace puts the chosen suggestion in (tags around it stay where they are); Ignore skips
 this finding; Ignore this rule turns its group off (Settings) and drops the group’s other findings; Add to dictionary
 is for word findings only.</p>
 {panel("found")}
 <p class="cap">The walk finished: the count and the finding shown.</p>
 <div class="row2"><div class="col" style="width:610px">{panel("done").replace('class="spanel"', 'class="spanel" style="width:610px"')}</div>
 <div class="col" style="width:610px">{panel("off").replace('class="spanel"', 'class="spanel" style="width:610px"')}</div></div>
 <p class="cap">Nothing found, and Grammar chosen while grammar check is off.</p>
 {panel("spelling")}
 <p class="cap">Spelling: today’s panel unchanged, with the filter above the word.</p>
</div>

<h2>4 · Settings › Spelling &amp; grammar</h2>
<div class="row2">
 <div class="col">{settings_window2("spelling", spelling_page(), scroll=(4, 800))}
 <p class="cap">A page of its own after Line editor: the Spelling section moves here from the Line editor page, then
 Grammar check (Off, English (US), (UK), (Canada), (Australia)) and Harper’s 15 rule groups (its own curated grouping,
 harper-core/default_config.json), each a switch with Harper’s description. Ignore this rule unticks a group here.</p></div>
 <div class="col">{settings_window2("spelling", spelling_page(off=True), scroll=(4, 800))}
 <p class="cap">Off (the default): the groups are kept but greyed out.</p>
 <div style="margin-top:14px">{combo_open()}</div>
 <p class="cap">The Grammar check list (Harper’s dialects: American, British, Canadian, Australian).</p></div>
</div>

<h2>5 · What Harper gets, and where the marks go back</h2>
{mapping()}
"""
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')


for key in THEMES:
    html = sheet(key)
    (OUT / f"gr1-{key}.html").write_text(html)
    # One page per section, for the card's tables.
    head, body = html.split("<h1>", 1)
    intro, *parts = body.split("<h2>")
    title = intro.split("</h1>", 1)[0]
    for n, part in enumerate(parts, 1):
        (OUT / f"gr1-{key}-{n}.html").write_text(f"{head}<h1>{title}</h1><h2>{part}" + ("" if part.endswith("</html>") else "</body></html>"))
print("wrote", OUT)
