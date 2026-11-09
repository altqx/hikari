# Hikari AI tools

The tools a model calls to read and edit subtitles ([ADR 0024](adr/0024-hikari-ai.md), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [AI1 #429](https://github.com/altqx/hikari/issues/429)). Every tool is an [Hikari API](api.md) operation, or a search the sidecar answers itself. A tool call reaches documents only through `_hikari/api.call`. That goes into HA1's `Api::call` under the sidecar's caller, which validates the parameters, refuses stale revisions and busy writes, and scopes the Documents.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/ai-sidecar/src/tools/api.json` | HA1's reference, as `apiReference()` gives it without `nodes`. |
| `src/ai-sidecar/src/tools/api.gen.ts` | The TypeScript types of every `$defs` entry, operation and event, generated from `api.json`. |
| `src/ai-sidecar/tools/gen-api.ts` | The generator: `bun tools/gen-api.ts` rewrites `api.gen.ts`. |
| `src/ai-sidecar/src/tools/catalogue.ts` | Where each operation is offered, the groups, and the tool definitions built from HA1's schemas. |
| `src/ai-sidecar/src/tools/toolset.ts` | `ToolSet`: the tools offered to one conversation, calls, searches, and errors worded for the model. |

HA1 stays the source of truth. `ApiReference.TheSidecarsCopyIsCurrent` in `hikari_api_tests` fails when `api.json` differs from the registry. To fix it, rerun that test with `HIKARI_API_WRITE_SIDECAR_REFERENCE=src/ai-sidecar/src/tools/api.json`, then `bun tools/gen-api.ts` in `src/ai-sidecar`. A bun test then fails when `api.gen.ts` differs from the generator's output.

## The tools

A tool's name is its operation's, with `.` as `_` (`lines.find` is `lines_find`), because providers allow only letters, digits, `_` and `-`. Its description is the operation's summary. Its input schema is the operation's parameter schema with every `$ref` inlined and `const` written as a one-value `enum`, since Gemini takes no `const`. Descriptions name tools, not operations. Each tool carries MCP's `readOnlyHint`, taken from the operation's read or write mark, and `_meta["hikari/group"]`.

**The core tools** are always offered. There are 20:

- `documents_list`, `documents_get`;
- `lines_list`, `lines_get`, `lines_find`;
- `styles_list`, `info_list`;
- `selection_get`, `selection_set`;
- `document_apply`;
- `video_info`, `video_frame_at_time`, `video_time_of_frame`, `audio_info`;
- `files_list`, `files_read`, `files_write`;
- `search_tools`, `search_commands`, `run_command`.

**The searchable groups** are offered once `search_tools` finds them. A group is listed only once it holds a tool:

| Group | Tools now |
| --- | --- |
| `typeset` | `video_frame`, `video_ink_bounds`, `video_colours`, `video_keyframes` ([frames](#frames)) |
| `karaoke` | `audio_peaks` |
| `effects` | `nodes_catalogue` |
| `app` | `panels_list`, `panels_open`, `panels_close`, `panels_focus`, `settings_list`, `settings_get`, `settings_set` |
| `translate`, `automation`, `plugins` | none yet; the tool cards add them. |

- **`search_tools {query?, group?}`** matches words against names, descriptions and groups, ranking words in a name first. It answers up to ten tools with their schemas and offers them from then on.
- **`search_commands {query, window?, limit?}`** reads `commands.list` and keeps the commands whose id or name holds every word.
- **`run_command`** is `commands.run`.

**Placing operations.** `kPlacements` in `catalogue.ts` places every HA1 operation; only `commands.list` (behind `search_commands`) and `api.version` are left out on purpose. A bun test fails when HA1 gains an operation that isn't placed, so each new operation is a decision. Deprecated operations aren't offered.

## Frames

The frame tools are HA1's `video.*` operations ([AI4 #432](https://github.com/altqx/hikari/issues/432)). Frames, ink bounds and colours are worked out in C++, next to the renderer; the sidecar crops and applies the image budget.

- **`video_frame`** answers a PNG image and `{frame, crop, width, height}`. Image point (x, y) is video point (crop.x + x × crop.width / width, crop.y + y × crop.height / height), since a model's coordinates are relative to the image it saw.
  - `subtitles: false` draws none, `line` draws that Line alone, and otherwise every Line is drawn.
  - **Cropped by default.** Without `crop` or `full: true`, the sidecar first asks `video.ink_bounds` for the ink of `line`, or of every Line, and crops to it grown by a quarter of its longer side, 32 px at least. The frame is whole when nothing is drawn, or when the app can't draw apart from the video.
  - The tool takes `full` and leaves out HA1's `maxPixels`, which the image budget sets.
- **`video_ink_bounds`** gives the [ink bounds](../CONTEXT.md) of one Line or every Line on a frame.
- **`video_colours`** samples colours as ASS `&HBBGGRR&`, for `\c` tags, rather than letting the model guess them.
- **`video_keyframes`** lists keyframes in a time range, with their times.

**Image budgets.** `ToolSet.imageBudget` caps `maxEdge` and sets `maxPixels` at the size past which the provider scales an image down or bills more tiles, so no pixel sent is thrown away. `kImageBudgets` in `images.ts` holds one per wire, from the [research note](research/hikari-ai-integration.md#41-what-the-models-take-and-what-it-costs) and each provider's vision guide:

| Wire | Longest edge | Pixels |
| --- | --- | --- |
| `anthropic-messages` | 1,568 | 1,192,464 (1092 × 1092) |
| `responses` | 2,048 | 2,560,000 (2,500 patches of 32 × 32) |
| `chat-completions` | 2,048 | 1,048,576 (768 on the short side of a 16:9 frame) |
| `gemini` | 1,536 | 2,359,296 (four 768 × 768 tiles) |

The Gemini and Chat Completions rows are HikariSub's own caps, derived from each vendor's tiling rules. The vendors don't state them as limits.

The agent sets the budget from the provider's wire. A caller whose provider isn't known, such as an external agent over MCP, gets the smallest of each.

## Documents

A tool that takes `document` works on the editing target when it is omitted. The sidecar's `Caller` has no `documents.all` grant. It sees the editing target and the Documents the user attached with `@`, which `AiSidecarHost::setAttachedDocuments` sets. Anything else gives `not_granted`, which tells the model to ask the user to attach that Document.

## Results and errors

A result is MCP's `CallToolResult`. HA1's result comes back as JSON text, and `video_frame` adds an `image` part. A failure has `isError: true` and a message for the model: HA1's message, with operation names given as tools, followed by what to do next.

| HA1 code | Next step given |
| --- | --- |
| `stale_revision` | The revision now (`data.current`), then: read the Lines again and prepare the edits against it. |
| `not_granted` for a Document | Work on the active Document, or ask the user to attach this one with `@`. |
| `invalid_params` | Check the tool's parameters and call it again. |
| `not_found` | Look the id up again. |
| `busy`, `read_only`, `protected` | Wait, or work on another Document. |

An unknown tool, arguments that aren't an object, or a lost link also come back as text, and never as a JSON-RPC error.

## The link

Qt calls the tools with two `_hikari/*` requests:

| Method | Params | Result |
| --- | --- | --- |
| `_hikari/tools.list` | `{all?}` | `{tools}`: those offered now, or every tool with `all` |
| `_hikari/tools.call` | `{name, arguments}` | MCP's `CallToolResult` |

`list` and `call` have the shapes of MCP's `tools/list` and `tools/call`. The [MCP server](ai-mcp.md) and the agents ([AI7 #435](https://github.com/altqx/hikari/issues/435), [AI15 #443](https://github.com/altqx/hikari/issues/443)) serve the same `ToolSet`.

## Tests

- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/tools`):
  - the generated types are current, and every operation is placed;
  - names fit every provider, the read and write marks are HA1's, and the schemas are self-contained;
  - searches, routing, the worded errors and the PNG frame run against a fake HA1;
  - `video_frame`'s default crop, its margin, `full`, and the image budgets;
  - the two link methods.
- `hikari_backends_ai_tools_tests` runs the real `hikari-ai` against the real API over a fake host, with no provider and no network:
  - a scripted agent finds a Line, edits it, is refused a stale revision, then finds and runs a command;
  - `search_tools` offers `video_frame`;
  - tools reach the active Document and the attached ones only;
  - every tool's mark matches its operation.
