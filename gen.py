import pathlib, sys
icons = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])


def ic(role, size=16):
    if not role:
        return '<span class="ic"></span>'
    svg = (icons / f"{role}.svg").read_text().strip()
    return f'<span class="ic">{svg}</span>'


def item(role, text, key="", sub=False, active=False, new=False, dim=False):
    cls = "it" + (" act" if active else "") + (" new" if new else "") + (" dim" if dim else "")
    return (f'<div class="{cls}">{ic(role)}<span class="t">{text}</span>'
            f'<span class="k">{key}</span><span class="ar">{"›" if sub else ""}</span></div>')


SEP = '<div class="sep"></div>'


def menu(rows, style=""):
    return f'<div class="menu" style="{style}">{"".join(rows)}</div>'


subs_menu = menu([item("shift-times", "Shift times…", "Ctrl+I"),
                  item("timing-postprocessor", "Timing postprocessor…"),
                  item("timing-assistant", "Timing helpers", sub=True, active=True, new=True),
                  item("qc-notes", "QC notes…"), SEP,
                  item("styles", "Style manager"), item("script-properties", "Script properties"),
                  item(None, "…", dim=True)])
helpers_menu = menu([
    item("snap-start-to-previous-keyframe", "Start to previous keyframe", new=True),
    item("snap-start-to-next-keyframe", "Start to next keyframe", new=True),
    item("snap-end-to-previous-keyframe", "End to previous keyframe", new=True),
    item("snap-end-to-next-keyframe", "End to next keyframe", new=True), SEP,
    item("lead-in", "Add lead-in", new=True), item("lead-in-remove", "Remove lead-in", new=True),
    item("lead-out", "Add lead-out", new=True), item("lead-out-remove", "Remove lead-out", new=True), SEP,
    item("time-offset-start", "Start one frame earlier", new=True),
    item("time-offset-start", "Start one frame later", new=True),
    item("time-offset-end", "End one frame earlier", new=True),
    item("time-offset-end", "End one frame later", new=True), SEP,
    item("continuous-previous", "Extend previous line to this start", new=True),
    item("continuous-next", "Extend next line to this end", new=True), SEP,
    item("timing-assistant", "Timing assistant", new=True),
    item("commit", "Accept assistant suggestion", new=True)],
    "position:absolute;left:236px;top:56px;width:300px")


def btn(role, text, key, primary=False, ring=False):
    cls = "btn" + (" primary" if primary else "") + (" ring" if ring else "")
    return f'<span class="{cls}">{ic(role)}<span>{text}</span><span class="kb">{key}</span></span>'


def strip(sugg=True):
    ticks = "".join(f'<span class="kf" style="left:{x}%"></span>' for x in (8, 31, 77))
    now = '<span class="span now" style="left:34%;width:40%"></span>'
    new = '<span class="span sug" style="left:31%;width:49%"></span>' if sugg else ""
    prev = '<span class="span other" style="left:0;width:27%"></span>'
    prevs = '<span class="span othersug" style="left:27%;width:4%"></span>' if sugg else ""
    return f'<div class="strip">{prev}{prevs}{now}{new}{ticks}<span class="lab" style="left:31%">kf</span><span class="lab" style="left:77%">kf</span></div>'


def rows(change=False):
    def tv(v, edit=False, focus=False):
        if edit:
            return f'<span class="input mono{" focus" if focus else ""}">{v}{"<span class=caret></span>" if focus else ""}</span>'
        return f'<span class="mono">{v}</span>'
    r = [('Start', '0:01:02.14', '0:01:02.02', 'Snapped to the keyframe 3 frames earlier.'),
         ('End', '0:01:05.40', '0:01:05.80', 'Lead-out of 400 ms. No keyframe within 300 ms before or 900 ms after.'),
         ('Line 11 end', '0:01:01.60', '0:01:02.02', 'Linked: it ended 540 ms before this start.')]
    out = ['<div class="grid"><span class="h"></span><span class="h">Now</span><span class="h"></span><span class="h">Suggested</span><span class="h">Why</span>']
    for i, (k, a, b, why) in enumerate(r):
        out.append(f'<span class="k2">{k}</span>{tv(a)}<span class="arr">→</span>'
                   f'{tv(b, change and i < 2, change and i == 0)}<span class="why">{why}</span>')
    out.append('</div>')
    return "".join(out)


def assistant(change=False, narrow=False):
    head = (f'<div class="ahead">{ic("timing-assistant")}<b>Line 12 of 48</b>'
            f'<span class="muted">Default · Yuki · 23.976 fps</span></div>'
            '<div class="text">I can\'t believe you came all the way here just to see me.</div>')
    actions = ('<div class="actions">' + btn("commit", "Accept", "Enter", primary=True, ring=not change) +
               btn("edit", "Change" if not change else "Done", "Alt+C", ring=change) +
               btn("chevron-right", "Skip", "Alt+S") + '</div>')
    foot = '<div class="foot">Accepted 11, skipped 0. Each accepted line is one undo step.</div>'
    return f'<div class="assist{" narrow" if narrow else ""}">{head}{strip()}{rows(change)}{actions}{foot}</div>'


def rule(title, on, body):
    return (f'<div class="rule"><span class="cb{" on" if on else ""}"></span><b>{title}</b></div>'
            f'<div class="rbody">{body}</div>')


def ms(v):
    return f'<span class="input mono small">{v} ms</span>'


form = ('<div class="preset"><span class="input">Default</span><span class="btn">Save</span></div>'
        '<div class="lines"><span class="muted">Lines</span><span class="input">Selected lines · All styles</span></div>' +
        rule("Lead-in", True, f'Start earlier by {ms(120)}') +
        rule("Lead-out", True, f'End later by {ms(400)}') +
        rule("Close small gaps", True, f'Start back to the previous end if within {ms(620)}<br>End on to the next start if within {ms(0)}') +
        rule("Snap to keyframes", True, f'<span class="kv">before / after</span><br>Start within {ms(350)} {ms(100)}<br>End within {ms(300)} {ms(900)}'))


def tabs(narrow=False):
    return ('<div class="modes"><span class="seg">Preview</span><span class="seg on">'
            + ic("timing-assistant") + 'Assistant</span></div>')


wide = (f'<div class="dock" style="width:860px"><div class="tabs"><span class="tab">Timing postprocessor</span><span class="sp"></span></div>'
        f'<div class="wide"><div class="form">{form}</div><div class="vr"></div><div class="pane">{tabs()}{assistant()}</div></div></div>')
narrow = (f'<div class="dock" style="width:330px"><div class="tabs"><span class="tab">Timing postprocessor</span><span class="sp"></span></div>'
          f'<div class="pane">{tabs(True)}{assistant(change=True, narrow=True)}</div>'
          '<div class="collapsed">› Rules: Lead-in 120 ms, Lead-out 400 ms, Gaps 620 ms, Keyframes 350/100, 300/900</div></div>')


def card(title, caption, body, w, h):
    return f'<div class="card" style="width:{w}px"><div class="cap"><b>{title}</b><br>{caption}</div><div class="stage" style="height:{h}px">{body}</div></div>'


html = f'''<!doctype html><html><head><meta charset="utf-8"><title>AU19 timing helpers</title><style>
:root{{--bg:#E5E9EC;--panel:#F9FAFB;--raised:#EDF0F3;--field:#FFFFFF;--text:#202832;--muted:#526170;--line:#AAB5BE;--accent:#1D5BA8;--onAccent:#FFFFFF;--select:#D6DFEB;--sug:#2E7D4F;--kf:#C0392B}}
body.dark{{--bg:#171B20;--panel:#20262D;--raised:#29313A;--field:#171D24;--text:#E8EDF2;--muted:#A5B1BD;--line:#414B57;--accent:#A6C8F2;--onAccent:#101D2C;--select:#303D4C;--sug:#8FD6A8;--kf:#FF8A80}}
body.highContrastWhite{{--bg:#FFFFFF;--panel:#FFFFFF;--raised:#EBEBEB;--field:#FFFFFF;--text:#000000;--muted:#1A1A1A;--line:#000000;--accent:#0037B3;--onAccent:#FFFFFF;--select:#C9DAF8;--sug:#005A1E;--kf:#A00000}}
body.highContrastBlack{{--bg:#000000;--panel:#080808;--raised:#151515;--field:#000000;--text:#FFFFFF;--muted:#EEEEEE;--line:#FFFFFF;--accent:#FFFF00;--onAccent:#000000;--select:#253F60;--sug:#00FF66;--kf:#FF6666}}
*{{box-sizing:border-box}}
body{{margin:0;padding:20px;background:var(--bg);color:var(--text);font:13px/1.4 "Inter","Noto Sans",sans-serif;display:flex;flex-wrap:wrap;gap:24px;width:1300px}}
.cap{{color:var(--muted);margin-bottom:10px;min-height:38px}} .cap b{{color:var(--text)}}
.stage{{position:relative}}
.muted{{color:var(--muted)}} .mono{{font-family:"JetBrains Mono","DejaVu Sans Mono",monospace;font-size:12px}}
.menu{{position:absolute;left:0;top:0;width:240px;background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:4px 0;box-shadow:0 4px 14px rgba(0,0,0,.18)}}
.it{{display:flex;align-items:center;gap:8px;padding:3px 10px;height:26px;white-space:nowrap}}
.it.act{{background:var(--select)}} .it.dim{{color:var(--muted)}}
.ic{{width:16px;height:16px;flex:none;display:inline-flex}} .ic svg{{width:16px;height:16px}} .ic svg #accent{{stroke:var(--accent)}}
.it .t{{flex:1}} .it .k{{color:var(--muted);font-size:12px}} .it .ar{{width:8px;color:var(--muted)}}
.it.new .t{{text-decoration:underline dotted var(--accent);text-underline-offset:3px}}
.sep{{height:1px;background:var(--line);margin:4px 8px;opacity:.6}}
.dock{{background:var(--panel);border:1px solid var(--line)}}
.tabs{{display:flex;height:28px;background:var(--raised);border-bottom:1px solid var(--line)}}
.tab{{padding:0 12px;display:flex;align-items:center;font-weight:600;background:var(--panel);border-right:1px solid var(--line)}}
.wide{{display:flex}} .form{{width:300px;padding:8px;display:flex;flex-direction:column;gap:8px}} .vr{{width:1px;background:var(--line)}}
.pane{{flex:1;padding:8px;min-width:0}}
.input{{display:inline-flex;align-items:center;height:24px;background:var(--field);border:1px solid var(--line);border-radius:2px;padding:0 6px}}
.input.small{{width:70px;justify-content:flex-end}} .input.focus{{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}}
.caret{{display:inline-block;width:1px;height:14px;background:var(--text);margin-left:1px}}
.preset{{display:flex;gap:6px}} .preset .input{{flex:1}} .lines{{display:flex;gap:8px;align-items:center}} .lines .input{{flex:1}}
.rule{{display:flex;gap:8px;align-items:center}} .rbody{{margin-left:22px;color:var(--text);line-height:1.9}} .kv{{color:var(--muted);margin-left:74px}}
.cb{{width:14px;height:14px;border:1.5px solid var(--line);border-radius:3px;background:var(--field);position:relative;flex:none}}
.cb.on{{background:var(--accent);border-color:var(--accent)}}
.cb.on::after{{content:"";position:absolute;left:3px;top:0;width:4px;height:8px;border:solid var(--onAccent);border-width:0 2px 2px 0;transform:rotate(45deg)}}
.btn{{height:26px;padding:0 10px;display:inline-flex;align-items:center;gap:6px;border:1px solid var(--line);border-radius:2px;background:var(--raised);white-space:nowrap}}
.btn.primary{{background:var(--accent);color:var(--onAccent);border-color:var(--accent)}} .btn.primary .ic svg #accent{{stroke:var(--onAccent)}}
.btn .kb{{font-size:11px;opacity:.75}} .ring{{outline:1px solid var(--text);outline-offset:1px}}
.modes{{display:flex;margin-bottom:8px}} .seg{{display:inline-flex;gap:6px;align-items:center;height:24px;padding:0 10px;border:1px solid var(--line);background:var(--raised)}}
.seg.on{{background:var(--select);border-color:var(--accent);font-weight:600}}
.ahead{{display:flex;gap:8px;align-items:center}} .text{{margin:4px 0 8px;padding:6px 8px;background:var(--field);border:1px solid var(--line);border-radius:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}}
.strip{{position:relative;height:34px;background:var(--raised);border:1px solid var(--line);margin-bottom:8px}}
.span{{position:absolute;height:8px}} .span.now{{top:6px;border:1.5px solid var(--muted)}} .span.sug{{top:18px;background:var(--sug)}}
.span.other{{top:6px;border:1px dashed var(--muted)}} .span.othersug{{top:18px;background:var(--sug);opacity:.55}}
.kf{{position:absolute;top:0;bottom:0;width:2px;background:var(--kf)}} .lab{{position:absolute;bottom:-1px;font-size:10px;color:var(--kf);margin-left:4px}}
.grid{{display:grid;grid-template-columns:auto auto auto auto 1fr;gap:6px 10px;align-items:center}}
.grid .h{{color:var(--muted);font-size:12px}} .k2{{color:var(--muted)}} .arr{{color:var(--muted)}} .why{{color:var(--text)}}
.narrow .grid{{grid-template-columns:auto auto auto auto}} .narrow .why{{grid-column:1 / -1;color:var(--muted);font-size:12px;margin:-4px 0 2px}}
.narrow .grid .h:nth-child(5){{display:none}}
.actions{{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}} .foot{{margin-top:8px;color:var(--muted);font-size:12px}}
.collapsed{{border-top:1px solid var(--line);padding:6px 8px;color:var(--muted);font-size:12px}}
.legend{{color:var(--muted);font-size:12px;width:100%}}
</style></head><body>
<script>document.body.className=(location.hash||"#light").slice(1)</script>
{card("Subtitles › Timing helpers", "Every new command, in one submenu next to the Timing postprocessor. Each acts on the selected lines and is one undo step.", subs_menu + helpers_menu, 560, 530)}
{card("Timing postprocessor, wide: Assistant", "The rules on the left feed the suggestion. One line at a time; the strip shows keyframes, the line now (outline) and the suggestion (filled), with the previous line's link.", wide, 860, 520)}
{card("Timing postprocessor, narrow: Change", "Alt+C makes the suggested times editable; Enter accepts what you typed. Rules fold into one summary row.", narrow, 340, 520)}
<div class="legend">Dotted underline: new in AU19. No default keys. The two Extend rows borrow continuous-previous/next until IC draws the time-only join role.</div>
</body></html>'''
out.write_text(html)
