#!/usr/bin/env python3
"""PU5 #307 visual tool popups mockup: per theme, Edit shapes (legacy "Vector shape editing") and Edit tags
(legacy "Tag editing") as resizable list + detail windows with a live preview, changes kept as they are made,
Undo in place of Apply / OK / Cancel, Restore default in the ⋯ menu, and the questions left as PK1 message
boxes.

Drawn with PK1's mockup kit (kit.py: the theme roles of src/ui/theme.cpp, DG1's frame, the popover frame, the
message box and the toast, IC1's icons from the tree). Shapes are legacy's five defaults plus an example of the
user's own; tags are legacy's 22 definitions under HY1's sections (#339), plus one of the user's own."""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu5"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, field, combo, segmented, window, ic, message_box, toast  # noqa: E402

kit.REPO_ICONS += ["add", "duplicate", "delete", "undo", "redo", "restore-default", "tool-drawing",
                   "tool-all-tags", "tag-edit", "panel-menu", "shape-presets", "pick-lines", "clear", "edit-paste"]


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=520):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def error(text):
    return f'<div class="ferr">{ic("notice-error", 14, "err")}<span>{text}</span></div>'


def hint(text):
    return f'<div class="ghint">{text}</div>'


def bad(f):
    return f.replace('class="fld', 'class="fld bad', 1)


def label(text):
    return f'<span class="glabel">{text}</span>'


def section(title, body, first=False):
    return f'<div class="sec{" first" if first else ""}"><div class="stitle">{title}</div>{body}</div>'


def tool(name, tip, on=False):
    return f'<span class="qb tool{" on" if on else ""}" title="{tip}">{ic(name)}</span>'


def footer(undo_tip=None, redo=False, hover=False):
    tip = (f'<span class="tip" style="left:0;top:-30px">{undo_tip}</span>' if hover and undo_tip else "")
    undo = (f'<span class="b ibtn{"" if undo_tip else " off"}{" hov" if hover else ""}" style="position:relative">'
            f'{ic("undo")}<span>Undo</span>{tip}</span>')
    red = f'<span class="b ibtn{"" if redo else " off"}">{ic("redo")}<span>Redo</span></span>'
    return f'<span class="lead2">{undo}{red}</span>{btn("Close", primary=True, focus=True)}'


def menu(items, style=""):
    rows = []
    for it in items:
        if it is None:
            rows.append('<div class="msep"></div>')
            continue
        text, icon, key = it
        rows.append(f'<div class="mrow">{ic(icon) if icon else "<span class=gut></span>"}<span>{text}</span>'
                    f'<span class="mkey">{key}</span></div>')
    return f'<div class="menu" style="{style}">{"".join(rows)}</div>'


# ---------------------------------------------------------------------------------------------
# Drawings


def to_path(drawing):
    toks = drawing.split()
    out, cmd, nums = [], None, []

    def flush():
        if cmd is None:
            return
        pts = [f"{nums[i]} {nums[i + 1]}" for i in range(0, len(nums) - 1, 2)]
        if cmd in "mn":
            out.append("M " + pts[0] + "".join(" L " + p for p in pts[1:]))
        elif cmd == "l":
            out.append(" ".join("L " + p for p in pts))
        elif cmd == "b":
            for i in range(0, len(pts) - 2, 3):
                out.append(f"C {pts[i]} {pts[i + 1]} {pts[i + 2]}")
        elif cmd == "c":
            out.append("Z")

    for t in toks:
        if re.fullmatch(r"[mnlbspc]", t):
            flush()
            cmd, nums = t, []
        else:
            nums.append(t)
    flush()
    return " ".join(out) + " Z"


def bbox(drawing):
    nums = [float(t) for t in drawing.split() if not re.fullmatch(r"[mnlbspc]", t)]
    xs, ys = nums[0::2], nums[1::2]
    return min(xs), min(ys), max(xs), max(ys)


def star():
    import math
    pts = []
    for i in range(10):
        r = 100 if i % 2 == 0 else 42
        a = -math.pi / 2 + i * math.pi / 5
        pts.append(f"{round(r * math.cos(a))} {round(r * math.sin(a))}")
    return "m " + pts[0] + " l " + " ".join(pts[1:])


SHAPES = [
    ("rectangle", "m 0 0 l 100 0 100 100 0 100", 0),
    ("circle", "m -100 -100 b -45 -155 45 -155 100 -100 b 155 -45 155 45 100 100 b 46 155 -45 155 -100 100 "
               "b -155 45 -155 -45 -100 -100", 1),
    ("rounded square 1", "m -100 -25 b -100 -92 -92 -100 -25 -100 l 25 -100 b 92 -100 100 -92 100 -25 l 100 25 "
                         "b 100 92 92 100 25 100 l -25 100 b -92 100 -100 92 -100 25 l -100 -25", 1),
    ("rounded square 2", "m -100 -60 b -100 -92 -92 -100 -60 -100 l 60 -100 b 92 -100 100 -92 100 -60 l 100 60 "
                         "b 100 92 92 100 60 100 l -60 100 b -92 100 -100 92 -100 60 l -100 -60", 1),
    ("rounded square 3", "m -100 -85 b -100 -96 -96 -100 -85 -100 l 85 -100 b 96 -100 100 -96 100 -85 l 100 85 "
                         "b 100 96 96 100 85 100 l -85 100 b -96 100 -100 96 -100 85 l -100 -85", 1),
    ("sign star", star(), 1),
]


def thumb(drawing, size=20):
    x0, y0, x1, y1 = bbox(drawing)
    pad = (x1 - x0) * 0.08
    vb = f"{x0 - pad} {y0 - pad} {x1 - x0 + 2 * pad} {y1 - y0 + 2 * pad}"
    return (f'<svg class="thumb" width="{size}" height="{size}" viewBox="{vb}">'
            f'<path d="{to_path(drawing)}" vector-effect="non-scaling-stroke"/></svg>')


def preview(drawing, size=268, empty=False):
    x0, y0, x1, y1 = bbox(drawing)
    span = max(x1 - x0, y1 - y0, 1) * 1.5
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    # Keep the cursor point (0, 0) in view.
    lo_x, hi_x = min(cx - span / 2, -span * 0.12), max(cx + span / 2, span * 0.12)
    lo_y, hi_y = min(cy - span / 2, -span * 0.12), max(cy + span / 2, span * 0.12)
    s = max(hi_x - lo_x, hi_y - lo_y)
    vb = f"{lo_x} {lo_y} {s} {s}"
    grid = "".join(f'<path d="M {g} {lo_y} V {lo_y + s} M {lo_x} {g} H {lo_x + s}"/>'
                   for g in range(int(lo_x // 50) * 50, int(lo_x + s) + 50, 50))
    k = s / 24
    cross = (f'<path class="cur" d="M {-k} 0 H {k} M 0 {-k} V {k}"/>'
             f'<circle class="cur" cx="0" cy="0" r="{k * 0.35}"/>')
    shape = "" if empty else f'<path class="shp" d="{to_path(drawing)}"/>'
    size_txt = "" if empty else f'{round(x1 - x0)} × {round(y1 - y0)}'
    note = ('<span class="pnote">The last drawing stays in the list until this one is valid</span>' if empty
            else f'<span class="pnote">{size_txt}</span>')
    return (f'<div class="pv"><svg width="{size}" height="{size}" viewBox="{vb}">'
            f'<g class="grid">{grid}</g>{shape}{cross}</svg>{note}</div>')


# ---------------------------------------------------------------------------------------------
# Edit shapes


def shape_list(sel, rename=None):
    rows = []
    for i, (n, d, _) in enumerate(SHAPES):
        name = rename if (i == sel and rename) else n
        own = '<span class="own">yours</span>' if n == "sign star" else ""
        rows.append(f'<div class="lrow{" cur" if i == sel else ""}">{thumb(d)}<span>{name}</span>{own}</div>')
    tools = (tool("add", "New shape") + tool("duplicate", "Duplicate") + tool("delete", "Delete")
             + '<span class="sp"></span>' + tool("panel-menu", "More"))
    return f'<div class="side"><div class="llist2">{"".join(rows)}</div><div class="ltools">{tools}</div></div>'


def shapes_window(sel=2, state="default"):
    name, drawing, mode = SHAPES[sel]
    empty = state == "empty"
    nm = field(name if state != "clash" else "circle", 220, focus=state == "clash")
    if state == "clash":
        nm = bad(nm) + error("Another shape is named “circle”. Press Enter to replace it.")
    text = "" if empty else drawing
    area = (f'<div class="area mono{" focus bad" if empty else ""}">{text or "<span class=caret></span>"}</div>')
    take = f'<span class="b sm ibtn">{ic("tool-drawing")}<span>Take from the active line</span></span>'
    seg = segmented(["Width and height", "Keep aspect ratio", "Width only"],
                    ["Width and height", "Keep aspect ratio", "Width only"][mode], small=True)
    detail = (section("Name", f'<div class="gcol">{nm}</div>', first=True)
              + section("Scale from the cursor", seg)
              + section("Drawing", f'{area}' + (error("A shape needs a drawing.") if empty else "")
                        + f'<div class="gline" style="margin-top:8px">{take}</div>'))
    body = (f'<div class="split">{shape_list(sel)}<div class="detail">{detail}</div>'
            f'<div class="pcol"><div class="stitle">Preview</div>{preview(drawing, empty=empty)}</div></div>')
    undo = {"default": None, "empty": "Undo “Edit drawing”", "clash": "Undo “Rename shape”"}[state]
    return window("Edit shapes", body, footer(undo, hover=state == "empty"), 1000, resizable=True)


def shapes_menu_window():
    w = shapes_window(5, "default")
    m = menu([("Restore default shapes…", "restore-default", ""), None,
              ("Undo all changes", "undo", "")], "position:absolute;left:170px;top:396px;width:230px")
    return f'<div style="position:relative">{w}{m}</div>'


def shapes_page(key):
    th = THEMES[key]
    clash = message_box("Edit shapes", "Replace the shape “circle”?",
                        "“rounded square 1” takes its name and its place in the list; the old circle is "
                        "removed. Undo brings it back while this window is open.", "warning",
                        btn("Replace", danger=True) + btn("Keep editing", primary=True, focus=True))
    restore = message_box("Edit shapes", "Restore the default shapes?",
                          "Your 1 shape is removed and legacy’s five shapes come back. Undo brings yours back "
                          "while this window is open.", "warning",
                          btn("Restore defaults", danger=True) + btn("Cancel", primary=True, focus=True))
    return (
        f'<h1>PU5 #307 · Edit shapes (legacy “Vector shape editing”) · {th["name"]}</h1>'
        '<p class="note">1 of 2. Opened by the drawing tool’s Shape list ▸ “Edit shapes…” in the inspector bar '
        '(VT1). A resizable window on DG1’s frame, modeless: the list on the left, the shape on the right, its '
        'preview beside it. Every change is kept as it is made: the drawing tool’s Shape list follows at once '
        'and the file is written. Undo and Redo step back through this window’s changes (Ctrl+Z, Ctrl+Shift+Z); '
        'Close and Esc keep them. Apply, OK, Cancel and the New shape name field go.</p>'
        '<h2>As it opens, on the shape the tool has</h2>'
        + row(col(shapes_window(2),
                  "The list shows each shape drawn small. Under it: New shape, Duplicate, Delete and ⋯ (icon "
                  "buttons, the name on hover). <b>New shape</b> starts from the active Line’s drawing when it "
                  "has one, else a 100 × 100 square, named “New shape” (“New shape 2”…) with the name selected "
                  "to type over. <b>Delete</b> asks nothing (Undo); it is greyed on the last shape, its tooltip "
                  "saying why. <b>Scale from the cursor</b> is legacy’s “Scaling relative to cursor” as three "
                  "buttons; the “Scaling mode” box legacy always greyed is not shown (its stored value is kept). "
                  "<b>Take from the active line</b> puts the active Line’s \\p drawing in this shape (legacy "
                  "made an “Untitled” one; New shape does that now). The preview draws the shape around the "
                  "point you click with the tool (the cross), at the size it is drawn, on a 50-unit grid.", 1000))
        + '<h2>While typing: an empty drawing, and the Undo tooltip</h2>'
        + row(col(shapes_window(2, "empty"),
                  "A value the tool cannot use is said under its field in danger, and the shape keeps its last "
                  "good value meanwhile (legacy’s “Field \"shape\" cannot be empty.” box after Apply). Leaving "
                  "the shape or closing the window with the field still empty puts the last drawing back in "
                  "it. Undo’s tooltip names the step it undoes.", 1000))
        + '<h2>A name another shape has</h2>'
        + row(col(shapes_window(2, "clash"),
                  "Typing a name another shape has (ignoring case) says so under the field; the shape keeps "
                  "its old name. Enter (or leaving the field) asks the question on the right.", 1000))
        + row(col(clash,
                  "The #55 clash box as a PK1 message box: titled with the window, warning, Replace in danger, "
                  "Keep editing the default (back to the name, selected). Legacy kept both shapes.", 468),
              col(restore,
                  "The ⋯ menu’s Restore default shapes… asks first: Cancel the default, the count of the "
                  "user’s own shapes that go. Legacy’s “Are you sure you want to reset to default?” with "
                  "Yes / No. It is one Undo step too.", 468), top=24)
        + '<h2>The ⋯ menu</h2>'
        + row(col(shapes_menu_window(),
                  "⋯ holds Restore default shapes… and Undo all changes (back to how the shapes were when the "
                  "window opened, one Undo step). Here on the user’s own shape.", 1000))
    )


# ---------------------------------------------------------------------------------------------
# Edit tags

COMMON = [("blur", "blur"), ("blur edge", "be"), ("border", "bord"), ("shadow", "shad"), ("font size", "fs"),
          ("spacing", "fsp"), ("alpha", "alpha"), ("color1", "1c"), ("color3", "3c")]
MORE = [("1a", "1a"), ("3a", "3a"), ("4a", "4a"), ("color4", "4c"), ("fading", "fad"), ("fax", "fax"),
        ("fay", "fay"), ("xborder", "xbord"), ("yborder", "ybord"), ("xshadow", "xshad"), ("yshadow", "yshad"),
        ("position", "pos"), ("tanimation", "t")]
YOURS = [("glow edge", "be")]


def tag_row(n, code, sel, query=""):
    shown = n
    if query and query in n:
        i = n.index(query)
        shown = f'{n[:i]}<b>{query}</b>{n[i + len(query):]}'
    return (f'<div class="lrow trow{" cur" if n == sel else ""}"><span>{shown}</span>'
            f'<span class="code">\\{code}</span></div>')


def tag_list(sel, query="", more_open=False):
    if query:
        hits = [(n, c) for n, c in COMMON + MORE + YOURS if query in n or query in c]
        rows = "".join(tag_row(n, c, sel, query) for n, c in hits)
    else:
        rows = (f'<div class="shead">{ic("chevron-down", 14)}Common</div>'
                + "".join(tag_row(n, c, sel) for n, c in COMMON)
                + f'<div class="shead">{ic("chevron-down" if more_open else "chevron-right", 14)}More'
                  f'<span class="cnt">{len(MORE)}</span></div>'
                + ("".join(tag_row(n, c, sel) for n, c in MORE) if more_open else "")
                + f'<div class="shead">{ic("chevron-down", 14)}Your tags</div>'
                + "".join(tag_row(n, c, sel) for n, c in YOURS))
    search = field(query or "Search tags", None, placeholder=not query, focus=bool(query), icon="search")
    search = search.replace('class="fld', 'class="fld wide', 1)
    tools = (tool("add", "New tag") + tool("duplicate", "Duplicate") + tool("delete", "Delete")
             + '<span class="sp"></span>' + tool("panel-menu", "More"))
    return (f'<div class="side tags"><div class="lsearch">{search}</div><div class="llist2">{rows}</div>'
            f'<div class="ltools">{tools}</div></div>')


CHANGES = ["Set", "Add", "Multiply", "Gradient"]
VARIANTS = {"Set": ["At the cursor", "At the start of the text", "Clip rectangle with \\t animation"],
            "Multiply": ["Set by line number", "Add by line number"],
            "Gradient": ["Across characters, increasing", "Across characters, decreasing",
                         "Across lines, increasing", "Across lines, decreasing"]}


def num(text, width=72, focus=False, invalid=False):
    f = field(text, width, focus=focus, mono=True)
    return bad(f) if invalid else f


def vfield(lab, text, width=64):
    return f'<span class="vlab">{lab}</span>{num(text, width)}'


def strip(tag, code, values, change, variant):
    vals = "".join(f'<span class="vlab">{lab}</span><span class="sfld">{v}</span>' for lab, v in values)
    segs = "".join(f'<span class="{"on" if s == change else ""}">{s}</span>' for s in CHANGES)
    var = (f'<span class="scombo">{variant}{ic("chevron-down", 14)}</span>' if variant else "")
    return (f'<div class="strip"><span class="stool">{ic("tool-all-tags")}<b>Hydra</b></span><i></i>'
            f'<span class="vlab">Tag</span><span class="scombo tp">{ic("search", 14)}{tag}'
            f'<span class="code">\\{code}</span>{ic("chevron-down", 14)}</span><i></i>{vals}<i></i>'
            f'<span class="vlab">Change</span><span class="sseg">{segs}</span>{var}'
            '</div>')


def tags_window(sel="blur", state="default"):
    if sel == "blur":
        name, code, lo, hi, step, digits = "blur", "blur", "0", "100", "0.5", "1"
        values, change, variant = [("Value", "0")], "Set", "At the cursor"
        writes = '{<mark>\\blur0</mark>}I’ll be waiting at the station.'
    else:
        name, code, lo, hi, step, digits = "color1", "1c", "0", "255", "1", "0"
        values, change, variant = [("R", "250"), ("G", "214"), ("B", "92")], "Set", "At the cursor"
        writes = '{<mark>\\1c&amp;H5CD6FA&amp;</mark>}I’ll be waiting at the station.'
    invalid = state == "range"
    count = segmented(["1", "2", "3", "4"], str(len(values)), small=True)
    vals = "".join(vfield(lab, v) for lab, v in values)
    tag_f = (f'<span class="fld mono" style="width:150px"><span class="pre">\\</span>'
             f'<span class="ftext">{code}</span></span>')
    rng = (f'<div class="gline">{num(lo)}<span class="dash">to</span>'
           f'{num("-5" if invalid else hi, focus=invalid, invalid=invalid)}</div>')
    if invalid:
        rng += error("The maximum must be greater than the minimum (0). The tag keeps 0 to 255.")
    seg = segmented(CHANGES, change, small=True)
    var = combo(variant, 230)
    detail = (
        section("Tag", f'<div class="grid">{label("Name")}{field(name, 210)}{label("Tag")}{tag_f}</div>',
                first=True)
        + section("Values", f'<div class="grid">{label("Range")}<div class="gcol">{rng}</div>'
                            f'{label("Step")}<div class="gline">{num(step)}<span class="vlab" '
                            f'style="margin-left:14px">Decimal places</span>{num(digits, 48)}</div>'
                            f'{label("Count")}<div class="gline">{count}</div>'
                            f'{label("Starts at")}<div class="gline">{vals}</div></div>')
        + section("Change", f'<div class="grid">{label("Starts on")}<div class="gcol">{seg}'
                            f'<div class="gline">{var}</div></div></div>'))
    pv = (f'<div class="stitle">Preview</div>'
          f'<div class="pvt"><div class="pvl">In the inspector bar</div>'
          f'{strip(name, code, values, change, variant)}'
          f'<div class="pvl" style="margin-top:12px">Set writes, on the active line (#14)</div>'
          f'<div class="wline mono">{writes}</div></div>')
    body = (f'<div class="split">{tag_list(sel)}<div class="detail wide">{detail}'
            f'<div class="sec">{pv}</div></div></div>')
    undo = "Undo “Edit maximum”" if invalid else None
    return window("Edit tags", body, footer(undo), 1060, resizable=True)


def tags_menu():
    return menu([("Restore default tags…", "restore-default", ""), None, ("Undo all changes", "undo", "")],
                "width:230px")


def tags_page(key):
    th = THEMES[key]
    clash = message_box("Edit tags", "Replace the tag “shadow”?",
                        "“glow edge” takes its name and its place in the list; the old shadow is removed. Undo "
                        "brings it back while this window is open.", "warning",
                        btn("Replace", danger=True) + btn("Keep editing", primary=True, focus=True))
    restore = message_box("Edit tags", "Restore the default tags?",
                          "Your 1 tag is removed and legacy’s 22 come back, with their ranges and steps. Undo "
                          "brings yours back while this window is open.", "warning",
                          btn("Restore defaults", danger=True) + btn("Cancel", primary=True, focus=True))
    search = (f'<div class="side tags solo">' + tag_list("shadow", "sha").split('<div class="side tags">', 1)[1])
    return (
        f'<h1>PU5 #307 · Edit tags (legacy “Tag editing”) · {th["name"]}</h1>'
        '<p class="note">2 of 2. Opened by Hydra’s tag list ▸ “Edit tags…” (HY1 #339) in the inspector bar. '
        'The same window as Edit shapes: the list on the left in HY1’s order and sections, the definition on '
        'the right in three sections, the preview under them. Every change is kept as it is made (Hydra’s '
        'strip follows at once, the file is written); Undo, Redo, Close. Legacy’s Apply, OK, Cancel, the New '
        'tag name field and its four error boxes go.</p>'
        '<h2>As it opens, on the tag Hydra has</h2>'
        + row(col(tags_window("blur"),
                  "<b>The list</b> is the strip’s: each definition’s tag beside its name, Common, More (folded, "
                  "with its count) and Your tags, the search field filtering by name or tag. <b>Tag</b> is "
                  "typed without its backslash. <b>Values</b>: the range, step and decimal places as legacy’s; "
                  "<b>Count</b>, how many values the tag takes, is 1–4 (legacy’s “No / One / Two / Three additional values”), "
                  "and <b>Starts at</b> holds them, named as the strip names them (Value; R G B; X Y; In Out; "
                  "Start End; else 1–4). <b>Change ▸ Starts on</b> is the change option the tag brings to the "
                  "strip, in HY1’s words: the switch and the choice beside it (legacy’s eight options and the "
                  "three placings, the same values in the file). <b>Preview</b>: the strip as Hydra will show "
                  "this tag, and what Set writes at the starting values on the active Line (or the first "
                  "Line’s text when none is active).", 1060))
        + '<h2>Several values, and a range that cannot be used</h2>'
        + row(col(tags_window("color1", "range"),
                  "color1 takes three values, R G B. A maximum under the minimum is said under the field "
                  "(legacy’s “Field \"Maximum value\" must contain a value greater than field \"Minimum "
                  "value\".” box), the definition keeps its last good range meanwhile, and leaving the tag puts "
                  "it back in the field. The step’s two checks (more than zero; not too large for the range) "
                  "and an empty Tag show the same way.", 1060))
        + '<h2>Search, and the questions</h2>'
        + row(col(search,
                  "Typing filters every section at once; Enter takes the first match, Down moves into the "
                  "list. <b>New tag</b> adds “New tag” with an empty Tag field focused (it reaches the strip "
                  "once it has a tag); Duplicate copies the definition as “shadow copy”.", 300),
              col(clash,
                  "A name another definition has (ignoring case): the same message box as Edit shapes. Legacy "
                  "kept both, and the strip showed the first.", 468),
              col(tags_menu() + '<div style="height:16px"></div>' + restore,
                  "⋯: Restore default tags… (asks, Cancel the default) and Undo all changes. Legacy’s “Are you "
                  "sure you want to reset to default?” with Yes / No.", 468), top=0)
    )


# ---------------------------------------------------------------------------------------------

EXTRA_CSS = """
.split{display:flex;gap:0;min-height:%(h)s;margin-top:4px;outline:1px solid %(line)s}
.side{width:220px;flex:none;display:flex;flex-direction:column;background:%(field)s;box-shadow:inset -1px 0 %(line)s}
.side.tags{width:250px}
.side.solo{height:300px;outline:1px solid %(line)s;box-shadow:none}
.lsearch{padding:8px 8px 4px}
.llist2{flex:1;overflow:hidden;padding:4px 0}
.lrow{display:flex;gap:10px;align-items:center;height:30px;padding:0 10px}
.lrow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s;font-weight:650}
.trow{height:26px;padding-left:28px}
.trow .code{margin-left:auto}
.code{font:11px 'Liberation Mono',Consolas,monospace;color:%(muted)s;white-space:nowrap}
.shead{display:flex;align-items:center;gap:4px;height:26px;padding:0 8px;font-weight:650}
.shead .cnt{margin-left:auto;color:%(muted)s;font-weight:400;font-size:12px}
.own{margin-left:auto;font-size:11px;color:%(muted)s}
.thumb{flex:none}
.thumb path{fill:%(select)s;stroke:%(text)s;stroke-width:1}
.ltools{display:flex;align-items:center;gap:2px;padding:4px 6px;box-shadow:inset 0 1px %(line)s,inset -1px 0 %(line)s}
.ltools .sp{flex:1}
.qb.tool{width:26px;height:26px}
.detail{flex:1;min-width:0;padding:4px 20px 16px;background:%(panel)s}
.detail.wide{padding-right:24px}
.pcol{width:300px;flex:none;padding:14px 16px;box-shadow:inset 1px 0 %(line)s;background:%(panel)s}
.sec{padding:14px 0 14px;box-shadow:inset 0 1px %(line)s}
.sec.first{box-shadow:none}
.stitle{font-weight:650;margin-bottom:10px}
.grid{display:grid;grid-template-columns:96px 1fr;column-gap:12px;row-gap:10px;align-items:start}
.glabel{height:28px;display:flex;align-items:center;color:%(text)s}
.gcol{display:flex;flex-direction:column;gap:6px;min-width:0}
.gline{display:flex;align-items:center;gap:6px;min-height:28px;flex-wrap:wrap}
.ghint{color:%(muted)s;font-size:12px}
.mono,.fld.mono .ftext{font-family:'Liberation Mono',Consolas,monospace;font-size:12px}
.fld .pre{margin-right:-4px;color:%(muted)s;font:12px 'Liberation Mono',Consolas,monospace}
.fld.bad,.area.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s}
.ferr{display:flex;align-items:center;gap:6px;color:%(danger)s;font-size:12px;margin-top:6px}
.area{height:118px;padding:8px 10px;background:%(field)s;border:1px solid %(line)s;border-radius:1px;line-height:1.6;
      word-break:break-word;overflow:hidden}
.area.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.area.focus.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s}
.caret{display:inline-block;width:1px;height:16px;background:%(text)s;vertical-align:middle}
.ibtn{gap:6px}
.seg{align-self:flex-start}
.seg span{white-space:nowrap}
.b.ibtn{min-width:0}
.b.off{opacity:.46}
.b.hov{border-color:%(accent)s;background:%(select)s}
.lead2{display:flex;gap:8px;margin-right:auto}
.tip{position:absolute;white-space:nowrap;background:%(raised)s;outline:1px solid %(line)s;padding:3px 8px;
     font-size:12px;color:%(text)s;font-weight:400}
.pv{display:flex;flex-direction:column;align-items:center;gap:8px}
.pv svg{background:%(field)s;outline:1px solid %(line)s}
.pv .grid path{stroke:%(line)s;stroke-width:.6;opacity:.6;fill:none;vector-effect:non-scaling-stroke}
.pv .shp{fill:%(select)s;stroke:%(accent)s;stroke-width:2;vector-effect:non-scaling-stroke}
.pv .cur{stroke:%(danger)s;fill:none;stroke-width:1.5;vector-effect:non-scaling-stroke}
.pnote{color:%(muted)s;font-size:12px;text-align:center;max-width:268px}
.dash{color:%(muted)s;margin:0 4px}
.vlab{color:%(muted)s;margin:0 2px 0 6px}
.vlab:first-child{margin-left:0}
.menu{position:relative;background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;
      box-shadow:0 10px 30px rgba(0,0,0,.33);z-index:3}
.mrow{display:flex;align-items:center;gap:10px;height:28px;padding:0 14px 0 10px}
.mrow:first-child{background:%(select)s}
.mrow .gut{width:16px}
.mkey{margin-left:auto;color:%(muted)s}
.msep{height:1px;background:%(line)s;margin:4px 0}
.pvt{display:flex;flex-direction:column}
.pvl{color:%(muted)s;font-size:12px;margin-bottom:6px}
.strip{display:flex;align-items:center;gap:6px;row-gap:6px;flex-wrap:wrap;min-height:38px;padding:6px 10px;background:%(panel)s;
       outline:1px solid %(line)s;white-space:nowrap;font-size:12px}
.strip>i{width:1px;height:18px;background:%(line)s;margin:0 4px}
.stool{display:flex;align-items:center;gap:6px}
.stool .ic{color:%(text)s}
.scombo{display:inline-flex;align-items:center;gap:6px;height:24px;padding:0 6px;background:%(raised)s;
        border:1px solid %(control)s;border-radius:2px}
.scombo.tp .code{margin-left:6px}
.sfld{display:inline-flex;align-items:center;justify-content:flex-end;width:50px;height:24px;padding:0 6px;
      background:%(field)s;border:1px solid %(line)s;font:12px 'Liberation Mono',Consolas,monospace}
.sseg{display:inline-flex;border:1px solid %(line)s}
.sseg span{padding:0 8px;height:22px;display:flex;align-items:center;background:%(raised)s;border-left:1px solid %(line)s}
.sseg span:first-child{border-left:none}
.sseg span.on{background:%(select)s;box-shadow:inset 0 -2px %(accent)s;font-weight:650}
.stgt{margin-left:auto;padding-left:12px;font-weight:650}
.wline{padding:8px 10px;background:%(field)s;outline:1px solid %(line)s}
.wline mark{background:%(select)s;color:%(text)s;outline:1px solid %(accent)s}
"""


def page(key, content, h):
    th = THEMES[key]
    html = kit.page(key, content)
    focus = th["accent"] if key not in kit.HC else th["text"]
    return html.replace("</style>", EXTRA_CSS % dict(th, h=h, focus=focus) + "</style>", 1)


def main():
    for key in THEMES:
        (HERE / f"pu5-{key}-1-shapes.html").write_text(page(key, shapes_page(key), "470px"))
        (HERE / f"pu5-{key}-2-tags.html").write_text(page(key, tags_page(key), "560px"))


main()
