
# =============================================================================================
# KA3 #278: the Kanji timer. A DG1 window (Subtitles > Kanji timer…, after Check karaoke): the song's
# romaji/kanji Line pairs on the left, the current pair's grouping strip on the right, Aegisub's
# DialogKanjiTimer behaviour (Link, Unlink, skip a romaji or kanji line, go back, accept a line;
# arrows grow and shrink the selections, Enter links, Backspace unlinks) on the PK1 kit.
# The lyrics are written for the mockup.

SUCCESS = {"light": "#008000", "dark": "#4DBA4D", "hcwhite": "#005A00", "hcblack": "#7CFC7C"}

GLYPHS.update({
    "check-small": '<path d="M3.5 8.5l3 3 6-7"/>',
    "dash": '<path d="M4.5 8h7"/>',
    "dot": '<circle cx="8" cy="8" r="2.5" fill="currentColor" stroke="none"/>',
    "ring-dot": '<circle cx="8" cy="8" r="3"/>',
    "changed": '<path d="M2.5 8a5.5 5.5 0 0 1 9.6-3.6M13.5 8a5.5 5.5 0 0 1-9.6 3.6"/>'
               '<path d="M12.5 1.5v3h-3M3.5 14.5v-3h3"/>',
})
REPO_ICONS += ["karaoke", "karaoke-split", "spellchecker", "font-collector", "resample"]


def syl(text, cs, cls=""):
    return f'<span class="syl {cls}"><b>{text}</b><i>{cs}</i></span>'


def group(syls, kanji, kind="done", hover=False, blank_note=False):
    """syls: [(romaji, cs)], kanji: the destination text of the group ('' = a blank)."""
    tops = "".join(syl(t, c) for t, c in syls)
    kcls = "kan" + (" blank" if not kanji else "") + (" hov" if hover else "")
    return (f'<div class="g {kind}"><div class="ss">{tops}</div>'
            f'<div class="{kcls}">{kanji}</div></div>')


def rest(syls, kanji, hover_at=None):
    tops = "".join(syl(t, c) for t, c in syls)
    kan = "".join(f'<span class="rk{" hov" if i == hover_at else ""}">{k}</span>' for i, k in enumerate(kanji))
    return f'<div class="g rest"><div class="ss">{tops}</div><div class="kan rest">{kan}</div></div>'


def strip(groups, focus=True, scroll_left=False):
    more = f'<span class="more" title="Earlier groups">{ic("chevron-left", 14)}</span>' if scroll_left else ""
    return f'<div class="kst{" focus" if focus else ""}">{more}{"".join(groups)}</div>'


def result(parts, total):
    """parts: [(tag, text)]; tag text drawn in the muted colour as the Line editor's tags are."""
    body = "".join((f'<span class="rt">{{{t}}}</span>' if t else "") + f'<span class="rx">{x}</span>'
                   for t, x in parts)
    return (f'<div class="res"><span class="rlab">Result</span><span class="rfld">{body}</span>'
            f'<span class="rtot">{total}</span></div>')


def cmds(accept_enabled=True, prev=True, unlink=True):
    a = btn("Accept line", small=True) if accept_enabled else '<span class="b sm dis">Accept line</span>'
    u = btn("Unlink", small=True) if unlink else '<span class="b sm dis">Unlink</span>'
    p = btn(ic("chevron-left", 14) + "Previous", small=True) if prev else \
        f'<span class="b sm dis">{ic("chevron-left", 14)}Previous</span>'
    return (f'<div class="kcmd">{btn("Link", small=True)}{u}<span class="vsep"></span>'
            f'{btn("Skip romaji line", small=True)}{btn("Skip kanji line", small=True)}'
            f'<span class="grow"></span>{p}{a}</div>')


def keyhint():
    k = lambda s: f'<span class="kk">{s}</span>'
    return (f'<div class="khint">{k("↑")}{k("↓")} romaji {k("←")}{k("→")} kanji {k("Enter")} link '
            f'{k("Backspace")} unlink {k("Ctrl+Enter")} accept line {k("Ctrl+Z")} undo</div>')


def status(kind):
    return {
        "done": f'<span class="st ok">{ic("check-small", 14)}Done</span>',
        "applied": f'<span class="st">{ic("check-small", 14)}Applied</span>',
        "cur": '<span class="st cur">5 of 8 groups</span>',
        "sug": f'<span class="st">{ic("ring-dot", 14)}Suggested</span>',
        "check": f'<span class="st warn">{ic("notice-warning", 14)}Check</span>',
        "noromaji": f'<span class="st">{ic("dash", 14)}No romaji line</span>',
        "nokanji": f'<span class="st">{ic("dash", 14)}No kanji line</span>',
        "changed": f'<span class="st warn">{ic("changed", 14)}Changed</span>',
    }[kind]


def pair_row(n, kanji, romaji, kind, sel=False, hover=False):
    k = f'<div class="pk">{kanji}</div>' if kanji else '<div class="pk none">—</div>'
    r = f'<div class="pr">{romaji}</div>' if romaji else '<div class="pr">—</div>'
    cls = "prow" + (" sel" if sel else "") + (" hov" if hover else "")
    return (f'<div class="{cls}"><span class="pn">{n}</span><div class="pt">{k}{r}</div>{status(kind)}</div>')


SONG = [
    (1, "夢を見ていた", "yume wo mite ita", "applied"),
    (2, "眠れない夜に", "nemurenai yoru ni", "applied"),
    (3, "君の声が聞こえる", "kimi no koe ga kikoeru", "cur"),
    (4, "遠い空の向こうへ", "tooi sora no mukou e", "sug"),
    (5, "星を数えて", "hoshi wo kazoete yo", "check"),
    (6, "", "la la la", "nokanji"),
    (7, "明日へ走り出す", "ashita e hashiridasu", "sug"),
    (8, "一緒に歌おう", "issho ni utaou", "sug"),
]


def pair_list(rows=SONG, cur=3, height=None):
    st = f' style="height:{height}px"' if height else ""
    body = "".join(pair_row(n, k, r, s, sel=(n == cur)) for n, k, r, s in rows)
    return f'<div class="plist"{st}>{body}<span class="sbar" style="top:4px;height:120px"></span></div>'


def topline(romaji="Song romaji", kanji="Song kanji", suggest=True, err=""):
    e = f'<div class="ferr">{ic("notice-error", 14, "err")}{err}</div>' if err else ""
    return (f'<div class="ktop"><span class="tl">Romaji</span>{combo(romaji, 170)}'
            f'<span class="tl">Kanji</span>{combo(kanji, 170)}<span class="grow"></span>'
            f'{checkbox("Suggest groupings", suggest)}</div>{e}')


# The current pair in the main state: 君の声が聞こえる over ki|mi |no |ko|e |ga |ki|ko|e|ru.
G_KIMI = (("ki", 18), ("mi", 22))
MAIN_GROUPS = [
    group(G_KIMI, "君"), group((("no", 30),), "の"), group((("ko", 20), ("e", 25)), "声"),
    group((("ga", 35),), "が"),
    group((("ki", 22),), "聞", "cur"),
    group((("ko", 18),), "こ", "sug"), group((("e", 20),), "え", "sug"), group((("ru", 60),), "る", "sug"),
]
MAIN_RESULT = [("\\k40", "君"), ("\\k30", "の"), ("\\k45", "声"), ("\\k35", "が"), ("\\k22", "聞"),
               ("\\k18", "こ"), ("\\k20", "え"), ("\\k60", "る")]


def detail(text):
    return f'<div class="kdet">{text}</div>'


def pair_head(times, extra=""):
    return f'<div class="phead"><span class="ptimes">{times}</span>{extra}</div>'


def kt_window(body_right, list_html=None, top=None, foot=None, width=1040, pending=2):
    left = list_html if list_html is not None else pair_list()
    body = (f'{top if top is not None else topline()}<div class="kmain"><div class="kleft">{left}</div>'
            f'<div class="kright">{body_right}</div></div>')
    if foot is None:
        foot = btn("Accept all suggestions", lead=True) + btn("Apply", primary=True) + btn("Close")
    return window("Kanji timer", body, foot, width, resizable=True)


def main_right(focus=True):
    return (pair_head("0:00:14.10 – 0:00:16.80", '<span class="pdur">2.70 s</span>')
            + strip(MAIN_GROUPS, focus=focus)
            + detail('<span class="dk">聞</span> ← ki <span class="dr">(き)</span> · 22 cs')
            + cmds() + result(MAIN_RESULT, "270 cs") + keyhint())


# ---------------------------------------------------------------------------------------------
# States


def check_right(linked=False):
    """Line 5: 星を数えて over ho|shi |wo |ka|zo|e|te |yo: the romaji sings a "yo" the kanji does not write."""
    gs = [group((("ho", 30), ("shi", 25)), "星"), group((("wo", 30),), "を"),
          group((("ka", 20), ("zo", 22)), "数"), group((("e", 18),), "え"), group((("te", 40),), "て")]
    if linked:
        gs.append(group((("yo", 55),), ""))
        res = result([("\\i1\\kf55", "星"), ("\\kf30", "を"), ("\\kf42", "数"), ("\\kf18", "え"),
                      ("\\kf40", "て"), ("\\kf55", "")], "240 cs")
        det = detail('<span class="dk blank">blank</span> ← yo · 55 cs')
        return (pair_head("0:00:22.30 – 0:00:24.70", '<span class="pdur">2.40 s</span>') + strip(gs)
                + det + cmds() + res)
    gs = gs[:-1] + [group((("te", 40),), "て", "done"), group((("yo", 55),), "", "cur")]
    warn = (f'<div class="kwarn">{ic("notice-warning", 16, "warn")}<span>“yo” has no kanji left. '
            f'Enter links it on its own (a blank syllable), or ↑ on “て” takes it into that group.</span></div>')
    return (pair_head("0:00:22.30 – 0:00:24.70", '<span class="pdur">2.40 s</span>') + strip(gs)
            + warn + cmds(accept_enabled=False))


def hover_right():
    gs = [group(G_KIMI, "君"), group((("no", 30),), "の"),
          group((("ko", 20), ("e", 25)), "声", "cur"),
          rest((("ga", 35), ("ki", 22), ("ko", 18), ("e", 20), ("ru", 60)), "が聞こえる", hover_at=0)]
    return (pair_head("0:00:14.10 – 0:00:16.80", '<span class="pdur">2.70 s</span>')
            + strip(gs) + detail('Click a kanji: the current group ends there. Click a syllable: its romaji does.'))


def styles_window(case):
    if case == "same":
        top = topline("Song romaji", "Song romaji", err="Choose two different styles.")
        lst = '<div class="plist empty short"><span>No lines to pair</span></div>'
    else:
        top = topline("Song kanji", "Song romaji")
        lst = ('<div class="plist empty short"><span>No karaoke lines in “Song kanji”</span>'
               '<span class="esub">The romaji style needs Lines timed with \\k tags.</span></div>')
    right = '<div class="kempty short">Choose the romaji and kanji styles</div>'
    foot = '<span class="b lead dis">Accept all suggestions</span><span class="b dis">Apply</span>' + btn("Close")
    return kt_window(right, list_html=lst, top=top, foot=foot, width=760)


def list_kinds():
    rows = [pair_row(2, "眠れない夜に", "nemurenai yoru ni", "applied"),
            pair_row(3, "君の声が聞こえる", "kimi no koe ga kikoeru", "done"),
            pair_row(4, "遠い空の向こうへ", "tooi sora no mukou e", "sug", hover=True),
            pair_row(5, "星を数えて", "hoshi wo kazoete yo", "check"),
            pair_row(6, "", "la la la", "nokanji"),
            pair_row(9, "（間奏）", "", "noromaji"),
            pair_row(10, "君の名前を", "kimi no namae wo", "changed")]
    return f'<div class="plist solo">{"".join(rows)}</div>'


def close_box():
    return message_box("Kanji timer", "Apply the kanji timing of 3 lines?",
                       "Lines 3, 4 and 7 are done but not in the Document yet.", "warning",
                       btn("Apply", primary=True, focus=True) + btn("Discard", danger=True) + btn("Cancel"))


def subtitles_menu():
    items = [("font-collector", "Font collector", ""), ("resample", "Resample subtitles", ""),
             ("spellchecker", "Check spelling", ""), ("karaoke", "Check karaoke", ""),
             ("karaoke-split", "Kanji timer…", "hl")]
    rows = "".join(f'<div class="mi {c}"><span class="mic">{ic(i)}</span><span class="mt">{t}</span></div>'
                   for i, t, c in items)
    return f'<div class="menu" style="width:240px"><div class="mi dis"><span class="mic"></span>…</div>{rows}</div>'


# ---------------------------------------------------------------------------------------------
# Pages


def window_page(key):
    th = THEMES[key]
    return (f'<h1>KA3 Kanji timer: {th["name"]}</h1>'
            '<p class="note">Subtitles › Kanji timer… opens a DG1 window, modeless and resizable. Left: the song’s '
            'romaji/kanji Line pairs in Document order. Right: the chosen pair. Groups already linked are solid, '
            'the current group is selected, and the rest of the suggestion is dashed. The strip has keyboard focus. '
            'Nothing goes into the Document until Apply, which is one undo step.</p>'
            f'<div class="row"><div class="col">{kt_window(main_right())}</div></div>'
            '<h2>Where it opens</h2>'
            f'<div class="row"><div class="col">{subtitles_menu()}<p class="cap">Subtitles menu, after Check karaoke. '
            'The command can take a key (a new Global hotkey id, no default key).</p></div></div>')


def states_page(key):
    th = THEMES[key]

    def item(html, cap, w=None):
        st = f' style="width:{w}px"' if w else ""
        return f'<div class="col"{st}>{html}<p class="cap">{cap}</p></div>'

    def pane(html, w=700):
        return f'<div class="pane" style="width:{w}px">{html}</div>'

    return (f'<h1>KA3 Kanji timer: states, {th["name"]}</h1>'
            '<h2>A pair the suggestion cannot finish</h2><div class="row">'
            + item(pane(check_right(False)),
                   "Line 5’s romaji sings a “yo” its kanji does not write. The suggestion stops there and the pair "
                   "shows “Check” in the list. Accept line stays off until every syllable is in a group; the reason "
                   "is in the strip and in its tooltip, so no message box is needed.")
            + item(pane(check_right(True)),
                   "After Enter, “yo” is a blank group. The kanji Line keeps its own tags ({\\i1}), only its old "
                   "\\k tags are replaced, and each group takes its romaji’s tag (\\kf here). The groups add up "
                   "to the romaji Line’s 240 cs.")
            + '</div><h2>Mouse</h2><div class="row">'
            + item(pane(hover_right()),
                   "With the pointer over が, a click ends the current group there (声が); a click on a romaji syllable "
                   "ends its romaji. A click on a linked group reopens it, unlinking it and every group after it.")
            + '</div><h2>The pair list</h2><div class="row">'
            + item(list_kinds(),
                   "Every state a pair can be in: Applied, Done (not applied yet), Suggested (the hovered row), "
                   "Check, a romaji Line with no kanji partner and the reverse, and Changed (its Lines were edited "
                   "after it was done, so its grouping is gone). Clicking a row opens that pair.", 340)
            + item(close_box(), "Close with done pairs that are not applied yet. Esc does the same.", 480)
            + '</div><h2>Choosing the styles</h2><div class="row">'
            + item(styles_window("same"), "The same style twice: the reason under the choices, nothing to pair.")
            + item(styles_window("none"),
                   "A romaji style with no \\k Lines: the list says so in one muted line. The window guesses the "
                   "styles on opening (the style with the most \\k Lines, then one named kanji or jp).")
            + '</div>')


KA3_CSS = """
body{font-family:'Liberation Sans','Noto Sans CJK JP',Arial,sans-serif}
.grow{flex:1}
.dis{opacity:.46}
.b.sm .ic{margin-right:4px}
.ktop{display:flex;align-items:center;gap:8px;margin:4px 0 12px}
.tl{color:%(text)s}
.ktop .tl+.fld{margin-right:12px}
.ferr{display:flex;align-items:center;gap:6px;color:%(danger)s;margin:-4px 0 10px 58px}
.kmain{display:flex;gap:16px;align-items:stretch}
.kleft{width:300px;flex:none;display:flex}
.kright{flex:1;min-width:0;display:flex;flex-direction:column;gap:10px}
.pane{background:%(panel)s;outline:1px solid %(line)s;padding:16px;display:flex;flex-direction:column;gap:10px}

/* the pair list */
.plist{position:relative;flex:1;background:%(field)s;outline:1px solid %(line)s;padding:2px 0;overflow:hidden;min-height:300px}
.plist.solo{width:340px;min-height:0}
.plist.empty{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;color:%(muted)s;text-align:center;padding:0 16px}
.esub{font-size:12px}
.plist.short,.kempty.short{min-height:150px}
.prow{display:flex;align-items:center;gap:10px;padding:5px 12px 5px 10px;min-height:44px}
.prow+.prow{box-shadow:inset 0 1px %(rowline)s}
.prow.sel{background:%(select)s;box-shadow:inset 3px 0 %(accent)s}
.prow.hov{background:%(raised)s}
.pn{width:16px;color:%(muted)s;font:12px 'Liberation Mono',Consolas,monospace;text-align:right;flex:none}
.pt{flex:1;min-width:0}
.pk{font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pk.none,.pr{color:%(muted)s}
.pr{font-size:12px;margin-top:1px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.st{display:flex;align-items:center;gap:4px;font-size:12px;color:%(muted)s;flex:none}
.st.ok{color:%(success)s}
.st.cur{color:%(text)s}

/* the current pair */
.phead{display:flex;align-items:baseline;gap:10px}
.ptimes{font:13px 'Liberation Mono',Consolas,monospace;font-variant-numeric:tabular-nums}
.pdur{color:%(muted)s;font-size:12px}
.kst{display:flex;gap:6px;align-items:stretch;padding:12px;background:%(field)s;border:1px solid %(control)s;
     border-radius:1px;overflow:hidden;position:relative}
.kst.focus{outline:1px solid %(text)s;outline-offset:1px}
.lab{display:flex;flex-direction:column;justify-content:space-between;width:50px;flex:none;color:%(muted)s;font-size:11px;
     padding:12px 0 13px}
.more{display:flex;align-items:center;color:%(muted)s}
.g{display:flex;flex-direction:column;gap:4px;flex:none}
.ss{display:flex;gap:2px}
.syl{display:flex;flex-direction:column;align-items:center;justify-content:center;min-width:32px;height:40px;padding:0 6px;
     border:1px solid %(line)s;background:%(panel)s;border-radius:1px}
.syl b{font-weight:400;font-size:13px;line-height:1.2}
.syl i{font-style:normal;font-size:10px;color:%(muted)s;font-family:'Liberation Mono',Consolas,monospace}
.kan{display:flex;align-items:center;justify-content:center;height:44px;font-size:22px;border:1px solid %(line)s;
     background:%(panel)s;border-radius:1px;font-family:'Noto Sans CJK JP',sans-serif}
.kan.blank{background:repeating-linear-gradient(135deg,transparent 0 5px,%(rowline)s 5px 6px)}
.g.sug .syl,.g.sug .kan{border-style:dashed;border-color:%(control)s;background:transparent}
.g.cur .syl,.g.cur .kan{background:%(select)s;border-color:%(accent)s}
.g.cur .syl i{color:%(text)s}
.g.cur .kan.blank{background:repeating-linear-gradient(135deg,%(select)s 0 5px,%(accent)s 5px 6px)}
.g.rest .syl{border-color:transparent;background:transparent}
.g.rest .syl b{color:%(muted)s}
.kan.rest{border-color:transparent;background:transparent;color:%(muted)s;justify-content:flex-start;gap:2px}
.rk{padding:0 3px;border:1px solid transparent;border-radius:1px}
.rk.hov{border-color:%(accent)s;color:%(text)s;background:%(raised)s}
.kdet{color:%(muted)s;display:flex;align-items:baseline;gap:6px}
.dk{font-family:'Noto Sans CJK JP',sans-serif;font-size:16px;color:%(text)s}
.dk.blank{font-family:inherit;font-size:13px;font-style:italic;color:%(muted)s}
.dr{font-family:'Noto Sans CJK JP',sans-serif}
.kwarn{display:flex;align-items:flex-start;gap:8px;line-height:1.45}
.kcmd{display:flex;align-items:center;gap:6px;flex-wrap:wrap}
.vsep{width:1px;height:20px;background:%(line)s;margin:0 4px}
.res{display:flex;align-items:center;gap:8px}
.rlab{color:%(text)s;flex:none}
.rfld{flex:1;min-width:0;display:flex;align-items:center;height:30px;padding:0 8px;background:%(field)s;border:1px solid %(line)s;
      border-radius:1px;white-space:nowrap;overflow:hidden;font:13px 'Liberation Mono','Noto Sans CJK JP',monospace}
.rt{color:%(muted)s}
.rx{color:%(text)s;font-family:'Noto Sans CJK JP',sans-serif}
.rtot{color:%(muted)s;font-size:12px;flex:none;font-variant-numeric:tabular-nums}
.khint{display:flex;align-items:center;gap:5px;flex-wrap:wrap;color:%(muted)s;font-size:12px;margin-top:2px}
.kk{display:inline-flex;align-items:center;height:18px;padding:0 5px;border:1px solid %(line)s;border-radius:2px;
    background:%(raised)s;color:%(text)s;font-size:11px;margin-left:6px}
.kk:first-child{margin-left:0}
.kk+.kk{margin-left:0}
.kempty{flex:1;display:flex;align-items:center;justify-content:center;color:%(muted)s;min-height:300px;
        background:%(field)s;outline:1px solid %(line)s}

/* menus */
.menu{background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;box-shadow:0 10px 30px rgba(0,0,0,%(shadow)s)}
.mi{display:flex;align-items:center;height:26px;padding:0 12px 0 6px;gap:6px}
.mi.hl{background:%(select)s}
.mi.dis{color:%(muted)s;font-size:12px}
.mic{width:20px;display:flex;justify-content:center}
.mt{flex:1}
"""


def ka3_main():
    for key in THEMES:
        for name, fn in (("window", window_page), ("states", states_page)):
            html = page(key, fn(key))
            th = THEMES[key]
            extra = KA3_CSS % dict(th, shadow=".55" if key in DARKISH else ".28", success=SUCCESS[key],
                                   rowline=th["line"] + "7A")
            html = html.replace("</style>", extra + "</style>", 1)
            (OUT / f"ka3-{key}-{name}.html").write_text(html)


ka3_main()
