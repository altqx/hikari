# Text style

The rules for every English string a user sees in HikariSub: menus, buttons, labels, column headings, tooltips, status-bar help, accessible names, messages, toasts and the Log. It is for [#596](https://github.com/altqx/hikari/issues/596). The inventory of today's strings, and what breaks these rules, is in [text-inventory.md](text-inventory.md).

The owner decided three rules on 2026-11-02 ([#596](https://github.com/altqx/hikari/issues/596)): **British English**, **sentence case**, and **"…"** (U+2026), never "...". The rest of this guide follows from those three, from [CONTEXT.md](../../CONTEXT.md), and from what the app already does in most places.

## Scope

- **In scope:** every source string in `i18n/hikarisub_<lang>.ts`, meaning everything passed to `qsTr`, `qsTranslate`, `QT_TRANSLATE_NOOP` and `tr`. That includes the command names in `src/ui/command_names.cpp`, which double as hotkey descriptions. It also covers the text that C++ sends to toasts and the Log.
- **Out of scope:**
  - The `HikariSub.Automation.Gettext` keys (`i18n/hikarisub_gettext_*.ts`). Scripts look them up by their exact legacy text through `aegisub.gettext` ([localisation.md](../localisation.md)), so they never change.
  - The text a script shows itself: its macro names, its dialogs, its log lines.
  - ASS tag names, file extensions, format names, setting keys and hotkey command ids.
- A change to wording never changes a command id, a setting key or a saved file.

## Spelling

British English. Use the `-ise` forms and British spellings:

| Write | Not |
|---|---|
| colour, behaviour, favourite | color, behavior, favorite |
| centre, metre | center, meter |
| licence (noun), license (verb) | license (noun) |
| grey | gray |
| cancelled, labelled, travelling | canceled, labeled, traveling |
| initialise, organise, customise, minimise, capitalisation, synchronise, analyse | initialize, organize, customize, minimize, capitalization, synchronize, analyze |
| catalogue | catalog (decision 2) |
| towards, afterwards | toward, afterward |

Keep these as they are:
- **dialog**, for a window. "Dialogue" is an ASS event type, and using one word for both would mix them up.
- **program**, for software. In computing, British English writes it this way too.
- **disk** and **analogue**.
- Proper names, which keep their own spelling: "GNU General Public License", "Apache License 2.0".

## Case

**Sentence case** everywhere: capitalise only the first word and proper names. This covers menu items, buttons, labels, headings, window titles, tab names and tooltips.

- "Sort selected lines", "Apply to all", "Ignore all". Never "Apply to All".
- **Proper names** keep their capitals:
  - products: HikariSub, Aegisub, FFmpeg, Harper, GitHub, Windows;
  - formats and acronyms: ASS, SRT, CPS, FPS;
  - key names: Ctrl, Shift, Enter, Esc;
  - languages: Polish, Japanese;
  - the ASS section names Script Info, V4+ Styles and Events.
- **Names of the app's own surfaces** capitalise only their first word, the way they appear in the menus: Grid, Line editor, Style manager, Shift times, Timing postprocessor, Translation mode, Template editor, Log, Home. Inside a sentence the name keeps that capital, so it reads as a name: "Open in Style manager", "Turn off Translation mode?", "in the Grid". Ordinary nouns stay lower case: line, style, actor, effect, layer, margin, tag, syllable, keyframe. Compare "the line's style" (ordinary) with "Style manager" (a surface).
- Settings paths use "›": "Settings › Appearance", "Help › Credits". Each step is written exactly as the page or menu shows it.

## Punctuation

- **Ellipsis "…" (U+2026)**, never three dots. Put it on a command that cannot finish without more input from the user: one that opens a file picker, a dialog that asks for values, or a panel whose own Apply does the work. Examples: "Open video…", "Save as…", "Shift times…", "Paste over…", "Select lines…", "Find…".
- **No ellipsis**:
  - on a command named after a tool or window that you work in or look at: Settings, Style manager, History, Kanji timer, Script properties, About;
  - on a toggle, or on a command that acts at once;
  - on a status that is in progress ("Indexing video…" is fine).
- **Quotes:**
  - Curly quotes: “name” for a name, a file or text quoted from the subtitles, and ’ for the apostrophe: “%1” already exists, the line’s style, don’t.
  - Never straight quotes. The one exception is text the user types literally, such as an ASS tag `\pos(…)` or a regex, which is set in the monospace font rather than quoted.
- **Full stop:**
  - Labels, buttons, menu items, headings, column headings, placeholders and one-phrase tooltips take none.
  - A message, a description or help text that is one or more full sentences ends with one.
  - A toast or message-box title takes none.
- **Colon:**
  - None at the end of a field label ("Replace with", not "Replace with:").
  - Inside a line it introduces a value or a reason: "Line %1: no end time", "Not applied: the line cannot take these times".
- **Separators:**
  - " · " joins short facts on one line ("Saves %1 · Discards %2").
  - "›" for a path through menus or pages.
  - "–" (en dash) for a range ("0–255").
  - "—" is not used.
- **No "&" for "and"** in text. "&" in a source string only marks a mnemonic letter, and "&&" is a literal ampersand.
- **Hard line breaks:**
  - Never "\n" inside a label, tooltip or message to wrap it. The control wraps the text.
  - A line break may separate two facts in a tooltip that lists values ("Volume: %1", then "Output device: %2").
  - A message that needs a list puts the list under Details.
- **Contractions** are fine where they read naturally ("Don’t ask again", "can’t"). Use them the same way in one surface.
- **No exclamation marks, and no "please".**

## Numbers, units and keys

- Numbers are digits ("2 lines", "500 ms").
  - A unit follows the number after a space: `%1 ms`, `%1 px`, `%1 %`, `%1 fps`. Write "ms", not "milliseconds", in labels and tooltips.
  - Times use the Grid's format (`0:00:00.00`). Frames are "frames".
- Plurals go through `%n` (numerus) in every language: "%n line(s)" in the source, translated per form. Don't build a plural by joining strings.
- **Keys:**
  - Write them the way Qt shows them: "Ctrl+Z", "Shift+Del", "Alt+Up", with no spaces around "+". The mouse is "click", "right-click", "wheel", "Shift+click".
  - A bound key is never written into a label or menu text ("Copy\tCtrl+C"). The menu's shortcut column and the tooltip show the bound key from Settings, so a rebound key stays correct.
  - A fixed key that the code handles itself, such as Esc closing a popup, may be named in help text.

## Kinds of text

| Kind | Form | Write | Not |
|---|---|---|---|
| Command (menu item, button that acts, hotkey name) | Verb first, imperative, with what it acts on | Paste over…, Shift times…, Delete lines, Show in folder | Pasting over, Lines deletion, Open video containing folder |
| Check item, toggle | The state when it is on, as a noun phrase or a verb phrase that names that state | Auto-scroll, Show keyframes, Apply changes automatically | Show / hide render cost, Toggle auto-scroll |
| Choice in a group (radio, combo box item) | A noun or adjective, parallel with its siblings | Waveform, Spectrum | Use waveform, The spectrum |
| Field label, column heading, filter chip | A noun, singular for one value, with no leading article | Start time, End time, Style, Actor | The starting time, Styles (for one style), Search text: |
| Section heading, panel, window, tab | A noun phrase naming the content or tool. A window opened by a command takes that command's name, without the "…" | Style manager, Script properties, Select lines, Shift times | Font selection (for "Choose font…"), Dialogue shifting window |
| Placeholder | An example or a short hint, muted. Never the label again | 0:00:05.00, Filter macros | Enter the search text here |
| Icon-only button tooltip | The command's name, the same as its menu item. The bound key follows from the binding | Play line (key shown by the tooltip) | Click here to play the line |
| Status-bar help (menu help) | One clause in the third person, starting with the verb. No full stop | Shows or hides the Log | Allows to show the Log., Log |
| Description, help text | Full sentences, plain and short, saying what happens. A full stop at the end | Lines outside the selection keep their times. | This option allows you to… |
| Message-box title, toast title | The command that produced it ([visual-language.md](visual-language.md), "Notices and the Log") | Open video, Save, Find | Warning, Error, Info |
| Message text | What happened, then what to do, in one or two sentences. Technical text goes under Details | %1 did not load. Its error is in Details. | Error loading Lua script: %1\n%2 |
| Question | A question that names the outcome. The buttons name the actions | Delete the preset “%1”? Delete / Cancel | Are you sure? Yes / No |
| Empty state | One muted line saying why it is empty, plus the one command that helps | No templates yet. Add one with New to preview it here. | Nothing here! |
| Accessible name | The visible label. For an icon-only control, its tooltip name. The role and state come from Qt, not from the text | Play line | Play line button, Play line (checked) |

Never start a label with "The" or "A". Prefer "Selected text" to "The line’s selected text", and "System" to "The system’s". A full sentence may start with an article.

Don't say "Allows…", "This option…" or "Click to…". Say what the control does.

Never build a sentence out of translated pieces ("Video: " + name, " or "). Use one string with `%1` placeholders, so every language can order the words its own way.

## One name per thing

Each thing has one name everywhere: menu bar, context menus, ⋯ menus, the panel header, the window title, Settings, tooltips, help, messages and the site's guides. The terms follow [CONTEXT.md](../../CONTEXT.md). Where CONTEXT.md and the screen disagree, the screen changes, not the glossary, unless an owner decision below says otherwise.

| Thing | Name | Not |
|---|---|---|
| An entry in the subtitle list | line | dialogue, event, subtitle line, row (except a Grid row in Grid-only text) |
| The list of lines | Grid | subtitle grid, subtitles list, list of lines |
| The editing panel | Line editor | edit box, editor (alone) |
| What the user opens and edits | document (the content, saved or untitled); subtitle file (the file on disk); "subtitles" as the file type in File commands ("Open subtitles…", "Recently opened subtitles") | script (that means an Automation script, except in Script properties and Script resolution), project, tab (for the content) |
| A line's appearance defaults | style | — |
| A line's columns | Layer, Start time, End time, Style, Actor, Effect, Left margin, Right margin, Vertical margin, Text | The starting time, Styles |
| The translation-review flag | Unconfirmed | Not confirmed, doubtful |
| Document-level ASS information | Script properties | ASS file properties, ASS subtitle properties, Subtitle properties |
| Moving chosen times | Shift times (the panel and command); time shift (the concept) | Shift times / run timing postprocessor |
| Lead-in, lead-out and keyframe snapping | Timing postprocessor | postprocessor (alone) |
| Matching a translation's lines to the original | Translation matching (window and command name) | Dialogue shifting window |
| Applying the audio panel's timing to the line | Apply changes; Apply changes automatically; Go to the next line after applying | Commit, Auto-commit, Next after commit |
| Taking a panel out of the window | Float | Undock, detach |
| Removing a panel from view | Close | Hide (for a panel) |
| Opening a file's folder | Show in folder; "Show video in folder", "Show subtitles in folder" when the menu has several | Open video containing folder, Open the folder containing the subtitles, Open folder |
| A key combination | shortcut (decision 1) | hotkey, key binding |
| The settings window | Settings | Options, preferences, program settings |
| A menu of more items | More options (tooltip of ⋯) | %1 options, More audio options |
| The spelling tool | Spellchecker (panel); Check spelling (command) | spell checker, spell checking |
| A recovery copy | autosave | auto save |
| Collections of styles or fonts | Style catalogue, Font catalogue (decision 2) | catalog |
| Full-screen video | full screen (noun), full-screen (adjective) | fullscreen |
| Deleting versus removing | Delete destroys data (lines, files, presets, autosaves). Remove takes an item out of a list and leaves it intact (recent files, a shortcut, a font from a catalogue) | — |
| Play or pause in one control | Play or pause (command); Play and Pause (the button's two states) | Play / Pause |

## Changing a string

- **Translations:** a changed source string goes through the TR1 process in the same PR, into the ko, pl, ta and th `.ts` files. The catalogues hold no unfinished entries, and a PR keeps it that way.
  - A change that leaves the meaning alone keeps its translations, carried over as finished. This covers spelling, quotes, case, the ellipsis, a removed colon or full stop, and a removed hard-coded key. The translation takes the same punctuation change: it loses the colon or the "…" its source lost, and gains a full stop with its source (not in Thai, which has none).
  - A change of wording or meaning gets a new finished translation in each language, in the terms that catalogue already uses for the same things.
  - The source is renamed in place in each catalogue. Running `hikari_lupdate` afterwards must find no new source and change no catalogue.
- **Translator comments** (`//:`, `//~`) stay with their strings. If a rename makes a comment wrong, update the comment.
- **The site:** each area PR lists the labels it changed that the site's guides quote, under "Site:" in its report.
- **Tests** that look for a string by its text change with it. `tests/ui/menu_bar.txt` is regenerated, not edited by hand.

## The guard

`hikari_i18n_text_style_tests` (ctest, label i18n) reads every source in `i18n/hikarisub_pl.ts`, with mnemonics removed. It fails on:

- "..." in any source;
- a straight double quote, or an apostrophe next to a letter;
- an American spelling from the table above (the names "GNU General Public License" and "Apache License" excepted);
- a label that starts with "The " (a source with no full stop and at most eight words). A lower-case "the …" is a clause that follows a colon in a message, as in "Not applied: the line cannot take these times";
- a non-first word in title case within a short label, unless it is on the test's proper-name list, is all capitals or an identifier with inner capitals (YCbCr, DirectSound), or starts a new line or follows a colon, a full stop, an opening quote, "›" or " · ";
- a short label that ends with ":", or a source with leading, trailing or double spaces or line breaks. The code adds the line break or space around the translated text;
- a tab (a key written after the text), or "please".

Its allow-list, `i18n/text-style-allow.tsv`, has one row per exception: context, source (with `\n` and `\t` for line breaks and tabs), rule and a reason. A failure prints each new violation as a row ready to paste. "pending #596 <area>" marks a string that area's PR fixes. The other reasons are lasting: a folder name on disk, a style's name, a button named in a sentence. A row whose string no longer exists does not fail, so a PR that renames a string doesn't conflict with another that removes the row.

## Owner decisions

The owner answered yes to all six on [#596](https://github.com/altqx/hikari/issues/596) (2026-11-02), with the sample of before/after strings:

1. **"Shortcut", not "hotkey",** everywhere the user reads it: Settings › Hotkeys becomes Settings › Shortcuts. The file `hotkeys.toml` and the command ids stay.
2. **"Catalogue",** with CONTEXT.md's Style catalogue and Font catalogue renamed to match. The setting keys and the `catalogs` folder keep their names.
3. **"Apply changes", not "Commit",** in the audio panel: Apply changes, Apply changes automatically, Go to the next line after applying.
4. **"Close" and "Float" for panels,** in View › Panels and in the panel's ⋯ menu alike.
5. **No ellipsis on tool and window names:** "Kanji timer…" becomes "Kanji timer". "Script properties", "Settings" and "Style manager" stay without one.
6. **"Document" on screen,** following CONTEXT.md. "Subtitles" stays as the file type in File commands only.

CX2 ([#578](https://github.com/altqx/hikari/issues/578)) named the Grid's "Dialogue shifting window" command "Translation matching…".
