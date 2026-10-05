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

Draw with `Overlay` (lines, circles, text and filled polygons) in device pixels of the video window; the host converts to logical coordinates and paints it apart from the frame. Tests can swap in a tool with `VisualToolsController::setTool`.

## What T2 added for the families

T2 ([#177](https://github.com/altqx/hikari/issues/177), Position and Move: `visual_position.h`) needed more of legacy's `Visuals` than the crosshair did. These are shared by the later families:

- `visual_script.h`: what `Visuals` read from the script. `ScriptState` holds the Document, its Styles, SubsSize, the video's time and Timebase, the active Line as the Line editor holds it (the pending draft applied) and the text measure. Beside it are `linePosition` (GetPosition: the first `\pos` or `\move`, the tag's place, the default position with the stacking of unpositioned Lines), `posnScale` (GetPosnScale on the editor text, with its 7-value table kept by the tool between calls), `moveTimes` (GetMoveTimes from the video's frames), `calcMovePosition`, `textSize` (GetTextSize and GetDrawingSize through `TextMeasurePort`, legacy's swapped descent and leading kept), `changeText` / `replaceTag` (TagFindReplace's ChangeText and Replace) and the handles (`drawRect`, `drawCircle`, `drawCross`, `drawArrow`, `drawDashedLine`, `drawHelperLine`) in legacy's colours.
- `VisualHost` gives the video's time (`videoTimeMs`, VideoBox::Tell), its `timebase`, the `textMeasure` (the Qt port of GetLineTextExtents), the Grid's `ignoreFiltered` and `log` (HikariLog).
- `VisualTool` can override `warning()` (Position draws its warning only while none of its Lines is visible), `selected()` (legacy made a new tool on every family change, so its own state starts over; the toolbar's options stay), `blocked()` (the host calls it when it renders, or takes a pointer event, while the tool is blocked: legacy's Draw ran there, and Position's ends a helper-cross drag) and `options()` / `setOption()`: the rail's second row (legacy VideoToolbar's VisualItem), shown by `VisualToolOptions.qml` as toggles with their K1 icon and choices.
- `Pointer` has `rightDown` and the `DoubleClick` kind (legacy's LeftDClick; Qt sends it after the second press).
- The host shows the open gesture's staged texts on the video (`setPreview`, legacy's dummy rendering) and the committed Document after it; Esc resets the tool from its Lines.
- Keys reach the tool whether or not its pointer events are blocked (VideoBox::OnKeyPress, VideoBox.cpp:666-668).
- A tool decides when it reads its Lines again: legacy set Position again after its commits (SetModified, ShowEditOnVideo) but not Move (EditBox::Send and the batch's SetModified are dummies), so Move ignores the host's refresh after its own commit.

A family's options use the K1 roles the manifest names (frame-to-scale, scale-x, scale-y, two-points for T2); `VisualToolOptions.qml` maps each option's name to its role literally, so the icon test sees the roles placed; a family adding a toggle adds its name and role there, and `shell_tests`' `visualToolOptionIconsFollowTheModel` fails for any toggle whose shown role is not the one its `ToolOption::iconRole` gives.
