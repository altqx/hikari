# Agent panel in Quiet

How the Agent panel looks in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). Its threads, modes, cards, sub-agents, compaction and composer are [ai-agent.md](../ai-agent.md#the-panel)'s; this document says how they look. While an agent turn runs in the current tab, Discord shows With the AI agent, off by default ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Batch 4 lgtm", Q756–Q760; see [Decisions](#decisions)). The app doesn't change until the code card lands.

## Where it opens

Q756. The left side bar, full height, 420 wide, as today's build; the right side bar keeps Find, QC notes and Review.

## The header

The threads list button, the thread's title in semibold, the mode as a small badge ("Auto", `line` edge, `muted` text), the thread ⋯ and New thread.

## The timeline

- **The user's messages** sit at the trailing side on `raised`, radius 8, at most 86 % wide. **Answers** are plain text at full width.
- **Tool calls** (Q757). A run of reads folds into one `muted` line with a chevron: "Read 4 things · lines 44–49, keyframes · 1.2 s"; opening it lists each call in small monospace. An edit is a change row with a 1-unit `line` boundary, radius 6: the `review-panel` glyph, "Edited 3 lines" in semibold and what changed in `muted`, then **Undo** (quiet) and **Keep**.
- **Ask cards** (Q758). `panel`, radius 6, a 1-unit `line` boundary and a 3-unit `warning` edge on the leading side: the question in semibold, the reason in `muted`, the call in a monospace field, then Allow once (primary), Allow in this thread, Deny and Deny with a message… (quiet), wrapping as needed. An always-ask card has a `danger` boundary and edge, says it asks in every mode, offers Allow once and Deny, and Deny is the default.
- **A failed turn** is one line on `danger` at 12 %: the `notice-warning` glyph, the reason ("The turn stopped: the provider refused the key."), **Retry the turn** and **Settings › AI…** (quiet).

## The composer

From the top: the active tab and attached tabs as chips with Attach (`@`); the message box (`field`, radius 6, "Ask, or type / for commands"); [the meter](#the-meter); then the model and mode buttons (quiet) and, at the trailing end, Send.

## The meter

Q759. One line over the composer's buttons: a 64 × 4 bar in `accent` and "38K of 200K · $0.42 of $2.00" in small `muted` text. From 90 % of the window the bar and text are `warning`, the `notice-warning` glyph leads, and while a turn runs with automatic compaction on it adds "compacts at the next tool call".

## Running

Q760. While a turn runs:

- a 2-unit `accent` progress line under the header;
- in the timeline, an `accent` dot and "Working · 3 tool calls so far · 14 s" in `muted`;
- Send becomes Stop (Esc);
- a queued message shows under the message box, "Waiting: “Also check line 49.”" in `muted`, with **Steer now** (quiet).

## Panel states

With no provider and no agent, the panel's empty state keeps today's words: the `ai-assistant` glyph, "Add a provider to use the agent", the paragraph on where the agent runs, **Open Settings › AI** (primary) and "Nothing is sent until you add one." Turning AI on already downloads the AI component, so the line no longer says nothing downloads.

**With AI off** (owner Q801 B, [#1330](https://github.com/altqx/hikari/issues/1330)), the panel stays in View › Panels and shows only one card: the `ai-assistant` glyph, "AI is off", "Turn it on to use agents and AI actions. HikariSub then downloads the AI component once." in `muted`, and **Turn on AI** (primary), which turns AI on at once. Edit › Turn on AI… sits where Edit › Review ▸ is while AI is on, and shows this panel.

**While the AI component downloads**, or after its download failed, a strip under the header says "Getting the AI component: %1 of %2 MB", or "The AI component didn’t download. Check your connection and try again." with **Retry**.

## Discord

Presence shows **With the AI agent** (`agent`), off by default. Its art is the `ai-assistant` glyph on #F6B8F0 ([Art](discord-presence.md#art)).

## Decisions

The owner said "Batch 4 lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6943248299)):

| Q | Decision |
| --- | --- |
| Q756 | The Agent panel opens in the left side bar at full height |
| Q757 | A run of reads folds into one line that opens; edits are change rows with Undo and Keep |
| Q758 | Ask cards have a warning edge; always-ask cards a danger edge with Deny the default |
| Q759 | The meter is a small bar with tokens and cost over the composer's buttons, warning from 90 % |
| Q760 | A running turn shows a progress line, "Working · …" in the timeline, Stop in place of Send, and the queued message with Steer now |
