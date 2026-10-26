#!/usr/bin/env python3
"""KP1 #286 Karaoke preset mockup: one HTML sheet per theme, screenshotted by shoot.sh.

Built on TP1's preset mockup (tp1-mockup/gen.py), KA2's audio panel in Karaoke mode
(ka2-mockup/gen.py, AU1's merged strip) and KE2's Template editor as built on w6-ke2.

usage: gen.py <icons dir: qt's src/ui/icons plus w6-ke2's template-editor, loop> <out dir>
"""
import math
import pathlib
import random
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# theme.cpp baseRoles + the default accent picks; audio = Content::audio (as AU1/KA2).
THEMES = {
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", disabled="#75818D", checked="#3A4450", warning="#E0A030", comment="#1F2A3A",
                 hc=False, kw="#8E9BFF", kv="#5FC79A", tg="#7FB8F0",
                 a_bg="#36393E", a_wave="#202225", a_inact="#2F3136", a_scale="#202225", a_scaletext="#AEAFB2",
                 a_start="#940000", a_inbound="#00D77D", a_cursor="#8791FD", a_syl="#202225"),
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", disabled="#74808B", checked="#D3D9DE", warning="#8A5A00", comment="#E3EAF3",
                  hc=False, kw="#3B3FB0", kv="#00704A", tg="#1F5FA0",
                  a_bg="#DDE3E8", a_wave="#5B6774", a_inact="#9AA5B0", a_scale="#EDF0F3", a_scaletext="#526170",
                  a_start="#940000", a_inbound="#00875A", a_cursor="#4F56D6", a_syl="#3A4652"),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E", checked="#C9DAF8", warning="#6B4500",
                    comment="#EBEBEB", hc=True, kw="#0037B3", kv="#005A2B", tg="#0037B3",
                    a_bg="#FFFFFF", a_wave="#303030", a_inact="#8C8C8C", a_scale="#FFFFFF", a_scaletext="#000000",
                    a_start="#C00000", a_inbound="#007A3D", a_cursor="#8000C0", a_syl="#000000"),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", disabled="#8C8C8C", checked="#253F60", warning="#FFD54A",
                    comment="#151515", hc=True, kw="#FFFF00", kv="#00FF80", tg="#00FFFF",
                    a_bg="#000000", a_wave="#C8C8C8", a_inact="#6E6E6E", a_scale="#000000", a_scaletext="#FFFFFF",
                    a_start="#FF4040", a_inbound="#00FF80", a_cursor="#00FFFF", a_syl="#FFFFFF"),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Small glyphs the set has no role for (menu marks), drawn in its 16 px, 1 px stroke style.
NEW_ICONS = {
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "x": SVG_HEAD + '<path d="M4.5 4.5l7 7M11.5 4.5l-7 7"/></svg>',
    "chev": SVG_HEAD + '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/></svg>',
}
CHEVRON = ('<svg width="8" height="8" viewBox="0 0 8 8" fill="none" stroke="currentColor" stroke-width="1" '
           'stroke-linecap="round" stroke-linejoin="round"><path d="M1.5 3l2.5 2.5 2.5-2.5"/></svg>')


def svg(role):
    if role in NEW_ICONS:
        return NEW_ICONS[role]
    return (ICONS / f"{role}.svg").read_text().strip()


def icon(role, cls=""):
    return f'<span class="ic {cls}">{svg(role)}</span>'


def btn(role, cls="", title=""):
    return f'<span class="b {cls}" title="{title}">{icon(role)}</span>'


def split(role, cls="", marks=""):
    return (f'<span class="split {cls}"><span class="face">{icon(role)}{marks}</span>'
            f'<span class="arrow">{CHEVRON}</span></span>')


def seg(items, current, cls=""):
    return (f'<span class="seg {cls}">' + "".join(
        f'<span class="sg{" on" if i == current else ""}">{icon(r)}</span>' for i, r in enumerate(items)) + '</span>')


SEP = '<span class="sep"></span>'

# --- The sample Document: an OP with templates applied ------------------------------------------
# (n, layer, start, end, style, effect, text, kind) kind: code | template | kara | tl | fx
TEMPLATES = [
    (1, 0, "0:00:00.00", "0:00:00.00", "OP Romaji", "code once", "function fade_in(t) return t * 2 end", "code"),
    (2, 0, "0:00:00.00", "0:00:00.00", "OP Romaji", "template pre-line", "{\\an8\\pos($center,$top)\\fad(300,200)\\1a&H80&}", "template"),
    (3, 1, "0:00:00.00", "0:00:00.00", "OP Romaji", "template syl noblank", "!retime(\"syl\",-200,0)!{\\an5\\pos($scenter,$smiddle)\\fad(150,0)\\t(200,!200+$sdur!,\\fscx125\\fscy125\\1c&HB469FF&)}", "template"),
    (4, 0, "0:00:00.00", "0:00:00.00", "OP English", "template line keeptags", "{\\an2\\fad(200,200)}", "template"),
]
KARA = [
    (5, 0, "0:00:01.00", "0:00:04.50", "OP Romaji", "karaoke", "{\\k32}ki {\\k28}mi {\\k41}no {\\k25}na {\\k60}mae", "kara"),
    (6, 0, "0:00:04.60", "0:00:08.00", "OP Romaji", "karaoke", "{\\k30}ha{\\k30}ru {\\k40}no {\\k30}ka{\\k40}ze", "kara"),
    (7, 0, "0:00:08.10", "0:00:11.60", "OP Romaji", "karaoke", "{\\k45}ko{\\k35}ko{\\k30}ro {\\k50}no {\\k40}o{\\k50}to", "kara"),
    (8, 0, "0:00:11.70", "0:00:15.20", "OP Romaji", "karaoke", "{\\k38}to{\\k32}o{\\k44}ku {\\k30}e {\\k56}to", "kara"),
    (9, 0, "0:00:01.00", "0:00:08.00", "OP English", "", "Your name, carried on the spring wind", "tl"),
]
FX = [
    (10, 0, "0:00:00.70", "0:00:04.70", "OP Romaji", "fx", "{\\an8\\pos(640,40)\\fad(300,200)\\1a&H80&}ki mi no na mae", "fx"),
    (11, 1, "0:00:00.80", "0:00:01.32", "OP Romaji", "fx", "{\\an5\\pos(457,76)\\fad(150,0)\\t(200,520,\\fscx125\\fscy125\\1c&HB469FF&)}ki", "fx"),
    (12, 1, "0:00:01.12", "0:00:01.60", "OP Romaji", "fx", "{\\an5\\pos(503,76)\\fad(150,0)\\t(200,480,\\fscx125\\fscy125\\1c&HB469FF&)}mi", "fx"),
    (13, 1, "0:00:01.40", "0:00:02.01", "OP Romaji", "fx", "{\\an5\\pos(560,76)\\fad(150,0)\\t(200,610,\\fscx125\\fscy125\\1c&HB469FF&)}no", "fx"),
]
FX_FIRST, FX_LAST, FX_COUNT = 10, 321, 312
CUR = 5


def menubar(preset, open_switch=False):
    items = ["File", "Edit", "Automation", "Video", "Audio", "View", "Subtitles", "Help"]
    sw = (f'<span class="lsw{" open" if open_switch else ""}">{icon("layout")}<span>{preset}</span>'
          f'{icon("chev", "sm")}</span>')
    return '<div class="mbar">' + "".join(f'<span class="mbi">{it}</span>' for it in items) + sw + "</div>"


def doctabs(name="song-op.ass"):
    return (f'<div class="dtabs"><span class="dtab cur">{name}{icon("x", "sm")}</span>'
            f'<span class="dplus">{icon("add")}</span></div>')


def head_title(title):
    return f'<div class="dhead"><span class="t">{title}</span>{icon("panel-menu", "mut")}</div>'


def head_tabs(tabs, cur, tools=""):
    out = []
    for i, name in enumerate(tabs):
        more = f'<span class="dmore">{icon("panel-menu")}</span>' if i == cur else ""
        out.append(f'<span class="dt{" sel" if i == cur else ""}">{name}{more}</span>')
    return f'<div class="dhead tabs">{"".join(out)}<span class="grow"></span><span class="tslot">{tools}</span></div>'


# --- Video panel: KE2's live template preview on the frame ---------------------------------------

def tb(role, state=""):
    return f'<span class="tb {state}">{icon(role)}</span>'


FAMILIES = ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy",
            "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"]


def picture(w, h, preview=True):
    syl = ["ki", "mi", "no", "na", "mae"]
    fs = h * .085
    kara = "".join(f'<span class="ks{" hi" if i == 2 else ""}{" done" if i < 2 else ""}">{s}</span>'
                   for i, s in enumerate(syl))
    badge = (f'<div class="vbadge">{icon("template-editor", "sm")}<b>Preview</b>&nbsp;· Line 5 · not applied</div>'
             if preview else "")
    return (f'<div class="picture" style="width:{w}px;height:{h}px"><div class="scene"></div>'
            f'<div class="kline" style="font-size:{fs:.0f}px">{kara}</div>'
            f'<div class="vsub" style="font-size:{h * .055:.0f}px">Your name, <span class="red">carried on the spring wind</span></div>'
            f'{badge}</div>')


def video_panel(w, h, preview=True):
    rail = "".join(f'<span class="tb rb {"on" if i == 0 else ""}">{icon(r)}</span>' for i, r in enumerate(FAMILIES))
    stage_h = h - 35 - 30 - 64
    pic_h = stage_h - 6
    pic_w = min(int(pic_h * 16 / 9), w - 40)
    pic_h = int(pic_w * 9 / 16)
    bar = (f'<div class="bar"><span class="tool">{icon("tool-crosshair")}<span>Position pointer</span></span>'
           f'<span class="vsep"></span>{tb("copy-position")}</div>')
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_title("Video")}'
            f'<div class="dbody"><div class="vtop" style="height:{stage_h}px"><div class="rail">{rail}</div>'
            f'<div class="vstage">{picture(pic_w, pic_h, preview)}</div></div>{bar}'
            '<div class="seek"><i style="width:3%"></i><b style="left:3%"></b></div>'
            '<div class="times">0:00:01.751 &nbsp; 42 &nbsp; +751 ms &nbsp; −2 749 ms</div>'
            '<div class="trans">' + "".join(tb(r) for r in ["media-previous-file", "media-play", "play-line",
                                                           "media-stop", "media-next-file"])
            + '<span class="grow status">Frame 42 of 2 160</span>' + icon("volume")
            + '<span class="vol"><i></i></span></div></div></div>')


# --- Template editor (KE2, w6-ke2 at 56e430b54) -------------------------------------------------

def hl(text):
    """Template code: tags and $variables coloured, as KE2's editor."""
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c == "\\":
            j = i + 1
            while j < len(text) and text[j].isalnum():
                j += 1
            out.append(f'<span class="h-tag">{text[i:j]}</span>')
            i = j
        elif c == "$":
            j = i + 1
            while j < len(text) and text[j].isalnum():
                j += 1
            out.append(f'<span class="h-var">{text[i:j]}</span>')
            i = j
        elif c == "!":
            out.append('<span class="h-lua">!</span>')
            i += 1
        else:
            out.append(c.replace("<", "&lt;"))
            i += 1
    return "".join(out)


def template_panel(w, h, *, front="Template editor", compact=False):
    tabs = ["Line editor", "Template editor"]
    cur = tabs.index(front)
    tools = btn("show", "on", "Preview on the video") + btn("loop", "", "Loop the Line")
    if front == "Line editor":
        return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_tabs(tabs, cur, "")}'
                f'<div class="dbody pad">{line_editor_body()}</div></div>')
    rows = []
    for n, layer, s, e, st, eff, txt, kind in TEMPLATES:
        kw, rest = eff.split(" ", 1)
        uses = {1: "0.0 ms", 2: "4 Lines · 0.0 ms", 3: "26 Lines · 0.1 ms", 4: "1 Line · 0.0 ms"}[n]
        rows.append(f'<div class="trow{" sel" if n == 3 else ""}"><span class="tn">{n}</span>'
                    f'<span class="tk"><span class="h-kw">{kw}</span> <span class="h-kv">{rest}</span></span>'
                    f'<span class="tst">{st}</span>'
                    f'<span class="tx mono">{hl(txt)}</span><span class="tu">{uses}</span></div>')
    code = hl(TEMPLATES[2][6])
    prev = [("0", "0:00:00.70", "0:00:04.70", "{\\an8\\pos(640,40)\\fad(300,200)\\1a&H80&}ki mi no na mae"),
            ("1", "0:00:00.80", "0:00:01.32", "{\\an5\\pos(457,76)\\fad(150,0)\\t(200,520,\\fscx125\\fscy125\\1c&HB469FF&)}ki")]
    prev_rows = "".join(f'<div class="prow"><span>{l}</span><span>{s}</span><span>{e}</span>'
                        f'<span class="mono">{hl(t)}</span></div>' for l, s, e, t in prev[:1 if compact else 2])
    hint = "" if compact else (f'<div class="hint"><span class="kc">$</span> inserts a variable, '
                               f'<span class="kc">!</span> starts a Lua expression.</div>')
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_tabs(tabs, cur, tools)}'
            f'<div class="dbody pad te">'
            f'<div class="sh"><b>Templates</b><span class="mut">3 template Lines · 1 code Line · 4 karaoke Lines</span>'
            f'<span class="grow"></span><span class="pbtn">{icon("add", "sm")}New</span></div>'
            f'<div class="tlist">{"".join(rows)}</div>'
            f'<div class="sh"><b>Line 3</b><span class="mut">template syl noblank</span></div>'
            f'<div class="irow"><span class="lbl">Effect</span><span class="in grow mono">template syl noblank</span>'
            f'<span class="lbl">Style</span><span class="in dd w120">OP Romaji{icon("chev", "sm")}</span>'
            f'<span class="lbl">Layer</span><span class="in w38">1</span></div>'
            f'<div class="code mono">{code}</div>{hint}'
            f'<div class="sh"><b>Preview on Line 5</b><span class="mut">OP Romaji · ki mi no na mae</span>'
            f'<span class="grow"></span><span class="mut">6 Lines · 111 ms</span></div>'
            f'<div class="ptab"><div class="prow ph"><span>L</span><span>Start</span><span>End</span><span>Text</span></div>{prev_rows}</div>'
            f'<div class="foot"><span class="grow"></span><span class="apply"><span class="af">Apply</span>'
            f'<span class="aa">{CHEVRON}</span></span></div></div></div>')


def line_editor_body():
    roles = ["tag-bold", "tag-italic", "tag-underline", "tag-strikeout", "tag-font",
             "colour-primary", "colour-secondary", "colour-outline", "colour-shadow"]
    return ('<div class="tstrip">' + "".join(btn(r) for r in roles) + btn("panel-menu") + "</div>"
            '<div class="counters"><span>Wraps: 1/10&nbsp;&nbsp;Characters per second: 4&lt;=15</span></div>'
            f'<div class="fld focus">{KARA[0][6]}<span class="caret"></span></div>'
            '<div class="irow"><span class="in w38">0</span><span class="in w84">0:00:01.00</span>'
            '<span class="in w84">0:00:04.50</span><span class="in w84">0:00:03.50</span>'
            f'<span class="in dd grow">OP Romaji{icon("chev", "sm")}</span></div>'
            '<div class="irow"><span class="chk on"></span><span class="lbl">Comment</span>'
            f'<span class="in dd w120 ph">Actor{icon("chev", "sm")}</span>'
            f'<span class="in dd grow">karaoke{icon("chev", "sm")}</span></div>')


# --- Audio panel in Karaoke mode (AU1/AU2 strip, KA1/KA2/KA5 syllables) --------------------------

def strip(t, *, mode=1, narrow=False):
    marks = '<span class="marks"><i class="mk">A</i></span>'
    left = [btn("audio-previous-line"), btn("audio-play"), btn("play-line"), btn("media-stop"),
            btn("audio-next-line"), SEP, split("play-before-start"), SEP, btn("lead-in"), btn("lead-out"),
            split("commit", "", marks)]
    right = [seg(["audio-line-mode", "karaoke"], mode, "modeseg")]
    if mode == 1:
        right += [btn("karaoke-split"), split("karaoke-tap")]
    right.append(seg(["audio-waveform", "spectrum"], 0))
    right.append(SEP)
    if narrow:
        right.append(btn("panel-menu"))
    else:
        right += [btn("auto-scroll", "tg"), btn("audio-video-sync", "tg"), btn("spectrum-nonlinear"), SEP]
    right.append(f'<span class="b zoom">{icon("zoom")}<span class="ro">1.5×</span></span>')
    right.append(btn("volume-low"))
    return ('<div class="strip"><div class="grp">' + "".join(left) + '</div><div class="grow"></div>'
            '<div class="grp">' + "".join(right) + '</div></div>')


SYLS = [(0.12, 0.215, "ki"), (0.215, 0.30, "mi"), (0.30, 0.425, "no"), (0.425, 0.50, "na"), (0.50, 0.68, "mae")]


def display(t, w, h, *, karaoke=True, seed=5, cursor=0.36):
    rnd = random.Random(seed)
    disp_h = h - 16
    mid = 16 + disp_h / 2
    line = (0.12, 0.68)
    p = [f'<svg class="disp" width="{w}" height="{h}" viewBox="0 0 {w} {h}" xmlns="http://www.w3.org/2000/svg">']
    p.append(f'<rect x="0" y="0" width="{w}" height="{h}" fill="{t["a_bg"]}"/>')
    p.append(f'<rect x="0" y="0" width="{w}" height="16" fill="{t["a_scale"]}"/>')
    n = max(6, int(w / 90))
    step = w / n
    for i in range(n + 1):
        x = i * step
        p.append(f'<line x1="{x:.1f}" y1="11" x2="{x:.1f}" y2="16" stroke="{t["a_scaletext"]}" stroke-width="1"/>')
        p.append(f'<text x="{x + 3:.1f}" y="10" font-size="9.5" fill="{t["a_scaletext"]}" '
                 f'font-family="Noto Sans, Segoe UI, sans-serif">{i * 6.4 / n:.1f}</text>')
    ls, le = line[0] * w, line[1] * w
    p.append(f'<rect x="{ls:.1f}" y="16" width="{le - ls:.1f}" height="{disp_h}" fill="{t["accent"]}" fill-opacity="0.16"/>')
    for x in range(0, w, 2):
        env = 0.25 + 0.75 * abs(math.sin(x / 33.0 + seed)) * (0.6 + 0.4 * math.sin(x / 8.0))
        if (x // 56) % 5 == 4:
            env *= 0.15
        a = env * (disp_h / 2 - (14 if karaoke else 6)) * (0.6 + 0.4 * rnd.random())
        col = t["accent"] if ls <= x <= le else t["a_wave"]
        yoff = 7 if karaoke else 0
        p.append(f'<line x1="{x}" y1="{mid + yoff - a:.1f}" x2="{x}" y2="{mid + yoff + a:.1f}" stroke="{col}" stroke-width="1.6"/>')
    if karaoke:
        lane_y, lane_h = 18, 20
        sc = t["a_syl"] if not t["hc"] else t["text"]
        for (s0, s1, txt) in SYLS:
            x0, x1 = s0 * w, s1 * w
            cur = txt == "no"
            if t["hc"]:
                p.append(f'<rect x="{x0 + .5:.1f}" y="{lane_y + .5}" width="{x1 - x0 - 1:.1f}" height="{lane_h - 1}" '
                         f'fill="{t["accent"] if cur else t["field"]}" stroke="{t["accent"]}"/>')
                tc = t["onaccent"] if cur else t["text"]
            else:
                p.append(f'<rect x="{x0:.1f}" y="{lane_y}" width="{x1 - x0:.1f}" height="{lane_h}" fill="{t["accent"]}" '
                         f'fill-opacity="{0.9 if cur else 0.45}"/>')
                tc = t["onaccent"] if cur else t["text"]
            p.append(f'<text x="{(x0 + x1) / 2:.1f}" y="{lane_y + 14}" text-anchor="middle" font-size="12" '
                     f'font-weight="{700 if cur else 600}" fill="{tc}" font-family="Noto Sans, Segoe UI, sans-serif">{txt}</text>')
            p.append(f'<line x1="{x0:.1f}" y1="{lane_y}" x2="{x0:.1f}" y2="{h}" stroke="{sc}" stroke-width="1"/>')
    for x in (ls, le):
        p.append(f'<line x1="{x:.1f}" y1="16" x2="{x:.1f}" y2="{h}" stroke="{t["a_start"]}" stroke-width="2"/>')
    cx = cursor * w
    p.append(f'<line x1="{cx:.1f}" y1="16" x2="{cx:.1f}" y2="{h}" stroke="{t["a_cursor"]}" stroke-width="1.5"/>')
    p.append('</svg>')
    return "".join(p)


def audio_panel(t, w, h, *, mode=1, narrow=False):
    disp_h = h - 35 - 8 - 36
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_title("Audio")}'
            f'<div class="dbody"><div class="dispwrap" style="height:{disp_h}px">{display(t, w - 2, disp_h, karaoke=mode == 1)}</div>'
            f'<div class="scroll"><div class="thumb" style="left:2%;width:9%"></div></div>{strip(t, mode=mode, narrow=narrow)}</div></div>')


# --- Grid with generated Lines folded ------------------------------------------------------------

def grid_row(r, cls=""):
    n, layer, s, e, st, eff, txt, kind = r
    c = [cls] if cls else []
    if kind in ("code", "template", "kara"):
        c.append("cmt")
    if kind == "fx":
        c.append("fx")
    if n == CUR:
        c.append("cur")
    tx = txt.replace("{", "<span class=tg>{").replace("}", "}</span>")
    return (f'<tr class="{" ".join(c)}"><td>{n}</td><td>{layer}</td><td>{s}</td><td>{e}</td><td>{st}</td>'
            f'<td>{eff}</td><td class="txt">{tx}</td></tr>')


def fold_row(folded=True, hover=False):
    arrow = "chevron-right" if folded else "chevron-down"
    what = f'{FX_COUNT} generated Lines'
    return (f'<tr class="fold{" hov" if hover else ""}{" open" if not folded else ""}"><td colspan="2" class="frange">'
            f'{FX_FIRST}–{FX_LAST}</td><td>0:00:00.70</td><td>0:01:29.80</td>'
            f'<td colspan="3" class="txt"><span class="fbox">{icon(arrow, "sm")}<b>{what}</b>'
            f'<span class="fm">Effect fx · OP Romaji, OP English · from 4 karaoke Lines and 1 translation Line</span></span></td></tr>')


def grid_panel(w, h, *, rows, folded=True, hover_fold=False, title_menu=False, all_fx=False, compact=False):
    hdr = ("<tr><th>#</th><th>L.</th><th>Start</th><th>End</th><th>Style</th><th>Effect</th>"
           "<th class='txt'>Text</th></tr>")
    body = []
    for r in rows:
        if r == "fold":
            body.append(fold_row(folded, hover_fold))
            if not folded:
                body += [grid_row(x) for x in FX]
        else:
            body.append(grid_row(r))
    if all_fx:
        body += [grid_row(x) for x in FX]
    return (f'<div class="dock" style="width:{w}px;height:{h}px">{head_tabs(["Grid"], 0)}'
            f'<div class="dbody"><table class="grid{" compact" if compact else ""}">{hdr}{"".join(body)}</table></div></div>')


def statusbar(sel="1 Line selected"):
    return (f'<div class="sbar"><span>{sel}</span><span>1920 × 1080</span><span>23.976 FPS</span>'
            '<span class="grow"></span></div>')


# --- Arrangements ---------------------------------------------------------------------------------

W, H = 1280, 800
AREA = H - 28 - 32 - 24 - 4
GRID_ROWS = [KARA[0], KARA[1], KARA[2], KARA[4], "fold"]


def window(body, preset, *, open_switch=False):
    return (f'<div class="win" style="width:{W}px;height:{H}px">{menubar(preset, open_switch)}{doctabs()}'
            f'<div class="area">{body}</div>{statusbar()}</div>')


def karaoke_a(t, open_switch=False):
    grid_h, audio_h = 178, 154
    top_h = AREA - grid_h - audio_h - 4
    vw = int(W * .44)
    tw = W - vw - 8
    body = (f'<div class="row">{video_panel(vw, top_h)}{template_panel(tw, top_h, compact=True)}</div>'
            f'{audio_panel(t, W - 4, audio_h)}'
            f'{grid_panel(W - 4, grid_h, rows=GRID_ROWS)}')
    return window(body, "Karaoke", open_switch=open_switch)


def karaoke_b(t):
    grid_h, audio_h = 178, 154
    top_h = AREA - grid_h - 2
    vw = int(W * .44)
    rw = W - vw - 8
    body = (f'<div class="row">{video_panel(vw, top_h)}<div class="col">{audio_panel(t, rw, audio_h, narrow=True)}'
            f'{template_panel(rw, top_h - audio_h - 2, compact=True)}</div></div>'
            f'{grid_panel(W - 4, grid_h, rows=GRID_ROWS)}')
    return window(body, "Karaoke")


def today(t):
    # Editing with the Line | Karaoke switch turned on by hand; KE2 tabs the Template editor with the
    # Line editor; the Grid lists every generated Line.
    grid_h = 200
    top_h = AREA - grid_h - 2
    vw = int(W * .44)
    rw = W - vw - 8
    audio_h = 154
    body = (f'<div class="row">{video_panel(vw, top_h, preview=False)}<div class="col">'
            f'{audio_panel(t, rw, audio_h, narrow=True)}{template_panel(rw, top_h - audio_h - 2, front="Line editor")}</div></div>'
            f'{grid_panel(W - 4, grid_h, rows=[KARA[0], KARA[1], KARA[2], KARA[3], KARA[4]], all_fx=True)}')
    return window(body, "Editing")


def scaled(inner, s=.5):
    return (f'<div class="scaled" style="width:{W * s + 2:.0f}px;height:{H * s + 2:.0f}px">'
            f'<div style="transform:scale({s});transform-origin:0 0">{inner}</div></div>')


def menu(items, x, y, w=300):
    out = []
    for it in items:
        if it == "-":
            out.append('<div class="msep"></div>')
            continue
        if isinstance(it, str):
            out.append(f'<div class="mh">{it}</div>')
            continue
        role, text, key, state = it
        ic = icon(role) if role else '<span class="ic"></span>'
        out.append(f'<div class="mi {state}">{ic}<span class="mt">{text}</span><span class="mk2">{key}</span></div>')
    return f'<div class="menu" style="left:{x}px;top:{y}px;width:{w}px">{"".join(out)}</div>'


def cell(n, cap, note, stage, h, wide=False):
    span = ' style="grid-column:1 / span 2"' if wide else ""
    return (f'<div class="cell"{span}><div class="cap"><b>{n}</b>{cap}</div>'
            + (f'<div class="note">{note}</div>' if note else "") + f'<div class="stage" style="height:{h}px">{stage}</div></div>')


def right_group(mode):
    items = [seg(["audio-line-mode", "karaoke"], mode, "modeseg")]
    if mode == 1:
        items += [btn("karaoke-split"), split("karaoke-tap")]
    items += [seg(["audio-waveform", "spectrum"], 0), SEP, btn("panel-menu"),
              f'<span class="b zoom">{icon("zoom")}<span class="ro">1.5×</span></span>', btn("volume-low")]
    return '<div class="strip">' + "".join(items) + '</div>'


def mode_steps(t):
    """Choosing Karaoke switches the audio panel; leaving puts Line mode back."""
    def mini(mode, label, sub):
        return (f'<div class="step"><div class="slab"><b>{label}</b><span>{sub}</span></div>'
                f'<div class="mstrip">{right_group(mode)}</div></div>')
    return ('<div class="steps">'
            + mini(0, "Editing, before", "Audio in Line mode, as the user left it")
            + '<div class="arrowdown">View › Layout › Karaoke, or the layout switcher</div>'
            + mini(1, "Karaoke chosen", "Karaoke mode on: Auto split and Tap show; the active Line splits into syllables")
            + '<div class="arrowdown">Editing (or Timing, Translation, Typesetting, a saved layout not made from Karaoke)</div>'
            + mini(0, "Karaoke left", "Line mode back, because Karaoke turned it on")
            + '</div>')


def sheet(t):
    cells = []
    cells.append(cell(1, "Karaoke (recommended, A): Video and the Template editor on top, Audio across the window, a short Grid below.",
        "View &gt; Layout &gt; Karaoke or the layout switcher, on a fresh profile or not, and Reset this layout from it, give this "
        "arrangement at the window's size. Upper row about half the height: Video in Editing's 44% on the left with KE2's live "
        "preview on the frame, the Template editor on the right with the Line editor as the tab behind it. Audio in Karaoke mode "
        "spans the whole width under them: the waveform is 1 270 px wide instead of Editing's 710, so at the same zoom each "
        "syllable is almost twice as wide. The Grid is short and its generated Lines are folded into one row.",
        karaoke_a(t), H + 2, wide=True))

    sw = menu([("check", "Editing", "", ""), (None, "Timing", "", ""),
               (None, "Translation", "", ""), (None, "Typesetting", "", ""),
               (None, "Karaoke", "", "hov"), "-", (None, "OP timing", "", ""),
               "-", ("layout-save", "Save layout as…", "", ""), ("layout-reset", "Reset this layout", "", ""),
               ("layout-manage", "Manage layouts…", "", "")], 0, 0, 290)
    stage2 = (f'<div style="position:absolute;left:0;top:0">{sw}</div>'
              f'<div style="position:absolute;left:304px;top:0;width:336px">{mode_steps(t)}</div>')
    cells.append(cell(2, "Choosing Karaoke turns Karaoke mode on; leaving it puts back the mode it replaced.",
        "Karaoke is the fifth preset in View &gt; Layout and the layout switcher. Choosing it, or a saved layout made from it, "
        "switches the audio panel to Karaoke mode silently (no question, unlike Translation: nothing in the file changes). "
        "Choosing another arrangement afterwards puts Line mode back, but only if Karaoke was the one that turned Karaoke mode "
        "on and the user hasn't switched it since. The Line | Karaoke switch keeps working in every layout. Showing a tab, "
        "opening a file, a Session's arrangement and Reset this layout switch nothing (TM1's rule).",
        stage2, 470))

    g_folded = grid_panel(640, 200, rows=[KARA[2], KARA[3], KARA[4], "fold"], hover_fold=True, compact=True)
    g_open = grid_panel(640, 238, rows=[KARA[3], KARA[4], "fold"], folded=False, compact=True)
    gm = menu([("check", "Fold generated Lines", "", "hov"), "-", ("panel-float", "Undock", "", ""),
               ("tab-close", "Close", "", "")], 60, 230 + 30, 230)
    stage3 = (f'<div style="position:absolute;left:0;top:0">{g_folded}</div>'
              f'<div class="tag" style="position:absolute;left:0;top:210px">Unfolded, with the Grid’s ⋯ menu open</div>'
              f'<div style="position:absolute;left:0;top:230px">{g_open}</div>{gm}')
    cells.append(cell(3, "Generated Lines fold into one row per run, and unfold with a click.",
        "Generated Lines are those the templater wrote (Effect <code>fx</code>) and Kara Effector's "
        "(<code>Effector [fx]</code>). Each run of them shows as one row with the Line numbers it covers, its time span, its Styles "
        "and how many Lines it holds. A click on the row's arrow, or → with the row current, unfolds it; ← or the arrow folds "
        "it again. Choosing Karaoke folds every run; the Grid's ⋯ menu has Fold generated Lines in any layout. Folding only "
        "hides rows: the Lines stay in the file, play on the video and are found by Find, which unfolds the run it lands in.",
        stage3, 470))

    alt = (f'<div style="display:flex;flex-direction:column;gap:8px"><div class="tag">B · Editing’s shape: Audio above the Template editor</div>'
           f'{scaled(karaoke_b(t), .48)}</div>')
    cells.append(cell(4, "Alternative B: Editing's shape, Audio above the Template editor on the right.",
        "The picture is about a fifth bigger (44% more area) and the panels keep Editing's places, but the waveform is 710 px wide "
        "instead of 1 270, "
        "and the audio strip folds its toggles into ⋯ to fit. Recommended is A: syllable timing needs width more than the "
        "preview needs height.", alt, 410))

    tod = (f'<div style="display:flex;flex-direction:column;gap:8px"><div class="tag">Today: Editing, Karaoke switched on by hand</div>'
           f'{scaled(today(t), .48)}</div>')
    cells.append(cell(5, "For comparison: karaoke work today, in Editing.",
        "Karaoke mode is turned on by hand, the Template editor (KE2) is the tab behind the Line editor, and the Grid lists all "
        "312 generated Lines after the 9 the user edits.", tod, 410))

    notes = ('<div class="legend">'
             '<p><b>No new panel and no new ids.</b> Karaoke arranges panels that exist once KE2 (#284) merges: Video, Audio, '
             'Template editor, Line editor, Grid. No hotkey or panel registry number is needed.</p>'
             '<p><b>Closed in Karaoke:</b> Styles, Search, Timing, History, Original text and the Log, each in its usual place; '
             'View &gt; Panels opens them. In Translation mode the Original text takes its place above the Line editor (TM1).</p>'
             '<p><b>Built like Typesetting:</b> the arrangement is built the first time Karaoke is shown in a session '
             '(TP1\'s <code>buildsArrangement</code>). A tab, Session, file or saved layout holding a Karaoke arrangement keeps '
             'its own panels until Karaoke is chosen again or the layout is reset.</p>'
             '<p><b>Without audio:</b> the Audio panel shows its Open audio placeholder and the mode is set, ready for the audio.</p>'
             '<p><b>Docs and tests:</b> docs/qt/ux/workspaces.md gets the Karaoke row and section. Tests cover the panel set on '
             'a fresh profile and after Reset, the mode switched on and put back, the fold and unfold, and Find unfolding.</p>'
             '</div>')
    cells.append(cell(6, "Questions for the owner, and notes", "", QUESTIONS + notes, 640, wide=True))
    return cells


QUESTIONS = """<div class="questions"><ol>
<li><b>Arrangement:</b> Video and the Template editor on top, Audio across the full width, a short Grid below (A, recommended), or Editing's shape with Audio above the Template editor (B).</li>
<li><b>The Line editor:</b> a tab behind the Template editor, where KE2 puts the Template editor today (recommended), or a panel of its own below the Template editor, which then loses its preview table.</li>
<li><b>Which Lines fold:</b> Effect <code>fx</code> (the templater) and <code>Effector [fx]</code> (the bundled Kara Effector) (recommended), or <code>fx</code> only.</li>
<li><b>A folded row:</b> one row per run of generated Lines, its arrow or →/← to unfold and fold; clicking the row selects every Line in the run, so Delete, Copy and Shift times act on the whole run (recommended). The alternative is a row that can't be selected, only unfolded.</li>
<li><b>Leaving Karaoke:</b> puts back Line mode and unfolds the runs, but only what Karaoke itself changed: a mode or fold the user changed while in Karaoke stays as they set it (recommended). The alternative is to keep Karaoke mode and the folds after leaving, as Translation keeps its mode.</li>
</ol></div>"""


def css(t):
    hc = t["hc"]
    sh = 0 if hc else .3
    return f"""
*{{box-sizing:border-box}}
html,body{{margin:0;background:{t['bg']};color:{t['text']};font-family:"Noto Sans","Noto Sans CJK JP","Segoe UI",sans-serif;font-size:13px;line-height:1.35}}
code,.mono{{font-family:"Noto Sans Mono","DejaVu Sans Mono",monospace;font-size:12px}}
.top{{padding:14px 18px 2px;display:flex;align-items:baseline;gap:14px}}
.top h1{{font-size:15px;margin:0;font-weight:650}}
.top span{{color:{t['muted']};font-size:12px}}
.sheet{{display:grid;grid-template-columns:640px 640px;gap:4px 24px;padding:10px 18px 18px}}
.cell{{position:relative;padding-bottom:10px}}
.cap{{font-size:12.5px;font-weight:600;margin:8px 0 6px 2px}}
.cap b{{display:inline-flex;width:18px;height:18px;align-items:center;justify-content:center;border:1px solid {t['control']};border-radius:9px;font-size:11px;margin-right:6px}}
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;max-width:1280px;min-height:62px}}
.stage{{position:relative}}
.win{{position:relative;background:{t['bg']};border:1px solid {t['line']};overflow:hidden;display:flex;flex-direction:column}}
.mbar{{height:28px;flex:none;display:flex;align-items:center;gap:2px;padding:0 4px;background:{t['bg']}}}
.mbi{{padding:3px 6px;border-radius:2px}}
.lsw{{display:inline-flex;align-items:center;gap:6px;height:22px;padding:0 6px;margin-left:8px;border:1px solid {t['control']};border-radius:3px}}
.lsw.open{{background:{t['select']};border-color:{t['accent']}}}
.dtabs{{height:32px;flex:none;display:flex;align-items:center;gap:6px;padding:0 4px;background:{t['bg']}}}
.dtab{{display:inline-flex;align-items:center;gap:8px;height:24px;padding:0 6px 0 8px;border:1px solid {t['control']};border-radius:3px;background:{t['panel']};font-size:12.5px}}
.dtab.cur{{border-color:{t['accent']}}}
.dplus{{color:{t['muted']};display:inline-flex}}
.area{{flex:1;display:flex;flex-direction:column;gap:2px;padding:0 2px;min-height:0}}
.row{{display:flex;gap:4px}}
.col{{display:flex;flex-direction:column;gap:2px}}
.dock{{position:relative;background:{t['panel']};border:1px solid {t['line'] if hc else t['raised']};display:flex;flex-direction:column;overflow:hidden;flex:none}}
.dhead{{height:35px;flex:none;display:flex;align-items:center;padding:0 8px 0 12px;position:relative}}
.dhead .t{{font-weight:700;flex:1}}
.dhead.tabs{{padding:0;background:{t['raised']};border-bottom:1px solid {t['line']}}}
.dt{{height:35px;display:inline-flex;align-items:center;gap:6px;padding:0 10px;border-right:1px solid {t['line']}}}
.dt.sel{{background:{t['panel']};font-weight:700;margin-bottom:-1px;height:36px;padding-right:6px}}
.dmore{{display:inline-flex;width:20px;height:20px;align-items:center;justify-content:center}}
.tslot{{display:flex;align-items:center;gap:2px;padding-right:6px}}
.dbody{{position:relative;flex:1;display:flex;flex-direction:column;min-height:0}}
.dbody.pad{{padding:8px 10px;gap:6px}}
.grow{{flex:1}}
.mut{{color:{t['muted']}}}
.sep{{width:1px;height:18px;background:{t['line']};margin:0 3px;flex:none}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none;color:{t['text']}}}
.ic svg{{width:16px;height:16px}}
.ic.sm,.ic.sm svg{{width:12px;height:12px}}
.ic.mut{{color:{t['muted']}}}
svg #accent,svg g[id=accent]{{color:{t['accent']}}}
.b{{position:relative;height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;gap:4px;border:1px solid transparent;border-radius:3px;color:{t['text']};flex:none}}
.b.on{{background:{t['select']};border-color:{t['accent'] if hc else t['control']}}}
.b.tg{{background:{t['select']};border-color:{t['accent'] if hc else 'transparent'};box-shadow:inset 0 -2px 0 {t['accent']}}}
.b.zoom{{padding:0 6px 0 4px}}
.b .ro{{font-size:11.5px;font-variant-numeric:tabular-nums}}
.split{{display:inline-flex;align-items:stretch;height:26px;flex:none}}
.split .face{{position:relative;width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-right:none;border-radius:3px 0 0 3px}}
.split .arrow{{width:13px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:0 3px 3px 0;color:{t['muted']};position:relative}}
.split .arrow::before{{content:"";position:absolute;left:-1px;top:6px;bottom:6px;width:1px;background:{t['line']}}}
.marks{{position:absolute;right:-1px;bottom:0;display:flex;gap:1px}}
.mk{{font-style:normal;font-size:8px;line-height:9px;font-weight:700;min-width:9px;height:9px;text-align:center;border-radius:2px;background:{t['accent']};color:{t['onaccent']};padding:0 1px}}
.seg{{display:inline-flex;height:26px;border:1px solid {t['control']};border-radius:3px;overflow:hidden;margin:0 2px;flex:none}}
.sg{{width:26px;display:inline-flex;align-items:center;justify-content:center}}
.sg+.sg{{border-left:1px solid {t['line']}}}
.sg.on{{background:{t['select']};box-shadow:inset 0 -2px 0 {t['accent']}}}
.tb{{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:2px;flex:none}}
.tb.on{{background:{t['checked']};border-color:{t['control']}}}
/* video */
.vtop{{display:flex;min-height:0;flex:none}}
.rail{{width:32px;display:flex;flex-direction:column;gap:1px;padding:2px;border-right:1px solid {t['line']};overflow:hidden}}
.rail .tb{{width:28px;height:24px}}
.vstage{{flex:1;display:flex;align-items:center;justify-content:center;background:#000;overflow:hidden}}
.picture{{position:relative;overflow:hidden;background:#000}}
.scene{{position:absolute;inset:0;background:linear-gradient(180deg,#26203f 0%,#3b3366 50%,#6a4a6a 72%,#2a2230 100%)}}
.kline{{position:absolute;left:0;right:0;top:13%;text-align:center;font-weight:700;letter-spacing:.04em;color:#e6e0ff;opacity:.92;text-shadow:0 0 3px #000,0 0 2px #000}}
.ks{{margin:0 .18em}}
.ks.done{{color:#ffb4e6}}
.ks.hi{{color:#ff69b4;font-size:125%}}
.vsub{{position:absolute;left:0;right:0;bottom:8%;text-align:center;color:#fff;font-weight:600;text-shadow:-1px -1px 0 #000,1px -1px 0 #000,-1px 1px 0 #000,1px 1px 0 #000}}
.vsub .red{{color:#ff5050}}
.vbadge{{position:absolute;left:6px;top:6px;display:inline-flex;align-items:center;gap:5px;padding:2px 7px;font-size:11.5px;background:{t['panel']};color:{t['text']};border:1px solid {t['line']};border-radius:2px}}
.vbadge .ic{{color:{t['text']}}}
.bar{{display:flex;align-items:center;gap:6px;height:30px;flex:none;padding:0 8px;background:{t['panel']};border-top:1px solid {t['line']};white-space:nowrap}}
.tool{{display:inline-flex;align-items:center;gap:6px;font-weight:650}}
.vsep{{width:1px;height:18px;background:{t['line']};flex:none}}
.seek{{height:14px;margin:4px 8px 0;position:relative;flex:none}}
.seek::before{{content:"";position:absolute;left:0;right:0;top:6px;height:3px;background:{t['raised']};border:1px solid {t['line']}}}
.seek i{{position:absolute;left:0;top:6px;height:3px;background:{t['accent']}}}
.seek b{{position:absolute;top:2px;width:10px;height:10px;margin-left:-5px;border-radius:50%;background:{t['text']}}}
.times{{font-size:12px;padding:2px 10px;font-variant-numeric:tabular-nums;flex:none}}
.trans{{display:flex;align-items:center;gap:2px;padding:0 8px 4px;flex:none}}
.status{{text-align:center;font-size:12px;color:{t['muted']}}}
.vol{{width:60px;height:3px;background:{t['raised']};border:1px solid {t['line']};position:relative;margin:0 4px 0 6px}}
.vol i{{position:absolute;left:0;top:0;bottom:0;width:70%;background:{t['accent']}}}
/* template editor */
.te{{gap:4px;padding-bottom:6px}}
.sh{{display:flex;align-items:center;gap:8px;flex:none;height:22px}}
.pbtn{{display:inline-flex;align-items:center;gap:5px;height:24px;padding:0 10px;border:1px solid {t['control']};border-radius:3px;background:{t['raised']};font-size:12.5px}}
.tlist{{flex:none;border:1px solid {t['line']};background:{t['field']}}}
.trow{{display:flex;align-items:center;gap:10px;height:22px;padding:0 8px;white-space:nowrap;border-bottom:1px solid {t['raised'] if not hc else t['disabled']}}}
.trow:last-child{{border-bottom:none}}
.trow.sel{{background:{t['select']};box-shadow:inset 3px 0 0 {t['accent']}}}
.tn{{width:16px;text-align:right;color:{t['muted']};font-size:12px}}
.tk{{width:168px;overflow:hidden;text-overflow:ellipsis;font-family:"Noto Sans Mono","DejaVu Sans Mono",monospace;font-size:12px}}
.tst{{width:78px;font-size:12px;white-space:nowrap}}
.tx{{flex:1;overflow:hidden;text-overflow:ellipsis}}
.tu{{font-size:12px;color:{t['muted']};font-variant-numeric:tabular-nums}}
.h-kw{{color:{t['kw']};font-weight:700}}.h-kv{{color:{t['kv']}}}.h-tag{{color:{t['tg']}}}.h-var{{color:{t['warning']}}}.h-lua{{color:{t['warning']};font-weight:700}}
.irow{{display:flex;gap:5px;align-items:center;flex:none}}
.lbl{{font-size:12.5px}}
.in{{height:24px;display:inline-flex;align-items:center;gap:4px;padding:0 6px;background:{t['field']};border:1px solid {t['control']};border-radius:3px;font-size:12.5px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden}}
.in.dd{{justify-content:space-between}}
.in.ph{{color:{t['muted']}}}
.w38{{width:40px}}.w84{{width:84px}}.w120{{width:118px}}
.code{{flex:none;background:{t['field']};border:1px solid {t['control']};border-radius:3px;padding:4px 8px;line-height:1.45;word-break:break-all;height:42px;overflow:hidden}}
.hint{{font-size:11.5px;color:{t['muted']};flex:none}}
.kc{{display:inline-block;font-size:10.5px;line-height:14px;padding:0 4px;border:1px solid {t['control']};border-bottom-width:2px;border-radius:3px;background:{t['raised']};font-weight:600;color:{t['text']}}}
.ptab{{flex:none;border:1px solid {t['line']};background:{t['field']}}}
.prow{{display:flex;gap:10px;height:21px;align-items:center;padding:0 8px;white-space:nowrap;font-size:12px;font-variant-numeric:tabular-nums}}
.prow span:nth-child(1){{width:12px}}.prow span:nth-child(2),.prow span:nth-child(3){{width:70px}}
.prow span:nth-child(4){{flex:1;overflow:hidden;text-overflow:ellipsis}}
.prow.ph{{color:{t['muted']};border-bottom:1px solid {t['line']}}}
.foot{{display:flex;align-items:center;flex:none;margin-top:auto}}
.apply{{display:inline-flex;height:26px}}
.apply .af{{display:inline-flex;align-items:center;padding:0 22px;background:{t['accent']};color:{t['onaccent']};border-radius:3px 0 0 3px;font-weight:600}}
.apply .aa{{display:inline-flex;align-items:center;padding:0 6px;background:{t['accent']};color:{t['onaccent']};border-left:1px solid {t['onaccent']};border-radius:0 3px 3px 0}}
/* line editor (behind the Template editor) */
.tstrip{{display:flex;gap:1px;align-items:center;flex:none}}
.counters{{font-size:12.5px;flex:none}}
.fld{{background:{t['field']};border:1px solid {t['control']};border-radius:3px;padding:5px 9px;font-size:13px;flex:none;white-space:nowrap;overflow:hidden}}
.fld.focus{{border-color:{t['accent']};box-shadow:0 0 0 1px {t['accent']}}}
.caret{{display:inline-block;width:1px;height:15px;background:{t['text']};vertical-align:-3px;margin-left:1px}}
.chk{{width:14px;height:14px;border:1px solid {t['control']};border-radius:2px;display:inline-flex;flex:none}}
.chk.on{{background:{t['accent']};box-shadow:inset 0 0 0 2px {t['field']}}}
/* audio */
.dispwrap{{position:relative;overflow:hidden;flex:none}}
.disp{{display:block}}
.scroll{{height:8px;margin:0;background:{t['raised']};position:relative;flex:none}}
.scroll .thumb{{position:absolute;top:1px;bottom:1px;background:{t['control']};border-radius:3px}}
.strip{{display:flex;align-items:center;height:30px;padding:0 4px;margin-top:3px;white-space:nowrap;gap:2px}}
.grp{{display:flex;align-items:center;gap:2px}}
.modeseg{{box-shadow:0 0 0 2px {t['accent'] if hc else 'transparent'}}}
/* grid */
table.grid{{border-collapse:collapse;width:100%;font-size:12.5px;table-layout:fixed}}
.grid th{{text-align:left;font-weight:400;color:{t['muted']};padding:2px 6px;background:{t['raised']};border-bottom:1px solid {t['line']};height:21px}}
.grid td{{padding:0 6px;height:21px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;font-variant-numeric:tabular-nums;border-bottom:1px solid {t['raised'] if not hc else t['disabled']}}}
.grid th:nth-child(1){{width:40px}}.grid th:nth-child(2){{width:26px}}.grid th:nth-child(3),.grid th:nth-child(4){{width:80px}}
.grid th:nth-child(5){{width:150px}}.grid th:nth-child(6){{width:140px}}
.grid.compact th:nth-child(1){{width:52px}}.grid.compact th:nth-child(5){{width:84px}}.grid.compact th:nth-child(6){{width:60px}}
.grid tr.cmt td{{background:{t['comment']}}}
.grid tr.cur td{{background:{t['select']}}}
.grid tr.cur td:first-child{{box-shadow:inset 3px 0 0 {t['accent']}}}
.grid tr.fx td{{color:{t['muted']}}}
.grid .tg{{color:{t['muted']}}}
.grid tr.fold td{{border-top:1px dashed {t['control']};border-bottom:1px dashed {t['control']};color:{t['muted']}}}
.grid tr.fold.hov td{{background:{t['raised'] if not hc else t['select']}}}
.grid tr.fold.open td{{border-bottom:1px solid {t['raised'] if not hc else t['disabled']}}}
.frange{{font-size:12px}}
.fbox{{display:inline-flex;align-items:center;gap:6px}}
.fbox .ic{{color:{t['text']}}}
.fbox b{{color:{t['text']};font-weight:650}}
.fm{{font-size:12px}}
.sbar{{height:24px;flex:none;display:flex;align-items:center;gap:16px;padding:0 10px 0 240px;font-size:12px;background:{t['bg']}}}
.scaled{{overflow:hidden;border:1px solid {t['line']}}}
.tag{{font-size:11px;color:{t['muted']};font-weight:600;text-transform:uppercase;letter-spacing:.04em}}
.menu{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{sh})}}
.mi{{display:flex;align-items:center;gap:8px;height:28px;padding:0 12px 0 10px}}
.mi .mt{{flex:1;white-space:nowrap}}
.mi .mk2{{color:{t['muted']};font-size:12px;white-space:nowrap}}
.mi.hov{{background:{t['select']};box-shadow:inset 3px 0 0 {t['accent']}}}
.mh{{font-size:11px;color:{t['muted']};padding:4px 12px 2px;font-weight:600}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
/* mode steps */
.steps{{display:flex;flex-direction:column;gap:6px}}
.step{{border:1px solid {t['line']};background:{t['panel']};padding:6px 6px 4px}}
.slab{{display:flex;flex-direction:column;font-size:12px;margin:0 2px 2px}}
.slab span{{color:{t['muted']};font-size:11.5px}}
.mstrip .strip{{margin:0;padding:0 2px}}
.arrowdown{{font-size:11.5px;color:{t['muted']};padding-left:22px;position:relative}}
.arrowdown::before{{content:"↓";position:absolute;left:6px;top:-1px;color:{t['accent']};font-weight:700}}
.legend{{font-size:12px;color:{t['muted']};line-height:1.5;columns:2;column-gap:24px}}
.legend p{{margin:0 0 9px;break-inside:avoid}}
.legend b{{color:{t['text']}}}
.questions{{margin:0 0 12px;padding:10px 14px 6px;border:1px solid {t['line']};background:{t['panel']};font-size:12.5px}}
.questions ol{{margin:0;padding-left:20px}}
.questions li{{margin:0 0 6px}}
"""


def page(t):
    cells = sheet(t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>KP1 Karaoke — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>KP1 Karaoke preset and mode — {t["name"]}</h1>'
            f'<span>Mockup for #286 at 1280 × 800. Real icon set (with KE2\'s Template editor and Loop); no new glyphs.</span></div>'
            f'<div class="sheet">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"kp1-{key}.html").write_text(page(t))
print("wrote", ", ".join(f"kp1-{k}.html" for k in THEMES))
