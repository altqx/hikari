# External agents

A thread can run on an agent the user already has, instead of Hikari's own loop ([AI15 #443](https://github.com/altqx/hikari/issues/443), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)). Every agent edits only through Hikari's tools, into the [review](ai-review.md), under the thread's mode. Hikari never downloads or installs an agent's CLI. It uses the one on `PATH`.

**Instructions.** Every agent gets `session/new`'s `system`, which holds Hikari's prompt and the joined `AGENTS.md` text ([Threads](ai-agent.md#threads), PJ7 [#904](https://github.com/altqx/hikari/issues/904), [#905](https://github.com/altqx/hikari/issues/905)). Its own instruction files never load: its built-in file tools are off, and its settings sources are empty. Claude gets the text as its system prompt, and Codex as `developerInstructions`. Both get it again when they resume after the CLI exits. An ACP agent gets it as a text block before its first prompt, and again after a new `session/new`, because ACP has no instructions field. After `session/load` the agent already has it in its history.

## Claude

Claude runs through the Claude Agent SDK, on the user's own `claude` and its login. Hikari never starts, reads, stores or relays that login.

- **Picking it.** When Qt finds `claude` on `PATH`, the panel's model menu lists **Claude Code** first. A thread on it has the model `agent:claude`, which Qt sends as `session/new`'s `agent`. Such a thread needs no provider: it uses the same sidecar as the MCP server.
- **One query per thread.** `session/new` with `"agent": "claude"` opens the session. Its first `session/prompt` starts one long-running query, and every later turn of the thread goes into the same query. When `claude` exits, the next turn starts a new query that resumes Claude's session.
- **Only Hikari's tools.** The query runs with:
  - `tools: []`, so Claude's built-in tools are off;
  - `settingSources: []` and `strictMcpConfig`, so no user or project settings and no other MCP servers are loaded;
  - `permissionMode: "dontAsk"`, allowing only `mcp__hikari`.

  Hikari's tools come from the sidecar's own [MCP server](ai-mcp.md), in process, with one server per session, so Qt sees the session as the caller. A PreToolUse hook denies any other tool. The PreToolUse, PostToolUse and PostToolUseFailure hooks turn each call into ACP's `tool_call` and `tool_call_update`. The `usage` update after each turn carries Claude's reported cost.
- **Qt starts `claude`.** It does this in its sandbox, through `_hikari/agent.start` (ADR 0029). It sets `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1`, and passes on only the SDK's own markers from the sidecar.
- **Steering** isn't supported yet: `session/steer` answers `taken: false`, so Qt sends the message as the next turn.

### The SDK is fetched, never shipped

The sidecar's assets never contain `@anthropic-ai/claude-agent-sdk`, and the [release check](build.md) fails if they do.

1. On first use, the sidecar fetches the SDK's tarball from `registry.npmjs.org`, at the version in `src/ai-sidecar/bun.lock`. The request goes through Qt's `npm` relay target, which reaches only the registry, allows only GET and HEAD, and carries no key.
2. It checks the tarball against the lockfile's sha512 and refuses a mismatch before writing anything.
3. It unpacks only the tarball's regular files into `ai/vendor/claude-agent-sdk/<version>/`. A link, or a path outside the package, fails the fetch.
4. It loads the SDK's `sdk.mjs` with a dynamic `import()`.

Each version is fetched once. The SDK's `sdk.mjs` bundles what it needs, so its peer packages aren't fetched, and the build doesn't install them either (`peer = false` in `bunfig.toml`). Its optional native packages, which hold a `claude` of their own, are never fetched, here or in the build (`bun ci --omit=optional`). The weekly `AI sidecar bump` moves the version once a newer one is 7 days old.

## Codex

Codex runs as `codex app-server`, on the user's own `codex` and a ChatGPT sign-in that belongs to HikariSub.

- **Picking it.** When Qt finds `codex` on `PATH`, the model menu lists **Codex** after Claude Code. A thread on it has the model `agent:codex`.
- **Its own home and sign-in.** Qt starts Codex with `CODEX_HOME` set to `ai/agents/codex/home`, never `~/.codex`. Before the first Codex process starts, Qt writes that home's `auth.json` (readable only by the user) from HikariSub's sign-in in the keychain (`codex-chatgpt`, from Settings › AI). While Codex runs, only Codex refreshes the tokens. After the last Codex process exits, Qt puts the refreshed tokens back in the keychain and deletes the file. With no sign-in, Codex doesn't start. A file left behind for the same account is newer than the keychain, so Qt keeps it. If the user signed out while Codex ran, they stay signed out.
- **A private tools channel.** For each Codex process, Qt opens an MCP server with a token of its own.
  - **Linux:** the server is on `127.0.0.1` with a random port. Codex gets it as `-c mcp_servers.hikari.url=…` and `bearer_token_env_var="HIKARI_MCP_TOKEN"`.
  - **Windows:** an AppContainer can't reach loopback, so the server is a named pipe with a random name. Only the user and Codex's AppContainer may open it, it refuses remote clients, and it takes one connection, from a process in that Codex's own job. Codex starts `HikariSub.exe --mcp --channel <pipe>` as a stdio MCP server (`mcp_servers.hikari.command`, `args`) and passes it the token through `env_vars = ["HIKARI_MCP_TOKEN"]`, never on a command line. Codex's AppContainer may read and run HikariSub's install folder for this. Nothing on loopback is reachable from it: Hikari asks for no loopback exemption.
  - If the pipe can't be made exactly so, Codex doesn't start.

  Qt relays the channel's messages to the sidecar as the connection `channel:<session>`, and the sidecar runs their calls as that session, under the thread's mode, as it does for Hikari's own loop. The channel closes when the process exits.
- **Writes go to the review.** Codex would ask for approval of each MCP write, so Qt passes `default_tools_approval_mode="approve"` for Hikari's server. Hikari's review stays the gate. A Manual-mode edit waits as a proposal until the user applies it, and Codex fails a tool call after 60 seconds by default, so Qt also passes `tool_timeout_sec=86400`.
- **Read-only.** Qt's guard lets only a conversation's methods through. It makes every thread read-only with the approval policy `untrusted` and no `config` overrides, and declines every command, file change and permission request. The sidecar declines MCP elicitations.
- **One app-server per thread.** The first turn starts the process and a thread. Later turns go to the same thread. `session/steer` uses `turn/steer`, and Stop sends `turn/interrupt`. If Codex exits, the next turn starts a new process and resumes the thread. The `usage` update after each turn carries that turn's tokens.

## ACP agents

Any agent that speaks the [Agent Client Protocol](https://agentclientprotocol.com) over stdio can run Hikari's agent loop: OpenCode (`opencode acp`), Antigravity (`agy acp`) and the user's own agents from Settings › AI › External agents.

- **Picking one.** The model menu lists each ACP agent Qt finds after Claude Code and Codex. A thread on one has the model `agent:acp:<id>`: `opencode`, `antigravity`, or `custom-…` for the user's own agents.
- **Hikari's tools only.** Qt's guard puts Hikari's tools in every `session/new` and `session/load` as the agent's only MCP server. The guard also sets the session's folder and drops extra folders. It tells the agent the client has no file system and no terminal, and refuses any call for them. When the agent says it takes HTTP MCP servers (`mcpCapabilities.http`), it gets the channel itself: `http://127.0.0.1:<port>/mcp` with the token as a `Bearer` header. Otherwise it gets the bridge: `hikarisub --mcp --channel-url <url>` over stdio, with the token in `HIKARI_MCP_TOKEN`. The bridge only relays to `127.0.0.1` and exits when its stdin closes or the channel goes away. On Windows every ACP agent gets the bridge, as `hikarisub --mcp --channel <pipe>`, to a pipe only its own AppContainer may open, as for Codex. Permission requests are allowed for Hikari's own tools only. Every other request is rejected.
- **A sandbox of its own.** The agent has the network, but it can write only to its own state folders and a private work folder under `ai/agents/`. OpenCode writes to its XDG folders (`~/.config/opencode`, `~/.local/share/opencode`, `~/.cache/opencode`, `~/.local/state/opencode`). A custom agent gets `ai/agents/custom/<id>` and nothing else, and that folder is its home too.
- **Antigravity's own profile.** Qt runs `agy acp --gemini-dir ai/agents/antigravity/gemini --app-data-dir app-data --disable-telemetry --include-default-customizations=false`, with `HOME` set inside the same profile, so agy never reads `~/.gemini`. Qt never passes `--dangerously-skip-permissions` or `--ide-mcp-port`. Settings › AI asks for agy 1.5.0 or newer (`agy update`).
- **Antigravity's sign-in.** **Sign in with Google…** runs `agy acp --login` in the profile. HikariSub opens the link agy prints. The user signs in to Google and pastes the code the page shows. agy then writes its token to `gemini/app-data/antigravity-oauth-token` in the profile, because its sandbox gives it no keychain. As soon as that file holds a whole token, Qt moves it into the keychain (`antigravity-google`), deletes it and stops agy. It doesn't wait for agy to exit. Before the first agy process starts, Qt writes the file back, readable only by the user. After the last agy process exits, Qt puts a refreshed token back in the keychain and deletes the file, as it does for Codex. A sign-out while agy runs stays signed out, and a file a crash leaves behind is deleted when HikariSub next starts its AI. API-key and Enterprise sign-ins aren't offered.

## Tests

`test/fixtures/fake-claude.ts` stands in for `claude`. It speaks the SDK's control protocol as recorded from Claude Code 2.1.322: initialize, hook callbacks, `mcp_message` and interrupt. Each user message it gets is a script of tool calls. The sidecar tests drive the real SDK against it. They check that:

- an edit runs on Hikari's tools;
- a built-in tool is denied, including the file tools Read, Edit and Write;
- the system prompt, with the joined `AGENTS.md` text, reaches Claude, and again when it resumes;
- the arguments that turn Claude's tools, settings and permissions off are passed;
- one process serves every turn.

`test/fixtures/fake-codex.ts` stands in for `codex app-server`. It reads the channel from its `-c` arguments and makes each turn's scripted tool calls over MCP. The sidecar tests check:

- an edit over the session's channel;
- one process and thread for every turn, with each turn's usage;
- Stop as `turn/interrupt`;
- resuming the thread after Codex exits, with the same `developerInstructions` as `thread/start`;
- a built-in file change that asks for approval is never accepted.

In Qt, the broker tests cover the channel (its token, its arguments and its end), the hooks around each process, and Qt declining Codex's command and file-change approvals. The sign-in tests cover handing the sign-in to Codex and back.

`test/fixtures/fake-acp.ts` stands in for an ACP agent in the sidecar tests, and `tests/backends/fake_acp_agent.cpp` stands in for a stdio-only one in Qt. That fake starts the bridge from its `session/new` and makes an edit through it. The Antigravity tests check that every path agy gets stays inside its profile, that the token file goes back and forth between the keychain and the profile, and that the sign-in takes the code from Google's page.
