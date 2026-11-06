---
status: accepted
amends: 0024
---

# Hikari AI runs in a TypeScript sidecar on Bun

On 2026-11-02 altqx decided that Hikari AI's model work moves out of the C++ app into a TypeScript sidecar process that the app starts and talks to ([#410](https://github.com/altqx/hikari/issues/410)). The C++ app keeps the UI, the Hikari API ([HA1 #411](https://github.com/altqx/hikari/issues/411)), the staged review ([AI2 #430](https://github.com/altqx/hikari/issues/430)) and the keys in the OS store. The research is in [AI sidecar runtime](../research/ai-sidecar-runtime.md) and [agent SDK hosting](../research/agent-sdk-hosting.md); where they differ from this record, this record wins.

This amends [ADR 0024](0024-hikari-ai.md). It replaces these parts of it:

- "Providers are backend adapters behind the application boundary";
- "The Claude Agent SDK isn't embedded";
- "Claude subscriptions come through the ACP Claude Agent";
- the Order section.

The rest of ADR 0024 stands. That includes no AI in core, off and silent until a provider is added, keys in QtKeychain, per-role models, the panel, the three permission modes and the always-prompt list.

Amended by [ADR 0029](0029-the-ai-sidecar-runs-in-an-os-sandbox.md): the sidecar runs in an OS sandbox with no network and no keys; its requests go out through Qt, and Qt starts the agent CLIs, each in its own sandbox.

## Boundary

- **The sidecar does:** provider requests on the official SDKs (`openai`, `@anthropic-ai/sdk`, `@google/genai`), prompt caching, the agent loop, the model calls for every role, the MCP client ([AI13 #441](https://github.com/altqx/hikari/issues/441)), the MCP server protocol ([AI3 #431](https://github.com/altqx/hikari/issues/431)) and the external agents ([AI15 #443](https://github.com/altqx/hikari/issues/443)).
- **Qt keeps:**
  - the AI settings (the keyless provider config and the role models) and the keys;
  - the threads, saved in the user folder;
  - the permission modes, the always-prompt list and the review;
  - every piece of UI.
- **Documents only through the Hikari API.** The sidecar never opens a subtitle, a video or a user file itself.
- **Qt decides every permission.** It checks each write against the thread's mode and the always-prompt list, and every write stages in the review. The sidecar's own permission logic is never trusted.
- **AI-written Lua** still runs in the Lua helper ([ADR 0006](0006-isolated-lua-automation-host.md)), not in the sidecar.

## Runtime and build

- **Bun 1.4**, one executable per platform, built with `bun build --compile --bytecode --no-compile-autoload-dotenv --no-compile-autoload-bunfig` and cross-built from Linux for linux-x64 and windows-x64. Without the two `--no-compile-autoload-*` flags, a `.env` file in the working directory sets variables inside the binary.
- **The latest Bun every release.** Bun supports only "1.x.x" and has no LTS line, so every HikariSub release rebuilds the sidecar on the latest Bun 1.x.
- **Pinned toolchain.** The Bun toolchain and the target runtimes are upstream's baseline x86-64 builds, which run on CPUs without AVX2. They are fetched by SHA-256 the way harper-ls is, and passed with `--compile-executable-path`.
- **The source** is a TypeScript package at `src/ai-sidecar/`, built from the same commit as the app. The build is reproducible byte for byte.
- **Deno 2.9 LTS** is the fallback if Bun stops fitting.

## Starting and stopping

- **One long-lived sidecar per app instance.** It starts on the first AI action after a provider or agent exists. Startup, opening Settings › AI and adding a provider don't start it.
- **A cleaned environment.** Qt starts the sidecar with an allowlisted environment. Nothing provider-related is inherited: no `*_BASE_URL`, `*_API_KEY`, `ANTHROPIC_*`, `OPENAI_*`, `GEMINI_*`, `GOOGLE_*`, `OTEL_*` or `NODE_OPTIONS`.
  - Its working directory is a private folder in the user folder, never a subtitle's folder.
  - The sidecar passes every key and base URL to the SDKs explicitly, so the SDKs' environment fallbacks find nothing.
- **Stdin closed means exit.** The sidecar calls `process.exit` when stdin ends. Bun otherwise stays alive while a top-level `await` is pending.
- **Shutdown.** Qt closes stdin, waits 3 s, then kills the sidecar, using ADR 0006's figures. Agent CLIs and MCP servers the sidecar started go with it:
  - on Windows the sidecar runs in a job object with kill-on-close;
  - on Linux it runs in its own process group, and Qt kills the group.
- **Crashes.** Qt treats any of these as a crash: the process exits, a stdout line isn't JSON, or `initialize` gets no answer within 10 s.
  - In-flight requests fail with `SidecarLost`. An interrupted turn isn't rerun, and edits already staged stay in the review.
  - The next AI action restarts the sidecar, with backoff. After three crashes in a minute, Qt stops and shows the sidecar's log (the newest 64 KiB of stderr).
  - Qt owns the threads and replays their history into `session/new`, so a restart loses nothing.

## The link between Qt and the sidecar

- **Transport: stdin and stdout.** Before loading any dependency, the sidecar takes stdout for the protocol and sends `console.*` to stderr.
- **Framing:** JSON-RPC 2.0, one UTF-8 message per line, which is MCP's stdio framing. Both sides send requests.
- **Message shapes.** Agent sessions use ACP's messages: `session/new`, `session/prompt`, `session/cancel`, `session/update` and `session/request_permission`. The panel ([AI7 #435](https://github.com/altqx/hikari/issues/435)) therefore reads the same events from Hikari's own agent and from an external ACP agent. Hikari's own methods are `_hikari/*`:

| Direction | Method | Purpose |
| --- | --- | --- |
| Qt → sidecar | `initialize` | protocol version, HA1's `kApiVersion`, build id |
| Qt → sidecar | `session/new`, `session/prompt`, `session/cancel` | agent turns |
| Qt → sidecar | `_hikari/providers.set` | the keyless provider config and role models |
| Qt → sidecar | `_hikari/models.list` | catalogue refresh |
| Qt → sidecar | `_hikari/complete` | one-shot role calls: inline actions, translation passes, the judge |
| sidecar → Qt | `session/update` | text, tool calls, plans, usage and cost |
| sidecar → Qt | `session/request_permission` | Manual mode and the always-prompt list |
| sidecar → Qt | `_hikari/api.call`, `_hikari/api.subscribe` | Hikari's tools through HA1 |
| sidecar → Qt | `_hikari/secret.store` | refreshed OAuth tokens |

- **Streaming.** The sidecar coalesces text deltas into at most one notification per frame interval.
- **Cancel.** Turns are cancelled with `session/cancel`; any other request with a `$/cancel_request` notification that carries its id. A cancelled request still gets exactly one response, with the cancel error code.
- **Limits** are `HelperHost`'s: 64 outstanding requests, 16 MiB outstanding and 64 MiB per message. A larger or malformed line fails the session.
- **Versioning.** `initialize` carries an integer protocol major version, HA1's `kApiVersion` and the build id, and new features are added through capabilities. Hikari's tools follow HA1's add-only rule.
  - The app and the sidecar come from one commit, so a mismatch means a broken install. Qt refuses that sidecar and offers to download it again.

## Hikari's tools

- **One call into HA1.** The sidecar calls tools with `_hikari/api.call {operation, params}`, which goes straight into HA1's `Api::call` under a sidecar `Caller`. HA1 then:
  - validates the params against the operation's schema;
  - refuses stale revisions and busy writes;
  - scopes the documents to the active tab and the tabs the user `@`-attached.
- **An in-process MCP server.** Inside the sidecar, the same operations become an in-process MCP server for the agent SDKs.
- **MCP over ACP later.** The sidecar moves to MCP over ACP once that RFD is published. It is a draft today.
- **The judge.** In Auto mode, Qt builds the judge's input: the action and the user's messages, never file content or tool output. Qt then asks for the judge role through `_hikari/complete`, so the sidecar can't approve its own writes.
- **When a listener opens.** Only while something outside the sidecar needs Hikari's tools, and never with AI off. There are two cases:
  - **The MCP server for external agents (AI3).** The user turns it on. Qt owns the listeners: a QLocalServer for the stdio bridge (`UserAccessOption`, a random name and a token) and Streamable HTTP on 127.0.0.1 (the Origin check and a token). The sidecar speaks MCP and relays every call to `_hikari/api.call`.
  - **An agent session in Hikari that can't take tools in-band** (Codex, ACP agents, until MCP over ACP is published). The sidecar registers itself as that agent's stdio MCP server, through a private channel with a per-session token. The channel lasts only as long as that session.

## Agents

All three follow [opencode PR 58782](https://github.com/anomalyco/opencode/pull/58782).

- **Claude: the Claude Agent SDK** drives the user's own installed `claude` (`pathToClaudeCodeExecutable`).
  - Settings: `settingSources: []`, `tools: []`, `strictMcpConfig`, and `permissionMode: "dontAsk"` with only Hikari's tools allowed.
  - Hikari's tools come in over the in-process MCP server, and PreToolUse and PostToolUse hooks run.
  - There is one long-running query per thread, and never `--bare`.
  - The user signs in to their own `claude`. Hikari never starts, reads, stores or relays the Claude login.
  - ADR 0024's rule stands: ask Anthropic before advertising Claude subscriptions.
- **Codex: `codex app-server`** over stdio JSON-RPC, using the user's installed `codex`.
  - The types are generated from `codex app-server generate-json-schema` for a pinned minimum version, and features are detected rather than assumed. OpenAI labels app-server experimental.
  - The sandbox is read-only, and every `commandExecution` and `fileChange` approval is declined.
  - Sign-in stays with `codex login` unless the owner decides otherwise in AI15.
- **Gemini CLI and custom agents: ACP** (`gemini --acp`, a registry entry or a user command). Permission requests for anything but Hikari's tools are declined. Hikari never reuses Gemini CLI's OAuth.
- **Every agent edits only through Hikari's tools,** into the review, under the thread's mode.
- **Hikari never downloads or installs a vendor CLI.** It finds the CLI on `PATH` or at a path the user sets.

## Keys

- **Qt opens the keychain.** It reads keys with QtKeychain and sends only the key a request needs, inside that request on the pipe.
- **The sidecar never opens the keychain.** It holds a key only for the request that carries it, and never writes one to disk, its environment, a log or stderr.
- **OAuth** ([AI6 #434](https://github.com/altqx/hikari/issues/434)): tokens refresh in the sidecar, where the HTTP call is. The sidecar sends the new tokens to Qt with `_hikari/secret.store`, and Qt writes them to the keychain.
- **Agent CLIs** use the user's own login. Hikari passes them no key.
- **The process boundary separates crashes, not trust.** Code running as the user can read what the user can. The defences are the supply-chain rules below and, later, the OS sandbox.

## Network

- **An egress rule in the sidecar.** Every SDK client gets a custom `fetch` that:
  - allows only the base URLs of providers the user added;
  - refuses redirects;
  - applies AI5's plain-HTTP key rule: HTTPS, loopback, or a Tailscale address with a local address in the same range, resolved once and pinned ([#637](https://github.com/altqx/hikari/pull/637)).
- **The egress rule isn't a security boundary.** It keeps Hikari's own code honest. Bun has no permission model, so a malicious dependency could reach the network directly.
- **The only limit Qt enforces is not starting the sidecar.** With AI off, or with no provider or agent, nothing runs.
- **A real limit needs an OS sandbox.** Containing a running sidecar needs Landlock on Linux or an AppContainer on Windows; that is a follow-up card. The "What is sent" guide says so.

## Download, verification and update

- **A separate download.** The sidecar isn't in the AppImage, deb, rpm, AUR packages, NSIS installer or portable archives. Each HikariSub GitHub release carries one asset per platform, `hikari-ai-<version>-<platform>.gz`. It is a single gzip-compressed executable holding both Bun and the bundle, about 49 MiB on Linux and 52 MiB on Windows (measured on Bun 1.4.4).
- **When it downloads.** The download happens the first time the user adds a provider or agent, and the user is told what is downloaded and its size. It never happens while "Turn off all AI features" is on.
- **Integrity.** The asset's SHA-256 and size go in that release's `hikarisub-update.json` ([ADR 0019](0019-installers-and-seamless-updates.md)). This is a new member that only adds, so the manifest stays at schema 1.
  - The app reads the manifest of its own release over HTTPS.
  - Nothing is signed. As with app updates, HTTPS plus SHA-256 is the integrity check.
  - A file that fails the check is deleted and never run.
- **Storage.** The sidecar is kept in the user folder under `ai/sidecar/<version>/`, keyed by the app's version.
  - After an app update, the matching sidecar downloads with the update, or else on the next AI action.
  - The old copy is removed once the new one verifies.
  - An app never runs a sidecar from another version.
- **Developer and test builds** point at a locally built sidecar through a CMake option and download nothing.
- **Licence notices.** The executable embeds the licence notices, and `hikari-ai --licenses` prints them. They cover Bun (MIT), JavaScriptCore and WebCore (LGPL-2), tinycc (LGPL-2.1) and every bundled package. Bun publishes its patched WebKit, and the sidecar's source and build are in this repository, so the sidecar can be rebuilt and relinked. Release checks cover these obligations, as they do for the app's other modules.

## Supply chain

- **Locked and frozen.** Versions are exact, `bun.lock` is committed, and CI installs with `bun ci --ignore-scripts`. That install is frozen, runs no lifecycle scripts and has no `trustedDependencies`.
- **A cooldown.** `minimumReleaseAge = 7d` is set in the package's `bunfig.toml`, and lock diffs are reviewed like code.
- **Audit.** CI runs `bun audit` as a report, not a gate, so a third-party advisory can't block a release.
- **Few dependencies:** the provider SDKs, the MCP and ACP SDKs, and their dependencies. The MCP SDK stays on 1.x while the Claude Agent SDK's peer range is `^1.29`.

## The Claude Agent SDK is fetched, never shipped

`@anthropic-ai/claude-agent-sdk` says "© Anthropic PBC. All rights reserved. Use is subject to the Legal Agreements", and HikariSub is GPL-3.0. HikariSub therefore never ships it:

- **Not in anything HikariSub publishes.** The SDK isn't in the app packages, the sidecar asset or this repository. In the sidecar build it is a dev dependency for types only, and it is external to the bundle.
- **Fetched at enable time.** When the user first picks the Claude agent, the sidecar:
  - fetches the SDK's tarball from registry.npmjs.org at the version in `bun.lock`;
  - checks it against the lockfile's sha512 integrity;
  - unpacks it into the user folder under `ai/vendor/claude-agent-sdk/<version>/`, with its peers (`@anthropic-ai/sdk`, `@modelcontextprotocol/sdk` and `zod`) at the lockfile's versions;
  - loads it with a dynamic `import()`.
- **No vendor binary.** The SDK's optional native packages, about 330 MB each, are never fetched. The SDK runs the user's own `claude`.
- **No redistribution, confirmed.** No HikariSub artifact contains the SDK. npm delivers Anthropic's own package to the user, who uses it under Anthropic's terms. GPL-3.0 restricts conveying, not running (section 2), and HikariSub conveys only its own code.
- **Redistribution only.** This doesn't settle whether Anthropic allows the Claude subscription path for an agent built on the SDK. That is still the question ADR 0024 says to ask before advertising.
- **A release check** reads the bundler's list of inputs and fails if any file comes from the SDK.

## "Turn off all AI features"

This is [AI20 #649](https://github.com/altqx/hikari/issues/649).

- **Nothing AI is built.** With the switch on, the composition root constructs no AI object: no `KeychainKeyStore`, no `Providers`, no sidecar host, no AI command, no panel and no listener. Nothing is downloaded, read from the keychain, started or listening.
- **The switch lives in ordinary settings,** not in the keychain.
- **An earlier download stays.** A sidecar downloaded before stays on disk, inert, like the threads and keys.
- **No restart needed.** Turning AI off at runtime stops the sidecar, closes the listeners and unregisters the commands. Turning it on constructs them on demand.

## Tests

Tests use mock servers only. No test calls a real provider or a real vendor CLI, and no key appears in the repository or the logs.

- **AI off.** This test comes from Zed's 2026-10-28 fix for "LLM credentials being read when `disable_ai` was set to `true`" ([zed#66029](https://github.com/zed-industries/zed/pull/66029)).
  - A counting fake `KeyStore` sees zero reads across startup and a session.
  - No child process and no listening socket exist.
  - The command registry and the menu ledgers hold no AI command.
- **No provider.** No sidecar starts and nothing downloads.
- **The host**, in C++ against a fake sidecar:
  - a handshake timeout, and a version refusal;
  - a non-JSON stdout line restarts the sidecar;
  - a crash mid-turn fails with `SidecarLost` and leaves staged edits in place;
  - three crashes in a minute stop the restarts;
  - shutdown closes stdin, then kills, and the process tree goes with it;
  - a `.env` and a `bunfig.toml` in the working directory, and `ANTHROPIC_BASE_URL` in Qt's environment, don't reach the sidecar;
  - a sentinel key never appears in argv, the environment, stderr or the logs;
  - a download with the wrong hash is deleted and never run.
- **The sidecar**, under `bun test`:
  - AI5's provider tests, #637's route tests, [#641](https://github.com/altqx/hikari/pull/641)'s Gemini tests and AI19's caching cases, ported against local mock servers;
  - a dependency's `console.log` lands on stderr;
  - the sidecar exits when stdin ends;
  - a cancelled request gets exactly one response;
  - going over a limit fails the session.
- **Agents.** Fake `claude`, `codex` and ACP agents speak each protocol. The tests check that built-in tools stay off and that every write goes through `_hikari/api.call`.
- **Licence.** The release check above.

## Order

1. **A new sidecar-host card**, together with AI20's non-UI half. It covers:
   - `AiSidecarHost` on `HelperHost`'s patterns, and the TypeScript skeleton;
   - the build, the download and verification, and the cleaned start;
   - the link and `_hikari/api.call`;
   - the AI-off tests.
2. **The AI5 port.** OpenAI, Anthropic, OpenRouter, custom endpoints and Gemini ([AI18 #639](https://github.com/altqx/hikari/issues/639)) move to the official SDKs, along with the egress rule and caching ([AI19 #640](https://github.com/altqx/hikari/issues/640)).
   - The port PR deletes `ai_wire`, `ai_network`, and `Providers::stream`, `cancel` and `refreshModels`.
   - `KeychainKeyStore`, the keyless `Config`, `JsonConfigStore` and the rule that a provider is added only once its key is stored stay in C++ as the Settings model.
3. **AI1 and AI3.** [AI1 #429](https://github.com/altqx/hikari/issues/429) exports HA1's operations through `_hikari/api.call`, and AI3's MCP server runs in the sidecar.
4. **AI2, AI7 and AI6.** AI2 is unchanged. In AI7, the loop and the judge's model call run in the sidecar and the panel is in QML. AI6's sign-in runs in the sidecar, and Qt stores the tokens.
5. **AI13 and AI15** go in the sidecar.
6. **The tool cards** (AI4, AI8–AI12, AI14, AI16 and AI17): HA1 operations in C++, prompts and orchestration in TypeScript.
7. **AI20's UI**, after its design gate.

**The C++ reference.** #637 and #641 merge on green and stay as the reference until the TypeScript port passes the same cases; the port PR then deletes them. AI19's paused C++ branch isn't merged, and its cases move to the port.

Amended by [ADR 0030](0030-open-a-folder-as-a-project.md): the AI reaches the window's Project only through Hikari API folder grants. The sidecar's working directory stays its private folder, and Claude's and Codex's built-in file tools stay off.

Amended by [ADR 0031](0031-the-ai-sidecar-updates-on-its-own.md): the sidecar updates on its own from the `ai-channel` release, as a runtime and a bundle. An app runs any sidecar with its protocol major and a required API no newer than its own, so the one-commit rule, the refusal of a sidecar from another build and the download from the app's own release no longer apply.
