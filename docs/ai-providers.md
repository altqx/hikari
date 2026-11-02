# AI providers

The provider layer of Hikari AI ([ADR 0024](adr/0024-hikari-ai.md), [AI5 #433](https://github.com/altqx/hikari/issues/433)). It sends requests to the providers the user added and streams the answers back. The agent, its tools and its review come later and sit on top of it.

## Where it lives

| Part | Module | What it holds |
| --- | --- | --- |
| `hikari::application::ai` | `hikari_application` | Value types (providers, models, requests, events, usage), the role models, `Providers` and the three ports: `ProviderPort`, `KeyStore`, `ConfigStore`. No Qt. |
| `hikari::backends::ai` | `hikari_ai` | `NetworkProviderPort` (HTTP and server-sent events on Qt Network, one encoder and decoder per wire), `JsonConfigStore` and `KeychainKeyStore` (QtKeychain). |

The core knows nothing of any of it.

## Off until a provider is added

With no provider, `Providers::stream` fails at once with `NoProvider`. It reads no key and opens no connection. Nothing in the app constructs a request before the user adds a provider.

## Providers and wires

| Preset | Wire | Endpoint | Key header |
| --- | --- | --- | --- |
| OpenRouter | Chat Completions | `https://openrouter.ai/api/v1` | `Authorization: Bearer` |
| OpenAI | Responses (`store: false`) | `https://api.openai.com/v1` | `Authorization: Bearer` |
| Anthropic | Messages (`anthropic-version: 2023-06-01`) | `https://api.anthropic.com/v1` | `x-api-key` |
| Custom | Chat Completions or Responses | the user's base URL | `Authorization: Bearer`, or none |

- Custom endpoints cover local servers (Ollama, LM Studio, vLLM) and any OpenAI-compatible service. A custom provider needs no key unless the user gives one.
- Every request streams. Text arrives as `TextDelta`, tool calls as `ToolCallStarted` and `ToolCallArguments`, and the request ends with exactly one `Finished` (stop reason, the whole text, the tool calls, usage) or `Failed`.
- A user message carries text, images (base64, sent as a data URL or an Anthropic image block) and tool results. An assistant message carries text and tool calls.
- Redirects are refused, and nothing is cached.

## Keys

- Keys live only in the OS keychain through QtKeychain: the Secret Service (libsecret) or KWallet on Linux, the Credential Store on Windows. The entry is `ai-provider/<provider id>` under the service `HikariSub`.
- A provider is added only once its key is in the keychain. With no keyring daemon, or when the user refuses access, it isn't added, and the caller gets `KeychainUnavailable` or `KeychainDenied` to tell the user. There is no plain-text fallback.
- A key travels only over HTTPS, or over plain HTTP to this machine (`localhost`, `127.0.0.0/8`, `::1`) or to a Tailscale peer: a MagicDNS `*.ts.net` name, `fd7a:115c:a1e0::/48`, or `100.64.0.0/10` when this machine has an address in that range too (it is also ISPs' CGNAT space). A host name is resolved once and the request goes to the address that passed. Anything else, a LAN address included, fails with `InsecureKey` before a connection opens.
- The provider file and the logs never hold a key. The mock-server suite checks both.

## Models and roles

- `refreshModels` reads the provider's `/models`. Capabilities (tools, vision) and prices come from the catalogue where it reports them (OpenRouter does). Otherwise the preset's defaults apply: tools and vision for OpenAI and Anthropic, neither for OpenRouter's unlisted models and custom endpoints. The user's own correction for a model wins over both.
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

- `hikari_application_ai_providers_tests`: role fallback, capabilities, cost, keychain failures, cancellation, removal.
- `hikari_backends_ai_provider_tests`: a local mock server for each wire, covering streaming split at awkward bytes, tool calls, vision and history encoding, HTTP and in-stream errors, early ends, timeouts, cancellation, catalogues, the key rules and the provider file.

No test talks to a real provider.
