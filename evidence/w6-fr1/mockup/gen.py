#!/usr/bin/env python3
"""Shared mockup generator (PK1 #301, NT1 #302, AD3 #312): the theme roles (src/ui/theme.cpp baseRoles, default
accent), DG1's dialog frame and controls, PK1's message box and popover, IC1's icons. KA3 #278 appends ka3.py."""
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



# @@NT1@@

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
# FR1 #363: Getting started (first run and legacy upgrade). Appended to base.py by shoot.sh.

REPO_ICONS += ["language", "import", "appearance", "theme", "layout", "settings-video", "settings-audio", "editor",
               "folder-open", "show-in-folder", "autosave", "dictionary", "renderer", "audio-device", "audio-play",
               "whats-new", "help", "settings", "translation-mode", "karaoke", "report-issue", "credits",
               "monitor", "media-play", "window-minimize", "link"]

GRAMMAR = {"light": "#1F5FBF", "dark": "#7FB2FF", "hcwhite": "#0037B3", "hcblack": "#00E5FF"}
SPELL = {"light": "#FF6968", "dark": "#940000", "hcwhite": "#FF9C9C", "hcblack": "#C00000"}  # Theme.spellcheck

def radio(on, focus=False):
    return f'<span class="rad{" on" if on else ""}{" ring" if focus else ""}"></span>'


def mini_main(t, w=170, h=104):
    """The main window in theme t, drawn small: menu row, video, audio over the editor, grid."""
    vid = "#0B0E12" if t["code"] != "highContrastWhite" else "#1A1A1A"
    rows = "".join(f'<div style="height:4px;margin:3px 4px 0;background:{t["muted"]};opacity:.45;width:{x}%"></div>'
                   for x in (70, 55, 80, 62))
    return (f'<div class="mm" style="width:{w}px;height:{h}px;background:{t["bg"]};outline:1px solid {t["line"]}">'
            f'<div style="height:10px;background:{t["panel"]};display:flex;gap:4px;padding:3px 4px">'
            + "".join(f'<span style="width:12px;background:{t["text"]};opacity:.55"></span>' for _ in range(5))
            + f'</div><div style="display:flex;gap:2px;padding:2px;height:{int(h * .52)}px">'
            f'<div style="flex:44;background:{vid};display:flex;align-items:flex-end;justify-content:center;'
            f'padding-bottom:5px"><span style="width:46%;height:3px;background:#fff"></span></div>'
            f'<div style="flex:56;display:flex;flex-direction:column;gap:2px">'
            f'<div style="flex:2;background:{t["field"]};outline:1px solid {t["line"]};position:relative;overflow:hidden">'
            f'<svg width="100%" height="100%" viewBox="0 0 60 12" preserveAspectRatio="none"><path d="M0 6 L4 3 L8 9 L12 2 '
            f'L16 10 L20 5 L24 7 L28 1 L32 11 L36 4 L40 8 L44 3 L48 9 L52 5 L56 7 L60 6" fill="none" '
            f'stroke="{t["accent"]}" stroke-width="1"/></svg></div>'
            f'<div style="flex:3;background:{t["panel"]};outline:1px solid {t["line"]}">'
            f'<div style="margin:4px;height:10px;background:{t["field"]};outline:1px solid {t["accent"]}"></div></div>'
            f'</div></div><div style="margin:0 2px;background:{t["panel"]};height:{int(h * .48) - 18}px;'
            f'outline:1px solid {t["line"]}"><div style="height:5px;background:{t["select"]};margin-top:2px"></div>'
            f'{rows}</div></div>')


def block(label, flex, cls=""):
    return f'<div class="lb {cls}" style="flex:{flex}"><span>{label}</span></div>'


def col(*parts, flex=1):
    return f'<div class="lcol" style="flex:{flex}">{"".join(parts)}</div>'


def rowb(*parts, flex=1):
    return f'<div class="lrowb" style="flex:{flex}">{"".join(parts)}</div>'


def layout_thumb(kind):
    if kind == "Translate":
        inner = col(rowb(block("Video", 44, "vid"), col(block("Original text", 2), block("Line editor", 3), flex=56),
                         flex=58), block("Grid", 42))
    elif kind == "Time":
        inner = col(block("Audio", 30, "aud"), block("Line editor", 26), block("Grid", 44))
    elif kind == "Typeset":
        inner = col(rowb(block("Video", 74, "vid"), block("Styles", 26), flex=64),
                    rowb(block("Line editor", 55), block("Grid", 45), flex=36))
    else:
        inner = col(rowb(block("Video", 44, "vid"), block("Template editor", 56), flex=44), block("Audio · Karaoke", 30, "aud"),
                    block("Grid", 26))
    return f'<div class="lt">{inner}</div>'


LANGS = [("English", "English", ""), ("한국어", "Korean", "ko"), ("Polski", "Polish", ""), ("தமிழ்", "Tamil", "ta"),
         ("ไทย", "Thai", "th")]


IMPORT_ROWS = [
    (True, "Interface theme", "Light", "Dark", "Conflict"),
    (True, "Subtitle grid font", "Tahoma 10", "Liberation Sans 10", "Conflict"),
    (False, "Autosave files kept", "10", "3", "Conflict"),
    (True, "Dictionary", "Polish", "English (US)", "Conflict"),
    (True, "Play line (Audio hotkey)", "S", "Ctrl+Space", "Conflict"),
]


TREE_ROWS = [("hikari.ini", "your settings"), ("automation/", "your scripts and DependencyControl’s modules"),
             ("dictionaries/", "spelling dictionaries and your own words"), ("catalogs/", "font and style catalogs"),
             ("presets/", "karaoke templates, layouts and dialog presets"),
             ("autosave/  recovery/  log/", "backups and logs")]


# Entry points ---------------------------------------------------------------------------------


def help_menu():
    items = [("help", "&Getting started", True), ("link", "HikariSub &website", False),
             ("report-issue", "&Report an issue", False), ("check-updates", "Check for &updates", False),
             ("whats-new", "What's &new", False), ("about", "&About", False), ("credits", "&Credits", False)]
    rows = "".join(
        f'<div class="mi{" hot" if hot else ""}">{ic(i, 16)}<span>{t.replace("&", "")}</span></div>'
        for i, t, hot in items)
    return f'<div class="menu"><div class="mbar"><span>View</span><span>Subtitles</span><span class="on">Help</span>' \
           f'</div><div class="mdrop">{rows}</div></div>'


def whats_new_card():
    return ('<div class="wn"><div class="wnh">' + ic("whats-new", 16) + '<b>What’s new in 0.1.0</b></div>'
            '<ul><li><b>Getting started</b> walks through the settings most people change first. Open it any time '
            'from <span class="lnk2">Help › Getting started</span>.</li>'
            '<li>One user folder for your scripts, dictionaries and presets.</li></ul></div>')


def skip_toast():
    return toast("info", "Default settings in use",
                 "Help › Getting started opens the setup again.", "")



# Version 2 (owner, 2026-10-27): closer to MuseScore 4's first-launch setup (FirstLaunchSetupDialog.qml, v4.7.5):
# a compact window, page dots on top, a centred title and explanation, centred content, Back on the left and
# Next/Finish on the right; the Look page is MuseScore's ThemesPage with HikariSub's seven accent presets.

ACCENTS = {  # src/ui/theme.cpp kLightAccents / kDarkAccents: key, name, accent, onAccent, selected
    False: [("red", "Red", "#B3261E", "#FFFFFF", "#EBD7D6"), ("orange", "Orange", "#9C4600", "#FFFFFF", "#EBDFD6"),
            ("gold", "Gold", "#7A5E00", "#FFFFFF", "#EBE6D6"), ("green", "Green", "#145C4C", "#FFFFFF", "#D6EBE4"),
            ("blue", "Blue", "#1D5BA8", "#FFFFFF", "#D6DFEB"), ("purple", "Purple", "#5B3FB0", "#FFFFFF", "#DBD6EB"),
            ("pink", "Pink", "#9E2A73", "#FFFFFF", "#EBD6E3")],
    True: [("red", "Red", "#F4A9A3", "#2C1210", "#4C3230"), ("orange", "Orange", "#F5BB8A", "#2C1D10", "#4C3D30"),
           ("gold", "Gold", "#E3CF7E", "#2C2610", "#4C4630"), ("green", "Green", "#9CDBC9", "#102C24", "#304C47"),
           ("blue", "Blue", "#A6C8F2", "#101D2C", "#303D4C"), ("purple", "Purple", "#C6B5F4", "#18102C", "#38304C"),
           ("pink", "Pink", "#EFAAD3", "#2C1021", "#4C3041")],
}
NPAGES = 8


def with_accent(key, acc):
    t = dict(THEMES[key])
    for a in ACCENTS[key == "dark"]:
        if a[0] == acc:
            t.update(accent=a[2], onaccent=a[3], select=a[4])
    return t


def dots(i, n=NPAGES):
    return ('<div class="dots">' + "".join(
        f'<span class="dot{" on" if j == i else ""}"></span>' for j in range(n)) + '</div>')


def where(text):
    return f'<div class="where">{ic("settings", 14)}<span>{text}</span></div>'


def gs_window(i, title, intro, content, wh, last=False, extra=""):
    foot = ((btn("Back") if i > 0 else "") + '<span class="sp" style="flex:1"></span>' + btn("Skip setup")
            + extra + btn("Finish" if last else "Next", primary=True, focus=True))
    return (f'<div class="dlg gs" style="width:760px">{titlebar("Getting started")}'
            f'<div class="gsbody">{dots(i)}<div class="gsh">{title}</div><div class="gsi">{intro}</div>'
            f'<div class="gsc">{content}</div>{where(wh)}</div><div class="dfoot gsfoot">{foot}</div></div>')


def option(icon, title, text, on, badge="", width=300):
    b = f'<span class="badge">{badge}</span>' if badge else ""
    return (f'<div class="opt{" on" if on else ""}" style="width:{width}px">{ic(icon, 32, "oic")}'
            f'<div class="optn">{radio(on)}{title}</div>{b}<div class="optd">{text}</div></div>')


def sample(t, on):
    return (f'<div class="smp{" on" if on else ""}">{mini_main(t, 220, 136)}<div class="smpn">{radio(on)}'
            f'{t["name"]}</div></div>')


def accent_row(key, chosen):
    dark = key == "dark"
    circles = "".join(f'<span class="accc{" on" if k == chosen else ""}" style="background:{a}" title="{n}"></span>'
                      for k, n, a, _, _ in ACCENTS[dark])
    name = next(n for k, n, *_ in ACCENTS[dark] if k == chosen)
    return f'<div class="accs"><span class="accl">Accent colour</span>{circles}<span class="accn">{name}</span></div>'


def layout_card(kind, text, on=False):
    return (f'<div class="lc{" on" if on else ""}">{layout_thumb(kind)}<div class="lcn">{radio(on)}{kind}</div>'
            f'<div class="lcd">{text}</div></div>')


def item(content, cap, width):
    return f'<div class="col" style="width:{width}px">{content}<div class="cap">{cap}</div></div>'



# Pages ----------------------------------------------------------------------------------------


def p_language():
    rows = "".join(
        f'<div class="lrow{" cur" if i == 0 else ""}">{radio(i == 0)}<span class="lnat {cls}">{nat}</span>'
        f'<span class="lmeta">{eng}</span></div>' for i, (nat, eng, cls) in enumerate(LANGS))
    return gs_window(0, "Welcome to HikariSub",
                     "Let’s get started by choosing your language. Nothing is saved until Finish, and every choice "
                     "can be changed later.",
                     f'<div class="llist pl" style="width:340px">{rows}</div>'
                     '<div class="gsn">Chosen from the system language. This window switches to the one you pick.</div>',
                     "Later: Settings › General › Language")


def p_import_choice():
    return gs_window(1, "Bring over your old settings?",
                     "HikariSub 0.0.2 is installed on this computer. Nothing in it is changed.",
                     '<div class="opts">'
                     + option("import", "Import my old settings", "Theme, hotkeys, fonts, audio and video options and "
                              "your dictionary words. You review them on this page, and the next pages start from "
                              "them.", True, "Recommended")
                     + option("settings", "Set up from scratch", "Start from HikariSub’s defaults. File › Import "
                              "legacy settings… can still bring them over later.", False)
                     + '</div><div class="ccx">' + ic("folder-open", 14)
                     + '<span class="mono">~/.local/share/HikariSub/Legacy</span>'
                       '<span class="lnk2">Choose another…</span></div>',
                     "Later: File › Import legacy settings…")


def p_import_review():
    rows = "".join(
        f'<div class="irow{" cur" if i == 0 else ""}">{box(on)}<span class="ic1">{n}</span><span class="ic2">{a}</span>'
        f'<span class="ic3">{b}</span><span class="ic4 warn">{s}</span></div>'
        for i, (on, n, a, b, s) in enumerate(IMPORT_ROWS))
    content = ('<div class="frow wide">' + segmented(["Import my old settings", "Set up from scratch"],
                                                     "Import my old settings", small=True)
               + '<span class="cnt">213 of 214 chosen</span></div>'
               + '<div class="frow wide">' + segmented(["Conflicts 5", "All 214"], "Conflicts 5", small=True)
               + field("Search settings", 200, placeholder=True, icon="search")
               + '<span class="sp" style="flex:1"></span>' + btn("Proposed choice", small=True)
               + btn("All conflicts", small=True) + btn("None", small=True) + '</div>'
               + '<div class="itable"><div class="ihead"><span style="width:16px"></span><span class="ic1">Setting</span>'
                 '<span class="ic2">Legacy</span><span class="ic3">Now</span><span class="ic4">Status</span></div>'
               + rows + '</div>')
    return gs_window(1, "Choose what to bring over",
                     "The next pages start from these values; a change there wins over the imported one.",
                     content, "Later: File › Import legacy settings…")


def p_look(key, acc="green"):
    hc = key in HC
    t = THEMES[key] if hc else with_accent(key, acc)
    pair = ("hcwhite", "hcblack") if hc else ("light", "dark")
    samples = "".join(sample(THEMES[k] if k in HC else with_accent(k, acc), k == key) for k in pair)
    mid = (f'<div class="gsn" style="margin:0;height:28px;display:flex;align-items:center">Further high contrast '
           'settings are in Settings › Appearance.</div>' if hc else accent_row(key, acc))
    content = (f'<div class="look">{checkbox("Follow system theme", False)}<div class="smps">{samples}</div>{mid}'
               f'{checkbox("Enable high contrast", hc)}'
               '<div class="tbrow"><span class="accl">Title bar</span>'
               + segmented(["HikariSub’s", "The system’s"], "HikariSub’s", small=True)
               + '<span class="accn">Suits this desktop</span></div></div>')
    return gs_window(2, "Look", "Choose a theme. This window changes as you choose.", content,
                     "Later: Settings › Appearance")


def p_layout():
    cards = (layout_card("Translate", "The original text above your translation, with Translation mode on.")
             + layout_card("Time", "Audio across the top for timing Lines by ear.")
             + layout_card("Typeset", "A large video with the visual tools and the Styles panel.")
             + layout_card("Karaoke", "The Template editor beside the video, the audio in Karaoke mode."))
    return gs_window(3, "What do you mostly do?",
                     "Pick one to set the layout your first tab opens with. Leave it to keep Editing, which has a bit "
                     "of everything.", f'<div class="lcs">{cards}</div>',
                     "Later: View › Layout, or the layout switcher in the title bar")


def p_video_audio(linux=True):
    rend = (field("libass", 240) + '<span class="accn">The only renderer on Linux.</span>' if linux
            else combo("libass", 240) + '<span class="accn">or xy-VSFilter, VSFilterMod</span>')
    content = (f'<div class="fcol"><div class="frow"><span class="fl">Subtitles drawn by</span>{rend}</div>'
               '<div class="frame"><div class="fsub">I said I’d be there by nine.</div>'
               '<span class="fcap">libass · sample frame</span></div>'
               f'<div class="frow" style="margin-top:16px"><span class="fl">Sound plays on</span>'
               + combo("System default (Built-in Audio Analog Stereo)" if linux else "System default (Speakers)", 330)
               + f'<span class="b sm">{ic("audio-play", 14)}&nbsp;Test sound</span>'
                 '<span class="meter"><span style="width:62%"></span></span></div></div>')
    return gs_window(4, "Video and audio", "How subtitles are drawn on the video, and where the sound goes.",
                     content, "Later: Settings › Video and Settings › Audio")


def sample_line():
    return ('<div class="editor sl"><span style="white-space:pre">She <span class="gr1">has</span> seen '
            '<span class="sp1">teh</span> last episode already.</span></div>')


def p_editing():
    gram = segmented(["Off", "English (US)", "(UK)", "(Canada)", "(Australia)"], "English (US)", small=True)
    content = ('<div class="fcol">'
               f'<div class="frow"><span class="fl">Backups</span>after every {field("1", 48)} edits, keep '
               f'{field("3", 48)} per file</div>'
               f'<div class="frow"><span class="fl">Spelling</span>{checkbox("Check spelling", True)}'
               + combo("English (United States)", 220) + '</div>'
               f'<div class="frow"><span class="fl">Grammar</span>{gram}</div>'
               '<div class="gsn" style="margin:0 0 0 132px">Off by default. Harper checks grammar on this computer.</div>'
               '<div class="gsl2">Example</div>' + sample_line()
               + '<div class="legend"><span class="sw sp1">teh</span> spelling<span class="sw gr1">has</span> grammar'
                 '</div></div>')
    return gs_window(5, "Editing", "Backups while you work, and the checks on the Line editor’s text.", content,
                     "Later: Settings › General › Saving and Settings › Spelling &amp; grammar")


def p_updates():
    content = ('<div class="opts">'
               + option("check-updates", "Stable versions only", "Tested releases.", True, "Recommended", 260)
               + option("whats-new", "Pre-releases too", "Beta versions as well, to try what’s coming. They can have "
                        "bugs.", False, width=260)
               + f'</div><div style="margin-top:18px">{checkbox("Check for updates automatically", False)}</div>'
               '<div class="gsn" style="text-align:center">At most once a day, when HikariSub starts. What’s new on '
               'Home needs it too.</div>')
    return gs_window(6, "Updates", "Which versions HikariSub offers you, and whether it looks for them by itself.",
                     content, "Later: Settings › Updates")


def p_files():
    rows = "".join(f'<div class="tr"><span class="mono">{a}</span><span class="lmeta">{b}</span></div>'
                   for a, b in TREE_ROWS)
    content = (f'<div class="fcol"><div class="frow">{field("~/.config/hikarisub", 420, mono=True, icon="folder-open")}'
               + btn("Open folder") + f'</div><div class="tree">{rows}</div>'
               '<div class="gsn">Caches stay apart in <span class="mono">~/.cache/hikarisub</span>. Uninstalling '
               'leaves this folder alone.</div></div>')
    return gs_window(7, "Your files", "Everything of yours is in one folder. Copy it to move your setup to another "
                                      "computer.", content,
                     "Later: Settings › General › User folder, and Help › Open user folder", last=True)


# Sheets ---------------------------------------------------------------------------------------


def alt_css(key, acc):
    """The whole sheet CSS again with another accent, scoped to .alt: the Look page with that accent picked."""
    t = with_accent(key, acc)
    css = (CSS % dict(t, page=t["bg"], body="", shadow=".55" if key in DARKISH else ".28", focus=t["accent"],
                      tbline="") + fr1_css(key, t))
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    out = []
    for rule in css.split("}"):
        if "{" not in rule:
            continue
        sel, body = rule.split("{", 1)
        sel = ",".join(".alt " + s.strip() for s in sel.split(",") if s.strip())
        out.append(f"{sel}{{{body}}}")
    return "\n".join(out)


ALT = {"light": "blue", "dark": "purple"}


def sheet_a(key):
    t = THEMES[key]
    second = (item(f'<div class="alt">{p_look(key, ALT[key])}</div>',
                   f"3. Look with {ALT[key].capitalize()} picked: the window takes the accent at once. Enable high "
                   "contrast swaps the two samples for High contrast white and black.", 760)
              if key in ALT else
              item(p_look("hcwhite" if key == "hcblack" else "light"),
                   "3. Look, Light picked: Enable high contrast unticked brings back Light, Dark and the accents.",
                   760))
    return (f'<h1>FR1 Getting started, version 2: {t["name"]} (1 of 2)</h1>'
            '<p class="note">Closer to MuseScore 4’s first-launch setup: a compact window, page dots on top, a centred '
            'title and explanation, Back on the left, Next and Finish on the right. Every choice applies at Finish; '
            'Skip setup, Close and Esc keep the defaults and import nothing. The legacy-upgrade run has 8 pages, a '
            'fresh install 7 (no Import).</p><div class="row">'
            + item(p_language(), "1. Language: the shipped translations by their own names.", 760)
            + item(p_import_choice(), "2. Import (only when legacy is installed).", 760)
            + item(p_import_review(), "2. Import chosen: the import wizard’s review table on the page.", 760)
            + item(p_look(key, "green" if key in ALT else "green"),
                   "3. Look, as MuseScore’s theme page: Follow system theme, two theme samples, the seven accent "
                   "colours (Settings › Appearance’s presets; hover names them), Enable high contrast, and the title "
                   "bar." if key not in HC else
                   "3. Look with Enable high contrast ticked: the samples are the two high contrast themes and the "
                   "accents give way to a pointer to Settings.", 760)
            + second
            + item(p_layout(), "4. What you do: each thumbnail is that layout’s arrangement.", 760)
            + '</div>')


def sheet_b(key):
    t = THEMES[key]
    return (f'<h1>FR1 Getting started, version 2: {t["name"]} (2 of 2)</h1>'
            '<p class="note">Pages 5–8, then the ways in.</p><div class="row">'
            + item(p_video_audio(), "5. Video and audio on Linux. The sample frame is drawn by the renderer; Test "
                                    "sound plays on the chosen device.", 760)
            + item(p_video_audio(False), "5. On Windows the renderer is a choice and the frame redraws with it.", 760)
            + item(p_editing(), "6. Editing. The example Line follows the choices (shown with English (US) grammar; "
                                "Off is the default). GR1’s mockup sets the underline colour.", 760)
            + item(p_updates(), "7. Updates.", 760)
            + item(p_files(), "8. Your files (UD1’s folder). Finish replaces Next.", 760)
            + '<div class="col" style="width:760px"></div>'
            + '</div><h2>Ways in</h2><div class="row" style="align-items:flex-start">'
            + item(help_menu(), "Help › Getting started (a command: hotkey id 5176).", 300)
            + item(whats_new_card(), "What’s new for existing users points to it.", 420)
            + item(f'<div style="width:360px">{skip_toast()}</div>', "After Skip setup, Close or Esc.", 360)
            + '</div>')


FR1_CSS = """
.gs .gsbody{position:relative;display:flex;flex-direction:column;align-items:center;height:500px;padding:18px 28px 0;
            box-shadow:inset 0 -1px %(line)s}
.dots{display:flex;gap:8px;margin-bottom:18px}
.dot{width:8px;height:8px;border-radius:50%%;background:%(disabled)s;opacity:.6}
.dot.on{background:%(accent)s}
.gsh{font-size:16px;font-weight:650;margin-bottom:6px;text-align:center}
.gsi{color:%(muted)s;line-height:1.45;text-align:center;max-width:600px}
.gsc{width:100%%;display:flex;flex-direction:column;align-items:center;margin-top:22px}
.gsn{color:%(muted)s;font-size:12px;line-height:1.45;margin:10px 0 0;max-width:640px}
.gsl2{color:%(muted)s;font-size:12px;margin:14px 0 4px}
.where{position:absolute;left:0;right:0;bottom:14px;display:flex;align-items:center;justify-content:center;gap:6px;
       color:%(muted)s;font-size:12px}
.gsfoot{padding:16px 20px}
.mono{font-family:'Liberation Mono',Consolas,monospace;font-size:12px}
.sp{width:16px;flex:none}
.rad{width:16px;height:16px;border-radius:50%%;border:1px solid %(control)s;background:%(field)s;flex:none;position:relative}
.rad.on{border-color:%(accent)s}
.rad.on::after{content:"";position:absolute;inset:3px;border-radius:50%%;background:%(accent)s}
.lrow .rad{margin-right:2px}
.lnat{width:130px}
.lnat.ko{font-family:'Noto Sans CJK KR','Noto Sans CJK JP',sans-serif}
.lnat.ta{font-family:'Noto Sans Tamil',sans-serif}
.lnat.th{font-family:'IBM Plex Sans Thai Looped','Kanit',sans-serif}
.lrow .lmeta{margin-left:0}
.badge{font-size:11px;font-weight:600;padding:1px 6px;border-radius:1px;outline:1px solid %(accent)s;color:%(accent)s}
.opts{display:flex;gap:16px;justify-content:center}
.opt{display:flex;flex-direction:column;align-items:center;gap:8px;padding:18px 16px;outline:1px solid %(line)s;
     background:%(field)s;text-align:center}
.opt.on{outline:2px solid %(accent)s;background:%(select)s}
.oic{color:%(text)s}
.optn{display:flex;align-items:center;gap:8px;font-weight:650}
.optd{color:%(muted)s;line-height:1.45;font-size:12px}
.opt.on .optd{color:%(text)s}
.ccx{display:flex;align-items:center;gap:6px;margin-top:14px;color:%(muted)s}
.ccx .lnk2{margin-left:10px}
.frow.wide{width:100%%;margin:0 0 8px}
.cnt{color:%(muted)s;margin-left:auto}
.itable{width:100%%;outline:1px solid %(line)s;background:%(field)s}
.ihead,.irow{display:flex;align-items:center;gap:10px;height:28px;padding:0 10px}
.ihead{color:%(muted)s;font-size:12px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
.irow+.irow{box-shadow:inset 0 1px %(line)s}
.irow.cur{background:%(select)s}
.ic1{flex:1;min-width:0}
.ic2{width:120px}
.ic3{width:130px;color:%(muted)s}
.ic4{width:64px}
.look{display:flex;flex-direction:column;align-items:center;gap:16px;margin-top:-6px}
.smps{display:flex;gap:48px}
.smp{display:flex;flex-direction:column;align-items:center;gap:8px}
.smp .mm{outline-offset:0}
.smp.on .mm{outline:2px solid %(accent)s !important;outline-offset:2px}
.smpn{display:flex;align-items:center;gap:8px;font-weight:600}
.mm{display:flex;flex-direction:column;overflow:hidden}
.accs,.tbrow{display:flex;align-items:center;gap:4px;height:28px}
.accl{color:%(text)s;margin-right:10px}
.accn{color:%(muted)s;font-size:12px;margin-left:10px}
.accc{width:20px;height:20px;border-radius:50%%;margin:0 2px}
.accc.on{outline:2px solid %(text)s;outline-offset:2px}
.tbrow{margin-top:2px}
.lcs{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;width:100%%}
.lc{padding:8px;outline:1px solid %(line)s;background:%(field)s;display:flex;flex-direction:column;gap:6px;min-width:0}
.lc .lcd{min-height:48px}
.lcn{display:flex;align-items:center;gap:8px;font-weight:600}
.lcd{color:%(muted)s;font-size:12px;line-height:1.4}
.lt{height:104px;background:%(bg)s;outline:1px solid %(line)s;padding:2px;display:flex}
.lcol{display:flex;flex-direction:column;gap:2px;min-width:0;min-height:0}
.lrowb{display:flex;gap:2px;min-width:0;min-height:0}
.lb{background:%(panel)s;outline:1px solid %(line)s;display:flex;align-items:center;justify-content:center;
    min-width:0;min-height:0;font-size:10px;color:%(muted)s;text-align:center;overflow:hidden}
.lb.vid{background:%(vid)s;color:#C8CED4}
.lb.aud{background:%(field)s;color:%(accent)s}
.fcol{display:flex;flex-direction:column;align-items:flex-start;width:620px}
.fl{width:124px;flex:none;color:%(text)s}
.frame{position:relative;width:620px;height:150px;margin-top:6px;outline:1px solid %(line)s;
       background:linear-gradient(160deg,#41566B 0%%,#7A8C78 55%%,#C7A27A 100%%);display:flex;align-items:flex-end;justify-content:center}
.fsub{font:600 22px 'Liberation Sans',Arial,sans-serif;color:#FFFFFF;margin-bottom:18px;
      text-shadow:-2px -2px 0 #000,2px -2px 0 #000,-2px 2px 0 #000,2px 2px 0 #000,0 2px 0 #000,0 -2px 0 #000,2px 0 0 #000,-2px 0 0 #000,3px 4px 3px rgba(0,0,0,.6)}
.fcap{position:absolute;right:6px;top:4px;font-size:10px;color:#fff;background:rgba(0,0,0,.45);padding:1px 5px}
.meter{width:80px;height:6px;background:%(field)s;outline:1px solid %(line)s;display:flex}
.meter span{background:%(accent)s}
.editor.sl{height:34px;font-size:14px;width:100%%}
.sp1{background:%(spell)s}
.gr1{text-decoration:underline wavy %(grammar)s;text-decoration-thickness:1.5px;text-underline-offset:4px}
.legend{display:flex;align-items:center;gap:8px;color:%(muted)s;font-size:12px;margin-top:6px}
.legend .sw{color:%(text)s;padding:0 3px;margin-left:8px}
.legend .sw:first-child{margin-left:0}
.tree{width:100%%;margin-top:10px;outline:1px solid %(line)s;background:%(field)s;padding:4px 0}
.tr{display:flex;gap:16px;align-items:center;height:26px;padding:0 12px}
.tr .mono{width:200px}
.tr .lmeta{margin-left:0}
.menu{width:260px}
.mbar{display:flex;gap:2px;background:%(panel)s;padding:0 4px;height:28px;align-items:center;box-shadow:inset 0 -1px %(line)s}
.mbar span{padding:4px 8px}
.mbar span.on{background:%(select)s}
.mdrop{background:%(panel)s;outline:1px solid %(line)s;padding:4px 0;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);width:240px}
.mi{display:flex;align-items:center;gap:10px;height:28px;padding:0 12px;color:%(text)s}
.mi .ic{color:%(muted)s}
.mi.hot{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.wn{background:%(panel)s;outline:1px solid %(line)s;padding:12px 16px;line-height:1.5}
.wnh{display:flex;align-items:center;gap:8px;margin-bottom:6px}
.wn ul{padding-left:18px}
.wn li{margin:2px 0}
"""


def fr1_css(key, t):
    return FR1_CSS % dict(t, shadow=".55" if key in DARKISH else ".28", grammar=GRAMMAR[key], spell=SPELL[key],
                          vid="#0B0E12" if key != "hcwhite" else "#1A1A1A")


def fr1_main():
    for key in THEMES:
        extra = fr1_css(key, THEMES[key]) + (alt_css(key, ALT[key]) if key in ALT else "")
        for name, fn in (("pages", sheet_a), ("more", sheet_b)):
            html = page(key, fn(key)).replace("</style>", extra + "</style>", 1).replace("width:1328px", "width:1596px")
            (OUT / f"fr1-{key}-{name}.html").write_text(html)


fr1_main()
