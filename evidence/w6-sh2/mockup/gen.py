#!/usr/bin/env python3
"""SH2 #221 Home page mockup: one HTML page per theme/state, screenshotted by shoot.sh."""
import html
import pathlib
import re
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

THEMES = {
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", warning="#8A5A00", danger="#A51F31", disabled="#74808B", hc=False),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", warning="#E0A030", danger="#FFADAD", disabled="#75818D", hc=False),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", warning="#6B4500", danger="#A00000", disabled="#6E6E6E",
                    hc=True),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", warning="#FFD54A", danger="#FFADAD", disabled="#8C8C8C",
                    hc=True),
}


def icon(role, t, size=16, colour=None):
    svg = (ICONS / f"{role}.svg").read_text()
    c = colour or t["text"]
    svg = re.sub(r'<g id="accent">', f'<g id="accent" stroke="{t["accent"]}">', svg)
    svg = svg.replace('stroke="currentColor"', f'stroke="{c}"')
    svg = re.sub(r'width="16" height="16"', f'width="{size}" height="{size}"', svg, count=1)
    return f'<span class="ic">{svg}</span>'


RECENT = [
    # name, folder, opened, missing
    ("Frieren - 27 [1080p].ass", "~/Subs/Frieren S2/work", "Today 23:41", False),
    ("Frieren - 26 [1080p].ass", "~/Subs/Frieren S2/work", "Today 21:07", False),
    ("Frieren - 26 signs.ass", "~/Subs/Frieren S2/typesetting", "Yesterday 18:52", False),
    ("Dandadan OP karaoke.ass", "~/Subs/Dandadan/karaoke", "Yesterday 10:15", False),
    ("Dandadan - 05.srt", "/mnt/nas/fansub/Dandadan/raw-tl", "16 Oct 2026", True),
    ("Blue Box - 12 TL check.ass", "~/Subs/Blue Box", "14 Oct 2026", False),
    ("Blue Box - 11 TL check.ass", "~/Subs/Blue Box", "11 Oct 2026", False),
    ("Kusuriya - 30 [JP-EN].ass", "D:/Fansub/Kusuriya no Hitorigoto/Season 2/Episode 30/final", "8 Oct 2026", False),
    ("old project.ssa", "~/Downloads", "2 Oct 2026", True),
]

WHATS_NEW = """
<h4>HikariSub 0.1.0 &middot; 18 Oct 2026</h4>
<ul>
<li>Document tabs sit at the top of the window.</li>
<li>Files opened from the desktop join the running window as tabs.</li>
<li>The Line editor follows the selected Line's Style preview.</li>
<li>Fixed: audio kept playing on the old device after unplugging headphones.</li>
</ul>
<h4>HikariSub 0.0.9 &middot; 4 Oct 2026</h4>
<ul>
<li>High contrast themes with pickable text and accent colours.</li>
<li>Shift times keeps its last offset per Document.</li>
</ul>
"""


def css(t):
    return f"""
* {{ box-sizing: border-box; }}
html, body {{ margin: 0; background: #6b7480; }}
body {{ font-family: "Noto Sans", "Segoe UI", sans-serif; font-size: 13px; color: {t['text']}; }}
.win {{ width: var(--w); height: var(--h); display: flex; flex-direction: column; background: {t['bg']};
        overflow: hidden; position: relative; }}
.menubar {{ height: 26px; display: flex; align-items: center; padding: 0 4px; background: {t['panel']};
            border-bottom: 1px solid {t['line']}; }}
.menubar span {{ padding: 3px 8px; }}
.main {{ flex: 1; overflow: hidden; display: flex; justify-content: center; }}
.page {{ width: 100%; max-width: 1120px; padding: 24px; display: flex; flex-direction: column; gap: 16px; }}
.actions {{ display: flex; gap: 8px; align-items: center; }}
.btn {{ display: inline-flex; align-items: center; gap: 6px; height: 30px; padding: 0 12px;
        border: 1px solid {t['control']}; border-radius: 1px; background: {t['raised']}; color: {t['text']}; }}
.btn.primary {{ background: {t['accent']}; color: {t['onaccent']}; border-color: {t['accent']}; }}
.btn.small {{ height: 24px; padding: 0 8px; font-size: 12px; }}
.btn .kbd {{ color: {t['muted']}; font-size: 11px; margin-left: 4px; }}
.btn.primary .kbd {{ color: {t['onaccent']}; opacity: .8; }}
.cols {{ flex: 1; display: flex; gap: 16px; min-height: 0; }}
.cols.stack {{ flex-direction: column; }}
.left {{ flex: 1; display: flex; flex-direction: column; gap: 16px; min-width: 0; min-height: 0; }}
.box {{ background: {t['panel']}; border: 1px solid {t['line']}; border-radius: 1px; }}
.session {{ display: flex; align-items: center; gap: 12px; padding: 10px 12px; }}
.session .what {{ flex: 1; min-width: 0; }}
.session .t {{ font-weight: 600; }}
.session .d {{ color: {t['muted']}; font-size: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
.recent {{ flex: 1; display: flex; flex-direction: column; min-height: 0; }}
.rhead {{ min-height: 42px; display: flex; align-items: center; gap: 12px; padding: 8px 12px; border-bottom: 1px solid {t['line']}; }}
.rhead .h {{ font-weight: 600; flex: 1; }}
.search {{ width: 220px; height: 26px; display: flex; align-items: center; gap: 6px; padding: 0 8px;
           background: {t['field']}; border: 1px solid {t['control']}; border-radius: 1px; color: {t['muted']}; }}
.colhead {{ display: flex; padding: 4px 12px 4px 15px; font-size: 11px; color: {t['muted']};
            border-bottom: 1px solid {t['line']}; }}
.colhead .n {{ flex: 1; }}
.colhead .o {{ width: 150px; text-align: right; }}
.rows {{ flex: 1; overflow: hidden; position: relative; }}
.row {{ display: flex; align-items: center; gap: 12px; padding: 5px 12px 5px 15px; position: relative;
        border-bottom: 1px solid {t['line'] if t['hc'] else t['raised']}; }}
.row .ft {{ flex: 1; min-width: 0; }}
.row .nm {{ font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
.row .pt {{ color: {t['muted']}; font-size: 11px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
.row .when {{ width: 150px; text-align: right; color: {t['muted']}; font-size: 12px;
              font-variant-numeric: tabular-nums; white-space: nowrap; }}
.row.hover {{ background: {t['raised']}; }}
.row.sel {{ background: {t['select']}; }}
.row.sel::before {{ content: ""; position: absolute; left: 0; top: 0; bottom: 0; width: 3px; background: {t['accent']}; }}
.row.focus {{ outline: 1px solid {t['text']}; outline-offset: -1px; }}
.row.missing .nm {{ color: {t['muted']}; font-weight: 400; }}
.badge {{ display: inline-flex; align-items: center; gap: 4px; font-size: 11px; color: {t['warning']};
          border: 1px solid {t['warning']}; border-radius: 3px; padding: 0 5px; margin-left: 8px; font-weight: 400;
          vertical-align: 1px; }}
.hoverbtns {{ display: flex; gap: 2px; }}
.ibtn {{ width: 24px; height: 24px; display: inline-flex; align-items: center; justify-content: center;
         border-radius: 1px; }}
.ibtn.h {{ background: {t['bg']}; }}
.news {{ width: 320px; display: flex; flex-direction: column; min-height: 0; }}
.cols.stack .news {{ width: auto; flex: 0 0 auto; max-height: 190px; }}
.news .rhead .h {{ flex: 1; }}
.news .body {{ padding: 4px 16px 12px; overflow: hidden; flex: 1; line-height: 1.45; }}
.news h4 {{ margin: 10px 0 4px; font-size: 13px; }}
.news ul {{ margin: 0; padding-left: 18px; }}
.news li {{ margin: 2px 0; }}
.news .foot {{ padding: 8px 16px; border-top: 1px solid {t['line']}; }}
.link {{ color: {t['accent']}; text-decoration: underline; }}
.slot {{ border-style: dashed; }}
.empty {{ color: {t['muted']}; padding: 16px 12px; }}
.status {{ height: 24px; background: {t['panel']}; border-top: 1px solid {t['line']}; display: flex;
           align-items: center; padding: 0 12px; font-size: 11px; color: {t['muted']}; }}
.ic {{ display: inline-flex; }}
.menu {{ position: absolute; background: {t['panel']}; border: 1px solid {t['line']}; border-radius: 5px;
         box-shadow: 0 10px 30px rgba(0,0,0,.33); padding: 4px 0; min-width: 210px; z-index: 5; }}
.menu .mi {{ display: flex; align-items: center; gap: 8px; padding: 5px 12px; }}
.menu .mi .k {{ margin-left: auto; color: {t['muted']}; font-size: 11px; padding-left: 24px; }}
.menu .mi.on {{ background: {t['select']}; }}
.menu .mi.off {{ color: {t['disabled']}; }}
.menu hr {{ border: 0; border-top: 1px solid {t['line']}; margin: 4px 0; }}
.tip {{ position: absolute; background: {t['raised']}; border: 1px solid {t['line']}; padding: 3px 8px;
        font-size: 12px; z-index: 6; }}
.note {{ position: absolute; right: 8px; bottom: 30px; font-size: 11px; color: {t['muted']}; opacity: .7; }}
"""


def page(theme, w=1280, h=800, state="normal"):
    t = THEMES[theme]
    narrow = w < 1000
    rows = RECENT
    first_run = state == "first-run"
    out = []
    for i, (name, folder, when, missing) in enumerate([] if first_run else rows):
        cls = ["row"]
        if state == "normal" and i == 1:
            cls += ["sel", "focus"]
        if state == "normal" and i == 3:
            cls.append("hover")
        if state == "menu" and i == 4:
            cls += ["sel"]
        if missing:
            cls.append("missing")
        sep = "\\" if folder.startswith("D:") else "/"
        path = folder.replace("/", sep) + sep + name
        nm = html.escape(name)
        if missing:
            nm += f'<span class="badge">{icon("remove", t, 10, t["warning"]) if False else ""}Missing</span>'
        right = f'<div class="when">{when}</div>'
        if missing:
            right = f'<div class="hoverbtns"><span class="btn small">Remove</span></div>' + right
        elif "hover" in cls:
            right = (f'<div class="hoverbtns"><span class="ibtn h">{icon("show-in-folder", t)}</span></div>' + right)
        out.append(f'<div class="{" ".join(cls)}"><div class="ft"><div class="nm">{nm}</div>'
                   f'<div class="pt">{html.escape(path)}</div></div>{right}</div>')
    if first_run:
        out.append('<div class="empty">No recent files</div>')

    session = ""
    scroll = f'<div style="position:absolute;right:2px;top:2px;width:6px;height:40%;background:{t["control"]};border-radius:3px;opacity:.7"></div>' if narrow else ''
    if not first_run:
        session = f"""
<div class="box session">
  {icon("last-session", t, 20)}
  <div class="what"><div class="t">Last session</div>
  <div class="d">3 documents &middot; Frieren - 27 [1080p].ass, Frieren - 26 [1080p].ass, Frieren - 26 signs.ass</div></div>
  <span class="btn">Restore</span>
</div>"""

    news = f"""
<div class="box news">
  <div class="rhead"><span class="h">What's new</span></div>
  <div class="body">{WHATS_NEW}</div>
  <div class="foot"><span class="link">All release notes</span></div>
</div>"""

    extras = ""
    if state == "menu":
        extras = f"""
<div class="menu" style="left:420px; top:388px">
  <div class="mi off">{icon("open-subtitles", {**t, 'text': t['disabled'], 'accent': t['disabled']})} Open<span class="k">Enter</span></div>
  <div class="mi">{icon("show-in-folder", t)} Show in folder</div>
  <div class="mi">{icon("edit-copy", t)} Copy path</div>
  <hr>
  <div class="mi on">{icon("remove", t)} Remove from list<span class="k">Del</span></div>
  <div class="mi">{icon("clear", t)} Remove missing files</div>
</div>"""
    if state == "normal" and theme == "light":
        extras = f'<div class="tip" style="left:{w-627 - (0 if not narrow else -336)}px; top:{412 if not narrow else 0}px">Show in folder</div>' if not narrow else ''

    return f"""<!doctype html><html><head><meta charset="utf-8"><style>:root{{--w:{w}px;--h:{h}px}}{css(t)}</style></head>
<body><div class="win">
<div class="menubar"><span>File</span><span>Edit</span><span>Automation</span><span>Video</span><span>Audio</span><span>View</span><span>Subtitles</span><span>Help</span></div>
<div class="main"><div class="page">
  <div class="actions">
    <span class="btn primary">{icon("tab-new", {**t, 'text': t['onaccent']})} New</span>
    <span class="btn">{icon("open-subtitles", t)} Open…</span>
    <span class="btn">{icon("open-video", t)} Open video…</span>
  </div>
  <div class="cols{' stack' if narrow else ''}">
    <div class="left">
      {session}
      <div class="box recent">
        <div class="rhead"><span class="h">Recent files</span>
          {'' if first_run else f'<span class="search">{icon("search", {**t, "text": t["muted"]}, 14)} Filter</span>'}</div>
        {'' if first_run else '<div class="colhead"><span class="n">Name</span><span class="o">Last opened</span></div>'}
        <div class="rows">{''.join(out)}{scroll}</div>
      </div>
    </div>
    {news}
  </div>
</div></div>
<div class="status"></div>
{extras}
</div></body></html>"""


for theme in THEMES:
    (OUT / f"home-{theme}.html").write_text(page(theme))
(OUT / "home-dark-menu.html").write_text(page("dark", state="menu"))
(OUT / "home-light-narrow.html").write_text(page("light", 900, 680))
(OUT / "home-dark-first-run.html").write_text(page("dark", state="first-run"))
print("ok")
