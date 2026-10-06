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
- A change of the active Line or of the editing target drops an open gesture the same way, and the tool is reset to the new Line (legacy's SetVisual dropped the tool's unsent preview). The gesture is never moved to the new Line.

Draw with `Overlay` (lines, circles, text, and filled polygons below or above the lines) in device pixels of the video window; the host converts to logical coordinates and paints it apart from the frame. Tests can swap in a tool with `VisualToolsController::setTool`.

## What T2 added for the families

T2 ([#177](https://github.com/altqx/hikari/issues/177), Position and Move: `visual_position.h`) needed more of legacy's `Visuals` than the crosshair did. These are shared by the later families:

- `visual_script.h`: what `Visuals` read from the script. `ScriptState` holds the Document, its Styles, SubsSize, the video's time and Timebase, the active Line as the Line editor holds it (the pending draft applied) and the text measure. Beside it are `linePosition` (GetPosition: the first `\pos` or `\move`, the tag's place, the default position with the stacking of unpositioned Lines), `posnScale` (GetPosnScale on the editor text, with its 7-value table kept by the tool between calls), `moveTimes` (GetMoveTimes from the video's frames), `calcMovePosition`, `textSize` (GetTextSize and GetDrawingSize through `TextMeasurePort`, legacy's swapped descent and leading kept), `changeText` / `replaceTag` (TagFindReplace's ChangeText and Replace) and the handles (`drawRect`, `drawCircle`, `drawCross`, `drawArrow`, `drawDashedLine`, `drawHelperLine`) in legacy's colours.
- `VisualHost` gives the video's time (`videoTimeMs`, VideoBox::Tell), its `timebase`, the `textMeasure` (the Qt port of GetLineTextExtents), the Grid's `ignoreFiltered` and `log` (HikariLog).
- `VisualTool` can override `warning()` (Position draws its warning only while none of its Lines is visible), `selected()` (legacy made a new tool on every family change, so its own state starts over; the toolbar's options stay), `blocked()` (the host calls it when it renders, or takes a pointer event, while the tool is blocked: legacy's Draw ran there, and Position's ends a helper-cross drag) and `options()` / `setOption()`: the rail's second row (legacy VideoToolbar's VisualItem), shown by `VisualToolOptions.qml` as toggles with their K1 icon and choices.
- `Pointer` has `rightDown` and the `DoubleClick` kind (legacy's LeftDClick; Qt sends it after the second press).
- The host shows the open gesture's staged texts on the video (`setPreview`, legacy's dummy rendering) and the committed Document after it; Esc resets the tool from its Lines.
- Keys reach the tool whether or not its pointer events are blocked (VideoBox::OnKeyPress, VideoBox.cpp:666-668).
- A tool decides when it reads its Lines again: legacy set Position again after its commits (SetModified, ShowEditOnVideo) but not Move (EditBox::Send and the batch's SetModified are dummies), so Move ignores the host's refresh after its own commit.

A family's options use the K1 roles the manifest names (frame-to-scale, scale-x, scale-y, two-points for T2); `VisualToolOptions.qml` lists the roles the families' options show literally, so the icon test sees the roles placed; a family adding a toggle adds its role there, and `shell_tests`' `visualToolOptionIconsFollowTheModel` fails for any toggle whose shown role is not the one its `ToolOption::iconRole` gives.

## Scale and the rotations (T3)

T3 ([#178](https://github.com/altqx/hikari/issues/178)) adds `ScaleTool` (`visual_scale.h`), `RotationZTool` and `RotationXYTool` (`visual_rotation.h`), ports of legacy Scale, RotationZ and RotationXY (VisualScale.cpp, VisualRotationZ.cpp, VisualRotationXY.cpp) with their toolbar items (ScaleItem, RotationZItem, RotationXYItem). What they share with their legacy bases, Visuals and TagFindReplace, is in `visual_transform.h` (namespace `visual::transform`): GetPosnScale, GetMoveTimes, CalcMovePos, GetPosition with GetDialogueAdditionalPosition, GetTextSize, ChangeOrg, FindTag, Replace, ReplaceAll, ChangeText, getfloat and the clip rewrites. T2's helpers port some of the same legacy functions; the two stay apart until the families share one module.

- **Two paths, as legacy's Visuals::SetVisual.** A gesture whose targets are the active Line alone edits the editor's text: the tag goes into the block at the editor's caret (FindTag's mode 0; the host's `editorSelection`, the Line editor's caret in the raw text), each sample from the previous one, and the caret is put at the tag afterwards (`setEditorSelection`, only when the commit went through; a refused commit leaves the caret and the tool reads the unchanged text again). Any other target set (the batch picker's Lines) takes legacy's several-Line path: each target's text through ChangeVisual(txt, dial, n) from position 0, recomputed for the commit.
- **Options.** `options()` and `setOption()` are the rail's second row (`VisualToolOptions.qml`, a row above the values below the canvas, shown when the family has options): legacy's icons (K1's frame-to-scale, scale-x, link, scale-y, original-frame, tool-scale-rotation, resample, two-points), help texts as tooltips, the items' greying as disabled buttons and their links (the rectangle or "preserve proportions" switch "change all" on; the aspect ratio switches width on; RotationZ's "preserve proportions" switches "change all" on). A family's toggles stay when the family changes; its other state starts over (`selected`, legacy Visuals::Get).
- **No reset after their own step.** The host resets a tool on every new revision; these three return true from `keepsStateAfterCommit()`, so the controller counts their own step's revision as seen. Legacy sent these edits with the visual dummy flag and ran no SetVisual after them (Visuals.cpp:791-829, SubsGridBase.cpp:1125-1157), so the tool kept the caret at the tag, its angles, its press point and its rectangle.
- **Esc.** With a gesture open Esc drops it and the tool reads the unchanged text again (its handles go back). With none, Esc drops a tool's pending step (`cancelPending`): the first point of RotationZ's two-point angle, the card's evidence; legacy had no key for it.
- **Drawing.** Legacy's Direct3D shapes in device pixels: Scale's three arrows (DrawArrow) or its rectangles, RotationZ's ring, angle handle and \org cross or its two points, RotationXY's grid, axes and arrow cones projected as its matrices set up (`projectXY`, `drawXYGrid`: D3DXMatrixRotationYawPitchRoll, LookAtLH from z = -17.2, PerspectiveFovLH at 120 degrees, translated to \org in clip space, clipped at the near plane). Their colours are legacy's fixed overlay colours, not settings.
- **Evidence.** The `legacy_visual_t3_capture` probe ([local-t3-visual-20261005](../../tests/fixtures/legacy-observations/local-t3-visual-20261005/README.md)) runs the legacy tools unchanged; `VisualCapture.ReplaysTheLegacyT3Probe` replays every case and compares every Line, the editor's text and caret, the commits (SetModified and Send against the history steps), the log and the handles exactly; `VisualOverlay` checks the ring against RotationZ::DrawVisual's formulas and the X/Y grid against the D3DX pipeline worked by hand. `hikari_backends_visual_geometry_tests` renders the tools' edits with libass and checks the ink turns about the \org the tool draws and scales away from the position its arrows start from.

## The tool's own row, the preview and the notices

A family's own buttons (legacy VideoToolbar's items for the toggled family, VisualItem) are `VisualTool::options()` and `setOption()`: a toggle (a mode), a choice, or an action (a button that acts at once). `VisualToolOptions.qml` shows them in a row above the values, with their K1 icon and legacy's help text; a role a family uses must be named on its `iconRole` line so `icon_tests` sees it placed.

While a gesture is open the video shows its staged texts, and a tool's `previewLines()` (the vector clip's mask) after the Document's Lines: legacy's "dummy" rendering (Visuals::RenderSubs, AppendClipMask). The host builds that Document (`VisualToolsController::subtitles`, and `setPreview` when it changes); nothing reaches the Document or its history.

A tool commits through the host; the clips, as T3's three, keep their state afterwards (`keepsStateAfterCommit()`): legacy never reset them after their own edit (EditBox::Send with visualdummy and SetModified with dummy skip ShowEditOnVideo's SetVisual, SubsGridBase.cpp:1147-1149), so the controller treats the revision their own commit made as seen. Another edit, an undo or another active Line resets the tool (legacy SetCurVisual); a cancelled gesture (Esc) resets it too, so its points and corners go back to the Line.

A notice legacy showed in a message box (the vector clip's "Double m" refusal) is `VisualHost::notice`, shown as legacy's modal message box titled "Warning" (VisualClips.cpp:1013-1016) until OK; legacy's wxBell is `VisualHost::bell`.

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

## Drawing and shape presets (T5)

T5 ([#180](https://github.com/altqx/hikari/issues/180)) adds `DrawingTool` (`visual_drawing.h`), legacy's Shapes over DrawingAndClip as VECTORDRAW, and the shape presets with their "Vector shape editing" dialog (`shape_presets.h`, `src/ui/shape_editor.h`, `ShapesEditionDialog.qml`).

| Part | File | Legacy |
| --- | --- | --- |
| Reading a Line's drawing: \pos / \move, \fscx / \fscy, the \p scale, \an, \frz, \org, the drawing (ParseTags' "pvector") | `drawing::readLine`, `drawing::parseTags` | Visuals::GetPosnScale, DrawingAndClip::SetCurVisual, Dialogue::ParseTags, GetDefaultPosition |
| Writing it: \p1, \pos and \an added when missing, plain text put in blocks, "{\p0}" after a drawing that ends the text | `drawing::putDrawing` | DrawingAndClip::ChangeVectorVisual (drawing branch) |
| The alignment's offset, the rotation, a \move drawing following the video's time | `drawing::drawingSize`, `drawing::rotate`, `drawing::movePosition` | Visuals::CalcDrawingSize, RotateDrawing, CalcMovePos (DrawVisual) |
| Free drawing: T4's `VectorEditor` with six modes (no Invert clip), Shift nudging by a tenth | `DrawingTool` | DrawingAndClip (VECTORDRAW), VectorItem(false) |
| Shapes: the rectangle a drag draws (edges, corners, inside), keeping the shape's proportions in "Preserve aspect ratio" (Shift toggles it), the shape written scaled into it, \fscx / \fscy for "Changing scale" | `DrawingTool` (`shapePointer`, `shapeBody`) | Shapes (VisualDrawingShapes.cpp:355-776) |
| The presets: legacy's five defaults, Config/ShapesSettings.txt read and written as legacy did | `defaultShapePresets`, `parseShapePresets`, `writeShapePresets` | LoadSettings, SaveSettings |
| The dialog: Add shape, Delete shape, the list's question, Get shape from active line, Apply, OK, Cancel, Restore default | `ShapesEdition` (application), `ShapeEditor` (QML model), `ShapesEditionDialog.qml` | ShapesEdition |

The tool's row is VectorItem for the drawing: the six point modes (the K1 roles vector-drag to vector-delete) and the shape list ("Choose", the presets' names, "Edit"). Legacy's list is a text choice in the row (VideoToolbar.cpp:503-512); the row here is icon-only, as every tool strip, so the list is a `ToolOption::Kind::Choice` with the role shape-presets that `VisualToolOptions.qml` shows as an icon button opening a ShellMenu of the entries (the chosen one checked, "Edit" after a separator); the button is on while a preset is chosen, and its tooltip and accessible description name it. With a preset chosen the modes take no click and show none pushed. "Edit" opens the dialog on a copy of the presets; OK gives them back to the tool and writes the file beside the settings (UTF-8 with a BOM), Cancel drops them. The presets are the host's (`VisualHost::shapePresets`, loaded at the first use as VideoToolbar::GetShapesSettings did), and the video's time is `VisualHost::videoTimeMs`.

A single Line's gesture follows legacy's preview text (dummytext): the first sample after a reset rewrites the Line's text with ChangeVectorVisual, the next ones replace only the drawing where it went, and the release commits it. Several Lines (the batch picker's) are each rewritten from their own text on every sample and on release. Saving a preset under the name of another (ignoring case) asks "A shape named "…" already exists." with Replace and Rename (accepted on #55); legacy kept both.

The `legacy_drawing_capture` probe (`tools/legacy-capture/clip_capture.cpp` built with PROBE_DRAWING) runs the legacy VisualClips.cpp and the Shapes and preset functions of VisualDrawingShapes.cpp unchanged on `inputs/drawing-cases.txt`; `hikari_application_visual_drawing_tests` replays its observations ([local-t5-drawing-20261005](../../tests/fixtures/legacy-observations/local-t5-drawing-20261005/README.md)) and `hikari_backends_visual_drawing_render_tests` checks the tool's points against libass's render of the drawing.

## The Position shifter and the all-tags tool (T6)

T6 ([#181](https://github.com/altqx/hikari/issues/181)) adds the last two families: `PositionShifterTool` (`visual_shift.h`, legacy MoveAll) and `AllTagsTool` (`visual_all_tags.h`, legacy AllTags with its sliders), with the all-tags tool's definitions and their "Tag editing" dialog (`all_tags.h`, `src/ui/all_tags_editor.h`, `AllTagsEditionDialog.qml`).

| Part | File | Legacy |
| --- | --- | --- |
| The shifter: the active Line's position, \move points, clips, drawings and \org as handles; a drag (Shift locks an axis, the right button takes the first handle) or A/D/W/S (Shift a tenth) moves every chosen kind in every target | `PositionShifterTool` | MoveAll (VisualMoveAll.cpp), MoveAllItem (VideoToolbar.cpp:354-414) |
| The definitions: legacy's 22 defaults, Config/AllTagsSettings.txt read and written as legacy did ("HYDRA2.0" first, UTF-8 with a BOM, an older file replaced by the defaults) | `defaultAllTags`, `parseAllTags`, `writeAllTags` | LoadSettings, SaveSettings, GetNames (VisualAllTagsEdition.cpp:457-575) |
| The all-tags tool: a slider per value of the chosen definition, its thumb at the Line's value (the tag found at the caret or from the start, else the Style's); a thumb drag, a track click or the wheel writes the tag by the change option; Shift and the wheel step through the definitions; the right button drags the sliders; the Line editor's "Insert difference" keys put the video's time into a \fad or \t | `AllTagsTool` | AllTags (VisualAllTags.cpp), AllTagsSlider (VisualAllTagsControls.cpp), AllTagsItem (VideoToolbar.cpp:646-824) |
| The "Tag editing" dialog: Add tag, Delete tag, the list's "Save changes" question, the fields (legacy NumCtrl's number fields), Apply, OK, Cancel, Restore default | `AllTagsEdition`, `NumberField` (application), `AllTagsEditor` (QML model), `AllTagsEditionDialog.qml` | AllTagsEdition (VisualAllTagsEdition.cpp:155-455) |

**Targets.** Both tools edit the batch picker's Lines that the Grid shows (`VisualHost::lineShown`): legacy's SubsFile::GetSelections left out the Lines a filter or a closed Line group hides (SubsFile.cpp:503-512), so a picked Line that is hidden keeps its text. The all-tags tool takes legacy's two paths as T3's tools do: the active Line alone edits the Line editor's text (Insert at the caret, or as the definition's placing says), any other set each target's text from its start; Multiply, Multiply+ and the line gradients count the shown targets.

**Commits.** A shifter release or key is one "Visual position adjustment tool" step and the tool reads its Lines again (legacy's SetModified ran SetVisual); an all-tags release, track click, wheel step or hotkey is one "Visual Hydra tool" step and the tool keeps its state (`keepsStateAfterCommit`, legacy's visual dummy). A commit that would change no Line records nothing (`Gesture::changesAnyLine`); legacy's CopyDialogue and Send made an undo step of it.

**The rows.** MoveAllItem's six toggles show the K1 roles shift-position to shift-origins, the drawings staying pushed while the tool leaves them out (ChangeTool drops them beside a position or \move kind, as legacy). AllTagsItem's two text choices and text button are, as every tool strip, icons: tag-list and tag-change-option open ShellMenus of their entries (the chosen one checked, named in the tooltip and the accessible description), tag-edit opens the dialog; choosing a tag also chooses its own change option, as legacy's list did. The three roles were drawn for T6; legacy had no bitmaps for them. Legacy titled the dialog's field box "Tag editing" as the dialog itself; the dialog's title alone names it here.

**Drawing.** The handles and sliders keep legacy's Direct3D shapes and fixed colours in device pixels: the shifter's squares, \move end circles, crosses (orange \org, blue clip, magenta drawing) and the clip and drawing outlines (lines, Bézier and B-spline curves through T4's `flattenCurve`); the sliders' track, ticks with a value at every second, the thumb (hovered, held) and the value under the pointer, the labels centred in legacy's rectangles with the label font (`measureLabel`).

The `legacy_visual_t6_capture` probe (`tools/legacy-capture/visual_t6_capture.cpp`) runs the legacy VisualMoveAll.cpp, VisualAllTags.cpp and VisualAllTagsControls.cpp unchanged on `inputs/visual-t6-cases.txt`, recording what they draw; `hikari_application_visual_shift_tests` replays its observations ([local-t6-visual-20261006](../../tests/fixtures/legacy-observations/local-t6-visual-20261006/README.md)).
