# Legacy observations: T1 visual coordinates (local probe, 2026-10-05)

Captured by the `legacy_visual_capture` probe ([`visual_capture.cpp`](../../../../tools/legacy-capture/visual_capture.cpp), target in [`tools/legacy-capture/CMakeLists.txt`](../../../../tools/legacy-capture/CMakeLists.txt)) on the cases in [`inputs/visual-cases.txt`](../../../../tools/legacy-capture/inputs/visual-cases.txt). The probe compiles the legacy `HikariSub/VisualCross.cpp` at `20d647c4` unchanged and copies these definitions out of their files unchanged ([`extract_functions.py`](../../../../tools/legacy-capture/extract_functions.py)): `RendererVideo::UpdateRects`, `SetZoom`, `ResetZoom`, `SetVisualZoom`, `Zoom`, `ZoomMouseHandle`, `Visuals::SizeChanged`, the inline `Visuals::GetCalculatedInPos*` / `GetCalculatedOutPos*`, the `Cross` and `FloatRect` classes, `VideoBox::GetVideoRect`, `GetWindowSize`, `OnCopyCoords` and `getfloat`, with the legacy Linux compatibility header (`platform.h`) and the `MIN`/`MAX`/`MID` macros of `config.h`. They run against stand-ins ([`tools/legacy-capture/visual/`](../../../../tools/legacy-capture/visual)): the video window's client size and panel height, the frame's encoded size and SAR reduced as `ProviderFFMS2.cpp:364-380` does and opened as `RendererFFMS2::OpenFile` does (an odd width made even), the AspectRatioDialog override (`m_AspectRatio`), the script's PlayRes, VIDEO_ZOOM_PERCENT unset (so the zoom toggle zooms 2x), a 7-pixel-a-character, 14-pixel-high label font, and a clipboard that keeps the text.

Built locally with GCC 16.2.1 and the distribution's wxWidgets 3.2.11 (wxGTK, `wx-config --cxxflags base,core`). Run:

```
cmake -S tools/legacy-capture -B <dir> -DLEGACY_SOURCE=<checkout at 20d647c4> -DWX_CONFIG=$(which wx-config)
cmake --build <dir> --target legacy_visual_capture
<dir>/legacy_visual_capture < tools/legacy-capture/inputs/visual-cases.txt > observations.jsonl
```

`observations.jsonl` holds one line per case (18): the frame size and aspect, the window, video (`m_BackBufferRect`) and source (`m_MainStreamRect`) rectangles, the zoom (percent, mode, `m_ZoomRect`), the tools' `zoomMove`, `zoomScale` and `coeffW`/`coeffH`; for each view point the Out conversion, the crosshair's label, label rectangle, four line ends, whether the point is on the video and whether the crosshair is on, its coefficients and the VIDEO_COPY_COORDS text; for each script point the In conversion; for each Ctrl+click or middle click the Line's text and translation after it and the history entry (`VISUAL_POSITION`). Legacy works in device pixels; a case's `dpr` is echoed only so the rewrite can feed the same device pixels through its logical coordinates. sha256 `0960bc18…deaec`.

The cases: a 16:9 frame filling the window, pillarbox, letterbox, an anamorphic SAR (720x480 at 32:27), the aspect override (0.5, and 1.25 on a tall window), an odd width, a script resolution unlike the video's, zoom at a point, the zoom toggle, zoom then reset, the zoom-mode pan (a drag inside the zoom rectangle, and one clamped at the corner), SAR with override, zoom and a 1.5 ratio together, device pixel ratios 1.25 and 1.5, and a wheel zoom without the tools' refresh (`zoom-wheel-stale`).

## Compared with the rewrite

`VisualCapture.ReplaysTheLegacyProbe` (`hikari_application_visual_tools_tests`) sets every case up through `visual::VideoView` from logical coordinates at the case's device pixel ratio and replays it through `visual::CrosshairTool` and `visual::copyCoordinatesText`: every rectangle, zoom value, coefficient, conversion, label, label rectangle (on the video), line end, visibility flag, copied text and clicked Line text is equal, float for float. There is no difference to record.

The capture shows these legacy behaviours, kept:

- The crosshair's label and click use their own coefficients, the script size over the video rectangle less one pixel on a side without a bar (`VisualCross.cpp:80-92`), so the centre of a 1280x720 frame in a 640x360 rectangle reads `961, 541` and a click there writes `\pos(961.502,541.504)`; the other tools' conversion (`GetCalculatedOutPos`) gives 960, 540.
- The label truncates toward zero (`-106, 0` left of a pillarbox) and is drawn only on the video (bounds inclusive); off the video the crosshair stays on but is not drawn.
- VIDEO_COPY_COORDS scales the pointer by the whole window less one pixel (`OnCopyCoords`), ignoring the letterbox and the zoom: left of a pillarbox it copies positive coordinates, and zoomed it copies the unzoomed position.
- A zoom by the wheel (`SetZoom(percent, pos)`) does not refresh the tools' transform (`zoom-wheel-stale`): until the next resize, tool change or edit (`RendererVideo::SetVisual`), the crosshair reads and writes as if unzoomed.
- `UpdateRects` scales the zoom rectangle by the video rectangle's right and bottom edges, not its size, and `ResetZoom` keeps the zoom mode on.
- A click removes every `\pos` and `\move` with their arguments (the regex `\\(pos|move)([^\\}]+)`), anywhere in the text, then puts `\pos` first in the first block, or in a new one; in TLMode it edits the translation unless it is empty.
