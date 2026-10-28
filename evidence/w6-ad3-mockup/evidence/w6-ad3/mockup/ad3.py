
# ---------------------------------------------------------------------------------------------
# AD3 #312: the run's notice and progress card (a Notice, in the toast corner) and each run's log in the
# Automation panel (AM1's panel, a Runs view beside its Scripts). Appended to base.py by shoot.sh.

GLYPHS.update({
    # The busy mark: a three-quarter ring, its open end in the accent (turns while the run lasts).
    "busy": '<path d="M8 1.5a6.5 6.5 0 1 0 6.5 6.5" opacity=".35"/><g class="acc"><path d="M8 1.5a6.5 6.5 0 0 1 6.5 6.5"/></g>',
    "applied": '<circle cx="8" cy="8" r="6.5"/><g class="acc"><path d="M5 8.25l2 2 4-4.5"/></g>',
    "cancelled": '<circle cx="8" cy="8" r="6.5"/><g class="acc"><path d="M4.4 11.6l7.2-7.2"/></g>',
    "chevron-up": '<path d="M4.5 9.5l3.5-3.5 3.5 3.5"/>',
})
for n in ("automation", "run-script", "rerun-macro", "refresh", "history", "unload", "automation-manager",
          "open-subtitles", "folder-open"):
    if n not in REPO_ICONS:
        REPO_ICONS.append(n)

STATE = {
    "running": ("busy", ""),
    "applied": ("applied", ""),
    "problems": ("notice-warning", "warn"),
    "cancelled": ("cancelled", "warn"),
    "failed": ("notice-error", "err"),
}

LEVELS = ["fatal", "error", "warning", "hint", "debug", "trace"]
LEVEL_CLS = {"fatal": "err", "error": "err", "warning": "warn"}


def bar(pct=None, slim=False):
    """The kit's ProgressBar, slim. pct None: busy (the script set no progress yet)."""
    cls = "pbar" + (" slim" if slim else "")
    fill = (f'<span class="pfill" style="width:{pct}%"></span>' if pct is not None
            else '<span class="pfill busy" style="left:22%;width:28%"></span>')
    return f'<span class="{cls}">{fill}</span>'


def logline(text, level=None, t=None, host=None, cls=""):
    """One line of a run's log: [time] [level] text. host: a line HikariSub adds (outcome, Lua error)."""
    ts = f'<span class="lts">{t}</span>' if t else ""
    if host:
        glyph, gcls = host
        return (f'<div class="ll host {cls}">{ts}<span class="lvl">{ic(glyph, 14, gcls)}</span>'
                f'<span class="ltx">{text}</span></div>')
    lv = (f'<span class="lvl {LEVEL_CLS.get(level, "")}">{level}</span>' if level
          else '<span class="lvl"></span>')
    return f'<div class="ll {cls}">{ts}{lv}<span class="ltx">{text}</span></div>'


TEMPLATE_LOG = [
    ("Found 1 380 dialogue lines, 24 templates", None, "0:00.081"),
    ("Template line 3: no syllables, skipped", "hint", "0:00.112"),
    ("Line 212: \\k tags sum to 2 140 ms, the line lasts 2 100 ms", "warning", "0:00.904"),
    ("Line 213: \\k tags sum to 1 990 ms, the line lasts 2 020 ms", "warning", "0:00.931"),
    ("Processing line 400", None, "0:01.744"),
    ("Template Line: 18 — unknown variable $sylwidth2", "warning", "0:01.802"),
    ("Processing line 412", None, "0:01.836"),
]


def card_log(rows, open_rows=6, timestamps=False, more=0):
    body = "".join(logline(t, lv, ts if timestamps else None) for t, lv, ts in rows[-open_rows:])
    top = f'<div class="ll muted more">{more} earlier lines · all in the Automation panel</div>' if more else ""
    return f'<div class="clog">{top}{body}<span class="sbar" style="top:4px;height:36px"></span></div>'


def card(state, title, task="", pct=None, note="", log=None, log_count=0, log_open=False, foot="", closable=False,
         width=380, focus=None):
    glyph, cls = STATE[state]
    head = (f'<div class="khead">{ic(glyph, 16, cls)}<span class="ctitle">{title}</span>'
            + (f'<span class="cpct">{pct} %</span>' if state == "running" and pct is not None else "")
            + (f'<span class="tx" title="Close">{ic("tab-close", 14)}</span>' if closable else "") + '</div>')
    parts = [head]
    if task:
        parts.append(f'<div class="ctask">{task}</div>')
    if state == "running":
        parts.append(f'<div class="cbar">{bar(pct)}</div>')
    if note:
        parts.append(f'<div class="cnote">{note}</div>')
    if log is not None and log_open:
        parts.append(log)
    disc = ""
    if log_count:
        disc = (f'<span class="cdisc{" ring" if focus == "log" else ""}">'
                f'{ic("chevron-down" if log_open else "chevron-right", 14)}Log · {log_count} lines</span>')
    parts.append(f'<div class="cfoot">{disc}<span class="cfill"></span>{foot}</div>')
    return f'<div class="card" style="width:{width}px">{"".join(parts)}</div>'


def pill(title, pct=None):
    return (f'<div class="pill">{ic("busy", 16)}<span class="ptitle">{title}</span>'
            f'<span class="pbarwrap">{bar(pct, slim=True)}</span></div>')


OPEN_PANEL = btn("Open in panel", small=True, flat=True)


def card_states():
    log = card_log(TEMPLATE_LOG)
    return [
        (pill("Strip tags", 60),
         "<b>A short run.</b> As a macro starts, a one-line notice at the toast corner: the macro’s name and a slim bar "
         "(busy until the script sets a progress). A run that ends within about 1 second with nothing in its log "
         "shows only this, which goes when it ends. No buttons; it never takes the keyboard."),
        (card("running", "Apply karaoke template", "Processing line 412 of 1 380", 30,
              "Episode 03.ass is read-only until the macro ends.", log, 7, False,
              btn("Cancel", small=True)),
         "<b>After 1 second</b>, the notice grows into the card in place: name, the script’s task and progress, the "
         "read-only Document, Cancel. The log is folded, its line count on the disclosure."),
        (card("running", "Apply karaoke template", "Processing line 412 of 1 380", 30,
              "Episode 03.ass is read-only until the macro ends.", log, 7, True,
              btn("Cancel", small=True), focus="log"),
         "<b>A run that writes to its log</b> opens the card at once, the log unfolded (the last lines, following "
         "the end unless scrolled up; the level is shown when the script gave one). Space or a click folds it."),
        (card("running", "Apply karaoke template", "Cancelling…", 30,
              "The script has not stopped yet. Force stop ends its helper; nothing is changed.", None, 7, False,
              btn("Force stop", small=True, danger=True) + btn("Cancelling…", small=True)),
         "<b>Cancel</b> becomes “Cancelling…” (disabled); after 3 seconds without the script stopping, Force stop "
         "appears beside it in the danger colour (automation.md’s grace, unchanged)."),
    ]


def end_states():
    log = card_log(TEMPLATE_LOG, 4)
    return [
        (card("applied", "Apply karaoke template", "Applied. The script wrote 7 lines to its log.", log=log,
              log_count=7, log_open=True, foot=OPEN_PANEL, closable=True),
         "<b>Applied, with a log:</b> the card stays until × (legacy kept its window open for the output). "
         "“Open in panel” shows this run in the Automation panel. Applied with nothing logged: the card just goes."),
        (card("problems", "Apply karaoke template named problems", "Applied. 2 template lines have problems.",
              log=log, log_count=7, log_open=True, foot=OPEN_PANEL, closable=True),
         "<b>Applied, problems named</b> (KE1-template-problems): the same, in the warning colour, its log open."),
        (card("cancelled", "Apply karaoke template cancelled", "Nothing was changed.", log_count=9,
              foot=OPEN_PANEL, closable=True),
         "<b>Cancelled:</b> stays until ×, the log folded but kept (Aegisub’s dropped it)."),
        (card("failed", "Fix overlaps failed",
              "timing-tools.lua:212: attempt to index a nil value (field ‘end_time’). Nothing was changed.",
              log_count=4, foot=OPEN_PANEL, closable=True),
         "<b>Failed</b> (an error, refused, not valid, helper lost, force-stopped): the first line of the error, in "
         "the danger colour; the traceback is in the run’s log, and the Log panel gets its entry as today (NT1)."),
    ]


def card_page(key):
    th = THEMES[key]
    hero = card("running", "Apply karaoke template", "Processing line 412 of 1 380", 30,
                "Episode 03.ass is read-only until the macro ends.", card_log(TEMPLATE_LOG), 7, True,
                btn("Cancel", small=True))
    toast_above = toast("info", "Save", "Saved Episode 03.ass.")

    def gallery(items, width=380):
        return '<div class="gal">' + "".join(
            f'<div class="gcell" style="width:{width}px">{w}<p class="cap">{c}</p></div>' for w, c in items) + '</div>'

    return "".join([
        f'<h1>AD3: a macro’s progress card ({th["name"]})</h1>',
        '<p class="note">The progress no longer opens the Automation panel (today every run opens it, AM1’s rule): a '
        'run shows a <b>notice</b> in the toast corner, bottom right above the status bar, which grows into a '
        '<b>progress card</b> when the run lasts longer than about 1 second or writes to its log. It is PK1’s Notice '
        'kind: it never takes the keyboard (F6 reaches it, below), it is drawn on the toast’s surface (radius 4, a '
        '1-pixel line, a shadow), one at a time (one macro runs at a time), under the toasts, which stack above it. '
        'The rest of the window stays usable; only the run’s Document is read-only, as today. Behind it is a real '
        'screenshot of today’s build in this theme (qt 7788b8a7c); names, files and lines are examples.</p>',
        f'<div class="app"><div class="stack">{toast_above}{hero}</div></div>',
        '<h2>While it runs</h2>',
        gallery(card_states()),
        '<h2>When it ends</h2>',
        gallery(end_states()),
        '<h2>Rules</h2>',
        '<table class="notes">'
        '<tr><td>The 1-second rule</td><td>Measured from the start of the macro itself (after its validation). Time '
        'spent in the script’s own dialog or file picker does not count: a macro that asks first and then works '
        'gets its second after the dialog closes. Anything written to the log opens the card at once.</td></tr>'
        '<tr><td>Keyboard</td><td>The card never takes focus. F6 / Shift+F6 reach it as a region while it shows '
        '(Tab then moves between its log disclosure and buttons; Esc gives the focus back without cancelling). '
        'The Automation menu gets “Cancel macro” while one runs (no hotkey id needed).</td></tr>'
        '<tr><td>Fullscreen</td><td>The card shows in the fullscreen video window, as the toasts do (PK1).</td></tr>'
        '<tr><td>What stays</td><td>Applied with nothing logged: the card goes. Applied with a log, problems named, '
        'cancelled, failed: the card stays until ×; a new run replaces it. Every run, whatever its end, is kept in '
        'the Automation panel’s Runs (next page). A run whose script-resolution question was declined ends quietly '
        '(as today).</td></tr>'
        '<tr><td>Screen readers</td><td>The card is a group named by its title; the task and the outcome are '
        'announced as they change (politely, at most once a second), never the log lines.</td></tr>'
        '</table>',
    ])


# ---------------------------------------------------------------------------------------------
# The Automation panel


def dock(title, body, width=440, height=760):
    return (f'<div class="adock" style="width:{width}px;height:{height}px"><div class="adhdr"><span>{title}</span>'
            f'<span class="dmore">{ic("panel-menu")}</span></div><div class="adbody">{body}</div></div>')


def seg_tabs(chosen, runs=5):
    def tab(name, label):
        return f'<span class="{"on" if name == chosen else ""}">{label}</span>'
    return (f'<span class="seg sm">{tab("scripts", "Scripts")}'
            f'{tab("runs", f"Runs <span class=cnt>{runs}</span>")}</span>')


def topstrip(chosen):
    icons = (f'<span class="qb" title="Load script…">{ic("automation")}</span>'
             f'<span class="qb dis" title="Refresh autoload scripts">{ic("refresh")}</span>'
             f'<span class="qb dis" title="Rerun last macro">{ic("rerun-macro")}</span>')
    return f'<div class="astrip">{seg_tabs(chosen)}<span class="cfill"></span>{icons}</div>'


def scripts_view():
    def sec(name, n, open_=True):
        return (f'<div class="asec">{ic("chevron-down" if open_ else "chevron-right", 14)}<b>{name}</b>'
                f'<span class="muted">{n}</span></div>')

    def script(name, extra=""):
        return f'<div class="ascript"><b>{name}</b><span class="cfill"></span>{extra}</div>'

    def macro(name, keys="", cls="", extra=""):
        return (f'<div class="amacro {cls}"><span>{name}</span><span class="cfill"></span>{extra}'
                f'<span class="muted">{keys}</span></div>')

    icons2 = (f'<span class="qb dis">{ic("refresh")}</span><span class="qb">{ic("panel-menu")}</span>')
    return "".join([
        topstrip("scripts"),
        f'<div class="afilter">{field("Filter macros", placeholder=True, icon="filter")}</div>',
        sec("Subtitle scripts", 1),
        script("karaoke-templater.lua", '<span class="muted">Running</span>' + icons2),
        macro("Apply karaoke template", "", "run", ic("busy", 14)),
        macro("Remove karaoke template lines", "", "dim"),
        sec("Autoload", 3),
        script("timing-tools.lua", icons2.replace(" dis", "")),
        macro("Fix overlaps", "Ctrl+Alt+O", "dim"),
        macro("Snap to keyframes", "", "dim"),
        script("strip-tags.lua", icons2.replace(" dis", "")),
        macro("Strip tags", "Ctrl+Alt+T", "dim"),
        macro("Strip comments", "", "dim"),
        script("select-overlaps.lua", icons2.replace(" dis", "")),
        macro("Select overlapping lines", "", "dim"),
        sec("Shortcuts for missing macros", 1, False),
    ])


RUNS = [
    ("running", "Apply karaoke template", "16:42", "Processing line 412 of 1 380 · 30 %", 7),
    ("failed", "Fix overlaps", "16:38", "Failed after 0.4 s · Episode 03.ass", 4),
    ("cancelled", "Apply karaoke template", "16:31", "Cancelled after 12 s · Episode 03.ass", 9),
    ("applied", "Strip tags", "16:25", "Applied in 0.1 s · Episode 03.ass", 0),
    ("problems", "Apply karaoke template", "16:20", "Applied, problems named, 3.2 s · Episode 02.ass", 11),
]


def run_row(i, sel):
    state, name, t, sub, n = RUNS[i]
    glyph, cls = STATE[state]
    lines = f' · {n} lines' if n else ' · nothing logged'
    b = f'<span class="rbar">{bar(30, slim=True)}</span>' if state == "running" else ""
    return (f'<div class="rrow{" sel" if sel else ""}">{ic(glyph, 16, cls)}<div class="rtext">'
            f'<div class="rtop"><span class="rname">{name}</span><span class="muted">{t}</span></div>'
            f'<div class="rsub">{sub}{lines if state != "running" else ""}</div>{b}</div></div>')


FAILED_LOG = [
    ("Fix overlaps: 1 380 lines, 46 selected", None, "0:00.004", None),
    ("Line 18 overlaps line 19 by 40 ms", "debug", "0:00.010", None),
    ("Line 18: moved the end to 0:00:41.36", "trace", "0:00.011", None),
    ("Line 31 overlaps line 32 by 120 ms", "debug", "0:00.016", None),
    ("Line 31: the next line starts before this one; left as it is", "warning", "0:00.017", None),
    ("Line 44: no end time", "error", "0:00.402", None),
    ("timing-tools.lua:212: attempt to index a nil value (field ‘end_time’)", None, "0:00.403", ("notice-error", "err")),
    ("  stack traceback: timing-tools.lua:212 in function ‘shift_end’", None, None, "tb"),
    ("  timing-tools.lua:340 in function &lt;timing-tools.lua:322&gt;", None, None, "tb"),
    ("Nothing was changed.", None, "0:00.404", ("about", "")),
]


def run_log(rows, timestamps=False, level="Trace", flt="", count=None, combo_open=False):
    out = []
    for text, lv, ts, host in rows:
        if host == "tb":
            out.append(logline(text, None, "" if timestamps else None, cls="tb"))
        elif host:
            out.append(logline(text, None, ts if timestamps else None, host=host))
        else:
            out.append(logline(text, lv, ts if timestamps else None))
    flt_field = (field(flt, focus=True, icon="filter", trail=f'<span class="muted">{count} of 10</span>')
                 if flt else field("Filter", placeholder=True, icon="filter"))
    strip = (f'<div class="lstrip"><span class="lvcombo{" open" if combo_open else ""}">'
             f'{combo("Up to " + level.lower() if level != "Trace" else "All levels", 128)}</span>'
             f'<span class="lflt">{flt_field}</span>'
             f'<span class="qb{" on" if timestamps else ""}" title="Show times">{ic("history")}</span>'
             f'<span class="qb" title="Copy the lines shown">{ic("edit-copy")}</span>'
             f'<span class="qb" title="Save the lines shown…">{ic("save")}</span></div>')
    return f'<div class="rlog">{strip}<div class="rlines{" ts" if timestamps else ""}">{"".join(out)}</div></div>'


def level_menu(chosen="Debug"):
    counts = {"fatal": 0, "error": 1, "warning": 1, "hint": 0, "debug": 2, "trace": 1}
    items = []
    for i, lv in enumerate(LEVELS):
        name = f"Up to {lv}" if lv != "trace" else "All levels"
        items.append(f'<div class="mi{" hl" if name.lower() == ("up to " + chosen.lower()) else ""}">'
                     f'<span class="mic">{ic("check") if name.lower() == ("up to " + chosen.lower()) else ""}</span>'
                     f'<span class="mt">{name}</span><span class="mk">{i} · {counts[lv]}</span></div>')
    items.append('<div class="msep"></div><div class="mi dis"><span class="mic"></span><span class="mt">'
                 'Lines without a level always show</span></div>')
    return f'<div class="menu lvmenu" style="width:236px">{"".join(items)}</div>'


def runs_view(sel=1, timestamps=False, level="Trace", flt="", count=None, rows=None, combo_open=False):
    rows = rows if rows is not None else FAILED_LOG
    return "".join([
        topstrip("runs"),
        '<div class="rlist">' + "".join(run_row(i, i == sel) for i in range(len(RUNS))) + '</div>',
        run_log(rows, timestamps, level, flt, count, combo_open),
    ])


def menu(items, width=230):
    out = []
    for it in items:
        if it == "-":
            out.append('<div class="msep"></div>')
            continue
        text, icon, extra = it
        out.append(f'<div class="mi{" " + extra if extra else ""}"><span class="mic">{ic(icon) if icon else ""}'
                   f'</span><span class="mt">{text}</span></div>')
    return f'<div class="menu" style="width:{width}px">{"".join(out)}</div>'


def panel_menu():
    return menu([("Load script…", "automation", ""), ("Refresh autoload scripts", "refresh", ""),
                 ("Shortcuts in Settings…", "hotkeys", ""), "-", ("Clear runs", "remove", "")], 236)


def panel_page(key):
    th = THEMES[key]
    filtered = [r for r in FAILED_LOG if r[1] in ("error", "warning", None)]
    lvl = [r for r in FAILED_LOG if r[1] != "trace"]
    return "".join([
        f'<h1>AD3: each run’s log in the Automation panel ({th["name"]})</h1>',
        '<p class="note">The panel gets two views, <b>Scripts</b> (AM1’s list, as today) and <b>Runs</b>, switched '
        'at the top beside the panel’s three commands. AM1’s run box at the top of the list goes: the card shows '
        'the run, and the running macro’s row turns its mark. Runs keeps the session’s last 20 runs, newest first, '
        'whatever their end; the card’s “Open in panel” and a failed run’s toast Details open it at that run. '
        'Below the list, the chosen run’s log: a level filter (the six levels of '
        '<code>aegisub.debug.out</code>, fatal to trace), a filter field, times on or off, Copy and Save. Panels are '
        'drawn at AM1’s evidence size (460 × 760).</p>',
        '<div class="row">',
        f'<div class="col">{dock("Automation", scripts_view())}<p class="cap" style="max-width:440px">'
        '<b>Scripts while a macro runs:</b> no run box; the script says “Running” as today and the macro’s row '
        'shows the busy mark. The rest is AM1’s, unchanged.</p></div>',
        f'<div class="col">{dock("Automation", runs_view())}<p class="cap" style="max-width:440px">'
        '<b>Runs</b>, the failed run chosen (from its card’s “Open in panel”). Each line: its level when the '
        'script gave one (error and fatal in the danger colour, warning in the warning colour, the word always '
        'shown), then the text, in the monospace font. HikariSub’s own lines (the Lua error with its traceback, '
        'the outcome) carry an icon instead of a level and always show.</p></div>',
        '</div>',
        '<div class="row" style="margin-top:28px">',
        f'<div class="col"><div class="anchorbox">{dock("Automation", runs_view(level="Debug", rows=lvl, combo_open=True))}'
        f'<div style="position:absolute;left:14px;top:404px">{level_menu()}</div></div>'
        '<p class="cap" style="max-width:440px"><b>The level filter</b> is a threshold, as the trace level is: '
        '“Up to debug” hides trace. Each entry shows the level’s number and how many lines it has. Lines the '
        'script wrote without a level show at every setting, as they always show in Aegisub. Levels above '
        'Settings › Automation › Trace level (3 by default) are never recorded: the helper still drops them '
        'first, so a script’s output is unchanged.</p></div>',
        f'<div class="col">{dock("Automation", runs_view(timestamps=True, flt="line 31", count=2, rows=[r for r in FAILED_LOG if "line 31" in r[0].lower()]))}'
        '<p class="cap" style="max-width:440px"><b>Times on, filtered.</b> The clock shows each line’s time since '
        'the run started (m:ss.mmm; the row above has the clock time it started), remembered for every run. The '
        'filter keeps the lines that contain the text (NT1’s Log filter), with the count; Esc clears it. Copy and '
        'Save take the lines shown, times included when shown; Save writes UTF-8 text headed by the macro, its '
        'script, the Document, the start and the outcome.</p></div>',
        '</div>',
        '<h2>The panel’s ⋯ menu</h2>',
        f'<div class="row"><div class="col">{panel_menu()}<p class="cap" style="max-width:236px">“Import legacy '
        'hotkeys…” leaves it for Settings › Hotkeys (PU7 #309). “Clear runs” empties Runs (never the running one).'
        '</p></div></div>',
    ])


AD3_CSS = """
.gal{display:flex;flex-wrap:wrap;gap:24px 28px;align-items:flex-start}
.gcell{display:flex;flex-direction:column}
.gcell .cap{max-width:100%%}
.muted{color:%(muted)s}
code{font:12px 'Liberation Mono',Consolas,monospace}
.cfill{flex:1}
.dis{color:%(disabled)s}
.stack{width:380px;bottom:38px}

/* the notice and the card: the toast's surface */
.card,.pill{background:%(panel)s;outline:1px solid %(line)s;border-radius:4px;box-shadow:0 6px 20px rgba(0,0,0,%(shadow)s);
            color:%(text)s}
.card{display:flex;flex-direction:column;padding:10px 8px 8px 12px}
.khead{display:flex;align-items:center;gap:10px;min-height:22px}
.ctitle{flex:1;font-weight:650;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.cpct{color:%(muted)s;font-variant-numeric:tabular-nums;padding-right:4px}
.ctask{color:%(text)s;margin:4px 0 0 26px;line-height:1.4}
.cbar{margin:8px 4px 0 26px}
.cnote{color:%(muted)s;margin:6px 4px 0 26px;font-size:12px;line-height:1.4}
.cfoot{display:flex;align-items:center;gap:6px;margin:8px 0 0 20px}
.cdisc{display:inline-flex;align-items:center;gap:4px;height:24px;padding:0 6px;color:%(text)s}
.clog{position:relative;margin:8px 4px 0 26px;background:%(field)s;outline:1px solid %(line)s;padding:4px 10px 4px 6px;
      max-height:132px;overflow:hidden}
.pill{display:flex;align-items:center;gap:10px;height:36px;padding:0 12px;width:236px;margin-left:auto}
.ptitle{font-weight:650;white-space:nowrap}
.pbarwrap{flex:1;min-width:40px}
.pbar{position:relative;display:block;height:6px;background:%(raised)s;outline:1px solid %(line)s;border-radius:1px;overflow:hidden}
.pbar.slim{height:4px}
.pfill{position:absolute;left:0;top:0;bottom:0;background:%(accent)s}
.pfill.busy{opacity:.85}
.stack .toast{width:380px}

/* log lines */
.ll{display:flex;gap:8px;align-items:baseline;font:12px/18px 'Liberation Mono',Consolas,monospace;color:%(text)s;
    white-space:nowrap}
.ll .ltx{overflow:hidden;text-overflow:ellipsis;min-width:0}
.ll.more{font-style:italic}
.ll.host .ltx{font-family:'Liberation Sans',Arial,sans-serif;font-size:12px}
.ll.host .lvl{align-self:center}
.ll.tb{color:%(muted)s}
.lvl{width:52px;flex:none;color:%(muted)s;font-size:11px;display:flex}
.lts{width:62px;flex:none;color:%(muted)s;font-variant-numeric:tabular-nums}
.ll.tb .lts{visibility:hidden}

/* the Automation dock (AM1's) */
.adock{background:%(panel)s;outline:1px solid %(line)s;display:flex;flex-direction:column;position:relative}
.adhdr{display:flex;align-items:center;justify-content:space-between;height:34px;padding:0 10px 0 12px;font-weight:650;
       box-shadow:inset 0 -1px %(line)s;flex:none}
.dmore{display:flex;color:%(text)s}
.adbody{flex:1;display:flex;flex-direction:column;min-height:0;overflow:hidden}
.astrip{display:flex;align-items:center;gap:2px;padding:10px 10px 0 12px}
.astrip .seg{margin-right:8px}
.seg .cnt{color:%(muted)s;margin-left:6px;font-weight:400;font-variant-numeric:tabular-nums}
.qb.dis{color:%(disabled)s}
.qb.on{background:%(select)s;box-shadow:inset 0 -2px %(accent)s}
.afilter{padding:8px 10px 6px 12px}
.afilter .fld{width:100%%}
.asec{display:flex;align-items:center;gap:6px;height:30px;padding:0 12px}
.ascript{display:flex;align-items:center;gap:8px;height:30px;padding:0 10px 0 32px}
.amacro{display:flex;align-items:center;gap:8px;height:28px;padding:0 12px 0 44px}
.amacro.dim{color:%(disabled)s}
.amacro.run{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}

/* Runs */
.rlist{margin:10px 10px 0 12px;outline:1px solid %(line)s;background:%(field)s;flex:none}
.rrow{display:flex;gap:10px;align-items:flex-start;padding:6px 10px}
.rrow+.rrow{box-shadow:inset 0 1px %(line)s}
.rrow.sel{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.rrow .ic{margin-top:1px}
.rtext{flex:1;min-width:0}
.rtop{display:flex;justify-content:space-between;gap:8px}
.rname{font-weight:650}
.rsub{color:%(muted)s;font-size:12px;margin-top:2px}
.rbar{display:block;margin-top:6px}
.rlog{flex:1;display:flex;flex-direction:column;min-height:0;margin:10px 10px 10px 12px}
.lstrip{display:flex;align-items:center;gap:4px;margin-bottom:6px}
.lflt{flex:1;min-width:0;display:flex}
.lflt .fld{width:100%%}
.rlines{flex:1;background:%(field)s;outline:1px solid %(line)s;padding:4px 8px;overflow:hidden}
.lvcombo{display:flex}
.lvcombo.open .fld{border-color:%(focus)s;box-shadow:inset 0 0 0 1px %(focus)s}

/* menus */
.menu{background:%(panel)s;outline:1px solid %(line)s;border-radius:5px;padding:4px 0;box-shadow:0 10px 30px rgba(0,0,0,%(shadow)s)}
.mi{display:flex;align-items:center;height:26px;padding:0 12px 0 6px;gap:6px}
.mi.hl{background:%(select)s}
.mi.dis{color:%(muted)s;font-size:12px}
.mic{width:20px;display:flex;justify-content:center}
.mt{flex:1}
.mk{color:%(muted)s;font-variant-numeric:tabular-nums}
.msep{height:1px;background:%(line)s;margin:4px 8px}
"""


def ad3_main():
    for key in THEMES:
        for name, fn in (("card", card_page), ("panel", panel_page)):
            html = page(key, fn(key))
            th = THEMES[key]
            extra = AD3_CSS % dict(th, shadow=".55" if key in DARKISH else ".28",
                                   focus=th["accent"] if key not in HC else th["text"])
            html = html.replace("</style>", extra + "</style>", 1)
            (OUT / f"ad3-{key}-{name}.html").write_text(html)


ad3_main()
