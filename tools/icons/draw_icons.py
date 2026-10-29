#!/usr/bin/env python3
"""K1: the HikariSub vector icon set, drawn as code.

Writes one monochrome SVG per icon role into src/ui/icons/ and the set's
manifest (src/ui/icons/manifest.json). The SVG files are the shipped source;
this script is how they were drawn and is the place to change them, so that
every icon keeps the rules of docs/ux/icons.md:

* a 16 x 16 viewBox, every coordinate on the quarter-unit grid;
* one stroke weight (1 unit) with round caps and joins, set once on the root;
* paint is only currentColor or none, so the icon is tinted at run time;
* at most one accent layer, the group <g id="accent">, painted with the
  accent colour (never the only carrier of meaning);
* no raster, text, style sheets, opacity or colours of its own.

Run:  python3 tools/icons/draw_icons.py
"""

import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "src", "ui", "icons")


def q(v):
    """Snap to the quarter-unit grid and print without trailing zeros."""
    v = round(v * 4) / 4
    if v == int(v):
        return str(int(v))
    return ("%.2f" % v).rstrip("0")


# ---------------------------------------------------------------- primitives

def stroke(d):
    return '<path d="%s"/>' % d


def fill(d, rule=None):
    extra = ' fill-rule="evenodd"' if rule == "evenodd" else ""
    return '<path d="%s" fill="currentColor" stroke="none"%s/>' % (d, extra)


def solid(d):
    """A filled shape whose outline keeps the round joins (filled and stroked)."""
    return '<path d="%s" fill="currentColor"/>' % d


def rect(x, y, w, h, r=1):
    return '<rect x="%s" y="%s" width="%s" height="%s" rx="%s"/>' % (q(x), q(y), q(w), q(h), q(r))


def frect(x, y, w, h, r=0):
    rx = ' rx="%s"' % q(r) if r else ""
    return '<rect x="%s" y="%s" width="%s" height="%s"%s fill="currentColor" stroke="none"/>' % (
        q(x), q(y), q(w), q(h), rx)


def circle(cx, cy, r):
    return '<circle cx="%s" cy="%s" r="%s"/>' % (q(cx), q(cy), q(r))


def dot(cx, cy, r):
    return '<circle cx="%s" cy="%s" r="%s" fill="currentColor" stroke="none"/>' % (q(cx), q(cy), q(r))


def accent(*parts):
    return '<g id="accent">' + "".join(parts) + "</g>"


# 3 x 5 pixel letters for the format badges (filled unit cells).
FONT = {
    "A": ["010", "101", "111", "101", "101"],
    "S": ["111", "100", "111", "001", "111"],
    "R": ["110", "101", "110", "101", "101"],
    "T": ["111", "010", "010", "010", "010"],
    "M": ["101", "111", "111", "101", "101"],
    "P": ["110", "101", "110", "100", "100"],
    "D": ["110", "101", "101", "101", "110"],
    "V": ["101", "101", "101", "101", "010"],
    "L": ["100", "100", "100", "100", "111"],
    "2": ["110", "001", "010", "100", "111"],
    "F": ["111", "100", "110", "100", "100"],
}


def letters(text, x, y):
    """Filled pixel letters starting at (x, y); 3 wide, 1 apart."""
    d = []
    for i, ch in enumerate(text):
        rows = FONT[ch]
        for r, row in enumerate(rows):
            c = 0
            while c < 3:
                if row[c] == "1":
                    run = 1
                    while c + run < 3 and row[c + run] == "1":
                        run += 1
                    d.append("M%s %sh%sv1h-%sz" % (q(x + i * 4 + c), q(y + r), run, run))
                    c += run
                else:
                    c += 1
    return fill("".join(d))


# ---------------------------------------------------------------- motifs

def page(cut=False):
    """A sheet with a folded corner; cut leaves the lower right for a badge."""
    if cut:
        return stroke("M8.5 14.5h-5V1.5h6l3 3v4") + stroke("M9.5 1.5v3h3")
    return stroke("M12.5 14.5h-9V1.5h6l3 3z") + stroke("M9.5 1.5v3h3")


def floppy(cut=False):
    if cut:
        return (stroke("M8.5 13.5h-6v-11h9l2 2v4") + stroke("M5.5 2.5v3h5v-3")
                + stroke("M4.5 13.5v-4h4"))
    return (stroke("M2.5 2.5h9l2 2v9h-11z") + stroke("M5.5 2.5v3h5v-3")
            + stroke("M4.5 13.5v-4h7v4"))


def film(x=1.5, y=2.5, w=11, h=8, strip=3):
    """A film frame: a rectangle with perforated side strips strip wide."""
    x2, y2 = x + w, y + h
    s = q(strip)
    return (rect(x, y, w, h, 1) + stroke("M%s %sv%s M%s %sv%s" % (q(x + strip), q(y), q(h), q(x2 - strip), q(y), q(h)))
            + stroke("M%s %sh%s M%s %sh%s M%s %sh-%s M%s %sh-%s" % (
                q(x), q(y + h / 3), s, q(x), q(y + 2 * h / 3), s, q(x2), q(y + h / 3), s, q(x2), q(y + 2 * h / 3), s)))


def video_film(right=8, bottom=8.5, divider=10):
    """The video icons' film frame (10 x 7.5 units at 1.5, 2.5), open at its
    lower right for a badge: the right edge stops at y right, the bottom
    edge at x bottom and the right strip's divider at y divider, so that a
    clear unit of background parts the film from the badge on any surface
    (the knockout is geometry, not a mask or a background-coloured stroke)."""
    return (stroke("M11.5 %sV3.5a1 1 0 0 0-1-1h-8a1 1 0 0 0-1 1V9a1 1 0 0 0 1 1H%s" % (q(right), q(bottom)))
            + stroke("M4.5 2.5V10M8.5 2.5V%s" % q(divider)) + stroke("M1.5 5h3M1.5 7.5h3M11.5 5h-3M11.5 7.5h-3"))


def note(x=6.5):
    """An eighth note whose stem stands at x."""
    return stroke("M%s 11.5V2.5l4 1.5" % q(x)) + dot(x - 2, 11.5, 2)


def magnifier(cx=6.5, cy=6.5, r=4.5, handle=4.5):
    h = r * math.sqrt(0.5)
    return circle(cx, cy, r) + stroke("M%s %sl%s %s" % (q(cx + h), q(cy + h), q(handle), q(handle)))


def diamond(cx, cy, r):
    return stroke("M%s %sl%s %s-%s %s-%s-%sz" % (q(cx), q(cy - r), q(r), q(r), q(r), q(r), q(r), q(r)))


def plus_badge():
    return accent(stroke("M12 9.5v5M9.5 12h5"))


def cross_badge():
    return accent(stroke("M10 10l4 4M14 10l-4 4"))


def clock_badge():
    return accent(circle(12, 12, 3), stroke("M12 10.5V12l1 1"))


def waveform(x=1.5, top=4.5, bottom=11.5):
    mid = (top + bottom) / 2
    heights = [1, 3, 2, 3.5, 1.5, 2.5, 1]
    d = []
    for i, hgt in enumerate(heights):
        d.append("M%s %sv%s" % (q(x + i * 2), q(mid - hgt), q(2 * hgt)))
    return stroke("".join(d))


def triangle(x, y, w, h, right=True):
    """A filled play triangle in the box (x, y, w, h)."""
    if right:
        return solid("M%s %sv%sl%s-%sz" % (q(x), q(y), q(h), q(w), q(h / 2)))
    return solid("M%s %sv%sl-%s-%sz" % (q(x + w), q(y), q(h), q(w), q(h / 2)))


def gear():
    teeth, ro, ri = 8, 6.75, 5
    pts = []
    for i in range(teeth):
        a = 2 * math.pi * i / teeth
        for da, r in ((-0.2, ri), (-0.12, ro), (0.12, ro), (0.2, ri)):
            pts.append((8 + r * math.cos(a + da), 8 + r * math.sin(a + da)))
    d = "M" + " ".join("%s %s" % (q(x), q(y)) for x, y in pts) + "z"
    return stroke(d) + circle(8, 8, 2)


def letter_a(x=2, top=1.5, base=10.5, width=9):
    """A stroked capital A between top and base."""
    mid = x + width / 2
    bar = top + (base - top) * 0.65
    return stroke("M%s %sL%s %sl%s %s" % (q(x), q(base), q(mid), q(top), q(width / 2), q(base - top))) + stroke(
        "M%s %sh%s" % (q(x + width * 0.2), q(bar), q(width * 0.6)))


def roboto_a():
    """The colour buttons' capital A in Roboto's proportions: 8 units wide
    by 9 tall (outer), the apex cut flat as Roboto's is, the bar at 3/8 of
    the height with its ends on the legs. It stands on y 10, so the swatch
    below keeps a clear unit from its feet."""
    return stroke("M4.5 9.5L7.5 1.5H8.5L11.5 9.5") + stroke("M5.75 6.5H10.25")


def time_insert(start):
    """Insert start / end time from video: a small film frame hands its time
    down an elbow arrow into the start (left) or end (right) cell of the
    line's timing bar; the arrow and the filled cell are the accent."""
    if start:
        return (film(6.5, 1.5, 8, 6, 2) + stroke("M5.5 10.5h8a1 1 0 0 1 1 1v2a1 1 0 0 1-1 1h-8")
                + stroke("M10.5 10.5v4")
                + accent(stroke("M6.5 4.5h-3V9M1.5 7l2 2 2-2"),
                         fill("M2 10h4v5H2a1 1 0 0 1-1-1v-3a1 1 0 0 1 1-1z")))
    return (film(1.5, 1.5, 8, 6, 2) + stroke("M10.5 10.5h-8a1 1 0 0 0-1 1v2a1 1 0 0 0 1 1h8")
            + stroke("M5.5 10.5v4")
            + accent(stroke("M9.5 4.5h3V9M10.5 7l2 2 2-2"),
                     fill("M14 10h-4v5h4a1 1 0 0 0 1-1v-3a1 1 0 0 0-1-1z")))


def layout(*regions):
    """A window with panels; each region adds its contents."""
    return rect(1.5, 2.5, 13, 11, 1) + "".join(regions)


# ---------------------------------------------------------------- the set

ICONS = []


def icon(role, label, legacy, surfaces, body, mirror=False):
    ICONS.append({
        "role": role,
        "label": label,
        "legacy": legacy,
        "surfaces": surfaces,
        "mirror": mirror,
        "body": body,
    })


FILE, EDIT, SUBS, TIMING, VIDEO, AUDIO, VIEW, AUTO, HELP = (
    "menu:file", "menu:edit", "menu:subtitles", "menu:timing", "menu:video", "menu:audio", "menu:view",
    "menu:automation", "menu:help")
# (no main toolbar: the user's decision of 2026-10-05; legacy's toolbar
# actions are reached through the menus)
TRANSPORT, FULLSCREEN, AUDIOBOX, EDITOR, VISUAL, PICKER, TABS = (
    "video-transport", "video-fullscreen", "audio-box", "line-editor", "visual-tools", "colour-picker",
    "document-tabs")
AUTOPANEL = "automation-panel"  # AM1: the Automation panel (was the automation manager window)
TEMPLATEPANEL = "template-editor"  # KE2: the Template editor panel and its preview badge on the video

# File
icon("open-subtitles", "Open subtitles", ["opensubs.png"], [FILE, "video-context-menu"], page(True) + plus_badge())
icon("save", "Save", ["Save.png"], [FILE, "tab-menu", "shift-times-panel"], floppy())
icon("save-all", "Save all subtitles", ["SaveAll.png"], [FILE, "tab-menu"],
     stroke("M4.5 3.5v-2h8l2 2v8h-2") + stroke("M1.5 4.5h9l2 2v8h-11z") + stroke("M4.5 4.5v2.5h4V4.5")
     + stroke("M3.5 14.5v-3h6v3"))
icon("save-as", "Save as", ["SaveAs.png"], [FILE],
     floppy(True) + accent(stroke("M9.5 14.5l.5-2 3.5-3.5 1.5 1.5-3.5 3.5z")))
icon("save-translation", "Save translation", ["SaveTl.png"], [FILE],
     floppy(True) + accent(stroke("M10 10.5h4.5M12.25 10.5v4")))
icon("recent-subtitles", "Recently opened subtitles", ["RecentSubs.png"], [FILE, "video-context-menu"], page(True) + clock_badge())
icon("close-subtitles", "Remove subtitles from the editor", ["Close.png"], [FILE], page(True) + cross_badge())
icon("save-with-video-name", "Save subtitles with video name", ["SaveWithVideoName.png"], [FILE],
     floppy(True) + accent(rect(9.5, 9.5, 5, 5, 1), stroke("M11.5 9.5v5")))
icon("last-session", "Load last session", ["Last Session.png"], [FILE],
     stroke("M5.5 4.5v-2h9v7h-2") + rect(1.5, 6.5, 11, 7, 1) + stroke("M1.5 8.5h11")
     + accent(stroke("M5 11.5h4M7.5 10l1.5 1.5L7.5 13")), mirror=True)
icon("settings", "Settings", ["Settings.png"], [FILE, "settings-dialog", "font-catalog"], gear())
icon("exit", "Exit", ["Exit.png"], [FILE],
     stroke("M8 1.5v6") + stroke("M4.5 4a5.5 5.5 0 1 0 7 0"))

# Edit
UNDO = stroke("M5.5 3.5l-3 3 3 3") + stroke("M2.5 6.5h7a3.5 3.5 0 0 1 0 7h-3")
icon("undo", "Undo", ["Undo.png"], [EDIT], UNDO, mirror=True)
icon("redo", "Redo", ["Redo.png"], [EDIT],
     stroke("M10.5 3.5l3 3-3 3") + stroke("M13.5 6.5h-7a3.5 3.5 0 0 0 0 7h3"), mirror=True)
icon("undo-to-last-save", "Undo to last save", ["UndoToLastSave.png"], [EDIT],
     stroke("M7.5 4.5l-3 3 3 3") + stroke("M4.5 7.5h5.5a3 3 0 0 1 0 6h-2") + accent(stroke("M1.5 2.5v10")),
     mirror=True)
icon("history", "History", ["History.png"], [EDIT, "history-panel"],
     stroke("M2.5 8a5.5 5.5 0 1 0 1.75-4") + stroke("M2.5 1.5v3h3") + stroke("M8 5v3.5l2.5 1.5"))
icon("find-replace", "Find and replace", ["FindReplace.png"], [EDIT, "search-tool"],
     magnifier(6, 6, 4, 2) + accent(stroke("M8.5 12.5h6M12.5 10.5l2 2-2 2")), mirror=True)
icon("search", "Find", ["Search.png"], [EDIT, "search-tool"], magnifier(6.5, 6.5, 4.5, 4))
icon("sort", "Sort lines", ["Sort.png"], [EDIT, "style-manager"],
     stroke("M1.5 4.5h7M1.5 8h5M1.5 11.5h3") + accent(stroke("M12 2.5v11M9.5 11l2.5 2.5 2.5-2.5")), mirror=True)
icon("sort-selected", "Sort selected lines", ["SortSel.png"], [EDIT],
     stroke("M4 4.5h5M4 8h3.5M4 11.5h2") + stroke("M12 2.5v11M9.5 11l2.5 2.5 2.5-2.5")
     + accent(stroke("M1.5 3v10")), mirror=True)
icon("select-lines", "Select lines", ["Sellines.png"], [EDIT, "select-lines"],
     stroke("M7.5 4h7M7.5 8h7M7.5 12h7") + accent(frect(2, 3, 3, 2), frect(2, 11, 3, 2)) + rect(2.5, 7, 2, 2, 0),
     mirror=True)

# Video
# The video icons' badges (the user's review of 2026-10-13, round 2) stand in
# the film's open corner one clear pixel from it at 16 px, as audio-from-video's
# note does. Each cut is the least that leaves that pixel: the + and the x
# (open-video, close-video) share audio-from-video's cut; the clock, a pixel
# wider, takes a half unit more of the right and bottom edges.
icon("open-video", "Open video", ["openvideo.png"], [VIDEO, "video-context-menu", "dummy-video"],
     video_film() + accent(stroke("M12.5 10.5v4M10.5 12.5h4")))
# The clock's ring is drawn through pixel centres, flat for two units on each
# side with arcs of radius 4 at the corners: at 16 px it lands on whole pixels
# (a seven-pixel ring), and larger it reads as round. The hands are one unit
# each from the centre pixel, a clear pixel inside the ring.
icon("recent-video", "Recently opened videos", ["RecentVideo.png"], [VIDEO, "video-context-menu"],
     video_film(7.5, 7.5, 8.5)
     + accent(stroke("M11.5 9.5h2a4 4 0 0 1 2 2v2a4 4 0 0 1-2 2h-2a4 4 0 0 1-2-2v-2a4 4 0 0 1 2-2z"),
              stroke("M12.5 11.5v1h1")))
icon("open-keyframes", "Open keyframes", ["OpenKeyframes.png"], [VIDEO], diamond(6, 6.5, 4.5) + plus_badge())
icon("recent-keyframes", "Recently opened keyframes", ["RecentKeyframes.png"], [VIDEO],
     diamond(6, 6.5, 4.5) + clock_badge())
# Insert start / end time from video (the user's review of 2026-10-13): the
# video's frame hands its time down into the line's start or end cell of the
# timing bar below, as legacy's SetStartTime/SetEndTime bitmaps did; the
# go-to pair keeps the bracket and the arrow.
icon("set-start-time", "Insert start time from video", ["SetStartTime.png"], [VIDEO],
     time_insert(start=True))
icon("set-end-time", "Insert end time from video", ["SetEndTime.png"], [VIDEO],
     time_insert(start=False))
icon("frame-previous", "Previous frame", ["PrevFrame.png"], [VIDEO, TRANSPORT],
     rect(6.5, 3.5, 8, 9, 1) + stroke("M9.5 3.5v9M11.5 3.5v9") + accent(stroke("M4 5.5L1.5 8 4 10.5")))
icon("frame-next", "Next frame", ["NextFrame.png"], [VIDEO, TRANSPORT],
     rect(1.5, 3.5, 8, 9, 1) + stroke("M4.5 3.5v9M6.5 3.5v9") + accent(stroke("M12 5.5L14.5 8 12 10.5")))
icon("keyframe-previous", "Go to previous keyframe", ["PrevKeyframe.png"], [VIDEO, TRANSPORT],
     diamond(10.5, 8, 4) + accent(stroke("M4 5.5L1.5 8 4 10.5")))
icon("keyframe-next", "Go to next keyframe", ["NextKeyframe.png"], [VIDEO, TRANSPORT],
     diamond(5.5, 8, 4) + accent(stroke("M12 5.5L14.5 8 12 10.5")))
icon("video-to-start-time", "Go to start time", ["videoonstime.png"], [VIDEO],
     stroke("M5.5 2.5h-2v11h2") + accent(stroke("M14.5 8h-8M9 5.5L6.5 8 9 10.5")))
icon("video-to-end-time", "Go to end time of line", ["videoonetime.png"], [VIDEO],
     stroke("M10.5 2.5h2v11h-2") + accent(stroke("M1.5 8h8M7 5.5L9.5 8 7 10.5")))
icon("audio-to-video-time", "Set audio position to video time", ["SetVideoTimeOnAudio.png"], [VIDEO],
     waveform(1.5, 4.5, 11.5) + accent(stroke("M8 1.5v13")))
icon("audio-marker-to-video-time", "Set audio marker to video time", ["SetVideoTimeOnAudioMark.png"],
     [VIDEO], waveform(1.5, 4.5, 11.5) + accent(stroke("M10.5 14.5v-13h3.5l-1 1.5 1 1.5h-3.5")))
icon("zoom", "Zoom video", ["Zoom.png"], [VIDEO],
     magnifier(6.5, 6.5, 4.5, 4) + accent(stroke("M6.5 4.5v4M4.5 6.5h4")))

# Media transport (video box, fullscreen, menus)
icon("media-play", "Play", ["play.png", "play1.png", "PlayMenu.png"], [TRANSPORT, FULLSCREEN, VIDEO, "video-context-menu",
                                                                        AUTOPANEL],
     triangle(4.5, 2.5, 9, 11))
icon("media-pause", "Pause", ["pause.png", "pause1.png", "PauseMenu.png"], [TRANSPORT, FULLSCREEN, VIDEO, "video-context-menu"],
     frect(3.5, 2.5, 3, 11, 0.5) + frect(9.5, 2.5, 3, 11, 0.5))
icon("media-stop", "Stop", ["stop.png", "stop1.png", "button_stop.png"], [TRANSPORT, FULLSCREEN, AUDIOBOX, "video-context-menu", AUTOPANEL],
     frect(3, 3, 10, 10, 1))
icon("play-line", "Play the current line", ["playline.png", "playline1.png", "button_playsel.png"],
     [TRANSPORT, FULLSCREEN, AUDIOBOX],
     stroke("M3.5 2.5h-2v11h2M12.5 2.5h2v11h-2") + triangle(5.5, 4.5, 6, 7))
icon("media-previous-file", "Previous file", ["backward.png", "backward1.png"], [TRANSPORT, FULLSCREEN],
     frect(1, 2, 2, 12, 0.5) + triangle(4.5, 2.5, 9, 11, right=False))
icon("media-next-file", "Next file", ["forward.png", "forward1.png"], [TRANSPORT, FULLSCREEN],
     triangle(2.5, 2.5, 9, 11) + frect(13, 2, 2, 12, 0.5))
# V5 #184: the fullscreen panel's "Show toolbar" (legacy a check box,
# VideoFullscreen.cpp:68): the window with its toolbar row. FS1 #275: the
# bar's mode button (FS1-bar-mode), which took its place.
icon("show-toolbar", "Full screen bar", [], [FULLSCREEN],
     layout(stroke("M1.5 9.5h13"), frect(3.5, 10.75, 2, 1.5, 0.25), frect(7, 10.75, 2, 1.5, 0.25),
            frect(10.5, 10.75, 2, 1.5, 0.25)))

# Audio menu
icon("open-audio", "Open audio", ["OpenAudio.png"], [AUDIO], note(6.5) + plus_badge())
icon("recent-audio", "Recently opened audio", ["RecentAudio.png"], [AUDIO], note(6.5) + clock_badge())
icon("audio-from-video", "Open audio from video", ["OpenAudioFromVideo.png"], [AUDIO],
     video_film() + accent(stroke("M13.5 12.5v-8l1.5 .5"), dot(11.5, 12.5, 2)))
icon("close-audio", "Close audio", ["CloseAudio.png"], [AUDIO], note(6.5) + cross_badge())

# Audio box
icon("audio-previous-line", "Previous line", ["button_prev.png"], [AUDIOBOX],
     solid("M7.5 3.5v9L2 8z") + solid("M14 3.5v9L8.5 8z"))
icon("audio-next-line", "Next line", ["button_next.png"], [AUDIOBOX],
     solid("M8.5 3.5v9L14 8z") + solid("M2 3.5v9L7.5 8z"))
icon("audio-play", "Play", ["button_PlayLine.png"], [AUDIOBOX],
     triangle(4.5, 3, 8, 10))
icon("play-before-mark", "Play before the tag", ["button_playbefore.png"], [AUDIOBOX],
     triangle(2, 4, 6, 8) + accent(stroke("M11.5 2v12")))
icon("play-after-mark", "Play after the tag", ["button_playafter.png"], [AUDIOBOX],
     accent(stroke("M4.5 2v12")) + triangle(8, 4, 6, 8))
icon("play-before-start", "Play 500ms before the start time", ["button_playfivehbefore.png"], [AUDIOBOX],
     triangle(1.5, 4.5, 5, 7) + stroke("M12.5 2.5h-2v11h2"))
icon("play-after-start", "Play 500 ms after the start time", ["button_playfirstfiveh.png"], [AUDIOBOX],
     stroke("M5.5 2.5h-2v11h2") + triangle(9, 4.5, 5, 7))
icon("play-before-end", "Play 500ms before the end time", ["button_playlastfiveh.png"], [AUDIOBOX],
     triangle(2, 4.5, 5, 7) + stroke("M10.5 2.5h2v11h-2"))
icon("play-after-end", "Play 500ms after the end time", ["button_playfivehafter.png"], [AUDIOBOX],
     stroke("M3.5 2.5h2v11h-2") + triangle(9, 4.5, 5, 7))
icon("play-to-end", "Play to the end", ["button_playtoend.png"], [AUDIOBOX],
     triangle(2, 3.5, 7, 9) + stroke("M11.5 3.5v9M14 3.5v9"))
icon("lead-in", "Add lead-in to the active line", ["button_leadin.png"], [AUDIOBOX],
     stroke("M13.5 2.5h-2v11h2") + accent(stroke("M9.5 8h-8M4 5.5L1.5 8 4 10.5")))
icon("lead-out", "Add lead-out to the active line", ["button_leadout.png"], [AUDIOBOX],
     stroke("M2.5 2.5h2v11h-2") + accent(stroke("M6.5 8h8M12 5.5l2.5 2.5-2.5 2.5")))
icon("commit", "Commit", ["button_audio_commit.png"], [AUDIOBOX], stroke("M2.5 8.5l3.5 3.5 7.5-8"))
icon("go-to-selection", "Go to selection", ["button_audio_go.png"], [AUDIOBOX, "reference-tray"],
     stroke("M1.5 8h6M5.5 6l2 2-2 2") + accent(stroke("M11 3.5H9.5v9H11M12.5 3.5H14v9h-1.5")))
icon("karaoke", "Karaoke mode", ["button_karaoke.png"], [AUDIOBOX],
     rect(1.5, 4.5, 13, 7, 1) + stroke("M5.5 4.5v7M10.5 4.5v7") + accent(frect(2, 5, 3, 6)))
icon("karaoke-split", "Karaoke split mode", ["button_auto_split.png"], [AUDIOBOX],
     rect(1.5, 4.5, 13, 7, 1) + stroke("M5.5 4.5v7") + accent(stroke("M10.5 1.5v13")))
icon("auto-commit", "Auto commit", ["button_auto_commit.png"], [AUDIOBOX],
     stroke("M1.5 7l3 3 6-6.5") + accent(letters("A", 11, 10)))
icon("next-after-commit", "Go to the next line after commit", ["button_go_next_after_commit.png"], [AUDIOBOX],
     stroke("M1.5 7.5l2.5 2.5 5-5.5") + accent(stroke("M9.5 11.5h5M12.5 9.5l2 2-2 2")))
icon("auto-scroll", "Scroll to the active line", ["button_auto_go.png"], [AUDIOBOX],
     stroke("M1.5 6h5M4.5 4l2 2-2 2") + stroke("M9.5 2.5H8v7h1.5M12.5 2.5H14v7h-1.5") + accent(letters("A", 3, 10)))
icon("spectrum", "Spectrum mode", ["button_spectrum.png"], [AUDIOBOX],
     stroke("M2.5 13.5v-3M5 13.5v-7M7.5 13.5v-10M10 13.5v-6M12.5 13.5v-8"))
icon("spectrum-nonlinear", "Non-linear spectrum", ["SpectrumNonLinear.png"], [AUDIOBOX],
     stroke("M2.5 13.5v-3M5 13.5v-7M7.5 13.5v-10M10 13.5v-6M12.5 13.5v-8")
     + accent(stroke("M1.5 12.5C6 12.5 11 9 14.5 2.5")))
icon("link", "Link", ["button_link.png", "ScaleLink.png"], [AUDIOBOX, VISUAL, "script-properties"],
     stroke("M7 4.5l1.5-1.5a2.5 2.5 0 0 1 3.5 3.5L10.5 8M9 11.5l-1.5 1.5a2.5 2.5 0 0 1-3.5-3.5L5.5 8")
     + stroke("M6.5 9.5l3-3"))

# Line editor tag buttons
# A filled F on whole pixels, as tag-bold's B: Roboto's narrow F (the top arm
# 6 units, the middle arm 5, centred at half the cap height), the stem and
# arms two units; two units narrower than the B, so lighter by its width and
# its open form.
icon("tag-font", "Font selection", ["Font.png"], [EDITOR, "font-dialog"],
     fill("M5 2h6v2H7v3h3v2H7v5H5z"))
icon("tag-bold", "Bold", ["Bold.png"], [EDITOR],
     fill("M4 2h6a2 2 0 0 1 2 2v1.5c0 .75-.25 1.5-1 2 1.25.25 2 1.25 2 2.5v1.5a2.5 2.5 0 0 1-2.5 2.5H4z"
          "M6 4h3.25a.75.75 0 0 1 .75.75v1.5a.75.75 0 0 1-.75.75H6zM6 9h4.25a.75.75 0 0 1 .75.75v1.5a.75.75 0 0 1-.75.75H6z",
          "evenodd"))
icon("tag-italic", "Italic", ["Italic.png"], [EDITOR], stroke("M7 2.5h5M4 13.5h5M9.5 2.5l-3 11"))
icon("tag-underline", "Underline", ["Under.png"], [EDITOR],
     stroke("M4.5 2.5V7a3.5 3.5 0 0 0 7 0V2.5") + accent(stroke("M3.5 13.5h9")))
icon("tag-strikeout", "Strikethrough", ["Strike.png"], [EDITOR],
     stroke("M11.25 4.25C10.75 3 9.5 2.5 8 2.5c-2 0-3.25 1-3.25 2.5 0 1.25.75 2 2 2.5M10.25 9.25c.5.5.75 1 .75 1.75 0 1.5-1.25 2.5-3 2.5-1.75 0-3-.75-3.5-2")
     + accent(stroke("M2.5 8h11")))
icon("colour-primary", "Primary color", ["kolor1.png"], [EDITOR], roboto_a() + accent(frect(4, 12, 8, 3, 0.5)))
icon("colour-secondary", "Secondary color for karaoke", ["kolor2.png"], [EDITOR],
     roboto_a() + rect(4.5, 12.5, 7, 2, 0.5) + accent(frect(4, 12, 4, 3, 0.5)))
icon("colour-outline", "Border color", ["kolor3.png"], [EDITOR],
     roboto_a() + accent(rect(4.5, 12.5, 7, 2, 0.5)))
icon("colour-shadow", "Shadow color", ["kolor4.png"], [EDITOR],
     roboto_a() + rect(4.5, 11.5, 6, 2, 0.5) + accent(fill("M11 12h1v3H5v-1h6z")))
# LE4: the Line editor's alignment picker (\an1-\an9 in numpad layout):
# the 3 x 3 cells as small squares, the chosen one larger in the accent; the
# mixed icon (several Lines with different alignments) has no chosen cell.
ALIGN_CELLS = {7: (3, 3), 8: (8, 3), 9: (13, 3), 4: (3, 8), 5: (8, 8), 6: (13, 8), 1: (3, 13), 2: (8, 13), 3: (13, 13)}
ALIGN_NAMES = {1: "Bottom-left", 2: "Bottom-center", 3: "Bottom-right", 4: "Middle-left", 5: "Center",
               6: "Middle-right", 7: "Top-left", 8: "Top-center", 9: "Top-right"}


def alignment_grid(chosen=None):
    cells = "".join(frect(x - 1, y - 1, 2, 2) for n, (x, y) in sorted(ALIGN_CELLS.items()) if n != chosen)
    if chosen is None:
        return cells
    x, y = ALIGN_CELLS[chosen]
    return cells + accent(frect(x - 1.5, y - 1.5, 3, 3, 0.5))


for n in range(1, 10):
    icon("alignment-%d" % n, "%s (an%d)" % (ALIGN_NAMES[n], n), [], [EDITOR], alignment_grid(n))
icon("alignment-mixed", "Text position (mixed)", [], [EDITOR], alignment_grid())

# Subtitles and timing menus
# The Line editor: its Options page and fullscreen's Open editor (WS3
# retired the Subtitles menu's editor switch it was drawn for).
icon("editor", "Line editor", ["editor.png"], ["video-context-menu", "settings-dialog"],
     rect(1.5, 2.5, 13, 11, 1) + stroke("M4 6.5h5M4 9.5h7") + accent(stroke("M11.5 5v3")), mirror=True)
icon("script-properties", "ASS file properties", ["Assprops.png"], [SUBS, "script-properties", "settings-dialog"],
     page() + stroke("M5.5 7.5h1M8 7.5h2.5M5.5 10h1M8 10h2.5M5.5 12.5h1M8 12.5h2.5"), mirror=True)
icon("styles", "Style manager", ["Style.png"], [SUBS, "style-manager", "search-tool", "select-lines", "misspell-replacer",
                                                "grid-menu", "tab-menu"],
     stroke("M8 1.5a6.5 6.5 0 1 0 0 13c1 0 1.5-.75 1.5-1.5s-.5-1-.5-1.75.75-1.25 1.5-1.25H12a2.5 2.5 0 0 0 2.5-2.5C14.5 4 11.5 1.5 8 1.5z")
     + accent(dot(4.75, 7.5, 1), dot(6.75, 4.5, 1), dot(10, 4.5, 1)))
CONVERT = stroke("M2.5 3.5h9M9.5 1.5l2 2-2 2")
icon("convert", "Convert", ["Convert.png"], [SUBS],
     stroke("M2.5 5.5h10M10 3l2.5 2.5L10 8") + stroke("M13.5 10.5h-10M6 8l-2.5 2.5L6 13"))
icon("convert-ass", "Convert to ASS", ["Convass.png"], [SUBS], CONVERT + accent(letters("ASS", 2, 9)))
icon("convert-srt", "Convert to SRT", ["ConvSRT.png"], [SUBS], CONVERT + accent(letters("SRT", 2, 9)))
icon("convert-mdvd", "Convert to MDVD", ["ConvMDVD.png"], [SUBS], CONVERT + accent(letters("MDVD", 0, 9)))
icon("convert-mpl2", "Convert to MPL2", ["ConvMPL2.png"], [SUBS], CONVERT + accent(letters("MPL2", 0, 9)))
icon("convert-tmp", "Convert to TMP", ["Convtmp.png"], [SUBS], CONVERT + accent(letters("TMP", 2, 9)))
icon("shift-times", "Shift times", ["Time.png"], [TIMING, SUBS],
     circle(6.5, 6.5, 5) + stroke("M6.5 4v2.5l2 1.5") + accent(stroke("M9.5 12.5h5M12.5 10.5l2 2-2 2")))
icon("resample", "Resample subtitles", ["subsResample.png"], [SUBS, VISUAL, "resample"],
     rect(1.5, 8.5, 6, 6, 1) + stroke("M1.5 5.5v-3a1 1 0 0 1 1-1h11a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1h-3")
     + accent(stroke("M6 10l6-6M8.5 4H12v3.5")))
icon("font-collector", "Font collector", ["FontCollector.png"], [SUBS, "font-collector", "grid-menu"],
     stroke("M1.5 9.5h3l1 2h5l1-2h3v4a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1z")
     + accent(stroke("M5.5 7L8 1.5 10.5 7M6.5 5h3")))
icon("spellchecker", "Check spelling", ["Spellchecker.png"], [SUBS, "spell-checker", "text-field-menu"],
     stroke("M1.5 3.5h9M1.5 7h6") + accent(stroke("M1.5 11l1.5-1.5 1.5 1.5 1.5-1.5L7.5 11"))
     + stroke("M8.5 11l2 2 4-4.5"), mirror=True)
icon("hide-tags", "Hide tags", ["HideTags.png"], [SUBS, EDITOR],
     stroke("M5.5 2.5h-1a1 1 0 0 0-1 1v3L2.5 8l1 1.5v3a1 1 0 0 0 1 1h1")
     + stroke("M10.5 2.5h1a1 1 0 0 1 1 1v3l1 1.5-1 1.5v3a1 1 0 0 1-1 1h-1") + accent(stroke("M10 4l-4 8")))

# Automation and help
icon("automation", "Load script", ["Automation.png"], [AUTO, AUTOPANEL],
     page() + accent(stroke("M6.5 7.5L5 9.25 6.5 11M9.5 7.5L11 9.25 9.5 11")))
icon("help", "HikariSub website", ["Help.png"], [HELP],
     circle(8, 8, 6.5) + stroke("M6 6.25a2 2 0 1 1 3 1.75c-.5.25-1 .5-1 1.25v.5") + dot(8, 11.5, 0.75))
icon("report-issue", "Report an issue", ["Nazi.png"], [HELP],
     stroke("M5.5 6.5a2.5 2.5 0 0 1 5 0v4a2.5 2.5 0 0 1-5 0z") + stroke("M6.5 4.5l-1-2M9.5 4.5l1-2")
     + stroke("M2.5 7.5h3M10.5 7.5h3M2.5 11h3M10.5 11h3") + accent(stroke("M8 6.5v6")))
icon("check-updates", "Check for updates", ["About.png"], [HELP],
     stroke("M13.5 8a5.5 5.5 0 1 1-1.75-4") + stroke("M13.5 1.5v3h-3") + accent(stroke("M8 5v5.5M6 8.5l2 2 2-2")))
icon("about", "About", ["About.png"], [HELP, "about", "translation-matching"],
     circle(8, 8, 6.5) + stroke("M8 7.5v4") + dot(8, 5, 0.75))
icon("credits", "Credits", ["Helpers.png"], [HELP, "credits"],
     circle(6, 5, 2.5) + stroke("M1.5 13.5a4.5 4.5 0 0 1 9 0") + accent(circle(11.5, 5.5, 2), stroke("M12 9.5a3 3 0 0 1 2.5 3")))
# SH3 (#223): Help > What's new and the Home page's What's new: a page of
# notes with a spark.
icon("whats-new", "What's new", [], [HELP, "home"],
     stroke("M9.5 2.5h-6v11h9v-6") + stroke("M5.5 9.5h5M5.5 11.5h3") + accent(stroke("M12.5 1v4M10.5 3h4")))

# Notices (DG1 #222 and PK1 #301: the message box, toasts and the inline bar):
# the severity in the accent layer, which the notice draws in the warning or
# danger colour. The popup kit's own controls: the date picker's calendar and
# the month arrows, and the Details disclosure.
icon("notice-warning", "Warning", [], ["dialogs"],
     stroke("M8 2l6.5 11.5h-13z") + accent(stroke("M8 6.5v3.5"), dot(8, 11.75, 0.75)))
icon("notice-error", "Error", [], ["dialogs", "notices"],
     stroke("M5.5 1.5h5l4 4v5l-4 4h-5l-4-4v-5z") + accent(stroke("M8 4.75v4"), dot(8, 11.25, 0.75)))
icon("calendar", "Choose a date", [], ["date-picker"],
     rect(1.5, 2.5, 13, 12, 1) + stroke("M1.5 6h13M5 1v3M11 1v3")
     + accent(stroke("M4.5 9h1M7.5 9h1M10.5 9h1M4.5 11.5h1M7.5 11.5h1")))
icon("chevron-left", "Previous", [], ["date-picker"], stroke("M9.5 4.5L6 8l3.5 3.5"), mirror=True)
icon("chevron-right", "Next", [], ["date-picker", "dialogs"], stroke("M6.5 4.5L10 8l-3.5 3.5"), mirror=True)
icon("chevron-down", "Expanded", [], ["dialogs"], stroke("M4.5 6.5L8 10l3.5-3.5"))

# AD3 (#312): a macro run, on the progress card and in the Automation panel's
# Runs: running (the ring turns; its accent quarter leads), applied,
# cancelled. A failed run takes notice-error, problems notice-warning.
icon("run-busy", "Running", [], ["automation", "notices"],
     stroke("M8 1.5a6.5 6.5 0 1 0 6.5 6.5") + accent(stroke("M8 1.5a6.5 6.5 0 0 1 6.5 6.5")))
icon("run-applied", "Applied", [], ["automation", "notices"],
     circle(8, 8, 6.5) + accent(stroke("M5 8.25l2 2 4-4.5")))
icon("run-cancelled", "Cancelled", [], ["automation", "notices", AUTO],
     circle(8, 8, 6.5) + accent(stroke("M4.5 11.5L11.5 4.5")))

# Layouts (WS3 #217: the layout switcher and View > Layout, which replace
# legacy's View arrangements; their icons were retired with them). The cut
# window and the refresh badge are IC1's (#226) for the same commands.
LAYOUT_CUT = (stroke("M8.5 13.5h-6a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1h11a1 1 0 0 1 1 1v5") + stroke("M1.5 6h13M6 6v7.5"))
icon("layout", "Layout", [], [VIEW, "status-bar"],
     rect(1.5, 1.5, 5, 5, 1) + rect(9.5, 1.5, 5, 5, 1) + rect(1.5, 9.5, 5, 5, 1)
     + stroke("M1.5 3.5h5M9.5 3.5h5M1.5 11.5h5") + accent(rect(9.5, 9.5, 5, 5, 1), stroke("M9.5 11.5h5")))
icon("layout-save", "Save layout as", [], [VIEW, "status-bar", "save-layout"],
     LAYOUT_CUT + accent(stroke("M12.5 9.5v5M10 12h5")))
icon("layout-reset", "Reset this layout", [], [VIEW, "status-bar"],
     LAYOUT_CUT + accent(stroke("M14.5 12a2.5 2.5 0 1 1-.75-1.75"), stroke("M14 9v1.5h-1.5")))
icon("layout-manage", "Manage layouts", [], [VIEW, "status-bar", "manage-layouts"],
     LAYOUT_CUT + accent(stroke("M10 10.5h4.5M10 12.5h4.5M10 14.5h4.5")))

# The main window's own buttons (SH1 #220: the custom title bar). Legacy had the
# system's frame; the glyphs are drawn small and plain, as the caption buttons
# of Windows 11, MuseScore 4 and KDE Breeze are, with no accent layer.
icon("window-minimize", "Minimise", [], ["title-bar"], stroke("M3.5 8.5h9"))
icon("window-maximize", "Maximise", [], ["title-bar"], rect(3.5, 3.5, 9, 9, 0.5))
icon("window-restore", "Restore", [], ["title-bar"],
     rect(3.5, 5.5, 7, 7, 0.5) + stroke("M5.5 5.5v-2h7v7h-2"))
icon("window-close", "Close", [], ["title-bar"], stroke("M3.5 3.5l9 9M12.5 3.5l-9 9"))

# Visual tools (the visual tool rail and its sub-toolbars)
icon("tool-crosshair", "Position pointer", ["Cross.png"], [VISUAL],
     circle(8, 8, 4) + stroke("M8 1.5v3M8 11.5v3M1.5 8h3M11.5 8h3") + accent(dot(8, 8, 1)))
ARROWS4 = stroke("M8 1.5v13M1.5 8h13M6 3.5l2-2 2 2M6 12.5l2 2 2-2M3.5 6l-2 2 2 2M12.5 6l2 2-2 2")
icon("tool-position", "Text positioning", ["Position.png"], [VISUAL], ARROWS4 + accent(dot(8, 8, 1.75)))
icon("tool-move", "Text moving", ["Move.png"], [VISUAL],
     accent(circle(3.5, 8, 2)) + stroke("M6.5 8h8M12.5 6l2 2-2 2"))
icon("tool-scale", "Text scaling", ["Scale.png"], [VISUAL],
     rect(1.5, 8.5, 6, 6, 1) + stroke("M7.5 8.5l6-6M10 2.5h3.5V6") + accent(stroke("M1.5 5.5v-3a1 1 0 0 1 1-1h3M14.5 10.5v3a1 1 0 0 1-1 1h-3")))
icon("tool-rotate-z", "Text Z rotation", ["FRZ.png"], [VISUAL],
     stroke("M13.5 8a5.5 5.5 0 1 1-1.75-4") + stroke("M13.5 1.5v3h-3") + accent(dot(8, 8, 1.5)))
icon("tool-rotate-xy", "Text X / Y rotation", ["FRXY.png"], [VISUAL],
     stroke("M8 1.5v13") + stroke("M11 4.5c2.25.5 3.5 1.5 3.5 2.5 0 1.75-3 3-6.5 3S1.5 8.75 1.5 7c0-1.25 1.75-2.25 4.25-2.75")
     + accent(stroke("M4.25 3l1.5 1.25L4.5 5.75")))
icon("tool-perspective", "Perspective", [], [VISUAL],
     stroke("M4.5 3.5l8-1.5 2 11.5-12.5-1.5z") + stroke("M8.5 2.75L8.25 12.75M3.25 7.75l10.25-.5")
     + accent(dot(4.5, 3.5, 1.5), dot(12.5, 2, 1.5), dot(14.5, 13.5, 1.5), dot(2, 12, 1.5)))
icon("tool-clip-rect", "Rectangle clipping", ["cliprect.png"], [VISUAL],
     stroke("M4 2.5h8M4 13.5h8M2.5 4v8M13.5 4v8") + accent(frect(1, 1, 3, 3), frect(12, 1, 3, 3), frect(1, 12, 3, 3),
                                                       frect(12, 12, 3, 3)))
icon("tool-clip-vector", "Vector clipping", ["clip.png"], [VISUAL],
     stroke("M3 3.5l9.5 1.5 1 8-9 1.5z") + accent(frect(1.5, 2, 3, 3), frect(11, 3.5, 3, 3), frect(12, 11.5, 3, 3),
                                                  frect(3, 12.5, 3, 3)))
icon("tool-drawing", "Vector drawing", ["drawing.png"], [VISUAL],
     stroke("M8 1.5l4.5 6.5-1.5 6.5h-6L3.5 8z") + stroke("M8 1.5v6") + accent(dot(8, 9, 1.25)))
icon("tool-move-all", "Position shifter", ["MoveAll.png"], [VISUAL],
     rect(5.5, 5.5, 5, 5, 1) + stroke("M8 3.5v-2M6.5 3L8 1.5 9.5 3M8 12.5v2M6.5 13L8 14.5 9.5 13M3.5 8h-2M3 6.5L1.5 8 3 9.5M12.5 8h2M13 6.5l1.5 1.5-1.5 1.5")
     + accent(dot(8, 8, 1)))
icon("tool-all-tags", "Hydra (all tags)", ["AllTags.png"], [VISUAL],
     stroke("M4.5 2.5h-1a1 1 0 0 0-1 1v3l-1 1.5 1 1.5v3a1 1 0 0 0 1 1h1")
     + stroke("M11.5 2.5h1a1 1 0 0 1 1 1v3l1 1.5-1 1.5v3a1 1 0 0 1-1 1h-1")
     + accent(stroke("M8 5v6M5.5 6.5l5 3M10.5 6.5l-5 3")))
icon("tool-scale-rotation", "Scale and rotation shifter", ["ScaleRotation.png"], [VISUAL],
     circle(8, 8, 6) + accent(stroke("M5 11l6-6M7.5 5H11v3.5M8.5 11H5V7.5")))

# Vector drawing sub-tools
icon("vector-drag", "Move points", ["VectorDrag.png"], [VISUAL],
     accent(frect(6, 6, 4, 4)) + stroke("M6 3.5l2-2 2 2M6 12.5l2 2 2-2M3.5 6l-2 2 2 2M12.5 6l2 2-2 2"))
icon("vector-line", "Add line", ["VectorLine.png"], [VISUAL],
     stroke("M4 12l8-8") + accent(rect(1.5, 11.5, 3, 3, 0), rect(11.5, 1.5, 3, 3, 0)))
icon("vector-bezier", "Add Bézier curve", ["VectorBezier.png"], [VISUAL],
     stroke("M3 11.5C3 5 13 11 13 4.5") + stroke("M3 11.5l3-8M13 4.5l-3 8")
     + accent(rect(1.5, 11.5, 3, 3, 0), rect(11.5, 1.5, 3, 3, 0)))
icon("vector-bspline", "Add B-spline", ["VectorBspline.png"], [VISUAL],
     accent(stroke("M2.5 13.5l3-11 5 11 3-11")) + stroke("M2.5 13.5C4 6 5.5 5 8 8s4 2 5.5-5.5"))
icon("vector-point", "Add separate point", ["VectorMove.png"], [VISUAL],
     accent(frect(2, 9, 5, 5)) + stroke("M11.5 2.5v6M8.5 5.5h6"))
icon("vector-delete", "Delete point", ["VectorDelete.png"], [VISUAL],
     stroke("M1.5 13.5C4 9 6 9 8 11s4 2 6.5-2.5") + accent(stroke("M3 2.5l4 4M7 2.5l-4 4")))
# The drawing's shape presets (legacy's shape list, a text choice in the row:
# VideoToolbar.cpp:503-559; an icon with its menu since T5's review)
icon("shape-presets", "Shape presets", [], [VISUAL],
     circle(5.5, 5.5, 4) + stroke("M4.25 10.5l2.5 4h-5z") + accent(rect(8.5, 8.5, 6, 6, 1)))
# The clip kept outside the shape: the frame and the shape outlined, the
# kept region hatched in the accent (K1's outline style; the polish
# redrew the solid square of the first draft).
icon("clip-invert", "Invert clip", ["InvertClipIcon1.png"], [VISUAL],
     rect(1.5, 1.5, 13, 13, 1) + circle(8, 8, 3.5)
     + accent(stroke("M1.5 4l2.5-2.5M1.5 6.5l5-5M12 1.5l2.5 2.5M9.5 1.5l5 5M1.5 12l2.5 2.5M1.5 9.5l5 5M14.5 12L12 14.5M14.5 9.5l-5 5")))

# Position shifter sub-tools
icon("shift-position", "Move position points", ["MovePos.png"], [VISUAL],
     stroke("M8 2.5v11M2.5 8h11") + accent(dot(8, 8, 2)))
icon("shift-move-start", "Change \\move starting points", ["MoveMoveStart.png"], [VISUAL],
     accent(dot(3.5, 8, 2)) + stroke("M6.5 8h7M11.5 6l2 2-2 2"))
icon("shift-move-end", "Change \\move ending points", ["Move.png"], [VISUAL],
     circle(3.5, 8, 2) + stroke("M6.5 8h4") + accent(dot(13, 8, 2)))
icon("shift-clips", "Move clips", ["MoveClips.png"], [VISUAL],
     stroke("M3 1.5h7M3 9.5h7M1.5 3v5M11.5 3v5") + accent(stroke("M8 12.5h6.5M12.5 10.5l2 2-2 2")))
icon("shift-drawings", "Move drawings", ["MoveDrawings.png"], [VISUAL],
     stroke("M2 2.5l7 1.5-1 6-5.5-1z") + accent(stroke("M8 12.5h6.5M12.5 10.5l2 2-2 2")))
icon("shift-origins", "Move \\org points", ["MoveOrgs.png"], [VISUAL],
     accent(circle(4.5, 11.5, 2.5), stroke("M4.5 8.5v6M1.5 11.5h6")) + stroke("M6.5 8C7.5 5 10 3.5 13.5 3.5M11.5 1.5l2 2-2 2"))
# The all-tags tool's row (legacy's tag list, change options and Edit
# button, text controls in the row: VideoToolbar.cpp:737-799; icons with
# their menus since T6, as the drawing's shape list)
icon("tag-list", "Tag list", [], [VISUAL],
     stroke("M7.5 4.5h6M7.5 8h6M7.5 11.5h4") + accent(stroke("M2 2.5l3 11")))
icon("tag-change-option", "Tag change options", [], [VISUAL],
     stroke("M1.5 13.5h13l-13-6z") + accent(stroke("M11.5 1.5v5M9 4h5")))
icon("tag-edit", "Edit the tag list", [], [VISUAL],
     stroke("M2.5 13.5l1-3.5 7.5-7.5 2.5 2.5-7.5 7.5z") + stroke("M9.5 4l2.5 2.5") + accent(stroke("M2 2l2 5")))
icon("two-points", "Set angle from 2 points", ["TwoPoints.png"], [VISUAL],
     stroke("M3.5 12.5l9-8") + stroke("M1.5 12.5h13") + stroke("M8.5 12.5a5 5 0 0 0-1.25-3.25")
     + accent(dot(3.5, 12.5, 1.5), dot(12.5, 4.5, 1.5)))
# VT3: the Perspective tool's toggles
icon("perspective-plane", "Show surrounding plane", [], [VISUAL],
     stroke("M5.5 5.5l5-.75 1 5.75-6.75-.75z")
     + accent(stroke("M2.5 2.5h1.5M6 2l1.5-.25M10 1.25l2.5-.25.5 2.5M13.5 5.5l.5 2.75M14.5 10.5l.5 3-2.5-.25"
                     "M10 13l-2-.25M5.5 12.5l-2-.25-1.5-.25.25-2.5M1.5 7.5l.5-2.5")))
icon("perspective-lock-outer", "Lock outer quad", [], [VISUAL],
     stroke("M2.5 2.5l9-1 1 6M8 13.5l-6.5-.5 1-10.5")
     + accent(rect(9.5, 11.5, 5, 3.5, .5), stroke("M10.5 11.5v-1.25a1.5 1.5 0 0 1 3 0v1.25")))
icon("perspective-grid", "Show grid", [], [VISUAL],
     stroke("M4.5 2.5l7-.5 3 12.5-13 .25z")
     + accent(stroke("M6.75 2.25l-1.5 12.5M9.25 2.25l1.25 12.25M3.25 6.5l9.5-.5M2.25 10.5l11.5-.25")))
icon("frame-to-scale", "Set scale by rectangle", ["FrameToScale.png"], [VISUAL],
     stroke("M2.5 9.5v-6a1 1 0 0 1 1-1h8a1 1 0 0 1 1 1v3M9.5 12.5h-6a1 1 0 0 1-1-1")
     + accent(stroke("M12.5 9.5v5M10 12h5")))
icon("scale-x", "Scale width", ["ScaleX.png"], [VISUAL],
     stroke("M1.5 11.5h13M3.5 9.5l-2 2 2 2M12.5 9.5l2 2-2 2") + accent(stroke("M6 2.5l4 4.5M10 2.5L6 7")))
icon("scale-y", "Scale height", ["ScaleY.png"], [VISUAL],
     stroke("M4.5 1.5v13M2.5 3.5l2-2 2 2M2.5 12.5l2 2 2-2") + accent(stroke("M9 3.5l2.25 3 2.25-3M11.25 6.5v4")))
icon("original-frame", "Set a custom rectangle for the current scale", ["OriginalFrame.png"], [VISUAL],
     rect(1.5, 2.5, 13, 11, 1) + accent(stroke("M5 5.5h6M8 5.5v5")))

# Colour picker, history, tabs
icon("eyedropper", "Pick a colour from the screen", ["eyedropper.png"], [PICKER],
     stroke("M10.5 3.5l1.75-1.75a1.75 1.75 0 0 1 2.5 2.5L13 6") + stroke("M9 3l4 4")
     + stroke("M10 4l-6.5 6.5-1 3 3-1L12 6") + accent(stroke("M4.75 11.25l3.25-3.25")))
icon("tab-close", "Close tab", [], [TABS, FILE, "dock-headers"], stroke("M4.5 4.5l7 7M11.5 4.5l-7 7"))
icon("tab-new", "Open new tab", [], [TABS], stroke("M8 3v10M3 8h10"))
icon("document-modified", "Modified", [], [TABS], accent(dot(8, 8, 3)))

# UI polish (2026-10-05, the icon-gap analysis): the icon buttons that were
# glyph text ("⇈ ↑ ↓ ⇊", "+", "-", " ... "), the generic list and edit
# commands, and the commands that had no icon beside siblings that do.
FOLDER = stroke("M1.5 12.5v-9a1 1 0 0 1 1-1h3.5l1.5 1.5h6a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1z") + stroke(
    "M1.5 6h13")
FOLDER_CUT = stroke("M8.5 13.5h-6a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1h3.5l1.5 1.5h6a1 1 0 0 1 1 1v4") + stroke("M1.5 6h13")
LISTS, STYLEDLG, SEARCH, SHIFTPANEL, GRIDMENU, TEXTMENU, VIDEOMENU, TABMENU = (
    "style-manager", "style-manager", "search-tool", "shift-times-panel", "grid-menu", "text-field-menu",
    "video-context-menu", "tab-menu")
SCRIPT_CUT = page(True) + stroke("M6.5 6.5L5 8.25 6.5 10")

# Ordering (Style manager's move buttons; legacy drew arrow_list.png and,
# rotated, its double ARROW_LIST_DOUBLE beside the lists, stylestore.cpp:56-71)
icon("move-to-top", "Move to beginning", ["arrowListDouble.png"], [STYLEDLG],
     accent(stroke("M2.5 1.5h10")) + stroke("M7.5 14.5v-10M3 9l4.5-4.5L12 9"))
icon("move-up", "Move up", ["arrow_list.png"], [STYLEDLG], stroke("M7.5 13.5v-11M3 7l4.5-4.5L12 7"))
icon("move-down", "Move down", [], [STYLEDLG], stroke("M7.5 2.5v11M3 9l4.5 4.5L12 9"))
icon("move-to-bottom", "Move to end", [], [STYLEDLG],
     accent(stroke("M2.5 14.5h10")) + stroke("M7.5 1.5v10M3 7l4.5 4.5L12 7"))
# IC1: LE5's tag buttons move along their strip (the button's context menu):
# move-up's arrow turned, for "Move left" and "Move right" as they read, so
# they do not mirror (a panel's Move tab left is tab-move-left, a tab).
icon("move-left", "Move left", [], [EDITOR], stroke("M13.5 7.5h-11M7 3L2.5 7.5 7 12"))
icon("move-right", "Move right", [], [EDITOR], stroke("M2.5 7.5h11M9 3l4.5 4.5L9 12"))
# Transfers between the Style manager's two lists (legacy drew them with the
# same arrow_list.png as the moves, stylestore.cpp:73-88): the two lists as
# boxes, the copied style's direction between them in the accent, so a
# transfer never wears a move's arrow.
icon("copy-to-storage", "Copy style from ASS to storage", [], [STYLEDLG],
     rect(1.5, 1.5, 12, 3, 1) + rect(1.5, 11.5, 12, 3, 1)
     + accent(stroke("M7.5 10v-4M5 8.5l2.5-2.5L10 8.5")))
icon("copy-to-ass", "Copy style from storage to ASS", [], [STYLEDLG],
     rect(1.5, 1.5, 12, 3, 1) + rect(1.5, 11.5, 12, 3, 1)
     + accent(stroke("M7.5 6v4M5 7.5L7.5 10 10 7.5")))
icon("copy-to-all-ass", "Copy style from storage to every open ASS file", [], [STYLEDLG],
     rect(1.5, 1.5, 12, 3, 1) + rect(1.5, 12.5, 10, 2, 1) + stroke("M3.5 10.5h9a1 1 0 0 1 1 1v1")
     + accent(stroke("M7.5 6v3M5 6.5L7.5 9 10 6.5")))
# A double chevron: a list that drops open. LE5 retired it from the Line
# editor's "Manage tag buttons" list (the "+" popover and the buttons'
# context menu replaced it); WS3's layout switcher keeps it.
icon("menu-more", "Layouts", [], ["status-bar"], stroke("M4 3.5l4 3.5 4-3.5M4 8.5l4 3.5 4-3.5"))
# D3: a panel header's "⋯" menu (MuseScore's MenuButton): three dots in a
# row.
icon("panel-menu", "Panel options", [], ["dock-headers"], dot(3.5, 8, 1.25) + dot(8, 8, 1.25) + dot(12.5, 8, 1.25))

# Generic list and edit commands
icon("add", "Add", [], [SHIFTPANEL, LISTS, "font-catalog", GRIDMENU], stroke("M7.5 2.5v10M2.5 7.5h10"))
icon("remove", "Remove", [], [SHIFTPANEL, EDITOR, AUTOPANEL], stroke("M2.5 7.5h10"))
icon("edit", "Edit", [], [EDITOR, LISTS, GRIDMENU, VISUAL, AUTOPANEL],
     stroke("M11 2l3 3-8.5 8.5-3.5 1 1-3.5z") + accent(stroke("M9.5 3.5l3 3")))
icon("duplicate", "Copy", [], [LISTS],
     stroke("M5.5 4.5v-2a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1h-2") + rect(2.5, 4.5, 8, 10, 1)
     + accent(stroke("M6.5 7.5v4M4.5 9.5h4")))
icon("delete", "Delete", [], [LISTS, GRIDMENU, TEXTMENU],
     stroke("M2.5 4h11M6 4V2.5h4V4") + stroke("M4 4.5l.75 9a1 1 0 0 0 1 1h4.5a1 1 0 0 0 1-1l.75-9")
     + stroke("M6.75 7v5M9.25 7v5"))
icon("import", "Load", [], [LISTS, FILE],
     stroke("M5.5 9.5h-3a1 1 0 0 1-1-1v-6a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1h-3")
     + accent(stroke("M7.5 4.5v10M4.5 11.5l3 3 3-3")))
icon("clear", "Clear", [], [LISTS, VISUAL, GRIDMENU, AUTOPANEL],
     stroke("M9 2l5 5-6.5 6.5-5-5z") + accent(stroke("M5.5 5.5l5 5")) + stroke("M7.5 13.5h6"))
icon("refresh", "Refresh", [], ["font-catalog", AUTOPANEL],
     stroke("M2.5 8a5.5 5.5 0 0 1 9.75-3.5") + stroke("M12.5 1.5v3h-3")
     + stroke("M13.5 8a5.5 5.5 0 0 1-9.75 3.5") + stroke("M3.5 14.5v-3h3"))
icon("filter", "Filter", [], [GRIDMENU, "font-catalog"], stroke("M1.5 2.5h13L9.5 8.5V13l-3 1.5v-6z"))
icon("edit-copy", "Copy", [], [GRIDMENU, TEXTMENU],
     stroke("M5.5 4.5v-2a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1h-2") + rect(2.5, 4.5, 8, 10, 1))
icon("edit-cut", "Cut", [], [GRIDMENU, TEXTMENU],
     circle(4.5, 12, 2) + circle(11.5, 12, 2) + stroke("M5.75 10.5L11.5 1.5M10.25 10.5L4.5 1.5"))
icon("edit-paste", "Paste", [], [GRIDMENU, TEXTMENU],
     stroke("M5.5 2.5h-2a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1v-10a1 1 0 0 0-1-1h-2") + rect(5.5, 1.5, 5, 2, 0.5)
     + accent(stroke("M5.5 7.5h5M5.5 10h5M5.5 12.5h3")), mirror=True)

# Files and folders
icon("folder-open", "Choose a folder", [], [SEARCH, "font-collector", "settings-import"],
     stroke("M1.5 13.5v-10a1 1 0 0 1 1-1h3l1.5 1.5h5a1 1 0 0 1 1 1v1.5") + stroke("M1.5 13.5l2.25-7h10.75l-2.25 7z"))
icon("find-in-files", "Find in subtitles", [], [SEARCH], FOLDER_CUT + accent(circle(11.5, 11.5, 2),
                                                                             stroke("M13 13l1.5 1.5")))
icon("show-in-folder", "Open the containing folder", [], [TABMENU],
     FOLDER_CUT + accent(stroke("M10 14.5l4.5-4.5M11.5 10h3v3")))
icon("compare", "Subtitle comparison", [], [TABMENU, "dock-headers"],
     rect(1.5, 2.5, 6, 11, 1) + rect(8.5, 2.5, 6, 11, 1) + stroke("M3.5 5.5h2M3.5 8h2M3.5 10.5h2M10.5 5.5h2M10.5 8h2")
     + accent(stroke("M10.5 10.5h2")))
icon("reference", "Show subtitles preview", [], [GRIDMENU],
     page(True) + accent(rect(9.5, 11, 5, 3.5, 0.5), stroke("M10.75 11V9.75a1.25 1.25 0 0 1 2.5 0V11")))
icon("extract-subtitles", "Load subtitles from an MKV file", [], [GRIDMENU],
     film(1.5, 1.5, 9, 7) + stroke("M2.5 11.5h5M2.5 14h3.5") + accent(stroke("M9.5 12.5h5M12.5 10.5l2 2-2 2")))
icon("multireplace", "Fix minor errors", [], [SUBS, "misspell-replacer"],
     stroke("M1.5 2.5h11M1.5 6h8M1.5 9.5h5")
     + accent(stroke("M8.5 9h6M12.75 7.25L14.5 9l-1.75 1.75"), stroke("M14.5 13h-6M10.25 11.25L8.5 13l1.75 1.75")),
     mirror=True)

# Video
# The x crosses at 12.5, 13 so that each arm runs through the points Qt's
# antialiased one-pixel lines draw sharp (a half unit across, a whole unit
# down): at 16 px both arms are single pixels, as heavy as open-video's +.
icon("close-video", "Unload video", [], [VIDEO], video_film() + accent(stroke("M10.5 11l4 4M14.5 11l-4 4")))
icon("fullscreen", "Full screen", [], [VIDEOMENU],
     stroke("M1.5 5.5v-3a1 1 0 0 1 1-1h3M10.5 1.5h3a1 1 0 0 1 1 1v3M14.5 10.5v3a1 1 0 0 1-1 1h-3M5.5 14.5h-3a1 1 0 0 1-1-1v-3"))
icon("fullscreen-exit", "Exit full screen", [], [VIDEOMENU, FULLSCREEN],
     stroke("M5.5 1.5v3a1 1 0 0 1-1 1h-3M10.5 1.5v3a1 1 0 0 0 1 1h3M14.5 10.5h-3a1 1 0 0 0-1 1v3M1.5 10.5h3a1 1 0 0 1 1 1v3"))
icon("frame-snapshot", "Save frame as PNG", [], [VIDEOMENU],
     stroke("M1.5 5.5a1 1 0 0 1 1-1h2l1.5-2h4l1.5 2h2a1 1 0 0 1 1 1v7a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1z")
     + accent(circle(8, 8.5, 2.5)))
icon("zoom-reset", "Turn off video zoom", [], [VIDEO],
     magnifier(6.5, 6.5, 4.5, 4) + accent(stroke("M5 5l3 3M8 5L5 8")))
icon("volume", "Volume", [], [TRANSPORT, FULLSCREEN, AUDIOBOX],
     stroke("M1.5 6h2.5l3.5-3v10L4 10H1.5z") + accent(stroke("M10 5.5a3.5 3.5 0 0 1 0 5M12 3.5a6.25 6.25 0 0 1 0 9")))
# FS1 (#275): the fullscreen speaker while muted (FS1-speaker-gestures).
icon("volume-muted", "Unmute", [], [FULLSCREEN, TRANSPORT],
     stroke("M1.5 6h2.5l3.5-3v10L4 10H1.5z") + accent(stroke("M10.5 6l4 4M14.5 6l-4 4")))

# Line editor, reference tray, automation
icon("pick-lines", "Pick selected lines", [], [VISUAL],
     stroke("M1.5 4.5h7M1.5 8h7M1.5 11.5h7") + accent(circle(12.5, 4.5, 2), stroke("M12.5 6.5v7")), mirror=True)
icon("match-previous", "Previous match", [], ["reference-tray"], stroke("M3.5 10L8 5.5l4.5 4.5"))
icon("match-next", "Next match", [], ["reference-tray"], stroke("M3.5 6L8 10.5 12.5 6"))
icon("unload", "Unload", [], [AUTOPANEL], SCRIPT_CUT + cross_badge())
icon("run-script", "Run the last loaded script", [], [AUTO, AUTOPANEL],
     SCRIPT_CUT + accent(solid("M10 9.5v5l4.5-2.5z")))

# Options pages (the depth-0 pages; Editor and Subtitle properties take the
# editor and script-properties roles)
icon("settings-video", "Video", [], ["settings-dialog"], film(1.5, 3, 13, 10))
icon("settings-audio", "Audio", [], ["settings-dialog"], waveform(2, 3.5, 12.5) + stroke("M14.5 6.5v3"))
icon("appearance", "Appearance", [], ["settings-dialog"],
     circle(8, 8, 6.5) + accent(fill("M8 1.5a6.5 6.5 0 0 1 0 13z")))
icon("hotkeys", "Hotkeys", [], ["settings-dialog", AUTOPANEL],
     rect(1.5, 3.5, 13, 9, 1) + stroke("M4 6.5h1M7.5 6.5h1M11 6.5h1M4 9.5h1M11 9.5h1") + accent(stroke("M6.5 9.5h3")))


# Audio panel, wave 6 (#213): the output device menu (AV1 #238), a speaker
# cabinet so that volume's cone keeps its one meaning; the cursor sync toggle
# (AV2 #239), one playhead through a video frame and a waveform.
icon("audio-output", "Output device", [], [AUDIOBOX],
     rect(3.5, 1.5, 9, 13, 1) + circle(8, 4.75, 1.25) + accent(circle(8, 10.25, 2.75)))
icon("audio-video-sync", "Sync the video and audio cursors", [], [AUDIOBOX],
     rect(1.5, 1.5, 13, 5, 1) + stroke("M2.5 11.25v1.5M4.5 10v4M6.5 10.75v2.5M10.5 10.5v3M12.5 11.25v1.5")
     + accent(stroke("M8.5 1v14")))
# AU1 (#267): the audio panel's mode switches and the speaker's level.
# Line timing is one span between its start and end boundaries; the waveform
# is bars mirrored about the middle (the spectrum's stand on a baseline).
# The speaker keeps volume's body and shows the level in its waves: none at
# the quietest, one, then volume's two (the volume never reaches 0, so there
# is no muted icon).
icon("audio-line-mode", "Line timing mode", [], [AUDIOBOX],
     stroke("M2.5 3.5v9M13.5 3.5v9") + accent(frect(4, 6.5, 8, 3, 0.5)))
icon("audio-waveform", "Waveform mode", [], [AUDIOBOX],
     stroke("M1.5 7v2M3.5 5v6M5.5 2.5v11M7.5 5.5v5M9.5 3.5v9M11.5 6v4M13.5 4.5v7"))
icon("volume-quiet", "Volume", [], [AUDIOBOX], stroke("M1.5 6h2.5l3.5-3v10L4 10H1.5z"))
icon("volume-low", "Volume", [], [AUDIOBOX],
     stroke("M1.5 6h2.5l3.5-3v10L4 10H1.5z") + accent(stroke("M10 5.5a3.5 3.5 0 0 1 0 5")))
# KA2 (#277): tap-to-time and slowed playback. Tap is a key pressed down,
# the latency calibration a metronome, the playback speed a gauge.
icon("karaoke-tap", "Tap to time", [], [AUDIOBOX],
     rect(2.5, 9.5, 11, 4, 1) + accent(stroke("M8 1.5v5.5M5.5 4.5L8 7l2.5-2.5")))
icon("tap-calibrate", "Calibrate tap latency", [], [AUDIOBOX],
     stroke("M5.5 2.5h5l2.5 11h-10z") + stroke("M4 10.5h8") + accent(stroke("M8 10.5l3.5-6.5")))
icon("playback-speed", "Playback speed", [], [AUDIOBOX],
     stroke("M2.5 12a5.5 5.5 0 1 1 11 0") + accent(stroke("M8 12L5.5 8")) + dot(8, 12, 1.25))

# IC1 (#226): every menu and context-menu item that runs a command has an
# icon (docs/ux/menu-icon-gaps.md lists the items and the ones left
# without: check items, entries named by their data). The rules above hold:
# an open/add badge is the plus, recent and timed the clock, a removal the
# cross; a list's rows are lines, the Grid's tree a parent row with
# indented children.
PANELS = "dock-headers"
ROWS3 = stroke("M1.5 3.5h7M1.5 8h7M1.5 12.5h7")
EYE = stroke("M1.5 8C3 5 5.25 3.5 8 3.5s5 1.5 6.5 4.5c-1.5 3-3.75 4.5-6.5 4.5S3 11 1.5 8z")
TRASH_CUT = (stroke("M1.5 3.5h9M4.5 3.5V2h3v1.5") + stroke("M2.5 4.5l.75 9a1 1 0 0 0 1 1H8M9.5 4.5l-.25 3.5")
             + stroke("M5 6.5v5M7 6.5v3"))
SESSION_CUT = (stroke("M5.5 6.5v-4h9v5") + stroke("M8.5 13.5h-6a1 1 0 0 1-1-1v-5a1 1 0 0 1 1-1h9a1 1 0 0 1 1 1v1")
               + stroke("M1.5 8.5h11"))
LAYOUT_CUT = (stroke("M8.5 13.5h-6a1 1 0 0 1-1-1v-9a1 1 0 0 1 1-1h11a1 1 0 0 1 1 1v5") + stroke("M1.5 6h13M6 6v7.5"))
BOOK = "M3.5 13V3a1.5 1.5 0 0 1 1.5-1.5h7.5v10H5a1.5 1.5 0 0 0 0 3h7.5"
TREE_ROWS = stroke("M1.5 3.5h8M7 8h7.5M7 12.5h7.5")
TREE_LINKS = stroke("M3.5 5.5v7h2M3.5 8h2")
LINE_BARS = "M7 4.5H2.5a1 1 0 0 0-1 1v5a1 1 0 0 0 1 1H7M8.5 4.5h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-5"
COPY_BACK = stroke("M5.5 4.5v-2a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1h-2")
PASTE_BOARD = (stroke("M5.5 2.5h-2a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1v-10a1 1 0 0 0-1-1h-2")
               + rect(5.5, 1.5, 5, 2, 0.5))
FPS_FILM = film(1.5, 1.5, 13, 6, 3)
REFRESH_BADGE = accent(stroke("M14.5 12a2.5 2.5 0 1 1-.75-1.75"), stroke("M14 9v1.5h-1.5"))
SWAP = accent(stroke("M10.5 12.5v-9M9.5 4.5l1-1 1 1M13.5 3.5v9M12.5 11.5l1 1 1-1"))

# File menu
icon("autosave", "Open auto save", [], [FILE], floppy(True) + clock_badge())
icon("remove-temporary", "Remove temporary files", [], [FILE], TRASH_CUT + clock_badge())
icon("log", "Show / Hide log window", [], [FILE],
     rect(1.5, 2.5, 13, 11, 1) + stroke("M7 5.5h5M7 8h4M7 10.5h5") + accent(stroke("M4 5.5h1.5M4 8h1.5M4 10.5h1.5")),
     mirror=True)
icon("session-open", "Load session from file", [], [FILE], SESSION_CUT + plus_badge())
icon("session-save", "Save session to file", [], [FILE],
     floppy(True) + accent(stroke("M11.5 9.5h3v3"), rect(9.5, 11, 3.5, 3.5, 0.5)))

# Automation menu
icon("rerun-macro", "Rerun last macro", [], [AUTO],
     SCRIPT_CUT + REFRESH_BADGE)
# AD2: Repeat last run, the script page with a repeat loop.
icon("repeat-run", "Repeat last run", [], [AUTO, AUTOPANEL],
     SCRIPT_CUT + accent(stroke("M9.5 11.5v-.5A1.5 1.5 0 0 1 11 9.5h3.5"), stroke("M13.5 8.5l1 1-1 1"),
                         stroke("M14.5 12.5v.5A1.5 1.5 0 0 1 13 14.5H9.5"), stroke("M10.5 13.5l-1 1 1 1")))
icon("automation-manager", "Show panel", [], [AUTO],
     stroke("M4.5 3.5v-2h6l3 3v8h-2") + stroke("M10.5 14.5h-9v-10h6l3 3z")
     + accent(stroke("M5 9L3.75 10.25 5 11.5M7 9l1.25 1.25L7 11.5")))

# KE2: the Template editor (View > Panels, the video's preview badge), its
# header's Loop the Line and a template's problem (drawn in the danger colour).
icon("template-editor", "Template editor", [], [VIEW, TEMPLATEPANEL],
     stroke("M4.5 1.5h8v10") + rect(2.5, 3.5, 8, 11, 0.5)
     + accent(stroke("M5.5 6.5c-.75 0-.75.5-.75 1.5s-.5 1-1 1c.5 0 1 0 1 1s0 1.5.75 1.5"
                     "M7.5 6.5c.75 0 .75.5.75 1.5s.5 1 1 1c-.5 0-1 0-1 1s0 1.5-.75 1.5")))
icon("loop", "Loop the Line", [], [TEMPLATEPANEL],
     stroke("M2.5 9V7.5a3 3 0 0 1 3-3h7M10.5 2.5l2 2-2 2")
     + accent(stroke("M13.5 7v1.5a3 3 0 0 1-3 3h-7M5.5 13.5l-2-2 2-2")))
icon("problem", "Problem", [], [TEMPLATEPANEL],
     circle(8, 8, 6) + stroke("M8 4.75v4") + dot(8, 11.25, 0.75))

# Video menu and the video's context menu
icon("dummy-video", "Open dummy video", [], [VIDEO], video_film() + accent(frect(10, 10, 4.5, 4.5, 0.5)))
icon("video-streams", "Streams", [], [VIDEO, VIDEOMENU],
     stroke("M1.5 3.5h6M1.5 7h6M1.5 10.5h3") + accent(note(10.5)))
icon("chapters", "Chapters", [], [VIDEO, VIDEOMENU],
     stroke("M1.5 13.5h13M3.5 13.5v-2M12.5 13.5v-2") + accent(stroke("M6.5 13.5v-10h5l-1.5 2 1.5 2h-5")))
icon("copy-position", "Copy video position", [], [VIDEOMENU],
     COPY_BACK + rect(2.5, 4.5, 8, 10, 1)
     + accent(circle(6.5, 9.5, 1.5), stroke("M6.5 6.5v1.5M6.5 11v1.5M3.5 9.5H5M8 9.5h1.5")))
icon("monitor", "Open in full screen on another monitor", [], [VIDEOMENU, FULLSCREEN],
     rect(1.5, 2.5, 13, 9, 1) + stroke("M6.5 11.5v2M9.5 11.5v2M4.5 13.5h7")
     + accent(stroke("M3.5 6v-1.5h2M12.5 6v-1.5h-2M3.5 8v1.5h2M12.5 8v1.5h-2")))
icon("progress-bar", "Show / hide progress bar", [], [VIDEOMENU, FULLSCREEN],
     rect(1.5, 2.5, 13, 11, 1) + stroke("M9.5 10.5h3") + accent(stroke("M3.5 10.5h4"), dot(8.5, 10.5, 1.25)))
# KE1-render-cost-overlay: a stopwatch over the picture's subtitles
icon("render-cost", "Show / hide render cost", [], [VIDEO, VIDEOMENU, FULLSCREEN],
     circle(8, 9, 5.5) + stroke("M8 3.5V1.5M6.5 1.5h3M12.5 3.5l1 1") + accent(stroke("M8 9l2.5-2.5"), dot(8, 9, 1)))
icon("aspect-ratio", "Change aspect ratio", [], [VIDEOMENU, FULLSCREEN],
     rect(1.5, 3.5, 13, 9, 1) + accent(stroke("M4 8h8M5.5 6.5L4 8l1.5 1.5M10.5 6.5L12 8l-1.5 1.5")))
icon("video-filters", "Filters", [], [VIDEOMENU, FULLSCREEN],
     rect(1.5, 6, 3.5, 4, 1) + rect(6.25, 6, 3.5, 4, 1) + rect(11, 6, 3.5, 4, 1)
     + accent(stroke("M5 8h1.25M9.75 8H11")))

# View menu and the panels' menus
icon("panels", "Panels", [], [VIEW], layout(stroke("M6.5 2.5v11M6.5 8h8")))
icon("show", "Show", [], [VIEW], EYE + accent(dot(8, 8, 2)))
icon("hide", "Hide", [], [VIEW, GRIDMENU], EYE + circle(8, 8, 2) + accent(stroke("M2.5 13.5l11-11")))
icon("panel-float", "Float", [], [VIEW, PANELS],
     stroke("M8.5 2.5h-5a1 1 0 0 0-1 1v9a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1v-5") + accent(stroke("M9.5 6.5l5-5M10.5 1.5h4v4")))
icon("panel-dock", "Dock", [], [VIEW, PANELS],
     stroke("M8.5 2.5h-5a1 1 0 0 0-1 1v9a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1v-5") + accent(stroke("M14.5 1.5l-5 5M9.5 2.5v4h4")))
icon("panel-move", "Move panel", [], [VIEW, PANELS],
     layout(stroke("M6.5 2.5v11")) + accent(stroke("M3.5 8h8M9.5 6l2 2-2 2")))
icon("tab-move-left", "Move tab left", [], [PANELS],
     stroke("M1.5 13.5h13M4.5 13.5v-5a1 1 0 0 1 1-1h5a1 1 0 0 1 1 1v5") + accent(stroke("M11.5 3.5h-8M5.5 1.5l-2 2 2 2")))
icon("tab-move-right", "Move tab right", [], [PANELS],
     stroke("M1.5 13.5h13M4.5 13.5v-5a1 1 0 0 1 1-1h5a1 1 0 0 1 1 1v5") + accent(stroke("M4.5 3.5h8M10.5 1.5l2 2-2 2")))
icon("layout-restore", "Restore the previous layout", [], [VIEW],
     LAYOUT_CUT + accent(stroke("M10.5 9.5L9 11l1.5 1.5"), stroke("M9 11h3.5a1.75 1.75 0 0 1 0 3.5H11")), mirror=True)

# Document tab menu, font catalogs
icon("tab-close-all", "Close all tabs", [], [TABMENU],
     stroke("M4.5 3.5v-1a1 1 0 0 1 1-1h8a1 1 0 0 1 1 1v8a1 1 0 0 1-1 1h-1") + rect(1.5, 4.5, 10, 10, 1)
     + accent(stroke("M4.5 7.5l4 4M8.5 7.5l-4 4")))
icon("fonts-from-subtitles", "Add fonts from subtitles", [], ["font-catalog"], page(True) + accent(letters("F", 11, 10)))

# Text fields' menu
icon("dictionary", "Installed languages", [], [TEXTMENU, "settings-dialog"],
     stroke(BOOK) + accent(stroke("M6.5 4.5h3.5M6.5 6.5h2")))
icon("dictionary-add", "Add word to dictionary", [], [TEXTMENU],
     stroke("M3.5 13V3a1.5 1.5 0 0 1 1.5-1.5h7.5v6") + stroke("M8 11.5H5a1.5 1.5 0 0 0 0 3h3")
     + stroke("M6.5 4.5h3.5M6.5 6.5h2") + plus_badge())

# Grid menu and the tree's menu
icon("make-tree", "Make tree", [], [GRIDMENU], TREE_ROWS + accent(TREE_LINKS), mirror=True)
icon("tree-add-lines", "Add lines", [], [GRIDMENU],
     stroke("M1.5 3.5h8M7 8h7.5M7 12.5h1") + TREE_LINKS + plus_badge(), mirror=True)
icon("tree-select", "Select tree lines", [], [GRIDMENU],
     stroke("M4 3.5h7M8.5 8h6M8.5 12.5h6M5.5 5.5v7h1.5M5.5 8h1.5") + accent(stroke("M1.5 2.5v11")), mirror=True)
icon("insert-before", "Insert before", [], [GRIDMENU],
     stroke("M1.5 8.5h13M1.5 12.5h13") + accent(stroke("M1.5 3.5h7M12.5 1.5v4M10.5 3.5h4")), mirror=True)
icon("insert-after", "Insert after", [], [GRIDMENU],
     stroke("M1.5 3.5h13M1.5 7.5h13") + accent(stroke("M1.5 12.5h7M12.5 10.5v4M10.5 12.5h4")), mirror=True)
icon("swap-lines", "Swap", [], [GRIDMENU], stroke("M1.5 4h6M1.5 12h6") + SWAP, mirror=True)
icon("join-lines", "Join lines", [], [GRIDMENU],
     stroke("M1.5 3.5h3l3.5 4.5M1.5 12.5h3L8 8") + accent(stroke("M8 8h6.5")), mirror=True)
icon("join-keep-first", "Join lines and keep first", [], [GRIDMENU],
     stroke("M1.5 12.5h3L8 8") + accent(stroke("M1.5 3.5h3l3.5 4.5h6.5")), mirror=True)
icon("join-keep-last", "Join lines and keep last", [], [GRIDMENU],
     stroke("M1.5 3.5h3L8 8") + accent(stroke("M1.5 12.5h3L8 8h6.5")), mirror=True)
icon("continuous-previous", "Set times as a continuous (previous line)", [], [GRIDMENU],
     rect(1.5, 4.5, 4, 7, 1) + rect(10.5, 4.5, 4, 7, 1) + accent(stroke("M9.5 8h-3M8 6.5L6.5 8 8 9.5")))
icon("continuous-next", "Set times as a continuous (next line)", [], [GRIDMENU],
     rect(1.5, 4.5, 4, 7, 1) + rect(10.5, 4.5, 4, 7, 1) + accent(stroke("M6.5 8h3M8 6.5L9.5 8 8 9.5")))
icon("split-lines", "Split lines", [], [GRIDMENU], stroke(LINE_BARS) + accent(stroke("M7.75 2v12")))
icon("split-at-video", "Split line at video time", [], [GRIDMENU],
     stroke(LINE_BARS) + accent(stroke("M7.75 4v10"), solid("M6.25 1.5h3L7.75 3z")))
icon("split-frames", "Split lines into frames", [], [GRIDMENU],
     stroke("M1.5 2.5h13M1.5 13.5h13") + rect(1.5, 4.5, 3, 7, 1) + rect(6.5, 4.5, 3, 7, 1) + rect(11.5, 4.5, 3, 7, 1)
     + accent(stroke("M5.5 2.5v11M10.5 2.5v11")))
icon("split-characters", "Split lines into characters", [], [GRIDMENU],
     letters("A", 2, 5) + letters("S", 10, 5) + accent(stroke("M7.5 2.5v11")))
icon("split-words", "Split lines into words", [], [GRIDMENU],
     stroke("M1.5 5.5h4M9.5 5.5h5M1.5 10.5h4M9.5 10.5h3") + accent(stroke("M7.5 2.5v11")), mirror=True)
icon("split-wraps", "Split lines by wraps", [], [GRIDMENU],
     stroke("M1.5 3.5h9M1.5 12.5h7") + accent(stroke("M10.5 3.5H12a2.5 2.5 0 0 1 0 5H6.5M8 7L6.5 8.5 8 10")), mirror=True)
icon("select-visible", "Select all lines visible on video", [], [GRIDMENU],
     rect(1.5, 1.5, 13, 8, 1) + triangle(6.5, 3.5, 3, 4) + stroke("M5.5 12h9M5.5 14.5h6")
     + accent(frect(1.5, 11.25, 2.5, 1.5), frect(1.5, 13.75, 2.5, 1.5)), mirror=True)
icon("paste-translation", "Paste translation text", [], [GRIDMENU, "original-panel"],
     stroke("M5.5 2.5h-2a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h5M10.5 2.5h2a1 1 0 0 1 1 1v5") + rect(5.5, 1.5, 5, 2, 0.5)
     + stroke("M5.5 7.5h5M5.5 10h2.5") + accent(stroke("M10 10.5h4.5M12.25 10.5v4")))
icon("translation-shift", "Dialogue shifting window", [], [GRIDMENU],
     ROWS3 + accent(stroke("M11.5 3v10M10 4.5l1.5-1.5L13 4.5M10 11.5l1.5 1.5 1.5-1.5")), mirror=True)
icon("filter-off", "Turn off filtering", [], [GRIDMENU],
     stroke("M9.5 8.5l5-6h-13l5 6v5l1.5-.75") + cross_badge())
icon("columns", "Hide columns", [], [GRIDMENU],
     rect(1.5, 2.5, 13, 11, 1) + stroke("M1.5 5.5h13M5.5 5.5v8M10.5 5.5v8") + accent(frect(6, 3, 4, 2)))
icon("columns-copy", "Copy columns", [], [GRIDMENU],
     COPY_BACK + rect(2.5, 4.5, 8, 10, 1) + accent(stroke("M5.5 4.5v10M7.5 4.5v10")))
icon("columns-paste", "Paste columns", [], [GRIDMENU], PASTE_BOARD + accent(stroke("M6.5 6v6.5M9.5 6v6.5")))
icon("fps", "Set new FPS", [], [GRIDMENU, "settings-dialog"], FPS_FILM + accent(letters("FPS", 2, 9)))
icon("fps-from-video", "Set FPS from video", [], [GRIDMENU],
     FPS_FILM + letters("FPS", 2, 9) + accent(stroke("M8 2.5v3.5M6.75 4.75L8 6l1.25-1.25")))

# Tab menu's comparison, Subtitles menu
icon("compare-off", "Turn off comparison", [], [TABMENU],
     rect(1.5, 2.5, 6, 11, 1) + stroke("M3.5 5.5h2M3.5 8h2M3.5 10.5h2") + stroke("M8.5 8.5v-5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v5")
     + cross_badge())
icon("shift-times-run", "Shift times / run time post processor", [], [SUBS],
     circle(6.5, 6.5, 5) + stroke("M6.5 4v2.5l2 1.5") + accent(solid("M11 10.5v4l3.5-2z")))

# Options: the primary options of each page (PF1 #225 places them; the
# page list keeps the page icons above, and options shared with a menu take
# its icon: dictionary, autosave, fps, styles, resample, zoom, lead-in).
icon("language", "Language", [], ["settings-dialog"],
     circle(8, 8, 6.5) + stroke("M8 1.5c-2 1.75-3 4-3 6.5s1 4.75 3 6.5c2-1.75 3-4 3-6.5s-1-4.75-3-6.5z")
     + accent(stroke("M1.5 8h13")))
icon("ui-font", "Program font", [], ["settings-dialog"],
     stroke("M1.5 13.5L5 2.5h1l3.5 11M3 9.5h5") + accent(circle(11.5, 11.5, 2), stroke("M13.5 9.5v4")))
icon("renderer", "Subtitle display filter", [], ["settings-dialog"],
     rect(1.5, 2.5, 13, 11, 1) + triangle(6.5, 4.5, 3, 3.5) + accent(stroke("M4 9.5h8M5.5 11.5h5")))
icon("video-seek", "Video seeking method", [], ["settings-dialog"],
     stroke("M1.5 12.5h13") + stroke("M2.5 7.5h5M5.5 5.5l2 2-2 2") + accent(stroke("M10.5 4.5v8"), solid("M9 2.5h3l-1.5 2z")))
icon("audio-device", "Audio output", [], ["settings-dialog"],
     rect(3.5, 1.5, 9, 13, 1) + circle(8, 10, 2.5) + accent(dot(8, 5, 1.25)))
icon("audio-delay", "Audio delay", [], ["settings-dialog"],
     stroke("M1.5 7v2M3.5 5v6M5.5 6v4M7.5 4.5v7") + clock_badge())
icon("theme", "Theme", [], ["settings-dialog"],
     stroke("M13.5 10A6 6 0 1 1 6 2.5a5 5 0 0 0 7.5 7.5z") + accent(stroke("M11.5 1.5v2M10.5 2.5h2")))
icon("accent-colour", "Accent color", [], ["settings-dialog"],
     stroke("M8 1.5c2.5 3.5 5 6 5 8.5a5 5 0 0 1-10 0C3 7.5 5.5 5 8 1.5z") + accent(fill("M4.5 10a3.5 3.5 0 0 0 7 0z")))
icon("ui-colours", "UI colors", [], ["settings-dialog"],
     rect(1.5, 2.5, 4, 4, 0.5) + rect(1.5, 9.5, 4, 4, 0.5) + stroke("M8 4.5h6.5M8 11.5h6.5")
     + accent(frect(2, 3, 3, 3), frect(2, 10, 3, 3)), mirror=True)
icon("restore-default", "Restore default", [], ["settings-dialog"],
     stroke("M2.5 8a5.5 5.5 0 1 0 1.75-4") + stroke("M2.5 1.5v3h3") + accent(dot(8, 8, 1.5)), mirror=True)
# PF1's pages and rows without an icon above: the Subtitle grid page, the
# hidden questions' row.
icon("settings-grid", "Subtitle grid", [], ["settings-dialog"],
     rect(1.5, 2.5, 13, 11, 1) + stroke("M1.5 5.5h13M5.5 2.5v11") + accent(stroke("M7.5 8.25h5M7.5 10.75h3")),
     mirror=True)
icon("notices", "Notices", [], ["settings-dialog"],
     rect(1.5, 5.5, 13, 8, 1) + stroke("M4 8.5h6M4 10.75h4") + accent(stroke("M3.5 3.5h9")), mirror=True)

# Translation mode (TM1 #232): the Original text panel's actions and the
# Subtitles menu's switch. Drawn by hand for TM1 and brought into the set
# here, so that running this script keeps them.
icon("paste-selection-translation", "Paste the selected", [], ["original-panel"],
     stroke("M5.5 2.5h-2a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h9a1 1 0 0 0 1-1v-10a1 1 0 0 0-1-1h-2")
     + rect(5.5, 1.5, 5, 2, 0.5) + stroke("M5.5 7.5h5M5.5 12.5h3") + accent(frect(5, 9, 6, 2.25)))
icon("comment-original", "Comment out original", [], ["original-panel"],
     stroke("M5 2.5c-1 0-1.5.5-1.5 1.5v2.5c0 .75-.5 1.5-1.5 1.5 1 0 1.5.75 1.5 1.5v2.5c0 1 .5 1.5 1.5 1.5"
            "M11 2.5c1 0 1.5.5 1.5 1.5v2.5c0 .75.5 1.5 1.5 1.5-1 0-1.5.75-1.5 1.5v2.5c0 1-.5 1.5-1.5 1.5")
     + accent(stroke("M6 6.5h4M6 9.5h3")), mirror=True)
icon("unconfirmed", "Not confirmed", [], ["original-panel"],
     stroke("M3.5 14.5v-13M3.5 2.5h9l-2 3 2 3h-9")
     + accent(stroke("M6.5 4.25a1.5 1.5 0 1 1 2 1.25c-.5.25-.5.5-.5 1"),
              '<circle cx="8" cy="7.75" r="0.25" fill="currentColor"/>'))
icon("moving-tags", "Moving tags", [], ["original-panel"],
     stroke("M3.5 3.5c-1 0-1.5.5-1.5 1.5v1c0 .75-.5 1-1 1 .5 0 1 .25 1 1v1c0 1 .5 1.5 1.5 1.5"
            "M6.5 3.5c1 0 1.5.5 1.5 1.5v1c0 .75.5 1 1 1-.5 0-1 .25-1 1v1c0 1-.5 1.5-1.5 1.5")
     + accent(stroke("M10.5 7h4M12.5 5l2 2-2 2")), mirror=True)
icon("translation-mode", "Translation mode", [], [SUBS],
     stroke("M1.5 3.5h8M1.5 6h5") + accent(stroke("M1.5 10.5h8M1.5 13h5M12.5 3v9M11 10.5l1.5 1.5 1.5-1.5")),
     mirror=True)

# TP1 (#274): the Styles panel's Apply to the selected lines: a Style's
# swatch over two Lines, the arrow bringing it down to them.
icon("style-apply", "Apply to the selected lines", [], ["styles-panel"],
     rect(1.5, 1.5, 6, 6, 1) + stroke("M1.5 11.5h7M1.5 14h7")
     + accent(stroke("M12 2.5v7M9.5 7.5l2.5 2.5 2.5-2.5M10.5 12.5h4M10.5 14.5h4")), mirror=True)

# The legacy bitmaps the set does not replace, and why (the card's Excluded:
# the application and file-type icons stay, and controls Qt draws itself).
NOT_REPLACED = {
    "application and file-type icons (kept as they are)": [
        "HikariLargeIcon.ico", "HikariSmallIcon.ico", "HikariSmallIcon321.ico", "HikariSub_256x256_ass.png",
        "HikariSub_256x256_exe.ico", "HikariSub_256x256_exe.png", "HikariSub_256x256_srt.png",
        "HikariSub_256x256_sub.png", "HikariSub_256x256_txt.png", "HikariSub_ass.ico", "HikariSub_srt.ico",
        "HikariSub_ssa.ico", "HikariSub_sub.ico", "HikariSub_txt.ico"],
    "controls Qt draws (check boxes, radio buttons, menu marks, list arrows, sliders, grippers)": [
        "Check.png", "CheckBox.png", "CheckBoxInactive.png", "CheckBoxSelected.png", "CheckBoxSelectedInactive.png",
        "Radio.png", "RadioInactive.png", "RadioSelected.png", "RadioSelectedInactive.png", "arrow.png",
        "arrow_list_pushed.png", "dot.png", "separator.png", "Gripper.png",
        "progressbar.png", "progresshandle.png"],
    "cursors (not UI icons)": ["blank.cur", "eyedropper.cur"],
    "not referenced by legacy (resource.rc has no entry)": ["ChangeAllTags.png"],
    # The user's wave-5 decision (2026-10-05, #175): GLOBAL_VIDEO_INDEXING
    # ("Open video with FFMS2") is retired, so its menu item and icon go.
    "retired commands (GLOBAL_VIDEO_INDEXING, wave 5 decision 2026-10-05)": ["FFMS2 Indexing.png"],
}


# The roles drawn for surfaces a later card builds: that card places
# them (written to the manifest as "pending"; the manifest test requires every
# other role to be named by the QML, and a pending role not to be).
PENDING = {}
for card, roles in {
}.items():
    for role in roles:
        PENDING[role] = card


# ---------------------------------------------------------------- writing

def svg(body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16" '
            'fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" '
            'stroke-linejoin="round">' + body + "</svg>\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    roles = set()
    manifest = []
    keep = {"manifest.json"}
    for item in ICONS:
        if item["role"] in roles:
            sys.exit("duplicate role " + item["role"])
        roles.add(item["role"])
        name = item["role"] + ".svg"
        keep.add(name)
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(svg(item["body"]))
        manifest.append({
            "role": item["role"],
            "file": name,
            "label": item["label"],
            "accent": 'id="accent"' in item["body"],
            "mirror": item["mirror"],
            "legacy": item["legacy"],
            "surfaces": item["surfaces"],
        })
        if item["role"] in PENDING:
            manifest[-1]["pending"] = PENDING.pop(item["role"])
    if PENDING:
        sys.exit("pending roles not in the set: " + ", ".join(PENDING))
    for name in os.listdir(OUT):
        if name not in keep:
            os.remove(os.path.join(OUT, name))
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump({"grid": 16, "strokeWidth": 1, "icons": manifest, "notReplaced": NOT_REPLACED}, f, indent=1,
                  ensure_ascii=False)
        f.write("\n")
    print("%d icons" % len(manifest))


if __name__ == "__main__":
    main()
