#!/usr/bin/env python3
"""SH1 #220 custom title bar mockup: per theme, a full window and a sheet of states.

The window body under the title bar is a real screenshot of today's HikariSub
(w6-ws3's build, qt 1989b6636's parent) in that theme, taken by body.sh under
vx11; the title bar above it is drawn here from the theme layer's roles
(src/ui/theme.cpp baseRoles, default accent) and IC1's icon style. The layout
menu is the real WS3 menu, cropped from the same run."""
import base64
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-sh1")
OUT = pathlib.Path(__file__).resolve().parent

THEMES = {
    "light": dict(code="light", name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", press="#DDE2E7"),
    "dark": dict(code="dark", name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", press="#343E49"),
    "hcwhite": dict(code="highContrastWhite", name="High contrast white", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    press="#C9DAF8"),
    "hcblack": dict(code="highContrastBlack", name="High contrast black", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    press="#253F60"),
}
HC = {"hcwhite", "hcblack"}

# Today's menu bar (Main.qml), mnemonic letter index per title.
MENUS = [("File", 0), ("Edit", 0), ("Automation", 0), ("Video", 0), ("Audio", 1), ("View", 3), ("Subtitles", 0),
         ("Help", 0)]
TITLE = "Episode 03.ass - HikariSub"
LAYOUT = "Editing"

# New glyphs in IC1's 16-unit, 1-wide stroke style (would join src/ui/icons).
GLYPHS = {
    "window-minimize": '<path d="M3.5 8.5h9"/>',
    "window-maximize": '<rect x="3.5" y="3.5" width="9" height="9" rx=".5"/>',
    "window-restore": '<rect x="3.5" y="5.5" width="7" height="7" rx=".5"/><path d="M5.5 5.5v-2h7v7h-2"/>',
    "window-close": '<path d="M3.5 3.5l9 9M12.5 3.5l-9 9"/>',
}


def icon_svg(name):
    svg = (TREE / "src/ui/icons" / f"{name}.svg").read_text()
    inner = re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip()).replace('<g id="accent">', '<g class="acc">')
    return inner


def sprite():
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    syms = {**GLYPHS, "layout": icon_svg("layout"), "menu-more": icon_svg("menu-more")}
    body = "".join(f'<symbol id="i-{n}" viewBox="0 0 16 16"><g {attrs}>{s}</g></symbol>' for n, s in syms.items())
    return f'<svg width="0" height="0" style="position:absolute"><defs>{body}</defs></svg>'


def ic(name, size=16):
    return f'<svg class="ic" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


APP_ICON = "data:image/png;base64," + base64.b64encode(
    (TREE / "packaging/linux/icons/hicolor/32x32/apps/hikarisub.png").read_bytes()).decode()


def menus(alt=False, focus=None, open_=None, fold_from=None):
    out = []
    for i, (t, m) in enumerate(MENUS):
        if fold_from is not None and i >= fold_from:
            break
        label = t[:m] + f'<u>{t[m]}</u>' + t[m + 1:] if alt else t
        cls = "m" + (" ring" if focus == i else "") + (" open" if open_ == i else "")
        out.append(f'<span class="{cls}">{label}</span>')
    if fold_from is not None:
        out.append(f'<span class="m more" title="More menus">{ic("menu-more", 14)}</span>')
    return "".join(out)


def switcher(state=""):
    return (f'<span class="sw {state}">{ic("layout")}<span class="swl">{LAYOUT}</span>'
            f'{ic("menu-more", 12)}</span>')


def buttons(maxed=False, hover=None, press=None, red=False):
    out = []
    for key, glyph, name in (("min", "window-minimize", "Minimise"),
                             ("max", "window-restore" if maxed else "window-maximize", "Restore" if maxed else "Maximise"),
                             ("close", "window-close", "Close")):
        cls = "wb " + key + (" hover" if hover == key else "") + (" press" if press == key else "") + (
            " red" if red and key == "close" else "")
        out.append(f'<span class="{cls}" title="{name}">{ic(glyph)}</span>')
    return f'<span class="wbs">{"".join(out)}</span>'


def titlebar(width=1280, title=TITLE, inactive=False, alt=False, focus=None, sw="", maxed=False, hover=None,
             press=None, red=False, fold_from=None, show_title=True, system_row=False):
    cls = "tb" + (" inactive" if inactive else "") + (" sysrow" if system_row else "")
    icon = "" if system_row else f'<span class="appicon" title="HikariSub"><img src="{APP_ICON}" width="16" height="16"></span>'
    t = f'<span class="title">{title}</span>' if show_title and not system_row else ""
    wb = "" if system_row else buttons(maxed, hover, press, red)
    return (f'<div class="{cls}" style="width:{width}px">{icon}<span class="lead">{menus(alt, focus, None, fold_from)}'
            f'{switcher(sw)}</span>{t}{wb}</div>')


CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(page)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
.win{position:relative;width:1280px;box-shadow:0 8px 28px rgba(0,0,0,.35);outline:1px solid %(line)s}
.body{position:relative;height:774px;background:url(%(body)s) 0 -26px no-repeat}
.body .cover{position:absolute;left:1170px;top:741px;width:110px;height:33px;background:%(panel)s}
.tb{position:relative;display:flex;align-items:center;height:32px;background:%(panel)s;overflow:hidden;white-space:nowrap;%(tbline)s}
.appicon{flex:none;display:flex;align-items:center;justify-content:center;width:32px;height:32px;margin-left:2px}
.lead{display:flex;align-items:center;flex:none;gap:0;z-index:1}
.m{display:flex;align-items:center;height:24px;padding:0 8px;border-radius:1px;color:%(text)s}
.m u{text-decoration-thickness:1px;text-underline-offset:2px}
.m.ring{outline:1px solid %(text)s;outline-offset:-1px;background:%(raised)s}
.m.more{padding:0 5px}
.sw{display:flex;align-items:center;gap:4px;height:24px;margin-left:8px;padding:0 4px 0 6px;border:1px solid %(line)s;
    border-radius:1px;background:%(raised)s;color:%(text)s}
.sw.open{background:%(select)s;border-color:%(accent)s}
.sw.hover{border-color:%(accent)s;background:%(select)s}
.swl{max-width:200px;overflow:hidden;text-overflow:ellipsis}
.title{position:absolute;top:0;height:32px;line-height:32px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
       color:%(text)s;text-align:center}
.wbs{display:flex;margin-left:auto;flex:none;z-index:1}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.wb.hover{background:%(raised)s;%(hcring)s}
.wb.press{background:%(press)s;%(hcring)s}
.wb.red.hover{background:#C42B1C;color:#FFFFFF;outline:none}
.tb.inactive .m,.tb.inactive .title,.tb.inactive .wb,.tb.inactive .swl{color:%(disabled)s}
.tb.inactive .appicon img{opacity:.55}
.tb.sysrow{height:27px}
.tb.sysrow .lead{margin-left:2px}
.sysframe{height:28px;display:flex;align-items:center;justify-content:center;font:12px 'Liberation Sans',Arial,sans-serif;
          color:#bbb;background:repeating-linear-gradient(135deg,#3a3f46 0 8px,#353a40 8px 16px);border-bottom:1px solid #222}
.sysframe.border{height:6px;background:#5e81ac;border:0}
h1{font-size:18px;font-weight:600;margin:0 0 4px}
h2{font-size:13px;font-weight:650;margin:22px 0 6px;color:%(text)s}
p.note{color:%(muted)s;font-size:12px;margin:0 0 8px;max-width:1280px;line-height:1.45}
.strip{position:relative;width:1280px;outline:1px solid %(line)s}
.under{height:34px;background:url(%(body)s) 0 -26px no-repeat}
.menuwrap{position:relative;height:240px;background:url(%(body)s) 0 -26px no-repeat}
.menuimg{position:absolute;box-shadow:0 10px 30px rgba(0,0,0,.33)}
.pair{display:flex;gap:24px}
.mini{position:relative;width:628px;outline:1px solid %(line)s;overflow:hidden}
.mini .crop{margin-left:-652px;width:1280px}
.cap{font-size:11px;color:%(muted)s;margin:4px 0 0}
.check{display:flex;align-items:center;gap:8px;padding:10px 12px;background:%(panel)s;width:628px;outline:1px solid %(line)s}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px}
.hint{color:%(muted)s;font-size:12px;padding:0 12px 10px 36px;background:%(panel)s;width:628px}
"""

JS = """
<script>
// The title centres on the window when it fits between the menus and the
// buttons; otherwise it centres in the free space and elides; under 80 px it hides.
for (const tb of document.querySelectorAll('.tb')) {
  const t = tb.querySelector('.title'); if (!t) continue;
  const W = tb.clientWidth, L = tb.querySelector('.lead').getBoundingClientRect().right - tb.getBoundingClientRect().left + 32;
  const wb = tb.querySelector('.wbs'); const R = W - (wb ? wb.offsetWidth : 0) - 32;
  t.style.width = 'auto'; const need = t.scrollWidth;
  if (W/2 - need/2 >= L && W/2 + need/2 <= R) { t.style.left = (W/2 - need/2) + 'px'; t.style.width = need + 'px'; }
  else if (R - L >= 80) { const w = Math.min(need, R - L); t.style.left = (L + (R - L - w)/2) + 'px'; t.style.width = w + 'px'; }
  else t.style.display = 'none';
}
</script>"""


def page(key, content, page_bg=None):
    th = THEMES[key]
    css = CSS % dict(th, page=page_bg or th["bg"], body=f"shots/body-{th['code']}.png",
                     hcring=f"outline:1px solid {th['line']};outline-offset:-1px" if key in HC else "",
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}{JS}</body></html>')


def window(key, **kw):
    return (f'<div class="win">{titlebar(**kw)}<div class="body"><div class="cover"></div></div></div>')


def sheet(key):
    th = THEMES[key]
    menu_png = f"shots/layoutmenu-{th['code']}.png"
    s = [f'<h1>SH1 title bar: states ({th["name"]})</h1>']

    s.append('<h2>Pointer: hover on Minimise, Maximise pressed</h2>')
    s.append(f'<div class="strip">{titlebar(hover="min")}<div class="under"></div></div>')
    s.append(f'<div class="strip" style="margin-top:8px">{titlebar(press="max")}<div class="under"></div></div>')

    s.append('<h2>Close on hover: the theme’s hover (recommended, left) or Windows’ red (right)</h2>')
    s.append('<div class="pair">'
             f'<div class="mini"><div class="crop">{titlebar(hover="close")}</div></div>'
             f'<div class="mini"><div class="crop">{titlebar(hover="close", red=True)}</div></div></div>')

    s.append('<h2>Keyboard: Alt or F10 shows the mnemonics and rings File; Left/Right walk the menus and the layout '
             'switcher</h2>')
    s.append(f'<div class="strip">{titlebar(alt=True, focus=0)}<div class="under"></div></div>')

    s.append('<h2>Layout switcher open (WS3’s menu, unchanged, now opening downwards)</h2>')
    s.append(f'<div class="strip">{titlebar(sw="open")}<div class="menuwrap">'
             f'<img class="menuimg" src="{menu_png}" style="left:%SWX%px;top:0"></div></div>')

    s.append('<h2>Maximised (Restore glyph) and window not active (text and glyphs in the disabled role)</h2>')
    s.append(f'<div class="strip">{titlebar(maxed=True)}<div class="under"></div></div>')
    s.append(f'<div class="strip" style="margin-top:8px">{titlebar(inactive=True)}<div class="under"></div></div>')

    s.append('<h2>Narrow windows: the title elides first (1000), then hides (780); below that the last menus fold '
             'into ⋯ (660)</h2>')
    long_title = "Episode 03 - The last train home (v2 final).ass - HikariSub"
    s.append(f'<div class="strip" style="width:1000px">{titlebar(width=1000, title=long_title)}</div>')
    s.append(f'<div class="strip" style="width:780px;margin-top:8px">{titlebar(width=780)}</div>')
    s.append(f'<div class="strip" style="width:660px;margin-top:8px">{titlebar(width=660, fold_from=6)}</div>')

    s.append('<h2>System title bar (sway, Hyprland, niri, river, i3 by default; anywhere with “Use system title bar” '
             'on): the frame is the system’s, menus and switcher stay in the window’s first row</h2>')
    s.append('<div class="pair"><div><div class="mini"><div class="sysframe">system title bar (sway draws it)</div>'
             f'{titlebar(width=628, system_row=True)}</div><p class="cap">sway: its own title bar above</p></div>'
             '<div><div class="mini"><div class="sysframe border"></div>'
             f'{titlebar(width=628, system_row=True)}</div><p class="cap">Hyprland: a border only</p></div></div>')

    s.append('<h2>Settings › Appearance › Window</h2>')
    s.append('<div class="check"><span class="box"></span>Use system title bar</div>'
             '<div class="hint">Off by default on Windows, KDE Plasma and GNOME; on by default on tiling window '
             'managers. Applies at once.</div>')
    return "".join(s)


def main():
    for key, th in THEMES.items():
        (OUT / f"sh1-{key}.html").write_text(page(key, window(key)))
        # The switcher's left edge: icon (34) + menu titles; measured in the page by JS below.
        html = page(key, sheet(key)).replace(
            "%SWX%px", "0px").replace(
            "</body>", "<script>for(const w of document.querySelectorAll('.menuwrap')){"
                       "const sw=w.previousElementSibling.querySelector('.sw');"
                       "const x=sw.getBoundingClientRect().left-w.getBoundingClientRect().left;"
                       "w.querySelector('.menuimg').style.left=x+'px'}</script></body>")
        (OUT / f"sh1-{key}-states.html").write_text(html)


main()
