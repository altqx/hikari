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
| `video.frame` | read | 1 | One video frame as BGRA pixels in base64, optionally cropped, scaled to fit maxEdge. |
| `audio.info` | read | 1 | Whether audio is open and loaded, its duration and sample rate. |
| `audio.peaks` | read | 1 | The lowest and highest 16-bit sample in each of `buckets` equal slices of a time range. |
| `files.list` | read | 1 | The files the user granted this caller, with read or write access. |
| `files.read` | read | 1 | A granted file's content, as UTF-8 text or base64; at most 16 MiB. |
| `files.write` | write | 1 | Replaces a file granted for writing with new content. |
| `nodes.catalogue` | read | 1 | The node types the node-effect editor offers, with their ports and parameters. |
| `nodes.validate` | read | 1 | Checks a node-effect graph; each problem names its node and port or field. |

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

One video frame as BGRA pixels in base64, optionally cropped, scaled to fit maxEdge.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `frame` | integer |  | Default: the frame shown. At least 0. |
| `subtitles` | boolean |  | Render the subtitles on it. Default true. |
| `crop` | object |  |  |
| `crop.x` | integer | yes | At least 0. |
| `crop.y` | integer | yes | At least 0. |
| `crop.width` | integer | yes | At least 1. |
| `crop.height` | integer | yes | At least 1. |
| `maxEdge` | integer |  | Default 1024. From 16 to 4096. |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `frame` | integer | yes | At least 0. |
| `width` | integer | yes | At least 1. |
| `height` | integer | yes | At least 1. |
| `format` | `"bgra"` | yes |  |
| `data` | string | yes | width * 4 bytes per row, top row first, base64. |

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

The files the user granted this caller, with read or write access.

**Parameters**: none.

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `files` | array of [GrantedFile](#grantedfile) | yes |  |

### `files.read`

Read. Since version 1.

A granted file's content, as UTF-8 text or base64; at most 16 MiB.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes | A handle from files.list. |
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

Replaces a file granted for writing with new content.

**Parameters**

| Field | Type | Required | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes | A handle from files.list. |
| `encoding` | one of `utf8` or `base64` |  | Default utf8. |
| `data` | string | yes |  |

**Result**

| Field | Type | Always | Meaning |
| --- | --- | --- | --- |
| `file` | string | yes |  |
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

## Events

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
