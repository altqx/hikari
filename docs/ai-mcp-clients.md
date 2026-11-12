# MCP servers the agent uses

The user can add MCP servers in Settings › AI, and the in-app agent gets their tools ([AI13 #441](https://github.com/altqx/hikari/issues/441), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)). This is the other direction from [HikariSub's MCP server](ai-mcp.md), which serves HikariSub's tools to agents outside it.

## Who does what

| Part | Does |
| --- | --- |
| `McpClients` in the sidecar (`src/ai-sidecar/src/mcp/client.ts`) | Speaks MCP on `@modelcontextprotocol/sdk` 1.x. It connects each server, lists its tools and offers them as one tool group per server. Before each call, it asks Qt. |
| `McpClients` (`src/backends/ai_mcp_clients.*`) | Starts each stdio server, relays each HTTP server's requests with its secrets, reads every message, and admits each tool call through the thread's gate. |
| `AiMcpServers` (`src/app/ai_mcp_servers.*`) | The server list in Settings › AI: `ai.mcp.servers`, plus the secrets in the keychain. |
| `AgentThreads::gateMcp` (`src/backends/ai_agent.cpp`) | Decides a tool call by the thread's mode and the tool's hints. |

The SDK stays on 1.x while the Claude Agent SDK's peer range is `^1.29`.

## Starting and stopping

- **Nothing starts with AI off.** With "Turn off all AI features" on, no `McpClients` exists.
- **Nothing starts until a thread opens a session.** `session/new` waits up to 5 s for the servers to connect. A server that is slower adds its tools when it is ready. One that fails is tried again by the next session.
- **Qt starts stdio servers, not the sidecar** (ADR 0029). The sidecar sends `_hikari/agent.start` with the agent `mcp:<id>`. Qt takes the program and arguments from the user's settings and refuses any arguments from the sidecar.
  - Servers run unsandboxed in v1, with the user's own environment plus the variables stored for that server. Their working folder is the home folder.
  - Like the agent CLIs, they end with the sidecar: when it is lost, stops or is refused, `AgentBroker` kills them. On Linux they also get `PR_SET_PDEATHSIG`, and on Windows a kill-on-close job.
- **HTTP servers** (streamable HTTP) go through `_hikari/http.request` with the target `{kind: "mcp", id}`. The sidecar only knows a placeholder URL. Qt sends every request to the URL from the settings, adds `Authorization: Bearer` from the keychain, and masks the token in what it relays back. The key rule for plain HTTP applies: a token goes over `http` only to this computer or a tailnet peer.
- **Changes apply at once.** A changed server gets a new revision, and the sidecar connects to it again. A removed server can't start any more.

## The link

| Message | From | Does |
| --- | --- | --- |
| `_hikari/mcpServers.set {servers: [{id, name, transport, revision}]}` | Qt | The servers, with no command, URL or secret. Qt sends it whenever the sidecar is ready and whenever the list changes. |
| `_hikari/mcpServers.status {server, state, tools?, error?}` | sidecar | `connecting`, `ready` (with the tool count) or `failed`, for the settings row. |
| `_hikari/mcpServers.admit {sessionId, server, tool, arguments}` | sidecar | Asks to call one tool once with these arguments. Qt answers `{}` or refuses as HA1 does (code -32000, `data.code` `refused` or `not_found`). |

## Tools

- **Namespaced:** a tool is offered as `<server id>__<tool>`, in `[A-Za-z0-9_-]`. A name longer than 64 characters is cut and ends in a hash of the whole name. Each server is a group, `mcp_<id>`, that `search_tools` finds ([tool sources](ai-tools.md#the-tools)). None are offered up front, so they don't count toward the up-front budget.
- **Results:** text and images pass through as they came. Other content goes to the model as JSON text, and `structuredContent` does too when a result has no content.
- **Threads only.** Only the in-app agent loop gets these tools. They aren't offered to the Claude Code or Codex sessions, or to agents that use HikariSub's MCP server.

## What Qt enforces

Qt doesn't rely on the sidecar's word for anything here:

- **Hints come from the server's own answer.** Qt reads each `tools/list` answer as it passes through, on stdio and over HTTP (JSON or server-sent events), and keeps each tool's `readOnlyHint` and `destructiveHint`. A hint the server left out takes MCP's default: not read-only, and destructive.
- **Calls need an admission.** A `tools/call` reaches the server only when Qt admitted that tool with exactly those arguments, once. Anything else gets a JSON-RPC error from Qt. The sidecar may send only `initialize`, `ping`, `tools/list`, `tools/call`, `notifications/initialized`, `notifications/cancelled` and answers.
- **Trust:** hints loosen nothing unless the user trusts the server ("Trust what this server says about its tools"). Without trust, every tool counts as destructive.

The decision (`decideMcp`):

| Tool, after trust | Every mode |
| --- | --- |
| `readOnlyHint` | Runs, never asks |
| Destructive (the default) | Asks in every mode: Allow once or Deny |
| `destructiveHint: false`, approved in Settings | Runs |
| `destructiveHint: false`, not approved | Manual asks, Auto asks the judge, Full access runs. "Allow in this thread" covers that one tool. |

A tool that the server didn't list is refused as `not_found`. Once the turn has stopped, everything but a read-only tool is refused.

## Settings › AI

The **MCP servers** row sits in the MCP section, under the MCP server's settings, as the [Settings › AI mockup](https://github.com/altqx/hikari/issues/649#issuecomment-6612977714) draws it. Until a server is added it reads "No MCP servers yet. Add one to give the agent its tools." **Add server…** opens the form in place:

- **Name**, and **Runs as**: "A command on this computer" or "A URL (streamable HTTP)".
- For a command: **Command**, split the way a shell would, and **Environment**, one `NAME=value` per line.
- For a URL: **URL** (`http` or `https`) and an optional **Bearer token**.
- **Trust what this server says about its tools**, off by default.

The environment and the token go to the keychain (`mcp-server:<id>`), never to the settings file, and they are never shown again. On Edit, leaving them empty keeps them.

Each row shows the command or URL, the status ("Starts when the agent needs it", "Starting…", "Connected · N tools" or "Failed: …"), Edit and Delete, and the trust check box. Once the server has run, its tools are listed: one that may change things but isn't destructive gets "Run <tool> without asking"; the others say "only reads, never asks" or "asks every time". A note says that servers run as the user, outside the AI component's sandbox, and that changes apply at once.

## Tests

- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/mcp/client.test.ts`) uses a real SDK server behind a fake relayed process: nothing starts before a session, tools are grouped and named per server, every call asks Qt first and a refused one never reaches the server, `search_tools` finds a server's group, a changed or removed server stops, a server that exits is started again, and HTTP goes through the relay.
- `hikari_backends_ai_mcp_clients_tests`: hints from the server's own list (pages too) with MCP's defaults, admissions that work once and only for those arguments, the allowed methods, JSON and event-stream answers, the settings rows and secrets, a stdio server started unsandboxed with its own arguments and secrets, a call Qt didn't admit answered with an error, servers ending with the sidecar, a removed server refused, and the HTTP target with its URL, token and guard.
- `hikari_backends_ai_agent_tests`: trust and the decision table, the allowance key, and a thread whose MCP calls run, ask and always ask, with the ask naming the server.
