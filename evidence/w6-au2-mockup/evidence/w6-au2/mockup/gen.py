#!/usr/bin/env python3
"""AU2 #268 mockup: the audio panel's Customise menu and the Classic arrangement.

One HTML sheet per theme, screenshotted by shoot.sh. The panel is AU1 as built
(base.py: the AU1 mockup's drawing code with the shipped icons).

usage: gen.py <src/ui/icons> <out dir>
"""
import pathlib
import sys

ICONS = pathlib.Path(sys.argv[1])
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
exec(open(pathlib.Path(__file__).with_name("base.py")).read())

# The controls AU1 folded, in legacy's order (AudioBox.cpp:149-193): id, role, menu text, keys.
MARK_PLAYS = [("beforeMark", "play-before-mark", "Play before the mark", "Num 0"),
              ("afterMark", "play-after-mark", "Play after the mark", "Num .")]
TIME_PLAYS = [("before", "play-before-start", "Play 500 ms before the start", "Q"),
              ("first", "play-after-start", "Play the first 500 ms", "E"),
              ("last", "play-before-end", "Play the last 500 ms", "D"),
              ("after", "play-after-end", "Play 500 ms after the end", "W"),
              ("toEnd", "play-to-end", "Play to the end", "T")]
COMMIT_TOGGLES = [("autoCommit", "auto-commit", "Auto-commit", ""),
                  ("nextCommit", "next-after-commit", "Go to the next line after commit", "")]
GO = ("goto", "go-to-selection", "Go to selection", "B")
ALL = {x[0] for x in MARK_PLAYS + TIME_PLAYS + COMMIT_TOGGLES} | {"goto", "sliders"}
CLASSIC = ALL  # and the Play around button hidden


def b(role, cls=""):
    return btn(role, cls)


def strip2(t, *, shown=frozenset(), around=True, open_menu=None, hover=None, fold=False, folded=(),
           auto=True, nxt=True, zoom="1.0×", vol="volume", mode=0, view=0, play="play-before-start"):
    """AU1's row plus the controls shown from Customise. `folded` lists ids that went into ⋯."""
    vis = lambda k: k in shown and k not in folded
    marks = ""
    mk_auto = auto and not vis("autoCommit")
    mk_next = nxt and not vis("nextCommit")
    if mk_auto or mk_next:
        marks = '<span class="marks">' + ('<i class="mk">A</i>' if mk_auto else "") + (
            '<i class="mk">↓</i>' if mk_next else "") + '</span>'
    h = lambda k: " hov" if hover == k else ""
    left = [b("audio-previous-line"), b("audio-play"), b("play-line"), b("media-stop"), b("audio-next-line"), SEP]
    if around:
        left.append(split(play, h("around")))
    marks_btns = [b(r, h(k)) for k, r, _, _ in MARK_PLAYS if vis(k)]
    time_btns = [b(r, h(k)) for k, r, _, _ in TIME_PLAYS if vis(k)]
    if marks_btns:
        left += ([SEP] if around else []) + marks_btns
    if time_btns:
        left += ([SEP] if around or marks_btns else []) + time_btns
    left += [SEP, b("lead-in"), b("lead-out"), split("commit", "", marks)]
    for k, r, _, _ in COMMIT_TOGGLES:
        if vis(k):
            left.append(b(r, ("tg" if (auto if k == "autoCommit" else nxt) else "") + h(k)))
    if vis("goto"):
        left.append(b("go-to-selection", h("goto")))
    gear = f'<span class="b{" on" if open_menu == "gear" else ""}{h("gear")}">{icon("settings")}</span>'
    zoomb = f'<span class="b zoom">{icon("zoom")}<span class="ro">{zoom}</span></span>'
    if fold:
        right = [b("panel-menu", "on" if open_menu == "overflow" else ""), zoomb, b(vol), gear]
    else:
        right = [seg(["audio-line-mode", "karaoke"], mode)]
        if mode == 1:
            right.append(b("karaoke-split", "tg"))
        right += [seg(["audio-waveform", "spectrum"], view), SEP, b("auto-scroll", "tg"), b("audio-video-sync", "tg"),
                  b("spectrum-nonlinear"), SEP, zoomb, b(vol), gear]
    return ('<div class="strip"><div class="grp">' + "".join(left) + '</div><div class="grow"></div>'
            '<div class="grp">' + "".join(right) + '</div></div>')


def vslider(frac, label):
    return (f'<div class="vs" title="{label}"><div class="vtrack"><div class="vfill" style="height:{frac * 100:.0f}%"></div>'
            f'<div class="vknob" style="bottom:{frac * 100:.0f}%"></div></div></div>')


def slider_column(t, height, link=True):
    """Legacy's column right of the display: horizontal zoom, then vertical zoom and volume with Link under them."""
    return (f'<div class="scol" style="height:{height}px">'
            f'<div class="scl"><span class="sl" title="Horizontal stretching">↔</span>{vslider(0.5, "Horizontal stretching")}</div>'
            f'<div class="scr"><div class="pair">'
            f'<div class="scl"><span class="sl" title="Vertical stretching">↕</span>{vslider(0.38, "Vertical stretching")}</div>'
            f'<div class="scl"><span class="sl">{icon("volume-low")}</span>{vslider(0.38, "Volume")}</div></div>'
            f'<span class="b lk{" tg" if link else ""}" title="Link the volume and stretch sliders">{icon("link")}</span>'
            f'</div></div>')


def body2(t, w, height, strip_html, *, sliders=False, **wave):
    disp_h = height - 4 - 8 - 30 - 6
    col_w = 74 if sliders else 0
    disp = f'<div class="dispwrap" style="height:{disp_h}px">{waveform(t, w - 2 - col_w, disp_h, **wave)}</div>'
    top = disp + scrollbar()
    if sliders:
        top = (f'<div class="toprow"><div class="dcol" style="width:{w - 2 - col_w}px">{top}</div>'
               f'{slider_column(t, disp_h + 10)}</div>')
    return top + strip_html


def customise_rows(shown, around, hover=None):
    rows = ['<div class="phead">Show on the panel</div>',
            mrow("play-before-start", "Play around button", "", checked=around, hover=hover == "around")]
    rows.append(MSEP)
    for k, r, text, keys in MARK_PLAYS + TIME_PLAYS:
        rows.append(mrow(r, text, keys, checked=k in shown, hover=hover == k))
    rows.append(MSEP)
    for k, r, text, keys in COMMIT_TOGGLES + [GO]:
        rows.append(mrow(r, text, keys, checked=k in shown, hover=hover == k))
    rows.append(MSEP)
    rows.append(mrow("link", "Zoom and volume sliders", "", checked="sliders" in shown, hover=hover == "sliders"))
    rows.append(MSEP)
    rows.append(mrow(None, "Classic arrangement", "", hover=hover == "classic"))
    rows.append(mrow(None, "Default arrangement", "", hover=hover == "default"))
    return rows


def sheet2(key, t):
    cells = []
    WIDE = 1100
    # 1 default, menu open
    s = strip2(t, open_menu="gear")
    body = body2(t, W, H, s)
    m = menu(customise_rows(set(), True, hover="goto"), left=W - 300, top=H + 38, width=296)
    cells.append(cell(1, "Customise (the gear at the row's right end): every control AU1 folded, each as its own item.",
                      panel(t, W, body, extra=m),
                      "Checked items show as buttons; the menu stays open while you tick. <b>Classic arrangement</b> "
                      "and <b>Default arrangement</b> (AU1's row) are one click each. The choice is a profile setting."))
    # 2 a custom pick
    pick = {"before", "after", "autoCommit", "goto"}
    s = strip2(t, shown=pick, hover="goto", fold=True)
    body = body2(t, W, H, s)
    tp = tip("Go to selection (B)", left=388, top=H + 40)
    cells.append(cell(2, "A custom pick: two plays, Auto-commit and Go shown as buttons.",
                      panel(t, W, body, extra=tp),
                      "Shown plays sit after Play around in legacy's order. A shown toggle sits after Commit and its "
                      "mark leaves Commit's face (here only <b>↓</b> remains). Every control stays in its menu too. "
                      "AU1's row fills the default 640 px dock, so any extra button folds the switches into ⋯ there."))
    # 3 classic, wide dock (full width)
    s = strip2(t, shown=CLASSIC, around=False)
    body = body2(t, WIDE, H, s, sliders=True)
    cells.append(cell(3, f"Classic arrangement in a wide dock ({WIDE} px): close to legacy's audio box.",
                      panel(t, WIDE, body),
                      "Play before/after the mark, the five 500 ms plays and Play to end as buttons (Play around "
                      "hidden), Auto-commit and Next-after-commit as toggles beside Commit, Go, and legacy's slider "
                      "column beside the display: horizontal stretch, then vertical stretch and volume with Link "
                      "under them. AU1's switches, zoom and speaker stay. "
                      "The Back to line chip still shows while the Line is out of view."))
    cells[-1] = cells[-1].replace('<div class="cell">', '<div class="cell wide">', 1)
    # 4 classic in the default 640 px dock: what does not fit goes into ⋯
    folded = ("autoCommit", "nextCommit", "goto")
    s = strip2(t, shown=CLASSIC, around=False, fold=True, folded=folded, open_menu="overflow")
    body = body2(t, W, H, s, sliders=True)
    rows = [mrow("auto-commit", "Auto-commit", "", checked=True), mrow("next-after-commit",
            "Go to the next line after commit", "", checked=True), mrow("go-to-selection", "Go to selection", "B"),
            MSEP, mrow("audio-line-mode", "Line", "", checked=True), mrow("karaoke", "Karaoke", "", checked=False),
            MSEP, mrow("audio-waveform", "Waveform", "", checked=True), mrow("spectrum", "Spectrum", "", checked=False),
            MSEP, mrow("auto-scroll", "Auto-scroll", "", checked=True),
            mrow("audio-video-sync", "Sync with video", "", checked=True),
            mrow("spectrum-nonlinear", "Speech (non-linear spectrum)", "", checked=False)]
    m = menu(rows, left=W - 330, top=H + 38, width=290)
    cells.append(cell(4, "Classic in the default 640 px dock: what does not fit folds into ⋯.",
                      panel(t, W, body, extra=m),
                      "AU1's fold first: the switches and toggles into ⋯. If the row still does not fit, the shown "
                      "extras follow from the right end (here Auto-commit, Next-after-commit and Go). Transport, the "
                      "shown plays, the timing group, zoom, speaker and the gear stay."))
    # 5 classic menu state
    s = strip2(t, shown=CLASSIC, around=False, fold=True, folded=folded, open_menu="gear")
    body = body2(t, W, H, s, sliders=True)
    m = menu(customise_rows(CLASSIC, False, hover="default"), left=W - 300, top=H + 38, width=296)
    cells.append(cell(5, "After Classic: the menu shows what it turned on; Default arrangement goes back to AU1.",
                      panel(t, W, body, extra=m),
                      "Classic is a starting point, not a mode: each item can still be ticked or cleared afterwards."))
    return cells


EXTRA_CSS = """
.cell.wide{grid-column:1 / span 2}
.stage{height:700px}
.cell.wide .stage{height:250px}
.toprow{display:flex}
.dcol{display:flex;flex-direction:column}
.scol{display:flex;gap:4px;padding:0 4px 0 6px;border-left:1px solid %(line)s}
.scl{display:flex;flex-direction:column;align-items:center;gap:3px;width:20px}
.scr{display:flex;flex-direction:column;align-items:center;gap:3px}
.pair{display:flex;gap:2px;flex:1}
.sl{font-size:11px;color:%(muted)s;height:16px;display:inline-flex;align-items:center}
.vs{flex:1;display:flex;justify-content:center;padding:4px 0}
.vtrack{position:relative;width:4px;height:100%%;background:%(line)s;border-radius:2px}
.vfill{position:absolute;left:0;right:0;bottom:0;background:%(accent)s;border-radius:2px}
.vknob{position:absolute;left:-6px;width:16px;height:16px;margin-bottom:-8px;border-radius:8px;background:%(panel)s;border:1px solid %(control)s}
.b.lk{width:42px}
"""


def page2(key, t):
    cells = sheet2(key, t)
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>AU2 Customise — {t["name"]}</title>'
            f'<style>{css(t)}{EXTRA_CSS % t}</style></head><body>'
            f'<div class="top"><h1>AU2 audio panel: Customise and Classic — {t["name"]}</h1>'
            f'<span>Mockup for #268, on AU1 as built (#267). Real icon set; no new icons.</span></div>'
            f'<div class="grid">{"".join(cells)}</div></body></html>')


for key, t in THEMES.items():
    (OUT / f"au2-{key}.html").write_text(page2(key, t))
print("wrote", ", ".join(f"au2-{k}.html" for k in THEMES))
