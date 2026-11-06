# Settings › AI

The AI tab of Settings ([AI20 #649](https://github.com/altqx/hikari/issues/649)). The owner approved its [mockup](https://github.com/altqx/hikari/issues/649#issuecomment-6612977714) and all 11 recommendations on 2026-11-04. The tab sits between Shortcuts and Updates and privacy, with the `ai-assistant` icon. It stays in the list while AI is off, because it is the way back.

## What it holds

| Section | What | Saved |
| --- | --- | --- |
| AI features | **Turn off all AI features** (`ai.off`, off by default) | On OK or Apply, like every other setting |
| Providers | Add, edit, test and delete providers | At once (see below) |
| Models | Chat, Translator, Reviewer, Vision and Judge. Every role but Chat can be "Same as chat"; Vision lists only models that read images. Refresh models fetches every provider's catalogue again | On OK or Apply |
| External agents | Codex: found on `PATH` with its version, and HikariSub's own ChatGPT sign-in for it (AI6) | At once (see below) |
| Privacy | **Use prompt caching** (`ai.promptCaching`, on by default), the What is sent to providers link, and a line that says whether the AI component runs in its sandbox here; "isn’t sandboxed on this system" shows in the warning colour where the sandbox can’t apply ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)) | On OK or Apply |

The Agent and MCP sections arrive with AI7 and AI3/AI13, below Models and above Privacy, as drawn in the mockup. External agents has only the Codex row so far; AI15 adds Claude Code and ACP agents, their detection, Choose… and their run errors.

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

## External agents: Codex

- **The row** says whether `codex` is on `PATH` ("Found on PATH · 0.58.0.", from `codex --version`) and whether HikariSub is signed in to ChatGPT for it. The sign-in doesn't need the CLI.
- **Sign in with ChatGPT…** shows a device code under the row: the code, Copy, "Open auth.openai.com", and a countdown from the code's 15 minutes. Nothing opens on its own. "Can't use a code? Sign in through the browser" opens auth.openai.com and listens on localhost:1455 until the page says the user can close it. While another app holds port 1455, the panel says so in place of that link.
- **A workspace that turns off codes** (the code request answers 404) shows "Your ChatGPT workspace turns off sign-in codes. Sign in through the browser instead." with the browser as the main action. The other errors: a refusal, an expired code ("Get a new code"), an unreachable host, and a timeout.
- **Signed in** shows "Signed in as <email> · Sign out". Sign out deletes HikariSub's sign-in; Codex in the terminal stays signed in.
- **Import from ~/.codex (read-only)** shows while `~/.codex/auth.json` exists and HikariSub isn't signed in. It asks first, because the two then share one sign-in and whichever refreshes it signs the other out. It copies the tokens and never writes the file.
- **An expired session** is refreshed when the page opens. When ChatGPT refuses the refresh, the row says "Sign-in expired" with "Sign in again…".
- **Where the session lives:** one keychain entry, `codex-chatgpt` (`ai-provider/codex-chatgpt`), holding `{access, refresh, idToken, accountId, email, expiresAt}`, kept for AI15's `codex app-server` login. HikariSub never writes `~/.codex`.
- **Who does what** ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md#keys)): the sidecar runs the sign-in and its 127.0.0.1 listener, only while a sign-in runs. Qt opens only https pages on auth.openai.com and openrouter.ai, and writes the keychain. Tokens reach Qt only through `_hikari/secret.store`, under the name the running sign-in expects.

## The AI component download

The sidecar isn't in the packages ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)). When none is installed for this version, opening the add form reads the release manifest for its size, without downloading, and the form says: "Your first provider downloads the AI component once: N MB." Test connection or Add starts the download, with progress in the form, and a test waits for it. A failed download fails the test with its reason. A sign-in downloads it first in the same way. Nothing downloads while AI is off, and builds with `HIKARI_AI_LOCAL_SIDECAR` download nothing.

## Where it lives

- `AiSettings` (`src/app/ai_settings.*`) is the page's model: provider rows, model choices, role values, the form's state, the download and the Codex row. It uses only what `AiFeatures` built.
- `AiSignIn` (`src/backends/ai_signin.*`) runs sign-ins in the sidecar, answers `_hikari/secret.store`, and keeps the Codex session: import, refresh and sign-out. The sidecar's side is `src/ai-sidecar/src/signin/`.
- `SettingsAiPage.qml` is the page. `SettingsDialog` hands it the `ai` object, and calls `commit()` on OK and Apply to save the staged role models.
- `ai.off` and `ai.promptCaching` are bound to the `ai` page (`OptionsPage::Ai`), so Reset page and settings import and export cover them.

## Tests

- `hikari_ui_ai_settings_tests`: adding Gemini after a test, keys and addresses a new provider needs, a refused keychain, editing with and without a new key, deleting, testing, roles that wait for Apply, the one-time download and a failed one, AI off, and the caching switch reaching every request.
- `hikari_ui_shell_tests` (`settingsAiPageAddsGeminiAndFoldsAwayWhenAiIsOff`): the page in the app, with fake AI ports: the switch folds the page, the Gemini form, adding through it, and roles saved only by OK.
- `hikari_ui_ai_settings_tests` also covers the Codex row: found on `PATH`, the code, its page opened only when asked, cancel, the browser, a workspace without codes, sign-out, import, refresh; and the OpenRouter sign-in, its refusal and sign-out.
- `hikari_backends_ai_signin_tests`: the device and OpenRouter sign-ins against the fake sidecar, a secret under the wrong name, a refusing keychain, cancel, the URL allow-list, import leaving `auth.json` untouched, refresh and sign-out. The sidecar's `test/signin/signin.test.ts` runs the flows against a mock auth.openai.com and openrouter.ai: polling, 404 codes off, denials, expiry, 429, the browser's state check and port 1455, OpenRouter's random path, and refresh.
- `hikari_application_ai_providers_tests`, `hikari_backends_ai_requests_tests` and the sidecar's `caching.test.ts` cover `update`, `unassign`, the first catalogue, `probe` and `caching: false`.
