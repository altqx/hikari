#!/usr/bin/env python3
"""SH3 #223 What's new mockup: the Home page card (SH2's approved layout, filled) and Help > What's new on
DG1's dialog frame, in the four themes, plus the states. Usage: gen.py <icons dir> <out dir>.

Theme roles are src/ui/theme.cpp's (default accent), as in the SH2 and DG1 mockups; the Home page is drawn
from SH2's approved mockup (evidence/w6-sh2-mockup gen.py), the dialog from DG1's (evidence/w6-dg1-mockup).
Release names, dates and notes are made up."""
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
                  select="#D6EBE4", warning="#8A5A00", disabled="#74808B", hc=False, dark=False),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", warning="#E0A030", disabled="#75818D", hc=False, dark=True),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", warning="#6B4500", disabled="#6E6E6E", hc=True,
                    dark=False),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", warning="#FFD54A", disabled="#8C8C8C", hc=True, dark=True),
}

# A new glyph in IC1's 16-unit, 1-wide stroke style for Help > What's new: a page with a spark.
NEW_ICONS = {
    "whats-new": '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
                 'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">'
                 '<path d="M9.5 2.5h-6v11h9v-6"/><path d="M5.5 9.5h5M5.5 11.5h3"/>'
                 '<g id="accent"><path d="M12.5 1v4M10.5 3h4"/></g></svg>',
}


def icon(role, t, size=16, colour=None):
    svg = NEW_ICONS.get(role) or (ICONS / f"{role}.svg").read_text()
    c = colour or t["text"]
    svg = re.sub(r'<g id="accent">', f'<g id="accent" stroke="{t["accent"]}">', svg)
    svg = svg.replace('stroke="currentColor"', f'stroke="{c}"')
    svg = re.sub(r'width="16" height="16"', f'width="{size}" height="{size}"', svg, count=1)
    return f'<span class="ic">{svg}</span>'


# ---------------------------------------------------------------------------------------------------------
# Release notes: what GitHub's API returns as `body`, already rendered the way SH3 renders Markdown
# (headings become section labels under the version, links in the accent, code in the field role).

LINK = '<a class="lk" title="https://github.com/altqx/hikari/pull/{n}">#{n}</a>'

RELEASES = {
    "0.1.3": dict(date="21 Oct 2026", pre=False, body=f"""
<div class="sec">New</div>
<ul>
<li><b>Translation mode:</b> the original text stays beside the translation, with its own tags.</li>
<li>Karaoke check steps through the syllables against the audio ({LINK.format(n=287)}).</li>
<li>Help › What's new shows these notes.</li>
</ul>
<div class="sec">Fixed</div>
<ul>
<li>The video no longer jumps a frame when a Line is selected during playback.</li>
<li><code>Shift times</code> keeps the selection after it runs.</li>
</ul>"""),
    "0.1.2": dict(date="9 Oct 2026", pre=False, body="""
<div class="sec">Fixed</div>
<ul>
<li>The audio display redraws after a theme change.</li>
<li>Opening an <code>.srt</code> with a byte order mark no longer adds an empty first Line.</li>
<li>Dark theme: the Grid's comment rows are readable again.</li>
</ul>"""),
    "0.1.3-beta.2": dict(date="17 Oct 2026", pre=True, body="""
<ul>
<li>Translation mode's preset opens the original beside the translation.</li>
<li>Known issue: the karaoke check may skip the last syllable.</li>
</ul>"""),
    "0.1.0": dict(date="1 Oct 2026", pre=False, body=f"""
<p>The first release of the Qt rewrite. Read the <a class="lk" title="https://github.com/altqx/hikari/blob/qt/docs/qt/migration.md">migration notes</a> before replacing HikariSub 0.0.x.</p>
<div class="sec">Highlights</div>
<ul>
<li>Document tabs, each with its own panel layout.</li>
<li>Panels dock, float and tab; layouts are saved per tab.</li>
</ul>"""),
}


def version_block(v, t):
    r = RELEASES[v]
    badge = '<span class="pre">Pre-release</span>' if r["pre"] else ""
    return (f'<div class="ver"><div class="vh"><span class="vn">HikariSub {v}</span>{badge}'
            f'<span class="vd">{r["date"]}</span></div><div class="md">{r["body"]}</div></div>')


def notes(versions, t, more=0):
    s = "".join(version_block(v, t) for v in versions)
    if more:
        s += f'<p class="more">And {more} earlier versions: <a class="lk">all release notes</a></p>'
    return s


# ---------------------------------------------------------------------------------------------------------
# Shared CSS (SH2's Home roles, DG1's frame)


def css(t):
    hc = t["hc"]
    return f"""
* {{ box-sizing: border-box; }}
html, body {{ margin: 0; background: {t['bg']}; }}
body {{ font-family: "Noto Sans", "Segoe UI", sans-serif; font-size: 13px; color: {t['text']}; }}
.ic {{ display: inline-flex; }}
a.lk {{ color: {t['accent']}; text-decoration: underline; cursor: pointer; }}
.win {{ width: var(--w); height: var(--h); display: flex; flex-direction: column; background: {t['bg']};
        overflow: hidden; position: relative; outline: 1px solid {t['line']}; }}
.titlebar {{ height: 32px; display: flex; align-items: center; background: {t['panel']};
             border-bottom: 1px solid {t['line']}; flex: none; }}
.titlebar .app {{ padding: 0 10px 0 10px; display: flex; }}
.titlebar span.m {{ padding: 3px 8px; }}
.titlebar .wt {{ flex: 1; text-align: center; color: {t['muted']}; }}
.titlebar .wb {{ width: 46px; height: 32px; display: flex; align-items: center; justify-content: center; }}
.main {{ flex: 1; overflow: hidden; display: flex; justify-content: center; }}
.page {{ width: 100%; max-width: 1120px; padding: 24px; display: flex; flex-direction: column; gap: 16px; }}
.actions {{ display: flex; gap: 8px; align-items: center; }}
.btn {{ display: inline-flex; align-items: center; gap: 6px; height: 30px; padding: 0 12px;
        border: 1px solid {t['control']}; border-radius: 1px; background: {t['raised']}; color: {t['text']}; }}
.btn.primary {{ background: {t['accent']}; color: {t['onaccent']}; border-color: {t['accent']}; }}
.btn.small {{ height: 24px; padding: 0 8px; font-size: 12px; }}
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
.rhead .since {{ color: {t['muted']}; font-size: 12px; }}
.search {{ width: 220px; height: 26px; display: flex; align-items: center; gap: 6px; padding: 0 8px;
           background: {t['field']}; border: 1px solid {t['control']}; border-radius: 1px; color: {t['muted']}; }}
.colhead {{ display: flex; padding: 4px 12px 4px 15px; font-size: 11px; color: {t['muted']};
            border-bottom: 1px solid {t['line']}; }}
.colhead .n {{ flex: 1; }}
.rows {{ flex: 1; overflow: hidden; position: relative; }}
.row {{ display: flex; align-items: center; gap: 12px; padding: 5px 12px 5px 15px; position: relative;
        border-bottom: 1px solid {t['line'] if hc else t['raised']}; }}
.row .ft {{ flex: 1; min-width: 0; }}
.row .nm {{ font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
.row .pt {{ color: {t['muted']}; font-size: 11px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
.row .when {{ width: 130px; text-align: right; color: {t['muted']}; font-size: 12px; white-space: nowrap; }}
.row.sel {{ background: {t['select']}; }}
.row.sel::before {{ content: ""; position: absolute; left: 0; top: 0; bottom: 0; width: 3px; background: {t['accent']}; }}
.row.missing .nm {{ color: {t['muted']}; font-weight: 400; }}
.badge {{ font-size: 11px; color: {t['warning']}; border: 1px solid {t['warning']}; border-radius: 3px;
          padding: 0 5px; margin-left: 8px; font-weight: 400; }}
.status {{ height: 24px; background: {t['panel']}; border-top: 1px solid {t['line']}; flex: none; }}

/* What's new: the card */
.news {{ width: 320px; display: flex; flex-direction: column; min-height: 0; position: relative; }}
.cols.stack .news {{ width: auto; flex: 0 0 auto; height: 200px; }}
.news .body {{ padding: 0 16px 12px; overflow: hidden; flex: 1; position: relative; }}
.news .foot {{ padding: 8px 16px; border-top: 1px solid {t['line']}; display: flex; gap: 6px; align-items: center; }}
.sbar {{ position: absolute; right: 3px; width: 4px; border-radius: 2px; background: {t['control']}; opacity: .8; }}

/* Release notes, wherever they show */
.ver {{ padding-top: 12px; }}
.ver + .ver {{ margin-top: 10px; border-top: 1px solid {t['line'] if hc else t['raised']}; }}
.vh {{ display: flex; align-items: baseline; gap: 4px 8px; flex-wrap: wrap; }}
.vn {{ font-weight: 650; white-space: nowrap; }}
.vd {{ margin-left: auto; color: {t['muted']}; font-size: 12px; white-space: nowrap; }}
.pre {{ font-size: 11px; color: {t['muted']}; border: 1px solid {t['control']}; border-radius: 3px; padding: 0 5px; white-space: nowrap; }}
.md {{ line-height: 1.45; }}
.md p {{ margin: 6px 0 0; }}
.md .sec {{ font-size: 11px; font-weight: 650; color: {t['muted']}; margin: 8px 0 2px; letter-spacing: .02em; }}
.md ul {{ margin: 4px 0 0; padding-left: 18px; }}
.md li {{ margin: 2px 0; }}
.md code {{ font: 12px "Noto Sans Mono", Consolas, monospace; background: {t['raised']}; padding: 0 3px;
           border-radius: 1px; {f"outline: 1px solid {t['line']};" if hc else ""} }}
.more {{ color: {t['muted']}; margin: 14px 0 0; }}
.tip {{ position: absolute; background: {t['raised']}; border: 1px solid {t['line']}; padding: 3px 8px;
        font-size: 12px; z-index: 6; white-space: nowrap; }}

/* DG1's dialog frame */
.dlg {{ background: {t['panel']}; color: {t['text']}; display: flex; flex-direction: column;
        outline: 1px solid {t['line']}; box-shadow: 0 8px 28px rgba(0,0,0,{'.55' if t['dark'] else '.28'}); }}
.on-app {{ position: absolute; }}
.dtb {{ display: flex; align-items: center; height: 32px; flex: none; {f"box-shadow: inset 0 -1px {t['line']};" if hc else ""} }}
.dtitle {{ flex: 1; padding-left: 12px; }}
.dtb .wb {{ width: 46px; height: 32px; display: flex; align-items: center; justify-content: center; }}
.dbody {{ padding: 12px 24px 0; display: flex; flex-direction: column; flex: 1; min-height: 0; }}
.dfoot {{ display: flex; gap: 8px; justify-content: flex-end; padding: 24px; flex: none; }}
.dfoot .lead {{ margin-right: auto; }}
.b {{ display: inline-flex; align-items: center; justify-content: center; gap: 6px; min-width: 80px; height: 30px;
      padding: 0 10px; border-radius: 1px; background: {t['raised']}; border: 1px solid {t['line']}; color: {t['text']};
      white-space: nowrap; }}
.b.pri {{ background: {t['accent']}; border-color: {t['accent']}; color: {t['onaccent']}; font-weight: 650; }}
.ring {{ outline: 1px solid {t['text']}; outline-offset: 1px; }}
.intro {{ color: {t['muted']}; margin: 4px 0 12px; }}
.dnotes {{ position: relative; flex: 1; min-height: 0; background: {t['field']}; outline: 1px solid {t['line']};
           overflow: hidden; padding: 0 20px 12px 16px; }}
.empty {{ flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 10px;
          text-align: center; color: {t['muted']}; background: {t['field']}; outline: 1px solid {t['line']}; padding: 24px; }}
.empty .t {{ color: {t['text']}; font-weight: 600; }}
.pbar {{ width: 200px; height: 6px; background: {t['raised']}; outline: 1px solid {t['line']}; overflow: hidden; }}
.pbar span {{ display: block; width: 35%; height: 100%; margin-left: 30%; background: {t['accent']}; }}

/* Menu */
.menu {{ position: absolute; background: {t['panel']}; border: 1px solid {t['line']}; border-radius: 5px;
         box-shadow: 0 10px 30px rgba(0,0,0,.33); padding: 4px 0; min-width: 230px; z-index: 5; }}
.menu .mi {{ display: flex; align-items: center; gap: 8px; padding: 5px 12px; }}
.menu .mi.on {{ background: {t['select']}; }}
.menu .mi .k {{ margin-left: auto; color: {t['muted']}; font-size: 11px; padding-left: 24px; }}
.menu hr {{ border: 0; border-top: 1px solid {t['line']}; margin: 4px 0; }}

/* Sheets */
.sheet {{ padding: 24px; width: max-content; }}
.sheet h1 {{ font-size: 18px; font-weight: 600; margin: 0 0 4px; }}
.sheet h2 {{ font-size: 13px; font-weight: 650; margin: 26px 0 8px; }}
.sheet .note {{ color: {t['muted']}; font-size: 12px; margin: 0 0 12px; max-width: 1280px; line-height: 1.45; }}
.cap {{ font-size: 11px; color: {t['muted']}; margin: 6px 0 0; max-width: 340px; line-height: 1.4; }}
.srow {{ display: flex; gap: 28px; align-items: flex-start; }}
.scol {{ display: flex; flex-direction: column; }}
"""


RECENT = [
    ("Frieren - 27 [1080p].ass", "~/Subs/Frieren S2/work", "Today 23:41", False),
    ("Frieren - 26 [1080p].ass", "~/Subs/Frieren S2/work", "Today 21:07", False),
    ("Frieren - 26 signs.ass", "~/Subs/Frieren S2/typesetting", "Yesterday 18:52", False),
    ("Dandadan OP karaoke.ass", "~/Subs/Dandadan/karaoke", "Yesterday 10:15", False),
    ("Dandadan - 05.srt", "/mnt/nas/fansub/Dandadan/raw-tl", "16 Oct 2026", True),
    ("Blue Box - 12 TL check.ass", "~/Subs/Blue Box", "14 Oct 2026", False),
    ("Blue Box - 11 TL check.ass", "~/Subs/Blue Box", "11 Oct 2026", False),
    ("Kusuriya - 30 [JP-EN].ass", "~/Fansub/Kusuriya no Hitorigoto/Season 2/Episode 30/final", "8 Oct 2026", False),
    ("old project.ssa", "~/Downloads", "2 Oct 2026", True),
]


def card(t, versions=("0.1.3", "0.1.2"), since="0.1.1", more=0, tip=False, scroll=True, at_end=False):
    since_html = f'<span class="since">Since {since}</span>' if since else ""
    bar = ('<span class="sbar" style="bottom:6px;height:22%"></span>' if at_end else
           '<span class="sbar" style="top:6px;height:46%"></span>') if scroll else ""
    tip_html = ('<div class="tip" style="left:120px;top:205px">https://github.com/altqx/hikari/pull/287</div>'
                if tip else "")
    return f"""
<div class="box news" id="news">
  <div class="rhead"><span class="h">What's new</span>{since_html}</div>
  <div class="body">{notes(versions, t, more)}{bar}</div>
  <div class="foot">{icon("link", {**t, 'text': t['accent']}, 14)}<a class="lk">All release notes</a></div>
  {tip_html}
</div>"""


def titlebar(t):
    menus = "".join(f'<span class="m">{m}</span>' for m in
                    ("File", "Edit", "Automation", "Video", "Audio", "View", "Subtitles", "Help"))
    return (f'<div class="titlebar"><span class="app">{icon("about", t)}</span>{menus}<span class="wt">HikariSub</span>'
            f'<span class="wb">—</span><span class="wb">{icon("tab-new", t, 12)}</span><span class="wb">✕</span></div>')


def home(t, w=1280, h=800, news=None, extras=""):
    narrow = w < 1000
    rows = []
    for i, (name, folder, when, missing) in enumerate(RECENT):
        cls = ["row"] + (["sel"] if i == 1 else []) + (["missing"] if missing else [])
        nm = html.escape(name) + ('<span class="badge">Missing</span>' if missing else "")
        rows.append(f'<div class="{" ".join(cls)}"><div class="ft"><div class="nm">{nm}</div>'
                    f'<div class="pt">{html.escape(folder + "/" + name)}</div></div><div class="when">{when}</div></div>')
    news = card(t) if news is None else news
    return f"""
<div class="win" style="--w:{w}px;--h:{h}px">
{titlebar(t)}
<div class="main"><div class="page">
  <div class="actions">
    <span class="btn primary">{icon("tab-new", {**t, 'text': t['onaccent']})} New</span>
    <span class="btn">{icon("open-subtitles", t)} Open…</span>
    <span class="btn">{icon("open-video", t)} Open video…</span>
  </div>
  <div class="cols{' stack' if narrow else ''}">
    <div class="left">
      <div class="box session">{icon("last-session", t, 20)}
        <div class="what"><div class="t">Last session</div>
        <div class="d">3 documents · Frieren - 27 [1080p].ass, Frieren - 26 [1080p].ass, Frieren - 26 signs.ass</div></div>
        <span class="btn">Restore</span></div>
      <div class="box recent">
        <div class="rhead"><span class="h">Recent files</span>
          <span class="search">{icon("search", {**t, "text": t["muted"]}, 14)} Filter</span></div>
        <div class="colhead"><span class="n">Name</span><span>Last opened</span></div>
        <div class="rows">{''.join(rows)}</div>
      </div>
    </div>
    {news}
  </div>
</div></div>
<div class="status"></div>
{extras}
</div>"""


def dialog(t, body, foot, w=620, h=560, style=""):
    return (f'<div class="dlg" style="width:{w}px;height:{h}px;{style}">'
            f'<div class="dtb"><span class="dtitle">What\'s new</span>'
            f'<span class="wb">{icon("tab-new", t, 12) if False else "▢"}</span><span class="wb">✕</span></div>'
            f'<div class="dbody">{body}</div><div class="dfoot">{foot}</div></div>')


def foot(t, close_primary=True):
    return (f'<span class="b lead">{icon("link", t, 14)}All release notes</span>'
            f'<span class="b{" pri ring" if close_primary else ""}">Close</span>')


def whats_new_dialog(t, style=""):
    body = (f'<p class="intro">Changes since version 0.1.1, the one you used before.</p>'
            f'<div class="dnotes">{notes(("0.1.3", "0.1.2"), t)}'
            f'<span class="sbar" style="top:6px;height:70%"></span></div>')
    return dialog(t, body, foot(t), style=style)


def empty_dialog(t, kind):
    if kind == "off":
        inner = (f'{icon("whats-new", t, 32)}<div class="t">No release notes yet</div>'
                 '<div>HikariSub fetches the release notes only while<br>“Check for updates automatically” is on.</div>')
    elif kind == "fetching":
        inner = (f'<div class="t">Fetching the release notes…</div><div class="pbar"><span></span></div>')
    else:
        inner = (f'{icon("notice-warning", {**t, "accent": t["warning"]}, 32, t["warning"])}'
                 '<div class="t">Cannot fetch the release notes</div>'
                 '<div>HikariSub tries again at the next start.</div>')
    return dialog(t, f'<div class="empty">{inner}</div>', foot(t), w=440, h=340)


def doc(t, content):
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css(t)}</style></head>'
            f'<body>{content}</body></html>')


def help_menu(t):
    items = [("help", "HikariSub website"), ("report-issue", "Report an issue"), ("check-updates", "Check for updates"),
             ("whats-new", "What's new"), ("about", "About"), ("credits", "Credits")]
    s = "".join(f'<div class="mi{" on" if n == "whats-new" else ""}">{icon(n, t)} {label}</div>' for n, label in items)
    return f'<div class="menu" style="left:456px;top:32px">{s}</div>'


for key, t in THEMES.items():
    # 1. Home with the card.
    (OUT / f"sh3-home-{key}.html").write_text(doc(t, home(t, news=card(t, tip=key == "light"))))
    # 2. Help > What's new over the main window (centred on it, 20 lower: DG1's first-open rule).
    (OUT / f"sh3-dialog-{key}.html").write_text(doc(t, home(t, extras=whats_new_dialog(
        t, "position:absolute;left:330px;top:150px"))))
# The Help menu with the new item.
(OUT / "sh3-menu-light.html").write_text(doc(THEMES["light"], home(THEMES["light"], extras=help_menu(THEMES["light"]))))

# 3. States, in Light and Dark.
for key in ("light", "dark"):
    t = THEMES[key]
    s = [f'<div class="sheet"><h1>SH3 states ({t["name"]})</h1>',
         '<p class="note">Card variants on the Home page (320 wide, as SH2 reserved), and the dialog when it has '
         'nothing to show. Versions, dates and notes are made up.</p>',
         '<h2>The card</h2><div class="srow" style="height:560px">',
         f'<div class="scol" style="height:520px">{card(t, ("0.1.3-beta.2", "0.1.2"), "0.1.1", scroll=False)}'
         '<p class="cap">Beta channel (“Stable versions only” off, or hikarisub-beta-bin): pre-releases in the range '
         'show with a Pre-release badge.</p></div>',
         f'<div class="scol" style="height:520px">{card(t, ("0.1.0",), None, scroll=False)}'
         '<p class="cap">First start (no earlier version recorded, or coming from legacy 0.0.x): the installed '
         'version only, no “Since”.</p></div>',
         f'<div class="scol" style="height:520px">{card(t, ("0.1.2",), "0.0.2", more=4, at_end=True)}'
         '<p class="cap">More than 10 versions behind, scrolled to the end: the newest 10, then a line that opens '
         'all release notes on GitHub.</p></div>',
         '</div>',
         '<h2>The dialog with nothing to show (Help › What\'s new)</h2><div class="srow">',
         f'<div class="scol">{empty_dialog(t, "off")}<p class="cap">Update checks off and nothing fetched before: '
         'no request is made.</p></div>',
         f'<div class="scol">{empty_dialog(t, "fetching")}<p class="cap">Checks on, nothing cached yet: opening the '
         'dialog fetches.</p></div>',
         f'<div class="scol">{empty_dialog(t, "failed")}<p class="cap">Checks on, offline, nothing cached. On Home '
         'the card stays hidden in all three cases.</p></div>',
         '</div>',
         '<h2>Narrow window (900 × 680): the card under the list, 200 high, as SH2</h2>',
         home(t, 900, 680, news=card(t)),
         '</div>']
    (OUT / f"sh3-states-{key}.html").write_text(doc(t, "".join(s)))
print("ok")
