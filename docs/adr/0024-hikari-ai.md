---
status: accepted
---

# Add Hikari AI on the Hikari API, with the user's own providers

On 2026-10-28 altqx settled Hikari AI in the wave-8 grilling session ([#410](https://github.com/altqx/hikari/issues/410), card [AI0 #428](https://github.com/altqx/hikari/issues/428)). Hikari AI is an agent that reads and edits subtitles through HikariSub's own tools, in an in-app panel or from an external agent. The prior art, the providers and the options are in the [AI integration research note](../research/hikari-ai-integration.md); where it differs from this record, this record wins.

## Boundary

- **Tools.** The AI's tool surface is built on the Hikari API ([HA1 #411](https://github.com/altqx/hikari/issues/411)) in the application layer. Plugins, the MCP server and node-editor nodes use the same API.
- **Providers** are backend adapters behind the application boundary.
- **No AI in core.** The core knows nothing of models, providers or prompts.

## Providers and sign-in

- **Bring your own provider only.** There is no Hikari server and no credits.
- **Off and silent** until the user adds a provider: nothing is sent before then.
- **ChatGPT** through OpenAI's documented "Sign in with ChatGPT" flow for open-source apps.
- **Anthropic** through API keys. Claude subscriptions come through the ACP Claude Agent; we ask Anthropic before advertising that path.
- **The Claude Agent SDK isn't embedded.**
- **OpenRouter** through PKCE, **OpenAI and Anthropic** through keys, and **custom OpenAI-compatible endpoints**.
- **Keys** live in the OS keychain through QtKeychain, never in settings files or logs ([AI5 #433](https://github.com/altqx/hikari/issues/433)).
- **Per-role models:** chat, translator, reviewer, vision and judge.

## Order

The tool surface and an MCP server come first. Then the Keep/Undo review, the in-app agent and an ACP client. The AI starts after HA1 and gains tools as each feature lands.

## The agent panel

- The panel belongs to the window, not to a tab, and sees every open tab.
- Tools take a target Document: the active tab by default, and `@` attaches others.
- Edits to a background tab stage in that tab's review, which shows a badge.
- Threads are saved in the user folder.

## Tools

About 20 core tools, plus searchable groups: `typeset.*`, `translate.*`, `karaoke.*`, `automation.*`, `effects.*`, `plugins.*` and `run_command`.

## Permission modes

Each thread has a mode. Settings holds the default for new threads, and that default is Auto.

1. **Manual.** The user approves every write action. Read-only tools run without prompts. An MCP tool counts as read-only only when it carries `readOnlyHint` and comes from a server the user trusts.
2. **Auto (the default).** A judge role approves actions. The judge sees the action and the user's messages, never file content or tool output. It approves the action, or passes it to the user with a reason.
3. **Full access.** No prompts, after a one-time warning, and the thread carries a red badge.

### Always prompt, in every mode

- deleting or overwriting files outside the subtitle's folder and the user folder;
- running external programs;
- installing a plugin or granting one full trust;
- sending local files to the network;
- changing AI permissions, keys or budgets;
- MCP tools marked `destructiveHint`.

### Subtitle edits

Every mode shows each edit as a diff. Manual waits for Keep, Auto applies once the judge approves, and Full access applies at once. Every change is one undo step, and the review stays open so it can be undone.

### External agents

Agents connected over MCP or ACP edit only through Hikari's tools, into the same review, under the same three modes.

## AI-written Lua

A script the AI writes is shown and approved on every run, and runs as an ordinary Automation macro ([ADR 0006](0006-isolated-lua-automation-host.md)). It is saved to a scratch folder and never added to Autoload automatically.

## Typesetting

Tools get cropped frames by default. The model proposes, and the deterministic solvers (VT3's perspective, Hydra, clips) place. A render-and-compare check runs before the result reaches the review.

## Translation

- The pipeline follows Honya: a pre-pass, the translator, checks, the reviewer and a coherence pass.
- TM1's tag rules apply, and Lines that still need review stay Unconfirmed.
- The glossary, the characters and the style live in a project folder in Honya's format, shared with Honya. Script Info points to it.

## Karaoke

No aligner is bundled. The tools use KA5's onsets. WhisperX and MFA are optional external programs the user installs.

## Cost

A soft cap per thread, a daily cap, a cost meter, and an estimate before every bulk run.

## Around the AI

- **Analytics** ([AN1 #404](https://github.com/altqx/hikari/issues/404)) counts AI use by feature and tool id only, never prompts, output, models or providers.
- **Design gates:** the review ([AI2 #430](https://github.com/altqx/hikari/issues/430)), the agent panel ([AI7 #435](https://github.com/altqx/hikari/issues/435)) and the inline actions ([AI8 #436](https://github.com/altqx/hikari/issues/436)) need owner-approved light and dark mockups before production.
- **The site** gets an AI guide with a "What is sent" section.
