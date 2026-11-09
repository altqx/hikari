import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
icons = pathlib.Path(sys.argv[1])


def icon(name):
    svg = (icons / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


def chrome(title):
    return (
        '<div class="dh"><span class="tabs"><span class="on">{{i:ai-assistant}}Agent</span>'
        '<span>{{i:review-keep}}Review</span></span><span class="sp"></span><span class="ib">{{i:menu-more}}</span></div>'
        f'<div class="th"><span class="ib">{{{{i:history}}}}</span><span class="t">{title}</span>'
        '<span class="ib">{{i:menu-more}}</span><span class="ib">{{i:add}}</span></div>'
    )


def composer(state, model):
    globe = {"on": "ib sm web on", "hover": "ib sm web on ring", "dis": "ib sm web dis"}[state]
    return (
        '<div class="comp"><div class="txt"><span class="ph">Ask, or type @ to attach, / for commands</span></div>'
        f'<div class="bar"><span class="ib sm">{{{{i:add}}}}</span><span class="{globe}">{{{{i:web-search}}}}</span>'
        f'<span class="cb">{model}{{{{i:chevron-down}}}}</span><span class="cb">Auto{{{{i:chevron-down}}}}</span>'
        '<span class="sp"></span><span class="send">{{i:ai-send}}</span></div></div>'
    )


html = (here / "head.part").read_text() + (here / "body.part").read_text()
html = re.sub(r"\{\{chrome:([^}]*)\}\}", lambda m: chrome(m.group(1)), html)
html = re.sub(r"\{\{composer:(on|hover|dis):([^}]*)\}\}", lambda m: composer(m.group(1), m.group(2)), html)
html = re.sub(r"\{\{i:([a-z-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder")
(here / "ai23b.html").write_text(html)
