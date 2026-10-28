#!/usr/bin/env python3
"""PF1 #225 Settings redesign mockup: one HTML sheet per theme.

Drawn from the theme layer's roles (src/ui/theme.cpp baseRoles, default accent), DG1's dialog frame
(DialogWindow.qml: the 32-high title bar, 24 margins, the footer), PK1's kit (the message box, the menu
popup) and the icon set (src/ui/icons; IC1 drew the ten option icons PF1 places). Two icons are new
here (settings-grid, notices), drawn to K1's rules (16-unit grid, 1-wide stroke, one accent layer).
The pieces (frame, buttons, fields, message box) are PK1's mockup's (evidence/w6-pk1-mockup gen.py)."""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pf1")
OUT = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else pathlib.Path(__file__).resolve().parent / "out")
OUT.mkdir(parents=True, exist_ok=True)

THEMES = {
    "light": dict(code="light", name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", warning="#8A5A00", danger="#A51F31"),
    "dark": dict(code="dark", name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", warning="#E0A030", danger="#FFADAD"),
    "hcwhite": dict(code="highContrastWhite", name="High contrast white", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    warning="#6B4500", danger="#A00000"),
    "hcblack": dict(code="highContrastBlack", name="High contrast black", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    warning="#FFD54A", danger="#FFADAD"),
}
HC = {"hcwhite", "hcblack"}
DARKISH = {"dark", "hcblack"}

# The three new icons, K1's rules: 16 grid, 1-wide stroke, round caps, one accent layer.
NEW = {
    # The Subtitle grid page: a table, its time column and an active row.
    "settings-grid": '<rect x="1.5" y="2.5" width="13" height="11" rx="1"/><path d="M1.5 5.5h13M5.5 2.5v11"/>'
                     '<g id="accent"><path d="M7.5 8.25h5M7.5 10.75h3"/></g>',
    # Notices: a toast with its text, another behind it.
    "notices": '<rect x="1.5" y="5.5" width="13" height="8" rx="1"/><path d="M4 8.5h6M4 10.75h4"/>'
               '<g id="accent"><path d="M3.5 3.5h9"/></g>',
}
GLYPHS = {
    "chevron-down": '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/>',
    "chevron-up": '<path d="M4.5 9.5l3.5-3.5 3.5 3.5"/>',
}
REPO = ["settings", "appearance", "editor", "settings-video", "settings-audio", "convert", "script-properties",
        "automation", "hotkeys", "language", "ui-font", "renderer", "video-seek", "audio-device", "audio-delay",
        "theme", "accent-colour", "ui-colours", "restore-default", "dictionary", "autosave", "fps", "styles",
        "resample", "zoom", "lead-in", "folder-open", "search", "filter", "karaoke", "audio-waveform",
        "window-close", "window-maximize", "notice-warning", "add", "edit", "move-up", "move-down", "remove",
        "tag-bold", "tag-italic", "tag-font", "media-play", "about"]


def icon_body(name):
    if name in NEW:
        return NEW[name].replace('<g id="accent">', '<g class="acc">')
    svg = (TREE / "src/ui/icons" / f"{name}.svg").read_text()
    return re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip()).replace('<g id="accent">', '<g class="acc">')


def sprite():
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    syms = {**GLYPHS, **{n: icon_body(n) for n in REPO + list(NEW)}}
    body = "".join(f'<symbol id="i-{n}" viewBox="0 0 16 16"><g {attrs}>{s}</g></symbol>' for n, s in syms.items())
    return f'<svg width="0" height="0" style="position:absolute"><defs>{body}</defs></svg>'


def ic(name, size=16, cls=""):
    return f'<svg class="ic {cls}" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


# ---------------------------------------------------------------------------------------------
# Controls (PK1's)


def btn(text, primary=False, focus=False, lead=False, danger=False, small=False, icon=None, menu=False, on=False):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" lead" if lead else "") + (
        " dng" if danger else "") + (" sm" if small else "") + (" on" if on else "")
    i = ic(icon) if icon else ""
    m = f'<span class="bsep"></span>{ic("chevron-down", 14)}' if menu else ""
    return f'<span class="{cls}">{i}<span>{text}</span>{m}</span>'


def box(on):
    mark = ('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
            'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
    return f'<span class="box{" on" if on else ""}">{mark if on else ""}</span>'


def field(text, width=None, placeholder=False, focus=False, icon=None, trail=None, mono=False, dis=False):
    w = f"width:{width}px;" if width else ""
    cls = "fld" + (" ph" if placeholder else "") + (" focus" if focus else "") + (" mono" if mono else "") + (
        " dis" if dis else "")
    lead = ic(icon, 16, "fic") if icon else ""
    tail = f'<span class="ftail">{trail}</span>' if trail else ""
    return f'<span class="{cls}" style="{w}">{lead}<span class="ftext">{text}</span>{tail}</span>'


def combo(text, width=220, dis=False):
    return field(text, width, trail=ic("chevron-down", 14), dis=dis)


def spin(value, unit="", width=96):
    u = f'<span class="unit">{unit}</span>' if unit else ""
    return (f'<span class="fld spin" style="width:{width}px"><span class="ftext num">{value}</span>{u}'
            f'<span class="arrows">{ic("chevron-up", 10)}{ic("chevron-down", 10)}</span></span>')


def segmented(options, chosen):
    return ('<span class="seg">' + "".join(
        f'<span class="{"on" if o == chosen else ""}">{o}</span>' for o in options) + '</span>')


# ---------------------------------------------------------------------------------------------
# The Settings window's form: sections of rows; every row's leading 16-wide slot holds the
# option's icon (primary options), the check box (checks) or nothing, so all texts share one edge.


def section(title, *rows, first=False):
    return (f'<div class="sec{" first" if first else ""}"><div class="stitle">{title}</div>'
            f'<div class="srows">{"".join(rows)}</div></div>')


def row(label, control, icon=None, note=None, wide=False):
    slot = ic(icon) if icon else ""
    n = f'<div class="rnote">{note}</div>' if note else ""
    return (f'<div class="r{" wide" if wide else ""}"><span class="slot">{slot}</span>'
            f'<span class="rl">{label}</span><span class="rc">{control}</span></div>{n}')


def check(label, on=False, note=None, focus=False):
    n = f'<div class="rnote">{note}</div>' if note else ""
    return (f'<div class="r chk{" ring" if focus else ""}"><span class="slot">{box(on)}</span>'
            f'<span class="rl wrap">{label}</span></div>{n}')


def block(html):
    return f'<div class="rblock">{html}</div>'


PAGES = [("general", "General", "settings"), ("appearance", "Appearance", "appearance"),
         ("editor", "Line editor", "editor"), ("grid", "Subtitle grid", "settings-grid"),
         ("video", "Video", "settings-video"), ("audio", "Audio", "settings-audio"),
         ("conversion", "Conversion", "convert"), ("properties", "Subtitle properties", "script-properties"),
         ("automation", "Automation", "automation"), ("hotkeys", "Hotkeys", "hotkeys")]


def page_list(current):
    rows = "".join(
        f'<div class="pg{" cur" if k == current else ""}">{ic(i)}<span>{n}</span></div>' for k, n, i in PAGES)
    return f'<div class="pages">{rows}</div>'


def titlebar(title):
    return (f'<div class="dtb"><span class="dtitle">{title}</span><span class="wbs">'
            f'<span class="wb">{ic("window-maximize")}</span><span class="wb">{ic("window-close")}</span></span></div>')


def footer(menu_open=False):
    menu = ""
    if menu_open:
        menu = ('<div class="menu" style="left:24px;bottom:60px">'
                f'<div class="mi">{ic("restore-default")}<span>Reset this page</span></div>'
                f'<div class="mi hi"><span class="noic"></span><span>Reset all settings…</span></div></div>')
    return (f'<div class="dfoot">{menu}{btn("Reset page", lead=True, icon="restore-default", menu=True, on=menu_open)}'
            f'{btn("OK", primary=True)}{btn("Cancel")}{btn("Apply")}</div>')


def settings_window(current, body, menu_open=False, scroll=None, height=640):
    bar = ""
    if scroll:
        top, h = scroll
        bar = f'<span class="sbar" style="top:{top}px;height:{h}px"></span>'
    return (f'<div class="dlg settings" style="width:880px;height:{height}px">{titlebar("Settings")}'
            f'<div class="split">{page_list(current)}<div class="pbody">{bar}<div class="form">{body}</div></div></div>'
            f'{footer(menu_open)}</div>')


# ---------------------------------------------------------------------------------------------
# Pages


def general():
    return "".join([
        section("Language",
                row("Interface language", combo("English"), icon="language"), first=True),
        section("Tabs",
                check("Always open subtitles in a new tab", True),
                check("Open sorted subtitles"),
                check("Synchronize time shifting window in all tabs"),
                row("Tab name length", spin("40", "characters", 150))),
        section("Saving",
                row("Save while editing every", spin("300", "edits", 150), icon="autosave",
                    note="0 turns saving while editing off."),
                row("Autosave files kept", spin("200", "", 150))),
        section("Notices",
                check("Show notices as toasts", True, note="Off: notices go to the Log only."),
                row("Questions hidden with “Don’t ask again”", btn("Ask again", small=True), icon="notices")),
        section("Folders",
                row("Fonts folder", field("/home/aki/Fonts", 260) + btn("Choose…", small=True), icon="folder-open")),
    ])


def appearance(key):
    th = THEMES[key]
    cards = ""
    for k in ["light", "dark", "hcwhite", "hcblack"]:
        t = THEMES[k]
        on = " on" if k == key else ""
        cards += (f'<span class="tcard{on}" style="background:{t["bg"]};border-color:{th["accent"] if on else th["line"]}">'
                  f'<span class="tpan" style="background:{t["panel"]};border-color:{t["line"]}">'
                  f'<b style="color:{t["text"]}">{t["name"]}</b><i style="background:{t["muted"]}"></i>'
                  f'<i style="background:{t["muted"]};width:32px"></i>'
                  f'<span class="tacc" style="background:{t["accent"]}"></span></span></span>')
    if key in HC:
        colours = section("Colors",
                          row("Accent", f'<span class="sw big" style="background:{th["accent"]}"></span>'
                              + btn("Choose…", small=True), icon="ui-colours"),
                          row("Text and icons", f'<span class="sw big" style="background:{th["text"]}"></span>'
                              + btn("Choose…", small=True)),
                          row("Borders", f'<span class="sw big" style="background:{th["line"]}"></span>'
                              + btn("Choose…", small=True)),
                          row("", btn("Reset to default", small=True, icon="restore-default")))
    else:
        light = ["#145C4C", "#B3261E", "#9C4600", "#7A5E00", "#1D5BA8", "#5B3FB0", "#9E2A73"]
        dark = ["#9CDBC9", "#F4A9A3", "#F5BB8A", "#E3CF7E", "#A6C8F2", "#C6B5F4", "#EFAAD3"]
        sws = "".join(f'<span class="sw{" on" if i == 0 else ""}" style="background:{c}"></span>'
                      for i, c in enumerate(dark if key == "dark" else light))
        colours = section("Accent color", row("Accent", f'<span class="sws">{sws}</span>', icon="accent-colour"))
    return "".join([
        section("Theme",
                block(f'<div class="tcards">{cards}</div>'),
                check("Follow system theme", key == "dark",
                      note="Light or Dark as the system’s color scheme sets it; high contrast stays as chosen."),
                first=True),
        colours,
        section("Font",
                row("Program font", combo("Noto Sans", 200) + spin("10", "pt", 84), icon="ui-font")),
        section("Window",
                check("Use system title bar",
                      note="Off by default on Windows, KDE Plasma and GNOME; on by default on tiling window managers.")),
    ])


def tag_buttons():
    rows = [("tag-bold", "Bold", "{\\b1}", False), ("tag-italic", "Italic", "{\\i1}", True),
            ("", "Fade", "{\\fad(200,200)}", False), ("tag-font", "Sign font", "{\\fnArial Black}", False)]
    body = "".join(f'<div class="lrow{" cur" if sel else ""}"><span class="lic">{ic(i) if i else ""}</span>'
                   f'<span class="ln">{n}</span><span class="lt">{t}</span></div>' for i, n, t, sel in rows)
    tools = "".join(f'<span class="qb">{ic(i)}</span>' for i in ["add", "edit", "move-up", "move-down", "remove"])
    return f'<div class="llist">{body}</div><div class="qbs">{tools}</div>'


def editor():
    return "".join([
        section("Spelling",
                check("Check spelling", True),
                row("Dictionary", combo("English (United States)"), icon="dictionary"),
                check("Show suggestions on double-click", True), first=True),
        section("Display",
                check("Hide tags"),
                check("Show video frames instead of times", note="In the line editor and the subtitle grid.")),
        section("Editing",
                check("Stay on the line when editing times"),
                check("Use numpad shortcuts in text fields")),
        section("Translation mode",
                check("Show the original", True),
                check("Hide the original on the video")),
        section("Tag buttons", block(tag_buttons())),
    ])


def grid():
    return "".join([
        section("Font", row("Grid font", combo("Noto Sans", 200) + spin("10", "pt", 84), icon="tag-font"), first=True),
        section("Active line",
                check("Select the line at the previous tab’s time"),
                check("Change the active line when adding to the selection"),
                check("Keep the selection when duplicating lines"),
                check("Don’t center the active line"),
                check("Find the visible line after leaving full screen")),
        section("Columns", check("Hide empty columns", True)),
        section("Counting",
                check("Count spaces and punctuation for wraps"),
                check("Count spaces and punctuation for CPS", True)),
        section("New lines",
                row("Start frame offset", spin("0", "ms")),
                row("End frame offset", spin("0", "ms")),
                row("ASS tag replacement", field("☀", 96))),
    ])


def video():
    return "".join([
        section("Playback",
                check("Left mouse button pauses", True),
                check("Open video from the context menu in full screen"),
                check("Open video at the active line’s time", True),
                row("Seek on", combo("Selecting a line")),
                row("Then play", combo("Nothing")), first=True),
        section("Subtitles on video",
                row("Subtitle display filter", combo("libass"), icon="renderer",
                    note="Shown when a CSRI renderer is installed."),
                check("Turn off the edits preview", note="Applies when the tab opens again.")),
        section("Decoding",
                row("Seeking method", combo("Unsafe (always fast)"), icon="video-seek",
                    note="Applies when the video opens again."),
                check("Convert colors on the GPU", True, note="Applies when the video opens again."),
                row("Preferred audio", field("jpn;eng", 220), note="Languages or track names, separated by semicolons.")),
        section("View", row("Start zoom", spin("100", "%"), icon="zoom")),
        section("Script resolution",
                check("Turn off the visual tools warning"),
                check("Don’t warn about a resolution mismatch"),
                check("Set a missing script resolution from the video without asking")),
    ])


def audio():
    return "".join([
        section("Output",
                row("Device", combo("System default", 260), icon="audio-device"),
                row("API", combo("WASAPI", 260), note="Windows only. Applies when the output opens again."),
                first=True),
        section("Playback",
                row("Audio delay", spin("0", "ms"), icon="audio-delay"),
                row("Play around the marker", spin("1000", "ms")),
                row("Lead-in and lead-out", spin("200", "ms") + spin("300", "ms"), icon="lead-in"),
                check("Follow audio during playback", True),
                check("Don’t play after changing the line"),
                check("Hotkeys follow the panel under the pointer", True)),
        section("Display",
                check("Show the time beside the cursor", True),
                check("Show seconds markers", True),
                check("Show the selection background", True),
                check("Show the video position", True),
                check("Show keyframes", True),
                row("Line boundaries thickness", spin("2", "px")),
                row("Inactive lines", combo("Before and after the active"))),
        section("Snapping", check("Snap to keyframes", True), check("Snap to other lines")),
        section("Karaoke", check("Merge every “n” with the previous syllable"),
                check("Move the syllable line after a click", True),
                check("Give a double consonant its own syllable (so|ma|t|ta)"),
                row("Switch to \\kf from", spin("100", "cs"), icon="karaoke",
                    note="KA1 (#318, open): syllables this long or longer.")),
        section("Cache", check("Load audio into RAM"), row("Cache files kept", spin("40", "files"),
                note="0 keeps them all.")),
    ])


def conversion():
    return "".join([
        section("Style",
                row("Catalog", combo("Default"), icon="styles"),
                row("Style", combo("Default")), first=True),
        section("Timing",
                row("Frame rate", combo("23.976", 120), icon="fps"),
                check("Frame rate from the video", True),
                check("New end times"),
                row("Time per letter", spin("70", "ms"))),
        section("ASS",
                row("Resolution", spin("1920", "", 96) + '<span class="x">×</span>' + spin("1080", "", 96),
                    icon="resample"),
                row("Tags at the start of each line", field("", 220)),
                check("Show the conversion window first", True)),
    ])


def properties():
    rows = [("Title", "Episode", True), ("Author", "", False), ("Translator", "Aki", True),
            ("Proofreading", "", False), ("Timer", "Ren", True), ("Editing", "", False)]
    table = '<div class="ptable"><span class="pth"></span><span class="pth"></span><span class="pth">Use</span>' + "".join(
        f'<span class="pl">{n}</span>{field(v, None)}<span class="pc">{box(on)}</span>' for n, v, on in rows) + '</div>'
    return "".join([
        section("Written into new files", block(table), first=True),
        section("Files", check("Always ask before changing subtitle information", True),
                check("Save the panel layout in subtitle files")),
    ])


def automation():
    return "".join([
        section("Loading",
                row("Load scripts", combo("After program start asynchronously", 280), icon="automation"),
                check("Compatibility with older HikariSub scripts"), first=True),
        section("Debugging", row("Lua trace level", spin("3", "", 84))),
    ])


HOTKEYS = [
    ("Global", None, None),
    ("Save", "Ctrl+S", ""), ("Save as", "Ctrl+Shift+S", ""), ("Settings", "Ctrl+,", "mod"),
    ("Find", "Ctrl+F", ""), ("Undo", "Ctrl+Z", ""),
    ("Audio", None, None),
    ("Play line", "R", ""), ("Commit", "G", "sel"), ("Next line", "F", ""), ("Lead-in", "Q", ""),
    ("Video", None, None),
    ("Play / pause", "Space", ""), ("Previous frame", "Left", ""), ("Next frame", "Right", ""),
]


def hotkeys():
    head = ('<div class="htools">' + combo("All", 200) +
            field("Search hotkeys", 280, placeholder=True, icon="search") + '</div>')
    rows = ""
    for n, k, s in HOTKEYS:
        if k is None:
            rows += f'<div class="hgrp">{n}</div>'
        else:
            rows += (f'<div class="hrow{" sel" if s == "sel" else ""}{" mod" if s == "mod" else ""}">'
                     f'<span>{n}</span><span class="keys">{k}</span></div>')
    table = (f'<div class="htable"><div class="hhead"><span>Function</span><span>Hotkey</span></div>{rows}</div>')
    acts = ('<div class="hacts">' + btn("Map hotkey…", small=True) + btn("Restore default", small=True)
            + btn("Delete", small=True) + '</div>')
    return head + table + acts


def message_box():
    body = (f'<div class="mb">{ic("notice-warning", 32, "big warn")}<div class="mbtext">'
            '<div class="mbmain">Reset all settings to their defaults?</div>'
            '<div class="mbdetail">Every page goes back to its defaults when you press OK or Apply; Cancel keeps '
            'your settings. Hotkeys and the appearance stay as they are: reset them on their own pages.</div>'
            '</div></div>')
    return (f'<div class="dlg mbox" style="width:468px">'
            f'<div class="dtb"><span class="dtitle">Reset all settings</span><span class="wbs"><span class="wb">'
            f'{ic("window-close")}</span></span></div><div class="dbody">{body}</div>'
            f'<div class="dfoot">{btn("Reset all", danger=True)}{btn("Cancel", primary=True, focus=True)}</div></div>')


def icon_strip():
    placed = [("language", "Language"), ("ui-font", "Program font"), ("renderer", "Subtitle display filter"),
              ("video-seek", "Seeking method"), ("audio-device", "Audio output"), ("audio-delay", "Audio delay"),
              ("theme", "Theme"), ("accent-colour", "Accent color"), ("ui-colours", "UI colors"),
              ("restore-default", "Reset page")]
    shared = [("autosave", "Saving"), ("folder-open", "Fonts folder"), ("dictionary", "Dictionary"),
              ("tag-font", "Grid font"), ("zoom", "Start zoom"), ("lead-in", "Lead-in and lead-out"),
              ("styles", "Catalog"), ("fps", "Frame rate"), ("resample", "Resolution"), ("automation", "Load scripts")]
    new = [("settings-grid", "Subtitle grid (page)"), ("notices", "Notices")]

    def cells(items, cls=""):
        return "".join(f'<div class="icell {cls}">{ic(r, 16)}{ic(r, 32)}<span class="iname">{r}</span>'
                       f'<span class="ilabel">{lab}</span></div>' for r, lab in items)
    return (f'<div class="igrp"><div class="ih">New in PF1 (to approve)</div><div class="icells">{cells(new, "new")}</div></div>'
            f'<div class="igrp"><div class="ih">Drawn by IC1, pending PF1: placed here</div><div class="icells">{cells(placed)}</div></div>'
            f'<div class="igrp"><div class="ih">Shared with a menu (the same meaning)</div><div class="icells">{cells(shared)}</div></div>')


# ---------------------------------------------------------------------------------------------

CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(bg)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:1860px}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
h1{font-size:18px;font-weight:600;margin:0 0 6px}
h2{font-size:13px;font-weight:650;margin:26px 0 10px}
p.note{color:%(muted)s;font-size:12px;margin:0 0 6px;max-width:1780px;line-height:1.45}
.cap{font-size:12px;color:%(muted)s;margin:8px 0 0;max-width:880px;line-height:1.45}
.row2{display:flex;gap:40px;align-items:flex-start}
.col{display:flex;flex-direction:column}

/* DG1 window */
.dlg{position:relative;background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s)}
.dtb{display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtitle{flex:1;min-width:0;padding-left:12px;white-space:nowrap}
.wbs{display:flex;margin-left:auto;flex:none}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.dbody{padding:12px 24px 0;display:flex;flex-direction:column}
.dfoot{position:relative;display:flex;gap:8px;justify-content:flex-end;padding:16px 24px;flex:none}
.settings .dfoot{box-shadow:inset 0 1px %(line)s}
.dfoot .lead{margin-right:auto}

/* the split: page list on the background, the page on the panel */
.split{flex:1;display:flex;min-height:0;box-shadow:inset 0 1px %(line)s}
.pages{width:220px;flex:none;background:%(bg)s;padding:8px 0;box-shadow:inset -1px 0 %(line)s}
.pg{display:flex;align-items:center;gap:12px;height:36px;padding:0 16px;color:%(text)s}
.pg.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s;font-weight:650}
.pbody{position:relative;flex:1;min-width:0;overflow:hidden}
.form{padding:20px 28px 24px 24px}
.sbar{position:absolute;right:3px;width:4px;border-radius:2px;background:%(control)s;opacity:.8}

/* sections and rows */
.sec{padding-top:16px;margin-top:16px;box-shadow:inset 0 1px %(line)s}
.sec.first{padding-top:0;margin-top:0;box-shadow:none}
.stitle{font-weight:650;margin-bottom:10px}
.srows{display:flex;flex-direction:column;gap:8px}
.r{display:flex;align-items:center;gap:8px;min-height:28px}
.r.chk{min-height:22px}
.slot{width:16px;flex:none;display:flex;align-items:center;justify-content:center}
.rl{width:220px;flex:none;line-height:1.35}
.rl.wrap{width:auto;flex:1}
.rc{display:flex;align-items:center;gap:8px;flex:1;min-width:0}
.rnote{margin:-5px 0 0 24px;color:%(muted)s;font-size:12px;line-height:1.4}
.rblock{margin-left:24px}
.ring{outline:1px solid %(focus)s;outline-offset:2px}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-width:80px;height:30px;padding:0 10px;border-radius:1px;
   background:%(raised)s;border:1px solid %(control)s;color:%(text)s;white-space:nowrap;flex:none}
.b.sm{min-width:0;height:28px}
.b.on{background:%(select)s;border-color:%(accent)s}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.b.dng{color:%(danger)s;border-color:%(danger)s;background:%(panel)s}
.b.ring{outline:1px solid %(focus)s;outline-offset:1px}
.bsep{width:1px;align-self:stretch;margin:5px 0 5px 4px;background:%(line)s}
.qb{display:inline-flex;align-items:center;justify-content:center;width:26px;height:26px;border-radius:1px;color:%(text)s}
.qbs{display:flex;gap:2px;margin-top:4px}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.fld{display:inline-flex;align-items:center;gap:6px;height:28px;padding:0 8px;background:%(field)s;border:1px solid %(line)s;
     border-radius:1px;color:%(text)s;min-width:0}
.fld.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.fld.ph .ftext{color:%(muted)s}
.ftext{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.ftext.num{text-align:right;font-variant-numeric:tabular-nums}
.fic,.ftail{color:%(muted)s}
.ftail{display:flex;margin-left:auto}
.spin{padding-right:2px}
.unit{color:%(muted)s}
.arrows{display:flex;flex-direction:column;color:%(muted)s;margin-left:2px}
.x{color:%(muted)s}
.seg{display:inline-flex;border:1px solid %(line)s;border-radius:1px;overflow:hidden;flex:none}
.seg span{padding:0 10px;height:26px;display:flex;align-items:center;background:%(raised)s;border-left:1px solid %(line)s}
.seg span:first-child{border-left:none}
.seg span.on{background:%(select)s;box-shadow:inset 0 -2px %(accent)s;font-weight:650}
.big{color:%(text)s}
.warn,.warn .acc{color:%(warning)s}

/* appearance */
.tcards{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;max-width:560px}
.tcard{height:64px;border:1px solid;border-radius:1px;padding:6px;display:flex}
.tcard.on{border-width:2px;padding:5px}
.tpan{position:relative;flex:1;border:1px solid;border-radius:1px;padding:4px 6px;display:flex;flex-direction:column;gap:3px}
.tpan b{font-size:11px;white-space:nowrap;overflow:hidden}
.tpan i{display:block;width:48px;height:3px;border-radius:1px}
.tacc{position:absolute;right:5px;bottom:5px;width:24px;height:12px;border-radius:1px}
.sws{display:flex;gap:8px}
.sw{width:22px;height:22px;border-radius:50%%;outline:1px solid %(line)s;flex:none}
.sw.on{outline:2px solid %(text)s;outline-offset:2px}
.sw.big{border-radius:1px;width:28px}

/* lists */
.llist{outline:1px solid %(line)s;background:%(field)s;padding:2px 0;max-width:520px}
.lrow{display:flex;gap:8px;align-items:center;height:28px;padding:0 8px}
.lrow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.lic{width:16px}
.ln{width:120px}
.lt{color:%(muted)s;font-family:'Liberation Mono',monospace;font-size:12px}

/* subtitle properties */
.ptable{display:grid;grid-template-columns:120px 300px 40px;gap:8px 10px;align-items:center}
.pth{color:%(muted)s;font-size:12px;text-align:center}
.pl{color:%(text)s}
.pc{display:flex;justify-content:center}

/* hotkeys */
.htools{display:flex;gap:8px;margin-bottom:10px}
.htable{outline:1px solid %(line)s;background:%(field)s}
.hhead,.hrow{display:grid;grid-template-columns:1fr 180px;padding:0 10px;height:26px;align-items:center}
.hhead{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.hgrp{height:26px;display:flex;align-items:center;padding:0 10px;font-weight:650;color:%(muted)s;font-size:12px}
.hrow{padding-left:22px}
.hrow.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.hrow.mod span{color:%(warning)s}
.keys{color:%(muted)s}
.hacts{display:flex;gap:8px;margin-top:10px}

/* menu (PK1: radius 5, the shadow) */
.menu{position:absolute;z-index:3;min-width:220px;background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;
      box-shadow:0 10px 30px rgba(0,0,0,.33)}
.mi{display:flex;align-items:center;gap:10px;height:28px;padding:0 14px 0 10px}
.mi.hi{background:%(select)s}
.noic{width:16px}

/* message box */
.mb{display:flex;gap:16px;align-items:flex-start;margin-top:8px}
.mbtext{display:flex;flex-direction:column;gap:6px;padding-top:4px;min-width:0}
.mbmain{font-size:14px;font-weight:650;line-height:1.4}
.mbdetail{color:%(muted)s;line-height:1.45}
.mbox .dfoot{padding:24px}

/* icons */
.igrp{margin-bottom:14px}
.ih{color:%(muted)s;font-size:12px;margin-bottom:6px}
.icells{display:flex;flex-wrap:wrap;gap:8px}
.icell{display:grid;grid-template-columns:16px 32px 1fr;grid-template-rows:32px auto auto;column-gap:10px;align-items:center;
       width:168px;padding:10px;background:%(panel)s;outline:1px solid %(line)s}
.icell.new{outline:2px solid %(accent)s}
.iname{grid-column:1 / -1;font-family:'Liberation Mono',monospace;font-size:12px;margin-top:6px}
.ilabel{grid-column:1 / -1;color:%(muted)s;font-size:12px}
"""


def sheet(key):
    th = THEMES[key]
    css = CSS % dict(th, shadow=".55" if key in DARKISH else ".28",
                     focus=th["text"],
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    hc_note = (" In high contrast the Appearance page shows the three color pickers in place of the accent swatches."
               if key in HC else "")
    content = f"""
<h1>PF1 Settings redesign: {th['name']}</h1>
<p class="note">One window on DG1’s frame (resizable, 880 × 640 to start, its place and size kept), titled “Settings” after
the menu command. A flat page list on the left (MuseScore 4’s Preferences: 220 wide, 36-high rows, each page’s icon; the
selected page in the selection color with the accent marker and bold). Legacy’s sub-pages are sections now. Each page is
one column of titled sections split by rules; every row starts with a 16-wide slot that holds the option’s icon (primary
options), its check box, or nothing, so all labels share one edge. No search across pages (the reference has none);
Hotkeys keeps its context filter and gains a search field, as MuseScore’s Shortcuts page has.{hc_note}</p>
<h2>General, and the Reset page menu open</h2>
<div class="row2">
 <div class="col">{settings_window("general", general(), scroll=(4, 420))}
 <p class="cap">General: language, tabs, saving, notices (PK1’s toasts and “Ask again”) and folders. The texts are
 legacy’s, shortened where a section title already says it.</p></div>
 <div class="col">{settings_window("audio", audio(), menu_open=True, scroll=(4, 210))}
 <p class="cap">Audio (legacy Audio and Audio › Advanced as one page, scrolled to the top). “Reset page” resets the page
 shown; its menu holds “Reset all settings…”. Resets are staged like every change: OK or Apply keeps them, Cancel undoes them.</p></div>
</div>
<h2>Appearance and Video</h2>
<div class="row2">
 <div class="col">{settings_window("appearance", appearance(key))}
 <p class="cap">Appearance (K2’s page): the four themes as cards, Follow system theme, the accent swatches (in high
 contrast the three color pickers), the program font (from legacy Editor › Advanced) and SH1’s title bar choice.
 Previews live as today; Cancel takes the preview back.</p></div>
 <div class="col">{settings_window("video", video(), scroll=(4, 300))}
 <p class="cap">Video, with VF1’s two follow lists under Playback. Settings that take effect only later say when in a
 muted line under them, in place of legacy’s “(requires reloading)” in the label.</p></div>
</div>
<h2>Hotkeys, Line editor, and the “Reset all settings…” question</h2>
<div class="row2">
 <div class="col">{settings_window("hotkeys", hotkeys())}
 <p class="cap">Hotkeys: the context filter, a search field (function names and keys), the list grouped by context,
 a changed row in the warning color until OK or Apply, and Map hotkey… (PK1’s capture popover), Restore default and
 Delete for the selected row. Reset page restores every default hotkey.</p></div>
 <div class="col">{settings_window("editor", editor(), scroll=(4, 330))}
 <p class="cap">Line editor: spelling, display, editing, Translation mode and LE5’s tag buttons.</p>
 <div style="margin-top:20px">{message_box()}</div>
 <p class="cap">PK1’s message box for “Reset all settings…”: the destructive verb outlined in the danger color, Cancel
 the default.</p></div>
</div>
<h2>Subtitle grid and Conversion</h2>
<div class="row2">
 <div class="col">{settings_window("grid", grid(), scroll=(4, 380))}
 <p class="cap">Subtitle grid: the grid’s settings from legacy Editor and Editor › Advanced, with GR3’s “Hide empty columns”.</p></div>
 <div class="col">{settings_window("conversion", conversion())}
 <p class="cap">Conversion (legacy Editor › Conversion): the style, timing and ASS settings for converting to ASS.</p></div>
</div>
<h2>Subtitle properties and Automation</h2>
<div class="row2">
 <div class="col">{settings_window("properties", properties())}
 <p class="cap">Subtitle properties: what new files’ information gets (Use writes the value), and WS2’s panel layout in files.</p></div>
 <div class="col">{settings_window("automation", automation())}
 <p class="cap">Automation: the Automation settings from legacy Editor and Editor › Advanced.</p></div>
</div>
<h2>Icons</h2>
{icon_strip()}
"""
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')


for key in THEMES:
    (OUT / f"pf1-{key}.html").write_text(sheet(key))
print("wrote", OUT)
