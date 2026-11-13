# AI agent

The agent works through the user's subtitles in threads, one conversation each ([ADR 0024](adr/0024-hikari-ai.md), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [AI7 #435](https://github.com/altqx/hikari/issues/435)). The sidecar runs the loop: model requests, tool calls, the plan and steering. Qt owns everything else: the threads and their history, the permission modes, the judge, the cost caps, the review and the UI.

AI7 ships in parts:

- **AI7a**: the loop and the thread engine, with no UI.
- **AI7b**: the Agent panel (panel 14), its two commands (5252, 5253) and the Settings section.
- **AI7c**: Manual-mode proposals ([#886](https://github.com/altqx/hikari/issues/886)).

[Compaction](#compaction) is AI21 ([#725](https://github.com/altqx/hikari/issues/725)); sub-agents (AI22) are a separate card.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/ai-sidecar/src/agent/session.ts` | `AgentSession`: one thread's turns, its tools, `update_plan`, `read_skill`, steering and cancel. `AgentSessions` holds them by id. |
| `src/ai-sidecar/src/agent/compaction.ts` | The token count, the kept ranges, stage 1's stubs and stage 2's summary request. |
| `src/ai-sidecar/src/protocol.ts` | The session methods and `session/update` shapes. |
| `src/backends/ai_agent.{h,cpp}` | The policy functions, `AgentThread`, `ThreadStore` and `AgentThreads`, which runs the threads and decides every call. |
| `src/backends/ai_sidecar_host.{h,cpp}` | The session gate: a call that names a session waits for Qt's decision. |
| `src/app/agent_controller.{h,cpp}` | `AgentController`: builds `AgentThreads` while AI is on and gives the panel its threads, the open thread and its items. |
| `src/ui/AgentPanel.qml` | The Agent panel. |

## The protocol

The messages follow the [Agent Client Protocol](https://agentclientprotocol.com) where it has one.

| Method | Direction | Params | Result |
| --- | --- | --- | --- |
| `session/new` | Qt → sidecar | `{sessionId, model: {providerId, model}, system?, history?, cacheKey?, caching?, maxOutputTokens?, webSearch?, contextTokens?, autoCompact?, delegate?, task?}` | `{sessionId}` |
| `session/prompt` | Qt → sidecar | `{sessionId, prompt: [Part], model?, caching?, webSearch?, contextTokens?, autoCompact?}` | `{stopReason}`: `end_turn`, `max_tokens`, `max_turn_requests`, `refusal`, `cancelled` or `compaction_failed` |
| `_hikari/session.compact` | Qt → sidecar | `{sessionId, instruction?, apply?}` | `{compacted, summary?}` |
| `session/cancel` | Qt → sidecar | `{sessionId}` (a notification) | none |
| `_hikari/session.steer` | Qt → sidecar | `{sessionId, prompt: [Part]}` | `{taken}` |
| `_hikari/session.close` | Qt → sidecar | `{sessionId}` | `null` |
| `session/update` | sidecar → Qt | `{sessionId, update}` | none |
| `_hikari/task.capabilities` | sidecar → Qt | `{sessionId}` | `{targets, running, limit, mode}` |
| `_hikari/task.request` | sidecar → Qt | `{sessionId, task, title?, role?, target?, mode?, scope, clientRequestId?}` | a task state |
| `_hikari/task.status` | sidecar → Qt | `{sessionId, taskId}` | a task state |
| `_hikari/task.wait` | sidecar → Qt | `{sessionId, taskId, timeoutMs}` | a task state |
| `_hikari/task.cancel` | sidecar → Qt | `{sessionId, taskId}` | a task state |

- Qt names the session: the thread's id. `session/new` again replaces the session, which is how a thread reopens after a restart or a crash, with its saved `history`.
- The system prompt is Qt's `system` followed by the [skills catalogue](ai-skills.md#progressive-disclosure), which the first turn looks up and the thread keeps.
- A turn makes at most 64 model requests. Text comes in `agent_message_chunk` updates, at most one every 16 ms.
- `update` is one of `agent_message_chunk {content}`, `tool_call {toolCallId, title, kind, status, rawInput}`, `tool_call_update {toolCallId, status, content}`, `plan {entries}`, `usage {usage, compaction?}`, `history {message}`, `compacted {…}`, `compaction_failed {reason}`, `sources {textLength, citations?, searchSuggestions?, searches?}` and `search_failed {service, reason, httpStatus}`. `history` carries each message the sidecar keeps, in the order kept, so Qt's copy stays whole.
- **Web search** (AI23b). `webSearch` on `session/new` lets the session search, and on `session/prompt` switches it from that turn on. While it is on, requests ask for the provider's own search, and `search_tools` offers the `web` group if the sidecar has a search service. While it is off, the `web` tools leave the offered tools too, even ones `search_tools` already found.
- **`sources`** comes after each model request that cited, searched or sent Google's suggestions, before its `usage`. `textLength` is that request's answer text in UTF-16. `citations` are AI23a's `{index, url, title, quote?, start?, end?}`, with spans in that text. `searches` are the provider's own searches and page reads, `{kind: "search" | "read", query?, url?, urls?}`: Anthropic's `server_tool_use` blocks, OpenAI's `web_search_call` items (with their `sources`), and Gemini's `webSearchQueries` with its grounding pages.
- **`search_failed`** comes when the search service answers that the plan has no searches left (`usedUp`) or refuses the key (`key`). The tool call still fails for the model with words saying to answer without searching, so the turn goes on.
- The tools are the [Hikari AI tools](ai-tools.md), plus `update_plan` and `read_skill`, plus the `web` group while web search has a search service (AI23). Every call the session makes goes out as `_hikari/api.call` with `session` set.
- **Cancel.** A cancelled model request ends at once. During tool calls, the running call finishes and each one after it gets the result "The user stopped the turn.", so the history stays valid.
- **Steer.** A steered message joins the turn after its next tool call, appended to the tool results. `{taken: false}` means the turn ended first. Qt then sends the message as the next turn.

## Sub-agents

AI22 ([#730](https://github.com/altqx/hikari/issues/730)): a thread hands parts of its work to sub-agents, each a thread of its own on any provider or agent. The design is AI7's approved mockup, states 12–14 ([#435](https://github.com/altqx/hikari/issues/435), Q16–Q18), with the details of owner Q414–Q423. The `_hikari/task.*` methods are link methods, not HA1 operations, and the protocol stays at version 1.

- **The tools.** A root thread's session gets `delegate: true`, which offers the `agent` group: `agent_capabilities`, `agent_delegate {task, title?, role?, target?, mode?, wait?, timeoutMs?, scope: {document, lines?}, clientRequestId?}`, `agent_task_status` and `agent_task_cancel`. Hikari's loop offers them from the start, and `search_tools` finds them by name without moving them out of the cached prefix ([tools](ai-tools.md#the-tools)); Claude Code through its in-process MCP server and Codex and ACP agents through their `channel:<session>` server. Claude Code's own Agent and Task tools are off, and Codex starts with `features.multi_agent` and `features.multi_agent_v2` off.
- **A child** is a thread saved with `task: {parent, id, brief, scope, status, …}`. Its session gets `task: {parent, taskId, scope}` and a `system` that names the Document and Lines it may change and says it can't delegate. Its first item is the brief, marked as written by the agent. Children aren't in the threads list; deleting the parent deletes them.
- **Qt checks every request**, whatever the sidecar asks. It refuses with error -32020 and `data.code`:

| Code | When |
| --- | --- |
| `depth` | the asking thread is a child itself |
| `too_many_children` | 4 children of the parent already run |
| `mode_escalation_denied` | the asked mode is broader than the parent's |
| `cost_cap`, `daily_cap` | the parent is at its cap, or the day at its cap |
| `scope_overlap` | the scope shares Lines with a running child's, or either takes the whole Document |
| `invalid_scope` | the Document isn't the parent's editing target or attached to it |
| `no_target` | the named model or agent isn't one `agent_capabilities` lists |
| `declined` | the user cancelled this turn's estimate |
| `not_found`, `stopped` | no such task of the parent's; the parent's turn stopped |

- **The scope.** A child's writes go only through `document.apply` on its Document, and with Lines set only to those Lines and the Lines it inserts. Styles, Script Info, other writes and non-read-only MCP tools are refused. Reads and view changes are free. A child edits with the revision it read; on `stale_revision` the sidecar re-reads the Lines and retries once when they are unchanged.
- **The mode** is the parent's or narrower. Changing the parent's mode narrows running children at once, never widens them.
- **Targets.** `agent_capabilities` lists the providers' chat models and the Claude Code, Codex and ACP agents found on `PATH`. With no target named, a child runs on its parent's model or agent.
- **The estimate.** Before a turn's third child starts, the parent's timeline shows an estimate card, once per turn: the briefs plus the scopes' Lines, at about 4 characters per token, priced at the target model's price. Run starts it; Cancel refuses it and every later delegation in that turn with `declined`.
- **Results.** `wait: true` waits up to `timeoutMs` (10 minutes by default, 30 at most). A timeout leaves the child running. Stopping the parent answers its open waits with `stopped`. Otherwise a finished child's report, cut to 32,000 characters, goes to the parent as its next turn, "Results from your sub-agents:", once the parent isn't running and no child waits for the user.
- **Ends.** A child ends `done`, `failed`, `stopped` (by the user, by the parent's Stop, or `cap`) or `interrupted` when the sidecar is lost; an interrupted child doesn't rerun. A child running when Hikari quit comes back interrupted. Its staged edits stay in the review in every case.
- **Questions.** A child's open asks, proposals and scripts show in the parent's timeline as well, named after the child. Only the user answers them; the parent model never sees them. The judge of a child's call sees the root thread's user messages and the child's brief, marked as the agent's words, never a file or a tool's output.
- **Cost.** A child's cost, judge included, counts in its parent's meter and toward the parent's cap and the daily cap. Going over stops the parent and its children.
- **Review.** A child's changes are review turns of their own whose `parent` is the parent's turn; the Review panel shows them under the parent's turn, each as its own change set with Undo and Keep. Undo and History name the child: "<child title>: <change name>".

## Compaction

The sidecar compacts what a thread replays to the model, on every provider (owner, 2026-11-04, after Zed's `agent.auto_compact`). Provider-side compaction is opaque or tied to encrypted content, so Hikari doesn't use it. External agents (Claude Code, Codex) compact themselves; their threads have no compaction.

- **Counting.** The last response's real `inputTokens`, plus about 4 characters per token for everything added since. The window is the model's `contextTokens` from the catalogue, 128k when it doesn't say. Qt sends both on `session/new` and `session/prompt`.
- **Kept word for word:** the system prompt and the tools (so the cached prefix stays), the first user message, the last two user messages and everything after them. Staged edits, pending approvals and attached tabs live in Qt, not in the replayed messages, so compaction never touches them.
- **Stage 1** replaces each older result of a read-only tool (`readOnlyHint`, `update_plan`, `read_skill`) with a one-line stub: "[Cleared to save context: lines_find returned 26950 characters. Call it again if you need them.]".
- **Stage 2** asks the thread's own model for a summary of the older turns, at most 8,192 tokens, in fixed sections: Goal, Decisions, Files and Lines touched, Open work, User preferences. It replaces the messages between the first user message and the last two with one assistant message, "Summary of the conversation so far" and the summary. A later compaction sees that message in its transcript and folds it in.
- **Automatically**, with `autoCompact` on, before each model request of a turn, so also between tool calls: at 90% of the window. Stage 1 runs first; stage 2 runs only if stage 1 leaves 60% or more, and aims under 40%. After a compaction the next automatic one waits until the count has grown by another 10% of the window.
- **By hand**, `_hikari/session.compact` runs both stages, with the user's `instruction` added to the summary request. It is refused while a turn runs: Qt holds it until the turn ends, then sends any queued message. `apply: false` only returns a summary of the whole thread (New thread from summary).
- **Failure.** A failed summary request is retried once. A second failure changes nothing: a manual compaction answers with the provider's error, and an automatic one sends `compaction_failed {reason}` and stops the turn with `compaction_failed`.
- **Cost.** Each summary request sends `usage` with `compaction: true`. It counts toward the thread's cost, its cap and the day's, but not toward the context tokens.
- **`compacted {stage, before, after, stubs: [{callId, content}], summary?, message?, headEnd?, keptFrom?}`**: each listed `toolResult` takes its stub; with a summary, the messages from `headEnd` up to `keptFrom` become `message`. Qt applies the same change to the history it saves, so a reopened thread replays the compacted conversation. `before` and `after` are estimates.
- **History.** The panel's items keep the whole conversation. A user item's `history` index follows its message; it goes when the summary replaced the message, since Retry can't go back past it.

## Modes

Each thread has a mode. Qt decides every write a session sends, so a sidecar that asks for more gets no more.

| Mode | Writes |
| --- | --- |
| Manual | `document.apply` becomes a [proposal](#proposals) and writes nothing until the user applies it. Other writes ask: Allow once, Allow in this thread, or Deny. |
| Auto | The judge decides. When it doesn't approve, the user is asked with the judge's reason. |
| Full access | Everything runs, except the always-ask list, [AI-written Lua](#ai-written-lua) and [plugin installs](#ai-written-plugins). |

- **Private settings.** `settings_list` and `settings_get` never show a thread or an MCP agent the `privacy.*` settings (the usage data ID among them) or the `recent.*` lists (recent-file paths and times). Folder settings stay readable. Plugins still read them.
- Reads never ask. Neither do `selection.set`, `panels.open`, `panels.close` and `panels.focus`, which change no content and no setting.
- `typeset.apply` isn't on the always-ask list: it changes the Document only after its render check passes, and asks as other writes do ([#1002](https://github.com/altqx/hikari/issues/1002)). Through MCP it is refused like every write but `document.apply`.
- `karaoke.apply_template` changes the Document like `typeset.apply` and asks as other writes do.
- `translation.project_write` follows the thread mode and isn't on the always-ask list: it writes only Honya's four project files in the document's own project folder ([AI translation](ai-translation.md)). No agent tool offers it; a translation run uses it after its Lines are written.
- `plugins.scaffold` asks as other writes do: it writes only the AI plugins folder and installs nothing ([AI-written plugins](#ai-written-plugins)). `plugins.install` asks on every install in every mode, and `plugins.run` is on the always-ask list. Through MCP all three are refused.
- `effects.save` and `effects.apply` ask as other writes do: the first writes a graph Line, the second a graph's generated Lines, each one undo step. Neither runs a Lua node, so an AI-built graph never runs code the user hasn't seen ([#444](https://github.com/altqx/hikari/issues/444)). Through MCP both are refused.
- **MCP servers' tools** go through the modes by their hints: a trusted server's read-only tools never ask, destructive ones ask in every mode, and the mode decides the rest ([MCP servers](ai-mcp-clients.md#what-qt-enforces)).
- **Web search** isn't under the thread modes or the always-ask list: the `web` tools go out through the HTTP relay, not `_hikari/api.call`. The per-thread globe (AI23b) controls it instead. It is on only while Let AI search the web is on, and a search sends only its query, never a file.
- **Allow in this thread** covers the operation, or for `commands.run` the one command, until the thread is deleted.
- **The always-ask list** ([#410](https://github.com/altqx/hikari/issues/410)) asks in every mode, with Allow once and Deny only. `alwaysAsk` holds it:

| Category | Asks for | Doesn't ask, and why |
| --- | --- | --- |
| Deleting or overwriting files outside the subtitles' folders and the user folder | `files.write` to a file outside them (checked by the granted file's path); `commands.run` `GLOBAL_DELETE_TEMPORARY_FILES`; `settings.set` `fontCollector.directory` to a folder outside them | The Save commands write only the subtitle's own file and open a file dialog for any other path. Save as, Export, Save timecodes and Save session open a file dialog, so the user picks the path. Video snapshots make new files and never overwrite. `VIDEO_DELETE_FILE` only closes the video. |
| Running external programs | `commands.run` `GLOBAL_AUTOMATION_LOAD_SCRIPT`, `GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT`, `GLOBAL_AUTOMATION_REPEAT_LAST_RUN`, `GLOBAL_AUTOMATION_RELOAD_AUTOLOAD` and any `plugin.` command; every `plugins.run`; `settings.set` `automation.scriptEditor`, `automation.lastScript`, `automation.disabledAutoload`, `updater.autoInstall`; every `karaoke.align`, which runs the user's WhisperX or `mfa` | `commands.run` reaches only built-in commands today; plugin commands ask in case that changes. |
| Installing a plugin or granting full trust | `settings.set` `plugins.autoUpdate`, `plugins.network`, `automation.trustedFolders`; `plugins.install` has its own card ([AI-written plugins](#ai-written-plugins)) | The Plugins window (`GLOBAL_PLUGINS`) installs and trusts only when the user clicks. |
| Sending local files to the network | `settings.set` of any `privacy.` setting (crash reports, usage data, Discord) | Crash and issue reports send only from their own dialogs. |
| AI settings | `settings.set` of any `ai.` setting | |

  The subtitles' folders are the folders of the open tabs' files. There is no project folder yet.

  `hikari_backends_ai_agent_tests` fails when a new write operation appears, or a new Automation or delete command, or a new setting under `automation.`, `plugins.`, `privacy.`, `updater.`, `fontCollector.` or `fonts.`, until it is classified here.
- **A denial** goes to the model as a `refused` error that says the user denied it, with the user's message if they wrote one, and tells it not to try again.
- **The judge** is a `_hikari/complete` request on the Judge role. It sees the action and the user's own messages, never a file's content or a tool's output, so nothing the agent read can talk it into approving. Anything but `{"decision": "approve"}` asks the user. Its cost counts toward the thread's.
- A write with no session, other than from an MCP connection, is refused once any thread runs. So is a write from a thread whose turn has stopped.

## Proposals

In Manual mode each `document.apply` waits as a proposal card in the thread's timeline. The Review panel and the Grid don't change until the user applies it (owner Q225, Q253–Q262).

- **One card per call.** It shows the Document, the change's name and one row per edit: the Line's row with its old text struck through and its new text, or Inserted, Deleted, Moved, the other fields set, or the Script Info or Style operation. The first five rows show; Show all opens the rest. Clicking a row selects its Line and applies nothing.
- **Apply** runs the call as it came, into the review as one undoable turn, and the card gives way to the change row. **Decline** writes nothing; the agent gets `refused` with reason `declined`: "The user declined these subtitle edits; nothing changed. Don't propose them again unless they ask.", followed by the user's words from **Decline and tell the agent…**. Edits are applied or declined as a whole, never one Line at a time, and there's no Allow in this thread for them.
- **Out of date.** When the Document's revision moves while a proposal waits, the card turns out of date and Apply goes off, since `document.apply` would refuse the old revision anyway. **Ask again** answers the agent `stale_revision`, so it reads the Document again and proposes again.
- **The turn waits** on the proposal as it does on a question. Stop, a cost cap and deleting the thread withdraw it with nothing written.
- **Focus.** A new proposal takes the keyboard focus on Apply only when the focus is in the Agent panel but not in the message box, so Enter in the message box never applies one. Enter presses the focused button; Esc closes the reply field.
- MCP agents, plugins and AI features are unchanged: their edits go straight into the review.

## AI-written Lua

AI12 ([#440](https://github.com/altqx/hikari/issues/440), ADR 0024). The sidecar only writes the script. `automation_run {name, script, macro?}` (`automation.run`) carries it to Qt, which asks the user on every run, in every mode:

- **Never judged, never allowed for the thread.** `decide` answers `Script` for `automation.run` before anything else. The card offers Run and Don't run only, and a rerun of the same script asks again.
- **Only an approved call runs.** Run sets `scriptApproved` on that one call's caller. `automation.run` refuses any other caller with `not_granted`, so a plugin, an MCP agent or a thread can't run Lua without the card. MCP agents can't reach it anyway: through MCP only `document.apply` writes.
- **An ordinary macro.** Qt saves the script to `<user folder>/ai/scripts/<name>.lua`, replacing one of that name, and loads it in the Lua helper ([ADR 0006](adr/0006-isolated-lua-automation-host.md)). It runs one macro, the one named or the first, on the active tab's selection. That is one undo step, "Automation: <macro>" like any macro, and the Automation progress card offers Force stop. The script is unloaded afterwards and is never added to Autoload or to the subtitle's Script Info.
- **The answer** is `{file, outcome, message, log}`, with the outcome Automation reports: `applied`, `refused`, `failed`, `cancelled`, `declined`, `stopped`, `lost` or `invalid` (no such macro). Another macro running answers `busy`; no open document, `refused`.
- AI-written Lua never runs in the sidecar.

## AI-written plugins

AI17 ([#445](https://github.com/altqx/hikari/issues/445), owner Q406–Q412). An agent writes a plugin with `plugins_scaffold` and asks to install it with `plugins_install`:

- **Scaffold.** `plugins.scaffold {plugin, name, commands, code?, files?, tier?, permissions?}` writes `<user folder>/ai/plugins/<id>/` from the [plugin guide's](plugin-guide.md) template and packs it as `<id>.hikariplugin` beside it, replacing an earlier one. The manifest is restricted unless `tier` is `unrestricted`; without `code`, `main.lua` registers each command. It asks as other writes do and installs nothing.
- **What installs.** Only a package `plugins.scaffold` made or a `.hikariplugin` the user attached or granted (`file`). Never a marketplace package. A package over 1 MiB is refused before any card.
- **Every install asks, in every mode.** `decide` answers `Install` for `plugins.install` before anything else; the judge never sees it and there's no Allow in this thread. The card, "Install this plugin?", shows what the Plugins window's install question shows: the tier, Unverified or the signing key, the permissions and the commands it adds, then the first 12 lines of `main.lua` and Show all N files. It offers Install, Don't install and Don't install and tell the agent….
- **Full trust.** A manifest with `tier: unrestricted` turns the card red: "Install with full access?", with the button named so and never the default. A package signed by another key than the installed copy shows the key warning and Trust and install.
- **Only the approved package installs.** `plugins.review` gives the package's SHA-256; Install sets `approvedPackage` to it on that one call's caller. `plugins.install` refuses any other caller with `not_granted`, and a package that changed since the card with `refused`. Qt installs through the same path as Install from file…, so the plugin turns on and Plugins can turn it off or uninstall it.
- **One row per install** stays in the thread: "Installed X 0.1.0 · restricted" with Open in Plugins, or "Didn't install X" with the user's words. A declined install answers the agent `refused` with reason `declined` and those words.
- **Running a command.** `plugins.run {plugin, command}` runs an installed plugin's command as its menu item would. It is on the always-ask list.

## Threads

- **Saved** as `<user folder>/ai/threads/<id>.json`: the title, mode, model, the history, the panel's items, the cost, the cap, what was allowed and the globe (`webSearch`). Images don't stay: a saved thread names where one was.
- **Search results don't stay** (owner Q200): when a turn ends, each `web_search` and `web_fetch` result in the history becomes "The search results aren't kept after the turn." Their rows keep only the query and the pages' URLs. The citations and Google's suggestions are saved with the answer. Anthropic's own search results stay in the history as Anthropic sends them, encrypted, because the answer's citations refer to them.
- **The Project tag.** A thread started in a window with a Project keeps that Project's folder (`project`). The tag stays in the thread file and is never sent to the sidecar or a provider.
- **Instructions** ([ADR 0030](adr/0030-open-a-folder-as-a-project.md)). `session/new`'s `system` is Hikari's prompt, then `<user folder>/AGENTS.md`, then `AGENTS.md` at the root of the thread's Project, each under its own heading. The Project's comes last, so it wins. Each file is read up to 32 KiB, cut at a character boundary. A missing, empty or unreadable file is skipped quietly. It loads in an untrusted Project too. Claude Code and Codex threads get the same `system`.
- **The caller** of a thread's calls is `{id: "ai:<thread>", label: "AI", reviewed: true}` with the Documents the user attached, so each thread's changes land in review turns of their own.
- **Items** the panel shows, by `kind`:

| Kind | Fields |
| --- | --- |
| `user` | `text`, `turn` (the review turn), `history` (where its message starts), `time`, `steered?`, `brief?` (a child's task), `results?` (sub-agents' results) |
| `text` | `text`, `citations?` (numbered from 1 per answer, `start` and `end` in `text` when placed), `searchSuggestions?` (Google's HTML) |
| `tool` | `id`, `tool`, `read`, `input`, `status` (`running`, `done`, `failed`), `output`; `web_search` and `web_fetch` keep `service` and `urls` instead of `output` |
| `search` | `by` (`provider`, or `google` for Gemini), `searches`, placed before the answer that searched |
| `change` | `document`, `title`, `name`, `judged` |
| `plan` | `entries` |
| `ask` | `ask`, `type` (`ask`, `judge`, `always`), `tool`, `operation`, `params`, `reason`, `status` (`open`, `allowed`, `allowedInThread`, `denied`, `withdrawn`) |
| `script` | `ask`, `name`, `macro`, `script`, `lines`, `title`, `needs`, `status` (`open`, `running`, `ran`, `failed`, `declined`, `withdrawn`), `outcome?`, `result?`, `file?` |
| `tasks` | the panel's group of consecutive `task` items (`task`, `child`): `children`, each `{id, task, title, target, status, stoppedFor, cost, costUnknown, document, lines}` |
| `estimate` | `ask`, `children`, `tokens`, `cost?`, `status` (`open`, `started`, `declined`, `withdrawn`) |
| `notice` | `notice` (`lost`, `cap`, `dailyCap`, `failed`, `stopped`, `parent`, `maxTokens`, `refusal`, `maxRequests`, `noModel`, `noSidecar`, `searchUsedUp`, `searchKey`, `compactFailed`, `nothingToCompact`), `detail?`; the search ones have `service` and `status` and show once per turn |
| `compaction` | `before`, `after` (tokens), `summary?`; or `from` (the earlier thread's title) and `summary` in a thread made from a summary |

- **Queue and steer.** A message sent while a turn runs waits; it runs when the turn ends, or joins it with Steer now.
- **A lost sidecar** ends the turn with `lost`. Retry drops the history from that turn's message on, opens the session again and sends the message again.

## Cost caps

- Each thread has a cap ($2.00 by default) and the day has one ($10.00), counted from the usage each model request reports and the thread's judge requests. The day's spending is kept in `<user folder>/ai/threads/spend.json` for 31 days.
- Going over either cap cancels the turn: the call already running finishes, open questions close, and the thread shows `cap` or `dailyCap`. A thread over its cap doesn't start another turn. **Raise cap** adds one default cap to it.
- A model with no price and no reported cost marks the thread's cost as unknown; it never counts toward a cap.

## The panel

The Agent panel is panel 14. With AI off it isn't built, and its commands, Show Agent panel (5252) and New agent thread (5253), are gone. Neither has a default key. It opens as a tab beside the Review panel, or right of the Grid.

- **Header:** the threads list, the thread's title, a Full access badge, the thread menu (Compact conversation, New thread from summary, Rename, Export as Markdown, Delete) and New thread.
- **The threads list** shows the active window's Project threads first, under "This project", and then the others by day.
- **A banner** above the timeline says "This project’s AGENTS.md instructions are on" when the open thread's Project has a non-empty `AGENTS.md`. Its Open button opens the file in the system editor.
- **Without a chat model** it says so and opens Settings › AI. Nothing starts until a provider is added.
- **The timeline** shows the items above. Ask cards offer Allow once, Allow in this thread, Deny and Deny with a message; an always-ask card has a red edge, says it asks in every mode, and offers Allow once and Deny only. A `cap` notice offers Raise and continue; `lost` and `failed` offer Retry the turn.
- **Web search** (AI23b, design gate [#800](https://github.com/altqx/hikari/issues/800#issuecomment-6737834934), Q192–Q200):
  - **The globe** sits after Attach. It is per thread, starts on, and is hidden while Let AI search the web is off. Its hover says "Search the web: on for this thread" or "off for this thread". On Anthropic a second line says that changing it mid-thread costs one cache miss. When the provider can't search and no search service is chosen, the globe is dimmed and its hover points to Settings › AI › Web search.
  - **Citations** are numbered chips after the text they cite. Under the answer, a **Sources** list shows each title as a link with its site; more than five fold behind "Show N more". Hovering a chip or a source shows its title, site, the quoted text when the provider sends one, and Open in browser. Nothing is fetched to draw it. Links open in the browser, and only http and https ones.
  - **Google's search suggestions** show right under a Gemini answer that searched, drawn from Google's snippet as sent ([SearchSuggestions.qml](../src/ui/SearchSuggestions.qml)).
  - **Search rows** say "Searched the web", "Searched with Google" or "Searched with <service>", then the query; a page read says "Read <site>". Expanding a row lists the full URLs.
  - **A used-up plan** or a refused key shows a warning notice naming the service and its HTTP status, with Open Settings › AI. The answer says it didn't search.
- **The composer** shows the active tab, the attached tabs (`@` attaches another open tab), the model and the mode. Enter sends, Shift+Enter starts a new line, Esc stops. `/compact [instruction]` compacts the conversation, once the running turn ends; `/new` starts a thread and `/mode` opens the mode menu. While a turn runs, a message waits in the queue strip with Steer now.
- **Sub-agents.** A group card, "3 sub-agents · 2 running · 1 needs you", with Stop all, and a row per child: title, cost, Open and Stop, then its state, harness ("Hikari · <model>", "Claude Code", "Codex" or the ACP agent's name) and scope. A row is one focusable group, "Sub-agent <title>, <state>"; Enter opens it. A child's question card says "<child> asks you". The opened child is read-only: a banner names the parent, there is no composer, and the footer shows "<mode> · capped by the parent", the scope and Stop. The back arrow or Esc returns to the parent. No new command or hotkey.
- **Full access** warns once with what still asks, unless the user ticks Don’t warn me again.
- **The meter** shows the context tokens and the thread's cost against its cap. At 90% of the window it turns amber with a warning glyph, and while a turn runs with automatic compaction on it adds "compacts at the next tool call".
- **Compaction** shows as a divider, "Context compacted 313K → 21.2K tokens", the same for both stages. A stage 2 divider opens to show the summary the model now sees; everything above it stays. With automatic compaction off, a card offers Compact conversation and New thread from summary at 97%. A failed compaction shows "Couldn’t compact the conversation" with Retry and New thread from summary.
- **New thread from summary** opens a new thread with the same title, mode and model, whose history starts from a summary of the whole thread. Its first item is a divider, "Carried on from the summary of “…”".

Settings › AI › Agent holds the mode for new threads (`ai.agent.mode`, Auto), the thread cap (`ai.agent.threadCapCents`, $2.00), the daily cap (`ai.agent.dailyCapCents`, $10.00) the cost meter (`ai.agent.costMeter`) and Compact long conversations automatically (`ai.agent.autoCompact`, on). 0 means no cap.

## Tests

- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/agent`, `test/rpc.test.ts`): streaming in chunks, tool calls and their results, broken arguments, `update_plan`, steering, cancel during a request and during tool calls, a second prompt while busy, the request limit, stop reasons, a provider failure, replayed history, and the session methods over the link. `test/agent/search.test.ts`: `webSearch` on and off per prompt, the `web` tools leaving with it, `sources` before `usage`, and `search_failed`. `test/providers/search.test.ts`: each wire's searches. `test/agent/tasks.test.ts`: a root thread on a scripted Anthropic provider finds the `agent` tools by group and by words while its tools stay byte for byte the same.
- `hikari_ai_sidecar_bun_tests`, `test/agent/compaction.test.ts`: a scripted model filling a thread past 90%: stage 1 alone mid-turn when it brings it under 60%, stage 2 when it doesn't and under 40% after, the head and the tail byte for byte, the system and tools unchanged; `/compact` with an instruction, refused mid-turn; a second summary folding in the first; one retry, then `compaction_failed` with nothing removed; nothing with `autoCompact` off; `apply: false`.
- `hikari_backends_ai_agent_tests`, against the fake sidecar's scripted sessions and the real API over a fake host:
  - each mode's decisions, the always-ask list, Allow in this thread, a denial with the user's message;
  - the judge's request holds the user's messages and the action but no tool output, and its cost counts;
  - writes without a thread, from an unknown thread or after a stop are refused;
  - Stop withdraws an open question; the thread cap and the daily cap stop a turn; Raise cap;
  - a crash mid-turn, then Retry on the earlier history;
  - Steer now and a queued message;
  - a thread saved, reopened and replayed into a new session, then deleted;
  - `AGENTS.md`: the user's before the Project's after Hikari's prompt, empty ones dropped, the 32 KiB cut on a character boundary, and each thread reading its own Project's;
  - a thread in a Project with a key for its provider opening, prompting and ending under the alias, a thread outside it under the user's id, and no key sent;
  - compaction: the stubs and the summary applied to the saved history with user items following, `/compact` held until the turn ends with its instruction and the queued message after it, the summary's cost, a failed compaction and New thread from summary, an automatic failure, and the window and setting sent to the session;
  - `webSearch` following Settings and the globe, which is saved; an answer's citations placed on its text and its searches in a row before it; service rows keeping only their pages, the finished turn dropping the results, and one used-up notice per turn.
- `hikari_backends_ai_agent_tests`, `AgentTasks*`: Qt's refusals (depth, a fifth child, a broader mode, the caps, an overlap, a scope's Document) whatever the sidecar asks; a child writing only its Lines; two children on other providers editing disjoint Lines into their own change sets under the parent's turn, each with an exact Undo; the estimate declined; a child's question answered by the user and its judge holding the brief but no tool output; Stop stopping every child with its edits kept; children's cost in the parent's meter and cap; a crash interrupting children; a saved running child loading interrupted.
- `hikari_ui_agent_panel_tests`, the panel over the whole app and the fake sidecar: a turn's text and change in the timeline and the review, the threads list, the Full access warning, an always-ask card in Full access, the cap with Raise and continue, a queued message and Esc, the panel without a chat model, and AI off; the globe per thread and its hover, citation chips, Sources with Show 1 more and a link opened, the search rows, the used-up notice, only web links opening, and the globe hidden with Let AI search the web off; the Project's threads listed first, and the `AGENTS.md` banner with Open.
- `hikari_ui_cited_answer_tests`: chips after their spans in order, escaped text, spans off the text or inside a surrogate pair dropped, and the site of a URL.
