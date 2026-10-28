#!/usr/bin/env python3
"""NT1 #302 mockup: per theme, the OK-only notices as toasts, the ones that stay modal as message boxes,
and the Log as a dock panel. Built on PK1's mockup generator (evidence/w6-pk1-mockup).

The main window behind the toasts and the Log panel is a real screenshot of today's HikariSub (qt ce0c6740e)
in that theme, taken by body.sh under vx11. Everything else is drawn here from the theme layer's roles
(src/ui/theme.cpp baseRoles, default accent), DG1's dialog frame (DialogWindow.qml), the colour
popover's frame (ColourPopover.qml: padding 12, radius 4, a 1-pixel line, a shadow) and IC1's icons."""
import pathlib
import re
import sys

TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-nt1")
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

# New glyphs in IC1's 16-unit, 1-wide stroke style (the accent group takes the severity colour).
GLYPHS = {
    "notice-error": '<path d="M5.5 1.5h5l4 4v5l-4 4h-5l-4-4v-5z"/><g class="acc"><path d="M8 4.75v4"/>'
                    '<circle cx="8" cy="11.25" r=".75" fill="currentColor" stroke="none"/></g>',
    "calendar": '<rect x="1.5" y="2.5" width="13" height="12" rx="1"/><path d="M1.5 6h13M5 1v3M11 1v3"/>'
                '<g class="acc"><path d="M4.5 9h1M7.5 9h1M10.5 9h1M4.5 11.5h1M7.5 11.5h1"/></g>',
    "chevron-down": '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/>',
    "chevron-right": '<path d="M6.5 4.5l3.5 3.5-3.5 3.5"/>',
    "chevron-left": '<path d="M9.5 4.5l-3.5 3.5 3.5 3.5"/>',
    "check": '<path d="M3.5 8.5l3 3 6-7"/>',
}
REPO_ICONS = ["about", "notice-warning", "search", "tab-close", "log", "styles", "hotkeys", "open-audio",
              "spellchecker", "remove-temporary", "check-updates", "window-close", "window-maximize", "edit-copy",
              "save", "menu-more", "filter", "panels", "show", "hide", "panel-float", "last-session", "panel-menu"]


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


# ---------------------------------------------------------------------------------------------
# DG1's frame and controls


def titlebar(title, resizable=False):
    btns = ((f'<span class="wb">{ic("window-maximize")}</span>' if resizable else "")
            + f'<span class="wb">{ic("window-close")}</span>')
    return f'<div class="dtb"><span class="dtitle">{title}</span><span class="wbs">{btns}</span></div>'


def window(title, body, foot, width, resizable=False, style="", foot_cls=""):
    return (f'<div class="dlg" style="width:{width}px;{style}">{titlebar(title, resizable)}'
            f'<div class="dbody">{body}</div><div class="dfoot {foot_cls}">{foot}</div></div>')


def btn(text, primary=False, focus=False, lead=False, danger=False, small=False, flat=False):
    cls = "b" + (" pri" if primary else "") + (" ring" if focus else "") + (" lead" if lead else "") + (
        " dng" if danger else "") + (" sm" if small else "") + (" flat" if flat else "")
    return f'<span class="{cls}">{text}</span>'


def box(on, partial=False):
    mark = ('<svg width=12 height=12 viewBox="0 0 12 12"><path d="M2.5 6.5l2.5 2.5 4.5-5.5" fill=none '
            'stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg>')
    if partial:
        mark = '<span class="part"></span>'
    return f'<span class="box{" on" if on or partial else ""}">{mark if on or partial else ""}</span>'


def checkbox(text, on, focus=False, extra=""):
    return f'<div class="cb{" ring" if focus else ""}">{box(on)}<span class="cbt">{text}</span>{extra}</div>'


def field(text, width=None, placeholder=False, focus=False, icon=None, trail=None, mono=False):
    w = f"width:{width}px;" if width else ""
    cls = "fld" + (" ph" if placeholder else "") + (" focus" if focus else "") + (" mono" if mono else "")
    lead = ic(icon, 16, "fic") if icon else ""
    tail = f'<span class="ftail">{trail}</span>' if trail else ""
    return f'<span class="{cls}" style="{w}">{lead}<span class="ftext">{text}</span>{tail}</span>'


def combo(text, width=None):
    return field(text, width, trail=ic("chevron-down", 14))


def segmented(options, chosen, small=False):
    return ('<span class="seg' + (" sm" if small else "") + '">' + "".join(
        f'<span class="{"on" if o == chosen else ""}">{o}</span>' for o in options) + '</span>')


def popover(body, width, style="", cls=""):
    return f'<div class="pop {cls}" style="width:{width}px;{style}">{body}</div>'


# ---------------------------------------------------------------------------------------------
# Message box


SEVERITY = {"info": ("about", ""), "warning": ("notice-warning", "warn"), "error": ("notice-error", "err")}


def message_box(title, main, detail="", severity="info", foot="", width=468, extra="", details_open=None):
    glyph, cls = SEVERITY[severity]
    body = (f'<div class="mb">{ic(glyph, 32, "big " + cls)}<div class="mbtext"><div class="mbmain">{main}</div>'
            + (f'<div class="mbdetail">{detail}</div>' if detail else "") + extra + '</div></div>')
    if details_open is not None:
        body += (f'<div class="disc">{ic("chevron-down" if details_open else "chevron-right", 14)}Details</div>')
        if details_open:
            body += (f'<div class="dets"><pre>{details_open}</pre>'
                     f'<span class="qb" title="Copy details">{ic("edit-copy", 14)}</span></div>')
    return window(title, body, foot, width)


def message_boxes():
    return [
        (message_box("Delete style", "Delete the style “Song romaji”?",
                     "12 lines use it. They will use Default instead.", "warning",
                     btn("Delete", danger=True) + btn("Cancel", primary=True, focus=True)),
         "A destructive choice: the verb in the danger colour, and Cancel, the safe button, as the default "
         "(Enter). The title names the command, never “Confirmation”."),
        (message_box("Restore last session", "Open the files from the last session?",
                     "Episode 03.ass and Episode 03.mkv", "info",
                     btn("Open", primary=True, focus=True) + btn("Not now"),
                     extra=f'<div class="mbcheck">{checkbox("Don’t ask again", False)}</div>'),
         "A question legacy had a setting for (Settings › General › On start): “Don’t ask again” writes that "
         "setting. Settings › General › “Ask again” turns every hidden question back on."),
        (message_box("Open subtitles", "Cannot open “Episode 04.ass”.",
                     "The file is not UTF-8 and its encoding could not be detected.", "error",
                     btn("Choose encoding…", lead=True) + btn("OK", primary=True, focus=True),
                     details_open="uchardet: no confident guess (0.31)\nfirst bad byte at 0x1A2F (line 48)"),
         "An error that stops an action stays modal. The technical text sits under Details, collapsed until "
         "asked for, with a copy button."),
    ]


# ---------------------------------------------------------------------------------------------
# Notices


def toast(severity, title, text="", actions="", fade=False, hover=False):
    glyph, cls = SEVERITY[severity]
    return (f'<div class="toast{" hover" if hover else ""}">{ic(glyph, 16, cls)}<div class="tt">'
            f'<div class="ttitle">{title}</div>' + (f'<div class="ttext">{text}</div>' if text else "")
            + (f'<div class="tacts">{actions}</div>' if actions else "") + '</div>'
            f'<span class="tx" title="Dismiss">{ic("tab-close", 14)}</span></div>')


def inline_bar(severity, text, actions):
    glyph, cls = SEVERITY[severity]
    return (f'<div class="ibar">{ic(glyph, 16, cls)}<span class="ibt">{text}</span>{actions}'
            f'<span class="tx" title="Close">{ic("tab-close", 14)}</span></div>')




# Page

CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{background:%(page)s;color:%(text)s;font:13px 'Liberation Sans',Arial,sans-serif;padding:24px;width:1328px}
.acc{color:%(accent)s}
.ic{display:block;flex:none}
h1{font-size:18px;font-weight:600;margin:0 0 8px}
h2{font-size:13px;font-weight:650;margin:28px 0 10px}
p.note{color:%(muted)s;font-size:12px;margin:0 0 8px;max-width:1280px;line-height:1.45}
.cap{font-size:11px;color:%(muted)s;margin:8px 0 0;max-width:520px;line-height:1.4}
.row{display:flex;gap:28px;align-items:flex-start;flex-wrap:wrap}
.col{display:flex;flex-direction:column}
.notes{background:%(raised)s;outline:1px solid %(line)s;padding:12px 16px;width:1280px;line-height:1.5}
.notes td{padding:3px 14px 3px 0;vertical-align:top}
.notes td:first-child{font-weight:650;white-space:nowrap}

/* main window backdrop */
.app{position:relative;width:1280px;height:800px;background:url(%(body)s) 0 0 no-repeat;outline:1px solid %(line)s;margin-top:12px}
.barslot{position:absolute;left:0;top:64px;width:1280px}
.stack{position:absolute;right:12px;bottom:34px;width:360px;display:flex;flex-direction:column;gap:8px}

/* DG1 window */
.dlg{background:%(panel)s;color:%(text)s;display:flex;flex-direction:column;outline:1px solid %(line)s;
     box-shadow:0 8px 28px rgba(0,0,0,%(shadow)s)}
.dtb{display:flex;align-items:center;height:32px;flex:none;background:%(panel)s;%(tbline)s}
.dtitle{flex:1;min-width:0;padding-left:12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.wbs{display:flex;margin-left:auto;flex:none}
.wb{display:flex;align-items:center;justify-content:center;width:46px;height:32px;color:%(text)s}
.dbody{padding:12px 24px 0;display:flex;flex-direction:column}
.dfoot{display:flex;gap:8px;justify-content:flex-end;padding:24px}
.dfoot .lead,.pfoot .lead{margin-right:auto}

/* controls */
.b{display:inline-flex;align-items:center;justify-content:center;min-width:80px;height:30px;padding:0 10px;border-radius:1px;
   background:%(raised)s;border:1px solid %(line)s;color:%(text)s;white-space:nowrap;flex:none}
.b.sm{min-width:0;height:26px;padding:0 10px}
.b.on{background:%(select)s;border-color:%(accent)s}
.b.flat{background:none;border-color:transparent;color:%(accent)s;padding:0 6px;height:24px}
.b.pri{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s;font-weight:650}
.b.dng{color:%(danger)s;border-color:%(danger)s}
.ring{outline:1px solid %(text)s;outline-offset:1px}
.qb{display:inline-flex;align-items:center;justify-content:center;width:22px;height:22px;border-radius:1px;color:%(text)s}
.cb{display:flex;align-items:center;gap:8px;height:24px}
.box{width:16px;height:16px;border:1px solid %(control)s;background:%(field)s;border-radius:1px;display:flex;
     align-items:center;justify-content:center;flex:none}
.box.on{background:%(accent)s;border-color:%(accent)s;color:%(onaccent)s}
.fld{display:inline-flex;align-items:center;gap:6px;height:28px;padding:0 8px;background:%(field)s;border:1px solid %(line)s;
     border-radius:1px;color:%(text)s;min-width:0}
.fld.wide{display:flex;width:100%%}
.fld.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.fld.ph .ftext{color:%(muted)s}
.ftext{flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.fic,.ftail{color:%(muted)s}
.ftail{display:flex;margin-left:auto}
.frow{display:flex;align-items:center;gap:8px;margin:6px 0}
.flabel{width:56px;color:%(text)s;flex:none}
.seg{display:inline-flex;border:1px solid %(line)s;border-radius:1px;overflow:hidden;flex:none}
.seg span{padding:0 10px;height:26px;display:flex;align-items:center;background:%(raised)s;border-left:1px solid %(line)s}
.seg span:first-child{border-left:none}
.seg span.on{background:%(select)s;color:%(text)s;box-shadow:inset 0 -2px %(accent)s;font-weight:650}
.seg.sm span{height:24px;padding:0 8px}
.sbar{position:absolute;right:3px;width:4px;border-radius:2px;background:%(control)s;opacity:.8}
.big{color:%(text)s}
.warn,.warn .acc{color:%(warning)s}
.err,.err .acc{color:%(danger)s}
.lnk2{color:%(accent)s;cursor:pointer}

/* message box */
.mb{display:flex;gap:16px;align-items:flex-start;margin-top:8px}
.mbtext{display:flex;flex-direction:column;gap:6px;padding-top:4px;min-width:0}
.mbmain{font-size:14px;font-weight:650;line-height:1.4}
.mbdetail{color:%(muted)s;line-height:1.45}
.mbcheck{margin-top:8px}
.disc{display:flex;align-items:center;gap:4px;margin:14px 0 0 48px;color:%(text)s}
.dets{position:relative;margin:6px 0 0 48px;background:%(field)s;outline:1px solid %(line)s;padding:8px 34px 8px 10px}
.dets pre{font:12px 'Liberation Mono',Consolas,monospace;color:%(text)s;white-space:pre-wrap}
.dets .qb{position:absolute;right:6px;top:6px}

/* notices */
.toast{display:flex;gap:10px;align-items:flex-start;padding:10px 8px 10px 12px;background:%(panel)s;outline:1px solid %(line)s;
       border-radius:4px;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);color:%(text)s}
.toast.hover{background:%(raised)s}
.toast>.ic{margin-top:1px}
.tt{flex:1;min-width:0;display:flex;flex-direction:column;gap:3px}
.ttitle{font-weight:650;line-height:1.35}
.ttext{color:%(muted)s;line-height:1.4}
.tacts{display:flex;gap:4px;margin:4px 0 0 -6px}
.tx{display:flex;align-items:center;justify-content:center;width:22px;height:22px;color:%(muted)s;flex:none}
.ibar{display:flex;align-items:center;gap:8px;height:38px;padding:0 6px 0 10px;background:%(raised)s;
      box-shadow:inset 0 -1px %(line)s,inset 0 1px %(line)s;color:%(text)s}
.ibt{flex:1}

/* popover */
.pop{position:relative;background:%(panel)s;outline:1px solid %(line)s;border-radius:4px;padding:12px;
     box-shadow:0 6px 22px rgba(0,0,0,%(shadow)s);color:%(text)s;z-index:2}
.ptitle{font-weight:650;margin-bottom:8px}
.pfoot{display:flex;gap:6px;justify-content:flex-end;margin-top:10px}
.pfoot.wrap{flex-wrap:wrap;justify-content:flex-start}
.anchorbox{position:relative}

/* clash review */
.allrow{display:flex;align-items:center;gap:10px;margin-top:14px}
.ctable{margin-top:10px;outline:1px solid %(line)s;background:%(field)s}
.chead,.crow2{display:flex;align-items:center;gap:10px;padding:0 10px;min-height:38px}
.chead{min-height:28px;color:%(muted)s;font-size:12px;box-shadow:inset 0 -1px %(line)s;background:%(raised)s}
.crow2+.crow2{box-shadow:inset 0 1px %(line)s}
.crow2 .cname{font-weight:650}
.cname{width:110px;flex:none}
.cdiff{width:170px;flex:none;color:%(muted)s}
.cact{flex:1;display:flex;gap:8px;align-items:center}

/* choose styles */
.slist{position:relative;margin-top:8px;height:186px;overflow:hidden;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.srow{display:flex;align-items:center;gap:8px;height:22px;padding:0 14px 0 8px}
.srow.cur{background:%(select)s}
.sname{flex:1}
.scount{color:%(muted)s;font-size:12px}
.sfoot{display:flex;gap:14px;align-items:center;margin-top:10px}

/* hotkeys */
.htable{outline:1px solid %(line)s;background:%(field)s}
.hhead,.hrow{display:grid;grid-template-columns:1fr 130px;padding:0 10px;height:26px;align-items:center}
.hhead{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.hrow.sel{background:%(select)s}
.keys{color:%(muted)s}
.capture{display:flex;align-items:center;justify-content:center;gap:6px;height:48px;background:%(field)s;
         border:1px solid %(line)s;border-radius:1px}
.capture.focus{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}
.wait{color:%(muted)s}
.kc{display:inline-flex;align-items:center;height:28px;padding:0 10px;border:1px solid %(control)s;border-bottom-width:2px;
    border-radius:3px;background:%(raised)s;font-weight:650}
.plus{color:%(muted)s}
.conf{display:flex;gap:8px;align-items:flex-start;margin-top:10px;line-height:1.4}

/* lists */
.llist{margin-top:10px;outline:1px solid %(line)s;background:%(field)s;padding:2px 0}
.llist.pl{margin-top:0}
.lrow{display:flex;gap:10px;align-items:center;height:28px;padding:0 10px}
.lrow.cur{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.lnum{color:%(muted)s;width:12px}
.lmeta{color:%(muted)s;margin-left:auto}
.editor{height:30px;display:flex;align-items:center;padding:0 10px;background:%(field)s;border:1px solid %(line)s}
.miss{text-decoration:underline wavy %(danger)s;text-underline-offset:3px;background:%(select)s}

/* calendar */
.calhead{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
.calm{font-weight:650}
.cal{display:grid;grid-template-columns:repeat(7,32px);row-gap:2px}
.dh{height:24px;display:flex;align-items:center;justify-content:center;color:%(muted)s;font-size:11px}
.dd{height:28px;display:flex;align-items:center;justify-content:center;border-radius:1px}
.dd.out{color:%(disabled)s}
.dd.sel{background:%(accent)s;color:%(onaccent)s;font-weight:650;outline:1px solid %(text)s;outline-offset:1px}
"""


def page(key, content):
    th = THEMES[key]
    css = CSS % dict(th, page=th["bg"], body=f"shots/body-{th['code']}.png",
                     shadow=".55" if key in DARKISH else ".28",
                     focus=th["accent"] if key not in HC else th["text"],
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in HC else "")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{sprite()}'
            f'{content}</body></html>')

# ---------------------------------------------------------------------------------------------
# NT1: the OK-only notices


def gallery(items, cols=3, width=360):
    cells = "".join(f'<div class="gcell" style="width:{width}px">{d}<p class="cap">{c}</p></div>' for d, c in items)
    return f'<div class="gal">{cells}</div>'


DET = btn("Details", small=True, flat=True)

# (toast, caption): every OK-only notice that needs no answer (inventory rows A1-A28).
NOTICE_TOASTS = [
    (toast("warning", "Spellchecker", "No dictionary files were found in the “Dictionary” folder. Spell checking "
           "will be disabled"),
     "A1 · was “Spellchecker” box, OK. Also “Failed to initialize spell checker.” (error)."),
    (toast("error", "Add word to dictionary", "Error. Word “kawaii” was not added."),
     "A2 · was “Spellchecker” box, OK. The Line editor’s menu command names it."),
    (toast("info", "Spellchecker", "No spelling errors were found"),
     "A3 · was “Warning” over the Spellchecker window, OK. The window stays open."),
    (toast("info", "Delete unused styles", "2 styles deleted: Old OP, Karaoke romaji.", DET),
     "A5, A6 · was “Status of deleted styles”, OK, with the two lists. The lists (used, deleted) go to Details. "
     "From the Style manager’s Clear and the hotkey."),
    (toast("warning", "Open video", "The file is not a valid video file or is corrupted, or codecs or a splitter "
           "may be missing", DET),
     "A8 · the player notice (Windows, DirectShow): was “Warning”, OK. Details: the player’s own error."),
    (toast("warning", "Vector clipping", "Double “m” was blocked because of a VSFilter bug"),
     "A9 · the visual-tool warning: was “Warning”, OK. Title: the tool. Vector drawing the same."),
    (toast("info", "Run the last loaded script", "This subtitle file does not have any scripts added"),
     "A10 · automation Info: was “Info”, OK. The script errors stay message boxes (next page)."),
    (toast("info", "Load subtitles from an MKV/OGM file", "The file does not contain any subtitle tracks."),
     "A21 · no MKV tracks: was a box of the same title, OK."),
    (toast("info", "Check for updates", "You already have the latest version"),
     "A15 · was the update message window, OK."),
    (toast("warning", "Check for updates", "Cannot check for updates", DET),
     "A16 · was the update message window, OK. Details: the network error."),
    (toast("error", "Download and install", "Cannot download the update: the connection timed out.", DET),
     "A17 · was the “Update” window, OK. The offer comes back at the next check."),
    (toast("error", "Install update", "The update to 0.1.0 was not installed. This version still runs.", DET),
     "A18 · reported at the next start, was the “Update” window, OK. Details: the installer’s log lines."),
    (toast("warning", "Settings", "The selected catalog for conversion does not exist and will be changed to "
           "the default"),
     "A26 · was “Warning” over Settings, OK (one per missing item; now one toast each, no queue)."),
    (toast("info", "Select lines", "12 lines selected."),
     "A27 · was “Select” with Close and OK (Close also closed Select lines). Select lines stays open; its own "
     "Close closes it."),
    (toast("info", "Find", "Could not find the specified phrase “kawaii”."),
     "A28 · was “Confirmation”, OK. Search’s questions (Yes/No) are not notices: they stay for PU2."),
    (toast("info", "Replace all", "Replaced 12 times."),
     "A28 · was “Find and Replace”, OK."),
]

# Messages that popped the Log window up (B rows): now toasts, the title naming the command.
LOG_TOASTS = [
    (toast("error", "Open subtitles", "Could not open Episode 04.ass; nothing was changed.", DET),
     "B · L2, L5, L6. Errors stay until ×."),
    (toast("error", "Save", "Episode 03.ass could not be saved.", DET),
     "B · L1. Open question 3: these two kinds could be message boxes instead."),
    (toast("warning", "Open audio", "Delay failed, it’s longer than audio duration time"),
     "B · L24: the audio opens, the delay is dropped."),
    (toast("warning", "Resample subtitles", "3 values could not be scaled.", DET),
     "B · L21: one toast for the run, each value in Details (was one Log line each)."),
    (toast("warning", "Hotkeys", "Shortcut “Ctrl+Shift+Ä” is invalid"),
     "B · L27: the key is not bound; the rest load."),
    (toast("warning", "Language", "Cannot find translation, language change failed"),
     "B · L46: the interface stays in English."),
]


def toasts_page(key):
    th = THEMES[key]
    stack = "".join([
        toast("info", "Delete unused styles", "2 styles deleted: Old OP, Karaoke romaji.", DET, hover=True),
        toast("warning", "Spellchecker", "No dictionary files were found in the “Dictionary” folder. Spell "
              "checking will be disabled"),
        toast("info", "Load subtitles from an MKV/OGM file", "The file does not contain any subtitle tracks."),
    ])
    return "".join([
        f'<h1>NT1: OK-only notices become toasts ({th["name"]})</h1>',
        '<p class="note">Every notice whose only button was OK is now a toast on PK1’s stack: bottom right of the '
        'main window, above the status bar, three at most, the newest on top. Information fades after 5 seconds '
        '(the pointer holds it: the top one here is under it); warnings and errors stay until ×. The title names '
        'the command that raised it (never “Warning”, “Info” or “Confirmation”); the text is legacy’s, so its '
        'translations stay. Details, where there is more, opens the Log panel at that entry. Every notice is also '
        'in the Log; with Settings › General › Notices › “Show notices as toasts” off, only there.</p>',
        f'<div class="app"><div class="stack">{stack}</div></div>',
        '<h2>Every OK-only notice, as a toast (inventory part A, 16 popups)</h2>',
        gallery(NOTICE_TOASTS),
        '<h2>Messages that popped the Log window up are toasts too (inventory part B, 49 kinds; a sample)</h2>',
        '<p class="note">The Log no longer opens by itself. Each message that opened it is a toast with the '
        'severity its call site gives it and the command as its title; silent messages stay silent.</p>',
        gallery(LOG_TOASTS),
    ])


# ---------------------------------------------------------------------------------------------
# The ones that stay modal


OK = btn("OK", primary=True, focus=True)

KEEP_BOXES = [
    (message_box("Save", "Episode 03.ass is read-only.", "Save it under another name, or change the file’s "
                 "attribute.", "warning", btn("Save as…", primary=True, focus=True) + btn("Cancel"), width=400),
     "A19 · was “Warning”, OK, which then opened Save as. Now Save as… or Cancel. Save session (A20) the same "
     "with the session file."),
    (message_box("Replace", "The edited line cannot be committed.", "", "error", OK, width=400),
     "A4 · Spellchecker’s Replace and Replace all refused: was “Warning”."),
    (message_box("Save style", "Style named “Signs” already exists.", "", "error", OK, width=400),
     "A7 · Style manager: had no title of its own (kept “Status of deleted styles” after a Clear). New style: "
     "“The document has no styles to edit.”"),
    (message_box("Run the last loaded script", "Error loading Lua script: karaoke.lua", "", "error", OK, width=400,
                 details_open="karaoke.lua:42: unexpected symbol near 'end'"),
     "A11 · was “Error”. The script editor opens after it, as today. A12 (the helper ended) the same title."),
    (message_box("Edit script", "Cannot start editor.", "", "error", OK, width=400),
     "A13 · was “Automation error”."),
    (message_box("Run the last loaded script", "Validation Lua script “karaoke.lua” failed", "", "error", OK,
                 width=400, details_open="karaoke.lua:12: attempt to index a nil value (local 'line')"),
     "A14 · was “Error” plus a separate Log pop-up (L26); the runtime error is now its Details."),
    (message_box("Tag editing", "Field “Tag” cannot be empty.", "", "error", OK, width=400),
     "A22 · was “Error”. Vector shape editing (A23) the same, under its title."),
    (message_box("Manage font catalogs", "Enter a name for the new catalog", "", "warning", OK, width=400),
     "A24 · was “Info”; closing it returns to the name."),
    (message_box("Font collector", "Select the folder where you want to copy fonts", "", "warning", OK, width=400),
     "A25 · had no title. Its other refusals the same."),
    (message_box("Find in files", "Search path is invalid", "", "error", OK, width=400),
     "A29 · was “Confirmation”, OK."),
]


def boxes_page(key):
    th = THEMES[key]
    return "".join([
        f'<h1>NT1: OK-only notices that stay modal ({th["name"]})</h1>',
        '<p class="note">A notice for a command that was refused or failed stays a message box (PK1’s kind, on '
        'DG1’s frame, modal to its window): the title is the command, the severity icon, legacy’s text as the '
        'main line, technical text under Details. 13 popups, 10 shown (the others repeat a shown one). Legacy’s '
        'texts are kept so their translations stay; only titles and the read-only save’s buttons change.</p>',
        gallery(KEEP_BOXES, width=400),
    ])


# ---------------------------------------------------------------------------------------------
# The Log panel

LOG_ROWS = [
    ("06:31:04", "warning", "Hotkeys", "Shortcut “Ctrl+Shift+Ä” is invalid", []),
    ("06:31:05", "info", "Hotkeys", "The shortcut Ctrl+0 of “View all” was removed: it now zooms the video to fit.",
     []),
    ("06:32:40", "warning", "Spellchecker", "No dictionary files were found in the “Dictionary” folder. Spell "
     "checking will be disabled", []),
    ("06:33:12", "info", "Delete unused styles", "2 styles deleted: Old OP, Karaoke romaji.",
     ["Styles used: Default, Sign", "Styles deleted: Old OP, Karaoke romaji"]),
    ("06:34:13", "error", "Open subtitles", "Could not open Episode 04.ass; nothing was changed.",
     ["The file is not UTF-8 and its encoding could not be detected."]),
    ("06:35:50", "warning", "Resample subtitles", "3 values could not be scaled.",
     ["In line 48, value '1.2' cannot be scaled in tag '\\fscx'"]),
]


def log_row(t, sev, title, text, details, mark=False):
    glyph, cls = SEVERITY[sev]
    det = "".join(f'<div class="ldet">{d}</div>' for d in details)
    return (f'<div class="lr{" mark" if mark else ""}"><span class="lt">{t}</span>{ic(glyph, 16, cls)}'
            f'<div class="lm"><div><span class="lti">{title}</span> <span>{text}</span></div>{det}</div></div>')


def sev_toggle(sev, n, name):
    glyph, cls = SEVERITY[sev]
    return f'<span class="stg on" title="{name}">{ic(glyph, 16, cls)}<span>{n}</span></span>'


def log_panel(mark=3, first=2):
    rows = "".join(log_row(*r, mark=(i == mark)) for i, r in enumerate(LOG_ROWS) if first <= i <= mark)
    tools = (sev_toggle("info", 2, "Information") + sev_toggle("warning", 3, "Warnings")
             + sev_toggle("error", 1, "Errors") + '<span class="tsep"></span>'
             + field("Filter", 220, placeholder=True, icon="filter")
             + f'<span class="qb" style="margin-left:auto" title="Copy">{ic("edit-copy")}</span>')
    return (f'<div class="logdock"><div class="dhdr"><span class="dtab">Log<span class="dmore">{ic("panel-menu")}'
            f'</span></span></div><div class="lbody"><div class="ltools">{tools}</div>'
            f'<div class="lrows">{rows}<span class="sbar" style="top:30px;height:40px"></span></div></div></div>')


def menu(items, width=230):
    out = []
    for it in items:
        if it == "-":
            out.append('<div class="msep"></div>')
            continue
        text, icon, extra = it
        out.append(f'<div class="mi{" " + extra if extra else ""}"><span class="mic">{ic(icon) if icon else ""}'
                   f'</span><span class="mt">{text}</span></div>')
    return f'<div class="menu" style="width:{width}px">{"".join(out)}</div>'


def log_page(key):
    th = THEMES[key]
    file_menu = menu([("Remove temporary files", "remove-temporary", ""), ("Log", "log", "hl"),
                      ("Last session", "last-session", "")])
    panels = menu([("Video", None, ""), ("Audio", None, ""), ("…", None, ""), ("Search", None, ""),
                   ("Automation", None, ""), ("Log", None, "hl")], 170)
    sub = menu([("Show", "show", "hl"), ("Hide", "hide", "dis"), ("Float", "panel-float", "dis")], 140)
    settings = (f'<div class="grp"><div class="gt">Notices</div>{checkbox("Show notices as toasts (off: in the Log only)", True)}'
                f'<div class="frow" style="gap:10px">{btn("Ask again")}<span class="muted">Brings back the questions '
                'hidden with “Don’t ask again”.</span></div></div>')
    return "".join([
        f'<h1>NT1: the Log as a dock panel ({th["name"]})</h1>',
        '<p class="note">The Log window becomes the Log panel, a dock like Search and Automation: closed by default, '
        'along the bottom under the Grid when opened (it can be moved, tabbed or floated like any panel, and saved '
        'in layouts). It never opens by itself. A toast’s Details or “N more notices” opens it, scrolls to that '
        'notice and marks it (here “Delete unused styles”, scrolled to; the Grid shares the height with the panel). Each entry: the time, the severity, the command in '
        'semibold, legacy’s text, and its details below. The text can be selected and copied; Copy copies the '
        'entries shown.</p>',
        '<p class="note">Filters (new: today’s window and legacy’s have none): the three severity toggles with '
        'their counts, and a filter field over the text. The panel shows the whole Log from the start of the '
        'program, as File › Show / Hide log window does today.</p>',
        f'<div class="app">{log_panel()}</div>',
        '<h2>Where it is opened from, and the setting</h2>',
        '<div class="row">',
        f'<div class="col">{file_menu}<p class="cap" style="max-width:230px">File › “Show / Hide log window” '
        'becomes “Log”: it shows the panel, or hides it when it shows (legacy’s place in File stays).</p></div>',
        f'<div class="col"><div class="row" style="gap:2px">{panels}{sub}</div><p class="cap" style="max-width:320px">'
        'View › Panels lists the Log like every panel: Show, Hide, Float, Dock, and F6 reaches it while it is '
        'open.</p></div>',
        f'<div class="col">{settings}<p class="cap" style="max-width:460px">Settings › General › Notices (PK1’s, '
        'default on). Off: no toasts at all; every notice goes to the Log panel only. Message boxes are not '
        'notices and still show.</p></div>',
        '</div>',
    ])


NT1_CSS = """
.gal{display:flex;flex-wrap:wrap;gap:24px 28px;align-items:flex-start}
.gcell{display:flex;flex-direction:column}
.gcell .cap{max-width:100%%}
.muted{color:%(muted)s}

/* Log dock, over the Grid's lower rows */
.logdock{position:absolute;left:0;top:606px;width:1280px;height:166px;background:%(panel)s;
         box-shadow:0 -1px %(line)s;display:flex;flex-direction:column}
.dhdr{height:34px;background:%(bg)s;display:flex;flex:none}
.dtab{display:flex;align-items:center;gap:8px;padding:0 8px 0 10px;background:%(panel)s;font-weight:650}
.dmore{display:flex;color:%(text)s}
.lbody{flex:1;display:flex;flex-direction:column;min-height:0}
.ltools{display:flex;align-items:center;gap:4px;height:36px;padding:0 8px;flex:none}
.stg{display:inline-flex;align-items:center;gap:5px;height:26px;padding:0 8px;border:1px solid %(line)s;border-radius:1px;
     background:%(select)s;box-shadow:inset 0 -2px %(accent)s;font-variant-numeric:tabular-nums}
.tsep{width:1px;height:20px;background:%(line)s;margin:0 6px}
.lrows{position:relative;flex:1;overflow:hidden;background:%(field)s;box-shadow:inset 0 1px %(line)s;padding:2px 0}
.lr{display:flex;align-items:flex-start;gap:8px;padding:3px 10px;line-height:18px}
.lr .ic{margin-top:1px}
.lr.mark{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.lt{color:%(muted)s;font-variant-numeric:tabular-nums;width:56px;flex:none}
.lm{flex:1;min-width:0}
.lti{font-weight:650}
.ldet{color:%(muted)s;padding-left:14px}

/* menus and settings */
.menu{background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;box-shadow:0 10px 30px rgba(0,0,0,%(shadow)s)}
.mi{display:flex;align-items:center;height:26px;padding:0 12px 0 6px;gap:6px}
.mi.hl{background:%(select)s}
.mi.dis{color:%(disabled)s}
.mic{width:20px;display:flex;justify-content:center}
.msep{height:1px;background:%(line)s;margin:4px 8px}
.grp{background:%(panel)s;outline:1px solid %(line)s;padding:10px 14px 12px;width:460px}
.gt{font-weight:650;margin-bottom:6px}
"""


def nt1_main():
    for key in THEMES:
        for name, fn in (("toasts", toasts_page), ("boxes", boxes_page), ("log", log_page)):
            html = page(key, fn(key))
            th = THEMES[key]
            extra = NT1_CSS % dict(th, shadow=".55" if key in DARKISH else ".28")
            html = html.replace("</style>", extra + "</style>", 1)
            (OUT / f"nt1-{key}-{name}.html").write_text(html)


nt1_main()
