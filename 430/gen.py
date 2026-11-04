import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent
icons = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else here / "icons"


def icon(name):
    svg = (icons / f"{name}.svg").read_text().strip()
    return svg.replace('<g id="accent">', '<g class="acc">')


html = (here / "review.template.html").read_text()
html = re.sub(r"\{\{i:([a-z-]+)\}\}", lambda m: icon(m.group(1)), html)
if "{{" in html:
    sys.exit("unfilled placeholder")
(here / "review.html").write_text(html)
