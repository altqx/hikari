# Proposed evidence-gated implementation sequence

For [Sequence the Qt rewrite into evidence-gated implementation issues](https://github.com/altqx/hikari/issues/65). **Conditional planning only: no production work, implementation issues, release or cutover is authorized here.** The [architecture](../architecture.md), [coverage ledger](../coverage.md), [compatibility](../compatibility.md), [build](../build.md) and [testing contract](../testing.md) govern this route. Local card IDs below are planning references, not created tickets or readiness labels. The big-bang rewrite still replaces the core; incremental construction on `qt` does not introduce a wx-backed production bridge.

## Gate 0: finish the specification and feasibility decisions

The planning ticket depends on the open design/native prerequisites recorded in GitHub. Accepted decisions close their own blockers; do not shorten the remaining graph merely because one card could compile independently.

| Input group | What must be recorded before the final implementation graph becomes ready |
| --- | --- |
| Application policy | Accepted outcomes from [edit transactions](edit-transactions.md), [Document/Session lifecycle](application-lifecycle.md), [settings/import](settings-import.md) including named departures, failure behavior and unresolved-input disposition; the [native docking contract](../docking.md) is now accepted, with its qualification matrix still required. |
| Backend/automation | Follow the accepted [backend contract](../backends.md), including isolated FFMS2 qualification first. Resolve remaining [video pipeline](video-pipeline.md) and [automation host](../automation.md) choices. One persistent Lua process/state per loaded script, one active macro application-wide and explicit restart/no rerun are accepted; transport details and remaining cancellation/API outcomes stay open. |
| Surface reactions | Record actual reactions and written outcomes for translation/comparison, automation, preferences, audio, Styles, video/tools, Grid/History, Search/correction, timing, utilities/Conversion, file/recovery and detailed Line editor studies. A representative control is not complete inventory coverage. |
| Native feasibility | The mapped-editor ticket is resolved with named E28 obligations transferred (see V2-E below). Resolve the existing build, painted-grid, font-identity and media-handoff tickets with their required evidence and explicit limits. Close design-critical gaps, or explicitly revise the affected design through its decision ticket; do not call them ordinary implementation details. |
| Readiness policy | The [beta/rollback/cutover policy](cutover-readiness.md) is accepted: bounded opt-in betas under applicable checks, isolated profiles/tested rollback, and all stable gates plus concrete owner approval before cutover. No release or branch operation is authorized. |

The distinction is **feasibility needed to choose the design** versus **production qualification of the resulting implementation**. A viable official Qt provisioning/dependency slice, native mapped-input/accessibility direction, renderer font-identity seam and coherent media ownership are prerequisites, not retrospective excuses. Full application corpus parity, real production performance, crash recovery, supported-platform packaging and release signatures belong in the production cards and final qualification; demanding those before an application exists would be circular. Each prerequisite ticket must state which bounded design question passed and which later obligations transfer by name. A failed or inconclusive design-critical result cannot be transferred as a pass.

## Freeze inputs and protect boundaries

At graph creation, pin the accepted spec/ADR commit, prototype outcome and research/source blob for each card. This draft was prepared against spec commit `50b14c10dc9ab936cc1afa6350de01872fb91d4d`; later accepted decisions require a new recorded baseline, not silently moving links. Preserve the pinned [UI][ui], [data][data] and [core][core] inventories and their source baseline as characterization inputs. Static source observations are not executed legacy oracles.

The [currently approved departure ledger](../compatibility-decisions.md) includes C03-preservation, C02-loss-preview, C05-audio-association, C01-equality, C01-fps-isolation, T42-A, C07-atomic-rejection, C43-save-identity, L58-write-close, L58-staged-replacement, L58-recovery-copy, C04-short-file, G56-contiguity and J56-selected-only-join. Plan characterization and implementation for those exact outcomes; other mixed-row subcases require their own recorded dispositions. Shared draft/undo/lifecycle policies remain partly unresolved.

The stable module direction is already accepted: plain-C++ core values and operations; application-owned Documents, actions/targets and history; backend owners behind ports; Qt models/QML observing state; one composition root. No backend handles in core, second authoritative QML model, shared mutable decoder pointer or wx implementation reuse. Types for Document time, media PTS, frames and samples remain distinct. Stable Line IDs survive the specified in-memory operations, not an invented persisted identity scheme.

After their decisions settle, freeze the minimal command, file/lifecycle and backend value contracts consumed by the first workflows. Do not prematurely freeze class names, container choices or a plugin SDK. Changing an internal representation is routine; changing a visible result, ownership guarantee or approved IPC contract reopens the responsible decision. Assign one owner to each shared interface change, notify dependent cards and rerun affected contract evidence before integration.

## Agent-sized card contract

Every eventual issue must contain:

- **Inputs:** immutable spec/approval/source/fixture links; exact inventory row/action IDs; toolchain, platform and prerequisite evidence.
- **Outcome:** one demonstrable operation or infrastructure capability, owned paths/interfaces and explicit exclusions. Exclusions identify sibling ownership, never remove product scope.
- **Dependencies:** predecessor card IDs plus unresolved design/native blockers. No dependency on a hoped-for alternative.
- **Evidence:** observable success and relevant failure/cancellation/stale-target cases, comparison rule, fixture provenance, exact commands and required native observations. Record not-applicable reasons.
- **Completion:** applicable automated checks and required observations passed, artifacts linked, inventory rows updated. Missing qualification leaves the capability and implementation ticket incomplete even when code may merge into `qt` under the accepted rule.

Target one agent session of coherent implementation and review. Split when a card introduces multiple independently reviewable operations, unrelated persistent formats or several new native owners. Share fixture infrastructure; do not split one atomic behavior into isolated UI/backend tickets that each claim success without integration. Evidence follow-ups may run on another host, but remain linked blockers of the originating capability. No test-count/coverage quota replaces behavior.

## Phase 1: build and evidence infrastructure

These are concrete first-card candidates **after Gate 0**, not work to start now. All inherit the card contract and pinned inputs above; platform slices retain their own qualification status.

| Card | Bounded outcome, dependency and exclusions | Required evidence |
| --- | --- | --- |
| B1 — artifact lock and dependency ownership | Promote the proven build-slice inputs into committed CMake locks/manifests/overlays. Depends on provisioning feasibility; exclude new package upgrades, installers and production UI. | Verify exact Qt installer/package/archive identities, vcpkg inputs and matching private FFMS2 headers/library; missing/hash-changed input fails explicitly. |
| B2-W / B2-U / B2-F — reconstruct one declared platform | Separate Windows, Ubuntu and Fedora cards consuming B1; declare compiler/SDK/host tools and run shared workflow presets. No substitute Qt source, bootstrap script or assumed credentials. | Empty/warm cache and offline-prefetched reconstruction, corrupt/missing artifact diagnostics, dependency origins and a minimal Qt/FFMS2/libass load. A success on one host closes only its slice. |
| E1 — fixture manifest and legacy capture route | Establish small licensed/synthetic fixtures, source hashes and reproducible legacy observation capture; depends on approved compatibility dispositions. Exclude declaring source-only defects reproduced. | Preserve original hashes; retain observed old result, approved new expectation and explicit inconclusive status. Characterize hazardous cases only with bounded copies. |
| E2 — one runner integration per tool/session | Separate cards wire CTest labels to GoogleTest, QtTest, Quick Test, Spix, accessibility, image comparison and calibrated performance infrastructure as needed. Depends on the relevant B2 slice; no production capability claim. | A controlled passing and failing observable case, bounded waits and retained artifacts; native UIA/AT-SPI and NVDA/Orca sessions are qualified separately from offscreen tools. Test hooks do not ship. |
| B3 — module skeleton and minimal application | Construct accepted target boundaries and explicit composition; depends on B2 and initial runner cards. Exclude placeholder implementations of unresolved behavior. | Core builds without Qt; dependency-direction checks, QML lint/compile/load and a minimal supported-platform startup. |

Latest MSVC/Windows SDK, official Qt account/license authorization and declared Linux tools remain prerequisites, not tasks silently performed by CMake. The follow-up build decision uses GitHub Actions for Linux validation and pins exact toolchain/runner inputs for qualifying runs. Matching MinGW and MSVC Qt 6.11.2 kits are installed and have built the bounded Grid specimen; those installed-kit observations do not substitute for reproducible full provisioning. Begin payload/license inventory in Phase 1; final installers come later.

## Phase 2: core and first complete editing workflow

| Card | Bounded outcome and dependencies | Exclusions and acceptance evidence |
| --- | --- | --- |
| C1 — typed time and lookup | Implement accepted checked values, rational conversions and distinct frame lookups; B3/E1/GoogleTest. | Exclude devices and new rounding policy. Boundary/overflow, negative/empty ranges, rational CFR/VFR and the three approved timing outcomes; retain old comparisons. |
| C2 — ASS structural load | Parse ordered sections/records, raw source spans and basic typed Line/Style fields; B3/E1. | Exclude export and unapproved parser corrections. Known/unknown/repeated sections, invalid bytes/diagnostics and untouched input hashes. SSA/TLMode/annotation edge families receive explicit sibling cards. |
| C3 — one-Line ASS save preservation | Encode unchanged input and a single changed Line with unrelated raw sections retained; C2/C1. | Exclude other formats and filesystem activation. Exact bytes only where contracted; semantic/normalization reports otherwise; C03 preservation fixtures. |
| A1 — one-Line command/draft transaction | Apply the accepted draft/commit/undo policy using stable IDs, explicit scope and target revision; C2 plus final transaction decision. | Exclude bulk commands and Lua. Commit, undo/redo, controlled composition-state guard, stale/protected rejection and saved-identity outcomes; actual native IME event ordering belongs to V2. |
| V1-M — read-only Qt projection | Expose core Lines and stable identities through the Qt model; B3/C2 and resolved Grid selection contract. | Exclude painting, shell and edits. Model notifications/tester, reset/lifetime and identity through filtering/order changes. |
| V1-G — painted Grid viewport | Implement the selected renderer against V1-M; resolved Grid feasibility/design. | Exclude row commands and shell integration. Visible-row/geometry correctness, scrolling/reuse and the applicable qualified rendering/resource evidence. |
| V1-A — native Grid semantics | Supply the chosen accessibility representation and keyboard focus over V1-G/V1-M. | Exclude whole-application accessibility. Independent UIA/AT-SPI and human screen-reader observations of the advertised navigation/selection; missing host evidence keeps this card incomplete. |
| V1-S — read-only Classic integration | Bind the real model/Grid to the minimal four-panel shell and explicit Editing/protected targets; B3/V1-M/G/A. | Exclude movable/floating implementation and persistence, which get separate docking cards from the accepted outcome. Verify target guards, labels, panel traversal and focus restoration; this is not a completed Workspace. |
| F1 — destination/write coordination | Implement the accepted permit, ordering and result-publication contract against a controllable file port; final lifecycle decision and B3/E1. | Exclude filesystem durability. Test revocation/authorization, stale destinations, overlapping writes and one terminal result; mocks establish only coordination behavior. |
| F2-W / F2-L — native write owner | Separate Windows/Linux adapters consuming F1, each bounded to a declared filesystem/environment and operation set. | Native copied-file temporary-write/replace/persistence outcomes, interruption and cleanup; no universal filesystem guarantee. Additional filesystem/failure cases get explicit sibling cards and capability blockers. |
| A2 — captured open/save application operation | Compose staged load and acknowledged write; C3/A1/F1 plus the required F2 slices. | Exclude Session/recovery bundles. Copied-file open/edit/save/reopen, stale revision/options-bound plan rejection, cancelled/failed write, external change, Save As association and publication races; inherit unresolved file-owner evidence rather than claim a mock-only pass. |
| V2 — edit, undo, save and reopen | Bind the detailed Line editor to A1/A2/V1-S using one source of truth. | Exclude claiming every tag/translation operation. Spix workflow and native text/IME/focus evidence, protected-target checks, unchanged unrelated source data and explicit error recovery. |

**V2-E — mapped editor native qualification.** The [mapped-editor ticket](https://github.com/altqx/hikari/issues/28) transferred these obligations by name; each becomes its own card or explicit V2 evidence, and a missing observation keeps the editor capability incomplete: E28-ime (actual Windows/Linux IME and candidate behavior), E28-rtl (bidi editing and shaping), E28-at (UIA/AT-SPI and NVDA/Orca), E28-grammar (complete ASS grammar, drawing/malformed spans, Style-aware restoration), E28-undo (production grouping under the accepted transaction policy), E28-shortcuts (complete editor shortcuts) and E28-perf (calibrated editing performance). The accepted boundary defaults, after-tag insertion and retained intervening tokens, are characterized as fixtures in the same cards.

This vertical workflow exposes integration faults early without declaring the editor complete. Build additional per-format reader/encoder cards for SRT, MicroDVD, MPL2, TMPlayer, plain text and SSA distinctions, then marker/translation/group variants. Each owns its loss/timing/encoding evidence; C02 preview approval never authorizes new conversion semantics. Add current behavior or obtain a named outcome for unresolved defects—do not implement a preferred correction under “cleanup.”

## Phase 3: native owners and paired workflows

Run independent adapters in parallel only after their interfaces and process choices settle. First cards expose one owned indexed frame, one libass overlay with font-byte lease, one bounded source-PCM range, one general-player delivered-frame result, and one isolated unchanged Lua dialog invocation. Each excludes the next composition step; each returns explicit unsupported/error/cancel outcomes and tests generation/lifetime release on its own native owner.

Then create separate integration cards for CPU/GPU overlay equivalence, renderer-selected collection/reimport, real PortAudio PCM/resampling, and general↔indexed transport handoff. Decode completion, scene consumption, physical display and device drain remain different evidence. The silent Windows callback/software sample cannot pass real output or Linux gates. Production media-process selection follows the backend decision; Lua helper isolation does not select it.

Automation integration splits into startup/registration/state, typed dialogs, media/font/clipboard services, staged edits and cancellation/crash/restart. Preserve native modules, FFI and DependencyControl and characterize unchanged API quirks; C06–C08 remain named decisions. External script effects cannot be rolled back by Document undo or helper termination. Each integration card includes a complete user workflow and its cancellation/error outcome, not only a successful IPC exchange.

## Phase 4: inventory-complete surfaces and carry-over

The following are **work packages to split**, not oversized implementation issues. For each, assign every exact inventory row/action/setting to a bounded operation card, plus its shared-service dependency and native evidence. Every row has one accountable owner and may link several contributing cards; multiple menu paths can share an action without disappearing from verification.

| Work package | Split by operation and retained scope |
| --- | --- |
| Grid/History | Selection/current/anchor; clipboard/paste-special; insert/delete/duplicate; split/join/sort; columns; filter/hidden scope; Line groups; history navigation. |
| Editor/translation | Metadata/time fields; tag insertion/wrapping; completion/custom buttons; raw/mapped input; spelling; text-role transfer/confirmation; comparison matching/navigation; export roles. |
| Audio/video/tools | Audition/selection/advance; waveform/spectrum/zoom; karaoke; player tracks/chapters/fullscreen/capture; each visual-tool family and presets, shared coordinate transforms. |
| Styles/fonts/utilities | Document Styles; catalog transfer/conflicts; font catalogs/picker/colors; collector; Script properties; resampling; complete Conversion options; MKV extraction. |
| Search/timing | Find/results; checked replace; external-file scope; select-lines; spelling/personal words; ordered correction rules; time shift/profiles; postprocessor; keyframe workflows. |
| Application continuity | New/open/reload/close; Session partial restore; autosave/recovery; cleanup; tasks/logs/errors; update notifications; Help/notices; Workspace persistence. |
| Settings/localisation/automation | Setting registry and routes; shortcut contexts/conflicts; one-shot importer/recovery; TS/QM migration/language switch/script lookup; manager/rerun/hotkeys and remaining script API services. |

Carry-over directly reads existing files/catalogs/Sessions/autosaves and unchanged scripts; settings/hotkeys use the one-shot importer with preserved originals and unresolved entries. Authored rules/dictionaries survive; legacy themes do not migrate. Give import interruption/idempotency/rollback and each preserved collection concrete cards. Never infer full coverage from a prototype's representative samples or the inventory's aggregate counts.

## Phase 5: qualification and replacement readiness

Apply Windows/Ubuntu merge checks and Fedora/native/nightly coverage throughout, not only at the end. Attach platform gaps to the affected capability. Then split final package work by artifact/platform: per-user Inno installer, portable ZIP, Linux AppImage and recovery tar, followed by clean-machine offline/install/upgrade/interruption/uninstall/relocation checks and source/license/signature verification. Keep full packages and notification-only updates; signing identities/accounts remain owner-controlled.

Bind reference machines, fixtures and calibration before performance qualification. Run the accepted native/accessibility/IME/render/resource matrix, long-lived cleanup and compatibility corpus against the candidate package. Failures create bounded repair/requalification work; they do not silently reduce thresholds or supported scope. Windows 10 stays first-class until its separately approved retirement and notice; later macOS remains possible without becoming an initial-delivery blocker.

Finally reconcile every inventory row, approved departure and required evidence link. Instantiate the accepted readiness/cutover checklist for one exact candidate; unresolved gates prevent stable completion. The plan ends with a reviewable candidate record, not an automatic release or `main` replacement. No phase, card, runner, fixture corpus or native gate is claimed completed here.

[ui]: https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md
[data]: https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md
[core]: https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md
