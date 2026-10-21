#!/usr/bin/env python3
"""AD1 #310 automation script dialog mockup: per theme, three sheets (layout and size, controls, keys and
results). Usage: gen.py <tree> (a HikariSub checkout, for IC1's icons).

Drawn from the theme layer's roles (src/ui/theme.cpp baseRoles, default accent), DG1's dialog frame
(DialogWindow.qml), PK1's kit pieces (its approved mockup, evidence/w6-pk1-mockup gen.py: fields, popover,
buttons) and the colour popover's compact layout (ColourPopover.qml). Script names, fields and values are
examples modelled on real scripts."""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-ad1")
OUT = pathlib.Path(__file__).resolve().parent

THEMES = {
    "light": dict(code="light", name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", press="#DDE2E7", warning="#8A5A00",
                  danger="#A51F31"),
    "dark": dict(code="dark", name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", press="#343E49", warning="#E0A030",
                 danger="#FFADAD"),
    "hcwhite": dict(code="highContrastWhite", name="High contrast white", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    press="#C9DAF8", warning="#6B4500", danger="#A00000"),
    "hcblack": dict(code="highContrastBlack", name="High contrast black", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    press="#253F60", warning="#FFD54A", danger="#FFADAD"),
}
HC = {"hcwhite", "hcblack"}
DARKISH = {"dark", "hcblack"}

GLYPHS = {
    "chevron-up": '<path d="M4.5 9.5l3.5-3.5 3.5 3.5"/>',
    "check": '<path d="M3.5 8.5l3 3 6-7"/>',
    "pointer": '<path d="M3 1.5v11l3-2.5 2 4.5 2-1-2-4.3h4z" fill="currentColor" stroke="currentColor"/>',
}
REPO_ICONS = ["about", "search", "window-close", "window-maximize", "chevron-down", "eyedropper", "notice-error"]


def icon_svg(name):
    svg = (TREE / "src/ui/icons" / f"{name}.svg").read_text()
    return re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip()).replace('<g id="accent">', '<g class="acc">')


def sprite():
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    syms = {**GLYPHS, **{n: icon_svg(n) for n in REPO_ICONS}}
    body = "".join(f'<symbol id="i-{n}" viewBox="0 0 16 16"><g {attrs}>{s}</g></symbol>' for n, s in syms.items())
    return f'<svg width="0" height="0" style="position:absolute"><defs>{body}</defs></svg>'


def ic(name, size=16, cls=""):
    return f'<svg class="ic {cls}" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


def mn(text, show):
    """A label's `&` mnemonic: the letter underlined while Alt is held (`show`), the `&` never drawn."""
    return re.sub(r"&(.)", (r'<u>\1</u>' if show else r"\1"), text)


# ---------------------------------------------------------------------------------------------
# DG1 frame, PK1 pieces


def titlebar(title, resizable=True, inactive=False):
    btns = ((f'<span class="wb">{ic("window-maximize")}</span>' if resizable else "")
            + f'<span class="wb">{ic("window-close")}</span>')
    return f'<div class="dtb{" off" if inactive else ""}"><span class="dtitle">{title}</span><span class="wbs">{btns}</span></div>'


def btn(text, primary=False, focus=False, small=False, alt=False, hover=False):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" sm" if small else "") + (
        " hov" if hover else "")
    return f'<span class="{cls}">{mn(text, alt)}</span>'


def box(on):
    mark = ('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
            'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
    return f'<span class="box{" on" if on else ""}">{mark if on else ""}</span>'


def popover(body, width, style="", cls=""):
    return f'<div class="pop {cls}" style="width:{width}px;{style}">{body}</div>'


def tooltip(text, style):
    return f'<span class="tip" style="{style}">{text}</span>'


def pointer(style):
    return f'<span class="ptr" style="{style}">{ic("pointer", 16)}</span>'


# ---------------------------------------------------------------------------------------------
# The script's controls. Each takes the spec's own keys plus a drawing state.


def c_label(c, alt=False, **_):
    return f'<span class="lab">{mn(c["label"], alt)}</span>'


def c_edit(c, focus=False, mono=False, bad=False, **_):
    cls = "fld" + (" focus" if focus else "") + (" mono" if mono else "") + (" bad" if bad else "")
    caret = '<span class="caret"></span>' if focus else ""
    return f'<span class="{cls}"><span class="ftext">{c.get("text", "")}{caret}</span></span>'


def c_textbox(c, focus=False, **_):
    lines = "<br>".join(c.get("text", "").split("\n"))
    caret = '<span class="caret"></span>' if focus else ""
    return f'<span class="fld area{" focus" if focus else ""}"><span class="atext">{lines}{caret}</span></span>'


def spin_arrows(enabled=True):
    return (f'<span class="arrows{"" if enabled else " dis"}"><span>{ic("chevron-up", 12)}</span>'
            f'<span>{ic("chevron-down", 12)}</span></span>')


def c_num(c, focus=False, bad=False, typed=None, empty=False, **_):
    """intedit, or floatedit: spin arrows unless it is a floatedit without a step."""
    arrows = c["class"] == "intedit" or c.get("step")
    cls = "fld num" + (" focus" if focus else "") + (" bad" if bad else "") + ("" if arrows else " noarrows")
    caret = '<span class="caret"></span>' if focus else ""
    if empty:
        lo, hi = c.get("min"), c.get("max")
        txt = f'<span class="phd">{lo} – {hi}</span>{caret}'
    else:
        txt = f'{typed if typed is not None else c["value"]}{caret}'
    return f'<span class="{cls}"><span class="ftext">{txt}</span>{spin_arrows() if arrows else ""}</span>'


def c_check(c, focus=False, alt=False, hover=False, **_):
    return (f'<span class="cb{" ring" if focus else ""}{" hov" if hover else ""}">{box(c.get("value"))}'
            f'<span class="cbt">{mn(c["label"], alt)}</span></span>')


def c_drop(c, focus=False, open_=False, **_):
    cls = "fld drop" + (" focus" if focus else "") + (" on" if open_ else "")
    return (f'<span class="{cls}"><span class="ftext">{c.get("value", "")}</span>'
            f'<span class="ftail">{ic("chevron-down", 14)}</span></span>')


def swatch(rgb, alpha=None, big=False):
    a = 1 if alpha is None else (255 - alpha) / 255
    r, g, b = rgb
    return (f'<span class="sw{" big" if big else ""}"><span style="background:rgba({r},{g},{b},{a:.3f})"></span>'
            f'</span>')


def c_colour(c, focus=False, text_focus=False, bad=False, typed=None, open_=False, **_):
    rgb = c["rgb"]
    alpha = c.get("alpha") if c["class"] == "coloralpha" else None
    value = typed if typed is not None else c["value"]
    caret = '<span class="caret"></span>' if text_focus else ""
    sw = (f'<span class="swb{" ring" if focus else ""}{" on" if open_ else ""}">{swatch(rgb, alpha)}</span>')
    fcls = "fld mono hex" + (" focus" if text_focus else "") + (" bad" if bad else "")
    return f'<span class="colour">{sw}<span class="{fcls}"><span class="ftext">{value}{caret}</span></span></span>'


DRAW = {"label": c_label, "edit": c_edit, "textbox": c_textbox, "intedit": c_num, "floatedit": c_num,
        "checkbox": c_check, "dropdown": c_drop, "color": c_colour, "coloralpha": c_colour}


def grid(controls, states=None, cols=None, row_h=None, grow_rows=(), tab_order=False):
    """The script's grid: x, y, width, height per control (Aegisub's grid-bag). Columns holding an edit,
    a textbox or a dropdown grow; rows holding a textbox grow. `states` maps a control name to drawing
    state keywords."""
    states = states or {}
    ncols = max(c["x"] + c.get("w", 1) for c in controls)
    growing = [c for c in controls if c["class"] in ("edit", "textbox", "dropdown", "color", "coloralpha")]
    grow = {c["x"] for c in growing if c.get("w", 1) == 1}
    for c in growing:  # a spanning control grows its columns only when none of them grows already
        span = set(range(c["x"], c["x"] + c.get("w", 1)))
        if not span & grow:
            grow |= span
    cols = cols or " ".join("minmax(min-content,1fr)" if i in grow else "auto" for i in range(ncols))
    nrows = max(c["y"] + c.get("h", 1) for c in controls)
    rows = " ".join(("minmax(28px,1fr)" if r in grow_rows else "auto") for r in range(nrows))
    cells = []
    order = sorted([c for c in controls if c["class"] != "label"], key=lambda c: (c["y"], c["x"]))
    for c in controls:
        st = states.get(c.get("name") or c.get("label", "").replace("&", ""), {})
        inner = DRAW[c["class"]](c, **st)
        badge = ""
        if tab_order and c["class"] != "label":
            badge = f'<span class="tord">{order.index(c) + 1}</span>'
        just = "start" if c["class"] in ("label", "checkbox") else "stretch"
        cells.append(f'<div class="cell {c["class"]}" style="grid-column:{c["x"] + 1}/span {c.get("w", 1)};'
                     f'grid-row:{c["y"] + 1}/span {c.get("h", 1)};justify-self:{just}">{inner}{badge}</div>')
    return f'<div class="sgrid" style="grid-template-columns:{cols};grid-template-rows:{rows}">{"".join(cells)}</div>'


def hint_strip(text, lines=1):
    if text is None:
        return ""
    return f'<div class="hints" style="min-height:{8 + 18 * lines}px"><span>{text}</span></div>'


def script_dialog(title, controls, buttons, *, width=None, height=None, states=None, hint="", hint_lines=1,
                  default=0, focus_button=None, alt=False, scroll=None, cols=None, grow_rows=(), extra="",
                  tab_order=False, hover_button=None, style=""):
    """A script's dialog on DG1's frame: the grid (scrolling when capped), the hint strip, the footer."""
    w = f"width:{width}px;" if width else ""
    h = f"height:{height}px;" if height else ""
    body = grid(controls, states, cols, grow_rows=grow_rows, tab_order=tab_order)
    sbar = ""
    if scroll:
        top, size = scroll
        sbar = f'<span class="vbar"><span style="top:{top}%;height:{size}%"></span></span>'
    foot = "".join(btn(b, primary=(i == default), focus=(i == focus_button), alt=alt, hover=(i == hover_button))
                   for i, b in enumerate(buttons))
    return (f'<div class="dlg" style="{w}{h}{style}">{titlebar(title)}'
            f'<div class="dscroll{" capped" if scroll else ""}"><div class="dcontent">{body}</div>{sbar}</div>'
            f'{hint_strip(hint, hint_lines)}<div class="dfoot">{foot}</div>{extra}</div>')


# ---------------------------------------------------------------------------------------------
# Example dialogs (modelled on lyger's GradientEverything, unanimated's HYDRA, arch1t3cht's GitSigns)


def gradient(**kw):
    c = [
        dict(**{"class": "label"}, x=0, y=0, label="&Mode"),
        dict(**{"class": "dropdown"}, name="mode", x=1, y=0, w=3, value="Character",
             hint="How the gradient runs: per character, per Line, or across the clip."),
        dict(**{"class": "label"}, x=0, y=1, label="&Tags"),
        dict(**{"class": "edit"}, name="tags", x=1, y=1, w=3, text="\\c\\3c\\alpha\\blur",
             hint="The tags to gradient, in one block."),
        dict(**{"class": "label"}, x=0, y=2, label="&Start"),
        dict(**{"class": "color"}, name="c1", x=1, y=2, value="#E83A5F", rgb=(232, 58, 95),
             hint="The colour at the start of the gradient."),
        dict(**{"class": "label"}, x=2, y=2, label="&End"),
        dict(**{"class": "coloralpha"}, name="c2", x=3, y=2, value="#3A7BE880", rgb=(58, 123, 232), alpha=0x80,
             hint="The colour and alpha at the end of the gradient."),
        dict(**{"class": "label"}, x=0, y=3, label="&Accel"),
        dict(**{"class": "floatedit"}, name="accel", x=1, y=3, value="1.25", min=0.1, max=10, step=0.05,
             hint="How fast the gradient changes; 1 is even."),
        dict(**{"class": "label"}, x=2, y=3, label="St&ripes"),
        dict(**{"class": "intedit"}, name="stripes", x=3, y=3, value="4", min=1, max=200,
             hint="Split each Line into this many pieces."),
        dict(**{"class": "checkbox"}, name="bord", x=1, y=4, w=3, label="&Ignore border and shadow", value=True,
             hint="Leave \\3c, \\4c, \\bord and \\shad as they are."),
        dict(**{"class": "checkbox"}, name="keep", x=1, y=5, w=3, label="&Keep the first and last colours",
             value=False, hint="Pin the first and last pieces to Start and End."),
        dict(**{"class": "label"}, x=0, y=6, label="&Notes"),
        dict(**{"class": "textbox"}, name="notes", x=1, y=6, w=3, h=2,
             text="Runs on the picked Lines.\nKaraoke tags are kept.",
             hint="Kept with the preset."),
    ]
    return c


GRADIENT_BUTTONS = ["&Apply", "&Load preset", "Cancel"]


def hydra_controls():
    tags = [("bord", "2"), ("shad", "0"), ("blur", "0.6"), ("be", "1"), ("fs", "64"), ("fsp", "0"),
            ("fscx", "100"), ("fscy", "100"), ("frz", "0"), ("frx", "0"), ("fry", "0"), ("fax", "0"),
            ("fay", "0"), ("xbord", "0"), ("ybord", "0"), ("xshad", "2"), ("yshad", "2"), ("alpha", "&H00&"),
            ("1a", "&H00&"), ("3a", "&H80&"), ("4a", "&HC0&"), ("an", "8"), ("q", "2"), ("org", "640,360"),
            ("c", "&HFFFFFF&"), ("2c", "&H0000FF&"), ("3c", "&H000000&"), ("4c", "&H000000&"), ("fn", "Arial"),
            ("pos", "640,600"), ("clip", "0,0,1280,720"), ("iclip", "0,0,0,0")]
    c = [dict(**{"class": "label"}, x=0, y=0, w=4, label="Tags applied to the picked Lines; tick the ones to set.")]
    half = (len(tags) + 1) // 2
    for i, (t, v) in enumerate(tags):
        col = 0 if i < half else 2
        row = 1 + (i if i < half else i - half)
        c.append(dict(**{"class": "checkbox"}, name=t, x=col, y=row, label="\\" + t, value=i in (0, 2, 13, 14)))
        c.append(dict(**{"class": "edit"}, name=t + "v", x=col + 1, y=row, text=v))
    return c


# ---------------------------------------------------------------------------------------------
# Sheet 1: layout and size


def screen(inner, w, h, label):
    return (f'<div class="screen" style="width:{w}px;height:{h}px"><span class="slabel">{label}</span>'
            f'{inner}</div>')


def sheet_layout(key):
    th = THEMES[key]
    g = gradient()
    focus_accel = {"accel": dict(focus=True)}
    natural = script_dialog("Gradient by character", g, GRADIENT_BUTTONS, width=470, states=focus_accel,
                            hint="How fast the gradient changes; 1 is even. 0.1 to 10, in steps of 0.05.")
    wide = script_dialog("Gradient by character", g, GRADIENT_BUTTONS, width=720, height=470,
                         states={"notes": dict(focus=True)}, hint="Kept with the preset.",
                         grow_rows=(6, 7), style="")
    hy = hydra_controls()
    capped = script_dialog("HYDRA", hy, ["&Apply", "&Repeat last", "Load &full", "Cancel"], width=560, height=470,
                           states={"blurv": dict(focus=True)}, hint=None, scroll=(18, 46),
                           style="position:absolute;left:120px;top:30px")
    hover_hint = script_dialog(
        "Gradient by character", g, GRADIENT_BUTTONS, width=470,
        states={"bord": dict(hover=True), "accel": dict(focus=True)},
        hint="Leave \\3c, \\4c, \\bord and \\shad as they are.",
        extra=pointer("left:82px;top:188px") + tooltip("Leave \\3c, \\4c, \\bord and \\shad as they are.",
                                                       "left:92px;top:210px"))
    return "".join([
        f'<h1>AD1 · Automation script dialogs: layout and size ({th["name"]})</h1>',
        '<p class="note">A script’s <code>aegisub.dialog.display</code> as a DG1 window: the macro’s name as '
        'the title, the script’s grid inside the 24-wide margins, a hint strip above the footer, and the '
        'script’s buttons in its own order at the trailing edge, the Enter button filled in the accent. '
        'The window is resizable (Maximise in the title bar). The example is modelled on lyger’s '
        'GradientEverything; the values it returns are Aegisub’s, unchanged.</p>',
        '<div class="row">',
        f'<div class="col">{natural}<p class="cap">At its natural size. The keyboard is in <i>Accel</i> (a '
        '<code>floatedit</code> with <code>step = 0.05</code>), so the strip shows its hint and its range. '
        '<code>button_ids = {ok = "&amp;Apply", cancel = "Cancel"}</code>: Apply is the Enter button; '
        'Cancel and Esc return <code>false</code>.</p></div>',
        f'<div class="col">{hover_hint}<p class="cap">The pointer over a control shows that control’s hint '
        'in the strip, and the tooltip still appears (Aegisub’s only hint). With the pointer away, the strip '
        'goes back to the focused control. The hint is also the control’s accessible description.</p></div>',
        '</div>',
        '<h2>Resized: the fields grow, the labels and check boxes do not</h2>',
        '<div class="row">',
        f'<div class="col">{wide}<p class="cap" style="max-width:720px">Columns holding an edit, a textbox, a '
        'dropdown or a colour take the extra width; rows holding a textbox take the extra height. Label '
        'columns keep their width, check boxes keep theirs. The size is kept per script and dialog and comes '
        'back at the next open (the place too, as every DG1 window).</p></div>',
        '<div class="col" style="max-width:420px"><div class="notes"><b>Size rules</b><ul>'
        '<li>Opens at the content’s natural size, centred on the main window 20 lower (DG1).</li>'
        '<li>Never larger than the screen’s free area less 20 on each side; past that the controls scroll '
        'and the hint strip and the buttons stay put (below).</li>'
        '<li>Can grow, never shrink below the natural size (or the screen cap).</li>'
        '<li>Rows are at least one text line; a textbox at least two lines (Aegisub: 30 pixels).</li>'
        '<li>All sizes follow the program font and the screen scale (Aegisub’s colour buttons are raw '
        'pixels).</li>'
        '<li>Kept under the script file, the macro and the dialog’s control names, so one script’s '
        'different dialogs keep their own sizes.</li></ul></div></div>',
        '</div>',
        '<h2>Taller than the screen: the controls scroll, the buttons stay</h2>',
        '<div class="row">',
        screen(capped, 800, 530, "Screen’s free area (example: a 1366 × 600 laptop, scaled)"),
        '<p class="cap" style="max-width:420px;margin-top:0">A long dialog (modelled on HYDRA’s full mode, 32 '
        'tag rows) is capped to the screen. The grid scrolls under a thin bar in the theme’s colours; Tab and '
        'the arrows bring the focused control into view; the hint strip and the footer stay. The strip keeps '
        'one height while the dialog is open (the tallest hint it holds, up to three lines), so the buttons '
        'never jump. A dialog with no hints at all has no strip (HYDRA here). The full-width label stays a '
        'plain label.</p>',
        '</div>',
    ])


# ---------------------------------------------------------------------------------------------
# Sheet 2: controls


def demo(controls, states=None, width=340, cols=None, hint=None):
    return f'<div class="demo" style="width:{width}px">{grid(controls, states, cols)}{hint_strip(hint).replace("hints", "hints solo")}</div>'


def num_row(cls, value, **st):
    lab = dict(**{"class": "label"}, x=0, y=0, label=st.pop("label", "Stripes"))
    c = dict(**{"class": cls}, name="n", x=1, y=0, value=value, min=st.pop("min", 1), max=st.pop("max", 200),
             step=st.pop("step", None))
    return [lab, c], {"n": st}


def colour_popover(alpha=True):
    pal = ["#FFFFFF", "#000000", "#E83A5F", "#F28C28", "#F2D22E", "#3BB273", "#3A7BE8", "#7E57C2",
           "#D9D9D9", "#595959", "#FFB3C1", "#FFD8A8", "#FFF3B0", "#B7E4C7", "#B3CDF7", "#D1C4E9"]
    rec = ["#3A7BE8", "#E83A5F", "#F2D22E", "#FFFFFF", "#202020", "#9CDBC9"]
    cells = "".join(f'<span class="pc" style="background:{p}"></span>' for p in pal)
    recs = "".join(f'<span class="pc" style="background:{p}"></span>' for p in rec)
    al = (f'<span class="plab">Alpha</span><span class="fld num sm" style="width:76px"><span class="ftext">128'
          f'</span>{spin_arrows()}</span>') if alpha else ""
    return popover(
        f'<div class="cprow"><span class="ba"><span style="background:rgba(58,123,232,1)"></span>'
        f'<span style="background:rgba(58,123,232,.5)"></span></span>'
        f'<span class="qb">{ic("eyedropper", 16)}</span></div>'
        f'<div class="pal">{cells}</div><div class="plab2">Recent colours</div><div class="pal">{recs}</div>'
        f'<div class="cprow"><span class="plab">Hex</span><span class="fld mono sm" style="width:104px">'
        f'<span class="ftext">#3A7BE8</span></span>{al}</div>'
        f'<div class="more">More…</div>', 282, "margin-top:4px")


MASKS = ["", "square", "rounded square", "circle", "triangle 1", "triangle 2", "fade left", "fade right",
         "fade up", "fade down", "alignment grid", "rect. gradient", "ring", "diamond", "star"]


def drop_list(items, cur, filtered=None, width=230, scroll=False):
    rows = []
    for it in items:
        txt = it if it else "&nbsp;"
        if filtered and it:
            txt = re.sub(f"({re.escape(filtered)})", r"<b>\1</b>", it, flags=re.I)
        rows.append(f'<div class="lrow{" cur" if it == cur else ""}{" blank" if not it else ""}">{txt}</div>')
    search = ""
    if filtered is not None:
        search = (f'<span class="fld focus wide sm">{ic("search", 14, "fic")}<span class="ftext">{filtered}'
                  f'<span class="caret"></span></span></span>')
    bar = '<span class="sbar" style="top:6px;height:70px"></span>' if scroll else ""
    return (f'<div class="dlist" style="width:{width}px">{search}<div class="llist">{"".join(rows)}{bar}</div>'
            f'</div>')


def sheet_controls(key):
    th = THEMES[key]
    parts = [f'<h1>AD1 · Automation script dialogs: the controls ({th["name"]})</h1>',
             '<p class="note">Each control class with its states. What the script gets back is listed under '
             'each; it is Aegisub’s value in every case.</p>']

    # Numbers
    a, sa = num_row("intedit", "4")
    b, sb = num_row("intedit", "", focus=True, empty=True)
    c_, sc = num_row("intedit", "4", focus=True, bad=True, typed="5", min=10, max=500, label="Size")
    d, sd = num_row("intedit", "10", min=10, max=500, label="Size")
    e, se = num_row("floatedit", "1.25", focus=True, step=0.05, min=0.1, max=10, label="Accel")
    f, sf = num_row("floatedit", "0.6", min=0, max=20, label="Blur")
    nums = [
        (demo(a, sa, 240), "<code>intedit</code>: arrows, the wheel, Up / Down and Page Up / Page Down step it by "
                           "1 / 10 within <code>min</code>…<code>max</code>. Returns an integer."),
        (demo(b, sb, 240), "Emptied: the range is the placeholder. Leaving it empty puts the last value back."),
        (demo(c_, sc, 240, hint="10 to 500. 5 becomes 10 when you leave the field."),
         "Typing is never blocked (Aegisub beeps at “5” on the way to “50” when <code>min = 10</code>). Out of "
         "range shows in the danger colour, and the strip says what it will become."),
        (demo(d, sd, 240), "Committed (Tab, a click elsewhere, Enter or a button): clamped to 10."),
        (demo(e, se, 240), "<code>floatedit</code> with <code>step</code>: the arrows, wheel and keys step by "
                           "it (Aegisub’s spin control; legacy ignored it). Returns a number."),
        (demo(f, sf, 240), "<code>floatedit</code> without <code>step</code>: a number field, no arrows, as "
                           "Aegisub (question 1). Clamped on commit too."),
    ]
    parts.append('<h2>Numbers: clamp on commit, not while typing</h2><div class="grid3">')
    parts += [f'<div class="col">{x}<p class="cap">{t}</p></div>' for x, t in nums]
    parts.append('</div>')

    # Colours
    col = [dict(**{"class": "label"}, x=0, y=0, label="Start"),
           dict(**{"class": "color"}, name="c1", x=1, y=0, value="#E83A5F", rgb=(232, 58, 95))]
    cola = [dict(**{"class": "label"}, x=0, y=0, label="End"),
            dict(**{"class": "coloralpha"}, name="c2", x=1, y=0, value="#3A7BE880", rgb=(58, 123, 232), alpha=128)]
    typed = [dict(**{"class": "label"}, x=0, y=0, label="Start"),
             dict(**{"class": "color"}, name="c1", x=1, y=0, value="#E83A5F", rgb=(232, 58, 95))]
    bad = [dict(**{"class": "label"}, x=0, y=0, label="Start"),
           dict(**{"class": "color"}, name="c1", x=1, y=0, value="#E83A5F", rgb=(232, 58, 95))]
    parts.append('<h2>Colours: a swatch, an editable value, the shared colour popover</h2><div class="row">')
    parts.append(f'<div class="col">{demo(col, None, 240)}<p class="cap"><code>color</code>: the swatch opens '
                 'the popover; the field takes typing. Returns <code>"#E83A5F"</code>.</p>'
                 f'<div style="height:14px"></div>{demo(typed, {"c1": dict(text_focus=True, typed="&amp;H5F3AE8&amp;")}, 240)}'
                 '<p class="cap">The field takes <code>#RRGGBB</code>, <code>RRGGBB</code> or ASS '
                 '<code>&amp;HBBGGRR&amp;</code>; the swatch follows as you type. Committed, it shows (and '
                 'returns) <code>"#E83A5F"</code>.</p>'
                 f'<div style="height:14px"></div>{demo(bad, {"c1": dict(text_focus=True, bad=True, typed="#E83A5")}, 240)}'
                 '<p class="cap">Not a colour: the danger colour; leaving the field puts the last colour back.</p>'
                 '</div>')
    parts.append(f'<div class="col">{demo(cola, {"c2": dict(focus=True, open_=True)}, 240)}'
                 f'<div style="margin-left:46px">{colour_popover()}</div>'
                 '<p class="cap" style="max-width:320px"><code>coloralpha</code>: the swatch shows the alpha over a '
                 'checkerboard; the popover is SY2’s compact one with Alpha (0 opaque). Enter, a click outside or '
                 'the swatch again keeps the colour; Esc puts it back and leaves the dialog open. Returns '
                 '<code>"#3A7BE880"</code>. <code>alpha</code> stays a text field, as Aegisub.</p></div>')
    parts.append('</div>')

    # Dropdowns
    short = [dict(**{"class": "label"}, x=0, y=0, label="Mode"),
             dict(**{"class": "dropdown"}, name="m", x=1, y=0, value="Character")]
    longd = [dict(**{"class": "label"}, x=0, y=0, label="Mask"),
             dict(**{"class": "dropdown"}, name="m", x=1, y=0, value="fade left")]
    blank = [dict(**{"class": "label"}, x=0, y=0, label="Channel"),
             dict(**{"class": "dropdown"}, name="m", x=1, y=0, value="")]
    parts.append('<h2>Dropdowns: type to filter above 10 items; the blank item stays</h2><div class="row">')
    parts.append(f'<div class="col">{demo(short, {"m": dict(focus=True, open_=True)}, 240)}'
                 f'<div style="margin-left:62px">{drop_list(["Character", "Line", "Vertical", "Horizontal"], "Character", width=178)}</div>'
                 '<p class="cap">Ten items or fewer: a plain list. Typing a letter jumps to the next item '
                 'starting with it.</p></div>')
    shown = [m for m in MASKS if "fad" in m]
    parts.append(f'<div class="col">{demo(longd, {"m": dict(focus=True, open_=True)}, 260)}'
                 f'<div style="margin-left:48px">{drop_list(shown, "fade left", filtered="fad", width=212)}</div>'
                 '<p class="cap">Above 10 items (Masquerade’s 15 masks): typing opens the list with a filter '
                 'field; matches stay in the script’s order, the first one is current, Enter picks it, Esc '
                 'closes the list only.</p></div>')
    parts.append(f'<div class="col">{demo(blank, {"m": dict(focus=True, open_=True)}, 240)}'
                 f'<div style="margin-left:62px">{drop_list(["", "stable", "beta", "alpha"], "", width=178)}</div>'
                 '<p class="cap">The script’s <code>value</code> is not one of its items (DependencyControl’s '
                 'channel list): a blank item comes first and is current, as Aegisub. Untouched, it returns '
                 '<code>""</code>.</p></div>')
    parts.append('</div>')

    # Text, check boxes, mnemonics
    txt = [dict(**{"class": "label"}, x=0, y=0, label="&Tags"),
           dict(**{"class": "edit"}, name="t", x=1, y=0, text="\\c\\3c\\alpha\\blur"),
           dict(**{"class": "checkbox"}, name="k", x=1, y=1, label="&Ignore border and shadow", value=True),
           dict(**{"class": "checkbox"}, name="k2", x=1, y=2, label="&Keep the first and last colours",
                value=False)]
    parts.append('<h2>Text, check boxes and <code>&amp;</code> mnemonics</h2><div class="row">')
    parts.append(f'<div class="col">{demo(txt, {"k2": dict(focus=True)}, 330)}<p class="cap">Space ticks the '
                 'focused check box (legacy: nothing). <code>edit</code> and <code>textbox</code> look and '
                 'return as before; the textbox grows with the window.</p></div>')
    parts.append(f'<div class="col">{demo(txt, {"Tags": dict(alt=True), "k": dict(alt=True), "k2": dict(alt=True, focus=True)}, 330)}'
                 '<p class="cap">Alt held: the <code>&amp;</code> letters are underlined. Alt+I ticks “Ignore…”, '
                 'Alt+T goes to the field after the label <i>Tags</i>, Alt+A presses <i>Apply</i>. The '
                 '<code>&amp;</code> is never drawn; the returned label keeps it, as Aegisub returns it.</p></div>')
    parts.append(f'<div class="col"><div class="dfoot solo">{btn("&Apply", primary=True, alt=True)}'
                 f'{btn("&Load preset", alt=True)}{btn("Cancel", alt=True)}</div>'
                 '<p class="cap" style="max-width:300px">The footer with Alt held. <code>&amp;&amp;</code> draws '
                 'one <code>&amp;</code>. Labels stay plain text: no icons, no renaming, no header styling.</p></div>')
    parts.append('</div>')
    return "".join(parts)


# ---------------------------------------------------------------------------------------------
# Sheet 3: keys and results


KEYS = [
    ("Tab / Shift+Tab", "Row by row, left to right through the grid, then the buttons in the script’s order",
     "In the script’s order (Aegisub); legacy: creation order, arrows by platform"),
    ("Enter", "Presses the focused button; anywhere else the Enter button (the <code>ok</code> id, else the "
              "first button). In a single-line field it commits the field first",
     "Single-line field: nothing; check box or dropdown: ends with <code>false</code>"),
    ("Ctrl+Enter", "In a textbox, presses the Enter button (Enter is a new line there)", "Nothing"),
    ("Esc", "Returns <code>false</code> from anywhere, text fields included. An open list or colour popover "
            "takes the first Esc and closes", "Text fields swallow Esc"),
    ("Space", "Ticks the focused check box, presses the focused button", "Nothing on either"),
    ("Alt+letter", "The <code>&amp;</code> letter of a button (presses it), a check box (ticks it) or a label "
                   "(the next control after it takes the keyboard)", "Not handled"),
    ("Up / Down", "Steps a number; moves through a dropdown’s items", "Numbers: caret moves; legacy "
                                                                       "per-platform focus moves elsewhere"),
    ("Page Up / Down", "Steps a number by 10 steps", "Nothing"),
    ("Close (×)", "Returns <code>false</code>", "Same"),
]

RESULTS = [
    ("<code>{ok, cancel}</code> ids, Cancel pressed (arch1t3cht GitSigns Configure: <code>return if not btn</code>)",
     "<code>false</code>: nothing saved", "<code>\"Cancel\"</code>: the settings are saved anyway",
     "<code>false</code>: nothing saved"),
    ("Same, Esc or ×", "<code>false</code>", "<code>false</code>", "<code>false</code>"),
    ("<code>{ok = \"Go\", cancel = \"Cancel\"}</code>, Cancel pressed (lyger SemiColorCalc: "
     "<code>if pressed == \"Cancel\" then aegisub.cancel()</code>)",
     "<code>false</code>: the script runs on", "<code>\"Cancel\"</code>: the script stops",
     "<code>false</code>: the script runs on, as in Aegisub"),
    ("No ids, a button labelled Cancel", "<code>\"Cancel\"</code>", "<code>\"Cancel\"</code>",
     "<code>\"Cancel\"</code>"),
    ("<code>{ok = \"Go\"}</code> only, Esc", "<code>\"Go\"</code> (wx falls back to the affirmative)",
     "<code>false</code>", "<code>false</code>"),
    ("Enter, ids <code>{ok = \"Apply\"}</code>, Apply not first", "<code>\"Apply\"</code>",
     "the first button’s label", "<code>\"Apply\"</code>"),
    ("No buttons given: OK / Cancel", "<code>\"\"</code> / <code>false</code>", "same", "same"),
]


def sheet_keys(key):
    th = THEMES[key]
    order = script_dialog("Gradient by character", gradient(), GRADIENT_BUTTONS, width=470, tab_order=True,
                          hint="The tags to gradient, in one block.", states={"tags": dict(focus=True)})
    krows = "".join(f'<tr><td>{k}</td><td>{a}</td><td class="m">{b}</td></tr>' for k, a, b in KEYS)
    rrows = "".join(f'<tr><td>{p}</td><td>{a}</td><td>{l}</td><td><b>{n}</b></td></tr>' for p, a, l, n in RESULTS)
    return "".join([
        f'<h1>AD1 · Automation script dialogs: keys and results ({th["name"]})</h1>',
        '<div class="row">',
        f'<div class="col">{order}<p class="cap">Tab order follows the grid (row, then column), then the '
        'buttons: the numbers here. A control spanning rows sits at its top row.</p></div>',
        '<div class="col" style="max-width:780px"><table class="kt"><tr><th>Key</th><th>AD1</th>'
        f'<th>Legacy today</th></tr>{krows}</table>'
        '<p class="cap" style="max-width:780px">One rule set on every platform: it replaces the per-platform '
        'emulation of legacy’s keys (AutomationDialog.qml’s header). Ledger row AD1-keyboard.</p></div>',
        '</div>',
        '<h2>What the script gets back: the button paths</h2>',
        '<table class="kt wide"><tr><th>Path</th><th>Aegisub</th><th>Legacy and qt today</th><th>AD1</th></tr>'
        f'{rrows}</table>',
        '<p class="cap" style="max-width:1100px">AD1 honours the third <code>button_ids</code> argument as Aegisub '
        'does (AD1-button-ids): a <code>cancel</code> button returns <code>false</code>, <code>ok</code> is the '
        'Enter button. Scripts written for Aegisub (GitSigns) work as there. lyger’s SemiColorCalc gives a '
        '<code>cancel</code> id but checks <code>== "Cancel"</code>, which only worked on Kainote’s dropped ids: '
        'its Cancel now behaves as in Aegisub (it carries on), and the ledger row names it. Esc never presses a '
        'button, unlike wx’s fallback. The values table comes back on every path, as before.</p>',
        '<div class="notes" style="margin-top:18px"><b>Ledger rows (compatibility-decisions.md)</b><ul>'
        '<li><b>AD1-button-ids</b>: the button-id table is read; <code>cancel</code> returns false, '
        '<code>ok</code> is the Enter button.</li>'
        '<li><b>AD1-float-step</b>: <code>floatedit</code> steps by <code>step</code> (legacy ignored it).</li>'
        '<li><b>AD1-keyboard</b>: the key table above, one set for every platform.</li>'
        '<li><b>AD1-number-commit</b>: numbers take any typing and clamp when committed; the arrows and '
        'keys step them.</li>'
        '<li><b>AD1-dialog-size</b>: the window is resizable, capped to the screen, and keeps its size.</li>'
        '</ul></div>',
    ])


# ---------------------------------------------------------------------------------------------
# Page

CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(page)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:1328px}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
code{font:12px 'Liberation Mono',Consolas,monospace}
h1{font-size:18px;font-weight:600;margin:0 0 8px}
h2{font-size:13px;font-weight:650;margin:28px 0 10px}
p.note{color:%(muted)s;font-size:12px;margin:0 0 14px;max-width:1280px;line-height:1.45}
.cap{font-size:11px;color:%(muted)s;margin:8px 0 0;max-width:470px;line-height:1.4}
.row{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap}
.col{display:flex;flex-direction:column;position:relative}
.grid3{display:grid;grid-template-columns:repeat(3,300px);gap:22px 40px}
.notes{background:%(raised)s;outline:1px solid %(line)s;padding:12px 16px;line-height:1.5}
.notes ul{margin:6px 0 0 18px}

/* DG1 window */
.dlg{position:relative;background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s)}
.dtb{display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtb.off{color:%(disabled)s}
.dtitle{flex:1;min-width:0;padding-left:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.wbs{display:flex;margin-left:auto;flex:none}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.dscroll{position:relative;flex:1;min-height:0;padding:12px 24px 0;overflow:hidden;display:flex;flex-direction:column}
.dscroll.capped{padding-right:30px}
.dscroll.capped .dcontent{margin-top:-96px}
.dcontent{flex:1;display:flex;flex-direction:column}
.vbar{position:absolute;right:8px;top:12px;bottom:4px;width:6px}
.vbar span{position:absolute;left:0;width:6px;border-radius:3px;background:%(control)s}
.hints{display:flex;align-items:flex-start;margin:14px 24px 0;padding-top:8px;box-shadow:inset 0 1px %(line)s;
       color:%(muted)s;font-size:12px;line-height:18px}
.hints.solo{margin:8px 0 0}
.dfoot{display:flex;gap:8px;justify-content:flex-end;padding:16px 24px 24px;flex:none}
.dfoot.solo{padding:0}

/* the script's grid */
.sgrid{display:grid;gap:6px 8px;align-items:center;align-content:start;flex:1}
.cell{position:relative;display:flex;min-width:0;align-self:stretch;align-items:center}
.cell.textbox{align-items:stretch}
.cell>*{flex:1}
.cell.label>*,.cell.checkbox>*{flex:none}
.lab{white-space:nowrap}
.tord{position:absolute;right:-6px;top:-7px;min-width:16px;height:16px;padding:0 3px;border-radius:8px;background:%(accent)s;
      color:%(onaccent)s;font-size:10px;font-weight:700;display:flex;align-items:center;justify-content:center;z-index:3}
.demo{background:%(panel)s;outline:1px solid %(line)s;padding:12px}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;min-width:80px;height:30px;padding:0 12px;border-radius:1px;
   background:%(raised)s;border:1px solid %(line)s;color:%(text)s;white-space:nowrap;flex:none}
.b.sm{min-width:0;height:26px;padding:0 10px}
.b.hov{background:%(press)s}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.ring{outline:1px solid %(text)s;outline-offset:1px}
.b u,.cbt u,.lab u{text-decoration-thickness:1px;text-underline-offset:2px}
.qb{display:inline-flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:1px;color:%(text)s;
    border:1px solid %(line)s;background:%(raised)s}
.cb{display:inline-flex;align-items:center;gap:8px;height:24px;padding-right:2px}
.cb.hov .box{border-color:%(text)s}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.fld{position:relative;display:inline-flex;align-items:center;gap:6px;height:28px;padding:0 8px;background:%(field)s;
     border:1px solid %(line)s;border-radius:1px;color:%(text)s;min-width:0}
.fld.sm{height:26px}
.fld.wide{display:flex;width:100%%}
.fld.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.fld.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s;color:%(danger)s}
.fld.mono .ftext{font:12px 'Liberation Mono',Consolas,monospace}
.fld.drop{background:%(raised)s}
.fld.drop.on{border-color:%(focus)s}
.ftext{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.phd{color:%(muted)s}
.caret{display:inline-block;width:1px;height:15px;background:%(text)s;vertical-align:-3px;margin-left:1px}
.fic,.ftail{color:%(muted)s}
.ftail{display:flex;margin-left:auto}
.fld.area{height:auto;min-height:52px;align-items:flex-start;padding:5px 8px;align-self:stretch}
.atext{line-height:19px}
.fld.num{padding-right:0;min-width:96px}
.fld.num.noarrows{padding-right:8px}
.arrows{display:flex;flex-direction:column;align-self:stretch;width:18px;border-left:1px solid %(line)s;background:%(raised)s}
.arrows span{flex:1;display:flex;align-items:center;justify-content:center;color:%(text)s}
.arrows span+span{border-top:1px solid %(line)s}
.colour{display:flex;gap:6px;align-items:center;min-width:0}
.colour .fld{flex:1;min-width:96px}
.swb{display:inline-flex;align-items:center;justify-content:center;width:40px;height:28px;border:1px solid %(line)s;
     background:%(raised)s;border-radius:1px;flex:none}
.swb.on{border-color:%(focus)s}
.sw{display:block;width:28px;height:18px;outline:1px solid %(control)s;
    background:repeating-conic-gradient(#BBB 0 25%%,#FFF 0 50%%) 0 0/8px 8px}
.sw span{display:block;width:100%%;height:100%%}
.tip{position:absolute;z-index:6;padding:4px 8px;background:%(raised)s;color:%(text)s;outline:1px solid %(line)s;
     font-size:12px;white-space:nowrap;box-shadow:0 3px 10px rgba(0,0,0,%(shadow)s)}
.ptr{position:absolute;z-index:7;color:#000;filter:drop-shadow(0 0 1px #fff) drop-shadow(0 0 1px #fff)}

/* popover and lists */
.pop{position:relative;background:%(panel)s;outline:1px solid %(line)s;border-radius:4px;padding:12px;
     box-shadow:0 6px 22px rgba(0,0,0,%(shadow)s);color:%(text)s;z-index:2}
.cprow{display:flex;align-items:center;gap:8px}
.cprow+.pal,.pal+.cprow{margin-top:10px}
.ba{display:flex;width:84px;height:26px;outline:1px solid %(control)s;
    background:repeating-conic-gradient(#BBB 0 25%%,#FFF 0 50%%) 0 0/8px 8px}
.ba span{flex:1}
.cprow .qb{margin-left:auto}
.pal{display:grid;grid-template-columns:repeat(8,22px);gap:6px;margin-top:10px}
.pc{width:22px;height:22px;outline:1px solid %(line)s}
.plab{color:%(muted)s}
.plab2{color:%(muted)s;margin-top:10px}
.more{color:%(accent)s;margin-top:10px}
.dlist{position:relative;background:%(panel)s;outline:1px solid %(line)s;box-shadow:0 6px 22px rgba(0,0,0,%(shadow)s);
       padding:4px;z-index:2;margin-top:2px;display:flex;flex-direction:column;gap:4px}
.llist{position:relative;padding:0}
.lrow{display:flex;align-items:center;height:24px;padding:0 8px}
.lrow.cur{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.lrow b{font-weight:700}
.sbar{position:absolute;right:2px;width:4px;border-radius:2px;background:%(control)s}

/* screen */
.screen{position:relative;outline:1px dashed %(control)s;background:%(raised)s}
.slabel{position:absolute;left:8px;top:6px;font-size:11px;color:%(muted)s}

/* tables */
.kt{border-collapse:collapse;background:%(field)s;outline:1px solid %(line)s;font-size:12px;line-height:1.4}
.kt.wide{width:1280px}
.kt th{text-align:left;font-weight:650;color:%(muted)s;background:%(raised)s;padding:6px 10px;border-bottom:1px solid %(line)s}
.kt td{padding:6px 10px;vertical-align:top;border-top:1px solid %(line)s}
.kt td:first-child{white-space:nowrap;font-weight:650}
.kt.wide td:first-child{white-space:normal;font-weight:400;width:430px}
.kt td.m{color:%(muted)s}
"""


def page(key, content):
    th = THEMES[key]
    css = CSS % dict(th, page=th["bg"], shadow=".55" if key in DARKISH else ".28",
                     focus=th["accent"] if key not in HC else th["text"],
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')


def main():
    for key in THEMES:
        (OUT / f"ad1-{key}-layout.html").write_text(page(key, sheet_layout(key)))
        (OUT / f"ad1-{key}-controls.html").write_text(page(key, sheet_controls(key)))
        (OUT / f"ad1-{key}-keys.html").write_text(page(key, sheet_keys(key)))


main()
