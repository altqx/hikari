---
status: accepted
amends: 0019, 0026, 0027
---

# The AI sidecar updates on its own

On 2026-11-04 altqx asked that "the sidecar and harness should have an independent update from the main app so users can get the latest", and settled the design the same day (Q48–Q71, [AI24 #744](https://github.com/altqx/hikari/issues/744)). The open points of the plan were answered the same day too; the owner answered the channel and the signing environment ("yes to all"). The research is in [CLI updates](../research/cli-updates.md).

The sidecar now carries everything that may change between app releases: Bun, the Claude Agent SDK and the provider SDKs, the agent loop, the prompts and tool descriptions, the built-in skills, the agent adapters, the MCP server and the provider quirks. Qt keeps the UI, the review, the keys and the Hikari API. Changes there still need an app update.

This amends [ADR 0026](0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md). It replaces:

- in Runtime and build, "the latest Bun every release" and "built from the same commit as the app";
- in Versioning, "the app and the sidecar come from one commit": Qt no longer refuses a sidecar from another build;
- Download, verification and update, apart from the developer builds and the licence notices.

It amends [ADR 0019](0019-installers-and-seamless-updates.md) and [ADR 0027](0027-the-update-manifest-is-signed-with-minisign.md): `hikarisub-update.json` no longer names the sidecar, and the sidecar has a signed manifest of its own.

## Two assets

- **The runtime** is `hikari-ai-runtime`, a compiled Bun that only loads the bundle it is given (`src/ai-sidecar/src/runtime.ts`), built with the same `--compile --bytecode --no-compile-autoload-*` flags as before. Its id is `<Bun version>-<8 hex digits>`, from a SHA-256 over its inputs, so it changes only when Bun or the loader does. The asset is `hikari-ai-runtime-<id>-<platform>.gz`, about 49 MiB.
- **The bundle** is `hikari-ai.js` and its bytecode `hikari-ai.js.jsc`, one zip for both platforms: `hikari-ai-<version>.zip`, a few MiB. Most updates download only this.
  - **Each provider SDK** is a file of its own next to it, `hikari-ai-{anthropic,gemini,openai}.js` with its bytecode, loaded the first time that provider is used. Loading all of them at start made the split start twice as slowly as one file.
- **Reproducible:** CI builds each twice and compares them byte for byte, and the bundle must be the same for both platforms.
- **As fast as one file:** CI starts the split sidecar and a single executable 20 times each. The median of the split must stay within 1.10× the single file plus 20 ms.

## The channel

- **One rolling release, `ai-channel`,** in this repository holds every runtime, every bundle and the signed manifest (owner, 2026-11-04). It is never marked latest. A rolling release keeps the sidecar from filling the release list, which the app's and legacy's update checks read 50 at a time.
- **Versions** are `ai-v<protocol major>.<n>.0`, and each one also gets its git tag. The release notes list the PR titles of each version.
- **The manifest** is at a fixed URL, `releases/download/ai-channel/hikari-ai-update.json`. It lists the last 10 releases, newest first:

  ```json
  {
    "schema": 1,
    "version": "1.4.0",
    "runtimes": { "1.4.4-c0afb666": { "linux-x64": { "name": "…", "size": 1, "sha256": "…" }, "windows-x64": { … } } },
    "releases": [
      { "version": "1.4.0", "protocol": 1, "required_api": 1, "runtime": "1.4.4-c0afb666",
        "bundle": { "name": "hikari-ai-1.4.0.zip", "size": 1, "sha256": "…" }, "packages": { "bun": "1.4.4", … } }
    ],
    "blocked": []
  }
  ```

- **Blocked versions** are listed in `src/ai-sidecar/channel.json`, in the PR that fixes or reverts them. The next release's manifest carries the list.
- **Signed** as in ADR 0027, with the trusted comment `hikarisub ai-v<version> hikari-ai-update.json`. The app refuses a manifest whose signature is missing or bad, and one older than the newest it has seen.

## Publishing

`.github/workflows/ai-sidecar.yml` publishes from main only:

- **On merge:** a merge to main that changes the sidecar publishes the next version.
- **By hand:** a run on main with `publish`.
- **With the app:** each app release starts that run on main. It publishes only when the sidecar has changed since the last `ai-v*` tag, so the newest sidecar always matches the newest app.
- **Weekly:** `ai-sidecar-bump.yml` opens a PR that bumps the Claude Agent SDK once a newer version is 7 days old. Bun and the provider SDKs ride along. The PR comes from `GITHUB_TOKEN`, which starts no workflows, so the job dispatches CI on its branch. Until AI15 adds the Claude Agent SDK, it opens nothing.
- **The compatibility check:** each build runs `initialize` against the API references of main and of the last 3 app releases that have AI. Until such a release exists, that is main alone. A release that one of them would accept and that fails there doesn't publish.
- **Signing:** the signing job runs in the protected environment `ai-release`, which admits only main and holds `MINISIGN_SECRET_KEY` and `MINISIGN_PASSWORD` as environment secrets (owner, 2026-11-04). The repository-level copies stay for `release.yml`. As in ADR 0027, the job runs only the pinned `minisign` and the repository's scripts. Only its signing step receives the key. Before it signs, it checks the published manifest with the app's own check, and afterwards it checks the new one too.
- **Uploading:** the publishing job uploads the new bundle and any new runtime first, then the manifest and its signature. It reads each back by digest and then pushes the tag.

## Which sidecar an app runs

- **The rule:** an app runs a release with its own protocol major and a `required_api` no newer than its `kApiVersion`.
- **Tools:** `initialize` carries the app's Hikari API reference, and the sidecar offers only the operations that app has.
- **Older apps** keep their last compatible sidecar.

## On the user's machine

- **The store** is `<user folder>/ai/sidecar/`: `runtime/<id>/`, `bundle/<version>/` and `state.json`. The state names the current version, the previous one, a staged one, the versions skipped after failing, and the newest manifest seen. Only the current, previous and staged sidecars and their runtimes are kept.
- **The sandbox** ([ADR 0029](0029-the-ai-sidecar-runs-in-an-os-sandbox.md)): Qt downloads, checks and unpacks every update. The sidecar never does, and has no network to do it with. Its profile reads and executes only the current runtime's and bundle's folders, and it can't write to the store.
- **Checks** happen when AI starts, then hourly while AI is on, and never with AI off. A check fetches the manifest from the download URL, not the API, with a conditional GET.
- **The first download** comes from the channel, when the first provider is added, as before.
- **Updates** download in the background. A new sidecar switches in once nothing has been busy for 2 minutes: no request outstanding, no turn, no AI22 sub-agent, no MCP client. Otherwise it waits for the next AI start.
- **Rollback:** a new sidecar that fails `initialize`, or crashes twice within 10 minutes, is skipped from then on, and the previous one runs again. The user sees one quiet notice. A crash report goes out if crash reports are on.
- **Settings:** "Update AI components automatically" (`ai.autoUpdate`) is on by default. With it off, checks still run, and the Update button downloads.
- **Policy:** `HIKARI_AI_AUTO_UPDATE=0`, `HKLM\SOFTWARE\Policies\HikariSub\AiAutoUpdate` = 0 (DWORD), or `{"ai_auto_update": false}` in `/etc/hikarisub/policy.json` turns automatic updates off for every user on the machine. Manual updates still work.
- **Crash reports and analytics**, when they're on, carry the sidecar's version and its SDK versions.

## The user's `claude` and `codex`

- **Badges** compare the installed `--version` with the latest from the install method's source. The compatibility table (minimum supported, known broken) rides in the signed AI manifest. No badge shows until AI15 lands; the CLI probe already finds `claude` and `codex` on `PATH`.
- **Update now** runs the official command only when the binary's real path proves the install method. It runs on the user's click, outside SB1's sandbox, with a fixed command from Qt, and never one from the sidecar. Anything else gets a command to copy, and nothing that needs admin ever runs.
