---
status: accepted
amends: 0026
---

# The AI sidecar runs in an OS sandbox, and its keys and network stay in Qt

On 2026-11-04 altqx accepted all seven recommendations of [SB1 #651](https://github.com/altqx/hikari/issues/651) ([the answers](https://github.com/altqx/hikari/issues/651#issuecomment-6620146045)). The research, including the Windows AppContainer spike, is in [AI sidecar sandbox](../research/ai-sidecar-sandbox.md); where it differs from this record, this record wins.

This amends [ADR 0026](0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md). It replaces these parts of it:

- the Keys and Network sections;
- "Agent CLIs and MCP servers the sidecar started go with it", under Starting and stopping.

It adds to ADR 0026's Agents and Tests sections. The rest of ADR 0026 stands.

Amended by [AI15c #443](https://github.com/altqx/hikari/issues/443) on 2026-11-12: ACP agents run in a profile of their own instead of unsandboxed. MCP servers stay unsandboxed in v1.

Amended by [AI15d #931](https://github.com/altqx/hikari/issues/931) on 2026-11-13. On Windows, an agent's private tools channel is a named pipe that only the user and that agent's AppContainer may open, from a process in the agent's own job. Agents get no loopback exemption.

## The sandbox

- **One shared module.** `hikari_sandbox` in `src/backends` starts a process under a `sandbox::Profile`: the paths it may read and execute, read, or read and write, and whether it gets the network. Plugins ([PL7 #419](https://github.com/altqx/hikari/issues/419)) reuse it.
- **Only the listed paths.** The sidecar gets its install folder (read and execute) and its work folder (read and write, with its temporary files in it), plus the system's libraries, certificates and name-service files to read. Nothing else in the home folder is reachable.
- **Linux: Landlock and seccomp,** applied in the child before `exec`.
  - Landlock handles every filesystem right the kernel knows. From ABI 9 it also limits connecting to UNIX sockets outside the profile.
  - The seccomp filter refuses namespaces, `io_uring`, `clone3` and x32 calls.
  - A profile without the network can't open a socket at all. One with the network can open only IPv4, IPv6 and netlink sockets, never a UNIX socket.
- **Windows: an AppContainer per profile,** with access entries on the listed paths.
  - The sidecar has no capabilities, so it has no network. Agents get `internetClient`. There is no WFP or firewall step.
  - The process starts inside its kill-on-close job (`PROC_THREAD_ATTRIBUTE_JOB_LIST`), so it never runs outside it.
- **Where the sandbox can't apply,** AI still runs. Examples are a kernel without Landlock or seccomp, or an AppContainer that can't be created. The host logs what is missing under `hikari.ai.sandbox`, and Settings › AI shows "not sandboxed on this system".
- **macOS** isn't a target.

## Keys

- **The sidecar never holds a key.** The SDKs send the placeholder `hikari-key` where a key goes.
  - Qt replaces the placeholder in the relayed request's header values with the key from the keychain.
  - For a provider without a key, Qt drops that header.
  - Qt masks the key as `[key]` in everything it relays back.
- **Qt opens the keychain,** as before. No key travels over the link, in either direction.
- **OAuth tokens** ([AI6 #434](https://github.com/altqx/hikari/issues/434)) are keys under this rule. Qt runs the sign-ins and refreshes a token itself, and writes it straight to the keychain; the sidecar never sees it.
- **Agent CLIs** use their own login. Claude keeps the user's `claude` login. Codex's Hikari session ([AI15 #443](https://github.com/altqx/hikari/issues/443)) goes in from Qt and never passes through the sidecar.

## Network

- **The sidecar has no network.** All of its egress goes to Qt as `_hikari/http.request {stream, target, method, url, headers, body}`. Qt answers with `_hikari/http.response {stream, status, headers}` and `_hikari/http.data {stream, data}` notifications as the body arrives, then `{}`. A failure is code -32010 with the provider error, and `$/cancel_request` aborts the request.
- **An allowlist of targets.** A target is a kind and an id. `provider` maps to that provider's base URL and key. `search` maps to the user's chosen search service and its key, and only while web search is on, or to a service under Test connection with the key typed for it ([AI23 #742](https://github.com/altqx/hikari/issues/742)). The request is refused when:
  - the URL isn't under that base, or it has dot segments;
  - the method isn't GET, POST, PUT, PATCH, DELETE or HEAD.
- **What the relay does to a request:**
  - It never follows a redirect and keeps no cookies.
  - It drops hop-by-hop, `Host`, `Content-Length`, `Accept-Encoding`, proxy and cookie headers.
  - It honours the system proxy.
  - It applies AI5's plain-HTTP key rule: the host is resolved, checked and pinned, and the name goes in `Host`.
- **New kinds are Qt's decision.** A card that needs another host adds a target kind in Qt, as AI23 did with `search`. The sidecar can't name a URL outside the targets.
- **This is now a boundary.** Wherever the sandbox applies, a malicious dependency in the sidecar can reach nothing but the relay.

## Agents

- **Qt starts the agent CLIs,** each in its own sandbox, and relays their stdio. The sidecar never starts a process.
  - The sidecar sends `_hikari/agent.start {process, agent, args, env}`. It picks the process number.
  - It then sends `.write {process, data}`, `.closeInput` and `.kill`.
  - Qt sends `_hikari/agent.output {process, stream, data}` and `_hikari/agent.exit {process, code}`.
  - At most 8 agents run at once, and every agent ends with the sidecar that started it.
- **The sidecar names an agent; Qt picks the program.** The program is the CLI Qt found on `PATH` or the path the user set. Qt also sets the environment, and the sidecar may add only the variables Qt lists for that agent.
- **Claude.** The Agent SDK's `spawnClaudeCodeProcess` returns the relayed process.
  - Qt refuses any arguments that bypass permissions, add folders, plugins or settings, or name MCP servers other than the SDK's in-process ones.
  - It requires `--tools=` and `--strict-mcp-config`.
  - `claude` may write only `~/.claude`, `~/.claude.json` and its work folder. It gets the network.
- **Codex.** Qt runs the native `codex app-server`, the one npm's launcher would run, with Hikari's own `CODEX_HOME`. A guard in Qt reads the JSON-RPC in both directions:
  - only a conversation's methods pass;
  - every thread is read-only, with `approvalPolicy: "untrusted"` and no `config` overrides;
  - Qt itself declines every command, file change and permission request;
  - a response from the sidecar passes only for a request the agent sent.

  Codex's own bubblewrap sandbox can't start inside Landlock. That affects only commands, and Hikari declines all of them.
- **MCP servers run unsandboxed in v1.** They are user commands (`npx …`) that need their own config, caches and hosts, so Qt starts them without a profile. The AI guide says so.
- **ACP agents run in a profile.** Each one has the network, but it can read and write only its own state folders and a private work folder under `ai/agents/`, beside its program and the system files every process needs. Where Landlock exists, no Project or other user file is readable to it: OpenCode's four XDG `opencode` folders, Antigravity's profile, and `ai/agents/custom/<id>` for the user's own agents, which is their home too.

## Tests

- **The sandbox:** a probe child shows that files outside the profile, sockets and new namespaces are refused.
- **The relay:**
  - the key is substituted and masked, also across chunk boundaries;
  - redirects, URLs outside the base and plain-HTTP keys are refused;
  - names are pinned;
  - a cancel aborts the request.
- **The real sidecar, sandboxed,** lists models through the relay with a key only Qt holds.
- **The broker, against a fake agent:**
  - refused arguments, and the environment allowlist;
  - the Codex guard's rewrites, refusals and approvals;
  - an agent outside its profile can't start.
