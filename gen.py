import pathlib, sys, re
icons = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2])

def ic(role):
    if not role: return ""
    return f'<span class="ic">{(icons / f"{role}.svg").read_text().strip()}</span>'

def item(role, text, key="", sub=False, check=None, radio=None, active=False, new=False):
    cls = "it" + (" act" if active else "") + (" new" if new else "")
    mark = f'<span class="cb{" on" if check else ""}"></span>' if check is not None else ""
    if radio is not None: mark = f'<span class="rd{" on" if radio else ""}"></span>'
    return (f'<div class="{cls}">{mark}{ic(role)}<span class="t">{text}</span>'
            f'<span class="k">{key}</span><span class="ar">{"›" if sub else ""}</span></div>')
SEP = '<div class="sep"></div>'
def head(t): return f'<div class="hd">{t}</div>'
def menu(rows, style=""): return f'<div class="menu" style="{style}">{"".join(rows)}</div>'

names = {7:"Top left",8:"Top centre",9:"Top right",4:"Middle left",5:"Middle centre",6:"Middle right",1:"Bottom left",2:"Bottom centre",3:"Bottom right"}

grid = menu([item("join-lines","Join",sub=True), item("split-lines","Split",sub=True), item("swap-lines","Swap lines"), SEP,
             item("line-break-insert","Line breaks",sub=True), item("change-case","Change case",sub=True),
             item("dialogue-swap","Swap dialogue alternatives"), SEP,
             item("change-alignment","Change alignment, keep position",sub=True,new=True),
             item("align-center","Align and distribute",sub=True,active=True,new=True),
             item("pos-to-margins","Position to margins",new=True), item("margins-to-pos","Margins to position",new=True), SEP,
             item("clear","Delete text"), item("delete","Delete 3 lines")], "left:0;top:0")
align = menu([head("Relative to"), item(None,"The active line",radio=True,new=True), item(None,"The video frame",radio=False,new=True), SEP,
              item("align-left","Align left edges",new=True), item("align-center","Align centres",new=True), item("align-right","Align right edges",new=True), SEP,
              item("align-top","Align top edges",new=True), item("align-middle","Align middles",new=True), item("align-bottom","Align bottom edges",new=True), SEP,
              item("distribute-horizontal","Distribute horizontally",new=True), item("distribute-vertical","Distribute vertically",new=True)],
             "left:274px;top:208px")
changeal = menu([item(f"alignment-{n}", f"{names[n]} (\\an{n})", check=None, new=True) for n in (7,8,9,4,5,6,1,2,3)], "left:0;top:0;position:relative")

def toggle(role, on=True): return f'<span class="tg{" on" if on else ""}">{ic(role)}</span>'
strip = ('<div class="strip">' + "".join(toggle(r, r not in ("shift-drawings",)) for r in
         ("shift-position","shift-move-start","shift-move-end","shift-clips","shift-drawings","shift-origins")) +
         '<span class="vsep"></span>' + ic("shift-by-x") +
         '<span class="fld">X</span><span class="num focus">-12.5</span>' + ic("shift-by-y") +
         '<span class="fld">Y</span><span class="num">0</span><span class="hint">Enter shifts 3 lines</span></div>')

cols = ('<div class="dlg"><div class="title">Copy columns</div><div class="body">'
        '<div class="sub">Copy these columns of the 3 selected lines:</div><div class="grid2">'
        + "".join(f'<div class="ck"><span class="cb{" on" if on else ""}"></span>{n}</div>' for n,on in
                  [("Layer",False),("Start time",False),("End time",False),("Style",False),("Actor",False),("Margin left",False),
                   ("Margin right",False),("Margin vertical",False),("Effect",False),("Text",False)])
        + '</div><div class="grp new">Tags</div><div class="grid2">'
        '<div class="ck"><span class="cb"></span>Start tag block</div><div class="ck"><span class="cb on"></span>Clip (\\clip or \\iclip)</div></div>'
        '<div class="ck"><span class="cb on"></span>These tags:<span class="num wide mono">\\blur, \\bord, \\3c</span></div>'
        '<div class="hint">Paste puts each tag into the start block, replacing the same tag.</div>'
        '</div><div class="foot"><span class="btn">Select all</span><span class="btn">None</span><span class="grow"></span>'
        '<span class="btn">Cancel</span><span class="btn primary">Copy</span></div></div>')

cyc_rows = [("\\blur","0.6, 0.8, 1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10, 0.4, 0.5"),("\\bord","0, 1, 1.5, 2, 2.5, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 20"),
            ("\\shad","0, 1, 1.5, 2, 2.5, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12"),("\\alpha","FF, 00, 10, 30, 60, 80, A0, C0, E0"),
            ("\\an","1, 2, 3, 4, 5, 6, 7, 8, 9"),("\\fsp","0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 30")]
cyc = ('<div class="dlg"><div class="title">' + ic("cycle-values") + 'Settings › Editing › Cycle values</div><div class="body">'
       '<div class="sub">Each "Cycle" command steps the tag in the selected lines\' start block to the next value. "Cycle back" steps the other way.</div>'
       '<table><tr><th>Tag</th><th>Values, in order</th></tr>' +
       "".join(f'<tr><td class="mono">{t}</td><td><span class="num full mono">{v}</span></td></tr>' for t,v in cyc_rows) +
       '</table><div class="foot2"><span class="btn">Restore defaults</span></div></div></div>')

le = menu([item("select-all","Select all","Ctrl+A"), SEP, item("line-break-insert","Line breaks",sub=True),
           item("change-case","Change case",sub=True), item("dialogue-swap","Swap dialogue alternatives"),
           item("phantom","Phantom",sub=True,active=True,new=True), SEP, item("spellchecker","Check spelling",check=True)], "left:0;top:44px")
ph = menu([item(None,"Keep the start aligned",new=True), item(None,"Keep the end aligned",new=True)], "left:274px;top:150px")
lefield = '<div class="field mono">Watashi wa <span class="selt">Tanaka</span> desu.\\NBoku wa Tanaka-san no tomodachi.</div>'

def card(title, caption, body, h):
    return f'<div class="card"><div class="cap"><b>{title}</b><br>{caption}</div><div class="stage" style="height:{h}px">{body}</div></div>'

css = pathlib.Path(sys.argv[3]).read_text()
html = f'''<!doctype html><html><head><meta charset="utf-8"><title>AU21</title><style>{css}</style></head><body>
<script>document.body.className=(location.hash||"#light").slice(1)</script>
<div class="col">
{card("Grid: row menu", "A new group after AU20's text group. Align and distribute is open.", grid + align, 600)}
{card("Position shifter strip", "X and Y fields after the six toggles. Enter shifts every ticked kind in every target, one undo step.", strip, 60)}
{card("Line editor text menu: Phantom", "Acts on the active line, using the selected text (or {} markers).", lefield + le + ph, 270)}
</div><div class="col">
{card("Change alignment, keep position", "The submenu: the sign stays where it is.", changeal, 270)}
{card("Copy columns (Paste columns gets the same choices)", "New: the Tags group.", cols, 330)}
{card("Cycle values", "The lists are saved in settings. Defaults are ua.Cycles'.", cyc, 330)}
</div></body></html>'''
out.write_text(html)
