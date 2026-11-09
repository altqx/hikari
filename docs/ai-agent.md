# AI agent

The agent works through the user's subtitles in threads, one conversation each ([ADR 0024](adr/0024-hikari-ai.md), [ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md), [AI7 #435](https://github.com/altqx/hikari/issues/435)). The sidecar runs the loop: model requests, tool calls, the plan and steering. Qt owns everything else: the threads and their history, the permission modes, the judge, the cost caps, the review and the UI.

AI7 ships in parts:

- **AI7a**: the loop and the thread engine, with no UI.
- **AI7b**: the Agent panel (panel 14), its two commands (5252, 5253) and the Settings section.
- **AI7c**: Manual-mode proposals.

Context compaction (AI21) and sub-agents (AI22) are separate cards.

## Where it lives

| Path | Holds |
| --- | --- |
| `src/ai-sidecar/src/agent/session.ts` | `AgentSession`: one thread's turns, its tools, `update_plan`, steering and cancel. `AgentSessions` holds them by id. |
| `src/ai-sidecar/src/protocol.ts` | The session methods and `session/update` shapes. |
| `src/backends/ai_agent.{h,cpp}` | The policy functions, `AgentThread`, `ThreadStore` and `AgentThreads`, which runs the threads and decides every call. |
| `src/backends/ai_sidecar_host.{h,cpp}` | The session gate: a call that names a session waits for Qt's decision. |
| `src/app/agent_controller.{h,cpp}` | `AgentController`: builds `AgentThreads` while AI is on and gives the panel its threads, the open thread and its items. |
| `src/ui/AgentPanel.qml` | The Agent panel. |

## The protocol

The messages follow the [Agent Client Protocol](https://agentclientprotocol.com) where it has one.

| Method | Direction | Params | Result |
| --- | --- | --- | --- |
| `session/new` | Qt → sidecar | `{sessionId, model: {providerId, model}, system?, history?, cacheKey?, caching?, maxOutputTokens?}` | `{sessionId}` |
| `session/prompt` | Qt → sidecar | `{sessionId, prompt: [Part], model?, caching?}` | `{stopReason}`: `end_turn`, `max_tokens`, `max_turn_requests`, `refusal` or `cancelled` |
| `session/cancel` | Qt → sidecar | `{sessionId}` (a notification) | none |
| `_hikari/session.steer` | Qt → sidecar | `{sessionId, prompt: [Part]}` | `{taken}` |
| `_hikari/session.close` | Qt → sidecar | `{sessionId}` | `null` |
| `session/update` | sidecar → Qt | `{sessionId, update}` | none |

- Qt names the session: the thread's id. `session/new` again replaces the session, which is how a thread reopens after a restart or a crash, with its saved `history`.
- A turn makes at most 64 model requests. Text comes in `agent_message_chunk` updates, at most one every 16 ms.
- `update` is one of `agent_message_chunk {content}`, `tool_call {toolCallId, title, kind, status, rawInput}`, `tool_call_update {toolCallId, status, content}`, `plan {entries}`, `usage {usage}` and `history {message}`. `history` carries each message the sidecar keeps, in the order kept, so Qt's copy stays whole.
- The tools are the [Hikari AI tools](ai-tools.md), plus `update_plan`, plus the `web` group while web search has a search service (AI23). Every call the session makes goes out as `_hikari/api.call` with `session` set.
- **Cancel.** A cancelled model request ends at once. During tool calls, the running call finishes and each one after it gets the result "The user stopped the turn.", so the history stays valid.
- **Steer.** A steered message joins the turn after its next tool call, appended to the tool results. `{taken: false}` means the turn ended first. Qt then sends the message as the next turn.

## Modes

Each thread has a mode. Qt decides every write a session sends, so a sidecar that asks for more gets no more.

| Mode | Writes |
| --- | --- |
| Manual | `document.apply` runs, into the review. Other writes ask: Allow once, Allow in this thread, or Deny. |
| Auto | The judge decides. When it doesn't approve, the user is asked with the judge's reason. |
| Full access | Everything runs, except the always-ask list. |

- Reads never ask. Neither do `selection.set`, `panels.open`, `panels.close` and `panels.focus`, which change no content and no setting.
- **Allow in this thread** covers the operation, or for `commands.run` the one command, until the thread is deleted.
- **The always-ask list** ([#410](https://github.com/altqx/hikari/issues/410)) asks in every mode, with Allow once and Deny only. `alwaysAsk` holds it:

| Category | Asks for | Doesn't ask, and why |
| --- | --- | --- |
| Deleting or overwriting files outside the subtitles' folders and the user folder | `files.write` to a file outside them (checked by the granted file's path); `commands.run` `GLOBAL_DELETE_TEMPORARY_FILES`; `settings.set` `fontCollector.directory` to a folder outside them | The Save commands write only the subtitle's own file and open a file dialog for any other path. Save as, Export, Save timecodes and Save session open a file dialog, so the user picks the path. Video snapshots make new files and never overwrite. `VIDEO_DELETE_FILE` only closes the video. |
| Running external programs | `commands.run` `GLOBAL_AUTOMATION_LOAD_SCRIPT`, `GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT`, `GLOBAL_AUTOMATION_REPEAT_LAST_RUN`, `GLOBAL_AUTOMATION_RELOAD_AUTOLOAD` and any `plugin.` command; `settings.set` `automation.scriptEditor`, `automation.lastScript`, `automation.disabledAutoload`, `updater.autoInstall` | `commands.run` reaches only built-in commands today; plugin commands ask in case that changes. |
| Installing a plugin or granting full trust | `settings.set` `plugins.autoUpdate`, `plugins.network`, `automation.trustedFolders` | The Plugins window (`GLOBAL_PLUGINS`) installs and trusts only when the user clicks. |
| Sending local files to the network | `settings.set` of any `privacy.` setting (crash reports, usage data, Discord) | Crash and issue reports send only from their own dialogs. |
| AI settings | `settings.set` of any `ai.` setting | |

  The subtitles' folders are the folders of the open tabs' files. There is no project folder yet.

  `hikari_backends_ai_agent_tests` fails when a new write operation appears, or a new Automation or delete command, or a new setting under `automation.`, `plugins.`, `privacy.`, `updater.`, `fontCollector.` or `fonts.`, until it is classified here.
- **A denial** goes to the model as a `refused` error that says the user denied it, with the user's message if they wrote one, and tells it not to try again.
- **The judge** is a `_hikari/complete` request on the Judge role. It sees the action and the user's own messages, never a file's content or a tool's output, so nothing the agent read can talk it into approving. Anything but `{"decision": "approve"}` asks the user. Its cost counts toward the thread's.
- A write with no session, other than from an MCP connection, is refused once any thread runs. So is a write from a thread whose turn has stopped.

## Threads

- **Saved** as `<user folder>/ai/threads/<id>.json`: the title, mode, model, the history, the panel's items, the cost, the cap and what was allowed. Images don't stay: a saved thread names where one was.
- **The caller** of a thread's calls is `{id: "ai:<thread>", label: "AI", reviewed: true}` with the Documents the user attached, so each thread's changes land in review turns of their own.
- **Items** the panel shows, by `kind`:

| Kind | Fields |
| --- | --- |
| `user` | `text`, `turn` (the review turn), `history` (where its message starts), `time`, `steered?` |
| `text` | `text` |
| `tool` | `id`, `tool`, `read`, `input`, `status` (`running`, `done`, `failed`), `output` |
| `change` | `document`, `title`, `name`, `judged` |
| `plan` | `entries` |
| `ask` | `ask`, `type` (`ask`, `judge`, `always`), `tool`, `operation`, `params`, `reason`, `status` (`open`, `allowed`, `allowedInThread`, `denied`, `withdrawn`) |
| `notice` | `notice` (`lost`, `cap`, `dailyCap`, `failed`, `stopped`, `maxTokens`, `refusal`, `maxRequests`, `noModel`, `noSidecar`), `detail?` |

- **Queue and steer.** A message sent while a turn runs waits; it runs when the turn ends, or joins it with Steer now.
- **A lost sidecar** ends the turn with `lost`. Retry drops the history from that turn's message on, opens the session again and sends the message again.

## Cost caps

- Each thread has a cap ($2.00 by default) and the day has one ($10.00), counted from the usage each model request reports and the thread's judge requests. The day's spending is kept in `<user folder>/ai/threads/spend.json` for 31 days.
- Going over either cap cancels the turn: the call already running finishes, open questions close, and the thread shows `cap` or `dailyCap`. A thread over its cap doesn't start another turn. **Raise cap** adds one default cap to it.
- A model with no price and no reported cost marks the thread's cost as unknown; it never counts toward a cap.

## The panel

The Agent panel is panel 14. With AI off it isn't built, and its commands, Show Agent panel (5252) and New agent thread (5253), are gone. Neither has a default key. It opens as a tab beside the Review panel, or right of the Grid.

- **Header:** the threads list, the thread's title, a Full access badge, the thread menu (Rename, Export as Markdown, Delete) and New thread.
- **Without a chat model** it says so and opens Settings › AI. Nothing starts until a provider is added.
- **The timeline** shows the items above. Ask cards offer Allow once, Allow in this thread, Deny and Deny with a message; an always-ask card has a red edge, says it asks in every mode, and offers Allow once and Deny only. A `cap` notice offers Raise and continue; `lost` and `failed` offer Retry the turn.
- **The composer** shows the active tab, the attached tabs (`@` attaches another open tab), the model and the mode. Enter sends, Shift+Enter starts a new line, Esc stops. `/new` starts a thread and `/mode` opens the mode menu. While a turn runs, a message waits in the queue strip with Steer now.
- **Full access** warns once with what still asks, unless the user ticks Don't warn me again.
- **The meter** shows the context tokens and the thread's cost against its cap.

Settings › AI › Agent holds the mode for new threads (`ai.agent.mode`, Auto), the thread cap (`ai.agent.threadCapCents`, $2.00), the daily cap (`ai.agent.dailyCapCents`, $10.00) and the cost meter (`ai.agent.costMeter`). 0 means no cap.

## Tests

- `hikari_ai_sidecar_bun_tests` (`src/ai-sidecar/test/agent`, `test/rpc.test.ts`): streaming in chunks, tool calls and their results, broken arguments, `update_plan`, steering, cancel during a request and during tool calls, a second prompt while busy, the request limit, stop reasons, a provider failure, replayed history, and the session methods over the link.
- `hikari_backends_ai_agent_tests`, against the fake sidecar's scripted sessions and the real API over a fake host:
  - each mode's decisions, the always-ask list, Allow in this thread, a denial with the user's message;
  - the judge's request holds the user's messages and the action but no tool output, and its cost counts;
  - writes without a thread, from an unknown thread or after a stop are refused;
  - Stop withdraws an open question; the thread cap and the daily cap stop a turn; Raise cap;
  - a crash mid-turn, then Retry on the earlier history;
  - Steer now and a queued message;
  - a thread saved, reopened and replayed into a new session, then deleted.
- `hikari_ui_agent_panel_tests`, the panel over the whole app and the fake sidecar: a turn's text and change in the timeline and the review, the threads list, the Full access warning, an always-ask card in Full access, the cap with Raise and continue, a queued message and Esc, the panel without a chat model, and AI off.
