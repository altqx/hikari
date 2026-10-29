#!/usr/bin/env python3
"""Renders the visual review page's images from a built tree's UI test hooks.

Run:  python3 tools/review/review_assets.py <build dir> <output dir> [k1|k2|k2-focus|o5|d3...]
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("review_assets.py needs Pillow (python-pillow, or pip install pillow)")

K1_ROLES = ["copy-to-storage", "copy-to-ass", "copy-to-all-ass", "panel-menu"]
# MUST KILL: copies icon_tests contactSheets' grid at 100 %
K1_COLUMNS = 8
K1_CELL_WIDTH = 168
K1_CELL_HEIGHT = 44
TIMEOUT_SECONDS = 600
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
    ("options", "Options dialog", "The Options dialog as it opens, on its first page (Editor)."),
]
PARTIAL_CATALOG = {"polish", "thai"}
FOCUS_VIEWS = [
    ("grid", "Grid focused",
     "The main window with a document open and the Grid focused: the accent corner mark at the Grid panel's top "
     "right, the current row's 1-wide ring inside its accent outline."),
    ("tabbed", "Tabbed group focused",
     "Shift times as a tab beside the Line editor, its field focused by a click (no ring): the group's corner mark."),
    ("floating", "Floating panel focused",
     "The Audio panel floating and focused, on the main window's background (the margin around it is the "
     "canvas): the corner mark at its header's top right."),
    ("button", "Focused button", "Shift times' Apply with keyboard focus: the 1-wide ring, and the group's mark."),
    ("field", "Focused text field",
     "Shift times' time field with keyboard focus: the accent border with the 1-wide ring beside it, and the "
     "group's mark."),
    ("checkbox", "Focused check box",
     "Shift times' Tag times with keyboard focus: the 1-wide ring round the whole control, and the group's mark."),
    ("swatch", "Focused accent swatch",
     "The Options dialog's Appearance page, the Green accent swatch with keyboard focus: the 1-wide ring "
     "outside it. The high contrast themes have no accent swatches: there the Dark theme card has the focus."),
]
DOCK_VIEWS = [
    ("main", "Main window",
     "The default arrangement with a document and a reference open; no video or audio loaded."),
    ("bottom", "Bottom panels",
     "The Grid's one-tab bar and the Reference tray with its toolbar in the header."),
    ("tabbed", "Tabbed group", "Shift times as a tab beside the Line editor."),
    ("menu", "Panel menu", "The tabbed group's panel menu, open."),
    ("floating", "Floating panel",
     "The Audio panel floating, on the main window's background (the margin around it is the canvas)."),
    ("drag", "Drag", "The Video panel dragged by its header over the Grid, the drop indicator shown."),
]


def run(build, binary, functions, env):
    """Runs the hooks `functions` of test `binary`; exits if one fails, times out or skips."""
    path = os.path.join(build, "tests", "ui", binary)
    if not os.path.exists(path):
        sys.exit("missing %s: build the tree first" % path)
    full = dict(os.environ)
    full["QT_QPA_PLATFORM"] = "offscreen"
    full.update(env)
    hooks = "%s %s" % (binary, " ".join(functions))
    print("running %s %s" % (hooks, " ".join("%s=%s" % kv for kv in sorted(env.items()))), flush=True)
    try:
        result = subprocess.run([path, "-v1"] + functions, env=full, cwd=os.path.dirname(path),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as e:
        output = e.stdout or ""
        sys.stdout.write(output if isinstance(output, str) else output.decode(errors="replace"))
        sys.exit("%s timed out after %d s" % (hooks, TIMEOUT_SECONDS))
    if result.returncode != 0:
        sys.stdout.write(result.stdout)
        sys.exit("%s failed (%d)" % (hooks, result.returncode))
    if re.search(r"^SKIP\b", result.stdout, re.MULTILINE):
        sys.stdout.write(result.stdout)
        sys.exit("%s skipped: no images written" % hooks)


def written(folder, name, hook):
    """The path of `name` in `folder`, which `hook` was to write."""
    path = os.path.join(folder, name)
    if not os.path.exists(path):
        sys.exit("%s did not write %s" % (hook, path))
    return path


def label_font(size):
    try:
        path = subprocess.run(["fc-match", "-f", "%{file}", "sans-serif:bold"],
                              stdout=subprocess.PIPE, text=True, check=True).stdout
        return ImageFont.truetype(path, size)
    except (OSError, subprocess.CalledProcessError):
        return ImageFont.load_default()


def k1_sheet(build, work, out):
    """The four icons' sheets at each scale, cropped and captioned."""
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
            sheet = Image.open(written(folder, "review-%s-%d.png" % (theme, percent),
                                       "icon_tests contactSheets")).convert("RGB")
            scale = percent / 100
            rows_of_cells = (len(K1_ROLES) + K1_COLUMNS - 1) // K1_COLUMNS
            expected = (round(K1_COLUMNS * K1_CELL_WIDTH * scale), round(rows_of_cells * K1_CELL_HEIGHT * scale))
            if sheet.size != expected:
                sys.exit("icon_tests contactSheets drew a %d x %d sheet at %d %%, not %d x %d: update K1_COLUMNS "
                         "and K1_CELL_* to its layout" % (sheet.size + (percent,) + expected))
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
                "item at 16 and 32 px in Light and Dark at 100, 150 and 200 % (offscreen, Linux). Each strip is "
                "the four cells of the icon test's contact sheet, with the role names at the sheet's 11 px; the "
                "theme and scale captions are added by the review script. Approve or reject each icon.",
    }]


def k2_items(build, work, out):
    folder = os.path.join(work, "k2")
    run(build, "hikari_ui_theme_tests", ["swatchSheet"], {"HIKARI_THEME_SHEET_DIR": folder})
    shutil.copy(written(folder, "k2-accents.png", "theme_tests swatchSheet"), os.path.join(out, "k2-accents.png"))
    shutil.copy(written(folder, "k2-contrast.txt", "theme_tests swatchSheet"), os.path.join(out, "k2-contrast.txt"))
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
        shutil.copy(written(shots, "main-window-%s.png" % theme, "shell_tests surfaceScreenshots"),
                    os.path.join(out, file))
        items.append({
            "id": "k2-main-window-%s" % theme,
            "card": "K2",
            "title": "K2: main window, %s" % name,
            "file": file,
            "note": "A document, video and audio open, a modified tab, the Grid focused (its header and "
                    "current row ringed); 1600 x 900, offscreen, Linux.",
        })
    return items


def k2_focus_items(build, work, out):
    folder = os.path.join(work, "k2-focus")
    run(build, "hikari_ui_theme_tests", ["panelFocusMarkContrast"], {"HIKARI_THEME_SHEET_DIR": folder})
    shutil.copy(written(folder, "k2-focus-mark-contrast.txt", "theme_tests panelFocusMarkContrast"),
                os.path.join(out, "k2-focus-mark-contrast.txt"))
    shots = os.path.join(work, "focus")
    run(build, "hikari_ui_shell_tests", ["focusScreenshots"], {"HIKARI_SURFACE_SHOT_DIR": shots})
    items = []
    for theme, name in THEMES:
        for view, title, note in FOCUS_VIEWS:
            file = "k2-focus-%s-%s.png" % (view, theme)
            shutil.copy(written(shots, "focus-%s-%s.png" % (view, theme), "shell_tests focusScreenshots"),
                        os.path.join(out, file))
            items.append({
                "id": "k2-focus-%s-%s" % (view, theme),
                "card": "K2",
                "title": "K2 focus: %s, %s" % (title, name),
                "file": file,
                "note": note + " The user's 2026-10-13 decision: panels lose their ring for the accent corner "
                        "mark (k2-focus-mark-contrast.txt has its contrast in every theme and accent); controls "
                        "and the Grid's current row keep a 1-wide ring. 1600 x 900 window, offscreen, Linux.",
            })
    return items


def o5_items(build, work, out):
    folder = os.path.join(work, "o5")
    run(build, "hikari_ui_language_tests", ["screenshots"], {"HIKARI_LANGUAGE_SHOT_DIR": folder})
    items = []
    for language, name in LANGUAGES:
        for view, title, note in LANGUAGE_VIEWS:
            file = "o5-%s-%s.png" % (view, language)
            shutil.copy(written(folder, "%s-%s.png" % (view, language), "language_tests screenshots"),
                        os.path.join(out, file))
            partial = (" English text is expected where the catalog has no translation yet (strings new in "
                       "this application)." if language in PARTIAL_CATALOG else "")
            items.append({
                "id": "o5-%s-%s" % (view, language),
                "card": "O5",
                "title": "O5: %s, %s" % (title, name),
                "file": file,
                "note": note + " Language switched live; offscreen, Linux." + partial,
            })
    return items


def d3_items(build, work, out):
    folder = os.path.join(work, "d3")
    run(build, "hikari_ui_shell_tests", ["dockChromeScreenshots"], {"HIKARI_SURFACE_SHOT_DIR": folder})
    items = []
    for theme, name in THEMES:
        for view, title, note in DOCK_VIEWS:
            file = "d3-%s-%s.png" % (view, theme)
            shutil.copy(written(folder, "dock-%s-%s.png" % (view, theme), "shell_tests dockChromeScreenshots"),
                        os.path.join(out, file))
            items.append({
                "id": "d3-%s-%s" % (view, theme),
                "card": "D3",
                "title": "D3: %s, %s" % (title, name),
                "file": file,
                "note": note + " Offscreen, Linux.",
            })
    return items


PARTS = [("k1", k1_sheet), ("k2", k2_items), ("k2-focus", k2_focus_items), ("o5", o5_items), ("d3", d3_items)]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    build, out = (os.path.abspath(a) for a in sys.argv[1:3])
    parts = sys.argv[3:] or [name for name, _ in PARTS]
    unknown = [p for p in parts if p not in dict(PARTS)]
    if unknown:
        sys.exit("unknown part %s; the parts are %s" % (", ".join(unknown), ", ".join(n for n, _ in PARTS)))
    os.makedirs(out, exist_ok=True)
    items = []
    with tempfile.TemporaryDirectory(prefix="hikari-review-") as work:
        for name, part in PARTS:
            if name in parts:
                items += part(build, work, out)
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(items, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("%d items in %s" % (len(items), out))


if __name__ == "__main__":
    main()
