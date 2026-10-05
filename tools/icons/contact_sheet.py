#!/usr/bin/env python3
"""K1: the HTML contact sheet of the HikariSub icon set, for review.

Writes one self-contained page (default out/k1/index.html) showing every
icon of src/ui/icons/manifest.json in the light, dark and high-contrast
appearances at 16 and 32 px, with its hover/pressed and disabled colours,
beside the legacy bitmap it replaces (HikariSub/Bitmaps, embedded), its
label, the surfaces using it and whether it mirrors in right-to-left
layouts. The page links the PNG sheets hikari_ui_icon_tests writes into the
same folder (k1-<appearance>-<percent>.png, rendered by the app's Icon item).

The colours are the registry defaults (src/application/settings.cpp); the
inline SVGs take them through CSS `color`, as currentColor does in the app.

Run:  python3 tools/icons/contact_sheet.py [output.html]
"""

import base64
import html
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ICONS = os.path.join(ROOT, "src", "ui", "icons")
BITMAPS = os.path.join(ROOT, "HikariSub", "Bitmaps")
SETTINGS = os.path.join(ROOT, "src", "application", "settings.cpp")

APPEARANCES = [
    # id, name, background (visual-language.md bg), text
    ("light", "Light", "#E5E9EC", "#202832"),
    ("dark", "Dark", "#171B20", "#E8EDF2"),
    ("highContrast", "High contrast", "#000000", "#FFFFFF"),
]
SLOTS = ["normal", "accent", "active", "disabled"]


def defaults():
    """The icon colours' registry defaults, read from settings.cpp."""
    text = open(SETTINGS, encoding="utf-8").read()
    found = re.findall(r'kIconColourSettings\[(\d)\]\[(\d)\].*?std::string\("(#[0-9A-Fa-f]{6})"\)', text)
    out = {}
    for a, s, colour in found:
        out[(APPEARANCES[int(a)][0], SLOTS[int(s)])] = colour
    if len(out) != 12:
        sys.exit("expected the 12 icon colour settings in settings.cpp")
    return out


def inline(svg, size):
    svg = re.sub(r'width="16" height="16"', 'width="%d" height="%d"' % (size, size), svg)
    return svg.replace('<g id="accent">', '<g class="accent">').strip()


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "out", "k1", "index.html")
    manifest = json.load(open(os.path.join(ICONS, "manifest.json"), encoding="utf-8"))
    colours = defaults()
    css = []
    for aid, _, bg, fg in APPEARANCES:
        c = {s: colours[(aid, s)] for s in SLOTS}
        css.append(
            ".%(a)s{background:%(bg)s;color:%(n)s}.%(a)s .accent{color:%(ac)s}"
            ".%(a)s .hover,.%(a)s .hover .accent{color:%(act)s}"
            ".%(a)s .off,.%(a)s .off .accent{color:%(dis)s}"
            ".%(a)s .lbl{color:%(fg)s}"
            % {"a": aid, "bg": bg, "fg": fg, "n": c["normal"], "ac": c["accent"], "act": c["active"],
               "dis": c["disabled"]})
    rows = []
    for icon in manifest["icons"]:
        svg = open(os.path.join(ICONS, icon["file"]), encoding="utf-8").read()
        cells = []
        for aid, _, _, _ in APPEARANCES:
            cells.append(
                '<td class="sw %s"><span>%s</span><span>%s</span><span class="hover">%s</span>'
                '<span class="off">%s</span></td>' % (aid, inline(svg, 16), inline(svg, 32), inline(svg, 16),
                                                      inline(svg, 16)))
        legacy = []
        for name in icon["legacy"]:
            path = os.path.join(BITMAPS, name)
            data = base64.b64encode(open(path, "rb").read()).decode("ascii")
            legacy.append('<img alt="%s" title="%s" src="data:image/png;base64,%s">' % (
                html.escape(name), html.escape(name), data))
        rows.append(
            "<tr><td><code>%s</code><div class=meta>%s%s</div></td>%s<td class=legacy>%s<div class=meta>%s</div></td>"
            "<td class=meta>%s</td></tr>" % (
                html.escape(icon["role"]), html.escape(icon["label"]),
                " &middot; mirrors in RTL" if icon["mirror"] else "", "".join(cells),
                "".join(legacy) or "&mdash;", html.escape(", ".join(icon["legacy"])),
                html.escape(", ".join(icon["surfaces"]))))
    sheets = []
    for aid, name, _, _ in APPEARANCES:
        for pct in (100, 150, 200):
            png = "k1-%s-%d.png" % (aid, pct)
            sheets.append('<a href="%s">%s %d%%</a>' % (png, name, pct))
    swatches = []
    for aid, name, bg, _ in APPEARANCES:
        items = "".join('<span class=chip><i style="background:%s"></i>%s %s</span>' % (
            colours[(aid, s)], s, colours[(aid, s)]) for s in SLOTS)
        swatches.append("<div><b>%s</b> on %s: %s</div>" % (name, bg, items))
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
.legacy img{image-rendering:pixelated;width:32px;height:32px;margin-right:4px;vertical-align:middle;background:#fff}
.chip{display:inline-flex;align-items:center;margin-right:12px;font-family:ui-monospace,monospace;font-size:12px}
.chip i{display:inline-block;width:12px;height:12px;margin-right:4px;border:1px solid var(--line)}
.sheets a{margin-right:12px}
.wrap{overflow-x:auto}
%s
</style></head><body>
<h1>HikariSub icon set (K1)</h1>
<p>%d icons drawn in house on a 16-unit grid (docs/qt/ux/icons.md). Each appearance column shows the icon
at 16 and 32 px, then hover/pressed and disabled at 16 px, in the default icon colours; the accent layer
is the second colour. The legacy bitmap each one replaces is shown on white at 2x. PNG sheets rendered by the
application's Icon item: <span class=sheets>%s</span></p>
%s
<div class=wrap><table>
<thead><tr><th>Role</th><th>Light</th><th>Dark</th><th>High contrast</th><th>Legacy bitmap</th><th>Surfaces</th></tr></thead>
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
