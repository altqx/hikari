#!/usr/bin/env python3
"""TM1 #232 Translation mode mockup: one HTML sheet per theme, screenshotted by shoot.sh.

usage: gen.py <src/ui/icons> <out dir>
"""
import pathlib
import random
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# theme.cpp baseRoles + the default accent picks (as the AU1 mockup).
THEMES = {
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", disabled="#75818D", hc=False, video="#0B0D10", wave="#5E6873", danger="#F28B82"),
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", disabled="#74808B", hc=False, video="#1C2026", wave="#5B6774", danger="#B3261E"),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E", hc=True, video="#000000",
                    wave="#303030", danger="#B00000"),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", disabled="#8C8C8C", hc=True, video="#000000",
                    wave="#C8C8C8", danger="#FF6060"),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Icons this card would add, drawn in the set's 16 px, 1 px stroke style.
NEW_ICONS = {
    # Paste the selected: the clipboard with a selected run of text.
    "paste-selection-translation": SVG_HEAD + '<path d="M5.5 2.5h-2a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1v-10a1 1 0 0 0-1-1h-2"/>'
    '<rect x="5.5" y="1.5" width="5" height="2" rx="0.5"/><path d="M5.5 7.5h5M5.5 12.5h3"/>'
    '<g id="accent"><rect x="5" y="9" width="6" height="2.25" fill="currentColor" stroke="none"/></g></svg>',
    # Comment out original: the text inside braces.
    "comment-original": SVG_HEAD + '<path d="M5 2.5c-1.25 0-1.75.5-1.75 1.75v2c0 .9-.5 1.5-1.25 1.75.75.25 1.25.85 1.25 1.75v2c0 1.25.5 1.75 1.75 1.75'
    'M11 2.5c1.25 0 1.75.5 1.75 1.75v2c0 .9.5 1.5 1.25 1.75-.75.25-1.25.85-1.25 1.75v2c0 1.25-.5 1.75-1.75 1.75"/>'
    '<g id="accent"><path d="M6 6.5h4M6 9.5h3"/></g></svg>',
    # Not confirmed: a flag with a question.
    "unconfirmed": SVG_HEAD + '<path d="M3.5 14.5v-13M3.5 2.5h9l-2 3 2 3h-9"/>'
    '<g id="accent"><path d="M6.6 4.3a1.4 1.4 0 1 1 1.9 1.3c-.4.2-.5.4-.5.8"/><circle cx="8" cy="7.4" r=".35" fill="currentColor"/></g></svg>',
    # Moving tags: an override block moving across.
    "moving-tags": SVG_HEAD + '<path d="M3.5 3.5c-1 0-1.5.5-1.5 1.5v1c0 .75-.4 1.2-1 1.5.6.3 1 .75 1 1.5v1c0 1 .5 1.5 1.5 1.5'
    'M6.5 3.5c1 0 1.5.5 1.5 1.5v1c0 .75.4 1.2 1 1.5-.6.3-1 .75-1 1.5v1c0 1-.5 1.5-1.5 1.5"/>'
    '<g id="accent"><path d="M10 8h5M13 6l2 2-2 2"/></g></svg>',
    # Translation mode: the original line over its translation.
    "translation-mode": SVG_HEAD + '<path d="M1.5 3.5h8M1.5 6h5"/><g id="accent"><path d="M1.5 10.5h8M1.5 13h5"/>'
    '<path d="M12.5 3v9M11 10.5l1.5 1.5 1.5-1.5"/></g></svg>',
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "chev": SVG_HEAD + '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/></svg>',
    "x": SVG_HEAD + '<path d="M4.5 4.5l7 7M11.5 4.5l-7 7"/></svg>',
    "pencil": SVG_HEAD + '<path d="M10.5 2.5l3 3-8 8h-3v-3z"/></svg>',
}


def svg(role):
    if role in NEW_ICONS:
        return NEW_ICONS[role]
    return (ICONS / f"{role}.svg").read_text().strip()


def icon(role, cls=""):
    return f'<span class="ic {cls}">{svg(role)}</span>'


def btn(role, cls="", title=""):
    return f'<span class="b {cls}" title="{title}">{icon(role)}</span>'


# The sample Document: Original text, Translated text, unconfirmed.
LINES = [
    ("0:00:01.00", "0:00:03.20", "The gate opens at dawn.", "門は夜明けに開く。", False),
    ("0:00:03.40", "0:00:05.10", "Stay close to the wall.", "壁から離れるな。", False),
    ("0:00:05.30", "0:00:07.90", "{\\i1}Did you hear that?{\\i0}", "{\\i1}今の、聞こえた？{\\i0}", True),
    ("0:00:08.10", "0:00:10.00", "Nothing. Only the wind.", "", False),
    ("0:00:10.20", "0:00:12.60", "Then why are you shaking?", "", False),
    ("0:00:12.80", "0:00:14.40", "Keep walking.", "", False),
    ("0:00:14.60", "0:00:16.90", "The harbour is past the bridge.", "", False),
]
CUR = 2


def menubar(open_menu=None):
    items = ["File", "Edit", "Automation", "Video", "Audio", "View", "Subtitles", "Help"]
    out = []
    for it in items:
        cls = " open" if it == open_menu else ""
        out.append(f'<span class="mbi{cls}"><u>{it[0]}</u>{it[1:]}</span>')
    return f'<div class="mbar">{"".join(out)}</div>'


def doctabs(name="harbour-ep01.ass"):
    return (f'<div class="dtabs"><span class="dtab cur">{name}{icon("x", "sm")}</span>'
            f'<span class="dplus">{icon("add")}</span></div>')


def head_title(title):
    return (f'<div class="dhead"><span class="t">{title}</span>{icon("panel-menu", "mut")}</div>')


def head_tabs(title, tools="", focus=False):
    mark = '<span class="fmark"></span>' if focus else ""
    return (f'<div class="dhead tabs"><span class="dt sel">{title}<span class="dmore">{icon("panel-menu")}</span></span>'
            f'<span class="grow"></span><span class="tslot">{tools}</span>{mark}</div>')


def original_tools(unconfirmed=False, moving=True, hover=None):
    def b(role, name, cls=""):
        h = " hov" if hover == role else ""
        return btn(role, cls + h, name)
    return (b("paste-translation", "Paste all")
            + b("paste-selection-translation", "Paste the selected")
            + b("comment-original", "Comment out original")
            + '<span class="sep"></span>'
            + b("unconfirmed", "Not confirmed", "tg" if unconfirmed else "")
            + b("moving-tags", "Moving tags", "tg" if moving else ""))


def field(text, h, caret=False, cls="", placeholder=""):
    c = '<span class="caret"></span>' if caret else ""
    body = text if text else f'<span class="ph">{placeholder}</span>'
    return f'<div class="fld {cls}" style="height:{h}px">{body}{c}</div>'


def tagstrip():
    roles = ["tag-bold", "tag-italic", "tag-underline", "tag-strikeout", "tag-font",
             "colour-primary", "colour-secondary", "colour-outline", "colour-shadow", "tag-list"]
    return '<div class="tstrip">' + "".join(btn(r) for r in roles) + btn("add") + "</div>"


def inspector():
    return ('<div class="insp">'
            '<div class="irow"><span class="in w30">0</span><span class="in w84">0:00:05.30</span>'
            '<span class="in w84">0:00:07.90</span><span class="in w84">0:00:02.60</span>'
            f'<span class="in dd grow">Default{icon("chev", "sm")}</span>{icon("pencil", "mut")}</div>'
            '<div class="irow"><span class="chk"></span><span class="lbl">Comment</span>'
            f'<span class="in dd w150 ph">Actor{icon("chev", "sm")}</span><span class="in w38">0</span>'
            '<span class="in w38">0</span><span class="in w38">0</span>'
            f'<span class="in dd grow ph">Effect{icon("chev", "sm")}</span></div></div>')


def line_editor(text_h=70, show_check=False, focus=False, tl=True):
    o, t = LINES[CUR][2], LINES[CUR][3]
    check = ('<div class="crow"><span class="chk on">' + svg("check") + '</span><span>Translator mode</span></div>'
             if show_check else "")
    label = "Translated text" if tl else "Line text"
    return (f'<div class="dock" style="flex:1">{head_title("Line editor")}'
            f'<div class="dbody pad">{tagstrip()}{check}'
            '<div class="counters">Wraps: 1/14&nbsp;&nbsp;Characters per second: 6&lt;=15</div>'
            f'{field(t if tl else o, text_h, caret=True, cls="focus" if focus else "", placeholder=label)}'
            f'{inspector()}</div></div>')


def original_panel(h, unconfirmed=False, hover=None, focus=False):
    o = LINES[CUR][2]
    return (f'<div class="dock" style="height:{h}px">{head_tabs("Original text", original_tools(unconfirmed, hover=hover), focus)}'
            f'<div class="dbody pad">{field(o, h - 35 - 16)}</div></div>')


def video_panel(w):
    rail = ["tool-crosshair", "tool-move", "tool-move-all", "tool-scale", "tool-rotate-z", "tool-rotate-xy",
            "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-position"]
    r = "".join(btn(x, "on" if i == 0 else "") for i, x in enumerate(rail))
    return (f'<div class="dock" style="width:{w}px">{head_title("Video")}'
            f'<div class="dbody vid"><div class="vrow"><div class="rail">{r}</div>'
            f'<div class="vframe"><div class="scene"></div><div class="sub">{LINES[CUR][3].replace("{\\i1}", "<i>").replace("{\\i0}", "</i>")}</div></div></div>'
            '<div class="seek"><span class="knob" style="left:22%"></span></div>'
            f'<div class="vtrans">{btn("audio-previous-line")}{btn("media-play")}{btn("play-line")}{btn("media-stop")}{btn("audio-next-line")}'
            '<span class="grow"></span><span class="tc">0:00:06.14 / 0:23:40.00</span><span class="grow"></span>'
            f'{btn("volume")}</div></div></div>')


def waveform(w, h, t):
    rnd = random.Random(7)
    bars = []
    sel0, sel1 = int(w * .38), int(w * .62)
    if True:
        bars.append(f'<rect x="{sel0}" y="0" width="{sel1 - sel0}" height="{h}" fill="{t["accent"]}" opacity="0.18"/>')
    for x in range(0, w, 3):
        a = abs(rnd.gauss(0, .28)) * (1.2 if sel0 < x < sel1 else .8)
        bh = max(2, min(h - 6, a * h))
        col = t["accent"] if sel0 < x < sel1 else t["wave"]
        bars.append(f'<rect x="{x}" y="{(h - bh) / 2:.1f}" width="2" height="{bh:.1f}" fill="{col}"/>')
    bars.append(f'<rect x="{sel0}" y="0" width="1.5" height="{h}" fill="#C0392B"/>')
    bars.append(f'<rect x="{sel1}" y="0" width="1.5" height="{h}" fill="#C0392B"/>')
    return f'<svg width="{w}" height="{h}" style="display:block">{"".join(bars)}</svg>'


def audio_panel(w, h, t):
    return (f'<div class="dock" style="height:{h}px">{head_title("Audio")}'
            f'<div class="dbody"><div class="awave">{waveform(w - 2, h - 35 - 34, t)}</div>'
            f'<div class="atrans">{btn("audio-previous-line")}{btn("audio-play")}{btn("play-line")}{btn("media-stop")}{btn("audio-next-line")}'
            f'<span class="sep"></span>{btn("lead-in")}{btn("lead-out")}{btn("commit")}<span class="grow"></span>{btn("auto-scroll")}{btn("zoom")}{btn("volume")}</div></div></div>')


def grid_panel(h, cols_tl=False):
    rows = []
    hdr = "<tr><th>#</th><th>Start</th><th>End</th><th>Style</th><th>CPS</th><th>Wraps</th><th class='txt'>Text</th></tr>"
    for i, (s, e, o, tl, unc) in enumerate(LINES):
        txt = tl if tl else o
        cls = ' class="cur"' if i == CUR else ""
        style = "TLMode Original" if False else "Default"
        rows.append(f'<tr{cls}><td>{i + 1}</td><td>{s}</td><td>{e}</td><td>{style}</td><td>{len(txt) // 2}</td>'
                    f'<td>{len(txt)}</td><td class="txt">{txt}</td></tr>')
    return (f'<div class="dock" style="height:{h}px">{head_tabs("Grid", "")}'
            f'<div class="dbody"><table class="grid">{hdr}{"".join(rows)}</table></div></div>')


def statusbar(preset, open_switch=False):
    cls = " open" if open_switch else ""
    return (f'<div class="sbar"><span>1 line selected</span><span>1280 × 720</span><span class="grow"></span>'
            f'<span class="lsw{cls}">{icon("layout")}<span>{preset}</span>{icon("chev", "sm")}</span></div>')


def window(t, preset="Translation", mode=True, w=1280, h=800, open_menu=None, extra="", unconfirmed=False,
           hover=None, show_check=False, dim=False, switch_open=False):
    top_h = 500
    grid_h = h - 26 - 32 - 30 - top_h - 2
    vw = int(w * .44)
    rw = w - vw - 4
    right = []
    if preset == "Editing":
        right.append(audio_panel(rw, 160, t))
    if mode:
        right.append(original_panel(150 if preset == "Translation" else 118, unconfirmed, hover))
    right.append(line_editor(text_h=72 if preset == "Translation" else 52, show_check=show_check, focus=True, tl=mode))
    body = (f'<div class="area"><div class="row" style="height:{top_h}px">{video_panel(vw)}'
            f'<div class="col" style="width:{rw}px">{"".join(right)}</div></div>'
            f'{grid_panel(grid_h)}</div>')
    d = '<div class="dim"></div>' if dim else ""
    return (f'<div class="win" style="width:{w}px;height:{h}px">{menubar(open_menu)}{doctabs()}{body}'
            f'{statusbar(preset, switch_open)}{d}{extra}</div>')


def dialog(title, text, detail, buttons, x, y, w=440):
    bs = "".join(f'<span class="pbtn {c}">{b}</span>' for b, c in buttons)
    return (f'<div class="dlg" style="left:{x}px;top:{y}px;width:{w}px">'
            f'<div class="dlgt">{title}{icon("x", "sm mut")}</div>'
            f'<div class="dlgb"><div class="dlgm">{text}</div><div class="dlgd">{detail}</div></div>'
            f'<div class="dlgf"><span class="grow"></span>{bs}</div></div>')


def menu(items, x, y, w=330, cls=""):
    out = []
    for it in items:
        if it == "-":
            out.append('<div class="msep"></div>')
            continue
        role, text, key, state = it
        c = ' ' + state if state else ""
        chk = svg("check") if "checked" in (state or "") else ""
        ic = icon(role) if role else '<span class="ic"></span>'
        out.append(f'<div class="mi{c}"><span class="cb">{chk}</span>{ic}<span class="mt">{text}</span>'
                   f'<span class="mk2">{key}</span></div>')
    return f'<div class="menu {cls}" style="left:{x}px;top:{y}px;width:{w}px">{"".join(out)}</div>'


def cell(n, cap, note, stage, h, wide=False):
    span = ' style="grid-column:1 / span 2"' if wide else ""
    return (f'<div class="cell"{span}><div class="cap"><b>{n}</b>{cap}</div><div class="note">{note}</div>'
            f'<div class="stage" style="height:{h}px">{stage}</div></div>')


def sheet(key, t):
    cells = []
    # 1. The Translation preset with the mode on.
    cells.append(cell(1, "Translation preset, mode on: Original text is a panel; the Line editor edits the Translated text.",
        "Video left, Original text above the Line editor, Grid below (Audio closed, as the preset is today). The panel's header "
        "holds Paste all, Paste the selected, Comment out original | Not confirmed, Moving tags as icon buttons with their names "
        "on hover. No Translator mode check box: the Line editor goes straight from the tag strip to the counters.",
        window(t, "Translation", True, unconfirmed=True), 800, wide=True))

    # 2. The one-time question.
    q = dialog("Translation mode", "Turn on Translation mode for this file?",
               "Each Line keeps its text as the Original text and gets a Translated text beside it. "
               "The file saves both until you turn the mode off.",
               [("Not now", ""), ("Turn on", "acc")], 100, 150)
    small = (f'<div class="scaled">{window(t, "Translation", False, dim=True)}</div>{q}')
    cells.append(cell(2, "Choosing Translation on a file without TLMode asks once.",
        "From View &gt; Layout or the layout switcher. Window-modal, Turn on is the default (Enter). Turn on: one undo step "
        "(“Turning on translator mode”) and the Original text panel appears. Not now: the tab keeps the arrangement without "
        "the mode and the question isn’t asked again in this tab. SRT and other line formats get the arrangement only.",
        small, 530))

    # 3. Subtitles menu + today's confirmation.
    items = [("shift-times", "Shift times...", "", ""), ("shift-times-run", "Shift times / run time post processor", "", ""),
             ("styles", "Style manager", "", ""), ("script-properties", "ASS file properties", "", ""),
             ("font-collector", "Font collector", "", ""), ("resample", "Resample subtitles", "", ""),
             ("spellchecker", "Check spelling", "", ""), "-",
             ("translation-mode", "Translation mode", "", "checked hov"), "-",
             ("hide-tags", "Hide tags", "", ""), ("hide-tags", "Hide tags in the line editor", "", "")]
    bar = f'<div class="mbfrag">{menubar("Subtitles")}</div>'
    m = menu(items, 205, 26, 320)
    c = dialog("Confirmation", "Are you sure you want to turn off translation mode?",
               "Foreign language text will be deleted.", [("Yes", ""), ("No", "acc")], 200, 380, 420)
    cells.append(cell(3, "Subtitles › Translation mode: bindable; turning it off asks as today.",
        "A checked item with its own hotkey (none by default; Shift+click maps it). Checking it turns the mode on at once; "
        "unchecking asks today’s question, No by default. Turning the mode off closes the Original text panel; the tab keeps "
        "its arrangement.",
        bar + m + c, 530))

    # 4. Editing preset with the mode still on.
    cells.append(cell(4, "Another preset keeps the mode on: here Editing, with Original text above the Line editor.",
        "The Original text panel follows the Document’s mode the way the Reference tray follows a comparison: it shows in any "
        "arrangement while the mode is on, where that arrangement last had it, else above the Line editor; it can be moved, "
        "tabbed or floated like any panel.",
        window(t, "Editing", True), 800, wide=True))

    # 5. Toolbar details: hover + narrow dock overflow.
    o = LINES[CUR][2]
    wide_panel = (f'<div class="dock" style="width:640px;height:120px">{head_tabs("Original text", original_tools(True, hover="unconfirmed"))}'
                  f'<div class="dbody pad">{field(o, 66)}</div></div>'
                  '<div class="tip" style="left:448px;top:38px">Not confirmed<div class="dim">Marks the selected Lines for review (Unconfirmed)</div></div>')
    narrow = (f'<div class="dock" style="width:300px;height:120px;margin-top:96px">'
              f'{head_tabs("Original text", btn("menu-more", "on", "More"))}'
              f'<div class="dbody pad">{field(o, 66)}</div></div>')
    over = menu([("paste-translation", "Paste all", "", ""), ("paste-selection-translation", "Paste the selected", "", ""),
                 ("comment-original", "Comment out original", "", ""), "-",
                 ("unconfirmed", "Not confirmed", "", "checked"), ("moving-tags", "Moving tags", "", "checked hov")],
                60, 250, 236)
    cells.append(cell(5, "Header controls: names on hover; a narrow dock folds them into a menu.",
        "Toggles show their state as a filled face with an accent edge (not colour alone). Below the width the five buttons "
        "need beside the tab, they move into a » menu in the same order, with check marks for the two toggles.",
        wide_panel + narrow + over, 470))

    # 6. Line editor with the mode off, before/after.
    before = (f'<div class="dock" style="width:640px">{head_title("Line editor")}<div class="dbody pad">{tagstrip()}'
              '<div class="crow"><span class="chk"></span><span>Translator mode</span></div>'
              '<div class="counters">Wraps: 1/23&nbsp;&nbsp;Characters per second: 9&lt;=15</div>'
              f'{field(LINES[CUR][2], 44)}</div></div>')
    after = (f'<div class="dock" style="width:640px;margin-top:22px">{head_title("Line editor")}<div class="dbody pad">{tagstrip()}'
             '<div class="counters">Wraps: 1/23&nbsp;&nbsp;Characters per second: 9&lt;=15</div>'
             f'{field(LINES[CUR][2], 44)}</div></div>')
    cells.append(cell(6, "The Translator mode check box is gone.",
        "Top: today, mode off. Bottom: after. The mode is reached through the Translation preset and Subtitles › Translation "
        "mode; the Line editor has one text field when it is off. LE1 redesigns the rest of the Line editor.",
        '<div class="tag">Today</div>' + before + '<div class="tag">After</div>' + after, 470))
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
.sheet{{display:grid;grid-template-columns:640px 640px;gap:4px 24px;padding:10px 18px 18px}}
.cell{{position:relative;padding-bottom:10px}}
.cap{{font-size:12.5px;font-weight:600;margin:8px 0 6px 2px}}
.cap b{{display:inline-flex;width:18px;height:18px;align-items:center;justify-content:center;border:1px solid {t['control']};border-radius:9px;font-size:11px;margin-right:6px}}
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;max-width:1280px;min-height:32px}}
.stage{{position:relative}}
.win{{position:relative;background:{t['bg']};border:1px solid {t['line']};overflow:hidden;display:flex;flex-direction:column}}
.mbar{{height:26px;display:flex;align-items:center;gap:2px;padding:0 4px;background:{t['bg']}}}
.mbi{{padding:3px 6px;border-radius:2px}}
.mbi u{{text-decoration:none}}
.mbi.open{{background:{t['select']};{'outline:1px solid '+t['accent']+';' if hc else ''}}}
.mbfrag{{width:640px;border:1px solid {t['line']};border-bottom:none}}
.dtabs{{height:32px;display:flex;align-items:center;gap:6px;padding:0 4px;background:{t['bg']}}}
.dtab{{display:inline-flex;align-items:center;gap:8px;height:24px;padding:0 6px 0 8px;border:1px solid {t['control']};border-radius:3px;background:{t['panel']};font-size:12.5px}}
.dtab.cur{{border-color:{t['accent']}}}
.dplus{{color:{t['muted']};display:inline-flex}}
.area{{flex:1;display:flex;flex-direction:column;gap:2px;padding:0 2px}}
.row{{display:flex;gap:4px}}
.col{{display:flex;flex-direction:column;gap:2px}}
.dock{{position:relative;background:{t['panel']};border:1px solid {t['line'] if hc else t['raised']};display:flex;flex-direction:column;overflow:hidden}}
.dhead{{height:35px;flex:none;display:flex;align-items:center;padding:0 8px 0 12px;position:relative}}
.dhead .t{{font-weight:700;flex:1}}
.dhead.tabs{{padding:0;background:{t['raised']};border-bottom:1px solid {t['line']}}}
.dt{{height:35px;display:inline-flex;align-items:center;gap:6px;padding:0 6px 0 10px;border-right:1px solid {t['line']}}}
.dt.sel{{background:{t['panel']};font-weight:700;margin-bottom:-1px;height:36px}}
.dmore{{display:inline-flex;width:20px;height:20px;align-items:center;justify-content:center}}
.tslot{{display:flex;align-items:center;gap:2px;padding-right:6px}}
.fmark{{position:absolute;right:0;top:0;width:0;height:0;border-top:10px solid {t['accent']};border-left:10px solid transparent}}
.dbody{{position:relative;flex:1;display:flex;flex-direction:column;min-height:0}}
.dbody.pad{{padding:8px 8px 8px 8px;gap:6px}}
.grow{{flex:1}}
.sep{{width:1px;height:18px;background:{t['line']};margin:0 4px}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none;color:{t['text']}}}
.ic svg{{width:16px;height:16px}}
.ic.sm,.ic.sm svg{{width:12px;height:12px}}
.ic.mut{{color:{t['muted']}}}
svg #accent{{color:{t['accent']}}}
.b{{height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:3px;color:{t['text']}}}
.b.hov{{background:{t['field']};border-color:{t['control']}}}
.b.on{{background:{t['select']};border-color:{t['accent'] if hc else t['control']}}}
.b.tg{{background:{t['select']};border-color:{t['accent'] if hc else 'transparent'};box-shadow:inset 0 -2px 0 {t['accent']}}}
.b.tg.hov{{border-color:{t['control']}}}
.tstrip{{display:flex;gap:2px;align-items:center}}
.crow{{display:flex;align-items:center;gap:8px;height:22px}}
.chk{{width:14px;height:14px;border:1px solid {t['control']};border-radius:2px;display:inline-flex;align-items:center;justify-content:center;flex:none}}
.chk.on{{background:{t['accent']};border-color:{t['accent']};color:{t['onaccent']}}}
.chk svg{{width:12px;height:12px}}
.counters{{font-size:12.5px;height:18px}}
.fld{{background:{t['field']};border:1px solid {t['control']};border-radius:3px;padding:6px 9px;font-size:14px;flex:none;position:relative}}
.fld.focus{{border-color:{t['accent']};box-shadow:0 0 0 1px {t['accent']}}}
.fld .ph{{color:{t['muted']}}}
.caret{{display:inline-block;width:1px;height:16px;background:{t['text']};vertical-align:-3px;margin-left:1px}}
.insp{{display:flex;flex-direction:column;gap:6px}}
.irow{{display:flex;gap:4px;align-items:center}}
.in{{height:26px;display:inline-flex;align-items:center;gap:4px;padding:0 6px;background:{t['field']};border:1px solid {t['control']};border-radius:3px;font-size:12.5px;font-variant-numeric:tabular-nums}}
.in.dd{{justify-content:space-between}}
.in.ph{{color:{t['muted']}}}
.w30{{width:34px}}.w84{{width:84px}}.w150{{width:150px}}.w38{{width:40px}}
.lbl{{margin-right:6px}}
.vid{{padding:0}}
.vrow{{flex:1;display:flex;min-height:0}}
.rail{{width:34px;display:flex;flex-direction:column;gap:2px;align-items:center;padding:4px 0;border-right:1px solid {t['line'] if hc else t['raised']}}}
.vframe{{flex:1;margin:10px;position:relative;background:{t['video']};display:flex;align-items:flex-end;justify-content:center;overflow:hidden}}
.scene{{position:absolute;inset:0;background:{'none' if hc else 'linear-gradient(180deg,#25303d 0%,#3b4654 45%,#59524a 70%,#2a2622 100%)'}}}
.sub{{position:relative;margin-bottom:22px;color:#fff;font-size:22px;text-shadow:0 0 3px #000,0 0 2px #000,1px 1px 2px #000}}
.seek{{height:6px;margin:0 10px 0 44px;background:{t['field']};border:1px solid {t['line']};border-radius:3px;position:relative}}
.seek .knob{{position:absolute;top:-5px;width:14px;height:14px;margin-left:-7px;border-radius:7px;background:{t['panel']};border:1px solid {t['control']}}}
.vtrans,.atrans{{height:34px;display:flex;align-items:center;gap:2px;padding:0 6px}}
.tc{{font-size:12px;font-variant-numeric:tabular-nums;color:{t['muted']}}}
.awave{{flex:1;background:{t['field']};overflow:hidden}}
table.grid{{border-collapse:collapse;width:100%;font-size:12.5px}}
.grid th{{text-align:left;font-weight:400;color:{t['muted']};padding:3px 8px;background:{t['raised']};border-bottom:1px solid {t['line']}}}
.grid td{{padding:2px 8px;white-space:nowrap;font-variant-numeric:tabular-nums;border-bottom:1px solid {t['raised'] if not hc else t['disabled']}}}
.grid td.txt,.grid th.txt{{width:60%}}
.grid tr.cur td{{background:{t['select']}}}
.grid tr.cur td:first-child{{box-shadow:inset 3px 0 0 {t['accent']}}}
.sbar{{height:30px;display:flex;align-items:center;gap:16px;padding:0 10px 0 240px;font-size:12px;color:{t['text']};background:{t['bg']}}}
.lsw{{display:inline-flex;align-items:center;gap:6px;padding:2px 6px;border-radius:3px}}
.lsw.open{{background:{t['select']}}}
.dim{{position:absolute;inset:0;background:rgba(0,0,0,{.0 if hc else .45});{'outline:none' if not hc else ''}}}
.scaled{{width:640px;height:400px;overflow:hidden;border:1px solid {t['line']}}}
.scaled .win{{transform:scale(.5);transform-origin:0 0}}
.dlg{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:4px;box-shadow:0 8px 28px rgba(0,0,0,{sh + .1})}}
.dlgt{{height:32px;display:flex;align-items:center;justify-content:space-between;padding:0 10px 0 14px;font-size:12.5px;color:{t['muted']};border-bottom:1px solid {t['line'] if hc else t['raised']}}}
.dlgb{{padding:16px 16px 4px}}
.dlgm{{font-size:14px;font-weight:650;margin-bottom:10px}}
.dlgd{{font-size:12.5px;color:{t['text']};max-width:420px}}
.dlgf{{display:flex;gap:12px;padding:16px}}
.pbtn{{display:inline-flex;align-items:center;justify-content:center;min-width:88px;height:28px;padding:0 14px;border:1px solid {t['control']};border-radius:3px;background:{t['raised']};font-size:12.5px}}
.pbtn.acc{{background:{t['accent']};border-color:{t['accent']};color:{t['onaccent']};font-weight:600;{'outline:2px solid '+t['text']+';outline-offset:1px;' if hc else ''}}}
.menu{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{sh})}}
.mi{{display:flex;align-items:center;gap:8px;height:28px;padding:0 12px 0 8px}}
.mi .mt{{flex:1;white-space:nowrap}}
.mi .mk2{{color:{t['muted']};font-size:12px;white-space:nowrap}}
.mi.hov{{background:{t['select']};box-shadow:inset 3px 0 0 {t['accent']}}}
.cb{{width:14px;height:14px;display:inline-flex;align-items:center;justify-content:center;flex:none;color:{t['text']}}}
.cb svg{{width:14px;height:14px}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
.tip{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};padding:4px 8px;font-size:12px;border-radius:2px;max-width:320px;box-shadow:0 2px 8px rgba(0,0,0,{sh})}}
.tip .dim{{position:static;background:none;color:{t['muted']};font-size:11px}}
.tag{{font-size:11px;color:{t['muted']};font-weight:600;margin:0 0 4px 2px;text-transform:uppercase;letter-spacing:.04em}}
"""


def page(key, t):
    cells = sheet(key, t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>TM1 Translation mode — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>TM1 Translation mode — {t["name"]}</h1>'
            f'<span>Mockup for #232. Real icon set; new icons: Paste the selected, Comment out original, Not confirmed, '
            f'Moving tags, Translation mode.</span></div>'
            f'<div class="sheet">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"tm1-{key}.html").write_text(page(key, t))
print("wrote", ", ".join(f"tm1-{k}.html" for k in THEMES))
