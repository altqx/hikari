# AI providers

The provider layer of Hikari AI ([ADR 0024](adr/0024-hikari-ai.md), [AI5 #433](https://github.com/altqx/hikari/issues/433)). It sends requests to the providers the user added and streams the answers back. Since [AI5P #652](https://github.com/altqx/hikari/issues/652) it runs in the TypeScript sidecar on the providers' official SDKs ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)). C++ keeps the Settings model: the keychain, the keyless provider file and the role models.

## Where it lives

| Part | Module | What it holds |
| --- | --- | --- |
| `hikari::application::ai` | `hikari_application` | Value types (providers, models, roles, usage, errors, events), `Providers` and the `KeyStore` and `ConfigStore` ports. No Qt. |
| `hikari::backends::ai` | `hikari_ai` | `AiRequests` (requests through the sidecar), `HttpRelay` (the sidecar's network, with the key added), `JsonConfigStore` and `KeychainKeyStore` (QtKeychain). |
| `src/ai-sidecar/src/providers` | the sidecar | One client per wire on `openai`, `@anthropic-ai/sdk` and `@google/genai`, the relayed fetch, the catalogue and the error mapping. |

The core knows nothing of any of it.

## Off until a provider is added

With no provider, `AiRequests::complete` fails at once with `NoProvider`. It never asks for the sidecar, so nothing starts, no key is read and no connection opens. With AI turned off, no `Providers` or sidecar exists at all.

## The sidecar methods

| Method | Params | Result |
| --- | --- | --- |
| `_hikari/providers.set` | `{providers: [{id, preset, wire, baseUrl}], searchService?}` | `null` |
| `_hikari/complete` | `{stream, providerId, request}` | `{reason, text, toolCalls, usage, citations?, searchSuggestions?, providerState?}` |
| `_hikari/models.list` | `{providerId}` | `{models: [{id, name, capabilities?, price?, contextTokens?}]}` |
| `_hikari/conversation.end` | `{providerId, cacheKey}` | `null`; deletes the conversation's explicit cache |
| `_hikari/search.test` | `{service}` | `null`, or the service's error; runs one search |

- `AiRequests` sends `providers.set` with the current config before each request, then the request with the role's model. No key goes to the sidecar ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)).
- The sidecar sends each provider request back to Qt as `_hikari/http.request` with target `{kind: "provider", id}`. `HttpRelay` reads the key from the keychain, puts it where the SDK wrote the `hikari-key` placeholder, and drops that header for a provider without a key.
- While a completion runs, `session/update` notifications carry `{stream, update}` with `text`, `toolCall {index, id, name}`, `toolArguments {index, delta}`, `usage` and `citation {citation}` updates. C++ turns them into `TextDelta`, `ToolCallStarted` and `ToolCallArguments`; the answer becomes `Finished`. Updates that carry a `sessionId` instead belong to an [agent thread](ai-agent.md).
- A failure answers with code `-32010` and `data: {kind, httpStatus, detail}`, which becomes `Failed`. `$/cancel_request` closes the provider's connection and answers `-32800`, which becomes `Failed{Cancelled}`.
- `src/ai-sidecar/src/providers/schema.ts` holds the JSON Schemas of every method's params and results.

## Providers and wires

| Preset | Wire | SDK | Endpoint | Key header |
| --- | --- | --- | --- | --- |
| OpenRouter | Chat Completions | `openai` | `https://openrouter.ai/api/v1` | `Authorization: Bearer` |
| OpenAI | Responses (`store: false`) | `openai` | `https://api.openai.com/v1` | `Authorization: Bearer` |
| Anthropic | Messages | `@anthropic-ai/sdk` | `https://api.anthropic.com/v1` | `x-api-key` |
| Google Gemini | Gemini API (`streamGenerateContent`) | `@google/genai` | `https://generativelanguage.googleapis.com/v1beta` | `x-goog-api-key` |
| Custom | Chat Completions or Responses | `openai` | the user's base URL | `Authorization: Bearer`, or none |

- Custom endpoints cover local servers (Ollama, LM Studio, vLLM) and any OpenAI-compatible service. A custom provider needs no key unless the user gives one.
- Every request streams and ends with exactly one `Finished` (stop reason, the whole text, the tool calls, usage) or `Failed`.
- A user message carries text, images (base64) and tool results. An assistant message carries text and tool calls.
- The SDKs never retry. A Chat Completions stream that ends without a `finish_reason` fails as `Malformed`, even after `[DONE]`.
- No response is cached on this machine. Prompt caching happens on the provider; see [Caching](#caching).

## Google Gemini

The key is a Gemini API key from Google AI Studio ([AI18 #639](https://github.com/altqx/hikari/issues/639)). The client uses `@google/genai` on the Gemini API, not Google's OpenAI-compatible endpoint. Vertex AI, which signs in through Google Cloud, is not covered.

- **Request.** `POST {base}/models/{model}:streamGenerateContent?alt=sse` with the key in `x-goog-api-key`, never in the URL. The body holds `contents` (role `user` or `model`, each with `parts`), `systemInstruction`, `tools[].functionDeclarations` (each with `parametersJsonSchema`) and `generationConfig.maxOutputTokens`. The model goes in the path, not the body.
- **Parts.** Text is `text`. An image is `inlineData` (`mimeType` and base64 `data`), and a whole request with inline data stays under 20 MB. A tool call is `functionCall` (`id`, `name`, `args` as an object). A tool result is `functionResponse` (`id`, the call's `name`, and `response` as `{"result": …}`, or `{"error": …}` when the tool failed).
- **Thought signatures.** A part may carry an opaque `thoughtSignature`. A call's signature is kept in `ToolCall::signature` and sent back, unchanged, on the same `functionCall` part in the history. Thought parts (`thought: true`) are not shown.
- **Calls without an id.** When a `functionCall` has no `id`, the client names it `call_<n>` in the order of the response, and the history carries that id on both the call and its result.
- **Stream.** Each event is a whole `GenerateContentResponse`. A function call arrives whole, so `ToolCallArguments` carries its full arguments at once. The stream has no end marker: the request finishes when the connection closes after a `finishReason`, and fails as `Malformed` when it closes without one.
- **Stop reasons.** `STOP` ends with `End`, or `ToolUse` when a call came. `MAX_TOKENS` and `CONTINUATION` end with `MaxTokens`. `SAFETY`, `RECITATION`, `LANGUAGE`, `BLOCKLIST`, `PROHIBITED_CONTENT`, `SPII`, `IMAGE_SAFETY`, `IMAGE_PROHIBITED_CONTENT`, `IMAGE_RECITATION` and `ESCALATION` end with `Refused`, as does a prompt that `promptFeedback.blockReason` blocked. `MALFORMED_FUNCTION_CALL`, `UNEXPECTED_TOOL_CALL`, `TOO_MANY_TOOL_CALLS` and `MALFORMED_RESPONSE` fail as `Malformed`, `MISSING_THOUGHT_SIGNATURE` as `Rejected`, and `PUP_LIMITED_DISABLED` as `Unauthorized`, each with the `finishMessage`, which the SDK drops and the client reads from the raw stream.
- **Usage.** Input is `promptTokenCount` plus `toolUsePromptTokenCount`. Output is `candidatesTokenCount` plus `thoughtsTokenCount`, since thinking is billed as output. Cached input is `cachedContentTokenCount`, which `promptTokenCount` already includes.
- **Catalogue.** `GET {base}/models?pageSize=1000`, 1000 being the most one page holds. Only models whose `supportedActions` (`supportedGenerationMethods` on the wire) include `generateContent` are listed, under their id without `models/`, with `displayName` and `inputTokenLimit`. The catalogue reports neither capabilities nor prices, so the preset's defaults apply (tools, vision and video), and cost stays unknown.
- **Video.** Every Gemini model takes video input. The `video` capability records that for the vision tools ([AI4 #432](https://github.com/altqx/hikari/issues/432)), and nothing sends video yet.
- **Errors.** HTTP errors carry `{"error": {"code", "message", "status"}}` and map like any other provider's: 401 and 403 to `Unauthorized`, 429 (`RESOURCE_EXHAUSTED`) to `RateLimited`. An error object inside the stream fails the request with its code. Limits apply per Google Cloud project, not per key.

Sources, read on 2026-11-02:
- the [Gemini API v1beta discovery document](https://generativelanguage.googleapis.com/$discovery/rest?version=v1beta), revision 20261101, for `Content`, `Part` (including `thoughtSignature`), `FunctionCall`, `FunctionResponse`, `FunctionDeclaration`, `GenerationConfig`, `Candidate.finishReason`, `PromptFeedback.blockReason`, `UsageMetadata` and `Model`;
- [API reference overview](https://ai.google.dev/api) for the `x-goog-api-key` header;
- [Generating content](https://ai.google.dev/api/generate-content) for `streamGenerateContent` with `alt=sse`;
- [Models](https://ai.google.dev/api/models) for `models.list` and its page size;
- [Image understanding](https://ai.google.dev/gemini-api/docs/image-understanding) for the 20 MB inline limit;
- [Video understanding](https://ai.google.dev/gemini-api/docs/video-understanding) for video input;
- [Pricing](https://ai.google.dev/gemini-api/docs/pricing) for thinking billed as output;
- [Rate limits](https://ai.google.dev/gemini-api/docs/rate-limits) and [API errors](https://ai.google.dev/gemini-api/docs/api-errors) for limits and status codes.

## Web search

The agent can search the web while the request has `webSearch: true` ([AI23 #742](https://github.com/altqx/hikari/issues/742)). `AiRequests::setWebSearch` turns it on with the user's search service, if any. With it off, `AiRequests` drops `webSearch` from every request and sends no `searchService`.

| Preset and wire | How it searches | Sent with `webSearch` |
| --- | --- | --- |
| Anthropic, Messages | its own search and page reading | `web_search_20250305` and `web_fetch_20250910` server tools, five uses each |
| OpenAI, Responses | its own search | the `web_search` tool |
| Google Gemini, Gemini API | Google Search and URL context | `googleSearch` and `urlContext` in the tool with the functions. Beside functions, `toolConfig` asks for `includeServerSideToolInvocations` and `VALIDATED` calling (Gemini 3 and later) |
| OpenRouter, Chat Completions | its own search and page reading | the `openrouter:web_search` and `openrouter:web_fetch` server tools |
| Custom, or a preset on another wire | the user's search service, if one is chosen | nothing; the sidecar offers `web_search` and `web_fetch` through `search_tools` ([AI tools](ai-tools.md#web-search)) |

- **Citations.** Each cited source becomes `{index, url, title, quote?, start?, end?}`, numbered from 1 in the order the answer cites them; a URL cited again keeps its first number. They stream as `citation` updates and come back in `Finished::citations`.
  - `start` and `end` place the citation's chip: the cited part of the final answer text, in UTF-16 code units as Qt holds it, `end` exclusive. They come only in `Finished`, for a URL's first placed citation. Protocol 1 gained them as optional fields, so an older reader ignores them.
  - The sidecar maps each provider's offsets onto the final text. Anthropic cites a whole text block, so its span is that block and the chip goes at its end. OpenRouter's and OpenAI's `start_index` and `end_index` count characters (code points), into the message on Chat Completions and into the annotated text part on Responses. Gemini's segments count UTF-8 bytes and are kept only where they land on the segment's own `text`.
  - An offset past the text or inside a character is dropped, and so is one whose part of the text the sidecar didn't see. Qt keeps them as `Citation::span` only when both are whole numbers, `start < end`, the span fits the text and neither end splits a surrogate pair. Otherwise, and when they are absent, the citation shows in Sources only.
  - Anthropic's `web_search_result_location` gives the quote as `cited_text`.
  - OpenRouter's `url_citation` annotations give `content` as the quote.
  - OpenAI's `response.output_text.annotation.added` gives no quote.
  - Gemini's `groundingSupports` give each cited segment as the quote, then the other `groundingChunks` follow.
- **Google's search suggestions.** Gemini's `searchEntryPoint.renderedContent` comes back as `Finished::searchSuggestions`. Google's terms require it to be shown with a grounded answer.
- **Provider state.** A turn that ran a server tool keeps the raw assistant content in `Finished::providerState` (`{type: "providerState", wire, json}`). Sent back as a part of the assistant message, it replaces that message on the same wire, so Anthropic's `server_tool_use` and result blocks, and Gemini's `toolCall` and `toolResponse` parts, go back unchanged. Another wire ignores it.
- **Limits.** Anthropic's `pause_turn` ends the turn as `End`. A conversation's cache prefix includes whether web search was on.
- **The search service.** `searchService` in `providers.set` names Brave, Tavily, Exa or Firecrawl. The sidecar reaches it through `_hikari/http.request` with target `{kind: "search", id}`. `withSearchTargets` allows only the chosen service while web search is on, or a service while `AiRequests::testSearch` runs, and refuses any other with `Rejected`. The key is the keychain entry `ai-provider/search:<service>` (`SearchKeys`), or the typed key during its test, and it goes where the sidecar wrote `hikari-key`, as for a provider.

| Service | Base URL | Key header |
| --- | --- | --- |
| Brave Search | `https://api.search.brave.com` | `X-Subscription-Token` |
| Tavily | `https://api.tavily.com` | `Authorization: Bearer` |
| Exa | `https://api.exa.ai` | `x-api-key` |
| Firecrawl | `https://api.firecrawl.dev` | `Authorization: Bearer` |

Sources, read on 2026-11-06: Anthropic's [web search](https://platform.claude.com/docs/en/agents-and-tools/tool-use/web-search-tool) and [web fetch](https://platform.claude.com/docs/en/agents-and-tools/tool-use/web-fetch-tool) tools, OpenAI's [web search](https://developers.openai.com/api/docs/guides/tools-web-search), OpenRouter's [server tools](https://openrouter.ai/docs/features/web-search), Google's [grounding with Google Search](https://ai.google.dev/gemini-api/docs/google-search) and the `@google/genai` 2.31.0 types, and the [Brave](https://api-dashboard.search.brave.com/app/documentation/web-search/get-started), [Tavily](https://docs.tavily.com/documentation/api-reference/endpoint/search), [Exa](https://docs.exa.ai/reference/search) and [Firecrawl](https://docs.firecrawl.dev/api-reference/endpoint/search) API references.

## Keys

- Keys live only in the OS keychain through QtKeychain: the Secret Service (libsecret) or KWallet on Linux, the Credential Store on Windows. The entry is `ai-provider/<provider id>` under the service `HikariSub`. The sidecar never holds a key: only Qt's relay adds it to the outgoing request, and it masks the key as `[key]` in what comes back.
- An OpenRouter key can come from signing in ([Settings › AI](ai-settings.md#providers)); it is stored like a pasted one, and the provider file marks it `signedIn`. HikariSub's ChatGPT sign-in for Codex is the entry `ai-provider/codex-chatgpt`. Qt runs both sign-ins and the Codex refresh itself, and writes the tokens straight to the keychain; the sidecar never sees them ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md#keys)).
- A provider is added only once its key is in the keychain. With no keyring daemon, or when the user refuses access, it isn't added, and the caller gets `KeychainUnavailable` or `KeychainDenied` to tell the user. There is no plain-text fallback.
- Every SDK client sends through the relay. Qt sends a request only to a URL under the provider's base URL, never follows a redirect, keeps no cookies and honours the system proxy.
- A key travels only over HTTPS, or over plain HTTP to this machine (`localhost`, `127.0.0.0/8`, `::1`) or to a Tailscale peer: an address in `100.64.0.0/10` or `fd7a:115c:a1e0::/48` while this machine has an address in the same range (the IPv4 range is also ISPs' CGNAT space). A host name, `*.ts.net` included, counts only by the address it resolves to: it is resolved once and the request goes to the address that passed, with the name kept in `Host`, so DNS can't send the key elsewhere. Anything else, a LAN address included, fails with `InsecureKey` before a connection opens.
- The provider file, the sidecar's answers and the logs never hold a key. The tests check each.

## Models and roles

- `AiRequests::refreshModels` asks the sidecar for the provider's catalogue and `Providers::setModels` stores it. `AiRequests::probe` lists the catalogue of a provider that isn't stored yet, with the key the user typed, for Test connection in [Settings › AI](ai-settings.md); nothing is saved.
- `Providers::update` changes an added provider in place, keeping its key unless given a new one, and `Providers::unassign` sends a role back to the chat model. Capabilities (tools, vision) and prices come from the catalogue where it reports them (OpenRouter does). Otherwise the preset's defaults apply: tools and vision for OpenAI and Anthropic, tools, vision and video for Gemini, neither for OpenRouter's unlisted models and custom endpoints. The user's own correction for a model wins over both.
- The roles are chat, translator, reviewer, vision and judge. A role without its own model uses the chat model. The first catalogue to arrive fills the chat role while it is empty, whether it came with the provider or from a later refresh.

## Usage and cost

`Usage` holds input and output tokens, and within the input the cache reads (`cachedInputTokens`), the cache writes (`cacheWriteTokens`) and the part of the writes kept for an hour (`cacheWriteHourTokens`). Cost is the provider's own figure when it reports one (OpenRouter's `usage.cost`). Otherwise `costOf` prices reads at `Price::cachedInput`, writes at `Price::cacheWrite`, one-hour writes at `Price::cacheWriteHour` and the rest at `Price::input`, each falling back to the input price; without a price, cost is unknown. OpenRouter's catalogue gives all three cache prices (`input_cache_read`, `input_cache_write`, `input_cache_write_1h`). Caps, the meter and estimates belong to the agent.

## Caching

Prompt caching runs on the provider the user chose, so long agent sessions and repeated translation and review passes don't pay full price for the same prefix every turn ([AI19 #640](https://github.com/altqx/hikari/issues/640)).

- **Stable prefix.** A request goes out as the system prompt, the tools sorted by name, the request's `context` (pinned material such as the document summary or the style list), then the conversation. Nothing that changes per turn (timestamps, ids, the playhead) belongs in the first three. The sidecar builds every body the same way each turn, so the same prefix is the same bytes.
- **Anthropic.** Two `cache_control` breakpoints, within Anthropic's limit of four: one on the last block of the stable prefix (the context block, else the system block, else the last tool), and one on the last block of the last message. The system prompt and context go as `system` text blocks. `cacheLifetime: "minutes"` sends `{"type": "ephemeral"}` (five minutes), and `"hour"` adds `"ttl": "1h"`. A prompt shorter than the model's minimum is sent without caching and without an error. Reads are `cache_read_input_tokens`; writes are `cache_creation_input_tokens`, of which `cache_creation.ephemeral_1h_input_tokens` were for an hour.
- **OpenAI.** Caching is automatic. The request's `cacheKey` goes out as `prompt_cache_key`, the same for every turn of one thread, which keeps the turns on one cache. The context goes as the first `developer` input. Reads are `input_tokens_details.cached_tokens`, and writes are `input_tokens_details.cache_write_tokens`, which newer models charge for.
- **OpenRouter.** `cacheKey` goes out as `prompt_cache_key` for sticky routing. For `anthropic/` and `google/` models, which need explicit markers, `cache_control` goes on the last system block and on the last part of the latest user message. Other models cache on their own. Reads are `prompt_tokens_details.cached_tokens`, and writes are `prompt_tokens_details.cache_write_tokens`.
- **Google Gemini, implicit.** Implicit caching is on by default for Gemini 2.5 and newer, above a minimum prompt size that depends on the model. The system prompt and context go as `systemInstruction` parts and the tools are sorted, ahead of `contents`, so a repeated prefix hits the cache. Reads come back as `cachedContentTokenCount`.
- **Google Gemini, explicit.** A request with `explicitCache` and a `cacheKey` is for a large fixed context reused over many turns, such as a whole script for translation.
  - **Create.** The sidecar first stores the system prompt, tools and context with `caches.create` (`POST {base}/cachedContents`, with `model` and a `ttl` of `300s`, or `3600s` for `"hour"`).
  - **Use.** Each request then sends `cachedContent` and the conversation only, without `systemInstruction` or `tools`.
  - **Reuse and replace.** The cache is reused while the provider, model and prefix stay the same and more than 30 seconds remain. It is deleted and made again when the prefix changes or it runs out, and dropped when a request on it fails.
  - **Delete.** `_hikari/conversation.end` (`AiRequests::endConversation`) deletes it when the thread ends. When the provider is removed, the sidecar forgets the cache and it expires on its own, since deleting it would need the key.
  - **Fallback.** When the provider refuses to make the cache (a 4xx: a prefix below the minimum, or a model without explicit caches such as Gemini 4 Argon), the whole request goes out and that prefix isn't tried again. Any other failure sends the whole request and tries again on the next turn.
  - **Storage cost.** Google bills cache storage per hour, and that cost is not in `Usage`.
  - **Explicit caches are generateContent-only.** They aren't offered for Gemini 4 Argon and later, nor in Google's newer Interactions API. There a turn can chain on the server with `previous_interaction_id`, which needs the interaction stored (the default, kept up to 55 days); `store: false` turns storage off and with it the chaining, and only implicit caching applies. Hikari uses generateContent and stores nothing on Google beyond the explicit cache a request asks for.
- **Custom endpoints** get nothing extra: no cache key, no markers, no explicit caches.
- **Turned off.** Settings › AI › Use prompt caching (`ai.promptCaching`, on by default) is the switch. With it off, `AiRequests` sends every request with `caching: false`, and the sidecar drops the cache key, the explicit cache and every marker. OpenAI and Gemini's implicit cache still work on their own, which no request can turn off.
- **Privacy.** Caching stays on the provider the user chose. Nothing new is stored on this machine, and no answer is replayed from a local cache.

Sources, read on 2026-11-02 (Google's caching and Interactions pages again on 2026-11-03): Anthropic's [prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching), OpenAI's [prompt caching](https://developers.openai.com/api/docs/guides/prompt-caching), OpenRouter's [prompt caching](https://openrouter.ai/docs/features/prompt-caching) and its [models list](https://openrouter.ai/api/v1/models) for the price fields, Google's [context caching](https://aistudio.google.com/docs/caching), [caching API](https://ai.google.dev/api/caching), [Interactions API](https://aistudio.google.com/docs/interactions-overview) and [pricing](https://ai.google.dev/gemini-api/docs/pricing), and `CachedContent` in the [Gemini API v1beta discovery document](https://generativelanguage.googleapis.com/$discovery/rest?version=v1beta).

## Errors

| Kind | When |
| --- | --- |
| `NoProvider`, `NoModel` | Nothing added, or no model for the role |
| `Key`, `InsecureKey` | The key can't be read, or would go over plain HTTP to another machine |
| `Unauthorized`, `RateLimited`, `Rejected`, `Server` | HTTP 401/403, 429, other 4xx, 5xx, or the same reported inside the stream |
| `Network`, `Timeout`, `Malformed` | No connection, nothing received for two minutes, or a stream that is broken or ends before it finishes |
| `Cancelled` | The caller cancelled |

`detail` carries the provider's own message, to show as is.

## The provider file

`JsonConfigStore` writes the providers, their catalogues, the user's capability corrections and the role models as JSON, atomically. It holds no keys.

## Tests

- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/providers`): a local mock server for each wire, Gemini included, covering streaming split at awkward bytes, tool calls, vision and history encoding, cache markers and keys on the wire, a stable prefix that is byte-identical across turns, cache reads and writes in usage, the Gemini explicit cache lifecycle, native search tools per wire with citations, suggestions and provider state, each wire's citation offsets landing on the right characters of a Thai and emoji answer, found tools after the stable prefix and as `tool_reference` blocks on Anthropic, HTTP and in-stream errors, safety blocks, early ends, timeouts, cancellation, catalogues, the key rules and the JSON-RPC methods against their schemas.
- `hikari_application_ai_providers_tests`: role fallback, capabilities, cost, keychain failures, removal, the catalogue, a Gemini model in every role, and search service ids and keys.
- `hikari_backends_ai_requests_tests`: requests through a fake sidecar: no provider starts nothing, the model reaches the sidecar and no key does, the relay reaches a provider with its keychain key, errors keep their kind, `webSearch` reaches the sidecar only while it is on, citations, suggestions and provider state come back, a citation span lands on its characters in Thai and emoji text and a span that does not fit is dropped, and a search test reaches its service only while it runs, with the typed key when there is one.
- `hikari_backends_ai_http_relay_tests`: the relay against a scripted local server: the key is added and masked, an SSE event passes before the next arrives, keyless providers, the key rules, pinned names, URLs outside the base, redirects, dropped headers, unknown targets and cancellation; only the chosen search service, or one under test, is reachable, with its own or the typed key and its HTTPS host.
- `hikari_backends_ai_relay_sidecar_tests`: the real sidecar, sandboxed, lists models through the relay with a key only Qt holds.
- `hikari_backends_ai_config_store_tests`: the provider file keeps everything but the key.

No test talks to a real provider.
