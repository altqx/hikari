import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
icons = here / "icons"


def icon(name):
    svg = (icons / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


tools = "".join(
    f'<span class="ib">{{{{i:{n}}}}}</span>' if n != "|" else '<span class="sepv"></span>'
    for n in ["open-subtitles", "save", "|", "undo", "redo", "|", "open-video", "open-audio", "|",
              "find-replace", "styles", "automation", "|", "settings"]
)
chrome = (
    '<div class="tbar"><span class="app">HikariSub</span><span class="m">File</span><span class="m">Edit</span>'
    '<span class="m">Subtitle</span><span class="m">Timing</span><span class="m">Video</span><span class="m">Audio</span>'
    '<span class="m">Automation</span><span class="m">View</span><span class="m">Help</span><span class="doc">song.ass</span>'
    '<span class="sp"></span><span class="wc">{{i:window-minimize}}</span><span class="wc">{{i:window-maximize}}</span>'
    '<span class="wc">{{i:window-close}}</span></div>'
    f'<div class="tools">{tools}<span class="sp"></span><span class="ib">{{{{i:translation-mode}}}}</span>'
    '<span class="ib">{{i:template-editor}}</span><span class="ib on">{{i:node-editor}}</span></div>'
)

lines = [
    (8, "0:00:01.20", "ki ra ki ra hi ka ru"),
    (9, "0:00:03.05", "yo zo ra no ho shi yo"),
    (10, "0:00:05.20", "ma ba ta ki shi te wa"),
    (11, "0:00:08.40", "mi n na wo mi te ru"),
    (12, "0:00:11.70", "ka ra o ke no u ta"),
    (13, "0:00:14.90", "ko ko ro ni hi bi ku"),
    (14, "0:00:17.60", "a no hi no yu me wo"),
    (15, "0:00:20.30", "i ma mo o bo e te ru"),
    (16, "0:00:23.10", "ka ze ni no se te"),
    (17, "0:00:25.80", "to o ku e to"),
    (18, "0:00:28.40", "ha ru ka na so ra e"),
    (19, "0:00:31.00", "to n de yu ke"),
]
def source(fx):
    rows = "".join(
        f'<div class="r{" on" if n == 12 else ""}"><span class="n">{n}</span><span class="tm mono">{t}</span>'
        f'<span class="tx">{x}</span><span class="g">{"1" if fx else ""}</span></div>'
        for n, t, x in lines
    )
    return (
    '<div class="panel" style="flex:1"><div class="ph">Source lines<span class="crumb">· 24 karaoke Lines, Default</span>'
        '<span class="sp"></span><span class="ib">{{i:panel-menu}}</span></div>'
        '<div class="src"><div class="r hd"><span class="n">#</span><span class="tm">Start</span><span class="tx">Text</span>'
        f'<span class="g">fx</span></div>{rows}</div></div>'
    )


def video(kept):
    badge = "Kept from the last run that worked" if kept else "Preview · Line 12 · not applied"
    return (
        '<div class="panel"><div class="ph">Video<span class="sp"></span><span class="ib">{{i:panel-menu}}</span></div>'
        f'<div class="video{" dim" if kept else ""}"><div class="scene"></div>'
        '<div class="kara"><span class="sung">ka ra o </span><span class="now">ke</span> no u ta</div>'
        f'<div class="badge">{badge}</div></div>'
        '<div class="trans"><span class="ib">{{i:play-line}}</span><span class="ib">{{i:loop}}</span>'
        '<span class="ib">{{i:show}}</span><span class="t mono">0:00:12.84</span></div>'
        '<div class="pv"><h5>Preview on Line 12</h5>'
        '<div class="ln mono"><span class="mut">0</span><span>0:00:11.40</span>'
        '<span class="x">{\\fad(300,300)\\1c&amp;H78DC64&amp;\\k30}{\\kf42}ka{\\kf38}ra{\\kf35}o{\\kf61}ke…</span></div></div></div>'
    )


html = (here / "nodes.template.html").read_text()
html = (html.replace("{{chrome}}", chrome).replace("{{tools}}", tools).replace("{{sourcePanelNoFx}}", source(False)).replace("{{sourcePanel}}", source(True))
        .replace("{{videoPanelKept}}", video(True)).replace("{{videoPanel}}", video(False)))
html = re.sub(r"\{\{i:([a-z-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder: " + html[html.index("{{"):html.index("{{") + 40])
(here / "nodes.html").write_text(html)
