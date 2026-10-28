#!/usr/bin/env python3
"""DG1 #222 dialog frame mockup: per theme, the frame over the real app, the five dialogs, and the
frame's states.

The main window behind the dialogs is a real screenshot of today's HikariSub (qt 9c1eef089, SH1's
build) in that theme, taken by body.sh under vx11, and so are the "today" crops. The dialogs are
drawn here from the theme layer's roles (src/ui/theme.cpp baseRoles, default accent), SH1's title
bar metrics (MainTitleBar.qml) and IC1's icons (src/ui/icons)."""
import base64
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-dg1")
OUT = pathlib.Path(__file__).resolve().parent

THEMES = {
    "light": dict(code="light", name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF",
                  text="#202832", muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C",
                  onaccent="#FFFFFF", select="#D6EBE4", disabled="#74808B", press="#DDE2E7", warning="#8A5A00"),
    "dark": dict(code="dark", name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24",
                 text="#E8EDF2", muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9",
                 onaccent="#102C24", select="#304C47", disabled="#75818D", press="#343E49", warning="#E0A030"),
    "hcwhite": dict(code="highContrastWhite", name="High contrast white", bg="#FFFFFF", panel="#FFFFFF",
                    raised="#EBEBEB", field="#FFFFFF", text="#000000", muted="#1A1A1A", line="#000000",
                    control="#000000", accent="#0037B3", onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E",
                    press="#C9DAF8", warning="#6B4500"),
    "hcblack": dict(code="highContrastBlack", name="High contrast black", bg="#000000", panel="#080808",
                    raised="#151515", field="#000000", text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF",
                    control="#FFFFFF", accent="#FFFF00", onaccent="#000000", select="#253F60", disabled="#8C8C8C",
                    press="#253F60", warning="#FFD54A"),
}
HC = {"hcwhite", "hcblack"}
DARKISH = {"dark", "hcblack"}

# Caption glyphs from SH1 (MainTitleBar.qml), and one new glyph in IC1's 16-unit, 1-wide stroke style.
GLYPHS = {
    "window-maximize": '<rect x="3.5" y="3.5" width="9" height="9" rx=".5"/>',
    "window-close": '<path d="M3.5 3.5l9 9M12.5 3.5l-9 9"/>',
    "notice-warning": '<path d="M8 2l6.5 11.5h-13z"/><g class="acc"><path d="M8 6.5v3.5"/>'
                      '<circle cx="8" cy="11.75" r=".6" fill="currentColor" stroke="none"/></g>',
}
REPO_ICONS = ["about", "credits", "check-updates", "edit-copy", "link", "report-issue", "show-in-folder"]


def icon_svg(name):
    svg = (TREE / "src/ui/icons" / f"{name}.svg").read_text()
    return re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip()).replace('<g id="accent">', '<g class="acc">')


def sprite():
    attrs = 'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
    syms = {**GLYPHS, **{n: icon_svg(n) for n in REPO_ICONS}}
    body = "".join(f'<symbol id="i-{n}" viewBox="0 0 16 16"><g {attrs}>{s}</g></symbol>' for n, s in syms.items())
    return f'<svg width="0" height="0" style="position:absolute"><defs>{body}</defs></svg>'


def ic(name, size=16, cls=""):
    # Scaled icons keep IC1's 1-unit stroke relative to 16 (vector-effect off), as Icon.qml draws them.
    return f'<svg class="ic {cls}" width="{size}" height="{size}"><use href="#i-{name}"/></svg>'


def png(path):
    return "data:image/png;base64," + base64.b64encode((TREE / path).read_bytes()).decode()


APP_ICON_64 = png("packaging/linux/icons/hicolor/64x64/apps/hikarisub.png")

# ---------------------------------------------------------------------------------------------
# The frame


def caption(kind, hover=None, press=None):
    glyph, name = {"max": ("window-maximize", "Maximise"), "close": ("window-close", "Close")}[kind]
    cls = "wb" + (" hover" if hover == kind else "") + (" press" if press == kind else "")
    return f'<span class="{cls}" title="{name}">{ic(glyph)}</span>'


def titlebar(title, resizable=False, inactive=False, hover=None, press=None, centred=False):
    btns = (caption("max", hover, press) if resizable else "") + caption("close", hover, press)
    cls = "dtb" + (" inactive" if inactive else "") + (" centred" if centred else "")
    return f'<div class="{cls}"><span class="dtitle">{title}</span><span class="wbs">{btns}</span></div>'


def dialog(title, body, foot, width, resizable=False, inactive=False, hover=None, press=None, frame="custom",
           centred=False, style="", extra_cls=""):
    """frame: custom (the frame DG1 adds), windows (Windows' own bar, theme-following), linux-sys (the
    window manager's decoration, drawn as a placeholder), none (bare: Hyprland/sway borders only)."""
    if frame == "custom":
        top = titlebar(title, resizable, inactive, hover, press, centred)
    elif frame == "windows":
        top = (f'<div class="wintb"><span class="dtitle">{title}</span><span class="wbs">'
               f'{"<span class=wb>" + ic("window-maximize") + "</span>" if resizable else ""}'
               f'<span class="wb">{ic("window-close")}</span></span></div>')
    elif frame == "linux-sys":
        top = f'<div class="sysframe">{title} <span class="sysnote">(the window manager draws this bar)</span></div>'
    else:
        top = ""
    cls = "dlg" + (" fr-" + frame) + (" " + extra_cls if extra_cls else "")
    return (f'<div class="{cls}" style="width:{width}px;{style}">{top}<div class="dbody">{body}</div>'
            f'<div class="dfoot">{foot}</div></div>')


def btn(text, primary=False, focus=False, lead=False, hover=False):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" lead" if lead else "") + (
        " hov" if hover else "")
    return f'<span class="{cls}">{text}</span>'


def checkbox(text, on):
    return (f'<div class="cb"><span class="box{" on" if on else ""}">'
            f'{"<svg width=12 height=12 viewBox=\"0 0 12 12\"><path d=\"M2.5 6.5l2.5 2.5 4.5-5.5\" fill=none stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>" if on else ""}'
            f'</span>{text}</div>')


# ---------------------------------------------------------------------------------------------
# The five dialogs

VERSION = "0.1.0 (portable)"

# The Qt rewrite's own components (vcpkg.json, plus libass's text stack), with their licences.
COMPONENTS = [
    ("Qt", "The Qt Company Ltd. and other contributors", "LGPL 3"),
    ("KDDockWidgets", "Klarälvdalens Datakonsult AB (KDAB)", "GPL 2 or 3"),
    ("FFmpeg", "The FFmpeg developers", "LGPL 2.1 or later"),
    ("FFMS2", "Fredrik Mellbin and contributors", "MIT"),
    ("libass", "libass contributors", "ISC"),
    ("FreeType", "David Turner, Robert Wilhelm, Werner Lemberg and others", "FreeType licence"),
    ("FriBidi", "FriBidi contributors", "LGPL 2.1 or later"),
    ("HarfBuzz", "HarfBuzz contributors", "MIT"),
    ("LuaJIT", "Mike Pall", "MIT"),
    ("Boost", "Boost contributors", "Boost Software Licence 1.0"),
    ("ICU", "Unicode, Inc. and others", "Unicode licence"),
    ("Hunspell", "László Németh and others", "MPL 1.1, GPL 2 or LGPL 2.1"),
    ("PortAudio", "Ross Bencina, Phil Burk and contributors", "MIT"),
    ("curl", "Daniel Stenberg and contributors", "curl licence"),
    ("PCRE2", "University of Cambridge and others", "BSD 3-clause"),
    ("uchardet", "uchardet contributors", "MPL 1.1, GPL 2 or LGPL 2.1"),
    ("CSRI", "David Lamparter · Windows only", "BSD"),
    ("VSFilter", "Gabest and contributors · when VSFiltermod is installed", "GPL 2 or later"),
]


def about(focus_copy=False, **kw):
    body = (
        '<div class="ahead">'
        f'<img class="alogo" src="{APP_ICON_64}" width="64" height="64" alt="">'
        '<div class="ameta"><div class="aname">HikariSub</div>'
        f'<div class="aver">Version {VERSION}'
        f'<span class="qb{" ring" if focus_copy else ""}" title="Copy version">{ic("edit-copy", 14)}</span></div>'
        '<div class="aby">Subtitle editor by altqx.</div>'
        '</div></div>'
        '<p class="alic">HikariSub is free software under the GNU General Public License, version 3. It comes '
        'with no warranty.</p>'
        '<div class="alinks">'
        f'<span class="lnk">{ic("link", 14)}HikariSub website</span>'
        f'<span class="lnk">{ic("report-issue", 14)}Report an issue</span></div>'
    )
    return dialog("About HikariSub", body, btn("OK", primary=True, focus=not focus_copy), 460, **kw)


PEOPLE = [
    ("Development", [("altqx", "Design and development")]),
    ("Translation", [("altqx", "Korean, Polish, Tamil and Thai")]),
]


def credits(**kw):
    s = []
    for head, people in PEOPLE:
        s.append(f'<div class="seclabel">{head}</div><div class="plist">')
        s.extend(f'<span class="pname">{n}</span><span class="pwhat">{w}</span>' for n, w in people)
        s.append('</div>')
    rows = "".join(f'<div class="crow"><span class="cname">{n}</span><span class="cby">© {by}'
                   f'<span class="clic">{lic}</span></span></div>' for n, by, lic in COMPONENTS)
    s.append('<div class="seclabel">Built with</div>'
             f'<div class="clist"><div class="cscroll">{rows}</div>'
             '<span class="sbar" style="top:4px;height:96px"></span></div>')
    return dialog("Credits", "".join(s), btn("OK", primary=True, focus=True), 540, **kw)


NOTES = """New
• Translation mode: the original text stays beside the translation, with its own tags.
• Karaoke check: a modeless window that steps through the syllables against the audio.
• Help › What's new shows these notes.

Fixed
• The video no longer jumps a frame when a line is selected during playback.
• Shift times keeps the selection after it runs.
• The audio display redraws after a theme change."""


def update_available(state="idle", requirement=False, **kw):
    notes = NOTES.replace("\n", "<br>")
    body = (
        '<div class="uhead">'
        f'{ic("check-updates", 32, "big")}'
        '<div><div class="uname">HikariSub 0.2.0</div>'
        f'<div class="umuted">You have version {VERSION}.</div></div></div>'
        f'<div class="unotes"><div class="unotes-in">{notes}</div><span class="sbar" style="top:4px;height:150px"></span></div>'
    )
    if requirement:
        body += (f'<div class="ureq">{ic("notice-warning", 16, "warn")}'
                 'This version needs Windows 10 1809 or later.</div>')
    if state == "downloading":
        body += ('<div class="uprog"><div class="pbar"><span style="width:42%"></span></div>'
                 '<span class="umuted">Downloading… 42 %</span></div>')
    body += ('<div class="uopts">' + checkbox("Check for updates automatically", True)
             + checkbox("Stable versions only", True) + checkbox("Install updates automatically", False) + '</div>')
    if state == "downloading":
        foot = btn("Remind me in a week", lead=True) + btn("Close", focus=True)
    elif requirement:
        foot = btn("Remind me in a week", lead=True) + btn("Close", primary=True, focus=True)
    else:
        foot = btn("Remind me in a week", lead=True) + btn("Download and install", primary=True, focus=True) + btn("Close")
    return dialog("A new version is available", body, foot, 560, resizable=True, **kw)


def message(title, text, glyph="about", **kw):
    body = (f'<div class="msg">{ic(glyph, 32, "big" + (" warn" if glyph == "notice-warning" else ""))}'
            f'<p>{text}</p></div>')
    return dialog(title, body, btn("OK", primary=True, focus=True), 468, **kw)


def aur_notice(prerelease=False, **kw):
    if prerelease:
        text = ('HikariSub 0.2.0-beta.1 is a prerelease. The AUR package carries stable versions only. To try it, '
                'use the AppImage from the release page.')
    else:
        text = ('HikariSub 0.2.0 is available; you have 0.1.0. You installed HikariSub from the AUR, so update it '
                'with your package manager or AUR helper, for example <code>yay -Syu</code> or '
                '<code>paru -Syu</code>.')
    body = f'<div class="msg">{ic("check-updates", 32, "big")}<p>{text}</p></div>'
    foot = btn("What's new", lead=True) + btn("Remind me in a week") + btn("Close", primary=True, focus=True)
    return dialog("A new version is available", body, foot, 520, **kw)


# ---------------------------------------------------------------------------------------------
# Page


CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(page)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:%(pagew)spx}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
h1{font-size:18px;font-weight:600;margin:0 0 4px}
h2{font-size:13px;font-weight:650;margin:26px 0 8px}
p.note{color:%(muted)s;font-size:12px;margin:0 0 8px;max-width:1280px;line-height:1.45}
.cap{font-size:11px;color:%(muted)s;margin:6px 0 0;max-width:600px;line-height:1.4}
.row{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap}
.col{display:flex;flex-direction:column}

/* main window backdrop */
.app{position:relative;width:1280px;height:800px;background:url(%(body)s) 0 0 no-repeat;outline:1px solid %(line)s}
.app .dlg{position:absolute}

/* the dialog window */
.dlg{background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s)}
.dlg.noshadow{box-shadow:none}
.dtb,.wintb{position:relative;display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtitle{flex:1;min-width:0;padding-left:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dtb.centred .dtitle{position:absolute;left:0;right:0;text-align:center;padding:0 60px;pointer-events:none}
.dtb.inactive .dtitle,.dtb.inactive .wb{color:%(disabled)s}
.wbs{display:flex;margin-left:auto;flex:none;z-index:1}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.wb.hover{background:%(raised)s;%(hcring)s}
.wb.press{background:%(press)s;%(hcring)s}
.wintb{height:31px;background:%(winbar)s;color:%(wintext)s;font:12px 'Liberation Sans',Arial,sans-serif;box-shadow:none}
.wintb .dtitle{padding-left:14px}
.wintb .wb{color:%(wintext)s;height:31px}
.sysframe{height:30px;display:flex;align-items:center;padding-left:12px;gap:6px;font:12px 'Liberation Sans',Arial,sans-serif;
          color:#ddd;background:repeating-linear-gradient(135deg,#3a3f46 0 8px,#353a40 8px 16px);flex:none}
.sysnote{color:#9aa}
.fr-none{outline:3px solid #5e81ac}
.dbody{padding:12px 24px 0;display:flex;flex-direction:column;gap:0}
.dfoot{display:flex;gap:8px;justify-content:flex-end;padding:24px 24px 24px}
.dfoot .lead{margin-right:auto}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;min-width:80px;height:30px;padding:0 10px;border-radius:1px;
   background:%(raised)s;border:1px solid %(line)s;color:%(text)s;white-space:nowrap}
.b.hov{background:%(select)s;border-color:%(accent)s}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.ring{outline:1px solid %(text)s;outline-offset:1px}
.qb{display:inline-flex;align-items:center;justify-content:center;width:22px;height:22px;margin-left:6px;border-radius:1px;color:%(text)s}
.qb.ring{outline-offset:0}
.lnk{display:inline-flex;align-items:center;gap:6px;color:%(accent)s}
.lnk .ic{color:%(accent)s}
.seclabel{font-size:11px;font-weight:650;color:%(muted)s;margin:18px 0 6px;letter-spacing:.02em}
.cb{display:flex;align-items:center;gap:8px;height:24px}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.sbar{position:absolute;right:3px;width:4px;border-radius:2px;background:%(control)s;opacity:.8}
code{font:12px 'Liberation Mono',Consolas,monospace;background:%(raised)s;padding:1px 4px;border-radius:1px;
     outline:1px solid %(line)s}
.big{color:%(text)s}
.warn{color:%(warning)s}
.warn .acc{color:%(warning)s}

/* About */
.ahead{display:flex;gap:16px;align-items:center;margin-top:4px}
.alogo{flex:none}
.aname{font-size:18px;font-weight:650}
.aver{display:flex;align-items:center;color:%(muted)s;margin-top:2px}
.aby{margin-top:6px;line-height:1.4}
.alinks{display:flex;gap:24px;margin-top:16px}
.clist{position:relative;height:250px;background:%(field)s;outline:1px solid %(line)s;overflow:hidden}
.cscroll{padding:6px 14px 6px 10px}
.crow{display:flex;gap:10px;line-height:18px;padding:2px 0}
.cname{flex:none;width:104px}
.cby{color:%(muted)s;flex:1}
.clic{display:block;font-size:11px;color:%(muted)s;opacity:.85}
.alic{margin-top:14px;line-height:1.45;color:%(muted)s}

/* Credits */
.plist{display:grid;grid-template-columns:110px 1fr;column-gap:12px;row-gap:5px;line-height:1.4}
.pname{font-weight:650}
.pwhat{color:%(muted)s}
.ptext{line-height:1.45}
.dbody .seclabel:first-child{margin-top:6px}

/* Updates */
.uhead{display:flex;gap:14px;align-items:center;margin-top:4px}
.uname{font-size:18px;font-weight:650}
.umuted{color:%(muted)s;margin-top:2px}
.unotes{position:relative;margin-top:16px;height:158px;background:%(field)s;outline:1px solid %(line)s;overflow:hidden}
.unotes-in{padding:8px 16px 8px 10px;line-height:1.5}
.ureq{display:flex;gap:8px;align-items:center;margin-top:12px;color:%(text)s}
.uprog{display:flex;gap:12px;align-items:center;margin-top:12px}
.pbar{flex:1;height:6px;background:%(raised)s;outline:1px solid %(line)s;border-radius:1px;overflow:hidden}
.pbar span{display:block;height:100%%;background:%(accent)s}
.uopts{margin-top:12px;display:flex;flex-direction:column;gap:2px}
.msg{display:flex;gap:16px;align-items:flex-start;margin-top:8px}
.msg p{line-height:1.5;max-width:400px;padding-top:6px}

/* today */
.today{outline:1px solid %(line)s;display:block}
.notes{background:%(raised)s;outline:1px solid %(line)s;padding:12px 16px;width:1280px;line-height:1.5}
.notes td{padding:3px 14px 3px 0;vertical-align:top}
.notes td:first-child{font-weight:650;white-space:nowrap}
"""


def page(key, content, width=1280):
    th = THEMES[key]
    css = CSS % dict(th, page=th["bg"], pagew=width + 48, body=f"shots/body-{th['code']}.png",
                     shadow=".55" if key in DARKISH else ".28",
                     hcring=f"outline:1px solid {th['line']};outline-offset:-1px" if key in HC else "",
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "",
                     winbar="#202020" if key in DARKISH else "#F3F3F3",
                     wintext="#FFFFFF" if key in DARKISH else "#1A1A1A")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')


def overview(key):
    th = THEMES[key]
    # About, 460 wide, centred on the 1280x800 main window and moved 20 down (MuseScore's rule).
    s = [f'<h1>DG1 dialog frame: About over the main window ({th["name"]})</h1>',
         '<p class="note">The dialog is its own top-level window in the custom title-bar style: the title, and '
         'Close (Maximise too when the dialog can be resized; never Minimise). It opens centred on the window '
         'that opened it, 20 lower, and later where it was left. It is modeless: the main window behind it stays '
         'usable and is not dimmed. Windows draws the shadow; on Wayland and X11 the frame is a 1-pixel line '
         'with no shadow, as SH1’s main window. The main window is today’s build (qt 9c1eef089).</p>',
         f'<div class="app">{dialog_at(about(), 410, 270)}</div>']
    return "".join(s)


def dialog_at(html, x, y):
    return html.replace('style="width:', f'style="left:{x}px;top:{y}px;width:', 1)


def dialogs(key):
    th = THEMES[key]
    s = [f'<h1>DG1: About, Credits and the update notices ({th["name"]})</h1>',
         '<p class="note">Every dialog shows its default button in the accent with the keyboard ring on it, '
         'where focus starts. Footers follow HikariSub’s rule: OK before Close on Windows and KDE (shown), after '
         'it on GNOME and macOS; extra actions sit at the leading edge. Release notes, the version and every '
         'string are examples.</p>',
         '<div class="row">',
         f'<div class="col">{about()}<p class="cap">About: fixed size. The version copies with the button '
         'beside it. The links open the browser. No legacy attribution; the GPL notice the licence asks of an '
         'interactive program.</p></div>',
         f'<div class="col">{credits()}<p class="cap">Credits: the Qt rewrite’s own people, and the components '
         'it is built on (moved here from About) with their licences; the list scrolls. CSRI and VSFilter show '
         'only where they apply.</p></div>',
         '</div>',
         '<div class="row" style="margin-top:28px">',
         f'<div class="col">{update_available()}<p class="cap">Update available: resizable (the notes grow). '
         '“Open the download page” replaces “Download and install” where HikariSub does not install updates '
         'itself.</p></div>',
         f'<div class="col">{update_available("downloading")}<p class="cap">While downloading: the progress '
         'replaces the install button. Closing the window keeps the download going; the update-ready bar (unchanged) '
         'takes over when it is done.</p></div>',
         '</div>',
         '<div class="row" style="margin-top:28px">',
         f'<div class="col">{update_available(requirement=True)}<p class="cap">A release this system cannot run: '
         'the requirement in the warning colour, no install button.</p></div>',
         '<div class="col" style="gap:20px">',
         message("Check for updates", "You already have the latest version."),
         message("Check for updates", "Cannot check for updates. Check the connection and try again from Help › "
                 "Check for updates.", "notice-warning"),
         message("Update", "HikariSub 0.2.0 did not start, so the version you had was put back.", "notice-warning"),
         '<p class="cap" style="margin-top:-12px">Update message: one notice window with a type icon (information, '
         'or the new warning glyph); titled with the command that opened it (“Check for updates”), or “Update” for '
         'the installer’s notices.</p>',
         '</div></div>',
         '<div class="row" style="margin-top:28px">',
         f'<div class="col">{aur_notice()}<p class="cap">AUR notice (installed from the AUR): the commands in '
         'monospace.</p></div>',
         f'<div class="col">{aur_notice(prerelease=True)}<p class="cap">AUR notice for a prerelease.</p></div>',
         '</div>',
         '<h2>Today (qt 9c1eef089): in-window, modal, one unformatted label each</h2>',
         f'<div class="row"><img class="today" src="shots/crop-about-{th["code"]}.png">'
         f'<img class="today" src="shots/crop-credits-{th["code"]}.png"></div>']
    return "".join(s)


def frame(key):
    th = THEMES[key]
    small = lambda **kw: message("Check for updates", "You already have the latest version.", **kw)
    s = [f'<h1>DG1 dialog frame: states and behaviour ({th["name"]})</h1>',
         '<h2>Title bar: active; not active (disabled role); Close under the pointer (the theme’s hover, no red, '
         'as SH1); Close pressed</h2>',
         '<div class="row">',
         *(f'<div class="col">{d}<p class="cap">{c}</p></div>' for d, c in (
             (small(), "Active"), (small(inactive=True), "Not active"),
             (small(hover="close"), "Close under the pointer"), (small(press="close"), "Close pressed"))),
         '</div>',
         '<h2>Resizable dialogs add Maximise; a long title elides before the buttons</h2>',
         '<div class="row">',
         message("A new version is available", "Maximise shows only on a dialog that can be resized "
                 "(Update available here; Settings, the Style editor and the Style manager later).", resizable=True,
                 hover="max"),
         message("Resample the subtitles to the video’s resolution and keep the aspect ratio", "The title takes "
                 "the room left of the buttons and elides at its end."),
         '</div>',
         '<h2>Title placement: at the leading edge (recommended, left) or centred like the main window (right)</h2>',
         '<div class="row">', small(), small(centred=True), '</div>',
         '<h2>“Use system title bar” on (Settings › Appearance; the default on tiling window managers): the '
         'window system’s own frame, nothing drawn</h2>',
         '<div class="row">',
         f'<div class="col">{small(frame="windows")}<p class="cap">Windows: its own bar, dark under Dark and High '
         'contrast black (title_bars.h), light otherwise.</p></div>',
         f'<div class="col">{small(frame="linux-sys")}<p class="cap">KDE Plasma, GNOME, X11 window managers: '
         'their decoration.</p></div>',
         f'<div class="col">{small(frame="none", extra_cls="noshadow")}<p class="cap">Hyprland, sway, niri: '
         'a floating window with the compositor’s border; Esc and Close in the footer close it.</p></div>',
         '</div>',
         '<h2>Keyboard: Tab moves the ring (here onto the version’s copy button); Enter is the default button; '
         'Esc closes</h2>',
         f'<div class="row">{about(focus_copy=True)}<div class="col" style="gap:12px">',
         '<p class="cap">GNOME and macOS footer order (Close last):</p>',
         dialog("A new version is available", '<p>…</p>',
                btn("Remind me in a week", lead=True) + btn("Close") + btn("Download and install", primary=True,
                                                                            focus=True), 560, resizable=True),
         '</div></div>',
         '<h2>Behaviour (the frame component every dialog uses; DG2, SH3, PF1 and SY1 build on it)</h2>',
         '<div class="notes"><table>',
         '<tr><td>Window</td><td>A real top-level window, transient for its opener (stays above it, minimises '
         'with it, gets no taskbar entry). Windows: SH1’s native frame code (the bar is the caption: drag moves, '
         'the right-click system menu, double-click maximises only a resizable dialog). Wayland and X11: frameless '
         'with SH1’s 1-pixel line and resize band.</td></tr>',
         '<tr><td>First open</td><td>Centred on the window that opened it (the main window, a floating panel, '
         'the Style manager), moved 20 down, kept 20 inside the screen’s free area, shrunk if it does not fit.</td></tr>',
         '<tr><td>Later opens</td><td>Where it was left, at the size it was left, per dialog (a fixed-size dialog '
         'keeps its position only). A saved place that is no longer on a screen (monitor unplugged) falls back to '
         'the first-open rule, using the floating panels’ screen rules. Wayland: size only, the compositor places '
         'the window.</td></tr>',
         '<tr><td>Modality</td><td>Per dialog. About, Credits and the update notices are modeless: one window '
         'each; opening it again raises it. Questions that block an operation (DG2) are modal to their parent '
         'window only, so floating panels stay usable.</td></tr>',
         '<tr><td>Keys</td><td>Esc closes (as Close or Cancel; a field or a list takes Esc first). Enter presses '
         'the default button from any single-line field. Focus starts on the first field, else on the default '
         'button. Tab and Shift+Tab stay inside the dialog.</td></tr>',
         '<tr><td>Closing</td><td>Focus goes back to the control that opened it, or to the opener’s last focused '
         'control when a menu or shortcut opened it. Closing the main window closes its dialogs.</td></tr>',
         '<tr><td>Theme</td><td>Follows the theme live: the bar, body and buttons repaint on a switch, like the '
         'main window.</td></tr>',
         '</table></div>']
    return "".join(s)


def main():
    for key in THEMES:
        (OUT / f"dg1-{key}.html").write_text(page(key, overview(key)))
        (OUT / f"dg1-{key}-dialogs.html").write_text(page(key, dialogs(key)))
        (OUT / f"dg1-{key}-frame.html").write_text(page(key, frame(key)))


main()
