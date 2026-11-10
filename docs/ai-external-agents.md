# External agents

A thread can run on an agent the user already has, instead of Hikari's own loop ([AI15 #443](https://github.com/altqx/hikari/issues/443), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)). Every agent edits only through Hikari's tools, into the [review](ai-review.md), under the thread's mode. Hikari never downloads or installs an agent's CLI. It uses the one on `PATH`.

## Claude

Claude runs through the Claude Agent SDK, on the user's own `claude` and its login. Hikari never starts, reads, stores or relays that login.

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

Each version is fetched once. The SDK's `sdk.mjs` bundles what it needs, so its peer packages aren't fetched. Its optional native packages, which hold a `claude` of their own, are never fetched, here or in the build (`bun ci --omit=optional`). The weekly `AI sidecar bump` moves the version once a newer one is 7 days old.

## Tests

`test/fixtures/fake-claude.ts` stands in for `claude`. It speaks the SDK's control protocol as recorded from Claude Code 2.1.322: initialize, hook callbacks, `mcp_message` and interrupt. Each user message it gets is a script of tool calls. The sidecar tests drive the real SDK against it. They check that:

- an edit runs on Hikari's tools;
- a built-in tool is denied;
- the arguments that turn Claude's tools, settings and permissions off are passed;
- one process serves every turn.
