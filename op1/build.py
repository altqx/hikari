"""Builds index.html: the Macro chains design gate in HikariSub's four themes. python3 build.py; ?only=<section> shows one."""
import html
import json
import pathlib

here = pathlib.Path(__file__).resolve().parent
icons = {p.stem: p.read_text().strip() for p in sorted((here / "icons").glob("*.svg"))}

THEMES = {
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", accent="#145C4C", onAccent="#FFFFFF", select="#D6EBE4",
                  danger="#A51F31", warning="#8A5A00", success="#008000"),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", accent="#9CDBC9", onAccent="#102C24", select="#304C47",
                 danger="#FFADAD", warning="#E0A030", success="#4DBA4D"),
    "hcw": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                text="#000000", muted="#1A1A1A", line="#000000", accent="#0037B3", onAccent="#FFFFFF", select="#C9DAF8",
                danger="#A00000", warning="#6B4500", success="#005A00"),
    "hcb": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", accent="#FFFF00", onAccent="#000000", select="#253F60",
                danger="#FFADAD", warning="#FFD54A", success="#7CFC7C"),
}

# Set icons, inlined from src/ui/icons so they follow currentColor like IconTheme tints them.
SET = {}
root = here.parent.parent / "src" / "ui" / "icons"
for role in ["plugin-official", "media-record", "run-script", "import", "delete", "edit", "move-up", "move-down", "panel-menu", "duplicate",
             "notice-warning", "automation", "media-stop", "hotkeys", "add"]:
    p = root / f"{role}.svg"
    if p.exists():
        SET[role] = p.read_text().strip()


def icon(role, accent=True):
    svg = SET.get(role, '<svg viewBox="0 0 16 16"></svg>')
    return f'<span class="ico{" acc" if accent else ""}">{svg}</span>'


def esc(s):
    return html.escape(s, quote=True)


def panel(title, body, width=300):
    return (f'<div class="panel" style="width:{width}px"><div class="ph"><span>{esc(title)}</span>'
            f'<span class="tb">{icon("panel-menu")}</span></div><div class="pb">{body}</div></div>')


def strip(*roles):
    return '<div class="strip">' + "".join(f'<span class="tb" title="{esc(n)}">{icon(r)}</span>' for r, n in roles) + "</div>"


def rows(items, selected=None):
    out = []
    for i, (text, detail, extra) in enumerate(items):
        cls = "row sel" if i == selected else "row"
        if "muted" in extra:
            cls += " muted"
        lead = icon(extra["icon"]) if "icon" in extra else ""
        out.append(f'<div class="{cls}">{lead}<div class="rt"><div>{esc(text)}</div>'
                   f'<div class="rd">{esc(detail)}</div></div></div>')
    return '<div class="list">' + "".join(out) + "</div>"


def button(text, style="normal"):
    return f'<span class="btn {style}">{esc(text)}</span>'


def field(label, value, kind="text"):
    if kind == "dropdown":
        control = f'<span class="fld dd">{esc(value)}<span class="caret">▾</span></span>'
    elif kind == "number":
        control = f'<span class="fld num">{esc(value)}<span class="spin">⌃⌄</span></span>'
    else:
        control = f'<span class="fld">{esc(value)}</span>'
    return f'<label class="lf"><span class="ll">{esc(label)}</span>{control}</label>'


def check(text, on):
    return f'<label class="ck"><span class="box{" on" if on else ""}">{"✓" if on else ""}</span>{esc(text)}</label>'


def choice(options, value):
    return '<span class="choice">' + "".join(
        f'<span class="opt{" on" if o == value else ""}">{esc(o)}</span>' for o in options) + "</span>"


def group(title, body):
    return f'<div class="grp"><div class="gt">{esc(title)}</div>{body}</div>'


def label(text, style="body"):
    return f'<div class="lbl {style}">{esc(text)}</div>'


def dialog(title, body, buttons, width=440):
    btns = "".join(button(t, s) for t, s in buttons)
    return (f'<div class="dlg" style="width:{width}px"><div class="dt">{esc(title)}</div>'
            f'<div class="db">{body}</div><div class="dbtn">{btns}</div></div>')


MODES = ["Constant", "Ask", "Exclude"]

library = panel("Macro chains",
    '<div class="rowb sp">' + button("Record a chain") +
    strip(("run-script", "Run"), ("edit", "Edit"), ("import", "Import AegisubChain chains…"), ("delete", "Delete")) + "</div>" +
    rows([("Signs", "4 steps · asks 2 · slot 1", {}),
          ("Karaoke prep", "3 steps · slot 2", {}),
          ("Fade fix", "2 steps · imported, shows its dialogs on the first run", {}),
          ("Old export", "1 step · “Split” isn’t loaded", {"muted": 1})], selected=0) +
    '<div class="rowb">' + button("Run", "primary") + button("Edit") + "</div>")

recording = panel("Macro chains",
    label("Recording", "heading") +
    label("Run macros and commands as usual. Each one that applies becomes a step; Undo takes the last one back.", "muted") +
    field("Name", "Signs") +
    rows([("1. Fade in/out", "Typesetting · 1 dialog", {"icon": "automation"}),
          ("2. Duplicate lines", "Command", {"icon": "duplicate"}),
          ("3. Blur and glow", "Typesetting · 2 dialogs", {"icon": "automation"})]) +
    '<div class="rowb">' + button("Stop and save", "primary") + button("Discard", "danger") + "</div>")

editor = panel("Macro chains",
    field("Name", "Signs") +
    rows([("1. Fade in/out", "Previous selection", {"icon": "automation"}),
          ("2. Duplicate lines", "Macro’s selection", {"icon": "duplicate"}),
          ("3. Blur and glow", "Changed lines · asks 2", {"icon": "automation"})], selected=2) +
    strip(("move-up", "Move up"), ("move-down", "Move down"), ("delete", "Remove step")) +
    group("Step 3: Blur and glow",
          field("Selection after this step", "Changed lines", "dropdown") +
          check("Show this step’s dialogs, filled in", False) +
          group("Dialog 1 · presses “Apply”",
                '<div class="fg">' +
                "".join(f'<span class="fn">{esc(n)}</span>{choice(MODES, m)}<span class="fld sm{" off" if m == "Exclude" else ""}" title="{esc(v)}">{esc(v)}</span>'
                        for n, m, v in [("Blur", "Ask", "2"), ("Glow colour", "Constant", "&H00FFFF&"),
                                        ("Layers", "Exclude", "3"), ("Keep original", "Ask", "✓")]) +
                "</div>") +
          group("Dialog 2 · presses “Yes”",
                '<div class="fg"><span class="fn">Sure?</span>' + choice(MODES, "Constant") + '<span class="fld sm">✓</span></div>')) +
    field("Shortcut slot", "Slot 1 (Run chain slot 1)", "dropdown") +
    '<div class="rowb">' + button("Save", "primary") + button("Cancel") + "</div>", width=420)

replay = dialog("Signs",
    group("Step 1: Fade in/out", field("Fade in (ms)", "200", "number")) +
    group("Step 3: Blur and glow", field("Blur", "2", "number") + check("Keep original", True)),
    [("Run", "primary"), ("Cancel", "normal")], width=360)

importing = dialog("Import AegisubChain chains",
    label("arch.AegisubChain.json holds 3 chains.", "body") +
    rows([("Signs", "4 steps · asks 2", {}),
          ("Fade fix", "2 steps", {}),
          ("Old export", "1 step · no loaded macro “Gone/Away”: kept, it won’t run until the script is loaded",
           {"icon": "notice-warning"})]) +
    label("An imported chain shows each of its dialogs once, filled in, the first time it runs, so it learns them. "
          "After that it runs as recorded. A button set to “Set in dialog” becomes a constant.", "muted") +
    label("Inspired by AegisubChain by arch1t3cht.", "small"),
    [("Import 3 chains", "primary"), ("Cancel", "normal")], width=460)

failed = panel("Macro chains",
    rows([("Signs", "4 steps · asks 2 · slot 1", {})], selected=0) +
    '<div class="bar warn">' + icon("notice-warning", False) + '<div>“Signs” stopped at step 3, Blur and glow: the macro failed. '
    'Steps 1 and 2 are one undo step.</div></div>' +
    '<div class="rowb">' + button("Undo the chain") + button("Show the log") + "</div>")


def icon_sheet():
    names = {"a-linked": "A · Linked chain with a play mark (recommended)", "b-steps": "B · Steps on a path",
             "c-script": "C · Script page with a link"}
    out = []
    for stem, svg in icons.items():
        sizes = "".join(f'<span class="isz" style="width:{s}px;height:{s}px">{svg}</span>' for s in (16, 24, 32, 48, 128))
        out.append(f'<div class="irow"><div class="iname">{esc(names.get(stem, stem))}</div>{sizes}</div>')
    mark = f'<span class="ico acc mark">{SET["plugin-official"]}</span>'
    installed = (f'<div class="list pm" style="width:330px"><div class="row sel accbar"><span class="isz" style="width:24px;height:24px">'
                 f'{icons["a-linked"]}</span><div class="rt"><div class="nm"><span>Macro chains 1.0.0</span>{mark}'
                 f'<span class="tip">By HikariSub</span></div><div class="rd">From the marketplace</div></div></div></div>')
    listed = (f'<div class="list pm" style="width:330px"><div class="row"><span class="isz" style="width:32px;height:32px">'
              f'{icons["a-linked"]}</span><div class="rt"><div class="nm"><span>Macro chains</span>{mark}</div>'
              f'<div class="rd">Records a chain of commands and macros and replays it as one undo step.</div></div></div></div>')
    return ("".join(out) + label("Installed, in the plugin manager (variant A, pointer on the mark):", "muted") + installed +
            label("In the marketplace list:", "muted") + listed)


SECTIONS = [
    ("library", "1. The panel: your chains", library),
    ("recording", "2. Recording a chain", recording),
    ("editor", "3. Editing a chain: each field Constant, Ask or Exclude, each step’s selection", editor),
    ("replay", "4. The one merged dialog when the chain runs (none when nothing is Ask)", replay),
    ("import", "5. Importing AegisubChain’s chains", importing),
    ("failed", "6. A step that fails", failed),
    ("icon", "7. The plugin’s icon", icon_sheet()),
]

CSS = """
body{margin:0;font:13px/1.35 "Inter","Noto Sans",sans-serif;background:#f3f4f6;color:#111}
h2{font-size:15px;margin:18px 16px 8px}
.sec{padding:0 0 12px}
.cols{display:flex;gap:12px;padding:0 16px;align-items:flex-start}
.col{background:var(--bg);color:var(--text);padding:12px;border-radius:6px}
.col>.cap{font-size:11px;color:var(--muted);margin-bottom:8px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:4px}
.ph{display:flex;justify-content:space-between;align-items:center;padding:4px 6px 4px 8px;border-bottom:1px solid var(--line);font-weight:600}
.pb{padding:8px;display:flex;flex-direction:column;gap:8px}
.ico{display:inline-flex;width:16px;height:16px;color:var(--text);vertical-align:middle}
.ico svg{width:16px;height:16px}
.ico.acc #accent{color:var(--accent)}
.strip{display:flex;gap:2px}
.tb{display:inline-flex;padding:3px;border-radius:3px}

.list{border:1px solid var(--line);background:var(--field);border-radius:3px}
.row{display:flex;gap:8px;align-items:center;padding:5px 8px}
.row.sel{background:var(--select)}
.row.muted{color:var(--muted)}
.rd{font-size:12px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pm .row{padding:6px 8px;position:relative}
.pm .rt{min-width:0;flex:1}
.row.accbar{box-shadow:inset 3px 0 0 var(--accent)}
.nm{display:flex;gap:4px;align-items:center;position:relative}
.ico.mark,.ico.mark svg{width:14px;height:14px}
.tip{position:absolute;left:142px;top:-1px;background:var(--raised);color:var(--text);border:1px solid var(--line);padding:2px 6px;font-size:12px;border-radius:3px;white-space:nowrap}
.rowb{display:flex;gap:6px}
.rowb.sp{justify-content:space-between;align-items:center}
.btn{display:inline-block;padding:4px 12px;border:1px solid var(--line);border-radius:3px;background:var(--raised)}
.btn.primary{background:var(--accent);color:var(--onAccent);border-color:var(--accent)}
.btn.danger{color:var(--danger);border-color:var(--danger)}
.lf{display:flex;flex-direction:column;gap:3px}
.ll{font-size:12px;color:var(--muted)}
.fld{display:flex;justify-content:space-between;border:1px solid var(--line);background:var(--field);padding:3px 6px;border-radius:3px;min-height:16px}
.fld.sm{min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;display:block}
.choice{flex:none}
.fld.off{color:var(--muted);border-style:dashed}
.caret,.spin{color:var(--muted);font-size:10px}
.ck{display:flex;gap:6px;align-items:center}
.box{width:13px;height:13px;border:1px solid var(--line);background:var(--field);display:inline-flex;align-items:center;justify-content:center;font-size:10px;border-radius:2px}
.box.on{background:var(--accent);color:var(--onAccent);border-color:var(--accent)}
.choice{display:inline-flex;border:1px solid var(--line);border-radius:3px;overflow:hidden}
.opt{padding:2px 7px;font-size:12px;border-left:1px solid var(--line)}
.opt:first-child{border-left:0}
.opt.on{background:var(--accent);color:var(--onAccent)}
.grp{border:1px solid var(--line);border-radius:3px;padding:6px 8px 8px;display:flex;flex-direction:column;gap:6px}
.gt{font-weight:600;font-size:12.5px}
.fg{display:grid;grid-template-columns:auto max-content minmax(56px,1fr);gap:6px 8px;align-items:center}
.fn{font-size:12.5px}
.lbl.heading{font-weight:600;font-size:14px}
.lbl.muted{color:var(--muted)}
.lbl.small{font-size:11.5px;color:var(--muted)}
.dlg{background:var(--panel);border:1px solid var(--line);border-radius:6px;box-shadow:0 4px 18px rgba(0,0,0,.25)}
.dt{padding:8px 12px;font-weight:600;border-bottom:1px solid var(--line)}
.db{padding:12px;display:flex;flex-direction:column;gap:10px}
.dbtn{display:flex;justify-content:flex-end;gap:6px;padding:8px 12px;border-top:1px solid var(--line)}
.bar{display:flex;gap:8px;padding:6px 8px;border:1px solid var(--warning);border-radius:3px}
.bar .ico{color:var(--warning)}
.irow{display:flex;gap:14px;align-items:center;margin-bottom:10px}
.iname{width:200px;font-size:12px}
.isz{display:inline-flex}
.isz svg{width:100%;height:100%}
"""


def page():
    out = [f"<!doctype html><meta charset=utf-8><title>OP1 Macro chains: design gate</title><style>{CSS}</style>",
           '<div id=root>']
    for sid, title, body in SECTIONS:
        cols = "".join(
            f'<div class="col" style="' + ";".join(f"--{k}:{v}" for k, v in t.items() if k != "name") +
            f'"><div class="cap">{esc(t["name"])}</div>{body}</div>' for t in THEMES.values())
        out.append(f'<div class="sec" data-id="{sid}"><h2>{esc(title)}</h2><div class="cols">{cols}</div></div>')
    out.append("</div><script>const o=new URLSearchParams(location.search).get('only');"
               "if(o)document.querySelectorAll('.sec').forEach(s=>{if(s.dataset.id!==o)s.remove()});</script>")
    return "".join(out)


(here / "index.html").write_text(page())
(here / "sections.json").write_text(json.dumps([s[0] for s in SECTIONS]))
print("ok", len(SET), "set icons")
