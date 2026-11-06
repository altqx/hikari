# An OS sandbox for the AI sidecar (SB1)

Research for card SB1 ([#651](https://github.com/altqx/hikari/issues/651)). [ADR 0026](../adr/0026-hikari-ai-runs-in-a-typescript-sidecar-on-bun.md)
says "Bun has no permission model", so the sidecar's egress rule "isn't a security boundary", and that a real
limit "needs Landlock on Linux or an AppContainer on Windows". The card asks for that limit on the sidecar and the
children it starts. It also asks for owner decisions on the agent CLIs (`claude` and `codex` for
[AI15 #443](https://github.com/altqx/hikari/issues/443)) and on how far the network limits go on Windows. The
questions are in §8. Until the owner answers, this note is facts, not a decision.

Sources were read on **2026-11-04**:

- Linux kernel `v7.3` and `v7.4-rc1` (the UAPI header, `security/landlock/`, the docs on docs.kernel.org) and
  man-pages 6.19 on man7.org. The newest stable kernel is 7.3.1.
- [containers/bubblewrap][bwrap] at v0.12.0 (2026-08-26), Ubuntu's release notes and AppArmor packaging.
- Microsoft Learn (Win32 security, WFP, capability declarations) and Chromium's [sandbox design][cr-sandbox].
- Bun at tag `bun-v1.4.4` (2026-10-22), docs and source.
- [openai/codex][codex] at `f39e45e4a68d`, [anthropic-experimental/sandbox-runtime][srt] at `98a4016d2760`
  (npm `@anthropic-ai/sandbox-runtime` 0.0.81), [zed-industries/zed][zed] at `dd3437011469`,
  [anomalyco/opencode][oc] at `27dd925604ed` and [google-gemini/gemini-cli][gem] at `89e9d25d67e7`.
  Paths such as `codex:codex-rs/linux-sandbox/README.md:47` are under those commits.
- `@anthropic-ai/claude-agent-sdk` 0.3.318 (`npm pack`).

Where a source says nothing, this note says so. "Inference" marks a conclusion no source states.

## 0. Summary

- **What the sidecar needs is small.** It is one executable, it has a private working folder, and it is the
  only user of `ai/vendor/`. It talks to Qt on stdio and reaches a short list of HTTPS endpoints. ADR 0026 already
  says it "never opens a subtitle, a video or a user file". So it can be confined much more tightly than a
  coding agent can.
- **Per-host network limits can't be had from the OS without privileges, on either platform.**
  - Landlock limits TCP by port only, and only from ABI 4 (kernel 6.7).
  - A network namespace needs unprivileged user namespaces, which Ubuntu 24.04 and 26.04 restrict through
    AppArmor. An AppImage can't install the profile that lifts that.
  - An AppContainer's network is either nothing, "the Internet", or "home and work networks". Loopback to the
    parent is blocked. WFP filters need admin.
- **Every tool we compared limits network the same way:** no direct route out of the sandbox, plus a proxy outside
  it that checks each host (Codex, Claude Code's sandbox runtime, Zed).
- **Recommendation: the sidecar gets no sockets at all.** Its custom `fetch` sends each request to Qt over the
  existing link, and Qt makes the connection after checking the provider allowlist.
  - This works the same on Linux (seccomp denies `socket()`) and Windows (an AppContainer with no capabilities).
  - It needs no admin, no user namespaces and no new kernel.
  - It closes the D-Bus and keyring hole that Landlock leaves open below kernel 7.1.
  - It means Qt can add the API key itself, so the sidecar never holds one.
  - It amends ADR 0026's Network and Keys sections.
- **Linux mechanism:** Landlock for files and seccomp for sockets. Qt applies both in the child before `exec`.
  No bubblewrap.
- **Windows mechanism:** an AppContainer with no capabilities, ACEs granting only the sidecar's folders, and the
  job object. The job must be created and joined before the process runs.
- **Agent CLIs can't run inside the sidecar's sandbox.** They need their login, their install and the network. The
  recommendation is that Qt starts each one in a sandbox of its own and relays its stdio to the sidecar.
  - The Claude Agent SDK's `spawnClaudeCodeProcess` hook makes this possible for Claude.
  - Codex app-server and ACP are plain stdio already.
  - Their network can't be limited per host, and the docs will say so.

## 1. What the sidecar needs

Paths are under the user folder: `~/.config/hikarisub/` on Linux and `%APPDATA%\HikariSub\` on Windows
(`src/app/user_folder.cpp:161`).

| Need | Where | Access |
| --- | --- | --- |
| Its own executable | `ai/sidecar/<version>/` | read, execute |
| Working folder, private temp | `ai/work/` (`src/app/ai_features.cpp:51`), `TMPDIR` under it | read, write |
| The Claude Agent SDK, fetched at enable time | `ai/vendor/claude-agent-sdk/<version>/` | read, write (it fetches and unpacks) |
| Runtime libraries | Linux `/usr/lib`, `/lib64/ld-linux*`, `/etc/ld.so.cache`; Windows system DLLs | read, execute |
| TLS roots | Bun's bundled Mozilla roots; it also reads `/etc/ssl/cert.pem` and `/etc/ssl/certs` unless `SSL_CERT_FILE`/`SSL_CERT_DIR` are set ([Bun TLS][bun-tls], `bun:src/runtime/socket/cert_files.rs`) | read |
| Devices, process info | `/dev/null`, `/dev/urandom`, `/proc/self` | read |
| The link | stdin, stdout, stderr (inherited pipes) | as is |
| Network | the base URLs of the providers the user added, remote MCP servers (AI13), `registry.npmjs.org` while fetching the SDK | HTTPS, or plain HTTP under AI5's loopback and tailnet rule |
| Children | `claude`, `codex app-server`, ACP agents, stdio MCP servers | see §6 |

Not needed: the rest of the user folder (threads, `providers.json`, settings), the user's home, the keyring, D-Bus,
other processes, and listening sockets (ADR 0026 makes Qt own every listener).

Two Bun facts matter here:

- A `--compile` executable writes embedded `.node` addons to a temp file, `.bun-{uid}-{hash}`, which is "never
  deleted". On Windows the ownership check is `let ours = true` (`bun:src/runtime/jsc_hooks.rs`). The sidecar
  embeds no addons today, but its temp folder should be private anyway.
- Crash reports upload to bun.report by default on macOS and Windows. `DO_NOT_TRACK=1` turns that off
  ([Bun env vars][bun-env]). The sidecar's environment allowlist should set it.

JavaScriptCore maps its JIT pool read, write and execute at once (`oven-sh/WebKit:Source/JavaScriptCore/jit/ExecutableAllocator.cpp`).
So no sandbox may enforce W^X on the sidecar: no `MemoryDenyWriteExecute`-style seccomp rule and no Windows ACG.

## 2. Linux

### 2.1 Landlock

| ABI | Kernel | Adds |
| --- | --- | --- |
| 1 | 5.13 | filesystem rights: execute, read and write file, read dir, remove, make |
| 2 | 5.19 | `FS_REFER` (rename and link across folders) |
| 3 | 6.2 | `FS_TRUNCATE` |
| 4 | 6.7 | `NET_BIND_TCP`, `NET_CONNECT_TCP`, by port |
| 5 | 6.10 | `FS_IOCTL_DEV` |
| 6 | 6.12 | scoping of abstract UNIX sockets and signals |
| 7 | 6.15 | audit log flags |
| 8 | 7.0 | `RESTRICT_SELF_TSYNC` |
| 9 | 7.1 | `FS_RESOLVE_UNIX`: connect to pathname UNIX sockets |
| 10 | 7.2 | `NET_BIND_UDP`, `NET_CONNECT_SEND_UDP` |
| 11 | 7.3 | `RESTRICT_SELF_NO_NEW_PRIVS` |

The kernel version is the `landlock_abi_version` constant at each tag (`security/landlock/syscalls.c`). The names
are in the [UAPI header][ll-uapi] at v7.3 (`FS_RESOLVE_UNIX` is line 432, the UDP rights lines 481–482). ABI 12 is in
7.4-rc1 and isn't released. The owner's machine reports ABI 10 on 7.2.15.

- **Unprivileged, inherited, permanent.** "For unprivileged processes, setting the no_new_privs attribute is
  required." The policy "will stay enforced on all this thread's descendants", and there are at most "16 layers of
  stacked rulesets" ([Landlock docs][ll-doc]).
- **Best effort is the documented pattern.** Read the ABI with `landlock_create_ruleset(NULL, 0,
  LANDLOCK_CREATE_RULESET_VERSION)`, then drop the rights the kernel lacks ([Landlock docs][ll-doc]).
- **Files opened before enforcement keep their rights** ([Landlock docs][ll-doc]). That covers the stdio pipes.
- **No mounts.** "Threads sandboxed with filesystem restrictions cannot modify filesystem topology, whether via
  mount(2) or pivot_root(2)" ([Landlock docs][ll-doc]). So a bubblewrap-based sandbox can't start inside ours
  (inference). That matters for Codex, §5.1.
- **Network by port only.** A rule is `{allowed_access, port}`. Allowing 443 allows every host on 443
  ([UAPI][ll-uapi]). UDP is open below ABI 10, and that includes DNS.
  - Erratum 1 limits the TCP rights to `IPPROTO_TCP`, leaving out "SMC, MPTCP, SCTP" (`security/landlock/errata/abi-4.h`).
  - MPTCP is on by default and falls back to plain TCP ([MPTCP sysctl][mptcp]). Inference, untested:
    `socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP)` passes a port rule.
- **The UNIX socket hole.** Below ABI 9 a landlocked process can still `connect()` to a pathname socket such as
  `$XDG_RUNTIME_DIR/bus`. The docs list "Pathname UNIX sockets (ABI < 9)" under previous limitations
  ([Landlock docs][ll-doc]). Through the session bus that reaches the Secret Service, which hands an unlocked
  collection's secrets to any client (inference). From ABI 9, `FS_RESOLVE_UNIX` covers "connect(2) as well as …
  sendmsg(2) with an explicit recipient address" to sockets created outside the domain ([UAPI][ll-uapi]).

Distribution kernels all build Landlock and list it in `CONFIG_LSM`:

- **Ubuntu** 22.04 (5.15, ABI 1) and 24.04 (6.8, ABI 4).
- **Debian** 12 (6.1, ABI 2) and 13 (6.12, ABI 6).
- **Fedora** rawhide and **Arch**.

Sources are each distribution's kernel config (Ubuntu `debian.master/config/annotations`, Debian `debian/config/config`,
Fedora `kernel-x86_64-fedora.config`, Arch `config.x86_64`).

GitHub's `ubuntu-24.04` runner image 20261026 has "Kernel Version: 7.0.0-1017-azure" ([runner image][runner]).
That is ABI 8, so CI has no `FS_RESOLVE_UNIX` and no UDP rights.

### 2.2 seccomp

- **Unprivileged with no_new_privs, and kept across `execve`** ([seccomp(2)][seccomp2]).
- **"BPF programs may not dereference pointers"** ([seccomp filter][seccomp-doc]), so seccomp can't filter
  `connect()` by path or address. It can filter `socket()` by its domain and protocol, which are plain values.
- **Denying `socket()` closes direct networking only if the side doors are closed too:**
  - `io_uring`: `IORING_OP_SOCKET` (5.19) and `IORING_OP_CONNECT` (5.5) don't go through the syscalls
    ([io_uring_enter(2)][iouring]). Deny `io_uring_setup`.
  - The architecture: "Always check the arch value!" ([seccomp filter][seccomp-doc]). x32 can bypass a filter
    keyed on `AUDIT_ARCH_X86_64` ([seccomp(2)][seccomp2]), and i386 has `socketcall`.
  - `IPPROTO_MPTCP` (262) and ping sockets. systemd sets `ping_group_range` open, so ICMP echo is reachable
    (inference from systemd's `sysctl.d/50-default.conf`).
- **The cleanest rule denies `socket()` for every family and allows `socketpair()`.** A connected pair can't reach
  D-Bus or resolved, and Bun may use pairs for child stdio. Codex blocks `io_uring_*` "to prevent bypassing
  socket-family restrictions" (`codex:codex-rs/linux-sandbox/README.md`). Claude Code's runtime blocks
  `socket(AF_UNIX)` and `io_uring_*` ([srt README][srt]).

### 2.3 Namespaces and bubblewrap

- **Ubuntu restricts unprivileged user namespaces.** "The Ubuntu kernel now restricts the use of unprivileged
  user namespaces", and the fallback profile will "deny the subsequent use of any capabilities within the user
  namespace" ([Ubuntu 24.04 release notes][ubuntu-notes]). A process needs "the 'userns,' rule in their AppArmor
  profile" ([Ubuntu blog][ubuntu-blog]).
- **Ubuntu's bwrap profile:** on 24.04 it was dropped in 24.04.1 after a Flatpak regression. On 26.04,
  `bwrap-userns-restrict` is active and runs children as `unpriv_bwrap` with `audit deny capability` (Ubuntu
  AppArmor packaging).
- **Who is exempt:** upstream AppArmor ships `userns` profiles for Flatpak, Chromium, Chrome, VS Code and others,
  attached by path. Loading a profile needs `CAP_MAC_ADMIN` (`security/apparmor/policy.c`), so an AppImage can't
  get one. A deb or rpm postinst could.
- **No setuid fallback.** bubblewrap v0.12.0 "removes the support for building a setuid" binary
  ([release][bwrap-rel]).
- **What a network namespace would give:** "only a loopback device" ([bwrap README][bwrap]). Traffic then leaves
  only through a socket bind-mounted from outside, which is what Codex and the Claude Code runtime do (§5).

So bubblewrap would add mount isolation and a network namespace on Arch, Fedora and Debian. On stock Ubuntu it
needs root to install a profile, and the app would need a second code path.

### 2.4 What fits the sidecar

Qt applies the sandbox in the child after `fork` and before `exec`. `AiSidecarHost` already uses
`setChildProcessModifier` for `PR_SET_PDEATHSIG` (`src/backends/ai_sidecar_host.cpp:181`).

1. **In the parent:** read the ABI, open each granted path with `O_PATH`, build the ruleset and the BPF program.
2. **In the child:** `prctl(PR_SET_NO_NEW_PRIVS)`, `landlock_restrict_self`, then `seccomp(SECCOMP_SET_MODE_FILTER)`.
   These are raw syscalls, safe after `fork`.
3. **Landlock** handles every filesystem right the ABI has. It grants §1's paths only, and handles `NET_*` and
   the scopes as deny-all where the ABI has them.
4. **seccomp** denies:
   - `socket()` for every family, and `io_uring_setup`;
   - other architectures;
   - `ptrace` and `process_vm_*`;
   - `unshare`/`clone` with new namespaces, `mount`, `bpf` and `perf_event_open`.

| Kernel | Files | Network | D-Bus / keyring |
| --- | --- | --- | --- |
| no Landlock (< 5.13, or not in `CONFIG_LSM`) | not confined | none (seccomp) | closed (seccomp) |
| ABI 1–3 (Ubuntu 22.04, Debian 12) | confined; truncate unguarded below 3 | none | closed |
| ABI 4+ (Ubuntu 24.04, Debian 13, CI) | confined | none | closed |

The network column assumes the brokered egress of §7.1. Without it, the sidecar needs `socket(AF_INET*)`. The
network then becomes "any host on port 443" from ABI 4 on, and the D-Bus hole stays open below ABI 9.

## 3. Windows

### 3.1 AppContainer

- **No admin needed.** `CreateAppContainerProfile` "creates a profile for the current user"
  ([CreateAppContainerProfile][w-cacp]). The process is created with `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`
  and `EXTENDED_STARTUPINFO_PRESENT` ([Implementing an AppContainer][w-impl]). Chrome does this unelevated
  (`chromium:sandbox/win/src/app_container_base.cc`).
- **The profile writes per-user state.** It holds "folders and registry storage that are per-user and per-app"
  under `%LOCALAPPDATA%\Packages\<name>\AC`, and `LOCALAPPDATA`, `TEMP` and `TMP` are redirected there
  ([Implementing an AppContainer][w-impl]). The uninstaller should call `DeleteAppContainerProfile`
  ([DeleteAppContainerProfile][w-dacp]).
- **Files.** "The permitted access is the intersection of that granted by the user/group SIDs and AppContainer
  SIDs" ([Implementing an AppContainer][w-impl]).
  - System files carry `ALL APPLICATION PACKAGES` ACEs ([Chromium sandbox][cr-sandbox]). The user's profile
    doesn't, so it is unreadable (inference from the access rule).
  - The app grants its own folders by adding an ACE for the AppContainer SID. The owner always has `WRITE_DAC`, so
    no admin is needed ([WFP access control][w-wfpacl] states the owner rule).
  - The sidecar's own executable under `%APPDATA%` needs that ACE too. Chromium notes its binaries "need ACLs to be
    laid down" ([Chromium sandbox][cr-sandbox]).
- **Network.** With no capability there is no network: network checks apply "if the token is a Low Box token and
  the `INTERNET_CLIENT` Capability is not present" ([Chromium sandbox][cr-sandbox]). The capabilities are:
  - `internetClient`: "No local network access".
  - `privateNetworkClientServer`: home and work networks, and it "does not provide access to the Internet"
    ([capability declarations][w-caps]).
  - Loopback: "loopback connections for IPC are blocked by default". The exemption needs administrator rights
    ([IPC][w-ipc]).
  - "Home and work" means local subnets, and "all other network endpoints" are Internet ([network
    isolation][w-netiso]). So a Tailscale `100.x` peer counts as Internet (inference), and a local Ollama on
    `localhost` is unreachable.
  - Nothing narrower than these categories exists without admin.
- **Pipes.** Inherited stdio handles carry their access with them ([CreateProcessW][w-cp]). A named pipe from the
  parent works if its DACL grants the AppContainer SID and it carries a Low mandatory label ([sharing named
  objects][w-share], [integrity control][w-mic]). AF_UNIX from an AppContainer isn't documented.
- **Children inherit** the token, "the integrity level (IL) and app container info" ([legacy apps][w-legacy]). A
  child can't get more than the sidecar has. `PROCESS_CREATION_CHILD_PROCESS_RESTRICTED` forbids children
  altogether.
- **LPAC** (`ALL_APPLICATION_PACKAGES_OPT_OUT`) also cuts registry reads unless `registryRead` is granted
  ([Implementing an AppContainer][w-impl]). Whether Bun runs under LPAC is untested.
- **JIT.** ACG is a separate opt-in mitigation ([UpdateProcThreadAttribute][w-uppta]). No source says plain
  `VirtualProtect` JIT fails in an AppContainer. A spike must show Bun runs there.

### 3.2 Restricted tokens, integrity levels and job objects

- **Restricted tokens** add a second access check for restricting SIDs ([restricted tokens][w-rt]).
- **Low integrity** stops writes up, but it can still read "most files", and it keeps "TCP/IP Sockets"
  ([Chromium sandbox][cr-sandbox]).
- **Neither limits the network.** Chromium gets "no network access" only by adding the Low Box (AppContainer)
  token ([Chromium sandbox][cr-sandbox]). Gemini CLI's no-admin Windows sandbox is a Low-integrity restricted
  token in a job, and with network "off" it only throttles: "Network might not be throttled"
  (`gem:packages/cli/src/utils/sandbox/windows/GeminiSandbox.cs:292-302`).
- **Job objects** limit process count, memory, CPU, UI and breakaway, and support nesting from Windows 8
  ([job objects][w-job]). They don't touch files or network.
- **A gap in `AiSidecarHost`:** it creates and joins its job in `handshake()`, after the process has started
  (`src/backends/ai_sidecar_host.cpp:199-205`). A child started in that window escapes the kill-on-close.
  Starting suspended with `PROC_THREAD_ATTRIBUTE_JOB_LIST` closes the window. SB1 should fix this along the way.

### 3.3 WFP and the firewall

- **WFP needs admin.** Everyone gets only `FWPM_ACTRL_OPEN` and `FWPM_ACTRL_CLASSIFY`, and adding a filter needs
  `FWPM_ACTRL_ADD` ([WFP access control][w-wfpacl]).
- **What a filter can match:** `FWPM_CONDITION_ALE_PACKAGE_ID` ("the security identifier (SID) of an app
  container"), the app's path, and the remote address and port ([filter conditions][w-wfpcond]).
- **Host names:** only through firewall "dynamic keywords". Those need Defender with Network Protection and DoH
  off, and they race the first connection ([dynamic keywords][w-dyn]).
- **An elevated installer step** could add persistent filters keyed on the AppContainer SID. That costs a UAC
  prompt in a per-user install and does nothing for portable mode. It also leaves IP lists to maintain, and a
  changed provider list would need admin again.
- **How others pay this cost:** Codex's legacy `elevated` mode "requires administrator-approved setup"
  ([Codex Windows sandbox][codex-win]). The Claude Code runtime needs a one-time elevated `windows-install`
  ([srt README][srt]).

## 4. macOS

macOS isn't a shipped platform (ADR 0004).

- **Seatbelt.** Codex and the Claude Code runtime still drive `/usr/bin/sandbox-exec` with generated `(deny
  default)` profiles (`codex:codex-rs/sandboxing/src/seatbelt.rs:57-61`, `srt:src/sandbox/macos-sandbox-utils.ts:1083`).
  Apple doesn't publish the man page online, so its deprecation notice wasn't checked here.
- **App Sandbox.** It can pass its sandbox to a child, but the child "must use exactly two App Sandbox
  entitlement keys" ([entitlement reference][apple-ent]). That means signing every child, which doesn't fit
  user-installed CLIs.
- **The macOS route would be Seatbelt.** It would also allow outbound only to a proxy's localhost port, as Codex
  and Zed do.

## 5. How comparable tools do it

### 5.1 Codex CLI

- **Linux.** "The helper uses bubblewrap for filesystem and namespace isolation, and seccomp for syscall filtering."
  It prefers `bwrap` on `PATH` and falls back to a bundled one.
  - Landlock is legacy and "rejected for these policies because it cannot isolate app-server Unix sockets"
    (`codex:codex-rs/linux-sandbox/README.md`).
  - seccomp blocks network syscalls, allows only `AF_UNIX`, and blocks `io_uring` in every mode
    (`codex-rs/linux-sandbox/src/seccomp.rs:192-232`).
- **Filesystem.** Reads cover the whole disk (`--ro-bind / /`). Writes go to workspace roots and temp, with
  `.git`, `.agents`, `.codex` and `.aws` kept read-only (`codex-rs/protocol/src/permissions.rs:43-54`).
- **Network.** "By default, the agent runs with network access turned off" ([Codex security][codex-sec]).
  - For per-host access, the optional `codex-network-proxy` runs on the host with an allowlist where "deny wins"
    (`codex-rs/network-proxy/README.md`).
  - Linux reaches it through a network namespace and "an internal TCP->UDS->TCP routing bridge". macOS allows only
    `localhost:{port}`.
- **Windows** has three backends:
  - MXC, which routes "into Microsoft's MXC `BaseContainerRunner`" and never "edits host ACLs, creates sandbox
    users, runs setup, or requests elevation" (`codex-rs/mxc-sandbox/README.md`). It needs Windows 11 24H2 or
    later ([Codex Windows sandbox][codex-win]).
  - The legacy `elevated` mode: sandbox users plus WFP, after one admin setup.
  - The legacy `unelevated` mode: a restricted token, with "weaker network isolation".
- **What is confined:** only the commands Codex runs. The `codex` process is hardened but not confined
  (`codex-rs/process-hardening/src/lib.rs`). MCP stdio servers start unsandboxed
  (`codex-rs/rmcp-client/src/stdio_server_launcher.rs:471`).
  - So `codex app-server` with `sandbox: read-only` confines nothing that Hikari runs. Hikari declines every
    `commandExecution` anyway (AI15).

### 5.2 Claude Code's sandbox and sandbox-runtime

- **Mechanisms.** macOS uses `sandbox-exec`. Linux uses bubblewrap, socat, and a static `apply-seccomp` that
  blocks `socket(AF_UNIX)` and `io_uring_*`. Windows (alpha) runs as a dedicated `srt-sandbox` user with a
  write-restricted token, a job, WFP and ACEs ([srt README][srt]).
- **Network.** Deny-all with a domain allowlist: "Empty allowedDomains = no network access". HTTP and SOCKS
  proxies run on the host and refuse loopback, link-local and metadata addresses. Linux reaches them "over a Unix
  domain socket" bind-mounted into a network namespace.
- **Filesystem.** Reads are allowed by default. Writes are allowed only where listed, and shell rc files, `.git`
  hooks and config, `.mcp.json` and IDE folders are always denied (`srt:src/sandbox/sandbox-utils.ts:21-48`).
  Claude Code's docs say reads cover "most of the machine, including credential files such as `~/.ssh`… There is
  no built-in credential deny list" ([Claude Code sandboxing][cc-sb]).
- **Nested user namespaces are refused by default.** "A nested bubblewrap or `srt`" fails with EPERM
  (`srt:vendor/seccomp-src/seccomp-unix-block.c:90-101`).
- **What is confined:** Claude Code sandboxes its Bash, PowerShell and Monitor tools. "Claude's file tools, MCP
  servers, and hooks run outside it", and "On native Windows, Claude Code runs commands unsandboxed"
  ([Claude Code sandboxing][cc-sb]). The runtime's headline use is wrapping MCP servers (`"command": "srt"`).
- **Known limits:**
  - domain fronting, and exfiltration through broad domains;
  - Schannel clients don't work under the Windows token;
  - the WFP fence is bypassable through token impersonation, and SMB, WSL and DNS aren't fenced
    ([srt README][srt]).

### 5.3 Zed

- **Mechanisms.** A `sandbox` crate: Seatbelt on macOS and bubblewrap plus seccomp on Linux. Windows is "WSL: same
  as Linux; non-WSL: not supported". It refuses a setuid `bwrap` (`zed:crates/sandbox/README.md:39-44`,
  `linux_bubblewrap.rs:96-140`).
- **Network.** Blocked by default. Per-domain access goes through a proxy inside Zed, reached from inside on one
  localhost port.
- **Filesystem.** Reads cover all files. Writes go to the worktrees and a private temp folder, with Git metadata
  kept read-only.
- **Symlink-swap race.** Linux grants are pinned with `O_PATH` descriptors and checked again inside the sandbox
  (`crates/sandbox/README.md:80-275`). A path-based design of ours needs the same care.
- **What is confined:** the built-in agent's terminal tool only. External ACP agents (Claude, Codex, Gemini)
  run unsandboxed: `create_terminal_entity` is called with no sandbox (`crates/agent_servers/src/acp/v1.rs:10474-10510`).
- **Extensions**, relevant to PL7, are WASM on wasmtime. Each gets only its own work folder, and host APIs are
  gated by a manifest capability plus the user's grants (`process:exec`, `download_file`, `npm:install`;
  `docs/src/extensions/capabilities.md`). The default grants everything, and `process:exec` children run
  natively.

### 5.4 opencode

No OS sandbox. "OpenCode does **not** sandbox the agent… If you need true isolation, run OpenCode inside a Docker
container or VM", and "The permission system is not a sandbox" (`oc:SECURITY.md:15-30`). The permission system has
allow, ask and deny per tool, command glob and path (`packages/web/src/content/docs/permissions.mdx`).

### 5.5 Gemini CLI

Gemini CLI can sandbox the whole process (`-s`, with Docker, Podman, gVisor, LXC or Seatbelt profiles). It can also
sandbox its shell and write tools: bwrap with `--unshare-all` on Linux, and a Low-integrity restricted token in a
job on Windows (`gem:docs/cli/sandbox.md`).

### 5.6 Comparison

| | Linux | Windows | Network | Default reads | Admin | Confines |
| --- | --- | --- | --- | --- | --- | --- |
| Codex | bwrap + seccomp | MXC (24H2+); legacy elevated or restricted token | off; host proxy with allowlist | whole disk | no (elevated: one UAC) | commands it runs |
| Claude Code / srt | bwrap + socat + seccomp | alpha: sandbox user + WFP | deny-all; host proxy with allowlist | whole disk | Windows: one UAC | Bash tools; srt: any wrapped tree |
| Zed | bwrap + seccomp | WSL only | blocked; in-app proxy | whole disk | no | agent terminal only |
| opencode | none | none | none | n/a | n/a | nothing |
| Gemini CLI | bwrap or containers | Low-IL token + job | open by default | whole disk | no | tools or whole CLI |
| **Hikari sidecar (proposed)** | Landlock + seccomp | AppContainer, no capabilities | none; Qt brokers with allowlist | its own folders only | no | the whole sidecar |

Two lessons carry over:

- **Nobody limits hosts at the OS level.** They all take the direct route away and put an allowlisting proxy
  outside.
- **Nobody confines their own main process,** because a coding agent needs the whole disk. The sidecar doesn't, so
  it can be confined far more tightly than any of these.

## 6. Children: agent CLIs and MCP servers

A sandbox is inherited (Landlock, seccomp, AppContainer tokens), so a child started by the sidecar can't have more
than the sidecar. What each child needs:

- **`claude`** needs its install (the native one is under `~/.local/bin` and `~/.local/share/claude`), its login
  and state (`~/.claude/`, `~/.claude.json`), and HTTPS to Anthropic.
  - With `tools: []` it runs no commands, so its own sandbox doesn't matter.
  - Hikari's tools reach it in-band, as the SDK's `mcp_message` control requests over its stdio.
- **`codex app-server`** needs its install and a `CODEX_HOME`. AI15 proposes a Hikari-private one, so `~/.codex`
  isn't needed. It also needs HTTPS to OpenAI.
  - Its read-only sandbox is bubblewrap, which can't start under our Landlock (§2.1). That matters only for
    commands, and Hikari declines all of them.
  - Its Hikari tools come over an MCP channel that Qt owns (ADR 0026).
- **ACP agents and stdio MCP servers** are user commands (`gemini --acp`, `npx …`). Each needs whatever it needs:
  its own config, npm caches, any host.

If the sidecar starts them, any grant they need becomes a grant for the sidecar, and the sidecar's sandbox is
gone. If Qt starts them, the sidecar only asks:

- **Claude.** The Agent SDK's `spawnClaudeCodeProcess(options: SpawnOptions) => SpawnedProcess` is documented as
  "Use this to run Claude Code in VMs, containers, or remote environments" (`sdk.d.ts:2553`, 0.3.318).
  `SpawnedProcess` is stdin and stdout streams, `kill` and exit events (`sdk.d.ts:9990`). So the sidecar's spawn
  function can ask Qt to start `claude` and relay its stdio over the link.
- **Codex app-server and ACP** are plain stdio JSON-RPC, so Qt can relay them the same way.

Qt then decides what runs:

- **The program** comes from Qt's own settings (the path the user set, or the one found on `PATH`), never from
  the sidecar.
- **Claude's arguments** are built by the SDK, so Qt can only check them. It refuses permission-bypass flags and
  requires built-in tools off. The fake-`claude` test catches SDK drift.
- **For Codex**, Qt sits in the relay. It can force `sandbox: read-only` on `thread/start` and answer every
  `commandExecution` and `fileChange` approval with `decline` itself.

Each agent's own sandbox:

- **Linux:** Landlock grants the agent's install (read, execute), its login folder (read, write) and a scratch
  working folder. seccomp denies `socket(AF_UNIX)`, which closes D-Bus. Network is "any host", or "any host on
  443" from ABI 4. A per-host limit would need a network namespace (§2.3).
- **Windows:** a second AppContainer with `internetClient`, plus ACEs on the agent's install and login folders.
  - Loopback is blocked, so Hikari's MCP channel for Codex has to be a named pipe that grants that SID.
  - Whether `claude.exe` and `codex.exe` run there at all is untested. Codex's credential store must be the file
    store, because Credential Manager is out of reach.
  - Adding ACEs to the user's own CLI folders is reversible, but it is a change to their files.

## 7. Design options

### 7.1 How the sidecar's requests leave

| | E1: per-provider UNIX socket | E2: requests over the link (recommended) | E3: direct, best effort |
| --- | --- | --- | --- |
| How | The sidecar calls `fetch(url, {unix: "<ai/work/net/N.sock>"})`. Qt listens on one socket per allowed origin, connects to the pinned address and copies bytes. TLS runs end to end in the sidecar ([Bun unix fetch][bun-unix]) | The sidecar's custom `fetch` sends `_hikari/http.request` and streams the body back as notifications. Qt checks the allowlist, applies AI5's plain-HTTP rule, makes the request and streams the response | The sidecar keeps sockets. Landlock allows TCP 443 (ABI 4+); Windows gets `internetClient` |
| Per-host limit | yes | yes | no |
| Linux socket rule | must allow `socket(AF_UNIX)`, so D-Bus is reachable below ABI 9 (Ubuntu 24.04, CI) | deny every `socket()` | must allow `AF_INET*` |
| Windows | AF_UNIX from an AppContainer is undocumented; Bun's TLS-over-unix tests are `skipIf(isWindows)` (`bun:test/js/web/fetch/fetch.unix.test.ts`) | AppContainer with no capabilities; nothing new to test | no localhost or LAN endpoints (Ollama) |
| Localhost, LAN, Tailscale endpoints | Qt connects, so yes | Qt connects, so yes | Linux yes; Windows no |
| Keys | stay in the sidecar | Qt can add the key itself, so the sidecar never holds one | stay in the sidecar |
| Cost | small forwarder in Qt | a streaming HTTP relay in Qt (QNetworkAccessManager), and an ADR 0026 amendment | none, and no real limit |

E2 gives the same rule on every kernel and on Windows without new privileges. It fits ADR 0026's own line that "Qt
decides every permission". The relay's allowlist would be:

- the providers' base URLs;
- the remote MCP servers the user added;
- `registry.npmjs.org`, while fetching the Claude SDK packages.

The custom `fetch` already exists (`src/ai-sidecar/src/providers/egress.ts:155`), and so does the C++ route rule
from #637 that the port keeps as a reference.

### 7.2 Filesystem scope

- **F1, only listed paths (recommended).** §1's table and nothing else. The sidecar has no use for the user's
  files.
- **F2, read everything, write little** (what Codex, Claude Code and Zed do for agents). It lets a malicious
  dependency read `~/.ssh` or a browser profile. Without a network that's less harmful, but the sidecar has no
  reason to read them.

### 7.3 Where the sandbox lives (PL7)

One `backends` module would serve both cards:

- a profile with read-and-execute paths, read-write paths and a network setting (`none`, or `any` for agents);
- applied by Qt to a `QProcess` before it runs, through the child modifier on Linux and an AppContainer
  `CreateProcess` on Windows.

The Lua plugin helper (PL7 #419) would use it with network `none`. ADR 0022 already routes plugin network
through HikariSub's services, which is E2's shape.

## 8. Owner questions

The comment on #651 has the same questions.

1. **Agent CLIs.**
   - (a) Inside the sidecar's sandbox. This hands the sidecar their login and network.
   - (b) Qt starts each one in its own sandbox and relays stdio (recommended).
   - (c) Qt starts them unsandboxed, with argument and protocol checks.
   - (d) As today: the sidecar starts them. The sandbox then doesn't hold.

   On Windows, (b) depends on a spike showing `claude.exe` and `codex.exe` run in an AppContainer. If they don't,
   fall back to (c) there and document it.
2. **How the sidecar reaches providers.**
   - E2 (recommended): requests over the link, with Qt checking hosts.
   - E1: a UNIX-socket forwarder.
   - E3: direct, best effort.

   Sub-question: with E2, should Qt add the API key so the sidecar never holds one? Recommended yes, with OAuth
   refresh following later. Either way, ADR 0026's Network and Keys sections get amended.
3. **Windows network limits.**
   - The sidecar: no network capability, with all egress brokered (recommended).
   - Agent CLIs: `internetClient`, documented as not limited per host.
   - No elevated WFP or firewall step, because it needs a UAC prompt, does nothing for portable mode and leaves
     IP lists to maintain.
4. **Filesystem scope.** F1 for the sidecar (recommended). Each agent profile gets its install, its login folder
   and a scratch folder only.
5. **MCP servers and ACP or custom agents.** Qt starts them from its own settings, unsandboxed in v1, and the
   docs say so (recommended), as Codex, Zed and Claude Code do. Or put each one in a generic profile with network
   `any` and its working folder writable.
6. **Where the sandbox can't apply** (no Landlock, or the AppContainer spike fails). Run with a visible "not
   sandboxed on this system" in Settings › AI and in "What is sent" (recommended), or refuse to start AI.
7. **PL7.** SB1 builds the shared sandbox module, and PL7 applies it to the Lua helper (recommended).

[bwrap]: https://github.com/containers/bubblewrap
[bwrap-rel]: https://github.com/containers/bubblewrap/releases/tag/v0.12.0
[ll-doc]: https://docs.kernel.org/userspace-api/landlock.html
[ll-uapi]: https://github.com/torvalds/linux/blob/v7.3/include/uapi/linux/landlock.h
[mptcp]: https://docs.kernel.org/networking/mptcp-sysctl.html
[seccomp-doc]: https://docs.kernel.org/userspace-api/seccomp_filter.html
[seccomp2]: https://man7.org/linux/man-pages/man2/seccomp.2.html
[iouring]: https://man7.org/linux/man-pages/man2/io_uring_enter.2.html
[ubuntu-notes]: https://documentation.ubuntu.com/release-notes/24.04/
[ubuntu-blog]: https://ubuntu.com/blog/ubuntu-23-10-restricted-unprivileged-user-namespaces
[runner]: https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md
[bun-tls]: https://bun.com/docs/guides/http/tls
[bun-env]: https://bun.com/docs/runtime/environment-variables
[bun-unix]: https://bun.com/docs/guides/http/fetch-unix
[cr-sandbox]: https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md
[w-impl]: https://learn.microsoft.com/en-us/windows/win32/secauthz/implementing-an-appcontainer
[w-cacp]: https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-createappcontainerprofile
[w-dacp]: https://learn.microsoft.com/en-us/windows/win32/api/userenv/nf-userenv-deleteappcontainerprofile
[w-caps]: https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations
[w-ipc]: https://learn.microsoft.com/en-us/windows/apps/develop/communication/interprocess-communication
[w-netiso]: https://learn.microsoft.com/en-us/previous-versions/windows/apps/hh770532(v=win.10)
[w-cp]: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw
[w-share]: https://learn.microsoft.com/en-us/windows/apps/develop/communication/sharing-named-objects
[w-mic]: https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control
[w-legacy]: https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-for-legacy-applications-
[w-uppta]: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute
[w-rt]: https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens
[w-job]: https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects
[w-wfpacl]: https://learn.microsoft.com/en-us/windows/win32/fwp/access-control
[w-wfpcond]: https://learn.microsoft.com/en-us/windows/win32/fwp/filtering-condition-identifiers-
[w-dyn]: https://learn.microsoft.com/en-us/windows/security/operating-system-security/network-security/windows-firewall/dynamic-keywords
[apple-ent]: https://developer.apple.com/library/archive/documentation/Miscellaneous/Reference/EntitlementKeyReference/Chapters/EnablingAppSandbox.html
[codex]: https://github.com/openai/codex
[codex-sec]: https://learn.chatgpt.com/docs/agent-approvals-security
[codex-win]: https://learn.chatgpt.com/docs/windows/windows-sandbox
[srt]: https://github.com/anthropic-experimental/sandbox-runtime
[cc-sb]: https://code.claude.com/docs/en/sandboxing
[zed]: https://github.com/zed-industries/zed
[oc]: https://github.com/anomalyco/opencode
[gem]: https://github.com/google-gemini/gemini-cli
