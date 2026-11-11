# External agents

A thread can run on an agent the user already has, instead of Hikari's own loop ([AI15 #443](https://github.com/altqx/hikari/issues/443), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)). Every agent edits only through Hikari's tools, into the [review](ai-review.md), under the thread's mode. Hikari never downloads or installs an agent's CLI. It uses the one on `PATH`.

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
- **A private tools channel.** For each Codex process, Qt opens an MCP server on `127.0.0.1` with a random port and a token of its own. Codex gets them as `-c mcp_servers.hikari.url=…` and `bearer_token_env_var="HIKARI_MCP_TOKEN"`. Qt relays the channel's messages to the sidecar as the connection `channel:<session>`, and the sidecar runs their calls as that session, under the thread's mode, as it does for Hikari's own loop. The channel closes when the process exits.
- **Writes go to the review.** Codex would ask for approval of each MCP write, so Qt passes `default_tools_approval_mode="approve"` for Hikari's server. Hikari's review stays the gate. A Manual-mode edit waits as a proposal until the user applies it, and Codex fails a tool call after 60 seconds by default, so Qt also passes `tool_timeout_sec=86400`.
- **Read-only.** Qt's guard lets only a conversation's methods through. It makes every thread read-only with the approval policy `untrusted` and no `config` overrides, and declines every command, file change and permission request. The sidecar declines MCP elicitations.
- **One app-server per thread.** The first turn starts the process and a thread. Later turns go to the same thread. `session/steer` uses `turn/steer`, and Stop sends `turn/interrupt`. If Codex exits, the next turn starts a new process and resumes the thread. The `usage` update after each turn carries that turn's tokens.
- **Linux only for now.** Windows' AppContainer blocks loopback, so Qt neither offers nor starts Codex there until it has a channel the container can reach ([#931](https://github.com/altqx/hikari/issues/931)).

## Tests

`test/fixtures/fake-claude.ts` stands in for `claude`. It speaks the SDK's control protocol as recorded from Claude Code 2.1.322: initialize, hook callbacks, `mcp_message` and interrupt. Each user message it gets is a script of tool calls. The sidecar tests drive the real SDK against it. They check that:

- an edit runs on Hikari's tools;
- a built-in tool is denied;
- the arguments that turn Claude's tools, settings and permissions off are passed;
- one process serves every turn.

`test/fixtures/fake-codex.ts` stands in for `codex app-server`. It reads the channel from its `-c` arguments and makes each turn's scripted tool calls over MCP. The sidecar tests check:

- an edit over the session's channel;
- one process and thread for every turn, with each turn's usage;
- Stop as `turn/interrupt`;
- resuming the thread after Codex exits.

In Qt, the broker tests cover the channel (its token, its arguments and its end) and the hooks around each process. The sign-in tests cover handing the sign-in to Codex and back.
