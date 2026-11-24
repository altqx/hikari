# Resample subtitles

Subtitles › Resample subtitles (`src/ui/ResampleWindow.qml`, `changeResolution` in `src/application/resample.cpp`) fits the subtitles to another resolution. Its strings live in the window's own context. DS reviewed this spec; the owner decided Q807.

## Rows

In this order:

1. Subtitles and Target, each with "From subtitles" and "From video", then the scale hint (as today).
2. **Aspect ratio** with its hint, then **Edges**. Both show only when the two sizes differ in shape by more than 1 % (Q583's rule, as the resolution question uses).
3. **YCbCr matrix** with its hint, always shown.
4. "Adjust 3D rotations" (as today).
5. Resample and Cancel.

## Aspect ratio

Segmented buttons, "Keep it | Stretch | Letterbox | Crop". The hint under them says what the selected one does. "Border" is not used: in ASS it means the outline.

| Choice | Hint | What the code does |
| --- | --- | --- |
| **Keep it** (default) | Text keeps its proportions. Positions follow the new picture. | Today's Resample. |
| **Stretch** | Text stretches with the picture. | Today's stretch: Scale X or Y × the larger ratio ÷ the smaller. |
| **Letterbox** | The video adds bars around the picture the subtitles were made for. Signs stay on what they mark. | Aegisub's add borders: the edges grow equally on the sides (new picture wider) or at the top and bottom (taller), so the old picture fits the new shape, then scales evenly. |
| **Crop** | The video cuts off the edges of the picture the subtitles were made for. Signs stay on what they mark. | Aegisub's remove borders: the same edges, negative. |

The four segments and the Letterbox hint fit today's window width without wrapping, at 1366 × 768 in every theme.

## Edges

A disclosure named "Edges", collapsed at first. Collapsed with any value other than 0, its trailing text lists the values in `muted`, for example "Top 120 · Bottom 120".

Open, it holds two rows of spin boxes, each with its label in front: Left and Right, then Top and Bottom. Each shows "px" in its unit slot. A check box, "Same on both sides", is on by default (Aegisub's Symmetrical): while it is on, Right follows Left and Bottom follows Top, and the Right and Bottom boxes are disabled. The hint is "Added to (+) or cut from (−) each edge of the subtitles' picture before it is scaled."

Letterbox and Crop fill the edges in; Keep it and Stretch set them to 0. Typing a number selects none of the four buttons, and the Aspect ratio hint says "Custom edges. Text keeps its proportions." Any shape difference that is left is kept as Keep it keeps it (Q807 = A; Aegisub's Manual mode stretches instead).

The edges shift every position, margin and clip by the left and top edge before scaling, as Aegisub's resampler does. A set layout resolution follows Aegisub 3.5.1: LayoutResY grows or shrinks with the top and bottom edges (`LayoutResY + round(LayoutResY × (top + bottom) ÷ subtitles' height)`), and LayoutResX follows it and the change of shape.

## YCbCr matrix

A dropdown with the raw names Script properties uses. The first row keeps the subtitles' matrix ("Keep TV.601"); when a video is open its matrix comes next, marked "(video)"; the other matrices follow. Choosing another one converts the colours as the matrix question's "Convert colours and set to …" does (#1356) and sets the matrix. The hint is "Colours are converted so that they look the same under the new matrix." With no matrix or `None`, the dropdown is disabled and the hint says "The subtitles have no YCbCr matrix to convert from."

## Undo

Resampling is one undo step, and a matrix change made in the window is part of it.
