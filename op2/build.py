"""Builds index.html: the Keyframe generator design gate in HikariSub's four themes. python3 build.py; ?only=<section> shows one."""
import html
import json
import pathlib

here = pathlib.Path(__file__).resolve().parent
icons = {p.stem: p.read_text().strip() for p in sorted((here / "icons").glob("*.svg"))}

THEMES = {
    "light": dict(name="Light", bg="#E5E9EC", panel="#F9FAFB", raised="#EDF0F3", field="#FFFFFF", text="#202832",
                  muted="#526170", line="#AAB5BE", accent="#145C4C", onAccent="#FFFFFF", select="#D6EBE4",
                  danger="#A51F31", warning="#8A5A00", success="#008000", disabled="#7A8691"),
    "dark": dict(name="Dark", bg="#171B20", panel="#20262D", raised="#29313A", field="#171D24", text="#E8EDF2",
                 muted="#A5B1BD", line="#414B57", accent="#9CDBC9", onAccent="#102C24", select="#304C47",
                 danger="#FFADAD", warning="#E0A030", success="#4DBA4D", disabled="#7D8996"),
    "hcw": dict(name="High contrast white", bg="#FFFFFF", panel="#FFFFFF", raised="#EBEBEB", field="#FFFFFF",
                text="#000000", muted="#1A1A1A", line="#000000", accent="#0037B3", onAccent="#FFFFFF", select="#C9DAF8",
                danger="#A00000", warning="#6B4500", success="#005A00", disabled="#5C5C5C"),
    "hcb": dict(name="High contrast black", bg="#000000", panel="#080808", raised="#151515", field="#000000",
                text="#FFFFFF", muted="#EEEEEE", line="#FFFFFF", accent="#FFFF00", onAccent="#000000", select="#253F60",
                danger="#FFADAD", warning="#FFD54A", success="#7CFC7C", disabled="#A8A8A8"),
}

SET = {}
root = pathlib.Path("/home/altq/.t3/worktrees/hikari/w8-op2-b59df4b82eaa7614/src/ui/icons")
for role in ["plugin-official", "panel-menu", "notice-info", "notice-warning", "save", "open-keyframes", "file-keyframes",
             "cancel", "plugins"]:
    p = root / f"{role}.svg"
    if p.exists():
        SET[role] = p.read_text().strip()


def icon(role, accent=True):
    svg = SET.get(role, '<svg viewBox="0 0 16 16"></svg>')
    return f'<span class="ico{" acc" if accent else ""}">{svg}</span>'


def esc(s):
    return html.escape(s, quote=True)


def panel(body, width=340):
    return (f'<div class="panel" style="width:{width}px"><div class="ph"><span>Keyframe generator</span>'
            f'<span class="tb">{icon("panel-menu")}</span></div><div class="pb">{body}</div></div>')


def button(text, style="normal", off=False):
    return f'<span class="btn {style}{" off" if off else ""}">{esc(text)}</span>'


def slider(label, value, off=False):
    o = " off" if off else ""
    return (f'<label class="lf{o}"><span class="ll">{esc(label)}</span><span class="sl"><span class="tr">'
            f'<span class="fill" style="width:{value}%"></span><span class="kn" style="left:{value}%"></span></span>'
            f'<span class="sv">{value}</span></span><span class="ends"><span>Fewer cuts</span><span>More cuts</span></span></label>')


def number(label, value, unit, off=False):
    o = " off" if off else ""
    return (f'<label class="lf{o}"><span class="ll">{esc(label)}</span><span class="fld num">{esc(value)}'
            f'<span class="u">{esc(unit)}<span class="spin">⌃⌄</span></span></span></label>')


def label(text, style="body"):
    return f'<div class="lbl {style}">{esc(text)}</div>'


def grp(title, body):
    return f'<div class="grp"><div class="gt">{esc(title)}</div>{body}</div>'


def controls(off=False):
    return slider("Sensitivity", 50, off) + number("Minimum gap", "10", "frames", off)


def jobcard(frac, text):
    return (f'<div class="job"><div class="jh"><span class="spinner"></span><b>Keyframe generator: Finding keyframes</b>'
            f'<span class="pc">{round(frac * 100)} %</span></div><div class="jt">{esc(text)}</div>'
            f'<div class="bar"><span style="width:{frac * 100}%"></span></div>'
            f'<div class="jb">{button("Cancel")}</div></div>')


summary = grp("Found 412 keyframes",
              label("In 34,046 frames: a shot every 3.4 s on average.", "muted") +
              label("Sensitivity 50 · minimum gap 10 frames", "small") +
              '<div class="rowb">' + button("Set keyframes", "primary") + button("Save as keyframes file…") + "</div>")

ready = panel(controls() + '<div class="rowb">' + button("Find keyframes", "primary") + "</div>" +
              label("Reads every frame of the open video. Keyframes you have now stay until you set the new ones.", "small"))

scanning = ('<div class="stack">' +
            panel(controls(True) + '<div class="rowb">' + button("Find keyframes", "primary", True) + "</div>" +
                  label("Finding keyframes… Cancel it in the card above the status bar.", "muted")) +
            jobcard(0.31, "Frame 10,496 of 34,046") + "</div>")

result = panel(controls() + '<div class="rowb">' + button("Find keyframes again") + "</div>" + summary)

done = panel(controls() + '<div class="rowb">' + button("Find keyframes again") + "</div>" +
             grp("Found 412 keyframes",
                 '<div class="ok">' + icon("notice-info", False) + "<span>The video’s keyframes now. Saved as ep05_keyframes.txt.</span></div>" +
                 '<div class="rowb">' + button("Set keyframes", "primary", True) + button("Save as keyframes file…") + "</div>"))

cancelled = panel(controls() + '<div class="rowb">' + button("Find keyframes", "primary") + "</div>" +
                  '<div class="note">' + icon("notice-info", False) +
                  "<span>Cancelled at frame 8,192 of 34,046. Nothing was changed.</span></div>")

novideo = panel('<div class="empty">Open a video to find its keyframes.</div>' + controls(True) +
                '<div class="rowb">' + button("Find keyframes", "primary", True) + "</div>")


def icon_sheet():
    names = {"a-cut": "A · Film frame with a keyframe (recommended)", "b-score": "B · Scene score with a keyframe on its peak",
             "c-track": "C · Frames over keyframes on a line"}
    out = []
    for stem, svg in icons.items():
        sizes = "".join(f'<span class="isz" style="width:{s}px;height:{s}px">{svg}</span>' for s in (16, 24, 32, 48, 128))
        out.append(f'<div class="irow"><div class="iname">{esc(names.get(stem, stem))}</div>{sizes}</div>')
    mark = f'<span class="ico acc mark">{SET["plugin-official"]}</span>'
    installed = (f'<div class="list pm" style="width:330px"><div class="row sel accbar"><span class="isz" style="width:24px;height:24px">'
                 f'{icons["a-cut"]}</span><div class="rt"><div class="nm"><span>Keyframe generator 0.1.0</span>{mark}</div>'
                 f'<div class="rd">From the marketplace</div></div></div></div>')
    listed = (f'<div class="list pm" style="width:330px"><div class="row"><span class="isz" style="width:32px;height:32px">'
              f'{icons["a-cut"]}</span><div class="rt"><div class="nm"><span>Keyframe generator</span>{mark}</div>'
              f'<div class="rd">Finds the video’s scene changes and makes keyframes from them.</div></div></div></div>')
    return ("".join(out) + label("Installed, in the plugin manager (variant A):", "muted") + installed +
            label("In the marketplace list:", "muted") + listed)


SECTIONS = [
    ("ready", "1. The panel, a video open", ready),
    ("scanning", "2. Scanning: HikariSub's job card has the progress and Cancel", scanning),
    ("result", "3. The result, before it changes anything", result),
    ("done", "4. After Save as keyframes file… (Set keyframes alone says “The video’s keyframes now.”)", done),
    ("cancelled", "5. Cancelled", cancelled),
    ("novideo", "6. No video open", novideo),
    ("icon", "7. The plugin’s icon", icon_sheet()),
]

CSS = (here / "css.txt").read_text().split('"""')[1] + """
.stack{display:flex;flex-direction:column;gap:10px}
.btn.off,.lf.off{color:var(--disabled);border-color:var(--disabled)}
.btn.primary.off{background:var(--raised);color:var(--disabled);border-color:var(--disabled)}
.lf.off .ll,.lf.off .sv,.lf.off .ends{color:var(--disabled)}
.lf.off .fill,.lf.off .kn{background:var(--disabled)}
.lf.off .fld{color:var(--disabled);border-color:var(--disabled)}
.sl{display:flex;align-items:center;gap:8px}
.tr{position:relative;flex:1;height:4px;border-radius:2px;background:var(--line)}
.fill{position:absolute;left:0;top:0;bottom:0;border-radius:2px;background:var(--accent)}
.kn{position:absolute;top:-5px;width:12px;height:12px;margin-left:-6px;border-radius:6px;background:var(--accent);border:1px solid var(--panel)}
.sv{width:24px;text-align:right}
.ends{display:flex;justify-content:space-between;font-size:11.5px;color:var(--muted)}
.fld.num{width:140px}
.u{color:var(--muted);display:flex;gap:6px}
.note,.ok{display:flex;gap:8px;align-items:flex-start}
.empty{color:var(--muted);text-align:center;padding:10px 0}
.job{width:340px;background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:8px 10px;display:flex;flex-direction:column;gap:6px}
.jh{display:flex;gap:8px;align-items:center}
.jh b{flex:1}
.spinner{width:11px;height:11px;border:1.5px solid var(--text);border-radius:50%}
.jt{padding-left:19px}
.bar{margin-left:19px;height:4px;background:var(--line);border-radius:2px;border:0;padding:0}
.bar span{display:block;height:4px;background:var(--accent);border-radius:2px}
.jb{display:flex;justify-content:flex-end}
"""


def page():
    out = [f"<!doctype html><meta charset=utf-8><title>OP2 Keyframe generator: design gate</title><style>{CSS}</style>",
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
