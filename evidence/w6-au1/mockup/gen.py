#!/usr/bin/env python3
"""AU1 #267 audio panel mockup: one HTML sheet per theme, screenshotted by shoot.sh.

usage: gen.py <src/ui/icons> <out dir>
"""
import math
import pathlib
import random
import re
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# theme.cpp baseRoles + the default accent picks; audio = Content::audio.
THEMES = {
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", disabled="#75818D", hc=False,
                 a_bg="#36393E", a_wave="#202225", a_inact="#2F3136", a_scale="#202225", a_scaletext="#AEAFB2",
                 a_start="#940000", a_inbound="#00D77D", a_cursor="#8791FD", a_key="#F4F4F4",
                 a_sbg="#000000", a_secho="#674FD7", a_sinner="#F4F4F4", a_syl="#202225", a_syltext="#8791FD"),
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", disabled="#74808B", hc=False,
                  a_bg="#DDE3E8", a_wave="#5B6774", a_inact="#9AA5B0", a_scale="#EDF0F3", a_scaletext="#526170",
                  a_start="#940000", a_inbound="#00875A", a_cursor="#4F56D6", a_key="#2E3A46",
                  a_sbg="#FFFFFF", a_secho="#674FD7", a_sinner="#1A1F26", a_syl="#3A4652", a_syltext="#E0E3FF"),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E", hc=True,
                    a_bg="#FFFFFF", a_wave="#303030", a_inact="#8C8C8C", a_scale="#FFFFFF", a_scaletext="#000000",
                    a_start="#C00000", a_inbound="#007A3D", a_cursor="#8000C0", a_key="#000000",
                    a_sbg="#FFFFFF", a_secho="#0050C8", a_sinner="#000000", a_syl="#000000", a_syltext="#FFFFFF"),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", disabled="#8C8C8C", hc=True,
                    a_bg="#000000", a_wave="#C8C8C8", a_inact="#6E6E6E", a_scale="#000000", a_scaletext="#FFFFFF",
                    a_start="#FF4040", a_inbound="#00FF80", a_cursor="#00FFFF", a_key="#FFFFFF",
                    a_sbg="#000000", a_secho="#00C8FF", a_sinner="#FFFFFF", a_syl="#FFFFFF", a_syltext="#000000"),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Icons this card would add (drawn in the set's 16 px, 1 px stroke style).
NEW_ICONS = {
    # Line timing mode: one span between a start and an end boundary.
    "audio-line-mode": SVG_HEAD + '<path d="M2.5 3.5v9M13.5 3.5v9"/><g id="accent"><rect x="4" y="6.5" width="8" height="3" '
                       'fill="currentColor" stroke="none"/></g></svg>',
    "waveform": SVG_HEAD + '<path d="M1.5 7v2M3.5 5v6M5.5 2.5v11M7.5 5.5v5M9.5 3.5v9M11.5 6v4M13.5 4.5v7"/></svg>',
    "volume-mute": SVG_HEAD + '<path d="M1.5 6h2.5l3.5-3v10L4 10H1.5z"/><g id="accent"><path d="M10.5 6l4 4M14.5 6l-4 4"/></g></svg>',
    "volume-low": SVG_HEAD + '<path d="M1.5 6h2.5l3.5-3v10L4 10H1.5z"/><g id="accent"><path d="M10 5.5a3.5 3.5 0 0 1 0 5"/></g></svg>',
    "volume-high": SVG_HEAD + '<path d="M1.5 6h2.5l3.5-3v10L4 10H1.5z"/><g id="accent"><path d="M10 5.5a3.5 3.5 0 0 1 0 5M12 3.5a6.25 6.25 0 0 1 0 9"/></g></svg>',
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "reset": SVG_HEAD + '<path d="M3 3.5v3.5h3.5"/><path d="M3.5 7A5 5 0 1 1 4.5 11.5"/></svg>',
}


def svg(role):
    if role in NEW_ICONS:
        return NEW_ICONS[role]
    return (ICONS / f"{role}.svg").read_text().strip()


def icon(role, cls=""):
    return f'<span class="ic {cls}">{svg(role)}</span>'


CHEVRON = ('<svg width="8" height="8" viewBox="0 0 8 8" fill="none" stroke="currentColor" stroke-width="1" '
           'stroke-linecap="round" stroke-linejoin="round"><path d="M1.5 3l2.5 2.5 2.5-2.5"/></svg>')


def btn(role, cls="", title=""):
    return f'<span class="b {cls}" title="{title}">{icon(role)}</span>'


def split(role, cls="", marks="", menu_open=False):
    o = " open" if menu_open else ""
    return (f'<span class="split {cls}{o}"><span class="face">{icon(role)}{marks}</span>'
            f'<span class="arrow">{CHEVRON}</span></span>')


def seg(items, current):
    out = ['<span class="seg">']
    for i, role in enumerate(items):
        out.append(f'<span class="sg{" on" if i == current else ""}">{icon(role)}</span>')
    out.append('</span>')
    return "".join(out)


SEP = '<span class="sep"></span>'

PLAY_AROUND = [
    # role, text, keys
    ("play-before-start", "Play 500 ms before the start", "Q"),
    ("play-after-start", "Play the first 500 ms", "E"),
    ("play-before-end", "Play the last 500 ms", "D"),
    ("play-after-end", "Play 500 ms after the end", "W"),
    ("play-to-end", "Play to the end", "T"),
    None,
    ("play-before-mark", "Play before the mark", "Num 0"),
    ("play-after-mark", "Play after the mark", "Num ."),
]


def strip(t, *, mode=0, view=0, play="play-before-start", auto=True, nxt=False, vol="volume-high",
          zoom="1.5×", open_menu=None, hover=None, narrow=False):
    """The button row. mode 0 Line / 1 Karaoke, view 0 Waveform / 1 Spectrum."""
    marks = ""
    if auto or nxt:
        marks = '<span class="marks">' + (
            '<i class="mk">A</i>' if auto else "") + ('<i class="mk">↓</i>' if nxt else "") + '</span>'
    h = lambda k: " hov" if hover == k else ""
    left = [
        btn("audio-previous-line", h("prev")), btn("audio-play", h("play")), btn("play-line", h("playline")),
        btn("media-stop", h("stop")), btn("audio-next-line", h("next")), SEP,
        split(play, h("around"), menu_open=open_menu == "around"), SEP,
        btn("lead-in", h("in")), btn("lead-out", h("out")),
        split("commit", h("commit"), marks, menu_open=open_menu == "commit"),
    ]
    if narrow:
        right = [btn("panel-menu", "on" if open_menu == "overflow" else "", "More"),
                 f'<span class="b zoom{" on" if open_menu == "zoom" else ""}">{icon("zoom")}<span class="ro">{zoom}</span></span>',
                 f'<span class="b{" on" if open_menu == "vol" else ""}">{icon(vol)}</span>']
    else:
        right = [seg(["audio-line-mode", "karaoke"], mode)]
        if mode == 1:
            right.append(btn("karaoke-split", "tg" + h("split")))
        right += [seg(["waveform", "spectrum"], view), SEP,
                  btn("auto-scroll", "tg" + h("scroll")), btn("audio-video-sync", "tg" + h("sync")),
                  btn("spectrum-nonlinear", h("speech")), SEP,
                  f'<span class="b zoom{" on" if open_menu == "zoom" else ""}{h("zoom")}">{icon("zoom")}<span class="ro">{zoom}</span></span>',
                  f'<span class="b{" on" if open_menu == "vol" else ""}{h("vol")}">{icon(vol)}</span>']
    return ('<div class="strip"><div class="grp">' + "".join(left) + '</div><div class="grow"></div>'
            '<div class="grp">' + "".join(right) + '</div></div>')


def waveform(t, w, h, *, spectrum=False, karaoke=False, sel=(0.36, 0.62), seed=3, chip=None, ruler0=45):
    """The audio display: ruler on top, waveform/spectrum, Line selection and marks."""
    rnd = random.Random(seed)
    disp_h = h - 18
    mid = 18 + disp_h / 2
    parts = [f'<svg class="disp" width="{w}" height="{h}" viewBox="0 0 {w} {h}" xmlns="http://www.w3.org/2000/svg">']
    bg = t["a_sbg"] if spectrum else t["a_bg"]
    parts.append(f'<rect x="0" y="0" width="{w}" height="{h}" fill="{bg}"/>')
    # ruler
    parts.append(f'<rect x="0" y="0" width="{w}" height="18" fill="{t["a_scale"]}"/>')
    step = w / 12
    for i in range(13):
        x = i * step
        parts.append(f'<line x1="{x:.1f}" y1="12" x2="{x:.1f}" y2="18" stroke="{t["a_scaletext"]}" stroke-width="1"/>')
        parts.append(f'<text x="{x + 3:.1f}" y="11" font-size="10" fill="{t["a_scaletext"]}" '
                     f'font-family="Noto Sans, Segoe UI, sans-serif">{ruler0 + i}</text>')
    sx0, sx1 = (sel[0] * w, sel[1] * w) if sel else (None, None)
    if sel:
        parts.append(f'<rect x="{sx0:.1f}" y="18" width="{sx1 - sx0:.1f}" height="{disp_h}" fill="{t["accent"]}" '
                     f'fill-opacity="0.22"/>')
    if spectrum:
        for x in range(0, w, 3):
            for b in range(0, disp_h, 4):
                f = b / disp_h
                env = 0.5 + 0.5 * math.sin(x / 37.0 + seed) * math.cos(x / 13.0)
                v = max(0.0, env * (1 - f) ** 1.6 + rnd.random() * 0.18 - 0.1)
                if v < 0.12:
                    continue
                col = t["a_sinner"] if v > 0.55 else t["a_secho"]
                op = min(1.0, v * 1.3)
                parts.append(f'<rect x="{x}" y="{18 + disp_h - b - 4}" width="3" height="4" fill="{col}" '
                             f'fill-opacity="{op:.2f}"/>')
    else:
        for x in range(0, w, 2):
            env = 0.25 + 0.75 * abs(math.sin(x / 41.0 + seed)) * (0.6 + 0.4 * math.sin(x / 9.0))
            if (x // 70) % 4 == 3:
                env *= 0.18
            a = env * (disp_h / 2 - 4) * (0.6 + 0.4 * rnd.random())
            inside = sel and sx0 <= x <= sx1
            col = t["accent"] if inside else t["a_wave"]
            parts.append(f'<line x1="{x}" y1="{mid - a:.1f}" x2="{x}" y2="{mid + a:.1f}" stroke="{col}" '
                         f'stroke-width="1.6"/>')
    # keyframes (dashed)
    for kx in (0.18, 0.79):
        parts.append(f'<line x1="{kx * w:.1f}" y1="18" x2="{kx * w:.1f}" y2="{h}" stroke="{t["a_key"]}" '
                     f'stroke-dasharray="3 3" stroke-width="1" opacity="0.7"/>')
    # inactive neighbour boundaries
    for ix in (0.12, 0.30, 0.68, 0.92):
        parts.append(f'<line x1="{ix * w:.1f}" y1="18" x2="{ix * w:.1f}" y2="{h}" stroke="{t["a_inbound"]}" '
                     f'stroke-width="1" stroke-dasharray="1 2"/>')
    if sel:
        for x in (sx0, sx1):
            parts.append(f'<line x1="{x:.1f}" y1="18" x2="{x:.1f}" y2="{h}" stroke="{t["a_start"]}" stroke-width="2"/>')
        if karaoke:
            sy = [0.0, 0.17, 0.35, 0.52, 0.71, 1.0]
            syl = ["ki", "mi", "no", "na", "mae"]
            for i in range(1, len(sy) - 1):
                x = sx0 + (sx1 - sx0) * sy[i]
                parts.append(f'<line x1="{x:.1f}" y1="18" x2="{x:.1f}" y2="{h}" stroke="{t["a_syl"]}" '
                             f'stroke-width="1"/>')
            for i, s in enumerate(syl):
                x = sx0 + (sx1 - sx0) * (sy[i] + sy[i + 1]) / 2
                parts.append(f'<text x="{x:.1f}" y="34" text-anchor="middle" font-size="12" font-weight="600" '
                             f'fill="{t["a_syltext"]}" font-family="Noto Sans, Segoe UI, sans-serif">{s}</text>')
    # play cursor
    cx = 0.47 * w if sel else 0.55 * w
    parts.append(f'<line x1="{cx:.1f}" y1="18" x2="{cx:.1f}" y2="{h}" stroke="{t["a_cursor"]}" stroke-width="1"/>')
    parts.append('</svg>')
    out = "".join(parts)
    if chip:
        out += chip
    return out


def scrollbar(pos=0.45, size=0.18):
    return (f'<div class="scroll"><div class="thumb" style="left:{pos * 100:.0f}%;width:{size * 100:.0f}%"></div></div>')


def panel(t, w, body, *, height=212, extra="", title="Audio"):
    return (f'<div class="dock" style="width:{w}px">'
            f'<div class="dhead"><span class="t">{title}</span><span class="b">{icon("panel-menu")}</span></div>'
            f'<div class="dbody" style="height:{height}px">{body}</div>{extra}</div>')


def audio_body(t, w, height, strip_html, **wave):
    disp_h = height - 4 - 8 - 30 - 6
    return (f'<div class="dispwrap" style="height:{disp_h}px">{waveform(t, w - 2, disp_h, **wave)}</div>'
            f'{scrollbar()}{strip_html}')


def menu(rows, *, left, top, width=268, cls=""):
    out = [f'<div class="menu {cls}" style="left:{left}px;top:{top}px;width:{width}px">']
    for r in rows:
        out.append(r)
    out.append('</div>')
    return "".join(out)


def mrow(role, text, keys="", *, checked=None, current=False, hover=False, dis=False):
    c = " cur" if current else ""
    c += " hov" if hover else ""
    c += " dis" if dis else ""
    lead = ""
    if checked is not None:
        lead = f'<span class="cb{" on" if checked else ""}">{svg("check") if checked else ""}</span>'
    ic = icon(role) if role else '<span class="ic"></span>'
    return (f'<div class="mi{c}">{lead}{ic}<span class="mt">{text}</span>'
            f'<span class="mk2">{keys}</span></div>')


MSEP = '<div class="msep"></div>'


def tip(text, *, left, top):
    return f'<div class="tip" style="left:{left}px;top:{top}px">{text}</div>'


def cell(n, caption, inner, note=""):
    note_html = f'<div class="note">{note}</div>' if note else ""
    return (f'<div class="cell"><div class="cap"><b>{n}</b> {caption}</div>{note_html}<div class="stage">{inner}</div></div>')


STAGE = 0
W = 640  # the default Workspace's audio dock at 1280 px (see AV evidence)
H = 196


def strip_x(n_left_buttons):
    return n_left_buttons


def sheet(key, t):
    cells = []
    # 1 default
    s = strip(t, hover="around")
    body = audio_body(t, W, H, s)
    tp = tip("Play 500 ms before the start (Q)<br><span class='dim'>Click repeats the last choice; the arrow lists all seven</span>",
             left=160, top=H + 40)
    cells.append(cell(1, "Default: Line mode, Waveform. The display takes the slider column's width.",
                      panel(t, W, body, extra=tp),
                      "Left: transport (Previous line, Play, Play line, Stop, Next line), <b>Play around</b>, "
                      "lead-in, lead-out, <b>Commit</b>. Right: Line | Karaoke, Waveform | Spectrum, "
                      "Auto-scroll · Sync with video · Speech, zoom with its readout, speaker with the level."))
    # 2 play around menu
    s = strip(t, open_menu="around", play="play-after-end")
    body = audio_body(t, W, H, s)
    rows = []
    for r in PLAY_AROUND:
        if r is None:
            rows.append(MSEP)
            continue
        rows.append(mrow(r[0], r[1], r[2], current=r[0] == "play-after-end", hover=r[0] == "play-to-end"))
    m = menu(rows, left=158, top=H + 38, width=286)
    cells.append(cell(2, "Play around: the arrow lists the seven plays with their hotkeys.",
                      panel(t, W, body, extra=m),
                      "The face shows the last choice (here 500 ms after the end, W) and a click on it plays that "
                      "again. Each hotkey keeps working without the menu."))
    # 3 commit menu
    s = strip(t, open_menu="commit", auto=True, nxt=True)
    body = audio_body(t, W, H, s)
    rows = [mrow("commit", "Commit", "Enter or G"), MSEP,
            mrow("auto-commit", "Auto-commit", "", checked=True),
            mrow("next-after-commit", "Go to the next line after commit", "", checked=True, hover=True)]
    m = menu(rows, left=269, top=H + 38, width=300)
    cells.append(cell(3, "Commit: the arrow holds Auto-commit and Next-after-commit.",
                      panel(t, W, body, extra=m),
                      "The small marks on the face (<b>A</b> auto-commit, <b>↓</b> next line) show which are on, "
                      "as shapes, not colour alone. The tooltip names them too."))
    # 4 speaker popover
    s = strip(t, open_menu="vol", vol="volume-low")
    body = audio_body(t, W, H, s)
    pop = (f'<div class="pop" style="left:{W - 300}px;top:{H + 38}px;width:298px">'
           f'<div class="prow"><span class="pl">Volume</span><span class="slider"><span class="fill" style="width:38%"></span>'
           f'<span class="knob" style="left:38%"></span></span><span class="val">38</span></div>'
           f'<div class="phint">Linked to vertical stretch</div>'
           f'<div class="msep"></div>'
           f'<div class="phead">Output device</div>'
           + mrow(None, "System default (Built-in Audio)", "", checked=False)
           + mrow(None, "Built-in Audio Analog Stereo", "", checked=False)
           + mrow(None, "USB Headset", "", checked=True, hover=True)
           + MSEP + mrow("settings-audio", "Audio settings…", "")
           + '</div>')
    cells.append(cell(4, "Speaker: volume beside the AV1 device list; the icon shows the level.",
                      panel(t, W, body, extra=pop),
                      "Mute, low and high level icons (new). Wheel over the speaker changes the volume. "
                      "\"Linked to vertical stretch\" shows only while Link is on."))
    # 5 zoom popover
    s = strip(t, open_menu="zoom")
    body = audio_body(t, W, H, s)
    pop = (f'<div class="pop" style="left:{W - 350}px;top:{H + 38}px;width:340px">'
           f'<div class="prow"><span class="pl">Horizontal</span><span class="slider"><span class="fill" style="width:62%"></span>'
           f'<span class="knob" style="left:62%"></span></span><span class="val">1.5×</span></div>'
           f'<div class="prow"><span class="pl">Vertical</span><span class="slider"><span class="fill" style="width:38%"></span>'
           f'<span class="knob" style="left:38%"></span></span><span class="val">38</span></div>'
           f'<div class="prow"><span class="pl"></span><span class="chk on">{svg("check")}</span>'
           f'<span class="cl">Link volume to vertical stretch</span></div>'
           f'<div class="msep"></div>'
           f'<div class="prow foot"><span class="hint">Shift+wheel and Ctrl+wheel on the waveform</span>'
           f'<span class="pbtn">{icon("reset")}Reset</span></div>'
           '</div>')
    cells.append(cell(5, "Zoom: horizontal and vertical stretch and Link in one button with a readout.",
                      panel(t, W, body, extra=pop),
                      "The readout is the horizontal stretch against the default (1×). Shift+wheel and "
                      "Ctrl+wheel on the waveform zoom as before, and the readout follows."))
    # 6 back to line chip
    s = strip(t)
    chip = (f'<div class="chip" style="left:8px;top:28px">{icon("move-left")}<span>Back to line</span>'
            f'<span class="ck">B</span></div>')
    body = audio_body(t, W, H, s, sel=None, chip=chip, ruler0=63)
    cells.append(cell(6, "Back to line: shown only while the current Line is out of view.",
                      panel(t, W, body),
                      "The chip sits on the edge the Line lies beyond (left here) and points to it; a click scrolls "
                      "back. It hides again once the Line is in view. B keeps working everywhere."))
    # 7 karaoke + spectrum
    s = strip(t, mode=1, view=1, hover="split")
    body = audio_body(t, W, H, s, spectrum=True, karaoke=True)
    tp = tip("Auto split syllables", left=W - 236, top=H + 40)
    cells.append(cell(7, "Karaoke mode in Spectrum: Auto split appears beside the switch.",
                      panel(t, W, body, extra=tp),
                      "Auto split shows only in Karaoke mode. Speech enhances the spectrum's speech frequencies "
                      "and stays in the group in both views (it changes only what the Spectrum view draws)."))
    # 8 narrow
    NW = 440
    s = strip(t, narrow=True, open_menu="overflow")
    body = audio_body(t, NW, H, s)
    rows = [mrow("audio-line-mode", "Line", "", checked=True), mrow("karaoke", "Karaoke", "", checked=False), MSEP,
            mrow("waveform", "Waveform", "", checked=True), mrow("spectrum", "Spectrum", "", checked=False), MSEP,
            mrow("auto-scroll", "Auto-scroll", "", checked=True),
            mrow("audio-video-sync", "Sync with video", "", checked=True, hover=True),
            mrow("spectrum-nonlinear", "Speech (non-linear spectrum)", "", checked=False)]
    m = menu(rows, left=NW - 286, top=H + 38, width=270)
    cells.append(cell(8, "A narrow dock (440 px): the right-hand switches fold into ⋯ instead of clipping.",
                      panel(t, NW, body, extra=m),
                      "Below the width the full row needs, the switches and toggles move into a ⋯ menu in the same "
                      "order. Transport, Play around, the timing group, zoom and speaker always stay."))
    return cells


def css(t):
    hc = t["hc"]
    STAGE = H + 37 + 262
    return f"""
*{{box-sizing:border-box}}
html,body{{margin:0;background:{t['bg']};color:{t['text']};font-family:"Noto Sans","Segoe UI",sans-serif;font-size:13px;line-height:1.35}}
.top{{padding:14px 18px 2px;display:flex;align-items:baseline;gap:14px}}
.top h1{{font-size:15px;margin:0;font-weight:650}}
.top span{{color:{t['muted']};font-size:12px}}
.grid{{display:grid;grid-template-columns:660px 660px;gap:4px 22px;padding:10px 18px 18px}}
.cell{{position:relative;padding-bottom:6px}}
.cap{{font-size:12px;font-weight:600;margin:8px 0 6px 2px}}
.cap b{{display:inline-flex;width:18px;height:18px;align-items:center;justify-content:center;border:1px solid {t['control']};border-radius:9px;font-size:11px;margin-right:4px}}
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;max-width:650px;min-height:32px}}
.stage{{position:relative;height:{STAGE}px}}
.dock{{position:relative;background:{t['field']};border:1px solid {t['line']}}}
.dhead{{height:35px;display:flex;align-items:center;padding:0 8px 0 12px;border-bottom:1px solid {t['line']}}}
.dhead .t{{font-weight:700;flex:1}}
.dbody{{position:relative;display:flex;flex-direction:column}}
.dispwrap{{position:relative;overflow:hidden}}
.disp{{display:block}}
.scroll{{height:6px;margin:4px 0 0;background:{t['field']};border:1px solid {t['line']};border-radius:3px;position:relative}}
.scroll .thumb{{position:absolute;top:-1px;bottom:-1px;background:{t['control']};border-radius:3px}}
.strip{{display:flex;align-items:center;height:30px;padding:0 4px;margin-top:4px}}
.grp{{display:flex;align-items:center;gap:2px}}
.grow{{flex:1}}
.sep{{width:1px;height:18px;background:{t['line']};margin:0 3px}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none}}
.ic svg{{width:16px;height:16px}}
svg #accent{{color:{t['accent']}}}
.b{{height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;gap:4px;border:1px solid transparent;border-radius:3px;color:{t['text']}}}
.b.hov,.split.hov .face,.split.hov .arrow{{background:{t['raised']};border-color:{t['control']}}}
.b.on,.split.open .face,.split.open .arrow{{background:{t['select']};border-color:{t['accent'] if hc else t['control']}}}
.b.tg{{background:{t['select']};border-color:{t['accent'] if hc else 'transparent'};box-shadow:inset 0 -2px 0 {t['accent']}}}
.b.tg.hov{{border-color:{t['control']}}}
.b.zoom{{padding:0 6px 0 4px}}
.b .ro{{font-size:11.5px;font-variant-numeric:tabular-nums;color:{t['text']}}}
.split{{display:inline-flex;align-items:stretch;height:26px}}
.split .face{{position:relative;width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-right:none;border-radius:3px 0 0 3px}}
.split .arrow{{width:13px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:0 3px 3px 0;color:{t['muted']};position:relative}}
.split .arrow::before{{content:"";position:absolute;left:-1px;top:6px;bottom:6px;width:1px;background:{t['line']}}}
.marks{{position:absolute;right:-1px;bottom:0px;display:flex;gap:1px}}
.mk{{font-style:normal;font-size:8px;line-height:9px;font-weight:700;min-width:9px;height:9px;text-align:center;border-radius:2px;background:{t['accent']};color:{t['onaccent']}}}
.seg{{display:inline-flex;height:26px;border:1px solid {t['control']};border-radius:3px;overflow:hidden;margin:0 2px}}
.sg{{width:26px;display:inline-flex;align-items:center;justify-content:center;color:{t['text']}}}
.sg+.sg{{border-left:1px solid {t['line']}}}
.sg.on{{background:{t['select']};box-shadow:inset 0 -2px 0 {t['accent']}}}
.menu,.pop{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{0 if hc else .28})}}
.mi{{display:flex;align-items:center;gap:8px;height:28px;padding:0 12px 0 10px}}
.mi .mt{{flex:1;white-space:nowrap}}
.mi .mk2{{color:{t['muted']};font-size:12px;white-space:nowrap}}
.mi.hov{{background:{t['raised']}}}
.mi.cur{{background:{t['select']};font-weight:600;box-shadow:inset 3px 0 0 {t['accent']}}}
.mi.dis{{color:{t['disabled']}}}
.mi.dis svg #accent{{color:{t['disabled']}}}
.cb{{width:14px;height:14px;display:inline-flex;align-items:center;justify-content:center;flex:none}}
.cb svg{{width:14px;height:14px}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
.phead{{font-size:11px;color:{t['muted']};font-weight:600;padding:4px 12px 2px}}
.prow{{display:flex;align-items:center;gap:10px;height:30px;padding:0 12px}}
.prow .pl{{width:70px;color:{t['text']}}}
.prow .val{{width:36px;text-align:right;font-variant-numeric:tabular-nums;font-weight:600}}
.slider{{flex:1;height:4px;background:{t['line']};border-radius:2px;position:relative}}
.slider .fill{{position:absolute;left:0;top:0;bottom:0;background:{t['accent']};border-radius:2px}}
.slider .knob{{position:absolute;top:-6px;width:16px;height:16px;margin-left:-8px;border-radius:8px;background:{t['panel']};border:1px solid {t['control']}}}
.phint{{font-size:11px;color:{t['muted']};padding:0 12px 4px 92px;margin-top:-4px}}
.chk{{width:14px;height:14px;border:1px solid {t['control']};border-radius:2px;display:inline-flex;align-items:center;justify-content:center}}
.chk.on{{background:{t['accent']};border-color:{t['accent']};color:{t['onaccent']}}}
.chk svg{{width:12px;height:12px}}
.prow .cl{{flex:1}}
.prow.foot .hint{{flex:1;font-size:11px;color:{t['muted']}}}
.pbtn{{display:inline-flex;align-items:center;gap:5px;height:24px;padding:0 8px;border:1px solid {t['control']};border-radius:3px;background:{t['raised']};font-size:12px}}
.tip{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};padding:4px 8px;font-size:12px;border-radius:2px;max-width:300px}}
.tip .dim{{color:{t['muted']};font-size:11px}}
.chip{{position:absolute;display:inline-flex;align-items:center;gap:6px;height:26px;padding:0 8px 0 6px;background:{t['panel']};border:1px solid {t['accent'] if hc else t['control']};border-radius:13px;box-shadow:0 2px 8px rgba(0,0,0,{0 if hc else .3});font-size:12px;font-weight:600}}
.chip .ck{{font-weight:400;color:{t['muted']};font-size:11px;border:1px solid {t['line']};border-radius:2px;padding:0 4px}}
.chip svg path{{color:{t['accent']}}}
"""


def page(key, t):
    cells = sheet(key, t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>AU1 audio panel — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>AU1 audio panel — {t["name"]}</h1>'
            f'<span>Mockup for #267. Real icon set; new icons: Line mode, Waveform, three volume levels.</span></div>'
            f'<div class="grid">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"au1-{key}.html").write_text(page(key, t))
print("wrote", ", ".join(f"au1-{k}.html" for k in THEMES))
