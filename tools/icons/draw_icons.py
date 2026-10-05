#!/usr/bin/env python3
"""K1: the HikariSub vector icon set, drawn as code.

Writes one monochrome SVG per icon role into src/ui/icons/ and the set's
manifest (src/ui/icons/manifest.json). The SVG files are the shipped source;
this script is how they were drawn and is the place to change them, so that
every icon keeps the rules of docs/qt/ux/icons.md:

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


def film(x=1.5, y=2.5, w=11, h=8):
    """A film frame: a rectangle with perforated side strips."""
    x2, y2 = x + w, y + h
    return (rect(x, y, w, h, 1) + stroke("M%s %sv%s M%s %sv%s" % (q(x + 3), q(y), q(h), q(x2 - 3), q(y), q(h)))
            + stroke("M%s %sh3 M%s %sh3 M%s %sh-3 M%s %sh-3" % (
                q(x), q(y + h / 3), q(x), q(y + 2 * h / 3), q(x2), q(y + h / 3), q(x2), q(y + 2 * h / 3))))


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

# File
icon("open-subtitles", "Open subtitles", ["opensubs.png"], [FILE], page(True) + plus_badge())
icon("save", "Save", ["Save.png"], [FILE], floppy())
icon("save-all", "Save all subtitles", ["SaveAll.png"], [FILE],
     stroke("M4.5 3.5v-2h8l2 2v8h-2") + stroke("M1.5 4.5h9l2 2v8h-11z") + stroke("M4.5 4.5v2.5h4V4.5")
     + stroke("M3.5 14.5v-3h6v3"))
icon("save-as", "Save as", ["SaveAs.png"], [FILE],
     floppy(True) + accent(stroke("M9.5 14.5l.5-2 3.5-3.5 1.5 1.5-3.5 3.5z")))
icon("save-translation", "Save translation", ["SaveTl.png"], [FILE],
     floppy(True) + accent(stroke("M10 10.5h4.5M12.25 10.5v4")))
icon("recent-subtitles", "Recently opened subtitles", ["RecentSubs.png"], [FILE], page(True) + clock_badge())
icon("close-subtitles", "Remove subtitles from the editor", ["Close.png"], [FILE], page(True) + cross_badge())
icon("save-with-video-name", "Save subtitles with video name", ["SaveWithVideoName.png"], [FILE],
     floppy(True) + accent(rect(9.5, 9.5, 5, 5, 1), stroke("M11.5 9.5v5")))
icon("last-session", "Load last session", ["Last Session.png"], [FILE],
     stroke("M5.5 4.5v-2h9v7h-2") + rect(1.5, 6.5, 11, 7, 1) + stroke("M1.5 8.5h11")
     + accent(stroke("M5 11.5h4M7.5 10l1.5 1.5L7.5 13")), mirror=True)
icon("settings", "Settings", ["Settings.png"], [FILE, "settings-dialog"], gear())
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
icon("sort", "Sort lines", ["Sort.png"], [EDIT],
     stroke("M1.5 4.5h7M1.5 8h5M1.5 11.5h3") + accent(stroke("M12 2.5v11M9.5 11l2.5 2.5 2.5-2.5")), mirror=True)
icon("sort-selected", "Sort selected lines", ["SortSel.png"], [EDIT],
     stroke("M4 4.5h5M4 8h3.5M4 11.5h2") + stroke("M12 2.5v11M9.5 11l2.5 2.5 2.5-2.5")
     + accent(stroke("M1.5 3v10")), mirror=True)
icon("select-lines", "Select lines", ["Sellines.png"], [EDIT, "misspell-replacer"],
     stroke("M7.5 4h7M7.5 8h7M7.5 12h7") + accent(frect(2, 3, 3, 2), frect(2, 11, 3, 2)) + rect(2.5, 7, 2, 2, 0),
     mirror=True)

# Video
icon("open-video", "Open video", ["openvideo.png"], [VIDEO], film(1.5, 2.5, 11, 7.5) + plus_badge())
icon("recent-video", "Recently opened videos", ["RecentVideo.png"], [VIDEO],
     film(1.5, 2.5, 11, 7.5) + clock_badge())
icon("open-keyframes", "Open keyframes", ["OpenKeyframes.png"], [VIDEO], diamond(6, 6.5, 4.5) + plus_badge())
icon("recent-keyframes", "Recently opened keyframes", ["RecentKeyframes.png"], [VIDEO],
     diamond(6, 6.5, 4.5) + clock_badge())
icon("set-start-time", "Insert start time from video", ["SetStartTime.png"], [VIDEO],
     stroke("M5.5 2.5h-2v11h2") + accent(stroke("M14.5 8h-8M9 5.5L6.5 8 9 10.5")))
icon("set-end-time", "Insert end time from video", ["SetEndTime.png"], [VIDEO],
     stroke("M10.5 2.5h2v11h-2") + accent(stroke("M1.5 8h8M7 5.5L9.5 8 7 10.5")))
icon("frame-previous", "Previous frame", ["PrevFrame.png"], [VIDEO, TRANSPORT],
     rect(6.5, 3.5, 8, 9, 1) + stroke("M9.5 3.5v9M11.5 3.5v9") + accent(stroke("M4 5.5L1.5 8 4 10.5")))
icon("frame-next", "Next frame", ["NextFrame.png"], [VIDEO, TRANSPORT],
     rect(1.5, 3.5, 8, 9, 1) + stroke("M4.5 3.5v9M6.5 3.5v9") + accent(stroke("M12 5.5L14.5 8 12 10.5")))
icon("keyframe-previous", "Go to previous keyframe", ["PrevKeyframe.png"], [VIDEO, TRANSPORT],
     diamond(10.5, 8, 4) + accent(stroke("M4 5.5L1.5 8 4 10.5")))
icon("keyframe-next", "Go to next keyframe", ["NextKeyframe.png"], [VIDEO, TRANSPORT],
     diamond(5.5, 8, 4) + accent(stroke("M12 5.5L14.5 8 12 10.5")))
icon("video-to-start-time", "Go to start time", ["videoonstime.png"], [VIDEO],
     stroke("M5.5 2.5h-2v11h2") + accent(stroke("M14.5 8h-8M12 5.5L14.5 8 12 10.5")))
icon("video-to-end-time", "Go to end time of line", ["videoonetime.png"], [VIDEO],
     stroke("M10.5 2.5h2v11h-2") + accent(stroke("M1.5 8h8M4 5.5L1.5 8 4 10.5")))
icon("audio-to-video-time", "Set audio position to video time", ["SetVideoTimeOnAudio.png"], [VIDEO],
     waveform(1.5, 4.5, 11.5) + accent(stroke("M8 1.5v13")))
icon("audio-marker-to-video-time", "Set audio marker to video time", ["SetVideoTimeOnAudioMark.png"],
     [VIDEO], waveform(1.5, 4.5, 11.5) + accent(stroke("M10.5 14.5v-13h3.5l-1 1.5 1 1.5h-3.5")))
icon("zoom", "Zoom video", ["Zoom.png"], [VIDEO],
     magnifier(6.5, 6.5, 4.5, 4) + accent(stroke("M6.5 4.5v4M4.5 6.5h4")))

# Media transport (video box, fullscreen, menus)
icon("media-play", "Play", ["play.png", "play1.png", "PlayMenu.png"], [TRANSPORT, FULLSCREEN, VIDEO],
     triangle(4.5, 2.5, 9, 11))
icon("media-pause", "Pause", ["pause.png", "pause1.png", "PauseMenu.png"], [TRANSPORT, FULLSCREEN, VIDEO],
     frect(3.5, 2.5, 3, 11, 0.5) + frect(9.5, 2.5, 3, 11, 0.5))
icon("media-stop", "Stop", ["stop.png", "stop1.png", "button_stop.png"], [TRANSPORT, FULLSCREEN, AUDIOBOX],
     frect(3, 3, 10, 10, 1))
icon("play-line", "Play the current line", ["playline.png", "playline1.png", "button_playsel.png"],
     [TRANSPORT, FULLSCREEN, AUDIOBOX],
     stroke("M3.5 2.5h-2v11h2M12.5 2.5h2v11h-2") + triangle(5.5, 4.5, 6, 7))
icon("media-previous-file", "Previous file", ["backward.png", "backward1.png"], [TRANSPORT, FULLSCREEN],
     circle(8, 8, 6.5) + stroke("M5.5 5.5v5") + triangle(7, 5.5, 4, 5, right=False))
icon("media-next-file", "Next file", ["forward.png", "forward1.png"], [TRANSPORT, FULLSCREEN],
     circle(8, 8, 6.5) + stroke("M10.5 5.5v5") + triangle(5, 5.5, 4, 5))

# Audio menu
icon("open-audio", "Open audio", ["OpenAudio.png"], [AUDIO], note(6.5) + plus_badge())
icon("recent-audio", "Recently opened audio", ["RecentAudio.png"], [AUDIO], note(6.5) + clock_badge())
icon("audio-from-video", "Open audio from video", ["OpenAudioFromVideo.png"], [AUDIO],
     film(1.5, 1.5, 9, 7) + accent(stroke("M12.5 14.5V9.5l2 .75"), dot(11.25, 14.25, 1.25)))
icon("close-audio", "Close audio", ["CloseAudio.png"], [AUDIO], note(6.5) + cross_badge())

# Audio box
icon("audio-previous-line", "Previous line", ["button_prev.png"], [AUDIOBOX],
     solid("M7.5 3.5v9L2 8z") + solid("M14 3.5v9L8.5 8z"))
icon("audio-next-line", "Next line", ["button_next.png"], [AUDIOBOX],
     solid("M8.5 3.5v9L14 8z") + solid("M2 3.5v9L7.5 8z"))
icon("audio-play", "Play", ["button_PlayLine.png"], [AUDIOBOX],
     triangle(6, 3, 7.5, 10) + accent(stroke("M3 2.5v11")))
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
icon("go-to-selection", "Go to selection", ["button_audio_go.png"], [AUDIOBOX],
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
icon("tag-font", "Font selection", ["Font.png"], [EDITOR], letter_a(2.5, 2.5, 13.5, 11))
icon("tag-bold", "Bold", ["Bold.png"], [EDITOR],
     fill("M4 2h4.5a2.75 2.75 0 0 1 0 5.5h.5a3.25 3.25 0 0 1 0 6.5H4z"
          "M6 4h2.5a.75.75 0 0 1 0 1.5H6zM6 9.5h3a1.25 1.25 0 0 1 0 2.5H6z", "evenodd"))
icon("tag-italic", "Italic", ["Italic.png"], [EDITOR], stroke("M7 2.5h5M4 13.5h5M9.5 2.5l-3 11"))
icon("tag-underline", "Underline", ["Under.png"], [EDITOR],
     stroke("M4.5 2.5V7a3.5 3.5 0 0 0 7 0V2.5") + accent(stroke("M3.5 13.5h9")))
icon("tag-strikeout", "Strikethrough", ["Strike.png"], [EDITOR],
     stroke("M11.25 4.25C10.75 3 9.5 2.5 8 2.5c-2 0-3.25 1-3.25 2.5 0 1.25.75 2 2 2.5M10.25 9.25c.5.5.75 1 .75 1.75 0 1.5-1.25 2.5-3 2.5-1.75 0-3-.75-3.5-2")
     + accent(stroke("M2.5 8h11")))
icon("colour-primary", "Primary color", ["kolor1.png"], [EDITOR], letter_a(3, 1.5, 10.5, 10) + accent(frect(2, 12, 12, 3, 0.5)))
icon("colour-secondary", "Secondary color for karaoke", ["kolor2.png"], [EDITOR],
     letter_a(3, 1.5, 10.5, 10) + rect(8, 12.5, 5.5, 2, 0.5) + accent(frect(2, 12, 6, 3, 0.5)))
icon("colour-outline", "Border color", ["kolor3.png"], [EDITOR],
     letter_a(3, 1.5, 10.5, 10) + accent(rect(2.5, 12.5, 11, 2, 0.5)))
icon("colour-shadow", "Shadow color", ["kolor4.png"], [EDITOR],
     letter_a(3, 1.5, 10.5, 10) + rect(2.5, 11.5, 10, 2, 0.5) + accent(frect(4, 14, 10, 1.5)))
icon("alignment", "Alignment (\\an)", [], [EDITOR],
     frect(2, 2, 2, 2) + frect(7, 2, 2, 2) + frect(12, 2, 2, 2) + frect(2, 7, 2, 2) + frect(7, 7, 2, 2)
     + frect(12, 7, 2, 2) + frect(2, 12, 2, 2) + frect(12, 12, 2, 2) + accent(frect(6.5, 11.5, 3, 3, 0.5)))

# Subtitles and timing menus
icon("editor", "Enable / Disable editor", ["editor.png"], [SUBS],
     rect(1.5, 2.5, 13, 11, 1) + stroke("M4 6.5h5M4 9.5h7") + accent(stroke("M11.5 5v3")), mirror=True)
icon("script-properties", "ASS file properties", ["Assprops.png"], [SUBS, "script-properties"],
     page() + stroke("M5.5 7.5h1M8 7.5h2.5M5.5 10h1M8 10h2.5M5.5 12.5h1M8 12.5h2.5"), mirror=True)
icon("styles", "Style manager", ["Style.png"], [SUBS, "style-manager"],
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
icon("resample", "Resample subtitles", ["subsResample.png"], [SUBS, VISUAL],
     rect(1.5, 8.5, 6, 6, 1) + stroke("M1.5 5.5v-3a1 1 0 0 1 1-1h11a1 1 0 0 1 1 1v11a1 1 0 0 1-1 1h-3")
     + accent(stroke("M6 10l6-6M8.5 4H12v3.5")))
icon("font-collector", "Font collector", ["FontCollector.png"], [SUBS, "font-collector"],
     stroke("M1.5 9.5h3l1 2h5l1-2h3v4a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1z")
     + accent(stroke("M5.5 7L8 1.5 10.5 7M6.5 5h3")))
icon("spellchecker", "Check spelling", ["Spellchecker.png"], [SUBS, "spell-checker"],
     stroke("M1.5 3.5h9M1.5 7h6") + accent(stroke("M1.5 11l1.5-1.5 1.5 1.5 1.5-1.5L7.5 11"))
     + stroke("M8.5 11l2 2 4-4.5"), mirror=True)
icon("hide-tags", "Hide tags", ["HideTags.png"], [SUBS, EDITOR],
     stroke("M5.5 2.5h-1a1 1 0 0 0-1 1v3L2.5 8l1 1.5v3a1 1 0 0 0 1 1h1")
     + stroke("M10.5 2.5h1a1 1 0 0 1 1 1v3l1 1.5-1 1.5v3a1 1 0 0 1-1 1h-1") + accent(stroke("M10 4l-4 8")))

# Automation and help
icon("automation", "Load script", ["Automation.png"], [AUTO, "automation-manager"],
     page() + accent(stroke("M6.5 7.5L5 9.25 6.5 11M9.5 7.5L11 9.25 9.5 11")))
icon("help", "HikariSub website", ["Help.png"], [HELP],
     circle(8, 8, 6.5) + stroke("M6 6.25a2 2 0 1 1 3 1.75c-.5.25-1 .5-1 1.25v.5") + dot(8, 11.5, 0.75))
icon("report-issue", "Report an issue", ["Nazi.png"], [HELP],
     stroke("M5.5 6.5a2.5 2.5 0 0 1 5 0v4a2.5 2.5 0 0 1-5 0z") + stroke("M6.5 4.5l-1-2M9.5 4.5l1-2")
     + stroke("M2.5 7.5h3M10.5 7.5h3M2.5 11h3M10.5 11h3") + accent(stroke("M8 6.5v6")))
icon("check-updates", "Check for updates", ["About.png"], [HELP],
     stroke("M13.5 8a5.5 5.5 0 1 1-1.75-4") + stroke("M13.5 1.5v3h-3") + accent(stroke("M8 5v5.5M6 8.5l2 2 2-2")))
icon("about", "About", ["About.png"], [HELP],
     circle(8, 8, 6.5) + stroke("M8 7.5v4") + dot(8, 5, 0.75))
icon("credits", "Credits", ["Helpers.png"], [HELP],
     circle(6, 5, 2.5) + stroke("M1.5 13.5a4.5 4.5 0 0 1 9 0") + accent(circle(11.5, 5.5, 2), stroke("M12 9.5a3 3 0 0 1 2.5 3")))

# View menu (arrangements; legacy draws these items without icons)
VID = triangle(6.5, 4, 3, 3.5)
SUBLINES = stroke("M4 10.5h8M4 12h5")
icon("view-all", "All", [], [VIEW],
     layout(stroke("M1.5 8.5h13M8 2.5v6"), triangle(4, 4, 2.5, 3), accent(stroke("M10 4.5v2M11.5 4v3M13 5v1")),
            stroke("M4 11h8")))
icon("view-video-subs", "Video and subs", [], [VIEW], layout(stroke("M1.5 8.5h13"), VID, stroke("M4 11h8")))
icon("view-audio-subs", "Audio and subs", [], [VIEW],
     layout(stroke("M1.5 8.5h13"), accent(stroke("M5 4.5v2M6.5 4v3M8 3.5v4M9.5 4.5v2M11 4v3")), stroke("M4 11h8")))
icon("view-only-video", "Only video", [], [VIEW], layout(triangle(6.5, 5, 4, 6)))
icon("view-only-subs", "Only subtitles", [], [VIEW], layout(stroke("M4 5.5h8M4 8h8M4 10.5h5")), mirror=True)

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
icon("clip-invert", "Invert clip", ["InvertClipIcon1.png"], [VISUAL],
     fill("M2 2h12v12H2zM8 4.5a3.5 3.5 0 1 0 0 7 3.5 3.5 0 0 0 0-7z", "evenodd"))

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
icon("two-points", "Set angle from 2 points", ["TwoPoints.png"], [VISUAL],
     stroke("M3.5 12.5l9-8") + stroke("M1.5 12.5h13") + stroke("M8.5 12.5a5 5 0 0 0-1.25-3.25")
     + accent(dot(3.5, 12.5, 1.5), dot(12.5, 4.5, 1.5)))
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
icon("tab-close", "Close tab", [], [TABS], stroke("M4.5 4.5l7 7M11.5 4.5l-7 7"))
icon("tab-new", "Open new tab", [], [TABS], stroke("M8 3v10M3 8h10"))
icon("document-modified", "Modified", [], [TABS], accent(dot(8, 8, 3)))

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
        "arrowListDouble.png", "arrow_list.png", "arrow_list_pushed.png", "dot.png", "separator.png", "Gripper.png",
        "progressbar.png", "progresshandle.png"],
    "cursors (not UI icons)": ["blank.cur", "eyedropper.cur"],
    "not referenced by legacy (resource.rc has no entry)": ["ChangeAllTags.png"],
    # The user's wave-5 decision (2026-10-05, #175): GLOBAL_VIDEO_INDEXING
    # ("Open video with FFMS2") is retired, so its menu item and icon go.
    "retired commands (GLOBAL_VIDEO_INDEXING, wave 5 decision 2026-10-05)": ["FFMS2 Indexing.png"],
}


# The roles drawn for surfaces a later wave-5 card builds: that card places
# them (written to the manifest as "pending"; the manifest test requires every
# other role to be named by the QML, and a pending role not to be).
PENDING = {}
for card, roles in {
    "T1 #176": ["tool-crosshair"],
    "T2 #177": ["tool-position", "tool-move"],
    "T3 #178": ["tool-scale", "tool-rotate-z", "tool-rotate-xy", "two-points", "frame-to-scale", "scale-x", "scale-y",
                "original-frame"],
    "T4 #179": ["tool-clip-rect", "tool-clip-vector", "clip-invert"],
    "T4 #179, T5 #180": ["vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete"],
    "T5 #180": ["tool-drawing"],
    "T6 #181": ["tool-move-all", "tool-all-tags", "tool-scale-rotation", "shift-position", "shift-move-start",
                "shift-move-end", "shift-clips", "shift-drawings", "shift-origins"],
    "V3 #182": ["recent-video", "recent-keyframes", "media-previous-file", "media-next-file"],
    "V4 #183": ["zoom"],
    "V6 #185": ["set-start-time", "set-end-time"],
    "E4 #186": ["alignment"],
    "E6 #188": ["hide-tags"],
    "Y7 #190": ["eyedropper"],
    "Y8 #191": ["font-collector"],
    "D2 #201": ["editor", "view-all", "view-video-subs", "view-audio-subs", "view-only-video", "view-only-subs"],
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
