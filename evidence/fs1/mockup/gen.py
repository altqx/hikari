#!/usr/bin/env python3
"""FS1 #275 fullscreen video mockup: one HTML page per theme, screenshotted by shoot.sh.

The icons are the K1/IC1 set from src/ui/icons (inlined as symbols, the accent
group in the theme's accent); the colours are the theme layer's roles
(src/ui/theme.cpp baseRoles, the default accent preset)."""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-fs1")
OUT = pathlib.Path(__file__).resolve().parent

ICONS = ["media-previous-file", "media-play", "media-pause", "play-line", "media-stop", "media-next-file",
         "volume", "fullscreen-exit", "show-toolbar", "editor", "settings-audio", "audio-output",
         "tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy",
         "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "clip-invert",
         "panel-menu"]
# New glyph for the muted speaker (would join IC1's set): volume's body, a cross for the waves.
EXTRA = {"volume-muted": '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1" '
         'stroke-linecap="round" stroke-linejoin="round"><path d="M1.5 6h2.5l3.5-3v10L4 10H1.5z"/>'
         '<g id="accent"><path d="M10.5 6l4 4M14.5 6l-4 4"/></g></svg>'}

THEMES = {
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", warning="#8A5A00", disabled="#74808B", shadow="rgba(0,0,0,.28)"),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", warning="#E0A030", disabled="#75818D", shadow="rgba(0,0,0,.5)"),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", warning="#6B4500", disabled="#6E6E6E", shadow="none"),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", warning="#FFD54A", disabled="#8C8C8C", shadow="none"),
}


def symbol(name, svg):
    inner = re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip())
    inner = inner.replace('<g id="accent">', '<g class="acc">')
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    return f'<symbol id="i-{name}" viewBox="0 0 16 16"><g {attrs}>{inner}</g></symbol>'


def sprite():
    out = [symbol(n, (TREE / "src/ui/icons" / f"{n}.svg").read_text()) for n in ICONS]
    out += [symbol(n, s) for n, s in EXTRA.items()]
    return '<svg width="0" height="0" style="position:absolute"><defs>' + "".join(out) + "</defs></svg>"


def ic(name, size=16, cls=""):
    return f'<svg class="ic {cls}" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


def btn(name, tip=None, cls="", size=18):
    t = f' title="{tip}"' if tip else ""
    return f'<span class="b {cls}"{t}>{ic(name, size)}</span>'


TOOLS = ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy",
         "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"]


def picture(kind="dialogue", inset=None):
    """The video picture: a dusk platform scene drawn in CSS, and the subtitles over it."""
    style = f' style="{inset}"' if inset else ""
    subs = ""
    if kind == "dialogue":
        subs = '<div class="sub">We\'ll meet again at the station,<br>I promise.</div>'
    elif kind == "sign":
        subs = ('<div class="sign">駅 STATION</div>'
                '<div class="cliprect"><i class="h tl"></i><i class="h tr"></i><i class="h bl"></i><i class="h br"></i></div>'
                '<div class="sub">We\'ll meet again at the station,<br>I promise.</div>')
    return (f'<div class="pic"{style}><div class="sky"></div><div class="sun"></div><div class="hills"></div>'
            f'<div class="platform"></div><div class="poles"></div>{subs}</div>')


def seek(pos=0.06, chapters=(0.18, 0.52, 0.81)):
    ticks = "".join(f'<i class="chap" style="left:{c*100:.1f}%"></i>' for c in chapters)
    return (f'<div class="seek"><div class="track"><div class="fill" style="width:{pos*100:.1f}%"></div>{ticks}'
            f'<div class="knob" style="left:{pos*100:.1f}%"></div></div></div>')


def bar(playing=True, tool=None, mode="Auto", muted=False, down=None, inspector=False):
    transport = (btn("media-previous-file", "Previous video file (,)") +
                 btn("media-pause" if playing else "media-play", "Pause (Space)" if playing else "Play (Space)") +
                 btn("play-line", "Play the current line") + btn("media-stop", "Stop") +
                 btn("media-next-file", "Next file (.)"))
    times = '<span class="times mono">00:01:23,450;  2003;  48;  1340 ms, -1960 ms</span>'
    line = ('<span class="line"><span class="ln">12</span><span class="lt mono">0:01:22.11 – 0:01:25.41</span>'
            '<span class="ltext">We\'ll meet again at the station, <span class="nl">↵</span> I promise.</span></span>'
            + btn("editor", "Open editor"))
    tool_icon = TOOLS[tool] if tool is not None else "tool-crosshair"
    right = (btn(tool_icon, "Visual tools", "split" + (" down" if down == "tools" else ""), 18) +
             '<span class="sep"></span>' +
             btn("volume-muted" if muted else "volume", "Volume", "speaker" + (" down" if down == "speaker" else "")) +
             '<span class="vol"><span class="vtrack"><span class="vfill" style="width:%d%%"></span></span></span>' % (0 if muted else 78) +
             '<span class="sep"></span>' +
             f'<span class="b mode{" down" if down == "mode" else ""}">{ic("show-toolbar", 18)}<span class="cap">{mode}</span></span>' +
             btn("fullscreen-exit", "Exit full screen (Esc)"))
    insp = ""
    if inspector:
        insp = ('<div class="slot"><span class="slottag">VT1 tool inspector bar (#269)</span>'
                '<span class="pl"><b>Clip rectangle</b></span>'
                '<span class="pl"><span class="k">x1</span><span class="num mono">412</span><span class="k">y1</span><span class="num mono">188</span>'
                '<span class="k">x2</span><span class="num mono">868</span><span class="k">y2</span><span class="num mono">336</span></span>'
                f'<span class="pl">{btn("clip-invert", "Invert clip", "", 16)}<span class="k">Invert</span></span>'
                '<span class="slotnote">slot: VT1 fills this row; FS1 only places it</span></div>')
    return (f'<div class="bar">{insp}{seek()}<div class="row"><span class="grp">{transport}</span>'
            f'<span class="sep"></span>{times}<span class="sep"></span>{line}<span class="grow"></span>{right}</div></div>')


def rail(active):
    items = "".join(f'<span class="b{" on" if i == active else ""}">{ic(t, 18)}</span>' for i, t in enumerate(TOOLS))
    return f'<div class="rail">{items}</div>'


def title():
    return '<div class="title"><span>Episode 04 [1080p].mkv</span></div>'


def progress():
    return ('<div class="prog"><div class="pframe"><div class="pbar" style="width:6%"></div></div>'
            '<div class="ptext">00:01:23 / 00:23:40</div></div>')


def output_menu():
    rows = [("System default", True), ("Built-in Audio Analog Stereo", False), ("Scarlett 2i2 USB", False)]
    items = "".join(f'<div class="it">{"<span class=chk>✓</span>" if c else "<span class=chk></span>"}{t}</div>' for t, c in rows)
    return (f'<div class="menu" style="right:150px;bottom:44px;width:250px">{items}<div class="msep"></div>'
            f'<div class="it">{ic("settings-audio")}Audio settings…</div></div>')


def mode_menu():
    rows = [("Auto", "Show on pointer movement, hide when idle", False), ("Always", "Keep the bar; the picture ends above it", True),
            ("Never", "Picture only", False)]
    items = "".join(f'<div class="it two">{"<span class=chk>✓</span>" if c else "<span class=chk></span>"}'
                    f'<span><span>{t}</span><small>{d}</small></span></div>'
                    for t, d, c in rows)
    return f'<div class="menu" style="right:40px;bottom:58px;width:300px"><div class="mh">Full screen bar<span class="key" style="float:right">Tab cycles</span></div>{items}</div>'


def tools_menu():
    names = ["Crosshair", "Position", "Move", "Scale", "Rotate Z", "Rotate X/Y", "Clip rectangle", "Vector clip",
             "Drawing", "Move all", "All tags"]
    items = "".join(f'<div class="it">{"<span class=chk>✓</span>" if i == 0 else "<span class=chk></span>"}{ic(TOOLS[i])}{n}</div>'
                    for i, n in enumerate(names))
    return f'<div class="menu" style="right:300px;bottom:58px;width:210px">{items}</div>'


def tip(text, style):
    return f'<div class="tip" style="{style}">{text}</div>'


def scene(caption, body):
    return f'<figure><div class="screen">{body}</div><figcaption>{caption}</figcaption></figure>'


def page(key):
    t = THEMES[key]
    var = ";".join(f"--{k}:{v}" for k, v in t.items() if k != "name")
    scenes = [
        scene("<b>1 · Auto, playing, the pointer moved.</b> The bar and the file name fade in; after 1.5 s without "
              "movement they fade out with the pointer. The speaker names its three gestures in its tooltip. The tool rail is hidden: no visual tool is on.",
              picture() + title() + bar()
              + tip("Volume −12 dB · click: mute · wheel: volume · right-click: output device", "right:96px;bottom:52px")
              + '<div class="cursor" style="left:1082px;top:678px"></div>'),
        scene("<b>2 · Paused with Clip rectangle on.</b> Paused, the bar stays. The rail shows while a tool is on, "
              "and the bar's top row is the VT1 inspector slot (dashed here). The handles stay drawn.",
              picture("sign") + title() + rail(6) + bar(playing=False, tool=6, inspector=True)
              + '<div class="cursor" style="left:872px;top:340px"></div>'),
        scene("<b>3 · Speaker:</b> click mutes, wheel sets the volume, right-click opens the AV1 output menu. "
              "An open menu holds the bar. The legacy progress bar (VIDEO_PROGRESS_BAR) stays top right.",
              picture() + title() + progress() + bar(down="speaker") + output_menu()),
        scene("<b>4 · Always</b> (today's \"Show toolbar\"): the bar is pinned and the picture ends above it. "
              "The mode button (or Tab) cycles Auto → Always → Never; its menu names the three.",
              picture(inset="left:76px;right:76px;bottom:86px") + bar(mode="Always", down="mode") + mode_menu()),
        scene("<b>5 · Auto idle, or Never:</b> only the picture; the pointer is hidden. On entering full screen a "
              "hint names the way out for 2 s.",
              picture() + '<div class="hint">Press <span class="kbd">Esc</span> to exit full screen</div>'),
        scene("<b>6 · Choosing a tool without the rail:</b> the bar's tool button lists the eleven tools "
              "(their hotkeys keep working). Speaker muted: the new volume-muted glyph, slider at the bottom.",
              picture() + title() + bar(muted=True, down="tools") + tools_menu()),
    ]
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><title>FS1 fullscreen mockup: {t['name']}</title>
<style>
:root {{ {var}; --font:"Noto Sans","DejaVu Sans",sans-serif; --mono:"DejaVu Sans Mono",monospace; }}
body {{ margin:0; background:#3a3f45; font-family:var(--font); font-size:12px; color:#e8edf2; }}
h1 {{ font-size:18px; margin:0; padding:20px 24px 4px; font-weight:600; }}
.sub1 {{ padding:0 24px 8px; color:#b9c3cc; font-size:13px; }}
.sheet {{ display:grid; grid-template-columns:repeat(2, 1280px); gap:24px; padding:16px 24px 24px; width:max-content; }}
figure {{ margin:0; }}
figcaption {{ font-size:14px; line-height:1.45; padding:8px 2px 0; color:#dfe5ea; max-width:1280px; }}
.screen {{ width:1280px; height:720px; position:relative; overflow:hidden; background:#000; font-family:var(--font); color:var(--text); }}
.ic {{ display:block; flex:none; }}
.acc {{ color:var(--accent); stroke:var(--accent); }}
.mono {{ font-family:var(--mono); font-variant-numeric:tabular-nums; }}

/* the picture */
.pic {{ position:absolute; inset:0; overflow:hidden; }}
.sky {{ position:absolute; inset:0; background:linear-gradient(#2d3a64 0%, #8a5a7d 45%, #e59a6a 70%, #f3c27d 80%); }}
.sun {{ position:absolute; left:58%; top:52%; width:120px; height:120px; border-radius:50%; background:#ffd9a0; filter:blur(2px); }}
.hills {{ position:absolute; left:-5%; right:-5%; top:62%; bottom:0; background:#3b2b45; border-radius:50% 50% 0 0 / 18% 18% 0 0; }}
.platform {{ position:absolute; left:0; right:0; top:74%; bottom:0; background:linear-gradient(#262033, #15121d); }}
.poles {{ position:absolute; left:8%; right:8%; top:30%; height:46%; background:repeating-linear-gradient(90deg, transparent 0 210px, #1b1724 210px 220px); }}
.sub {{ position:absolute; left:0; right:0; bottom:13%; text-align:center; font:600 38px/1.2 "Noto Sans",sans-serif; color:#fff;
        text-shadow:0 0 3px #000,0 0 3px #000,2px 2px 0 #000,-2px 2px 0 #000,2px -2px 0 #000,-2px -2px 0 #000; }}
.sign {{ position:absolute; left:412px; top:188px; width:456px; height:148px; display:grid; place-items:center;
         font:700 64px/1 "Noto Sans CJK JP","Noto Sans",sans-serif; color:#f4efe6; background:#2b4f6e; border:4px solid #f4efe6; box-sizing:border-box;
         transform:perspective(900px) rotateY(-8deg); }}
.cliprect {{ position:absolute; left:412px; top:188px; width:456px; height:148px; border:1px dashed #fff; outline:1px dashed #000; outline-offset:-2px; }}
.cliprect .h {{ position:absolute; width:8px; height:8px; background:#fff; border:1px solid #000; }}
.h.tl {{ left:-5px; top:-5px; }} .h.tr {{ right:-5px; top:-5px; }} .h.bl {{ left:-5px; bottom:-5px; }} .h.br {{ right:-5px; bottom:-5px; }}
.cursor {{ position:absolute; width:0; height:0; border-left:7px solid #fff; border-right:7px solid transparent; border-bottom:14px solid transparent;
           border-top:7px solid #fff; filter:drop-shadow(0 0 1px #000); transform:rotate(-15deg); }}

/* chrome over the picture */
.title {{ position:absolute; left:0; right:0; top:0; height:44px; display:flex; align-items:center; padding:0 16px; font-size:13px;
          background:var(--panel); border-bottom:1px solid var(--line); }}
.title span {{ color:var(--text); }}
.bar {{ position:absolute; left:0; right:0; bottom:0; background:var(--panel); border-top:1px solid var(--line); padding:4px 8px 6px; }}
.seek {{ height:18px; display:flex; align-items:center; padding:0 6px; }}
.track {{ position:relative; flex:1; height:4px; border-radius:2px; background:var(--line); }}
.fill {{ position:absolute; left:0; top:0; bottom:0; background:var(--accent); border-radius:2px; }}
.chap {{ position:absolute; top:-3px; width:2px; height:10px; background:var(--muted); }}
.knob {{ position:absolute; top:-5px; width:14px; height:14px; margin-left:-7px; border-radius:50%; background:var(--field); border:1px solid var(--control); box-sizing:border-box; }}
.row {{ display:flex; align-items:center; height:34px; gap:2px; }}
.grp {{ display:flex; gap:2px; }}
.b {{ width:30px; height:30px; display:grid; place-items:center; border-radius:3px; color:var(--text); flex:none; position:relative; }}
.b.on {{ background:var(--select); box-shadow:inset 0 0 0 1px var(--accent); }}
.b.down {{ background:var(--raised); box-shadow:inset 0 0 0 1px var(--control); }}
.b.split::after {{ content:""; position:absolute; right:2px; bottom:3px; border-left:3px solid transparent; border-right:3px solid transparent; border-top:3px solid var(--muted); }}
.b.mode {{ width:auto; display:flex; gap:6px; padding:0 8px; }}
.b.mode .cap {{ font-size:12px; color:var(--text); }}
.sep {{ width:1px; height:20px; background:var(--line); margin:0 8px; flex:none; }}
.times {{ font-size:12px; color:var(--text); white-space:pre; }}
.line {{ display:flex; align-items:center; gap:8px; min-width:0; flex:0 1 auto; overflow:hidden; }}
.ln {{ color:var(--muted); font-size:11px; border:1px solid var(--line); border-radius:2px; padding:0 4px; }}
.lt {{ color:var(--muted); font-size:12px; white-space:nowrap; }}
.ltext {{ color:var(--text); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; font-size:13px; max-width:330px; }}
.nl {{ color:var(--muted); }}
.grow {{ flex:1; }}
.vol {{ width:80px; height:30px; display:flex; align-items:center; padding:0 4px; }}
.vtrack {{ position:relative; flex:1; height:4px; background:var(--line); border-radius:2px; }}
.vfill {{ position:absolute; left:0; top:0; bottom:0; background:var(--accent); border-radius:2px; }}
.vfill::after {{ content:""; position:absolute; right:-6px; top:-5px; width:12px; height:12px; border-radius:50%; background:var(--field); border:1px solid var(--control); box-sizing:border-box; }}
.rail {{ position:absolute; left:8px; top:56px; display:flex; flex-direction:column; gap:1px; padding:2px; background:var(--panel); border:1px solid var(--line); border-radius:3px; }}
.slot {{ display:flex; align-items:center; gap:14px; height:34px; margin:2px 0 4px; padding:0 10px; border:1px dashed var(--accent); border-radius:3px; position:relative; }}
.slottag {{ position:absolute; top:-9px; left:10px; background:var(--panel); color:var(--accent); font-size:10px; padding:0 4px; font-weight:600; }}
.slotnote {{ margin-left:auto; color:var(--muted); font-size:11px; font-style:italic; }}
.pl {{ display:flex; align-items:center; gap:6px; }}
.pl .k {{ color:var(--muted); }}
.pl .num {{ min-width:44px; height:24px; display:flex; align-items:center; padding:0 6px; box-sizing:border-box; border:1px solid var(--control); border-radius:1px; background:var(--field); color:var(--text); }}
.prog {{ position:absolute; right:14px; top:58px; text-align:right; }}
.pframe {{ width:170px; height:12px; border:1px solid #000; outline:1px solid #fff; outline-offset:-2px; position:relative; margin-left:auto; }}
.pbar {{ position:absolute; left:2px; top:2px; bottom:2px; background:#fff; }}
.ptext {{ color:#fff; font-size:18px; margin-top:4px; text-shadow:0 0 2px #000,1px 1px 0 #000,-1px -1px 0 #000,1px -1px 0 #000,-1px 1px 0 #000; }}
.menu {{ position:absolute; background:var(--panel); border:1px solid var(--line); border-radius:5px; padding:4px 0; box-shadow:0 10px 30px var(--shadow); color:var(--text); font-size:12px; }}
.menu .it {{ display:flex; align-items:center; gap:8px; padding:5px 10px; }}
.menu .it.two {{ align-items:flex-start; }}
.menu .it.two > span:nth-child(2) {{ display:flex; flex-direction:column; flex:1; }}
.menu small {{ color:var(--muted); font-size:11px; }}
.menu .key {{ color:var(--muted); }}
.menu .chk {{ width:12px; color:var(--accent); font-weight:700; }}
.menu .msep {{ border-top:1px solid var(--line); margin:4px 0; }}
.menu .mh {{ padding:4px 10px 6px; color:var(--muted); font-size:11px; }}
.menu .it:first-of-type.hl {{ background:var(--select); }}
.tip {{ position:absolute; background:var(--panel); color:var(--text); border:1px solid var(--line); padding:4px 7px; font-size:11px; box-shadow:0 4px 12px var(--shadow); white-space:nowrap; }}
.hint {{ position:absolute; left:50%; top:40px; transform:translateX(-50%); background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:16px; padding:7px 16px; font-size:13px; box-shadow:0 6px 18px var(--shadow); }}
.kbd {{ border:1px solid var(--control); border-radius:3px; padding:0 5px; font-family:var(--mono); font-size:12px; }}
</style></head><body>{sprite()}
<h1>FS1 #275 · Full screen video · {t['name']}</h1>
<div class="sub1">1280×720 monitor at 1:1. Icons from the K1/IC1 set; colours are the theme's roles. The picture and the subtitles are drawn stand-ins.</div>
<div class="sheet">{''.join(scenes)}</div></body></html>"""


for key in THEMES:
    (OUT / f"fs1-{key}.html").write_text(page(key))
print("wrote", ", ".join(f"fs1-{k}.html" for k in THEMES))
