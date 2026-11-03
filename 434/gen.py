import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
icons = here / "icons"


def icon(name):
    svg = (icons / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


pages = [
    ("settings", "General"),
    ("appearance", "Appearance"),
    ("editor", "Line editor"),
    ("spellchecker", "Spelling and grammar"),
    ("settings-grid", "Grid"),
    ("settings-video", "Video"),
    ("settings-audio", "Audio"),
    ("convert", "Conversion"),
    ("script-properties", "Script properties"),
    ("automation", "Automation"),
    ("hotkeys", "Shortcuts"),
    ("ai-assistant", "AI"),
    ("check-updates", "Updates and privacy"),
]
side = '<div class="side">' + "".join(
    f'<div class="it{" on" if label == "AI" else ""}">{{{{i:{role}}}}}{label}</div>' for role, label in pages
) + "</div>"
foot = (
    '<div class="foot"><span class="btn">{{i:restore-default}}Reset page</span><span class="ib">{{i:chevron-down}}</span>'
    '<span class="sp"></span><span class="btn primary">OK</span><span class="btn">Cancel</span><span class="btn">Apply</span></div>'
)

html = (here / "signin.template.html").read_text()
html = html.replace("{{side}}", side).replace("{{foot}}", foot)
html = re.sub(r"\{\{i:([a-z-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder")
(here / "signin.html").write_text(html)
