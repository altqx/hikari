# Settings › AI

The AI tab of Settings ([AI20 #649](https://github.com/altqx/hikari/issues/649)). The owner approved its [mockup](https://github.com/altqx/hikari/issues/649#issuecomment-6612977714) and all 11 recommendations on 2026-11-04. The tab sits between Shortcuts and Updates and privacy, with the `ai-assistant` icon. It stays in the list while AI is off, because it is the way back.

## What it holds

| Section | What | Saved |
| --- | --- | --- |
| AI features | **Turn off all AI features** (`ai.off`, off by default) | On OK or Apply, like every other setting |
| Providers | Add, edit, test and delete providers | At once (see below) |
| Models | Chat, Translator, Reviewer, Vision and Judge. Every role but Chat can be "Same as chat"; Vision lists only models that read images. Refresh models fetches every provider's catalogue again | On OK or Apply |
| Web search | **Let AI search the web** (`ai.webSearch`, on by default), how each provider searches, and **Search service** (`ai.searchService`: None, Brave Search, Tavily, Exa or Firecrawl) with its key | The switch and the service on OK or Apply; the key once its test passes, or on Add anyway |
| External agents | Claude Code and Codex: each found on `PATH` with its version, a badge and Update now (AI24b), and for Codex HikariSub's own ChatGPT sign-in (AI6) | At once (see below) |
| AI components | The AI component's version, its Bun and provider libraries, the update status with Check now and Update now, What's new, the rollback notice, and **Update AI components automatically** (`ai.autoUpdate`, on by default) (AI24b) | The switch on OK or Apply; the buttons at once |
| Privacy | **Use prompt caching** (`ai.promptCaching`, on by default), the What is sent to providers link, and a line that says whether the AI component runs in its sandbox here; "isn’t sandboxed on this system" shows in the warning colour where the sandbox can’t apply ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)) | On OK or Apply |

The owner approved the Web search [mockup](https://github.com/altqx/hikari/issues/742#issuecomment-6686712813) on 2026-11-06, with the switch on by default ([AI23 #742](https://github.com/altqx/hikari/issues/742)).

The Agent and MCP sections arrive with AI7 and AI3/AI13, below Models and above Privacy, as drawn in the mockup. AI15 adds ACP agents to External agents, with Choose… and their run errors. The owner approved AI24b's [design gate](https://github.com/altqx/hikari/issues/744#issuecomment-6731988977) on 2026-11-09.

## Turn off all AI features

Checking the box folds the rest of the page away at once and lists what is kept (providers, keys, threads, the downloaded AI component) and what is hidden. OK or Apply then turns AI off without a restart: `AiFeatures` destroys its objects and stops the sidecar ([architecture](architecture.md#ai)). Unchecking it and pressing OK or Apply brings everything back. While the box is unchecked but not yet applied, the page says to press OK or Apply.

With AI off, `AiSettings` lists no providers and refuses every action, and nothing reads the keychain, downloads or starts.

## Providers

- **Add provider** opens a menu: OpenAI, Anthropic, Google Gemini, OpenRouter and Custom endpoint…. The form opens below the list, not in a second window: Name, the key (masked, with Show key), Test connection, Cancel and Add.
- **Google Gemini** asks for a "Gemini API key", links to [Google AI Studio](https://aistudio.google.com/apikey) and says Vertex AI and Google Cloud sign-in aren't supported.
- **Custom endpoint** adds Base URL and API (Chat Completions or Responses). Its key is optional: a local server such as Ollama or LM Studio needs none, and a key is sent only over HTTPS, to this computer or to a Tailscale peer ([key rules](ai-providers.md#keys)).
- **Changes apply at once,** outside OK, Cancel and Apply, because the key goes straight to the system keychain. A provider exists only once its key is stored. A stored key is never shown again: Edit leaves the key field empty, which keeps the key, and a typed key replaces it.
- **Each row** shows the wire and host, the last test ("Connected · N models", the error, or "Not tested · N models"), where the key is ("Key in keychain" or "No key"), and Test connection, Edit and Delete. A rejected key says "Replace the key with Edit". Delete asks first and removes the key from the keychain; roles that used the provider go back to the chat model.
- **OpenRouter can sign in** ([AI6 #434](https://github.com/altqx/hikari/issues/434)): "Sign in with OpenRouter…" sits next to the key field, "or paste a key". It opens openrouter.ai in the browser, which makes a key for HikariSub; the key goes to the keychain like a pasted one, and the provider is added and tested at once. The row then says "Signed in (OpenRouter)", and a rejected key says "Sign in again with Edit". Edit offers Sign out, which deletes the key from this computer and leaves the provider without one; the key stays in the user's OpenRouter keys until they delete it there. Pasting a key over a signed-in one makes it a pasted key.
- **Test connection** in the form lists the provider's catalogue with the typed key through `AiRequests::probe`, and saves nothing. Add then stores the provider with that catalogue. A provider added without a test fetches its catalogue right after. The first catalogue fills the chat role while it is empty.

## Web search

- **The switch.** **Let AI search the web** is on at first. With it off, `AiRequests` drops `webSearch` from every request and tells the sidecar no search service ([AI providers](ai-providers.md#web-search)). Nothing searches while AI is off.
- **How each provider searches.** With the switch on and a provider added, a list names each provider with "Its own search, and reads pages" (Anthropic, OpenRouter, OpenAI on Responses), "Google Search, and reads pages" (Gemini), the chosen service, or "Needs a search service" in the warning colour. A note says Gemini answers that searched show Google's search suggestions, as Google's terms require.
- **Search service** is for providers without their own search: None, Brave Search (search only), Tavily, Exa or Firecrawl. Brave can't read a page, which the menu and the note under it say. Qt never fetches a page for the agent: the relay reaches only the chosen service's host.
- **The key** row opens under the menu once a service is chosen: the key (masked, with Show key), Test connection and a link to the service's key page. Test connection runs one search through `_hikari/search.test` with the typed key, which counts against the plan. The relay serves the typed key from Qt for that test only; the sidecar never holds it. The key goes to the keychain, as `ai-provider/search:<service>`, only when the test passes. When it fails, the row shows why and an **Add anyway** button, which saves the key regardless and keeps the failure on show. Add anyway hides again if the typed key changes. The test reaches the service before OK or Apply chooses it. Provider keys still save at once ([owner Q121](https://github.com/altqx/hikari/issues/742), 2026-11-07).
- **Stored key.** The row then shows the last test ("Connected", "Not tested", "Key rejected (401)" with Replace key…, or "Searches used up (429)", also 402, 432 or 433), Test connection and Remove key, which asks first. Without a provider, the test says to add one.
- **What the sidecar gets.** The service reaches the sidecar only while the switch is on and its key is in the keychain, so a provider without its own search is never offered a search that can't run.
- **Privacy.** Once a service is chosen, the Privacy line says "Requests go only to the providers you add and to <service>."

## External agents: Claude Code and Codex

Each row names the program and the path `PATH` gives, with a badge ([#744](https://github.com/altqx/hikari/issues/744)). Opening the page runs `claude --version` and `codex --version`, and asks each one's source for the latest version.

- **The badges:** Up to date, Update available, Unsupported version (older than the manifest's minimum) and Known broken version (a version the manifest lists as broken). Up to date and Update available need the latest version, so neither shows while it is unknown. The line below says the version, the latest and how it was installed: "2.1.4 · 2.2.0 is out · installed with Claude's installer".
- **Where the latest comes from:** npm's registry for npm, pnpm, Bun, Yarn, Volta, Codex's installer and anything unknown; Homebrew's formula or cask API for Homebrew; Claude's release channel for Claude's installer, `latest` or `stable` as `autoUpdatesChannel` in `~/.claude/settings.json` says. Each source is asked at most once a day, only while the page is open, and the answers wait in `<user folder>/ai/agent-versions.json`. The minimum and broken versions come from the signed AI manifest's `agents` member, which `src/ai-sidecar/channel.json` fills.
- **How it was installed** comes from the path and where its links lead, as [docs/research/cli-updates.md](research/cli-updates.md) found: Claude's installer (`~/.local/bin/claude`, `~/.local/share/claude/`), Codex's installer (`<CODEX_HOME>/packages/standalone/`), npm (`<prefix>/lib/node_modules/<package>/`), pnpm, Bun, Yarn, Volta, Homebrew (`<prefix>/Cellar` or `Caskroom`, with `<prefix>/bin/brew`), WinGet, Chocolatey and mise.
- **Update now** shows for a badge other than Up to date, and runs only the official command for that method: `claude update`, `codex update` (with `CODEX_HOME` set to the home that owns it), `npm install -g --prefix <prefix> <package>@latest`, `pnpm add -g`, `bun i -g`, `yarn global add`, `volta install` or `brew upgrade [--cask]`. Qt runs it on the click, outside the AI sandbox, with nothing from the sidecar. It waits while a session of that agent runs ("Update now waits until it ends"), and gives up after 30 minutes. Running sessions keep the old version.
- **The check afterwards:** HikariSub runs `--version` again. An unchanged version says "Update didn't take" with the command to run in a terminal, because package managers can exit 0 without updating.
- **A command to copy** replaces Update now for WinGet (`winget upgrade Anthropic.ClaudeCode`), Chocolatey (`choco upgrade claude-code`), and on Windows for npm, pnpm and Yarn, whose `.cmd` shims HikariSub doesn't start. mise and unknown installs say to update it as it was installed.

### Codex's ChatGPT sign-in

- **The row** also says whether HikariSub is signed in to ChatGPT for Codex. The sign-in doesn't need the CLI.
- **Sign in with ChatGPT…** shows a device code under the row: the code, Copy, "Open auth.openai.com", and a countdown from the code's 15 minutes. Nothing opens on its own. "Can't use a code? Sign in through the browser" opens auth.openai.com and listens on localhost:1455 until the page says the user can close it. While another app holds port 1455, the panel says so in place of that link.
- **A workspace that turns off codes** (the code request answers 404) shows "Your ChatGPT workspace turns off sign-in codes. Sign in through the browser instead." with the browser as the main action. The other errors: a refusal, an expired code ("Get a new code"), an unreachable host, and a timeout.
- **Signed in** shows "Signed in as <email> · Sign out". Sign out deletes HikariSub's sign-in; Codex in the terminal stays signed in.
- **Import from ~/.codex (read-only)** shows while `~/.codex/auth.json` exists and HikariSub isn't signed in. It asks first, because the two then share one sign-in and whichever refreshes it signs the other out. It copies the tokens and never writes the file.
- **An expired session** is refreshed when the page opens. When ChatGPT refuses the refresh, the row says "Sign-in expired" with "Sign in again…".
- **Where the session lives:** one keychain entry, `codex-chatgpt` (`ai-provider/codex-chatgpt`), holding `{access, refresh, idToken, accountId, email, expiresAt}`, kept for AI15's `codex app-server` login. HikariSub never writes `~/.codex`.
- **Who does what** ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md#keys)): Qt runs the whole sign-in: the HTTP calls, the 127.0.0.1 listener (open only while a sign-in runs) and the Codex refresh. It writes the tokens straight to the keychain, so the sidecar never sees them. Qt opens only https pages on auth.openai.com and openrouter.ai.

## AI components

The section sits between External agents and Privacy, and folds away with the rest of the page while AI is off. Builds with `HIKARI_AI_LOCAL_SIDECAR` don't show it.

- **What it lists:** the AI component's version, "Runs on Bun 1.4.4", the provider libraries (Anthropic, OpenAI and Google Gen AI with their versions), and the status. **What's new** opens the `ai-channel` release.
- **The status:** "Up to date · checked 12 minutes ago", "Checking…", "1.5.0 is available · 3.1 MB" with **Update now**, "Downloading 1.5.0 · 1.2 of 3.1 MB" with Cancel, or "1.5.0 is ready · starts once AI has been idle for 2 minutes". **Check now** reads the channel at once.
- **When a check fails:** "Couldn't check: no connection", with when it last worked; "Couldn’t check: bad signature", which keeps the current version and installs nothing from that list; and "Newest this HikariSub runs" with "Update HikariSub to get newer AI features." when the channel has a newer component only a newer app runs.
- **Update AI components automatically** (`ai.autoUpdate`) is on by default. With it off, the hourly check still runs and Update now downloads. A machine policy ([ADR 0031](adr/0031-the-ai-sidecar-updates-on-its-own.md)) holds it off, says "Your administrator turned automatic updates off on this computer.", and leaves Check now and Update now working.
- **Turning it off:** clear **File › Settings › AI › AI components › Update AI components automatically**, then OK or Apply.
- **The rollback notice:** after a rollback the section says "AI component 1.5.0 stopped working, so HikariSub went back to 1.4.0 and won't install 1.5.0 again." With **Always send crash reports** on, a report goes out and the notice adds "A crash report was sent." It stays, in `state.json`, until Dismiss. Nothing pops up.

## The AI component download

The sidecar isn't in the packages ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)). When no sidecar this app runs is installed, opening the add form reads the `ai-channel` manifest for the size of the runtime and bundle it would fetch, without downloading, and the form says: "Your first provider downloads the AI component once: N MB." Test connection or Add starts the download, with progress in the form, and a test waits for it. A failed download fails the test with its reason. A sign-in doesn't need it: only the test that follows an OpenRouter sign-in waits for the download. Nothing downloads while AI is off, and builds with `HIKARI_AI_LOCAL_SIDECAR` download nothing.

Later versions arrive on their own ([ADR 0031](adr/0031-the-ai-sidecar-updates-on-its-own.md)): `ai.autoUpdate`, on by default, lets the hourly check download a newer sidecar, which switches in at an idle moment.

## Where it lives

- `AiSettings` (`src/app/ai_settings.*`) is the page's model: provider rows, model choices, role values, the form's state, the download, the Codex row, the AI components section and the agent rows. It uses only what `AiFeatures` built.
- `src/backends/ai_agent_cli.*` finds the agent CLIs, tells how each was installed, reads the latest versions and runs Update now.
- `AiSignIn` (`src/backends/ai_signin.*`) runs the sign-ins with `QNetworkAccessManager` and a `QTcpServer` on 127.0.0.1, and keeps the Codex session: import, refresh and sign-out.
- `SettingsAiPage.qml` is the page. `SettingsDialog` hands it the `ai` object, and calls `commit()` on OK and Apply to save the staged role models.
- `ai.off`, `ai.autoUpdate`, `ai.promptCaching`, `ai.webSearch` and `ai.searchService` are bound to the `ai` page (`OptionsPage::Ai`), so Reset page and settings import and export cover them.

## Tests

- `hikari_ui_ai_settings_tests`: adding Gemini after a test, keys and addresses a new provider needs, a refused keychain, editing with and without a new key, deleting, testing, roles that wait for Apply, the one-time download and a failed one, AI off, and the caching switch reaching every request.
- `hikari_ui_shell_tests` (`settingsAiPageAddsGeminiAndFoldsAwayWhenAiIsOff`): the page in the app, with fake AI ports: the switch folds the page, the Gemini form, adding through it, roles saved only by OK, and an Exa key that fails its test kept out of the keychain until Add anyway, a Brave key tested and then stored from the Web search section, named in the Privacy line and chosen by OK.
- `hikari_ui_shell_tests` (`settingsAiAgentsAndComponentsOfferTheirUpdates`): the agent badges, both Update now buttons and a newer AI component in the app, from a seeded store and fake CLIs. With `HIKARI_SETTINGS_SHOT_DIR` set it also saves both sections in the four themes.
- `hikari_ui_ai_settings_tests` also covers web search: a key stored only after its one search passes, and removed; a rejected key and a used-up plan kept out of the keychain, and Add anyway storing the rejected one; no test without a provider, with Add anyway still storing the key; the service reaching the sidecar only with the switch on and its key stored; and which providers search on their own.
- `hikari_ui_ai_settings_tests` also covers the Codex row: found on `PATH`, the code, its page opened only when asked, cancel, the browser, a workspace without codes, sign-out, import, refresh; and the OpenRouter sign-in, its refusal and sign-out.
- `hikari_ui_ai_settings_tests` also covers the AI components section (a newer version, Update now staging it, a bad signature, the rollback notice and Dismiss) and the agent rows (badges against the latest version, Update now with a fake CLI, and an update that leaves the version alone).
- `hikari_backends_ai_agent_cli_tests`: each install method from fake paths, Windows `.cmd` shims, version parsing, badges, Claude's channel, the daily cache against a local server, and Update now's outcomes with a fake CLI (updated, unchanged, failed, timed out, cancelled). No test runs a real `claude` or `codex`.
- `hikari_backends_ai_signin_tests` runs the flows against a fake auth.openai.com and openrouter.ai (`tests/support/fake_auth_server.h`): the device code's polling, its 3 s floor, 404 codes off, denials, expiry, 429 and server errors; the browser's state check and its busy port; OpenRouter's random callback path; refresh, a refused refresh, cancelling, a refusing keychain, the URL allow-list, import leaving `auth.json` untouched, and sign-out.
- `hikari_application_ai_providers_tests`, `hikari_backends_ai_requests_tests` and the sidecar's `caching.test.ts` cover `update`, `unassign`, the first catalogue, `probe` and `caching: false`.
