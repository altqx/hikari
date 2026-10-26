#!/usr/bin/env python3
"""LE1 #233 Line editor layout mockup: one HTML sheet per theme, screenshotted by shoot.sh.

usage: gen.py <src/ui/icons> <out dir>
Colours are theme.cpp's base roles with the default accent; icons are the set's (src/ui/icons) unless named in
NEW_ICONS. "Today" cells embed shots/today-THEME-*.png from today.sh (the TM1 part-2 build).
"""
import html
import pathlib
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
SHOTS = pathlib.Path(__file__).resolve().parent / "shots"

# theme.cpp baseRoles + the default accent; syn = the editor's syntax roles (braces, operators, names, values).
THEMES = {
    "dark": dict(name="Dark", app="dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", hc=False, danger="#FFADAD",
                 warning="#E0A030", syn=("#8791FD", "#00C3FF", "#00C3FF", "#0076FF")),
    "light": dict(name="Light", app="light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", hc=False, danger="#A51F31",
                  warning="#8A5A00", syn=("#7E3B00", "#787600", "#787600", "#34C200")),
    "hcwhite": dict(name="High contrast white", app="highContrastWhite", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    hc=True, danger="#A00000", warning="#6B4500", syn=("#7A3E00", "#005A9E", "#005A9E", "#006400")),
    "hcblack": dict(name="High contrast black", app="highContrastBlack", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    hc=True, danger="#FFADAD", warning="#FFD54A", syn=("#FFB060", "#60D0FF", "#60D0FF", "#7CFC7C")),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Glyphs this card would add (K1's 16 px, 1 px stroke style): the three margin marks, and the mockup's own chevron.
NEW_ICONS = {
    "margin-left": SVG_HEAD + '<path d="M2.5 2v12"/><path d="M13.5 5.5h-6v5h6"/><g id="accent"><path d="M3.5 8h3M5 6.5 6.5 8 5 9.5"/></g></svg>',
    "margin-right": SVG_HEAD + '<path d="M13.5 2v12"/><path d="M2.5 5.5h6v5h-6"/><g id="accent"><path d="M12.5 8h-3M11 6.5 9.5 8 11 9.5"/></g></svg>',
    "margin-vertical": SVG_HEAD + '<path d="M2 2.5h12M2 13.5h12"/><path d="M5.5 6.5h5v3h-5z"/><g id="accent"><path d="M8 3.5v2M8 10.5v2"/></g></svg>',
    "chev": SVG_HEAD + '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/></svg>',
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "info": SVG_HEAD + '<circle cx="8" cy="8" r="6"/><g id="accent"><path d="M8 7.5v4"/><circle cx="8" cy="5" r=".6" fill="currentColor" stroke="none"/></g></svg>',
}


def svg(role):
    return NEW_ICONS[role] if role in NEW_ICONS else (ICONS / f"{role}.svg").read_text().strip()


def icon(role, cls=""):
    return f'<span class="ic {cls}">{svg(role)}</span>'


def btn(role, cls="", title=""):
    return f'<span class="b {cls}" title="{html.escape(title)}">{icon(role)}</span>'


SEP = '<span class="sep"></span>'

# The tag strip's groups, in order; the end groups fold first.
G_TAGS = [("tag-bold", "Bold"), ("tag-italic", "Italic"), ("tag-underline", "Underline"), ("tag-strikeout", "Strikeout")]
G_LOOK = [("tag-font", "Font selection"), ("colour-primary", "Primary color"),
          ("colour-secondary", "Secondary color for karaoke"), ("colour-outline", "Border color"),
          ("colour-shadow", "Shadow color")]
G_TL = [("paste-translation", "Paste all"), ("paste-selection-translation", "Paste the selected"),
        ("comment-original", "Comment out original")]
G_TL_TOGGLES = [("unconfirmed", "Not confirmed"), ("moving-tags", "Tags from original")]
G_CUSTOM = [("tool-move", "Move \\move"), ("tag-edit", "Blur \\blur3")]


def align_choice():
    return f'<span class="b dd" title="Text position">{icon("alignment-2")}{icon("chev", "xs")}</span>'


def counters(t, chars="21/43", cps="8/15", warn_chars=False, warn_cps=False, hover=None):
    def one(name, val, warn):
        n, lim = val.rsplit("/", 1)
        w = " warn" if warn else ""
        mark = icon("notice-warning", "sm") if warn else ""
        h = " hov" if hover == name else ""
        return (f'<span class="cn{w}{h}">{mark}<span class="cl">{name}</span><span class="cv">{n}</span>'
                f'<span class="cm">/{lim}</span></span>')
    return f'<span class="counters">{one("Wraps", chars, warn_chars)}{one("CPS", cps, warn_cps)}</span>'


def strip(t, groups=("tags", "look", "align", "custom"), folded=False, more_open=False, cnt=None, hover=None,
          toggles_on=("moving-tags",)):
    parts = []
    for g in groups:
        if g == "tags":
            parts.append("".join(btn(r, "hov" if hover == r else "", n) for r, n in G_TAGS))
        elif g == "look":
            parts.append("".join(btn(r, "hov" if hover == r else "", n) for r, n in G_LOOK))
        elif g == "align":
            parts.append(align_choice())
        elif g == "tl":
            parts.append("".join(btn(r, "", n) for r, n in G_TL) + '<span class="sep thin"></span>'
                         + "".join(btn(r, "tg" if r in toggles_on else "", n) for r, n in G_TL_TOGGLES))
        elif g == "custom":
            parts.append("".join(btn(r, "", n) for r, n in G_CUSTOM) + btn("add", "", "Add a tag button"))
    more = btn("menu-more", "on" if more_open else "", "More") if folded else ""
    c = cnt if cnt is not None else counters(t)
    return f'<div class="strip">{SEP.join(parts)}{more}<span class="grow"></span>{c}</div>'


def syn(t, raw):
    """ASS text with the Line editor's syntax colours (braces, \\ operators, tag names, values)."""
    br, op, nm, va = t["syn"]
    out, i = [], 0
    while i < len(raw):
        if raw[i] == "{":
            j = raw.index("}", i)
            inner = raw[i + 1:j]
            seg = []
            k = 0
            while k < len(inner):
                if inner[k] == "\\":
                    m = k + 1
                    while m < len(inner) and inner[m].isalpha():
                        m += 1
                    name = inner[k + 1:m]
                    # \fad, \an: the digits and brackets after the name are the value
                    n2 = m
                    while n2 < len(inner) and inner[n2] != "\\":
                        n2 += 1
                    seg.append(f'<span style="color:{op}">\\</span><span style="color:{nm}">{name}</span>'
                               f'<span style="color:{va}">{html.escape(inner[m:n2])}</span>')
                    k = n2
                else:
                    seg.append(html.escape(inner[k]))
                    k += 1
            out.append(f'<span style="color:{br}">{{</span>{"".join(seg)}<span style="color:{br}">}}</span>')
            i = j + 1
        else:
            j = raw.find("{", i)
            j = len(raw) if j < 0 else j
            out.append(html.escape(raw[i:j]))
            i = j
    return "".join(out)


CARET = '<span class="caret"></span>'


def field(body, h=None, focus=False, cls="", caption=None, sel=False):
    hh = f"height:{h}px;" if h else ""
    cap = f'<div class="fcap">{caption}</div>' if caption else ""
    return f'{cap}<div class="fld{" focus" if focus else ""} {cls}" style="{hh}">{body}</div>'


def inp(val, w=None, cls="", grow=False, ph=False, dd=False, pend=False):
    st = f"width:{w}px;" if w else ""
    c = cls + (" grow" if grow else "") + (" ph" if ph else "") + (" dd" if dd else "") + (" pend" if pend else "")
    chev = icon("chev", "sm") if dd else ""
    return f'<span class="in {c}" style="{st}">{val}{chev}</span>'


def lab(text, ctrl, cls=""):
    return f'<span class="lf {cls}"><span class="ll">{text}</span>{ctrl}</span>'


def check(on=False, text="Comment"):
    c = svg("check") if on else ""
    return f'<span class="ck"><span class="chk{" on" if on else ""}">{c}</span>{text}</span>'


def margins(t, vals=("0", "0", "0"), style=("90", "90", "50"), hint=True, above=False):
    out = []
    for role, v, s in zip(("margin-left", "margin-right", "margin-vertical"), vals, style):
        shown = (f'<span class="sv">{s}</span>' if hint and v == "0" else v)
        out.append(f'<span class="in mg">{icon(role, "mut")}{shown}</span>')
    return "".join(out)


LINE = dict(layer="0", start="0:00:05.30", end="0:00:07.90", dur="0:00:02.60", style="Default", actor="Mina",
            effect="", comment=False)


def inspector_beside(t, L=LINE, warn_times=False, pend_layer=False, hint=True):
    tw = ' style="color:%s"' % t["warning"] if warn_times else ""
    s = f'<span{tw}>{L["start"]}</span>'
    e = f'<span{tw}>{L["end"]}</span>'
    r1 = (lab("Start", inp(s, 88, "tn" + (" warnf" if warn_times else "")))
          + lab("End", inp(e, 88, "tn" + (" warnf" if warn_times else "")))
          + lab("Duration", inp(L["dur"], 88, "tn"))
          + lab("Style", inp(L["style"], dd=True, grow=True), "grow")
          + btn("edit", "", "Edit style"))
    ly = f'<span style="color:{t["warning"]}">{L["layer"]}</span>' if pend_layer else L["layer"]
    r2 = (check(L["comment"])
          + lab("Layer", inp(ly, 44, "tn", pend=pend_layer))
          + lab("Actor", inp(L["actor"] or "", dd=True, grow=True), "grow")
          + lab("Effect", inp(L["effect"], dd=True, grow=True), "grow")
          + lab("Margins", margins(t, hint=hint)))
    return f'<div class="insp"><div class="irow">{r1}</div><div class="irow">{r2}</div></div>'


def inspector_above(t, L=LINE, hint=True):
    def cell(label, ctrl, span=False):
        return f'<div class="ac{" span" if span else ""}"><div class="al">{label}</div><div class="actl">{ctrl}</div></div>'
    rows = [
        cell("Start", inp(L["start"], cls="tn grow")) + cell("End", inp(L["end"], cls="tn grow")),
        cell("Duration", inp(L["dur"], cls="tn grow")) + cell("Layer", inp(L["layer"], cls="tn grow")),
        cell("Style", inp(L["style"], dd=True, grow=True) + btn("edit", "", "Edit style"), span=True),
        cell("Actor", inp(L["actor"], dd=True, grow=True)) + cell("Effect", inp(L["effect"], dd=True, grow=True)),
        cell("Margins", margins(t, hint=hint)) + cell("&nbsp;", check(L["comment"])),
    ]
    return '<div class="agrid">' + "".join(rows) + "</div>"


def dock(title, body, w, h=None, scroll=None, cls=""):
    """scroll = (thumb top fraction, thumb height fraction): the editor's scroll bar."""
    hh = f"height:{h}px;" if h else ""
    bar = ""
    if scroll:
        top, size = scroll
        bar = (f'<div class="sbar"><div class="sthumb" style="top:{top * 100:.1f}%;height:{size * 100:.1f}%"></div></div>')
    return (f'<div class="dock {cls}" style="width:{w}px;{hh}"><div class="dhead"><span class="t">{title}</span>'
            f'{icon("panel-menu", "mut")}</div><div class="dbody{" scrolled" if scroll else ""}">'
            f'<div class="dcontent">{body}</div>{bar}</div></div>')


TEXT3 = r"She {\i1}really{\i0} {\b1}hates{\b0} the rain."


def editor_default(t, w=640, h=200, text=TEXT3, focus=True, **kw):
    body = (strip(t, cnt=kw.pop("cnt", None), hover=kw.pop("hover", None))
            + field(syn(t, text) + (CARET if focus else ""), focus=focus, cls="fill")
            + kw.pop("issue", "")
            + inspector_beside(t, **kw))
    return dock("Line editor", body, w, h)


def issue(t, text, kind="danger"):
    role = "notice-error" if kind == "danger" else "notice-warning"
    return f'<div class="issue {kind}">{icon(role, "sm")}<span>{text}</span></div>'


def chip(t, n, text, kind, placed, style="", colour=None):
    sw = f'<span class="sw" style="background:{colour}"></span>' if colour else ""
    return (f'<span class="rchip{" placed" if placed else ""}"><span class="rnum">{n}</span>{sw}'
            f'<span class="rtext" style="{style}">{html.escape(text)}</span><span class="rkind">{kind}</span></span>')


def runstrip(t, placed=(True, False)):
    return ('<div class="runs">' + chip(t, 1, "really", "Italic", placed[0], "font-style:italic")
            + chip(t, 2, "hates", "Bold", placed[1], "font-weight:700") + "</div>")


def tooltip(text, sub, x, y, w=None):
    ww = f"width:{w}px;" if w else ""
    return f'<div class="tip" style="left:{x}px;top:{y}px;{ww}">{text}<div class="tsub">{sub}</div></div>'


def menu(items, x, y, w=250):
    out = []
    for it in items:
        if it == "-":
            out.append('<div class="msep"></div>')
            continue
        role, text, key, state = it
        c = " " + state if state else ""
        ic = icon(role) if role else '<span class="ic"></span>'
        chk = svg("check") if "checked" in (state or "") else ""
        out.append(f'<div class="mi{c}"><span class="cb">{chk}</span>{ic}<span class="mt">{text}</span>'
                   f'<span class="mk">{key}</span></div>')
    return f'<div class="menu" style="left:{x}px;top:{y}px;width:{w}px">{"".join(out)}</div>'


def today(t, which, w, h):
    p = SHOTS / f"today-{t['app']}-{which}.png"
    return f'<img class="shot" src="file://{p}" width="{w}" height="{h}">'


def cell(n, cap, note, stage, h, span=2):
    sp = f' style="grid-column:span {span}"' if span > 1 else ""
    return (f'<div class="cell"{sp}><div class="cap"><b>{n}</b>{cap}</div><div class="note">{note}</div>'
            f'<div class="stage" style="height:{h}px">{stage}</div></div>')


def tag(text):
    return f'<div class="tag">{text}</div>'


def sheet(t):
    cells = []
    # 1. Default size, today and proposed.
    cells.append(cell(1, "Default size (Editing layout at 1280 × 800, Line editor 640 × 200): today and proposed.",
        "Today’s strip, counters, field and the two unlabelled inspector rows (left, the running app). Proposed (right): "
        "one strip row with the tag groups split by thin rules and the counters at its right end; the text field takes "
        "the free height (two lines here instead of one); every field has its name beside it.",
        f'<div class="pair"><div>{tag("Today")}{today(t, "default", 640, 200)}</div>'
        f'<div>{tag("Proposed")}{editor_default(t)}</div></div>', 244))

    # 2. Wide.
    cells.append(cell(2, "Wide (a floating Line editor, 1000 × 230): the same two rows; the lists and the text get the room.",
        "Start, End and Duration keep their width; Style, Actor and Effect stretch. Legacy’s single row above 850 px goes: "
        "with names on the fields it would need about 1350 px, and two stable rows are easier to scan.",
        editor_default(t, 1000, 230, text=r"{\an8\fad(200,0)}Kitamachi Station", focus=False,
                       L=dict(LINE, start="0:00:10.20", end="0:00:12.60", dur="0:00:02.40", style="Sign - Station",
                              layer="1", actor="")), 236))

    # 3. Minimum.
    narrow_body = (strip(t, groups=("tags",), folded=True)
                   + field(syn(t, TEXT3) + CARET, focus=True, h=56)
                   + inspector_above(t))
    min_body = (strip(t, groups=("tags",), folded=True) + field(syn(t, TEXT3) + CARET, focus=True, h=33)
                + inspector_above(t))
    fold = menu([("tag-font", "Font selection", "", ""), ("colour-primary", "Primary color", "", ""),
                 ("colour-secondary", "Secondary color for karaoke", "", ""), ("colour-outline", "Border color", "", ""),
                 ("colour-shadow", "Shadow color", "", ""), "-",
                 ("alignment-2", "Text position", "", "sub"), "-",
                 ("tool-move", "Move \\move", "Ctrl+1", "hov"), ("tag-edit", "Blur \\blur3", "", ""),
                 ("add", "Add a tag button…", "", "")], 128, 104, 250)
    cells.append(cell(3, "Minimum width (360): the strip folds into More, the fields put their names above, two to a row.",
        "The strip keeps Bold … Strikeout and the counters; the other groups fold, last first, into More (the double chevron; right): icon, name "
        "and hotkey, Text position as a submenu, the custom buttons after it. The strip no longer sets the dock’s minimum "
        "width, so a long row of custom tag buttons can’t widen the dock. Left: 360 × 400, everything shown. Middle: 360 × "
        "150, the smallest height (strip and one line of text); the rest scrolls with today’s bar.",
        f'<div class="trio"><div>{dock("Line editor", narrow_body, 360, 400)}</div>'
        f'<div>{dock("Line editor", min_body, 360, 150, scroll=(0, .32))}</div>'
        f'<div style="position:relative">{dock("Line editor", narrow_body, 360, 400)}{fold}</div></div>', 404))

    # 4. Translation mode, Original text panel open (Translation preset).
    tl = "Sie hasst den Regen wirklich."
    tl_body = (strip(t)
               + field('Sie <span class="selx">hasst</span> den Regen <span style="font-style:italic;box-shadow:inset 0 -2px 0 '
                       f'{t["accent"]}">wirklich</span>.' + CARET, focus=True, cls="fill")
               + runstrip(t)
               + inspector_beside(t))
    cells.append(cell(4, "Translation mode with the Original text panel (Translation preset, 713 × 330): room for TM1’s run strip.",
        "The field is the Translated text; TM1 part 2’s run chips sit right under it (they act on its selection), then the "
        "inspector. The counters measure the Translated text, as today. The karaoke notice, when shown, goes under the chips "
        "in the secondary text colour.",
        dock("Line editor", tl_body, 713, 330), 336, span=1))

    # 5. Today at its narrowest.
    cells.append(cell(5, "For comparison: today at its narrowest beside Audio (480).",
        "The strip sets today’s minimum width; the inspector’s fields have no names, so 0 / 0 / 0 and the three times "
        "read only by tooltip.",
        today(t, "narrow", 480, 200), 206, span=1))
    # 6. Translation mode, Original text panel closed: both fields in the Line editor.
    both = (strip(t, groups=("tags", "look", "tl"), folded=True)
            + field(syn(t, TEXT3), caption="Original text", h=33)
            + field("Sie hasst den Regen wirklich." + CARET, caption="Translated text", focus=True, h=33)
            + runstrip(t, (False, False))
            + '<div class="knote">' + icon("info", "sm mut") + "Karaoke line: the tags before its text are carried, "
              "the karaoke timing is not.</div>"
            + inspector_beside(t))
    cells.append(cell(6, "Panel closed: both fields here, named; its buttons join the strip as icons.",
        "Paste all, Paste the selected, Comment out original | Not confirmed, Tags from original become a strip group right "
        "after the colours; at 640 Text position and the custom buttons fold into More. At the Editing layout’s 200 px the content scrolls (wave 5’s overflow fix): "
        "left the dock, right the whole content.",
        f'<div class="pair2"><div>{tag("The dock")}{dock("Line editor", both, 640, 200, scroll=(0, .52))}</div>'
        f'<div>{tag("The whole content")}{dock("Line editor", both, 640)}</div></div>', 356))

    # 7. States.
    over = editor_default(t, 640, 222, text=r"She {\i1}really{\i0} {\b1}hates{\b0} the rain, and the wind, and the cold.",
                          cnt=counters(t, "51/43", "19/15", True, True, hover="CPS"), warn_times=True, pend_layer=True,
                          issue=issue(t, "Not applied: the start time is after the end time."),
                          L=dict(LINE, start="0:00:08.30", layer="2"))
    tip = tooltip("CPS 19/15", "Characters per second. Should not exceed 15 characters per second", 386, 80, 250)
    comment = editor_default(t, 640, 200, text="Kaito turns away.", focus=False,
                             cnt='<span class="counters"></span>',
                             L=dict(LINE, comment=True, actor="TL note", effect=""))
    cells.append(cell(7, "States: warnings in the warning colour with a mark; problems under the field; a Comment Line.",
        "Left: over 43 per line and 15 CPS (warning colour and ⚠, not colour alone), Start after End, a wheel-stepped Layer "
        "not yet sent (outlined), and the message right under the text instead of under the inspector. The counters keep "
        "today’s tooltips. Right: a Comment Line shows no counters, as today.",
        f'<div class="pair"><div style="position:relative">{over}{tip}</div><div>{comment}</div></div>', 236))

    # 8. Margins.
    mg = (f'<div class="mgdemo">{lab("Margins", margins(t))}<span class="dash">→</span>'
          f'{lab("Margins", margins(t, vals=("120", "0", "0")))}</div>'
          + tooltip("Left margin", "0: the Style’s (90)", 74, 46, 170))
    cells.append(cell(8, "Margins: one name, three marked fields; 0 shows the Style’s margin greyed.",
        "Three new 16 px marks (left, right, vertical) in K1’s style tell the fields apart without L/R/V letters. A margin "
        "of 0 (use the Style’s) shows the Style’s value in the secondary colour; the value stored and typed is still 0. The "
        "tooltips and accessible names stay Left margin, Right margin, Vertical margin.",
        mg, 120, span=1))

    return cells


def css(t):
    hc = t["hc"]
    sh = 0 if hc else .3
    return f"""
*{{box-sizing:border-box}}
html,body{{margin:0;background:{t['bg']};color:{t['text']};font-family:"Noto Sans","Noto Sans CJK JP","Segoe UI",sans-serif;font-size:13px;line-height:1.35}}
.top{{padding:14px 18px 2px;display:flex;align-items:baseline;gap:14px}}
.top h1{{font-size:15px;margin:0;font-weight:650}}
.top span{{color:{t['muted']};font-size:12px}}
.sheet{{display:grid;grid-template-columns:740px 560px;gap:6px 24px;padding:10px 18px 18px}}
.cell{{position:relative;padding-bottom:10px}}
.cap{{font-size:12.5px;font-weight:600;margin:8px 0 6px 2px}}
.cap b{{display:inline-flex;width:18px;height:18px;align-items:center;justify-content:center;border:1px solid {t['control']};border-radius:9px;font-size:11px;margin-right:6px}}
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;min-height:32px}}
.stage{{position:relative}}
.pair{{display:grid;grid-template-columns:640px 640px;gap:20px}}
.pair2{{display:grid;grid-template-columns:640px 640px;gap:20px;align-items:start}}
.trio{{display:grid;grid-template-columns:360px 360px 360px;gap:40px}}
.tag{{font-size:11px;color:{t['muted']};font-weight:600;margin:0 0 4px 2px;text-transform:uppercase;letter-spacing:.04em;height:14px}}
img.shot{{display:block;border:1px solid {t['line']}}}
.dock{{position:relative;background:{t['panel']};border:1px solid {t['line'] if hc else t['raised']};display:flex;flex-direction:column;overflow:hidden}}
.dhead{{height:35px;flex:none;display:flex;align-items:center;padding:0 8px 0 12px}}
.dhead .t{{font-weight:700;flex:1}}
.dbody{{position:relative;flex:1;min-height:0;overflow:hidden}}
.dcontent{{display:flex;flex-direction:column;gap:6px;padding:4px 12px 10px 12px;height:100%}}
.dbody.scrolled .dcontent{{height:auto;padding-right:16px}}
.sbar{{position:absolute;right:2px;top:2px;bottom:2px;width:6px;border-radius:3px;background:{t['field']};border:1px solid {t['line']}}}
.sthumb{{position:absolute;left:-1px;right:-1px;border-radius:3px;background:{t['control'] if hc else t['muted']};opacity:{1 if hc else .6}}}
.grow{{flex:1;min-width:0}}
.sep{{width:1px;height:16px;background:{t['line']};margin:0 5px;flex:none}}
.sep.thin{{margin:0 3px}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none;color:{t['text']}}}
.ic svg{{width:16px;height:16px}}
.ic.sm,.ic.sm svg{{width:12px;height:12px}}
.ic.xs,.ic.xs svg{{width:10px;height:10px}}
.ic.mut{{color:{t['muted']}}}
svg #accent{{color:{t['accent']}}}
.b{{height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:3px;color:{t['text']};flex:none}}
.b.dd{{gap:1px;padding:0 3px 0 4px}}
.b.hov{{background:{t['field']};border-color:{t['control']}}}
.b.on{{background:{t['select']};border-color:{t['accent'] if hc else t['control']}}}
.b.tg{{background:{t['select']};border-color:{t['accent'] if hc else 'transparent'};box-shadow:inset 0 -2px 0 {t['accent']}}}
.strip{{display:flex;align-items:center;gap:1px;height:28px;flex:none}}
.counters{{display:inline-flex;gap:12px;font-size:12px;font-variant-numeric:tabular-nums;white-space:nowrap;padding-left:8px}}
.cn{{display:inline-flex;align-items:center;gap:4px;padding:1px 3px;border-radius:2px}}
.cn.hov{{background:{t['raised']}}}
.cl{{color:{t['muted']}}}
.cm{{color:{t['muted']};margin-left:-3px}}
.cn.warn .cv,.cn.warn .ic{{color:{t['warning']};font-weight:{700 if hc else 600}}}
.fcap{{font-size:11.5px;color:{t['muted']};margin:0 0 -3px 1px}}
.fld{{background:{t['field']};border:1px solid {t['control']};border-radius:3px;padding:5px 8px;font-size:14px;flex:none;position:relative;overflow:hidden}}
.fld.fill{{flex:1;min-height:33px}}
.fld.focus{{border-color:{t['accent']};box-shadow:0 0 0 1px {t['accent']}}}
.caret{{display:inline-block;width:1px;height:16px;background:{t['text']};vertical-align:-3px;margin-left:1px}}
.selx{{background:{t['select']};outline:1px solid {t['accent']}}}
.issue{{display:flex;align-items:center;gap:6px;font-size:12px;margin:-2px 0 0 1px;flex:none}}
.issue.danger{{color:{t['danger']}}}
.issue.danger .ic{{color:{t['danger']}}}
.insp{{display:flex;flex-direction:column;gap:6px;flex:none}}
.irow{{display:flex;align-items:center;gap:10px}}
.lf{{display:inline-flex;align-items:center;gap:5px;flex:none}}
.lf.grow{{flex:1;min-width:0}}
.ll{{color:{t['muted']};font-size:12px;white-space:nowrap}}
.in{{height:26px;display:inline-flex;align-items:center;gap:5px;padding:0 6px;background:{t['field']};border:1px solid {t['control']};border-radius:3px;font-size:12.5px;white-space:nowrap;overflow:hidden;flex:none;position:relative}}
.in.grow{{flex:1;min-width:60px}}
.in.dd{{justify-content:space-between}}
.in.tn{{font-variant-numeric:tabular-nums}}
.in.pend{{outline:1px solid {t['warning']};outline-offset:-2px}}
.in.mg{{width:52px;padding:0 4px;gap:3px;font-variant-numeric:tabular-nums}}
.in.mg + .in.mg{{margin-left:-2px}}
.sv{{color:{t['muted']};{'font-style:italic;' if hc else ''}}}
.ck{{display:inline-flex;align-items:center;gap:6px;font-size:12.5px;flex:none}}
.chk{{width:14px;height:14px;border:1px solid {t['control']};border-radius:2px;display:inline-flex;align-items:center;justify-content:center;flex:none}}
.chk.on{{background:{t['accent']};border-color:{t['accent']};color:{t['onaccent']}}}
.chk svg{{width:12px;height:12px}}
.agrid{{display:grid;grid-template-columns:1fr 1fr;gap:6px 8px;flex:none}}
.ac.span{{grid-column:span 2}}
.al{{color:{t['muted']};font-size:11.5px;margin:0 0 2px 1px;height:15px}}
.actl{{display:flex;align-items:center;gap:2px;min-height:26px}}
.actl .in.mg{{width:auto;flex:1}}
.runs{{display:flex;flex-wrap:wrap;gap:4px;flex:none}}
.rchip{{display:inline-flex;align-items:center;gap:4px;height:22px;padding:0 6px 0 4px;border:1px solid {t['line']};border-radius:3px;font-size:12.5px}}
.rchip.placed{{background:{t['select'] if hc else 'color-mix(in srgb,'+t['accent']+' 20%,transparent)'};border-color:{t['accent']}}}
.rnum{{min-width:15px;height:15px;border-radius:8px;border:1px solid {t['accent']};font-size:10px;display:inline-flex;align-items:center;justify-content:center;padding:0 3px}}
.rchip.placed .rnum{{background:{t['accent']};color:{t['onaccent']}}}
.rkind{{color:{t['muted']};font-size:11.5px}}
.sw{{width:10px;height:10px;border:1px solid {t['line']}}}
.knote{{display:flex;align-items:center;gap:6px;color:{t['muted']};font-size:12px;flex:none}}
.tip{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};padding:4px 8px;font-size:12px;border-radius:2px;box-shadow:0 2px 8px rgba(0,0,0,{sh})}}
.tsub{{color:{t['muted']};font-size:11px}}
.menu{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{sh})}}
.mi{{display:flex;align-items:center;gap:8px;height:28px;padding:0 12px 0 8px}}
.mi .mt{{flex:1;white-space:nowrap}}
.mi .mk{{color:{t['muted']};font-size:12px}}
.mi.hov{{background:{t['select']};box-shadow:inset 3px 0 0 {t['accent']}}}
.mi.sub .mk::after{{content:"›";font-size:15px}}
.cb{{width:12px;display:inline-flex}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
.mgdemo{{display:flex;align-items:center;gap:14px;padding:10px;background:{t['panel']};border:1px solid {t['line'] if hc else t['raised']};width:max-content}}
.dash{{color:{t['muted']}}}
"""


def page(t):
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>LE1 Line editor — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>LE1 Line editor layout — {t["name"]}</h1>'
            f'<span>Mockup for #233. Theme roles from theme.cpp, icons from the set; new: three margin marks. '
            f'“Today” shots are the running app (qt + TM1 part 2).</span></div>'
            f'<div class="sheet">{"".join(sheet(t))}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"le1-{key}.html").write_text(page(t))
print("wrote", ", ".join(f"le1-{k}.html" for k in THEMES))
