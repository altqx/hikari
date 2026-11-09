# Dawn Peek icons

The shipped HikariSub icon design (#803). These SVGs are the masters for every app and file-type icon raster.

| File | Use | viewBox |
| --- | --- | --- |
| `app.svg` | app icon, in-app icon | 876 × 734 |
| `ass.svg` | `.ass` file icon; `text-x-ssa` on Linux | 628 × 934 |
| `srt.svg` | `.srt` file icon | 628 × 934 |
| `ssa.svg` | `.ssa` file icon (Windows) | 628 × 934 |
| `sub.svg` | `.sub` file icon; `text-x-microdvd` on Linux | 628 × 934 |
| `txt.svg` | `.txt` file icon (Windows) | 628 × 934 |

Each viewBox is trimmed tight to the art, with no padding. The app icon is opaque inside its outline.

## Rasters

`tools/icons/app_icons.py` writes every shipped raster from these masters: the Linux hicolor PNGs and scalable SVGs under `packaging/linux/icons/hicolor`, and the Windows `.ico` files in `src/app/windows`. Run it after you change a master, and commit what it writes. `tests/tools/app_icons_tests.py` fails until you do.

The script centres each master on a square canvas, with the art edge to edge on its long axis. It renders the canvas at 2304 px and downsamples to each size. At 16–48 px, rendering directly at the target size makes the outer edge slightly heavier, because the stacked layers share that edge.

## How it was made

The sources are the raster masters in the icon source package: a 1024 px app master and 512 px file icons. The file icons were upscaled 2× before tracing.

1. The raster was denoised and its anti-aliased edges were sharpened.
2. It was split into colour regions, and each region was traced with potrace.
3. Each region is filled with a flat colour or a two- or three-stop linear gradient fitted to its pixels. Thin dark regions become flat outline strokes. The face is the exception: all its regions share one radial gradient (`#face`) fitted to the source's soft highlight, so it has no banded edges.
4. Paths use one decimal place and relative commands. The SVGs have no `<image>`, clip paths, masks or filters, so QtSvg renders them.

The file-icon badge is drawn by hand, not traced. It is one path with a straight top edge and rounded bottom corners fitted to the page outline. The paws overlap its top edge, as in the source. The source has a faint page edge beside the badge, which is left out.

`ssa.svg` is `ass.svg`, and `sub.svg` and `txt.svg` are `srt.svg`, with only the badge letters changed. The letters keep the scale, spacing, baseline and centre of the letters they replace.

## Fonts

The badge letters are glyph outlines, so no font ships with the icons:

- Dawn Peek: Poppins Bold, fitted to the letters in the source.
- Light Note (`../light-note`): Geologica at weight 900.

Both fonts are under the SIL Open Font License 1.1.
