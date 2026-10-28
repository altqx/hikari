#!/usr/bin/env python3
"""KE2 #284 Template editor mockup: one HTML sheet per theme, screenshotted by shoot.sh.

Built on the KA2/AU1 mockup kit (theme roles from src/ui/theme.cpp, the IC1 icon set) and the
current shell (SH1 title bar, docks with a bold title and a panel menu).

usage: gen.py <src/ui/icons> <out dir>
"""
import html
import pathlib
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# theme.cpp baseRoles + default accents; syn = Content::syntax (EDITOR_* colours, index 0 is the text).
THEMES = {
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", control="#7A8691", accent="#9CDBC9", onaccent="#102C24",
                 select="#304C47", disabled="#75818D", hc=False, danger="#FFADAD", warning="#E0A030",
                 success="#4DBA4D", comment="#253A57", alt="#242A31",
                 syn=[None, "#8791FD", "#00C3FF", "#00C3FF", "#0076FF", "#ADABAB", "#BDB76B", "#FB4544", "#00C3FF",
                      "#8D80D3", "#11A243"]),
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", control="#76828D", accent="#145C4C", onaccent="#FFFFFF",
                  select="#D6EBE4", disabled="#74808B", hc=False, danger="#A51F31", warning="#8A5A00",
                  success="#008000", comment="#D6E3F5", alt="#F0F2F4",
                  syn=[None, "#7E3B00", "#787600", "#787600", "#34C200", "#817F7F", "#BDB76B", "#FB4544", "#00C3FF",
                       "#8D80D3", "#11A243"]),
    "hcwhite": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                    text="#000000", muted="#1A1A1A", line="#000000", control="#000000", accent="#0037B3",
                    onaccent="#FFFFFF", select="#C9DAF8", disabled="#6E6E6E", hc=True, danger="#A00000",
                    warning="#6B4500", success="#005A00", comment="#F2E2B0", alt="#FFFFFF",
                    syn=[None, "#7A3E00", "#005A9E", "#005A9E", "#006400", "#595959", "#6B5E00", "#B00000",
                         "#005A9E", "#5B2DA8", "#00663A"]),
    "hcblack": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                    text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", control="#FFFFFF", accent="#FFFF00",
                    onaccent="#000000", select="#253F60", disabled="#8C8C8C", hc=True, danger="#FFADAD",
                    warning="#FFD54A", success="#7CFC7C", comment="#403400", alt="#000000",
                    syn=[None, "#FFB060", "#60D0FF", "#60D0FF", "#7CFC7C", "#BEBEBE", "#F0E68C", "#FF6060",
                         "#60D0FF", "#C8A8FF", "#A0E0A0"]),
}

SVG_HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" fill="none" '
            'stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">')
# Icons this card would add (IC1's 16 px, 1 px stroke, accent group).
NEW_ICONS = {
    # Template editor: a page holding a template's braces, the generated copies fanned behind it.
    "template-editor": SVG_HEAD + '<path d="M4.5 1.5h8v10"/><rect x="2.5" y="3.5" width="8" height="11" rx=".5"/>'
                       '<g id="accent"><path d="M5.5 6.5c-.8 0-.8.6-.8 1.5s-.4 1-.9 1c.5 0 .9.1.9 1s0 1.5.8 1.5'
                       'M7.5 6.5c.8 0 .8.6.8 1.5s.4 1 .9 1c-.5 0-.9.1-.9 1s0 1.5-.8 1.5"/></g></svg>',
    # Loop the Line: the Line's span played again and again.
    "loop": SVG_HEAD + '<path d="M2.5 9V7.5a3 3 0 0 1 3-3h7"/><path d="M10.5 2.5l2 2-2 2"/>'
            '<g id="accent"><path d="M13.5 7v1.5a3 3 0 0 1-3 3h-7"/><path d="M5.5 13.5l-2-2 2-2"/></g></svg>',
    # A problem: a circle with an exclamation mark (drawn in `danger`).
    "problem": SVG_HEAD + '<circle cx="8" cy="8" r="6"/><path d="M8 4.75v4"/>'
               '<circle cx="8" cy="11.25" r=".6" fill="currentColor"/></svg>',
    "check": SVG_HEAD + '<path d="M3 8.5l3 3 7-7"/></svg>',
    "radio-on": SVG_HEAD + '<circle cx="8" cy="8" r="5.5"/><circle cx="8" cy="8" r="2.5" fill="currentColor"/></svg>',
    "radio-off": SVG_HEAD + '<circle cx="8" cy="8" r="5.5"/></svg>',
    "close": SVG_HEAD + '<path d="M4 4l8 8M12 4l-8 8"/></svg>',
    "pick": SVG_HEAD + '<path d="M4.5 6.5l3.5 3.5 3.5-3.5"/></svg>',
}


def svg(role):
    if role in NEW_ICONS:
        return NEW_ICONS[role]
    return (ICONS / f"{role}.svg").read_text().strip()


def icon(role, cls=""):
    return f'<span class="ic {cls}">{svg(role)}</span>'


CHEVRON = ('<svg width="8" height="8" viewBox="0 0 8 8" fill="none" stroke="currentColor" stroke-width="1" '
           'stroke-linecap="round" stroke-linejoin="round"><path d="M1.5 3l2.5 2.5 2.5-2.5"/></svg>')


def btn(role, cls="", title=""):
    return f'<span class="b {cls}" title="{title}">{icon(role)}</span>'


SEP = '<span class="sep"></span>'


def kc(k):
    return f'<span class="kc">{k}</span>'


# ---------------------------------------------------------------- ASS / template highlighting
# Roles as core::syntax::Role: 1 braces, 2 tag operators, 3 tag names, 4 tag values, 6 template variables,
# 7 code marks, 8 functions, 9 keywords, 10 strings. A token is (text, role).

def hl(tokens):
    out = []
    for text, role in tokens:
        t = html.escape(text)
        out.append(f'<span class="s{role}">{t}</span>' if role else t)
    return "".join(out)


def tag(name, value=""):
    return [("\\", 2), (name, 3)] + ([(value, 4)] if value else [])


MAIN_TEXT = (
    [("!", 7), ("retime", 8), ("(", 2), ('"syl"', 10), (",", 2), ("-200", 4), (",", 2), ("0", 4), (")", 2), ("!", 7),
     ("{", 1)] + tag("an", "5") + tag("pos") + [("(", 2), ("$scenter", 6), (",", 2), ("$smiddle", 6), (")", 2)]
    + tag("fad") + [("(", 2), ("150", 4), (",", 2), ("0", 4), (")", 2)]
    + tag("t") + [("(", 2), ("200", 4), (",", 2), ("!", 7), ("200", 4), ("+", 2), ("$sdur", 6), ("!", 7), (",", 2)]
    + tag("fscx", "125") + tag("fscy", "125") + tag("1c", "&HB469FF&") + [(")", 2)]
    + tag("t") + [("(", 2), ("!", 7), ("200", 4), ("+", 2), ("$sdur", 6), ("!", 7), (",", 2), ("!", 7), ("400", 4),
                  ("+", 2), ("$sdur", 6), ("!", 7), (",", 2)]
    + tag("fscx", "100") + tag("fscy", "100") + [(")", 2), ("}", 1)]
)
MAIN_TYPING = MAIN_TEXT[:]  # with "$s" typed at the end of \pos's first argument (autocomplete state)
BAD_TEXT = (
    [("{", 1)] + tag("an", "5") + tag("pos") + [("(", 2), ("$scenter", 6), (",", 2), ("$smiddle", 6), (")", 2)]
    + tag("1c") + [("&H", 4), ("!", 7), ("math", 9), (".", 2), ("rnd", 8), ("(", 2), ("1", 4), (",", 2), ("5", 4),
                   (")", 2), ("!", 7), ("0FF&", 4), ("}", 1)]
)

# The Document: 1 code once, 2–4 templates, 5–22 karaoke Lines (12 is current), generated Lines from 23.
TEMPLATES = [
    # (line, kind, effect, style, text, lines for Line 12, ms for Line 12, lines in the last full run, ms full run)
    (1, "code", "code once", "T1a", "function fade_alpha(t) return t > 0.5 and \"&H80&\" or \"&H00&\" end",
     0, 0.1, 0, 0.1),
    (2, "template", "template pre-line", "T1a", "{\\an8\\pos($center,$middle)\\fad(300,200)\\1a&HFF&}", 1, 0.6, 18, 11),
    (3, "template", "template syl noblank", "T1a", "!retime(\"syl\",-200,0)!{\\an5\\pos($scenter,$smiddle)…", 5, 2.9,
     94, 412),
    (4, "template", "template line keeptags", "T2a", "{\\an2\\fad(200,200)}", 0, 0.2, 18, 9),
]
SYLS = [("ki", 0, 320), ("mi", 320, 600), ("no", 600, 1010), ("na", 1010, 1260), ("mae", 1260, 1860)]
LINE12 = ("0:00:48.92", "0:00:52.31")


def ms_to_ass(ms):
    cs = ms // 10
    return f"0:00:{cs // 100:02d}.{cs % 100:02d}"


def kind_badge(kind):
    return f'<span class="kb {kind}">{kind}</span>'


# ---------------------------------------------------------------- the Template editor panel

def tlist(t, *, selected=3, draft=(3,), problem=(), full=False, w=640, narrow=False):
    rows = []
    for (n, kind, effect, style, text, lines, ms, full_lines, full_ms) in TEMPLATES:
        cls = " cur" if n == selected else ""
        mark = ('<span class="dm" title="Changed, not applied">●</span>' if n in draft else
                f'<span class="pm">{icon("problem")}</span>' if n in problem else '<span class="dm"></span>')
        words = effect.split(" ")
        eff = (f'<span class="ew">{words[0]}</span> ' + " ".join(f'<span class="em">{x}</span>' for x in words[1:]))
        if n in problem:
            stat = '<span class="st bad">Problem</span>'
        elif full:
            stat = f'<span class="st">{full_lines} Lines · {full_ms:g} ms</span>'
        else:
            stat = (f'<span class="st">{lines} Line{"s" if lines != 1 else ""} · {ms:g} ms</span>' if kind == "template"
                    else f'<span class="st">{ms:g} ms</span>')
        if narrow:
            rows.append(f'<div class="tr{cls}">{mark}<span class="tn">{n}</span><span class="tef">{eff}</span>'
                        f'<span class="grow"></span>{stat}</div>')
        else:
            rows.append(f'<div class="tr{cls}">{mark}<span class="tn">{n}</span><span class="tef">{eff}</span>'
                        f'<span class="ts">{style}</span><span class="tt">{html.escape(text)}</span>{stat}</div>')
    return "".join(rows)


def field(label, value, *, w=None, pick=False, cls="", mono=False):
    style = f' style="width:{w}px"' if w else ""
    p = f'<span class="fp">{icon("pick")}</span>' if pick else ""
    return (f'<span class="fl">{label}</span><span class="f {cls}{" mono" if mono else ""}"{style}>'
            f'<span class="fv">{value}</span>{p}</span>')


def editor_block(t, *, text=MAIN_TEXT, effect="template syl noblank", caret_after=None, problem=None, narrow=False,
                 open_pick=False):
    eff = ('<span class="s9">template</span> <span class="s6">syl</span> <span class="s6">noblank</span>'
           if effect.startswith("template") else html.escape(effect))
    if narrow:
        row = (f'<div class="frow col">{field("Effect", eff, pick=True, mono=True, cls="wide" + (" open" if open_pick else ""))}</div>'
               f'<div class="frow">{field("Style", "T1a", w=150, pick=True)}{field("Layer", "1", w=60)}</div>')
    else:
        row = (f'<div class="frow">{field("Effect", eff, w=262, pick=True, mono=True, cls="open" if open_pick else "")}'
               f'{field("Style", "T1a", w=118, pick=True)}{field("Layer", "1", w=46)}</div>')
    body = hl(text)
    if caret_after:
        body = hl(text[:caret_after]) + '<span class="caret"></span>' + hl(text[caret_after:])
    if problem:
        body = hl(text[:problem[0]]) + '<span class="squig">' + hl(text[problem[0]:problem[1]]) + '</span>' + \
               hl(text[problem[1]:])
    prob = ""
    if problem:
        prob = (f'<div class="pbar">{icon("problem")}<span><b>Template Line: 3</b>, in '
                f'<span class="mono">!math.rnd(1,5)!</span>: attempt to call a nil value (field \'rnd\')</span></div>')
    return (f'<div class="sec"><div class="sh">Line 3<span class="dimx">template syl noblank</span></div>{row}'
            f'<div class="code">{body}</div>{prob}'
            f'<div class="hint2">{kc("$")} inserts a variable, {kc("!")} starts a Lua expression. '
            f'Both open a list of what fits.</div></div>')


def gen_rows(t, *, stale=False, n=6):
    rows = [("pre-line", 0, "0:00:48.62", "0:00:52.51", "{\\an8\\pos(640,62)\\fad(300,200)\\1a&HFF&}ki mi no na mae")]
    for i, (s, a, b) in enumerate(SYLS):
        rows.append(("syl", 1, ms_to_ass(48920 + a - 200), ms_to_ass(48920 + b),
                     f"{{\\an5\\pos({508 + 62 * i},62)\\fad(150,0)\\t(200,{200 + b - a},\\fscx125…}}{s}"))
    out = []
    for kind, layer, a, b, text in rows[:n]:
        mine = kind == "syl"
        out.append(f'<div class="gr{" mine" if mine else ""}{" stale" if stale else ""}">'
                   f'<span class="gl">{layer}</span><span class="gt">{a}</span><span class="gt">{b}</span>'
                   f'<span class="gx">{html.escape(text)}</span></div>')
    return "".join(out)


def preview_block(t, *, stale=False, collapsed=False, problem=False):
    if problem:
        head = (f'<div class="sh">Preview on Line 12<span class="dimx">T1a · ki mi no na mae</span>'
                f'<span class="grow"></span><span class="warnx">Kept from the last run that worked</span></div>')
    else:
        head = (f'<div class="sh">Preview on Line 12<span class="dimx">T1a · ki mi no na mae</span>'
                f'<span class="grow"></span><span class="dimx r">6 Lines · 3.8 ms</span></div>')
    if collapsed:
        return f'<div class="sec fold"><span class="fold-ch">{CHEVRON}</span>{head}</div>'
    cols = ('<div class="gh"><span class="gl">L</span><span class="gt">Start</span><span class="gt">End</span>'
            '<span class="gx">Text</span></div>')
    return f'<div class="sec">{head}<div class="gen">{cols}{gen_rows(t, stale=stale or problem)}</div></div>'


def footer(t, *, state="draft", menu_open=False, narrow=False):
    if state == "draft" and narrow:
        left = '<span class="dm">●</span><span>Changed since Apply</span>'
    elif state == "draft":
        left = '<span class="dm">●</span><span>Line 3 changed since the last Apply; the generated Lines are older.</span>'
    elif state == "applied":
        left = (f'<span class="okx">{svg("check")}</span><span>Applied: 130 Lines from 18 karaoke Lines in '
                f'0.43 s. {kc("Ctrl+Z")} removes them in one step.</span>')
    elif state == "problem":
        left = (f'<span class="badx">{svg("problem")}</span><span>Apply is off while a template has a problem.</span>')
    else:
        left = '<span class="dimx">No changes. Apply regenerates every karaoke Line.</span>'
    dis = " dis" if state == "problem" else ""
    apply_ = (f'<span class="apl{dis}{" open" if menu_open else ""}"><span class="af">Apply</span>'
              f'<span class="aa">{CHEVRON}</span></span>')
    discard = ""
    return f'<div class="foot">{left}<span class="grow"></span>{discard}{apply_}</div>'


def te_panel(t, w, *, body, title="Template editor", preview_on=True, loop=False, tabs=None, h=None):
    tools = (btn("show", "tg" if preview_on else "", "Show the preview on the video")
             + btn("loop", "tg" if loop else "", "Loop the Line") + SEP + btn("panel-menu"))
    if tabs:
        tabhtml = "".join(f'<span class="tab{" on" if i == tabs[1] else ""}">{name}</span>'
                          for i, name in enumerate(tabs[0]))
        headl = f'<span class="tabs">{tabhtml}</span>'
    else:
        headl = f'<span class="t">{title}</span>'
    hs = f";height:{h}px" if h else ""
    return (f'<div class="dock te" style="width:{w}px{hs}"><div class="dhead">{headl}<span class="grow"></span>'
            f'{tools}</div><div class="dbody">{body}</div></div>')


def te_body(t, *, selected=3, draft=(3,), problem_rows=(), editor=None, preview=None, foot=None, full=False,
            w=640, narrow=False, list_extra=""):
    lst = (f'<div class="sec"><div class="sh">Templates<span class="dimx">2 template Lines · 1 code Line · '
           f'18 karaoke Lines</span><span class="grow"></span><span class="ab2">{icon("add")}New'
           f'<span class="aa2">{CHEVRON}</span></span></div>'
           f'<div class="tl{" narrow" if narrow else ""}">{tlist(t, selected=selected, draft=draft, problem=problem_rows, full=full, narrow=narrow)}'
           f'</div>{list_extra}</div>')
    return lst + (editor or "") + (preview or "") + (foot or "")


# ---------------------------------------------------------------- menus, popovers, tooltips

def menu(rows, *, left, top=None, width=268, cls="", bottom=None):
    y = f"bottom:{bottom}px" if bottom is not None else f"top:{top}px"
    return f'<div class="menu {cls}" style="left:{left}px;{y};width:{width}px">' + "".join(rows) + '</div>'


def mrow(role, text, keys="", *, checked=None, radio=None, hover=False, dis=False, sub=""):
    c = (" hov" if hover else "") + (" dis" if dis else "")
    lead = ""
    if checked is not None:
        lead = f'<span class="cb">{svg("check") if checked else ""}</span>'
    if radio is not None:
        lead = f'<span class="cb">{svg("radio-on" if radio else "radio-off")}</span>'
    ic = icon(role) if role else ('<span class="ic"></span>' if lead == "" else "")
    sub_html = f'<span class="msub">{sub}</span>' if sub else ""
    return (f'<div class="mi{c}">{lead}{ic}<span class="mt">{text}{sub_html}</span>'
            f'<span class="mk2">{keys}</span></div>')


MSEP = '<div class="msep"></div>'


def mhead(text):
    return f'<div class="phead">{text}</div>'


def tip(text, *, left, top):
    return f'<div class="tip" style="left:{left}px;top:{top}px">{text}</div>'


def cell(n, caption, inner, note="", wide=False, h=None):
    note_html = f'<div class="note">{note}</div>' if note else ""
    hs = f' style="height:{h}px"' if h else ""
    return (f'<div class="cell{" wide" if wide else ""}"><div class="cap"><b>{n}</b> {caption}</div>{note_html}'
            f'<div class="stage"{hs}>{inner}</div></div>')


# ---------------------------------------------------------------- the video frame and the window

def frame(t, w, h, *, badge="preview", problem=False, show_source=False):
    """A night scene with Line 12's karaoke over it, as the preview draws it (the frame does not follow the theme)."""
    scale = w / 640
    syl_html = []
    for i, (s, a, b) in enumerate(SYLS):
        cls = "sung" if i < 2 else ("now" if i == 2 else "")
        syl_html.append(f'<span class="ks {cls}">{s}</span>')
    if problem:
        syl_html = [f'<span class="ks {"sung" if i < 2 else ("now" if i == 2 else "")}">{s}</span>'
                    for i, (s, a, b) in enumerate(SYLS)]
    if badge == "preview":
        b = (f'<div class="vbadge">{icon("template-editor")}<span><b>Preview</b> · Line 12 · not applied</span></div>')
    elif badge == "stale":
        b = (f'<div class="vbadge warn">{icon("problem")}<span><b>Preview</b> · Line 12 · last run that worked</span>'
             f'</div>')
    else:
        b = ""
    src = ('<div class="ksrc">ki mi no na mae</div>' if show_source else "")
    return (f'<div class="frame" style="width:{w}px;height:{h}px;--s:{scale:.3f}">'
            f'<div class="sky"></div><div class="hill"></div>'
            f'<div class="kline">{"".join(syl_html)}</div>{src}'
            f'<div class="sub2">Your name, carried on the spring wind</div>{b}</div>')


def grid(t, w, *, rows_h=None):
    rows = [
        ("1", "0:00:00.00", "0:00:00.00", "T1a", "code once", "function fade_alpha(t) return t &gt; 0.5 and …", "cm", ""),
        ("2", "0:00:00.00", "0:00:00.00", "T1a", "template pre-line",
         "{\\an8\\pos($center,$middle)\\fad(300,200)\\1a&amp;HFF&amp;}", "cm", ""),
        ("3", "0:00:00.00", "0:00:00.00", "T1a", "template syl noblank",
         "!retime(\"syl\",-200,0)!{\\an5\\pos($scenter,$smiddle)\\fad(150,0)\\t(200,!200+$sdur!,…", "cm", "draft"),
        ("4", "0:00:00.00", "0:00:00.00", "T2a", "template line keeptags", "{\\an2\\fad(200,200)}", "cm", ""),
        ("11", "0:00:45.10", "0:00:48.40", "T1a", "karaoke", "{\\k35}ha{\\k28}ru{\\k41}no {\\k25}ka{\\k30}ze", "cm", ""),
        ("12", "0:00:48.92", "0:00:52.31", "T1a", "karaoke", "{\\k32}ki{\\k28}mi{\\k41}no {\\k25}na{\\k60}mae", "cm",
         "cur"),
        ("13", "0:00:52.80", "0:00:56.02", "T1a", "karaoke", "{\\k22}ko{\\k30}ko{\\k38}ro {\\k26}no {\\k64}o{\\k40}to",
         "cm", ""),
        ("97", "0:00:48.72", "0:00:49.24", "T1a", "fx", "{\\an5\\pos(508,62)\\fad(150,0)\\t(200,520,\\fscx125\\fscy125…}ki",
         "", ""),
    ]
    out = ['<div class="grid2"><div class="g2h"><span class="c0">#</span><span class="c1">Start</span>'
           '<span class="c1">End</span><span class="c2">Style</span><span class="c3">Effect</span>'
           '<span class="c4">Text</span></div>']
    for n, a, b, st, ef, tx, kind, mark in rows:
        cls = ("cmt " if kind == "cm" else "") + ("cur" if mark == "cur" else "")
        dm = '<span class="dm">●</span>' if mark == "draft" else ""
        out.append(f'<div class="g2r {cls}"><span class="c0">{dm}{n}</span><span class="c1">{a}</span>'
                   f'<span class="c1">{b}</span><span class="c2">{st}</span><span class="c3">{ef}</span>'
                   f'<span class="c4">{tx}</span></div>')
    out.append('</div>')
    return "".join(out)


def window(t):
    W, H = 1320, 820
    menus = "".join(f'<span class="mt2">{m}</span>' for m in
                    ["File", "Edit", "Automation", "Video", "Audio", "Subtitles", "View", "Help"])
    title = (f'<div class="wtitle"><span class="appi"></span>{menus}<span class="lay">{icon("layout")}Editing'
             f'<span class="aa2">{CHEVRON}</span></span><span class="grow"></span>'
             f'<span class="wtt">song.ass — HikariSub</span><span class="grow"></span>'
             f'{icon("window-minimize")}{icon("window-maximize")}{icon("window-close")}</div>')
    doct = '<div class="doctabs"><span class="dt on">song.ass</span><span class="dt add">+</span></div>'
    video = (f'<div class="dock" style="width:600px;height:432px"><div class="dhead"><span class="t">Video</span>'
             f'<span class="grow"></span>{btn("panel-menu")}</div><div class="vbody">{frame(t, 576, 324)}'
             f'<div class="vtrans">{btn("media-play")}{btn("play-line")}{btn("media-stop")}'
             f'<span class="vt">0:00:49.53 · frame 1187</span><span class="grow"></span>{btn("volume-low")}</div>'
             f'</div></div>')
    audio = (f'<div class="dock" style="width:712px;height:120px"><div class="dhead"><span class="t">Audio</span>'
             f'<span class="grow"></span>{btn("panel-menu")}</div><div class="abody">'
             + "".join(f'<span class="asyl" style="left:{60 + i * 120}px;width:{110}px">{s}</span>'
                       for i, (s, a, b) in enumerate(SYLS)) + '<span class="acur"></span></div></div>')
    te = te_panel(t, 712, tabs=(["Line editor", "Template editor"], 1), h=306, body=(
        te_body(t, w=712, preview=None, editor=(
            f'<div class="sec"><div class="code small">{hl(MAIN_TEXT)}</div></div>'),
            foot=footer(t))))
    return (f'<div class="win2" style="width:{W}px">{title}{doct}<div class="wmain">'
            f'<div class="wcol">{video}</div><div class="wcol">{audio}{te}</div></div>'
            f'<div class="dock" style="width:{W - 8}px;margin:4px"><div class="dhead"><span class="t">Grid</span>'
            f'<span class="grow"></span>{btn("panel-menu")}</div>{grid(t, W - 8)}</div>'
            f'<div class="sbar"><span>1 Line selected</span><span>0:00:49.53</span><span>23.976 FPS</span>'
            f'<span class="grow"></span><span>{icon("layout")} Editing</span></div></div>')


# ---------------------------------------------------------------- the sheet

PW = 640  # a panel cell


def sheet(key, t):
    cells = []

    cells.append(cell(1, "The Template editor, docked as a tab beside the Line editor, with the preview on the video.",
                      window(t),
                      "Opened from View › Panels › Template editor (and by KP1's Karaoke preset later). It edits the "
                      "Document's template and code Lines like any other edit (the Grid shows the new text, Ctrl+Z "
                      "takes it back). Each edit re-runs the templater on the <b>current Line</b> only, and the video "
                      "draws that result in place of the Line's generated Lines; <b>no generated Line is written until "
                      "Apply</b>. The Grid marks a template Line changed since the last Apply (●).", wide=True))

    body = te_body(t, editor=editor_block(t), preview=preview_block(t), foot=footer(t))
    cells.append(cell(2, "Editing a template: its Line, the effect, the highlighted text and the current Line's output.",
                      te_panel(t, PW, body=body),
                      "One list of the template and code Lines in Grid order, each with what it made for the "
                      "current Line and how long it took (KE1's run report). The text field is the Line editor's, "
                      "with its template highlighting. <b>Preview on Line 12</b> lists the Lines Apply would write "
                      "for that Line; the selected template's own Lines carry the accent bar."))

    # 3 Autocomplete popover for variables, with live values.
    caret = 18  # after "$scenter"? place after "\pos(" + "$s"
    typing = MAIN_TEXT[:17] + [("$s", 6)]
    ed = editor_block(t, text=typing, caret_after=len(typing))
    body = te_body(t, editor=ed, preview=preview_block(t), foot=footer(t))
    rows = [("$scenter", "640", "horizontal centre of the syllable", True), ("$smiddle", "62", "vertical middle", False),
            ("$sstart", "0", "start, from the Line's start (ms)", False), ("$send", "320", "end (ms)", False),
            ("$sdur", "320", "duration (ms)", False), ("$skdur", "32", "duration in centiseconds", False),
            ("$si", "1", "number of the syllable in the Line", False), ("$swidth", "54", "width (px)", False)]
    pop_rows = "".join(f'<div class="ac{" hov" if cur else ""}"><span class="acn">{n}</span>'
                       f'<span class="acv">{v}</span><span class="acd">{d}</span></div>' for n, v, d, cur in rows)
    pop = (f'<div class="pop" style="left:150px;top:300px;width:430px">'
           f'<div class="phead">Syllable variables · values for Line 12, syllable 1 “ki”</div>{pop_rows}'
           f'<div class="msep"></div><div class="phint">{kc("↑")}{kc("↓")} choose · {kc("Enter")} insert · '
           f'{kc("Esc")} close</div></div>')
    cells.append(cell(3, "Typing <span class=\"mono\">$s</span> lists the syllable variables with their values here.",
                      te_panel(t, PW, body=body) + pop,
                      "A Popover anchored to the caret (#301). It lists the stock variables that start with what was "
                      "typed, with each one's value on the current Line's first syllable, so a position or a time can "
                      "be checked before it is used. <span class=\"mono\">!</span> lists karaskel's functions and the "
                      "template helpers (retime, relayer, maxloop) the same way."))

    # 4 Effect picker popover: class + modifiers.
    ed = editor_block(t, open_pick=True)
    body = te_body(t, editor=ed, preview=preview_block(t), foot=footer(t))
    rows = [mhead("Runs for each"),
            mrow(None, "Line, before its syllables", "pre-line", radio=False),
            mrow(None, "Line", "line", radio=False),
            mrow(None, "Syllable", "syl", radio=True),
            mrow(None, "Character", "char", radio=False),
            mrow(None, "Furigana syllable", "furi", radio=False), MSEP,
            mhead("Modifiers"),
            mrow(None, "Skip blank syllables", "noblank", checked=True, hover=True),
            mrow(None, "Keep the Line's override tags", "keeptags", checked=False),
            mrow(None, "Every Style, not only T1a", "all", checked=False),
            mrow(None, "Repeat", "loop n", checked=False),
            mrow(None, "Only syllables marked with an effect", "fx name", checked=False),
            mrow(None, "No text, tags only", "notext", checked=False),
            MSEP, '<div class="phint">The Effect text stays stock syntax; this picker only writes it.</div>']
    pop = menu(rows, left=78, top=262, width=340, cls="pop2")
    cells.append(cell(4, "The Effect picker: what the template runs for, and its modifiers.",
                      te_panel(t, PW, body=body) + pop,
                      "The Effect field accepts stock syntax as typed (<span class=\"mono\">template syl noblank</span>);"
                      " its picker is a Popover that writes the same words, with each modifier named in plain English "
                      "and its keyword on the right. Code Lines get once / line / syl / furi.", h=800))

    # 5 A problem in a template.
    ed = editor_block(t, text=BAD_TEXT, problem=(14, 25))
    body = te_body(t, problem_rows=(3,), draft=(), editor=ed, preview=preview_block(t, problem=True),
                   foot=footer(t, state="problem"))
    cells.append(cell(5, "A template with a problem: the Line, the expression and the message, in place.",
                      te_panel(t, PW, body=body),
                      "KE1's message names the template Line; KE1's part tracking gives the expression, which is "
                      "underlined in the text (wavy, not only coloured). The preview keeps the last output that "
                      "worked, dimmed, and says so on the video too (cell 9). Apply waits until the problem is fixed."))

    # 6 Apply menu and the applied state.
    body = te_body(t, draft=(), full=True, editor=editor_block(t), preview=preview_block(t),
                   foot=footer(t, state="applied"))
    rows = [mrow(None, "Apply to every karaoke Line", ""),
            mrow(None, "Apply to the selected Lines", "", hover=True,
                 sub="KE1's regenerate-selected: the other Lines' output stays as it is.")]
    m = menu(rows, left=PW - 404, bottom=46, width=400)
    cells.append(cell(6, "Apply: one undo step for every Line the templates make.",
                      te_panel(t, PW, body=body) + m,
                      "Apply writes the templater's whole result as <b>one undo step</b>, "
                      "the same Lines as Automation › Apply karaoke template. Its arrow offers KE1's Apply to the "
                      "selected Lines. Afterwards the list shows each template's Lines and time from the full run."))

    # 7 Empty state with the New menu open.
    empty = (f'<div class="sec"><div class="sh">Templates<span class="grow"></span><span class="ab2 open">'
             f'{icon("add")}New<span class="aa2">{CHEVRON}</span></span></div>'
             f'<div class="empty"><b>This Document has no template Lines.</b><br>'
             f'A template Line is a Comment whose Effect starts with <span class="mono">template</span>; the templater '
             f'turns each karaoke Line of its Style into new Lines. 18 karaoke Lines (Style T1a) are ready.</div></div>')
    rows = [mrow(None, "Template for each syllable", "template syl", hover=True),
            mrow(None, "Template for each Line", "template line"),
            mrow(None, "Template before each Line", "template pre-line"),
            mrow(None, "Template for each character", "template char"), MSEP,
            mrow(None, "Code run once", "code once"),
            mrow(None, "Code for each Line", "code line"),
            mrow(None, "Code for each syllable", "code syl"), MSEP,
            '<div class="phint">The new Line goes above the first karaoke Line, with that Line\'s Style.</div>']
    m = menu(rows, left=PW - 384, top=80, width=372)
    cells.append(cell(7, "No template Lines yet: what a template is, and New.",
                      te_panel(t, PW, body=empty, preview_on=False, h=420) + m,
                      "New inserts a template or code Line as a Comment in the Grid, ready to type into. KE3's built-in "
                      "effect presets will be offered from this menu too (their own card).", h=440))

    # 8 Narrow dock.
    body = te_body(t, narrow=True, editor=editor_block(t, narrow=True), preview=preview_block(t, collapsed=True),
                   foot=footer(t, narrow=True))
    cells.append(cell(8, "At the panel's minimum width (360 px) the fields stack and the preview folds.",
                      '<div style="display:flex;gap:16px">' + te_panel(t, 360, body=body) +
                      '<div class="note" style="max-width:250px;margin-top:4px">Labels go above their fields '
                      '(MuseScore\'s narrow rule), the list drops the Style and text columns, and Preview on Line 12 '
                      'becomes a one-row section that unfolds on click. Nothing moves into a scroll-only area: the '
                      'panel scrolls as a whole.</div></div>'))

    # 9 The video overlay at full size, and the problem state on video.
    v = (f'<div style="display:flex;flex-direction:column;gap:10px">{frame(t, 640, 360)}'
         f'<div class="cap2">The same frame after a problem: the last output that worked, marked.</div>'
         f'{frame(t, 640, 360, badge="stale")}</div>')
    cells.append(cell(9, "On the video: the preview replaces Line 12's generated Lines, as Apply will.",
                      v,
                      "The frame draws the Document with Line 12's old generated Lines left out and the preview's "
                      "Lines put in, through the same libass track, so text measurement and placement match what Apply "
                      "writes. The badge sits top left (the render-cost overlay moves below it when both are on); the "
                      "eye in the panel header turns the preview off.", h=780))
    return cells


def css(t):
    hc = t["hc"]
    syn = "".join(f".s{i}{{color:{c}}}" for i, c in enumerate(t["syn"]) if c)
    return f"""
*{{box-sizing:border-box}}
html,body{{margin:0;background:{t['bg']};color:{t['text']};font-family:"Noto Sans","Segoe UI",sans-serif;font-size:13px;line-height:1.35}}
.top{{padding:14px 18px 2px;display:flex;align-items:baseline;gap:14px}}
.top h1{{font-size:15px;margin:0;font-weight:650}}
.top span{{color:{t['muted']};font-size:12px}}
.grid{{display:grid;grid-template-columns:660px 660px;gap:4px 22px;padding:10px 18px 18px}}
.cell{{position:relative;padding-bottom:6px}}
.cell.wide{{grid-column:1 / span 2}}
.cap{{font-size:12px;font-weight:600;margin:8px 0 6px 2px}}
.cap b{{display:inline-flex;width:18px;height:18px;align-items:center;justify-content:center;border:1px solid {t['control']};border-radius:9px;font-size:11px;margin-right:4px}}
.cap2{{font-size:11.5px;color:{t['muted']}}}
.note{{font-size:11.5px;color:{t['muted']};margin:0 2px 8px;max-width:650px;min-height:48px}}
.cell.wide .note{{max-width:1300px;min-height:0}}
.stage{{position:relative}}
.mono{{font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:12px}}
.grow{{flex:1}}
.dock{{position:relative;background:{t['field']};border:1px solid {t['line']};display:flex;flex-direction:column;overflow:hidden}}
.dhead{{height:35px;flex:none;display:flex;align-items:center;gap:2px;padding:0 6px 0 12px;border-bottom:1px solid {t['line']};background:{t['panel']}}}
.dhead .t{{font-weight:700}}
.tabs{{display:flex;align-self:stretch;margin-left:-12px}}
.tab{{display:flex;align-items:center;padding:0 14px;color:{t['muted']};font-weight:600}}
.tab.on{{color:{t['text']};box-shadow:inset 0 -2px 0 {t['accent']};background:{t['field']}}}
.dbody{{position:relative;display:flex;flex-direction:column;flex:1;min-height:0}}
.ic{{width:16px;height:16px;display:inline-flex;flex:none}}
.ic svg{{width:16px;height:16px}}
svg #accent{{color:{t['accent']}}}
.b{{position:relative;height:26px;min-width:26px;display:inline-flex;align-items:center;justify-content:center;border:1px solid transparent;border-radius:3px;color:{t['text']}}}
.b.tg{{background:{t['select']};border-color:{t['accent'] if hc else 'transparent'};box-shadow:inset 0 -2px 0 {t['accent']}}}
.sep{{width:1px;height:18px;background:{t['line']};margin:0 3px}}
.sec{{padding:10px 12px 8px;border-bottom:1px solid {t['line']}}}
.sec.fold{{display:flex;align-items:center;gap:6px}}
.sec.fold .sh{{margin:0;flex:1;min-width:0}}
.fold-ch{{display:inline-flex;transform:rotate(-90deg);color:{t['muted']}}}
.sh{{display:flex;align-items:center;gap:8px;font-weight:700;margin-bottom:8px;white-space:nowrap}}
.dimx{{color:{t['muted']};font-weight:400;font-size:12px;overflow:hidden;text-overflow:ellipsis}}
.dimx.r{{font-variant-numeric:tabular-nums}}
.warnx{{color:{t['warning']};font-weight:600;font-size:12px}}
.ab2{{display:inline-flex;align-items:center;gap:5px;height:24px;padding:0 4px 0 6px;border:1px solid {t['control']};border-radius:3px;background:{t['raised']};font-weight:600;font-size:12px}}
.ab2.open{{background:{t['select']}}}
.aa2{{display:inline-flex;margin-left:2px;color:{t['muted']}}}
.tl{{border:1px solid {t['line']};background:{t['field']}}}
.tr{{display:flex;align-items:center;gap:8px;height:28px;padding:0 8px 0 4px;white-space:nowrap;font-size:12.5px}}
.tr+.tr{{border-top:1px solid {t['line'] if hc else t['alt']}}}
.tr.cur{{background:{t['select']};box-shadow:inset 2px 0 0 {t['accent']}}}
.dm{{width:12px;color:{t['accent']};font-size:11px;text-align:center;flex:none}}
.pm{{width:12px;color:{t['danger']};display:inline-flex;flex:none}}
.pm .ic,.pm .ic svg{{width:12px;height:12px}}
.tn{{width:22px;text-align:right;color:{t['muted']};font-variant-numeric:tabular-nums;flex:none}}
.tef{{width:178px;flex:none;font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:12px;overflow:hidden;text-overflow:ellipsis}}
.tef .ew{{color:{t['syn'][9]};font-weight:600}}
.tef .em{{color:{t['syn'][6]}}}
.tl.narrow .tef{{width:150px}}
.ts{{width:36px;flex:none;color:{t['muted']}}}
.tt{{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;color:{t['muted']};font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:11.5px}}
.st{{flex:none;color:{t['muted']};font-size:11.5px;font-variant-numeric:tabular-nums;min-width:86px;text-align:right}}
.st.bad{{color:{t['danger']};font-weight:700}}
.frow{{display:flex;align-items:center;gap:8px;margin-bottom:8px}}
.frow.col{{flex-direction:column;align-items:stretch;gap:3px}}
.fl{{color:{t['muted']};font-size:12px}}
.f{{display:inline-flex;align-items:center;height:26px;border:1px solid {t['control']};border-radius:3px;background:{t['field']};padding-left:6px;overflow:hidden}}
.f.wide{{width:100%}}
.f.open{{border-color:{t['accent']};box-shadow:0 0 0 1px {t['accent']}}}
.f .fv{{flex:1;white-space:nowrap;overflow:hidden}}
.f.mono .fv{{font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:12px}}
.fp{{display:inline-flex;align-items:center;justify-content:center;width:22px;align-self:stretch;border-left:1px solid {t['line']};color:{t['muted']}}}
.code{{border:1px solid {t['control']};border-radius:3px;background:{t['field']};padding:7px 8px;min-height:64px;font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:12.5px;line-height:1.55;word-break:break-all}}
.code.small{{min-height:0;font-size:11.5px;padding:5px 7px;line-height:1.4}}
{syn}
.caret{{display:inline-block;width:1.5px;height:15px;background:{t['text']};vertical-align:-3px;margin-left:1px}}
.squig{{text-decoration:underline wavy {t['danger']};text-underline-offset:3px;text-decoration-thickness:1.5px}}
.pbar{{display:flex;gap:6px;align-items:flex-start;margin-top:6px;color:{t['danger']};font-size:12px}}
.pbar .ic{{margin-top:1px}}
.pbar span{{color:{t['text']}}}
.hint2{{font-size:11px;color:{t['muted']};margin-top:6px}}
.kc{{display:inline-block;font-size:10.5px;line-height:14px;padding:0 4px;border:1px solid {t['control']};border-bottom-width:2px;border-radius:3px;color:{t['text']};background:{t['raised']};font-weight:600;margin:0 1px}}
.gen{{border:1px solid {t['line']};background:{t['field']};font-size:12px}}
.gh,.gr{{display:flex;align-items:center;gap:8px;height:22px;padding:0 8px;white-space:nowrap}}
.gh{{color:{t['muted']};font-size:11px;border-bottom:1px solid {t['line']};background:{t['panel']}}}
.gr+.gr{{border-top:1px solid {t['line'] if hc else t['alt']}}}
.gr.mine{{box-shadow:inset 2px 0 0 {t['accent']}}}
.gr.stale{{opacity:{1 if hc else .55}}}
.gr.stale .gx{{text-decoration:{'line-through' if hc else 'none'}}}
.gl{{width:14px;text-align:right;color:{t['muted']};font-variant-numeric:tabular-nums}}
.gt{{width:72px;font-variant-numeric:tabular-nums}}
.gx{{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;font-family:"Noto Sans Mono","Cascadia Mono",monospace;font-size:11.5px;color:{t['muted']}}}
.foot{{display:flex;align-items:center;gap:8px;padding:8px 12px;margin-top:auto;font-size:12px;background:{t['panel']};border-top:1px solid {t['line']}}}
.foot .dm{{width:auto}}
.okx,.badx{{display:inline-flex;width:14px;height:14px;flex:none}}
.okx svg,.badx svg{{width:14px;height:14px}}
.okx{{color:{t['success']}}}
.badx{{color:{t['danger']}}}
.wb2{{display:inline-flex;align-items:center;height:28px;padding:0 14px;border:1px solid {t['control']};border-radius:3px;background:{t['raised']};flex:none}}
.apl{{display:inline-flex;height:28px;flex:none}}
.apl .af{{display:inline-flex;align-items:center;padding:0 16px;background:{t['accent']};color:{t['onaccent']};font-weight:700;border-radius:3px 0 0 3px;border:{'2px solid ' + t['text'] if hc else '1px solid ' + t['accent']}}}
.apl .aa{{display:inline-flex;align-items:center;justify-content:center;width:20px;background:{t['accent']};color:{t['onaccent']};border-radius:0 3px 3px 0;border:{'2px solid ' + t['text'] if hc else '1px solid ' + t['accent']};border-left:1px solid {t['onaccent']}}}
.apl.open .aa{{outline:2px solid {t['text']};outline-offset:1px}}
.apl.dis .af,.apl.dis .aa{{background:{t['raised']};color:{t['disabled']};border-color:{t['line']}}}
.menu,.pop{{position:absolute;z-index:5;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};border-radius:3px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,{0 if hc else .28})}}
.mi{{display:flex;align-items:center;gap:8px;min-height:28px;padding:3px 12px 3px 10px}}
.mi .mt{{flex:1;white-space:nowrap}}
.msub{{display:block;font-size:11px;color:{t['muted']};white-space:normal}}
.mi .mk2{{color:{t['muted']};font-size:12px;white-space:nowrap;font-family:"Noto Sans Mono","Cascadia Mono",monospace}}
.mi.hov{{background:{t['raised']}}}
.mi.dis{{color:{t['disabled']}}}
.cb{{width:14px;height:14px;display:inline-flex;align-items:center;justify-content:center;flex:none}}
.cb svg{{width:14px;height:14px}}
.msep{{height:1px;background:{t['line']};margin:4px 0}}
.phead{{font-size:11px;color:{t['muted']};font-weight:600;padding:4px 12px 2px}}
.phint{{font-size:11px;color:{t['muted']};padding:2px 12px 4px}}
.ac{{display:flex;align-items:center;gap:10px;height:24px;padding:0 12px;font-size:12px}}
.ac.hov{{background:{t['select']};box-shadow:inset 2px 0 0 {t['accent']}}}
.acn{{width:84px;font-family:"Noto Sans Mono","Cascadia Mono",monospace;color:{t['syn'][6]};font-weight:600}}
.acv{{width:36px;text-align:right;font-variant-numeric:tabular-nums;font-weight:700}}
.acd{{color:{t['muted']}}}
.tip{{position:absolute;z-index:6;background:{t['panel']};border:1px solid {t['line'] if hc else t['control']};padding:4px 8px;font-size:12px;border-radius:2px;max-width:310px}}
.empty{{padding:14px 12px;color:{t['muted']};font-size:12.5px;line-height:1.5}}
.empty b{{color:{t['text']}}}
/* the video frame: not themed */
.frame{{position:relative;overflow:hidden;background:#0B1026;font-family:"Noto Sans","Segoe UI",sans-serif}}
.sky{{position:absolute;inset:0;background:linear-gradient(180deg,#0B1433 0%,#22285A 45%,#5A3D6E 72%,#C47A6A 100%)}}
.hill{{position:absolute;left:-10%;right:-10%;bottom:-40%;height:70%;border-radius:50%;background:#14142A}}
.kline{{position:absolute;top:calc(28px * var(--s));left:0;right:0;text-align:center;font-size:calc(30px * var(--s));font-weight:700;letter-spacing:.5px}}
.ks{{display:inline-block;margin:0 calc(5px * var(--s));color:#FFFFFF;-webkit-text-stroke:calc(1.5px * var(--s)) #2A1B3D;paint-order:stroke fill;text-shadow:0 0 calc(4px * var(--s)) rgba(0,0,0,.6)}}
.ks.sung{{color:#FF9EC7}}
.ks.now{{color:#FF69B4;transform:scale(1.25);margin:0 calc(12px * var(--s));text-shadow:0 0 calc(10px * var(--s)) #FF69B4}}
.ksrc{{position:absolute;top:calc(70px * var(--s));left:0;right:0;text-align:center;color:#fff;opacity:.4;font-size:calc(22px * var(--s))}}
.sub2{{position:absolute;bottom:calc(22px * var(--s));left:0;right:0;text-align:center;font-size:calc(22px * var(--s));color:#fff;-webkit-text-stroke:calc(1.2px * var(--s)) #000;paint-order:stroke fill}}
.vbadge{{position:absolute;left:8px;top:8px;display:flex;align-items:center;gap:6px;padding:3px 8px 3px 6px;background:rgba(10,12,16,.78);color:#F2F5F8;border:1px solid rgba(255,255,255,.35);border-radius:3px;font-size:12px}}
.vbadge svg #accent{{color:#9CDBC9}}
.vbadge.warn{{border-color:#E0A030}}
.vbadge.warn .ic{{color:#E0A030}}
/* the window */
.win2{{background:{t['bg']};border:1px solid {t['line'] if hc else t['control']};box-shadow:0 6px 22px rgba(0,0,0,{0 if hc else .35})}}
.wtitle{{height:32px;display:flex;align-items:center;gap:2px;padding:0 0 0 8px;background:{t['panel']};border-bottom:1px solid {t['line']}}}
.wtitle .ic{{margin:0 14px}}
.appi{{width:16px;height:16px;border-radius:4px;background:{t['accent']};margin-right:8px}}
.mt2{{padding:0 8px;line-height:30px}}
.lay{{display:inline-flex;align-items:center;gap:6px;margin-left:10px;height:24px;padding:0 6px;border:1px solid {t['line']};border-radius:3px}}
.lay .ic{{margin:0}}
.wtt{{color:{t['muted']};font-size:12px}}
.doctabs{{display:flex;height:30px;align-items:stretch;padding-left:4px;background:{t['bg']}}}
.dt{{display:flex;align-items:center;padding:0 14px;color:{t['muted']}}}
.dt.on{{background:{t['panel']};color:{t['text']};box-shadow:inset 0 2px 0 {t['accent']}}}
.wmain{{display:flex;gap:4px;padding:4px 4px 0}}
.wcol{{display:flex;flex-direction:column;gap:4px}}
.vbody{{padding:12px;display:flex;flex-direction:column;gap:8px;align-items:center;background:{t['field']}}}
.vtrans{{display:flex;align-items:center;gap:2px;width:100%}}
.vt{{margin-left:10px;color:{t['muted']};font-size:12px;font-variant-numeric:tabular-nums}}
.abody{{position:relative;flex:1;background:#202225;margin:6px;border:1px solid {t['line']}}}
.asyl{{position:absolute;top:8px;height:58px;border-left:1px solid #8791FD;color:#8791FD;font-size:12px;padding:2px 4px;background:repeating-linear-gradient(90deg,rgba(135,145,253,.10) 0 2px,transparent 2px 5px)}}
.acur{{position:absolute;top:0;bottom:0;left:300px;width:1px;background:#8791FD}}
.grid2{{font-size:12px}}
.g2h,.g2r{{display:flex;align-items:center;height:22px;padding:0 6px;white-space:nowrap;gap:8px}}
.g2h{{color:{t['muted']};border-bottom:1px solid {t['line']};background:{t['panel']}}}
.g2r.cmt{{background:{t['comment']}}}
.g2r.cur{{background:{t['select']};outline:1px solid {t['accent']};outline-offset:-1px}}
.c0{{width:34px;display:flex;gap:3px;justify-content:flex-end;font-variant-numeric:tabular-nums}}
.c0 .dm{{width:auto}}
.c1{{width:78px;font-variant-numeric:tabular-nums}}
.c2{{width:40px}}
.c3{{width:150px;color:{t['muted']}}}
.c4{{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis}}
.sbar{{display:flex;gap:18px;align-items:center;height:26px;padding:0 12px;font-size:12px;color:{t['muted']};border-top:1px solid {t['line']}}}
.sbar .ic{{vertical-align:-3px}}
"""


def page(key, t):
    cells = sheet(key, t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>KE2 Template editor — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>KE2 Template editor — {t["name"]}</h1>'
            f'<span>Mockup for #284: a dock panel for template and code Lines, with a live preview of the current Line '
            f'on the video. New icons: Template editor, Loop the Line, Problem.</span></div>'
            f'<div class="grid">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"ke2-{key}.html").write_text(page(key, t))
print("wrote", ", ".join(f"ke2-{k}.html" for k in THEMES))
