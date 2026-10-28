
# ---------------------------------------------------------------------------------------------
# SV1 #365: "Preview subtitles at" in Settings > Video > Subtitles on video. Appended to base.py.

SV1_CSS = """
body{width:1880px}
.set{display:flex;width:900px;height:430px;background:%(panel)s;outline:1px solid %(line)s;position:relative}
.nav{width:220px;flex:none;background:%(bg)s;box-shadow:inset -1px 0 %(line)s;padding:4px 0}
.nv{display:flex;align-items:center;gap:12px;height:36px;padding:0 16px;color:%(text)s}
.nv.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s;font-weight:650}
.pg{flex:1;padding:14px 24px 0 24px;overflow:hidden}
.sec{padding:0 0 14px;box-shadow:inset 0 -1px %(line)s;margin:0 0 14px;width:612px}
.sec:last-child{box-shadow:none}
.sh{font-weight:650;margin:0 0 10px}
.sr{display:flex;align-items:center;height:32px}
.sl{width:252px;padding-left:24px;flex:none}
.sn{color:%(muted)s;font-size:12.5px;padding-left:24px;margin:-2px 0 6px;line-height:1.4;max-width:560px}
.new{outline:1px dashed %(accent)s;outline-offset:1px;margin-bottom:6px}
.list{position:absolute;background:%(panel)s;outline:1px solid %(line)s;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);
      padding:2px 0;z-index:3}
.li{display:flex;align-items:center;gap:8px;height:28px;padding:0 10px 0 8px;white-space:nowrap}
.li.cur{background:%(select)s}
.li .tick{width:14px;color:%(accent)s}
.tag{font-size:11px;color:%(muted)s;margin-left:10px}
"""

NAV = ["General", "Appearance", "Line editor", "Subtitle grid", "Video", "Audio", "Conversion",
       "Subtitle properties", "Automation", "Hotkeys", "Updates"]
CHOICES = ["Video resolution (as encoded)", "Screen size (sharp)"]
NOTE = ("Screen size draws subtitles at the size the picture takes on screen, so they stay sharp on large "
        "and high-DPI screens. The script’s layout is the same either way. Also used by the previews in "
        "the Style manager, the font picker and the Template editor.")


def sv1_settings(chosen, open_list=False):
    nav = "".join(f'<div class="nv{" cur" if n == "Video" else ""}">{n}</div>' for n in NAV)
    combo_w = 260
    lst = ""
    if open_list:
        rows = "".join(
            f'<div class="li{" cur" if c == chosen else ""}"><span class="tick">{"&#10003;" if c == chosen else ""}'
            f'</span>{c}</div>' for c in CHOICES)
        lst = f'<div class="list" style="left:0;top:31px;width:{combo_w}px">{rows}</div>'
    pg = (
        '<div class="sec"><div class="sh">Playback</div>'
        f'<div class="sr"><span class="sl">Seek on</span>{combo("Double-clicking a line (always on)", combo_w)}</div>'
        f'<div class="sr"><span class="sl">Then play</span>{combo("Nothing", combo_w)}</div></div>'
        '<div class="sec"><div class="sh">Subtitles on video</div>'
        f'<div class="sr new"><span class="sl">Preview subtitles at</span><span style="position:relative">{combo(chosen, combo_w)}{lst}</span></div>'
        f'<div class="sn">{NOTE}</div>'
        f'<div class="sr">{checkbox("Turn off the edits preview", False)}</div>'
        '<div class="sn">Applies when the tab opens again.</div></div>'
        '<div class="sec"><div class="sh">Decoding</div>'
        f'<div class="sr"><span class="sl" style="padding-left:24px">Seeking method</span>{combo("Unsafe (always fast)", combo_w)}</div></div>'
    )
    return f'<div class="set"><div class="nav">{nav}</div><div class="pg">{pg}</div></div>'


def sv1_main():
    for key, th in THEMES.items():
        content = (f'<h1>SV1 #365 — Settings › Video › Subtitles on video ({th["name"]})</h1>'
                   '<p class="note">New row outlined (dashed outline is the mockup’s marker only). '
                   'Default stays Video resolution; the choice applies at once, with no reopen.</p>'
                   '<div class="row">'
                   f'<div class="col">{sv1_settings(CHOICES[0])}<div class="cap">Default</div></div>'
                   f'<div class="col">{sv1_settings(CHOICES[1], open_list=True)}<div class="cap">The list open, '
                   'Screen size chosen</div></div></div>')
        html = page(key, content).replace("</style>", (SV1_CSS % dict(
            th, shadow=".55" if key in DARKISH else ".28")) + "</style>")
        (OUT / f"sv1-{th['code']}.html").write_text(html)


sv1_main()
