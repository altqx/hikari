# Splash artwork

The Windows launcher shows this early splash before Qt loads (#584). The art is called "Hikari sunrise": the app icon's hair crown rises from the bottom-right corner like a sun, with the scalloped headdress frill as its corona, the ahoge on top, and the star clip and shine dashes as its rays.

## Files

| File | Pixels | Size |
| --- | --- | --- |
| `splash-dark.svg`, `splash-light.svg` | 480×270 logical | vector masters |
| `splash-dark-100.png`, `splash-light-100.png` | 480×270 | 54 KB, 43 KB |
| `splash-dark-150.png`, `splash-light-150.png` | 720×405 | 88 KB, 71 KB |
| `splash-dark-200.png`, `splash-light-200.png` | 960×540 | 123 KB, 98 KB |
| `splash-dark-300.png`, `splash-light-300.png` | 1440×810 | 80 KB, 76 KB |

The PNGs are opaque 8-bit sRGB, with no alpha and no gAMA, cHRM, iCCP or sRGB chunks. The 1×, 1.5× and 2× files are lossless RGB. The 3× files are 256-colour with dithering, to stay under 150 KB each. All eight come to 631 KB.

## Layout the launcher relies on

- The art fills the whole window. The hair runs off the right and bottom edges.
- The "HikariSub" wordmark is part of the art, as outlined paths, at the bottom-left. Its baseline is at y 214 logical.
- The launcher draws the version under the wordmark in Segoe UI 12 px, in the theme's muted colour (#A5B1BD dark, #526170 light), left-aligned at x 32 with its baseline at y 238. That area is plain sky.
- The launcher's 3 px progress bar (y 267–270) covers the bottom of the hair.
- The top-left of both variants is the theme background (#171B20 dark, #E5E9EC light).

## How it was made

1. Concepts were explored with GPT Image 2.5 Sunburst (OpenAI), using the app icon as the only reference image. The owner picked "Hikari sunrise".
2. The chosen concept was regenerated as a flat cel-shaded drawing on a plain background, at 2560×1440.
3. That drawing was traced into flat-colour vector shapes with VTracer.
4. The sky glow, the light-theme colours and the wordmark were added by hand in SVG. The wordmark is original monoline letterforms, outlined with Skia, with star sparkles for the dots on the i's.
5. The PNGs were rendered from the masters with `rsvg-convert` and optimised with oxipng. The 3× files were quantised with libimagequant.

No third-party artwork was traced or included. The artwork is original to HikariSub and is distributed under the project's licence (GPL-3.0).
