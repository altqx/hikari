#!/usr/bin/env python3
"""PU6 #308 tool popups mockup: per theme, the Spellchecker as a dock panel (its Words added list a window, its
messages toasts), the Font collector as a modeless window on DG1's frame, the conversion window with grouped
sections and a live preview, and Resample subtitles, ASS file properties and the incompatible resolution
question.

Drawn with PK1's mockup kit (kit.py: the theme roles of src/ui/theme.cpp, DG1's frame, the message box and the
toast, IC1's icons from the tree). The main window behind the Spellchecker panel is today's HikariSub (the NT1
build, qt 7788b8a7c) with the Log panel open in the place the Spellchecker panel takes, shot by shots.sh under
vx11; the panel is drawn over the Log's place."""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
TREE = sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-pu6"
sys.argv = [sys.argv[0], TREE]
import kit  # noqa: E402
from kit import THEMES, btn, field, combo, segmented, window, ic, message_box, toast, checkbox  # noqa: E402

kit.REPO_ICONS += ["panel-menu", "dictionary", "dictionary-add", "folder-open", "font-collector", "resample",
                   "script-properties", "convert", "search", "panel-move", "panel-float", "fps-from-video",
                   "show-in-folder", "refresh", "open-video"]


def cap(text, width=520):
    return f'<p class="cap" style="max-width:{width}px">{text}</p>'


def col(inner, caption="", width=520):
    return f'<div class="col">{inner}{cap(caption, width) if caption else ""}</div>'


def row(*cols, gap=28, top=0):
    return f'<div class="row" style="gap:{gap}px;margin-top:{top}px">{"".join(cols)}</div>'


def hint(text):
    return f'<div class="ghint">{text}</div>'


def error(text):
    return f'<div class="ferr">{ic("notice-error", 14, "err")}<span>{text}</span></div>'


def bad(f):
    return f.replace('class="fld', 'class="fld bad', 1)


def label(text, muted=False):
    return f'<span class="glabel{" m" if muted else ""}">{text}</span>'


def section(title, body, first=False):
    return f'<div class="sec{" first" if first else ""}"><div class="stitle">{title}</div>{body}</div>'


def grid(*cells, cols="120px 1fr"):
    cells = [c or "<span></span>" for c in cells]
    return f'<div class="grid" style="grid-template-columns:{cols}">{"".join(cells)}</div>'


def line(*parts):
    return f'<div class="gline">{"".join(parts)}</div>'


def off(b):
    return b.replace('class="b', 'class="b off', 1)


def num(v, w=64, focus=False):
    return field(v, w, focus=focus).replace('class="fld', 'class="fld num', 1)


def times():
    return '<span class="dash">×</span>'


def tipped(b, tip, left=0, top=-30):
    return (f'<span style="position:relative;display:inline-flex">{b}'
            f'<span class="tip" style="left:{left}px;top:{top}px">{tip}</span></span>')


def menu(items, style=""):
    rows = []
    for it in items:
        if it is None:
            rows.append('<div class="msep"></div>')
            continue
        text, icon, check, cur = (list(it) + [False, False])[:4]
        gut = (f'<span class="mchk">{kit.box(True) if check == "on" else kit.box(False) if check == "off" else ""}'
               f'</span>')
        rows.append(f'<div class="mrow{" cur" if cur else ""}">{gut}'
                    f'{ic(icon) if icon else "<span class=gut></span>"}<span>{text}</span></div>')
    return f'<div class="menu" style="{style}">{"".join(rows)}</div>'


def foot(*buttons, lead=""):
    return (f'<span class="lead2">{lead}</span>' if lead else "") + "".join(buttons)


# ---------------------------------------------------------------------------------------------
# 1. Spellchecker panel

SUGGESTIONS = ["them", "teem", "term", "tem", "the hm"]


def spell_body(state="word"):
    if state == "none":
        return ('<div class="sempty"><span>No spelling errors in Episode 03.ass</span>'
                f'{btn("Check again", small=True)}</div>')
    if state == "nodict":
        return ('<div class="sempty"><span>No dictionaries found</span>'
                f'{btn("Spelling settings…", small=True)}</div>')
    word = (f'<div class="sword"><span class="sw">tehm</span><span class="sline">Line 4</span></div>'
            f'<div class="sctx">They’re in my coat. Both of <span class="miss">tehm</span>.</div>')
    repl = line(label("Replace with"), field("them", 196, focus=True))
    left = f'<div class="scol" style="width:330px">{word}<div style="margin-top:10px">{repl}</div></div>'
    sug = "".join(f'<div class="lrow{" cur" if i == 0 else ""}">{s}</div>' for i, s in enumerate(SUGGESTIONS))
    mid = f'<div class="slist2" title="Suggestions">{sug}</div>'
    acts = (f'<div class="sacts">{btn("Replace", primary=True)}{btn("Replace all")}{btn("Ignore")}'
            f'{btn("Ignore all")}<span class="b ibtn wide2">{ic("dictionary-add")}<span>Add to dictionary</span>'
            f'</span></div>')
    return f'<div class="sbody">{left}{mid}{acts}</div>'


def dock(body, title="Spellchecker", height=180):
    return (f'<div class="dock" style="height:{height}px"><div class="dhead"><span class="dtab">{title}'
            f'<span class="dmore">{ic("panel-menu", 16)}</span></span></div>{body}</div>')


def spell_menu():
    return menu([("Ignore comments", None, "on", False), ("Ignore words in capitals", None, "off", False), None,
                 ("Words added to dictionary…", "dictionary", None, True), None,
                 ("Move panel…", "panel-move"), ("Undock", "panel-float"), ("Close", None)],
                style="width:260px")


ADDED = [("Hikari", False), ("Kenta", True), ("Mei", False), ("romaji", True), ("Shinkansen", False),
         ("tsundere", False), ("Umeda", False)]


def added_window(empty=False):
    if empty:
        lst = '<div class="alist empty"><span>No words added</span></div>'
        f = foot(off(btn("Remove", danger=True)), btn("Close", primary=True, focus=True))
    else:
        rows = "".join(f'<div class="arow">{kit.box(on)}<span>{w}</span></div>' for w, on in ADDED)
        lst = f'<div class="alist">{rows}</div>'
        f = foot(btn("Remove 2 words", danger=True), btn("Close", primary=True, focus=True))
    body = field("Filter", 300, placeholder=True, icon="search").replace('class="fld', 'class="fld wide', 1) + lst
    return window("Words added to dictionary", body, f, 348)


def spell_page(key):
    th = THEMES[key]
    sel = '<span class="esel" style="left:809px;top:327px;width:32px"></span>'
    app = f'<div class="app">{sel}<div class="slot">{dock(spell_body())}</div></div>'
    toasts = (toast("info", "Spellchecker", "No spelling errors were found")
              + toast("error", "Replace", "The edited line cannot be committed.")
              + toast("error", "Add to dictionary", "Error. Word “tehm” was not added.")
              + toast("warning", "Spellchecker", "No dictionary files were found in the “Dictionary” folder. "
                                                 "Spell checking will be disabled."))
    return (
        f'<h1>PU6: the Spellchecker as a panel ({th["name"]})</h1>'
        '<p class="note">Subtitles › Check spelling opens the <b>Spellchecker</b> panel and starts at the active line, '
        'as legacy’s window did. It opens along the bottom, the whole width (the Log’s place, drawn here over the '
        'Log panel of today’s build); like any panel it can be moved, tabbed, floated and saved in a layout, and '
        'View › Panels lists it. The Line editor selects the word and the Grid shows its line, as now. Closing the '
        'panel ends the check. Coming back into the panel after editing checks the line again (legacy OnActive).</p>'
        + app
        + '<h2>The panel’s parts</h2>'
        + row(col(spell_menu(),
                  "⋯ holds the two options (legacy’s check boxes) and <b>Words added to dictionary…</b>, which "
                  "replaces the “Remove from dictionary” button. Legacy’s Close button goes: the panel’s Close "
                  "(⋯ or Esc in the panel) ends the check.", 260),
              col(dock(spell_body("none"), height=110) + '<div style="height:12px"></div>'
                  + dock(spell_body("nodict"), height=110),
                  "Nothing left to check: the panel says so in one muted line, with <b>Check again</b> (starts at "
                  "the active line again); the “No spelling errors were found” toast still shows once. Without a "
                  "dictionary the panel says so and offers the spelling settings.", 600),
              gap=36)
        + row(col(added_window(),
                  "<b>Words added to dictionary</b> (legacy CustomCheckListBox): a compact window on DG1’s frame, "
                  "modal to the main window, with a filter. <b>Remove 2 words</b> (outlined in danger, never the "
                  "default) removes the checked words and closes; Close (Enter, Esc) keeps them all.", 348),
              col(added_window(empty=True),
                  "No words added yet: one muted line, Remove off.", 348),
              col(f'<div class="tstack">{toasts}</div>',
                  "Its messages are toasts titled by their command: the outcome (no errors), a refused replace "
                  "(was a message box), a word the dictionary refused, and a missing dictionary (NT1 made the "
                  "first, third and fourth toasts already).", 360),
              gap=36, top=28)
    )


# ---------------------------------------------------------------------------------------------
# 2. Font collector

LOG = [
    ("", "Found font “Noto Sans”."),
    ("", "In styles:\n  Default, Sign — tabs: Episode 03.ass"),
    ("warn", "Font “Noto Sans” is missing bold italics."),
    ("", "Found font “Klee One”."),
    ("", "In lines: 5"),
    ("warn", "Cannot find “Rounded Mplus 1c” font in Fonts folder."),
    ("", "In lines: 12, 13"),
    ("ok", "Found 2 of 3 fonts. Checked by the subtitle renderer (libass 0.17.4)."),
]


def collector(mode="Check fonts", stage="idle", log=True, width=620):
    modes = ["Check fonts", "Copy to a folder", "Zip archive"]
    hints = {"Check fonts": "Lists the fonts the subtitles use and whether this computer has each one.",
             "Copy to a folder": "Copies each font file the subtitles use into the folder.",
             "Zip archive": "Puts each font file the subtitles use into a zip archive."}
    dest_on = mode != "Check fonts"
    path = {"Copy to a folder": "D:\\Release\\Episode 03\\Fonts", "Zip archive": "D:\\Release\\Episode 03.zip"}
    pf = field(path.get(mode, "Choose a folder"), 420, placeholder=not dest_on,
               trail=None).replace('class="fld', 'class="fld wide', 1)
    browse = f'<span class="qb tool{"" if dest_on else " offq"}" title="Choose…">{ic("folder-open")}</span>'
    dest = (line(pf, browse)
            + checkbox("Write beside the subtitles, in a “Fonts” folder", False)
            + checkbox("Take the fonts from the loaded MKV file", False))
    busy = " offc" if stage == "working" else ""
    body = (grid(label("Action"), f'<div class="gcol{busy}">{segmented(modes, mode)}{hint(hints[mode])}</div>',
                 label("Save to"), f'<div class="gcol{busy or ("" if dest_on else " offc")}">{dest}</div>',
                 cols="72px 1fr"))
    if log:
        shown = LOG[:3] + [("", "Checking the fonts…")] if stage == "working" else LOG
        lines = "".join(f'<div class="ll {c}">{t}</div>' for c, t in shown)
        body += f'<div class="clog">{lines}</div>'
    else:
        body += '<div class="clog empty"><span>Start lists the fonts the subtitles use.</span></div>'
    if stage == "review":
        body += (f'<div class="rbar">{ic("notice-warning", 16, "warn")}<span class="ibt">1 of 3 fonts was not found, '
                 f'so the collection is incomplete.</span>{checkbox("Copy it, labelled incomplete", True)}'
                 f'{btn("Copy 2 fonts", primary=True, small=True)}</div>')
    working = stage == "working"
    start = btn("Start", primary=stage == "idle", focus=stage == "idle")
    lead = btn("Show folder", flat=False) if mode != "Check fonts" else ""
    if working:
        f = foot(off(btn("Start on all tabs")), off(btn("Start")), btn("Stop", primary=True), lead=off(lead) if lead else "")
    else:
        f = foot(btn("Start on all tabs"), start, btn("Close"), lead=lead)
    return window("Font collector", body, f, width, resizable=True)


def fonts_page(key):
    th = THEMES[key]
    replace = message_box("Font collector", "“Episode 03.zip” already exists.",
                          "Writing the new archive replaces it.", "warning",
                          btn("Replace", danger=True) + btn("Cancel", primary=True, focus=True))
    refusal = message_box("Font collector", "Select the folder where you want to copy fonts", "", "warning",
                          btn("OK", primary=True, focus=True))
    return (
        f'<h1>PU6: Font collector ({th["name"]})</h1>'
        '<p class="note">A window on DG1’s frame, no longer drawn inside the main window: modeless (the subtitles '
        'stay editable, Subtitles › Font collector raises it when open), resizable from its content size, and '
        'reopened where and as big as it was left. Same engine, log and staged writes as today (nothing is '
        'written until the review’s Copy / Write).</p>'
        + row(col(collector(),
                  "Check fonts, after Start. <b>Action</b> is one segmented choice with a one-line hint (legacy’s "
                  "“Options” radio box: Check availability of fonts, Copy to selected folder, Zip). Save to is "
                  "greyed for a check. The log keeps the warning and success colours; a double click goes to the "
                  "Style or Line.", 620),
              col(collector("Copy to a folder", "review"),
                  "Copy, after Start: the review is a bar above the footer. An incomplete collection needs its "
                  "box ticked before <b>Copy 2 fonts</b> works (Write archive for Zip). <b>Show folder</b> (legacy "
                  "“Save folder”, which opened the output folder) sits at the leading edge.", 620),
              gap=24)
        + row(col(collector("Zip archive", "working"),
                  "While it runs: Stop replaces Close; Start, Start on all tabs and the options are off. Before the "
                  "first Start the log says “Start lists the fonts the subtitles use.” in one muted line.", 620),
              col(replace + '<div style="height:20px"></div>' + refusal,
                  "The question when the archive exists (legacy “Confirmation”, “The zip file already exists, "
                  "delete it?” Yes / No, where No still replaced it): <b>Replace</b> in danger, Cancel the default "
                  "(nothing runs). The Start refusals stay PK1 message boxes, as NT1 made them.", 468),
              gap=24, top=28)
    )


# ---------------------------------------------------------------------------------------------
# 3. Conversion


def changes(items, flash=None):
    rows = []
    for i, (sev, text) in enumerate(items):
        glyph = {"warn": ("notice-warning", "warn"), "info": ("about", "")}[sev]
        rows.append(f'<div class="crow{" flash" if i == flash else ""}">{ic(glyph[0], 16, glyph[1])}'
                    f'<span>{text}</span></div>')
    return f'<div class="cprev"><div class="ptitle2">What changes</div>{"".join(rows)}</div>'


END_HINT = "A line lasts its length × this, at least 1 s, and ends where the next one starts."


def end_times(on=True, per="60", focus=False):
    return section("End times", grid(
        "", f'<div class="gcol">{checkbox("New end times", on)}</div>',
        label("Per character"), f'<div class="gcol">{line(num(per, 64, focus), label("ms", True))}'
                                f'{hint(END_HINT)}</div>', cols="104px 1fr"))


def to_ass_window():
    style = section("Style", grid(
        label("Catalog"), combo("Default", 200),
        label("Style"), f'<div class="gcol">{combo("Default", 200)}{hint("Every line gets this style.")}</div>',
        label("Tags at start"), f'<div class="gcol">{field("{\\be1}", 200, mono=True)}'
                                f'{hint("Written at the start of every line.")}</div>', cols="104px 1fr"), first=True)
    res = section("Resolution", grid(
        "", f'<div class="gcol">{line(num("1920"), times(), num("1080"))}'
            f'{hint("The new file’s script resolution.")}</div>', cols="104px 1fr"))
    left = f'<div class="copts">{style}{res}{end_times(False)}</div>'
    prev = changes([("info", "214 lines get the conversion style “Default”."),
                    ("info", "Formatting tags converted or removed in 12 lines.")])
    return window("Convert to ASS", f'<div class="csplit">{left}{prev}</div>',
                  foot(btn("Convert", primary=True, focus=True), btn("Cancel")), 760)


def to_mdvd_window(fps="23.976", bad_fps=False, flash=None, per="60", focus=False):
    fpsf = num(fps, 72, focus=bad_fps)
    if bad_fps:
        fpsf = bad(fpsf)
    fr = grid(label("Frames"), f'<div class="gcol">{line(fpsf, label("fps", True), checkbox("From video", False))}'
              + (error("Enter a frame rate of 1 or more.") if bad_fps else
                 hint("MicroDVD counts frames: times become frames at this rate.")) + '</div>', cols="104px 1fr")
    left = f'<div class="copts">{section("Frame rate", fr, first=True)}{end_times(True, per, focus)}</div>'
    if bad_fps:
        prev = ('<div class="cprev"><div class="ptitle2">What changes</div>'
                '<div class="cnone">Nothing to show until the frame rate is valid.</div></div>')
    else:
        prev = changes([("warn", "The script properties and styles are removed."),
                        ("warn", "3 comment lines removed."),
                        ("warn", "Layer, style, actor, margins or effect removed from 41 lines."),
                        ("warn", "Bookmarks, hidden lines and groups removed from 2 lines."),
                        ("info", "Formatting tags converted or removed in 12 lines."),
                        ("info", "Lines are sorted by time."),
                        ("info", "Times changed (rounding, frames or new end times) in 186 lines.")], flash=flash)
    convert = btn("Convert", primary=True)
    return window("Convert to MDVD", f'<div class="csplit">{left}{prev}</div>',
                  foot(off(convert) if bad_fps else convert, btn("Cancel", focus=bad_fps)), 760)


def conversion_page(key):
    th = THEMES[key]
    return (
        f'<h1>PU6: Conversion ({th["name"]})</h1>'
        '<p class="note">Subtitles › Conversion › Convert to … opens a window on DG1’s frame titled with the command, '
        'modal to the main window (the conversion acts on the editing target as it is). The options are grouped '
        'in sections with short labels and a hint under each, and only the sections that act on this conversion '
        'show (the others keep their values). <b>What changes</b> follows every change at once: each keystroke, '
        'tick or choice, not only when a field is left. Convert applies exactly what it lists, as today (C02).</p>'
        + row(col(to_ass_window(),
                  "An SRT file to ASS: Style (legacy “Catalog for style”, “Style”, “Tags to paste at the beginning "
                  "of every ASS line”), Resolution (“Resolution when converting to ASS”) and End times (“New end "
                  "times”, “Time for one letter in milliseconds”). No frame rate: SRT has times.", 760))
        + row(col(to_mdvd_window(flash=6, per="65", focus=True),
                  "An ASS file to MicroDVD: Frame rate (legacy “FPS”, “FPS from video”) and End times. The per "
                  "character time was just typed 65: the times row updates and marks itself for a moment.", 760),
              top=28)
        + row(col(to_mdvd_window(fps="0", bad_fps=True),
                  "A frame rate below 1: said under the field, What changes waits, Convert is off (legacy’s message "
                  "“Invalid FPS. Correct it in options and try again.” after Convert).", 760), top=28)
    )


# ---------------------------------------------------------------------------------------------
# 4. Resample subtitles, ASS file properties, Incompatible resolution


def resample_window(target=("1920", "1080"), stretch=False, video=True, tip=False):
    vbtn = btn("From video", small=True)
    tvbtn = off(vbtn)
    if not video:
        vbtn = off(vbtn)
        if tip:
            tvbtn = tipped(tvbtn, "No video is open", top=30)
    sbtn = btn("From subtitles", small=True)
    rows = grid(
        label("Subtitles"), f'<div class="gcol">{line(num("1280"), times(), num("720"), off(sbtn), vbtn)}'
                            f'{hint("The resolution the lines are written for now.")}</div>',
        label("Target"), f'<div class="gcol">{line(num(target[0], focus=not stretch), times(), num(target[1]), sbtn, tvbtn)}'
                         f'{hint("Positions, sizes and margins are scaled by 1.5.") if not stretch else hint("Scaled by 1.125 across and 1.5 down.")}</div>',
        cols="76px 1fr")
    if stretch:
        rows += section("Aspect ratio", grid(
            "", f'<div class="gcol">{segmented(["Keep it", "Stretch"], "Keep it")}'
                f'{hint("The aspect ratio changes from 16:9 to 4:3.")}</div>', cols="76px 1fr"))
    return window("Resample subtitles", rows, foot(btn("Resample", primary=True, focus=stretch), btn("Cancel")), 600)


def properties_window():
    credits = section("Credits", grid(
        label("Title"), field("Episode 03", None).replace('class="fld', 'class="fld wide', 1),
        label("Author"), field("Mei", None).replace('class="fld', 'class="fld wide', 1),
        label("Translator"), field("Kenta", None).replace('class="fld', 'class="fld wide', 1),
        label("Proofreading"), field("", None).replace('class="fld', 'class="fld wide', 1),
        label("Timer"), field("", None).replace('class="fld', 'class="fld wide', 1),
        label("Editing"), field("", None).replace('class="fld', 'class="fld wide', 1),
        cols="104px 1fr"), first=True)
    res = section("Resolution", grid(
        label("Subtitles"), line(num("1280"), times(), num("720"), btn("From video", small=True)),
        label("Layout"), f'<div class="gcol">{line(off(num("1280")), times(), off(num("720")), off(btn("From video", small=True)))}'
                         f'{checkbox("Same as the subtitles", True)}</div>',
        label("YCbCr matrix"), combo("TV.709", 200), cols="104px 1fr"))
    opts = section("Rendering", grid(
        label("Wrap style"), combo("0: Auto, top line wider", 300),
        label("Colliding lines"), combo("Normal", 200),
        "", checkbox("Scale border and shadow", True), cols="104px 1fr"))
    return window("ASS file properties", credits + res + opts,
                  foot(btn("Save", primary=True, focus=True), btn("Cancel")), 520)


def mismatch_box(stretch=False):
    buttons = (btn("Keep as is") + btn("Change resolution only")
               + (btn("Resample and stretch") if stretch else "") + btn("Resample", primary=True, focus=True))
    sizes = ("1920 × 1080", "1280 × 720") if not stretch else ("1920 × 1080", "1024 × 768")
    return message_box("Open video", f"The video is {sizes[0]} and the subtitles are {sizes[1]}.",
                       "Resample the subtitles to the video? Subtitles › Resample subtitles does it later too.",
                       "info", buttons, width=620 if stretch else 520,
                       extra=f'<div class="mbcheck">{checkbox("Don’t ask again", False)}</div>')


def resolution_page(key):
    th = THEMES[key]
    return (
        f'<h1>PU6: Resample subtitles, ASS file properties, the resolution question ({th["name"]})</h1>'
        '<p class="note">Both windows are on DG1’s frame, fixed size, modal to the main window. Resample subtitles '
        'gets <b>From video</b> and <b>From subtitles</b> beside each resolution (legacy had one button per row); a '
        'button is off when its value is already there or there is no video. The incompatible resolution question '
        'is a PK1 message box, its hard line breaks gone.</p>'
        + row(col(resample_window(),
                  "Same aspect ratio: no stretch choice (legacy greyed its box). The hint says the scale. Resample "
                  "is off while both resolutions match (legacy OK did nothing).", 600),
              col(resample_window(("1440", "1080"), stretch=True, video=False, tip=True),
                  "Another aspect ratio: <b>Aspect ratio</b> shows, Keep it (legacy “No stretch”, the default) or "
                  "Stretch. No video open: From video is off and says why.", 600), gap=24)
        + row(col(properties_window(),
                  "Legacy “ASS subtitle properties”, titled with its menu command. Credits, Resolution and "
                  "Rendering are sections with labels beside the fields; “Link the layout resolution to the "
                  "subtitle resolution” is <b>Same as the subtitles</b> under the Layout row it controls. Only "
                  "fields you change are written, as today.", 520),
              col(mismatch_box() + '<div style="height:20px"></div>' + mismatch_box(stretch=True),
                  "Legacy “Incompatible resolution” (a text with hard line breaks and three radios, Change / Do not "
                  "change / Disable warning): the title names the command that found it (opening the video), one "
                  "line with both sizes, the choices as buttons (Resample the default, Keep as is on Esc), and "
                  "<b>Resample and stretch</b> only when the aspect ratios differ (legacy showed that radio only "
                  "then). Don’t ask again is legacy’s Disable warning; Settings › Notices › Ask again brings it "
                  "back.", 620), gap=24, top=28)
    )


# ---------------------------------------------------------------------------------------------

EXTRA_CSS = """
body{width:1328px}
.app{position:relative;width:1280px;height:800px;background:url(%(body)s) 0 0 no-repeat;outline:1px solid %(line)s;margin-top:12px}
.slot{position:absolute;left:0;top:592px;width:1280px}
.esel{position:absolute;height:18px;background:%(select)s;mix-blend-mode:%(blend)s;opacity:.85;outline:1px solid %(accent)s}
.stack{position:absolute;right:12px;bottom:214px;width:360px;display:flex;flex-direction:column;gap:8px}
.dock{background:%(panel)s;box-shadow:inset 0 1px %(line)s;display:flex;flex-direction:column;width:100%%;outline:1px solid %(line)s}
.dhead{height:36px;flex:none;display:flex;background:%(raised)s;box-shadow:inset 0 1px %(line)s,inset 0 -1px %(line)s}
.dtab{display:flex;align-items:center;gap:8px;padding:0 10px;background:%(field)s;font-weight:650;box-shadow:inset -1px 0 %(line)s,inset 0 1px %(line)s}
.dmore{display:flex;color:%(text)s}
.sbody{display:flex;gap:24px;padding:12px 12px 12px 14px;flex:1;min-height:0}
.scol{display:flex;flex-direction:column}
.sword{display:flex;align-items:baseline;gap:10px}
.sw{font-size:18px;font-weight:650}
.sline{color:%(muted)s;font-size:12px}
.sctx{color:%(muted)s;margin-top:4px}
.sctx .miss{color:%(text)s}
.miss{text-decoration:underline wavy %(danger)s;text-underline-offset:3px}
.slist2{width:220px;flex:none;background:%(field)s;outline:1px solid %(line)s;padding:2px 0;overflow:hidden}
.slist2 .lrow{height:24px}
.sacts{display:grid;grid-template-columns:repeat(2,140px);gap:8px;align-content:start}
.sacts .b{min-width:0}
.wide2{grid-column:span 2;gap:6px}
.sempty{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;color:%(muted)s}
.lrow{display:flex;gap:10px;align-items:center;height:28px;padding:0 10px}
.lrow.cur{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.menu{position:relative;background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;
      box-shadow:0 10px 30px rgba(0,0,0,.33);z-index:3}
.mrow{display:flex;align-items:center;gap:8px;height:28px;padding:0 14px 0 8px}
.mrow.cur{background:%(select)s}
.mrow .gut{width:16px}
.mchk{width:16px;display:flex}
.mchk .box{width:14px;height:14px}
.msep{height:1px;background:%(line)s;margin:4px 0}
.tstack{display:flex;flex-direction:column;gap:8px;width:360px}
.alist{margin-top:10px;height:200px;outline:1px solid %(line)s;background:%(field)s;padding:2px 0;overflow:hidden}
.alist.empty{display:flex;align-items:center;justify-content:center;color:%(muted)s}
.arow{display:flex;align-items:center;gap:8px;height:26px;padding:0 10px}
.grid{display:grid;column-gap:12px;row-gap:10px;align-items:start}
.glabel{height:28px;display:flex;align-items:center;color:%(text)s;white-space:nowrap}
.glabel.m{color:%(muted)s}
.gcol{display:flex;flex-direction:column;gap:6px;min-width:0}
.gcol.offc{opacity:.46}
.gline{display:flex;align-items:center;gap:8px;min-height:28px}
.ghint{color:%(muted)s;font-size:12px;line-height:1.4}
.sec{padding:14px 0;box-shadow:inset 0 1px %(line)s}
.sec.first{box-shadow:none;padding-top:4px}
.stitle{font-weight:650;margin-bottom:10px}
.fld.num .ftext{text-align:right;font-variant-numeric:tabular-nums}
.fld.bad{border-color:%(danger)s;box-shadow:inset 0 0 0 1px %(danger)s}
.ferr{display:flex;align-items:center;gap:6px;color:%(danger)s;font-size:12px}
.dash{color:%(muted)s}
.b.off,.qb.offq{opacity:.46}
.b.ibtn{gap:6px}
.lead2{display:flex;gap:8px;margin-right:auto}
.qb.tool{width:28px;height:28px;border:1px solid %(line)s;background:%(raised)s}
.tip{position:absolute;white-space:nowrap;background:%(raised)s;outline:1px solid %(line)s;padding:3px 8px;
     font-size:12px;color:%(text)s;font-weight:400;z-index:4}
.seg{align-self:flex-start}
.seg span{white-space:nowrap}
.clog{margin-top:14px;height:210px;background:%(field)s;outline:1px solid %(line)s;padding:6px 10px;overflow:hidden;
      font-size:12px;line-height:1.5}
.clog.empty{display:flex;align-items:center;justify-content:center;color:%(muted)s;font-size:13px}
.ll{white-space:pre-wrap}
.ll.warn{color:%(warning)s}
.ll.ok{color:%(success)s}
.rbar{display:flex;align-items:center;gap:10px;margin-top:10px;padding:6px 8px 6px 10px;background:%(raised)s;outline:1px solid %(line)s}
.rbar .cb{white-space:nowrap}
.csplit{display:flex;gap:0;outline:1px solid %(line)s;margin-top:4px}
.copts{width:400px;flex:none;padding:12px 18px 4px;background:%(panel)s}
.cprev{flex:1;min-width:0;padding:12px 16px;background:%(field)s;box-shadow:inset 1px 0 %(line)s}
.ptitle2{font-weight:650;margin-bottom:10px}
.crow{display:flex;gap:8px;align-items:flex-start;padding:5px 6px;line-height:1.4}
.crow .ic{margin-top:1px}
.crow.flash{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.cnone{color:%(muted)s;padding:5px 6px}
"""


def page(key, content):
    th = THEMES[key]
    html = kit.page(key, content)
    success = {"light": "#008000", "dark": "#4DBA4D", "hcwhite": "#005A00", "hcblack": "#7CFC7C"}[key]
    css = EXTRA_CSS % dict(th, body=f"shots/body-{th['code']}.png", success=success,
                           blend="multiply" if key in ("light", "hcwhite") else "screen")
    return html.replace("</style>", css + "</style>", 1)


def main():
    for key in THEMES:
        (HERE / f"pu6-{key}-1-spellchecker.html").write_text(page(key, spell_page(key)))
        (HERE / f"pu6-{key}-2-fonts.html").write_text(page(key, fonts_page(key)))
        (HERE / f"pu6-{key}-3-conversion.html").write_text(page(key, conversion_page(key)))
        (HERE / f"pu6-{key}-4-resolution.html").write_text(page(key, resolution_page(key)))


main()
