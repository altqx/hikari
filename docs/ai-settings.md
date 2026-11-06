# Settings › AI

The AI tab of Settings ([AI20 #649](https://github.com/altqx/hikari/issues/649)). The owner approved its [mockup](https://github.com/altqx/hikari/issues/649#issuecomment-6612977714) and all 11 recommendations on 2026-11-04. The tab sits between Shortcuts and Updates and privacy, with the `ai-assistant` icon. It stays in the list while AI is off, because it is the way back.

## What it holds

| Section | What | Saved |
| --- | --- | --- |
| AI features | **Turn off all AI features** (`ai.off`, off by default) | On OK or Apply, like every other setting |
| Providers | Add, edit, test and delete providers | At once (see below) |
| Models | Chat, Translator, Reviewer, Vision and Judge. Every role but Chat can be "Same as chat"; Vision lists only models that read images. Refresh models fetches every provider's catalogue again | On OK or Apply |
| Privacy | **Use prompt caching** (`ai.promptCaching`, on by default), the What is sent to providers link, and a line that says whether the AI component runs in its sandbox here; "isn’t sandboxed on this system" shows in the warning colour where the sandbox can’t apply ([ADR 0029](adr/0029-the-ai-sidecar-runs-in-an-os-sandbox.md)) | On OK or Apply |

The Agent, External agents and MCP sections arrive with AI7, AI15 and AI3/AI13, below Models and above Privacy, as drawn in the mockup. Sign-in rows (AI6) go next to a provider's key and on the External agents rows.

## Turn off all AI features

Checking the box folds the rest of the page away at once and lists what is kept (providers, keys, threads, the downloaded AI component) and what is hidden. OK or Apply then turns AI off without a restart: `AiFeatures` destroys its objects and stops the sidecar ([architecture](architecture.md#ai)). Unchecking it and pressing OK or Apply brings everything back. While the box is unchecked but not yet applied, the page says to press OK or Apply.

With AI off, `AiSettings` lists no providers and refuses every action, and nothing reads the keychain, downloads or starts.

## Providers

- **Add provider** opens a menu: OpenAI, Anthropic, Google Gemini, OpenRouter and Custom endpoint…. The form opens below the list, not in a second window: Name, the key (masked, with Show key), Test connection, Cancel and Add.
- **Google Gemini** asks for a "Gemini API key", links to [Google AI Studio](https://aistudio.google.com/apikey) and says Vertex AI and Google Cloud sign-in aren't supported.
- **Custom endpoint** adds Base URL and API (Chat Completions or Responses). Its key is optional: a local server such as Ollama or LM Studio needs none, and a key is sent only over HTTPS, to this computer or to a Tailscale peer ([key rules](ai-providers.md#keys)).
- **Changes apply at once,** outside OK, Cancel and Apply, because the key goes straight to the system keychain. A provider exists only once its key is stored. A stored key is never shown again: Edit leaves the key field empty, which keeps the key, and a typed key replaces it.
- **Each row** shows the wire and host, the last test ("Connected · N models", the error, or "Not tested · N models"), where the key is ("Key in keychain" or "No key"), and Test connection, Edit and Delete. A rejected key says "Replace the key with Edit". Delete asks first and removes the key from the keychain; roles that used the provider go back to the chat model.
- **Test connection** in the form lists the provider's catalogue with the typed key through `AiRequests::probe`, and saves nothing. Add then stores the provider with that catalogue. A provider added without a test fetches its catalogue right after. The first catalogue fills the chat role while it is empty.

## The AI component download

The sidecar isn't in the packages ([ADR 0026](adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)). When no sidecar this app runs is installed, opening the add form reads the `ai-channel` manifest for the size of the runtime and bundle it would fetch, without downloading, and the form says: "Your first provider downloads the AI component once: N MB." Test connection or Add starts the download, with progress in the form, and a test waits for it. A failed download fails the test with its reason. Nothing downloads while AI is off, and builds with `HIKARI_AI_LOCAL_SIDECAR` download nothing.

Later versions arrive on their own ([ADR 0031](adr/0031-the-ai-sidecar-updates-on-its-own.md)): `ai.autoUpdate`, on by default, lets the hourly check download a newer sidecar, which switches in at an idle moment.

## Where it lives

- `AiSettings` (`src/app/ai_settings.*`) is the page's model: provider rows, model choices, role values, the form's state and the download. It uses only what `AiFeatures` built.
- `SettingsAiPage.qml` is the page. `SettingsDialog` hands it the `ai` object, and calls `commit()` on OK and Apply to save the staged role models.
- `ai.off` and `ai.promptCaching` are bound to the `ai` page (`OptionsPage::Ai`), so Reset page and settings import and export cover them.

## Tests

- `hikari_ui_ai_settings_tests`: adding Gemini after a test, keys and addresses a new provider needs, a refused keychain, editing with and without a new key, deleting, testing, roles that wait for Apply, the one-time download and a failed one, AI off, and the caching switch reaching every request.
- `hikari_ui_shell_tests` (`settingsAiPageAddsGeminiAndFoldsAwayWhenAiIsOff`): the page in the app, with fake AI ports: the switch folds the page, the Gemini form, adding through it, and roles saved only by OK.
- `hikari_application_ai_providers_tests`, `hikari_backends_ai_requests_tests` and the sidecar's `caching.test.ts` cover `update`, `unassign`, the first catalogue, `probe` and `caching: false`.
