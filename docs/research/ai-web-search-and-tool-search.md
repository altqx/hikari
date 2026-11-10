# AI: web search, page reading and tool search

Research step for [AI23 #742][AI23]: what each wire offers natively for web search, page reading and tool search, what it costs, how it cites, and how it interacts with prompt caching and `store: false`. It closes with a token budget for the tools sent up front, as the owner asked under Q46.

Sources:

- Vendor documentation and pricing pages, all fetched on **2026-11-06** (listed at the end). Where a page doesn't say something, this note says "not stated".
- HikariSub `origin/main` at `3002430ea`: `src/ai-sidecar/src/tools/`, `src/ai-sidecar/src/providers/`, [ADR 0026][ADR 0026], [ADR 0029][ADR 0029], [ai-tools.md](../ai-tools.md).
- A **measurement** of the up-front tool block, taken with `o200k_base` (OpenAI's tokenizer) on each wire's JSON shape ([Method](#method)). Anthropic and Gemini count tokens differently, so their figures are estimates.

## Summary

| Wire | Web search | Page reading | Tool search | Keeps caching? |
| --- | --- | --- | --- | --- |
| Anthropic Messages | Native `web_search`, $10 / 1k searches | Native `web_fetch`, tokens only | Native, also as a custom `tool_reference` result | Tool search: yes. Turning web tools on or off: no |
| OpenAI Responses | Native `web_search`, $10 / 1k calls + content tokens | None (the search tool reads pages itself) | Native `tool_search`, gpt-5.4 and later | Yes, until the loaded set changes |
| OpenAI Chat Completions | Only `gpt-5-search-api`, which always searches | None | None | n/a |
| Gemini | Google Search grounding, $14 / 1k queries after 5,000 a month (Gemini 3+) | URL context, tokens only | None | Explicit caches only on Gemini 3 and 2.5; search with a cache: not stated |
| OpenRouter (Chat Completions) | `openrouter:web_search` server tool (the `web` plugin / `:online` is deprecated) | `openrouter:web_fetch` | `deferred_tools` on Chat Completions; `openrouter:tool_search` only on Responses and Messages | Tool search: "preserved". Search: not stated |
| Any wire without native search | A key-based service through Qt's relay: Brave, Tavily, Exa or Firecrawl | Qt fetches only pages a search returned, or Firecrawl scrape | Hikari's own `search_tools` | Yes, when found tools join after the cached prefix |

**Recommendations:**

1. **Keep Hikari's `search_tools` as the one tool search on every wire.** Native tool search exists on Anthropic, OpenAI Responses (gpt-5.4 and later) and OpenRouter, but each has its own shape and limits. `search_tools` already works everywhere and can keep caching if found tools are added after the cached prefix (see [Tool search](#tool-search)). Use Anthropic's `tool_reference` return as an optimisation there only, since it is documented to keep the cache.
2. **Web search: native where the wire has it, else the user's service.** Anthropic `web_search_20250305` (the ZDR-eligible version), OpenAI Responses `web_search`, Gemini `google_search`, OpenRouter `openrouter:web_search`. Chat Completions providers and custom endpoints use the service the user adds a key for, through a new `search` target kind in Qt's relay ([ADR 0029][ADR 0029] reserves this).
3. **Web search and web fetch are found through `search_tools`, not sent up front,** as the owner asked under Q46. A native search tool is a server tool, though, so it can't be "found" mid-conversation without changing `tools`. On Anthropic, adding it invalidates the system and message caches. So the per-conversation globe toggle decides whether the native tool is in `tools` **from the first turn**. Mid-conversation, toggling costs one cache miss, and the panel says so.
4. **Up-front budget: 3,000 tokens for Hikari's tools,** measured as below. The owner raised it to 5,000 (Q123, 2026-11-09). Today's core block is about 2,900, of which `document_apply`'s schema is 1,069. Every web, MCP-server and agent tool is found on demand.
5. **Citations: one shape in the sidecar.** Each wire's citation fields map to `{index, url, title, quote?}`, and the panel shows numbered links. Raw search results are kept only for the turn, which also meets Brave's terms.

## Web search

### Anthropic

| | Web search | Web fetch |
| --- | --- | --- |
| Price | "$10 USD per 1,000 searches, plus standard token costs for search-generated content". Results are input tokens in later turns too. Failed searches aren't billed. | "no additional cost". The fetched content is billed as tokens; the docs estimate 10 kB ≈ 2,500 tokens and 500 kB PDF ≈ 125,000. Cap with `max_content_tokens`. |
| Citations | Always on. `web_search_result_location {url, title, encrypted_index, cited_text}`; `cited_text` is up to 150 characters. | Off by default; `citations.enabled` turns on `char_location {document_index, document_title, start_char_index, end_char_index, cited_text}`. |
| Raw results | `web_search_result {url, title, page_age, encrypted_content}`. `encrypted_content` must be sent back unchanged in later turns. No plain-text snippet. | The whole `document` (text, or base64 for a PDF) and `retrieved_at`. |
| ZDR | `web_search_20250305` is eligible. `_20260209` and later need `allowed_callers: ["direct"]`; dynamic filtering is never eligible. | Same rule. Not HIPAA-eligible. "Website publishers might retain request data." |
| Models | Claude 4.5 and later; dynamic filtering 4.6 and later. | Same. No pages that need JavaScript, and only URLs already in the conversation. |

Excerpt from the docs:

```json
{"type": "web_search_result_location", "url": "https://en.wikipedia.org/wiki/Claude_Shannon",
 "title": "Claude Shannon - Wikipedia", "encrypted_index": "Eo8BCioIAhgB...",
 "cited_text": "Claude Elwood Shannon (April 30, 1916 – ..."}
```

**Caching.** "Toggling web search" invalidates the system and message caches, though another page says modifying tool definitions invalidates the whole cache. Server-tool results get an automatic 5-minute breakpoint, but only when the request already has a `cache_control` marker.

**Overhead.** Any tool adds a tool-use system prompt: 286 tokens on Opus and Sonnet 5.5, 496–497 on 4.5 and 4.6, 675 on Opus 4.7. No extra overhead is stated for web search or fetch.

### OpenAI

- **Responses `web_search`:** "$10.00 / 1k calls + Search content tokens billed at model rates". The legacy `web_search_preview` on non-reasoning models is $25 / 1k with free content tokens. gpt-4o-mini and gpt-4.1-mini bill "a fixed block of 8,000 input tokens per call". `search_context_size` is low, medium or high; the search context is "limited to 128k".
- **Citations:** `url_citation {start_index, end_index, url, title}` on the output text. Apps "must show inline citations as prominent, clickable links".
- **Raw results:** `include: ["web_search_call.action.sources"]` returns "the complete list of URLs the model consulted". Titles and snippets in that list: not stated.
- **`store: false` and ZDR:** under ZDR, `store` is always false. Whether web search is ZDR-eligible: not stated. Live search isn't HIPAA-eligible; `external_web_access: false` (cache only) can be, with a ZDR project.
- **Caching with web search:** not stated.
- **Chat Completions:** only `gpt-5-search-api` searches, and it always searches before answering, with no source list or domain filters. The `gpt-4o(-mini)-search-preview` models shut down on 2026-07-23. So Chat Completions has **no tool-style search**; Hikari treats an OpenAI provider on Chat Completions like a custom endpoint.

### Gemini

The docs have moved to aistudio.google.com and default to the Interactions API; the generateContent version is at `?generate-content=true`.

- **Google Search:** Gemini 3 and later bill "each search query": 5,000 free a month, then $14 / 1k, capped at 1,500 requests a day. Gemini 2.5 bills per grounded prompt: $35 / 1k after 1,500 free a day.
- **URL context:** billed as input tokens. Up to 20 URLs per request and 34 MB per URL. No paywalled pages, YouTube or Workspace files. `urlMetadata[].urlRetrievalStatus` is SUCCESS, ERROR, PAYWALL or UNSAFE.
- **Citations:**

  ```json
  "groundingChunks": [{"web": {"uri": "https://vertexaisearch.cloud.google.com/...", "title": "aljazeera.com"}}],
  "groundingSupports": [{"segment": {"startIndex": 0, "endIndex": 85, "text": "Spain won..."}, "groundingChunkIndices": [0]}]
  ```

  The `uri` is a Google redirect and the `title` is the domain; there are no snippets. `webSearchQueries` lists the queries. The docs call the `searchEntryPoint.renderedContent` search-suggestion widget "required" under the terms. **This needs an owner decision:** showing Google's widget in the agent panel, or not offering Gemini's search.
- **With function calling:** preview, Gemini 3 and 4 only. The request must set `include_server_side_tool_invocations` and send back every part (`toolCall`, `toolResponse`, `thought_signature`), and the mode must be VALIDATED; "AUTO mode is not supported".
- **Explicit caches:** `CachedContent` fixes `tools`, `toolConfig` and `systemInstruction`. Explicit caching "is not supported for Gemini 4 Argon … and onward", so Hikari's `cachedContent` path only covers Gemini 3 and 2.5. Whether a cached request may add `google_search` or `url_context`: not stated.
- **Overhead:** reported as `toolUsePromptTokenCount`. Retention: not stated.

### OpenRouter

- **`:online`** is "exactly equivalent to" `plugins: [{"id": "web"}]`, and the server-tools page says "The Web Search plugin is deprecated". New code uses the `openrouter:web_search` server tool (beta).
- **Engines:** native for OpenAI, Anthropic, Google, Perplexity and SpaceXAI models; otherwise Exa.
- **Price:** Exa $7 / 1k requests (auto, fast), $12 (deep), $15 (deep reasoning), 10 results included, then $1 per 1k extra results. Native engines are passed through at the provider's price. Firecrawl $0.005 per search plus $0.0125 per result. `openrouter:web_fetch`: $1 / 1k with Exa, free with the model's own engine.
- **Citations:** `annotations[] {type: "url_citation", url_citation: {url, title, content?, start_index, end_index}}`; `content` is "Added by OpenRouter if available". No separate list of raw results.
- **ZDR:** ZDR settings "do not disable server tools or plugins"; only the Firecrawl engine is refused under ZDR.

### Search services a user adds a key for

| | Brave | Tavily | Exa | Firecrawl |
| --- | --- | --- | --- | --- |
| Host for Qt's relay | `api.search.brave.com` | `api.tavily.com` | `api.exa.ai` | `api.firecrawl.dev` |
| Price per 1k queries | $5 | $8 basic, $16 advanced | $7 auto/fast, $4 lite; contents $1 / 1k pages | 2 credits per 10 results; plans from $19 for 5,000 credits |
| Free tier | $5 of credit a month | 1,000 credits a month | $10 a month | 1,000 credits a month |
| Result fields | `title, url, description, extra_snippets` | `title, url, content, score, raw_content, favicon` | `title, url, publishedDate, text, highlights, summary` | `url, title, description`, plus `markdown` when scraping |
| Page text | LLM Context endpoint, 1k to about 33k tokens | `include_raw_content`, `/extract` | `contents` | `scrapeOptions`, `/scrape` |
| Terms | No storage "other than transient storage"; no AI training; attribution optional | Not stated | ZDR only on Enterprise | Scrape cache `maxAge` 2 days by default; ZDR only on Enterprise |

The Brave pages disagree on the free credit (the header says "up to $20", each plan says $5) and on storage (the FAQ allows it "with a plan that includes storage rights", the terms forbid it except under an Order Form). Hikari keeps results only for the turn, which meets the stricter reading.

## Tool search

**Measured up-front cost** ([Method](#method)). There are 31 tools in the catalogue, and 20 core tools are offered up front:

| Block | Anthropic | OpenAI Chat | OpenAI Responses | Gemini |
| --- | --- | --- | --- | --- |
| Core (20 tools) | 2,862 | 2,966 | 2,986 | 2,887 |
| All 31 tools | 3,610 | 3,770 | 3,801 | 3,646 |

Of the core block, descriptions are 381 tokens and schemas 2,274. `document_apply`'s schema alone is 1,069, then `lines_find` 270, `lines_get` 146, `lines_list` 126 and `selection_set` 125. Anthropic adds its tool-use system prompt (286 tokens on 5.5) on top.

**Native tool search, per wire:**

- **Anthropic.** Deferred tools are "not included in the system-prompt prefix" and are "appended inline as a `tool_reference` block", so "prompt caching is preserved". Every deferred definition is still sent in `tools` on every request, a deferred tool can't carry `cache_control` (that is a 400), and at least one tool must stay undeferred. A custom tool may return `[{"type": "tool_reference", "tool_name": "x"}]` to load tools. Up to 10,000 deferred tools; Opus 4.1 and earlier aren't supported. It is ZDR-eligible.
- **OpenAI Responses.** `{"type": "tool_search"}` with `defer_loading: true`, hosted or client-side (`tool_search_call` / `tool_search_output`), on gpt-5.4 and later. Loaded tools go "at the end of the model's context window", but "changing the loaded tool set will break the model's cache from that point forward". Price and ZDR: not stated.
- **OpenRouter.** `openrouter:tool_search` works on Responses and Messages only; Chat Completions returns a 400 and must use `deferred_tools`, with the state carried in `reasoning_details`. Caching is "preserved across a search".
- **Gemini and Chat Completions providers:** none.

**Recommendation.** Keep `search_tools` as the one mechanism, so MCP-server tools and AI22's agent tools are found the same way on every wire:

- Tools it finds are appended **after** the cached prefix, in the turn they are found, so the prefix of system prompt and core tools stays byte-identical. That matches how Responses and Anthropic place loaded tools.
- On Anthropic, `search_tools` returns `tool_reference` blocks instead of schemas in text, with the found tools marked `defer_loading`. This is the documented path that keeps the cache.
- Native hosted tool search (OpenAI's and OpenRouter's) isn't used: its benefit over `search_tools` is small at Hikari's catalogue size, and it would make the loaded set depend on the wire.
- **Budget: 3,000 tokens for the up-front block** (`o200k_base`), enforced by a sidecar test; 5,000 since owner Q123 (2026-11-09). Shrinking `document_apply`'s schema is the main lever if MCP tools later need room up front; it isn't needed for AI23.

## Effect on "What is sent"

With web search on, the provider (or the user's search service) receives the queries the model writes, and page reading sends the URLs. Hikari sends no document text to a search service: only the query. Both join "What is sent" as the owner asked under Q44.

## Questions for the owner

1. **Gemini's search suggestions.** Google's terms call the `searchEntryPoint` widget required. Show it under Gemini answers in the agent panel, or leave Gemini without search?
2. **Anthropic's newer web tools.** Use only `web_search_20250305` and `web_fetch` with `allowed_callers: ["direct"]`, so they stay ZDR-eligible, and give up dynamic filtering?
3. **Turning search on mid-conversation.** It costs one cache miss on Anthropic. Say so next to the globe toggle, or say nothing?
4. **OpenAI on Chat Completions.** These have no tool-style search. Use the user's search service for them, as for custom endpoints?

## Method

`out/ai23/tok/measure.ts` (not committed) builds the core and full tool lists with the sidecar's own `ToolSet`, shapes them as each wire's client does (`input_schema` for Anthropic; `{type: "function", function}` for Chat Completions; flat functions with `strict: false` for Responses; `functionDeclarations` with `parametersJsonSchema` for Gemini), sorted by name as `sortedTools` does, and counts the JSON with `js-tiktoken` 1.0.21's `o200k_base`.

## Sources

All fetched 2026-11-06.

- Anthropic: [web search tool][a-search], [web fetch tool][a-fetch], [tool search tool][a-toolsearch], [tool use with prompt caching][a-cache], [server tools][a-server], [pricing][a-pricing].
- OpenAI: [web search][o-search], [tool search][o-toolsearch], [your data][o-data], [pricing][o-pricing].
- Gemini: [Google Search][g-search], [URL context][g-url], [tool combination][g-combo], [caching][g-cache], [pricing][g-pricing], [caching API][g-cache-api], [generateContent API][g-generate].
- OpenRouter: [web search plugin][or-plugin], [server tools][or-server], [web search server tool][or-search], [web fetch][or-fetch], [tool search][or-toolsearch].
- Search services: [Brave Search API][brave], [Brave get started][brave-start], [Brave terms][brave-terms], [Tavily credits][tavily-credits], [Tavily search][tavily-search], [Exa pricing][exa-pricing], [Exa search][exa-search], [Firecrawl pricing][fc-pricing], [Firecrawl search][fc-search], [Firecrawl scrape][fc-scrape].

[AI23]: https://github.com/altqx/hikari/issues/742
[ADR 0026]: ../adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md
[ADR 0029]: ../adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md
[a-search]: https://platform.claude.com/docs/en/agents-and-tools/tool-use/web-search-tool
[a-fetch]: https://platform.claude.com/docs/en/agents-and-tools/tool-use/web-fetch-tool
[a-toolsearch]: https://platform.claude.com/docs/en/agents-and-tools/tool-use/tool-search-tool
[a-cache]: https://platform.claude.com/docs/en/agents-and-tools/tool-use/tool-use-with-prompt-caching
[a-server]: https://platform.claude.com/docs/en/agents-and-tools/tool-use/server-tools
[a-pricing]: https://platform.claude.com/docs/en/about-claude/pricing
[o-search]: https://developers.openai.com/api/docs/guides/tools-web-search
[o-toolsearch]: https://developers.openai.com/api/docs/guides/tools-tool-search
[o-data]: https://developers.openai.com/api/docs/guides/your-data
[o-pricing]: https://developers.openai.com/api/docs/pricing
[g-search]: https://aistudio.google.com/docs/google-search?generate-content=true
[g-url]: https://aistudio.google.com/docs/url-context?generate-content=true
[g-combo]: https://aistudio.google.com/docs/tool-combination?generate-content=true
[g-cache]: https://aistudio.google.com/docs/caching?generate-content=true
[g-pricing]: https://aistudio.google.com/docs/pricing
[g-cache-api]: https://aistudio.google.com/api-reference/caching
[g-generate]: https://aistudio.google.com/api-reference/generate-content
[or-plugin]: https://openrouter.ai/docs/guides/features/plugins/web-search
[or-server]: https://openrouter.ai/docs/guides/features/server-tools
[or-search]: https://openrouter.ai/docs/guides/features/server-tools/web-search
[or-fetch]: https://openrouter.ai/docs/guides/features/server-tools/web-fetch
[or-toolsearch]: https://openrouter.ai/docs/guides/features/server-tools/tool-search
[brave]: https://brave.com/search/api/
[brave-start]: https://api-dashboard.search.brave.com/app/documentation/web-search/get-started
[brave-terms]: https://api-dashboard.search.brave.com/terms-of-service
[tavily-credits]: https://docs.tavily.com/documentation/api-credits
[tavily-search]: https://docs.tavily.com/documentation/api-reference/endpoint/search
[exa-pricing]: https://exa.ai/pricing
[exa-search]: https://docs.exa.ai/reference/search
[fc-pricing]: https://www.firecrawl.dev/pricing
[fc-search]: https://docs.firecrawl.dev/features/search
[fc-scrape]: https://docs.firecrawl.dev/features/scrape
