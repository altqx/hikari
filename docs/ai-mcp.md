# HikariSub's MCP server

HikariSub serves its [AI tools](ai-tools.md) and the open Documents' state over the Model Context Protocol, so agents the user runs outside HikariSub can read and edit subtitles. Claude Code and Codex are two such agents ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [AI3 #431](https://github.com/altqx/hikari/issues/431)).

## Who does what

| Part | Does |
| --- | --- |
| `McpServer` (`src/backends/ai_mcp_server.*`) | Listens, checks the token, the Host and the Origin, and relays each message to the sidecar. |
| `AiMcp` (`src/app/ai_mcp.*`) | Turns the server on and off, from `ai.mcp.server`, `ai.mcp.http` and "Turn off all AI features". |
| `McpServer` in the sidecar (`src/ai-sidecar/src/mcp/`) | Speaks MCP: versions, `server/discover`, `initialize`, tools and resources. |
| `McpBridge` (`src/backends/ai_mcp_bridge.*`) | `hikarisub --mcp`, the stdio server an agent starts. |

Qt never parses MCP. It passes each message to `_hikari/mcp.message`, and writes back the reply the sidecar gives. The sidecar runs every tool through `_hikari/api.call`, marked with the connection. Qt then runs the call as that connection's caller, so HA1 checks and scopes it as it does any other caller's.

## On and off

- **Nothing listens** until the user turns `ai.mcp.server` on.
- **Nothing listens with AI off.** With "Turn off all AI features" on, no server exists, whatever `ai.mcp.server` says.
- **The sidecar starts** with the first message, not when the server starts listening. The MCP server needs no provider.
- **Without the AI component**, the server answers every message with "HikariSub's AI sidecar isn't installed". Settings › AI offers the download when the user turns the server on ([AI3c #869](https://github.com/altqx/hikari/issues/869)).
- **Streamable HTTP** listens only while `ai.mcp.http` is on as well.

## Settings › AI

The MCP section sits between External agents and Privacy, as the [Settings › AI mockup](https://github.com/altqx/hikari/issues/649#issuecomment-6612977714) draws it. Both check boxes take effect on OK or Apply, like every other setting.

- **Let agents outside HikariSub edit through MCP** (`ai.mcp.server`) turns the server on.
  - When the AI component isn't installed, checking it offers the component's one-time download first, with its size, as the provider form does. The box stays clear until the download finishes. **Download** installs the component with no provider (`AiSettings::installComponent`), then checks the box.
  - If the user presses **Cancel**, or the download fails, the setting stays off and a line under the box says why: "MCP stays off. It needs the AI component." or the download's error.
- **Also listen on http://127.0.0.1:<port>** (`ai.mcp.http`) shows under it while it is on. It names the last port once the server has listened over HTTP.
- **Let them read your skills** (`ai.mcp.skills`, off by default) shows under it too: "Yours, this project's and the built-in ones. Skills guide the agent; they grant no permission." ([skills](ai-skills.md#outside-hikari-ais-own-threads)).
- **Copy setup** copies one of three texts (`AiMcp::setup`):

| Entry | Copies |
| --- | --- |
| Claude Code command | `claude mcp add hikari -- "<program>" --mcp` |
| Codex config.toml | `[mcp_servers.hikari]` with `command = "<program>"` and `args = ["--mcp"]` |
| JSON with the address and token | `{"mcpServers": {"hikari": {"type": "http", "url": "http://127.0.0.1:<port>/mcp", "headers": {"Authorization": "Bearer <token>"}}}}`. Under the entry: "Holds your token; keep it private." |

`<program>` is `$APPIMAGE` when HikariSub runs from an AppImage, and the executable otherwise (`AiMcp::program`). The JSON entry is disabled until HikariSub listens on 127.0.0.1, and a line under the button says why: the HTTP box is off, or it waits for OK or Apply.

**Reset token**, next to Copy setup, acts at once (`AiMcp::resetToken`). It makes a new token, saves it, and drops every connected agent. A request with the old token gets 401, and a bridge that sends it is closed. Agents started with `hikarisub --mcp` read the new token when they start again. Agents set up with the JSON stop working until the user copies the setup again, and the line under the button says so.

`<user folder>/ai/mcp.json` holds `{socket, token, port}`, and only the user can read it. `writeMcpEndpoint` creates it as 0600, or on Windows with a protected ACL that allows only the user. Every later write, a reset included, rewrites it in place, so it keeps the permissions it was created with.
- `socket` is set while the server listens, and cleared when it stops or HikariSub quits. After a crash the bridge finds nothing at it and says the server is off.
- The token and the last HTTP port survive restarts, so agents' settings stay valid.

## Transports

**The local socket** serves `hikarisub --mcp`. It is a `QLocalServer` with `UserAccessOption` and a random name. The bridge sends the token as its first line; a wrong token closes the connection. After that, each side sends one JSON-RPC message per line. The bridge relays stdin and stdout, and exits when either side closes. When nothing listens, it says how to turn the server on, on stderr, and exits with 1.

**Streamable HTTP** listens on `http://127.0.0.1:<port>/mcp`. It takes `POST` only, and refuses a request with:

| Status | When |
| --- | --- |
| 403 | The `Host` isn't `127.0.0.1:<port>` or `localhost:<port>`, or an `Origin` is set and isn't one of those over `http`. |
| 401 | No `Authorization: Bearer <token>`, or a wrong token. |
| 405 | Any method but `POST`. |
| 415 | The body isn't `application/json`. |

Replies are `application/json`, never SSE. A notification gets 202. The sidecar checks that `MCP-Protocol-Version`, `Mcp-Method` and `Mcp-Name` agree with the body, and refuses a mismatch with 400 and -32020. HikariSub gives no `Mcp-Session-Id`.

## Protocol

- **2026-07-28.** Each request carries `io.modelcontextprotocol/protocolVersion` and `clientCapabilities` in `_meta`.
  - `server/discover` answers the supported versions, the capabilities `{tools, resources}` and the instructions.
  - Every result carries `resultType: "complete"` and `_meta["io.modelcontextprotocol/serverInfo"]`.
  - A request with another version gets -32022, listing the versions HikariSub supports.
- **Earlier versions.** A request without that `_meta` uses the older protocol. `initialize` agrees on the client's version when it is one of 2025-11-25, 2025-06-18, 2025-03-26 or 2024-11-05, and otherwise answers with 2025-11-25.
- **Not supported:** batches, prompts, subscriptions and list-change notifications. The server never sends requests. `notifications/cancelled` drops the reply.

**Tools.** `tools/list` gives every tool, the searchable groups' too, in one page. `tools/call` runs a tool through the connection's own `ToolSet` and answers its `CallToolResult`. An unknown tool name gets -32602.

**Resources.** Each is JSON (`application/json`):

| URI | Holds |
| --- | --- |
| `hikari://documents` | `documents_list` |
| `hikari://documents/<id>` | `documents_get` |
| `hikari://documents/<id>/lines` | Every Line at one revision, read 5000 at a time |
| `hikari://documents/<id>/styles` | `styles_list` |
| `hikari://documents/<id>/info` | `info_list` |

`resources/templates/list` gives the four templates under `hikari://documents/{document}`. An unknown URI, or a Document that isn't open, gets -32602 with `data.uri`.

## Who the calls run as

`McpServer::callerFor` sets the caller for an MCP connection's calls:
- the caller sees every open Document (`allDocuments`), as the user asked for when they turned the server on;
- its label, which names undo steps, is the client's `title` or `name` from `clientInfo`;
- it may read skills (`skills`) only while `ai.mcp.skills` is on, and then sees the active window's Project (`project`), which `McpServer::setSkills` answers at each call;
- its edits wait in the review ([AI2 #430](https://github.com/altqx/hikari/issues/430), [AI review](ai-review.md)) as `reviewed`, in a turn of its own for each connection. The Review panel lists each one under the client's name, and Keep and Undo act on it as on the sidecar's.

`document.apply` is the only write. Every other write is refused with `refused`: "Through MCP, only document.apply writes; its edits land in the review." The permission modes reach MCP with the agent panel ([AI7 #435](https://github.com/altqx/hikari/issues/435)).

## The link

| Method | Params | Result |
| --- | --- | --- |
| `_hikari/mcp.message` | `{connection, text, http?: {protocolVersion?, method?, name?}}` | `{reply?, status}`: the reply to write, if any, and its HTTP status |
| `_hikari/mcp.closed` (notification) | `{connection}` | |
| `_hikari/api.call` | `{operation, params, mcp?: {connection, client?}}` | as before; `mcp` makes it run as the connection's caller |

A local socket connection is `local-<n>`; all HTTP requests share `http`. The same server also runs in process for each [Claude thread](ai-external-agents.md), and is meant to serve AI15's per-session channel for Codex and ACP agents.

## Tests

- `hikari_ai_sidecar_bun_tests` (`test/mcp`, `test/rpc.test.ts`):
  - both eras, discovery and version refusals;
  - the HTTP header checks;
  - tools and resources against a fake HA1, with Lines read across pages;
  - malformed messages and cancellation;
  - the link methods.
- `hikari_backends_ai_mcp_tests` runs the real sidecar and API:
  - an agent on the bridge reads every Document and is refused a write;
  - a client without the token is dropped;
  - HTTP accepts the token from this Origin only, and refuses another Host;
  - the endpoint file round-trips, and only the user can read it, before and after a rewrite: 0600, or on Windows the same protected ACL for the user alone;
  - a new token drops the connected agents, the old token gets 401 and is closed on the socket, and the new one connects.
- `hikari_ui_ai_features_tests`: no listener unless `ai.mcp.server` is on, never with AI off, the token and port survive a restart, Copy setup's three texts, and Reset token saving a new token that the JSON setup uses.
- `hikari_ui_ai_settings_tests`, against a fake download: with no provider, the component downloads for MCP and the server reaches it; Cancel downloads nothing; a failed download says why.
- `hikari_ui_ai_mcp_review_tests`: in the app, a client on the bridge edits through the real sidecar as Claude Code. The edit waits in the Review panel under that name, Keep clears it with no step, and Undo puts the Line back as one step.
- `hikari_ui_shell_tests`:
  - `settingsAiPageAddsGeminiAndFoldsAwayWhenAiIsOff`: the MCP check boxes, Apply starting and stopping the server, each Copy setup entry's text on the clipboard, the JSON entry's line, and Reset token;
  - `settingsAiMcpOffersTheAiComponentWithoutAProvider`, against a fake download: a failed download and Cancel leave the setting off and say why, and Download then OK starts the server.
