#!/usr/bin/env python3
"""AD2 #311 mockup: Repeat last run, Last used, presets and Reset for script dialogs. Per theme, three
sheets (the dialog's menu and markers, Repeat last run, the setting and the rules). Usage: gen.py <tree>
(a HikariSub checkout, for IC1's icons).

Built on AD1's approved mockup generator (ad1lib.py, evidence/w6-ad1-mockup gen.py): the theme roles,
DG1's frame, PK1's pieces and the example script dialogs. Menus follow the app's menus; the ⋯ button sits
where MuseScore puts a dialog's left-side buttons (Reset preferences), DG1's leadingActions."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ad1lib as A  # noqa: E402

A.TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/altq/.t3/worktrees/hikari/w6-ad2")
OUT = pathlib.Path(__file__).resolve().parent
THEMES, ic, btn, mn = A.THEMES, A.ic, A.btn, A.mn

A.REPO_ICONS += ["panel-menu", "chevron-right", "run-script", "rerun-macro", "refresh", "automation", "undo",
                 "notice-warning", "delete", "save-as", "settings", "filter"]
# Proposed icon (IC1 rules: 16 grid, 1 px stroke, the accent part in its own group): a script page with
# the repeat loop, beside rerun-macro's page with its single turn.
A.GLYPHS["repeat-run"] = ('<path d="M8.5 14.5h-5V1.5h6l3 3v3"/><path d="M9.5 1.5v3h3"/>'
                          '<g class="acc"><path d="M9.5 10.5v-.5a1.5 1.5 0 0 1 1.5-1.5h3.5"/>'
                          '<path d="M13 7l1.5 1.5L13 10"/><path d="M14.5 12.5v.5a1.5 1.5 0 0 1-1.5 1.5H9.5"/>'
                          '<path d="M11 16l-1.5-1.5L11 13"/></g>')
A.GLYPHS["info"] = ('<circle cx="8" cy="8" r="6.5"/><path d="M8 7.5v4"/>'
                    '<circle cx="8" cy="5" r=".6" fill="currentColor" stroke="none"/>')

CSS = A.CSS + """
/* AD2 */
.dfoot .lead{margin-right:auto;display:flex;gap:8px}
.mb{display:inline-flex;align-items:center;justify-content:center;width:30px;height:30px;border:1px solid %(line)s;
    background:%(raised)s;color:%(text)s;border-radius:1px;flex:none}
.mb.on{background:%(press)s;border-color:%(focus)s}
.mb.dot::after{content:"";position:absolute;width:6px;height:6px;border-radius:3px;background:%(accent)s;
    transform:translate(9px,-9px)}
.cell.chg::before{content:"";position:absolute;left:-5px;top:4px;bottom:4px;width:2px;background:%(accent)s;border-radius:1px}
.cell.chg.hovm::before{width:4px;left:-6px}
.menu{position:absolute;z-index:8;background:%(panel)s;outline:1px solid %(line)s;padding:4px 0;min-width:220px;
      box-shadow:0 6px 22px rgba(0,0,0,%(shadow)s);color:%(text)s}
.mi{display:flex;align-items:center;gap:8px;height:26px;padding:0 10px 0 8px;white-space:nowrap}
.mi .ic{color:%(muted)s}
.mi .mt{flex:1}
.mi .ms{color:%(muted)s;font-size:12px;margin-left:24px}
.mi.cur{background:%(select)s}
.mi.cur .ic{color:%(text)s}
.mi.dis{color:%(disabled)s}
.mi.dis .ic,.mi.dis .ms{color:%(disabled)s}
.mi .sp{width:16px;flex:none}
.msep{height:1px;background:%(line)s;margin:4px 0}
.mhead{padding:4px 10px 2px 32px;font-size:11px;color:%(muted)s}
.ibar{display:flex;align-items:flex-start;gap:8px;margin:12px 24px 0;padding:8px 10px;background:%(raised)s;
      box-shadow:inset 3px 0 %(accent)s;font-size:12px;line-height:17px}
.ibar .ic{color:%(accent)s;margin-top:1px}
.ibar .act{margin-left:auto;color:%(accent)s;white-space:nowrap;text-decoration:underline}
.dock{background:%(panel)s;outline:1px solid %(line)s;display:flex;flex-direction:column}
.dhead{display:flex;align-items:center;height:30px;padding:0 4px 0 10px;background:%(raised)s;
       box-shadow:inset 0 -1px %(line)s;font-weight:650}
.dhead .ic{margin-left:auto;color:%(muted)s}
.strip{display:flex;align-items:center;gap:4px;padding:8px}
.tb{display:inline-flex;align-items:center;justify-content:center;width:28px;height:28px;border-radius:1px;color:%(text)s}
.tb.hov{background:%(press)s}
.tb.dis{color:%(disabled)s}
.tree{padding:0 8px 8px}
.tr{display:flex;align-items:center;gap:6px;height:24px;padding:0 6px}
.tr.s{font-weight:650}
.tr.m{padding-left:26px}
.tr.cur{background:%(select)s}
.tr .k{margin-left:auto;color:%(muted)s;font-size:12px}
.status{display:flex;align-items:center;height:24px;padding:0 10px;background:%(raised)s;color:%(muted)s;font-size:12px;
        box-shadow:inset 0 1px %(line)s}
.flow{display:grid;grid-template-columns:200px 36px 1fr;gap:10px 0;align-items:center}
.fbox{background:%(raised)s;outline:1px solid %(line)s;padding:8px 10px;font-size:12px;line-height:1.4}
.fbox b{display:block;margin-bottom:2px}
.farr{text-align:center;color:%(muted)s;font-size:18px}
.pill{display:inline-block;padding:0 6px;border-radius:9px;background:%(select)s;font-size:11px;line-height:18px}
.setp{background:%(panel)s;outline:1px solid %(line)s;width:640px}
.setp .side{width:150px;background:%(raised)s;box-shadow:inset -1px 0 %(line)s;padding:8px 0}
.setp .side div{height:26px;display:flex;align-items:center;padding:0 12px}
.setp .side .cur{background:%(select)s;box-shadow:inset 2px 0 %(accent)s}
.setp .main{flex:1;padding:14px 18px}
.sect{font-weight:650;margin:0 0 8px}
.sdesc{color:%(muted)s;font-size:12px;line-height:1.45;margin:2px 0 12px 24px}
.yes{color:%(accent)s;font-weight:650}
.mbar{display:flex;gap:18px;padding:6px 10px;background:%(raised)s;box-shadow:inset 0 -1px %(line)s}
"""


def page(key, content):
    th = THEMES[key]
    css = CSS % dict(th, page=th["bg"], shadow=".55" if key in A.DARKISH else ".28",
                     focus=th["accent"] if key not in A.HC else th["text"],
                     tbline=f"box-shadow:inset 0 -1px {th['line']}" if key in A.HC else "")
    return (f'<!doctype html><html><head><meta charset="utf-8"><style>{css}</style></head><body>{A.sprite()}'
            f'{content}</body></html>')


# ---------------------------------------------------------------------------------------------
# The dialog with the ⋯ button and changed-field markers


def grid(controls, states=None, changed=(), hover_marker=None):
    """AD1's grid, with the marker on controls whose value differs from the script's."""
    html = A.grid(controls, states)
    for c in controls:
        name = c.get("name")
        if name in changed:
            # the cell's opening tag carries the control's class and its place; mark that one
            tag = (f'<div class="cell {c["class"]}" style="grid-column:{c["x"] + 1}/span {c.get("w", 1)};'
                   f'grid-row:{c["y"] + 1}/span {c.get("h", 1)};')
            cls = "chg hovm" if name == hover_marker else "chg"
            html = html.replace(tag, tag.replace(f'class="cell {c["class"]}"', f'class="cell {c["class"]} {cls}"'), 1)
    return html


def menu_button(on=False, dot=False):
    return f'<span class="mb{" on" if on else ""}{" dot" if dot else ""}">{ic("panel-menu")}</span>'


def dialog(title, controls, buttons, *, width=None, states=None, hint="", changed=(), menu_on=False, extra="",
           bar="", hover_marker=None, default=0, focus_button=None, dot=False):
    w = f"width:{width}px;" if width else ""
    body = grid(controls, states, changed, hover_marker)
    foot = "".join(btn(b, primary=(i == default), focus=(i == focus_button)) for i, b in enumerate(buttons))
    return (f'<div class="dlg" style="{w}">{A.titlebar(title)}{bar}'
            f'<div class="dscroll"><div class="dcontent">{body}</div></div>'
            f'{A.hint_strip(hint)}<div class="dfoot"><span class="lead">{menu_button(menu_on, dot)}</span>{foot}</div>'
            f'{extra}</div>')


def mi(text, icon=None, short="", sub=False, cur=False, dis=False, check=None):
    lead = ic(icon) if icon else ('<span class="sp"></span>' if check is None else
                                  (ic("check") if check else '<span class="sp"></span>'))
    tail = ic("chevron-right", 14) if sub else (f'<span class="ms">{short}</span>' if short else "")
    return f'<div class="mi{" cur" if cur else ""}{" dis" if dis else ""}">{lead}<span class="mt">{text}</span>{tail}</div>'


def menu(rows, style):
    return f'<div class="menu" style="{style}">{"".join(rows)}</div>'


SEP = '<div class="msep"></div>'


def dialog_menu(style, cur=None, last_used=True):
    return menu([
        mi("Last used", "undo", cur=(cur == "last"), dis=not last_used),
        mi("Presets", "save-as", sub=True, cur=(cur == "presets")),
        SEP,
        mi("Reset to the script’s values", "refresh", cur=(cur == "reset")),
    ], style)


def presets_menu(style, cur=None):
    return menu([
        '<div class="mhead">Gradient by character</div>',
        mi("Soft pink fade", check=False, cur=(cur == "soft")),
        mi("Blue to clear", check=False, cur=(cur == "blue")),
        mi("Karaoke glow", check=False),
        SEP,
        mi("Save as preset…", "save-as", cur=(cur == "save")),
        mi("Delete preset", "delete", sub=True),
    ], style)


def gradient_values(**over):
    """AD1's GradientEverything dialog with some values changed (the user's answers)."""
    out = []
    for c in A.gradient():
        c = dict(c)
        if c.get("name") in over:
            v = over[c["name"]]
            if c["class"] in ("edit", "textbox"):
                c["text"] = v
            else:
                c["value"] = v
        out.append(c)
    return out


BUTTONS = A.GRADIENT_BUTTONS
LAST = dict(tags="\\c\\3c\\blur", accel="1.6", bord=False)


def sheet_menu(key):
    th = THEMES[key]
    plain = dialog("Gradient by character", A.gradient(), BUTTONS, width=470, menu_on=True,
                   hint="The tags to gradient, in one block.", states={"tags": dict(focus=False)},
                   extra=dialog_menu("left:24px;bottom:62px", cur="presets")
                   + presets_menu("left:248px;bottom:30px", cur="soft"))
    filled = dialog("Gradient by character", gradient_values(**LAST), BUTTONS, width=470,
                    changed=("tags", "accel", "bord"), hover_marker="tags",
                    hint="How fast the gradient changes; 1 is even. 0.1 to 10, in steps of 0.05.",
                    states={"accel": dict(focus=True)},
                    extra=A.tooltip("Changed from the script’s \\c\\3c\\alpha\\blur. Click the bar to put it back.",
                                    "left:70px;top:108px") + A.pointer("left:58px;top:84px"))
    save = dialog("Gradient by character", gradient_values(**LAST), BUTTONS, width=470, menu_on=True,
                  changed=("tags", "accel", "bord"), hint="",
                  extra=A.popover(
                      '<div style="font-weight:650;margin-bottom:8px">Save as preset</div>'
                      '<span class="fld focus wide"><span class="ftext">Soft pink fade<span class="caret"></span>'
                      '</span></span>'
                      '<div class="sdesc" style="margin:6px 0 10px">A preset named “Soft pink fade” exists. '
                      'Saving replaces it.</div>'
                      f'<div style="display:flex;gap:8px;justify-content:flex-end">{btn("Replace", primary=True, small=True)}'
                      f'{btn("Cancel", small=True)}</div>', 270, "position:absolute;left:24px;bottom:62px"))
    return "".join([
        f'<h1>AD2 · Script dialogs: the ⋯ menu, presets and markers ({th["name"]})</h1>',
        '<p class="note">AD1’s dialog gets one button of HikariSub’s own: ⋯ at the leading end of the footer, '
        'where MuseScore puts a dialog’s left-side buttons (DG1’s leadingActions). The script’s grid, buttons, '
        'hint strip and keys are AD1’s, unchanged. Everything in the menu only changes the values shown before '
        'a button is pressed: what the script gets back keeps Aegisub’s shape, and nothing is filled in unless '
        'the user asks (or turns the setting on, sheet 3).</p>',
        '<div class="row">',
        f'<div class="col">{plain}<p class="cap">The ⋯ menu: '
        '<b>Last used</b> fills in the answers given the last time this dialog was answered with a button other '
        'than its cancel one (off until there are any). <b>Presets</b> loads a named set saved for this script’s '
        'dialog; <b>Save as preset…</b> keeps the values shown now. <b>Reset to the script’s values</b> puts back '
        'what the script sent this time. Keys: Tab reaches ⋯ after the grid, before the script’s buttons; '
        'Space or Enter opens it, as any menu button.</p></div>',
        f'<div class="col">{filled}<p class="cap">After Last used (or a preset): a short accent bar at the '
        'leading edge marks each control that differs from the script’s value: Tags, Accel and the unticked '
        'check box. Hovering a bar (here Tags’) says what the script sent; clicking it puts that one control back '
        '(MuseScore’s per-property reset). The marker is also in the control’s accessible description '
        '(“changed”). In high contrast the bar uses the accent too, 2 px wide.</p></div>',
        '</div>',
        '<div class="row" style="margin-top:20px">',
        f'<div class="col">{save}<p class="cap">Save as preset… '
        'asks for a name in a popover over ⋯ (PK1). Enter saves; a name already used turns Save into '
        '<b>Replace</b> and says so. Esc or Cancel closes the popover only.</p></div>',
        '<div class="col" style="max-width:560px"><div class="notes"><b>What is kept, and under which key</b><ul>'
        '<li><b>Key:</b> the script file and the dialog’s controls (each one’s class and name), so every macro '
        'of the script that shows the same dialog shares its last answers and presets, and a script with two '
        'dialogs (HYDRA’s pages) keeps them apart. AD1 keeps the size under the same controls.</li>'
        '<li><b>Last used:</b> the values and the button of the last answer that was not the cancel button or '
        'Esc. One per key.</li>'
        '<li><b>Presets:</b> the values by control name (labels and buttons are not kept). Loading one fills the '
        'controls it names and leaves the others as they are; a dropdown value that is no longer one of the '
        'items is skipped.</li>'
        '<li>Kept in the profile’s settings, so they survive a restart; the 200 keys used longest ago are '
        'dropped first.</li>'
        '<li><b>Delete preset ›</b> asks first: a message box “Delete the preset “Blue to clear”?” with '
        '<b>Delete</b> and <b>Cancel</b>, Cancel the default (PK1’s safe default).</li>'
        '</ul></div></div>',
        '</div>',
    ])


# ---------------------------------------------------------------------------------------------
# Repeat last run


def automation_panel(hover_repeat=True):
    tip = (A.tooltip("Repeat last run: Gradient by character, as Apply with the same answers",
                     "left:118px;top:72px") if hover_repeat else "")
    tools = (f'<span class="fld" style="flex:1"><span class="ftext phd">Filter macros</span></span>'
             f'<span class="tb">{ic("automation")}</span><span class="tb">{ic("refresh")}</span>'
             f'<span class="tb">{ic("run-script")}</span><span class="tb{" hov" if hover_repeat else ""}">'
             f'{ic("repeat-run")}</span>')
    rows = ('<div class="tr s">lyger.GradientEverything.moon</div>'
            '<div class="tr m cur">Gradient by character<span class="k">Ctrl+Alt+G</span></div>'
            '<div class="tr m">Gradient by line</div>'
            '<div class="tr s">ua.HYDRA.lua</div>'
            '<div class="tr m">HYDRA</div>'
            '<div class="tr m">HYDRA: repeat last</div>'
            '<div class="tr s">phos.Abacus.moon</div>'
            '<div class="tr m">Abacus</div>')
    return (f'<div class="dock" style="width:330px;position:relative"><div class="dhead">Automation'
            f'{ic("panel-menu")}</div><div class="strip">{tools}</div><div class="tree">{rows}</div>{tip}'
            f'<span class="ptr" style="left:306px;top:50px">{ic("pointer", 16)}</span></div>')


def automation_menu():
    return (f'<div style="position:relative;width:390px;height:250px">'
            f'<div class="mbar"><span>File</span><span>Edit'
            f'</span><span>Video</span><span>Audio</span><span style="text-decoration:underline">Automation</span>'
            f'</div>' + menu([
                mi("Load script…", "automation"),
                mi("Refresh autoload scripts", "automation"),
                mi("Run the last loaded script", "run-script"),
                mi("Rerun last macro", "rerun-macro"),
                mi("Repeat last run", "repeat-run", short="", cur=True),
                mi("Show panel", "automation"),
                SEP,
                mi("Gradient by character", short="Ctrl+Alt+G"),
                mi("HYDRA"),
            ], "left:150px;top:28px;min-width:250px") + '</div>')


def sheet_repeat(key):
    th = THEMES[key]
    changed = gradient_values(**LAST)
    # the script added a control since the last run
    changed.append(dict(**{"class": "checkbox"}, name="clip", x=1, y=8, w=3, label="Follow the &clip", value=False,
                        hint="Run the gradient along the Line’s \\clip."))
    shown = dialog("Gradient by character", changed, BUTTONS, width=470,
                   changed=("tags", "accel", "bord"),
                   bar=(f'<div class="ibar">{ic("info")}<span>This dialog has changed since the last run, so it is '
                        'shown with your last answers filled in.</span></div>'),
                   hint="", focus_button=0)
    flow = "".join([
        '<div class="fbox"><b>Repeat last run</b>The last macro run, on the Lines selected now.</div>',
        '<div class="farr">→</div>',
        '<div class="fbox"><b>The script shows a dialog</b>Compared with the one at the same place in the last '
        'run: the controls’ classes, names, places and dropdown items, and the buttons.</div>',
        '<div></div><div></div>',
        '<div class="fbox"><b>The same</b> <span class="pill">replayed</span> No window opens. The script gets '
        'the last run’s values and button, exactly as it got them then. The next dialog of the run is '
        'compared in turn.</div>',
        '<div></div><div></div>',
        '<div class="fbox"><b>Different, or not in the last run</b> <span class="pill">shown</span> The dialog '
        'opens with the last answers filled in where the control names match, the markers on them and the '
        'bar above saying why. From there the run goes on as a normal one: later dialogs are shown, filled in '
        'the same way.</div>',
    ])
    return "".join([
        f'<h1>AD2 · Repeat last run ({th["name"]})</h1>',
        '<p class="note">A new command beside Rerun last macro. Rerun last macro (unchanged) runs the macro '
        'again and shows its dialogs as usual, with the script’s own values; Repeat last run runs it again '
        'with the answers given last time and shows no dialog when it is the same as last time (GIMP’s Repeat '
        'Last and Re-Show Last). Normal runs, and Rerun, are unchanged. It is in the Automation panel’s tool '
        'strip, the Automation menu and the Hotkeys list (Global, <code>GLOBAL_AUTOMATION_REPEAT_LAST_RUN</code>, '
        'no key by default). The progress card and the run’s log are AD3’s; a repeated run shows them as any '
        'run does, and its log starts with “Repeated with the last answers”.</p>',
        '<div class="row">',
        f'<div class="col">{automation_panel()}<p class="cap" style="max-width:330px">The panel’s tool strip: '
        'Load script, Refresh, Rerun last macro and the new Repeat last run (proposed icon: the script page '
        'with a repeat loop). Its tooltip names the macro and the button it will press. Off while a macro runs, '
        'under a modal window, and until a run has answered a dialog.</p></div>',
        f'<div class="col">{automation_menu()}<p class="cap" style="max-width:390px">The Automation menu: '
        'Repeat last run after Rerun last macro, with the same rules for being off. Its status-bar help: “Run '
        'the last macro again with the same answers, without its dialogs when they are unchanged”.</p></div>',
        '</div>',
        '<h2>What happens at each dialog of the repeated run</h2>',
        '<div class="row">',
        f'<div class="col" style="width:560px"><div class="flow">{flow}</div>'
        '<p class="cap" style="max-width:560px">The script’s own values may differ from last time (a value it '
        'works out from the selection, say): a repeat still sends the last answers, since that is what was '
        'asked for. A run that was stopped at a dialog by its cancel button or Esc is not recorded, so Repeat '
        'goes on repeating the last run that got past its dialogs. A macro with no dialog is simply run again. '
        'The record is kept for the session only.</p></div>',
        f'<div class="col">{shown}<p class="cap">The fallback: the script added “Follow the clip” since the '
        'last run, so the dialog is shown with the last answers in Tags, Accel and the border box (marked), '
        'the new control at the script’s value, and Apply focused, so Enter carries on.</p></div>',
        '</div>',
    ])


# ---------------------------------------------------------------------------------------------
# The setting and the rules


RULES = [
    ("The script sends the same value it sent last time (a default it never changes)",
     "Your last answer is filled in, marked.", "Tags: the script always sends <code>\\c\\3c\\alpha\\blur</code>"),
    ("The script sends a different value (it works it out from the selection, the clipboard or its own "
     "config)", "The script’s value wins; nothing is filled in.",
     "a-mo’s start frame; PerspectiveMotion’s values from the clip"),
    ("A control that was not in the last answer", "The script’s value.", "a control added by a new version"),
    ("No last answer yet (first run of this dialog)", "The script’s values.", ""),
    ("Last used, a preset, Reset (asked for in the ⋯ menu)", "Always applied, whatever the script sent.", ""),
    ("Repeat last run", "The last answers, as on sheet 2; the setting does not matter.", ""),
]

QUESTIONS = [
    ("Where the ⋯ button goes",
     "At the leading end of the footer, before the script’s buttons (MuseScore’s left-side buttons, DG1’s "
     "leadingActions), reached by Tab after the grid.",
     "In the title bar beside Maximise."),
    ("What “the same dialog” means for Repeat",
     "The same controls (class, name, place, dropdown items) and buttons; the values the script sends may "
     "differ, and the last answers are still replayed.",
     "Also require the script’s values to be the same, otherwise show the dialog filled in."),
    ("Cancelled runs",
     "A run stopped at a dialog by its cancel button or Esc is not recorded; Repeat repeats the last run "
     "that got past its dialogs.",
     "Record it too: Repeat would then press Cancel again."),
    ("The key for last answers and presets",
     "The script file and the dialog’s controls (class and name), shared by the script’s macros that show the "
     "same dialog.",
     "Per macro as well (AD1’s size key), so two macros never share presets."),
    ("Changed-value marker",
     "A 2 px accent bar at the control’s leading edge; hover says the script’s value, a click puts it back.",
     "The bar only, with Reset in the ⋯ menu as the one way back."),
    ("Repeat’s default key", "None; it can be bound in Hotkeys (Rerun last macro has none either).",
     "A default key (the owner’s pick)."),
    ("“Fill in my last answers” scope",
     "One setting for every script, on PF1’s Automation page (off by default).",
     "Per script, as a check item in the ⋯ menu."),
    ("Deleting a preset", "Asks first (message box, Cancel the default).", "Deletes at once from the menu."),
]


def settings_page(key):
    side = "".join(f'<div class="{"cur" if s == "Automation" else ""}">{s}</div>' for s in
                   ["General", "Appearance", "Editing", "Grid", "Video", "Audio", "Automation", "Hotkeys"])
    return (f'<div class="dlg setp">{A.titlebar("Settings")}<div style="display:flex"><div class="side">{side}</div>'
            '<div class="main"><div class="sect">Script dialogs</div>'
            f'<span class="cb">{A.box(True)}<span class="cbt">Fill in my last answers</span></span>'
            '<div class="sdesc">Script dialogs open with the answers you gave last time. A value the script works '
            'out itself, from the selection say, still wins. Off: they open with the script’s values, and '
            '⋯ › Last used fills yours in.</div>'
            '<div class="sect" style="margin-top:6px">Kept answers and presets</div>'
            f'<div style="display:flex;gap:8px;align-items:center;margin-left:24px">{btn("Clear last answers…", small=True)}'
            '<span class="sdesc" style="margin:0">Presets are kept; delete them from a dialog’s ⋯ menu.</span></div>'
            '</div></div></div>')


def sheet_rules(key):
    th = THEMES[key]
    rrows = "".join(f'<tr><td>{a}</td><td>{b}</td><td class="m">{c}</td></tr>' for a, b, c in RULES)
    qrows = "".join(f'<tr><td>{i + 1}. {a}</td><td><span class="yes">Recommended:</span> {b}</td><td class="m">{c}</td></tr>'
                    for i, (a, b, c) in enumerate(QUESTIONS))
    return "".join([
        f'<h1>AD2 · “Fill in my last answers” and the rules ({th["name"]})</h1>',
        '<div class="row">',
        f'<div class="col">{settings_page(key)}<p class="cap" style="max-width:640px">The opt-in setting, off by '
        'default, on PF1’s Automation page (#337; if PF1 has not merged when AD2 does, the PR says so and the '
        'row moves there with it). Clear last answers… asks first and forgets every kept answer and Repeat’s '
        'record.</p></div>',
        '</div>',
        '<h2>The safe-prefill rule: what each control shows when a dialog opens with the setting on</h2>',
        '<div class="row"><div class="col"><table class="kt wide"><tr><th>Case</th><th>The control shows</th><th>Example</th></tr>'
        f'{rrows}</table><p class="cap" style="max-width:1100px">The safe-prefill rule (research §3.4): a last '
        'answer replaces a control’s value only when the script sent the same value as the last time. A script '
        'that works out its own defaults, or remembers them itself (Relocator, AegisubChain’s documented '
        'clash), wins without any special case. The comparison is on the value as the script sent it.</p></div>',
        '</div>',
        '<h2>Questions (recommendation first)</h2>',
        f'<table class="kt wide"><tr><th>Question</th><th>Recommendation</th><th>Alternative</th></tr>{qrows}</table>',
        '<div class="notes" style="margin-top:18px"><b>Ledger rows (compatibility-decisions.md)</b><ul>'
        '<li><b>AD2-repeat-last-run</b>: a new command; it replays the recorded answers to the script without '
        'showing an unchanged dialog. Legacy and Aegisub only have Rerun.</li>'
        '<li><b>AD2-dialog-memory</b>: Last used, presets, Reset and the markers in the ⋯ menu; they change the '
        'values shown before a button is pressed only.</li>'
        '<li><b>AD2-fill-last-answers</b>: the opt-in setting with the safe-prefill rule. Off by default, so a '
        'script sees exactly Aegisub’s dialog unless the user turns it on.</li>'
        '</ul></div>',
    ])


def main():
    for key in THEMES:
        (OUT / f"ad2-{key}-menu.html").write_text(page(key, sheet_menu(key)))
        (OUT / f"ad2-{key}-repeat.html").write_text(page(key, sheet_repeat(key)))
        (OUT / f"ad2-{key}-rules.html").write_text(page(key, sheet_rules(key)))


main()
