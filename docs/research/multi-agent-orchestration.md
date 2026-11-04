# Sub-agents across harnesses: how t3code orchestrates, and what Hikari needs

Researched 2026-11-04 from source code and official docs. Revisions read: `pingdotgg/t3code` `96e887b` (2026-11-03) [1], `altqx/hikari` `origin/main` `86d0410eb` [2], `openai/codex` `f1b0721` [3], `zed-industries/zed` `b7a6e5c` [4]. Code citations use the prefixes `t3:`, `hk:`, `cx:` and `zed:` for those trees. **Unverified** means no primary source confirmed the claim.

## Summary

- **t3code's sub-agents are app-owned threads, not provider features.** A parent agent calls `delegate_task` on T3's own MCP server. T3 creates a child thread on any provider that has a V2 adapter, sends it only the task text, and hands the result back to the parent as a durable "context transfer". Providers' native sub-agents (Claude `Agent`, Codex `spawn_agent`) are observed and projected into the same graph, but cross-provider work always goes through `delegate_task`.
- **Hikari already has most of the parts.** It has ACP-shaped sessions on the Qt–sidecar link, Qt-owned threads, Qt-decided permissions, HA1's revision check, and AI2's review. What's missing is a dispatch tool, a parent/child lineage on threads, mode capping, aggregated cost, a child view in the panel, and a way to stop children's edits from colliding.
- **Children's edits are the hard part.** t3code isolates children with git worktrees. Hikari edits live Documents through HA1, where any write moves the whole Document's revision. The recommendation is a **write scope per child** (a Document plus a set of Lines, declared at dispatch and enforced by Qt), with a **change set per child** in AI2's review.
- **Recommended limits:** depth 1 (children can't delegate), at most 4 running children per thread, a child's mode never broader than its parent's, and child cost counted in the parent thread's cap.

| | t3code | Claude Agent SDK | Codex app-server | ACP | Zed Agent |
|---|---|---|---|---|---|
| Who starts a child | Parent calls the `delegate_task` MCP tool | Model calls the `Agent` tool | Model calls `spawnAgent` (collab tool) | Agent only; the client observes (RFD) | Model calls `spawn_agent` |
| Cross-provider child | Yes, on any instance with a V2 adapter | No | No | No | No (native Zed models only) |
| Async vs wait | `mode: "async"` (default) or `"wait"`, 10 min default timeout | Background by default (`async_launched`) | `wait` collab tool | n/a | Blocking by default |
| Depth limit | None enforced; telemetry caps at 5 | `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH`, default 3 | **Unverified** | n/a | `MAX_SUBAGENT_DEPTH = 1` |
| Concurrency limit | None enforced in the orchestrator | `CLAUDE_CODE_MAX_CONCURRENT_SUBAGENTS`, default 20 | **Unverified** | n/a | **Unverified** |
| Mode capping | Child ≤ parent; escalation refused | Child inherits `plan`/`acceptEdits`/`auto`/`bypass` from parent | Per-thread `approvalPolicy`/`sandbox` | Children must not circumvent restrictions | n/a |
| Isolation | `isolation: "shared"` or `"worktree"` | None (same cwd) | Per-thread `cwd` | Per-session `cwd` | "Disjoint write set" advice only |

## 1. t3code

### 1.1 Shape

- **One graph for forks, provider switches and sub-agents.** An `AppThread` has runs; each run has a tree of `ExecutionNode`s (tool, approval, subagent); a subagent node may own a provider thread with its own root node (`t3:docs/orchestration-v2/README.md:62-77`). The layers are native protocol → adapter/normaliser → V2 event store → execution graph → conversation projection → UI (`:44-54`).
- **Lineage:** `relationshipToParent: "fork" | "subagent" | null` (`t3:packages/contracts/src/orchestrationV2.ts:156`). Context transfers of kind `subagent_spawn` and `subagent_result` connect parent and child (`:170-171`). A subagent's `origin` is `"provider_native"` or `"app_owned"` (`:889`).
- **Two kinds of sub-agent.** Native ones are spawned by the provider and observed. App-owned ones are created when the parent calls an app tool, and their result comes back as a `subagent_result` transfer (`t3:docs/orchestration-v2/thread-lineage-and-context-transfer.md:56-72`).

### 1.2 Provider drivers

- **Driver and adapter.** Each provider instance exposes `orchestrationAdapter: ProviderAdapterV2["Service"]` (`t3:packages/provider-core/src/server/driver.ts:52`). The adapter service is `getCapabilities`, `planSelectionTransition` and `openSession` (`t3:packages/provider-core/src/server/ProviderAdapter.ts:666-681`). A session runtime has `ensureThread`, `resumeThread`, `startTurn`, `steerTurn`, `interruptTurn`, optional `stopSubagent`, `compactThread`, `respondToRuntimeRequest`, `readThreadSnapshot`, `rollbackThread` and `forkThread`, plus one normalised event stream (`:575-664`). Event kinds include `subagent.updated`, `node.updated`, `runtime_request.updated` and `turn.terminal` (`:83-183`).
- **Runtime policy per session:** `runtimeMode`, `interactionMode`, `cwd`, opaque `approvalPolicy`/`sandboxPolicy`, and an admin ceiling `maxRuntimeMode` (`ProviderAdapter.ts:48-59`).
- **Capabilities, not provider-name checks.** `SubagentCapabilities` has `supportsSubagents`, `exposesSubagentThreadIds`, `emitsSubagentLifecycle`, `canWaitForSubagents`, `canCloseSubagents` and `canForkSubagentThread` (`t3:docs/orchestration-v2/provider-capability-system.md:144-157`). Missing features degrade by policy: for example, no active steering means interrupt and restart (`:216-246`).
- **Per driver:**
  - Codex declares full native sub-agent support (`t3:apps/server/src/orchestration-v2/Adapters/CodexAdapterV2.ts:297-304`) and maps `collabAgentToolCall` items (`:1324`, `:4973`).
  - Claude declares sub-agents with lifecycle but no addressable thread ids (`ClaudeAdapterV2.ts:448-454`).
  - Generic ACP declares none (`t3:packages/provider-acp/src/server/adapter.ts:686-693`). OpenCode v2 declares them without close (`t3:packages/provider-opencode/src/server/v2/adapter.ts:150-157`).
- **Threads per process.** Codex and OpenCode v2 run several provider threads in one session (`CodexAdapterV2.ts:241`, `opencode/v2/adapter.ts:94`). Claude, ACP, Cursor, Pi and OpenCode v1 don't (`ClaudeAdapterV2.ts:391`, `provider-acp/src/server/adapter.ts:630`). A child on those drivers therefore gets its own process.
- **Which providers can run children:** any instance whose live `ProviderInstance` resolves a V2 `orchestrationAdapter`. That covers Codex, Claude Agent SDK, Cursor, Grok, ACP Registry agents, OpenCode, OpenCode 2, Pi and Antigravity. Others are listed by `orchestrator_capabilities` but marked unavailable (`t3:docs/orchestration-v2/orchestrator-mcp-server.md:187-196`).

### 1.3 How a parent's tools reach a child (MCP)

- **One authenticated MCP endpoint, `t3-code`,** at `http://127.0.0.1:<port>/mcp`. Before opening a provider session, `ProviderSessionManager` asks `McpSessionRegistry` for a bearer credential scoped to environment, parent thread, provider instance and provider session. The credential expires, idles out, and is revoked on release (`orchestrator-mcp-server.md:30-56`).
- **Injection per harness:**
  - Codex gets the server in `thread/start`, `thread/resume` and `thread/fork` config, registered twice so that `t3_thread_wait` is a direct call outside Code Mode (`:60-73`).
  - Claude gets `mcpServers: {"t3-code": {type:"http", ...}}` and `allowedTools: ["mcp__t3-code__*"]` (`:75-98`).
  - ACP agents get it in `session/new`/`session/load`/`session/fork` `mcpServers` (`:109-120`). Pi gets a bridge extension (`:151-185`).
- **Every child gets the same server.** That is how a child can delegate in turn.
- **Instructions are injected too.** For example: "use your native tool when the child uses your provider … Use `delegate_task` for another provider's model, user-asked settings, a separate checkout" and "If you are a subagent or child, do the work yourself" (`t3:packages/provider-core/src/server/orchestrationInstructions.ts:9-13`).

### 1.4 Start, await, read, cancel

- **`delegate_task` input:** `task`, `target {providerInstanceId?, driverKind?, model?}`, `title`, `role`, `isolation: "shared"|"worktree"`, `mode: "async"|"wait"` (default async), `timeoutMs` (wait only, default 10 min, "A timeout does not cancel the child"), `clientRequestId`, `runtimeMode`, `interactionMode` (`t3:packages/contracts/src/orchestratorMcp.ts:168-196`).
- **Result:** `taskId`, `childThreadId`, `childRunId`, `status` (`queued|running|waiting|completed|failed|cancelled|interrupted`), `workState` (`working|waiting_for_children|result_available`), `summary`, `resultContextTransferId`, `hasPendingQuestion`, `pendingApproval`, `waitTimedOut`, and `next` (`:204-240`). For a running task, `next` says "Completion wakes this thread, so end your turn. Do not wait on it or poll it." (`:199-202`).
- **The child gets only the task text,** plus "Act as the <role> sub-agent for this task." when a role is given (`t3:apps/server/src/mcp/delegatedTaskModes.ts:90-95`). "Parent conversation history is not copied into the child" (`orchestrator-mcp-server.md:18-21`).
- **Dispatch path:** MCP `tools/call` → `OrchestratorMcpService` → `ThreadManagementService` → V2 command `delegated_task.request` → child thread and run → parent `app_owned` subagent projection → `subagent_spawn` transfer → provider runs → terminal event → `subagent_result` transfer → wait result or `task_status` (`orchestrator-mcp-server.md:483-512`).
  - In code, `dispatchDelegatedTaskRequest` refuses unless the parent run is active and not stopping (`t3:apps/server/src/orchestration-v2/Orchestrator.ts:7672-7710`). It derives child ids from the command id, so retries are idempotent (`:7737-7749`), and emits `thread.created`, `node.updated` (kind `subagent`, `countsForRun: false`) and `subagent.updated` (`:7756-7866`).
- **Finalisation survives restarts.** It replays persisted events and is idempotent on an existing `subagent_result` (`orchestrator-mcp-server.md:509-512`). A failed child's error comes before any progress text, and success uses "the latest assistant content from the final work turn" (`:531-532`).
- **`task_status`** reads the durable projection. It refuses tasks from another parent, and the published `summary` stays stable across later follow-ups (`:317-326`). A follow-up in the child's conversation goes through `t3_thread_send` on `childThreadId`. Each new review round is a new `delegate_task` (`:265-272`).
- **Questions from children.** When a child opens a `user_input` request, the parent is steered or queued a notice to answer with `t3_pending_request` or ask the user. **Approvals are not routed** to the parent model (`:514-529`).
- **Cancel.** `task_cancel` sends `thread.stop` to the child and stops every task the child delegated (`:328-336`). The parent's Stop does the same for all its children, depth first, after commit (`t3:docs/orchestration-v2/feature-lifecycles.md:254-260`).

### 1.5 Worktree isolation

- **Default `shared`:** the child runs in the parent's checkout. With `isolation: "worktree"`, T3 resolves the parent checkout's `HEAD` and refuses if the project root can't see that commit. It then launches the child with `workspaceStrategy: {type:"worktree", baseRef: <sha>, startFromOrigin: false}` (`t3:apps/server/src/mcp/OrchestratorMcpService.ts:2393-2421`). "Uncommitted edits stay out" (`orchestratorMcp.ts:173-177`).
- **Checkpoints:** a child gets a nested checkpoint scope that doesn't advance the parent's run count (`feature-lifecycles.md:382-398`).

### 1.6 Runtime and permission modes

- **Ranks:** `approval-required` 0 < `auto-accept-edits` 1 < `auto` 2 < `full-access` 3. Interaction mode: `plan` < `default` (`delegatedTaskModes.ts:12-29`).
- **`resolveRuntimeMode`:**
  - A child broader than its parent fails with `runtime_mode_escalation_denied`.
  - An explicit mode the target doesn't offer fails with `invalid_request`.
  - An inherited mode steps down to the broadest mode the target offers (`:36-72`). Interaction mode works the same way (`:75-88`).
- **The same ceiling applies to any cross-thread tool:** "the target's runtime and interaction modes may not be broader than the caller's" (`orchestrator-mcp-server.md:534-545`).

### 1.7 Events to the UI

- **A delegated task shows up twice.** It is a `subagent` turn item in the parent's timeline (`Orchestrator.ts:7813-7836`) and a full child thread of its own.
- **No separate event bus.** UI views come from projections streamed with snapshot-plus-cursor semantics (`README.md:96-108`). The client groups children with `subagentGroupSummary` and `summarizeSubagentStatuses` (`t3:packages/client-runtime/src/state/subagentDisplay.ts:12`, `:25`).
- **Pending approvals from sub-agents** show in the UI when respondable (`provider-capability-system.md:122`, `feature-lifecycles.md:453`).

### 1.8 Limits

- **No depth or child-count limit in the orchestrator.** `McpHttpServer` walks the parent chain only to report depth to telemetry, capped at `MAX_REPORTED_DEPTH = 5` (`t3:apps/server/src/mcp/McpHttpServer.ts:892-907`, `t3:apps/server/src/telemetry/ProviderDimensions.ts:115`).
- **What does limit it:** the only child cap found is driver-specific (`MAX_CHILDREN = 16` in `t3:apps/server/src/provider/acp/AntigravityChildWork.ts:33`). In practice the bounds are the active-parent rule, mode capping, and the injected instruction that children don't delegate.

## 2. Hikari today

- **Boundary.** The sidecar runs providers, the loop, every role's model call, the MCP client and server, and the external agents. Qt keeps settings, keys, threads, permission modes, the always-prompt list, the review and all UI (`hk:docs/adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md:19-29`). "Qt decides every permission … The sidecar's own permission logic is never trusted" (`:28`).
- **The link is ACP-shaped:** `session/new`, `session/prompt`, `session/cancel`, `session/update`, `session/request_permission`, plus `_hikari/*` methods. It has 64 outstanding requests and 16 MiB outstanding (`ADR 0026:54-76`; `hk:src/ai-sidecar/src/protocol.ts:4-46`). Session methods are declared for later cards (`protocol.ts:41-46`).
- **The provider layer is stateless per call.** `_hikari/complete` takes `{stream, providerId, key?, request}`, and `Request` carries `model`, `system`, `messages`, `tools`, `context` and `cacheKey` (`hk:src/ai-sidecar/src/providers/types.ts:53-78`). `Usage` has input, output and cache counts plus `reportedCost` (`:92-103`). A Hikari-loop child on another provider or model is therefore just a session that names a different `providerId`/`model`.
- **Harnesses (AI15 #443):**
  - Claude Agent SDK: `settingSources: []`, `tools: []`, `strictMcpConfig`, `permissionMode: "dontAsk"`, only `mcp__hikari__*`, one long-running query per thread.
  - `codex app-server`: read-only sandbox, approvals declined.
  - ACP: `gemini --acp`, registry or custom command.
  - Codex and ACP agents reach Hikari's tools through a per-session stdio MCP channel ([#443](https://github.com/altqx/hikari/issues/443); ADR 0026:91-107, :89).
- **Modes (ADR 0024, AI7 #435):** Manual, Auto (the judge sees "the action and the user's messages, never file content or tool output", called through `_hikari/complete`) and Full access. The always-prompt list applies in every mode (`hk:docs/adr/0024-hikari-ai.md:43-66`). AI7's cost rule is "a cost meter, a per-thread soft cap and a daily cap. Usage comes from the sidecar; Qt enforces the caps" ([#435](https://github.com/altqx/hikari/issues/435)).
- **Review (AI2 #430):** "Pending AI change sets shown per Line …, Keep/Undo per Line and for all, one undo step per change, turn checkpoints. Edits to a background tab stage in that tab's review" ([#430](https://github.com/altqx/hikari/issues/430)). In Manual, edits wait for Keep. In Auto they apply after the judge approves, and in Full access at once (ADR 0024:60-62).
- **HA1 concurrency.** This is what children will collide on:
  - `document.apply` takes the `revision` the caller read. "If the revision moved since the read, nothing changes and the call fails with `stale_revision`" (`hk:docs/api.md:45`).
  - "One write runs at a time; another write meanwhile gets `busy`" (`:47`).
  - A caller "names itself for undo steps and holds its grants" (`:75`).
  - The revision is per Document, so two children on one tab invalidate each other's reads even on different Lines.
- **Compaction (AI21 #725)** applies to the sidecar's own loop only: "External agents (AI15) compact themselves and are out of scope". It keeps "staged edits and pending approvals" word for word, and compaction calls count toward the cost cap ([#725](https://github.com/altqx/hikari/issues/725)).
- **MCP:** AI3 #431 serves Hikari's tools to external agents, and its server code also serves AI15's per-session channel. AI13 #441 adds user MCP servers, namespaced per server, with Qt enforcing `readOnlyHint`/`destructiveHint` ([#431](https://github.com/altqx/hikari/issues/431), [#441](https://github.com/altqx/hikari/issues/441)).
- **Sign-in (AI6 #434):** Codex uses Hikari's own ChatGPT session. Claude and ACP agents use their own logins, and Hikari never sees them ([#434](https://github.com/altqx/hikari/issues/434) approved design gate, points 9 and 12).
- **No card mentions sub-agents.** The integration research lists "skills, subagents and compaction are Hikari's to build" for the own-loop option, and records Honya's role-gated sub-agents (`hk:docs/research/hikari-ai-integration.md:512-516`, `:623`).

## 3. Vendor primitives

### 3.1 Claude Agent SDK [5][6]

- **Definition.** `agents: Record<string, AgentDefinition>` takes `description`, `prompt`, `tools`, `disallowedTools`, `model`, `mcpServers`, `maxTurns`, `background`, `permissionMode`, `effort` and others. "Claude invokes subagents through the `Agent` tool". The tool is named `"Task"` in the `system:init` list and in `tool_use` blocks before v2.1.63.
- **Async by default.** A spawned sub-agent "runs in the background"; the tool result is `status: "async_launched"` and the report arrives later. It blocks (`status: "completed"`) only when Claude needs the result.
- **Context.** Fresh (non-fork) sub-agents get only their prompt and the Agent tool's prompt, plus tools. "Only its final message returns to the parent."
- **Attribution.** Sub-agent messages carry `parent_tool_use_id`, and in TS `agent_id`, which equals the `task_id` of its task events.
- **Limits:**
  - `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH` (default 3; "`1` stops your subagents from spawning any of their own");
  - `CLAUDE_CODE_MAX_CONCURRENT_SUBAGENTS` (default 20, "Concurrent subagent limit reached");
  - `maxBudgetUsd`, which counts sub-agent spend, refuses spawns, stops background sub-agents and ends with `error_max_budget_usd`.
- **Modes.** "The subagent inherits `plan`, `acceptEdits`, `auto`, or `bypassPermissions` from the main session, and under a `default`- or `dontAsk`-mode main session it runs in the mode you set" [6]. In `dontAsk`, "calls to `Agent`" need no approval [6].
- **Consequence for Hikari:** ADR 0026's `tools: []` should keep the built-in `Agent` out. **Unverified** that `tools: []` removes `Agent` specifically; `disallowedTools: ["Agent"]` makes it certain.

### 3.2 Codex app-server [7][3]

- **Threads and turns.** `thread/start` begins "a new conversation", `thread/resume` reopens one, `thread/fork` branches. `turn/start` "responds with the initial `turn` and streams events". `turn/steer` appends to the in-flight turn. `turn/interrupt` ends the turn with `status: "interrupted"` [7].
- **Per-thread settings.** `ThreadStartParams` has `model`, `cwd`, `approval_policy`, `sandbox`, `permissions`, `config` (a free map, where Hikari's MCP server goes) and `base_instructions` (`cx:codex-rs/app-server-protocol/src/protocol/v2/thread.rs:62-104`).
- **Several threads per process.** `thread/loaded/list` returns several loaded thread ids [7], and t3code drives many threads through one app-server (`CodexAdapterV2.ts:241`). The docs never state that **turns** on different threads run in parallel (**unverified**, though t3code's use implies it).
- **Native sub-agents:**
  - `ThreadItem::CollabAgentToolCall {tool, status, sender_thread_id, receiver_thread_ids, prompt, model, reasoning_effort, agents_states}` (`cx:.../v2/item.rs:437-457`).
  - `CollabAgentTool` = `spawnAgent`, `sendInput`, `resumeAgent`, `wait`, `closeAgent`, `sendMessage`, `followupTask`, `interruptAgent`, `listAgents` (`:1195-1205`).
  - Thread listing filters on source kinds `subAgent*` and on `parentThreadId`/`ancestorThreadId`, and archive/delete cascade to spawned descendants [7].
- **Not found:** the config key that turns native multi-agent off, and any depth or concurrency limit (**unverified**).

### 3.3 ACP [8][9][10]

- **Sessions.** "Each session has its own context, conversation history, and state, so one Agent can run several independent sessions." `session/new` takes `cwd` and `mcpServers` and returns a unique `sessionId` [8].
  - The spec doesn't require agents to run sessions **concurrently**. t3code's generic ACP adapter assumes one provider thread per session process (`provider-acp/src/server/adapter.ts:630`).
- **Subagent Sessions RFD** (behind `unstable_subagents`, latest revision 2026-10-30) [9]:
  - Each sub-agent is its own ACP session, associated through a `subagent_update` on the parent's stream. The state is `running`, `requires_action`, `idle` or `unknown`.
  - "The Client observes it … but cannot submit new work to it." A client may `session/cancel` a child. Pending permission requests then get the `cancelled` outcome, and they stay attributed to the child.
  - "The Agent **MUST NOT** use a child to circumvent restrictions."
  - ACP has **no client-initiated sub-agent**: Hikari creating a child is just another `session/new`.
- **MCP-over-ACP RFD:** an `acp` MCP transport (`mcpCapabilities.acp`, `mcp/connect`, `mcp/message`) [10]. This is the RFD ADR 0026:85 waits for.

### 3.4 Zed [11][12][4]

- **`spawn_agent`** "Spawns a subagent with its own context window to perform a delegated task". "Each subagent has access to the same tools as the parent agent" [11].
  - Input is `label`, `message`, an optional `session_id` (to continue one) and an optional `model`. Output is the final message plus `session_id` (`zed:crates/agent/src/tools/spawn_agent_tool.rs:45-98`).
  - Its guidance: "For code-edit subtasks, decompose work so each delegated task has a disjoint write set" (`:26`, `:31`).
- **Depth:** `MAX_SUBAGENT_DEPTH: u8 = 1`, and the tool is added only when `depth() < MAX_SUBAGENT_DEPTH` (`zed:crates/agent/src/thread.rs:77`, `:2295-2296`).
- **Models:** sub-agents inherit the parent's model unless `agent.subagent_model` is set. "This setting does not configure External Agents" [12].
- **Separate from parallel threads.** Zed's parallel threads are user-started, and two threads that "might edit the same files" can be isolated in a worktree [13]. External ACP agents are never used as sub-agents [13].

## 4. Mapping onto Hikari

| Piece | Lands on | New work |
|---|---|---|
| Dispatch tool in Hikari's loop | **New card (AI22)**, building on AI7's loop | `agent.delegate`, `agent.task_status`, `agent.task_cancel` as loop tools. Qt creates the child, so permissions stay in Qt |
| Child session per harness | AI15 (harness hosting), AI5P (providers) | A `parentSessionId`/`taskId` on `session/new`. One `claude` query per child; one `codex app-server` with one thread per child (**unverified** concurrency); one ACP process per child session |
| Tool exposure to children | AI3's server code, AI15's per-session channel | The delegate tools appear on the same `hikari` MCP server, so Claude and Codex parents can delegate too, as t3code does with `t3-code` |
| Edits into AI2's review | AI2 #430, HA1 | A change set per child, a write scope per child, and an HA1 caller per child |
| Mode capping | AI7 (modes in Qt) | Child mode ≤ parent mode; rank Manual < Auto < Full access |
| Cost | AI7 caps, AI19 usage | Child usage rolls into the parent thread's meter and soft cap and into the daily cap |
| Compaction | AI21 #725 | Open tasks and their results are kept word for word; children compact on their own |
| Panel UI | AI7's panel, design gate | Child cards in the parent timeline, a child-thread view, and the review grouped by child |
| Nesting, concurrency | New | Depth 1, at most 4 running children per thread |
| Cancellation | AI7, AI15 | Cascades from parent Stop; `agent.task_cancel`; each harness's own cancel |

### 4.1 The dispatch tool

- **Tools in the loop.** `agent.delegate {task, target? {agent | providerId, model?}, title?, role?, mode: "async"|"wait", timeoutMs?, permissionMode?: "inherit"|"manual"|"auto", scope {document, lines?}, clientRequestId?}`. It returns t3code's result shape cut down: `taskId`, `childSessionId`, `status`, `summary`, `pendingPermission`, `waitTimedOut`. Add `agent.task_status`, `agent.task_cancel` and `agent.capabilities`, which lists agents and providers that are usable now with their models.
- **The tools live in the sidecar, but Qt decides.** The sidecar turns the call into a new sidecar → Qt request, `_hikari/task.request`. Qt then:
  1. checks depth, the running-child count, the mode cap, the cost cap, and that the scope's Lines aren't held by another running child;
  2. writes the child thread, with lineage `{parentThreadId, taskId}`;
  3. answers. The sidecar then opens the child with `session/new`.
  
  This keeps ADR 0026's rule that the sidecar's permission logic is never trusted (ADR 0026:28). It matches t3code, where MCP is only an ingress and the orchestrator owns the command (`orchestrator-mcp-server.md:485-503`).
- **The child gets only the brief,** plus the role line and a fixed header: the Documents it may touch, its scope, and "do the work yourself; you cannot delegate". This follows t3code (`delegatedTaskModes.ts:90-95`) and Claude/Zed (fresh context).
- **Completion wakes the parent, with no polling.** When a child ends, Qt writes the result into the parent thread as a tool result if the parent is in a `wait`. Otherwise it queues a "task finished" message that starts or steers the parent's next turn. The `async` result tells the model to end its turn, as t3code's `next` does (`orchestratorMcp.ts:199-202`).
- **External parents get the same tools.** A Claude or Codex parent sees them as `mcp__hikari__agent_delegate` and so on through AI3/AI15's server, just as t3code injects `t3-code` (`orchestrator-mcp-server.md:58-98`).
- **Native sub-agents stay off in v1.** Keep Claude's `Agent` out (`disallowedTools: ["Agent"]`) and turn off Codex's collab tools. Native children would bypass Hikari's depth, scope and cost accounting, and the panel couldn't show them as threads.

### 4.2 Child sessions per harness

- **Hikari loop:** a new sidecar session with another `providerId`/`model` (`types.ts:53-78`). It is cheap, and needs no new process.
- **Claude Agent SDK:** one new `query()` per child, with the same locked settings as AI15. ADR 0026:98 already says one query per thread, and t3code's Claude adapter also has one thread per session (`ClaudeAdapterV2.ts:391`).
- **Codex:** `thread/start` per child on the existing `codex app-server` process, with `model`, `cwd` (the sidecar's private folder), `sandbox: read-only`, `approval_policy` and Hikari's MCP channel in `config` (`thread.rs:62-104`).
- **ACP:** `session/new` per child. Spawn a process per child unless the agent is known to run concurrent sessions, which follows t3code's ACP default.
- **The panel uses one shape for all of them.** Every child, of any harness, reaches Qt as its own `session/update` stream. On the parent stream it gets one `subagent_update {sessionId, state}` using the ACP RFD's states (`running|requires_action|idle|unknown`) [9]. This keeps ADR 0026's "panel reads the same events" rule.

### 4.3 Children's edits and AI2's review

The risk is real. HA1 refuses a write when the Document's revision moved (`docs/api.md:45`), and every child write moves it, so parallel children on one tab would keep failing with `stale_revision` and retrying.

Recommended design:
1. **An HA1 caller per child,** named `"AI · <child title>"`. Undo steps then read `"AI · Translate ep 3 lines 1–200: <step>"`, and grants never exceed the parent's (`docs/api.md:75`).
2. **A write scope per child,** declared in `agent.delegate`: one Document, and optionally a set of Line ids or a range. Qt refuses a child's write outside its scope with `not_granted`, and refuses a dispatch whose scope overlaps a running child's. This is Zed's "disjoint write set" advice (`spawn_agent_tool.rs:26`) made enforceable. It is the Hikari equivalent of t3code's worktree.
3. **Retry inside the scope.** When a child's `document.apply` gets `stale_revision`, the sidecar re-reads and retries once on its own if none of the child's scoped Lines changed. This needs HA1 or Qt to report which Lines moved between two revisions, for example a `lines.changedSince(revision)` read; that is new. Otherwise it fails back to the model.
4. **A change set per child in AI2's review,** grouped under the parent turn, each with Keep/Undo. Tab badges count all of them. Turn checkpoints nest: a child's change set is a sub-checkpoint of the parent turn, as t3code's nested checkpoint scopes are (`feature-lifecycles.md:382-398`).
5. **Writes stay serial.** `busy` (`docs/api.md:47`) is fine: writes are short, and the sidecar retries `busy` with a small backoff.

Alternatives considered:
- **Lock per tab** (one writing child per Document): simpler, but it serialises translation fan-out, which is the main use case.
- **A scratch copy per child** (a hidden Document, merged later): the closest match to worktrees, but it needs a Line-level merge and the review would show a merge, not edits. Leave it for later.

### 4.4 Mode capping

- **Ranks:** Manual 0 < Auto 1 < Full access 2. A child's mode defaults to the parent's; a broader one is refused (`mode_escalation_denied`), and a narrower one is allowed. This copies `resolveRuntimeMode` (`delegatedTaskModes.ts:36-72`).
- **The always-prompt list applies to every child** (ADR 0024:51-58). A child's `session/request_permission` shows in the panel on the parent thread, attributed to the child, as ACP's RFD requires attribution [9]. The parent model can't answer it, as in t3code (`orchestrator-mcp-server.md:523`).
- **The judge** (Auto) for a child's action sees the action, the **root thread's user messages**, and the delegation brief **marked as agent-written**. The brief must not count as the user's consent, or a prompt-injected parent could write a brief that gets its child's actions approved.
- **Changing the parent's mode** mid-run narrows running children at once and never widens them.

### 4.5 Cost

- **One meter.** Qt adds each child's `Usage` and cost (`types.ts:92-103`) to the parent thread's meter, soft cap and the daily cap. Compaction calls in children count too (AI21).
- **Over the soft cap:** `agent.delegate` is refused (`cost_cap`) and running children finish their current turn and stop, mirroring the Claude SDK's `maxBudgetUsd` behaviour [5].
- **Subscriptions.** Claude and Codex children on a subscription report $0, so the meter shows their tokens only (`hk:docs/research/agent-sdk-hosting.md:66`).
- **A fan-out of 3 or more children** counts as a "bulk run" for ADR 0024's estimate before bulk runs (ADR 0024:88).

### 4.6 Compaction

- **Children compact on their own.** Hikari-loop children use AI21; external children use their own harness.
- **The parent keeps open tasks word for word.** Every `agent.delegate` call whose task is still open, and its `taskId`, is added to AI21's "kept word for word" list, next to staged edits and pending approvals.
- **Results are bounded.** A child's result enters the parent as a tool result capped at 8k tokens, with the full text in the child thread. Older child results are stage-1 candidates (cleared to a stub) like read-tool results.

### 4.7 Panel UI

- **In the parent's timeline,** a child card shows the title, harness and model, state (running, needs you, done, failed, cancelled), cost, and Open and Stop actions. Several cards from one turn collapse into a group ("3 sub-agents · 2 running").
- **Opening a card** shows the child thread read-only except for Stop and permission answers. Following t3code, a child takes follow-ups only through the parent (`orchestrator-mcp-server.md:269-272`) or a new task.
- **The review groups by child.** Background-tab badges count children's edits.
- **This needs a mockup addendum** to AI7's design gate (ADR 0024:93).

### 4.8 Nesting and concurrency

- **Depth 1:** children get no delegate tools, and the instruction says so. Zed uses the same limit (`thread.rs:77`). Claude's own default is 3 [5], and t3code enforces none.
- **At most 4 running children per thread.** Extra requests fail with `too_many_children`; they aren't queued. The 64-request limit on the link (`protocol.ts:4-8`) and per-provider rate limits make a larger fan-out fragile.

### 4.9 Cancellation

- **`agent.task_cancel`** stops one child: Hikari loop `session/cancel`, Claude `interrupt()`, Codex `turn/interrupt`, ACP `session/cancel` [7][8]. The task ends `cancelled` with its partial summary.
- **The parent's Stop cascades** to every running child after the parent stops, as t3code does (`feature-lifecycles.md:254-260`).
- **Staged edits stay in the review,** as with `SidecarLost` (ADR 0026:50).
- **On a sidecar crash,** running children end `interrupted` and aren't rerun. Qt keeps them in the thread with a Retry action.

## 5. Open questions for the owner

1. **Should sub-agents be cross-harness from v1, or Hikari-loop only first?** *Recommendation:* the Hikari loop on any provider and model in v1, with Claude, Codex and ACP children as soon as AI15 lands, behind the same tool. The tool shape doesn't change between the two.
2. **How are children's edits isolated?** *Recommendation:* a write scope per child (a Document plus Lines) that Qt enforces, and a change set per child in AI2's review. A lock per tab would serialise translation fan-out, and a scratch copy needs a merge UI.
3. **May children delegate?** *Recommendation:* no (depth 1), like Zed.
4. **How many children run at once?** *Recommendation:* 4 per thread, refused above that, not queued.
5. **May native sub-agents (Claude `Agent`, Codex collab tools) run?** *Recommendation:* off in v1. They escape Hikari's scope and depth rules and don't show as threads.
6. **Who answers a child's permission request?** *Recommendation:* the user, in the panel, attributed to the child. Never the parent model. In Auto, the judge sees the root thread's user messages plus the brief marked as agent-written.
7. **Can a child run in a narrower mode than its parent?** *Recommendation:* yes. The model may ask for Manual for a child, and never for a broader mode.
8. **Do children count toward the parent's soft cap?** *Recommendation:* yes. A fan-out of 3 or more children shows the bulk-run estimate first.
9. **Can the user talk to a child directly?** *Recommendation:* no in v1. The child view is read-only except for Stop and permission answers.
10. **Should external parents (Claude Code or Codex through AI3) get the delegate tools?** *Recommendation:* only for agents hosted inside Hikari (AI15). Agents connected to AI3 from outside already have their own orchestration.

## 6. Draft card

> **AI22: AI: sub-agents across harnesses (orchestration)**
>
> ## Parent
>
> #410 (wave 8). ADR 0024 and ADR 0026 apply. Research: `docs/research/multi-agent-orchestration.md` (t3code's `delegate_task`, the Claude Agent SDK's sub-agents, Codex app-server threads, ACP's Subagent Sessions RFD, Zed's `spawn_agent`).
>
> **Blocked by:** AI7 #435 (the loop, modes and panel), AI2 #430 (the review) and AI15 #443 (the external harnesses). AI21 #725 is changed by this card.
>
> ## What
>
> A thread's agent can hand self-contained tasks to child agents on any harness Hikari hosts, and orchestrate them.
>
> - **Tools** in the sidecar's loop, and on the `hikari` MCP server for agents hosted in Hikari (AI15): `agent.capabilities`, `agent.delegate`, `agent.task_status` and `agent.task_cancel`. `agent.delegate` takes the task, a target (Hikari's own agent on a provider and model, Claude, Codex or an ACP agent), a title, a role, `async` or `wait`, a mode and a write scope.
> - **Qt decides.** The sidecar asks Qt with `_hikari/task.request`. Qt checks depth, the running-child limit, the mode cap, the cost cap and the scope, saves the child thread with its parent and task id, and only then is the child's `session/new` sent.
> - **The child sees only the task,** never the parent's conversation. Its result returns to the parent as a tool result of at most 8k tokens. The full text stays in the child thread.
> - **Harnesses:** Hikari's loop on any provider; one Claude Agent SDK query per child; one `codex app-server` thread per child; one ACP session per child. Native sub-agents (Claude's `Agent`, Codex's collab tools) stay off.
> - **Edits:**
>   - Each child is its own HA1 caller, so undo steps name it.
>   - Each child has a write scope (one Document, optionally a set of Lines). Qt refuses writes outside it and refuses a dispatch that overlaps a running child's scope.
>   - Each child's edits form their own change set in AI2's review, grouped under the parent's turn.
>   - On `stale_revision`, the sidecar retries once when none of the child's Lines changed.
> - **Modes:** Manual < Auto < Full access. A child's mode is never broader than its parent's, and narrowing the parent narrows running children. The always-prompt list applies to children. A child's permission requests show on the parent thread with the child's name, and only the user answers them. In Auto, the judge sees the root thread's user messages and the brief marked as agent-written.
> - **Limits:** children can't delegate (depth 1). At most 4 children run per thread; more are refused.
> - **Cost:** children's usage and cost count toward the parent thread's meter, soft cap and the daily cap. Over the soft cap, new children are refused and running ones stop after their turn. Starting 3 or more children shows the bulk-run estimate first.
> - **Compaction:** open tasks and their ids are kept word for word, as are staged edits. Older child results are cleared to a stub in stage 1. Children compact themselves.
> - **Cancel:** `agent.task_cancel` stops one child, and the parent's Stop stops all of them. Each harness uses its own cancel. A cancelled child keeps its partial result, and its staged edits stay in the review.
> - **Panel:** a card per child in the parent's timeline, showing title, harness and model, state, cost, Open and Stop, grouped per turn. Open shows the child thread read-only, except for Stop and permission answers. The review and tab badges group edits by child.
>
> ## Acceptance criteria
>
> - [ ] Mockup addendum to AI7's panel approved; evidence in the four themes.
> - [ ] A scripted provider in CI: a parent starts two Hikari-loop children on different providers with disjoint scopes on one tab. Both finish, their change sets appear separately in the review, and each one's Undo restores the exact Lines.
> - [ ] Fake `claude`, `codex` and ACP agents each complete a delegated edit task as children through `_hikari/api.call`.
> - [ ] Qt refuses, even when the sidecar asks: a child broader than its parent, a write outside the child's scope, an overlapping scope, a fifth running child, delegation from a child, and delegation over the soft cap.
> - [ ] A child's permission request shows on the parent thread, and the parent model can't answer it. The judge's input for a child holds no file content or tool output.
> - [ ] Stopping the parent stops every child. A cancelled child's staged edits stay in the review. A sidecar crash marks running children interrupted without rerunning them.
> - [ ] Children's cost appears in the parent's meter and counts toward its caps. Compaction of the parent keeps open tasks word for word.
> - [ ] Strings go through TR1, docs/ai-agent.md is updated, and a Site note is given.
> - [ ] Ubuntu and Windows MSVC CI green.
>
> **Design gate:** the panel addendum (child cards, the child view, review grouping) needs the owner's approval in light and dark before production code.

## Sources

1. https://github.com/pingdotgg/t3code/tree/96e887b6a866ba62ed3d6c2276651b4abb3441b1 (`docs/orchestration-v2/*.md`, `apps/server/src/mcp/`, `apps/server/src/orchestration-v2/`, `packages/contracts/src/orchestratorMcp.ts`, `packages/provider-core/src/server/`)
2. https://github.com/altqx/hikari/tree/86d0410eb (`docs/adr/0024-hikari-ai.md`, `docs/adr/0026-…`, `docs/api.md`, `docs/research/agent-sdk-hosting.md`, `docs/research/hikari-ai-integration.md`, `src/ai-sidecar/src/`); issues [#435](https://github.com/altqx/hikari/issues/435), [#443](https://github.com/altqx/hikari/issues/443), [#431](https://github.com/altqx/hikari/issues/431), [#441](https://github.com/altqx/hikari/issues/441), [#430](https://github.com/altqx/hikari/issues/430), [#725](https://github.com/altqx/hikari/issues/725), [#434](https://github.com/altqx/hikari/issues/434)
3. https://github.com/openai/codex/tree/f1b0721c88002066201e7d977c449fbf14f1e499/codex-rs/app-server-protocol/src/protocol/v2 (`item.rs`, `thread.rs`)
4. https://github.com/zed-industries/zed/tree/b7a6e5cdc013c5518cfb0a119b2aa457c538f020/crates/agent/src (`thread.rs`, `tools/spawn_agent_tool.rs`)
5. https://code.claude.com/docs/en/agent-sdk/subagents
6. https://code.claude.com/docs/en/agent-sdk/permissions#available-modes
7. https://learn.chatgpt.com/docs/app-server (redirected from developers.openai.com/codex/app-server)
8. https://agentclientprotocol.com/protocol/session-setup
9. https://agentclientprotocol.com/rfds/subagents.md
10. https://agentclientprotocol.com/rfds/mcp-over-acp
11. https://zed.dev/docs/ai/tools
12. https://zed.dev/docs/ai/agent-settings
13. https://zed.dev/docs/ai/agent-panel
