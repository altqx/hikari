# Hikari plugins: LuaJIT extensions with panels and safe access

> **Decided 2026-10-28.** The owner settled the open questions at the end of this note in a grilling session. The decisions are in the wave-8 tracking issue [#410](https://github.com/altqx/hikari/issues/410) (cards PL0–PL9, #412–#421); where they differ from a recommendation here, #410 wins.

This is a reference for the post-wave-7 feature "Hikari plugins" (placeholder card
[#366](https://github.com/altqx/hikari/issues/366)). The owner's request: "we should use luajit for the
plugins; the plugin should behave like an extension of hikari itself; it should be our own new format; it
should allow creating new panels, interacting with the app and files in a secure way." It covers what
HikariSub has today that a plugin system would build on, what LuaJIT can and cannot sandbox, how ten
other extension systems handle manifests, panels, permissions and distribution, and what this means for
a QML application. It ends with design options and the decisions left to the owner.

Sources read:

- The Qt rewrite at **`62446e2b5`** (`origin/qt`, 2026-10-28). Paths such as
  `src/helpers/lua/lua_helper.cpp:836` refer to that commit. The checkout at `/home/altq/Work/hikari` was
  367 commits behind it. The bundled DependencyControl is read from `Automation/automation/Include/l0` at
  the same commit.
- LuaJIT `v2.1` at `a4c064539e` (2026-10-21), the source of the docs at luajit.org. HikariSub pins
  `c6ffc141a8` (2026-09-08) on the same branch, 20 commits older.
- MuseScore at tag `v4.7.5` (`3654226c2e`), the same tag as the earlier
  [musescore-shell.md](musescore-shell.md). Also Aseprite `main` at `b3e34acbae` (2026-10-14), the
  aseprite/api docs at `662b12e`, Zed `main` at `37faef8` (2026-10-27), and Neovim, mpv and OBS Studio
  `master` docs and sources as of 2026-10-28.
- Vendor documentation (VS Code, Obsidian, Blender, Aegisub, Qt, MDN, Microsoft Learn, kernel.org,
  minisign, Sigstore, SemVer) and one lua-l mailing-list message. All web pages were fetched on
  **2026-10-28**.

Terms follow [CONTEXT.md](../../CONTEXT.md): Document, Line, Workspace, Automation macro, Editing target.
In this note, a **plugin** is the new HikariSub format. An **automation script** is an
Aegisub-compatible `.lua`/`.moon` file run by the existing host. A **helper** is a separate process
started by the application.

## 0. Summary

- **HikariSub already has the hard part of the runtime: one helper process per Lua state.** Each
  automation script runs in its own `hikari-lua-helper` process with a persistent LuaJIT state, over a
  versioned, bounded IPC protocol. Macros stage edits that the application validates and applies as one
  undo step (ADR 0006). The process boundary protects the application from crashes and hangs. **It is
  not a security boundary:** the helper opens every standard library, including `io`, `os`, `debug` and
  `ffi`, with the user's full OS rights (`docs/automation.md`, "Process containment is not a security
  sandbox").
- **LuaJIT 2.1 is maintained but only roughly sandboxable.** The v2.1 branch is the recommended
  "Production" branch, with rolling releases and 96 commits so far in 2026. It supports Windows
  x64/ARM64, Linux and macOS, including arm64. The project's own guidance is blunt. The FFI "is **not
  safe for use by untrusted Lua code**". "Loading untrusted bytecode is not safe!" Debug hooks "are not
  called from compiled code". And "the only promising approach is to sandbox Lua code at the process
  level and not the VM level." A Lua-level whitelist reduces what a plugin can reach. Only the process
  boundary, plus OS sandboxing (Landlock on Linux, AppContainer and job objects on Windows), can enforce
  it.
- **Of the systems surveyed, only two enforce permissions in code: Zed (WASM capabilities) and Aseprite
  (Lua prompts).** VS Code, Obsidian, Blender, MuseScore, OBS, Neovim and mpv give extensions the
  application's full rights. Their permissions are declarations, review policy or a trust prompt.
  Blender says so outright: it "cannot prevent third-party add-ons from violating this rule." Aseprite is
  the nearest model to this request: plain Lua with `io.open`, `os.execute`, `package.loadlib` and others
  wrapped in a permission prompt keyed by the calling script.
- **Panels come in three kinds:** declarative data that the host renders (VS Code tree views, OBS
  properties, Aegisub dialogs, Aseprite `Dialog`), a widget or DOM API in the same process (Obsidian,
  Blender `Panel.draw`, OBS docks of `QWidget`s), and isolated web content (VS Code webviews). **QML
  cannot be sandboxed:** Qt says QML "does not have the concept of separate privacy domains" and must
  only load trusted code. MuseScore runs plugin QML in its own `QQmlEngine` for API versioning, not for
  security.
- **HikariSub already renders a declarative UI from a helper.** `AutomationDialog.qml` builds fixed QML
  controls from a plain control list sent over IPC, with "no script-generated QML". That is the
  precedent for plugin panels.
- **Recommendation:** a new `.hikariplugin` zip with a JSON manifest. Each plugin runs in its own
  helper, in a fresh LuaJIT state whose environment is built from a whitelist: no `ffi`, `io`, `os`,
  `debug`, `jit`, `package.loadlib` or bytecode loading. Files are reached through handles the user
  grants in a picker, plus a private data folder. Network access is limited to hosts the manifest
  declares, and the application makes the requests. Panels are a declarative widget tree that HikariSub
  renders with its own QML components. Edits go through the existing staged-transaction path. OS
  sandboxing of the helper comes as a second layer. Packages are signed with minisign and listed in a
  curated index. Plugins that really need FFI or native libraries get an explicit "unrestricted" tier
  behind a full-trust prompt. Details are in §7, and the decisions the owner needs to make are in §8.

## 1. What HikariSub has today

### 1.1 The Lua automation host

- **One process per loaded script.** `src/helpers/lua/lua_helper.cpp:1-4`: "One process per loaded
  script; the script's LuaJIT state lives for the process. The Lua-facing API stays synchronous". ADR
  0006 records the owner's choices: separate process, separate persistent state per script, one helper
  per script, one active macro application-wide, explicit restart with no rerun
  (`docs/adr/0006-isolated-lua-automation-host.md`).
- **The application side.** `LuaScriptHost` owns one helper and its state. If the helper dies, the
  script becomes Unavailable and only an explicit restart brings it back
  (`src/backends/include/hikari/backends/lua_script_host.h:3-10, 27, 55`). `AutomationManager` holds the
  loaded scripts (`src/backends/automation_manager.cpp:106-180`).
- **The IPC layer.** `HelperHost` starts the helper, checks its protocol version before any work,
  routes replies by session and request ID, and bounds outstanding work: 64 requests and 16 MiB by
  default (`src/backends/include/hikari/backends/helper_host.h:1-17, 52-53`). It keeps stderr as
  separate bounded diagnostics. A frame over 64 MiB is a protocol violation
  (`helper_protocol.h:17`). The Lua protocol is version 8, with commands Load, Run, RunValidated and
  Validate (`lua_protocol.h:50-56`).
- **Host services.** These run only while a request is running: "each request carries this script's
  path and the run that asked (0 for the top level while loading)" (`lua_script_host.h:44-47`). There are
  23 fixed services, such as FrameFromMs, Frame, ClipboardGet/Set, OpenFiles, SaveFile, TextExtents and
  Gettext (`src/application/include/hikari/application/automation.h:191-197`). The application fills in
  who asked; the helper never supplies it (`automation.h:201-202`). **The protocol has no channel the
  application can use to push events to an idle helper.** A panel that reacts to selection or Document
  changes needs that channel.
- **Edits.** A macro starts from a snapshot that carries the Document's revision. Its result "is
  validated against that revision and applied atomically as one undo step through the EditSession, or
  rejected without changing anything"
  (`src/application/include/hikari/application/macro_transaction.h:1-6`).
- **Dialogs.** The helper sends a `DialogRequest`: a list of `DialogControl{kind, name, x, y, width,
  height, label, text, …}` plus buttons (`automation.h:41-62`). `AutomationDialog.qml` maps each `kind`
  to a fixed QML component (`src/ui/AutomationDialog.qml:640-651`). `docs/automation.md` (service
  table) records the rule: "App creates fixed QML control types from plain data … no script-generated
  QML."

### 1.2 The LuaJIT build and what scripts can reach

- **The build.** LuaJIT comes from a HikariSub-owned vcpkg overlay that pins commit `c6ffc141a8`
  (2026-09-08) (`ports/luajit/portfile.cmake:4`). It is built with `LUAJIT_ENABLE_LUA52COMPAT` and
  `LUAJIT_DISABLE_BUFFER` (`portfile.cmake:39, 109`), plus a local patch, "no x86 XLOAD fusion across C
  calls" (`ports/luajit/vcpkg.json`, `hikari-xload-fusion-across-calls.patch`). Only the helper links
  it: "Its flags export the executable's symbols (-Wl,-E), which Lua native modules need, so it must not
  be linked into the application" (`cmake/HikariDependencies.cmake:109-115`).
- **The state is not restricted.** `load` creates the state with `luaL_newstate()`, then
  `luaL_openlibs(L)` and the native preloads. It removes only `dofile` and `loadfile`, which are
  replaced by the legacy `include` (`lua_helper.cpp:836-846`). In LuaJIT, `luaL_openlibs` opens base,
  package, table, io, os, string, math, debug, bit and jit, and registers `ffi` in `package.preload`
  (LuaJIT `src/lib_init.c:18-34`). So `require "ffi"` works. The helper uses FFI itself: the clipboard
  goes through FFI function pointers (`lua_helper.cpp:2112`), and `do_register_lib_table` calls
  `require "ffi"` (`src/helpers/lua/lua_native.cpp:47-60`).
- **Module loading.** `package.path` is built from the script folder and the shared include folders.
  Module loader 2 is replaced by the legacy loader, which prefers `.moon` to `.lua`
  (`lua_helper.cpp:757-808`). MoonScript is compiled through `moonscript.loadstring`, kept in the
  registry (`lua_helper.cpp:805-808`).
- **DependencyControl.** It loads native modules through `requireffi`. The build compiles BadMutex,
  PreciseTimer and DownloadManager (libcurl) as shared libraries and stages them under
  `Include/{BM,PT,DM}` (`src/helpers/lua/CMakeLists.txt:36-59, 85-98`). Its update feeds are JSON with
  `dependencyControlFeedFormatVersion`, `knownFeeds` and per-channel `files[]`, each with a `sha1`
  (`Thirdparty/DependencyControl/DependencyControl.json`). Downloads are checked against that SHA-1
  (`l0/DependencyControl/UpdateTask.moon:1026-1033`). Trust is a list of trusted and blocked feed URLs,
  with a `fetchUntrustedFeeds` policy of `always`, `never` or `prompt`
  (`l0/DependencyControl/FeedTrust.moon:14-15, 41-60`). **There are no signatures.**
- **The stance.** `docs/automation.md` says: "Process containment is not a security sandbox.
  Lua/native/FFI filesystem and network access retain ordinary operating-system permissions … No
  additional permission system or restricted scripting API is accepted here." That stance is right for
  Aegisub compatibility. A plugin format can take a different one.

### 1.3 Where a plugin would hook in

| Surface | Today | What a plugin needs |
| --- | --- | --- |
| User folder (UD1) | `hikari.ini`, `automation/{autoload,include}`, `dictionaries/`, `catalogs/`, `presets/`, `autosave/ recovery/ log/`, and a separate cache folder (`src/app/include/hikari/app/user_folder.h:6-17, 45-55`). Bundled files are seeded by a SHA-256 manifest (`user_folder.h:57-82`). | A `plugins/` folder for installed packages, a per-plugin data folder, and a per-plugin cache. |
| Panel registry | A fixed list of 14 ids (`src/ui/workspace_layout.cpp:129-138`) versioned by `kPanelRegistry = 10` (`src/ui/workspace_layout.h:109-111`). A saved layout from a newer registry, or naming an unknown panel, is rejected (`workspace_layout.cpp:491, 497, 541, 596, 637`). Each panel is a `KDDW.DockWidget` written into `Main.qml` (`src/ui/Main.qml:6126-6150`, 15 `uniqueName`s). | Dock widgets created at run time with namespaced ids, and layouts that keep a placeholder for a missing plugin panel instead of being thrown away. |
| Commands and hotkeys | Numeric action ids. Legacy's 243 actions, then the rewrite's own (5169–5176, …), with script bindings from `kFirstScriptHotkey = 30100` (`src/application/include/hikari/application/hotkeys.h:72-89`). Macro bindings are stored by legacy name with the macro name and script SHA-256, and resolve only while that registration still matches (`automation_hotkeys.h:3-7`). | String command ids (`plugin.<id>.<command>`), bound like macro aliases. |
| Settings | A compile-time table of `SettingDefinition{id, legacyKey, type, scope, …}` (`src/application/include/hikari/application/settings.h:36-47`). | Plugin settings declared in the manifest and stored per plugin, outside the static table. |
| Dialogs | DG1 `DialogWindow.qml` is the frame of every dialog: a top-level window with a per-`dialogId` placement and a guide link (`src/ui/DialogWindow.qml:8-35`). Automation dialogs already use one id per script. | Plugin dialogs inside the same frame, with `dialogId = plugin.<id>.<dialog>`. |
| Architecture docs | "does not require … a third-party plugin SDK" (`docs/architecture.md:51`). "The map's exclusion of a general plugin ecosystem" (`architecture.md:57`). "without … a general extension SDK" (`docs/backends.md:60`). | **A new ADR**, since these lines rule out exactly this feature. |

## 2. LuaJIT 2.1 today

### 2.1 Releases, maintenance and platforms

- **Rolling releases.** "LuaJIT uses rolling releases … No release tarballs or binaries are made
  available." The version is `$major.$minor.$timestamp`, the commit time
  ([status](https://luajit.org/status.html)). The repository's `.relver` holds that timestamp.
- **Branches.** v2.0 is "Compatibility only". v2.1 is maintained, takes "limited" new features and is
  recommended for "Production". The next development branch is "(TBA)", and the old `master` "stays
  pinned to the v2.0 branch" ([status](https://luajit.org/status.html)). The v2.1 branch has 96 commits
  in 2026, the latest on 2026-10-21 (`git log v2.1`).
- **Platforms.** Windows 7+ on "x86, x64 or ARM64". macOS 10.4+ from v2.1, with ARM64 supported from
  v2.1 ([status](https://luajit.org/status.html)). MSVC builds are documented for x86, x64 and ARM64,
  including an x64→ARM64 cross build, and macOS builds must set `MACOSX_DEPLOYMENT_TARGET`
  ([install](https://luajit.org/install.html)). For the macOS hardened runtime there is
  `LUAJIT_ENABLE_OSX_HRT`, which uses `MAP_JIT` and `pthread_jit_write_protect_np`
  (`src/lj_mcode.c:117`, `src/lj_ccallback.c:288-333`).
- **Bytecode compatibility.** "Bytecode only stays compatible within a major+minor version" and "is not
  deterministic" ([extensions](https://luajit.org/extensions.html)). A package format should ship
  source, not bytecode.

### 2.2 Why FFI, `load` and bytecode defeat a Lua-level sandbox

- **FFI.** "The FFI library provides no memory safety … you can do anything you can do from C: access
  all memory, overwrite anything in memory". "The FFI library is **not safe for use by untrusted Lua
  code** … you definitely don't want to give this code access to the FFI library or to _any_ cdata
  object" ([FFI semantics](https://luajit.org/ext_ffi_semantics.html)).
- **Bytecode.** `load`, `loadstring` and `loadfile` take a mode; "the default mode string is "bt"",
  which accepts bytecode ([extensions](https://luajit.org/extensions.html)). "Loading untrusted bytecode
  is not safe! It's trivial to crash the Lua or LuaJIT VM with maliciously crafted bytecode … there's
  no bytecode verification on purpose" ([FAQ](https://luajit.org/faq.html)). LuaRocks' September 2026
  incident is a live example: "The rockspec parser only ever expected source code, but it never told
  `loadstring` to reject bytecode" ([LuaRocks notice](https://luarocks.org/security-incident-september-2026)).
- **The general answer.** "Can Lua code be safely sandboxed? Maybe for an extremely restricted subset of
  Lua and if you relentlessly scrutinize every single interface function you offer to the untrusted
  code … there are libraries that are inherently unsafe, e.g. the FFI library … the only promising
  approach is to sandbox Lua code at the process level and not the VM level"
  ([FAQ](https://luajit.org/faq.html)).
- **Resource exhaustion.** Mike Pall, lua-l, 2011-02-21: "string.find, string.rep and other C functions
  can be abused and will either run forever or allocate unlimited amounts of memory. This is true for
  both Lua and LuaJIT. The only reasonably safe way to handle untrusted Lua scripts is to isolate them in
  a process context and to make use of per-process quotas/limits provided by the operating system"
  ([lua-l msg01106](http://lua-users.org/lists/lua-l/2011-02/msg01106.html)).

### 2.3 Sandboxing tools that do exist

| Tool | What it does | Limit |
| --- | --- | --- |
| A separate `lua_State` per plugin | No shared globals. HikariSub already does this, in a separate process. | Nothing stops an exposed native call from reaching shared process state. |
| An environment whitelist (`setfenv` / a custom `_ENV` table for each chunk) | Only named functions are reachable. Leave out `io`, `os`, `debug`, `jit`, `package.loadlib`, `package.preload.ffi`, `string.dump`, `collectgarbage` beyond `"count"`, `getfenv`/`setfenv`, `newproxy`, and raw module loaders. | The FAQ warns this is "very hard to get … right even for the Lua core libraries" ([FAQ](https://luajit.org/faq.html)). It reduces exposure; it is not a boundary. |
| Text-only loading | Wrap `load` so it always passes mode `"t"` and the plugin's env ([extensions](https://luajit.org/extensions.html)). Read modules only from inside the package. | — |
| Build without FFI | `-DLUAJIT_DISABLE_FFI` (`src/Makefile:95`). | HikariSub needs FFI for automation, so this means a second LuaJIT build. Leaving `ffi` out of the environment and clearing `package.preload.ffi` achieves the same per state. |
| Instruction-count hooks | `lua_sethook` with a count. | "Debug hooks are not called from compiled code" (lua-l above; the FAQ's Ctrl-C entry). `LUAJIT_ENABLE_CHECKHOOK` makes traces check for hooks, but "is disabled by default, since the hook checks may be quite expensive in tight loops" (`src/lj_record.c:2949-2962`). Turning the JIT off (`luaJIT_setmode`) also works, at the cost of speed. |
| A memory cap through the allocator | `lua_newstate(allocf, ud)` with a counting allocator. On 64-bit it is available with GC64, the default; only 64-bit builds without GC64 replace it (`src/lj_state.c:246-250`; [install](https://luajit.org/install.html): "All LuaJIT 64 bit ports use 64 bit GC objects by default"). | It catches `string.rep` blow-ups only if the allocator refuses. Native modules allocate outside it. |
| VM hardening flags | `LUAJIT_SECURITY_PRNG/STRHASH/STRID/MCODE`, all secure by default (`src/lj_arch.h:736-768`). | These harden the VM against hash flooding and similar attacks. They are not a sandbox. |
| A process with OS limits | A wall-clock watchdog plus a kill (already in place: Force stop after 3 s, `docs/automation.md`), job-object or cgroup memory caps, and an OS filesystem and network sandbox (§5.3). | This is the only enforcing layer, per the LuaJIT FAQ. |

## 3. Prior art

### 3.1 Comparison

| System | Code | Manifest | Panels / UI | Permission model | Distribution | Versioning |
| --- | --- | --- | --- | --- | --- | --- |
| **VS Code** | JS/TS in the extension host | `package.json`: `engines.vscode` (semver range, "Cannot be `*`"), `activationEvents`, `contributes`, `extensionDependencies` ([manifest](https://code.visualstudio.com/api/references/extension-manifest)) | Declarative contributions plus data providers. A view is contributed in `contributes.views` and filled by a `TreeDataProvider`; VS Code renders it "in the style of the built-in views" ([tree view](https://code.visualstudio.com/api/extension-guides/tree-view)). Webviews are an `iframe` reached by `postMessage`, with scripts off by default and a recommended CSP, "used sparingly and only when VS Code's native API is inadequate" ([webview](https://code.visualstudio.com/api/extension-guides/webview)). The host prevents extensions from "Modifying the UI" ([extension host](https://code.visualstudio.com/api/advanced-topics/extension-host)). | None at run time: "The extension host has the same permissions as VS Code itself" ([runtime security](https://code.visualstudio.com/docs/configure/extensions/extension-runtime-security)). Workspace Trust only turns extensions off or limits them in Restricted Mode, and the extension author does the gating ([workspace trust](https://code.visualstudio.com/api/extension-guides/workspace-trust)). A publisher-trust dialog since 1.97. | The Marketplace runs a malware scan and signs every package; VS Code checks the signature at install. Publisher verification by domain ([runtime security](https://code.visualstudio.com/docs/configure/extensions/extension-runtime-security)). | `engines.vscode` semver. Lazy activation: "activate only when a user needs your extension" ([activation](https://code.visualstudio.com/api/references/activation-events)). |
| **Zed** | Rust → WASM (`wasm32-wasip2`) | `extension.toml`: `id`, `name`, `version`, `schema_version`, `authors`, `repository` ([developing](https://zed.dev/docs/extensions/developing-extensions)) | **None.** The WIT interface (`crates/extension_api/wit/since_v0.8.0/`) covers languages, LSP, DAP, context servers, slash commands, HTTP, process and settings, with no panel or view interface. | **Enforced.** Capabilities `process:exec`, `download_file` and `npm:install`, scoped by command, host or package in `granted_extension_capabilities`; a missing one makes the API call return an error ([capabilities](https://zed.dev/docs/extensions/capabilities)). `download-file` writes only "within the extension's working directory" (`extension.wit:52-57`). | A PR to `zed-industries/extensions` adding a git submodule ([publishing guide](https://github.com/zed-industries/zed/blob/main/docs/src/extensions/publishing/publishing-guide.md):40-45). A licence has been required since 2025-10-01. Prerequisites review. | A versioned WIT world per release (`since_v0.0.1` … `since_v0.8.0`). |
| **Neovim** | Lua (LuaJIT) in the editor process | No manifest. The layout is `plugin/*.lua` (run at start) and `lua/` (loaded on `require`) (`:help lua-plugin`) | Buffers the plugin "owns", `<Plug>` mappings, or an in-process LSP server for actions (`lua-plugin.txt:270-293`). | None for plugins. `vim.secure.read()` keeps a trust database for project-local files only (`lua.txt:5211-5220`). | Git. `vim.pack` (experimental) expects semver tags and keeps a JSON lockfile (`pack.txt:208-230`). Plugins can also be published to LuaRocks. | SemVer recommended (`lua-plugin.txt:322-335`). The RPC API has an explicit contract: "Function signatures will NOT CHANGE after release", and only optional fields are added (`api.txt:235-262`). |
| **Obsidian** | JS in the Electron renderer | `manifest.json`: `id`, `name`, `version` (semver), `minAppVersion`, `description`, `author`, `isDesktopOnly` ([manifest](https://docs.obsidian.md/Reference/Manifest)) | A DOM API: subclass `ItemView`, register it with `registerView`, and build `contentEl` in `onOpen` ([views](https://docs.obsidian.md/Plugins/User+interface/Views)). | None: "Obsidian cannot reliably restrict plugins to specific permissions … plugins will inherit Obsidian's access levels". Community plugins "can access files", "connect to internet", "install additional programs". Restricted mode turns all of them off ([plugin security](https://obsidian.md/help/Extending+Obsidian/Plugin+security)). | A community directory with automatic scans of every version, and manual review for popular or flagged plugins (same page). | `minAppVersion` plus semver. |
| **Blender** | Python in-process | `blender_manifest.toml`: `schema_version`, `id`, `version` (semver), `type`, `blender_version_min/max`, `license`, `platforms`, `wheels`, `[permissions]` ([schema 1.0.0](https://developer.blender.org/docs/features/extensions/schema/1.0.0/)) | A Python widget API ([`bpy.types.Panel`](https://docs.blender.org/api/current/bpy.types.Panel.html): `draw(context)` fills a `UILayout`). | **Declarative only.** Permissions are `files`, `network`, `clipboard`, `camera` and `microphone`, each with a reason. For network the add-on itself must check `bpy.app.online_access`, and "Blender cannot prevent third-party add-ons from violating this rule" ([add-ons](https://docs.blender.org/manual/en/latest/advanced/extensions/addons.html)). Python "does not restrict what a script can do" ([security](https://docs.blender.org/manual/en/latest/advanced/scripting/security.html)). | extensions.blender.org with review. A per-extension user folder that survives upgrades (`bpy.utils.extension_path_user`) ([add-ons](https://docs.blender.org/manual/en/latest/advanced/extensions/addons.html)). | `blender_version_min/max`. Repository-qualified module names (`bl_ext.<repo>.<id>`). |
| **OBS Studio** | Native modules (C/C++); Python 3 and "Luajit 2" scripts | No manifest. `OBS_DECLARE_MODULE`, `obs_module_load` | Native: `obs_frontend_add_dock_by_id(id, title, QWidget*)` (`docs/sphinx/reference-frontend-api.rst:469-475`). Scripts: `script_properties()` returns an `obs_properties_t` that OBS renders ([scripting](https://github.com/obsproject/obs-studio/blob/master/docs/sphinx/scripting.rst):79-86). | None (native code in-process). | Manual install. | Tied to the libobs ABI. |
| **MuseScore 4** | QML/JS | A zip with `manifest.json`: `uri`, `type` (`form` = UI, `macros` = no UI, `composite`), `title`, `version`, `apiversion` (default 2), `actions[]` with `ui_context`, `show_on_toolbar` (`src/framework/extensions/extensionstypes.h:87-92, 190-201`; `internal/extensionsloader.cpp:140-197`) | Plugin QML runs in a separate `QQmlEngine` per API version, with an `api` global (`internal/extensionsuiengine.cpp:44-110`). For API v1: "Old plugins could use standard modules … we need to think about how to limit this" (`extensionsuiengine.cpp:105-106`). | None. | Zip unpacked with no signature check (`internal/extensioninstaller.cpp:29-111`). | `apiversion` picks the engine. |
| **Aseprite** | Lua in-process (one engine) | A zip renamed `.aseprite-extension` with `package.json` (`contributes.scripts`, …). `init(plugin)` / `exit(plugin)`, `plugin:newCommand`, `plugin.preferences` ([plugin](https://www.aseprite.org/api/plugin)) | The `Dialog` API: widgets by method chaining, `data`, `modify`, `canvas` with an `onpaint` `GraphicsContext`, and non-blocking `show{wait=false}` ([dialog](https://www.aseprite.org/api/dialog)). The host renders it. | **Enforced prompts.** `overwrite_unsecure_functions` replaces `io.open/popen/lines/input/output`, `os.execute/remove/rename` and `package.loadlib` with wrappers that call `ask_access`, and turns `os.exit` and `os.tmpname` into errors (`src/app/script/security.cpp:37-73, 276-303`). The grant is stored per script key (the SHA-1 of the calling chunk's file name, from `lua_getstack`), per mode, or as "Full" access (`security.cpp:76-104, 306-436`). With no UI it allows everything (`security.cpp:313, 435`). The clipboard also prompts ([app.clipboard](https://github.com/aseprite/api/blob/main/api/app_clipboard.md)). | Manual install. | `app.apiVersion` (39 in v1.3.17). A security fix: "a script could gain already given access from other script" ([Changes.md](https://github.com/aseprite/api/blob/main/Changes.md):25-30). |
| **Aegisub automation 4** | Lua (LuaJIT in current builds) | The globals `script_name`, `script_description`, `script_author` and `script_version` ("freeform; no specific meaning") ([registration](https://aegisub.org/docs/latest/automation/lua/registration/)) | `aegisub.dialog.display(dialog, buttons, button_ids)`: "an array of Dialog Control tables" on a grid, with classes `label`, `edit`, `intedit`, `floatedit`, `textbox`, `dropdown`, `checkbox`, `color`, `coloralpha` and `alpha`; it "waits for it to close". `aegisub.dialog.open/save` pickers ([dialogs](https://aegisub.org/docs/latest/automation/lua/dialogs/)). | None. | DependencyControl feeds (§1.2): JSON, SHA-1 per file, trusted/blocked feed lists, no signatures. | Feed `version` per channel; `SemanticVersion` in DepCtrl. |
| **mpv** | Lua (and JS); "Each script runs in its own thread" with its own `luaL_newstate` (`DOCS/man/lua.rst:78-81`; `player/lua.c:445`) | No manifest. A script directory with `main.lua`, and `mp.get_script_directory()` (`lua.rst:47-71`) | No panels: OSD overlays (`mp.create_osd_overlay`, ASS text) (`lua.rst:629-640`). | None; `luaL_openlibs` (`player/lua.c:380`). | Manual. | — |

### 3.2 What the survey shows

- **Enforcement is rare, and both enforcing systems drew the boundary at a narrow API.** Zed's WASM
  guest has no ambient rights, so its capability list is complete by construction. Aseprite's prompts
  wrap the dangerous Lua functions but leave `debug`, `load` and the rest of the state open in a single
  in-process engine. Its own changelog fixed a script gaining another script's grant, and identifying
  the caller through debug info is exactly what an untrusted script can influence. A HikariSub design
  should take Zed's boundary (nothing ambient, only host calls) and Aseprite's user experience (a prompt
  naming the file and the operation, with "remember").
- **Declarative panels are the norm when the host wants the extension to look native.** VS Code
  separates data (tree providers) from rendering, and webviews are a discouraged escape hatch. OBS
  scripts get properties that OBS renders. Aegisub, and HikariSub's own `AutomationDialog.qml`, already
  render a control list. Systems that hand over a widget or DOM API (Obsidian, Blender, OBS native) are
  exactly those with no sandbox.
- **Every manifest has the same core:** an id, a semver version, a host-version range, entry points,
  and contributions (commands, menus, views, settings). Blender and Obsidian add platform or desktop-only
  flags. Blender adds per-permission reasons that the store shows to users.
- **Distribution:** curated registries (VS Code, Zed, Blender, Obsidian) with review or scanning;
  signing only at VS Code (by the marketplace). DependencyControl is the outlier: any URL can be a feed,
  trust is per feed URL, and integrity is SHA-1 only.

## 4. UI from a sandboxed LuaJIT plugin in a QML app

### 4.1 Plugins shipping QML: not sandboxable

- "Qt Qml assumes that the application developer provides all code and resources … QML does not have
  the concept of separate privacy domains. This means any code, whether loaded remotely or locally, can
  access all data in its scope" ([Handling Untrusted Data](https://doc.qt.io/qt-6/untrusteddata.html)).
- "The QML engine will not prevent particular resources from being loaded … Do not, for example, use
  import, Loader or XMLHttpRequest to load any untrusted code or content"
  ([Network transparency](https://doc.qt.io/qt-6/qtqml-documents-networktransparency.html)).
- Import paths and plugin paths are trust declarations, not filters: "Qt assumes that all files and
  folders under path come from a trusted source" (`addImportPath`, `setPluginPathList` in
  [QQmlEngine](https://doc.qt.io/qt-6/qqmlengine.html)). `QQmlAbstractUrlInterceptor` rewrites URLs. It
  is documented for "selecting different graphical assets for the current platform", with no security
  role ([QQmlAbstractUrlInterceptor](https://doc.qt.io/qt-6/qqmlabstracturlinterceptor.html)).
  Statically registered types, such as Qt Quick, Qt Core's `Settings`, dialogs, and every module
  HikariSub registers, are importable by any QML the engine loads.
- `QJSEngine` (without QML) can be interrupted from another thread with `setInterrupted(true)`. Its docs
  name no memory cap or sandbox ([QJSEngine](https://doc.qt.io/qt-6/qjsengine.html)).
- MuseScore confirms this in practice: a separate `QQmlEngine` per API version keeps API levels apart,
  and its own comment admits old plugins can import standard modules
  (`extensionsuiengine.cpp:105-106`).

**Conclusion:** QML from a plugin would run inside the application process with the application's
rights. It is a trusted-code channel, comparable to OBS native docks.

### 4.2 A declarative widget tree rendered by HikariSub (recommended)

The helper sends a tree of plain values. HikariSub maps each node `kind` to its own QML component, as
`AutomationDialog.qml:640-651` already does for dialog controls. Events (click, edit, selection) go back
as messages carrying the node `id`. The plugin answers with patches (`set{id, prop, value}`) or a new
tree.

- **Widget set.** Label, text field, number, checkbox, dropdown, colour (reusing the AD1 controls),
  button, icon button (the K1 icon roles only), list and tree rows (virtualised, with data paged on
  request, like VS Code's `getChildren`), tabs, a splitter, a progress bar, and a canvas taking a draw
  list (rects, lines, paths, text, images from the plugin package; compare Aseprite's `canvas` with
  `GraphicsContext`).
- **What it gives.** Plugin panels look like HikariSub panels and follow the theme, icons,
  accessibility names, high contrast and DG1 behaviour for free. Translations stay possible: the
  manifest ships its own strings. The professional-polish rules are enforced by construction:
  icon-only tool strips with hover names, no debug clutter (the owner's UI polish rule).
- **Cost.** Every widget is HikariSub API that must stay stable. Rich custom drawing, such as a
  waveform-like view, goes through the canvas draw list at IPC cost. That is acceptable for 2D
  overlays, not for per-frame video work.

### 4.3 Remote UI over IPC: a separate window per plugin

The plugin process opens its own window: a webview, or its own toolkit. It cannot become a dock panel
without embedding another process's window, which HikariSub does not do and Wayland does not offer. It would
also need a GUI toolkit inside the helper, which defeats the narrow-API boundary. **Not recommended**
except as the "unrestricted" tier's own business.

### 4.4 Webview panels (the VS Code model)

A `QtWebEngine` view with a CSP and `postMessage` would isolate the panel's code in Chromium's renderer
sandbox. It adds Chromium to every package (Qt WebEngine is not among the packaged Qt modules today,
`docs/distribution.md:94`). The UI would not look native, and plugins would be
written in two languages. VS Code itself calls webviews "resource heavy". **Not recommended** for v1.

## 5. Safe file, network and OS access

### 5.1 Files as capabilities

- **The browser model.** A page gets a `FileSystemFileHandle` or `FileSystemDirectoryHandle` only from
  a picker (`showOpenFilePicker`, `showDirectoryPicker`, `showSaveFilePicker`) or drag and drop. "Access
  to file/directory data is disallowed unless the user specifically permits it", and the origin private
  file system is a per-origin store invisible to the user
  ([File System API](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API)).
  `requestPermission({mode: 'read'|'readwrite'})` needs "transient user activation"
  ([requestPermission](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemHandle/requestPermission)).
- **The Aseprite model.** The plugin passes a path, and the host asks "allow read/write/execute of X?",
  with "remember" stored per script (`security.cpp:306-436`).
- **For HikariSub.** The helper already asks the application for pickers (`OpenFiles`, `SaveFile`,
  `automation.h:193`). The plugin variant returns an opaque handle id instead of a path, with read or
  write recorded. Further calls (`file.read(handle)`, `file.write(handle, bytes)`, `dir.list(handle)`)
  are host services that check the handle. The editing target and open Documents are reached through
  the Document API, never through paths. Every plugin gets `data/` and `cache/` folders under the user
  folder that it may use freely (compare Blender's `extension_path_user`, which "will be kept between
  upgrades but will be removed if the extension is uninstalled"). Handles can be kept across sessions
  only if the user ticks "remember" (Aseprite's grant). The grant is stored in the plugin's record and
  revocable in the Plugins page.

### 5.2 Network

- Zed scopes `download_file` by host ([capabilities](https://zed.dev/docs/extensions/capabilities)).
  Blender only asks add-ons to honour a switch. Qt routes all QML network access through one
  `QNetworkAccessManager` factory ([QQmlEngine](https://doc.qt.io/qt-6/qqmlengine.html)).
- **For HikariSub:** the manifest declares hosts (`"network": ["api.example.com"]`), which are shown at
  install. The plugin calls `http.request{url, method, headers, body}`, a host service run by the
  application's `QNetworkAccessManager`. It checks the host against the grant, enforces HTTPS, size and
  time limits, and respects one global "Allow plugins to use the internet" setting (Blender's
  `online_access`). The helper itself needs no sockets, which is what makes §5.3 possible.

### 5.3 OS sandboxing of the helper (defence in depth)

- **Linux: Landlock.** "Landlock empowers any process, including unprivileged ones, to securely
  restrict themselves." It covers filesystem rights, TCP bind and connect (ABI v4+), and abstract-socket
  and signal scoping (ABI v6+). "Once a thread is landlocked, there is no way to remove its security
  policy", and callers should use "a best-effort security approach by checking the Landlock ABI version
  at runtime" ([Landlock](https://docs.kernel.org/userspace-api/landlock.html)). A plugin helper could
  landlock itself after loading its package. It would allow reads of its package and LuaJIT's files,
  read and write of its own `data/` and `cache/`, and nothing else, with no TCP.
- **Windows: AppContainer and job objects.** AppContainer isolates files, registry, network, devices,
  windows and credentials, with "Read-write access … granted to specific persistent files"
  ([AppContainer isolation](https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation)).
  Job objects enforce working-set and CPU-time limits, and can kill the whole tree with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` ([Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)).
- **macOS** is a possible later platform (ADR 0004). Its app sandbox would need its own design.
- **Scope.** None of this is in place today, and the automation host must not get it: Aegisub scripts
  rely on `io` and FFI (ADR 0006). It is plugin-helper-only work, with native evidence on both platforms
  under the existing evidence rules (ADR 0015).

## 6. Package format options

### 6.1 Container and manifest

| Option | For | Against |
| --- | --- | --- |
| **A zip with `hikari-plugin.json`** (`.hikariplugin`) | Qt reads JSON natively (`QJsonDocument`, already used for layouts and update manifests). The same format as Obsidian, VS Code (`package.json`) and Aseprite. A JSON Schema can validate it. | No comments in hand-written files. |
| A zip with `hikari-plugin.toml` | Easy to edit by hand (Zed, Blender). | Adds a TOML parser dependency to the application. |
| A folder in `plugins/` (unpacked) | Easy to develop: load from a folder in a developer mode, as Zed's "install dev extension" does. | Not a distribution format. |

The suggested manifest fields, drawn from §3: `id` (reverse-DNS, lowercase), `name`, `version` (semver),
`hikari` (a semver range of the plugin API, as `engines.vscode`), `main` (a Lua file), `description`,
`authors`, `license` (SPDX), `homepage`, `platforms` (only for the unrestricted tier's native
libraries), `permissions` (each with a reason string, as in Blender), `contributes` (`commands`,
`menus`, `panels`, `settings`, `keybindings`) and `activation` (`onStartup`, `onCommand`, `onPanel`).
Keep `contributes` declarative, so HikariSub can show menus and panels before the plugin runs and start
the helper lazily (VS Code's activation events; one process per plugin makes laziness worth it).

### 6.2 Signatures

- **minisign.** Ed25519 over a BLAKE2b-512 prehash, with a "trusted comment" that "cannot be modified
  without the secret key" ([minisign](https://jedisct1.github.io/minisign/)). The format is small enough
  to verify without libsodium; any Ed25519 implementation and BLAKE2b suffice. Author keys can be pinned
  on first install (trust on first use), and the registry can co-sign.
- **Sigstore.** Keyless: an OIDC identity, a short-lived Fulcio certificate and a Rekor transparency log
  ([overview](https://docs.sigstore.dev/about/overview/)). Verification needs Sigstore's trust root and,
  for full assurance, online log proofs. That is a heavy dependency for a desktop editor that also works
  offline.
- **HikariSub today** signs nothing and relies on SHA-256 from GitHub over HTTPS for its own updates
  (ADR 0019: "Nothing is code-signed"). A plugin signature is a separate decision, and a cheap one with
  minisign.

### 6.3 Registry and updates

- **A DependencyControl-style feed:** any URL; trust per feed URL; per-file SHA-1; a channel per version
  (§1.2). It is flexible, but there is no central review, and the hash is SHA-1.
- **A curated index (Zed, Blender, Obsidian):** one repository that lists `{id, version, url, sha256,
  minisign signature, hikari range, permissions}`. Additions go through a PR with review. HikariSub
  reads the index over HTTPS, as the update checker reads release manifests (`src/app/update_checker.cpp`).
  Sideloading a `.hikariplugin` file stays possible, behind an "unverified source" warning.
- **Updates:** check the index on the update check's schedule. Show changed permissions before
  updating; a new permission needs fresh consent, as browser extensions do.

### 6.4 API versioning

- SemVer: "Major version zero (0.y.z) is for initial development. Anything MAY change at any time".
  Major "MUST be incremented if any backward incompatible changes are introduced to the public API"
  ([semver.org](https://semver.org/)).
- Neovim's contract is a good template for the IPC API: names never change after release, maps gain
  only optional fields, and an `opts` parameter may be added (`api.txt:235-262`). The helper protocol's
  handshake already refuses incompatible versions (`helper_host.h:1-5`). A plugin declares `"hikari":
  "^1.2"` and is refused, with a clear message, when the application's plugin API does not satisfy it.

## 7. Design options for HikariSub

All four options keep the Aegisub automation host as it is (ADR 0006). Plugins are a second, separate
runtime that reuses `HelperHost`, the IPC framing and the staged-transaction path.

### A. Isolated plugins with a narrow API and declarative UI (recommended core)

- **Runtime.** One helper per enabled plugin (`hikari-plugin-helper`, a sibling of the Lua helper,
  sharing `hikari_helper_endpoint`). It runs a fresh LuaJIT state from `lua_newstate` with a counting
  allocator. The plugin's environment is built from a whitelist: `string` without `dump`, `table`,
  `math`, `bit`, `coroutine`, text-only `load` bound to the env, and `require` limited to
  the package's own `lua/` folder. There is no `ffi`, `io`, `os`, `debug`, `jit` or `package.loadlib`.
  The JIT stays on, and a wall-clock watchdog with the existing Force stop handles runaway code.
- **API.** A `hikari` global, versioned as a whole: `commands`, `document` (read the snapshot, stage
  edits, apply as one undo step through `MacroTransaction`), `selection`, `video`/`audio` read services
  (the existing `HostService` set), `ui` (panels and dialogs as declarative trees), `files` (handles from
  pickers, plus `data`/`cache`), `http` (declared hosts), `settings` (from the manifest), `notify`
  (toasts and the Log panel), and `i18n`.
- **Events.** A new protocol direction lets the application push events to the helper:
  `selectionChanged`, `documentChanged{revision}`, `panelEvent{id}`, `commandInvoked`. Handlers run one
  at a time per plugin. Edits still go through one application-wide transaction queue, so ADR 0006's
  "one active macro" becomes "one applying transaction".
- **Security.** The narrow API plus process isolation, then Landlock or AppContainer and job objects in
  a second step (§5.3).
- **Trade-offs.** This is the most work: a new API surface, a widget set, event plumbing, dynamic docks
  and a layout placeholder for missing panels. Plugins cannot reuse DependencyControl modules or FFI
  libraries (Yutils, ASSFoundation through `requireffi`). Lua-level restrictions are not a boundary on
  their own (§2.2), so the claim "secure" rests on the process plus the OS sandbox. It is also the only
  option that meets "in a secure way" honestly.

### B. Trusted plugins: full LuaJIT, declared permissions (the Blender/Obsidian model)

- Same package, manifest, helper per plugin and declarative panels. The state is opened as automation's
  is, with `ffi`, `io` and `os`, and permissions are only shown at install.
- **Trade-offs.** Quick to build, maximally powerful, and DependencyControl-style libraries work.
  "Secure" would be untrue: Blender's own docs concede the declarations cannot be enforced. A malicious
  or careless plugin has the user's full rights. Crash isolation still holds.

### C. Tiered: A by default, B as "unrestricted" behind full trust (recommended overall)

- The manifest asks for `"tier": "unrestricted"` (or for permissions such as `native`/`ffi`,
  `filesystem`, `process`). Install then shows a full-trust prompt modelled on Aseprite's "Give full
  trust to this script" and VS Code's publisher-trust dialog. The curated index flags unrestricted
  plugins. Their helpers get the full library set and no OS sandbox.
- **Trade-offs.** Two documented behaviours to test, and some users will click through. Restricted
  plugins get real guarantees, and the escape hatch is explicit instead of silent. Over time, a common
  FFI need can become a host API (for example SubInspector-like text metrics are already `TextExtents`).

### D. QML-shipping plugins in a separate engine (the MuseScore model)

- A plugin ships QML that HikariSub loads into its own `QQmlEngine`, with an `api` object, as MuseScore
  does.
- **Trade-offs.** Rich, native-looking UI, little HikariSub widget work, and the panels can do anything
  QML can. It runs in the application process, with no sandbox possible (§4.1). A plugin crash takes
  the editor down, and plugins are written in QML/JS, not LuaJIT, against the owner's first
  requirement. **Not recommended**, except perhaps later for first-party bundled extensions.

## 8. Open questions for the owner

1. **Can a plugin use FFI or native libraries?**
   *Recommended:* Not by default. Offer the unrestricted tier (option C) with a full-trust prompt. Make
   restricted the default, and the only tier the curated index lists without a warning badge.
2. **How do plugins relate to automation scripts and DependencyControl?**
   *Recommended:* Keep them separate. Automation stays the Aegisub-compatible path (ADR 0006,
   unchanged). Plugins get their own `hikari` API, with no `aegisub` global and no `requireffi`. A
   plugin may contribute commands that appear beside macros in menus and the shortcut editor, but it
   has no access to automation's scripts.
3. **Should panels be declarative only, or may plugins ship QML?**
   *Recommended:* Declarative widget tree only (§4.2), including a canvas draw list. No plugin QML.
   Revisit QML only for first-party bundled extensions.
4. **One helper per plugin, or one shared plugin helper?**
   *Recommended:* One per plugin, the same as ADR 0006, for crash containment and per-plugin OS
   sandboxes. Start helpers lazily on activation events, and measure the per-process cost (A33-resources
   still has to measure it for automation).
5. **Concurrency: live panels versus "one active macro application-wide"?**
   *Recommended:* Plugins handle events concurrently (each in its own process), but at most one staged
   transaction applies at a time, application-wide. The target Document stays read-only only while that
   transaction applies, not while a panel is open.
6. **What does a plugin see of a Document?**
   *Recommended:* The editing target and its selection by default, with the same snapshot and revision
   checks as macros. Other open Documents only with a `documents.all` permission. A protected reference
   is read-only, as for tools.
7. **Files: handles only, or paths with prompts?**
   *Recommended:* Handles from pickers and drag and drop, plus the plugin's own `data/` and `cache/`.
   Offer a "remember for this plugin" grant for a chosen folder. No raw paths in the restricted tier.
8. **Network: allowed or not, and how scoped?**
   *Recommended:* Allowed only to hosts declared in the manifest, shown at install, HTTPS only, made by
   the application, and switched off by one global setting. No sockets in the helper.
9. **Manifest syntax: JSON or TOML?**
   *Recommended:* JSON (`hikari-plugin.json`) with a published JSON Schema. Qt-native, no new
   dependency. The extension: `.hikariplugin` (a zip).
10. **Distribution: curated index, open feeds, or both?**
    *Recommended:* One curated index repository (PR review, as Zed does), plus sideloading of
    `.hikariplugin` files with an "unverified" warning. No DependencyControl-style arbitrary feeds in v1.
11. **Signing?**
    *Recommended:* minisign. The author signs; the index records the author's key and HikariSub pins it
    at first install; an update signed by another key is refused unless the user re-trusts it. A
    registry co-signature is optional later. No Sigstore.
12. **API versioning and stability?**
    *Recommended:* Plugin API `0.x` through the beta, where anything may change (SemVer rule 4), then
    `1.0` with a Neovim-style contract: add only, optional fields, no renames. The manifest's `hikari`
    range is checked at load.
13. **OS sandbox in v1 or later?**
    *Recommended:* Ship v1 with the narrow API and process isolation, and label it plainly ("plugins
    cannot reach your files or the network except through HikariSub"). Add Landlock and
    AppContainer/job objects as v1.x hardening, with native evidence on both platforms. Do not promise
    "sandboxed" until that lands.
14. **Where do plugins live in the user folder?**
    *Recommended:* `plugins/<id>/` (the unpacked package, replaced on update), `plugin-data/<id>/` (kept
    across updates, removed on uninstall after asking), and `<cache>/plugins/<id>/`. Grants and
    enabled state go in `plugins.json` in the user folder, not in `hikari.ini`.
15. **Panel ids and layouts?**
    *Recommended:* Panel ids `plugin:<plugin-id>/<panel-id>`, outside the `kPanelRegistry` numbering.
    A saved layout keeps a missing plugin's panel as a placeholder ("Plugin X is not installed") instead
    of being rejected (today `workspace_layout.cpp:497` rejects unknown ids).
16. **Commands and shortcuts?**
    *Recommended:* String ids `plugin.<id>.<command>`, stored like macro bindings (with the plugin id
    and version), in the shortcut editor under a "Plugins" filter. No numeric id range.
17. **Which ADR and card sequence?**
    *Recommended:* A new ADR "HikariSub plugins" that supersedes `architecture.md:51, 57` and
    `backends.md:60` for plugins only. Then cards in this order: (1) manifest, package, install and
    signature; (2) plugin helper, sandboxed state and API skeleton (commands, notify, settings);
    (3) Document API on `MacroTransaction`; (4) declarative panels and dynamic docks with layout
    placeholders; (5) files and network services; (6) the curated index and updates; (7) the OS sandbox;
    (8) the unrestricted tier.
