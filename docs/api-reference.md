# Hikari API reference

Version 1 of the Hikari API, generated from HikariSub's own registry: the same reference `hikarisub --export-docs` writes to `api.json`. A plugin calls these operations with `hikari.call(operation, params)`, or `api.call` in a QML panel, and listens to the events with `hikari.events.on(name, fn)`.

## Operations

| Operation | Mark | Since | Summary |
| --- | --- | --- | --- |
| `documents.list` | read | 1 | The open Documents, in tab order, with the protected reference last. |
| `documents.get` | read | 1 | One Document's summary, Line and Style counts and its pending Line editor draft. |
| `lines.list` | read | 1 | Lines in Document order, a page at a time. |
| `lines.get` | read | 1 | Lines by id, in the order asked. |
| `lines.find` | read | 1 | Lines matching every criterion given: text or regex in one field, exact Style, Actor or Effect, Comment, and a time window. |
| `styles.list` | read | 1 | The Document's Styles, in order. |
| `info.list` | read | 1 | The Document's Script Info properties, in order. |
| `selection.get` | read | 1 | The selected Lines, in Document order, and the active Line. |
| `api.version` | read | 1 | The API version: it grows by one with each release that adds to the API. |
| `commands.list` | read | 1 | The commands of menus, toolbars and shortcuts, with their enabled state and keys. |
| `panels.list` | read | 1 | The workspace panels and whether each is open, floating or focused. |
| `settings.list` | read | 1 | Settings with their type, scope, default and current value. |
| `settings.get` | read | 1 | The current values of the settings named. |
| `commands.run` | write | 1 | Runs an enabled command as its menu item, toolbar button or shortcut would. |
| `panels.open` | write | 1 | Opens a panel where the layout keeps it. |
| `panels.close` | write | 1 | Closes a panel. |
| `panels.focus` | write | 1 | Opens a panel if needed and gives it the keyboard focus. |
| `settings.set` | write | 1 | Changes a setting; the value must have the setting's type. |
| `document.apply` | write | 1 | Applies edits prepared against a revision as one undo step; a stale revision changes nothing. |
| `selection.set` | write | 1 | Selects Lines and makes one active, as a click in the Grid would; never an undo step. |
| `document.group_begin` | write | 1 | Groups the steps the caller commits on the editing target from now on, by any write, into one undo step named after the caller and `name`. The group ends at document.group_end, when the plugin's command or event handler returns, when the plugin stops, or at the first step someone else makes. Plugins only. |
| `document.group_end` | write | 1 | Ends the caller's group: its steps become one undo step. |
| `video.info` | read | 1 | Whether a video is open, its size and aspect ratio, the frame shown and the keyframes. |
| `video.frame_at_time` | read | 1 | The video frame shown at a time. |
| `video.time_of_frame` | read | 1 | The time a video frame starts, in milliseconds. |
| `video.frame` | read | 1 | One video frame as BGRA pixels in base64, with every Line, one Line or none drawn on it, optionally cropped, scaled to fit maxEdge and maxPixels. |
| `video.ink_bounds` | read | 1 | Where one Line's ink, or every Line's, lies on a video frame, in video pixels. |
| `video.colours` | read | 1 | The colours of a video frame at points, as ASS &HBBGGRR&, each the mean of the square radius pixels around it. |
| `video.keyframes` | read | 1 | The keyframes from one time to another, each with its start time. |
| `video.frames` | read | 1 | Video frames from one to another, every stride-th, scaled to fit maxEdge, as luma or BGRA in base64. At most 256 frames and 16 MiB of pixels come in one call. |
| `video.set_keyframes` | write | 1 | Replaces the video's keyframes as Open keyframes does. With file, writes them there as an Aegisub keyframes file first and opens it. Not an undo step. |
| `audio.info` | read | 1 | Whether audio is open and loaded, its duration and sample rate. |
| `audio.peaks` | read | 1 | The lowest and highest 16-bit sample in each of `buckets` equal slices of a time range. |
| `files.list` | read | 1 | The files and folders the user granted this caller; with folder, also what that folder holds. |
| `files.read` | read | 1 | A granted file's content, as UTF-8 text or base64; at most 16 MiB. |
| `files.write` | write | 1 | Replaces a file granted for writing with new content; in a writable folder, makes it and its folders. An AI caller's write is staged for the user to review and reaches the disk only when kept. |
| `skills.list` | read | 1 | The caller's skills: each skill folder's name, whether it is the Project's or the user's, and its files, SKILL.md among them. |
| `skills.read` | read | 1 | One file of a skill skills.list gives, as UTF-8 text; at most 256 KiB. |
| `automation.macros` | read | 1 | The loaded Automation macros, as the Automation menu lists them. |
| `automation.run_macro` | write | 1 | Runs a loaded Automation macro on the active tab's selection, as the menu would, as one undo step. Answers fill its dialogs. Plugins only. |
| `automation.run` | write | 1 | Saves a Lua Automation script to the AI scripts folder and runs one of its macros on the active tab's selection, once the user approves the script. The run is one undo step. |
| `translation.project` | read | 1 | The Document's Honya project files: PROJECT.md, GLOSSARY.md, CHARACTERS.md and STYLE.md, those that exist. folder is null when the Document has no project folder. |
| `translation.project_write` | write | 1 | Replaces one of the Document's Honya project files, keeping Honya's Markdown and honya:data format. |
| `nodes.catalogue` | read | 1 | The node types the node-effect editor offers, with their ports and parameters. |
| `nodes.validate` | read | 1 | Checks a node-effect graph; each problem names its node and port or field. |
| `effects.graphs` | read | 1 | The Document's node-effect graphs, one per graph Line. |
| `effects.graph` | read | 1 | One graph Line's graph, with what keeps it from running. |
| `effects.edit` | read | 1 | Edits a graph in steps (add, connect, disconnect, remove, set) and gives the new graph with its problems; nothing in the Document changes. |
| `effects.preview` | read | 1 | Runs a graph on the Document's karaoke Lines and gives the Lines it would write; nothing is applied. Lua nodes don't run. |
| `effects.save` | write | 1 | Writes a graph to the Document as one undo step: into a new graph Line, or in place of a graph Line's. |
| `effects.apply` | write | 1 | Runs a graph Line and writes its Lines as one undo step, replacing those it wrote before; the karaoke Lines it ran on become Comments. Lua nodes don't run, so a graph with one applies from the node-effect editor. |
| `plugins.list` | read | 1 | The installed plugins with their state, trust tier and commands. |
| `plugins.manifest` | read | 1 | An installed plugin's hikari-plugin.json. |
| `plugins.scaffold` | write | 1 | Writes a plugin from the authoring guide's template into the AI plugins folder and packs it; nothing is installed. |
| `plugins.review` | read | 1 | What installing a package would do, as the install question shows it: its trust tier, signing, permissions, commands and files. |
| `plugins.install` | write | 1 | Installs a scaffolded or granted plugin package and turns it on, once the user approves it in HikariSub; it asks every time. |
| `plugins.run` | write | 1 | Runs an installed plugin's command as its menu item would. |
| `typeset.perspective` | read | 1 | The Line's text with the tags that render it on a quad (VT3's perspective solver): \pos, \org, \frx, \fry, \frz, \fax, \fscx and \fscy. |
| `typeset.clip` | read | 1 | The Line's text with its first clip replaced by a rectangle or a vector clip. |
| `typeset.drawing` | read | 1 | The Line's text with its first drawing replaced by a shape (\p1), placed at a position with an alignment. |
| `typeset.shift` | read | 1 | Moves the Lines' positions, \move points, clips, drawings and \org by an offset, as the Position shifter does. |
| `typeset.hydra` | read | 1 | Changes one tag across the Lines as the Hydra strip does: set it, add to it, multiply it by line number, or run a gradient across characters or Lines. |
| `typeset.check` | read | 1 | Renders one Line alone, with a proposed text, on its frame and compares its ink with a target. |
| `typeset.apply` | write | 1 | Applies typeset Lines as one undo step after every one passes the render check; when one misses its target nothing is applied. |
| `project.info` | read | 1 | The Project of the window the user works in, or null. |
| `vcs.refs` | read | 1 | The branches, tags and recent commits of the git repository holding the Document, or the Project's. Plugins whose manifest asks for vcs.read only. |
| `vcs.show` | read | 1 | The Document's file as a git branch, tag or commit has it. Plugins whose manifest asks for vcs.read only. |
| `karaoke.syllables` | read | 1 | A Line's karaoke syllables as the templater reads them: text, tag, start and end in ms. |
| `karaoke.split` | read | 1 | Splits Lines without \k tags into syllables as the audio editor's karaoke mode does, the time shared by character; Lines that already have \k tags keep them. |
| `karaoke.time` | read | 1 | Times a Line's existing syllables: each syllable's end in ms, in order; the \k tags are rewritten. |
| `karaoke.onsets` | read | 1 | Where sung notes start in the open audio, in ms, within a range. |
| `karaoke.snap` | read | 1 | Moves each syllable boundary inside the Lines to the nearest vocal onset within the window, keeping every syllable at least 10 ms long. |
| `karaoke.kanji` | read | 1 | Copies a timed romaji Line's syllables onto its kanji Line as the Kanji timer suggests the groups. |
| `karaoke.furigana` | read | 1 | The readings the dictionary suggests for a Line's kanji runs, in hiragana. |
| `karaoke.check` | read | 1 | The Karaoke check's problems: unclosed tags, totals that differ from the Line, empty syllables and syllables outside the Line. |
| `karaoke.template_preview` | read | 1 | The Lines the karaoke templater generates from one karaoke Line, without applying them. |
| `karaoke.apply_template` | write | 1 | Runs the karaoke templater on the karaoke Lines, or the Lines given, and applies what it generates as one undo step: old generated Lines are replaced. |
| `karaoke.align` | write | 1 | Runs the user's own WhisperX or Montreal Forced Aligner on the audio under the Lines and answers each word with its start and end in ms; the user approves every run, and nothing is applied. |
| `marks.set` | write | 1 | Marks Grid rows with a coloured bar and a tooltip, replacing the caller's marks on those Lines. Marks belong to the plugin, are never saved and go when it stops. Not an undo step. Plugins only. |
| `marks.clear` | write | 1 | Removes the caller's Grid row marks. Plugins only. |
| `marks.list` | read | 1 | The caller's Grid row marks on a Document. Plugins only. |

### `documents.list`

Read. Since version 1.

The open Documents, in tab order, with the protected reference last.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `documents` | array of objects | yes |  |
| `documents[].document` | [DocumentId](#documentid) |  |  |
| `documents[].title` | string |  |  |
| `documents[].path` | string or null |  |  |
| `documents[].format` | one of `ass`, `srt`, `tmplayer`, `microdvd`, `mpl2` or `text` |  |  |
| `documents[].revision` | [Revision](#revision) |  |  |
| `documents[].editingTarget` | boolean |  |  |
| `documents[].reference` | boolean |  | The protected comparison reference. |
| `documents[].dirty` | boolean |  |  |
| `documents[].readOnly` | boolean |  | A macro holds it, or it is the protected reference. |

### `documents.get`

Read. Since version 1.

One Document's summary, Line and Style counts and its pending Line editor draft.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  |  |
| `title` | string |  |  |
| `path` | string or null |  |  |
| `format` | one of `ass`, `srt`, `tmplayer`, `microdvd`, `mpl2` or `text` |  |  |
| `revision` | [Revision](#revision) |  |  |
| `editingTarget` | boolean |  |  |
| `reference` | boolean |  | The protected comparison reference. |
| `dirty` | boolean |  |  |
| `readOnly` | boolean |  | A macro holds it, or it is the protected reference. |
| `lineCount` | integer |  | At least 0. |
| `styleCount` | integer |  | At least 0. |
| `draftLine` | [LineId](#lineid) or null |  | The Line with uncommitted Line editor changes; a write commits them first. |

### `lines.list`

Read. Since version 1.

Lines in Document order, a page at a time.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `offset` | integer |  | At least 0. |
| `limit` | integer |  | 1000 when omitted. From 1 to 5000. |
| `fields` | array of [LineField](#linefield) |  | Only these fields besides id and row; all of them when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `total` | integer |  | At least 0. |
| `lines` | array of [Line](#line) | yes |  |

### `lines.get`

Read. Since version 1.

Lines by id, in the order asked.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `ids` | array of [LineId](#lineid) | yes | 1 to 5000 items. |
| `fields` | array of [LineField](#linefield) |  | Only these fields besides id and row; all of them when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `total` | integer |  | At least 0. |
| `lines` | array of [Line](#line) | yes |  |

### `lines.find`

Read. Since version 1.

Lines matching every criterion given: text or regex in one field, exact Style, Actor or Effect, Comment, and a time window.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `text` | string |  | A case-sensitive substring. Not empty. |
| `regex` | string |  | A regular expression, as Find and replace takes it. Not empty. |
| `ignoreCase` | boolean |  | For regex. |
| `in` | one of `text`, `translation`, `actor`, `effect` or `style` |  | Where text and regex look; text when omitted. |
| `style` | string |  |  |
| `actor` | string |  |  |
| `effect` | string |  |  |
| `comment` | boolean |  |  |
| `from` | integer |  | Lines ending after this time, in milliseconds. |
| `to` | integer |  | Lines starting before this time, in milliseconds. |
| `limit` | integer |  | 500 when omitted; total still counts every match. From 1 to 5000. |
| `fields` | array of [LineField](#linefield) |  | Only these fields besides id and row; all of them when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `total` | integer |  | At least 0. |
| `lines` | array of [Line](#line) | yes |  |

### `styles.list`

Read. Since version 1.

The Document's Styles, in order.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `styles` | array of [Style](#style) | yes |  |

### `info.list`

Read. Since version 1.

The Document's Script Info properties, in order.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `info` | array of objects | yes |  |
| `info[].key` | string | yes |  |
| `info[].value` | string | yes |  |

### `selection.get`

Read. Since version 1.

The selected Lines, in Document order, and the active Line.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `active` | [LineId](#lineid) or null | yes |  |
| `selected` | array of [LineId](#lineid) | yes |  |

### `api.version`

Read. Since version 1.

The API version: it grows by one with each release that adds to the API.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `version` | integer | yes | At least 0. |

### `commands.list`

Read. Since version 1.

The commands of menus, toolbars and shortcuts, with their enabled state and keys.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `window` | one of `global`, `subtitles`, `editor`, `video` or `audio` |  |  |
| `query` | string |  | A case-insensitive part of the command id or its name. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `commands` | array of objects | yes |  |
| `commands[].command` | string | yes | The stable command id, e.g. GLOBAL_SAVE_SUBS. |
| `commands[].name` | string | yes | The English name the shortcut editor shows. |
| `commands[].window` | one of `global`, `subtitles`, `editor`, `video` or `audio` | yes |  |
| `commands[].enabled` | boolean | yes |  |
| `commands[].disabledReason` | string |  |  |
| `commands[].keys` | array of string | yes |  |
| `commands[].kind` | one of `hotkey`, `macro` or `plugin` |  | Always given: a HikariSub command, an Automation macro (macro:<file>:<ordinal>) or a plugin's (plugin.<id>.<command>). |
| `commands[].file` | string |  | A macro's script file name. |
| `commands[].macro` | string |  | A macro's name. |

### `panels.list`

Read. Since version 1.

The workspace panels and whether each is open, floating or focused.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `panels` | array of objects | yes |  |
| `panels[].panel` | string | yes |  |
| `panels[].title` | string | yes |  |
| `panels[].open` | boolean | yes |  |
| `panels[].floating` | boolean | yes |  |
| `panels[].focused` | boolean | yes |  |

### `settings.list`

Read. Since version 1.

Settings with their type, scope, default and current value.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `prefix` | string |  | Only ids starting with it, e.g. grid. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `settings` | array of objects | yes |  |
| `settings[].setting` | string | yes |  |
| `settings[].type` | one of `boolean`, `integer`, `string` or `stringList` | yes |  |
| `settings[].scope` | one of `profile`, `collections` or `workspace` | yes |  |
| `settings[].default` | [SettingValue](#settingvalue) | yes |  |
| `settings[].value` | [SettingValue](#settingvalue) | yes |  |

### `settings.get`

Read. Since version 1.

The current values of the settings named.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `settings` | array of string | yes | At least 1 items. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `values` | object of [SettingValue](#settingvalue) by name | yes |  |

### `commands.run`

Write. Since version 1.

Runs an enabled command as its menu item, toolbar button or shortcut would.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `command` | string | yes | A command id from commands.list. Not empty. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `command` | string | yes |  |
| `ran` | `true` | yes |  |

### `panels.open`

Write. Since version 1.

Opens a panel where the layout keeps it.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes | A panel id from panels.list. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes |  |
| `title` | string | yes |  |
| `open` | boolean | yes |  |
| `floating` | boolean | yes |  |
| `focused` | boolean | yes |  |

### `panels.close`

Write. Since version 1.

Closes a panel.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes | A panel id from panels.list. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes |  |
| `title` | string | yes |  |
| `open` | boolean | yes |  |
| `floating` | boolean | yes |  |
| `focused` | boolean | yes |  |

### `panels.focus`

Write. Since version 1.

Opens a panel if needed and gives it the keyboard focus.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes | A panel id from panels.list. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes |  |
| `title` | string | yes |  |
| `open` | boolean | yes |  |
| `floating` | boolean | yes |  |
| `focused` | boolean | yes |  |

### `settings.set`

Write. Since version 1.

Changes a setting; the value must have the setting's type.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `setting` | string | yes |  |
| `value` | [SettingValue](#settingvalue) | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `setting` | string | yes |  |
| `value` | [SettingValue](#settingvalue) | yes |  |

### `document.apply`

Write. Since version 1.

Applies edits prepared against a revision as one undo step; a stale revision changes nothing.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `revision` | [Revision](#revision) | yes | The revision the edits were prepared against. |
| `name` | string | yes | A few words naming the change, as the user reads it in the review and in History after the caller's name. 1 to 200 characters. |
| `edits` | array of [Edit](#edit) | yes | Applied in order; any refusal leaves the Document as it was. 1 to 100000 items. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `inserted` | array of [LineId](#lineid) | yes | New Lines, in edit order. |
| `changed` | boolean | yes | False when the edits changed nothing: no undo step was added. |

### `selection.set`

Write. Since version 1.

Selects Lines and makes one active, as a click in the Grid would; never an undo step.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `selected` | array of [LineId](#lineid) | yes |  |
| `active` | [LineId](#lineid) or null |  | The first selected Line when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `active` | [LineId](#lineid) or null | yes |  |
| `selected` | array of [LineId](#lineid) | yes |  |

### `document.group_begin`

Write. Since version 1.

Groups the steps the caller commits on the editing target from now on, by any write, into one undo step named after the caller and `name`. The group ends at document.group_end, when the plugin's command or event handler returns, when the plugin stops, or at the first step someone else makes. Plugins only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string | yes | A few words naming the grouped step, as the user reads it in History after the caller's name. 1 to 200 characters. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |

### `document.group_end`

Write. Since version 1.

Ends the caller's group: its steps become one undo step.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `steps` | integer | yes | How many steps became one; 0 when the group made none. A group someone else's step ended earlier is no longer open. At least 0. |

### `video.info`

Read. Since version 1.

Whether a video is open, its size and aspect ratio, the frame shown and the keyframes.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `open` | boolean | yes |  |
| `width` | integer |  | At least 1. |
| `height` | integer |  | At least 1. |
| `aspect` | array of integer |  | 2 to 2 items. |
| `frameShown` | integer |  | At least 0. |
| `keyframes` | array of integer |  |  |

### `video.frame_at_time`

Read. Since version 1.

The video frame shown at a time.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `time` | integer | yes | Milliseconds. At least 0. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `frame` | integer | yes | At least 0. |

### `video.time_of_frame`

Read. Since version 1.

The time a video frame starts, in milliseconds.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `frame` | integer | yes | At least 0. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `time` | integer | yes |  |

### `video.frame`

Read. Since version 1.

One video frame as BGRA pixels in base64, with every Line, one Line or none drawn on it, optionally cropped, scaled to fit maxEdge and maxPixels.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `frame` | integer |  | Default: the frame shown, or with line the Line's first frame when it is not on the frame shown. At least 0. |
| `subtitles` | boolean |  | Render the subtitles on it. Default true. |
| `document` | [DocumentId](#documentid) |  | line's Document. Defaults to the active Document. |
| `line` | [LineId](#lineid) |  | Draw this Line alone. |
| `crop` | object |  |  |
| `crop.x` | integer | yes | At least 0. |
| `crop.y` | integer | yes | At least 0. |
| `crop.width` | integer | yes | At least 1. |
| `crop.height` | integer | yes | At least 1. |
| `maxEdge` | integer |  | Default 1024. From 16 to 4096. |
| `maxPixels` | integer |  | The most pixels the image may have. No limit when omitted. From 256 to 16777216. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `frame` | integer | yes | At least 0. |
| `width` | integer | yes | At least 1. |
| `height` | integer | yes | At least 1. |
| `crop` | object | yes |  |
| `crop.x` | integer | yes | At least 0. |
| `crop.y` | integer | yes | At least 0. |
| `crop.width` | integer | yes | At least 1. |
| `crop.height` | integer | yes | At least 1. |
| `format` | `"bgra"` | yes |  |
| `data` | string | yes | width * 4 bytes per row, top row first, base64. |

### `video.ink_bounds`

Read. Since version 1.

Where one Line's ink, or every Line's, lies on a video frame, in video pixels.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `frame` | integer |  | Default: the frame shown, or with line the Line's first frame when it is not on the frame shown. At least 0. |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) |  | This Line alone. Every Line when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `frame` | integer | yes | At least 0. |
| `bounds` | object or null | yes | Null when nothing is drawn. |

### `video.colours`

Read. Since version 1.

The colours of a video frame at points, as ASS &HBBGGRR&, each the mean of the square radius pixels around it.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `frame` | integer |  | Default: the frame shown. At least 0. |
| `points` | array of objects | yes | 1 to 256 items. |
| `points[].x` | integer | yes | At least 0. |
| `points[].y` | integer | yes | At least 0. |
| `radius` | integer |  | Default 0, the pixel alone. From 0 to 32. |
| `subtitles` | boolean |  | Sample with the subtitles drawn. Default false. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `frame` | integer | yes | At least 0. |
| `colours` | array of objects | yes |  |
| `colours[].x` | integer | yes |  |
| `colours[].y` | integer | yes |  |
| `colours[].colour` | string | yes | `pattern`: `"^&H[0-9A-F]{6}&$"`. |

### `video.keyframes`

Read. Since version 1.

The keyframes from one time to another, each with its start time.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `from` | integer |  | Milliseconds. Default 0. At least 0. |
| `to` | integer |  | Milliseconds. Default: the end. At least 0. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `keyframes` | array of objects | yes | At most 500 items. |
| `keyframes[].frame` | integer | yes | At least 0. |
| `keyframes[].time` | integer | yes |  |
| `total` | integer | yes | Every keyframe in the range; the first 500 are listed. At least 0. |

### `video.frames`

Read. Since version 1.

Video frames from one to another, every stride-th, scaled to fit maxEdge, as luma or BGRA in base64. At most 256 frames and 16 MiB of pixels come in one call.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `from` | integer | yes | At least 0. |
| `to` | integer | yes | At least 0. |
| `stride` | integer |  | Default 1, every frame. At least 1. |
| `maxEdge` | integer |  | Default 64. From 16 to 512. |
| `format` | one of `luma` or `bgra` |  | Default luma, one byte per pixel. |
| `subtitles` | boolean |  | Render the subtitles on them. Default false. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `width` | integer | yes | At least 1. |
| `height` | integer | yes | At least 1. |
| `format` | one of `luma` or `bgra` | yes |  |
| `frames` | array of objects | yes |  |
| `frames[].frame` | integer | yes | At least 0. |
| `frames[].data` | string | yes | width bytes per row for luma, width * 4 for bgra; top row first; base64. |

### `video.set_keyframes`

Write. Since version 1.

Replaces the video's keyframes as Open keyframes does. With file, writes them there as an Aegisub keyframes file first and opens it. Not an undo step.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `frames` | array of integer | yes | At most 100000 items. |
| `file` | string |  | A writable file handle, from files.pick or files.open. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `count` | integer | yes | At least 0. |
| `file` | string |  |  |

### `audio.info`

Read. Since version 1.

Whether audio is open and loaded, its duration and sample rate.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `open` | boolean | yes |  |
| `ready` | boolean |  |  |
| `duration` | integer |  | At least 0. |
| `sampleRate` | integer |  | At least 1. |
| `channels` | integer |  | At least 1. |

### `audio.peaks`

Read. Since version 1.

The lowest and highest 16-bit sample in each of `buckets` equal slices of a time range.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `start` | integer | yes | At least 0. |
| `end` | integer | yes | At least 1. |
| `buckets` | integer |  | Default 256. From 1 to 4096. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `start` | integer | yes |  |
| `end` | integer | yes |  |
| `min` | array of integer | yes |  |
| `max` | array of integer | yes |  |

### `files.list`

Read. Since version 1.

The files and folders the user granted this caller; with folder, also what that folder holds.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `folder` | string |  | A folder handle to list. |
| `path` | string |  | A folder inside it; default the folder itself. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `files` | array of [GrantedFile](#grantedfile) | yes |  |
| `folders` | array of [GrantedFolder](#grantedfolder) |  |  |
| `entries` | array of [FolderEntry](#folderentry) |  | With folder: in name order, at most 4096, without links that lead outside it. |
| `truncated` | boolean |  | Present when the folder holds more than 4096 entries. |

### `files.read`

Read. Since version 1.

A granted file's content, as UTF-8 text or base64; at most 16 MiB.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `file` | string |  | A handle from files.list. |
| `folder` | string |  | Instead of file, a folder handle from files.list. |
| `path` | string |  | With folder, the file's path inside it, with / between its parts. |
| `encoding` | one of `utf8` or `base64` |  | Default utf8. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes |  |
| `size` | integer | yes | At least 0. |
| `encoding` | one of `utf8` or `base64` | yes |  |
| `data` | string | yes |  |

### `files.write`

Write. Since version 1.

Replaces a file granted for writing with new content; in a writable folder, makes it and its folders. An AI caller's write is staged for the user to review and reaches the disk only when kept.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `file` | string |  | A handle from files.list. |
| `folder` | string |  | Instead of file, a folder handle from files.list. |
| `path` | string |  | With folder, the file's path inside it, with / between its parts. |
| `encoding` | one of `utf8` or `base64` |  | Default utf8. |
| `data` | string | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes |  |
| `size` | integer | yes | At least 0. |
| `staged` | boolean |  | True: waiting in the review, not on the disk yet. |

### `skills.list`

Read. Since version 1.

The caller's skills: each skill folder's name, whether it is the Project's or the user's, and its files, SKILL.md among them.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `skills` | array of objects | yes |  |
| `skills[].skill` | [SkillName](#skillname) | yes |  |
| `skills[].origin` | one of `project` or `yours` | yes |  |
| `skills[].files` | array of string | yes |  |

### `skills.read`

Read. Since version 1.

One file of a skill skills.list gives, as UTF-8 text; at most 256 KiB.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `skill` | [SkillName](#skillname) | yes |  |
| `file` | string | yes | A path skills.list gives, such as SKILL.md or references/tags.md. 1 to 512 characters. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `skill` | [SkillName](#skillname) | yes |  |
| `file` | string | yes |  |
| `size` | integer | yes | At least 0. |
| `data` | string | yes |  |

### `automation.macros`

Read. Since version 1.

The loaded Automation macros, as the Automation menu lists them.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `query` | string |  | Keep macros whose name, script or file name holds this, ignoring case. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `macros` | array of objects | yes |  |
| `macros[].script` | string | yes | The script's name. |
| `macros[].file` | string | yes | The script's file name. |
| `macros[].macro` | string | yes |  |
| `macros[].ordinal` | integer | yes | The macro's place among the script's, from 0. At least 0. |
| `macros[].description` | string | yes |  |
| `macros[].enabled` | boolean | yes | The script is loaded and shown in the user's window. |
| `macros[].disabledReason` | string |  |  |

### `automation.run_macro`

Write. Since version 1.

Runs a loaded Automation macro on the active tab's selection, as the menu would, as one undo step. Answers fill its dialogs. Plugins only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes | The script's file name. Not empty. |
| `macro` | string | yes | Not empty. |
| `answers` | array of [DialogAnswer](#dialoganswer) |  | One per dialog, in order, as macro.ran gives them. At most 64 items. |
| `show` | one of `never` or `prefilled` |  | never (the default) answers a dialog whose key matches without showing it; prefilled shows it filled in. A dialog without a matching answer, and a question, is always shown. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `outcome` | one of `applied`, `refused`, `failed`, `cancelled`, `declined`, `stopped` or `lost` | yes |  |
| `message` | string | yes |  |
| `log` | string | yes |  |
| `dialogs` | array of [DialogAnswer](#dialoganswer) |  | The answers its dialogs got, as macro.ran gives them; only when the caller sees the Document it ran on. |

### `automation.run`

Write. Since version 1.

Saves a Lua Automation script to the AI scripts folder and runs one of its macros on the active tab's selection, once the user approves the script. The run is one undo step.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string | yes | The script's file name without .lua; an earlier script of that name is replaced. At most 64 characters. `pattern`: `"^[A-Za-z0-9][A-Za-z0-9 _.-]*$"`. |
| `script` | string | yes | The whole Lua script, registering its macro with aegisub.register_macro. 1 to 262144 characters. |
| `macro` | string |  | The macro to run, by its registered name; default the first. Not empty. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes | The saved script's file name. |
| `outcome` | one of `applied`, `refused`, `failed`, `cancelled`, `declined`, `stopped`, `lost` or `invalid` | yes |  |
| `message` | string | yes |  |
| `log` | string | yes |  |

### `translation.project`

Read. Since version 1.

The Document's Honya project files: PROJECT.md, GLOSSARY.md, CHARACTERS.md and STYLE.md, those that exist. folder is null when the Document has no project folder.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `folder` | string or null | yes |  |
| `files` | array of objects | yes |  |
| `files[].name` | one of `PROJECT.md`, `GLOSSARY.md`, `CHARACTERS.md` or `STYLE.md` | yes |  |
| `files[].data` | string | yes |  |

### `translation.project_write`

Write. Since version 1.

Replaces one of the Document's Honya project files, keeping Honya's Markdown and honya:data format.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `file` | one of `PROJECT.md`, `GLOSSARY.md`, `CHARACTERS.md` or `STYLE.md` | yes |  |
| `data` | string | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `file` | one of `PROJECT.md`, `GLOSSARY.md`, `CHARACTERS.md` or `STYLE.md` | yes |  |
| `size` | integer | yes | At least 0. |

### `nodes.catalogue`

Read. Since version 1.

The node types the node-effect editor offers, with their ports and parameters.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `category` | string |  |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `nodes` | array of objects | yes |  |
| `nodes[].id` | string | yes |  |
| `nodes[].category` | string | yes |  |
| `nodes[].summary` | string | yes |  |
| `nodes[].inputs` | array of [NodePort](#nodeport) | yes |  |
| `nodes[].outputs` | array of [NodePort](#nodeport) | yes |  |
| `nodes[].params` | object | yes |  |
| `nodes[].since` | integer | yes | At least 1. |

### `nodes.validate`

Read. Since version 1.

Checks a node-effect graph; each problem names its node and port or field.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `graph` | object | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `valid` | boolean | yes |  |
| `problems` | array of objects | yes |  |
| `problems[].node` | string |  |  |
| `problems[].port` | string |  |  |
| `problems[].message` | string | yes |  |

### `effects.graphs`

Read. Since version 1.

The Document's node-effect graphs, one per graph Line.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `graphs` | array of objects | yes |  |
| `graphs[].line` | [LineId](#lineid) | yes |  |
| `graphs[].name` | string | yes |  |
| `graphs[].style` | string | yes | The Style whose karaoke Lines it runs on. |
| `graphs[].valid` | boolean | yes |  |

### `effects.graph`

Read. Since version 1.

One graph Line's graph, with what keeps it from running.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `name` | string | yes |  |
| `style` | string | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `graph` | object or null | yes | Null when the Line's text is no graph. |
| `valid` | boolean | yes |  |
| `problems` | array of objects | yes |  |
| `problems[].node` | string |  |  |
| `problems[].port` | string |  |  |
| `problems[].message` | string | yes |  |

### `effects.edit`

Read. Since version 1.

Edits a graph in steps (add, connect, disconnect, remove, set) and gives the new graph with its problems; nothing in the Document changes.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `graph` | object | yes | A graph as effects.graph gives it; {"format": 1, "nodes": [], "wires": []} starts an empty one. |
| `steps` | array of objects | yes | Applied in order; a refused step changes nothing. 1 to 500 items. |
| `steps[].op` | one of `add`, `connect`, `disconnect`, `remove` or `set` | yes |  |
| `steps[].type` | string |  | add: a node type from nodes.catalogue. |
| `steps[].node` | string |  | add: the new node's id, made from its type when omitted; remove, set: the node. |
| `steps[].fields` | object |  | add: the node's fields. |
| `steps[].from` | string |  | connect: an output, "node.port". |
| `steps[].to` | string |  | connect, disconnect: an input, "node.port"; connect replaces a wire already there. |
| `steps[].field` | string |  | set: the field. |
| `steps[].value` | any |  | set: the field's value; null restores its default. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `graph` | object | yes |  |
| `added` | array of string | yes | The ids of the nodes added, in step order. |
| `valid` | boolean | yes |  |
| `problems` | array of objects | yes |  |
| `problems[].node` | string |  |  |
| `problems[].port` | string |  |  |
| `problems[].message` | string | yes |  |

### `effects.preview`

Read. Since version 1.

Runs a graph on the Document's karaoke Lines and gives the Lines it would write; nothing is applied. Lua nodes don't run.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) |  | A graph Line: its graph, name and Style. |
| `graph` | object |  | A graph to run in place of the Line's. |
| `style` | string |  | The Style whose karaoke Lines it runs on; needed without line. |
| `name` | string |  | The graph's name, which the Lines' Effect "fx <name>" carries. |
| `sources` | array of [LineId](#lineid) |  | Only these karaoke Lines; all of them when omitted. 1 to 100000 items. |
| `limit` | integer |  | How many generated Lines to give; default 20. From 0 to 500. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `ran` | boolean | yes | False when a problem stopped the run; applying it would change nothing. |
| `complete` | boolean | yes |  |
| `sources` | integer | yes | How many karaoke Lines ran. |
| `generated` | integer | yes | How many Lines the run made. |
| `lines` | array of string | yes | The first generated Lines, as ASS Dialogue lines. |
| `problems` | array of objects | yes |  |
| `problems[].node` | string |  |  |
| `problems[].line` | [LineId](#lineid) |  |  |
| `problems[].message` | string | yes |  |

### `effects.save`

Write. Since version 1.

Writes a graph to the Document as one undo step: into a new graph Line, or in place of a graph Line's.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `graph` | object | yes |  |
| `line` | [LineId](#lineid) |  | The graph Line to replace; omit it to add a graph. |
| `name` | string |  | A new graph's name. 1 to 200 characters. |
| `style` | string |  | A new graph's Style: whose karaoke Lines it runs on. Not empty. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `graph` | object | yes |  |
| `valid` | boolean | yes |  |
| `problems` | array of objects | yes |  |
| `problems[].node` | string |  |  |
| `problems[].port` | string |  |  |
| `problems[].message` | string | yes |  |

### `effects.apply`

Write. Since version 1.

Runs a graph Line and writes its Lines as one undo step, replacing those it wrote before; the karaoke Lines it ran on become Comments. Lua nodes don't run, so a graph with one applies from the node-effect editor.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes | The graph Line. |
| `sources` | array of [LineId](#lineid) |  | Only these karaoke Lines; all of them when omitted. 1 to 100000 items. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `generated` | integer | yes |  |
| `sources` | integer | yes | How many karaoke Lines got Lines. |
| `changed` | boolean | yes |  |

### `plugins.list`

Read. Since version 1.

The installed plugins with their state, trust tier and commands.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `plugins` | array of objects | yes |  |
| `plugins[].id` | string | yes |  |
| `plugins[].name` | string | yes |  |
| `plugins[].version` | string | yes |  |
| `plugins[].description` | string | yes |  |
| `plugins[].source` | one of `file` or `marketplace` | yes |  |
| `plugins[].signed` | boolean | yes |  |
| `plugins[].enabled` | boolean | yes |  |
| `plugins[].tier` | one of `restricted` or `unrestricted` | yes |  |
| `plugins[].revoked` | boolean | yes |  |
| `plugins[].commands` | array of objects | yes | While the plugin runs. |
| `plugins[].commands[].command` | string | yes | plugin.<plugin id>.<command id> |
| `plugins[].commands[].title` | string | yes |  |

### `plugins.manifest`

Read. Since version 1.

An installed plugin's hikari-plugin.json.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes |  |
| `manifest` | object | yes |  |

### `plugins.scaffold`

Write. Since version 1.

Writes a plugin from the authoring guide's template into the AI plugins folder and packs it; nothing is installed.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes | Reverse-DNS, lowercase: com.example.sign-counter. An earlier scaffold of that id is replaced. At most 100 characters. `pattern`: `"^[a-z0-9]+(-[a-z0-9]+)*(\\.[a-z0-9]+(-[a-z0-9]+)*)+$"`. |
| `name` | string | yes | 1 to 80 characters. |
| `version` | string |  | Semantic version; default 0.1.0. At most 64 characters. |
| `description` | string |  | At most 1000 characters. |
| `tier` | one of `restricted` or `unrestricted` |  | restricted (default) runs in the sandbox without ffi, io, os and debug; unrestricted needs the user's full trust at install. |
| `permissions` | object |  | The manifest's permissions: documents.all, network {hosts, reason}, files, vcs.read. |
| `commands` | array of objects | yes | 1 to 256 items. |
| `commands[].id` | string | yes | `pattern`: `"^[a-z0-9]+(-[a-z0-9]+)*$"`. |
| `commands[].title` | string | yes | 1 to 80 characters. |
| `code` | string |  | main.lua, registering each command with hikari.commands.register; the guide's first plugin when omitted. At most 1048576 characters. |
| `files` | array of objects |  | More files beside main.lua and hikari-plugin.json. At most 64 items. |
| `files[].path` | string | yes | At most 200 characters. `pattern`: `"^([A-Za-z0-9_][A-Za-z0-9_.-]*/)*[A-Za-z0-9_][A-Za-z0-9_.-]*\\.(lua\|json\|md\|txt)$"`. |
| `files[].text` | string | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes |  |
| `folder` | string | yes |  |
| `package` | string | yes | The packed .hikariplugin. |
| `files` | array of string | yes |  |
| `manifest` | object | yes |  |

### `plugins.review`

Read. Since version 1.

What installing a package would do, as the install question shows it: its trust tier, signing, permissions, commands and files.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `plugin` | string |  | A plugin plugins.scaffold made. |
| `file` | string |  | A .hikariplugin the user granted, by its files.list handle. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes |  |
| `name` | string | yes |  |
| `version` | string | yes |  |
| `change` | one of `install`, `update`, `reinstall` or `downgrade` | yes |  |
| `installedVersion` | string | yes |  |
| `tier` | one of `restricted` or `unrestricted` | yes | unrestricted runs outside the sandbox and needs the user's full trust. |
| `needsRetrust` | boolean | yes | The signing key changed, or an unsigned package replaces a signed one. |
| `keyId` | string | yes | The signing key; empty when unsigned. |
| `pinnedKeyId` | string | yes |  |
| `permissions` | array of string | yes |  |
| `commands` | array of string | yes | The titles of the commands it adds. |
| `files` | array of objects | yes |  |
| `files[].path` | string | yes |  |
| `files[].text` | string | yes |  |
| `digest` | string | yes |  |

### `plugins.install`

Write. Since version 1.

Installs a scaffolded or granted plugin package and turns it on, once the user approves it in HikariSub; it asks every time.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `plugin` | string |  | A plugin plugins.scaffold made. |
| `file` | string |  | A .hikariplugin the user granted, by its files.list handle. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes |  |
| `name` | string | yes |  |
| `version` | string | yes |  |
| `change` | one of `install`, `update`, `reinstall` or `downgrade` | yes |  |
| `installedVersion` | string | yes |  |
| `tier` | one of `restricted` or `unrestricted` | yes | unrestricted runs outside the sandbox and needs the user's full trust. |
| `needsRetrust` | boolean | yes | The signing key changed, or an unsigned package replaces a signed one. |
| `keyId` | string | yes | The signing key; empty when unsigned. |
| `pinnedKeyId` | string | yes |  |
| `permissions` | array of string | yes |  |
| `commands` | array of string | yes | The titles of the commands it adds. |
| `files` | array of objects | yes |  |
| `files[].path` | string | yes |  |
| `files[].text` | string | yes |  |
| `digest` | string | yes |  |
| `installed` | `true` | yes |  |

### `plugins.run`

Write. Since version 1.

Runs an installed plugin's command as its menu item would.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `plugin` | string | yes |  |
| `command` | string | yes | The command's id within the plugin, as in its manifest. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `ran` | `true` | yes |  |

### `typeset.perspective`

Read. Since version 1.

The Line's text with the tags that render it on a quad (VT3's perspective solver): \pos, \org, \frx, \fry, \frz, \fax, \fscx and \fscy.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |
| `quad` | array of objects | yes | Script coordinates: top left, top right, bottom right, bottom left. 4 to 4 items. |
| `quad[].x` | number | yes |  |
| `quad[].y` | number | yes |  |
| `origin` | one of `center`, `nofax` or `keep` |  | Where \org goes: the quad's centre (default), the centre without \fax, or where the Line has it. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `text` | string | yes |  |
| `transform` | object | yes |  |

### `typeset.clip`

Read. Since version 1.

The Line's text with its first clip replaced by a rectangle or a vector clip.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |
| `rect` | object |  | Script coordinates, rounded to whole units. |
| `rect.x1` | number | yes |  |
| `rect.y1` | number | yes |  |
| `rect.x2` | number | yes |  |
| `rect.y2` | number | yes |  |
| `vector` | string |  | ASS drawing commands such as "m 0 0 l 100 0 100 50 0 50"; empty removes the vector clip. At most 100000 characters. |
| `inverse` | boolean |  | \iclip: draw outside the clip. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `text` | string | yes | The Line's text with the tags written; nothing is applied. |

### `typeset.drawing`

Read. Since version 1.

The Line's text with its first drawing replaced by a shape (\p1), placed at a position with an alignment.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |
| `shape` | string | yes | ASS drawing commands, in script units from the drawing's origin. 1 to 100000 characters. |
| `position` | object |  |  |
| `position.x` | number | yes |  |
| `position.y` | number | yes |  |
| `alignment` | integer |  | \an; 7 (top left) with position, else the Line's. From 1 to 9. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `text` | string | yes | The Line's text with the tags written; nothing is applied. |

### `typeset.shift`

Read. Since version 1.

Moves the Lines' positions, \move points, clips, drawings and \org by an offset, as the Position shifter does.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `lines` | array of [LineId](#lineid) | yes | 1 to 1000 items. |
| `dx` | number | yes | Script units. |
| `dy` | number | yes |  |
| `kinds` | array of one of `position`, `move_start`, `move_end`, `clip`, `drawing` or `origin` |  | What moves; every kind but drawing when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `lines` | array of objects | yes | The Lines the change alters, with their new text; nothing is applied. |
| `lines[].line` | [LineId](#lineid) | yes |  |
| `lines[].text` | string | yes |  |

### `typeset.hydra`

Read. Since version 1.

Changes one tag across the Lines as the Hydra strip does: set it, add to it, multiply it by line number, or run a gradient across characters or Lines.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `lines` | array of [LineId](#lineid) | yes | Multiply and the line gradients count them in this order. 1 to 1000 items. |
| `tag` | string | yes | A Hydra tag without the backslash: blur, bord, be, alpha, 1a, 3a, 4a, 1c (or c), 3c, 4c, fad, fax, fay, fs, fsp, shad, xbord, ybord, xshad, yshad, pos or t. Not empty. |
| `change` | one of `set`, `add`, `multiply` or `gradient` | yes |  |
| `values` | array of number | yes | set: the value; add: what to add; multiply: the start; gradient: the first value. Colours are R, G, B. 1 to 4 items. |
| `perLine` | array of number |  | multiply: added for each Line. At most 4 items. |
| `byLine` | one of `set` or `add` |  | multiply: set each Line's value, or add to it. Default set. |
| `to` | array of number |  | gradient: the last value. At most 4 items. |
| `across` | one of `characters` or `lines` |  | gradient: default characters. |
| `decreasing` | boolean |  | gradient: run from the end. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `lines` | array of objects | yes | The Lines the change alters, with their new text; nothing is applied. |
| `lines[].line` | [LineId](#lineid) | yes |  |
| `lines[].text` | string | yes |  |

### `typeset.check`

Read. Since version 1.

Renders one Line alone, with a proposed text, on its frame and compares its ink with a target.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |
| `text` | string |  | The proposed text; the Line's own when omitted. |
| `target` | object or object | yes | Where the Line's ink should land, in script coordinates: a quad or a box. |
| `frame` | integer |  | Default: the frame shown when the Line is on it, else the Line's first. At least 0. |
| `tolerance` | number |  | Video pixels; default 4. At least 0. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `frame` | integer | yes |  |
| `pass` | boolean | yes |  |
| `error` | number or null | yes | The largest distance between an edge of the ink's bounds and the target's, in video pixels; null when nothing is drawn. |
| `ink` | object or null | yes |  |
| `target` | object | yes |  |
| `target.x` | number | yes |  |
| `target.y` | number | yes |  |
| `target.width` | number | yes |  |
| `target.height` | number | yes |  |

### `typeset.apply`

Write. Since version 1.

Applies typeset Lines as one undo step after every one passes the render check; when one misses its target nothing is applied.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `revision` | [Revision](#revision) | yes |  |
| `name` | string | yes | A few words naming the change, such as "Typeset the shop sign". 1 to 200 characters. |
| `lines` | array of objects | yes | 1 to 100 items. |
| `lines[].line` | [LineId](#lineid) | yes |  |
| `lines[].text` | string | yes |  |
| `lines[].target` | object or object | yes | Where the Line's ink should land, in script coordinates: a quad or a box. |
| `lines[].frame` | integer |  | At least 0. |
| `tolerance` | number |  | Video pixels; default 4. At least 0. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `inserted` | array of [LineId](#lineid) | yes |  |
| `changed` | boolean | yes |  |
| `checks` | array of objects | yes |  |
| `checks[].line` | [LineId](#lineid) | yes |  |
| `checks[].frame` | integer | yes |  |
| `checks[].pass` | boolean | yes |  |
| `checks[].error` | number or null | yes | The largest distance between an edge of the ink's bounds and the target's, in video pixels; null when nothing is drawn. |
| `checks[].ink` | object or null | yes |  |
| `checks[].target` | object | yes |  |
| `checks[].target.x` | number | yes |  |
| `checks[].target.y` | number | yes |  |
| `checks[].target.width` | number | yes |  |
| `checks[].target.height` | number | yes |  |

### `project.info`

Read. Since version 1.

The Project of the window the user works in, or null.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `project` | [Project](#project) | yes |  |

### `vcs.refs`

Read. Since version 1.

The branches, tags and recent commits of the git repository holding the Document, or the Project's. Plugins whose manifest asks for vcs.read only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document; an unsaved one gives the Project's repository. |
| `limit` | integer |  | How many recent commits; 50 when left out. From 1 to 200. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `root` | string | yes | The repository's work tree. |
| `path` | string or null | yes | The Document's path in the repository, '/'-separated; null for the Project's. |
| `head` | null or object | yes | null before the first commit. |
| `branches` | array of objects | yes | At most 500, newest first. |
| `branches[].name` | string | yes |  |
| `branches[].commit` | string | yes |  |
| `branches[].current` | boolean | yes |  |
| `tags` | array of objects | yes | At most 500, newest first. |
| `tags[].name` | string | yes |  |
| `tags[].commit` | string | yes |  |
| `commits` | array of objects | yes | The newest from HEAD. |
| `commits[].commit` | string | yes |  |
| `commits[].summary` | string | yes |  |
| `commits[].author` | string | yes |  |
| `commits[].time` | integer | yes | Seconds since 1970, when it was committed. |

### `vcs.show`

Read. Since version 1.

The Document's file as a git branch, tag or commit has it. Plugins whose manifest asks for vcs.read only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `ref` | string | yes | A branch, tag, commit or revision such as HEAD~2. 1 to 256 characters. `pattern`: `"^[A-Za-z0-9._/@{}~^][A-Za-z0-9._/@{}~^-]*$"`. |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `ref` | string | yes |  |
| `commit` | string | yes |  |
| `path` | string | yes | The Document's path in the repository, '/'-separated. |
| `text` | string | yes | The file at that commit, as UTF-8. |

### `karaoke.syllables`

Read. Since version 1.

A Line's karaoke syllables as the templater reads them: text, tag, start and end in ms.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `syllables` | array of objects | yes |  |
| `syllables[].text` | string | yes |  |
| `syllables[].stripped` | string | yes |  |
| `syllables[].tag` | string | yes |  |
| `syllables[].start` | integer | yes |  |
| `syllables[].end` | integer | yes |  |

### `karaoke.split`

Read. Since version 1.

Splits Lines without \k tags into syllables as the audio editor's karaoke mode does, the time shared by character; Lines that already have \k tags keep them.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `lines` | array of [LineId](#lineid) | yes | 1 to 1000 items. |
| `bySyllable` | boolean |  | Default true: by syllable, with the language rules the user set; false: at spaces. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `lines` | array of objects | yes | The Lines that change, with their new text; nothing is applied. |
| `lines[].line` | [LineId](#lineid) | yes |  |
| `lines[].text` | string | yes |  |

### `karaoke.time`

Read. Since version 1.

Times a Line's existing syllables: each syllable's end in ms, in order; the \k tags are rewritten.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |
| `ends` | array of integer | yes | One per syllable, ascending, in ms from the video's start. 1 to 4096 items. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `text` | string | yes | The Line's text with the \k tags written; nothing is applied. |

### `karaoke.onsets`

Read. Since version 1.

Where sung notes start in the open audio, in ms, within a range.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `start` | integer | yes | At least 0. |
| `end` | integer | yes | At least 0. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `onsets` | array of integer | yes |  |

### `karaoke.snap`

Read. Since version 1.

Moves each syllable boundary inside the Lines to the nearest vocal onset within the window, keeping every syllable at least 10 ms long.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `lines` | array of [LineId](#lineid) | yes | 1 to 1000 items. |
| `window` | integer |  | Ms either side; default 100. From 1 to 1000. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `lines` | array of objects | yes | The Lines that change, with their new text; nothing is applied. |
| `lines[].line` | [LineId](#lineid) | yes |  |
| `lines[].text` | string | yes |  |
| `lines[].moved` | integer |  | Boundaries moved. |

### `karaoke.kanji`

Read. Since version 1.

Copies a timed romaji Line's syllables onto its kanji Line as the Kanji timer suggests the groups.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `romaji` | [LineId](#lineid) | yes |  |
| `kanji` | [LineId](#lineid) | yes |  |
| `multi` | boolean |  | Further syllables of a group become # syllables instead of joining the first. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `text` | string | yes |  |
| `clean` | boolean | yes | False when a group came out blank or the characters ran out: check it. |

### `karaoke.furigana`

Read. Since version 1.

The readings the dictionary suggests for a Line's kanji runs, in hiragana.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `runs` | array of objects | yes |  |
| `runs[].base` | string | yes |  |
| `runs[].reading` | string | yes |  |

### `karaoke.check`

Read. Since version 1.

The Karaoke check's problems: unclosed tags, totals that differ from the Line, empty syllables and syllables outside the Line.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `problems` | array of objects | yes |  |
| `problems[].line` | [LineId](#lineid) | yes |  |
| `problems[].kind` | one of `unclosed`, `not_for_audio_editor`, `editor_only`, `total_not_duration`, `zero_length` or `outside_line` | yes |  |
| `problems[].totalMs` | integer |  |  |
| `problems[].durationMs` | integer |  |  |
| `problems[].syllables` | array of integer |  | Which ones, from 1. |
| `unlisted` | integer | yes | Problems past the listed ones. |

### `karaoke.template_preview`

Read. Since version 1.

The Lines the karaoke templater generates from one karaoke Line, without applying them.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `line` | [LineId](#lineid) | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `line` | [LineId](#lineid) | yes |  |
| `lines` | array of objects | yes |  |
| `lines[].layer` | integer | yes |  |
| `lines[].start` | integer | yes |  |
| `lines[].end` | integer | yes |  |
| `lines[].style` | string | yes |  |
| `lines[].effect` | string | yes |  |
| `lines[].text` | string | yes |  |

### `karaoke.apply_template`

Write. Since version 1.

Runs the karaoke templater on the karaoke Lines, or the Lines given, and applies what it generates as one undo step: old generated Lines are replaced.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `revision` | [Revision](#revision) | yes |  |
| `lines` | array of [LineId](#lineid) |  | Default: every karaoke Line. 1 to 100000 items. |
| `name` | string |  | Default "Apply karaoke template". 1 to 200 characters. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `revision` | [Revision](#revision) | yes |  |
| `changed` | boolean | yes |  |

### `karaoke.align`

Write. Since version 1.

Runs the user's own WhisperX or Montreal Forced Aligner on the audio under the Lines and answers each word with its start and end in ms; the user approves every run, and nothing is applied.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `lines` | array of [LineId](#lineid) | yes | 1 to 1000 items. |
| `program` | one of `whisperx` or `mfa` | yes |  |
| `language` | string |  | WhisperX: a language code such as ja; default: it detects. At most 32 characters. |
| `dictionary` | string |  | MFA: the pronunciation dictionary, such as english_mfa. At most 200 characters. |
| `acousticModel` | string |  | MFA: the acoustic model, such as english_mfa. At most 200 characters. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `words` | array of objects | yes |  |
| `words[].text` | string | yes |  |
| `words[].start` | integer | yes |  |
| `words[].end` | integer | yes |  |

### `marks.set`

Write. Since version 1.

Marks Grid rows with a coloured bar and a tooltip, replacing the caller's marks on those Lines. Marks belong to the plugin, are never saved and go when it stops. Not an undo step. Plugins only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `marks` | array of objects | yes | 1 to 20000 items. |
| `marks[].line` | [LineId](#lineid) | yes |  |
| `marks[].colour` | [MarkColour](#markcolour) | yes |  |
| `marks[].tooltip` | string | yes | What the mark means; shown on hover after the plugin's name and read by screen readers. 1 to 200 characters. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `marked` | integer | yes | At least 0. |

### `marks.clear`

Write. Since version 1.

Removes the caller's Grid row marks. Plugins only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |
| `lines` | array of [LineId](#lineid) |  | These Lines only; every mark of the caller when omitted. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |

### `marks.list`

Read. Since version 1.

The caller's Grid row marks on a Document. Plugins only.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) |  | Defaults to the active Document. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `marks` | array of objects | yes |  |
| `marks[].line` | [LineId](#lineid) | yes |  |
| `marks[].colour` | [MarkColour](#markcolour) | yes |  |
| `marks[].tooltip` | string | yes | What the mark means; shown on hover after the plugin's name and read by screen readers. 1 to 200 characters. |

## Events

### `project.changed`

Since version 1.

The window the user works in opened or closed a Project, or the user moved to a window with another one.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `project` | [Project](#project) | yes |  |

### `document.changed`

Since version 1.

A Document's content changed: a commit, Undo or Redo.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `step` | string | yes | The History step now current. |

### `selection.changed`

Since version 1.

The selected or the active Line changed.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `revision` | [Revision](#revision) | yes |  |
| `active` | [LineId](#lineid) or null | yes |  |
| `selected` | array of [LineId](#lineid) | yes |  |

### `documents.changed`

Since version 1.

A Document opened or closed, or another became active.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `documents` | array of [DocumentId](#documentid) | yes |  |
| `editingTarget` | [DocumentId](#documentid) or null | yes |  |

### `panel.changed`

Since version 1.

A panel opened, closed, floated, docked or took the focus.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `panel` | string | yes |  |
| `title` | string | yes |  |
| `open` | boolean | yes |  |
| `floating` | boolean | yes |  |
| `focused` | boolean | yes |  |

### `command.ran`

Since version 1.

A command ran, from a menu, a key, the API or anywhere else.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `command` | string | yes |  |
| `ok` | boolean | yes | False when it was refused or failed. |

### `macro.ran`

Since version 1.

An Automation macro run ended, from the menu, a key, Repeat last run or the API, with the answers its dialogs got, in the form automation.run_macro takes.

**Data**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `document` | [DocumentId](#documentid) | yes |  |
| `file` | string | yes | The script's file name. |
| `macro` | string | yes |  |
| `outcome` | one of `applied`, `refused`, `failed`, `cancelled`, `declined`, `stopped` or `lost` | yes |  |
| `ok` | boolean | yes | True when it applied. |
| `dialogs` | array of [DialogAnswer](#dialoganswer) | yes |  |

## Types

### DocumentId

Type: integer. An open Document, as documents.list gives it. At least 1.

### SkillName

Type: string. A skill's name, which is its folder's. At most 64 characters. `pattern`: `"^[a-z0-9]+(-[a-z0-9]+)*$"`.

### LineId

Type: integer. A Line's stable id: it survives edits, moves and undo, unlike its row. At least 1.

### Revision

Type: integer. Counts the Document's committed changes; a write names the revision it was prepared against. At least 0.

### LineField

Type: one of `comment`, `layer`, `start`, `end`, `style`, `actor`, `marginL`, `marginR`, `marginV`, `effect`, `text`, `translation`, `unconfirmed` or `generatedFrom`.

### Line

Times are milliseconds. In translation mode text is the original and translation the translated text.

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `id` | [LineId](#lineid) | yes |  |
| `row` | integer | yes | 1-based position among the Document's Lines. At least 1. |
| `comment` | boolean |  |  |
| `layer` | integer |  |  |
| `start` | integer |  |  |
| `end` | integer |  |  |
| `style` | string |  |  |
| `actor` | string |  |  |
| `marginL` | integer |  |  |
| `marginR` | integer |  |  |
| `marginV` | integer |  |  |
| `effect` | string |  |  |
| `text` | string |  |  |
| `translation` | string |  |  |
| `unconfirmed` | boolean |  |  |
| `generatedFrom` | [LineId](#lineid) or null |  | The karaoke Line a templater run generated this one from. |

### Style

An ASS v4+ Style. Text fields hold the field's ASS text; colours are &HAABBGGRR.

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string | yes |  |
| `fontname` | string |  |  |
| `fontsize` | string |  |  |
| `primaryColour` | string |  |  |
| `secondaryColour` | string |  |  |
| `outlineColour` | string |  |  |
| `backColour` | string |  |  |
| `bold` | boolean |  |  |
| `italic` | boolean |  |  |
| `underline` | boolean |  |  |
| `strikeOut` | boolean |  |  |
| `scaleX` | string |  |  |
| `scaleY` | string |  |  |
| `spacing` | string |  |  |
| `angle` | string |  |  |
| `borderStyle` | one of `1`, `3` or `4` |  |  |
| `outline` | string |  |  |
| `shadow` | string |  |  |
| `alignment` | string |  |  |
| `marginL` | string |  |  |
| `marginR` | string |  |  |
| `marginV` | string |  |  |
| `encoding` | string |  |  |

### LineFields

Line fields to set; the others keep their values.

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `comment` | boolean |  |  |
| `layer` | integer |  |  |
| `start` | integer |  |  |
| `end` | integer |  |  |
| `style` | string |  |  |
| `actor` | string |  |  |
| `marginL` | integer |  |  |
| `marginR` | integer |  |  |
| `marginV` | integer |  |  |
| `effect` | string |  |  |
| `text` | string |  |  |
| `translation` | string |  |  |
| `unconfirmed` | boolean |  |  |

### StyleFields

Style fields to set; name renames it. A new Style starts from Arial 48.

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string |  |  |
| `fontname` | string |  |  |
| `fontsize` | string |  |  |
| `primaryColour` | string |  |  |
| `secondaryColour` | string |  |  |
| `outlineColour` | string |  |  |
| `backColour` | string |  |  |
| `bold` | boolean |  |  |
| `italic` | boolean |  |  |
| `underline` | boolean |  |  |
| `strikeOut` | boolean |  |  |
| `scaleX` | string |  |  |
| `scaleY` | string |  |  |
| `spacing` | string |  |  |
| `angle` | string |  |  |
| `borderStyle` | one of `1`, `3` or `4` |  |  |
| `outline` | string |  |  |
| `shadow` | string |  |  |
| `alignment` | string |  |  |
| `marginL` | string |  |  |
| `marginR` | string |  |  |
| `marginV` | string |  |  |
| `encoding` | string |  |  |

### Edit

One of:

- `op` `set_line`
- `op` `insert_line`: After or before a Line; at the end without either.
- `op` `delete_line`
- `op` `move_line`: Before a Line, or to the end with before null.
- `op` `set_style`: Changes the Style with that name, or adds it.
- `op` `delete_style`
- `op` `set_info`: A null value removes the property.

*`op` `set_line`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"set_line"` | yes |  |
| `line` | [LineId](#lineid) | yes |  |
| `fields` | [LineFields](#linefields) | yes |  |

*`op` `insert_line`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"insert_line"` | yes |  |
| `after` | [LineId](#lineid) |  |  |
| `before` | [LineId](#lineid) |  |  |
| `fields` | [LineFields](#linefields) |  |  |

*`op` `delete_line`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"delete_line"` | yes |  |
| `line` | [LineId](#lineid) | yes |  |

*`op` `move_line`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"move_line"` | yes |  |
| `line` | [LineId](#lineid) | yes |  |
| `before` | [LineId](#lineid) or null | yes |  |

*`op` `set_style`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"set_style"` | yes |  |
| `name` | string | yes | Not empty. |
| `fields` | [StyleFields](#stylefields) |  |  |

*`op` `delete_style`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"delete_style"` | yes |  |
| `name` | string | yes | Not empty. |

*`op` `set_info`*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `op` | `"set_info"` | yes |  |
| `key` | string | yes | Not empty. |
| `value` | string or null | yes |  |

### SettingValue

Type: boolean, integer, string or array of string.

### NodePort

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string | yes |  |
| `type` | string | yes |  |

### GrantedFile

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes | The handle files.read and files.write take. |
| `name` | string | yes |  |
| `access` | one of `read` or `write` | yes |  |

### GrantedFolder

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `folder` | string | yes | The handle files.list, files.read and files.write take with a path. |
| `name` | string | yes |  |
| `access` | one of `read` or `write` | yes |  |

### FolderEntry

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `path` | string | yes | Relative to the granted folder, with / between its parts. |
| `folder` | boolean | yes |  |

### Project

One of:

- null
- object

*object*

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string | yes | The Project folder's name. |
| `trusted` | boolean | yes | Whether the folder is in Trusted folders. |

### DialogControl

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `name` | string | yes |  |
| `kind` | one of `edit`, `textbox`, `intedit`, `floatedit`, `dropdown`, `checkbox`, `color`, `coloralpha` or `alpha` | yes | The control's Lua class, lowercased. |
| `label` | string |  | A checkbox's text. |
| `hint` | string |  |  |
| `items` | array of string |  | A dropdown's choices. |
| `min` | number |  |  |
| `max` | number |  |  |
| `step` | number |  | A floatedit's step; 0 for none. |

### DialogAnswer

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `key` | string |  | The dialog's key: its script and its controls' kinds and names, as Automation remembers answers by. When given, the key alone decides which dialog the answer fills. `pattern`: `"^[0-9a-f]{16}$"`. |
| `controls` | array of [DialogControl](#dialogcontrol) |  | The dialog's value-bearing controls, in order. Without a key, the answer fills the dialog of the script being run whose controls have these kinds and names; the other fields don't count. At most 256 items. |
| `values` | object of string, number or boolean by name | yes | The answered values by control name. |
| `button` | string |  | The pressed button's label; empty when the dialog was closed. |

### MarkColour

Type: one of `accent`, `warning`, `danger`, `success` or `muted`. A theme role, so the mark follows the theme and high contrast.
