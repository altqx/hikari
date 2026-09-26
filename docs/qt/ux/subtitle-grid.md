# Subtitle grid

**Accepted direction: a custom painted grid.** altqx chose [“Use painted grid”](https://github.com/altqx/hikari/issues/27#issuecomment-5849499565); [ADR-0003](../../adr/0003-painted-subtitle-grid.md) records the decision. This supersedes the TableView-first recommendation in [Rendering a 50k-line subtitle grid in QML](https://github.com/altqx/hikari/blob/c9f7088a9c4a0c26cb858c9129a8d5d6caa21628/docs/research/qml-grid.md). It is a renderer direction, not approval of every prototype interaction or evidence that a performance threshold has passed.

## Evidence and implementation boundary

The [native prototype at d99b429e](https://github.com/altqx/hikari/blob/d99b429e7a6e16107fa6d6bf80cc5fba04a6264d/HikariSub/prototypes/qml-grid/README.md) compares a Python `QQuickPaintedItem` viewport with TableView using 50,000 generated ASS rows. Its Windows/D3D11 scroll samples measure Python-delivered frame-signal cadence. Both views remain allocated, so the samples establish neither a fair memory comparison nor optimized C++ performance. The separate [28 keyboard observations](https://github.com/altqx/hikari/blob/d99b429e7a6e16107fa6d6bf80cc5fba04a6264d/HikariSub/prototypes/qml-grid/evidence/keyboard-boundaries-windows.json) exercise 120 generated rows through native key events.

The production rendering API, text-layout/cache strategy and CPU/GPU work split remain implementation choices. The accepted direction does not require shipping Python, using `QQuickPaintedItem`, or retaining the prototype's fixed row height and six columns. The [UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md) remains the capability baseline; the small spike does not remove column controls, row operations, filtering, tree behavior, translation or comparison support.

## Interaction contract

Document-owned stable line IDs identify commands and selection. A displayed row number is a projection after sorting, filtering or grouping, never document identity. Preserve selected IDs across reorder/filter operations, visibly account for hidden selections, and make batch-command scope explicit. Inserting or deleting rows must not transfer selection to an unrelated line occupying the same position.

Keep three states distinct: the selected set for batch operations, the current line used for inspection/editing, and keyboard focus indicating where input goes. Focus changes do not change the document's **editing target**. A **protected reference** remains read-only even when its grid has focus or selection.

Arrow and page navigation operate on displayed order. Home/End reach its first/last row; Shift extends from the selection anchor using the same range semantics as arrows. Empty results leave retained selection intact without seeking an invalid row. Keep Ctrl selection and reveal-current behavior coherent with those rules. The spike falls back to the destination when filtering hides its range anchor; the production hidden-anchor policy still needs an explicit interaction fixture and review. Keyboard focus must remain reachable after filtering or group collapse, including when the current line is hidden.

## Text and visible state

Provide raw ASS and tag-hidden display without modifying authoritative source text. The prototype's stripping regex is fixture code: production display must use the defined ASS interpretation and invalidate cached text when the line or display mode changes. Preserve correct shaping and clipping for Arabic/Hebrew, CJK, combining marks, emoji and mixed-direction text. UI direction must not reorder source text, timestamps or document data.

Retain selected/current/comment and other applicable row-state cues. Colours need accompanying accessible state names and non-colour distinctions; define precedence when states overlap. Column sizing, scrolling, hit testing, focus indicators and header interaction must agree at fractional scaling and when columns are hidden. Dense presentation must not make commands available only through hover or a pointer.

## Required accessibility follow-up

The follow-up ticket [Prototype the painted grid's native accessibility and performance contract](https://github.com/altqx/hikari/issues/45) covers the focused native study before accepting the production grid. The existing painted spike exposes a pane and a current-line inspector, not a row/cell accessibility adapter; the inspector is not a substitute for navigating the table.

The follow-up must design the accessible table/cell hierarchy, logical headers and counts, names, selection/current/focus states, bounds, actions and change notifications. Its objects must continue to identify the correct source lines as rows move, disappear or enter the viewport. Verify keyboard entry/exit and offscreen navigation with NVDA on Windows and Orca on Linux, including multi-selection, sorting, hidden selections, grouped rows and empty results. Inspect actual native accessibility exposure rather than treating attached role/name properties as proof. Record any platform gaps with an explicit resolution plan.

## Acceptance still required

The [performance contract](../performance.md) and [test strategy](../testing.md) are accepted. Bind exact reference hosts and fixtures before applying the accepted workloads, percentiles and gates to representative ASS corpora, cold/warm load, scroll/jump, large selection, filter/sort, edits and long mixed-script rows. Measure input response, CPU/GPU costs, memory/cache growth, resizing and scaling on the supported Windows/Linux configurations.

No real-corpus budget, native assistive-technology, CPU/GPU or memory pass is claimed here. The renderer choice is settled; those engineering and interaction obligations remain release evidence to produce, with failures resolved explicitly rather than excused by the chosen direction.
