import json, pathlib, sys
here = pathlib.Path(__file__).parent
icons_dir = pathlib.Path("/home/altq/.t3/worktrees/hikari/w8-op3-e6d97f7b8b155fcb/src/ui/icons")
plugin_icon = pathlib.Path("/home/altq/Work/hikari-wt/op3/hikari-official-plugins/plugins/git-line-marks/icon.svg")
names = ["refresh", "clear", "search", "panel-menu", "save", "notice-error", "notice-info", "notice-warning", "plugin-official", "plugins"]
icons = {n: (icons_dir / f"{n}.svg").read_text().strip() for n in names}
html = (here / "template.html").read_text()
html = html.replace("{{ICONS}}", json.dumps(icons)).replace("{{PLUGIN_ICON}}", json.dumps(plugin_icon.read_text().strip()))
(here / "index.html").write_text(html)
