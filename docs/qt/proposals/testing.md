# Rewrite test strategy proposal

**Proposal for [Choose the test strategy for the rewrite](https://github.com/altqx/hikari/issues/34), awaiting acceptance.** It applies the accepted [compatibility](../compatibility.md), [build](../build.md) and [performance](../performance.md) contracts. The [completed testing research](https://github.com/altqx/hikari/blob/ee59a12872f3d9e0ddeb203e5fa3c09566b158ce/docs/research/qml-testing.md) supports these choices; no new runner, suite or reference machine has been qualified.

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

These are proposed selections, not demonstrated integrations. [GoogleTest](https://google.github.io/googletest/primer.html), [QtTest](https://doc.qt.io/qt-6/qtest-overview.html), [Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html) and [Spix](https://github.com/faaxm/spix/blob/57cf7105e1250b999104d6439df7f449ba1afe93/README.md) supply distinct mechanisms. Native accessibility tool/session feasibility remains a first implementation slice; no commercial Squish purchase is assumed.

The existing custom harness's 63 cases remain pinned characterization inputs. Translate relevant assertions into the new suite when their behavior is retained or explicitly corrected; do not port the old implementation into the rewritten core. Examples include selection identity surviving sorting, undo restoring content, and stale seek results being discarded. Avoid getters-only tests, duplicating algorithms inside assertions, or mocking every collaborator merely to assert that the implementation called itself.

## Fixtures, goldens and approvals

Store a fixture manifest with source/license provenance, immutable source hash or generator/seed, expected semantic state/diagnostics, serializer byte contract, dependency/font versions and reproduction command. Synthetic redistributable fixtures form public CI; restricted real-world cases need owner-approved storage and access, not silent omission or public upload. Small fixtures live in Git; large media use declared digest-verified artifacts and explicit offline availability.

Characterize unchanged files and Lua scripts against a pinned legacy executable/source/runtime. Preserve both old observations and new expectations linked to the [approved-departure ledger](../compatibility-decisions.md). Unexecuted source findings are not runtime oracles; crashes/data-loss candidates require bounded copies and disposition. Compare semantic output unless exact serialization bytes are the contract, and verify original input hashes remain unchanged.

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

## Completion rule and human choice

Each implementation ticket must link its accepted behavior and affected inventory/defect entries; ship the smallest meaningful regression evidence for that behavior, including applicable failure/cancellation boundaries; document fixture provenance and exact commands; pass relevant automated checks; and link required native/accessibility/performance observations or explicitly state why they do not apply. Reversible copy/layout-only changes can use focused inspection without invented unit tests. Prototype tickets retain their separate throwaway/human-reaction contract.

**Policy choice: recommend evidence-gated completion.** Code may merge into `qt` after its applicable merge checks while a named native/reference-runner follow-up is pending, but the affected implementation ticket and capability remain incomplete until required evidence passes. The alternative is marking implementation done after automated checks and deferring all native/performance qualification to release, accepting a larger late rework risk. Release gates never become optional under either choice.

First infrastructure work must qualify the tool integrations, native sessions, fixture store and performance reference bindings. No coverage percentage or test-count quota substitutes for these observable contracts.
