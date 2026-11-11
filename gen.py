import pathlib, sys
icons = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])

def ic(role):
    if not role:
        return ""
    svg = (icons / f"{role}.svg").read_text().strip()
    return f'<span class="ic">{svg}</span>'

def item(role, text, key="", sub=False, check=None, active=False, dim=False, new=False):
    cls = "it" + (" act" if active else "") + (" dim" if dim else "") + (" new" if new else "")
    mark = ""
    if check is not None:
        mark = f'<span class="cb{" on" if check else ""}"></span>'
    return (f'<div class="{cls}">{mark}{ic(role)}<span class="t">{text}</span>'
            f'<span class="k">{key}</span><span class="ar">{"›" if sub else ""}</span></div>')

SEP = '<div class="sep"></div>'

def menu(rows, style=""):
    return f'<div class="menu" style="{style}">{"".join(rows)}</div>'

text_group_le = [item("line-break-insert", "Line breaks", sub=True, new=True),
                 item("change-case", "Change case", sub=True, active=True, new=True),
                 item("dialogue-swap", "Swap dialogue alternatives", new=True)]
le_menu = menu([item("undo", "Undo", "Ctrl+Z"), item("redo", "Redo", "Ctrl+Y"), SEP,
                item("edit-cut", "Cut", "Ctrl+X"), item("edit-copy", "Copy", "Ctrl+C"),
                item("edit-paste", "Paste", "Ctrl+V"), item("delete", "Delete"), SEP,
                item("select-all", "Select all", "Ctrl+A"), SEP] + text_group_le +
               [SEP, item("spellchecker", "Check spelling", check=True)])
case_menu = menu([item("case-lower", "lowercase", new=True), item("case-upper", "UPPERCASE", new=True),
                  item("case-title", "Title Case", new=True), item("case-sentence", "Sentence case", new=True), SEP,
                  item("fullwidth", "Full-width", new=True), item("halfwidth", "Half-width", new=True)],
                 "position:absolute;left:274px;top:258px")

grid_menu = menu([item("insert-after", "Insert", sub=True), item("duplicate", "Duplicate 3 lines"), SEP,
                  item("edit-cut", "Cut", "Ctrl+X"), item("edit-copy", "Copy", "Ctrl+C"),
                  item("edit-paste", "Paste", "Ctrl+V"), item("columns-paste", "Paste special", sub=True), SEP,
                  item("join-lines", "Join", sub=True), item("split-lines", "Split", sub=True),
                  item("swap-lines", "Swap lines"), item("make-tree", "Make line group"), SEP,
                  item("line-break-insert", "Line breaks", sub=True, active=True, new=True),
                  item("change-case", "Change case", sub=True, new=True),
                  item("dialogue-swap", "Swap dialogue alternatives", new=True), SEP,
                  item("clear", "Delete text"), item("delete", "Delete 3 lines")])
breaks_menu = menu([item("line-break-insert", "Insert balanced break", new=True),
                    item("line-break-left", "Move break left", new=True),
                    item("line-break-right", "Move break right", new=True),
                    item("line-break-remove", "Remove breaks", new=True), SEP,
                    item(None, "Wrap only at \\N (\\q2)", check=False, new=True)],
                   "position:absolute;left:274px;top:330px")

def card(title, caption, body):
    return f'<div class="card"><div class="cap"><b>{title}</b><br>{caption}</div><div class="stage">{body}</div></div>'

html = f'''<!doctype html><html><head><meta charset="utf-8"><title>AU20 text commands</title><style>
:root{{--bg:#E5E9EC;--panel:#F9FAFB;--raised:#EDF0F3;--field:#FFFFFF;--text:#202832;--muted:#526170;--line:#AAB5BE;--accent:#1D5BA8;--onAccent:#FFFFFF;--select:#D6DFEB}}
body.dark{{--bg:#171B20;--panel:#20262D;--raised:#29313A;--field:#171D24;--text:#E8EDF2;--muted:#A5B1BD;--line:#414B57;--accent:#A6C8F2;--onAccent:#101D2C;--select:#303D4C}}
body.highContrastWhite{{--bg:#FFFFFF;--panel:#FFFFFF;--raised:#EBEBEB;--field:#FFFFFF;--text:#000000;--muted:#1A1A1A;--line:#000000;--accent:#0037B3;--onAccent:#FFFFFF;--select:#C9DAF8}}
body.highContrastBlack{{--bg:#000000;--panel:#080808;--raised:#151515;--field:#000000;--text:#FFFFFF;--muted:#EEEEEE;--line:#FFFFFF;--accent:#FFFF00;--onAccent:#000000;--select:#253F60}}
*{{box-sizing:border-box}}
body{{margin:0;padding:20px;background:var(--bg);color:var(--text);font:13px/1.4 "Inter","Noto Sans","Noto Sans CJK JP",sans-serif;display:flex;gap:28px;width:1160px}}
.card{{width:550px}}
.cap{{color:var(--muted);margin-bottom:10px;min-height:38px}}
.cap b{{color:var(--text)}}
.stage{{position:relative;height:500px}}
.field{{background:var(--field);border:1px solid var(--line);border-radius:4px;padding:6px 8px;font-family:"JetBrains Mono","DejaVu Sans Mono",monospace;font-size:12px;width:520px}}
.menu{{position:absolute;left:0;top:44px;width:280px;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,.18)}}
.it{{display:flex;align-items:center;gap:8px;padding:3px 10px;height:26px;white-space:nowrap}}
.it.act{{background:var(--select)}}
.it .ic{{width:16px;height:16px;flex:none;display:inline-flex}}
.it .ic svg{{width:16px;height:16px}}
.it .ic svg #accent{{stroke:var(--accent)}}
.it .t{{flex:1}}
.it .k{{color:var(--muted);font-size:12px}}
.it .ar{{width:8px;color:var(--muted)}}
.it.new .t{{text-decoration:underline dotted var(--accent);text-underline-offset:3px}}
.sep{{height:1px;background:var(--line);margin:4px 8px;opacity:.6}}
.cb{{width:14px;height:14px;border:1.5px solid var(--line);border-radius:3px;background:var(--field);position:relative;flex:none;margin-left:-2px}}
.cb.on{{background:var(--accent);border-color:var(--accent)}}
.cb.on::after{{content:"";position:absolute;left:3px;top:0;width:4px;height:8px;border:solid var(--onAccent);border-width:0 2px 2px 0;transform:rotate(45deg)}}
.legend{{position:absolute;bottom:0;left:0;color:var(--muted);font-size:12px}}
</style></head><body>
<script>document.body.className=(location.hash||"#light").slice(1)</script>
{card("Line editor: right-click in the text", "The three entries sit above Check spelling. They act on the selected Lines, like the Grid's.",
      '<div class="field">I can\'t believe you came all the way here just to see me.</div>' + le_menu + case_menu +
      '<div class="legend">Dotted underline: new in AU20. No default keys.</div>')}
{card("Grid: row menu", "The same three entries, in their own group after Swap lines and Make line group.",
      '<div class="field">Dialogue: 0,0:01:02.10,0:01:05.40,Default-Main,,0,0,0,,…</div>' + grid_menu + breaks_menu)}
</body></html>'''
out.write_text(html)
