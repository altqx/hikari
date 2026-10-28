#!/usr/bin/env python3
"""HY1 #271 Hydra inspector strip mockup: one HTML sheet per theme.

Reuses VT1's mockup pieces (gen_vt1_ref.py: themes, buttons, fields, the
bar's CSS) so the strip reads as the merged inspector bar does."""
import pathlib
import sys

HERE = pathlib.Path(__file__).parent
ICON_DIR = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else HERE / "out")
OUT.mkdir(parents=True, exist_ok=True)

ref = (HERE / "gen_vt1_ref.py").read_text()
sys.argv = ["gen", str(ICON_DIR), str(OUT)]
exec(ref[: ref.index("BARS = {}")])
exec(ref[ref.index("def rail("): ref.index("def shape_menu(")])
exec(ref[ref.index("def css(t):"): ref.index("def strip(")])

HYDRA = 10

# ---------------------------------------------------------------- pieces


def tagpick(name, code, state=""):
    return (f'<span class="combo tagpick {state}">{icon("search")}<span class="cv">{name}</span>'
            f'<span class="code">\\{code}</span>{chev()}</span>')


def change(on, variant=None, vstate="", vwidth=176):
    segs = "".join(f'<span class="{"on" if s == on else ""}">{s}</span>' for s in ["Set", "Add", "Multiply", "Gradient"])
    v = combo(variant, vwidth, vstate) if variant else ""
    return grp("Change", f'<span class="segtext">{segs}</span>', v)


def swatch(rgb):
    return f'<span class="sw" style="background:rgb({rgb})"></span>'


def changes(n, picked=True):
    if not picked:
        return ('<span class="target"><span class="tc strong">Changes the active line</span>' +
                btn("pick-lines") + '</span>')
    return (f'<span class="target"><span class="tc strong">Changes {n} lines</span>' +
            btn("pick-lines", "on") + btn("clear") + '</span>')


def hydra(tag, values, ch, tgt, cls=""):
    return bar(tool(HYDRA, "Hydra"), SEP, grp("Tag", tag), SEP, grp(None, *values), SEP, ch, tgt=tgt, cls=cls)


def mark(label):
    # The value a drag on the video changes.
    return f'<span class="dt">◆</span>{label}'


MAIN = hydra(tagpick("blur", "blur"), [num("Value", "2.5", width=62)], change("Set", "At the cursor", "dis"),
             changes(3))

# ---------------------------------------------------------------- video


def video_hydra(w, h, hud=False, outlines=True):
    o = ""
    if outlines:
        o = ('<div class="pick" style="left:29%;top:82%;width:42%;height:12%"><i>1</i></div>'
             '<div class="pick" style="left:19%;top:11%;width:24%;height:11%"><i>2</i></div>'
             '<div class="pick" style="left:61%;top:29%;width:28%;height:11%"><i>3</i></div>')
    hd = ""
    if hud:
        hd = ('<div class="hud" style="left:52%;top:54%">blur&nbsp; 2.5 → <b>4</b></div>'
              '<div class="drag" style="left:38%;top:61%;width:13%"></div>'
              '<svg class="cursor" style="left:50.4%;top:59.6%" viewBox="0 0 18 18"><path d="M1 9h16M1 9l4-4M1 9l4 4M17 9l-4-4M17 9l-4 4" '
              'stroke="#fff" stroke-width="3"/><path d="M1 9h16M1 9l4-4M1 9l4 4M17 9l-4-4M17 9l-4 4" stroke="#000" stroke-width="1.3"/></svg>')
    return (f'<div class="video" style="width:{w}px;height:{h}px"><div class="scene"></div>'
            f'<div class="sign" style="left:20%;top:12%;font-size:{h*0.05:.0f}px;filter:blur({2 if hud else 1.2}px)">KITA-SENJU</div>'
            f'<div class="sign" style="left:62%;top:30%;font-size:{h*0.05:.0f}px;filter:blur({2 if hud else 1.2}px)">Platform 3 →</div>'
            f'<div class="sub" style="font-size:{h*0.065:.0f}px;filter:blur({2 if hud else 1.2}px)">I\'ll be waiting at the station.</div>{o}{hd}</div>')


def lastop(editing=False, style=""):
    v = num("Value", "3" if editing else "2.5", state="foc" if editing else "", width=58)
    note = ('<div class="lnote">Replaces that step: still one undo step</div>' if editing else
            '<div class="lnote">Change the value to redo it without a new drag</div>')
    return (f'<div class="lastop2" style="{style}"><div class="lh"><b>Last change</b>'
            f'<span class="lm">blur · Set · 3 lines</span><span class="grow"></span><span class="x">✕</span></div>'
            f'<div class="lv">{v}</div>{note}</div>')


def dock_hydra(w, h, vid_h):
    return (f'<div class="dock" style="width:{w}px;height:{h}px">'
            f'<div class="dhead"><span class="t">Video</span>{icon("panel-menu")}</div>'
            f'<div class="dbody"><div class="top">{rail(HYDRA)}<div class="stage" style="position:relative">'
            f'{video_hydra(w - 52, vid_h)}{lastop(style="right:14px;top:14px")}</div></div>'
            f'{MAIN}{transport()}</div></div>')

# ---------------------------------------------------------------- tag list

COMMON = [("blur", "blur"), ("blur edge", "be"), ("border", "bord"), ("shadow", "shad"), ("font size", "fs"),
          ("spacing", "fsp"), ("alpha", "alpha"), ("color1", "1c"), ("color3", "3c")]
MORE = [("1a", "1a"), ("3a", "3a"), ("4a", "4a"), ("color4", "4c"), ("fading", "fad"), ("fax", "fax"),
        ("fay", "fay"), ("xborder", "xbord"), ("yborder", "ybord"), ("xshadow", "xshad"), ("yshadow", "yshad"),
        ("position", "pos"), ("tanimation", "t")]
MINE = [("glow edge", "be")]


def li(name, code, chk=False, hi=False):
    return (f'<div class="mi {"chk" if chk else ""} {"hi" if hi else ""}"><span class="mk">{"✓" if chk else ""}</span>'
            f'<span class="grow">{name}</span><span class="code">\\{code}</span></div>')


def section(title, open_=True, count=None):
    arrow = ('<svg viewBox="0 0 16 16" width="12" height="12"><path d="' + ("M4 6l4 4 4-4" if open_ else "M6 4l4 4-4 4") +
             '" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>')
    c = f'<span class="cnt">{count}</span>' if count else ""
    return f'<div class="msec"><span class="arr">{arrow}</span><b>{title}</b>{c}</div>'


def taglist(query="", items=None, more_open=False, left=0, top=0):
    q = (f'<div class="msearch">{icon("search")}<span class="{"ph" if not query else ""}">'
         f'{query or "Search tags"}</span>{"<i class=caret></i>" if query else ""}</div>')
    if items is None:
        rows = section("Common") + "".join(li(n, c, n == "blur") for n, c in COMMON)
        rows += section("More", more_open, None if more_open else len(MORE))
        if more_open:
            rows += "".join(li(n, c) for n, c in MORE)
        rows += section("Your tags") + "".join(li(n, c) for n, c in MINE)
    else:
        rows = "".join(li(n, c, hi=i == 0) for i, (n, c) in enumerate(items))
    return (f'<div class="menu tl" style="left:{left}px;top:{top}px">{q}<div class="ms"></div>{rows}<div class="ms"></div>'
            f'<div class="mi"><span class="mk">{icon("tag-edit")}</span><span>Edit tags…</span></div></div>')


def variant_menu(items, chk, left, top, note=None):
    rows = "".join(f'<div class="mi {"chk" if i == chk else ""}"><span class="mk">{"✓" if i == chk else ""}</span>'
                   f'<span>{it}</span></div>' for i, it in enumerate(items))
    n = f'<div class="ms"></div><div class="mnote">{note}</div>' if note else ""
    return f'<div class="menu" style="left:{left}px;top:{top}px;min-width:230px">{rows}{n}</div>'

# ---------------------------------------------------------------- strips


def strip(label, html, extra="", h=None):
    hh = f"height:{h}px;" if h else ""
    return (f'<div><div class="lab">{label}</div><div style="position:relative;{hh}">'
            f'{html.replace("<div class=" + chr(34) + "bar ", "<div class=" + chr(34) + "bar strip ", 1)}{extra}</div></div>')


S_SET1 = hydra(tagpick("blur", "blur"), [num("Value", "2.5", width=62)], change("Set", "At the cursor"),
               changes(0, picked=False))
S_ADD = hydra(tagpick("border", "bord"), [num("By", "+1.5", width=62)], change("Add"), changes(3))
S_MUL = hydra(tagpick("font size", "fs"), [num("Start", "70", width=54), num("Per line", "+4", width=54)],
              change("Multiply", "Set by line number", "open", 170), changes(5))
S_GRAD = hydra(tagpick("blur edge", "be"), [num("From", "0", width=50), num("To", "8", width=50)],
               change("Gradient", "Across characters, increasing", "", 214), changes(0, picked=False))
S_COL = hydra(tagpick("color1", "1c"), [swatch("250,214,92"), num("R", "250", width=44), num("G", "214", width=44),
                                        num("B", "92", width=44)], change("Set", "At the start of the text", "dis"),
              changes(3))
S_COLG = ('<div class="bar wrap"><div class="flow">' + tool(HYDRA, "Hydra") + SEP + grp("Tag", tagpick("color3", "3c")) +
          grp(None, '<span class="gl">From</span>', swatch("20,20,20"), num("R", "20", width=38), num("G", "20", width=38),
              num("B", "20", width=38), '<span class="arrow">→</span>', '<span class="gl">To</span>', swatch("160,40,200"),
              num("R", "160", width=38), num("G", "40", width=38), num("B", "200", width=38)) +
          change("Gradient", "Across lines, increasing", "", 180) + '</div>' + changes(6) + '</div>')
S_POS = hydra(tagpick("position", "pos"), [num("X", "960", width=56), num(mark("Y"), "1012", width=56)],
              change("Set", "At the start of the text", "dis"), changes(3))
S_FAD = hydra(tagpick("fading", "fad"), [num("In", "200", "ms", width=62), num("Out", "350", "ms", width=62)],
              change("Set", "At the start of the text", "dis"), changes(3))
S_T = hydra(tagpick("tanimation", "t"), [num("Start", "0", "ms", width=62), num("End", "800", "ms", width=62)],
            change("Set", "At the cursor"), changes(0, picked=False))
S_DRAG = hydra(tagpick("blur", "blur"), [num(mark("Value"), "4", state="scrub", width=62)],
               change("Set", "At the cursor", "dis"), changes(3))

NARROW = ('<div class="bar wrap"><div class="flow">' + tool(HYDRA, "Hydra") + SEP + grp("Tag", tagpick("blur", "blur")) +
          grp(None, num("Value", "2.5", width=62)) + change("Set", "At the cursor", "dis") + '</div>' +
          changes(3) + '</div>')

DRAG_WRAP = ('<div class="bar wrap strip"><div class="flow">' + tool(HYDRA, "Hydra") + SEP + grp("Tag", tagpick("blur", "blur")) +
             grp(None, num(mark("Value"), "4", state="scrub", width=62)) + change("Set", "At the cursor", "dis", 120) + '</div>' +
             changes(3) + '</div>')

EXTRA_CSS = """
.tagpick{min-width:150px;gap:6px;padding-left:6px;background:var(--field)}
.tagpick .cv{flex:1}
.code{color:var(--muted);font-family:"Noto Sans Mono","DejaVu Sans Mono",monospace;font-size:11.5px}
.segtext span{cursor:default}
.sw{width:22px;height:22px;border:1px solid var(--control);border-radius:2px;flex:none;margin-right:6px;box-shadow:inset 0 0 0 1px var(--panel)}
.arrow{color:var(--muted);margin:0 8px 0 2px}
.dt{color:var(--accent);font-size:9px;margin-right:3px;vertical-align:1px}
.pick{position:absolute;border:1.5px dashed #6fd3ff;border-radius:2px}
.pick i{position:absolute;left:-1.5px;top:-17px;font-style:normal;font-size:11px;font-weight:700;background:#6fd3ff;color:#0b2230;padding:0 4px;border-radius:2px 2px 0 0}
.hud{position:absolute;background:rgba(10,14,18,.86);color:#fff;font-size:12px;padding:3px 8px;border-radius:3px;border:1px solid rgba(255,255,255,.35);font-variant-numeric:tabular-nums;white-space:nowrap}
.drag{position:absolute;height:0;border-top:1.5px dotted #fff;opacity:.85}
.lastop2{position:absolute;background:var(--panel);border:1px solid var(--line);border-radius:4px;box-shadow:0 8px 24px rgba(0,0,0,.35);padding:8px 10px 8px;width:268px;font-size:12px;z-index:5}
.lh{display:flex;align-items:center;gap:8px;margin-bottom:6px}
.lm{color:var(--muted)}
.x{color:var(--muted);font-size:11px;width:18px;text-align:center}
.lv{display:flex;align-items:center}
.lnote{color:var(--muted);font-size:11.5px;margin-top:5px}
.menu.tl{width:300px}
.msearch{display:flex;align-items:center;gap:6px;height:26px;margin:2px 6px 4px;padding:0 8px;border:1px solid var(--accent);box-shadow:inset 0 0 0 1px var(--accent);background:var(--field);border-radius:2px}
.msearch .ph{color:var(--muted)}
.caret{display:inline-block;width:1px;height:14px;background:var(--text);margin-left:-4px}
.msec{display:flex;align-items:center;gap:6px;height:26px;padding:0 10px 0 8px;font-size:12px}
.msec .arr{width:14px;color:var(--text);display:inline-flex;justify-content:center}
.num.foc .nf{border-color:var(--accent);box-shadow:inset 0 0 0 1px var(--accent)}
.num.foc .nv::after{content:"";display:inline-block;width:1px;height:14px;background:var(--text);margin-left:1px;vertical-align:-2px}
.msec .cnt{color:var(--muted);font-weight:400;margin-left:auto}
.mnote{font-size:11.5px;color:var(--muted);padding:2px 14px 4px 30px;max-width:240px;white-space:normal}
.mi .code{margin-left:16px}
.mi.hi .code{color:var(--onaccent)}
.q{font-size:12px;line-height:1.45;max-width:420px}
.q li{margin:0 0 5px}
"""


def page(key, t):
    seg_note = ('<div class="legend" style="max-width:440px">The <b>change option</b> is a four-way switch. '
                'The choices that refine it sit in the list beside it: where Set puts the tag, '
                'the two ways Multiply counts lines, the four gradients. Every one of legacy\'s eight options '
                'and three placings is there.</div>')
    body = f"""
<div class="themeName">HY1 · Hydra: the inspector strip · {t['name']}</div>
<div class="row">
 <div><div class="cap">1 · Video panel: Hydra on, three Lines picked and outlined, the last change card</div>{dock_hydra(1100, 700, 520)}</div>
 <div style="display:flex;flex-direction:column;gap:14px;width:600px">
  <div class="cap">2 · The tag list: search, Common first, the rest under More, your own tags last</div>
  <div style="position:relative;height:540px">
   <div style="position:absolute;left:0;top:0;width:290px">{grp("Tag", tagpick("blur", "blur", "open"))}</div>
   {taglist(left=0, top=30)}
   <div style="position:absolute;left:316px;top:0;width:290px">{grp("Tag", tagpick("blur", "blur", "open"))}</div>
   {taglist("sha", [("shadow", "shad"), ("xshadow", "xshad"), ("yshadow", "yshad")], left=316, top=30)}
   <div class="legend" style="position:absolute;left:316px;top:236px;width:280px">Typing filters every tag at once, by name or by tag (<b>sha</b> finds shadow, xshadow and yshadow). Enter takes the first match. <b>Edit tags…</b> opens the Tag editing dialog, as the Edit button did.<br><br>Shift+wheel over the video still steps through the list, in this order.</div>
  </div>
 </div>
</div>
<div class="row"><div class="strips" style="width:1250px">
 <div class="cap">3 · The change option: Set, Add, Multiply, Gradient; the rest under them</div>
 {strip("Set, one Line: the tag goes where the definition places it (at the cursor, at the start of the text, or the \\t clip rectangle)", S_SET1)}
 {strip("Add: every \\bord in the Lines moves by the amount", S_ADD)}
 {strip("Multiply: Line n gets Start + n × Per line (Set by line number), or its own value + n × Per line (Add by line number)", S_MUL,
        variant_menu(["Set by line number", "Add by line number"], 0, 718, 34), h=118)}
 {strip("Gradient: one value per character or per Line, from From to To", S_GRAD,
        variant_menu(["Across characters, increasing", "Across characters, decreasing", "Across lines, increasing",
                      "Across lines, decreasing"], 0, 688, 34), h=150)}
</div>
<div style="display:flex;flex-direction:column;gap:12px;width:440px;padding-top:22px">
 {seg_note}
 <div class="legend" style="max-width:440px"><b>The values say what they do.</b> Set shows Value; Add shows By; Multiply shows Start and Per line; a gradient shows From and To. Legacy left these in a slider's distance from where the drag began.</div>
 <div class="legend" style="max-width:440px"><b>Changes N lines</b> sits where VT1's target slot was. The picked Lines are outlined and numbered on the video before you apply, in the order Multiply and line gradients count them. With none picked it reads <b>Changes the active line</b>.</div>
 <div class="legend" style="max-width:440px"><b>With several Lines</b>, the tag always goes at the start of each Line's text, so the placing list greys out.</div>
</div></div>
<div class="row"><div class="strips" style="width:1250px">
 <div class="cap">4 · Tags with several values</div>
 {strip("color1: red, green and blue (0–255), with a swatch", S_COL)}
 {strip("color3 across six Lines: From and To for each channel; at this width the strip wraps", S_COLG)}
 {strip("position: X and Y. The ◆ marks the value a drag on the video changes; click a field to move it", S_POS)}
 {strip("fading: fade in and fade out", S_FAD)}
 {strip("tanimation (\\t): start and end time. A tag selected in the Line editor goes inside the \\t, as before", S_T)}
 <div class="cap" style="margin-top:8px">6 · Narrow panel: the strip wraps by groups, Changes N lines keeps its slot</div>
 <div style="width:560px">{NARROW}</div>
</div>
<div style="display:flex;flex-direction:column;gap:12px;width:440px">
 <div class="cap">5 · Dragging on the video</div>
 <div style="position:relative;width:440px">{video_hydra(440, 248, hud=True)}</div>
 <div style="width:440px">{DRAG_WRAP}</div>
 <div class="legend" style="max-width:440px">Drag sideways anywhere on the video to change the ◆ value. The Lines preview live, and a drag is one undo step on release. Shift gives finer steps, and Esc cancels. The wheel steps the value; Shift+wheel picks the next tag. The sliders drawn over the video are gone.</div>
 <div class="cap" style="margin-top:6px">7 · Editing the last change</div>
 <div style="position:relative;height:96px">{lastop(True, "left:0;top:0")}</div>
 <div class="legend" style="max-width:440px">After each apply, the card keeps that change's values. Change one and the change is redone in place of the same undo step. Another edit, an undo, or switching the tool or the Document closes the card.</div>
</div></div>
"""
    return (f"<!doctype html><html><head><meta charset='utf-8'><title>HY1 {t['name']}</title>"
            f"<style>{css(t)}{EXTRA_CSS}</style></head><body>{body}</body></html>")


for key, t in THEMES.items():
    (OUT / f"hy1-{key}.html").write_text(page(key, t))
print("ok")
