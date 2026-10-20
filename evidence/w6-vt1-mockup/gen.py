#!/usr/bin/env python3
"""VT1 #269 visual tool inspector bar mockup: one HTML sheet per theme."""
import pathlib
import re
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

THEMES = {
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", warning="#8A5A00", danger="#A51F31", disabled="#74808B", checked="#D3D9DE"),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", warning="#E0A030", danger="#FFADAD", disabled="#75818D", checked="#3A4450"),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", warning="#6B4500", danger="#A00000", disabled="#6E6E6E",
                    checked="#C9DAF8"),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", warning="#FFD54A", danger="#FFADAD", disabled="#8C8C8C",
                    checked="#253F60"),
}

FAMILIES = [("tool-crosshair", "Position pointer"), ("tool-position", "Text positioning"),
            ("tool-move", "Text moving"), ("tool-scale", "Text scaling"), ("tool-rotate-z", "Text Z rotation"),
            ("tool-rotate-xy", "Text X / Y rotation"), ("tool-clip-rect", "Rectangle clipping"),
            ("tool-clip-vector", "Vector clipping"), ("tool-drawing", "Vector drawing"),
            ("tool-move-all", "Position shifter"), ("tool-all-tags", "Hydra")]


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


def num(label, value, unit="", state="", width=74):
    return (f'<span class="num {state}"><span class="nl">{label}</span>'
            f'<span class="nf" style="width:{width}px"><span class="nv">{value}</span>'
            f'<span class="nu">{unit}</span></span></span>')


def ro(label, value):
    return f'<span class="ro"><span class="nl">{label}</span><span class="rv">{value}</span></span>'


def combo(value, width=None, state="", label=None):
    w = f' style="min-width:{width}px"' if width else ""
    lab = f'<span class="gl">{label}</span>' if label else ""
    return f'{lab}<span class="combo {state}"{w}><span class="cv">{value}</span>{chev()}</span>'


def grp(label, *items):
    lab = f'<span class="gl">{label}</span>' if label else ""
    return f'<span class="grp">{lab}{"".join(items)}</span>'


SEP = '<span class="vsep"></span>'


def tool(i, name):
    return f'<span class="tool" title="">{icon(FAMILIES[i][0])}<span class="tn">{name}</span></span>'


def target(picked=0, active=True):
    if picked:
        return ('<span class="target">' + btn("pick-lines", "on") +
                f'<span class="tc">{picked} lines</span>' + btn("clear") + '</span>')
    return '<span class="target">' + btn("pick-lines", "" if active else "dis") + '</span>'


def bar(*parts, tgt=None, cls=""):
    t = tgt if tgt is not None else ""
    return f'<div class="bar {cls}">{"".join(parts)}<span class="grow"></span>{t}</div>'


BARS = {}
BARS["crosshair"] = bar(tool(0, "Crosshair"), SEP, grp(None, ro("X", "1204"), ro("Y", "873")),
                        grp(None, btn("copy-position", tip=None)), tgt="")
BARS["position"] = bar(tool(1, "Position"), SEP,
                       grp("Rectangle", btn("frame-to-scale"), btn("scale-x", "on dis"), btn("scale-y", "on dis"),
                           combo("Bottom-center", 118)), SEP,
                       grp(None, num("X", "960"), num("Y", "1012.5")), tgt=target(0))
BARS["move"] = bar(tool(2, "Move"), SEP, grp(None, btn("two-points")), SEP,
                   grp("Start", num("X", "412"), num("Y", "880"), num("T", "0", "ms", width=62)), SEP,
                   grp("End", num("X", "1508"), num("Y", "880"), num("T", "2340", "ms", width=62)),
                   tgt=target(0))
BARS["scale"] = bar(tool(3, "Scale"), SEP,
                    grp("Rectangle", btn("frame-to-scale", "on"), btn("scale-x", "on dis"), btn("link", "on"),
                        btn("scale-y", "dis"), btn("original-frame")), SEP,
                    grp("Tags", btn("tool-scale-rotation", "on dis"), btn("resample")), SEP,
                    grp(None, num("X", "112.5", "%"), num("Y", "112.5", "%")), tgt=target(3))
BARS["scale-scrub"] = bar(tool(3, "Scale"), SEP,
                          grp("Rectangle", btn("frame-to-scale"), btn("scale-x", "on dis"), btn("link", "dis"),
                              btn("scale-y", "dis"), btn("original-frame", "dis")), SEP,
                          grp("Tags", btn("tool-scale-rotation"), btn("resample")), SEP,
                          grp(None, num("X", "126", "%", "scrub"), num("Y", "100", "%")), tgt=target(0))
BARS["rotz"] = bar(tool(4, "Rotate Z"), SEP, grp(None, btn("two-points")), SEP,
                   grp("Tags", btn("tool-scale-rotation"), btn("resample")), SEP,
                   grp(None, num("Z", "-12.4", "°", width=66)), SEP,
                   grp("Origin", num("X", "960"), num("Y", "540")), tgt=target(0))
BARS["rotxy"] = bar(tool(5, "Rotate X / Y"), SEP, grp("Tags", btn("tool-scale-rotation")), SEP,
                    grp(None, num("X", "0", "°", width=66), num("Y", "38", "°", width=66)), SEP,
                    grp("Origin", num("X", "960"), num("Y", "540")), tgt=target(0))
BARS["cliprect"] = bar(tool(6, "Rectangle clip"), SEP, grp(None, btn("clip-invert")), tgt=target(0))
MODES = ["vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete"]
BARS["clipvec"] = bar(tool(7, "Vector clip"), SEP,
                      grp("Points", f'<span class="seg">{"".join(btn(m, "on" if i == 1 else "") for i, m in enumerate(MODES))}</span>'),
                      SEP, grp(None, btn("clip-invert")), tgt=target(0))
BARS["drawing"] = bar(tool(8, "Drawing"), SEP,
                      grp("Points", f'<span class="seg">{"".join(btn(m, "dis") for m in MODES)}</span>'), SEP,
                      grp("Shape", combo("rounded square 1", 140, "open")), tgt=target(0))
BARS["shifter"] = bar(tool(9, "Position shifter"), SEP,
                      grp("Move", btn("shift-position", "on"), btn("shift-move-start", "on"),
                          btn("shift-move-end", "on"), btn("shift-clips"), btn("shift-drawings"),
                          btn("shift-origins", "on")), tgt=target(12))
BARS["hydra"] = bar(tool(10, "Hydra"), SEP, grp("Tag", combo("blur", 110)), SEP,
                    grp("Change", combo("Insert", 190)), grp(None, btn("tag-edit")), tgt=target(0))

# Narrow panel: the groups flow onto a second line, the target keeps its slot.
NARROW = ('<div class="bar wrap"><div class="flow">' + tool(3, "Scale") + SEP +
          grp("Rectangle", btn("frame-to-scale", "on"), btn("scale-x", "on dis"), btn("link", "on"),
              btn("scale-y", "dis"), btn("original-frame")) +
          grp("Tags", btn("tool-scale-rotation", "on dis"), btn("resample")) +
          grp(None, num("X", "112.5", "%"), num("Y", "112.5", "%")) +
          '</div>' + target(3) + '</div>')

SUB = '<div class="sub">I\'ll be waiting at the station.</div>'


def video(w, h, handles="pos"):
    hs = ""
    if handles == "pos":
        hs = ('<svg class="hand" viewBox="0 0 100 56.25" preserveAspectRatio="none">'
              '<rect x="49.2" y="50.2" width="1.6" height="1.6" fill="none" stroke="#ff8a00" stroke-width=".35"/>'
              '</svg>'
              '<div class="outl" style="left:30%;top:83%;width:40%;height:11%"></div>'
              '<div class="outl" style="left:20%;top:12%;width:22%;height:9%"></div>'
              '<div class="outl" style="left:62%;top:30%;width:26%;height:9%"></div>')
    return (f'<div class="video" style="width:{w}px;height:{h}px"><div class="scene"></div>'
            f'<div class="sign" style="left:20%;top:12%;font-size:{h*0.05:.0f}px">KITA-SENJU</div>'
            f'<div class="sign" style="left:62%;top:30%;font-size:{h*0.05:.0f}px">Platform 3 →</div>'
            f'<div class="sub" style="font-size:{h*0.065:.0f}px">I\'ll be waiting at the station.</div>{hs}</div>')


def rail(active, h=None):
    btns = "".join(f'<span class="tb rb {"on" if i == active else ""}">{icon(r)}</span>'
                   for i, (r, _) in enumerate(FAMILIES))
    return f'<div class="rail">{btns}</div>'


def transport():
    return ('<div class="seek"><i style="width:38%"></i><b style="left:38%"></b>'
            '<s style="left:31%;width:12%"></s></div>'
            '<div class="times"><span>0:12:41.208 &nbsp; 18263 &nbsp; +7 &nbsp; 292 / −1 041</span></div>'
            '<div class="trans">' + "".join(btn(r) for r in ["media-previous-file", "media-play", "play-line",
                                                            "media-stop", "media-next-file", "frame-previous"]) +
            '<span class="grow status">Paused</span>' + btn("frame-next") + icon("volume") +
            '<span class="vol"><i></i></span></div>')


def dock(w, h, active, barhtml, vid_h, title="Video", handles="pos"):
    return (f'<div class="dock" style="width:{w}px;height:{h}px">'
            f'<div class="dhead"><span class="t">{title}</span>{icon("panel-menu")}</div>'
            f'<div class="dbody"><div class="top">{rail(active)}<div class="stage">{video(w - 52, vid_h, handles)}</div></div>'
            f'{barhtml}{transport()}</div></div>')


def shape_menu():
    items = ["Choose", "rectangle", "circle", "rounded square 1", "rounded square 2", "rounded square 3"]
    rows = "".join(f'<div class="mi {"chk" if i == 3 else ""}"><span class="mk">{"✓" if i == 3 else ""}</span>'
                   f'<span>{"Free drawing" if i == 0 else it}</span></div>' for i, it in enumerate(items))
    return (f'<div class="menu" style="left:410px;top:38px">{rows}<div class="ms"></div>'
            f'<div class="mi"><span class="mk">{icon("edit")}</span><span>Edit shapes…</span></div></div>')


def hy1_sketch():
    return ('<div class="bar sketch">' + tool(10, "Hydra") + SEP +
            '<span class="grp"><span class="search">' + icon("search") + '<span>blur</span></span></span>' +
            grp(None, num("Value", "2.5", width=64)) + SEP +
            '<span class="grp"><span class="segtext"><span class="on">Set</span><span>Add</span><span>Multiply</span>'
            '<span>Gradient</span></span>' + chev() + '</span>' +
            '<span class="grow"></span><span class="target"><span class="tc strong">Changes 3 lines</span>' +
            btn("pick-lines", "on") + btn("clear") + '</span></div>')


def fullscreen(t):
    fam = "".join(f'<span class="tb {"on" if i == 1 else ""}">{icon(r)}</span>' for i, (r, _) in enumerate(FAMILIES))
    return ('<div class="fs"><div class="fsstage">' + video(800, 450, "pos") + '</div>' +
            '<div class="fsbar"><div class="seek"><i style="width:38%"></i><b style="left:38%"></b></div>'
            '<div class="trans">' + "".join(btn(r) for r in ["media-previous-file", "media-play", "play-line",
                                                            "media-stop", "media-next-file"]) +
            btn("show-toolbar", "on") + '<span class="times">0:12:41.208 / 0:23:40.012</span>'
            '<span class="grow status">frieren-27.mkv</span>' + icon("volume") + '<span class="vol"><i></i></span></div>'
            '<div class="fsrow"><span class="fam">' + fam + '</span>' + SEP +
            BARS["position"].replace('<div class="bar ">', '<div class="bar inline">') + '</div></div></div>')


def css(t):
    return f"""
:root{{--bg:{t['bg']};--panel:{t['panel']};--raised:{t['raised']};--field:{t['field']};--text:{t['text']};
--muted:{t['muted']};--line:{t['line']};--control:{t['control']};--accent:{t['accent']};--onaccent:{t['onaccent']};
--select:{t['select']};--danger:{t['danger']};--warning:{t['warning']};--disabled:{t['disabled']};--checked:{t['checked']}}}
*{{box-sizing:border-box}}
html,body{{margin:0;background:var(--bg);color:var(--text);font-family:"Noto Sans","Segoe UI",sans-serif;font-size:13px;line-height:1.3}}
.themeName{{font-size:15px;font-weight:650;padding:14px 20px 0}}
.row{{display:flex;gap:24px;padding:12px 20px 4px;align-items:flex-start}}
.cap{{font-size:11px;color:var(--muted);font-weight:600;letter-spacing:.03em;margin:0 0 6px 2px;text-transform:uppercase}}
.ic{{display:inline-flex;width:16px;height:16px;color:var(--text);flex:none}}
.ic svg{{width:16px;height:16px}}
.ic .acc{{color:var(--accent)}}
/* tool buttons (Fusion look: panel only when hovered, pressed or checked) */
.tb{{position:relative;width:24px;height:24px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:2px;flex:none}}
.tb.on{{background:var(--checked);border-color:var(--control)}}
.tb.dis .ic{{color:var(--disabled)}} .tb.dis .ic .acc{{color:var(--disabled)}}
.tb.dis.on{{border-color:var(--line)}}
.tb.hov{{background:var(--raised);border-color:var(--control)}}
.tip{{position:absolute;top:30px;left:-6px;white-space:pre;background:var(--panel);border:1px solid var(--line);padding:4px 7px;font-size:12px;z-index:9;box-shadow:0 4px 14px rgba(0,0,0,.25);color:var(--text)}}
/* the bar */
.bar{{display:flex;align-items:center;gap:6px;height:36px;padding:0 8px;background:var(--panel);border-top:1px solid var(--line);position:relative;white-space:nowrap}}
.bar.strip{{border:1px solid var(--line)}}
.bar.inline{{border:0;background:transparent;padding:0;height:32px;flex:1}}
.tool{{display:inline-flex;align-items:center;gap:6px;font-weight:650;padding-right:2px}}
.vsep{{width:1px;height:20px;background:var(--line);flex:none;margin:0 2px}}
.grp{{display:inline-flex;align-items:center;gap:3px}}
.gl{{color:var(--muted);font-size:12px;margin-right:4px}}
.grow{{flex:1}}
.seg{{display:inline-flex;gap:1px}}
.num{{display:inline-flex;align-items:center;gap:4px;margin-right:6px}}
.nl{{color:var(--muted);cursor:ew-resize;font-size:12px;padding:0 1px;border-bottom:1px dotted var(--control)}}
.nf{{height:24px;display:inline-flex;align-items:center;justify-content:flex-end;gap:3px;background:var(--field);border:1px solid var(--control);border-radius:2px;padding:0 6px;font-variant-numeric:tabular-nums}}
.nu{{color:var(--muted);font-size:12px;min-width:0}}
.num.scrub .nl{{color:var(--accent);border-bottom-color:var(--accent);position:relative}}
.num.scrub .nl::after{{content:'\\2194';position:absolute;left:-4px;top:14px;font-size:18px;color:var(--text);text-shadow:0 0 2px var(--bg)}}
.num.scrub .nf{{border-color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent)}}
.num.dis .nf{{color:var(--disabled);border-color:var(--line)}}
.ro{{display:inline-flex;gap:4px;margin-right:10px;font-variant-numeric:tabular-nums}}
.ro .nl{{border:0;cursor:default}}
.combo{{height:24px;display:inline-flex;align-items:center;justify-content:space-between;gap:8px;padding:0 6px 0 8px;background:var(--raised);border:1px solid var(--control);border-radius:2px}}
.combo .chev,.chev{{color:var(--text)}}
.combo.open{{border-color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent)}}
.target{{display:inline-flex;align-items:center;gap:3px}}
.tc{{font-size:12px;color:var(--text);font-variant-numeric:tabular-nums;padding:0 2px}}
.tc.strong{{font-weight:650}}
/* sketch for HY1 */
.bar.sketch{{border:1px dashed var(--control);background:var(--panel)}}
.search{{display:inline-flex;align-items:center;gap:6px;height:24px;min-width:120px;padding:0 8px;background:var(--field);border:1px solid var(--control);border-radius:2px}}
.segtext{{display:inline-flex;border:1px solid var(--control);border-radius:2px;overflow:hidden;height:24px}}
.segtext span{{padding:0 9px;display:inline-flex;align-items:center;border-left:1px solid var(--line);font-size:12px}}
.segtext span:first-child{{border-left:0}}
.segtext .on{{background:var(--accent);color:var(--onaccent);font-weight:600}}
.lastop{{position:absolute;left:12px;bottom:12px;background:var(--panel);border:1px dashed var(--control);padding:8px 10px;font-size:12px;width:220px}}
/* dock */
.dock{{background:var(--panel);border:1px solid var(--line);display:flex;flex-direction:column;position:relative}}
.dhead{{height:30px;display:flex;align-items:center;padding:0 8px 0 12px;border-bottom:1px solid var(--line);background:var(--field)}}
.dhead .t{{font-weight:700;flex:1}}
.dbody{{flex:1;display:flex;flex-direction:column;min-height:0}}
.top{{flex:1;display:flex;min-height:0}}
.rail{{width:32px;display:flex;flex-direction:column;gap:1px;padding:2px;border-right:1px solid var(--line)}}
.rail .tb{{width:28px;height:28px}}
.stage{{flex:1;display:flex;align-items:center;justify-content:center;background:#000;overflow:hidden}}
.video{{position:relative;overflow:hidden;background:#000}}
.scene{{position:absolute;inset:0;background:linear-gradient(180deg,#1d2a3f 0%,#34465e 46%,#5b4b44 47%,#2a2522 100%)}}
.scene::after{{content:"";position:absolute;left:8%;right:8%;top:47%;height:3%;background:#c9b48a;opacity:.55}}
.sign{{position:absolute;color:#f4f0e2;font-family:"Noto Serif",serif;font-weight:700;letter-spacing:.06em;text-shadow:0 0 2px #000,0 0 2px #000}}
.sub{{position:absolute;left:0;right:0;bottom:7%;text-align:center;color:#fff;font-weight:600;text-shadow:-2px -2px 0 #000,2px -2px 0 #000,-2px 2px 0 #000,2px 2px 0 #000,0 0 3px #000}}
.hand{{position:absolute;inset:0;width:100%;height:100%}}
.outl{{position:absolute;border:1px dashed #6fd3ff}}
.seek{{height:14px;margin:4px 8px 0;position:relative}}
.seek::before{{content:"";position:absolute;left:0;right:0;top:6px;height:3px;background:var(--raised);border:1px solid var(--line)}}
.seek i{{position:absolute;left:0;top:6px;height:3px;background:var(--accent)}}
.seek b{{position:absolute;top:2px;width:10px;height:10px;margin-left:-5px;border-radius:50%;background:var(--text)}}
.seek s{{position:absolute;top:11px;height:3px;background:var(--warning);opacity:.7}}
.times{{font-size:12px;color:var(--text);padding:2px 10px;font-variant-numeric:tabular-nums}}
.trans{{display:flex;align-items:center;gap:2px;padding:2px 8px 6px}}
.status{{text-align:center;font-size:12px;color:var(--muted)}}
.vol{{width:70px;height:3px;background:var(--raised);border:1px solid var(--line);position:relative;margin:0 4px 0 6px}}
.vol i{{position:absolute;left:0;top:0;bottom:0;width:70%;background:var(--accent)}}
/* menu */
.menu{{position:absolute;background:var(--panel);border:1px solid var(--line);box-shadow:0 10px 30px rgba(0,0,0,.33);border-radius:5px;padding:4px 0;z-index:8;min-width:190px}}
.mi{{display:flex;align-items:center;height:26px;padding:0 14px 0 6px;gap:6px}}
.mi .mk{{width:18px;display:inline-flex;justify-content:center;color:var(--text)}}
.mi.chk{{font-weight:600}}
.mi.hi{{background:var(--accent);color:var(--onaccent)}}
.ms{{height:1px;background:var(--line);margin:4px 0}}
/* fullscreen */
.fs{{position:relative;width:960px;height:540px;border:1px solid var(--line);overflow:hidden}}
.fs{{display:flex;flex-direction:column;background:#000}}
.fsstage{{flex:1;display:flex;align-items:center;justify-content:center;min-height:0}}
.fsbar{{background:var(--panel);border-top:1px solid var(--line)}}
.fsbar .trans{{padding-bottom:2px}}
.fsrow{{display:flex;align-items:center;gap:6px;padding:2px 8px 6px;border-top:1px solid var(--line)}}
.fam{{display:inline-flex;gap:1px}}
.fsbar .times{{padding:0 8px}}
.bar.wrap{{height:auto;align-items:flex-start;padding:5px 8px;white-space:normal}}
.flow{{display:flex;flex-wrap:wrap;gap:6px 6px;align-items:center;flex:1;min-width:0}}
.flow .grp{{height:26px}}
.strips{{display:flex;flex-direction:column;gap:10px}}
.lab{{font-size:11px;color:var(--muted);margin:0 0 3px 2px}}
.legend{{font-size:12px;color:var(--muted);max-width:420px;line-height:1.45}}
.legend b{{color:var(--text)}}
.cursor{{position:absolute;width:18px;height:18px}}
"""


def strip(label, html, extra=""):
    return f'<div><div class="lab">{label}</div><div style="position:relative">{html.replace(chr(60)+"div class=\"bar ", chr(60)+"div class=\"bar strip ", 1)}{extra}</div></div>'


def page(key, t):
    scrub_tip = ('<div class="tip" style="top:40px;left:560px;white-space:normal;width:250px">'
                 'Drag the label to change · Shift: 0.1 steps · Esc cancels. Type a value and press Enter. '
                 'Each drag or Enter is one undo step.</div>')
    body = f"""
<div class="themeName">VT1 · Visual tool inspector bar · {t['name']}</div>
<div class="row">
 <div><div class="cap">1 · Video panel, Position tool</div>{dock(860, 640, 1, BARS['position'], 455)}</div>
 <div style="display:flex;flex-direction:column;gap:18px">
  <div><div class="cap">2 · Narrow panel: groups wrap, the target stays</div>
   <div style="width:420px">{NARROW}</div></div>
  <div><div class="cap">3 · Scrubbing Scale X (live preview, one undo step on release)</div>
   <div style="position:relative;width:820px">{BARS['scale-scrub'].replace('<div class="bar ', '<div class="bar strip ', 1)}{scrub_tip}</div></div>
  <div style="margin-top:60px"><div class="cap">4 · Drawing: shape list open</div>
   <div style="position:relative;width:820px;height:230px">{BARS['drawing'].replace('<div class="bar ', '<div class="bar strip ', 1)}{shape_menu()}</div></div>
 </div>
</div>
<div class="row"><div class="strips" style="width:1000px">
 <div class="cap">5 · The bar for each of the 11 tools</div>
 {strip("Crosshair: the pointer's position (read-only) and Copy video position", BARS['crosshair'])}
 {strip("Position: rectangle mode off, so X and Y are greyed", BARS['position'])}
 {strip("Move: start and end points, with their \\move times", BARS['move'])}
 {strip("Scale: by rectangle with the aspect ratio kept; three Lines picked", BARS['scale'])}
 {strip("Rotate Z", BARS['rotz'])}
 {strip("Rotate X / Y", BARS['rotxy'])}
 {strip("Rectangle clip", BARS['cliprect'])}
 {strip("Vector clip: Add line on", BARS['clipvec'])}
 {strip("Drawing: a shape is chosen, so the point modes are greyed", BARS['drawing'].replace(' open"', '"'))}
 {strip("Position shifter: twelve Lines picked", BARS['shifter'])}
 {strip("Hydra as VT1 leaves it (the same three controls, now labelled)", BARS['hydra'])}
</div>
<div style="display:flex;flex-direction:column;gap:12px;width:400px;padding-top:22px">
 <div class="legend"><b>Reading the bar.</b> The tool's icon and name lead. Options are grouped under a short label. Number fields sit last, before the trailing target slot: Pick selected lines, how many are picked, and Clear.</div>
 <div class="legend"><b>Toggles</b> are icons with their legacy help text on hover; a greyed icon is unavailable in the current mode. <b>Choices</b> show the current value as text (alignment, shape, tag, change option) rather than hide it behind an icon menu.</div>
 <div class="legend"><b>Number fields:</b> drag the dotted label sideways to scrub (Shift for 0.1), or type and press Enter. A drag or an Enter is one undo step, and Esc during a drag cancels it. A field with no target (the Line is not on the video) is greyed.</div>
 <div class="legend"><b>Always there.</b> The bar keeps its height for every tool, including the crosshair, so the video never resizes when you change tools or the pointer moves. The read-only readout beside the video times is gone; its values are here.</div>
</div></div>
<div class="row">
 <div><div class="cap">6 · Fullscreen, toolbar shown: the same bar in the tool row (FS1 restyles the overlay)</div>{fullscreen(t)}</div>
 <div style="display:flex;flex-direction:column;gap:12px;width:740px">
  <div class="cap">7 · Room for HY1 (sketch only, not built in VT1)</div>
  {hy1_sketch()}
  <div class="legend">HY1 replaces Hydra's middle groups with its strip: a searchable tag field, a value field per tag value, the change option as a labelled switch, and "Changes N lines" in the target slot, with those Lines outlined on the video. The last-operation card floats over the video's bottom-left corner, above the bar, so the bar itself needs no extra height.</div>
  <div style="position:relative;height:150px;border:1px solid var(--line);background:#1d2a3f">
   <div class="lastop"><b>Last change</b> · blur · Insert · 3 lines<br><span style="display:inline-flex;gap:6px;margin-top:6px">{num("Value", "2.5", width=64)}</span></div>
  </div>
 </div>
</div>
"""
    return f"<!doctype html><html><head><meta charset='utf-8'><title>VT1 {t['name']}</title><style>{css(t)}</style></head><body>{body}</body></html>"


for key, t in THEMES.items():
    (OUT / f"vt1-{key}.html").write_text(page(key, t))
print("ok")
