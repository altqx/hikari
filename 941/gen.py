import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
repo_icons = here.parents[2] / "src/ui/icons"
extra = {
    # Candidate glyph for the Mine chip (question 3); no such icon exists yet.
    "tier-mine": '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
    'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">'
    '<circle cx="8" cy="5.5" r="2.5"/><path d="M3 13.5c.6-2.6 2.6-4 5-4s4.4 1.4 5 4"/></svg>',
}


def icon(name):
    if name in extra:
        return extra[name]
    svg = (repo_icons / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


pages = [
    ("settings", "General"), ("appearance", "Appearance"), ("editor", "Line editor"),
    ("spellchecker", "Spelling and grammar"), ("settings-grid", "Grid"), ("settings-video", "Video"),
    ("settings-audio", "Audio"), ("convert", "Conversion"), ("script-properties", "Script properties"),
    ("automation", "Automation"), ("hotkeys", "Shortcuts"), ("ai-assistant", "AI"),
    ("check-updates", "Updates and privacy"),
]
side = '<div class="side">' + "".join(
    f'<div class="it{" on" if label == "AI" else ""}">{{{{i:{role}}}}}{label}</div>' for role, label in pages
) + "</div>"
foot = (
    '<div class="foot"><span class="btn">{{i:restore-default}}Reset page</span><span class="ib">{{i:chevron-down}}</span>'
    '<span class="sp"></span><span class="btn primary">OK</span><span class="btn">Cancel</span><span class="btn">Apply</span></div>'
)
tabs = '<div class="tabs"><span class="tab on">Yours</span><span class="tab">Project</span></div>'

html = (here / "skills.template.html").read_text()
html = html.replace("{{css}}", (here / "base.css").read_text())
html = html.replace("{{side}}", side).replace("{{foot}}", foot).replace("{{tabs}}", tabs)
html = re.sub(r"\{\{i:([a-z0-9-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder")
(here / "skills.html").write_text(html)
