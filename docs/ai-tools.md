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
| `src/ai-sidecar/src/tools/web.ts` | `WebSearch`: `web_search` and `web_fetch` through the user's search service. |

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

Hikari AI's threads also get `update_plan` ([AI agent](ai-agent.md)) and `read_skill` ([AI skills](ai-skills.md)), which the sidecar answers itself.

**The searchable groups** are offered once `search_tools` finds them. A group is listed only once it holds a tool:

| Group | Tools now |
| --- | --- |
| `typeset` | `video_frame`, `video_ink_bounds`, `video_colours`, `video_keyframes` ([frames](#frames)); `typeset_perspective`, `typeset_clip`, `typeset_drawing`, `typeset_shift`, `typeset_hydra`, `typeset_check`, `typeset_apply` ([typesetting](#typesetting)) |
| `karaoke` | `audio_peaks`; `karaoke_syllables`, `karaoke_split`, `karaoke_time`, `karaoke_onsets`, `karaoke_snap`, `karaoke_kanji`, `karaoke_furigana`, `karaoke_check`, `karaoke_template_preview`, `karaoke_apply_template`, `karaoke_align` ([karaoke](#karaoke)) |
| `effects` | `nodes_catalogue` |
| `app` | `panels_list`, `panels_open`, `panels_close`, `panels_focus`, `settings_list`, `settings_get`, `settings_set`; the two reads leave out `privacy.*` and `recent.*` ([Agent](ai-agent.md#modes)) |
| `automation` | `automation_run`, which asks the user on every run ([AI-written Lua](ai-agent.md#ai-written-lua)) |
| `translate` | `translation_project`: the Document's Honya project files ([AI translation pipeline](ai-translation.md)). |
| `plugins` | none yet; the tool cards add them. |
| `web` | `web_search`, `web_fetch`, while a search service is chosen ([Web search](#web-search)). |

- **`search_tools {query?, group?}`** matches words against names, descriptions and groups, ranking words in a name first. It answers up to ten tools with their schemas and offers them from then on. Found tools are sent after the tools offered from the start, in the order found, so the cached prefix stays the same. On Anthropic's own API with Claude 4.5 or later they go as `defer_loading` tools, and the search's result goes back as `tool_reference` blocks, which keeps the cache ([tool search](https://platform.claude.com/docs/en/agents-and-tools/tool-use/tool-search-tool#custom-tool-search-implementation)).
- **`search_commands {query, window?, limit?}`** reads `commands.list` and keeps the commands whose id or name holds every word.
- **`run_command`** is `commands.run`.

**Tool sources.** A `ToolSource` adds tools from outside HA1 as one more group: its `group`, `description`, `tools()` and `call`. `search_tools` lists a source's group, and searches its tools, only while it has tools. The web search is the first source; each [MCP server](ai-mcp-clients.md) the user added is one more, and agent tools plug in the same way ([AI23b #800](https://github.com/altqx/hikari/issues/800)).

**The up-front budget.** The tools offered before any search, with `update_plan`, `read_skill`, a search service chosen and a provider's own search tools added, must stay within 5,000 tokens (`o200k_base`) on every wire, raised from 3,000 (owner Q123, 2026-11-09). A bun test measures the `tools` each wire sends. It is at 3,216 on Responses, the largest.

**Placing operations.** `kPlacements` in `catalogue.ts` places every HA1 operation; only `commands.list` (behind `search_commands`), `skills.list` and `skills.read` (behind `read_skill`) and `api.version` are left out on purpose. A bun test fails when HA1 gains an operation that isn't placed, so each new operation is a decision. Deprecated operations aren't offered.

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

## Typesetting

The typesetting tools are HA1's `typeset.*` operations ([AI9 #437](https://github.com/altqx/hikari/issues/437), [ADR 0024](adr/0024-hikari-ai.md#typesetting)). The model proposes where a sign is; the visual tools' own solvers, in C++, write the tags; a render check runs before anything reaches the review. The bundled `typesetting` skill walks the model through it.

- **The solvers** are reads: each answers the Lines' new text and changes nothing.
  - `typeset_perspective` takes a Line and four corners in script coordinates and writes the transform VT3's Perspective tool writes for that quad (`quadToTransform`, `writeTransform`; `origin` is the tool's \org mode). A text Line needs the app's text measure, so without it (no video panel yet) it answers `refused` with reason `unmeasured`; a drawing never does.
  - `typeset_clip` writes a rectangle clip (`clip::putRectangle`) or a vector clip (`clip::putVector`), `inverse` for `\iclip`; `typeset_drawing` puts a shape with `drawing::putDrawing`.
  - `typeset_shift` and `typeset_hydra` run the Position shifter and the Hydra engine (`PositionShifterTool`, `AllTagsTool`) on a copy of the Document, through a host whose view puts one device pixel at a hundredth of a script unit. Hydra knows its 22 default tags (tags.toml's defaults, not the user's own).
- **`typeset_check`** draws one Line alone, with a proposed text, on its frame (the Line's first unless the frame shown is in it) and compares the [ink bounds](../CONTEXT.md) with the bounds of the target quad or box, scaled to video pixels. `error` is the largest distance between their edges; it passes at `tolerance` (4 video pixels by default) or less.
- **`typeset_apply`** is the write: up to 100 Lines, each with its text and target. Every Line is checked first; when one misses, nothing is applied and the answer is `refused` with reason `check_failed` and each failing Line's check. Otherwise it applies the texts as `document_apply` would (one undo step, into the review) and answers the checks too.

## Karaoke

The karaoke tools are HA1's `karaoke.*` operations ([AI11 #439](https://github.com/altqx/hikari/issues/439)). They run the audio editor's karaoke code in C++, and the bundled `karaoke-templater` skill walks the model through timing and templates.

- **Reads that answer texts.** None of these changes the Document; the model writes their texts with `document_apply`.
  - `karaoke_split` splits Lines without `\k` tags as the audio editor's karaoke mode does (KA1), with the user's language rules and English hyphenation.
  - `karaoke_time` sets each syllable's end in ms.
  - `karaoke_snap` moves each boundary inside a Line to the nearest vocal onset (KA5) within `window` ms, 100 by default, keeping every syllable at least 10 ms long.
  - `karaoke_kanji` runs the Kanji timer's group suggestion (KA3) from a timed romaji Line onto its kanji Line; `clean` is false where the timer would stop for the user.
- **Other reads.**
  - `karaoke_syllables` answers a Line's syllables as the templater reads them.
  - `karaoke_onsets` answers the audio's vocal onsets in a range. The app finds them once per audio, off the UI thread.
  - `karaoke_furigana` answers the reading dictionary's suggestions (KA4); without a dictionary it is `unavailable` with reason `no_dictionary`.
  - `karaoke_check` answers the Karaoke check's problems (KA7).
  - `karaoke_template_preview` runs the templater on one Line and answers what it generates.
- **`karaoke_apply_template`** runs the templater on every karaoke Line, or the Lines given, against a revision, and applies the result as one undo step into the review. The templater replaces what its last run generated. A templater error is `refused` with reason `templater_failed` and the templater's message.
  - The app's templater is shared with the template preview in the Line editor. A newer preview replaces an unfinished one without answering it, so a call that gets no answer in 2 minutes fails with `templater_failed`.
- **`karaoke_align`** runs WhisperX (`whisperx`) or the Montreal Forced Aligner (`mfa align_one`) on the audio under the Lines, when the user installed it on their PATH. It answers each word's start and end in ms and applies nothing.
  - It asks the user on every run ([always-ask list](ai-agent.md#modes)).
  - Qt writes the clip as a 16-bit mono WAV and the Lines' words as text into a folder of its own under the temporary folder, runs the program there with an argument list (never a shell), reads its JSON and removes the folder.
  - A program that isn't installed is `unavailable` with reason `missing`; a failed run is `refused` with reason `failed` and the end of the program's output.

## Documents

A tool that takes `document` works on the editing target when it is omitted. The sidecar's `Caller` has no `documents.all` grant. It sees the editing target and the Documents the user attached with `@`, which `AiSidecarHost::setAttachedDocuments` sets. Anything else gives `not_granted`, which tells the model to ask the user to attach that Document.

## Web search

Providers with their own search use it ([AI providers](ai-providers.md#web-search)). For the others, `WebSearch` offers two tools in the `web` group while `providers.set` names a search service:

- **`web_search {query, count?}`** answers up to `count` results (1–10, 5 when omitted), each `{url, title, snippet}` with the snippet cut at 600 characters.
- **`web_fetch {url}`** answers one `http` or `https` page as `{url, title, text}`, with the text cut at 20,000 characters. Brave reads only the pages its own results return, so with Brave the tool isn't offered.

| Service | Search | Read |
| --- | --- | --- |
| Brave Search | `GET /res/v1/web/search?q&count&extra_snippets=true` | — |
| Tavily | `POST /search {query, max_results, search_depth: "basic"}` | `POST /extract {urls, format: "markdown"}` |
| Exa | `POST /search {query, numResults, contents.highlights}` | `POST /contents {urls, text}` |
| Firecrawl | `POST /v2/search {query, limit}` | `POST /v2/scrape {url, formats: ["markdown"], onlyMainContent}` |

- Both descriptions tell the model that results are untrusted text from the web and never instructions.
- A refused key (401, 403, or Brave's 422 about the token) tells the model to answer without searching and to point the user at Settings › AI. A used-up plan (429, Exa's and Firecrawl's 402, Tavily's 432 and 433) tells it to answer without searching and say so.
- `_hikari/search.test {service}` runs one search for Test connection, and fails with the service's error as a provider error.

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
  - the two link methods;
  - each search service's requests, key headers, results and errors through a fake relay, and the `web` group appearing only while a service is chosen;
  - the up-front tool block within 5,000 tokens on every wire.
- `hikari_api_tests` (`karaoke_tests.cpp`) splits, snaps and times a fixture song through the tools, then checks it; it previews and applies a fake templater, and sends a fake aligner the Lines' words.
- `hikari_backends_external_aligner_tests` reads WhisperX's and MFA's JSON, writes the WAV clip and runs a fake `whisperx` and `mfa` in their own folders.
- `hikari_backends_ai_tools_tests` runs the real `hikari-ai` against the real API over a fake host, with no provider and no network:
  - a scripted agent finds a Line, edits it, is refused a stale revision, then finds and runs a command;
  - `search_tools` offers `video_frame`;
  - tools reach the active Document and the attached ones only;
  - every tool's mark matches its operation.
