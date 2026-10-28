# Interactive vector overlays for visual typesetting tools in Qt Quick

Research for altqx/hikari#12 (`visual-overlays`). Feeds: "Choose the video decode, playback and presentation pipeline" (#25) and the visual-tools redesign.

Status: completed desk research, 2026-09-27. Qt documentation resolves to 6.11.2. Current application inventory is at `20d647c4`. No renderer/input benchmark or visual parity run is claimed.

## Summary

Use a model of editable features in ASS/script coordinates, independent of the video presenter. For the first prototype, Qt Quick Shapes (requesting CurveRenderer where available) plus ordinary QML handles and a numeric inspector is the best balance of clarity and native interaction. Compare QQuickPaintedItem for a simple reference and custom scene-graph geometry only when large-path profiling justifies it. Rendering inside the video RHI pass should be a measured optimization, not the place that owns tool state or hit testing.

## Implications for the decision

This is a recommendation for #25 and visual-tool prototype tickets, not a final renderer decision. Prove coordinate agreement and input/undo behavior before optimizing. Agent-built QML/HTML mockups should elicit user reactions to tool handles, numeric alternatives and feedback; Figma remains Starter for occasional handoff. HTML can validate interaction vocabulary, but native QML is required for renderer, DPI and tablet evidence.

## Detailed findings

### 1. What the visual tools draw today

Current tools are not solely Direct2D: `VisualClips.cpp` builds vector control points, converts them through zoom-aware helpers, draws lines/curves and tests point proximity; `VisualRotationXY.cpp` directly issues D3D primitives. The `Visual*` family spans position/move/scale/XY/Z rotation, clips, drawing shapes and all-tags editing. Porting only GraphicsD2D would leave other rendering/input coupling behind. [Clip tools](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualClips.cpp), [XY rotation](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/VisualRotationXY.cpp), [visual interface](https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Visuals.h)

Recommended separation: tool command/state machine; ASS geometry/selection model; view transform; visual presentation. A drag previews a transaction, mouse/key release commits once, and Escape/capture loss cancels consistently. Numerical all-tags edits and direct manipulation should dispatch the same commands.

### 2. Qt Quick Shapes and the CurveRenderer

Shapes express move/line/quadratic/cubic/arc paths declaratively. The default geometry renderer triangulates on CPU; path edits can retriangulate. CurveRenderer uses specialized curve shaders, improves scale-independent antialiasing and avoids zoom-only tessellation work; cubic curves are approximated by quadratics. Request it via `preferredRendererType` (6.6+) and inspect `rendererType`, because unsupported requests fall back. Avoid assuming mathematically identical Bézier output at every scale. [Shape](https://doc.qt.io/qt-6/qml-qtquick-shapes-shape.html)

`ShapePath` provides fill rules, stroke widths/joins/caps and dashed styles; these cover clip boundaries, movement trajectories and rotation guides. ASS drawing commands still need explicit conversion; SVG/QPainter path syntax is not itself ASS syntax. For many points, cache parsed geometry and update only changed paths. Do not create an expensive complete control for every offscreen point. [ShapePath](https://doc.qt.io/qt-6/qml-qtquick-shapes-shapepath.html)

Default Shape containment is bounding-box based; `FillContains` tests filled interiors, not a generous interactive stroke/control-point target. Complex fill testing can be expensive. Prefer explicit handle hit regions and a controller's closest-segment test with a view-space tolerance. Shape's asynchronous preparation may improve responsiveness but can leave a pending visual result; avoid accepting a new invisible geometry state without feedback during a drag. [Shape containment/preparation](https://doc.qt.io/qt-6/qml-qtquick-shapes-shape.html#containsMode-prop)

### 3. QQuickPaintedItem with QPainter

QQuickPaintedItem gives familiar QPainter/QPainterPath drawing and raster antialiasing. Its standard image target incurs texture upload; large high-DPI canvases and continuous repaints can make that expensive. Qt 6.9+ can use an OpenGL FBO target, but that optimization is backend-specific and is not a general RHI acceleration guarantee. It remains useful for a quick reference renderer and low-complexity overlays. [QQuickPaintedItem](https://doc.qt.io/qt-6/qquickpainteditem.html)

Hit testing belongs to the geometry model in this option too. `QPainterPathStroker` can create a stroke-shaped region with a chosen tolerance; keep tolerance in logical view units or map it carefully into source units. A transparent painted item does not automatically expose its individual points and actions to assistive technology. [Path stroker](https://doc.qt.io/qt-6/qpainterpathstroker.html)

### 4. Custom scene-graph geometry

A QQuickItem can retain batched handle quads, line meshes and fills as QSG geometry. `QSGGeometry` exposes vertex/index buffers and drawing modes; CPU-side geometry preparation and dirty updates are explicit. This can avoid one-QObject-per-point overhead, but robust stroking/joins, curve subdivision, antialiasing, clipping and buffer reuse become our implementation. Do not rely on wide native line primitives for consistent cross-backend quality; extruded triangle strokes are a design option. [QSGGeometry](https://doc.qt.io/qt-6/qsggeometry.html), [scene-graph lifecycle](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)

For dense paths, use a spatial index or coarse bounds before precise point/segment tests, and retain double-precision source coordinates while deriving GPU coordinates relative to the viewport. Benchmark edit/rebuild cost separately from drawing unchanged geometry. Fewer draw calls alone does not prove better interaction latency.

### 5. Drawing inside the video item's RHI pass

QQuickRhiItem can render video, libass and tool geometry into one offscreen target across Qt's hardware backends, but its QRhi dependencies have limited compatibility guarantees. Folding tool visuals into this pass may reduce some composition work while forcing every handle update through a larger renderer and coupling tool tests to video resources. Keep tool state and command semantics outside that renderer even if drawing is later combined. [QQuickRhiItem](https://doc.qt.io/qt-6/qquickrhiitem.html)

Recommendation: initially layer the tools above the video with the same transform; keep handle labels/numeric controls in QML. Only merge rendering if traces show this layer is a material bottleneck. The overlay must remain usable while paused, with dummy video, after decode failure and during window recreation.

| Criterion | Shapes | PaintedItem | Custom QSG | Video RHI pass |
|---|---|---|---|---|
| Antialiasing | CurveRenderer built-in; verify cubics | QPainter raster quality; upload cost | Implement edge coverage/MSAA strategy | Implement in chosen pass |
| Many changing points | CPU prep and object/update overhead | Repaint/upload area | Batched data; own tessellation | Batched data; video coupling |
| Hit testing | FillContains or app geometry | App geometry/path stroker | App geometry | App geometry |
| High DPI / zoom | Transform-aware; handle policy ours | Allocate adequate raster resolution | Vertex/coverage policy ours | Buffer DPR and transform policy ours |
| Input/accessibility | QML handles integrate readily | Separate semantic controls | Separate semantic controls | Separate semantic controls |

All performance comparisons above describe cost centers, not measured rankings.

### 6. Input: pointer handlers, tablet, touch

Pointer handlers separate event recognition from visual items. `DragHandler` supports device/button filtering, grab policies and dragging without directly moving a target; `handlerPoint` exposes position, pressure and rotation when supplied by the device. Hardware and platform support determine which stylus properties exist. Keep a normal mouse path equivalent to the stylus path; pressure should be optional, not necessary to set a clip point. [PointerHandler](https://doc.qt.io/qt-6/qml-qtquick-pointerhandler.html), [DragHandler](https://doc.qt.io/qt-6/qml-qtquick-draghandler.html), [handlerPoint](https://doc.qt.io/qt-6/qml-qtquick-handlerpoint.html)

Recommended gesture policy: one pointer selects/drags a feature; explicit pan mode or a separate button pans; two-finger gestures manipulate the viewport only. Resolve grab conflicts with parent Flickable/pinch handlers explicitly. Handle cancellation, lost capture, leaving the window, context menus and tool switches during drag. Test tablet eraser/hover where supported, trackpads and touch; no physical-device test is claimed here.

**Coordinate contract.** Store model points in script coordinates. Compute one transform through script/video scaling, sample aspect, rotation if applicable, letterbox, zoom and pan into logical item coordinates; use its inverse for input. Distinguish ASS XY rotation/projective effects from the 2D overlay camera. Keep fractional values during edits and round only according to the selected ASS serialization rule. Use constant logical-pixel handle size/tolerance, so zooming does not make points impossible to select. At singular/extreme transforms disable ambiguous operations with an explanation. Qt's DPI model separates logical UI coordinates from raw pixel buffers. [High DPI](https://doc.qt.io/qt-6/highdpi.html)

### 7. Keyboard-accessible alternatives to dragging

Every drag action needs a command-level alternative: focus next/previous point; select by list; arrow-key nudge with visible step size and modifiers; edit X/Y/angle/scale fields; insert/delete points; change line/curve type; toggle clip inversion; reset/cancel/commit. For movement show start/end coordinates and timing fields; for rotation expose pivot and axes; for all-tags use a searchable inspector rather than forcing direct manipulation.

Expose semantic roles/names, current values, selected state and supported actions through accessible controls. A single painted shape with one label does not expose its internal handles. Prefer a synchronized point list/inspector so a screen reader need not navigate thousands of decorative nodes. Focus should return to the initiating control after commit/cancel; selection must be apparent without colour alone. [Accessible attached properties/actions](https://doc.qt.io/qt-6/qml-qtquick-accessible.html)

### 8. Prior art: Aegisub, Krita, Inkscape, MuseScore

Aegisub's inspected `VisualTool` implementation (`ce97a367c611f2433d26ad4b37a7dc8f11d55bfe`) separates selectable features, layer-based hit testing, drag updates, modifier behavior and capture-loss cleanup. Its wx-based implementation is interaction prior art, not evidence that a particular Qt renderer is fastest. [VisualTool](https://github.com/TypesettingTools/Aegisub/blob/ce97a367c611f2433d26ad4b37a7dc8f11d55bfe/src/visual_tool.cpp)

Krita's `KoToolBase` paints with QPainter and a view converter and receives unified pointer events for mouse/stylus plus keyboard operations. The reusable lesson is to keep coordinate conversion and tool interaction separate from canvas paint. Krita's painting stack is not a drop-in Qt Quick Shapes implementation. [KoToolBase](https://github.com/KDE/krita/blob/master/libs/flake/KoToolBase.h)

Inkscape's official dependency tracking identifies GTK/gtkmm and related libraries; this research found no primary evidence of a production Qt Quick port. Treat its vector-editor interaction ideas as relevant, but do not cite it as a Qt renderer precedent. [Inkscape dependencies](https://wiki.inkscape.org/wiki/Tracking_Dependencies)

MuseScore's current `AbstractNotationPaintView` (`1c81f0a6f3eeb1acff185b4b67569903faf1df36`) derives from Muse QuickPaintedView, implements QPainter drawing, keeps a view transform and routes input through a controller. It also hooks notation accessibility and a separate playback-cursor item. This shows a painted complex canvas can coexist with QML and explicit accessibility, not that custom paint supplies either automatically. [Notation view](https://github.com/musescore/MuseScore/blob/1c81f0a6f3eeb1acff185b4b67569903faf1df36/src/notationscene/qml/MuseScore/NotationScene/abstractnotationpaintview.cpp)

## Open questions

The capability comparison is complete. The native prototype should use fixed fixtures at 10/100/1,000/10,000 points, including cubic/spline conversion, self-intersecting fills and zoom extremes; record drag input-to-frame latency, geometry preparation time and repaint/upload work. Compare CurveRenderer and a painted reference visually at fractional DPI. Verify keyboard parity, numeric/source round trips, one-drag/one-undo, capture cancellation and NVDA/Orca access. User reaction decides tool density and gesture conventions; measurements decide whether QSG/RHI optimization is warranted.
