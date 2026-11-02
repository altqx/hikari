# The Hikari API

The Hikari API is one typed, versioned set of operations over the running application. Plugins, the AI tools, the MCP server and node-editor nodes all call it ([#411](https://github.com/altqx/hikari/issues/411), wave 8 [#410](https://github.com/altqx/hikari/issues/410)). It lives in `src/api` (`hikari_api`), next to the application layer and like it free of Qt; the composition root implements its host ports.

Status: in progress. The version stays **0** until the card is done, then becomes **1**.

## Operations

An operation has a dotted name (`lines.list`), a **read** or **write** mark, a one-line summary, and a JSON Schema for its parameters and its result. A read changes no state at all; a write changes Document content, the selection, a panel, a setting, a file, or runs a command. The mark maps onto MCP's `readOnlyHint` and the AI approval policy.

Schemas use a subset of JSON Schema 2020-12: `type`, `properties`, `required`, `additionalProperties`, `items`, `enum`, `const`, `minimum`, `maximum`, `minLength`, `maxLength`, `minItems`, `maxItems`, `oneOf`, `anyOf` and `$ref` into the shared `$defs` (`Line`, `Style`, `DocumentId`, `LineId`, `Revision`, `SettingValue`, …). Every operation refuses unknown parameters. `integer` takes only whole JSON numbers written without a fraction or exponent.

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
| `settings.list`, `settings.get` | read | Settings with type, scope, default and value. |

Lines are addressed by their stable Line id, never by row ([document model](document-model.md)). Times are milliseconds. In Translation mode `text` is the original text and `translation` the translated text.

## Calls, callers and errors

`Api::call(caller, operation, params, reply)` validates the parameters, then runs the operation on the GUI thread. The reply comes exactly once, either before `call` returns or later for operations that wait on a backend.

A **caller** names itself for undo steps and holds its grants. Without the `documents.all` grant a caller sees only the editing target: an omitted `document` means the editing target, and naming any other gives `not_granted`.

An error is `{code, message, data}`. The message is English text for developers and models, not translated.

| Code | When |
| --- | --- |
| `unknown_operation` | No operation has that name. |
| `invalid_params` | The parameters break the schema; `data.pointer` is the JSON pointer of the first problem. |
| `not_found` | No such Document, Line or setting; `data` names it. |
| `not_granted` | The caller lacks the grant. |
| `unavailable` | The host has no such service (no Documents, commands, panels or settings). |

## Versioning

The version is a running number. Every release that grows the API bumps it, and each operation records the version it arrived in (`since`). Changes are add-only: an operation's name, mark and existing parameters and results stay; new optional parameters and result fields may be added. A removal is deprecated for at least one version first. A plugin declares the lowest version it needs.

## Tests

`tests/api` runs without network or Qt. A fake host holds real `Workspace` and `EditSession` objects. The **scripted test client** reads `.jsonl` scripts in `tests/api/scripts/`, one call per line, each with its expected result or error code, and checks every result against its operation's schema. The scripts double as examples of each operation.
