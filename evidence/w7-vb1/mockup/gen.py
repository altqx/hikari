#!/usr/bin/env python3
"""VB1 #481 adaptive visual toolbar and inspector bar mockup: three HTML sheets per theme.

gen.py ICONS OUT. The panel is drawn from today's Video panel (main b30fa7d77, captured in today/), the theme
roles of src/ui/theme.cpp, the K1 icons and PK1's popover surface. The picture is the app's dummy video
(?dummy:23.976:4000:1920:1080:47:163:161:c) with fixture.ass's lines.
"""
import math
import pathlib
import re
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

THEMES = {
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", warning="#8A5A00", danger="#A51F31", disabled="#74808B", checked="#D3D9DE",
                  note="#FFF6DD"),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", warning="#E0A030", danger="#FFADAD", disabled="#75818D", checked="#3A4450",
                 note="#2E2A1E"),
}

# The rail's order (VisualToolsController::railOrder): icon role, name.
RAIL = [("tool-crosshair", "Position pointer"), ("tool-position", "Text positioning"),
        ("tool-move", "Text moving"), ("tool-scale", "Text scaling"), ("tool-rotate-z", "Text Z rotation"),
        ("tool-rotate-xy", "Text X / Y rotation"), ("tool-perspective", "Perspective"),
        ("tool-clip-rect", "Rectangle clipping"), ("tool-clip-vector", "Vector clipping"),
        ("tool-drawing", "Vector drawing"), ("tool-move-all", "Position shifter"), ("tool-all-tags", "Hydra")]
CROSS, PERSP, CLIPV, HYDRA = 0, 6, 8, 11

# Geometry of today's panel (measured on the capture).
HEAD = 30          # dock header
BTN, GAP = 28, 1   # rail button and spacing
RAILPAD = 2
ROW = 26           # one line of the bar
BARPAD = 6         # its top and bottom padding together
SEEK, TIMES, TRANS = 20, 20, 32


def icon(role, size=16, cls=""):
    svg = (ICONS / f"{role}.svg").read_text().strip()
    svg = svg.replace('<g id="accent"', '<g class="acc"')
    svg = re.sub(r'width="16" height="16"', f'width="{size}" height="{size}"', svg, count=1)
    return f'<span class="ic {cls}">{svg}</span>'


def chev():
    return ('<svg class="chev" viewBox="0 0 16 16" width="12" height="12"><path d="M4 6l4 4 4-4" fill="none" '
            'stroke="currentColor" stroke-width="1.25" stroke-linecap="round" stroke-linejoin="round"/></svg>')


def btn(role, state="", tip=None):
    t = f'<span class="tip">{tip}</span>' if tip else ""
    return f'<span class="tb {state}">{icon(role)}{t}</span>'


def num(label, value, unit="", width=64):
    return (f'<span class="num"><span class="nl">{label}</span>'
            f'<span class="nf" style="width:{width}px"><span class="nv">{value}</span>'
            f'<span class="nu">{unit}</span></span></span>')


def ro(label, value):
    return f'<span class="ro"><span class="nl">{label}</span><span class="rv">{value}</span></span>'


def combo(value, width=None):
    w = f' style="min-width:{width}px"' if width else ""
    return f'<span class="combo"{w}><span class="cv">{value}</span>{chev()}</span>'


def grp(label, *items, first=False):
    lab = f'<span class="gl">{label}</span>' if label else ""
    rule = "" if first else '<span class="vsep"></span>'
    return f'<span class="grp">{rule}{lab}{"".join(items)}</span>'


def toolname(i):
    return f'<span class="tool">{icon(RAIL[i][0])}<span class="tn">{RAIL[i][1]}</span></span>'


PICK = '<span class="target">' + btn("pick-lines") + '</span>'
MORE = '<span class="target">' + btn("menu-more", "more") + '</span>'
MORE_OPEN = '<span class="target">' + btn("menu-more", "more on") + '</span>'
MODES = ["vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete"]


# Each tool's groups, in its order (today's bar at 640 pixels, without the leading name).
def groups(tool):
    if tool == CROSS:
        return []
    if tool == CLIPV:
        return [grp("Points", '<span class="seg">' + "".join(btn(m, "on" if i == 1 else "")
                                                              for i, m in enumerate(MODES)) + '</span>', first=True),
                grp(None, btn("clip-invert"))]
    if tool == PERSP:
        return [grp("Plane", btn("perspective-plane"), btn("perspective-lock-outer"), btn("perspective-grid"),
                    first=True),
                grp("Origin", combo("Center", 104), ro("X", "960"), ro("Y", "540")),
                grp("Rotation", num("X", "0", "°"), num("Y", "0", "°"), num("Z", "0", "°")),
                grp(None, num("Shear", "0", width=56))]
    if tool == HYDRA:
        return [grp("Tag", '<span class="combo search" style="min-width:150px">' + icon("search") +
                    '<span class="cv">blur</span><span class="gl">\\blur</span>' + chev() + '</span>', first=True),
                grp(None, num("Value", "0", width=56)),
                grp("Change", '<span class="segtext"><span class="on">Set</span><span>Add</span><span>Multiply</span>'
                    '<span>Gradient</span></span>', combo("At the cursor", 120))]
    raise KeyError(tool)


def bar_lines(tool, mode):
    """The bar's lines (each a list of groups) for a mode, at the 640-pixel panel."""
    g = groups(tool)
    if mode == "today":
        lead = toolname(tool)
        if tool == CROSS:
            return [[lead, grp(None, btn("copy-position"))]]
        if tool == PERSP:  # today: name, Plane, Origin on line 1; Rotation, Shear on line 2
            return [[lead, g[0].replace('<span class="grp">', '<span class="grp">'), g[1]],
                    [g[2].replace('<span class="vsep"></span>', ''), g[3]]]
        return [[lead] + g]
    if not g:
        return []
    if tool == PERSP and mode == "wrap":
        return [[g[0], g[1]], [g[2].replace('<span class="vsep"></span>', ''), g[3]]]
    if tool in (PERSP, HYDRA) and mode in ("more", "more-open", "overlay"):
        return [[g[0], g[1]]]
    return [g]


def bar_html(tool, mode, rows_reserved=None, cls=""):
    lines = bar_lines(tool, mode)
    if not lines:
        return ""
    trailing = PICK
    if tool == PERSP and mode in ("more", "overlay"):
        trailing = MORE + PICK
    if tool == HYDRA:
        trailing = (MORE if mode != "wrap" else "") + '<span class="tc">Changes the active line</span>'

    if tool == PERSP and mode == "more-open":
        trailing = MORE_OPEN + PICK
    rows = max(len(lines), rows_reserved or 0)
    body = "".join(f'<div class="bline">{"".join(l)}</div>' for l in lines)
    if rows_reserved and rows_reserved > len(lines):
        body += '<div class="band" style="height:{}px"><span>empty band</span></div>'.format(
            (rows_reserved - len(lines)) * (ROW + 6))
    h = rows * ROW + (rows - 1) * 6 + BARPAD
    return (f'<div class="bar {cls}" style="height:{h}px"><div class="blines">{body}</div>'
            f'<div class="btrail">{trailing}</div></div>')


def bar_height(tool, mode, reserved=None):
    lines = bar_lines(tool, mode)
    rows = max(len(lines), reserved or 0)
    if rows == 0 or mode == "overlay":
        return 0
    return rows * ROW + (rows - 1) * 6 + BARPAD


def video(w, h, tool):
    """The dummy video at w x h with fixture.ass's lines and the tool's overlay."""
    fs = h * 0.062
    ov = ""
    if tool == CROSS:
        ov = ('<svg class="ov" viewBox="0 0 1920 1080" preserveAspectRatio="none">'
              '<line x1="0" y1="430" x2="1920" y2="430" stroke="#fff" stroke-width="3"/>'
              '<line x1="1210" y1="0" x2="1210" y2="1080" stroke="#fff" stroke-width="3"/></svg>'
              f'<div class="xy" style="left:{1230/1920*100:.1f}%;top:{380/1080*100:.1f}%;font-size:{fs*0.8:.0f}px">1210,430</div>')
    elif tool == CLIPV:
        pts = [(420, 210), (1480, 160), (1560, 760), (980, 880), (360, 720)]
        poly = " ".join(f"{x},{y}" for x, y in pts)
        hs = "".join(f'<rect x="{x-14}" y="{y-14}" width="28" height="28" fill="#fff" stroke="#c00" stroke-width="5"/>'
                     for x, y in pts)
        ov = ('<svg class="ov" viewBox="0 0 1920 1080" preserveAspectRatio="none">'
              f'<path d="M0 0H1920V1080H0Z M{poly.replace(" ", " L")}Z" fill="rgba(0,0,0,.45)" fill-rule="evenodd"/>'
              f'<polygon points="{poly}" fill="none" stroke="#c00" stroke-width="5"/>{hs}</svg>')
    elif tool == PERSP:
        q = [(700, 470), (1230, 455), (1250, 625), (690, 610)]
        poly = " ".join(f"{x},{y}" for x, y in q)
        hs = "".join(f'<rect x="{x-12}" y="{y-12}" width="24" height="24" fill="#b0123a" stroke="#fff" stroke-width="3"/>'
                     for x, y in q)
        ov = ('<svg class="ov" viewBox="0 0 1920 1080" preserveAspectRatio="none">'
              f'<polygon points="{poly}" fill="none" stroke="#b0123a" stroke-width="6"/>{hs}'
              '<circle cx="965" cy="540" r="9" fill="#ff8a00"/></svg>')
    return (f'<div class="video" style="width:{w:.0f}px;height:{h:.0f}px">'
            f'<div class="sign" style="font-size:{fs:.0f}px">Station sign</div>'
            f'<div class="sub" style="font-size:{fs:.0f}px">The next train leaves at nine</div>{ov}</div>')


def rail_html(active, top_h, mode):
    """The rail in a top area top_h high: today's scroll, a More button, columns, or a strip (horizontal)."""
    fit = max(1, (top_h - 2 * RAILPAD + GAP) // (BTN + GAP))
    if mode == "scroll":
        shown = list(range(len(RAIL)))
        over = len(RAIL) > fit
        shift = max(0, (active + 1) * (BTN + GAP) - (top_h - 2 * RAILPAD)) if over else 0
        btns = f'<div style="margin-top:{-shift}px"></div>' + "".join(
            f'<span class="tb rb {"on" if i == active else ""}">{icon(r)}</span>' for i, (r, _) in enumerate(RAIL))
        bar = '<span class="rscroll"><i style="height:{}%"></i></span>'.format(int(100 * fit / len(RAIL))) if over else ""
        w = 32 + (7 if over else 0)
        return f'<div class="rail" style="width:{w}px"><div class="rcol">{btns}</div>{bar}</div>', w
    if mode == "more":
        if len(RAIL) <= fit:
            return rail_html(active, top_h, "columns")
        n = fit - 1
        shown = list(range(n))
        if active >= n:
            shown[-1] = active  # the on tool takes the last slot
        btns = "".join(f'<span class="tb rb {"on" if i == active else ""}">{icon(RAIL[i][0])}</span>' for i in shown)
        btns += f'<span class="tb rb more">{icon("menu-more")}</span>'
        return f'<div class="rail" style="width:32px"><div class="rcol">{btns}</div></div>', 32
    if mode == "columns":
        cols = math.ceil(len(RAIL) / fit)
        per = math.ceil(len(RAIL) / cols)
        colhtml = ""
        for c in range(cols):
            colhtml += '<div class="rcol">' + "".join(
                f'<span class="tb rb {"on" if i == active else ""}">{icon(RAIL[i][0])}</span>'
                for i in range(c * per, min(len(RAIL), (c + 1) * per))) + '</div>'
        w = 4 + cols * BTN + (cols - 1) * GAP
        return f'<div class="rail cols" style="width:{w}px">{colhtml}</div>', w
    raise KeyError(mode)


def times(tool, mode):
    t = '<span class="tt">00:00:01,001; 24; 24; 1001 ms, -3999 ms</span>'
    if tool == CROSS and mode != "today":
        t += '<span class="grow"></span><span class="xyro">' + ro("X", "1210") + ro("Y", "430") + '</span>' + \
             btn("copy-position")
    return f'<div class="times">{t}</div>'


def transport():
    return ('<div class="seek"><i style="width:2%"></i><b style="left:2%"></b></div>'
            '<div class="trans">' + "".join(btn(r) for r in ["media-previous-file", "media-play", "play-line",
                                                            "media-stop", "media-next-file", "frame-previous"]) +
            '<span class="grow status">Frame 24 of 4000 &nbsp;0:00:01.00</span>' + btn("frame-next") +
            icon("volume") + '<span class="vol"><i></i></span></div>')


def dock(w, h, tool, mode, rail="columns", reserved=None, extra="", strip=False, active=None):
    """A Video panel w x h with `tool` on, the bar in `mode` and the rail in `rail`."""
    active = tool if active is None else active
    bh = bar_height(tool, mode, reserved)
    top_h = h - HEAD - bh - SEEK - TIMES - TRANS
    strip_h = 34 if strip else 0
    if strip:
        railhtml, rw = "", 0
    else:
        railhtml, rw = rail_html(active, top_h, rail)
    sw, sh = w - rw - 2, top_h - strip_h
    vw, vh = (sw, sw * 9 / 16) if sw * 9 / 16 <= sh else (sh * 16 / 9, sh)
    stage = f'<div class="stage">{video(vw, vh, tool)}'
    if mode == "overlay" and bar_lines(tool, mode):
        stage += bar_html(tool, mode, cls="float")
    stage += '</div>'
    striphtml = ""
    if strip:
        striphtml = ('<div class="hstrip">' + "".join(
            f'<span class="tb {"on" if i == active else ""}">{icon(r)}</span>' for i, (r, _) in enumerate(RAIL)) +
            '</div>')
    barh = "" if mode == "overlay" else bar_html(tool, mode, reserved)
    return (f'<div class="dock" style="width:{w}px;height:{h}px">'
            f'<div class="dhead"><span class="t">Video</span>{icon("panel-menu")}</div>'
            f'<div class="dbody">{striphtml}<div class="top">{railhtml}{stage}</div>'
            f'{barh}<div class="ctl">{transport_seek()}{times(tool, mode)}{transport_row()}</div></div>{extra}</div>'), vh


def transport_seek():
    return '<div class="seek"><i style="width:2%"></i><b style="left:2%"></b></div>'


def transport_row():
    return ('<div class="trans">' + "".join(btn(r) for r in ["media-previous-file", "media-play", "play-line",
                                                            "media-stop", "media-next-file", "frame-previous"]) +
            '<span class="grow status">Frame 24 of 4000 &nbsp;0:00:01.00</span>' + btn("frame-next") +
            icon("volume") + '<span class="vol"><i></i></span></div>')


def more_popover(left, bottom):
    rows = (f'<div class="prow">{groups(PERSP)[2].replace(chr(60) + "span class=" + chr(34) + "vsep" + chr(34) + "></span>", "")}</div>'
            f'<div class="prow">{groups(PERSP)[3].replace(chr(60) + "span class=" + chr(34) + "vsep" + chr(34) + "></span>", "")}</div>')
    return f'<div class="pop" style="right:{left}px;bottom:{bottom}px">{rows}</div>'


def rail_more_popover(top, active, fit):
    shown = list(range(fit - 1))
    if active >= fit - 1:
        shown[-1] = active
    btns = "".join(f'<div class="mi"><span class="mk">{icon(RAIL[i][0])}</span><span>{RAIL[i][1]}</span></div>'
                   for i in range(len(RAIL)) if i not in shown)
    return f'<div class="pop menu" style="left:36px;top:{top}px">{btns}</div>'


def css(t):
    return f"""
:root{{--bg:{t['bg']};--panel:{t['panel']};--raised:{t['raised']};--field:{t['field']};--text:{t['text']};
--muted:{t['muted']};--line:{t['line']};--control:{t['control']};--accent:{t['accent']};--onaccent:{t['onaccent']};
--select:{t['select']};--danger:{t['danger']};--warning:{t['warning']};--disabled:{t['disabled']};--checked:{t['checked']};
--note:{t['note']}}}
*{{box-sizing:border-box}}
html,body{{margin:0;background:var(--bg);color:var(--text);font-family:"Noto Sans","Segoe UI",sans-serif;font-size:13px;line-height:1.3}}
h1{{font-size:17px;font-weight:650;margin:0;padding:16px 20px 2px}}
.intro{{padding:2px 20px 4px;color:var(--muted);max-width:1900px;font-size:13px}}
.grid{{display:grid;gap:18px 18px;padding:10px 20px 14px;align-items:start}}
.cap{{font-size:12px;font-weight:650;margin:0 0 3px 2px}}
.cap .rec{{color:var(--accent);margin-left:6px}}
.sub2{{font-size:12px;color:var(--muted);margin:0 0 7px 2px;max-width:640px;min-height:32px}}
.ic{{display:inline-flex;width:16px;height:16px;color:var(--text);flex:none}}
.ic svg{{width:16px;height:16px}}
.ic .acc{{color:var(--accent)}}
.tb{{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:2px;flex:none}}
.tb.on{{background:var(--checked);border-color:var(--control)}}
.tb.more.on{{background:var(--raised);border-color:var(--accent)}}
.tip{{position:absolute;top:30px;left:-6px;white-space:pre;background:var(--panel);border:1px solid var(--line);padding:4px 7px;font-size:12px;z-index:9;box-shadow:0 4px 14px rgba(0,0,0,.25);color:var(--text)}}
.bar{{display:flex;align-items:flex-start;gap:4px;padding:3px 4px 3px 6px;background:var(--panel);position:relative;white-space:nowrap;overflow:hidden;flex:none}}
.blines{{flex:1;display:flex;flex-direction:column;gap:6px;min-width:0}}
.bline{{display:flex;align-items:center;gap:6px;height:{ROW}px}}
.btrail{{display:flex;align-items:center;gap:2px;height:{ROW}px}}
.band{{display:flex;align-items:center;justify-content:center;border:1px dashed var(--danger);color:var(--danger);font-size:11px;margin-top:-6px;opacity:.85}}
.bar.float{{position:absolute;left:8px;right:8px;bottom:8px;border:1px solid var(--line);border-radius:4px;background:color-mix(in srgb,var(--panel) 88%,transparent);box-shadow:0 4px 14px rgba(0,0,0,.35);height:auto!important}}
.tool{{display:inline-flex;align-items:center;gap:6px;font-weight:650;padding-right:2px}}
.vsep{{width:1px;height:18px;background:var(--line);flex:none;margin:0 6px 0 0}}
.grp{{display:inline-flex;align-items:center;gap:3px}}
.gl{{color:var(--muted);font-size:12.5px;margin-right:3px}}
.grow{{flex:1}}
.seg{{display:inline-flex;gap:1px}}
.num{{display:inline-flex;align-items:center;gap:4px;margin-right:4px}}
.nl{{color:var(--muted);font-size:12.5px;padding:0 1px;border-bottom:1px dotted var(--control)}}
.nf{{height:24px;display:inline-flex;align-items:center;justify-content:flex-end;gap:3px;background:var(--field);border:1px solid var(--control);border-radius:2px;padding:0 6px;font-variant-numeric:tabular-nums}}
.nu{{color:var(--muted);font-size:12px}}
.ro{{display:inline-flex;gap:5px;margin:0 6px 0 4px;font-variant-numeric:tabular-nums}}
.ro .nl{{border:0}}
.combo{{height:24px;display:inline-flex;align-items:center;justify-content:space-between;gap:8px;padding:0 6px 0 8px;background:var(--raised);border:1px solid var(--control);border-radius:2px}}
.segtext{{display:inline-flex;border:1px solid var(--control);border-radius:2px;overflow:hidden;height:24px;margin-right:4px}}
.segtext span{{padding:0 8px;display:inline-flex;align-items:center;border-left:1px solid var(--line);font-size:12.5px}}
.segtext span:first-child{{border-left:0}}
.segtext .on{{background:var(--accent);color:var(--onaccent);font-weight:600}}
.combo.search{{background:var(--field);justify-content:flex-start}}
.tc{{font-size:12.5px;padding:0 4px}}
.target{{display:inline-flex;align-items:center;gap:2px}}
.dock{{background:var(--panel);border:1px solid var(--line);display:flex;flex-direction:column;position:relative;overflow:visible}}
.dhead{{height:{HEAD}px;display:flex;align-items:center;padding:0 8px 0 12px;flex:none}}
.dhead .t{{font-weight:700;flex:1}}
.dbody{{flex:1;display:flex;flex-direction:column;min-height:0}}
.top{{flex:1;display:flex;min-height:0}}
.rail{{display:flex;gap:1px;padding:2px;border:1px solid var(--line);border-radius:2px;margin:0 1px 0 1px;position:relative;overflow:hidden;flex:none}}
.rcol{{display:flex;flex-direction:column;gap:1px;margin-top:-1px}}
.rail .tb{{width:{BTN}px;height:{BTN}px}}
.rail .tb.more{{border-color:var(--line)}}
.rscroll{{position:absolute;right:1px;top:2px;bottom:2px;width:6px;border-radius:3px;background:var(--field);border:1px solid var(--line)}}
.rscroll i{{position:absolute;left:0;right:0;top:0;border-radius:3px;background:var(--muted);opacity:.6}}
.stage{{flex:1;display:flex;align-items:center;justify-content:center;background:#000;overflow:hidden;position:relative}}
.video{{position:relative;overflow:hidden;background-color:#2fa3a1;
background-image:repeating-conic-gradient(#2fa3a1 0 25%,#3cb0ae 0 50%);background-size:4px 4px}}
.sign,.sub,.xy{{position:absolute;color:#fff;font-family:Arial,sans-serif;text-shadow:-1px -1px 0 #000,1px -1px 0 #000,-1px 1px 0 #000,1px 1px 0 #000}}
.sign{{left:0;right:0;top:46%;text-align:center}}
.sub{{left:0;right:0;bottom:6%;text-align:center}}
.xy{{font-weight:700}}
.ov{{position:absolute;inset:0;width:100%;height:100%}}
.hstrip{{display:flex;gap:1px;padding:3px 6px;border-bottom:1px solid var(--line);flex:none;height:34px}}
.ctl{{flex:none}}
.seek{{height:{SEEK}px;margin:0 6px;position:relative}}
.seek::before{{content:"";position:absolute;left:0;right:0;top:8px;height:4px;background:var(--raised);border:1px solid var(--line);border-radius:2px}}
.seek i{{position:absolute;left:0;top:8px;height:4px;background:var(--accent)}}
.seek b{{position:absolute;top:3px;width:12px;height:14px;margin-left:-6px;border-radius:2px;background:var(--raised);border:1px solid var(--control)}}
.times{{height:{TIMES}px;display:flex;align-items:center;gap:2px;font-size:12.5px;padding:0 6px;font-variant-numeric:tabular-nums}}
.times .tb{{width:20px;height:20px}}
.xyro{{display:inline-flex;font-size:12.5px}}
.trans{{height:{TRANS}px;display:flex;align-items:center;gap:6px;padding:0 6px}}
.status{{text-align:center;font-size:12.5px}}
.vol{{width:90px;height:4px;background:var(--accent);border-radius:2px;position:relative;margin:0 4px 0 2px}}
.pop{{position:absolute;background:var(--panel);border:1px solid var(--line);border-radius:4px;box-shadow:0 8px 26px rgba(0,0,0,.35);padding:12px;z-index:8;display:flex;flex-direction:column;gap:8px}}
.prow{{display:flex;align-items:center;gap:6px;height:{ROW}px}}
.pop.menu{{padding:4px 0;gap:0;min-width:180px}}
.mi{{display:flex;align-items:center;height:26px;padding:0 14px 0 8px;gap:8px}}
.mi .mk{{width:18px;display:inline-flex;justify-content:center}}
.legend{{font-size:12.5px;color:var(--muted);line-height:1.5;max-width:640px}}
.legend b{{color:var(--text)}}
.verdict{{margin:0 20px 18px;padding:10px 14px;border:1px solid var(--line);border-left:3px solid var(--accent);background:var(--panel);max-width:1960px;font-size:13px;line-height:1.55}}
.verdict b{{color:var(--text)}}
.tbl{{border-collapse:collapse;font-size:12.5px;font-variant-numeric:tabular-nums}}
.tbl td,.tbl th{{border:1px solid var(--line);padding:3px 9px;text-align:right}}
.tbl th{{color:var(--muted);font-weight:600}}
.tbl td:first-child,.tbl th:first-child{{text-align:left}}
.tbl .rec td{{background:var(--select)}}
"""


def cell(cap, sub, html, rec=False):
    r = '<span class="rec">recommended</span>' if rec else ""
    return f'<div><div class="cap">{cap}{r}</div><div class="sub2">{sub}</div>{html}</div>'


W, SHORT, TALL = 640, 420, 760


def bar_page(t):
    tools = [(CROSS, "Position pointer (no controls)"), (CLIPV, "Vector clipping (seven controls)"),
             (PERSP, "Perspective (the most controls)")]
    options = [
        ("today", "Today", "The bar holds the tallest tool's two lines for every tool, so a one-line tool leaves an "
                           "empty band (red outline) and the rail scrolls."),
        ("wrap", "A · Fit and wrap", "The bar takes the lines its tool needs and wraps the rest; none for a tool "
                                     "without controls. The video resizes, animated, whenever the line count changes."),
        ("more", "B · One line, the rest under More", "Always one line; groups that don't fit fold, last first, "
                                                      "under More (the Line editor strip's pattern). No line for a "
                                                      "tool without controls."),
        ("overlay", "C · Floating over the picture", "The bar floats over the bottom of the picture and never "
                                                     "resizes the video, but it covers the subtitles."),
    ]
    rows = ""
    vheights = {}
    for mode, cap, sub in options:
        cells = ""
        for tool, tcap in tools:
            reserved = 2 if mode == "today" else None
            d, vh = dock(W, SHORT, tool, mode, rail="scroll" if mode == "today" else "columns", reserved=reserved)
            vheights[(mode, tool)] = vh
            cells += cell(f"{cap} — {tcap}", sub if tool == CROSS else "", d, rec=(mode == "more"))
        rows += f'<div class="grid" style="grid-template-columns:repeat(3,{W}px)">{cells}</div>'
    tbl = "<table class='tbl'><tr><th>Video height in a 640 × 420 panel</th>" + "".join(
        f"<th>{n}</th>" for _, n in tools) + "<th>Jumps per switch</th></tr>"
    jumps = {"today": "never", "wrap": "every change of line count", "more": "only to and from a tool without "
             "controls (one line, animated)", "overlay": "never"}
    for mode, cap, _ in options:
        tbl += f"<tr class='{'rec' if mode == 'more' else ''}'><td>{cap}</td>" + "".join(
            f"<td>{vheights[(mode, tool)]:.0f} px</td>" for tool, _ in tools) + f"<td>{jumps[mode]}</td></tr>"
    tbl += "</table>"
    body = (f"<h1>VB1 · Inspector bar: three ways to fit its content · {t['name']}</h1>"
            "<div class='intro'>A short Video panel (640 × 420, the owner's case) with the app's dummy video. "
            "The rail is drawn as recommended on the rail sheet in A–C, so only the bar differs. In A–C the bar drops "
            "the tool's icon and name: the rail's checked button already shows them, with the name on hover. "
            "Position pointer's X, Y and Copy video position move to the end of the times line.</div>"
            + rows + f"<div class='verdict'>{tbl}<br><b>Recommendation: B.</b> It never leaves an empty band, never "
            "clips, and keeps the video steady across every tool with controls. Only a switch to or from Position "
            "pointer moves the video, by one line, with a 120 ms slide. A wraps Perspective, X / Y rotation and Hydra "
            "onto two lines (three in a narrow panel), so the video jumps on most switches. C covers the subtitle "
            "line you are usually timing.</div>")
    return body


def rail_page(t):
    act = CROSS
    cells = ""
    d, _ = dock(W, SHORT, act, "more", rail="scroll")
    cells += cell("Today: a scroll bar", "Twelve tools need 351 px; this panel's rail has 248 px. The "
                  "four below the fold are reached by scrolling.", d)
    d, _ = dock(W, SHORT, HYDRA, "more", rail="more", extra=rail_more_popover(30 + 2 + 7 * 29 - 120, HYDRA, 8))
    cells += cell("R1 · More tools", "The rail ends in a More button that lists the tools that don't fit. "
                  "The on tool (Hydra here) takes the last slot so it stays in sight.", d)
    d, _ = dock(W, SHORT, act, "more", rail="columns")
    cells += cell("R2 · A second column", "The tools flow into as many 29 px columns as the height needs: two "
                  "here. Every tool stays in sight; the width comes out of the black pillarbox.", d, rec=True)
    d, _ = dock(W, SHORT, act, "more", strip=True)
    cells += cell("R3 · A horizontal strip", "On a short, wide panel the tools move to a strip above the "
                  "picture. It costs a line of height, which is what a short panel lacks.", d)
    grid1 = f'<div class="grid" style="grid-template-columns:repeat(4,{W}px)">{cells}</div>'
    cells = ""
    for h, note in [(TALL, "Tall panel: one column, as today."),
                    (250, "A 250 px panel, near its minimum: three columns of four.")]:
        d, _ = dock(W, h, act, "more", rail="columns")
        cells += cell(f"R2 · {W} × {h}", note, d, rec=True)
    grid2 = f'<div class="grid" style="grid-template-columns:repeat(2,{W}px)">{cells}</div>'
    return (f"<h1>VB1 · Tool rail: never clip a tool · {t['name']}</h1>"
            "<div class='intro'>The same short panel (640 × 420) with Position pointer on, and the bar as "
            "recommended (B).</div>" + grid1 + grid2 +
            "<div class='verdict'><b>Recommendation: R2.</b> Every tool stays one click away and in its place, the "
            "rail stays where layout A put it, and there is no menu to open. Columns are balanced (6 + 6, 4 + 4 + 4) "
            "and read top to bottom, then left to right; Up and Down still walk the tools in order. R1 hides tools; R3 "
            "takes height, which is what a short panel lacks.</div>")


def recommended_page(t):
    tools = [(CROSS, "Position pointer"), (CLIPV, "Vector clipping"), (PERSP, "Perspective")]
    cells = ""
    for tool, name in tools:
        d, _ = dock(W, TALL, tool, "more", rail="columns")
        cells += cell(f"Tall {W} × {TALL} — {name}", "", d)
    grid1 = f'<div class="grid" style="grid-template-columns:repeat(3,{W}px)">{cells}</div>'
    cells = ""
    for tool, name in tools:
        if tool == PERSP:
            d, _ = dock(W, SHORT, tool, "more-open", rail="columns",
                        extra=more_popover(42, SEEK + TIMES + TRANS + ROW + BARPAD + 2))
            cells += cell(f"Short {W} × {SHORT} — {name}, More open", "", d)
        else:
            d, _ = dock(W, SHORT, tool, "more", rail="columns")
            cells += cell(f"Short {W} × {SHORT} — {name}", "", d)
    grid2 = f'<div class="grid" style="grid-template-columns:repeat(3,{W}px)">{cells}</div>'
    legend = """
<div class='verdict'>
<b>The bar (B).</b> One line for every tool with controls, none for Position pointer. Groups keep the tool's order;
those that don't fit fold whole, last first, under More (<i>menu-more</i>, "More options" on hover), which opens PK1's
popover above it with the folded groups as they are on the bar, one group per line. The batch picker keeps its
trailing slot. A wider panel unfolds groups as soon as they fit.<br>
<b>Switching tools.</b> Among tools with controls the video never moves. To and from Position pointer the line slides
in or out over 120 ms and the picture scales with it.<br>
<b>Position pointer.</b> Its X and Y (while the pointer is over the picture) and Copy video position sit at the end of
the times line, where T1 kept the readout before VT1.<br>
<b>The rail (R2).</b> One column while it fits, then balanced columns, never a scroll bar.<br>
<b>Fullscreen</b> shares the bar, so its tool row gets the same one line and More; as today it shows no bar for
Position pointer. Its menus are MN1's and stay as they are.
</div>"""
    return (f"<h1>VB1 · Recommended: B + R2, tall and short panels · {t['name']}</h1>" + grid1 + grid2 + legend)


def page(title, t, body):
    return (f"<!doctype html><html><head><meta charset='utf-8'><title>{title}</title><style>{css(t)}</style>"
            f"</head><body>{body}</body></html>")


for key, t in THEMES.items():
    (OUT / f"vb1-{key}-bar.html").write_text(page("VB1 bar", t, bar_page(t)))
    (OUT / f"vb1-{key}-rail.html").write_text(page("VB1 rail", t, rail_page(t)))
    (OUT / f"vb1-{key}-recommended.html").write_text(page("VB1 recommended", t, recommended_page(t)))
print("ok")
