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
| `settings.list`, `settings.get` | read | Settings with type, scope, default and value. AI callers (`reviewed`) never see `privacy.*` or `recent.*`: a list leaves them out and a get answers `refused` with reason `private`. |
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
| `files.write` | write | Replaces a file granted for writing, or makes one in a writable folder. |

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
| `selection.changed` | The selection or active Line of a Document changed. |
| `documents.changed` | A Document opened or closed, or the editing target changed. Without `documents.all` the list holds the editing target and the caller's attached Documents only. |
| `panel.changed` | A panel opened, closed or floated. |
| `command.ran` | A command ran, from the API, a menu, a toolbar or a key. |

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

## Versioning

The version is a running number. Every release that grows the API bumps it, and each operation records the version it arrived in (`since`). Changes are add-only: an operation's name, mark and existing parameters and results stay; new optional parameters and result fields may be added. A removal is deprecated for at least one version first. A plugin declares the lowest version it needs.

`tests/api/reference-v1.json` is the frozen version 1 reference. A test fails when the API breaks it: an operation, event or `$defs` entry disappears, a read/write mark changes, a parameter becomes required, a result field stops being given, or a schema changes beyond adding optional properties or, in parameters, adding alternatives. Freezing a new version writes its reference with `HIKARI_API_WRITE_REFERENCE=<path> hikari_api_tests --gtest_filter=ApiReference.*`.

## Reference

`hikarisub --export-docs <dir>` writes `api.json`: `{schema, version, $defs, operations, events, nodes}`, each operation with its name, mark, summary, `since`, `deprecatedIn` when set, and its parameter and result schemas. `meta.json` gives the API version as `api`. It also writes `api.md`, the same reference as Markdown for authors, which [docs/api-reference.md](api-reference.md) holds; `ApiReferenceMarkdown.TheCheckedInReferenceIsCurrent` in `hikari_api_tests` fails when that file differs, and rerunning it with `HIKARI_API_WRITE_MARKDOWN_REFERENCE=docs/api-reference.md` rewrites it. The release attaches both in `hikarisub-docs-<version>.zip`, and the site builds its API reference from them. The AI sidecar keeps a copy as `src/ai-sidecar/src/tools/api.json`, from which its tool schemas and types are built; see [AI tools](ai-tools.md).

## Tests

`tests/api` runs without network or Qt. A fake host holds real `Workspace` and `EditSession` objects. The **scripted test client** reads `.jsonl` scripts in `tests/api/scripts/`, one call per line, each with its expected result or error code, and checks every result against its operation's schema. The scripts double as examples of each operation. `hikari_ui_api_host_tests` drives the same API against the real application and shell: a write is one named undo step that the Undo command reverts, a stale revision is refused, the Grid follows `selection.set`, panels open and close, and granted files are read and written on disk.
