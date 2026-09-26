# Migration-spec coverage

This is an evidence ledger for the full [Wayfinder destination](https://github.com/altqx/hikari/issues/1), not a completion claim. A research inventory, a throwaway demonstration and an accepted design answer are different evidence. Runtime gates described in a spec remain future work unless a linked observation actually covers them.

The capability baseline is the [wx UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md), including its complete action/menu/control appendices. The table below tracks surface families; it does not replace that detailed parity list. No absent prototype implies permission to drop a capability.

## Destination artifacts

| Requirement | Current authoritative evidence | What remains before the map can finish |
| --- | --- | --- |
| Architecture ADRs | Accepted foundation, platform, compatibility, build, media/audio, font, document/time and distribution contracts in [ADRs](../adr/); [architecture](architecture.md) | Final player/native feasibility, detailed automation, docking and lifecycle choices; responsibilities must agree across modules. |
| Reviewed runnable mockups and written UX for every surface | [Visual language](ux/visual-language.md), accepted [workspace/shell contract](ux/workspaces.md), [grid direction](ux/subtitle-grid.md); linked immutable HTML/QML artifacts | Most detailed surface workflows still need their own prototype, human reaction and written outcome. Generic controls do not sign off all screens. |
| Rewritten core design | Accepted [document model](document-model.md), [time semantics](time-semantics.md) and module ownership; source inventories linked from the map | Draft/command/undo, session/recovery and threading/resource contracts. Existing source tests were inventoried, not executed as rewrite proof. |
| Stateless CMake build | Accepted [CMake workflow/vcpkg/Qt contract](build.md) and platform policy | Exact artifact/toolchain locks, installer behavior and a clean Windows/Linux dependency slice must prove the chosen mechanism. |
| Agent-sized implementation plan | None yet | Sequence actual implementation issues with input contracts, deliverables, meaningful acceptance evidence and dependencies after design decisions settle. |
| Carry-over and parity | Accepted [compatibility contract](compatibility.md) and [six named departures](compatibility-decisions.md); [data inventory](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md), [core inventory](https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md) | Individual defect dispositions, concrete fixture ledger and per-operation loss/error rules. Preserve files, catalogs, sessions/autosaves and unchanged Lua scripts; one-shot settings/hotkeys import excludes themes. |
| Shared vocabulary | Accepted domain glossary in the single root [CONTEXT.md](../../CONTEXT.md) | Keep later contracts consistent; add genuinely new domain terms through review. Terminology acceptance does not decide format/API semantics. |
| Measurable verification and release gates | Accepted [performance](performance.md), [platform](platform-policy.md) and [distribution](distribution.md) policies; limited native observations | Accepted [testing stack and completion rule](testing.md); native input/accessibility matrix execution, actual reference hosts and deployment/cutover proof remain. HTML checks and one Windows synthetic sample are insufficient for platform-wide claims. |

## Surface evidence

“Partial” below means that the linked study settles only the stated boundary. Every unreviewed family remains in scope.

| Surface family | Current design evidence | Missing design coverage |
| --- | --- | --- |
| Application shell, tabs, workspaces and Home | Shared workspace; current-program Editing default; movable/floating tools; follow/pin; optional Home/protected comparison; Classic menus/local controls/status and the named tool/dialog grouping | Optional task presets are accepted. [Translation/comparison navigation](https://github.com/altqx/hikari/issues/49), native docking and detailed surface flows continue separately. |
| File operations, recents, sessions and recovery entry | Partial: in-memory empty/missing-media/save-discard-cancel examples | Complete open/save/save-translation/encoding/external-session flows; real-data compatibility and recovery decisions. |
| Subtitle grid and row operations | Painted renderer selected; native keyboard spike and small HTML selection examples | Full multi-selection/current/focus rules, clipboard/paste-special, split/join/sort scope, columns and row operations; [native feasibility continuation](https://github.com/altqx/hikari/issues/45). |
| Grid filtering and tree/grouping | Inventory only | Hidden-selection/action scope, filter controls, group editing, FPS/properties dialogs and navigation through collapsed/hidden rows. |
| ASS editor and line properties | Native [ASS editing study](https://github.com/altqx/hikari/issues/28); Enter commits/advances outside IME and editable hidden tags are required | Commit/advance and undo boundaries, full metadata/tag buttons/completion, mapped hidden-tag editing policy and feasibility, spell/IME/RTL and accessible editing. |
| Translation and two-document comparison | Accepted [stacked fields, bottom reference and independent reference navigation](ux/translation-comparison.md); runnable [study](https://github.com/altqx/hikari/issues/49) | Confirmation traversal, original protection/copy/tag movement, save translation, matching criteria and comparison navigation/synchronization. |
| Audio timing and karaoke | Accepted PortAudio/QSG direction; [audio timing/karaoke prototype](https://github.com/altqx/hikari/issues/53) in progress | Playback windows, selection/commit/advance, ruler/keyframes, waveform/spectrum controls, snapping, gains/zoom and karaoke splitting. |
| Video, transport and fullscreen | Accepted active-mode clock ownership; candidate pipeline and [native media handoff](https://github.com/altqx/hikari/issues/50) remain open | Exact editing/general playback transitions, stream/chapter/filter capabilities, navigation/capture/coordinates, fullscreen monitors and file actions. |
| Visual typesetting tools | [Video/typesetting prototype](https://github.com/altqx/hikari/issues/55) in progress | All transforms/clips/drawing/presets, shared coordinates and keyboard/numeric equivalents, contextual properties and cancel/commit. |
| Styles and external catalogs | [Styles/catalogs/font/color prototype](https://github.com/altqx/hikari/issues/54) in progress | Document/catalog distinction, create/edit/copy/rename/delete/import/transfer/order/clean, detailed style preview and catalog management. |
| Fonts, profiles and colors | Accepted renderer agreement contract; [Windows font identity experiment](https://github.com/altqx/hikari/issues/47) exposes public-log and fallback-reimport gaps | Font search/preview/provider identity, profile/catalog operations, alpha/recent/screen picker, missing-font and fallback behavior. |
| Find/replace, results and select-lines | Tool placement and selected-line literal replacement example | All expression/field/selection/tab/file scopes, result navigation, replace-all preview, selection operations and histories. |
| Minor-error correction and spelling review | Two sample spelling replacements in editor spike | Correction-rule management/results, word-by-word suggestions/ignore/replace and dictionary/language operations. |
| Shift times and postprocessor | Tool placement and synthetic selected-line start shift | Time/frame subsets, video/audio alignment, timed tags/end correction, profiles, lead/continuity/keyframe settings and preview/apply. |
| Script properties, resample and conversion | Inventory only | Metadata and resolutions, mismatch choices, coordinate changes, format/FPS/style settings, loss diagnostics and operation scope. |
| Font collector and MKV extraction | Inventory/backend research and native font identity evidence; no reviewed collector task flow | Source/use analysis, track/attachment/destination choices, progress/cancel/results and capability availability. |
| Automation manager, script dialogs and progress | Accepted helper-process boundary; [manager/dialog prototype](https://github.com/altqx/hikari/issues/51) in progress | Script/reload/rerun/macro discovery, dynamic dialog controls and return values, hotkeys, modal coordination, cancellation/errors and unchanged-script compatibility. |
| Settings, shortcut editor and importer | [Settings/import mechanics proposal](proposals/settings-import.md); [preferences/shortcut/import prototype](https://github.com/altqx/hikari/issues/52) in progress | Searchable scopes/pages, conflict/reset/profile behavior, one-shot preview/backup/idempotency, external paths/associations; no theme import. |
| History | Persistent tool placement and toy undo example | Inspect/undo/redo/last-save/revision selection, draft-versus-document history and scope/dirty-state feedback. |
| Autosave browser and cleanup | In-memory recovery-copy example | Real snapshot selection/opening, missing/corrupt cases, distinction from rebuildable caches and cleanup scope. |
| Logs, progress, errors, updates, Help/About | Inventory only | Nonblocking task ownership/cancellation, inspectable errors/results, update failure versus no-update, credits and notices. |

## Maintaining the ledger

Each future surface ticket should name the relevant inventory rows and action families, link a runnable immutable prototype plus run instructions, capture the user's decision and point to its written UX outcome. Record any explicitly approved merge/drop and its reason. Do not count a placeholder panel, shared component gallery, proposed placement worksheet or successful syntax check as a reviewed complete workflow.

Before closing the map, re-check every row against the accepted written specification and final implementation-plan issues. The present ledger deliberately exposes missing work; it does not shrink the destination to the artifacts already available.
