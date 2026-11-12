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
| `video.info` | read | 1 | Whether a video is open, its size and aspect ratio, the frame shown and the keyframes. |
| `video.frame_at_time` | read | 1 | The video frame shown at a time. |
| `video.time_of_frame` | read | 1 | The time a video frame starts, in milliseconds. |
| `video.frame` | read | 1 | One video frame as BGRA pixels in base64, with every Line, one Line or none drawn on it, optionally cropped, scaled to fit maxEdge and maxPixels. |
| `video.ink_bounds` | read | 1 | Where one Line's ink, or every Line's, lies on a video frame, in video pixels. |
| `video.colours` | read | 1 | The colours of a video frame at points, as ASS &HBBGGRR&, each the mean of the square radius pixels around it. |
| `video.keyframes` | read | 1 | The keyframes from one time to another, each with its start time. |
| `audio.info` | read | 1 | Whether audio is open and loaded, its duration and sample rate. |
| `audio.peaks` | read | 1 | The lowest and highest 16-bit sample in each of `buckets` equal slices of a time range. |
| `files.list` | read | 1 | The files and folders the user granted this caller; with folder, also what that folder holds. |
| `files.read` | read | 1 | A granted file's content, as UTF-8 text or base64; at most 16 MiB. |
| `files.write` | write | 1 | Replaces a file granted for writing with new content; in a writable folder, makes it and its folders. |
| `skills.list` | read | 1 | The user's skills: each skill folder's name and its files, SKILL.md among them. |
| `skills.read` | read | 1 | One file of a user's skill, as UTF-8 text; at most 256 KiB. |
| `automation.run` | write | 1 | Saves a Lua Automation script to the AI scripts folder and runs one of its macros on the active tab's selection, once the user approves the script. The run is one undo step. |
| `nodes.catalogue` | read | 1 | The node types the node-effect editor offers, with their ports and parameters. |
| `nodes.validate` | read | 1 | Checks a node-effect graph; each problem names its node and port or field. |
| `typeset.perspective` | read | 1 | The Line's text with the tags that render it on a quad (VT3's perspective solver): \pos, \org, \frx, \fry, \frz, \fax, \fscx and \fscy. |
| `typeset.clip` | read | 1 | The Line's text with its first clip replaced by a rectangle or a vector clip. |
| `typeset.drawing` | read | 1 | The Line's text with its first drawing replaced by a shape (\p1), placed at a position with an alignment. |
| `typeset.shift` | read | 1 | Moves the Lines' positions, \move points, clips, drawings and \org by an offset, as the Position shifter does. |
| `typeset.hydra` | read | 1 | Changes one tag across the Lines as the Hydra strip does: set it, add to it, multiply it by line number, or run a gradient across characters or Lines. |
| `typeset.check` | read | 1 | Renders one Line alone, with a proposed text, on its frame and compares its ink with a target. |
| `typeset.apply` | write | 1 | Applies typeset Lines as one undo step after every one passes the render check; when one misses its target nothing is applied. |
| `project.info` | read | 1 | The Project of the window the user works in, or null. |

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

Replaces a file granted for writing with new content; in a writable folder, makes it and its folders.

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

### `skills.list`

Read. Since version 1.

The user's skills: each skill folder's name and its files, SKILL.md among them.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `skills` | array of objects | yes |  |
| `skills[].skill` | [SkillName](#skillname) | yes |  |
| `skills[].files` | array of string | yes |  |

### `skills.read`

Read. Since version 1.

One file of a user's skill, as UTF-8 text; at most 256 KiB.

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
