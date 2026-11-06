# Dawn Peek icons

The shipped HikariSub icon design (#803). These SVGs are the masters for every app and file-type icon raster.

| File | Use | viewBox |
| --- | --- | --- |
| `app.svg` | app icon, favicon | 876 × 734 |
| `ass.svg` | `.ass` file icon | 628 × 934 |
| `srt.svg` | `.srt` file icon | 628 × 934 |

Each viewBox is trimmed tight to the art, with no padding. The app icon is opaque inside its outline.

## How it was made

The sources are the raster masters in the icon source package: a 1024 px app master and 512 px file icons. The file icons were upscaled 2× before tracing.

1. The raster was denoised and its anti-aliased edges were sharpened.
2. It was split into colour regions, and each region was traced with potrace.
3. Each region is filled with a flat colour or a two- or three-stop linear gradient fitted to its pixels. Thin dark regions become flat outline strokes.
4. Paths use one decimal place and relative commands. The SVGs have no `<image>`, clip paths, masks or filters, so QtSvg renders them.

The file-icon badge is drawn by hand, not traced. It is one path with a straight top edge and rounded bottom corners fitted to the page outline. The paws overlap its top edge, as in the source. The letters are Poppins Bold glyph outlines (SIL Open Font License), fitted to the letters in the source. The source has a faint page edge beside the badge, which is left out.

Render rasters from these SVGs at a larger size and downsample them. At 16–48 px, rendering directly at the target size makes the outer edge slightly heavier, because the stacked layers share that edge.
