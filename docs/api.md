# The Hikari API

The Hikari API is one typed, versioned set of operations over the running application. Plugins, the AI tools, the MCP server and node-editor nodes all call it ([#411](https://github.com/altqx/hikari/issues/411), wave 8 [#410](https://github.com/altqx/hikari/issues/410)). It lives in `src/api` (`hikari_api`), next to the application layer and like it free of Qt; the composition root implements its host ports.

The current version is **1**, and it stays 1 until the first public release.

## Operations

An operation has a dotted name (`lines.list`), a **read** or **write** mark, a one-line summary, and a JSON Schema for its parameters and its result. A read changes no state at all; a write changes Document content, the selection, a panel, a setting, a file, or runs a command. The mark maps onto MCP's `readOnlyHint` and the AI approval policy.

Schemas use a subset of JSON Schema 2020-12: `type`, `properties`, `required`, `additionalProperties`, `patternProperties`, `items`, `enum`, `const`, `minimum`, `maximum`, `minLength`, `maxLength`, `pattern` (ECMAScript, matched anywhere in the string), `minItems`, `maxItems`, `oneOf`, `anyOf` and `$ref` into the shared `$defs` (`Line`, `Style`, `DocumentId`, `LineId`, `Revision`, `SettingValue`, …). Every operation refuses unknown parameters. `integer` takes only whole JSON numbers written without a fraction or exponent.

| Operation | Mark | What it gives |
| --- | --- | --- |
| `api.version` | read | The API version. |
| `documents.list` | read | Open Documents in tab order, the protected reference last: id, title, path, format, revision, editing target, reference, dirty, read-only. |
| `documents.get` | read | One Document's summary, Line and Style counts, and the Line with a pending Line editor draft. |
| `lines.list` | read | Lines in Document order, by page (`offset`, `limit`), with chosen `fields`. |
| `lines.get` | read | Lines by id. |
| `lines.find` | read | Lines matching text or a regular expression in one field, an exact Style, Actor or Effect, Comment, and a time window. |
| `styles.list` | read | The Styles, with ASS field text and colours as `&HAABBGGRR`. |
| `info.list` | read | The Script Info properties. |
| `selection.get` | read | The active Line and the selected Lines, in Document order. |
| `commands.list` | read | Commands with their id (`GLOBAL_SAVE_SUBS`), name, window, enabled state, disabled reason and keys; `query` searches ids and names. |
| `panels.list` | read | Workspace panels: open, floating, focused. |
| `settings.list`, `settings.get` | read | Settings with type, scope, default, value, title, source and projectable, plugins' settings included ([grown](#settingslist-and-settingsget-grown)). AI callers (`reviewed`) never see `privacy.*` or `recent.*`: a list leaves them out and a get answers `refused` with reason `private`. |
| `video.info` | read | Whether a video is open; its size, aspect ratio, the frame shown and the keyframes. |
| `video.frame_at_time`, `video.time_of_frame` | read | Converts between milliseconds and frame numbers. |
| `video.frame` | read | One frame as BGRA in base64, with every Line, one Line (`line`) or none drawn on it, optionally cropped, scaled to fit `maxEdge` and `maxPixels`; `crop` gives the part of the frame shown. |
| `video.ink_bounds` | read | The [ink bounds](#drawing-apart-from-the-video) of one Line, or of every Line, on a frame. |
| `video.colours` | read | The colours at up to 256 points of a frame as `&HBBGGRR&`, each the mean of a square of `radius` pixels, without subtitles unless asked. |
| `video.keyframes` | read | The keyframes from one time to another, each with its start time; 500 at most, with the `total`. |
| `audio.info` | read | Whether audio is open and loaded; duration, sample rate, channels. |
| `audio.peaks` | read | The lowest and highest sample in each of `buckets` slices of a time range. |
| `files.list`, `files.read` | read | The files and folders granted to the caller, what a granted folder holds, and a file's content as UTF-8 or base64 (16 MiB at most). |
| `project.info` | read | The Project of the shell window used last: its name and whether its folder is trusted, or null. |
| `skills.list`, `skills.read` | read | The user's [skills](ai-skills.md#the-hikari-api) and one file of a skill as UTF-8 (256 KiB at most); only with the skills grant. |
| `nodes.catalogue` | read | The node-effect editor's node types, with ports and parameters. |
| `nodes.validate` | read | Checks a node-effect graph; each problem names its node and port or field. |
| `effects.graphs`, `effects.graph` | read | The Document's graph Lines, and one graph with its problems. |
| `effects.edit` | read | Edits a graph in steps (`add`, `connect`, `disconnect`, `remove`, `set`) and gives the new graph with its problems; the Document doesn't change. |
| `effects.preview` | read | Runs a graph on the karaoke Lines and gives the Lines it would write; Lua nodes don't run. |
| `effects.save` | write | A graph into a new graph Line, or in place of one, as one undo step. |
| `effects.apply` | write | A graph Line's generated Lines, as the editor's Apply writes them, as one undo step; Lua nodes don't run. |
| `plugins.list`, `plugins.manifest` | read | The installed plugins with their tier and commands, and one plugin's `hikari-plugin.json`. |
| `vcs.refs`, `vcs.show` | read | Plugins with `vcs.read` only: the branches, tags and recent commits of the git repository holding a Document, and the Document's file at a ref, read-only ([Git](plugins.md#git)). |
| `plugins.review` | read | A scaffolded or granted package as the install question shows it, with its text files and SHA-256. |
| `plugins.scaffold` | write | A new plugin from the guide's template in the AI plugins folder, packed as a `.hikariplugin`; nothing installs. |
| `plugins.install` | write | Installs the package the user approved in the agent's card; any other caller gets `not_granted` ([AI-written plugins](ai-agent.md#ai-written-plugins)). |
| `plugins.run` | write | Runs an installed plugin's command. |
| `typeset.perspective`, `typeset.clip`, `typeset.drawing`, `typeset.shift`, `typeset.hydra` | read | The [typesetting solvers](ai-tools.md#typesetting): a Line's text with a quad's transform, a clip, a drawing, an offset or a Hydra change written; nothing is applied. |
| `typeset.check` | read | Draws one Line alone with a proposed text and compares its ink bounds with a target quad or box. |
| `karaoke.syllables`, `karaoke.split`, `karaoke.time`, `karaoke.snap`, `karaoke.kanji` | read | The [karaoke tools](ai-tools.md#karaoke): a Line's syllables, or Lines' texts split, timed, snapped to onsets or copied from a romaji Line; nothing is applied. |
| `karaoke.onsets`, `karaoke.furigana`, `karaoke.check` | read | The audio's vocal onsets in a range, a Line's suggested readings, the Karaoke check's problems. |
| `karaoke.template_preview` | read | The Lines the karaoke templater generates from one Line. |
| `document.apply` | write | One change set to one Document, as one undo step. |
| `typeset.apply` | write | Typeset Lines as one undo step, only when every one passes `typeset.check`. |
| `karaoke.apply_template` | write | Runs the karaoke templater and applies what it generates as one undo step. |
| `karaoke.align` | write | Runs the user's own WhisperX or MFA on the Lines' audio and answers each word's times; asks on every run. |
| `selection.set` | write | The selected Lines and the active Line; the Grid follows. |
| `commands.run` | write | Runs a command as its menu item or toolbar button would. |
| `panels.open`, `panels.close`, `panels.focus` | write | Shows, closes or focuses a panel where the layout keeps it. |
| `settings.set` | write | One setting, type-checked. |
| `settings.open` | write | Opens the Settings window at a setting, on the Yours or Project tab ([schema](#settingsopen)). |
| `files.write` | write | Replaces a file granted for writing, or makes one in a writable folder. |
| `programs.run` | write | Runs one of the plugin's declared programs to the end and gives its output. Plugins with the `programs` permission only. |
| `programs.start`, `programs.send`, `programs.stop` | write | Starts one of the plugin's declared programs and keeps it running, writes to its stdin, or ends it. |
| `problems.list` | read | The problems on a Document: the open QC notes and plugins' problems, in Document order. |
| `problems.set` | write | Replaces the caller's problems under one set on a Document. Plugins only. |
| `problems.clear` | write | Removes the caller's problems. Plugins only. |
| `problems.fix` | write | Applies one of a problem's quick fixes as one undo step. |

Commands and panels are those of the shell window used last (ADR 0025), never a closed window's.

Lines are addressed by their stable Line id, never by row ([document model](document-model.md)). Times are milliseconds. In Translation mode `text` is the original text and `translation` the translated text.

## Writes

`document.apply` takes the Document `revision` the caller read, a step `name`, and a list of `edits`: `set_line`, `insert_line`, `delete_line`, `move_line`, `set_style`, `delete_style` and `set_info`. The whole list applies as one command through `EditSession::run`, the path Automation macros use, so it is one undo step named `"<caller>: <name>"`. If the revision moved since the read, nothing changes and the call fails with `stale_revision`; read again and retry. A pending Line editor draft on a touched Line is committed first, which moves the revision. If any edit fails, the Document stays as it was and `data.edit` gives the failing edit's index. A change set that changes nothing adds no step and answers `changed: false`.

One write runs at a time; another write meanwhile gets `busy`.

## Files

A caller reaches only files the user granted it: `Api::grantFile(caller, path, writable)` gives a handle such as `f1`, and `files.list` shows the handles with a display name, never the path. A file granted for reading refuses `files.write` with `not_granted`. Plugins get handles from pickers, drops and their folders ([Files](plugins.md#files)); the AI cards decide their own grants.

A caller can also hold a folder: `Api::grantFolder(caller, path, writable)` gives a handle such as `d2`, which `files.list` shows under `folders`. `files.list` with `folder` (and a `path` inside it) adds that folder's `entries`, each with its `path` relative to the folder, in name order and at most 4096. `files.read` and `files.write` take `folder` and `path` instead of `file`; the answer's `file` is a handle for that file from then on. A path goes through the same checks as a plugin's ([Files](plugins.md#files)): `..`, absolute paths, names Windows can't hold, and links that lead outside the folder are refused with `not_granted`, and a list leaves such links out. A write in a writable folder makes the missing folders on the way. `Api::revokeFiles` ends a caller's folders with its files.

An Agent panel thread's caller carries its window's Project folder (`Caller::project`). It holds that folder as `project`, with no grant and no prompt, whether or not the Project is trusted. MCP callers have no Project folder.

An AI caller's `files.write` never reaches the disk: the file is staged in the review (`AiReview`), the answer says `staged: true`, and the user keeps or undoes it in the Review panel. Its own `files.read` sees what it staged. In an untrusted Project its writes stage too (Q473). A write to the path of a Document open in a tab is refused with reason `open_document`; change the Document with `document.apply`. Without a review the `project` folder is read-only.

An AI caller (`reviewed`) never gets two kinds of file in a folder. **Excluded paths** are left out of a list and refused with reason `excluded`, whether named directly or reached through a link. They are the built-in `.hikari/credentials.toml`, `.env*`, `*.key`, `.git/` and `.hikari/project.local.toml`, together with the user's `ai.exclude` and the folder's Project's `ai.exclude`. A list adds to the others and never removes a default. Globs work as in `.gitignore`: one with a `/` before its end is anchored at the folder, one without matches a name at any depth, a trailing `/` matches folders only, and a folder's match covers everything inside it. **Media files** (video and audio) are refused with reason `media`; the AI reads them through `video.frame` and `audio.peaks`. An AI caller can't change `ai.exclude` through `settings.set` (reason `private`).

The user's skills are apart from files: `skills.*` need the caller's `skills` grant, which only Hikari AI's callers hold ([AI skills](ai-skills.md#the-hikari-api)).

## Drawing apart from the video

`video.frame` with `line` and `video.ink_bounds` draw a Document in a render context of their own, through the host's `RenderHost`, so the video's subtitles are never prepared again. The context is prepared the way the video prepares its subtitles, with the same renderer, script and fonts, and is parsed again only when the script or fonts change. With `line`, every other Line is left out of the script. Without `frame`, both use the frame shown, or the Line's first frame when the Line isn't on it. The ink bounds are the smallest rectangle holding every pixel drawn, and null when nothing is. A host without the port answers `unavailable`.

## Node catalogue

`api::nodeCatalogue()` holds the node types of the node-effect editor: an id, a category, a summary, input and output ports, and a parameter schema. It starts with the engine's nodes ([node effects](node-effects.md)), and `nodes.catalogue` and the reference list them. `nodes.validate` checks a graph in the [graph format](node-effects.md#the-graph-format) and returns `{valid, problems}`.

## Events

A caller subscribes to events by name and hears them only while subscribed.

| Event | When |
| --- | --- |
| `document.changed` | A Document's revision moved: an edit, an undo, a write. |
| `document.saved` | A Document was written to a file: Save, Save as, Save all or Save translation. It comes after the file is on disk, with the path and the revision written, and never for a cancelled or failed save. |
| `selection.changed` | The selection or active Line of a Document changed. |
| `documents.changed` | A Document opened or closed, or the editing target changed. Without `documents.all` the list holds the editing target and the caller's attached Documents only. |
| `panel.changed` | A panel opened, closed or floated. |
| `program.output` | A started program wrote to stdout or stderr. Only its plugin hears it, in order and never merged. |
| `program.exited` | A started program ended. Only its plugin hears it. |
| `problems.changed` | The problems on a Document changed: a plugin set or cleared them, an edit dropped some, or the open QC notes changed. It names the Document and the sources. |
| `command.ran` | A command was triggered, from the API, a menu, a toolbar or a key. It reports the trigger, not the outcome: a Save that opens a dialog or a question comes before anything is written, even when it is then cancelled. Use `document.saved` to know a file was written. |

The application notices changes after each refresh of its views and each selection change, so a burst of edits gives one event per refresh, not one per edit.

## Calls, callers and errors

`Api::call(caller, operation, params, reply)` validates the parameters, then runs the operation on the GUI thread. The reply comes exactly once, either before `call` returns or later for operations that wait on a backend.

A **caller** names itself for undo steps and holds its grants. Without the `documents.all` grant a caller sees only the editing target and its `attached` Documents, which the user attached with `@` in an AI thread. An omitted `document` means the editing target, and naming any other Document gives `not_granted`. Events about other Documents don't reach that caller.

An error is `{code, message, data}`. The message is English text for developers and models, not translated.

| Code | When |
| --- | --- |
| `unknown_operation` | No operation has that name. |
| `invalid_params` | The parameters break the schema; `data.pointer` is the JSON pointer of the first problem. |
| `not_found` | No such Document, Line or setting; `data` names it. |
| `not_granted` | The caller lacks the grant: a Document, a file or the skills. |
| `unavailable` | The host has no such service: no Documents, commands, panels, settings, video, audio, files or skills, or a frame that cannot be decoded. |
| `stale_revision` | The Document changed since the given revision; `data.current` is the revision now. |
| `read_only` | The Document can't be edited (an open Automation macro holds it). |
| `protected` | The Document is the protected reference. |
| `refused` | The application refused; `data.reason` says why when the caller can act on it (`disabled`, `not_run`, `group_break`, `invalid_draft`, `loading`, `private`, `too_large`, `not_text`, …). |
| `busy` | Another write is running. |

## Extension operations

[ADR 0034](adr/0034-plugins-grow-into-an-extension-system.md) adds these operations and events. They arrive with the code cards [#1222](https://github.com/altqx/hikari/issues/1222)–[#1231](https://github.com/altqx/hikari/issues/1231) on beta.5, all `since` 1, and each card moves its rows into the tables above as it lands. [Extensions](plugins.md#extensions) says how plugins use them. Schemas are in the subset above and use the shared `$defs`. New `$defs`: `Problem` and `ProgramProcess`, and for the [quick-pick services](#quick-picks-and-input-boxes) `QuickPickItem` and `QuickPickButton`.

| Operation | Mark | Callers | What it does | Card |
| --- | --- | --- | --- | --- |
| `status_items.set` | write | plugins | Changes one of the caller's status bar items. | #1224 |
| `formats.list` | read | all | The subtitle formats HikariSub can open and export, built-in and from plugins. | #1229 |
| `plugins.run` | write | all | Gains `args` and gives the command's `result`. | #1228 |
| `presence.set_text` | write | plugins | Sets or clears the caller's text for `{plugin}` in the user's Discord status. | #1245 |

"Plugins" means every plugin, Lua or program; AI and MCP callers get `not_granted`. `programs.*` need the plugin's `programs` permission, and `presence.set_text` its `presence` permission. While a plugin runs as a `read` [AI tool](plugins.md#ai-tools-and-mcp-servers), each of its write operations, and the services `net.fetch`, `files.write` and `hikari/settings.set`, answer `refused` with reason `read_tool`.

| Event | When | Card |
| --- | --- | --- |
| `plugin_settings.changed` | One of the plugin's own settings changed, from the plugin manager, Settings or a Project. Only its plugin hears it. | #1230 |

### New `$defs`

```json
"Problem": {
  "type": "object",
  "properties": {
    "id": {"type": "string", "maxLength": 64, "description": "Given by problems.list; problems.set ignores it."},
    "line": {"$ref": "#/$defs/LineId", "description": "Absent only for a QC note that matches no Line."},
    "time": {"type": "integer", "description": "A QC note's time in milliseconds; problems.set ignores it."},
    "field": {"enum": ["text", "translation"], "description": "text when omitted."},
    "from": {"type": "integer", "minimum": 0, "description": "UTF-16 offset in the field; with to, the squiggle. The whole Line when omitted."},
    "to": {"type": "integer", "minimum": 0},
    "severity": {"enum": ["error", "warning", "info", "hint"]},
    "message": {"type": "string", "minLength": 1, "maxLength": 500},
    "code": {"type": "string", "minLength": 1, "maxLength": 64, "description": "The check's own name, such as a grammar rule."},
    "source": {
      "type": "object",
      "description": "Given by problems.list.",
      "properties": {
        "kind": {"enum": ["qc", "plugin", "server"]},
        "id": {"type": "string", "description": "qc, a plugin id, or plugin:<plugin id>/<server id>."},
        "set": {"type": "string", "description": "The plugin's own source name from problems.set, or a QC note's section."},
        "name": {"type": "string", "description": "What the problems list shows."}
      },
      "required": ["kind", "id", "name"],
      "additionalProperties": false
    },
    "fixes": {
      "type": "array",
      "maxItems": 8,
      "items": {
        "type": "object",
        "properties": {
          "title": {"type": "string", "minLength": 1, "maxLength": 100},
          "edits": {
            "type": "array",
            "minItems": 1,
            "maxItems": 64,
            "description": "Replacements in this Line's field, by UTF-16 offsets in the text the problem was set against.",
            "items": {
              "type": "object",
              "properties": {
                "from": {"type": "integer", "minimum": 0},
                "to": {"type": "integer", "minimum": 0},
                "insert": {"type": "string", "maxLength": 10000}
              },
              "required": ["from", "to", "insert"],
              "additionalProperties": false
            }
          }
        },
        "required": ["title", "edits"],
        "additionalProperties": false
      }
    }
  },
  "required": ["severity", "message"],
  "additionalProperties": false
},
"ProgramProcess": {"type": "string", "pattern": "^p[0-9]{1,9}$", "description": "A started program, such as p1."}
```

### `status_items.set`

Write. Plugins only. Fields left out keep their value; `text` and `icon` can't both end up empty.

```json
{"type": "object", "properties": {
  "item": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64, "description": "An id from contributes.statusItems."},
  "text": {"type": "string", "maxLength": 40, "description": "Empty: icon only."},
  "icon": {"type": "string", "maxLength": 68, "pattern": "^((own:)?[a-z0-9]+(-[a-z0-9]+)*)?$", "description": "An icon role or own:<id>; empty: text only."},
  "tooltip": {"type": "string", "maxLength": 300},
  "tone": {"enum": ["normal", "warning", "danger"]},
  "visible": {"type": "boolean"}},
 "required": ["item"], "additionalProperties": false}
```

Result: `{"type": "object", "properties": {"item": {"type": "string"}}, "required": ["item"], "additionalProperties": false}`.

### `problems.list`

Read. Every caller, with the usual Document grants.

```json
{"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
  "lines": {"type": "array", "maxItems": 20000, "items": {"$ref": "#/$defs/LineId"}, "description": "These Lines only."},
  "severity": {"enum": ["error", "warning", "info", "hint"], "description": "This severity and worse only."},
  "source": {"type": "string", "description": "One source id only."}},
 "additionalProperties": false}
```

```json
{"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId"},
  "revision": {"$ref": "#/$defs/Revision"},
  "problems": {"type": "array", "items": {"$ref": "#/$defs/Problem"}}},
 "required": ["document", "revision", "problems"], "additionalProperties": false}
```

Problems come in Document order, then by `from`, and problems with no Line last. An open QC note is a problem of `kind` `qc` with severity `info`, its `time`, its section as `source.set`, and the Line the QC notes panel matches it to (the latest start at its time, else the nearest within 15 seconds), or no `line`.

### `problems.set`

Write. Plugins only. Replaces every problem the caller set under `set` on that Document. Each problem needs `line`; `to` comes with `from`, and `from` ≤ `to` ≤ the field's length, or the call is `invalid_params`. Problems are never saved and aren't undo steps; they go when the plugin stops or the Document closes, and a problem whose Line is deleted goes with it. A plugin holds at most 20000 problems per Document.

```json
{"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
  "revision": {"$ref": "#/$defs/Revision", "description": "The revision the offsets were measured on; stale_revision when it moved."},
  "set": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64, "description": "The plugin's own name for this group of checks; default when omitted."},
  "problems": {"type": "array", "maxItems": 20000, "items": {"$ref": "#/$defs/Problem"}}},
 "required": ["revision", "problems"], "additionalProperties": false}
```

```json
{"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId"},
  "count": {"type": "integer", "minimum": 0}},
 "required": ["document", "count"], "additionalProperties": false}
```

### `problems.clear`

Write. Plugins only.

```json
{"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId", "description": "Every Document when omitted."},
  "set": {"type": "string", "description": "Every set of the caller's when omitted."}},
 "additionalProperties": false}
```

Result: `{"type": "object", "properties": {"cleared": {"type": "integer", "minimum": 0}}, "required": ["cleared"], "additionalProperties": false}`.

### `problems.fix`

Write. Every caller. Applies the fix through `document.apply` as one undo step named `"<source name>: <fix title>"`, and answers as `document.apply` does. A fix whose Line changed since its problem was set answers `stale_revision`. Its edits may not overlap, or it answers `invalid_params`.

```json
{"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
  "problem": {"type": "string", "description": "An id from problems.list."},
  "fix": {"type": "integer", "minimum": 0, "description": "The fix's index."}},
 "required": ["problem", "fix"], "additionalProperties": false}
```

### `programs.run`

Write. Plugins with the `programs` permission. Runs the program with its declared arguments, then `args`. It answers once the program ends, or with `refused` and reason `timed_out` after `timeout`, when HikariSub ends it. Output is capped at 16 MiB per stream, and more is `refused` with reason `too_large`. A program not available on this platform answers `unavailable` with reason `platform`; one not downloaded yet, `unavailable` with reason `not_downloaded`.

```json
{"type": "object", "properties": {
  "program": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64},
  "args": {"type": "array", "maxItems": 256, "items": {"type": "string", "maxLength": 32768},
    "description": "${file:N} becomes the path of files[N], which the program may then open."},
  "files": {"type": "array", "maxItems": 16, "items": {"type": "string", "description": "A file or folder handle the plugin holds."},
    "description": "Read-only unless the handle is writable."},
  "stdin": {"type": "string", "maxLength": 16777216},
  "encoding": {"enum": ["utf8", "base64"], "description": "Of stdin, stdout and stderr; utf8 when omitted."},
  "timeout": {"type": "integer", "minimum": 1000, "maximum": 86400000, "description": "Milliseconds; 60000 when omitted."}},
 "required": ["program"], "additionalProperties": false}
```

```json
{"type": "object", "properties": {
  "exitCode": {"type": "integer"},
  "stdout": {"type": "string"},
  "stderr": {"type": "string"},
  "encoding": {"enum": ["utf8", "base64"]}},
 "required": ["exitCode", "stdout", "stderr", "encoding"], "additionalProperties": false}
```

### `programs.start`, `programs.send` and `programs.stop`

Write. Plugins with the `programs` permission. A plugin keeps at most 8 programs running. They end when the plugin stops.

```json
{"type": "object", "properties": {
  "program": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64},
  "args": {"type": "array", "maxItems": 256, "items": {"type": "string", "maxLength": 32768}},
  "files": {"type": "array", "maxItems": 16, "items": {"type": "string"}},
  "encoding": {"enum": ["utf8", "base64"], "description": "Of what program.output carries and programs.send takes."}},
 "required": ["program"], "additionalProperties": false}
```

`programs.start` answers `{"type": "object", "properties": {"process": {"$ref": "#/$defs/ProgramProcess"}}, "required": ["process"], "additionalProperties": false}`.

`programs.send` takes `{"process", "data", "close"}`, where `close: true` closes stdin after `data`:

```json
{"type": "object", "properties": {
  "process": {"$ref": "#/$defs/ProgramProcess"},
  "data": {"type": "string", "maxLength": 16777216},
  "close": {"type": "boolean"}},
 "required": ["process"], "additionalProperties": false}
```

`programs.stop` takes `{"process"}` and ends the program and its children. Both answer `{"process"}`, and a process that has ended answers `not_found`.

### `formats.list`

Read. Every caller.

```json
{"type": "object", "properties": {
  "formats": {"type": "array", "items": {"type": "object", "properties": {
    "id": {"type": "string", "description": "ass, srt, … or plugin:<plugin id>/<format id>."},
    "title": {"type": "string"},
    "extensions": {"type": "array", "items": {"type": "string"}},
    "import": {"type": "boolean"},
    "export": {"type": "boolean"},
    "plugin": {"type": "string", "description": "The plugin's id, for a plugin format."}},
   "required": ["id", "title", "extensions", "import", "export"], "additionalProperties": false}}},
 "required": ["formats"], "additionalProperties": false}
```

It takes no parameters (`{"type": "object", "additionalProperties": false}`). `documents.list`'s `format` takes these ids too.

### `plugins.run`, grown

`plugins.run` gains the optional parameter `args` (`{"type": "object"}`), which reaches the command's handler, and the optional result field `result`, what the handler returned. It stays on the AI's always-ask list. An [AI tool](plugins.md#ai-tools-and-mcp-servers) call goes through its own gate instead, which checks `args` against the tool's `parameters` and `result` against its `result`.

### `settings.open`

Write, as `panels.open` is. Every caller. It does what the link `hikarisub://settings/<id>` does ([Settings](plugins.md#settings-grown)): opens the Settings window in the shell window used last and shows the setting, or the window alone without `setting`. It changes no setting. There is no page or query parameter: HikariSub's page ids are never API.

```json
{"type": "object", "properties": {
  "setting": {"type": "string", "pattern": "^[A-Za-z0-9_.-]{1,200}$",
    "description": "A setting id from settings.list, such as appearance.theme or plugin.<plugin id>.<setting id>."},
  "scope": {"enum": ["yours", "project"], "description": "The tab; yours when omitted."}},
 "additionalProperties": false}
```

Result: `{"type": "object", "properties": {"opened": {"const": true}}, "required": ["opened"], "additionalProperties": false}`. An id `settings.list` doesn't give answers `not_found`, and an AI caller naming a `privacy.*` or `recent.*` setting gets `refused` with reason `private`. `project` without a trusted Project still opens the Project tab, with its note.

### `settings.list` and `settings.get`, grown

Each setting in their results gains three optional fields:

```json
"title": {"type": "string", "description": "The name the Settings window shows, in the UI language."},
"source": {"type": "string", "description": "app, or the id of the plugin whose setting it is."},
"projectable": {"type": "boolean", "description": "Whether a Project can override it on the Project tab."}
```

They also list each running plugin's settings, as `plugin.<plugin id>.<setting id>` with that plugin as `source`. A `secret` setting is left out of the list. `settings.get` and `settings.set` refuse a `secret` with reason `private` for every caller; its plugin reads it with `hikari.settings.get`. For AI callers, `settings.set` of a `plugin.` setting joins the always-ask list beside `plugins.*` (#1228 amends [ai-agent.md](ai-agent.md)).

### `presence.set_text`

Write. Plugins only, with the `presence` permission. Sets the caller's text, which shows only where the user's own Discord presence text has `{plugin}` and only while the user picked this plugin and presence is on ([Discord status text](plugins.md#discord-status-text)). Empty text clears it. HikariSub still sends presence at most once every 4 seconds, so only the newest text in that gap goes out.

```json
{"type": "object", "properties": {
  "text": {"type": "string", "maxLength": 64, "pattern": "^[^\\u0000-\\u001F\\u007F]*$", "description": "Plain text; empty clears it."}},
 "required": ["text"], "additionalProperties": false}
```

Result: `{"type": "object", "additionalProperties": false}`.

### Quick picks and input boxes

#1236. These are plugin services, like `panels.*` and `dialogs.*`: the helper's `Ui` service carries them, and program plugins call them by name over JSON-RPC. They aren't registry operations, so they have no read/write mark and no `reference-v1.json` entry, and AI and MCP callers can't reach them. They need no permission, and open only while the plugin answers something the user started, as [Quick picks and input boxes](plugins.md#quick-picks-and-input-boxes) says; elsewhere they answer `refused` with reason `not_user_initiated`. Opening one closes any other in that window, which answers as cancelled.

| Service | What it does |
| --- | --- |
| `quick_pick.ask` | Shows a quick pick and waits for the pick or a cancel. |
| `input_box.ask` | Shows an input box and waits for the text or a cancel. |
| `quick_pick.open` | Shows a live quick pick and answers its id at once; its events go to the plugin. |
| `quick_pick.update` | Changes an open quick pick. |
| `quick_pick.close` | Closes an open quick pick. |
| `input_box.open` | Shows a live input box and answers its id at once; its events go to the plugin. |
| `input_box.update` | Changes an open input box, with a message of a severity or a pending check. |
| `input_box.close` | Closes an open input box. |

#### `quick_pick.ask`

```json
{"type": "object", "properties": {
  "title": {"type": "string", "maxLength": 100},
  "placeholder": {"type": "string", "maxLength": 200},
  "value": {"type": "string", "maxLength": 1000, "description": "The filter text it starts with."},
  "items": {"type": "array", "maxItems": 10000, "items": {"$ref": "#/$defs/QuickPickItem"}},
  "many": {"type": "boolean", "description": "A check box per item; Enter takes every checked one."},
  "match_on": {"type": "array", "maxItems": 2, "items": {"enum": ["description", "detail"]}, "description": "Filtering always matches label, and these too."},
  "keep_order": {"type": "boolean", "description": "Keep the given order instead of sorting matches best first."},
  "step": {"type": "object", "properties": {"current": {"type": "integer", "minimum": 1}, "total": {"type": "integer", "minimum": 1}},
    "required": ["current", "total"], "additionalProperties": false},
  "back": {"type": "boolean", "description": "Back in the title row, for a step after the first."},
  "buttons": {"type": "array", "maxItems": 3, "items": {"$ref": "#/$defs/QuickPickButton"}, "description": "Icon buttons in the title row."}},
 "required": ["items"], "additionalProperties": false}
```

```json
{"type": "object", "properties": {
  "cancelled": {"type": "boolean"},
  "items": {"type": "array", "items": {"type": "string"}, "description": "The picked ids in list order; absent when cancelled."},
  "button": {"type": "string", "description": "A title or item button the user pressed instead of picking; cancelled is then true."},
  "back": {"type": "boolean", "description": "true when the user pressed Back; cancelled is then true."}},
 "required": ["cancelled"], "additionalProperties": false}
```

Item ids are unique in the list, and separators have none. With `many`, Enter with nothing checked answers `{"cancelled": false, "items": []}`.

#### `input_box.ask`

```json
{"type": "object", "properties": {
  "title": {"type": "string", "maxLength": 100},
  "prompt": {"type": "string", "maxLength": 300, "description": "One line under the box."},
  "placeholder": {"type": "string", "maxLength": 200},
  "value": {"type": "string", "maxLength": 10000},
  "selection": {"type": "object", "properties": {"from": {"type": "integer", "minimum": 0}, "to": {"type": "integer", "minimum": 0}},
    "required": ["from", "to"], "additionalProperties": false, "description": "UTF-16 offsets in value; all of it when omitted."},
  "password": {"type": "boolean"},
  "validation": {"type": "object", "properties": {
    "pattern": {"type": "string", "maxLength": 500, "description": "An ECMAScript expression the whole text must match."},
    "message": {"type": "string", "minLength": 1, "maxLength": 200},
    "min_length": {"type": "integer", "minimum": 0},
    "max_length": {"type": "integer", "minimum": 1, "maximum": 10000}},
   "required": ["message"], "additionalProperties": false},
  "step": {"type": "object", "properties": {"current": {"type": "integer", "minimum": 1}, "total": {"type": "integer", "minimum": 1}},
    "required": ["current", "total"], "additionalProperties": false},
  "back": {"type": "boolean"}},
 "additionalProperties": false}
```

```json
{"type": "object", "properties": {
  "cancelled": {"type": "boolean"},
  "value": {"type": "string", "description": "Absent when cancelled."},
  "back": {"type": "boolean", "description": "true when the user pressed Back; cancelled is then true."}},
 "required": ["cancelled"], "additionalProperties": false}
```

A `pattern` that isn't a valid expression answers `invalid_params`. A `validation` failure is always an error; levels and slow checks need `input_box.open`.

#### `input_box.open`, `input_box.update` and `input_box.close`

`input_box.open` takes `input_box.ask`'s parameters and answers `{"box": "b1"}` (`{"type": "string", "pattern": "^b[0-9]{1,9}$"}`). `input_box.update` takes `{box}`, any field `open` takes, and:

```json
"message": {"oneOf": [{"type": "null"}, {"type": "object", "properties": {
  "severity": {"enum": ["error", "warning", "info"]},
  "text": {"type": "string", "minLength": 1, "maxLength": 300}},
 "required": ["severity", "text"], "additionalProperties": false}], "description": "Shown under the box; null clears it. An error blocks Enter."},
"busy": {"type": "boolean", "description": "A check is pending: Enter waits for it."}
```

`input_box.close` takes `{box}`. Both answer `{box}`, and a box that has closed answers `not_found`. A plugin has at most one open, apart from its quick pick. Its events reach `hikari.input_box.on`, or a program plugin's `input_box` request:

```json
{"type": "object", "properties": {
  "kind": {"enum": ["value", "accept", "back", "hide"]},
  "box": {"type": "string"},
  "value": {"type": "string"}},
 "required": ["kind", "box"], "additionalProperties": false}
```

`value` and `accept` come with `value`. An `accept` comes only when the message isn't an error and `busy` is false. Waiting `value` events merge, so only the newest arrives.

#### `quick_pick.open`, `quick_pick.update` and `quick_pick.close`

`quick_pick.open` takes `quick_pick.ask`'s parameters with `items` optional, plus:

```json
"filter": {"enum": ["fuzzy", "none"], "description": "none shows the items as given, for a plugin that filters on its value events."},
"busy": {"type": "boolean"},
"active": {"type": "string", "description": "The id of the item to highlight."}
```

It answers `{"pick": "q1"}` (`{"type": "string", "pattern": "^q[0-9]{1,9}$"}`). `quick_pick.update` takes `{pick}` and any field `open` takes, and replaces what it names. `quick_pick.close` takes `{pick}`. Both answer `{pick}`, and a quick pick that has closed answers `not_found`. A plugin has at most one open.

Its events reach the plugin's `hikari.quick_pick.on` handler, or a program plugin's `quick_pick` request:

```json
{"type": "object", "properties": {
  "kind": {"enum": ["value", "active", "accept", "button", "back", "hide"]},
  "pick": {"type": "string"},
  "value": {"type": "string"},
  "item": {"type": "string"},
  "items": {"type": "array", "items": {"type": "string"}},
  "button": {"type": "string"}},
 "required": ["kind", "pick"], "additionalProperties": false}
```

`value` comes with `value`; `active` with `item`; `accept` with `items`; `button` with `button`, plus `item` for an item's button. Waiting `value` events merge, so only the newest arrives.

#### New `$defs`

```json
"QuickPickItem": {"type": "object", "properties": {
  "kind": {"enum": ["item", "separator"], "description": "item when omitted."},
  "id": {"type": "string", "minLength": 1, "maxLength": 200, "description": "Needed by every item but a separator."},
  "label": {"type": "string", "minLength": 1, "maxLength": 200},
  "description": {"type": "string", "maxLength": 200, "description": "Shown after the label, muted."},
  "detail": {"type": "string", "maxLength": 500, "description": "Shown on a second line."},
  "icon": {"type": "string", "maxLength": 68, "pattern": "^(own:)?[a-z0-9]+(-[a-z0-9]+)*$"},
  "picked": {"type": "boolean", "description": "Checked at the start, with many."},
  "always_show": {"type": "boolean", "description": "Kept whatever the filter."},
  "disabled": {"type": "string", "minLength": 1, "maxLength": 200, "description": "Why the item can't be picked; it shows greyed with this reason."},
  "buttons": {"type": "array", "maxItems": 2, "items": {"$ref": "#/$defs/QuickPickButton"}}},
 "required": ["label"], "additionalProperties": false},
"QuickPickButton": {"type": "object", "properties": {
  "id": {"type": "string", "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$", "maxLength": 64},
  "icon": {"type": "string", "maxLength": 68, "pattern": "^(own:)?[a-z0-9]+(-[a-z0-9]+)*$"},
  "tooltip": {"type": "string", "minLength": 1, "maxLength": 100}},
 "required": ["id", "icon", "tooltip"], "additionalProperties": false}
```

### Events

```json
"problems.changed": {"type": "object", "properties": {
  "document": {"$ref": "#/$defs/DocumentId"},
  "sources": {"type": "array", "items": {"type": "string"}}},
 "required": ["document", "sources"], "additionalProperties": false},
"program.output": {"type": "object", "properties": {
  "process": {"$ref": "#/$defs/ProgramProcess"},
  "stream": {"enum": ["stdout", "stderr"]},
  "data": {"type": "string"},
  "encoding": {"enum": ["utf8", "base64"]}},
 "required": ["process", "stream", "data", "encoding"], "additionalProperties": false},
"program.exited": {"type": "object", "properties": {
  "process": {"$ref": "#/$defs/ProgramProcess"},
  "exitCode": {"type": "integer"},
  "stopped": {"type": "boolean", "description": "true when programs.stop or the plugin's end stopped it."}},
 "required": ["process", "exitCode", "stopped"], "additionalProperties": false},
"plugin_settings.changed": {"type": "object", "properties": {
  "ids": {"type": "array", "items": {"type": "string"}}},
 "required": ["ids"], "additionalProperties": false}
```

Several `program.output` events of one process are delivered in order and never merged, unlike other events.

## Versioning

The version is a running number. Every release that grows the API bumps it, and each operation records the version it arrived in (`since`). Changes are add-only: an operation's name, mark and existing parameters and results stay; new optional parameters and result fields may be added. A removal is deprecated for at least one version first. A plugin declares the lowest version it needs.

`tests/api/reference-v1.json` is the frozen version 1 reference. A test fails when the API breaks it: an operation, event or `$defs` entry disappears, a read/write mark changes, a parameter becomes required, a result field stops being given, or a schema changes beyond adding optional properties or, in parameters, adding alternatives. Freezing a new version writes its reference with `HIKARI_API_WRITE_REFERENCE=<path> hikari_api_tests --gtest_filter=ApiReference.*`.

## Reference

`hikarisub --export-docs <dir>` writes `api.json`: `{schema, version, $defs, operations, events, nodes}`, each operation with its name, mark, summary, `since`, `deprecatedIn` when set, and its parameter and result schemas. `meta.json` gives the API version as `api`. It also writes `api.md`, the same reference as Markdown for authors, which [docs/api-reference.md](api-reference.md) holds; `ApiReferenceMarkdown.TheCheckedInReferenceIsCurrent` in `hikari_api_tests` fails when that file differs, and rerunning it with `HIKARI_API_WRITE_MARKDOWN_REFERENCE=docs/api-reference.md` rewrites it. The release attaches both in `hikarisub-docs-<version>.zip`, and the site builds its API reference from them. The AI sidecar keeps a copy as `src/ai-sidecar/src/tools/api.json`, from which its tool schemas and types are built; see [AI tools](ai-tools.md).

## Tests

`tests/api` runs without network or Qt. A fake host holds real `Workspace` and `EditSession` objects. The **scripted test client** reads `.jsonl` scripts in `tests/api/scripts/`, one call per line, each with its expected result or error code, and checks every result against its operation's schema. The scripts double as examples of each operation. `hikari_ui_api_host_tests` drives the same API against the real application and shell: a write is one named undo step that the Undo command reverts, a stale revision is refused, the Grid follows `selection.set`, panels open and close, and granted files are read and written on disk.
