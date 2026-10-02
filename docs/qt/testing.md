# Rewrite test strategy

**Accepted on 2026-09-27 in the live review with altqx for [test strategy #34](https://github.com/altqx/hikari/issues/34).** The user accepted the complete test stack and evidence-gated completion rule from the [reviewed proposal](https://github.com/altqx/hikari/blob/cb3246ec5e058455a7978c2a8f8369338ca21e97/docs/qt/proposals/testing.md); [ADR-0015](../adr/0015-behavioral-and-native-test-evidence.md) records the decision. It applies the accepted [compatibility](compatibility.md), [build](build.md) and [performance](performance.md) contracts. The [completed testing research](https://github.com/altqx/hikari/blob/ee59a12872f3d9e0ddeb203e5fa3c09566b158ce/docs/research/qml-testing.md) supports these choices; no new runner, suite or reference machine has been qualified.

## Frameworks and boundaries

Use **CTest** as the common runner, reached through CMake workflow presets, with labels for core, golden, Qt, QML, integration, native, accessibility, visual and performance. Pin test tools in the declared build inputs; release packages exclude test hooks and servers.

| Layer | Concrete choice and contract |
| --- | --- |
| Plain C++ core | **GoogleTest**, with GoogleMock only at external service ports where controllable failure/timing is needed. Test parsing, time arithmetic, stable identity, command outcomes, undo, queue/cancellation and ownership through public behavior. |
| Qt adapters/models | **QtTest**, including signal observations and `QAbstractItemModelTester`; verify notifications, lifetime and asynchronous publication, not private call sequences. |
| QML components | **Qt Quick Test** with `TestCase`/`SignalSpy`, deterministic application-service fakes and bounded waits for observable completion. Cover focus, selection, shortcuts, cancellation and composition guards. |
| Whole application | **Spix** in an opt-in test build, using its C++ interface and stable object names for open/edit/undo/save, document switching, recovery and media handoff. Do not ship its RPC endpoint. |
| Native accessibility | **pywinauto UIA** on Windows and **dogtail/AT-SPI** on configured Linux desktops, independently checking roles, names, states, actions and focus. Human **NVDA/Orca** runs verify speech and usable navigation. |
| Visual/rendering | Qt Quick Test captures plus a C++/Qt image-comparison utility under CTest; a separate native libass fixture renderer compares normalized RGBA output. Keep expected/actual/diff artifacts. |
| Performance | CTest-labelled native workload drivers plus calibrated presentation/capture/loopback instrumentation implementing the accepted performance contract. Neither GoogleTest timing nor QML callbacks alone certify its endpoints. |

These are accepted selections, with integration and qualification still required. [GoogleTest](https://google.github.io/googletest/primer.html), [QtTest](https://doc.qt.io/qt-6/qtest-overview.html), [Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html) and [Spix](https://github.com/faaxm/spix/blob/57cf7105e1250b999104d6439df7f449ba1afe93/README.md) supply distinct mechanisms. Native accessibility tool/session feasibility remains a first implementation slice; no commercial Squish purchase is assumed.

The existing custom harness's 63 cases remain pinned characterization inputs. Translate relevant assertions into the new suite when their behavior is retained or explicitly corrected; do not port the old implementation into the rewritten core. Examples include selection identity surviving sorting, undo restoring content, and stale seek results being discarded. Avoid getters-only tests, duplicating algorithms inside assertions, or mocking every collaborator merely to assert that the implementation called itself.

## Fixtures, goldens and approvals

Store a fixture manifest with source/license provenance, immutable source hash or generator/seed, expected semantic state/diagnostics, serializer byte contract, dependency/font versions and reproduction command. Synthetic redistributable fixtures form public CI; restricted real-world cases need owner-approved storage and access, not silent omission or public upload. Small fixtures live in Git; large media use declared digest-verified artifacts and explicit offline availability.

Characterize unchanged files and Lua scripts against a pinned legacy executable/source/runtime. Preserve both old observations and new expectations linked to the [approved-departure ledger](compatibility-decisions.md). Unexecuted source findings are not runtime oracles; crashes/data-loss candidates require bounded copies and disposition. Compare semantic output unless exact serialization bytes are the contract, and verify original input hashes remain unchanged.

For libass images, pin fonts, face identities, provider/configuration, renderer/shaping versions, geometry and time. Attachment-only hermetic fixtures and native system-provider fixtures are separate: copying font files does not prove the same fallback policy. For QML images, also pin OS/backend, scale, viewport, style/theme, locale, clock, animation state and data. Prefer geometry/state assertions for interactions; images establish appearance.

Use exact normalized image equality within a qualified deterministic configuration initially. Any tolerance or backend-specific baseline needs a documented reason and independent review of the differences; no universal cross-platform pixel equality is assumed. Baseline updates include before/after/diff, the changed contract or dependency, and review evidence. Agents must not regenerate expectations merely to make failures disappear. New behavior/UX departures need the existing human approval; routine updates implementing an already accepted outcome need not reopen that decision.

## CI levels

| Trigger | Required work |
| --- | --- |
| Every change before merge | Windows x64 and Ubuntu core/golden/Qt/QML suites; invoke `all_qmllint`, compile/load QML, and run affected application integration/visual cases. Include native smoke when UI/media/integration changes touch it. |
| Nightly and relevant subsystem changes | Fedora coverage; Windows interactive and Linux X11/Wayland desktop suites; real GPU rendering, accessibility automation, unchanged Lua scripts and package smoke. Run qualified AddressSanitizer/UndefinedBehaviorSanitizer configurations for core/adapters; treat unsupported tool combinations explicitly. |
| Milestone/release candidate | Full supported-platform native regression, NVDA/Orca and real Japanese IME/RTL input, install/upgrade/offline/recovery checks, and all accepted performance/resource gates on bound reference hosts. |

Offscreen QML checks cannot pass native IME, dialogs, clipboard, GPU presentation or accessibility. `QT_QPA_PLATFORM=offscreen`, Qt Quick's software adaptation and an RHI software device are different configurations; log the actual platform/backend. Do not use `qmltc` as a blanket gate: registered types, lint, ordinary cache compilation and runtime loading are the baseline.

Performance hosts still need identification: four physical cores, 8 GiB, SSD, integrated graphics, 60 Hz is an accepted class, not a qualified machine. Fixture hashes, reference bindings and calibration precede qualifying results. Preserve the accepted cold/warm/cache-miss conditions, repetitions, percentiles, maximums and memory accounting without weakening thresholds to suit CI. Hosted-runner timing and the Python painted-grid sample remain diagnostic only.

On failure, retain traces, artifacts and environment metadata. A rerun diagnoses nondeterminism; it does not erase the initial failure. Quarantine requires an identified issue, owner and replacement coverage or explicit acceptance impact. A skipped/unavailable test is unverified, not green evidence.

## Evidence-gated completion

Each implementation ticket must link its accepted behavior and affected inventory/defect entries; ship the smallest meaningful regression evidence for that behavior, including applicable failure/cancellation boundaries; document fixture provenance and exact commands; pass relevant automated checks; and link required native/accessibility/performance observations or explicitly state why they do not apply. Reversible copy/layout-only changes can use focused inspection without invented unit tests. Prototype tickets retain their separate throwaway/human-reaction contract.

**Accepted rule: evidence-gated completion.** Code may merge into `qt` after its applicable merge checks while a named native/reference-runner follow-up is pending, but the affected implementation ticket and capability remain incomplete until required evidence passes. A merged change is not a completed capability when its required native, accessibility or performance observations are missing. Release gates remain mandatory.

First infrastructure work must qualify the tool integrations, native sessions, fixture store and performance reference bindings. No coverage percentage or test-count quota substitutes for these observable contracts. Acceptance records the policy only: no new test framework integration, native session, fixture corpus, calibrated reference host or runtime gate is claimed qualified or passed.

## Runner integration (E2, 2026-09-29)

[`cmake/HikariTesting.cmake`](../../cmake/HikariTesting.cmake) registers every runner with CTest labels and a bounded run time. Select a family with `ctest --preset <preset> -L <label>`.

| Runner | Helper | Labels | Modes |
| --- | --- | --- | --- |
| GoogleTest 1.18.0 (plain C++) | `hikari_add_gtest(target SOURCES … [LIBRARIES …] [LABELS …])`; each test case is its own CTest entry | `gtest` | none needed |
| QtTest (C++ Qt) | `hikari_add_qttest(target SOURCES … [LIBRARIES …])` | `qttest` plus the mode | `offscreen` always; `xvfb` when `xvfb-run` exists |
| Qt Quick Test (QML) | `hikari_add_quicktest(target QML_DIR dir)` over `tst_*.qml` | `quicktest` plus the mode | same |

Each runner has controls under `tests/runners/`, all labelled `control`: a case that must pass, and a deliberately failing case registered with `WILL_FAIL`. If a failing control is ever reported as a failure, the runner has stopped detecting failures. The Ubuntu CI job installs Xvfb, so the `xvfb` variants run there. The Fedora container runs offscreen only for now. Offscreen and Xvfb results are not native desktop, screen-reader or IME evidence; those stay separate human qualification cards.

## Accessibility checks and screen-reader sessions (E2-a11y)

**Automated tree checks.** `tests/support/a11y` (test-only) gives cards three helpers:
- `accessibleTree()` walks Qt's accessibility tree from a window: role, name and state for each node.
- `hasNode()` finds a node by role and name.
- `tabOrder()` lists accessible names in keyboard focus order.

Use `hikari_add_qttest(… LIBRARIES hikari_a11y_testing)` and the `a11y` label. The runner's control covers a text field, a button and a custom-drawn control that declares its own semantics; a failing twin asserts a name that does not exist. A tree check proves what Qt exposes in-process. It does not prove what a screen reader announces.

**Human sessions.** A card that claims screen-reader behavior (for example Grid V1-A-Q or editor V2-E-at) needs a recorded session per platform:

1. Build the exact commit with the platform's release preset and record its hash, OS version and screen-reader version.
2. **Windows:** NVDA with speech viewer enabled. **Linux:** Orca in a real desktop session (GNOME or KDE), not Xvfb.
3. Follow the card's scripted steps using only the keyboard. For each step, record the key pressed, the announced text (NVDA speech viewer or Orca's debug log) and whether it matches the card's expected announcement.
4. Record failures verbatim and file them as blockers of the implementing card. Never reduce the expected behavior to match the observation.
5. Attach the transcript to the card, and check off the matching obligation (for example G45-uia or E28-at).

Offscreen and Xvfb runs never substitute for step 2.

## Rendered-image comparison (E2-img)

The test-only `tests/support/image` library compares a rendered `QImage` with `tests/references/<name>.png` under an `ImageTolerance`: the largest allowed per-channel difference, and the share of pixels allowed to exceed it. A mismatch writes `<name>-actual.png` and `<name>-diff.png` to the test's artifact directory. `HIKARI_UPDATE_REFERENCES=1` rewrites references from actual images; regenerated references are reviewed and committed like code.

Image tests run offscreen only: a reference belongs to one renderer, and offscreen uses Qt's deterministic software rasterizer (an Xvfb/OpenGL run antialiased the same scene differently in CI). The runner's controls, labelled `image`:
- a Qt Quick scene of shapes rendered offscreen;
- an ASS vector drawing rendered by libass;
- a tolerance case;
- a failing twin rendering a different scene against the same reference.

The controls avoid host fonts. QML uses shapes only. libass, which needs one font even for drawings, gets Titillium Web (OFL) from the locked Qt examples as an in-memory font, with no system font provider. A guard asserts that the drawing actually paints pixels, so an empty render cannot match an empty reference.

## Performance harness (E2-perf)

`tests/support/perf` follows the [performance contract](performance.md)'s statistics: each benchmark runs independent repetitions (five by default), and nearest-rank p50/p95/p99, maximum and mean are reported per run, never pooled. The p95 spread across runs is reported alongside. Reports are JSON and carry the host's identity: CPU, logical cores, OS and compiler.

A host counts as calibrated only when its fingerprint is listed in `tests/perf/reference-hosts.json`, which is empty until reference hosts are bound. Results from any other host are labelled observations, never budget passes. Perf tests carry the `perf` label: the `*-verify` presets exclude them, and `ctest --preset <platform>-x64-perf` runs them. The Ubuntu CI job runs them and keeps `perf-report-core.json`.

The harness enforces the contract minimums for warm latency workloads: ten seconds of warmup and at least 1,000 timed operations. A spec below either one throws before it runs, and a `control` self-test checks this with the ordinary tests. Each report records the warmup used. Without the warmup, the first run of the ASS-load workload was about twice as slow as the rest (a 98% p95 spread on the Ubuntu runner).

First workloads, each 5×200 operations: loading a generated 50,000-Line ASS script (the G fixture size), NTSC `frameAtOrAfter` lookups timed in batches of 1,000 because one lookup is close to the clock's resolution, and painting the Grid over 50,000 Lines at 1280×720. The Ubuntu CI job keeps both `perf-report-core.json` and `perf-report-ui.json`.
