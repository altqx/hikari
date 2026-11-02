# Hikari AI sidecar: runtime, IPC, secrets and migration

On 2026-11-02 the owner decided that all AI work moves into a TypeScript sidecar process that the Qt app starts and talks to. That covers the provider clients, the agent SDKs, the MCP client and server, prompt caching and the agent loop. The C++ side keeps the UI, the Hikari API ([HA1 #411][HA1]), the staged review ([AI2 #430][AI2]) and the keys in the OS store. The owner's record of that session (brain page `inbox/2026-11-02-47622ba2`) adds four points:

- the sidecar reaches documents only through the Hikari API;
- the runtime is a separate download fetched when AI is turned on;
- the Claude Agent SDK drives the user's installed `claude`, with its built-in tools off and Hikari's tools over MCP;
- the merged AI5 layer, [#637] and [#641] are to be ported to TypeScript and then deleted.

This note covers the runtime, the link between Qt and the sidecar, secrets, the supply chain, the "Turn off all AI features" switch ([AI20 #649][AI20]) and the migration. How each provider's agent SDK is hosted (licences, logins, the opencode approach) is in [agent-sdk-hosting.md](agent-sdk-hosting.md) and isn't repeated here.

Sources read:

- HikariSub `origin/main` at `8ed3e6c21` (2026-11-02): [ADR 0024](../adr/0024-hikari-ai.md), [ai-providers.md](../ai-providers.md), `src/application/ai_providers.*`, `src/backends/ai_*`, `src/api/`, `src/backends/helper_*`, `cmake/HikariHarper.cmake` and `packaging/`. The AI cards AI0–AI20 and PRs [#637] and [#641] were read through `gh`.
- Vendor documentation, specifications and release pages, all fetched on **2026-11-02**. Where a page doesn't say something, this note says so.
- **Measurements** taken on this machine on 2026-11-02 (Linux x86-64, 12 cores, 31 GiB). They are marked *measured* and described in [Method](#method). The machine was under load (load average 8–11) during the runs, so treat the timings as relative, not absolute. Nothing was measured on Windows; Windows figures are file sizes only.

## Summary

| Question | Recommendation |
| --- | --- |
| Runtime | **Bun 1.4, `bun build --compile --bytecode`**, one executable per platform. Bun runs **JavaScriptCore, not V8**. Deno 2.9 LTS is the fallback if the owner wants V8, an LTS security line or a smaller download. |
| IPC | **Newline-delimited JSON-RPC 2.0 over the sidecar's stdin/stdout**, with requests both ways. Agent sessions follow ACP's message shapes; Hikari's own calls are `_hikari/*` extension methods. A QLocalServer opens only when the user turns on the MCP server for external agents. |
| Calling Hikari's tools | One typed request, `_hikari/api.call {operation, params}`, which goes straight into HA1's `Api::call` under a sidecar `Caller`. Qt enforces scope, permission modes, the always-prompt list and the staged review. Inside the sidecar the same operations are offered to agent SDKs as an in-process MCP server. |
| Secrets | **Qt reads keys with QtKeychain and hands the sidecar only the key a request needs.** The sidecar never opens the keychain and never writes a key to disk or the environment it controls. Refreshed OAuth tokens go back to Qt to store. |
| Process model | **One long-lived sidecar per app instance**, started lazily on the first AI action after a provider exists. It is stateless across restarts: Qt keeps the threads. |
| Disable AI | With the switch on, nothing in Qt constructs any AI object: no keychain read, no sidecar, no listener, no AI commands. The runtime isn't downloaded until AI is turned on. |
| C++ AI5 | Keep the keychain store and the keyless provider config (the Settings model). Port the behaviour and the tests of the wire layer to TypeScript on the official SDKs, then delete `ai_wire`, `ai_network` and `Providers::stream`. Close #637 and #641 unmerged and port their tests. |

## 1. Runtime

### Candidates

| Runtime | Engine | Single-file build | Licence |
| --- | --- | --- | --- |
| Bun 1.4.4 | **JavaScriptCore** (WebKit), statically linked ([Bun licensing]) | `bun build --compile` (stable), `bun compile` AOT (experimental) ([Bun compiler]) | MIT; JavaScriptCore and WebCore LGPL-2, tinycc LGPL-2.1, both statically linked. Bun publishes its patched WebKit so it can be relinked ([Bun licensing]) |
| Deno 2.9.8 | V8 | `deno compile`, built on the stripped `denort` runtime ([Deno compile]) | MIT ([deno repo]) |
| Node.js 26.13 SEA | V8 | `node --build-sea`, since v25.5.0; "Stability: 1.1 - Active development" ([Node SEA]) | MIT plus bundled dependencies ([Node licence]) |
| Node + JS bundle | V8 | none: ship `node(.exe)` and a `.mjs` | as above |
| LLRT 0.9.0-beta | QuickJS | built in | Apache-2.0; "an **experimental** package … intended only for evaluation purposes", "**NOT** a drop in replacement for Node.js" ([LLRT]) |
| txiki.js 26.11 | QuickJS-ng | none | MIT ([txiki.js]) |
| Hermes | Hermes | none | MIT; made for React Native ([Hermes]); no Node APIs |

The owner's brief says "a lightweight V8 runtime like Bun". To be plain: **Bun is not V8.** Of the credible options, only Deno and Node use V8.

### Can each one run the SDKs? (*measured*)

A probe script ran the following against a local mock server, with no real provider contacted ([Method](#method)):

- `openai` 7.47.0: Chat Completions stream;
- `@anthropic-ai/sdk` 1.3.0: Messages stream;
- `@google/genai` 2.32.0: `generateContentStream`;
- `@modelcontextprotocol/sdk` 1.32.2: a stdio client calling a tool on a stdio server child;
- `child_process.spawn` with piped stdio;
- `@anthropic-ai/claude-agent-sdk` 0.3.316: a full `query()` turn through its `claude` CLI;
- `@openai/codex-sdk` 0.166.0: a `thread.run()` through its `codex` CLI, which reported the mock's 401.

| Runtime | 3 provider SDKs | MCP stdio | spawn + stdio | Claude Agent SDK | Codex SDK |
| --- | --- | --- | --- | --- | --- |
| Node 26.13 (`node`) | pass | pass | pass | pass | pass |
| Bun 1.4.4 (`bun`) | pass | pass | pass | pass | pass |
| Deno 2.9.8 (`deno run -A`) | pass | pass | pass | pass | pass |
| `bun build --compile` binary | pass | pass | pass | pass¹ | pass¹ |
| `deno compile` binary | pass | pass | pass | pass¹ | pass¹ |
| Node SEA binary | pass | pass | pass | pass¹ | pass¹ |
| LLRT 0.9.0-beta | `openai` only | loads | not run | fails | fails |

¹ Given an explicit path to the CLI (`pathToClaudeCodeExecutable`, `codexPathOverride`). The Claude Agent SDK README says so for Bun: "`require.resolve` doesn't work from inside the compiled `$bunfs`". It documents embedding and extracting the CLI with `extractFromBunfs` ([Agent SDK README]). Since the owner chose the user's installed `claude`, the path comes from the user's setup anyway.

Under LLRT, `@anthropic-ai/sdk`, the Claude Agent SDK and the Codex SDK fail on a missing `readline`, and `@google/genai` fails on a missing `node:http`. LLRT, txiki.js and Hermes are out.

MCP SDK v2 (`@modelcontextprotocol/server` and `/client` 2.4.0) is now the stable line for the 2026-07-28 spec. v1.x gets fixes for at least six months after 2026-07-27 ([MCP TS SDK]). A v2 stdio round trip passed on all three runtimes (*measured*). The Claude Agent SDK still declares `@modelcontextprotocol/sdk ^1.29` as a peer ([Agent SDK README]), so the sidecar may carry both lines for a while.

What the SDKs themselves claim:

- `openai`: Node 22/24, Deno 1.28+, Bun 1.0+ ([openai-node]);
- `@anthropic-ai/sdk`: Node 20+, Deno 1.28+, Bun 1.0+ ([anthropic-sdk-typescript]);
- MCP SDK: Node, Bun and Deno ([MCP TS SDK]);
- `@google/genai`: Node 20+, and Node 22+ from 3.0.0 ([js-genai]);
- Codex SDK: "Requires Node.js 18+". It "spawns the CLI and exchanges JSONL events over stdin/stdout" ([Codex SDK]).

### Size (*measured*, MiB)

"hello" is `console.log(1)`. "sidecar" bundles all seven packages above (a 3.1 MB bundle) and doesn't include the agent CLIs.

| Build | linux-x64 hello | linux-x64 sidecar | linux sidecar, xz -6 | win-x64 hello | win-x64 sidecar | win sidecar, xz -6 | win-arm64 hello |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `bun build --compile` | 104.1 | 106.9 | 38 | 109.9 | 112.7 | 40 | 95.6 |
| `deno compile` | 100.1 | 103.1 | 23 | 76.7 | 79.6 | 22 | 70.1 |
| Node SEA | 143.7 | 146.6 | 30 | ≈95.5² | not built | — | ≈84.2² |

² The size of `node.exe` in the official v26.13.0 zips. A SEA adds its blob, about 3 MiB here.

For scale, HikariSub 0.1.0-beta.3 ships a 108 MiB Windows installer and a 163 MiB AppImage. As a separate download, the Bun sidecar adds about 40 MiB compressed; Deno adds about 22 MiB.

Not in the sidecar: the agent CLIs are large. The `claude` binary in `@anthropic-ai/claude-agent-sdk-linux-x64` is 313 MiB and is itself a Bun single-file executable: it has `.bun` ELF sections, and Anthropic says "Claude Code ships as a Bun executable" ([Bun joins Anthropic]). `@openai/codex-linux-x64` unpacks to 436 MiB. Under the owner's decision these come from the user's own installs (see [agent-sdk-hosting.md](agent-sdk-hosting.md)).

### Startup and memory (*measured*, Linux)

Median of 15 runs; RSS is peak resident memory. The "sidecar" rows load all seven packages, then exit.

| Case | Median ms | Peak RSS MiB |
| --- | --- | --- |
| `bun --compile` hello | 4 | 8 |
| `deno compile` hello | 15 | 33 |
| Node SEA hello | 26 | 42 |
| `bun --compile` sidecar | 155 | 53 |
| **`bun --compile --bytecode` sidecar** | **56** | **46** |
| `deno compile` sidecar | 248 | 95 |
| Node SEA sidecar | 236 | 109 |
| Node SEA sidecar, `useCodeCache` | 183 | 98 |
| `node` + bundle | 248 | 109 |
| `bun` + bundle | 185 | 72 |

The sidecar starts once per app session, so startup matters less than resident memory. Bun uses about half the memory of the V8 options.

### Platform, glibc, Windows and ARM64

| | Bun | Deno | Node SEA |
| --- | --- | --- | --- |
| Cross-compile from Linux | yes, to every target ([Bun executables]) | yes, to every target ([Deno compile]) | yes via `"executable"`, but `useCodeCache` and `useSnapshot` must be off ([Node SEA]) |
| Windows ARM64 | since 1.3.10 ([Bun 1.3.10]) | since 2.9.3 ([Deno compile]) | official `win-arm64` builds ([Node dist]) |
| Highest glibc symbol (*measured*) | 2.17 | 2.27 | 2.28 |
| x64 CPU floor | one binary targeting Nehalem (SSE4.2), with AVX2/AVX-512 picked at runtime ([Bun executables]) | not stated | not stated |
| Windows signing | signtool works since 1.2.19 (payload in a `.bun` PE section, [Bun 1.2.19]); the original signature is stripped since 1.2.23 ([Bun 1.3]) | `signtool sign /fd SHA256 main.exe` ([Deno compile]) | `signtool sign /fd SHA256 sea.exe`, optional ([Node SEA]) |

HikariSub ships x86-64 only (`arch=('x86_64')` in the AUR template), and the Linux floor is AlmaLinux 9 (glibc 2.34). All three fit. Nothing is code-signed today ([ADR 0019](../adr/0019-installers-and-seamless-updates.md)), so signing is a future option, not a gate.

### Reproducible builds (*measured*)

The sidecar was built twice per tool and target and the hashes compared. Bun (linux and windows), Deno (linux and windows) and Node SEA (linux) are **byte-identical** when the output file name and the toolchain match. The output name is embedded, and on Windows the PE checksum follows it. Bun's docs add that an executable using `__dirname`, `__filename` or `require.resolve()` embeds the build path ([Bun executables]). All three start from the vendor's prebuilt runtime, so "reproducible" means *from the pinned runtime*, not from source.

Cross-compiling downloads the target's runtime: Bun downloads it from npm ([Bun 1.3.6]) and Deno fetches `denort-<target>.zip` from `dl.deno.land` ([Deno compile]). Neither page says whether the download is verified. Pin it the way `cmake/HikariHarper.cmake` pins harper-ls: fetch by SHA-256, then pass `--compile-executable-path` (Bun) or set `DENORT_BIN` (Deno).

### Release cadence and security fixes

| | Cadence (from the GitHub releases) | Security track |
| --- | --- | --- |
| Bun | 1.4.0 on 2026-08-20, then 1.4.1–1.4.4 by 2026-10-22 | `SECURITY.md` supports "1.x.x" only. There is no LTS line, so fixes land on the latest release. Acquired by Anthropic on 2025-12-02; "stays open-source & MIT-licensed" ([Bun joins Anthropic]) |
| Deno | minor every 12 weeks, patches "as needed" | an LTS line: 2.9 from v2.9.3 until 2027-01-31, with security patches backported ([Deno stability]) |
| Node | even majors get LTS: 26 entered LTS on 2026-10-28 and is supported until 2029-04-30 ([Node schedule]) | the long-standing security release process ([Node security]) |

With Bun, HikariSub must rebuild the sidecar on the latest Bun for each release, because older 1.x lines don't get fixes.

### Traps found

- **Bun reads `.env` and `bunfig.toml` from the working directory by default** in compiled executables ([Bun executables]). *Measured:* a `.env` in the working directory set a variable inside the binary. `--no-compile-autoload-dotenv --no-compile-autoload-bunfig` stopped it. A subtitle folder must never be able to set `ANTHROPIC_BASE_URL`. `deno compile` didn't read it.
- **Node SEA honours `NODE_OPTIONS`** unless the config sets `"execArgvExtension": "none"` ([Node SEA]).
- **The SDKs read environment variables.** Among them are `OPENAI_BASE_URL`, `OPENAI_API_KEY`, `ANTHROPIC_BASE_URL`, `ANTHROPIC_PROFILE`, `GEMINI_API_KEY`, `GOOGLE_GEMINI_BASE_URL` and OpenTelemetry switches (found by reading `node_modules`). This machine's own shell sets `ANTHROPIC_BASE_URL`. Qt must start the sidecar with a scrubbed environment, and the sidecar must pass every key and base URL explicitly.
- **Bun stays alive after stdin EOF while a top-level `await` is pending; Node and Deno exit** (*measured*). The sidecar must call `process.exit` on stdin `end`. The MCP stdio binding asks the same of servers ([MCP stdio]).
- **`deno compile` embeds the whole `node_modules`** by default ([Deno compile]). Here that was 837 MB, including both agent CLIs. Bundle first, or use `--exclude-unused-npm`/`--bundle`.
- Bun's `node:child_process` is marked partial: "serialization: "advanced" only works between Bun processes" ([Bun Node compat]). JSON over stdio isn't affected.

### Packaging fit

Under the owner's decision the runtime is a separate download, so the AppImage, deb, rpm, AUR package and NSIS installer don't change. The app fetches `hikari-ai-<version>-<platform>` from the same GitHub release, checks its SHA-256 from the release manifest, as [ADR 0019](../adr/0019-installers-and-seamless-updates.md) does for updates, and keeps it in the user data folder. If the owner later bundles it instead, it is one more file next to `harper-ls`: an `install` line in CMake, an NSIS `File` line, and a `[ ! -d licenses ]` copy for Bun's and WebKit's licences. A single static-ish binary needs no change to `collect-runtime.py` or the `deps-*.map` files.

## 2. IPC between Qt and the sidecar

### Transport

| | stdio (QProcess pipes) | QLocalSocket / QLocalServer |
| --- | --- | --- |
| Setup | none: the pipes exist when QProcess starts the child | listen on a name. Unix: a socket file (permissions from the umask unless `UserAccessOption` is set; macOS ignores them). Windows: a named pipe; `UserAccessOption` restricts it to the user ([QLocalServer]) |
| Peer identity | implicit: only the parent holds the pipe | anyone who can open the name. Needs a random name plus a token |
| Stray output | a dependency's `console.log` corrupts the stream | stdout stays free for logs |
| Precedent | MCP stdio, ACP, LSP; `HarperClient`; `HelperHost` | `docs/automation.md` recommends "a dedicated local protocol channel" because native modules write to stdio |

**Recommendation: stdio.** The sidecar takes `process.stdout` for the protocol before loading any dependency, and rebinds `console.*` to stderr. A test makes a dependency call `console.log` and checks that it lands on stderr. Any non-JSON line on stdout is a protocol violation and restarts the sidecar. Native addons are what made `automation.md` choose a separate channel, and the sidecar loads none. QLocalServer is still needed for one case: the AI3 stdio bridge, which an external agent such as Claude Code starts, not Qt.

### Framing and message shape

- **Framing:** one JSON-RPC 2.0 message per line, UTF-8, no embedded newlines. That is MCP's stdio framing, which MCP recommends reusing on any byte stream ([MCP transports], [MCP stdio]). LSP's `Content-Length` headers ([LSP]) aren't needed, because JSON escapes newlines.
- **Both sides send requests.** The 2026-07-28 MCP revision forbids server-initiated requests ([MCP transports]), so plain MCP can't carry Qt-to-sidecar control and sidecar-to-Qt tool calls on one link. ACP can: the client and the agent each expose methods. Agent sessions use `session/new`, `session/prompt`, the `session/cancel` notification and `session/update` notifications, and the agent asks for `session/request_permission` ([ACP overview]).
- **Agent sessions follow ACP's shapes.** The panel (AI7) then reads the same event stream whether a turn runs in Hikari's own sidecar or in an external ACP agent (AI15). The TypeScript side can use ACP's SDK types. ACP allows custom methods prefixed with `_` and custom data in `_meta` ([ACP overview]).

| Direction | Method | Purpose |
| --- | --- | --- |
| Qt → sidecar | `initialize` | protocol version, `hikariApiVersion` (HA1's `kApiVersion`), build id |
| Qt → sidecar | `session/new`, `session/prompt`, `session/cancel` | agent turns (AI7) |
| Qt → sidecar | `_hikari/providers.set` | the keyless provider config and role models |
| Qt → sidecar | `_hikari/models.list` | catalogue refresh |
| Qt → sidecar | `_hikari/complete` | one-shot role calls: inline actions (AI8), translation passes (AI10), the judge |
| sidecar → Qt | `session/update` (notification) | text, tool calls, plans, usage and cost |
| sidecar → Qt | `session/request_permission` | Manual mode, and the always-prompt list |
| sidecar → Qt | `_hikari/api.call`, `_hikari/api.subscribe` | Hikari's tools through HA1 |
| sidecar → Qt | `_hikari/secret.store` | refreshed OAuth tokens (AI6) |

### Streaming, cancellation and back-pressure

- **Streaming:** deltas arrive as notifications. The sidecar coalesces text deltas, at most one notification per frame interval, so a fast model can't flood the GUI thread.
- **Cancellation:** JSON-RPC 2.0 has none ([JSON-RPC]). Use `session/cancel` for turns, and for other requests a `$/cancel_request` notification carrying the request id, as ACP does ([MCP over ACP]). Following LSP, a cancelled request still gets a response, with an error code for cancellation ([LSP]). In the C++ model the stream still ends with exactly one `Finished` or `Failed`.
- **Back-pressure and limits:** borrow `HelperHost::Limits`: 64 outstanding requests, 16 MiB outstanding, a 64 MiB maximum message (`kMaxPayload`) and the newest 64 KiB of stderr kept. Qt watches `QProcess::bytesToWrite` before sending large payloads such as frames; the sidecar waits for stdout `drain`. Oversized or malformed lines fail the session.

### How the sidecar calls Hikari's tools safely

Options:

1. **The sidecar as an MCP client of Hikari's MCP server (AI3).** This needs a listener (a socket or a port) even when the user hasn't turned the MCP server on, which AI20 forbids. It is also a second channel.
2. **MCP over ACP:** tools declared as `"type": "acp"` and carried in `mcp/message` on the existing link, "without a second process or network endpoint" ([MCP over ACP]). This is the right shape, but it is a **draft** RFD that targets MCP 2026-07-28, and its SDK support is behind unstable flags ([MCP over ACP]).
3. **A typed RPC: `_hikari/api.call {operation, params}`** maps one-to-one onto `Api::call(caller, operation, params, reply)`. HA1 already validates params against each operation's JSON Schema, marks reads and writes, refuses stale revisions and busy writes, and scopes by `Caller`. Its test client already drives it from JSONL scripts (`tests/api/scripts/*.jsonl`).

**Recommendation:** use option 3 now, and move to option 2 when MCP over ACP is published. Inside the sidecar the same operation list becomes an in-process MCP server for the agent SDKs; the Claude Agent SDK has `createSdkMcpServer()` ([Agent SDK README]). Qt never trusts the sidecar's own permission logic:

- Qt gives the sidecar its own `Caller` id and label, and `allDocuments` only for `@`-attached tabs;
- every write stages in the review (AI2);
- the always-prompt list and the permission modes are enforced in Qt before a write applies;
- the judge runs in the sidecar, but Qt decides which actions need it.

When the user turns on the MCP server for external agents (AI3), the sidecar implements the MCP server protocol and relays to the same `_hikari/api.call`. The stdio bridge connects through a QLocalServer (`UserAccessOption`, random name, token). Streamable HTTP binds to 127.0.0.1 with the Origin check and a token, as AI3 says.

### Versioning

- **The link:** an integer major version in `initialize`, as ACP negotiates it ([ACP init]); features are added through capabilities. The sidecar and the app are built from the same commit and the download is keyed by version, so a mismatch means a broken install. Qt refuses with a clear message, as `HelperHost` refuses a helper (`Kind::Refuse`).
- **Hikari's tools:** HA1's `kApiVersion` (add-only, deprecate before removing).
- **The external MCP server:** MCP 2026-07-28 puts the version on every request and returns `UnsupportedProtocolVersionError` (-32022) on a mismatch ([MCP versioning]). Legacy clients need the `initialize` fallback, so the server should be dual-era.

### Crashes, restarts and the process tree

- **Detection:** `QProcess::finished`, a protocol violation, or no `initialize` answer within 10 s (`HelperHost`'s handshake time).
- **In-flight work:** fails with a new `SidecarLost` error kind, like `HostError::HelperLost`. An interrupted turn isn't rerun automatically, the same rule ADR 0006 sets for macros. Edits already staged in the review stay there, because the review lives in Qt.
- **Restart:** on the next AI action, with backoff. After three crashes in a minute, stop and show the log.
- **Threads:** Qt saves threads in the user folder (ADR 0024) and replays the history into `session/new` after a restart. MCP's stdio binding takes the same stateless stance: in-flight requests "are simply lost and the client can retry them against the fresh process" ([MCP stdio]).
- **Shutdown:** close stdin, wait 3 s, then kill. These are ADR 0006's figures and MCP's order ([MCP stdio]).
- **Child processes** (agent CLIs, user-added MCP servers):
  - Windows: put the sidecar in a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Children join the job by default, and closing the last handle kills the tree ([Job objects]).
  - Linux: start the sidecar in its own process group and kill the group. `PR_SET_PDEATHSIG` is a backstop for the sidecar only; it is tied to the creating *thread* and isn't inherited by children ([PR_SET_PDEATHSIG]).

### Process model

| | One long-lived sidecar | One per session |
| --- | --- | --- |
| Memory | about 46–53 MiB once (*measured*) | that much per open thread, plus the per-turn agent CLIs either way |
| Shared state | one provider catalogue, one MCP client pool, one caching key per thread | each copy reloads |
| Crash scope | every thread fails at once, then restarts from Qt's saved threads | one thread |
| Fit | MCP and ACP assume one client↔agent connection | needs a session router in Qt |

**One long-lived sidecar**, started lazily. The agent SDKs already start one CLI per turn, so per-session isolation exists where untrusted model output acts. AI-written Lua still runs in the Lua helper ([ADR 0006](../adr/0006-isolated-lua-automation-host.md)), not in the sidecar.

## 3. Secrets

The keys stay in the OS store ([ADR 0024](../adr/0024-hikari-ai.md), AI5). The question is who opens the store.

| | Qt reads (QtKeychain), passes per request | Sidecar reads (a keychain library) |
| --- | --- | --- |
| Code | exists and is tested (`KeychainKeyStore`, `AiKeychainTest`) | a second implementation: `Bun.secrets`, "new and experimental" ([Bun secrets]), or a native addon |
| What the sidecar holds | the keys of providers the user added, for the requests that use them | anything the keychain gives this user |
| A compromised npm dependency can read | keys in sidecar memory, which it could take in either design because the sidecar must send them | **any item** the store releases to this user. Secret Service implementations may let "any client application access items or collections unlocked by another" ([Secret Service]). `CredRead` reads "the user's credential set" of the logon session ([CredRead]) |
| AI off | Qt never constructs the store. Zed fixed exactly this on 2026-10-28: "LLM credentials being read when `disable_ai` was set to `true`" ([Zed #66029]) | the sidecar must not start at all, which is already the rule |
| Failure UX | the existing `KeychainUnavailable`/`KeychainDenied` paths | new paths in a second process |

**Recommendation: Qt reads and passes.**

- Keys travel on the stdio pipe, never on the command line.
- For direct provider calls (openai, @anthropic-ai/sdk, @google/genai), pass `apiKey` as a constructor option. An empty environment means the SDKs' env fallbacks find nothing.
- The agent CLIs take keys through the environment; the Codex SDK "injects its required variables (such as `CODEX_API_KEY`)" ([Codex SDK]). A process's initial environment is readable at `/proc/<pid>/environ` by anyone who passes a `PTRACE_MODE_READ_FSCREDS` check ([proc environ]), which in practice means the same user. Under the owner's decision those CLIs use the user's own login, so Hikari passes them no key.
- OAuth (AI6) refresh happens where the HTTP call happens, in the sidecar. It sends new tokens to Qt with `_hikari/secret.store`, and Qt writes them.

Neither design protects keys from malicious code that runs as the same user. The process boundary separates crashes, not trust. Node's own permission docs put it bluntly: the permission model "does not protect against malicious code" ([Node permissions]). The real defences are the supply chain (section 4) and, later, an OS sandbox (Landlock or AppContainer, as the plugin card already plans for "sandboxed" wording).

## 4. Supply chain

npm's 2025 Shai-Hulud worm spread "by injecting malicious post-install scripts into popular JavaScript packages" and stole "multiple types of secrets" ([npm plan]).

| Control | Bun | Deno |
| --- | --- | --- |
| Lockfile, committed | text `bun.lock` | `deno.lock` |
| Frozen install in CI | `bun ci` = `bun install --frozen-lockfile`; a `package.json` that disagrees with the lock is an error ([bun install]) | `deno install --frozen-lockfile` (from `deno install --help`, 2.9.8) |
| Dependency lifecycle scripts | not run unless listed in `trustedDependencies` ([bun install]). *Measured:* `protobufjs`'s postinstall and `@google/genai`'s preinstall were blocked, and both SDKs still worked | run only for packages named in `--allow-scripts` (from `deno install --help`, 2.9.8) |
| Cooldown on new versions | `minimumReleaseAge` ([bun install]) | — |
| Audit | `bun audit` | — |
| Runtime confinement | none in Bun's docs | `--allow-net=host,host`, `--allow-run`, `--allow-env=…` ([Deno permissions ref]). Subprocesses run "outside the sandbox" ([Deno security]) |

Steps for the sidecar package (a `src/ai-sidecar/` or similar):

- Pin exact versions, commit `bun.lock`, and build with `bun ci --ignore-scripts`.
- Set `minimumReleaseAge = 7d` in `bunfig.toml`, and review lock diffs like code.
- Fetch the Bun toolchain by SHA-256 the way harper-ls is fetched (`HIKARI_*_DOWNLOADS` seeding for offline configures). Cross-compile with `--compile-executable-path`.
- Build with `--no-compile-autoload-dotenv --no-compile-autoload-bunfig`, and start the sidecar with an empty environment and a private working directory.
- Run `bun audit` in CI as a report, not a gate, so a third-party advisory can't block a release.

**Network limited to the user's providers.** ADR 0024's "off and silent until a provider is added" is enforced first by not starting the sidecar at all until a provider exists and the user starts an AI action. Then:

- **In-process egress rule:** each SDK client gets a custom `fetch` that allows only the base URLs of the providers the user added, refuses redirects (AI5's rule) and applies the plain-HTTP key rule from #637 (resolve once, check the address, pin it).
- **What the egress rule can't stop:** Bun has no permission model, so a malicious dependency could call `fetch` directly. Say so in the "What is sent" guide.
- **Deno would be stronger, but less than it looks.** Compiled Deno binaries bake permissions in at compile time ([Deno compile]), and user-added custom endpoints aren't known then. Deno's permission broker decides per request: under `deno run` it asked a broker for `127.0.0.1:8099` and `example.invalid:80` and honoured "deny" (*measured*). It prints "Permission broker is an experimental feature", and a `deno compile` binary on 2.9.8 **ignored the broker entirely** (*measured*). Any `--allow-run` for an agent CLI reopens everything: "Treat both as equivalent to `--allow-all`" ([Deno security]).
- **Agent CLIs:** set `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1` (used in the probe). The CLIs' own traffic belongs in [agent-sdk-hosting.md](agent-sdk-hosting.md).

## 5. "Turn off all AI features" (AI20)

Zed's model: `"disable_ai": true` means "all AI features are disabled", including "the Threads Sidebar, Agent Panel, Edit Prediction, and Inline Assistant" ([Zed AI config]). It was added on 2025-07-23 for organisations whose "legal teams may require AI-free development environments" ([Zed blog]). Zed 1.27.0 (2026-10-28) fixed "LLM credentials being read when `disable_ai` was set to `true`" ([Zed preview], [Zed #66029]). That bug is the one to test for.

What the sidecar design needs:

| Requirement | How |
| --- | --- |
| Nothing shipped | the runtime is a download fetched when AI is turned on (owner, 2026-11-02). With AI off it is never downloaded. If it was downloaded earlier, it stays on disk, inert, like the threads and keys |
| Nothing loaded | the composition root reads the switch first. When it is on, it constructs no `KeychainKeyStore`, `Providers`, sidecar host, AI commands, panel or MCP listener. The switch lives in ordinary settings, not the keychain |
| Nothing started | the sidecar host isn't constructed, so it can't start |
| Nothing listening | AI3's QLocalServer and HTTP listener belong to the sidecar host, so they aren't created |
| No AI UI | AI commands are registered behind the switch, so command search, menus, hotkeys and toolbars never see them. Test by walking the registry and the menu ledgers, as AI20 asks |
| Turning it back on without a restart | construct the AI objects on demand. Turning it off at runtime means stopping the sidecar (close stdin, wait, kill), closing listeners and unregistering commands |

Tests:

- a counting fake `KeyStore` sees zero reads across startup and a session with the switch on;
- no child process and no listening socket exist;
- the registry and ledgers contain no AI command.

## 6. What happens to the merged C++ work

The owner has decided to port and then delete (brain page `inbox/2026-11-02-47622ba2`). The table says what that means file by file. `hikari_ai` isn't linked into the app today, because no composition code references it, so no user path depends on it.

| Part | Fate | Why |
| --- | --- | --- |
| `KeychainKeyStore` (`ai_keychain.cpp`) and `KeyStore` | **keep in C++** | Qt owns secrets (section 3) |
| `Config`, `Provider`, `Role`, `ModelRef`, `Capabilities`, `Price`, and `JsonConfigStore` | **keep in C++** as the Settings › AI model (AI20) | keyless config is UI state; the sidecar receives it through `_hikari/providers.set` |
| The add rule: key stored first, provider added only once the key is safe | **keep** in a slimmed `Providers` | it is a settings and keychain rule |
| `Request`, `Message`, `Event`, `Usage`, `Error`, `ErrorKind` | **port** as the protocol's TypeScript and JSON Schema types. C++ keeps only what the panel decodes | the panel still shows text, tool calls, usage and errors |
| `ai_wire.cpp` (SSE parser, three encoders and decoders), `ai_network.cpp` (`NetworkProviderPort`), and `Providers::stream`/`cancel`/`refreshModels` | **delete after the port**; the official SDKs replace the hand-written wires | the SDKs handle streaming, tool calls and caching fields |
| The key route rule and **#637** | **port** into the sidecar's egress `fetch`. Close #637 unmerged | the socket opens in the sidecar, so the check must run there |
| The Gemini adapter, **#641** | **port** on `@google/genai`. Close #641 unmerged. Its PR text is the spec: thought signatures, `functionResponse` ids, the stop reason and usage mapping | `@google/genai` handles the wire. Hikari's mapping to its own events remains |

Tests that carry over. Rewrite each against a local mock server under `bun test`, as the probe did:

| C++ test | Becomes |
| --- | --- |
| `ChatCompletions…`, `Responses…`, `Anthropic…StreamsTextToolCallsAndUsage` / `…EncodesVisionToolsAndHistory` | the same scenarios per provider, through the SDKs into the protocol's events |
| `HttpErrorsCarryTheProvidersMessage`, `ErrorsInsideTheStreamFailTheRequest`, `AStreamThatStopsEarlyFails`, `ASilentStreamTimesOut`, `CancelClosesTheConnection` | the same, checking the `ErrorKind` mapping and the two-minute idle timeout |
| `AKeyTravelsOnlyOverHttpsOrToThisMachineOrATailnet`, `ANameIsResolvedOnceAndTheCheckedAddressIsUsed`, `TailscaleRangesAndNames`, plus #637's three new tests (`AMagicDnsNameIsAllowedByName` is dropped, as #637 does) | egress `fetch` tests |
| `TheCatalogueReadsCapabilitiesAndPrices`, `RefreshingTheCatalogueKeepsTheUsersCorrections` | catalogue tests in the sidecar; the corrections test stays in C++ |
| #641's four Gemini tests | `@google/genai` mapping tests |
| `AiConfig.*`, `AProviderIsAddedOnlyOnceItsKeyIsInTheKeychain`, `AKeylessLocalServerNeedsNoKeychain`, `RemovingAProviderDropsItsKeyAndRoles`, `AConfigThatCannotBeSavedChangesNothing`, `TheConfigFileKeepsEverythingButTheKey`, `AiKeychainTest.*` | **stay in C++** |
| `WithoutAProviderNothingIsSent`, `WithoutAProviderNoConnectionIsMade`, `CancelBeforeTheKeyArrivesSendsNothing` | become host tests: no provider or AI off means no sidecar; a cancel before the key arrives sends nothing |
| `AiSse.*` | dropped with the SSE parser |

## 7. Recommendation and migration order

**Runtime: Bun**, compiled with `--bytecode`, cross-built from Linux for linux-x64 and windows-x64, pinned by SHA-256, and rebuilt on the latest Bun for every release. Its strengths:

- it has the lowest memory and startup (*measured*);
- every target SDK runs in a compiled binary (*measured*);
- the glibc floor is 2.17;
- Windows Authenticode signing works;
- Bun is the runtime Anthropic builds Claude Code and the Agent SDK on ([Bun joins Anthropic]).

Choose **Deno** instead if V8 itself, an LTS line with backported security fixes, or a download about 18 MiB smaller matters more. Its permission flags help less than they look, for the reasons in section 4.

**IPC:** stdio JSON-RPC as in section 2. **Secrets:** Qt reads and passes. **Process model:** one long-lived, lazily started sidecar.

Migration order across the cards:

1. **A sidecar ADR** amending ADR 0024. It drops "The Claude Agent SDK isn't embedded" and "Providers are backend adapters behind the application boundary", and records sections 1–3.
2. **A new card, the sidecar host** (C++ `AiSidecarHost` on `HelperHost`'s patterns, plus the TypeScript skeleton, the build, the download and verification, and the scrubbed start). It lands together with AI20's non-UI half: the switch gate and the tests in section 5.
3. **The AI5 port:** OpenAI, Anthropic, OpenRouter, custom endpoints and Gemini (AI18 #639, on `@google/genai`), with the egress rule (#637's tests) and **AI19** caching. Requests are built in TypeScript, so caching belongs there. Then delete the C++ wire code.
4. **AI1** tool schemas: HA1's registry exported through `_hikari/api.call`. **AI3**, the MCP server, is implemented in the sidecar and bridged to HA1.
5. **AI2**, the review, stays C++ and is unchanged. **AI7** puts the agent loop and the judge in TypeScript, and the panel in QML. **AI6** sign-in flows run in the sidecar, with tokens stored by Qt.
6. **AI13**, the MCP client, and **AI15**, ACP and the agent SDKs, go in the sidecar (see [agent-sdk-hosting.md](agent-sdk-hosting.md)).
7. The tool cards (AI4, AI8–AI12, AI14, AI16, AI17): HA1 operations in C++, prompts and orchestration in TypeScript.
8. **AI20's UI** after its design gate.

## Decisions for the owner

1. **Bun (JavaScriptCore) or Deno (V8)?** Bun is recommended. Choosing it means accepting that Bun isn't V8 and has no LTS line.
2. **Close #637 and #641 unmerged, or merge them first?** Both are green and mergeable, but the code is deleted after the port either way. Recommended: close them and port their tests.
3. **The protocol shape:** ACP-shaped sessions plus `_hikari/*` methods, and MCP over ACP for tools once that RFD is published (recommended). The alternative is a fully custom protocol.
4. **A new sidecar-host card**, ahead of AI18, AI19 and AI7?
5. **Where the runtime download comes from:** the HikariSub GitHub release, keyed by version and checked by SHA-256 from the release manifest (recommended), or the upstream Bun release plus a JavaScript bundle.
6. **An OS sandbox for the sidecar** (Landlock or AppContainer): now, or later as the plugin cards plan?

## Method

Scratch work ran in `/tmp/sidecar-research`, outside the repo.

- **Probe:** a project with the seven packages, pinned, installed with `bun install --ignore-scripts`. A Node `http` mock answered with canned SSE streams for `/chat/completions`, Anthropic `/v1/messages` and Gemini `:streamGenerateContent`, and a 401 for anything else. The agent SDKs were pointed at it through `ANTHROPIC_BASE_URL` and Codex's `baseUrl`, with `CLAUDE_CONFIG_DIR` and `CODEX_HOME` in a temp folder. No real provider was contacted.
- **Builds:** `bun build --compile --minify [--bytecode --format=esm] --target=bun-{linux-x64,windows-x64,windows-arm64}`. For Deno and Node, the same 3.1 MB bundle was built by Bun (`--target=node`) and compiled from a clean folder: `deno compile -A --no-check [--target …]`, and `node --build-sea` with `"mainFormat": "module"`.
- **Timing:** `fork`/`execv`/`wait4` from Python: one warm-up, then 15 runs, reporting the median wall time and `ru_maxrss`.
- **Reproducibility:** two builds per tool and target, compared with `sha256sum`, and the differing bytes located with `cmp -l`.
- **LLRT:** the official `llrt-linux-x64-no-sdk` 0.9.0-beta binary, run on per-package bundles.
- **Deno broker:** a Python Unix-socket broker allowing `127.0.0.1` only, run with `DENO_PERMISSION_BROKER_PATH` under `deno run` and under a `deno compile` binary.

## References

[HA1]: https://github.com/altqx/hikari/issues/411
[AI2]: https://github.com/altqx/hikari/issues/430
[AI20]: https://github.com/altqx/hikari/issues/649
[#637]: https://github.com/altqx/hikari/pull/637
[#641]: https://github.com/altqx/hikari/pull/641
[Bun licensing]: https://bun.com/docs/project/licensing
[Bun compiler]: https://bun.com/docs/compiler
[Bun executables]: https://bun.com/docs/compiler/executables
[Bun Node compat]: https://bun.com/docs/runtime/nodejs-compat
[Bun secrets]: https://bun.com/docs/runtime/secrets
[bun install]: https://bun.com/docs/pm/cli/install
[Bun 1.2.19]: https://bun.com/blog/bun-v1.2.19
[Bun 1.3]: https://bun.com/blog/bun-v1.3
[Bun 1.3.6]: https://bun.com/blog/bun-v1.3.6
[Bun 1.3.10]: https://bun.com/blog/bun-v1.3.10
[Bun joins Anthropic]: https://bun.com/blog/bun-joins-anthropic
[deno repo]: https://github.com/denoland/deno
[Deno compile]: https://docs.deno.com/runtime/reference/cli/compile/
[Deno stability]: https://docs.deno.com/runtime/fundamentals/stability_and_releases/
[Deno security]: https://docs.deno.com/runtime/fundamentals/security/
[Deno permissions ref]: https://docs.deno.com/runtime/reference/permissions/
[Node SEA]: https://nodejs.org/api/single-executable-applications.html
[Node permissions]: https://nodejs.org/api/permissions.html
[Node licence]: https://github.com/nodejs/node/blob/main/LICENSE
[Node dist]: https://nodejs.org/dist/v26.13.0/
[Node schedule]: https://github.com/nodejs/Release/blob/main/schedule.json
[Node security]: https://github.com/nodejs/node/blob/main/SECURITY.md
[LLRT]: https://github.com/awslabs/llrt
[txiki.js]: https://github.com/saghul/txiki.js
[Hermes]: https://github.com/facebook/hermes
[Agent SDK README]: https://www.npmjs.com/package/@anthropic-ai/claude-agent-sdk
[Codex SDK]: https://github.com/openai/codex/blob/main/sdk/typescript/README.md
[openai-node]: https://github.com/openai/openai-node#requirements
[anthropic-sdk-typescript]: https://github.com/anthropics/anthropic-sdk-typescript#requirements
[js-genai]: https://github.com/googleapis/js-genai
[MCP TS SDK]: https://github.com/modelcontextprotocol/typescript-sdk
[MCP transports]: https://modelcontextprotocol.io/specification/2026-07-28/basic/transports
[MCP stdio]: https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio
[MCP versioning]: https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning
[ACP overview]: https://agentclientprotocol.com/protocol/overview
[ACP init]: https://agentclientprotocol.com/protocol/initialization
[MCP over ACP]: https://agentclientprotocol.com/rfds/mcp-over-acp
[JSON-RPC]: https://www.jsonrpc.org/specification
[LSP]: https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
[QLocalServer]: https://doc.qt.io/qt-6/qlocalserver.html
[Job objects]: https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects
[PR_SET_PDEATHSIG]: https://man7.org/linux/man-pages/man2/PR_SET_PDEATHSIG.2const.html
[proc environ]: https://man7.org/linux/man-pages/man5/proc_pid_environ.5.html
[Secret Service]: https://specifications.freedesktop.org/secret-service/latest/unlocking.html
[CredRead]: https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw
[npm plan]: https://github.blog/security/supply-chain-security/our-plan-for-a-more-secure-npm-supply-chain/
[Zed AI config]: https://zed.dev/docs/ai/configuration
[Zed blog]: https://zed.dev/blog/disable-ai-features
[Zed preview]: https://zed.dev/releases/preview
[Zed #66029]: https://github.com/zed-industries/zed/pull/66029

- Runtimes: [Bun licensing], [Bun compiler], [Bun executables], [Bun Node compat], [Bun secrets], [bun install], [Bun 1.2.19], [Bun 1.3], [Bun 1.3.6], [Bun 1.3.10], [Bun joins Anthropic], [deno repo], [Deno compile], [Deno stability], [Deno security], [Deno permissions ref], [Node SEA], [Node permissions], [Node licence], [Node dist], [Node schedule], [Node security], [LLRT], [txiki.js], [Hermes].
- SDKs: [Agent SDK README], [Codex SDK], [openai-node], [anthropic-sdk-typescript], [js-genai], [MCP TS SDK].
- Protocols: [MCP transports], [MCP stdio], [MCP versioning], [ACP overview], [ACP init], [MCP over ACP], [JSON-RPC], [LSP].
- OS and Qt: [QLocalServer], [Job objects], [PR_SET_PDEATHSIG], [proc environ], [Secret Service], [CredRead].
- Supply chain and Zed: [npm plan], [Zed AI config], [Zed blog], [Zed preview], [Zed #66029].
- HikariSub: [HA1], [AI2], [AI20], [#637], [#641].
