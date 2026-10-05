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

1. Implement `visual::VisualTool`: `family()`, `pointer()`, `overlay()`, and as needed `reset()` (legacy SetCurVisual: the view, script or active Line changed), `key()` (true when used), `values()` / `setValue()` (the numeric alternative shown below the canvas), and `warnsOutsideLine()` (true but for the crosshair).
2. Return it from `makeVisualTool()` in `src/application/visual_tools.cpp`.

The host gives a tool only what legacy's Visuals got: pointer events in device pixels over the video while a video is open, the Video panel's keys that no Video binding takes, and resets. A tool other than the crosshair gets nothing outside its Line's time or on a comment (Visuals::Draw's blockevents); the host draws the warning centred on the video unless VIDEO_VISUAL_WARNINGS_OFF (approved departure T1-warning-centre).

Every edit goes through a gesture:

- `host.beginGesture(host.batchTargets(), history)` on press (or a nudge's key press). The targets are fixed then: the batch picker's Lines when some are picked, else the active Line. Changing the picker, selection or active Line later never retargets it. It is refused for a protected session, a Document a macro holds, or targets that are gone.
- `gesture->before(line)` is the target as the gesture began (the pending draft applied); `stage(line, text, translation)` replaces the staged text on every sample. Nothing reaches the Document, the draft or the history meanwhile.
- `host.commitGesture()` on release: one history step named `history` (`familyInfo(family).history`, legacy SubsFile.cpp:228-238), after the pending draft's own step. Without staged changes nothing is recorded; a gesture whose Document changed since it began is refused.
- Esc during the gesture (the host handles it) drops it and leaves the pre-gesture draft.

Draw with `Overlay` (lines, circles, text) in device pixels of the video window; the host converts to logical coordinates and paints it apart from the frame. Tests can swap in a tool with `VisualToolsController::setTool`.

## Scale and the rotations (T3)

T3 ([#178](https://github.com/altqx/hikari/issues/178)) adds `ScaleTool` (`visual_scale.h`), `RotationZTool` and `RotationXYTool` (`visual_rotation.h`), ports of legacy Scale, RotationZ and RotationXY (VisualScale.cpp, VisualRotationZ.cpp, VisualRotationXY.cpp) with their toolbar items (ScaleItem, RotationZItem, RotationXYItem). What they share with their legacy bases, Visuals and TagFindReplace, is in `visual_transform.h` (namespace `visual::transform`): GetPosnScale, GetMoveTimes, CalcMovePos, GetPosition with GetDialogueAdditionalPosition, GetTextSize, ChangeOrg, FindTag, Replace, ReplaceAll, ChangeText, getfloat and the clip rewrites. T2's helpers port some of the same legacy functions; the two stay apart until the families share one module.

- **Two paths, as legacy's Visuals::SetVisual.** A gesture whose targets are the active Line alone edits the editor's text: the tag goes into the block at the editor's caret (FindTag's mode 0; the host's `editorSelection`, the Line editor's caret in the raw text), each sample from the previous one, and the caret is put at the tag afterwards (`setEditorSelection`). Any other target set (the batch picker's Lines) takes legacy's several-Line path: each target's text through ChangeVisual(txt, dial, n) from position 0, recomputed for the commit.
- **Options.** `options()` and `setOption()` are the rail's second row (`VisualToolOptions.qml`, a row above the values below the canvas, shown when the family has options): legacy's icons (K1's frame-to-scale, scale-x, link, scale-y, original-frame, tool-scale-rotation, resample, two-points), help texts as tooltips, the items' greying as disabled buttons and their links (the rectangle or "preserve proportions" switch "change all" on; the aspect ratio switches width on; RotationZ's "preserve proportions" switches "change all" on). A family's toggles stay when the family changes; its other state starts over (`selected`, legacy Visuals::Get).
- **Esc.** With a gesture open Esc drops it and the tool reads the unchanged text again (its handles go back). With none, Esc drops a tool's pending step (`cancelPending`): the first point of RotationZ's two-point angle, the card's evidence; legacy had no key for it.
- **Drawing.** Legacy's Direct3D shapes in device pixels: Scale's three arrows (DrawArrow) or its rectangles, RotationZ's ring, angle handle and \org cross or its two points, RotationXY's grid, axes and arrow cones projected as its matrices set up (`projectXY`, `drawXYGrid`: D3DXMatrixRotationYawPitchRoll, LookAtLH from z = -17.2, PerspectiveFovLH at 120 degrees, translated to \org in clip space, clipped at the near plane). Their colours are legacy's fixed overlay colours, not settings.
- **Evidence.** The `legacy_visual_t3_capture` probe ([local-t3-visual-20261005](../../tests/fixtures/legacy-observations/local-t3-visual-20261005/README.md)) runs the legacy tools unchanged; `VisualCapture.ReplaysTheLegacyT3Probe` replays every case and compares every Line, the editor's text and the handles exactly. `hikari_backends_visual_geometry_tests` renders the tools' edits with libass and checks the ink turns about the \org the tool draws and scales away from the position its arrows start from.
