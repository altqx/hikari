
# =============================================================================================
# KA4 #279 step 3: furigana in the Kanji timer. A Furigana switch beside KA3's Suggest groupings and
# KA8's Multi-highlight (#); the reading of each kanji group drawn over its kanji in the strip; a
# Reading field for the current group; the stock furigana syntax (漢|かん, #|) in the Result.
# The lyrics are written for the mockup.

REPO_ICONS += ["restore-default", "dictionary"]


def kgroup(syls, kanji, reading=None, kind="done", typed=False, differs=False):
    """A KA3 group whose kanji cell carries its reading above the kanji (None: no kanji, no reading)."""
    tops = "".join(syl(t, c) for t, c in syls)
    if reading is None:
        kan = f'<div class="kan fk">{kanji}</div>'
    else:
        rcls = "rb" + (" typed" if typed else "") + (" none" if reading == "" else "")
        mark = f'<span class="dif" title="The dictionary reads it differently">{ic("dot", 10)}</span>' if differs else ""
        kan = (f'<div class="kan fk"><span class="{rcls}">{reading or "&nbsp;"}</span>'
               f'<span class="kb">{kanji}</span>{mark}</div>')
    return f'<div class="g {kind}"><div class="ss">{tops}</div>{kan}</div>'


def ftopline(furigana=True, multi=False, note=""):
    n = f'<div class="fnote">{ic("dictionary", 14)}{note}</div>' if note else ""
    return (f'<div class="ktop"><span class="tl">Romaji</span>{combo("Song romaji", 170)}'
            f'<span class="tl">Kanji</span>{combo("Song kanji", 170)}<span class="grow"></span>'
            f'{checkbox("Suggest groupings", True)}{checkbox("Multi-highlight (#)", multi)}'
            f'{checkbox("Furigana", furigana)}</div>{n}')


def reading_row(kanji, romaji, kana, cs, reading, typed=False, focus=False, hint="", disabled=False):
    left = (f'<span class="dk">{kanji}</span> ← {romaji} <span class="dr">({kana})</span> · {cs} cs')
    if disabled:
        return f'<div class="kdet">{left}</div>'
    fld = field(reading or "No furigana", 110, placeholder=not reading, focus=focus)
    reset = (f'<span class="b sm flat ibtn{" dis" if not typed else ""}" title="Use the suggestion">'
             f'{ic("restore-default", 16)}</span>')
    h = f'<span class="fhint">{ic("dictionary", 14)}{hint}</span>' if hint else ""
    return (f'<div class="kdet">{left}<span class="grow"></span>{h}<span class="tl rl">Reading</span>'
            f'{fld}{reset}</div>')


# Line 3 again: 君の声が聞こえる over ki|mi no ko|e ga ki ko e ru.
F_GROUPS = [
    kgroup(G_KIMI, "君", "きみ"), kgroup((("no", 30),), "の"), kgroup((("ko", 20), ("e", 25)), "声", "こえ"),
    kgroup((("ga", 35),), "が"), kgroup((("ki", 22),), "聞", "き", "cur"),
    kgroup((("ko", 18),), "こ", None, "sug"), kgroup((("e", 20),), "え", None, "sug"),
    kgroup((("ru", 60),), "る", None, "sug"),
]
F_RESULT = [("\\k40", "君|きみ"), ("\\k30", "の"), ("\\k45", "声|こえ"), ("\\k35", "が"), ("\\k22", "聞|き"),
            ("\\k18", "こ"), ("\\k20", "え"), ("\\k60", "る")]


def f_main_right():
    return (pair_head("0:00:14.10 – 0:00:16.80", '<span class="pdur">2.70 s</span>') + strip(F_GROUPS)
            + reading_row("聞", "ki", "き", 22, "き") + cmds() + result(F_RESULT, "270 cs") + keyhint())


def towa_right(focus=False):
    """Line 11: 永遠に over to|wa ni: sung とわ, the dictionary's えいえん."""
    gs = [kgroup((("to", 30), ("wa", 35)), "永遠", "とわ", "cur", differs=True),
          kgroup((("ni", 40),), "に", None, "sug")]
    return (pair_head("0:00:41.20 – 0:00:42.25", '<span class="pdur">1.05 s</span>') + strip(gs)
            + reading_row("永遠", "to wa", "とわ", 65, "とわ", focus=focus, hint="Dictionary: えいえん")
            + result([("\\k65", "永遠|とわ"), ("\\k40", "に")], "105 cs"))


def love_right():
    """Line 12: 愛を over love wo: the romaji is no kana, so the dictionary reads 愛."""
    gs = [kgroup((("love", 45),), "愛", "あい", "cur"), kgroup((("wo", 30),), "を", None, "sug")]
    return (pair_head("0:00:43.00 – 0:00:43.75", '<span class="pdur">0.75 s</span>') + strip(gs)
            + reading_row("愛", "love", "—", 45, "あい", hint="Not sung in kana")
            + result([("\\k45", "愛|あい"), ("\\k30", "を")], "75 cs"))


def typed_right():
    """The user typed ラヴ over 愛; Use the suggestion goes back to あい."""
    gs = [kgroup((("love", 45),), "愛", "ラヴ", "cur", typed=True), kgroup((("wo", 30),), "を", None, "sug")]
    return (pair_head("0:00:43.00 – 0:00:43.75", '<span class="pdur">0.75 s</span>') + strip(gs)
            + reading_row("愛", "love", "—", 45, "ラヴ", typed=True, focus=True)
            + result([("\\k45", "愛|ラヴ"), ("\\k30", "を")], "75 cs"))


def none_right():
    """An empty reading: no furigana over 君 on this pair."""
    gs = [kgroup(G_KIMI, "君", "", "cur", typed=True), kgroup((("no", 30),), "の", None, "sug")]
    return (pair_head("0:00:14.10 – 0:00:16.80", '<span class="pdur">2.70 s</span>') + strip(gs)
            + reading_row("君", "ki mi", "きみ", 40, "", typed=True, focus=True)
            + result([("\\k40", "君"), ("\\k30", "の")], "270 cs"))


def multi_right():
    """KA8's Multi-highlight with furigana: one reading per syllable, the further ones as #|."""
    gs = [kgroup(G_KIMI, "君", "きみ"), kgroup((("no", 30),), "の"),
          kgroup((("ko", 20), ("e", 25)), "声", "こえ", "cur")]
    res = result([("\\k18", "君|き"), ("\\k22", "#|み"), ("\\k30", "の"), ("\\k20", "声|こ"), ("\\k25", "#|え")],
                 "115 cs")
    return (pair_head("0:00:14.10 – 0:00:16.80", '<span class="pdur">2.70 s</span>') + strip(gs)
            + reading_row("声", "ko e", "こえ", 45, "こえ") + res)


def mixed_right(choice):
    """A group with kanji and kana (聞こ over ki ko): the three ways it could read."""
    if choice == "a":
        g, res = kgroup((("ki", 22), ("ko", 18)), "聞こ", "きこ", "cur"), [("\\k40", "聞こ|きこ")]
    elif choice == "b":
        g, res = kgroup((("ki", 22), ("ko", 18)), "聞こ", "き", "cur"), [("\\k22", "聞|き"), ("\\k18", "こ")]
    else:
        g, res = kgroup((("ki", 22), ("ko", 18)), "聞こ", "", "cur"), [("\\k40", "聞こ")]
    gs = [kgroup((("ga", 35),), "が"), g, kgroup((("e", 20),), "え", None, "sug")]
    return strip(gs) + result([("\\k35", "が")] + res + [("\\k20", "え")], "95 cs")


def nodict_right():
    gs = [kgroup((("to", 30), ("wa", 35)), "永遠", "とわ", "cur"), kgroup((("ni", 40),), "に", None, "sug"),
          kgroup((("love", 45),), "愛", "", "sug")]
    return (strip(gs) + reading_row("永遠", "to wa", "とわ", 65, "とわ")
            + result([("\\k65", "永遠|とわ"), ("\\k40", "に"), ("\\k45", "愛")], "150 cs"))


def readback_right():
    """A kanji Line that already has furigana: 君|きみ reads back as a typed reading."""
    gs = [kgroup(G_KIMI, "君", "きみ", "cur", typed=True), kgroup((("no", 30),), "の", None, "sug")]
    return (pair_head("0:00:14.10 – 0:00:16.80", '<span class="pdur">2.70 s</span>') + strip(gs)
            + reading_row("君", "ki mi", "きみ", 40, "きみ", typed=True))


def f_window_page(key):
    th = THEMES[key]
    return (f'<h1>KA4 Kanji timer furigana: {th["name"]}</h1>'
            '<p class="note">Furigana (beside Suggest groupings and Multi-highlight, remembered) draws each kanji '
            'group’s reading over its kanji in the strip and writes it into the Result in the stock furigana '
            'syntax, 君|きみ. The reading is the sung one, from the group’s romaji; MeCab with UniDic checks it and '
            'fills in where the romaji is not kana. Suggested readings are muted, typed ones are in the text colour. '
            'The current group’s reading is edited in the Reading field; the arrow button goes back to the '
            'suggestion. With Furigana off the window is KA3’s.</p>'
            f'<div class="row"><div class="col">{kt_window(f_main_right(), top=ftopline())}</div></div>')


def f_states_page(key):
    th = THEMES[key]

    def item(html, cap, w=620):
        st = f' style="width:{w}px"' if w else ""
        return f'<div class="col"{st}>{html}<p class="cap">{cap}</p></div>'

    def pane(html, w=620):
        return f'<div class="pane" style="width:{w - 34}px">{html}</div>'

    return (f'<h1>KA4 Kanji timer furigana: states, {th["name"]}</h1>'
            '<h2>Where the reading comes from</h2><div class="row">'
            + item(pane(towa_right()),
                   "永遠 is sung “to wa”, so the reading is とわ. The dictionary reads えいえん; the dot on the group "
                   "and the hint say so, and nothing changes unless the user types.")
            + item(pane(love_right()),
                   "愛 is sung “love”, which is no kana. The reading comes from the dictionary (あい), muted as a "
                   "suggestion, with the reason beside the field.")
            + '</div><h2>Editing</h2><div class="row">'
            + item(pane(typed_right()),
                   "A typed reading (ラヴ) is drawn in the text colour and is never replaced: regrouping, "
                   "Suggest and Accept all suggestions keep it. The arrow button goes back to the suggestion.")
            + item(pane(none_right()),
                   "An empty reading writes no furigana for that group (君 alone in the Result).")
            + '</div><div class="row">'
            + item(pane(readback_right()),
                   "A kanji Line that already has furigana (君|きみ) opens with it as typed readings, so it is "
                   "kept, not read as two more characters.")
            + item(pane(multi_right()),
                   "With KA8’s Multi-highlight on, each syllable of a group takes its share of the reading: the "
                   "first as 君|き, the further ones as #|み.")
            + '</div><h2>A group of kanji and kana (question 6)</h2><div class="row">'
            + item(pane(mixed_right("a"), 400), "(a) The whole group’s reading: 聞こ|きこ.", 400)
            + item(pane(mixed_right("b"), 400),
                   "(b) Only the kanji’s reading: the group is written as 聞|き and こ, its time split by the "
                   "romaji.", 400)
            + item(pane(mixed_right("c"), 400), "(c) No reading; the user groups 聞 alone for one.", 400)
            + '</div><h2>No dictionary</h2><div class="row">'
            + item(kt_window(nodict_right(), top=ftopline(note="No reading dictionary: readings come from the "
                                                           "romaji only."), width=1040),
                   "If the dictionary did not load, readings come from the romaji alone and a group sung in no "
                   "kana (愛 “love”) has none until the user types it. The note is one muted line, no message box.", 1040)
            + '</div>')


KA4_CSS = """
.cb+.cb{margin-left:12px}
.kan.fk{flex-direction:column;height:58px;gap:0;padding:0 6px;position:relative}
.rb{font-size:11px;line-height:14px;color:%(muted)s;font-family:'Noto Sans CJK JP',sans-serif;min-height:14px}
.rb.typed{color:%(text)s}
.rb.none{min-width:20px;border-bottom:1px dashed %(line)s;height:8px;min-height:0;margin-bottom:6px}
.kb{line-height:1.2}
.dif{position:absolute;top:2px;right:2px;color:%(warning)s;display:flex}
.g.cur .rb{color:%(text)s}
.g.cur .rb.typed{font-weight:700}
.rl{margin-left:8px}
.kdet .fld{height:26px}
.ibtn{padding:0 5px}
.fhint{display:flex;align-items:center;gap:5px;color:%(muted)s;font-size:12px}
.fnote{display:flex;align-items:center;gap:6px;color:%(muted)s;margin:-4px 0 10px;justify-content:flex-end}
"""


def ka4_main():
    for key in THEMES:
        for name, fn in (("window", f_window_page), ("states", f_states_page)):
            html = page(key, fn(key))
            th = THEMES[key]
            extra = (KA3_CSS % dict(th, shadow=".55" if key in DARKISH else ".28", success=SUCCESS[key],
                                    rowline=th["line"] + "7A") + KA4_CSS % th)
            html = html.replace("</style>", extra + "</style>", 1)
            (OUT / f"ka4-{key}-{name}.html").write_text(html)


ka4_main()
