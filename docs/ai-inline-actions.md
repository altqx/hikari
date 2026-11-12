# AI inline actions

One-shot AI actions on the Lines you chose, with no agent thread ([AI8 #436](https://github.com/altqx/hikari/issues/436)). The owner approved the mockups and all 13 recommendations (Q296–Q308) on [2026-11-11](https://github.com/altqx/hikari/issues/436#issuecomment-6787645975).

## Where it lives

| Path | Holds |
| --- | --- |
| `src/application/ai_inline.{h,cpp}` | Pure rules: which Lines are skipped, tag masking, the prompt and reading the answer. |
| `src/backends/ai_inline_run.{h,cpp}` | `InlineRun`: one `_hikari/complete` request per Line, at most 4 at once, with Stop, the daily cap and the cost. Translate uses `TranslationRun` instead ([AI translation pipeline](ai-translation.md)). |
| `src/app/inline_ai_controller.{h,cpp}` | `InlineAiController`: picks the Lines, starts runs, writes them into the review, and holds the actions and the Manual results. |
| `src/ui/InlineAiMenu.qml`, `InlineAiBox.qml`, `InlineAiBar.qml` | The **AI** submenu, the box that asks for an instruction or a language, and the bar over the Grid with progress, notices and the Manual results. |

## Where it shows

- **AI** in the Grid's menu, after Paste special, for the selected Lines.
- **AI** in the Line editor's text menu and the AI button at the end of its tag strip, for the active Line. With text selected in the field, only that span changes and the rest of the Line goes as context.
- **AI actions…** (Global 5283) opens the submenu at the active row, or at the Line editor's AI button when its text has focus. **Repeat last AI action** (Global 5284) runs the last action again on the current Lines. Neither has a default key. Both are AI commands, so they hide with AI off.
- An action is on only when a provider model serves its role (`InlineAiController::ready`); Claude Code or Codex on `PATH` doesn't count, as inline actions don't run agents. With no model at all the submenu's last entry is **Add a model in Settings › AI…**.

## Actions

| Action | Role | Asks |
| --- | --- | --- |
| Rewrite… | Chat | An instruction, with the last 5 as chips |
| Shorten to 15 CPS | Chat | Nothing; sends only the Lines over 15 CPS |
| Fix spelling and grammar | Reviewer | Nothing |
| Translate to <language> | Translator | Nothing; the language is **Translate to** in Settings |
| Translate to… | Translator | A language |
| Your actions | Their own role | Nothing, or their instruction when **Ask each time** is on |

Each role falls back to Chat. Every action changes only the Text, except Translate in Translation mode, which writes the Translated text. Both Translate actions run the [translation pipeline](ai-translation.md) as one request, not one request per Line. 15 CPS is the limit the Grid and the Line editor already use; there is no setting for it.

## What is sent

Each Line is one request with HikariSub's instructions, the action's instruction, and the Line: its text with each override block replaced by a numbered marker `⟦n⟧`, its start and end times, its style and actor names, and the text of up to **Lines of context** Lines before and after it, with their tags stripped. Nothing else from the file is sent. Comment, empty, tags-only, drawing (`\p`) and karaoke (`\k`) Lines are skipped. Inline actions use no tools and no web search. The privacy page carries the approved wording.

An answer must give back every marker exactly once and in order, or the Line is left as it is with "the answer dropped a tag". An answer equal to the Line isn't a change.

## Modes and the review

Runs follow the mode for new threads (`ai.agent.mode`):

- **Auto and Full access.** The run is written once, when it ends, as one `document.apply` from the caller `{id: "ai:inline", label: "AI", reviewed: true}`: one review turn and one History step, "AI: Rewrite (10 lines)". There is no judge.
- **Manual.** The results wait in a list over the Grid with a tick per Line. **Apply N** writes the ticked ones the same way; **Discard** writes nothing. A Line you edit after it was sent shows "out of date" and can't be applied.

Inline runs don't show in the Agent panel.

## Running

One run at a time. The bar over the Grid shows the progress, the cost so far and **Stop** (Esc). Stop writes the Lines that finished and drops the rest. Editing a Line drops it from the run. A run over 50 Lines asks first and shows what is left of the daily cap. Runs count toward the daily cap but not the thread cap; reaching the daily cap stops the run as Stop does. Failures show under the run with **Retry**, which runs the failed Lines again.

## Settings

The Inline actions section of [Settings › AI](ai-settings.md):

| Setting | Default | Saved |
| --- | --- | --- |
| `ai.inline.actions`: your actions, each `{name, instruction, role, ask}` | none | At once |
| `ai.inline.translateTo` | English | On OK or Apply |
| `ai.inline.contextLines` (0–5) | 2 | On OK or Apply |
| `ai.inline.recentInstructions` | none | After each Rewrite |

## Usage data

No new event; the two commands send `command_used` as every command does.

## Tests

- `hikari_application_ai_inline_tests`: skip rules, masking and unmasking, the prompt, spans, reading answers.
- `hikari_backends_ai_inline_run_tests`: one request per Line, dropped tags, failures, Stop, the daily cap, dropping a Line, no model, unknown prices, the request shape.
- `hikari_ui_inline_ai_tests`: the menus with AI on and off, and with only an agent CLI found, Auto's one turn and one History step, Manual's Apply with an out-of-date Line, Discard.
