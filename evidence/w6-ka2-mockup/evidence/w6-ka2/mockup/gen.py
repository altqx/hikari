#!/usr/bin/env python3
"""KA2 #277 tap-to-time mockup: one HTML sheet per theme, screenshotted by shoot.sh.

Built on AU1's approved panel (au1-mockup/gen.py, merged as #291) and DG1's window frame.

usage: gen.py <src/ui/icons> <out dir>
"""
import math
import pathlib
import random
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# theme.cpp baseRoles + the default accent picks; audio = Content::audio (as AU1).
THEMES = {
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", disabled="#75818D", hc=False, rec="#FF6B6B",
                 a_bg="#36393E", a_wave="#202225", a_inact="#2F3136", a_scale="#202225", a_scaletext="#AEAFB2",
                 a_start="#940000", a_inbound="#00D77D", a_cursor="#8791FD", a_key="#F4F4F4",
                 a_syl="#202225", a_syltext="#8791FD"),
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", disabled="#74808B", hc=False, rec="#C62828",
                  a_bg="#DDE3E8", a_wave="#5B6774", a_inact="#9AA5B0", a_scale="#EDF0F3", a_scaletext="#526170",
                  a_start="#940000", a_inbound="#00875A", a_cursor="#4F56D6", a_key="#2E3A46",
                  a_syl="#3A4652", a_syltext="#3A3FB0"),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E", hc=True, rec="#C00000",
                    a_bg="#FFFFFF", a_wave="#303030", a_inact="#8C8C8C", a_scale="#FFFFFF", a_scaletext="#000000",
                    a_start="#C00000", a_inbound="#007A3D", a_cursor="#8000C0", a_key="#000000",
                    a_syl="#000000", a_syltext="#000000"),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", disabled="#8C8C8C", hc=True, rec="#FF4040",
                    a_bg="#000000", a_wave="#C8C8C8", a_inact="#6E6E6E", a_scale="#000000", a_scaletext="#FFFFFF",
                    a_start="#FF4040", a_inbound="#00FF80", a_cursor="#00FFFF", a_key="#FFFFFF",
                    a_syl="#FFFFFF", a_syltext="#FFFFFF"),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Icons this card would add (IC1's 16 px, 1 px stroke style, accent group as the set does).
NEW_ICONS = {
    # Tap to time: a key pressed down.
    "karaoke-tap": SVG_HEAD + '<rect x="2.5" y="9.5" width="11" height="4" rx="1"/>'
                   '<g id="accent"><path d="M8 1.5v5.5M5.5 4.5L8 7l2.5-2.5"/></g></svg>',
    # Calibrate latency: a metronome.
    "tap-calibrate": SVG_HEAD + '<path d="M5.5 2.5h5l2.5 11h-10z"/><path d="M4 10.5h8"/>'
                     '<g id="accent"><path d="M8 10.5l3.5-6.5"/></g></svg>',
    # Speed: a gauge.
    "playback-speed": SVG_HEAD + '<path d="M2.5 12a5.5 5.5 0 1 1 11 0"/><g id="accent"><path d="M8 12L5.5 8"/></g>'
                      '<circle cx="8" cy="12" r="0.75" fill="currentColor"/></svg>',
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "radio-on": SVG_HEAD + '<circle cx="8" cy="8" r="5.5"/><circle cx="8" cy="8" r="2.5" fill="currentColor"/></svg>',
    "radio-off": SVG_HEAD + '<circle cx="8" cy="8" r="5.5"/></svg>',
    "submenu": SVG_HEAD + '<path d="M6 4l4 4-4 4"/></svg>',
    "close": SVG_HEAD + '<path d="M4 4l8 8M12 4l-8 8"/></svg>',
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


def strip(t, *, mode=1, tap=False, speed=100, open_menu=None, hover=None, fold=True, vol="volume-low"):
    """The AU1 button row. In Karaoke mode KA2 adds the Tap split button after Auto split.

    fold: at the default 640 px dock the three toggles (Auto-scroll, Sync with video, Speech) go into ⋯
    to make room for Tap; Line mode keeps AU1's row unchanged.
    """
    h = lambda k: " hov" if hover == k else ""
    marks = '<span class="marks"><i class="mk">A</i></span>'
    left = [
        btn("audio-previous-line"), btn("audio-play"), btn("play-line"),
        btn("media-stop"), btn("audio-next-line"), SEP,
        split("play-before-start"), SEP,
        btn("lead-in"), btn("lead-out"),
        split("commit", "", marks),
    ]
    right = [seg(["audio-line-mode", "karaoke"], mode)]
    if mode == 1:
        right.append(btn("karaoke-split", h("split")))
        right.append(split("karaoke-tap", ("tg" if tap else "") + h("tap"), menu_open=open_menu == "tap"))
    right.append(seg(["audio-waveform", "spectrum"], 0))
    right.append(SEP)
    if fold:
        right.append(btn("menu-more", "on" if open_menu == "overflow" else ""))
    else:
        right += [btn("auto-scroll", "tg"), btn("audio-video-sync", "tg"), btn("spectrum-nonlinear"), SEP]
    right.append(f'<span class="b zoom">{icon("zoom")}<span class="ro">1.5×</span></span>')
    sp = f'<span class="marks"><i class="mk sp">{speed}</i></span>' if speed != 100 else ""
    right.append(f'<span class="b spk{" on" if open_menu == "vol" else ""}{h("vol")}">{icon(vol)}{sp}</span>')
    return ('<div class="strip"><div class="grp">' + "".join(left) + '</div><div class="grow"></div>'
            '<div class="grp">' + "".join(right) + '</div></div>')


def kc(k):
    return f'<span class="kc">{k}</span>'


def tapbar(t, *, state="tap", line=12, syls=(), cur=None, done=0, nxt="kokoro no oto", msg=None, speed=50,
           actions=None):
    """The tap bar: an inline bar above the display while Tap is on.

    syls: the Line's syllables; done = how many are tapped; cur = index held now (or None).
    """
    if state == "tap":
        lead = f'<span class="st"><i class="dot"></i>Tapping</span>'
    elif state == "stopped":
        lead = f'<span class="st"><i class="sq"></i>Stopped</span>'
    else:
        lead = f'<span class="st"><i class="ring"></i>Ready</span>'
    chips = []
    for i, s in enumerate(syls):
        c = "done" if i < done else ("held" if i == cur else ("next" if i == done and cur is None else "todo"))
        if i == cur:
            c = "held"
        chips.append(f'<span class="sy {c}">{s}</span>')
    lyr = f'<span class="lyr"><span class="ln">Line {line}</span>{"".join(chips)}'
    if nxt:
        lyr += f'<span class="nx">› {nxt}</span>'
    lyr += '</span>'
    mid = f'<span class="msg">{msg}</span>' if msg else ""
    if actions is None:
        actions = (f'<span class="hint">{kc("⌫")} Undo</span>'
                   f'<span class="hint">{kc("Esc")} Stop</span>')
    return f'<div class="tapbar">{lead}{lyr}{mid}<span class="grow"></span>{actions}</div>'


def display(t, w, h, *, line=None, syls=(), cursor=0.5, ghost=None, rewind=None, prev_line=None, old_end=None,
            seed=5, ruler0=48, chip=None):
    """The audio display in Karaoke mode while tapping.

    line: (start, end) as fractions of the view; end may be None while the Line is being tapped.
    syls: list of (start, end, text, state) with state done | held | ghost.
    """
    rnd = random.Random(seed)
    disp_h = h - 18
    mid = 18 + disp_h / 2
    p = [f'<svg class="disp" width="{w}" height="{h}" viewBox="0 0 {w} {h}" xmlns="http://www.w3.org/2000/svg">']
    p.append(f'<defs><pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" '
             f'patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="6" stroke="{t["a_syl"] if not t["hc"] else t["text"]}" '
             f'stroke-width="2" opacity="{0.9 if t["hc"] else 0.55}"/></pattern></defs>')
    p.append(f'<rect x="0" y="0" width="{w}" height="{h}" fill="{t["a_bg"]}"/>')
    p.append(f'<rect x="0" y="0" width="{w}" height="18" fill="{t["a_scale"]}"/>')
    step = w / 12
    for i in range(13):
        x = i * step
        p.append(f'<line x1="{x:.1f}" y1="12" x2="{x:.1f}" y2="18" stroke="{t["a_scaletext"]}" stroke-width="1"/>')
        p.append(f'<text x="{x + 3:.1f}" y="11" font-size="10" fill="{t["a_scaletext"]}" '
                 f'font-family="Noto Sans, Segoe UI, sans-serif">{ruler0 + i}</text>')
    ls = line[0] * w if line else None
    le = (line[1] * w if line[1] is not None else cursor * w) if line else None
    if line:
        p.append(f'<rect x="{ls:.1f}" y="18" width="{le - ls:.1f}" height="{disp_h}" fill="{t["accent"]}" '
                 f'fill-opacity="0.18"/>')
    if prev_line:
        a, b = prev_line[0] * w, prev_line[1] * w
        p.append(f'<rect x="{a:.1f}" y="18" width="{b - a:.1f}" height="{disp_h}" fill="{t["a_inact"]}" '
                 f'fill-opacity="0.35"/>')
    for x in range(0, w, 2):
        env = 0.25 + 0.75 * abs(math.sin(x / 33.0 + seed)) * (0.6 + 0.4 * math.sin(x / 8.0))
        if (x // 56) % 5 == 4:
            env *= 0.15
        a = env * (disp_h / 2 - 14) * (0.6 + 0.4 * rnd.random())
        inside = line and ls <= x <= le
        col = t["accent"] if inside else t["a_wave"]
        p.append(f'<line x1="{x}" y1="{mid + 8 - a:.1f}" x2="{x}" y2="{mid + 8 + a:.1f}" stroke="{col}" '
                 f'stroke-width="1.6"/>')
    for ix in (0.06, 0.95):
        p.append(f'<line x1="{ix * w:.1f}" y1="18" x2="{ix * w:.1f}" y2="{h}" stroke="{t["a_inbound"]}" '
                 f'stroke-width="1" stroke-dasharray="1 2"/>')
    if old_end is not None:
        x = old_end * w
        p.append(f'<line x1="{x:.1f}" y1="44" x2="{x:.1f}" y2="{h}" stroke="{t["a_start"]}" stroke-width="1" '
                 f'stroke-dasharray="4 3" opacity="0.8"/>')
    # syllable lane (top 18..42): tapped blocks, gaps hatched between them
    lane_y, lane_h = 20, 22
    prev_end = None
    for (s0, s1, txt, st) in syls:
        x0 = s0 * w
        x1 = (s1 if s1 is not None else cursor) * w
        if prev_end is not None and x0 - prev_end > 2 and st != "ghost":
            p.append(f'<rect x="{prev_end:.1f}" y="{lane_y}" width="{x0 - prev_end:.1f}" height="{lane_h}" '
                     f'fill="url(#hatch)"/>')
        if st == "ghost":
            p.append(f'<rect x="{x0:.1f}" y="{lane_y + .5}" width="{x1 - x0:.1f}" height="{lane_h - 1}" fill="none" '
                     f'stroke="{t["a_syl"] if not t["hc"] else t["text"]}" stroke-dasharray="3 2"/>')
            p.append(f'<text x="{(x0 + x1) / 2:.1f}" y="{lane_y + 15}" text-anchor="middle" font-size="12" '
                     f'fill="{t["muted"]}" text-decoration="line-through" '
                     f'font-family="Noto Sans, Segoe UI, sans-serif">{txt}</text>')
            continue
        held = st == "held"
        fill_op = 0.9 if held else 0.5
        p.append(f'<rect x="{x0:.1f}" y="{lane_y}" width="{x1 - x0:.1f}" height="{lane_h}" fill="{t["accent"]}" '
                 f'fill-opacity="{fill_op}"/>')
        tc = t["onaccent"] if held or t["hc"] else t["text"]
        if t["hc"] and not held:
            p.append(f'<rect x="{x0 + .5:.1f}" y="{lane_y + .5}" width="{x1 - x0 - 1:.1f}" height="{lane_h - 1}" '
                     f'fill="{t["field"]}" stroke="{t["accent"]}"/>')
            tc = t["text"]
        p.append(f'<text x="{(x0 + x1) / 2:.1f}" y="{lane_y + 15}" text-anchor="middle" font-size="12" '
                 f'font-weight="{700 if held else 600}" fill="{tc}" '
                 f'font-family="Noto Sans, Segoe UI, sans-serif">{txt}</text>')
        p.append(f'<line x1="{x0:.1f}" y1="{lane_y}" x2="{x0:.1f}" y2="{h}" stroke="{t["a_syl"] if not t["hc"] else t["text"]}" stroke-width="1"/>')
        if s1 is not None:
            p.append(f'<line x1="{x1:.1f}" y1="{lane_y}" x2="{x1:.1f}" y2="{h}" stroke="{t["a_syl"] if not t["hc"] else t["text"]}" '
                     f'stroke-width="1" stroke-dasharray="2 2"/>')
        prev_end = x1
    if line:
        p.append(f'<line x1="{ls:.1f}" y1="18" x2="{ls:.1f}" y2="{h}" stroke="{t["a_start"]}" stroke-width="2"/>')
        if line[1] is not None:
            p.append(f'<line x1="{le:.1f}" y1="18" x2="{le:.1f}" y2="{h}" stroke="{t["a_start"]}" stroke-width="2"/>')
    if rewind:
        a, b = rewind[0] * w, rewind[1] * w
        y = 84
        p.append(f'<path d="M{a:.1f} {y + 10} C {a - 10:.1f} {y - 12}, {b + 10:.1f} {y - 12}, {b + 3:.1f} {y + 6}" '
                 f'fill="none" stroke="{t["a_cursor"]}" stroke-width="1.5"/>')
        p.append(f'<path d="M{b - 2:.1f} {y + 1} L{b + 3:.1f} {y + 7} L{b + 8:.1f} {y + 1}" fill="none" '
                 f'stroke="{t["a_cursor"]}" stroke-width="1.5"/>')
        p.append(f'<rect x="{(a + b) / 2 - 18:.1f}" y="{y - 18}" width="36" height="15" rx="2" fill="{t["panel"]}" '
                 f'stroke="{t["a_cursor"]}"/>')
        p.append(f'<text x="{(a + b) / 2:.1f}" y="{y - 7}" text-anchor="middle" font-size="10.5" font-weight="600" '
                 f'fill="{t["text"]}" font-family="Noto Sans, Segoe UI, sans-serif">−2 s</text>')
    cx = cursor * w
    p.append(f'<line x1="{cx:.1f}" y1="18" x2="{cx:.1f}" y2="{h}" stroke="{t["a_cursor"]}" stroke-width="1.5"/>')
    p.append('</svg>')
    out = "".join(p)
    if chip:
        out += chip
    return out


def scrollbar(pos=0.45, size=0.18):
    return f'<div class="scroll"><div class="thumb" style="left:{pos * 100:.0f}%;width:{size * 100:.0f}%"></div></div>'


def panel(t, w, body, *, extra="", title="Audio"):
    return (f'<div class="dock" style="width:{w}px">'
            f'<div class="dhead"><span class="t">{title}</span><span class="b">{icon("panel-menu")}</span></div>'
            f'<div class="dbody">{body}</div>{extra}</div>')


def audio_body(t, w, disp_h, strip_html, bar="", **disp):
    return (f'{bar}<div class="dispwrap" style="height:{disp_h}px">{display(t, w - 2, disp_h, **disp)}</div>'
            f'{scrollbar()}{strip_html}')


def menu(rows, *, left, top, width=268, cls=""):
    return f'<div class="menu {cls}" style="left:{left}px;top:{top}px;width:{width}px">' + "".join(rows) + '</div>'


def mrow(role, text, keys="", *, checked=None, radio=None, hover=False, dis=False, sub=False):
    c = (" hov" if hover else "") + (" dis" if dis else "")
    lead = ""
    if checked is not None:
        lead = f'<span class="cb">{svg("check") if checked else ""}</span>'
    if radio is not None:
        lead = f'<span class="cb">{svg("radio-on" if radio else "radio-off")}</span>'
    ic = icon(role) if role else '<span class="ic"></span>'
    tail = f'<span class="mk2">{keys}</span>'
    if sub:
        tail += f'<span class="cb">{svg("submenu")}</span>'
    return f'<div class="mi{c}">{lead}{ic}<span class="mt">{text}</span>{tail}</div>'


MSEP = '<div class="msep"></div>'


def mhead(text):
    return f'<div class="phead">{text}</div>'


def tip(text, *, left, top):
    return f'<div class="tip" style="left:{left}px;top:{top}px">{text}</div>'


def cell(n, caption, inner, note="", wide=False):
    note_html = f'<div class="note">{note}</div>' if note else ""
    return (f'<div class="cell{" wide" if wide else ""}"><div class="cap"><b>{n}</b> {caption}</div>{note_html}'
            f'<div class="stage">{inner}</div></div>')


W = 640  # the default Workspace's audio dock at 1280 px, as AU1
DISP = 150
SYL = ["ki", "mi", "no", "na", "mae"]


def editor(t, text, *, times="0:00:49.12 → 0:00:52.31", w=W):
    return (f'<div class="led" style="width:{w}px"><div class="lhead"><span>Line editor</span>'
            f'<span class="lt">{times}</span></div><div class="ltext">{text}</div></div>')


def calib(t, key):
    """DG1 frame: a compact Window opened from the Tap menu."""
    beats = []
    offs = [92, 71, 88, 79, 101, 84, 77, 90, 83, 86]
    for i in range(16):
        c = "done" if i < 10 else ("cur" if i == 10 else "")
        beats.append(f'<span class="beat {c}"></span>')
    # strip plot of tap offsets
    xs = []
    W2 = 300
    for i, o in enumerate(offs):
        x = (o + 0) / 200 * W2
        xs.append(f'<circle cx="{x:.1f}" cy="{12 + (i % 3) * 5}" r="3" fill="{t["accent"]}" '
                  f'fill-opacity="{1 if t["hc"] else .8}"/>')
    med = 85 / 200 * W2
    plot = (f'<svg width="{W2 + 20}" height="44" viewBox="-10 0 {W2 + 20} 44">'
            f'<line x1="0" y1="30" x2="{W2}" y2="30" stroke="{t["line"]}"/>'
            + "".join(f'<line x1="{v / 200 * W2:.1f}" y1="30" x2="{v / 200 * W2:.1f}" y2="34" stroke="{t["muted"]}"/>'
                      f'<text x="{v / 200 * W2:.1f}" y="43" font-size="9.5" text-anchor="middle" fill="{t["muted"]}" '
                      f'font-family="Noto Sans, Segoe UI, sans-serif">{v}</text>' for v in (0, 50, 100, 150, 200))
            + "".join(xs)
            + f'<line x1="{med:.1f}" y1="4" x2="{med:.1f}" y2="30" stroke="{t["text"]}" stroke-width="1.5"/>'
            + '</svg>')
    return (f'<div class="win" style="width:440px">'
            f'<div class="wtb"><span class="wt">Calibrate tap latency</span><span class="wx">{svg("close")}</span></div>'
            f'<div class="wbody">'
            f'<div class="wl">Press {kc("Space")} on each beep. 16 beeps at 100 BPM; the first four are practice.</div>'
            f'<div class="beats">{"".join(beats)}</div>'
            f'<div class="wl2">Your presses, in ms after each beep</div>{plot}'
            f'<div class="res"><span class="rl">Latency</span><span class="spin"><span class="v">85</span>'
            f'<span class="u">ms</span><span class="arr">▴<br>▾</span></span>'
            f'<span class="rs">Spread ±8 ms · 6 of 12 counted beeps so far</span></div>'
            f'</div>'
            f'<div class="wfoot"><span class="wb">Start again</span><span class="grow"></span>'
            f'<span class="wb">Cancel</span><span class="wb def">Apply</span></div></div>')


def sheet(key, t):
    cells = []
    bar_h = 30

    # 1 Karaoke mode, Tap off: the new split button and its tooltip.
    s = strip(t, hover="tap", speed=100)
    body = audio_body(t, W, DISP + bar_h, s, line=(0.36, 0.70), cursor=0.45,
                      syls=[(0.36, 0.43, "ki", "done"), (0.43, 0.50, "mi", "done"), (0.505, 0.57, "no", "done"),
                            (0.57, 0.62, "na", "done"), (0.62, 0.70, "mae", "done")])
    tp = tip("Tap to time (Y)<br><span class='dim'>Plays from 1 s before the Line; hold a key for each syllable. "
             "The arrow holds the tap options.</span>", left=232, top=DISP + bar_h + 84)
    cells.append(cell(1, "Karaoke mode: Tap joins Auto split beside the Line | Karaoke switch.",
                      panel(t, W, body, extra=tp),
                      "Tap is a split button like Play around and Commit: the face turns tap mode on and off, the "
                      "arrow holds its options. To make room at the default 640 px dock, Karaoke mode folds "
                      "Auto-scroll, Sync with video and Speech into ⋯ (AU1's narrow fold, one step earlier). "
                      "Line mode keeps AU1's row as it is."))

    # 2 Tap menu
    s = strip(t, open_menu="tap", speed=50)
    body = audio_body(t, W, DISP + bar_h, s, line=(0.36, 0.70), cursor=0.45,
                      syls=[(0.36, 0.43, "ki", "done"), (0.43, 0.50, "mi", "done"), (0.505, 0.57, "no", "done"),
                            (0.57, 0.62, "na", "done"), (0.62, 0.70, "mae", "done")])
    rows = [mrow("karaoke-tap", "Start tapping", "Y"), MSEP,
            mhead("Each syllable"),
            mrow(None, "Hold the key: releasing it leaves a gap", "", radio=True),
            mrow(None, "Press the key: no gaps", "", radio=False, hover=True), MSEP,
            mrow("playback-speed", "Speed", "50%", sub=True),
            mrow("tap-calibrate", "Calibrate latency…", "85 ms")]
    m = menu(rows, left=290, top=DISP + bar_h + 84, width=330)
    cells.append(cell(2, "Tap options: how a key makes a syllable, speed and latency.",
                      panel(t, W, body, extra=m),
                      "<b>Hold</b> (the default): key-down starts the next syllable and key-up ends it, so the time "
                      "between a release and the next press becomes a gap. <b>Press</b>: each press starts the next "
                      "syllable and ends the one before (no gaps). The menu shows the current speed and latency."))

    # 3 Speaker popover with Speed
    s = strip(t, open_menu="vol", speed=50)
    body = audio_body(t, W, DISP + bar_h, s, line=(0.36, 0.70), cursor=0.45,
                      syls=[(0.36, 0.43, "ki", "done"), (0.43, 0.50, "mi", "done"), (0.505, 0.57, "no", "done"),
                            (0.57, 0.62, "na", "done"), (0.62, 0.70, "mae", "done")])
    speeds = "".join(f'<span class="ps{" on" if v == 50 else ""}">{v}%</span>' for v in (25, 33, 50, 75, 100))
    pop = (f'<div class="pop" style="left:{W - 330}px;top:{DISP + bar_h + 84}px;width:328px">'
           f'<div class="prow"><span class="pl">Volume</span><span class="slider"><span class="fill" style="width:38%"></span>'
           f'<span class="knob" style="left:38%"></span></span><span class="val">38</span></div>'
           f'<div class="prow"><span class="pl">Speed</span><span class="pseg">{speeds}</span></div>'
           f'<div class="phint">Pitch is kept. {kc("[")} and {kc("]")} change the speed.</div>'
           f'<div class="msep"></div>'
           f'<div class="phead">Output device</div>'
           + mrow(None, "System default (Built-in Audio)", "", checked=False)
           + mrow(None, "USB Headset", "", checked=True)
           + MSEP + mrow("settings-audio", "Audio settings…", "")
           + '</div>')
    cells.append(cell(3, "Speed lives in the speaker popover, under Volume; the speaker shows it when it is not 100%.",
                      panel(t, W, body, extra=pop),
                      "25, 33, 50, 75 and 100%, in both Line and Karaoke mode, with the pitch kept (SoundTouch). "
                      "The speaker's mark (<b>50</b>) is a shape, not just colour; its tooltip reads "
                      "\"Volume 38 · Speed 50%\". The speed stays until changed and is saved with the profile."))

    # 4 Tapping in progress: holding "no"
    bar = tapbar(t, syls=SYL, done=2, cur=2, speed=50)
    s = strip(t, tap=True, speed=50)
    body = audio_body(t, W, DISP, s, bar=bar, line=(0.30, None), cursor=0.56, old_end=0.80,
                      syls=[(0.30, 0.39, "ki", "done"), (0.39, 0.47, "mi", "done"), (0.495, None, "no", "held")])
    cells.append(cell(4, "Tapping: the bar shows the Line's syllables; the held one grows at the playhead.",
                      panel(t, W, body),
                      "The first press set the Line's start (red); tapped syllables fill the lane at the top, a "
                      "release before the next press shows as a hatched gap. The old end stays dashed until the last "
                      "release sets the new one. The display follows the playhead. Other audio hotkeys pause while "
                      "tapping, except Stop."))

    # 5 Undo-rewind
    bar = tapbar(t, syls=SYL, done=1, msg=f"Undid “mi”", speed=50)
    s = strip(t, tap=True, speed=50)
    body = audio_body(t, W, DISP, s, bar=bar, line=(0.30, None), cursor=0.27, old_end=0.80, rewind=(0.56, 0.27),
                      syls=[(0.30, 0.39, "ki", "done"), (0.39, 0.47, "mi", "ghost")])
    cells.append(cell(5, "Undo: Backspace takes back the last syllable and plays again from 2 s before it.",
                      panel(t, W, body),
                      "Here “no” was still held, so it is dropped with “mi”, and playback jumped back 2 s of song "
                      "time and goes on at the same speed; tap “mi” again when it comes. Shift+Backspace takes back the "
                      "whole Line and starts again 2 s before it. Taps on finished Lines go back with Ctrl+Z."))

    # 6 Line finished: committed, next Line starts
    bar = tapbar(t, line=13, syls=["ko", "ko", "ro", "no", "o", "to"], done=0, nxt="hibiku", speed=50)
    s = strip(t, tap=True, speed=50)
    body = audio_body(t, W, DISP, s, bar=bar, prev_line=(0.05, 0.47), line=None, cursor=0.62,
                      syls=[(0.05, 0.13, "ki", "done"), (0.13, 0.21, "mi", "done"), (0.225, 0.30, "no", "done"),
                            (0.30, 0.37, "na", "done"), (0.37, 0.47, "mae", "done")])
    led = editor(t, '<span class="tg">{\\k31}</span>ki<span class="tg">{\\k27}</span>mi'
                    '<span class="tg gap">{\\k5}</span><span class="tg">{\\k26}</span>no'
                    '<span class="tg">{\\k24}</span>na<span class="tg">{\\k34}</span>mae',
                 times="0:00:49.12 → 0:00:52.59")
    cells.append(cell(6, "Line end: the last release commits the Line and the next one is ready at once.",
                      panel(t, W, body) + led,
                      "Each finished Line is one undo step (\"Tap timing\"); playback keeps going, so a whole song "
                      "can be tapped in one pass. A gap becomes an empty syllable (<b>{\\k5}</b>); a gap under 30 ms joins "
                      "the syllable before it. Taps are rounded to 10 ms. The Line's \\k, \\kf or \\ko tags keep "
                      "their kind."))

    # 7 Stopped mid-Line
    actions = (f'<span class="ab def">Resume<span class="k2">Space</span></span>'
               f'<span class="ab">Revert Line</span>')
    bar = tapbar(t, state="stopped", line=13, syls=["ko", "ko", "ro", "no", "o", "to"], done=2, nxt=None,
                 msg="2 of 6 tapped", speed=50, actions=actions)
    s = strip(t, tap=True, speed=50)
    body = audio_body(t, W, DISP, s, bar=bar, line=(0.20, None), cursor=0.41, old_end=0.82,
                      syls=[(0.20, 0.29, "ko", "done"), (0.30, 0.41, "ko", "done")])
    cells.append(cell(7, "Stop: Esc stops playback and keeps the Line's taps pending.",
                      panel(t, W, body),
                      "Resume (Space) plays again from 2 s before the next untapped syllable. Revert Line puts the "
                      "Line back as it was. Turning Tap off, or Esc a second time, also reverts the unfinished Line; "
                      "finished Lines stay committed."))

    # 8 Calibrate latency window
    cells.append(cell(8, "Calibrate latency: a compact Window (DG1 frame), opened from the Tap menu.",
                      f'<div class="winstage">{calib(t, key)}</div>',
                      "One-time, saved with the profile. The value is the median of the counted presses after "
                      "outliers; the spin box lets you type it too. The playhead already allows for the device's "
                      "own latency, so this only measures you, and it holds when the device changes."))

    # 9 Not split yet
    bar = tapbar(t, state="ready", line=14, syls=["hibiku"], done=0, nxt=None, speed=50,
                 msg="This Line is one syllable. Split it first, then tap.",
                 actions=f'<span class="ab def">{icon("karaoke-split")}Auto split</span>')
    s = strip(t, tap=True, speed=50)
    body = audio_body(t, W, DISP, s, bar=bar, line=(0.30, 0.62), cursor=0.20, syls=[(0.30, 0.62, "hibiku", "done")])
    cells.append(cell(9, "A Line with one syllable: tapping waits for it to be split.",
                      panel(t, W, body),
                      "Auto split runs KA1's splitter; the Karaoke bar's own split also works. With Hold or Press, a "
                      "Line with one syllable would only get its start and end, so Tap asks first."))
    return cells


def css(t):
    hc = t["hc"]
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
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;max-width:650px;min-height:48px}}
.stage{{position:relative;height:490px}}
.dock{{position:relative;background:{t['field']};border:1px solid {t['line']}}}
.dhead{{height:35px;display:flex;align-items:center;padding:0 8px 0 12px;border-bottom:1px solid {t['line']}}}
.dhead .t{{font-weight:700;flex:1}}
.dbody{{position:relative;display:flex;flex-direction:column;padding-bottom:4px}}
.dispwrap{{position:relative;overflow:hidden}}
.disp{{display:block}}
.scroll{{height:6px;margin:4px 0 0;background:{t['field']};border:1px solid {t['line']};border-radius:3px;position:relative}}
.scroll .thumb{{position:absolute;top:-1px;bottom:-1px;background:{t['control']};border-radius:3px}}
.strip{{display:flex;align-items:center;height:30px;padding:0 4px;margin-top:4px;white-space:nowrap}}
.grp{{display:flex;align-items:center;gap:2px}}
.grow{{flex:1}}
.sep{{width:1px;height:18px;background:{t['line']};margin:0 3px}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none}}
.ic svg{{width:16px;height:16px}}
svg #accent{{color:{t['accent']}}}
.b{{position:relative;height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;gap:4px;border:1px solid transparent;border-radius:3px;color:{t['text']}}}
.b.hov,.split.hov .face,.split.hov .arrow{{background:{t['raised']};border-color:{t['control']}}}
.b.on,.split.open .face,.split.open .arrow{{background:{t['select']};border-color:{t['accent'] if hc else t['control']}}}
.b.tg,.split.tg .face{{background:{t['select']};border-color:{t['accent'] if hc else 'transparent'};box-shadow:inset 0 -2px 0 {t['accent']}}}
.b.zoom{{padding:0 6px 0 4px}}
.b .ro{{font-size:11.5px;font-variant-numeric:tabular-nums;color:{t['text']}}}
.split{{display:inline-flex;align-items:stretch;height:26px}}
.split .face{{position:relative;width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-right:none;border-radius:3px 0 0 3px}}
.split .arrow{{width:13px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:0 3px 3px 0;color:{t['muted']};position:relative}}
.split .arrow::before{{content:"";position:absolute;left:-1px;top:6px;bottom:6px;width:1px;background:{t['line']}}}
.marks{{position:absolute;right:-1px;bottom:0px;display:flex;gap:1px}}
.mk{{font-style:normal;font-size:8px;line-height:9px;font-weight:700;min-width:9px;height:9px;text-align:center;border-radius:2px;background:{t['accent']};color:{t['onaccent']};padding:0 1px}}
.seg{{display:inline-flex;height:26px;border:1px solid {t['control']};border-radius:3px;overflow:hidden;margin:0 2px}}
.sg{{width:26px;display:inline-flex;align-items:center;justify-content:center;color:{t['text']}}}
.sg+.sg{{border-left:1px solid {t['line']}}}
.sg.on{{background:{t['select']};box-shadow:inset 0 -2px 0 {t['accent']}}}
.menu,.pop{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{0 if hc else .28})}}
.mi{{display:flex;align-items:center;gap:8px;height:28px;padding:0 10px 0 10px}}
.mi .mt{{flex:1;white-space:nowrap}}
.mi .mk2{{color:{t['muted']};font-size:12px;white-space:nowrap}}
.mi.hov{{background:{t['raised']}}}
.mi.dis{{color:{t['disabled']}}}
.cb{{width:14px;height:14px;display:inline-flex;align-items:center;justify-content:center;flex:none}}
.cb svg{{width:14px;height:14px}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
.phead{{font-size:11px;color:{t['muted']};font-weight:600;padding:4px 12px 2px}}
.prow{{display:flex;align-items:center;gap:10px;height:32px;padding:0 12px}}
.prow .pl{{width:58px;color:{t['text']}}}
.prow .val{{width:36px;text-align:right;font-variant-numeric:tabular-nums;font-weight:600}}
.slider{{flex:1;height:4px;background:{t['line']};border-radius:2px;position:relative}}
.slider .fill{{position:absolute;left:0;top:0;bottom:0;background:{t['accent']};border-radius:2px}}
.slider .knob{{position:absolute;top:-6px;width:16px;height:16px;margin-left:-8px;border-radius:8px;background:{t['panel']};border:1px solid {t['control']}}}
.pseg{{display:inline-flex;flex:1;height:26px;border:1px solid {t['control']};border-radius:3px;overflow:hidden}}
.ps{{flex:1;display:inline-flex;align-items:center;justify-content:center;font-size:12px;font-variant-numeric:tabular-nums}}
.ps+.ps{{border-left:1px solid {t['line']}}}
.ps.on{{background:{t['select']};box-shadow:inset 0 -2px 0 {t['accent']};font-weight:700}}
.phint{{font-size:11px;color:{t['muted']};padding:0 12px 6px 80px}}
.tip{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};padding:4px 8px;font-size:12px;border-radius:2px;max-width:310px}}
.tip .dim{{color:{t['muted']};font-size:11px}}
.kc{{display:inline-block;font-size:10.5px;line-height:14px;padding:0 4px;border:1px solid {t['control']};border-bottom-width:2px;border-radius:3px;color:{t['text']};background:{t['raised']};font-weight:600}}
.tapbar{{display:flex;align-items:center;gap:10px;height:30px;padding:0 8px;background:{t['panel']};border-bottom:1px solid {t['line']};white-space:nowrap;font-size:12px}}
.st{{display:inline-flex;align-items:center;gap:6px;font-weight:700}}
.dot{{width:9px;height:9px;border-radius:5px;background:{t['rec']};display:inline-block}}
.sq{{width:9px;height:9px;background:{t['text']};display:inline-block}}
.ring{{width:9px;height:9px;border-radius:5px;border:1.5px solid {t['text']};display:inline-block}}
.lyr{{display:inline-flex;align-items:center;gap:3px}}
.ln{{color:{t['muted']};margin-right:4px}}
.sy{{padding:1px 5px;border-radius:3px;font-weight:600;border:1px solid transparent}}
.sy.done{{color:{t['muted']};text-decoration:{'underline' if hc else 'none'}}}
.sy.held{{background:{t['accent']};color:{t['onaccent']}}}
.sy.next{{border-color:{t['accent']};}}
.sy.todo{{color:{t['text']}}}
.nx{{color:{t['muted']};margin-left:6px}}
.msg{{color:{t['text']};font-weight:600}}
.hint{{color:{t['muted']};font-size:11px;display:inline-flex;align-items:center;gap:4px}}
.spd{{display:inline-flex;align-items:center;gap:3px;font-size:11.5px;font-weight:600;font-variant-numeric:tabular-nums;border-left:1px solid {t['line']};padding-left:8px}}
.ab{{display:inline-flex;align-items:center;gap:6px;height:22px;padding:0 8px;border:1px solid {t['control']};border-radius:3px;background:{t['raised']};font-size:12px}}
.ab.def{{background:{t['accent']};color:{t['onaccent']};border-color:{t['accent']};font-weight:600}}
.ab .k2{{font-size:10px;opacity:.85;font-weight:400}}
.ab svg #accent{{color:inherit}}
.led{{margin-top:8px;background:{t['field']};border:1px solid {t['line']}}}
.lhead{{display:flex;justify-content:space-between;padding:4px 10px;font-size:11.5px;color:{t['muted']};border-bottom:1px solid {t['line']}}}
.lt{{font-variant-numeric:tabular-nums}}
.ltext{{padding:8px 10px;font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:13px}}
.ltext .tg{{color:{t['muted']}}}
.ltext .tg.gap{{color:{t['text']};background:{t['select']};outline:1px solid {t['accent']};padding:0 1px}}
.winstage{{padding:6px 0 0 30px}}
.win{{background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};box-shadow:0 6px 22px rgba(0,0,0,{0 if hc else .35})}}
.wtb{{height:32px;display:flex;align-items:center;padding-left:12px}}
.wt{{flex:1}}
.wx{{width:44px;height:32px;display:inline-flex;align-items:center;justify-content:center}}
.wx svg{{width:12px;height:12px}}
.wbody{{padding:8px 22px 6px}}
.wl{{margin-bottom:10px}}
.wl2{{font-size:11.5px;color:{t['muted']};margin:10px 0 2px}}
.beats{{display:flex;gap:6px}}
.beat{{width:16px;height:16px;border-radius:8px;border:1px solid {t['control']}}}
.beat:nth-child(-n+4){{border-style:dashed}}
.beat.done{{background:{t['accent']};border-color:{t['accent']}}}
.beat.cur{{border:3px solid {t['accent']}}}
.res{{display:flex;align-items:center;gap:10px;margin:10px 0 4px}}
.rl{{font-weight:600}}
.spin{{display:inline-flex;align-items:center;height:26px;border:1px solid {t['control']};background:{t['field']};border-radius:2px;padding-left:8px}}
.spin .v{{width:34px;text-align:right;font-variant-numeric:tabular-nums;font-weight:600}}
.spin .u{{color:{t['muted']};margin:0 6px 0 4px}}
.spin .arr{{font-size:8px;line-height:9px;border-left:1px solid {t['line']};padding:0 4px;color:{t['muted']}}}
.rs{{font-size:11.5px;color:{t['muted']}}}
.wfoot{{display:flex;gap:8px;padding:12px 22px 16px}}
.wb{{display:inline-flex;align-items:center;height:30px;padding:0 14px;border:1px solid {t['control']};background:{t['raised']}}}
.wb.def{{background:{t['accent']};color:{t['onaccent']};border:2px solid {t['text'] if hc else t['onaccent']};outline:1px solid {t['accent']};font-weight:700}}
"""


def page(key, t):
    cells = sheet(key, t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>KA2 tap-to-time — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>KA2 tap-to-time — {t["name"]}</h1>'
            f'<span>Mockup for #277 on AU1\'s panel (#291). New icons: Tap, Calibrate latency, Speed.</span></div>'
            f'<div class="grid">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"ka2-{key}.html").write_text(page(key, t))
print("wrote", ", ".join(f"ka2-{k}.html" for k in THEMES))
