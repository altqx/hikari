import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent


def icon(name):
    svg = (here / "icons" / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


html = (here / "import.template.html").read_text()
html = re.sub(r"\{\{i:([a-z-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder")
(here / "import.html").write_text(html)
