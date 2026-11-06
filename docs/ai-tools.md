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

**The searchable groups** are offered once `search_tools` finds them. A group is listed only once it holds a tool:

| Group | Tools now |
| --- | --- |
| `typeset` | `video_frame`, which answers a PNG image rather than HA1's BGRA pixels. |
| `karaoke` | `audio_peaks` |
| `effects` | `nodes_catalogue` |
| `app` | `panels_list`, `panels_open`, `panels_close`, `panels_focus`, `settings_list`, `settings_get`, `settings_set` |
| `translate`, `automation`, `plugins` | none yet; the tool cards add them. |
| `web` | `web_search`, `web_fetch`, while a search service is chosen ([Web search](#web-search)). |

- **`search_tools {query?, group?}`** matches words against names, descriptions and groups. It answers up to ten tools with their schemas and offers them from then on.
- **`search_commands {query, window?, limit?}`** reads `commands.list` and keeps the commands whose id or name holds every word.
- **`run_command`** is `commands.run`.

**Tool sources.** A `ToolSource` adds tools from outside HA1 as one more group: its `group`, `description`, `tools()` and `call`. `search_tools` lists a source's group, and searches its tools, only while it has tools. The web search is the first source; MCP-server and agent tools plug in the same way ([AI23b #800](https://github.com/altqx/hikari/issues/800)).

**The up-front budget.** The tools offered before any search, with a search service chosen and a provider's own search tools added, must stay within 3,000 tokens (`o200k_base`) on every wire. A bun test measures the `tools` each wire sends. It is at 2,996 on Responses, so a new core tool or a longer description needs room made elsewhere.

**Placing operations.** `kPlacements` in `catalogue.ts` places every HA1 operation; only `commands.list` (behind `search_commands`) and `api.version` are left out on purpose. A bun test fails when HA1 gains an operation that isn't placed, so each new operation is a decision. Deprecated operations aren't offered.

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
  - the two link methods;
  - each search service's requests, key headers, results and errors through a fake relay, and the `web` group appearing only while a service is chosen;
  - the up-front tool block within 3,000 tokens on every wire.
- `hikari_backends_ai_tools_tests` runs the real `hikari-ai` against the real API over a fake host, with no provider and no network:
  - a scripted agent finds a Line, edits it, is refused a stale revision, then finds and runs a command;
  - `search_tools` offers `video_frame`;
  - tools reach the active Document and the attached ones only;
  - every tool's mark matches its operation.
