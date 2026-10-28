#!/usr/bin/env python3
"""TM1 #232 tag transfer mockup (docs/research/translation-tag-transfer.md): one sheet per theme.

usage: gen_tags.py <src/ui/icons> <out dir>
Reuses gen.py's themes, CSS and pieces (importing it also rewrites the tm1-*.html sheets).
"""
import html
import sys

import gen  # noqa: E402  (runs with the same argv)

T = gen.THEMES
icon, btn, svg = gen.icon, gen.btn, gen.svg

# Run tints: one per run number; high-contrast themes use the text colour with a pattern instead.
TINTS = ["#E8A33D", "#6BA4F2", "#C77DDB"]


def tint(t, n):
    return t["accent"] if t["hc"] else TINTS[(n - 1) % len(TINTS)]


def badge(t, n, hollow=False):
    c = tint(t, n)
    if hollow:
        return f'<span class="bdg hol" style="border-color:{c};color:{c}">{n}</span>'
    return f'<span class="bdg" style="background:{c};border-color:{c}">{n}</span>'


def run(t, n, text, kind, placed=True, sel=False, sugg=False):
    """A run in a field: rendered formatting, a tinted underline and its number."""
    c = tint(t, n)
    style = {"i": "font-style:italic", "b": "font-weight:700", "c": "color:#F0503C"}[kind]
    deco = (f"text-decoration:underline dotted {c} 2px;text-underline-offset:4px" if sugg else
            f"box-shadow:inset 0 -2px 0 {c}" if placed else "")
    s = " sel" if sel else ""
    return f'<span class="run{s}" style="{style};{deco}">{html.escape(text)}<sup style="color:{c}">{n}</sup></span>'


def chip(t, n, kind, text, placed):
    k = {"i": "Italic", "b": "Bold", "c": "Colour"}[kind]
    sample = {"i": f"<i>{text}</i>", "b": f"<b>{text}</b>", "c": f'<span style="color:#F0503C">{text}</span>'}[kind]
    cls = "chip" + ("" if placed else " hol")
    key = f'<span class="ck">Alt+{n}</span>'
    return f'<span class="{cls}">{badge(t, n, not placed)}<span class="ckind">{k}</span>{sample}{key}</span>'


def tagline(t, raw, label="Saved as"):
    return f'<div class="saved"><span class="sl">{label}</span><code>{html.escape(raw)}</code></div>'


def fromorig(t, tags, open_=False):
    """The 'tags from original' indicator under the Translated field."""
    body = f'<code>{html.escape(tags)}</code>' if open_ else ""
    return (f'<div class="fo">{icon("moving-tags")}<span>Tags from original</span>'
            f'<span class="fos">{"" if open_ else "· " + html.escape(tags_summary(tags))}</span>{body}'
            f'{icon("chev", "sm mut")}</div>')


def tags_summary(tags):
    return tags


def original_panel(t, body, w=640, tools=None, h=None):
    hh = f"height:{h}px;" if h else ""
    return (f'<div class="dock" style="width:{w}px;{hh}">{gen.head_tabs("Original text", tools if tools is not None else gen.original_tools())}'
            f'<div class="dbody pad"><div class="fld" style="min-height:40px">{body}</div></div></div>')


def editor(t, body, below="", w=640, focus=True, placeholder=False):
    ph = '<span class="ph">Type the translation</span>' if placeholder else ""
    return (f'<div class="dock" style="width:{w}px;margin-top:4px">{gen.head_title("Line editor")}'
            f'<div class="dbody pad">{gen.tagstrip()}'
            '<div class="counters">Wraps: 1/27&nbsp;&nbsp;Characters per second: 11&lt;=15</div>'
            f'<div class="fld{" focus" if focus else ""}" style="min-height:44px">{ph}{body}</div>{below}</div></div>')


def runs_strip(chips):
    return f'<div class="rstrip"><span class="rsl">Runs</span>{"".join(chips)}<span class="grow"></span></div>'


CARET = '<span class="caret"></span>'


def sheet(t):
    cells = []
    # 1. Easy case: leading, line-wide and whole-line tags are applied, the translator types the text.
    orig = ('<span class="hid">{…}</span><i>Did you hear that?</i><span class="hid">{…}</span>')
    before = original_panel(t, orig) + editor(t, CARET, fromorig(t, "\\an8\\fad(200,0) · italic, whole line"),
                                              placeholder=True)
    after = (original_panel(t, orig)
             + editor(t, '<i>今の、聞こえた？</i>' + CARET, fromorig(t, "\\an8\\fad(200,0) · italic, whole line"))
             + tagline(t, r"{\an8\fad(200,0)\i1}今の、聞こえた？{\i0}"))
    cells.append(gen.cell(1, "Simple Lines: the original’s tags are already on the translation; you type only the text.",
        "Original <code>{\\an8\\fad(200,0)\\i1}Did you hear that?{\\i0}</code>. Line-wide tags (\\pos \\move \\org \\fad \\fade "
        "\\clip \\iclip \\an \\a \\q), everything before the first letter, everything after the last, and a run that covers "
        "the whole Line are carried over silently. The Translated field shows the text as it will look (italic here); a "
        "small “Tags from original” line under it opens to show them. The Original text keeps its tags (legacy Moving tags "
        "strips them: a named departure). Left: an untranslated Line. Right: after typing.",
        '<div class="pair"><div>' + before + '</div><div>' + after + '</div></div>', 330, wide=True))

    # 2. Hard case: runs inside the Line, word order changes.
    src = ('She ' + run(t, 1, "really ", "i") + run(t, 2, "hates", "b") + ' the rain.')
    step1 = (original_panel(t, src)
             + editor(t, 'Sie <span class="selx">hasst</span> den Regen wirklich.',
                      runs_strip([chip(t, 1, "i", "really", False), chip(t, 2, "b", "hates", False)]))
             + '<div class="tip" style="left:330px;top:150px">Bold run <b>2</b> · Alt+2<div class="dim">Places the run on the selected text</div></div>')
    step2 = (original_panel(t, src)
             + editor(t, 'Sie ' + run(t, 2, "hasst", "b") + ' den Regen ' + run(t, 1, "wirklich", "i") + '.' + CARET,
                      runs_strip([chip(t, 1, "i", "really", True), chip(t, 2, "b", "hates", True)]))
             + tagline(t, r"Sie {\b1}hasst{\b0} den Regen {\i1}wirklich{\i0}."))
    cells.append(gen.cell(2, "Runs inside the Line: write the sentence your way, then select a word and press its run key.",
        "Original <code>She {\\i1}really {\\i0\\b1}hates{\\b0} the rain.</code> (the <code>text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3</code> "
        "shape). The Original text shows each run in its formatting with a number; the Translated field is plain text and "
        "a Runs strip lists what is still to place (hollow). Select “hasst”, press Alt+2 (or click the chip): it turns bold "
        "and the chip fills. Runs go in any order — here 2 before 1, as German needs. “Place next run” (one key, like memoQ "
        "F9 / Phrase F8) takes the lowest unplaced run; Ctrl+B / Ctrl+I / Ctrl+U on a selection place the matching run when "
        "the original has one. Pressing a run key again on placed text removes the run; Ctrl+Z undoes as usual.",
        '<div class="pair"><div style="position:relative">' + step1 + '</div><div>' + step2 + '</div></div>', 360, wide=True))

    # 3. Suggestions: a run whose text comes back unchanged.
    src3 = 'Ask ' + run(t, 1, "Kaito", "c") + ' about the key.'
    sug = (original_panel(t, src3)
           + editor(t, 'Frag ' + run(t, 1, "Kaito", "c", placed=False, sugg=True) + ' nach dem Schlüssel.' + CARET,
                    runs_strip([chip(t, 1, "c", "Kaito", False)]))
           + '<div class="tip" style="left:300px;top:150px">Suggested: colour run <b>1</b><div class="dim">Tab places it · Esc dismisses</div></div>')
    cells.append(gen.cell(3, "A suggestion, never a silent guess.",
        "When a run’s text appears unchanged in the translation (names, numbers, titles), it is offered with a dotted "
        "underline; Tab accepts, Esc dismisses, typing on ignores it. Only this exact-match signal at first: no word "
        "aligner or online translation service (published tag-placement accuracy is 72–95 %).",
        sug, 300))

    # 4. Unplaced runs in the Grid.
    rows = [("12", "Sie hasst den Regen wirklich.", True), ("13", "Frag Kaito nach dem Schlüssel.", False),
            ("14", "Gehen wir.", False)]
    trs = "".join(
        f'<tr{" class=\"cur\"" if cur else ""}><td>{n}</td><td class="txt">{txt}</td>'
        f'<td>{"" if n != "13" else badge(t, 1, True) + " 1 run not placed"}</td></tr>' for n, txt, cur in rows)
    grid = (f'<div class="dock" style="width:640px">{gen.head_tabs("Grid", "")}'
            f'<div class="dbody"><table class="grid"><tr><th>#</th><th class="txt">Text</th><th>Runs</th></tr>{trs}</table></div></div>')
    cells.append(gen.cell(4, "A run left unplaced never blocks you; the Grid marks the Line.",
        "Committing with runs unplaced writes no empty tag pairs. The Line gets a “runs not placed” mark (a column the "
        "Grid can show and filter on), separate from Not confirmed. Brace comments, \\N breaks, karaoke (\\k) and drawings "
        "(\\p) are not carried: you break lines for your language; karaoke and drawing Lines get the leading tags only.",
        grid, 300))

    # 5. The header toggle.
    tools = (btn("paste-translation", "", "Paste all") + btn("paste-selection-translation", "", "Paste the selected")
             + btn("comment-original", "", "Comment out original") + '<span class="sep"></span>'
             + btn("unconfirmed", "", "Not confirmed") + btn("moving-tags", "tg hov", "Tags from original"))
    hdr = (original_panel(t, '<i>Did you hear that?</i>', tools=tools)
           + '<div class="tip" style="left:400px;top:40px">Tags from original<div class="dim">New translations take the original’s tags; you type only the text</div></div>')
    cells.append(gen.cell(5, "“Moving tags” becomes “Tags from original”, on by default.",
        "Same button and setting (translation.autoMoveTagsFromOriginal, default on). Off: the Translated text is free raw "
        "text, as Moving tags off is today.",
        hdr, 170))

    # 6. Editing the original later.
    later = (original_panel(t, '<span class="hid">{…}</span><i>Did you hear that?</i>')
             + editor(t, '<i>今の、聞こえた？</i>', fromorig(t, "\\an8\\fad(200,0)\\pos(640,80) · italic, whole line"), focus=False)
             + tagline(t, r"{\an8\fad(200,0)\pos(640,80)\i1}今の、聞こえた？{\i0}"))
    cells.append(gen.cell(6, "The translation follows later edits to the original’s tags.",
        "Dragging \\pos on video or changing \\fad in the original updates the translation’s carried tags in the same "
        "undo step — unless they were edited by hand there, then the Line is marked “tags differ from original” and "
        "nothing is overwritten. Placed runs follow value changes (a new colour); a run added to the original shows "
        "as a new chip.",
        later, 300))
    return cells


def css(t):
    hc = t["hc"]
    return gen.css(t) + f"""
.pair{{display:grid;grid-template-columns:640px 640px;gap:24px}}
.hid{{color:{t['muted']};font-size:11px;border:1px dashed {t['line']};border-radius:2px;padding:0 2px;margin:0 2px;vertical-align:1px}}
.run{{padding:0 1px}}
.run sup{{font-size:9px;font-weight:700;font-style:normal;margin-left:1px}}
.run.sel,.selx{{background:{t['select']};outline:1px solid {t['accent']}}}
.bdg{{display:inline-flex;width:16px;height:16px;border-radius:8px;border:1.5px solid;align-items:center;justify-content:center;font-size:10px;font-weight:700;color:{'#000' if not hc else t['onaccent']};flex:none}}
.bdg.hol{{background:none}}
.rstrip{{display:flex;align-items:center;gap:6px;flex-wrap:wrap}}
.rsl{{font-size:12px;color:{t['muted']};margin-right:2px}}
.chip{{display:inline-flex;align-items:center;gap:6px;height:24px;padding:0 6px 0 4px;border:1px solid {t['control']};border-radius:12px;background:{t['select']};font-size:12.5px}}
.chip.hol{{background:none;border-style:dashed}}
.ckind{{color:{t['muted']};font-size:11.5px}}
.ck{{color:{t['muted']};font-size:11px;margin-left:2px}}
.fo{{display:flex;align-items:center;gap:6px;font-size:12px;color:{t['muted']}}}
.fo code{{font-size:11.5px;color:{t['text']}}}
.fos{{flex:1}}
.saved{{display:flex;gap:10px;align-items:baseline;margin:8px 2px 0;font-size:12px}}
.saved .sl{{color:{t['muted']};flex:none}}
.saved code{{font-family:"Noto Sans Mono","DejaVu Sans Mono",monospace;font-size:12px;background:{t['field']};border:1px solid {t['line']};padding:2px 6px;border-radius:2px}}
.note code{{font-family:"Noto Sans Mono","DejaVu Sans Mono",monospace;font-size:11px;color:{t['text']}}}
.grid td .bdg{{vertical-align:-3px}}
"""


def page(t):
    return (f'<!doctype html><html><head><meta charset="utf-8"><title>TM1 tags from original — {t["name"]}</title>'
            f'<style>{css(t)}</style></head><body>'
            f'<div class="top"><h1>TM1 Tags from original — {t["name"]}</h1>'
            f'<span>Mockup for #232, from docs/research/translation-tag-transfer.md (design A with B’s exact-match suggestions).</span></div>'
            f'<div class="sheet">{"".join(sheet(t))}</div></body></html>')


for key, t in T.items():
    (gen.OUT / f"tags-{key}.html").write_text(page(t))
print("wrote", ", ".join(f"tags-{k}.html" for k in T))
