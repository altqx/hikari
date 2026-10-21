#!/usr/bin/env python3
"""TP1 #274 Typesetting preset mockup: one HTML sheet per theme, screenshotted by shoot.sh.

usage: gen.py <src/ui/icons> <out dir>
"""
import pathlib
import re
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# theme.cpp baseRoles + the default accent picks (as the TM1 and VT1 mockups).
THEMES = {
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", disabled="#75818D", checked="#3A4450", warning="#E0A030", hc=False),
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", disabled="#74808B", checked="#D3D9DE", warning="#8A5A00", hc=False),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E", checked="#C9DAF8", warning="#6B4500",
                    hc=True),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", disabled="#8C8C8C", checked="#253F60", warning="#FFD54A",
                    hc=True),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Glyphs the set has no role for; drawn in its 16 px, 1 px stroke style.
NEW_ICONS = {
    # Apply the Style to the selected Lines: a style swatch dropping onto lines.
    "style-apply": SVG_HEAD + '<rect x="1.5" y="1.5" width="6" height="6" rx="1"/><path d="M1.5 11.5h7M1.5 14h7"/>'
    '<g id="accent"><path d="M12 2.5v7M9.5 7.5l2.5 2.5 2.5-2.5"/><path d="M10.5 12.5h4M10.5 14.5h4"/></g></svg>',
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "chev": SVG_HEAD + '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/></svg>',
    "x": SVG_HEAD + '<path d="M4.5 4.5l7 7M11.5 4.5l-7 7"/></svg>',
    "pencil": SVG_HEAD + '<path d="M10.5 2.5l3 3-8 8h-3v-3z"/></svg>',
}


def svg(role):
    if role in NEW_ICONS:
        return NEW_ICONS[role]
    s = (ICONS / f"{role}.svg").read_text().strip()
    return re.sub(r'width="16" height="16"', 'width="16" height="16"', s, count=1)


def icon(role, cls=""):
    return f'<span class="ic {cls}">{svg(role)}</span>'


def btn(role, cls="", title=""):
    return f'<span class="b {cls}" title="{title}">{icon(role)}</span>'


# The sample Document: dialogue plus signs.
LINES = [
    ("0", "0:12:38.10", "0:12:40.02", "Default", "", "Where does this train go?"),
    ("0", "0:12:40.30", "0:12:43.88", "Default", "", "All the way to the coast."),
    ("1", "0:12:39.00", "0:12:45.50", "Sign - Station", "", "{\\an7\\pos(196,82)\\fscx112.5\\fscy112.5}KITA-SENJU"),
    ("1", "0:12:39.00", "0:12:45.50", "Sign - Station", "", "{\\an7\\pos(782,198)\\fscx112.5\\fscy112.5}Platform 3 →"),
    ("0", "0:12:41.20", "0:12:44.10", "Default", "Mio", "I'll be waiting at the station."),
    ("1", "0:12:44.20", "0:12:46.00", "Note", "", "{\\an8}Senju: a district in Tokyo"),
    ("0", "0:12:46.10", "0:12:48.40", "Default - Italic", "", "{\\i1}Don't be late.{\\i0}"),
    ("0", "0:12:48.60", "0:12:50.90", "Default", "", "I won't."),
    ("2", "0:12:49.00", "0:12:53.00", "Sign - Title", "", "{\\fad(200,200)}Episode 27"),
]
CUR = 2
PICKED = {2, 3}

# The Document's Styles: name, font, size, primary, outline, Lines using it.
STYLES = [
    ("Default", "Noto Sans", 48, "#FFFFFF", "#101010", 412, "sans"),
    ("Default - Italic", "Noto Sans", 48, "#FFFFFF", "#101010", 37, "sansi"),
    ("Note", "Noto Sans", 34, "#FFFFFF", "#1B3A66", 6, "sans"),
    ("Sign - Station", "Noto Serif", 64, "#F4F0E2", "#2B2B2B", 12, "serif"),
    ("Sign - Title", "Noto Serif", 90, "#FFE6A8", "#3A1E10", 1, "serif"),
    ("OP Romaji", "Noto Sans", 40, "#FFD7F0", "#5A1048", 24, "sans"),
    ("OP English", "Noto Sans", 36, "#FFFFFF", "#5A1048", 24, "sans"),
    ("Credits", "Noto Sans", 30, "#E8E8E8", "#000000", 9, "sans"),
]
CUR_STYLE = "Sign - Station"

FAMILIES = ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy",
            "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"]


def menubar():
    items = ["File", "Edit", "Automation", "Video", "Audio", "View", "Subtitles", "Help"]
    return '<div class="mbar">' + "".join(f'<span class="mbi">{it}</span>' for it in items) + "</div>"


def doctabs(name="frieren-27.ass"):
    return (f'<div class="dtabs"><span class="dtab cur">{name}{icon("x", "sm")}</span>'
            f'<span class="dplus">{icon("add")}</span></div>')


def head_title(title):
    return f'<div class="dhead"><span class="t">{title}</span>{icon("panel-menu", "mut")}</div>'


def head_tabs(title, tools=""):
    return (f'<div class="dhead tabs"><span class="dt sel">{title}<span class="dmore">{icon("panel-menu")}</span></span>'
            f'<span class="grow"></span><span class="tslot">{tools}</span></div>')


# --- Video panel (VT1's inspector bar, Scale tool, two Lines picked) ---------------------------------

def num(label, value, unit="", width=70):
    return (f'<span class="num"><span class="nl">{label}</span>'
            f'<span class="nf" style="width:{width}px"><span class="nv">{value}</span>'
            f'<span class="nu">{unit}</span></span></span>')


def grp(label, *items):
    lab = f'<span class="gl">{label}</span>' if label else ""
    return f'<span class="grp">{lab}{"".join(items)}</span>'


VSEP = '<span class="vsep"></span>'


def tb(role, state=""):
    return f'<span class="tb {state}">{icon(role)}</span>'


def scale_bar():
    return ('<div class="bar">'
            f'<span class="tool">{icon("tool-scale")}<span>Text scaling</span></span>{VSEP}'
            + grp("Rectangle", tb("frame-to-scale", "on"), tb("scale-x", "on dis"), tb("link", "on"),
                  tb("scale-y", "dis"), tb("original-frame")) + VSEP
            + grp("Tags", tb("tool-scale-rotation", "on dis"), tb("resample")) + VSEP
            + grp(None, num("X", "112.5", "%"), num("Y", "112.5", "%"))
            + '<span class="grow"></span><span class="target">' + tb("pick-lines", "on")
            + '<span class="tc">2 lines</span>' + tb("clear") + '</span></div>')


def picture(w, h):
    return (f'<div class="picture" style="width:{w}px;height:{h}px"><div class="scene"></div>'
            f'<div class="sign" style="left:9%;top:9%;font-size:{h * .075:.0f}px">KITA-SENJU</div>'
            f'<div class="sign" style="left:60%;top:30%;font-size:{h * .06:.0f}px">Platform 3 →</div>'
            '<div class="outl" style="left:8.2%;top:8%;width:33%;height:13%"></div>'
            '<div class="outl" style="left:59.2%;top:29%;width:29%;height:10.5%"></div>'
            '<svg class="hand" viewBox="0 0 100 56.25" preserveAspectRatio="none">'
            '<rect x="8.2" y="4.5" width="33" height="7.3" fill="none" stroke="#ff8a00" stroke-width=".25"/>'
            '<rect x="7.6" y="3.9" width="1.2" height="1.2" fill="#ff8a00"/><rect x="40.6" y="3.9" width="1.2" height="1.2" fill="#ff8a00"/>'
            '<rect x="7.6" y="11.2" width="1.2" height="1.2" fill="#ff8a00"/><rect x="40.6" y="11.2" width="1.2" height="1.2" fill="#ff8a00"/>'
            '</svg>'
            f'<div class="vsub" style="font-size:{h * .06:.0f}px">I\'ll be waiting at the station.</div></div>')


def video_panel(w, h):
    rail = "".join(f'<span class="tb rb {"on" if i == 3 else ""}">{icon(r)}</span>' for i, r in enumerate(FAMILIES))
    stage_h = h - 35 - 36 - 66
    pic_h = stage_h - 8
    pic_w = int(pic_h * 16 / 9)
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_title("Video")}'
            f'<div class="dbody"><div class="vtop" style="height:{stage_h}px"><div class="rail">{rail}</div>'
            f'<div class="vstage">{picture(pic_w, pic_h)}</div></div>'
            f'{scale_bar()}'
            '<div class="seek"><i style="width:53%"></i><b style="left:53%"></b><s style="left:52%;width:4%"></s></div>'
            '<div class="times">0:12:41.208 &nbsp; 18263 &nbsp; +2 &nbsp; −2 290</div>'
            '<div class="trans">' + "".join(tb(r) for r in ["media-previous-file", "media-play", "play-line",
                                                           "media-stop", "media-next-file", "frame-previous"])
            + '<span class="grow status">Frame 18263 of 34102</span>' + tb("frame-next") + icon("volume")
            + '<span class="vol"><i></i></span></div></div></div>')


# --- Styles panel ---------------------------------------------------------------------------------

def styles_tools(hover=None):
    def b(role, name, cls=""):
        h = " hov" if hover == role else ""
        return btn(role, cls + h, name)
    return (b("style-apply", "Apply to the selected Lines") + '<span class="sep"></span>'
            + b("add", "New Style") + b("edit", "Edit Style") + b("duplicate", "Duplicate")
            + b("delete", "Delete") + '<span class="sep"></span>' + b("styles", "Style manager"))


def sample(primary, outline, kind):
    fam = '"Noto Serif",serif' if kind == "serif" else '"Noto Sans",sans-serif'
    it = "italic" if kind == "sansi" else "normal"
    wt = 700 if kind == "serif" else 600
    sh = ",".join(f"{x}px {y}px 0 {outline}" for x, y in [(-1, -1), (1, -1), (-1, 1), (1, 1), (0, 1.5), (1.5, 0)])
    return (f'<span class="swatch"><span style="color:{primary};font-family:{fam};font-style:{it};'
            f'font-weight:{wt};text-shadow:{sh}">Aa</span></span>')


def style_rows(selected=CUR_STYLE, current=CUR_STYLE, hover=None, only=None):
    rows = []
    for name, font, size, prim, outl, used, kind in STYLES:
        if only and name not in only:
            continue
        cls = []
        if name == selected:
            cls.append("sel")
        if name == hover:
            cls.append("hov")
        cur = f'<span class="curmark" title="The current Line\'s Style">{icon("pencil", "sm")}</span>' if name == current else ""
        rows.append(f'<div class="srow {" ".join(cls)}">{sample(prim, outl, kind)}'
                    f'<span class="sname"><span class="n1">{name}</span>'
                    f'<span class="n2">{font} · {size}</span></span>'
                    f'<span class="sused">{used}</span>{cur}</div>')
    return "".join(rows)


def styles_panel(w, h, hover_tool=None, hover_row=None, filter_text=""):
    ftxt = filter_text or '<span class="ph">Filter</span>'
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_tabs("Styles", styles_tools(hover_tool))}'
            f'<div class="dbody pad"><div class="filter">{icon("filter", "mut")}{ftxt}</div>'
            '<div class="shead"><span>Style</span><span class="grow"></span><span>Lines</span></div>'
            f'<div class="slist">{style_rows(hover=hover_row)}</div></div></div>')


# --- Line editor and Grid -------------------------------------------------------------------------

def tagstrip():
    roles = ["tag-bold", "tag-italic", "tag-underline", "tag-strikeout", "tag-font",
             "colour-primary", "colour-secondary", "colour-outline", "colour-shadow"]
    return ('<div class="tstrip">' + "".join(btn(r) for r in roles)
            + f'<span class="in dd w150">{icon("alignment-7")}Top-left (an7){icon("chev", "sm")}</span>'
            + btn("menu-more") + btn("hide-tags") + "</div>")


def line_editor(w, h):
    text = LINES[CUR][5]
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_title("Line editor")}'
            f'<div class="dbody pad">{tagstrip()}'
            '<div class="counters"><span>Wraps: 1/10&nbsp;&nbsp;Characters per second: 2&lt;=15</span><span class="grow"></span>'
            '<span class="radio on"></span><span class="mut">Time</span><span class="radio"></span><span class="mut">Frames</span></div>'
            f'<div class="fld focus" style="height:40px">{text}<span class="caret"></span></div>'
            '<div class="irow"><span class="in w30">1</span><span class="in w84">0:12:39.00</span>'
            '<span class="in w84">0:12:45.50</span><span class="in w84">0:00:06.50</span>'
            f'<span class="in dd grow">Sign - Station{icon("chev", "sm")}</span>{icon("pencil", "mut")}</div>'
            '<div class="irow"><span class="chk"></span><span class="lbl">Comment</span>'
            f'<span class="in dd w120 ph">Actor{icon("chev", "sm")}</span><span class="in w38">0</span>'
            '<span class="in w38">0</span><span class="in w38">0</span>'
            f'<span class="in dd grow ph">Effect{icon("chev", "sm")}</span></div></div></div>')


def grid_panel(w, h):
    hdr = ("<tr><th>#</th><th>L.</th><th>Start</th><th>End</th><th>Style</th><th>Actor</th><th>CPS</th>"
           "<th class='txt'>Text</th></tr>")
    rows = []
    for i, (layer, s, e, st, actor, txt) in enumerate(LINES):
        cls = []
        if i == CUR:
            cls.append("cur")
        elif i in PICKED:
            cls.append("selr")
        c = f' class="{" ".join(cls)}"' if cls else ""
        rows.append(f'<tr{c}><td>{i + 1}</td><td>{layer}</td><td>{s}</td><td>{e}</td><td>{st}</td><td>{actor}</td>'
                    f'<td>{max(1, len(txt) // 4)}</td><td class="txt">{txt.replace("{", "<span class=tg>{").replace("}", "}</span>")}</td></tr>')
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_tabs("Grid")}'
            f'<div class="dbody"><table class="grid">{hdr}{"".join(rows)}</table></div></div>')


def statusbar(preset):
    return (f'<div class="sbar"><span>2 lines selected</span><span>1920 × 1080</span><span class="grow"></span>'
            f'<span class="lsw">{icon("layout")}<span>{preset}</span>{icon("chev", "sm")}</span></div>')


# --- Arrangements ---------------------------------------------------------------------------------

W, H = 1280, 800
AREA = H - 26 - 32 - 30 - 2


def window_typesetting(styles_side="right", bottom="side"):
    if bottom == "side":
        top_h = 486
        bot_h = AREA - top_h - 2
        sw = 304
        vw = W - sw - 8
        lw = 566
        gw = W - lw - 8
        vid = video_panel(vw, top_h)
        sty = styles_panel(sw, top_h)
        top = f'{vid}{sty}' if styles_side == "right" else f'{sty}{vid}'
        bottom_html = f'<div class="row">{line_editor(lw, bot_h)}{grid_panel(gw, bot_h)}</div>'
    else:
        le_h = 214
        grid_h = 150
        top_h = AREA - le_h - grid_h - 4
        sw = 304
        vw = W - sw - 8
        vid = video_panel(vw, top_h)
        sty = styles_panel(sw, top_h)
        top = f'{vid}{sty}' if styles_side == "right" else f'{sty}{vid}'
        bottom_html = f'{line_editor(W - 4, le_h)}{grid_panel(W - 4, grid_h)}'
    body = f'<div class="area"><div class="row">{top}</div>{bottom_html}</div>'
    return (f'<div class="win" style="width:{W}px;height:{H}px">{menubar()}{doctabs()}{body}'
            f'{statusbar("Typesetting")}</div>')


def window_today():
    # WS3's Typesetting today: Editing with Audio closed (Line editor takes the right column).
    top_h = 430
    grid_h = AREA - top_h - 2
    vw = int(W * .44)
    rw = W - vw - 8
    body = (f'<div class="area"><div class="row">{video_panel(vw, top_h)}'
            f'{line_editor(rw, top_h)}</div>{grid_panel(W - 4, grid_h)}</div>')
    return (f'<div class="win" style="width:{W}px;height:{H}px">{menubar()}{doctabs()}{body}'
            f'{statusbar("Typesetting")}</div>')


def scaled(inner, s=.5):
    return (f'<div class="scaled" style="width:{W * s + 2:.0f}px;height:{H * s + 2:.0f}px">'
            f'<div style="transform:scale({s});transform-origin:0 0">{inner}</div></div>')


def menu(items, x, y, w=300):
    out = []
    for it in items:
        if it == "-":
            out.append('<div class="msep"></div>')
            continue
        role, text, key, state = it
        ic = icon(role) if role else '<span class="ic"></span>'
        out.append(f'<div class="mi {state}">{ic}<span class="mt">{text}</span><span class="mk2">{key}</span></div>')
    return f'<div class="menu" style="left:{x}px;top:{y}px;width:{w}px">{"".join(out)}</div>'


def cell(n, cap, note, stage, h, wide=False):
    span = ' style="grid-column:1 / span 2"' if wide else ""
    return (f'<div class="cell"{span}><div class="cap"><b>{n}</b>{cap}</div><div class="note">{note}</div>'
            f'<div class="stage" style="height:{h}px">{stage}</div></div>')


def sheet(t):
    cells = []
    cells.append(cell(1, "Typesetting: the Video panel leads, Styles beside it, the Line editor and a short Grid below.",
        "View &gt; Layout &gt; Typesetting (or the layout switcher) on a fresh profile, and Reset this layout from it, give this "
        "arrangement at any window size, in these proportions: the upper row takes about two thirds of the height, Video about three "
        "quarters of its width with the tool rail and VT1's inspector bar, Styles the rest; below, the Line editor at its natural "
        "height beside the Grid. Audio is closed (View &gt; Panels opens it). The Grid keeps its columns; it is short, not trimmed.",
        window_typesetting(), H + 2, wide=True))

    # 2. Styles panel close-up: hover name + row menu.
    hover = styles_panel(304, 470, hover_tool="style-apply", hover_row="Sign - Title")
    tip = ('<div class="tip" style="left:6px;top:36px">Apply to the selected Lines'
           '<div class="dimt">Sets the Style of the 2 selected Lines (Enter, or double-click a Style)</div></div>')
    rmenu = menu([("style-apply", "Apply to the selected Lines", "Enter", "hov"), "-",
                  ("edit", "Edit…", "", ""), ("duplicate", "Duplicate", "", ""), ("delete", "Delete", "Delete", ""), "-",
                  ("select-lines", "Select Lines with this Style", "", ""),
                  ("copy-to-storage", "Copy to the catalog…", "", ""), "-",
                  ("styles", "Open in Style manager", "", "")], 330, 196, 290)
    stage2 = f'<div style="position:absolute;left:0;top:0">{hover}{tip}</div>' + rmenu
    filt = (f'<div class="dock" style="width:300px;height:170px;position:absolute;left:330px;top:0">'
            f'{head_tabs("Styles", styles_tools())}<div class="dbody pad">'
            f'<div class="filter foc">{icon("filter", "mut")}sign<span class="caret"></span><span class="grow"></span>{icon("x", "sm mut")}</div>'
            f'<div class="slist">{style_rows(only=("Sign - Station", "Sign - Title"))}</div></div></div>')
    cells.append(cell(2, "Styles is a new panel: the Document's Styles, applied with one key.",
        "Each row shows the Style drawn in its own font and colours, its font and size, and how many Lines use it. The pencil marks "
        "the current Line's Style, which follows the editing target. Enter or a double-click applies the chosen Style to the "
        "selected Lines (one undo step); the header holds Apply, New, Edit, Duplicate, Delete and Style manager, names on hover. "
        "Typing filters. The row menu (right-click, Menu key) adds Select Lines with this Style and Copy to the catalog. "
        "Catalogs and the full Style editor stay in the Style manager window (SY1).",
        stage2 + filt, 470))

    # 3. Alternatives, half size.
    alt = (f'<div style="display:flex;flex-direction:column;gap:8px"><div class="tag">B · Styles on the left (MuseScore side-panel place)</div>'
           f'{scaled(window_typesetting("left", "side"), .48)}</div>')
    cells.append(cell(3, "Alternative B: Styles on the left of the video.",
        "MuseScore keeps its side panels (Palettes, Properties) on the left of the score. Recommended is A (right), because the "
        "video then keeps its left edge and the rail stays where the Editing arrangement has it when you switch presets.",
        alt, 400))
    alt2 = (f'<div style="display:flex;flex-direction:column;gap:8px"><div class="tag">C · Line editor and Grid stacked, full width</div>'
            f'{scaled(window_typesetting("right", "stacked"), .48)}</div>')
    cells.append(cell(4, "Alternative C: the Line editor and Grid stacked across the window.",
        "The Grid's Text column gets the whole width, but the video loses about 140 px of height (the picture is 20% smaller). "
        "Recommended is A (side by side): the picture is what typesetting needs to be big.",
        alt2, 400))

    today = (f'<div style="display:flex;flex-direction:column;gap:8px"><div class="tag">Today (WS3): Editing with Audio closed</div>'
             f'{scaled(window_today(), .48)}</div>')
    notes = ('<div class="legend">'
             '<p><b>What changes.</b> Typesetting stops being Editing minus Audio: the preset builds its own arrangement. '
             'Timing, Translation and Editing are unchanged.</p>'
             '<p><b>Saved arrangements.</b> A tab, Session, file or saved layout that already holds a Typesetting arrangement '
             'keeps its own panels; Reset this layout and choosing Typesetting again give the new one.</p>'
             '<p><b>The Styles panel</b> is in View &gt; Panels in every arrangement, closed in the others. Like every '
             'panel it can be moved, tabbed or floated; it follows the editing Document (WS pin rules apply).</p>'
             '<p><b>Translation mode on:</b> the Original text panel takes its usual place above the Line editor (TM1).</p>'
             '<p><b>Video rows:</b> Seek on / Then play are not drawn: VF1 (#331) moves them to the Video panel\'s ⋯ menu.</p>'
             '</div>')
    cells.append(cell(5, "For comparison: what Typesetting gives today.",
        "The Line editor takes the right column, the Grid the full width; Styles cannot open as a panel.",
        today, 400))
    cells.append(cell(6, "Questions for the owner, and notes", "", QUESTIONS + notes, 520))
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
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;max-width:1280px}}
.stage{{position:relative}}
.win{{position:relative;background:{t['bg']};border:1px solid {t['line']};overflow:hidden;display:flex;flex-direction:column}}
.mbar{{height:26px;flex:none;display:flex;align-items:center;gap:2px;padding:0 4px;background:{t['bg']}}}
.mbi{{padding:3px 6px;border-radius:2px}}
.dtabs{{height:32px;flex:none;display:flex;align-items:center;gap:6px;padding:0 4px;background:{t['bg']}}}
.dtab{{display:inline-flex;align-items:center;gap:8px;height:24px;padding:0 6px 0 8px;border:1px solid {t['control']};border-radius:3px;background:{t['panel']};font-size:12.5px}}
.dtab.cur{{border-color:{t['accent']}}}
.dplus{{color:{t['muted']};display:inline-flex}}
.area{{flex:1;display:flex;flex-direction:column;gap:2px;padding:0 2px;min-height:0}}
.row{{display:flex;gap:4px}}
.dock{{position:relative;background:{t['panel']};border:1px solid {t['line'] if hc else t['raised']};display:flex;flex-direction:column;overflow:hidden;flex:none}}
.dhead{{height:35px;flex:none;display:flex;align-items:center;padding:0 8px 0 12px;position:relative}}
.dhead .t{{font-weight:700;flex:1}}
.dhead.tabs{{padding:0;background:{t['raised']};border-bottom:1px solid {t['line']}}}
.dt{{height:35px;display:inline-flex;align-items:center;gap:6px;padding:0 6px 0 10px;border-right:1px solid {t['line']}}}
.dt.sel{{background:{t['panel']};font-weight:700;margin-bottom:-1px;height:36px}}
.dmore{{display:inline-flex;width:20px;height:20px;align-items:center;justify-content:center}}
.tslot{{display:flex;align-items:center;gap:1px;padding-right:4px}}
.dbody{{position:relative;flex:1;display:flex;flex-direction:column;min-height:0}}
.dbody.pad{{padding:8px;gap:6px}}
.grow{{flex:1}}
.sep{{width:1px;height:18px;background:{t['line']};margin:0 3px}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none;color:{t['text']}}}
.ic svg{{width:16px;height:16px}}
.ic.sm,.ic.sm svg{{width:12px;height:12px}}
.ic.mut{{color:{t['muted']}}}
svg #accent,svg .acc,svg g[id=accent]{{color:{t['accent']}}}
.b{{height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:3px;color:{t['text']}}}
.b.hov{{background:{t['field']};border-color:{t['control']}}}
.tb{{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:2px;flex:none}}
.tb.on{{background:{t['checked']};border-color:{t['control']}}}
.tb.dis .ic,.tb.dis .ic svg g{{color:{t['disabled']}}}
.tb.dis.on{{border-color:{t['line']}}}
/* video */
.vtop{{display:flex;min-height:0;flex:none}}
.rail{{width:32px;display:flex;flex-direction:column;gap:1px;padding:2px;border-right:1px solid {t['line']}}}
.rail .tb{{width:28px;height:28px}}
.vstage{{flex:1;display:flex;align-items:center;justify-content:center;background:#000;overflow:hidden}}
.picture{{position:relative;overflow:hidden;background:#000}}
.scene{{position:absolute;inset:0;background:linear-gradient(180deg,#1d2a3f 0%,#34465e 46%,#5b4b44 47%,#2a2522 100%)}}
.scene::after{{content:"";position:absolute;left:8%;right:8%;top:47%;height:3%;background:#c9b48a;opacity:.55}}
.sign{{position:absolute;color:#f4f0e2;font-family:"Noto Serif",serif;font-weight:700;letter-spacing:.06em;text-shadow:0 0 2px #000,0 0 2px #000;white-space:nowrap}}
.vsub{{position:absolute;left:0;right:0;bottom:7%;text-align:center;color:#fff;font-weight:600;text-shadow:-2px -2px 0 #000,2px -2px 0 #000,-2px 2px 0 #000,2px 2px 0 #000,0 0 3px #000}}
.hand{{position:absolute;inset:0;width:100%;height:100%}}
.outl{{position:absolute;border:1px dashed #6fd3ff}}
.bar{{display:flex;align-items:center;gap:6px;height:36px;flex:none;padding:0 8px;background:{t['panel']};border-top:1px solid {t['line']};white-space:nowrap}}
.tool{{display:inline-flex;align-items:center;gap:6px;font-weight:650;padding-right:2px}}
.vsep{{width:1px;height:20px;background:{t['line']};flex:none;margin:0 2px}}
.grp{{display:inline-flex;align-items:center;gap:3px}}
.gl{{color:{t['muted']};font-size:12px;margin-right:4px}}
.num{{display:inline-flex;align-items:center;gap:4px;margin-right:6px}}
.nl{{color:{t['muted']};font-size:12px;padding:0 1px;border-bottom:1px dotted {t['control']}}}
.nf{{height:24px;display:inline-flex;align-items:center;justify-content:flex-end;gap:3px;background:{t['field']};border:1px solid {t['control']};border-radius:2px;padding:0 6px;font-variant-numeric:tabular-nums}}
.nu{{color:{t['muted']};font-size:12px}}
.target{{display:inline-flex;align-items:center;gap:3px}}
.tc{{font-size:12px;padding:0 2px}}
.seek{{height:14px;margin:4px 8px 0;position:relative;flex:none}}
.seek::before{{content:"";position:absolute;left:0;right:0;top:6px;height:3px;background:{t['raised']};border:1px solid {t['line']}}}
.seek i{{position:absolute;left:0;top:6px;height:3px;background:{t['accent']}}}
.seek b{{position:absolute;top:2px;width:10px;height:10px;margin-left:-5px;border-radius:50%;background:{t['text']}}}
.seek s{{position:absolute;top:11px;height:3px;background:{t['warning']};opacity:.7}}
.times{{font-size:12px;padding:2px 10px;font-variant-numeric:tabular-nums;flex:none}}
.trans{{display:flex;align-items:center;gap:2px;padding:2px 8px 4px;flex:none}}
.status{{text-align:center;font-size:12px;color:{t['muted']}}}
.vol{{width:70px;height:3px;background:{t['raised']};border:1px solid {t['line']};position:relative;margin:0 4px 0 6px}}
.vol i{{position:absolute;left:0;top:0;bottom:0;width:70%;background:{t['accent']}}}
/* styles panel */
.filter{{height:26px;flex:none;display:flex;align-items:center;gap:6px;padding:0 6px;background:{t['field']};border:1px solid {t['control']};border-radius:3px}}
.filter.foc{{border-color:{t['accent']};box-shadow:0 0 0 1px {t['accent']}}}
.filter .ph{{color:{t['muted']}}}
.shead{{display:flex;font-size:11.5px;color:{t['muted']};padding:0 10px 0 6px;flex:none}}
.slist{{display:flex;flex-direction:column;flex:1;min-height:0;overflow:hidden;border-top:1px solid {t['line'] if hc else t['raised']}}}
.srow{{display:flex;align-items:center;gap:8px;height:42px;flex:none;padding:0 8px 0 6px;border-bottom:1px solid {t['raised'] if not hc else t['disabled']};position:relative}}
.srow.sel{{background:{t['select']};box-shadow:inset 3px 0 0 {t['accent']}}}
.srow.hov{{background:{t['field'] if not hc else t['raised']}}}
.swatch{{width:46px;height:30px;flex:none;display:inline-flex;align-items:center;justify-content:center;font-size:17px;border:1px solid {t['line']};border-radius:2px;
background:conic-gradient(#6b6b6b 25%,#8a8a8a 0 50%,#6b6b6b 0 75%,#8a8a8a 0) 0 0/10px 10px}}
.sname{{display:flex;flex-direction:column;flex:1;min-width:0;line-height:1.2}}
.n1{{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}}
.n2{{font-size:11.5px;color:{t['muted']}}}
.sused{{font-size:12px;font-variant-numeric:tabular-nums;color:{t['muted']};min-width:26px;text-align:right}}
.curmark{{position:absolute;right:42px;top:14px;color:{t['accent']}}}
.curmark .ic{{color:{t['accent']}}}
/* line editor */
.tstrip{{display:flex;gap:1px;align-items:center;flex:none}}
.counters{{font-size:12.5px;height:18px;display:flex;align-items:center;gap:6px;flex:none}}
.mut{{color:{t['muted']}}}
.radio{{width:12px;height:12px;border-radius:6px;border:1px solid {t['control']};display:inline-block}}
.radio.on{{box-shadow:inset 0 0 0 3px {t['field']};background:{t['accent']}}}
.fld{{background:{t['field']};border:1px solid {t['control']};border-radius:3px;padding:6px 9px;font-size:13.5px;flex:none;position:relative;overflow:hidden;white-space:nowrap}}
.fld.focus{{border-color:{t['accent']};box-shadow:0 0 0 1px {t['accent']}}}
.caret{{display:inline-block;width:1px;height:16px;background:{t['text']};vertical-align:-3px;margin-left:1px}}
.irow{{display:flex;gap:4px;align-items:center;flex:none}}
.in{{height:26px;display:inline-flex;align-items:center;gap:4px;padding:0 6px;background:{t['field']};border:1px solid {t['control']};border-radius:3px;font-size:12.5px;font-variant-numeric:tabular-nums;white-space:nowrap}}
.in.dd{{justify-content:space-between}}
.in.ph{{color:{t['muted']}}}
.w30{{width:34px}}.w84{{width:84px}}.w150{{width:150px;margin-left:4px}}.w120{{width:120px}}.w38{{width:40px}}
.chk{{width:14px;height:14px;border:1px solid {t['control']};border-radius:2px;display:inline-flex;flex:none}}
.lbl{{margin-right:6px}}
/* grid */
table.grid{{border-collapse:collapse;width:100%;font-size:12.5px;table-layout:fixed}}
.grid th{{text-align:left;font-weight:400;color:{t['muted']};padding:3px 6px;background:{t['raised']};border-bottom:1px solid {t['line']}}}
.grid td{{padding:2px 6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums;border-bottom:1px solid {t['raised'] if not hc else t['disabled']}}}
.grid th:nth-child(1){{width:26px}}.grid th:nth-child(2){{width:24px}}.grid th:nth-child(3),.grid th:nth-child(4){{width:82px}}
.grid th:nth-child(5){{width:96px}}.grid th:nth-child(6){{width:44px}}.grid th:nth-child(7){{width:34px}}
.grid tr.cur td{{background:{t['select']}}}
.grid tr.cur td:first-child{{box-shadow:inset 3px 0 0 {t['accent']}}}
.grid tr.selr td{{background:{t['select']}}}
.grid .tg{{color:{t['muted']}}}
.sbar{{height:30px;flex:none;display:flex;align-items:center;gap:16px;padding:0 10px 0 240px;font-size:12px;background:{t['bg']}}}
.lsw{{display:inline-flex;align-items:center;gap:6px;padding:2px 6px;border-radius:3px}}
.scaled{{overflow:hidden;border:1px solid {t['line']}}}
.tag{{font-size:11px;color:{t['muted']};font-weight:600;text-transform:uppercase;letter-spacing:.04em}}
.menu{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{sh})}}
.mi{{display:flex;align-items:center;gap:8px;height:28px;padding:0 12px 0 10px}}
.mi .mt{{flex:1;white-space:nowrap}}
.mi .mk2{{color:{t['muted']};font-size:12px;white-space:nowrap}}
.mi.hov{{background:{t['select']};box-shadow:inset 3px 0 0 {t['accent']}}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
.tip{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};padding:4px 8px;font-size:12px;border-radius:2px;max-width:300px;box-shadow:0 2px 8px rgba(0,0,0,{sh})}}
.tip .dimt{{color:{t['muted']};font-size:11px}}
.legend{{font-size:12px;color:{t['muted']};line-height:1.5}}
.legend p{{margin:0 0 9px}}
.legend b{{color:{t['text']}}}
.questions{{margin:0 0 12px;padding:10px 14px 6px;border:1px solid {t['line']};background:{t['panel']};font-size:12.5px}}
.questions h2{{font-size:13px;margin:0 0 8px}}
.questions ol{{margin:0;padding-left:20px}}
.questions li{{margin:0 0 5px}}
"""


QUESTIONS = """<div class="questions"><ol>
<li><b>Where Styles docks:</b> right of the video (A, recommended) or left of it (B).</li>
<li><b>Below the video:</b> Line editor and Grid side by side (A, recommended) or stacked full width (C).</li>
<li><b>Styles panel content:</b> the Document's Styles only, catalogs and the Style editor staying in the Style manager window (recommended), or the catalog list in the panel too.</li>
<li><b>Enter / double-click on a Style:</b> applies it to the selected Lines, as a MuseScore palette applies to the selection (recommended), or opens the Style editor.</li>
</ol></div>"""


def page(t):
    cells = sheet(t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>TP1 Typesetting — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>TP1 Typesetting preset — {t["name"]}</h1>'
            f'<span>Mockup for #274 at 1280 × 800. Real icon set; one new glyph: Apply to the selected Lines.</span></div>'
            f'<div class="sheet">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"tp1-{key}.html").write_text(page(t))
print("wrote", ", ".join(f"tp1-{k}.html" for k in THEMES))
