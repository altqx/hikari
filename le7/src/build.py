import json, pathlib, sys
here = pathlib.Path(__file__).resolve().parent
icons = {p.stem: p.read_text().strip() for p in sorted(pathlib.Path(sys.argv[1]).glob("*.svg"))}
page = (here / "template.html").read_text()
page = page.replace("/*ICONS*/", json.dumps(icons)).replace("/*SPEC*/", (here / "spec.js").read_text())
(here.parent / "index.html").write_text(page)
print(len(icons), "icons,", len(page) // 1024, "KB")
