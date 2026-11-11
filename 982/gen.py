import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
repo_icons = here.parents[1] / "src/ui/icons"


def icon(name):
    svg = (repo_icons / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


pages = [
    ("spellchecker", "Spelling and grammar"), ("script-properties", "Script properties"),
    ("translation-mode", "Translation"), ("ai-assistant", "AI"),
]
side = '<div class="side">' + "".join(
    f'<div class="it{" on" if label == "AI" else ""}">{{{{i:{role}}}}}{label}</div>' for role, label in pages
) + "</div>"
foot = (
    '<div class="foot"><span class="sp"></span><span class="btn primary">OK</span><span class="btn">Cancel</span>'
    '<span class="btn">Apply</span></div>'
)
tabs = '<div class="tabs"><span class="tab">Yours</span><span class="tab on">Project</span></div>'

html = (here / "credentials.template.html").read_text()
html = html.replace("{{css}}", (here / "base.css").read_text())
html = html.replace("{{side}}", side).replace("{{foot}}", foot).replace("{{tabs}}", tabs)
html = re.sub(r"\{\{i:([a-z0-9-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder")
(here / "credentials.html").write_text(html)
