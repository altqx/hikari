# Visual tools: the rail, the shared transform and the tool interface

T1 ([#176](https://github.com/altqx/hikari/issues/176)) lays the ground the visual families T2-T6 build on. It follows [media.md](media.md) (tool state and handles kept apart from the decoded textures, one shared transform with its inverse), the accepted layout A of [reviewed-surface-layouts.md](ux/reviewed-surface-layouts.md) (a named rail beside the Video canvas, values below it) and the transaction rule of [edit-transactions.md](proposals/edit-transactions.md) and [surface-decision-routing.md](proposals/surface-decision-routing.md) (accepted on [#55](https://github.com/altqx/hikari/issues/55)).

## Where things live

| Part | File | Legacy |
| --- | --- | --- |
| Families, `VisualTool`, `VisualHost`, `Gesture`, `BatchPicker`, warnings | `src/application/include/hikari/application/visual_tools.h` | Visuals.h:56-68, VideoToolbar.cpp:44-54, Visuals::Draw / DrawWarning |
| The video window's geometry and the script-to-source-to-view transform | `visual_view.h` (`visual::VideoView`) | RendererVideo::UpdateRects, SetZoom, ResetZoom, Zoom, ZoomMouseHandle, SetVisualZoom; Visuals::SizeChanged, GetCalculatedIn/OutPos |
| The crosshair and VIDEO_COPY_COORDS | `visual_crosshair.h` (`CrosshairTool`, `copyCoordinatesText`) | VisualCross.cpp, VideoBox::OnCopyCoords |
| The host: pointer and key routing, Esc, the overlay, warnings, the batch picker, the frame's placement | `src/ui/visual_tools_controller.h` | VideoBox::OnMouseEvent / OnKeyPress, RendererVideo::SetVisual |
| The rail, the overlay and the values row | `src/ui/VisualToolRail.qml`, `VisualOverlay.qml`, `VisualToolValues.qml` | VideoToolbar, Visuals' drawing |

## The transform

`VideoView` works in the video window's device pixels, as legacy did, and keeps legacy's float steps and integer truncations: the fixtures replay the legacy probe's capture (`tools/legacy-capture/visual_capture.cpp`, `tests/fixtures/legacy-observations/local-t1-visual-20261005`) float for float, but for the approved departures T1-copy-coords-view and T1-wheel-zoom-stale ([compatibility-decisions.md](compatibility-decisions.md)). The host feeds it the panel's logical size and the window's device pixel ratio (`toDevice` / `toLogical`), the video's encoded size and SAR (the media helper's Open answer, so a decoder failure keeps the geometry), the script's PlayRes and the aspect override.

- `videoRect()` is where the frame is drawn (m_BackBufferRect) and `sourceRect()` the part of the frame drawn there (m_MainStreamRect, the zoom). The presenter takes both, so the frame, the subtitles and the tools agree.
- `scriptToView` and `viewToScript` are legacy's GetCalculatedInPos / GetCalculatedOutPos, the tools' transform and its inverse for hit testing. They use the zoom as of the last `refreshToolTransform()` (legacy SetVisualZoom), which every zoom change, resize, tool change and edit runs. Legacy's wheel zoom and zoom toggle skipped it, leaving the tools unzoomed until the next of the others; the approved departure T1-wheel-zoom-stale refreshes them.
- The crosshair keeps its own coefficients (VisualCross.cpp:80-92), legacy's, which differ from `viewToScript` by design (`crossCoefficients`, `crossScriptPoint`). VIDEO_COPY_COORDS copies the crosshair's position under the pointer as "x,y" (approved departure T1-copy-coords-view; legacy scaled by the whole window less one pixel, VideoBox.cpp:1395-1412).

## Adding a tool (T2-T6)

A family's card adds one class and one line; the rail, the routing and the drawing need no change.

1. Implement `visual::VisualTool`: `family()`, `pointer()`, `overlay()`, and as needed `reset()` (legacy SetCurVisual: the view, script or active Line changed), `key()` (true when used), `values()` / `setValue()` (the numeric alternative shown below the canvas), `options()` / `setOption()` (the family's own row, below), `previewLines()` (Lines the video renders after the Document's) and `warnsOutsideLine()` (true but for the crosshair).
2. Return it from `makeVisualTool()` in `src/application/visual_tools.cpp`.

The host gives a tool only what legacy's Visuals got: pointer events in device pixels over the video while a video is open, the Video panel's keys that no Video binding takes, and resets. A tool other than the crosshair gets nothing outside its Line's time or on a comment (Visuals::Draw's blockevents); the host draws the warning centred on the video unless VIDEO_VISUAL_WARNINGS_OFF (approved departure T1-warning-centre).

Every edit goes through a gesture:

- `host.beginGesture(host.batchTargets(), history)` on press (or a nudge's key press). The targets are fixed then: the batch picker's Lines when some are picked, else the active Line. Changing the picker, selection or active Line later never retargets it. It is refused for a protected session, a Document a macro holds, or targets that are gone.
- `gesture->before(line)` is the target as the gesture began (the pending draft applied); `stage(line, text, translation)` replaces the staged text on every sample. Nothing reaches the Document, the draft or the history meanwhile.
- `host.commitGesture()` on release: one history step named `history` (`familyInfo(family).history`, legacy SubsFile.cpp:228-238), after the pending draft's own step. Without staged changes nothing is recorded; a gesture whose Document changed since it began is refused.
- Esc during the gesture (the host handles it) drops it and leaves the pre-gesture draft.

Draw with `Overlay` (lines, circles, text, and filled polygons below or above the lines) in device pixels of the video window; the host converts to logical coordinates and paints it apart from the frame. Tests can swap in a tool with `VisualToolsController::setTool`.

## The tool's own row, the preview and the notices

A family's own buttons (legacy VideoToolbar's items for the toggled family, VisualItem) are `VisualTool::options()` and `setOption()`: a toggle (a mode), a choice, or an action (a button that acts at once). `VisualToolOptions.qml` shows them before the values row, with their K1 icon and legacy's help text; a role a family uses must be named on its `iconRole` line so `icon_tests` sees it placed.

While a gesture is open the video shows its staged texts, and a tool's `previewLines()` (the vector clip's mask) after the Document's Lines: legacy's "dummy" rendering (Visuals::RenderSubs, AppendClipMask). The host builds that Document (`VisualToolsController::subtitles`, and `setPreview` when it changes); nothing reaches the Document or its history.

A tool commits through the host and keeps its state afterwards: legacy never reset a tool after its own edit (EditBox::Send with visualdummy and SetModified with dummy skip ShowEditOnVideo's SetVisual, SubsGridBase.cpp:1147-1149), so the controller treats the revision its own commit made as seen. Another edit, an undo or another active Line resets the tool (legacy SetCurVisual); a cancelled gesture (Esc) resets it too, so its points and corners go back to the Line.

A notice legacy showed in a message box (the vector clip's "Double m" refusal) is `VisualHost::notice`, shown in the tool's row without blocking until the next click on the video; legacy's wxBell is `VisualHost::bell`.

## Clips (T4) and the vector point editor (T4, T5)

T4 ([#179](https://github.com/altqx/hikari/issues/179)) adds `RectangleClipTool` and `VectorClipTool` (`visual_clip.h`) and the vector point editor the drawing tool (T5) reuses (`visual_vector.h`).

| Part | File | Legacy |
| --- | --- | --- |
| Reading and writing a Line's clip, inverting, the mask's text | `visual_clip.h` (`clip::`) | ClipRect::SetCurVisual / ChangeVisual / InvertClip, DrawingAndClip::SetCurVisual / ChangeVectorVisual / InvertClip / CreateClipMask, Visuals::GetPosnScale's clip scale |
| The rectangle clip: drag, edges, corners, move, A/D/W/S, Invert clip, its mask | `RectangleClipTool` | ClipRect (VisualClipRect.cpp) |
| The vector clip: modes, Invert clip, the mask Line | `VectorClipTool` | DrawingAndClip as VECTORCLIP (VisualClips.cpp) |
| The points: parsing, writing, the modes' mouse, keys and wheel, selection, snapping, removal, drawing | `VectorEditor`, `parseVectorPoints`, `serializeVectorPoints`, `flattenCurve` | DrawingAndClip, ClipPoint, Visuals::GetVectorPoints / Curve / DrawRect / DrawCircle / DrawDashedLine, getfloat |

The editor holds legacy's points (`VectorPoint`: x, y, command, start, selected) and its state (the grabbed and hovered point, the selection box, the mode), takes pointer and key events in device pixels with a `VectorFrame` (DrawingAndClip's coeffW / coeffH divided by the scale, the drawing's _x / _y, the zoom and the video rectangle), and asks its owner to write the points: `apply(false)` while a gesture samples (legacy SetClip(true)) and `apply(true)` when it ends (SetClip(false)). For T5:

- Set `drawing = true` (Shift nudges by a tenth, legacy's VECTORDRAW).
- Give the frame the drawing's offset and the coefficients divided by its scale (`\fscx`, `\fscy`, `\p` as GetPosnScale computes them).
- Write the points with `serializeVectorPoints(points, "6.2f", offset)` into the `\p` block (ChangeVectorVisual's drawing branch); the shapes (VisualDrawingShapes) and the rotation are T5's own.

Every step keeps legacy's float arithmetic and int truncations, and the files are built without floating-point contraction. The `legacy_clip_capture` probe (`tools/legacy-capture/clip_capture.cpp`) runs the legacy VisualClipRect.cpp and VisualClips.cpp unchanged on `inputs/clip-cases.txt`; `hikari_application_visual_clip_tests` replays its observations ([local-t4-clip-20261005](../../tests/fixtures/legacy-observations/local-t4-clip-20261005/README.md)).

A gesture's targets follow legacy's two paths: with several Lines (the batch picker's; legacy several selected) each sample and the release rewrite every target from its own text; with one, each sample rewrites the text the previous sample left (legacy's Line editor) and the release commits it. A nudge (A/D/W/S, Delete) commits on its key's release, auto-repeat included, as the transaction rule says.
