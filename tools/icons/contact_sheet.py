#!/usr/bin/env python3
"""HTML contact sheet of the icon set: contact_sheet.py [output.html]."""

import html
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ICONS = os.path.join(ROOT, "src", "ui", "icons")
THEME = os.path.join(ROOT, "src", "ui", "theme.cpp")
THEME_H = os.path.join(ROOT, "src", "ui", "theme.h")

APPEARANCES = [
    # theme::codeName, name, theme::Code
    ("light", "Light", "Light"),
    ("dark", "Dark", "Dark"),
    ("highContrastWhite", "High contrast white", "HighContrastWhite"),
    ("highContrastBlack", "High contrast black", "HighContrastBlack"),
]
SLOTS = ["normal", "accent", "disabled"]
RGB = r'rgb\(0x([0-9A-Fa-f]{6})\)'


def theme_colours():
    """Each theme's background, text and icon colours, parsed from theme.cpp."""
    source = open(THEME, encoding="utf-8").read()
    header = open(THEME_H, encoding="utf-8").read()
    names = re.search(r'kRoleNames\{([^}]*)\}', header)
    default = re.search(r'kDefaultAccent = "(\w+)"', header)
    if not names or not default:
        sys.exit("expected kRoleNames and kDefaultAccent in theme.h")
    names = re.findall(r'"(\w+)"', names.group(1))
    out = {}
    for aid, _, code in APPEARANCES:
        body = re.search(r'case Code::%s:\s*return \{(.*?)\};' % code, source, re.S)
        values = re.findall(r'%s|\{\}' % RGB, body.group(1)) if body else []
        if len(values) != len(names):
            sys.exit("expected the %d roles of Code::%s in theme.cpp's baseRoles" % (len(names), code))
        roles = {n: ("#" + v.upper() if v else None) for n, v in zip(names, values)}
        if roles["accent"] is None:
            table = "kDarkAccents" if code == "Dark" else "kLightAccents"
            preset = re.search(r'%s\{\{.*?\{"%s",[^}]*?%s' % (table, default.group(1), RGB), source, re.S)
            if not preset:
                sys.exit("expected the %s preset in theme.cpp's %s" % (default.group(1), table))
            roles["accent"] = "#" + preset.group(1).upper()
        out[aid] = {
            "background": roles["background"], "text": roles["text"],
            "normal": roles["text"], "accent": roles["accent"], "disabled": roles["disabled"],
        }
    return out


def inline(svg, size):
    svg = re.sub(r'width="16" height="16"', 'width="%d" height="%d"' % (size, size), svg)
    return svg.replace('<g id="accent">', '<g class="accent">').strip()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "out", "k1", "index.html")
    manifest = json.load(open(os.path.join(ICONS, "manifest.json"), encoding="utf-8"))
    colours = theme_colours()
    css = []
    for aid, _, _ in APPEARANCES:
        c = colours[aid]
        bg, fg = c["background"], c["text"]
        css.append(
            ".%(a)s{background:%(bg)s;color:%(n)s}.%(a)s .accent{color:%(ac)s}"
            ".%(a)s .off,.%(a)s .off .accent{color:%(dis)s}"
            ".%(a)s .lbl{color:%(fg)s}"
            % {"a": aid, "bg": bg, "fg": fg, "n": c["normal"], "ac": c["accent"],
               "dis": c["disabled"]})
    rows = []
    for icon in manifest["icons"]:
        svg = open(os.path.join(ICONS, icon["file"]), encoding="utf-8").read()
        cells = []
        for aid, _, _ in APPEARANCES:
            cells.append(
                '<td class="sw %s"><span>%s</span><span>%s</span><span class="off">%s</span></td>'
                % (aid, inline(svg, 16), inline(svg, 32), inline(svg, 16)))
        rows.append(
            "<tr><td><code>%s</code><div class=meta>%s%s</div></td>%s"
            "<td class=meta>%s</td></tr>" % (
                html.escape(icon["role"]), html.escape(icon["label"]),
                " &middot; mirrors in RTL" if icon["mirror"] else "", "".join(cells),
                html.escape(", ".join(icon["surfaces"]))))
    sheets = []
    for aid, name, _ in APPEARANCES:
        for pct in (100, 150, 200):
            png = "k1-%s-%d.png" % (aid, pct)
            sheets.append('<a href="%s">%s %d%%</a>' % (png, name, pct))
    swatches = []
    for aid, name, _ in APPEARANCES:
        items = "".join('<span class=chip><i style="background:%s"></i>%s %s</span>' % (
            colours[aid][s], s, colours[aid][s]) for s in SLOTS)
        swatches.append("<div><b>%s</b> on %s: %s</div>" % (name, colours[aid]["background"], items))
    page = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>HikariSub icon set</title>
<style>
:root{--bg:#f6f7f8;--fg:#1d242c;--muted:#5b6672;--line:#d5dbe0}
@media (prefers-color-scheme: dark){:root{--bg:#14181c;--fg:#e4e9ee;--muted:#9aa6b2;--line:#323b45}}
body{margin:0;padding:24px 16px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}
h1{font-size:22px;margin:0 0 4px}p{max-width:72ch;color:var(--muted)}
table{border-collapse:collapse;width:100%%;margin-top:16px}
th,td{border-bottom:1px solid var(--line);padding:6px 8px;text-align:left;vertical-align:middle}
th{position:sticky;top:0;background:var(--bg);font-weight:600}
td.sw span{display:inline-flex;align-items:center;margin-right:10px}
td.sw svg{display:block}
.meta{color:var(--muted);font-size:12px}
.chip{display:inline-flex;align-items:center;margin-right:12px;font-family:ui-monospace,monospace;font-size:12px}
.chip i{display:inline-block;width:12px;height:12px;margin-right:4px;border:1px solid var(--line)}
.sheets a{margin-right:12px}
.wrap{overflow-x:auto}
%s
</style></head><body>
<h1>HikariSub icon set (K1)</h1>
<p>%d icons drawn in house on a 16-unit grid (docs/ux/icons.md). Each appearance column shows the icon
at 16 and 32 px, then disabled at 16 px, in the theme layer's colours (Light and Dark
with the default accent); the accent layer is the second colour. PNG sheets rendered by the
application's Icon item: <span class=sheets>%s</span></p>
%s
<div class=wrap><table>
<thead><tr><th>Role</th><th>Light</th><th>Dark</th><th>High contrast white</th><th>High contrast black</th><th>Surfaces</th></tr></thead>
<tbody>
%s
</tbody></table></div>
</body></html>
""" % ("".join(css), len(manifest["icons"]), " ".join(sheets), "".join(swatches), "\n".join(rows))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(page)
    print(out)


if __name__ == "__main__":
    main()
