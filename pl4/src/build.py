import json, pathlib, sys
here = pathlib.Path(__file__).resolve().parent
dirs = [pathlib.Path(sys.argv[1]), here / "icons-extra"]
icons = {p.stem: p.read_text().strip() for d in dirs for p in sorted(d.glob("*.svg"))}
page = (here / "template.html").read_text()
page = page.replace("/*ICONS*/", json.dumps(icons)).replace("/*SPEC*/", (here / "spec.js").read_text())
(here / "index.html").write_text(page)
print(len(icons), "icons,", len(page) // 1024, "KB")
