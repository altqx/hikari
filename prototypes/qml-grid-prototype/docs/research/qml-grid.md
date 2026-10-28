# Rendering a 50k-line subtitle grid in QML

Research ticket: altqx/hikari#14 (map: altqx/hikari#1). Completed desk research, 2026-09-27. Qt documentation currently resolves to 6.11.2; APIs introduced later than a chosen baseline must be gated. No 50k-row benchmark was run in this research.

## Summary

Start the performance prototype with `TableView` + `QAbstractTableModel`, recycled lightweight delegates, deterministic column widths and document-owned selection. Use `TreeView` only if the product really requires hierarchical groups. A custom-painted or scene-graph grid remains a fallback if profiling demonstrates an unmet requirement; it carries substantial interaction and accessibility costs. This is a research recommendation, not proof of the performance bar or an architecture decision.

## Implications for the decision

The view can virtualize cells, but formatting, sorting, filtering, text layout, signals and selection can still scale with the full model. Therefore the downstream prototype must exercise actual ASS text, states, grouping and keyboard actions, not just 50,000 cheap numeric rows. A QML prototype is needed for Qt behavior/performance; HTML is useful for user reaction to layout but cannot validate Qt rendering. Figma stays on Starter, for occasional handoff only.

## Detailed findings

### What today's grid draws

At HikariSub commit `20d647c4`, `SubsGridWindow.cpp` performs custom painting, composes columns for line/layer/times/style/actor/margins/effect/CPS/wraps/text and optional original/translation, skips invisible lines, and handles selected/current/comment state. It also draws tree arrows and has separate comparison preview machinery. Preserve these as model semantics rather than porting paint-loop state into individual QML delegates. [Current source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp), [preview source](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridPreview.cpp)

### Option A: TableView + HeaderView + QAbstractTableModel

`TableView` loads the visible region and recycles delegates by default. Reused delegates get new model values; private state must be reset, and pooled timers/bindings can remain active. `columnWidthProvider`, explicit widths, keyboard navigation, selection models and edit delegates cover the required building blocks. Model virtualization is not a guarantee about expensive role computation. A width provider returning zero hides a column; call `forceLayout()` when provider results change. [TableView](https://doc.qt.io/qt-6/qml-qtquick-tableview.html)

`HorizontalHeaderView` synchronizes with a table and uses model header data; its header proxy has its own model indexes. Avoid treating header indexes as document-cell indexes. Persist widths by logical column identity, not display position. [HeaderView](https://doc.qt.io/qt-6/qml-qtquick-controls-horizontalheaderview.html)

The C++ model should expose stable line IDs, raw ASS text, display text, row-state flags and editing roles. Implement `rowCount`, `columnCount`, `data`, `headerData`, and `setData`/editable flags as needed. Qt requires model calls on the owning thread; workers may compute immutable results and queue precise changes to it. Avoid full resets for each keystroke. [QAbstractTableModel](https://doc.qt.io/qt-6/qabstracttablemodel.html)

`ItemSelectionModel` distinguishes current index from selected indexes and supports ranges and selection flags. Keep bulk selection as ranges/IDs rather than eagerly binding every `selectedIndexes` element into JavaScript. Define how hidden/filtered rows participate in selection. A `SelectionRectangle` supports pointer range selection; it does not replace keyboard selection, row commands or a focused current line. [Selection model](https://doc.qt.io/qt-6/qml-qtqml-models-itemselectionmodel.html), [SelectionRectangle](https://doc.qt.io/qt-6/qml-qtquick-controls-selectionrectangle.html)

Use a C++ `QSortFilterProxyModel` or purpose-built projection for filtering. Map proxy indexes to document IDs for commands and preserve IDs through resorting. Dynamic filtering and recursive filtering have separate semantics; test parent visibility and selection after group collapse. [Proxy model](https://doc.qt.io/qt-6/qsortfilterproxymodel.html)

Recommended row design: fixed-height compact rows initially, state backgrounds/icons plus text labels for screen readers, and one delayed hover preview shared by the view. Cancel stale preview work on row change; offer the same preview by keyboard. Compute tag-stripped text with the ASS parser and cache by line revision/display mode. Escape text before producing styled spans. `Text` supports plain/styled/rich text and RTL-aware alignment; rich text and wrapping should be restricted to where they provide user value. Arabic/Hebrew content direction and the direction of the surrounding UI are separate choices. [Text](https://doc.qt.io/qt-6/qml-qtquick-text.html)

Version details matter: the documented `selectionMode` is since 6.6 and table edit triggers since 6.5. The convenient `TableViewDelegate` is since 6.9; it supplies selected/current/editing state and an editable text delegate, but an earlier baseline needs its own delegate. Use plain cells for times/metadata and the dedicated ASS editor for complex text rather than silently treating every cell as an unconstrained editor. [TableView](https://doc.qt.io/qt-6/qml-qtquick-tableview.html), [TableViewDelegate](https://doc.qt.io/qt-6/qml-qtquick-controls-tableviewdelegate.html)

### Option B: TreeView for grouping

Qt Quick `TreeView` is available since 6.3 and inherits `TableView`. It flattens a `QAbstractItemModel` tree through an internal proxy; delegates receive depth/expanded information, and the first column renders indentation. Its visual row is therefore not necessarily the model row. Model indexes/line IDs must drive editing and selection. [TreeView](https://doc.qt.io/qt-6/qml-qtquick-treeview.html)

Recommendation: represent group nodes explicitly, with meaningful accessible names and expansion actions. Decide whether group selection selects descendants and whether sorting can cross groups. For merely filtered sections or collapsible presentation, compare a flat projection before taking on mutable tree semantics. Table and tree variants should share the same command model so a UI experiment cannot corrupt document ordering.

### Option C: a single custom-painted item

`QQuickPaintedItem` uses QPainter; its normal image path uploads the raster result as a texture. Since 6.9, an FBO target can accelerate painting on OpenGL, but that optimization does not generalize to every RHI backend. Large dirty regions, scrolling text and high DPI can increase raster/upload work. It can be a simple prototype or a fallback, but a single item is not inherently faster. [QQuickPaintedItem](https://doc.qt.io/qt-6/qquickpainteditem.html)

A scene-graph implementation can retain rectangles/lines and text nodes for visible rows. Public `QSGTextNode` exists since Qt 6.7 and is created by `QQuickWindow::createTextNode`; it accepts already-laid-out `QTextLayout`/documents. Do not copy old private `QSGTextNode` internals into the application. `QTextLayout` supports caching and shaped glyph runs, but its documentation warns glyph extraction is expensive; cache by text/font/width/direction and invalidate deliberately. [QSGTextNode](https://doc.qt.io/qt-6/qsgtextnode.html), [QTextLayout](https://doc.qt.io/qt-6.8/qtextlayout.html)

The tradeoff is ownership of scrolling, clipping, hit testing, keyboard ranges, header resizing, editing placement, hover, focus and accessibility. A useful middle path is a custom rich-text cell inside a standard virtualized view, if profiling isolates text formatting as the bottleneck. That is a hypothesis to test, not a measured speedup.

### Benchmarks and real apps with large QML tables

Qt's item-view engineering article documents the integrated TableView/TreeView work and selection improvements; its earlier performance article describes the redesigned virtualized TableView. Neither establishes HikariSub's 50k-line latency, mixed-script layout costs or memory use. Qt's `qmlbench` offers repeatable whole-stack benchmarking methodology, not a ready-made result for this workload. [Qt item views](https://www.qt.io/blog/item-views-in-qt-quick), [Qt performance background](https://www.qt.io/blog/2018/11/16/qt-quick-performance-improvements-qt-5-12-lts), [qmlbench](https://github.com/qt-labs/qmlbench)

A primary-source search did not locate a reproducible published benchmark with the same row features and platform matrix. Examples demonstrate API use, not scale guarantees. QWidget `QTableView` numbers and old Controls 1 TableView anecdotes are not interchangeable with Qt Quick TableView. No app-scale or FPS numbers are claimed here.

Proposed prototype measurements (targets to confirm, not observed results):

| Scenario | Record | Failure to investigate |
|---|---|---|
| 1k/50k/100k rows, cold and warm | time to first usable frame, RSS, live delegates | Delegate count grows with all rows |
| Continuous scroll, PageDown, last-row jump | frame-time p50/p95/p99, stalls, model-call counts | Formatting/binding/layout work blocks input |
| 50k range selection, filter, group collapse | command latency, selected-ID correctness | O(n) JS materialization or wrong proxy mapping |
| Long ASS, CJK/RTL/emoji, tags hidden | glyph/cache cost, clipping, correct preview | Missing shaping or per-frame parsing |
| 100/125/150/200% DPI, resizing/hiding columns | frame timings and screenshots | Width oscillation, bad clipping or hit targets |
| Screen reader + keyboard | announced row/column/state, focus and actions | Offscreen/recycled cells inaccessible or stale |

Run a release build on named hardware with the actual Qt minor version, RHI backend, viewport size and data generator committed beside results. Record a 60 Hz frame-budget goal (16.7 ms) if that is the product target; do not declare it met from average FPS alone. Use the QML profiler to locate binding and creation work before replacing the view. [Qt performance guidance](https://doc.qt.io/qt-6/qtquick-performance.html)

### Accessibility

A painted surface does not expose individual rows/cells merely by drawing them. A custom grid must implement an accessible object hierarchy and table/cell interfaces, including row/column counts, selection and model-change events. `QAccessibleTableInterface` explicitly models those operations. A standard view with control delegates reduces this work but still needs runtime inspection on the chosen Qt version and platform; do not infer complete screen-reader support from visual selection. [QAccessibleTableInterface](https://doc.qt.io/qt-6/qaccessibletableinterface.html)

Expose state as text (comment, collision, unconfirmed, search match), include logical line number and relevant headers, keep current focus separate from multiple selection, and retain a reachable command for every hover action. Verify NVDA/UIA on Windows and Orca/AT-SPI on Linux, including filtered/grouped and recycled offscreen rows. This is a required prototype gate, not a completed accessibility certification.

## Open questions

Research is complete at the feasibility/comparison level. The downstream grid prototype must choose an agreed latency/memory budget and evaluate real data. User reactions must settle grouping semantics, density, column defaults, state precedence and preview behavior. Only measured failure should justify the substantially larger custom-grid implementation.
