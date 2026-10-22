#!/usr/bin/env python3
"""PU4 #306 video, audio and timing popups mockup: per theme, the video windows (Open dummy video, Set new
FPS), Change aspect ratio as a popover and as a compact window (and both in the fullscreen video window),
and the questions and notices (Next file / Previous file, Shift times, the player and dummy video toasts),
with the two popups PK1 already moved (the audio track chooser, Shift times' Choose styles).

Drawn with PK1's mockup kit (kit.py: the theme roles of src/ui/theme.cpp, DG1's frame, the popover frame,
the message box and the toast, IC1's icons from the tree)."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu4"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, checkbox, field, combo, popover, window, ic, toast, message_box  # noqa: E402

kit.REPO_ICONS += ["dummy-video", "fps", "aspect-ratio", "shift-times", "media-play", "media-pause",
                   "media-stop", "media-next-file", "media-previous-file", "fullscreen-exit", "open-video",
                   "audio-output", "add"]


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=520):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def off(text):
    return f'<span class="b pri off">{text}</span>'


def label(text):
    return f'<span class="glabel">{text}</span>'


def error(text):
    return f'<div class="ferr">{ic("notice-error", 14, "err")}<span>{text}</span></div>'


def hint(text):
    return f'<div class="ghint">{text}</div>'


def bad(text, width=None, mono=False):
    return field(text, width, mono=mono).replace('class="fld', 'class="fld bad', 1)


def editable(text, width=None, focus=False, invalid=False):
    f = field(text, width, focus=focus, trail=ic("chevron-down", 14))
    return f.replace('class="fld', 'class="fld bad', 1) if invalid else f


def spin(text, width=96, focus=False):
    return field(text, width, focus=focus, mono=True,
                 trail='<span class="spin"><i></i><i class="dn"></i></span>')


# ---------------------------------------------------------------------------------------------
# 1. Open dummy video, Set new FPS


def dummy_video(state):
    custom = state == "invalid"
    res = combo("Custom" if custom else "1920 × 1080 (HD 1080p)", 286)
    size = (f'<div class="gline">{spin("1000" if custom else "1920", focus=custom)}<span class="times">×</span>'
            f'{spin("1080")}<span class="unit">pixels</span></div>')
    swatch = '<span class="swatch" title="Choose the colour" style="background:rgb(47,163,254)"></span>'
    colour = (f'<div class="gcol"><div class="gline">{swatch}{field("&amp;HFEA32F&amp;", 112, mono=True)}</div>'
              f'{checkbox("Checkerboard pattern", False)}</div>')
    if custom:
        fps = f'<div class="gcol">{editable("150", 112, focus=True, invalid=True)}{error("Must be from 15 to 120.")}</div>'
        frames = ""
    else:
        fps = f'<div class="gline">{editable("23.976", 112)}</div>'
        frames = hint("This gives 35964 frames")
    body = (f'<div class="grid">{label("Video resolution")}<div class="gcol">{res}{size}</div>'
            f'{label("Colour")}{colour}'
            f'{label("Frames per second")}{fps}'
            f'{label("Duration")}<div class="gcol">{field("0:25:00.00", 112, mono=True)}{frames}</div></div>')
    foot = (off("Open") if custom else btn("Open", primary=True, focus=True)) + btn("Cancel")
    return window("Open dummy video", body, foot, 452)


def set_new_fps(state):
    invalid = state == "invalid"
    new = editable(".", 112, focus=True, invalid=True) if invalid else editable("25", 112)
    note = (error("Type a frame rate, such as 25.") if invalid
            else hint("Every start and end time × 0.959"))
    body = (f'<div class="grid narrow">{label("Subtitles FPS")}<div class="gline">{editable("23.976", 112)}</div>'
            f'{label("New FPS")}<div class="gcol">{new}{note}</div></div>')
    foot = (off("Change FPS") if invalid else btn("Change FPS", primary=True, focus=True)) + btn("Cancel")
    return window("Set new FPS", body, foot, 336)


def video_page(key):
    th = THEMES[key]
    return (
        f'<h1>PU4 #306 · Video, audio and timing popups · {th["name"]}</h1>'
        '<p class="note">1 of 3: the two video windows. Both are windows on DG1\'s frame (fixed size, centred on '
        'the opener, Esc closes, Enter presses the default). Grid ▸ Set new FPS was a bare <i>Window</i>; '
        'Open dummy video was an in-window Controls Dialog.</p>'
        '<h2>Open dummy video (File ▸ Open dummy video)</h2>'
        + row(col(dummy_video("default"),
                  "As it opens: legacy's defaults (1920 × 1080, &amp;HFEA32F&amp;, 23.976 fps, 0:25:00.00). The "
                  "frame count now follows the frame rate and the duration as they are typed (legacy computed it "
                  "once, for the defaults). The swatch opens SY2's colour popover. Titled with the command; OK "
                  "becomes Open.", 452),
              col(dummy_video("invalid"),
                  "While typing: the width no longer matches a preset, so the preset list shows Custom. A frame "
                  "rate outside 15–120 is said under its field (the frame count waits for a valid one) and Open is off, in place of legacy's “Invalid FPS "
                  "value.” after OK (today a log line; an error toast with NT1).", 452))
        + '<h2>Set new FPS (Grid ▸ Set new FPS, a compact window)</h2>'
        + row(col(set_new_fps("default"),
                  "Legacy's FPSDialog: Subtitles FPS 23.976 and New FPS 25 (the rewrite opens both at 23.976 "
                  "today; legacy's 25 comes back), legacy's “Change FPS” button. The line under it says what "
                  "the change does: every time × old ÷ new.", 336),
              col(set_new_fps("invalid"),
                  "Text that is not a frame rate: said under the field, Change FPS off (legacy's “Incorrect "
                  "FPS” box after the press; the rewrite today keeps the window open and says nothing).", 336))
    )


# ---------------------------------------------------------------------------------------------
# 2. Change aspect ratio, and the fullscreen window


def slider(at=0.36):
    pct = f"{at * 100:.0f}%"
    return (f'<div class="sl"><div class="slt"></div><div class="slf" style="width:{pct}"></div>'
            f'<div class="slh" style="left:{pct}"></div></div>')


def slider_ends():
    return '<div class="slends"><span>Taller</span><span>Wider</span></div>'


def aspect_body(value="1.778", focus=True):
    return (f'<div class="arrow"><span>Aspect ratio: <b class="mono">{value}</b></span></div>'
            f'<div class="{"slring" if focus else ""}">{slider()}</div>{slider_ends()}')


def aspect_popover(style=""):
    body = ('<div class="ptitle">Change aspect ratio</div>' + aspect_body()
            + f'<div class="pfoot">{btn("Reset", small=True, lead=True)}</div>')
    return popover(body, 432, style)


def aspect_window():
    return window("Change aspect ratio", aspect_body(),
                  btn("Reset", lead=True) + btn("OK", primary=True) + btn("Cancel"), 480)


def picture(w, h, inner="", cls=""):
    return f'<div class="pic {cls}" style="width:{w}px;height:{h}px">{inner}</div>'


def video_panel(inner):
    trans = "".join(f'<span class="qb">{ic(n)}</span>' for n in
                    ["media-previous-file", "media-play", "media-stop", "media-next-file"])
    return (f'<div class="vpanel"><div class="vhead">VIDEO<span class="vmore">{ic("menu-more")}</span></div>'
            + picture(620, 349, inner)
            + f'<div class="vbar">{trans}<span class="vtime mono">0:03:12.45 · 4618</span></div></div>')


def fullscreen(inner, w=1280, h=600):
    trans = "".join(f'<span class="qb">{ic(n)}</span>' for n in
                    ["media-previous-file", "media-play", "media-stop", "media-next-file"])
    bar = (f'<div class="fsbar">{trans}<span class="fsslide"><i style="width:28%"></i></span>'
           f'<span class="vtime mono">0:03:12.45 · 4618</span><span class="qb">{ic("audio-output")}</span>'
           f'<span class="qb">{ic("fullscreen-exit")}</span></div>')
    return f'<div class="fs" style="width:{w}px;height:{h}px">{picture(w, h - 44, inner, "fsp")}{bar}</div>'


def pointer(x, y):
    return (f'<svg class="ptr" style="left:{x}px;top:{y}px" width="14" height="20" viewBox="0 0 14 20">'
            '<path d="M1 1v15l4-4 3 7 2-1-3-7h6z" fill="#fff" stroke="#000" stroke-width="1"/></svg>')


def aspect_page(key):
    th = THEMES[key]
    pop_in_panel = video_panel(pointer(150, 90) + aspect_popover("position:absolute;left:150px;top:96px"))
    fs_pop = fullscreen(pointer(380, 180) + aspect_popover("position:absolute;left:380px;top:186px")
                        + '<div class="fstack">'
                        + toast("warning", "Open video",
                                "The file is not a valid video file or is corrupted, or codecs or a splitter "
                                "may be missing", btn("Details", flat=True))
                        + '</div>')
    fs_box = fullscreen(
        '<div style="position:absolute;left:406px;top:170px">'
        + message_box("Next file", "Open the next video in this folder?", "Episode 04.mkv", "info",
                      btn("Open", primary=True, focus=True) + btn("Cancel"),
                      extra=f'<div class="mbcheck">{checkbox("Don’t ask again", False)}</div>')
        + '</div>')
    return (
        f'<h1>PU4 #306 · Change aspect ratio and the fullscreen window · {th["name"]}</h1>'
        '<p class="note">2 of 3. Change aspect ratio follows PK1\'s small-form rule: a popover where the Video '
        'context menu was opened, a compact window from its hotkey (VIDEO_ASPECT_RATIO). Both set the ratio '
        'live as the slider moves, as legacy did; Esc (and the window\'s Cancel) puts back the ratio it opened '
        'with, Enter, a press outside or OK keeps it.</p>'
        '<h2>From the Video context menu: a popover at the menu\'s place</h2>'
        + row(col(pop_in_panel,
                  "Opened where the context menu was (the pointer), kept inside the window. Titled, as a menu "
                  "opened it. The slider has the keyboard (arrows, Page Up/Down); Taller / Wider name its ends "
                  "(legacy's slider is inverted, a taller picture to the left). Reset gives the video's own "
                  "ratio back (legacy: only reopening the video did).", 620),
              col(aspect_window(),
                  "From the hotkey: the same form as a compact window on DG1's frame, centred on the window "
                  "that has the keyboard. OK keeps the ratio, Cancel and Esc put it back.", 480))
        + '<h2>In the fullscreen video window (FS1)</h2>'
        + col(fs_pop,
              "Everything here opens in the fullscreen window while it is shown, as legacy parented its dialogs "
              "to m_FullScreenWindow: the popover in its content (from its own context menu), the compact window "
              "and the message boxes transient for it, and the toasts at its bottom right above the pinned bar "
              "(PK1's ToastStack already moves there).", 1280)
        + col(fs_box, "The previous / next file buttons of the fullscreen bar ask in the fullscreen window: the "
                      "message box is centred on it and modal to it alone; the main window is not raised.", 1280)
    )


# ---------------------------------------------------------------------------------------------
# 3. Questions and notices


def questions_page(key):
    th = THEMES[key]
    nxt = message_box("Next file", "Open the next video in this folder?", "Episode 04.mkv", "info",
                      btn("Open", primary=True, focus=True) + btn("Cancel"),
                      extra=f'<div class="mbcheck">{checkbox("Don’t ask again", False)}</div>')
    shift = message_box("Shift times", "Shift only the start times?",
                        "End times stay where they are, so a line shifted past its end ends before it starts.",
                        "warning", btn("Shift start times", primary=True, focus=True) + btn("Cancel"),
                        extra=f'<div class="mbcheck">{checkbox("Don’t ask again", False)}</div>')
    toasts = ('<div class="col" style="width:360px;gap:8px">'
              + toast("warning", "Open video",
                      "The file is not a valid video file or is corrupted, or codecs or a splitter may be "
                      "missing", btn("Details", flat=True))
              + toast("error", "Open dummy video", "Invalid FPS value.", btn("Details", flat=True))
              + '</div>')
    styles = ('<div class="col" style="width:470px">'
              f'<div class="frow"><span class="flabel" style="width:auto">According to the selected styles</span>'
              '</div>'
              f'<div class="frow"><span class="qb qbo">{ic("add")}</span>'
              f'{field("Default, Sign, Song romaji", 300)}</div>'
              + '<div style="margin-left:30px">'
              + kit.choose_styles().split('<div style="margin-left:192px">', 1)[1][:-len("</div></div>")]
              + '</div>'
              + '</div>')
    return (
        f'<h1>PU4 #306 · Questions and notices · {th["name"]}</h1>'
        '<p class="note">3 of 3. The two Yes/No questions become PK1 message boxes: titled with the command, '
        'a severity icon, buttons named for what they do, and “Don’t ask again” (Settings ▸ Notices ▸ Ask '
        'again brings them back). The answers that need nothing back are toasts.</p>'
        '<h2>Message boxes</h2>'
        + row(col(nxt,
                  "Next file / Previous file (the Video panel's and the fullscreen bar's buttons, their hotkeys): "
                  "legacy's “Confirmation”, “Are you sure you want to index the next video?” with Yes / No. The "
                  "detail names the file it will open; Previous file says “previous”. Open is the default.", 468),
              col(shift,
                  "Shift times with “Start times” or “End times” only: legacy's “Confirmation”, “Do you really "
                  "want to shift only start times?” with Yes / No. Warning, as it can leave a line ending before "
                  "it starts (undo puts it back, so Shift stays the default). The end-times question says "
                  "“Shift only the end times?”, “Shift end times”.", 468))
        + '<h2>Toasts (NT1 #332)</h2>'
        + row(col(toasts,
                  "The player notice (W1, Windows only: DirectShow could not open the video) and a refused dummy "
                  "video are toasts in NT1's form, its titles and texts; NT1 already makes this change, so PU4 "
                  "only checks it (and does it on PK1's ToastStack itself if NT1 has not merged when PU4 builds). "
                  "With the inline check above, the dummy video toast is left for a refused text only.", 360))
        + '<h2>Already on the kit (PK1), unchanged</h2>'
        + row(col(kit.list_chooser_window(),
                  "The audio track chooser: PK1's list chooser in its compact window.", 400),
              col(styles,
                  "Shift times' styles: the + button opens PK1's Choose styles popover under it.", 470))
    )


EXTRA_CSS = """
.grid{display:grid;grid-template-columns:118px 1fr;column-gap:12px;row-gap:12px;align-items:start;margin-top:8px}
.grid.narrow{grid-template-columns:92px 1fr}
.glabel{height:28px;display:flex;align-items:center;color:%(text)s}
.gcol{display:flex;flex-direction:column;gap:6px;min-width:0}
.gline{display:flex;align-items:center;gap:6px;min-height:28px}
.ghint{color:%(muted)s;font-size:12px}
.times,.unit{color:%(muted)s}
.unit{margin-left:4px;font-size:12px}
.mono,.fld.mono .ftext{font-family:'Liberation Mono',Consolas,monospace;font-size:12px}
.spin{display:flex;flex-direction:column;gap:3px;margin-left:4px}
.spin i{display:block;width:0;height:0;border-left:4px solid transparent;border-right:4px solid transparent;
        border-bottom:4px solid %(muted)s}
.spin i.dn{border-bottom:none;border-top:4px solid %(muted)s}
.swatch{width:32px;height:24px;border-radius:2px;outline:1px solid %(control)s;flex:none}
.fld.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s}
.ferr{display:flex;align-items:center;gap:6px;color:%(danger)s;font-size:12px}
.b.off{opacity:.46}
.arrow{display:flex;align-items:center;justify-content:space-between;margin-bottom:6px}
.arrow b{font-weight:650;font-size:13px}
.slring{outline:1px solid %(text)s;outline-offset:3px;border-radius:1px}
.sl{position:relative;height:20px;width:400px}
.slt{position:absolute;left:0;right:0;top:8px;height:4px;border-radius:2px;background:%(raised)s;outline:1px solid %(line)s}
.slf{position:absolute;left:0;top:8px;height:4px;border-radius:2px;background:%(accent)s}
.slh{position:absolute;top:1px;width:18px;height:18px;margin-left:-9px;border-radius:9px;background:%(field)s;
     border:1px solid %(control)s;box-shadow:0 1px 2px rgba(0,0,0,.3)}
.slends{display:flex;justify-content:space-between;width:400px;color:%(muted)s;font-size:11px;margin-top:6px}
.vpanel{width:622px;outline:1px solid %(line)s;background:%(field)s}
.vhead{display:flex;align-items:center;height:30px;padding:0 8px 0 12px;background:%(panel)s;font-weight:650;
       font-size:11px;letter-spacing:.02em;box-shadow:inset 0 -1px %(line)s}
.vmore{margin-left:auto;color:%(muted)s}
.pic{position:relative;background:linear-gradient(160deg,#2d4a63 0%%,#5f7f8c 38%%,#c79a6b 66%%,#2a2622 100%%)}
.pic::after{content:"";position:absolute;left:12%%;right:12%%;bottom:9%%;height:26px;border-radius:2px;
            background:rgba(255,255,255,.0)}
.vbar{display:flex;align-items:center;gap:2px;height:34px;padding:0 8px;background:%(panel)s;box-shadow:inset 0 1px %(line)s}
.vtime{margin-left:auto;color:%(muted)s}
.ptr{position:absolute;z-index:3}
.fs{position:relative;background:#000;outline:1px solid %(line)s}
.fsp{position:absolute;left:0;top:0}
.fsbar{position:absolute;left:0;right:0;bottom:0;height:44px;display:flex;align-items:center;gap:4px;padding:0 10px;
       background:%(panel)s;box-shadow:inset 0 1px %(line)s}
.fsslide{flex:1;height:4px;margin:0 12px;background:%(raised)s;outline:1px solid %(line)s;border-radius:2px;position:relative}
.fsslide i{position:absolute;left:0;top:0;bottom:0;background:%(accent)s;border-radius:2px}
.qbo{outline:1px solid %(line)s}
.fstack{position:absolute;right:12px;bottom:12px;width:360px}
"""


def page(key, content):
    th = THEMES[key]
    html = kit.page(key, content)
    return html.replace("</style>", EXTRA_CSS % dict(th) + "</style>", 1)


def main():
    for key in THEMES:
        (HERE / f"pu4-{key}-1-video.html").write_text(page(key, video_page(key)))
        (HERE / f"pu4-{key}-2-aspect.html").write_text(page(key, aspect_page(key)))
        (HERE / f"pu4-{key}-3-questions.html").write_text(page(key, questions_page(key)))


main()
