# Hosting provider agent SDKs in Hikari (AI15)

Research for card AI15 ([#443](https://github.com/altqx/hikari/issues/443)). The owner asked to "use the
provider's own SDK for each agent if possible, like Claude should use claude agent-sdk like they did in
anomalyco/opencode#58782, or follow how hermes-agent did it". The current decisions are in
[ADR 0024](../adr/0024-hikari-ai.md) and the [AI integration note](hikari-ai-integration.md). ADR 0024
says "The Claude Agent SDK isn't embedded" and sends Claude subscriptions through the ACP Claude Agent.
This note gives the facts behind revisiting that. Until the owner changes the ADR, the ADR wins.

Sources were read on **2026-11-02**:

- opencode PR [#58782][oc-pr] at merge commit `f29e2813` (merged 2026-10-30) and its [spec][oc-spec].
  Paths such as `oc:…/query-options.ts:51` are under `packages/core/src/plugin/provider/claude-agent/`
  at that commit.
- [NousResearch/hermes-agent][hermes] at `0aab6dcf` (MIT). Paths such as `hermes:agent/…` refer to it.
- [anthropics/claude-agent-sdk-python][cpy] at `2146f9d8` (MIT; it bundles Claude Code 2.1.316). Paths
  such as `cpy:…/query.py:586` are under `src/claude_agent_sdk/_internal/`.
- npm metadata for `@anthropic-ai/claude-agent-sdk` 0.3.316, `@agentclientprotocol/claude-agent-acp`
  0.99.1, `@openai/codex-sdk` and `@openai/codex` 0.166.0, `@agentclientprotocol/codex-acp` 2.5.0 and
  `@google/gemini-cli` 0.66.0 (`npm view`, `npm pack`).
- Vendor documentation, cited inline. Where a page did not say something, this note says so.

## 0. Summary

- **opencode runs the user's own `claude` binary through the TypeScript Agent SDK.** It never reads the
  Claude login, turns off every Claude Code built-in tool, serves its own tools as an in-process SDK MCP
  server, and uses two hooks to pair tool calls with results. Its permission system stays the only one.
  It never runs the 270 MB binary that the SDK downloads.
- **hermes-agent does not use the Claude Agent SDK.** It calls the Messages API itself with Claude Code's
  OAuth tokens, borrowed from `~/.claude/.credentials.json` or the macOS keychain, and presents itself as
  Claude Code (`claude-code/…` user agent, "You are Claude Code" system prefix). Anthropic's terms forbid
  this. For Codex, hermes has an opt-in `codex app-server` runtime that serves hermes tools back over stdio
  MCP. ACP is its generic path for other CLIs (Copilot, Cursor, Kiro and so on).
- **Neither SDK is a library that runs in our process.** The Claude Agent SDK and the Codex SDKs are
  wrappers that spawn the vendor CLI and speak JSON lines over stdio. Anthropic's own advice for "a
  language other than Python or TypeScript" is to run the CLI as a subprocess. OpenAI's for "a deep
  integration inside your own product" is `codex app-server`.
- **Gemini has no published agent SDK.** `packages/sdk` exists in the repo but is not on npm. The
  programmatic interface Google documents is ACP (`gemini --acp`).
- **Recommendation:** drive `claude` (stream-json) and `codex app-server` (JSON-RPC) directly from C++
  against binaries the user installs and signs in to. Don't bundle Node, the SDKs or the vendor
  binaries. Keep ACP for Gemini CLI, OpenCode, Copilot and custom agents, and as a fallback for Claude
  and Codex through their registry adapters. See §5.
- **Owner decisions:** whether to change ADR 0024; whether to ask Anthropic before shipping (opencode's
  own spec asks the same); whether Hikari may drive Codex app-server auth or must leave sign-in to
  `codex login`; and whether to depend on Codex app-server, which OpenAI still labels experimental. See §6.

## 1. opencode PR #58782: the Claude Agent provider

Merged 2026-10-30, +6866/−67, by Tarquinen ([PR][oc-pr]). The body says it "runs Claude models through
the user's own Claude Code login, using the Claude Agent SDK. To use it, install Claude Code, run
`claude auth login`, then pick Claude Agent in `/connect`."

| Topic | What it does | Source |
| --- | --- | --- |
| Process model | One long-running `claude` process per session, stopped after 5 minutes idle. The next message resumes the saved Claude session. The idle timer pauses while opencode runs Claude's tool calls. The process restarts when the system prompt, tools, connection settings or output limit change, "because the SDK cannot swap tools while running" | spec §Processes, §Tools; `oc:claude/sessions.ts:25` |
| Input | A `ReadableStream<SDKUserMessage>` passed as `prompt` to `query()`, so one process takes many turns | `oc:claude/session.ts:21-26` |
| Binary | The user's `claude`, from `OPENCODE_CLAUDE_PATH` or `PATH`, passed as `pathToClaudeCodeExecutable`. The SDK is an optional peer dependency, pinned at 0.3.301 and loaded on first use. Installing it "downloads its own copy of `claude` (about 270 MB unpacked). We never run it." Claude Code 2.1.248 or later is required | spec §Login, §Shipping; `oc:claude/io.ts`; `oc:claude/probe.ts:22`; `packages/core/package.json` |
| Auth | "opencode never reads Claude Code's saved login. Users sign in with `claude` in their terminal." Any login works: subscription, API key or cloud. It clears `CLAUDE_CODE_SIMPLE` because it "would skip the subscription login". A probe query that never sends a prompt reads `initializationResult()` for the account, version and models, without spending tokens | spec §Login; `oc:claude/query-options.ts:20-21`; `oc:claude/probe.ts:57`, `:111` |
| Isolation | `settingSources: []`, `settings: { disableAllHooks: true }`, `strictMcpConfig: true`, `skills: []`, `tools: []`, plus env vars that turn off CLAUDE.md, auto memory, auto compaction, away summaries, tool search and the advisor tool | `oc:claude/query-options.ts:11-26`, `:51-71` |
| Tools | opencode's tools go into an in-process MCP server (`type: "sdk"`) named `opencode`. Each tool is marked `readOnlyHint: false` and `anthropic/alwaysLoad`. The handler waits until opencode has run the call through its normal tool path, permission prompts included | `oc:tools/server.ts:22-33`; spec §Tools |
| Hooks | `PreToolUse` gives the handler Claude's `tool_use_id`, which an MCP handler cannot see. So tools run one at a time. `PostToolUse` returns `updatedMCPToolOutput` to add PDFs and images that an MCP result can't carry | `oc:tools/hooks.ts:8-21`; spec §Set by opencode |
| Permissions | `permissionMode: "dontAsk"` with `allowedTools: ["mcp__opencode__*"]`. Anything else is refused without asking, and opencode's own permission system decides each call. `canUseTool` is not used | `oc:claude/query-options.ts:67-68` |
| Sessions | New sessions get a random `sessionId`. Later turns `resume` it. After an edit or undo, the provider forks with `forkSession(id, { upToMessageId })`, checks that the copied prefix matches, and maps message ids. With no common prefix it starts fresh with a `<conversation-checkpoint>` message | `oc:claude/query-options.ts:103-108`; `oc:claude/fork.ts:12-16`; spec §History |
| Compaction | Claude Code's auto compaction is off. opencode compacts on "a throwaway copy of the chat" that keeps the tools and prompt, "so Claude can reuse its prompt cache" (newly cached tokens fell from 8,577 to 32 in one test) | spec §Titles, Summaries and Compaction |
| Usage and cost | It keeps the highest count per response, because Claude Code reports a response's usage several times while streaming. The last response's input is the context size. Subscriptions show $0. API key and cloud logins are priced from models.dev, because Claude's own cost "is a running total for the turn". Context windows come from `getContextUsage` | spec §Errors and Usage; `oc:language-model/usage.ts:25`, `:34`; `oc:claude/probe.ts:87` |
| Open questions | "Do we need to discuss anything here with anthropic or any kind of approval?"; branding ("Claude Agent", not "Claude Code"); "The CC dependency is big (our second biggest)" | spec §Open Questions and Concerns |

## 2. hermes-agent

The canonical repo is [NousResearch/hermes-agent][hermes] (MIT, about 260k stars). It is a Python agent
with its own tool loop. It does not use the Claude Agent SDK. The one `claude-agent-sdk` string in the
repo is a provider name in a config test (`hermes:tests/hermes_cli/test_config.py:1813`).

| Provider | How hermes reaches it | Source |
| --- | --- | --- |
| Claude | Its own `anthropic_messages` transport. Token order: `ANTHROPIC_TOKEN` / `CLAUDE_CODE_OAUTH_TOKEN`, API key, hermes's own PKCE grant (made with Claude Code's OAuth client id), then `~/.claude/.credentials.json` or the macOS Keychain item `Claude Code-credentials` "as a borrowed fallback". On OAuth routes it sends `claude-code-20250219` and `oauth-2025-04-20` betas, `user-agent: claude-code/<ver> (external, cli)`, `x-app: cli` and the system prefix "You are Claude Code, Anthropic's official CLI for Claude." Its docs say this path "routes as Claude Code" and only works on Max with extra usage credits | `hermes:agent/anthropic_credentials.py:1-9`, `:35`, `:48`; `hermes:agent/anthropic_adapter.py:238`, `:318`, `:427-428`; `hermes:website/docs/integrations/providers.md:140-146` |
| Codex (default) | `codex_responses` transport to `https://chatgpt.com/backend-api/codex` with ChatGPT OAuth. It can import `~/.codex/auth.json` and listens on the Codex client's `localhost:1455` callback | `hermes:hermes_cli/providers.py:32-33`; `providers.md:92` |
| Codex (opt-in runtime) | Spawns `codex app-server` (codex 0.125+) and speaks newline-delimited JSON-RPC: `initialize`, `thread/start`, `turn/start`, `item/*` until `turn/completed`. It bridges approval requests to its own approval callback. Hermes tools come back over a stdio MCP server, `hermes_tools_mcp_server`, registered as `[mcp_servers.hermes-tools]` in `~/.codex/config.toml`. Tools that need the live agent loop (subagents, memory) are not available there | `hermes:agent/transports/codex_app_server.py:1-6`, `:111`; `hermes:agent/transports/hermes_tools_mcp_server.py:1-5`; `hermes:website/docs/user-guide/features/codex-app-server-runtime.md` |
| Gemini | A native Gemini API adapter (API key) and a `vertex` provider. Gemini CLI is not driven | `hermes:agent/gemini_native_adapter.py`; `providers.py` `vertex` entry |
| Other CLIs | A generic ACP stdio client "behind a minimal OpenAI-client facade". Each request starts a short-lived ACP session and sends the conversation as one prompt. The Copilot, Kiro, Grok and Cursor plugins build on it | `hermes:agent/acp_client.py:1-12`; `hermes:agent/copilot_acp_client.py:1-12` |
| Hermes as an agent | Hermes is also an ACP server for editors | `hermes:website/docs/user-guide/features/acp.md:13-15` |

So "how hermes did it" means its own loop on raw provider APIs, with borrowed CLI logins. For Claude
that breaks Anthropic's terms (§3.1). For Codex it uses the endpoint OpenAI's SIWC docs say not to use
(the [AI note §2.2](hikari-ai-integration.md)). The part worth copying is the Codex app-server runtime
with the host's tools served back over MCP.

## 3. Providers

### 3.1 Anthropic

- **What the SDK is.** The Agent SDK is "a library that runs the Claude Code binary", in Python and
  TypeScript. "To drive the same agent loop from a language other than Python or TypeScript, run the CLI
  as a subprocess with the `-p` flag and `--output-format json`" ([SDK overview][a-sdk]).
- **What it does on the wire.** The Python SDK starts `claude --output-format stream-json --verbose …
  --input-format stream-json` (`cpy:transport/subprocess_cli.py:574`, `:793`). Over that pipe it sends an
  `initialize` control request that registers hook callback ids (`cpy:query.py:341`). It answers the CLI's
  `can_use_tool`, `hook_callback` and `mcp_message` control requests (`cpy:query.py:586`, `:640`, `:656`)
  and sends `interrupt` and `set_model` (`:794`, `:809`). `canUseTool` works by setting
  `--permission-prompt-tool stdio` (`cpy:…/types.py:2304`). In-process SDK MCP servers are tunnelled as
  `mcp_message`. The CLI reference and headless docs do not document this control protocol. It is only
  visible in the SDK source.
- **What the CLI documents** ([headless][a-headless], [CLI reference][a-cli]): `-p` with
  `--input-format stream-json`, `--output-format stream-json`, `--verbose`, `--include-partial-messages`;
  `--mcp-config` (stdio, or `"type": "http"` with `url` and `headers`, per [MCP docs][a-mcp]) and
  `--strict-mcp-config`; `--tools ""` to turn off built-in tools; `--allowedTools`;
  `--permission-mode` (`dontAsk` and others); `--permission-prompt-tool mcp__<server>__<tool>` for
  approvals in non-interactive mode; `--resume`, `--session-id`, `--fork-session`,
  `--no-session-persistence`; `--system-prompt` and `--append-system-prompt`; `--setting-sources`,
  `--settings`; `--max-budget-usd`. The `system/init` event lists MCP server status and a `capabilities`
  array "to feature-detect instead of comparing version strings". `system/api_retry` carries error
  categories such as `authentication_failed` and `billing_error`. The `result` event has usage and
  `total_cost_usd` (client-side estimates). SIGINT ends a turn; SIGTERM leaves it unfinished.
- **`--bare` cannot use a subscription.** "In bare mode, Claude Code never reads OAuth credentials or the
  system keychain", although it is "the recommended mode for scripted and SDK calls" ([headless][a-headless]).
  opencode keeps it off for this reason (`oc:claude/query-options.ts:20-21`).
- **SDK-only features:** `canUseTool` callbacks, in-process hook callbacks, in-process MCP servers,
  `setModel`, `getContextUsage`, and the session helpers `listSessions`, `getSessionMessages`,
  `forkSession(upToMessageId)`, `deleteSession` and `SessionStore` ([sessions][a-sessions],
  [user input][a-input]). The CLI covers resume and fork by flag, and approvals through
  `--permission-prompt-tool`.
- **Licence.** The TypeScript package is "SEE LICENSE IN README.md", and its LICENSE.md says "© Anthropic
  PBC. All rights reserved. Use is subject to the Legal Agreements" (npm 0.3.316). The Python SDK's code is
  MIT, but each wheel bundles the Claude Code binary (about 145 MB per platform on PyPI). SDK use "is
  governed by Anthropic's Commercial Terms of Service … except to the extent a specific component … is
  covered by a different license" ([SDK overview][a-sdk]). The TypeScript SDK's native binary packages
  unpack to 328 MB (linux-x64) and 335 MB (win32-x64). There were 27 TypeScript SDK releases in October 2026.
- **Subscription auth, quoted:**
  - "Unless previously approved, Anthropic does not allow third party developers to offer claude.ai login
    or rate limits for their products, including agents built on the Claude Agent SDK. Use the API key
    authentication methods described in the Quickstart instead." ([SDK overview][a-sdk])
  - "Developers building products or services that interact with Claude's capabilities, including those
    using the Agent SDK, should use API key authentication … Anthropic does not permit third-party
    developers to offer Claude.ai login into their own applications, or to route requests through Free,
    Pro, Max, or Ultra plan credentials on behalf of their users. Moreover, developers may not collect,
    store, or intermediate Claude.ai credentials or session tokens — sign-in to a Claude account must
    complete through Anthropic's own flow." ([legal][a-legal])
  - "Nor does it prevent an end user from signing in to the unmodified Claude Code binary with their own
    Claude subscription, including where a platform hosts Claude Code as described under *Can customers
    offer Claude Code in their products?*" That section requires the Commercial Terms, an unmodified
    binary with no authentication method removed, and "Each end user must authenticate with their own
    Anthropic API key, Claude subscription plan credentials, or 3P inference provider credential"
    ([legal][a-legal]).
  - Reading: hermes's approach (collecting and replaying Claude.ai tokens) is plainly out. opencode's
    approach (the user signs in to their own unmodified `claude`, and the host never touches the login)
    fits the "end user signing in to the unmodified Claude Code binary" sentence. The SDK overview's
    "Unless previously approved" note still covers "agents built on the Claude Agent SDK". Asking
    Anthropic, as ADR 0024 already says, resolves the tension.
- **Branding:** "Claude Agent" is allowed; "Claude Code" or "Claude Code Agent" as a product name is not
  ([SDK overview][a-sdk]). The legal page allows saying, in plain text, that a product "runs Claude Code".
- **Custom MCP server:** SDK `mcpServers` (stdio, HTTP or in-process `type: "sdk"`), or CLI
  `--mcp-config` plus `--strict-mcp-config`.

### 3.2 OpenAI Codex

- **What the SDKs are.** TypeScript `@openai/codex-sdk` (Apache-2.0, 80 KB) depends on `@openai/codex`
  and spawns `codex exec --experimental-json`, passing settings as `--config` TOML overrides and resuming
  with `resume <threadId>` (`@openai/codex-sdk` 0.166.0 `dist/index.js:178-241`). The Python SDK
  (`openai-codex`) "controls the local Codex app-server over JSON-RPC" and "Published SDK builds include a
  pinned Codex CLI runtime dependency" ([Codex SDK][o-sdk]). OpenAI positions the SDK for automation and
  CI: "If you are automating jobs or running Codex in CI, use the Codex SDK instead." App-server is "the
  interface Codex uses to power rich clients (for example, the Codex VS Code extension). Use it when you
  want a deep integration inside your own product" ([app-server][o-as]).
- **App-server protocol** ([app-server][o-as]): JSON-RPC 2.0 as JSONL over stdio (the default).
  `initialize` comes first. Then `thread/start`, `thread/resume`, `thread/fork`, `turn/start`,
  `turn/steer`, `turn/interrupt`, `thread/compact/start`, `thread/tokenUsage/updated`,
  `account/rateLimits/read`, and `account/login/start` (`apiKey`, `chatgpt`, `chatgptDeviceCode`). The
  server sends approval requests (`item/commandExecution/requestApproval`,
  `item/fileChange/requestApproval`) with decisions `accept`, `acceptForSession`, `decline`, `cancel`, plus
  `mcpServer/elicitation/request`. Experimental `dynamicTools` on `thread/start` lets the client run tools.
  `codex app-server generate-json-schema` emits schemas "specific to the Codex version you ran", which
  suits generated C++ types. `thread/start` takes a `config` map (`openai/codex
  codex-rs/app-server-protocol/src/protocol/v2/thread.rs:100`, main on 2026-11-02).
- **Stability:** "The app-server command and WebSocket transport are experimental and aren't supported for
  production workloads", and parts are gated behind `experimentalApi` ([app-server][o-as]).
- **Licence:** openai/codex is Apache-2.0 (`gh api repos/openai/codex`). The native packages unpack to
  456 MB (linux-x64) and 503 MB (win32-x64) at 0.166.0, with 10 stable releases in October 2026.
- **Auth and third-party use, quoted:** "Sign in with ChatGPT for subscription access", "Sign in with an
  API key for usage-based access", "API keys are still the recommended default for automation"
  ([Codex auth][o-auth]). On app-server login: "If you've built a local or open-source application using
  Codex app-server authentication, you can continue using it", with a recommendation to migrate to Sign
  in with ChatGPT, and "App-server authentication has never been permitted for commercial or hosted
  services" ([app-server][o-as], Auth endpoints). The [AI note §2.2](hikari-ai-integration.md) covers SIWC
  for Hikari's in-app agent.
- **Custom MCP server:** `[mcp_servers.<id>]` in `config.toml`, with `command`/`args` for stdio or `url`
  plus `bearer_token_env_var` for Streamable HTTP ([Codex MCP][o-mcp]). Pass it per run with `-c`
  overrides, or in `thread/start`'s `config`, without touching the user's file. Hermes instead writes
  `~/.codex/config.toml`.

### 3.3 Google Gemini CLI

- **No published SDK.** `packages/sdk` (`@google/gemini-cli-sdk`, `GeminiCliAgent`, Apache-2.0) is in the
  repo at a nightly version, but `npm view @google/gemini-cli-sdk` returns 404. The CLI itself is
  Apache-2.0, Node 20+, 99 MB unpacked (npm 0.66.0).
- **Headless mode** has `--output-format json` and `stream-json` output only. There is no streaming
  input and no approval channel ([headless][g-headless]).
- **ACP mode:** "`gemini --acp`", "designed for programmatic control". It supports `initialize`,
  `authenticate`, `newSession`, `loadSession`, `prompt`, `cancel`, `setSessionMode` and
  `unstable_setSessionModel`. A client MCP server is supplied in the handshake, and MCP tool permissions
  arrive as `session/request_permission` with `_meta['gemini-cli'].mcp.server_name` ([ACP mode][g-acp]).
- **Auth and terms:** a Google account (Gemini Code Assist), a Gemini Developer API key, or a Vertex key.
  "Directly accessing the services powering Gemini CLI (for example, the Gemini Code Assist service)
  using third-party software, tools, or services (for example, using OpenClaw with Gemini CLI OAuth) is a
  violation of applicable terms and policies" ([terms][g-tos]). Hikari may run `gemini`, but must never
  reuse its OAuth. The in-app Gemini adapter stays on API keys (AI18 #639).

### 3.4 Provider table

| | Claude | Codex | Gemini |
| --- | --- | --- | --- |
| Official SDK | Agent SDK, TS and Python; spawns `claude` | Codex SDK: TS spawns `codex exec`; Python drives app-server | none published |
| Interface for other languages | `claude -p` stream-json (documented flags; control protocol undocumented) | `codex app-server` JSON-RPC (documented, experimental) | `gemini --acp` |
| Licence | binary and TS SDK: Anthropic terms, all rights reserved; Python SDK code MIT | Apache-2.0 | Apache-2.0 |
| Subscription in a third-party app | user signs in to unmodified `claude`; host must not offer or intermediate claude.ai login; SDK note says ask first | local or open-source app-server auth tolerated; SIWC recommended; never for commercial or hosted | run the CLI; never reuse its OAuth |
| Custom MCP | `--mcp-config` / SDK `mcpServers` | `mcp_servers` via `-c` or `thread/start.config` | ACP handshake / `session/new` |
| ACP adapter | `claude-agent-acp` (Apache-2.0, Node, on the TS SDK) | `codex-acp` (Apache-2.0, Node, on app-server) | built in |

## 4. Hosting options for a C++/Qt app

**(a) A bundled Node or Python sidecar running the SDK**, speaking JSON-RPC to Hikari over stdio. It gets
everything in the SDK API. The cost is Node 26 LTS (34 MB `.tar.xz` for Linux x64, 41 MB `.zip` for
Windows x64 from nodejs.org) or a Python runtime, plus the SDK. It also means redistributing the
TypeScript SDK, which is "All rights reserved" under Anthropic's terms, inside a GPL-3.0 package. Either
the vendor binaries ship too (328–503 MB per platform per vendor) or the sidecar points them at the
user's install, as opencode does. Hikari would own a third language and a lockfile, and would follow the
SDK's release rate (27 Claude SDK releases in October).

**(b) Drive each CLI's protocol directly from C++.** `QProcess` plus a JSONL reader, which Hikari already
needs for MCP and ACP. Claude uses the documented stream-json flags, with Hikari's MCP server (AI3 #431)
passed by `--mcp-config` and approvals through `--permission-prompt-tool`. Codex uses app-server, with
types generated from `generate-json-schema`. There is no runtime to bundle and nothing redistributed.
The cost: Hikari tracks two vendor protocols. For Claude, the documented surface lacks hook callbacks,
`setModel` and `getContextUsage` (they need the undocumented control protocol). For Codex, app-server is
labelled experimental.

**(c) ACP through the adapters.** One protocol for every agent. `claude-agent-acp` and `codex-acp` are
themselves built on the TS Agent SDK and on app-server, so they inherit most features. The cost: the
adapters need Node. `claude-agent-acp` pulls the 328 MB SDK binary unless `CLAUDE_CODE_EXECUTABLE` is set
(`claude-agent-acp` 0.99.1 `dist/acp-agent.js:675-710`). The adapter, not Hikari, offers "Claude
Subscription" login (`--cli auth login --claudeai`, `dist/acp-agent.js:1368-1370`). ACP exposes less than
the SDKs: no hooks, no tool-call pairing and no prompt-cache control, though the adapters add extensions.

| | (a) SDK sidecar | (b) CLI protocol from C++ | (c) ACP adapter |
| --- | --- | --- | --- |
| Permission callback | `canUseTool` | Claude: `--permission-prompt-tool` MCP tool; Codex: approval server requests | `session/request_permission` |
| Hooks | yes, in process | Claude: hooks in `--settings` JSON, not in-process callbacks; Codex: not researched | no |
| Host tools | in-process SDK MCP | Hikari MCP server (stdio bridge or HTTP) | `session/new` `mcpServers` |
| Resume | yes | Claude `--resume`; Codex `thread/resume` | `session/load`, `session/resume` (stable) |
| Fork | `forkSession(upToMessageId)` | Claude `--fork-session` (whole session); Codex `thread/fork` | `session/fork` (unstable schema) |
| Usage, cost | per message plus result | stream events plus `result`; Codex `thread/tokenUsage/updated`, rate limits | `UsageUpdate` with context `used`/`size` and optional `cost` |
| Prompt caching | automatic in Claude Code; opencode keeps tools and prompt stable for cache hits | same (the agent owns the requests) | same |
| Interrupt, model switch | `interrupt()`, `setModel()` | Claude SIGINT, model set per process by `--model`; Codex `turn/interrupt`, model on `thread/start` | `session/cancel`, session config options |
| Bundle | Node or Python runtime + SDK (+ binaries) | none | Node + adapter (+ binary) |
| Windows | runtime + binaries per arch | `QProcess`; no SIGINT for a child, so Claude interrupt is harder | Node per arch |
| Update churn | SDK releases plus vendor CLI | vendor protocol; pin minimum versions, feature-detect (`capabilities`, version-matched schema) | adapter releases |
| Security | one more interpreter and package tree in Hikari | vendor binary runs as the user; Hikari tools stay behind AI3's token and Origin checks | as (b), plus adapter code |

The ACP sources for that table are the ACP v1 schema `schema-v1.29.1` (`schema.json` has
`ResumeSessionRequest`, `LoadSessionRequest`, `UsageUpdate`, `Cost`; `schema.unstable.json` adds
`ForkSessionRequest`).

All three let the vendor agent's own built-in tools (shell, file edits) run unless they are turned off.
Hikari must turn them off or deny them so that writes go only through its tools into AI2's review (ADR
0024 "External agents"). That means `--tools ""` plus `--strict-mcp-config` for Claude, as opencode does.
For Codex it means a read-only sandbox and declining every `commandExecution` and `fileChange` approval.

## 5. Recommendation for AI15

| Agent | Path | Why |
| --- | --- | --- |
| Claude | **(b)**: `claude -p --input-format stream-json --output-format stream-json --verbose --include-partial-messages`, user-installed `claude` (path setting, like `OPENCODE_CLAUDE_PATH`), `--tools ""`, `--strict-mcp-config --mcp-config` pointing at Hikari's MCP server, `--allowedTools "mcp__hikari__*"`, `--permission-prompt-tool mcp__hikari__approve` for Manual/Auto, `--session-id`/`--resume`/`--fork-session` for threads, no user or project setting sources (the SDK's `settingSources: []`; check which CLI form the binary accepts), plus opencode's isolation env vars. Never `--bare` | This is Anthropic's documented route for non-JS/Python hosts. It gets the SDK's loop, sessions and caching without bundling Node or a proprietary binary. Hikari never touches the login, matching opencode. Leave the undocumented control protocol out at first. Add `interrupt` later only if SIGINT proves unworkable on Windows, gated on `system/init.capabilities` |
| Codex | **(b)**: `codex app-server` over stdio, user-installed `codex`, C++ types generated from `generate-json-schema` for a pinned minimum version, Hikari's MCP server in `thread/start.config`, read-only sandbox, approval requests mapped to the three modes | This is OpenAI's documented deep-integration interface, and the one hermes and the new `codex-acp` use. The TS SDK's `codex exec` path lacks approvals. Sign-in stays with `codex login` unless the owner decides otherwise (§6) |
| Gemini CLI | **(c)** ACP, `gemini --acp` | No SDK is published, and ACP is Google's documented programmatic interface |
| OpenCode, Copilot, Goose, Cursor, custom | **(c)** ACP from the registry or a user command | No vendor SDK applies. ADR 0024's ACP client covers them |
| Claude Agent / Codex via ACP | keep as plain registry ACP agents | They cost nothing extra once the ACP client exists, and they cover users who prefer the adapters |

Build order for AI15: the ACP client (it covers Gemini and every custom agent), then Codex app-server,
then Claude stream-json, behind one app-layer external-agent interface so the review, the modes and the
panel see one shape. The card's acceptance criterion ("Claude Agent and Codex complete an edit task
through Hikari's tools") holds on either (b) or (c).

## 6. Owner decisions

1. **Change ADR 0024?** It says "The Claude Agent SDK isn't embedded" and "Claude subscriptions come
   through the ACP Claude Agent". The recommendation keeps the first (no SDK bundled), but replaces the
   ACP adapter with direct `claude` stream-json as the main Claude path. That needs an ADR amendment.
2. **Ask Anthropic first?** The legal page allows an end user signing in to the unmodified `claude`
   binary, but the SDK overview says "Unless previously approved" for agents built on the SDK.
   opencode's spec leaves the same question open. ADR 0024 already says to ask before advertising.
3. **Codex sign-in:** let Hikari call app-server `account/login/start` (tolerated for open-source local
   apps, migration to SIWC recommended), or require `codex login` in a terminal (the cleaner reading)?
4. **Experimental app-server:** accept a dependency OpenAI labels "experimental and aren't supported for
   production workloads", pinned to a minimum version, or start Codex on ACP (`codex-acp`, which wraps
   the same server) and move later?
5. **Bundle Node?** Not recommended. If the owner prefers the SDKs' extra hooks over (b), (a) needs Node
   (34–41 MB) plus Anthropic's permission to redistribute the "All rights reserved" TS SDK in a GPL-3.0
   package.

[oc-pr]: https://github.com/anomalyco/opencode/pull/58782
[oc-spec]: https://github.com/Tarquinen/opencode/blob/claude-agent-provider/specs/v2/claude-agent-provider.md
[hermes]: https://github.com/NousResearch/hermes-agent
[cpy]: https://github.com/anthropics/claude-agent-sdk-python
[a-sdk]: https://code.claude.com/docs/en/agent-sdk/overview
[a-legal]: https://code.claude.com/docs/en/legal-and-compliance
[a-headless]: https://code.claude.com/docs/en/headless
[a-cli]: https://code.claude.com/docs/en/cli-reference
[a-mcp]: https://code.claude.com/docs/en/mcp
[a-sessions]: https://code.claude.com/docs/en/agent-sdk/sessions
[a-input]: https://code.claude.com/docs/en/agent-sdk/user-input
[o-sdk]: https://learn.chatgpt.com/docs/codex-sdk
[o-as]: https://learn.chatgpt.com/docs/app-server
[o-auth]: https://learn.chatgpt.com/docs/auth
[o-mcp]: https://developers.openai.com/codex/mcp
[g-acp]: https://github.com/google-gemini/gemini-cli/blob/main/docs/cli/acp-mode.md
[g-headless]: https://github.com/google-gemini/gemini-cli/blob/main/docs/cli/headless.md
[g-tos]: https://github.com/google-gemini/gemini-cli/blob/main/docs/resources/tos-privacy.md
