# AI providers

The provider layer of Hikari AI ([ADR 0024](adr/0024-hikari-ai.md), [AI5 #433](https://github.com/altqx/hikari/issues/433)). It sends requests to the providers the user added and streams the answers back. Since [AI5P #652](https://github.com/altqx/hikari/issues/652) it runs in the TypeScript sidecar on the providers' official SDKs ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)). C++ keeps the Settings model: the keychain, the keyless provider file and the role models.

## Where it lives

| Part | Module | What it holds |
| --- | --- | --- |
| `hikari::application::ai` | `hikari_application` | Value types (providers, models, roles, usage, errors, events), `Providers` and the `KeyStore` and `ConfigStore` ports. No Qt. |
| `hikari::backends::ai` | `hikari_ai` | `AiRequests` (requests through the sidecar), `JsonConfigStore` and `KeychainKeyStore` (QtKeychain). |
| `src/ai-sidecar/src/providers` | the sidecar | One client per wire on `openai`, `@anthropic-ai/sdk` and `@google/genai`, the egress fetch, the catalogue and the error mapping. |

The core knows nothing of any of it.

## Off until a provider is added

With no provider, `AiRequests::complete` fails at once with `NoProvider`. It reads no key and never asks for the sidecar, so nothing starts and no connection opens. With AI turned off, no `Providers` or sidecar exists at all.

## The sidecar methods

| Method | Params | Result |
| --- | --- | --- |
| `_hikari/providers.set` | `{providers: [{id, preset, wire, baseUrl}]}` | `null` |
| `_hikari/complete` | `{stream, providerId, key, request}` | `{reason, text, toolCalls, usage}` |
| `_hikari/models.list` | `{providerId, key}` | `{models: [{id, name, capabilities?, price?, contextTokens?}]}` |

- `AiRequests` sends `providers.set` with the current config before each request, then the request with the role's model and the key it just read from the keychain. The sidecar keeps the key only for that call.
- While a completion runs, `session/update` notifications carry `{stream, update}` with `text`, `toolCall {index, id, name}`, `toolArguments {index, delta}` and `usage` updates. C++ turns them into `TextDelta`, `ToolCallStarted` and `ToolCallArguments`; the answer becomes `Finished`.
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

## Keys

- Keys live only in the OS keychain through QtKeychain: the Secret Service (libsecret) or KWallet on Linux, the Credential Store on Windows. The entry is `ai-provider/<provider id>` under the service `HikariSub`. The sidecar never opens the keychain and never reads a key from its environment.
- A provider is added only once its key is in the keychain. With no keyring daemon, or when the user refuses access, it isn't added, and the caller gets `KeychainUnavailable` or `KeychainDenied` to tell the user. There is no plain-text fallback.
- Every SDK client sends through one egress fetch. It reaches only URLs under the provider's base URL and never follows a redirect.
- A key travels only over HTTPS, or over plain HTTP to this machine (`localhost`, `127.0.0.0/8`, `::1`) or to a Tailscale peer: an address in `100.64.0.0/10` or `fd7a:115c:a1e0::/48` while this machine has an address in the same range (the IPv4 range is also ISPs' CGNAT space). A host name, `*.ts.net` included, counts only by the address it resolves to: it is resolved once and the request goes to the address that passed, with the name kept in `Host`, so DNS can't send the key elsewhere. Anything else, a LAN address included, fails with `InsecureKey` before a connection opens.
- The provider file, the sidecar's answers and the logs never hold a key. The tests check each.

## Models and roles

- `AiRequests::refreshModels` asks the sidecar for the provider's catalogue and `Providers::setModels` stores it. Capabilities (tools, vision) and prices come from the catalogue where it reports them (OpenRouter does). Otherwise the preset's defaults apply: tools and vision for OpenAI and Anthropic, tools, vision and video for Gemini, neither for OpenRouter's unlisted models and custom endpoints. The user's own correction for a model wins over both.
- The roles are chat, translator, reviewer, vision and judge. A role without its own model uses the chat model. The first provider added with a catalogue becomes the chat model.

## Usage and cost

`Usage` holds input, output and cached input tokens. Cost is the provider's own figure when it reports one (OpenRouter's `usage.cost`), else the model's price per million tokens times the tokens, else unknown. Caps, the meter and estimates belong to the agent.

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

- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/providers`): a local mock server for each wire, Gemini included, covering streaming split at awkward bytes, tool calls, vision and history encoding, HTTP and in-stream errors, safety blocks, early ends, timeouts, cancellation, catalogues, the key rules and the JSON-RPC methods against their schemas.
- `hikari_application_ai_providers_tests`: role fallback, capabilities, cost, keychain failures, removal, the catalogue and a Gemini model in every role.
- `hikari_backends_ai_requests_tests`: requests through a fake sidecar: no provider starts nothing, a cancel before the key sends nothing, the key and model reach the sidecar, errors keep their kind.
- `hikari_backends_ai_config_store_tests`: the provider file keeps everything but the key.

No test talks to a real provider.
