#!/usr/bin/env python3
"""The images of the user's visual review page, rendered by the application.

Runs the UI tests' screenshot hooks in a built tree and gathers what they
write into one folder, one PNG per review item, with a manifest.json listing
each item as {id, card, title, file, note}:

* K1: the four icons awaiting approval (copy-to-storage, copy-to-ass,
  copy-to-all-ass, panel-menu) drawn by the Icon item in Light and Dark at
  100, 150 and 200 %, one sheet (icon_tests contactSheets with
  HIKARI_ICON_SHEET_ROLES, as the polish pass's new-icons sheets);
* K2: the seven accent presets of Light and Dark on each surface role
  (theme_tests swatchSheet, HIKARI_THEME_SHEET_DIR) and the main window in
  the four themes (shell_tests surfaceScreenshots, HIKARI_SURFACE_SHOT_DIR);
* O5: the main window, the File menu and the Options dialog in Polish, Thai,
  the accented pseudolocale and right-to-left (language_tests screenshots,
  HIKARI_LANGUAGE_SHOT_DIR);
* D3: the dock and panel chrome in the four themes (shell_tests
  dockChromeScreenshots): the main window, the bottom panels' one-tab bars,
  a tabbed group, its "..." menu, a floating panel and a drag.

The tests run on Qt's offscreen platform, which draws with the system's
fonts through fontconfig on Linux. Windows counterparts come from the
Windows VM, not from this script.

Run:  python3 tools/review/review_assets.py <build dir> <output dir>
      (the build dir is a configured and built tree, e.g.
      out/build/ubuntu-x64-release; the output dir is created)
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

K1_ROLES = ["copy-to-storage", "copy-to-ass", "copy-to-all-ass", "panel-menu"]
K1_CELL_WIDTH = 168  # icon_tests contactSheets: a cell is 168 x 44 at 100 %
K1_CELL_HEIGHT = 44
SCALES = [(100, "1"), (150, "1.5"), (200, "2")]
THEMES = [
    ("light", "Light"),
    ("dark", "Dark"),
    ("highContrastWhite", "High contrast white"),
    ("highContrastBlack", "High contrast black"),
]
LANGUAGES = [
    ("polish", "Polish"),
    ("thai", "Thai"),
    ("pseudo-accents", "accented pseudolocale"),
    ("pseudo-rtl", "right-to-left pseudolocale"),
]
LANGUAGE_VIEWS = [
    ("main-window", "Main window", "The main window with a document open."),
    ("file-menu", "File menu", "The File menu, open over the main window."),
    ("options", "Options dialog", "The Options dialog as it opens."),
]
DOCK_VIEWS = [
    ("main", "Main window", "The default arrangement with a reference open."),
    ("bottom", "Bottom panels",
     "The Grid's one-tab bar and the Reference tray with its toolbar in the header."),
    ("tabbed", "Tabbed group", "Shift times as a tab beside the Line editor."),
    ("menu", "Panel menu", "The tabbed group's panel menu, open."),
    ("floating", "Floating panel",
     "The Audio panel floating, on the main window's background (the margin around it is the canvas)."),
    ("drag", "Drag", "The Video panel dragged by its header over the Grid, the drop indicator shown."),
]


def run(build, binary, functions, env):
    path = os.path.join(build, "tests", "ui", binary)
    if not os.path.exists(path):
        sys.exit("missing %s: build the tree first" % path)
    full = dict(os.environ)
    full["QT_QPA_PLATFORM"] = "offscreen"
    full.update(env)
    print("running %s %s %s" % (binary, " ".join(functions),
                                " ".join("%s=%s" % kv for kv in sorted(env.items()))), flush=True)
    result = subprocess.run([path] + functions, env=full, cwd=os.path.dirname(path),
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.exit("%s failed (%d)" % (binary, result.returncode))


def label_font(size):
    try:
        path = subprocess.run(["fc-match", "-f", "%{file}", "sans-serif:bold"],
                              stdout=subprocess.PIPE, text=True, check=True).stdout
        return ImageFont.truetype(path, size)
    except (OSError, subprocess.CalledProcessError):
        return ImageFont.load_default()


def k1_sheet(build, work, out):
    """The four icons' sheets at each scale, cropped to the four cells and
    stacked under a caption naming the theme and scale."""
    rows = []
    for percent, factor in SCALES:
        folder = os.path.join(work, "k1-%d" % percent)
        run(build, "hikari_ui_icon_tests", ["contactSheets"], {
            "QT_SCALE_FACTOR": factor,
            "HIKARI_ICON_SHEET_ROLES": ",".join(K1_ROLES),
            "HIKARI_ICON_SHEET_PREFIX": "review",
            "HIKARI_ICON_SHEET_DIR": folder,
        })
        for theme, name in THEMES[:2]:
            sheet = Image.open(os.path.join(folder, "review-%s-%d.png" % (theme, percent))).convert("RGB")
            scale = percent / 100
            crop = sheet.crop((0, 0, round(len(K1_ROLES) * K1_CELL_WIDTH * scale), round(K1_CELL_HEIGHT * scale)))
            rows.append(("%s %d %%" % (name, percent), crop))
    width = max(image.width for _, image in rows) + 32
    font = label_font(14)
    caption = 28
    height = sum(caption + image.height + 16 for _, image in rows) + 16
    page = Image.new("RGB", (width, height), "#ffffff")
    draw = ImageDraw.Draw(page)
    y = 16
    for text, image in rows:
        draw.text((16, y + 4), text, fill="#202832", font=font)
        y += caption
        page.paste(image, (16, y))
        y += image.height + 16
    target = os.path.join(out, "k1-four-icons.png")
    page.save(target)
    return [{
        "id": "k1-four-icons",
        "card": "K1",
        "title": "K1: the four new icons",
        "file": os.path.basename(target),
        "note": "copy-to-storage, copy-to-ass, copy-to-all-ass and panel-menu, drawn by the application's Icon "
                "item at 16 and 32 px in Light and Dark at 100, 150 and 200 % (offscreen, Linux). "
                "Approve or reject each icon.",
    }]


def k2_items(build, work, out):
    folder = os.path.join(work, "k2")
    run(build, "hikari_ui_theme_tests", ["swatchSheet"], {"HIKARI_THEME_SHEET_DIR": folder})
    shutil.copy(os.path.join(folder, "k2-accents.png"), os.path.join(out, "k2-accents.png"))
    shutil.copy(os.path.join(folder, "k2-contrast.txt"), os.path.join(out, "k2-contrast.txt"))
    items = [{
        "id": "k2-accents",
        "card": "K2",
        "title": "K2: the seven accents in Light and Dark",
        "file": "k2-accents.png",
        "note": "Each accent preset as a button and a selected line on the background, panel, raised and field "
                "surfaces, with its lowest contrast ratios (k2-contrast.txt has the full table). The high "
                "contrast themes have no accent choice.",
    }]
    shots = os.path.join(work, "surfaces")
    run(build, "hikari_ui_shell_tests", ["surfaceScreenshots"], {"HIKARI_SURFACE_SHOT_DIR": shots})
    for theme, name in THEMES:
        file = "k2-main-window-%s.png" % theme
        shutil.copy(os.path.join(shots, "main-window-%s.png" % theme), os.path.join(out, file))
        items.append({
            "id": "k2-main-window-%s" % theme,
            "card": "K2",
            "title": "K2: main window, %s" % name,
            "file": file,
            "note": "A document, video and audio open, a modified tab, the Grid focused (its header and "
                    "current row ringed); 1600 x 900, offscreen, Linux.",
        })
    return items


def o5_items(build, work, out):
    folder = os.path.join(work, "o5")
    run(build, "hikari_ui_language_tests", ["screenshots"], {"HIKARI_LANGUAGE_SHOT_DIR": folder})
    items = []
    for language, name in LANGUAGES:
        for view, title, note in LANGUAGE_VIEWS:
            file = "o5-%s-%s.png" % (view, language)
            shutil.copy(os.path.join(folder, "%s-%s.png" % (view, language)), os.path.join(out, file))
            items.append({
                "id": "o5-%s-%s" % (view, language),
                "card": "O5",
                "title": "O5: %s, %s" % (title, name),
                "file": file,
                "note": note + " Language switched live; offscreen, Linux.",
            })
    return items


def d3_items(build, work, out):
    folder = os.path.join(work, "d3")
    run(build, "hikari_ui_shell_tests", ["dockChromeScreenshots"], {"HIKARI_SURFACE_SHOT_DIR": folder})
    items = []
    for theme, name in THEMES:
        for view, title, note in DOCK_VIEWS:
            file = "d3-%s-%s.png" % (view, theme)
            shutil.copy(os.path.join(folder, "dock-%s-%s.png" % (view, theme)), os.path.join(out, file))
            items.append({
                "id": "d3-%s-%s" % (view, theme),
                "card": "D3",
                "title": "D3: %s, %s" % (title, name),
                "file": file,
                "note": note + " Offscreen, Linux.",
            })
    return items


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build, out = (os.path.abspath(a) for a in sys.argv[1:])
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="hikari-review-") as work:
        items = k1_sheet(build, work, out)
        items += k2_items(build, work, out)
        items += o5_items(build, work, out)
        items += d3_items(build, work, out)
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(items, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("%d items in %s" % (len(items), out))


if __name__ == "__main__":
    main()
