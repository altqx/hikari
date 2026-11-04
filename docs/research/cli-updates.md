# How t3code detects and updates agent CLIs (Claude Code, Codex, others)

Researched 2026-11-04. Primary sources only:

- t3code: shallow clone of `pingdotgg/t3code` at `a185dd924` (2026-11-03). Links pin that commit. Prefix `T/` = `https://github.com/pingdotgg/t3code/blob/a185dd924889cd7f925760431a9f29b973a35e2f/`.
- Codex: raw files from `openai/codex` main at `db3c44414` (2026-11-04). Prefix `C/` = `https://github.com/openai/codex/blob/db3c44414c2cd4ae33adaf4fa33382cb3dde6bd5/`.
- Claude Code docs: <https://code.claude.com/docs/en/setup> ("setup") and <https://code.claude.com/docs/en/troubleshoot-install> ("troubleshoot").
- Codex docs: <https://raw.githubusercontent.com/openai/codex/main/README.md> and <https://learn.chatgpt.com/docs/codex/cli>. (`developers.openai.com/codex/cli` returns a 308 redirect to the second URL.)
- npm docs: <https://docs.npmjs.com/cli/v11/configuring-npm/folders>.

No binaries from either clone were run.

---

## 1. Version display and comparison

**Yes, there are two separate checks: "latest" and "compatibility".**

### Installed version
- Claude: `claude --version` is the health probe (`T/apps/server/src/provider/ClaudeProvider.ts#L521-L526`).
- Codex: `codex --version`, parsed with `^codex-cli (\d+\.\d+\.\d+)$`, then `codex app-server --help` (`T/apps/server/src/provider/CodexInstallation.ts#L405-L411`, also `T/apps/server/src/provider/CodexProvider.ts#L623`).

### Latest-version check (`versionAdvisory`)
Implemented in `T/packages/provider-core/src/server/maintenanceResolver.ts`:
- `enrichProviderSnapshotWithVersionAdvisory` (L1034-L1075) resolves the latest version. It only does this when the provider is enabled and installed, the version is known, and `enableProviderUpdateChecks` is not false.
- That setting defaults to `true` (`T/packages/contracts/src/settings.ts#L1205`). Its UI description is "Check installed provider CLIs for newer available versions and fetch model catalog updates" (`T/apps/web/src/components/settings/SettingsPanels.tsx#L1911`).
- Where "latest" comes from, chosen per install method (`resolveLatestProviderVersion`, L1000-L1021):
  - **default:** npm registry `https://registry.npmjs.org/<pkg>/latest` with a 4 s timeout (L963-L976);
  - **npm on PATH:** prefers `npm view --global [--prefix P] --json <pkg>@latest version`, so the user's registry, auth and proxy apply (L795-L847);
  - **pnpm:** `pnpm outdated -g`, which respects `minimumReleaseAge` (L432-L464);
  - **Homebrew:** `brew info --json=v2 <name>`, because "Brew's release can lag npm" (L698-L707);
  - **Chocolatey:** `choco search claude-code --exact --limit-output` (L506-L516);
  - **Claude native installer:** `GET https://downloads.claude.ai/claude-code-releases/<channel>`. The channel is `autoUpdatesChannel`, read from `~/.claude/settings.json` plus the managed-settings files and drop-ins (`T/apps/server/src/provider/Drivers/ClaudeMaintenance.ts#L21-L95`).
- Results are cached. The install-method resolution has a 1 h TTL (`maintenanceResolver.ts#L43`, L885-L896).
- Comparison is semver (`compareSemverVersions`). The result status is `current`, `behind_latest` or `unknown` (L904-L930).

### Minimum-version / compatibility check (`compatibilityAdvisory`)
- `T/apps/server/src/provider/providerCompatibility.ts`. Each policy is per driver and per T3 version range. It sorts CLI versions into `supported`, `graceful`, `unsupported`, `broken` or `unknown`, and may name a `recommendedVersion` or `recommendedRange`.
- The policies live in `T/apps/server/src/provider/model-manifest.json` (bundled). A newer copy is fetched from `raw.githubusercontent.com/pingdotgg/t3code/main/...` with a 1 h TTL (`T/apps/server/src/provider/ModelManifest.ts#L25-L28`).
- Current values:
  - claudeAgent: `>=2.1.280` supported, `2.1.111–2.1.279` graceful, `<2.1.111` unsupported;
  - codex (T3 ≥0.0.44): `>=0.156.0` supported, `0.149–0.155` unsupported, `<0.149` broken.
- Per-model minimums: `updateRequiredModels` produces "Update Claude Code to vX or newer to use <models>." (`T/packages/client-runtime/src/providerUpdateRequiredModels.ts`).

### What the UI shows
`T/apps/web/src/components/settings/providerStatus.ts#L122-L183`, rendered in `T/apps/web/src/components/settings/ProviderInstanceCard.tsx#L961-L1066`. The provider card has a badge with a popover:
- **Behind latest:** title "Update available". Detail is "Update available: install vX." or the advisory message:
  - with a one-click command: "Install the update now or review provider settings.";
  - manual only: "Version X is out. T3 Code runs <path>; update it with the app or tool that installed it." (`maintenanceResolver.ts#L41`, L898-L902).
- **Compatibility problem:** title "Limited support", "Unsupported version" or "Known broken version", plus "Use <recommendation>."
- **Popover controls:** an **"Update now"** button (or "Install vX" for a pinned recommended version), then "or, update manually using", then the command in a `<code>` box with a copy button.
- **Elsewhere:** sidebar pills say "Updating <provider>", "<provider> update failed", "still needs an update" or "<provider> updated: vX" (`T/apps/web/src/components/ProviderUpdateLaunchNotification.logic.ts`). An **"Update all"** button sits in provider settings (`T/apps/web/src/components/ProviderUpdatesAction.tsx#L72-L117`).

## 2. Does t3code run the update, and how does it detect the install method?

**It runs the update itself when it can prove which tool owns the binary. Otherwise it only advises.**

`ProviderMaintenanceCapabilities.update` holds a command to run. `manualCommand` is "Displayed for manual updates; never executed by T3 Code" (`T/packages/provider-core/src/server/maintenance.ts#L20`).

### Detection
Detection is path-based. It looks at both the PATH-resolved command and its `realpath` (`resolveProviderMaintenanceCapabilitiesEffect`, `maintenanceResolver.ts#L849-L883`; decision tree `resolvePackageManagedProviderMaintenance`, L466-L655). Checks run in this order:

| Detected as | Proof | Command T3 runs |
|---|---|---|
| Claude via WinGet (Windows) | path matches `.../WinGet/(Links\|Packages/Anthropic.ClaudeCode_*)/claude.exe` | **none, manual only**. The source says "WinGet-managed claude update exits 0 without updating; T3 does not run winget upgrade" (L258-L263, L485-L492) |
| Claude via Chocolatey (Windows) | `.../chocolatey/(bin\|lib/claude-code/tools)/claude.exe` or `$ChocolateyInstall` | **none**. Shows `choco upgrade claude-code`, because the package directory is admin-only (L265-L280, L493-L518) |
| Native installer | Claude: path ends `/.local/bin/claude[.exe]` or contains `/.local/share/claude/` (`ClaudeMaintenance.ts#L27-L40`). Codex: realpath under `<home>/packages/standalone/` (`T/apps/server/src/provider/Drivers/CodexDriver.ts#L62-L95`). Not used if the realpath is actually an npm global or a mise install (L535-L544) | `<resolved claude> update`; `<resolved codex> update` with `CODEX_HOME=<owning home>` |
| Vite+ global | `/.vite-plus/bin/`, `.local/share/vite-plus/bin`, `AppData/Local/vite-plus/bin` | `vp i -g <pkg>` |
| bun global | `/.bun/bin/` | `bun i -g <pkg>@latest` |
| pnpm global | `.local/share/pnpm`, `Library/pnpm`, `AppData/Local/pnpm`, `pnpm/global` | `pnpm add -g [--allow-build=<pkg>] <pkg>@latest`. It pins the exact version because pnpm 11 `@latest` respects `minimumReleaseAge` (L186-L196) |
| yarn global | `/yarn/(data/)global/node_modules/` | `yarn global add <pkg>@latest` |
| Volta | realpath basename `volta-shim` and `<VOLTA_HOME>/tools/image/packages/<pkg>` exists | `volta install <pkg>@latest` |
| npm global | realpath contains `/lib/node_modules/<pkg>/` (prefix = text before it). On Windows: `<shimdir>/node_modules/<pkg>/package.json` exists (L302-L319, L747-L772) | `npm install -g --prefix <prefix> --allow-scripts=<pkg> <pkg>@latest` |
| mise | `/mise/installs\|shims\|command-wrappers/`, a mise keg, or a small `#!` script calling `mise x/exec` | **none, manual only** (version pinned in mise config) |
| Homebrew | realpath matches `<prefix>/(Cellar\|Caskroom)/<name>/<ver>/`, and `brew --prefix` confirms that prefix | `brew upgrade [--cask] <name>` (L321-L341, L662-L710) |
| anything else | n/a | Falls back to the native updater. Codex sets `requiresCommandPath: true`, so for Codex, and for any other `node_modules` path, the result is **manual only** (L545-L551, L654) |

- Packages used: Claude is `@anthropic-ai/claude-code`. Codex is `@openai/codex`, with native `update` that `requiresCommandPath` (`CodexDriver.ts#L79-L95`).
- Other drivers use the same resolver. Pi: `@earendil-works/pi-coding-agent`, native `update --self` (`T/packages/provider-pi/src/server/driver.ts#L45-L48`). OpenCode: `opencode-ai` or `@opencode/cli`, native `upgrade` (`T/packages/provider-opencode/src/server/driver.ts#L83-L87`). Cursor, Grok, Kiro, fx, Muse, Antigravity and the ACP registry use manual-only capabilities (files listed in the grep of `makeManualOnlyProviderMaintenanceCapabilities`).
- **T3-managed Codex.** With `setupMode: "managed"`, T3 downloads a **pinned** Codex release itself into `<baseDir>/tools/codex`:
  - the release is `rust-v0.166.0` from GitHub, with per-platform sha256 (`CodexInstallation.ts#L60-L103`, L185);
  - it prefers a local `codex` whose compatibility is `supported` (L424-L430);
  - managed Codex reports `update: null` (`T/apps/server/src/provider/Drivers/CodexManagedProvider.ts#L219`), so it updates only when T3 itself updates.

### Running the update
`T/apps/server/src/provider/providerMaintenanceRunner.ts#L382-L645`:
1. Takes a per-lock-key mutex. Two installs with the same owner queue, and the UI shows "Waiting for another provider update to finish." (L410-L417, L628-L634).
2. Re-resolves the install with `fresh: true`. If ownership changed, it aborts with "Provider installation changed" (L440-L455).
3. Checks the **candidate latest version** against the compatibility policy. If that version is `broken` or `unsupported`, it refuses: "The latest provider version is incompatible with this T3 Code release." (L457-L498).
4. Re-probes the provider. If it is already current, it reports "Already up to date." (L500-L532).
5. Spawns the command with the provider's environment. There is no shell except for Windows `.cmd` shims. The timeout is **30 min** because "`claude update` download[s] a ~300 MB binary without printing progress, and killing one mid-download leaves a truncated binary" (L44-L48, L95-L206). Progress lines are streamed to the UI once per second.
6. Verifies by re-probing. Outcomes are `succeeded`, `unchanged` ("could not verify" or "still detects an outdated provider version") or `failed` with the last line of output (L556-L604).

## 3. Confirmation and session restart

- **No confirmation dialog.** "Update now" or "Update all" runs the command straight away. The button handler calls `updateProvider` (`ProviderUpdatesAction.tsx#L42-L66`, L80-L99), and nothing confirms in between. The click itself is the consent. "Update everything" (sidebar pill) runs provider updates silently (`quiet: true`) before it updates T3 servers (`T/apps/web/src/components/sidebar/useUpdateEverything.ts#L150-L153`).
- **No automatic update.** I found no path that runs a provider update without a click. The only background work is the latest-version check.
- **Running sessions are not restarted.**
  - The success toast says "New sessions will use the updated provider." (`ProviderUpdateLaunchNotification.logic.ts#L88-L91`).
  - After an update, `refreshAfterUpdate` only force-refreshes the manifest, invalidates caches and re-probes each instance's snapshot (`T/apps/server/src/provider/ProviderRegistry.ts#L695-L760`).
  - Restart and continue-threads logic (`continueThreadsAfterServerUpdate`) applies only to **T3 server** updates (`T/apps/web/src/components/sidebar/updateEverything.logic.ts#L20-L60`).

## 4. Official update commands, auto-update, and detection from the binary path

### Claude Code (setup and troubleshoot docs)

| Install method | Install | Update | Auto-updates? |
|---|---|---|---|
| Native (recommended) | `curl -fsSL https://claude.ai/install.sh \| bash`; `irm https://claude.ai/install.ps1 \| iex` | `claude update` | **Yes**. Checks "on startup and periodically while running", installs in the background, and takes effect on the next start |
| npm | `npm install -g @anthropic-ai/claude-code` | `npm install -g @anthropic-ai/claude-code@latest` (avoid `npm update -g`) | Yes, when the npm global dir is writable. "Claude Code runs npm without `sudo` when it updates itself". A non-writable dir gives a one-time notice |
| Homebrew | `brew install --cask claude-code` (stable) or `claude-code@latest` | `brew upgrade claude-code[@latest]` | No. Opt in with `CLAUDE_CODE_PACKAGE_MANAGER_AUTO_UPDATE=1`, after which Claude runs the upgrade and prompts for a restart |
| WinGet | `winget install Anthropic.ClaudeCode` | `winget upgrade Anthropic.ClaudeCode` | No. The same opt-in exists, but "the upgrade may fail while Claude Code is running because Windows locks the executable" |
| apt / dnf / apk | signed repos at `downloads.claude.ai/claude-code/...` | `sudo apt upgrade claude-code`, `sudo dnf upgrade claude-code`, `apk upgrade claude-code` | No. "those commands need elevated privileges" |

Settings and environment variables (setup docs):
- **`autoUpdatesChannel`**: `latest` (default) or `stable`, about one week behind. It applies to auto-updates and `claude update`.
- **`minimumVersion`**: a floor for updates.
- **`requiredMinimumVersion` / `requiredMaximumVersion`** (managed settings): refuse to start outside the range.
- **`DISABLE_AUTOUPDATER=1`**: stops only the background check. "`claude update` and `claude install` still work".
- **`DISABLE_UPDATES`**: blocks all update paths.
- **`claude doctor`**: shows the install, `Auto-updates: enabled/disabled (set by env: DISABLE_AUTOUPDATER)` and the last update attempt.

What `claude update` prints: `Successfully updated from <old> to version <new>` or `Claude Code is up to date (<v>)`. For Homebrew, WinGet and apk installs it prints `Claude is up to date!` instead. This matches T3's finding that WinGet `claude update` exits 0 without updating.

Detecting the install method from the path:
- **Native:** `~/.local/bin/claude` is a symlink into `~/.local/share/claude/versions/` (setup docs, "Auto-updates"). On Windows the binary is `%USERPROFILE%\.local\bin\claude.exe` (troubleshoot docs, "Check for conflicting installations").
- **Legacy local npm:** `~/.claude/local/`.
- **npm global:** `npm -g ls @anthropic-ai/claude-code`. By npm convention the path is `{prefix}/lib/node_modules/...` on Unix and `{prefix}\node_modules` with prefix `%AppData%\npm` on Windows (npm folders docs).
- **Homebrew:** the `Caskroom` path. The docs do not document it; T3 relies on the `<prefix>/Caskroom/<name>/<ver>/` layout.
- **Installer record:** the troubleshoot docs say `~/.claude.json` is "where the installer records the install method". The key name is not documented.

### Codex CLI

Install methods (README):
- standalone: `curl -fsSL https://chatgpt.com/codex/install.sh | sh`, or `irm https://chatgpt.com/codex/install.ps1 | iex`. It downloads from `releases.openai.com/codex` and falls back to GitHub Releases;
- `npm install -g @openai/codex`;
- `brew install --cask codex`;
- raw GitHub release archives.

Update instructions:
- The official CLI page's "Update Codex" section only says to re-run the standalone installer (learn.chatgpt.com/docs/codex/cli).
- The binary also has **`codex update`** ("Update Codex to the latest version.", `C/codex-rs/cli/src/main.rs#L184-L185`, L880-L895). It maps the install method to a command (`C/codex-rs/tui/src/update_action.rs`):
  - npm: `npm install -g @openai/codex` (bun, vp and pnpm have equivalents);
  - Homebrew: `brew upgrade --cask codex`;
  - standalone Unix: `sh -c 's=$(curl -fsSL https://chatgpt.com/codex/install.sh) && CODEX_NON_INTERACTIVE=1 eval "$s"'`;
  - standalone Windows: `powershell -ExecutionPolicy Bypass -c "$env:CODEX_NON_INTERACTIVE=1; irm https://chatgpt.com/codex/install.ps1 | iex"`;
  - unknown: "Could not detect the Codex installation method. Please update manually".
- On success it prints "Update ran successfully! Please restart Codex." (`main.rs#L859`).

Auto-update:
- There is **no silent self-update**. With `check_for_update_on_startup` (default `true`, `C/codex-rs/core/config.schema.json`), the TUI refreshes a cached latest version in the background at most every 20 h. The source is GitHub `releases/latest`, the npm registry, or the Homebrew cask API for brew installs (`C/codex-rs/tui/src/updates.rs#L27-L62`).
- If a newer version is cached, the TUI shows a popup (dismissable per version, L150-L165). The update runs on exit only "after the user accepts the update prompt" (`main.rs#L763-L831`).
- Separately, there is an internal `app-server daemon update` and a pid-backed standalone updater loop for daemon-owned packages (`main.rs#L670-L695`).

Detecting the install method (Codex's own logic, `C/codex-rs/install-context/src/lib.rs#L120-L334`):
- **standalone:** the canonical exe dir is under `$CODEX_HOME/packages/standalone/releases/`, with `CODEX_HOME` defaulting to `~/.codex`;
- **npm/bun/pnpm/Vite+:** the `codex.js` shim sets `CODEX_MANAGED_BY_NPM`, `_BUN`, `_PNPM` or `_VITE_PLUS` in the child environment (`C/codex-cli/bin/codex.js#L172-L230`). From the outside, the path test is a realpath inside `.../node_modules/@openai/codex/`;
- **brew:** macOS only, exe under `/opt/homebrew` or `/usr/local`;
- **else:** `Other`, meaning manual.
- Consequence: `codex update` decides the method from its own `CODEX_HOME`. A caller with a different `CODEX_HOME` gets "Could not detect". That is why T3 passes `CODEX_HOME=<home owning the binary>` (`CodexDriver.ts#L77-L93`).

## 5. Problems for a sandboxed desktop app

Most items come from the sources above. Two items are my inference and are marked **(inference)**.

- **Network.**
  - Version checks reach registry.npmjs.org, downloads.claude.ai, api.github.com, formulae.brew.sh and raw.githubusercontent.com.
  - Updates download about 300 MB for Claude (T3 runner comment). Claude enforces a 10-minute total download deadline and retries 3 times (troubleshoot, "The connection dropped while downloading the update").
  - Corporate proxies need `HTTPS_PROXY`. T3 routes npm lookups through `npm view` so that npm's registry, auth and proxy apply (`maintenanceResolver.ts#L836`).
- **Writes outside any app install dir.**
  - Claude native writes `~/.claude/downloads`, `~/.local/bin`, `~/.local/share/claude`, `~/.local/state/claude`, `~/.cache/claude` and `~/.claude.json` (troubleshoot, "Check directory permissions").
  - Codex standalone writes `$CODEX_HOME/packages/standalone` and the `~/.local/bin/codex` launcher.
  - npm writes its global prefix, Homebrew its prefix.
  - A filesystem sandbox (Flatpak, MSIX, macOS App Sandbox) must allow these, or the update must run on the host **(inference: e.g. `flatpak-spawn --host`; not covered by the sources)**.
- **Admin rights.**
  - Windows npm's default global prefix is `%AppData%\npm`, which is per-user (npm docs), and Claude's native Windows install "do[es] not need to run as Administrator" (setup).
  - Admin is needed for Chocolatey (T3 refuses for this reason), for apt/dnf/apk (setup), and for any npm prefix under `/usr/local` or Program Files. Claude docs say never `sudo npm`: "an install that needs `sudo` can't update automatically".
- **Executes shell pipelines.** Codex standalone update is `curl | sh`, or `powershell -ExecutionPolicy Bypass`. AppLocker, EDR or a sandbox may block this. Claude troubleshoot docs mention AppLocker/EDR blocking `claude.exe` children.
- **Locked or replaced executables while sessions run.**
  - WinGet upgrades fail while Claude runs (setup).
  - Native Windows update renames `claude.exe` aside and can leave no `claude.exe` if the move fails (troubleshoot).
  - An npm in-place upgrade deletes the previous binary, so a running session can crash with `Bus error` (setup, "Install on network storage"). The native installer keeps versioned files.
  - This is why "new sessions only", T3's approach, is the safe default.
- **GUI environment ≠ shell environment.** A GUI-launched app may lack the user's PATH. T3 looks for `brew` inside the keg prefix because "GUI servers may lack Homebrew on PATH" (`maintenanceResolver.ts#L670-L674`), and it passes each provider's own PATH and home to the updater (`maintenance.ts#L29-L30`). The Codex `CODEX_HOME` mismatch breaks `codex update` (section 4).
- **False success.** `claude update` on WinGet, Homebrew or apk installs says "up to date" and exits 0. `pnpm add @latest` can install an older release because of `minimumReleaseAge`. Verify by re-probing `--version`, as T3 does, and don't trust the exit code.
- **No progress output** from `claude update`, so a long timeout is required (T3: 30 min). Killing it mid-download risks a truncated binary (T3 runner comment).
- **Version managers (mise, Volta, asdf-style shims).** These are pinned by config. T3 treats mise as manual-only, and Volta has its own command.
- **macOS App Sandbox (inference).** A sandboxed app generally cannot exec user-installed binaries outside its container without entitlements. Hosting user CLIs at all may need a non-sandboxed helper. The sources do not cover this.
