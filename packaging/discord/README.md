# Discord Rich Presence artwork

The art for HikariSub's Discord Rich Presence (#599). The Discord application is "HikariSub", client id `1566681222510411967`. Presence shows "Editing subtitles", the current mode and the elapsed time. The large image is the app, and the small image is the mode.

## Assets

| Key | File | Use |
| --- | --- | --- |
| `hikari` | `hikari.png` | large image |
| `editing` | `editing.png` | small image, Editing mode |
| `timing` | `timing.png` | small image, Timing mode |
| `translation` | `translation.png` | small image, Translation mode |
| `typesetting` | `typesetting.png` | small image, Typesetting mode |
| `karaoke` | `karaoke.png` | small image, Karaoke mode |
| none | `app-icon.png` | App Icon under General Information |

All PNGs are 1024×1024, opaque and lossless, optimised with oxipng. Each `.svg` next to a PNG is its master.

Discord's rules for uploaded art assets: PNG, JPEG or WebP; 1024×1024 recommended, and the launch checklist treats that as the minimum; up to 300 assets per app. Asset keys are lowercased on upload.

## Design

The art is called "Daylight sticker". The app icon's chibi appears as a white-outlined sticker on a pale sky with a soft sunburst.

- Discord shows `hikari` as a rounded square at about 60–120 px, so the sticker fills the square.
- Discord shows the mode image as a circle about 20–40 px across, over the large image's bottom-right corner. Each mode image is one navy (#1E2A6B) glyph on a full-bleed pastel colour, kept inside the inscribed circle, with strokes about 1.5 px wide at 20 px.
- The glyphs follow the app's own icons in `src/ui/icons/`:
  - `editing` follows `editor.svg`;
  - `timing` follows `audio-line-mode.svg`;
  - `translation` follows `translation-mode.svg`;
  - `karaoke` follows `karaoke.svg`;
  - `typesetting` follows `tool-position.svg`, the move tool from the visual typesetting tools.
- The disk colours are #8FD3FF (`editing`), #7EE0C3 (`timing`), #B9A6FF (`translation`), #FBD46D (`typesetting`) and #FFA8A0 (`karaoke`).
- `app-icon` is the `hikari` art with the sticker scaled to 84%, so it survives the places where Discord crops app icons to a circle.

## How it was made

1. Concepts were explored with GPT Image 2.5 Sunburst (OpenAI), using the app icon and the splash art as reference images. The owner picked "Daylight sticker".
2. That concept was regenerated as a flat cel-shaded drawing on a plain background, at 2048×2048.
3. The drawing was traced into flat-colour vector shapes with VTracer.
4. The sky gradient, the sunburst and the drop shadow were added by hand in SVG. The mode glyphs were drawn by hand as geometric SVG on a 1024 grid.
5. The PNGs were rendered from the masters with `rsvg-convert` and optimised with `oxipng -o max --strip safe`.

No third-party artwork was traced or included. The artwork is original to HikariSub and is distributed under the project's licence (GPL-3.0).

## Uploading

1. Open the [Developer Portal](https://discord.com/developers/applications) and select the HikariSub application.
2. Under **General Information**, set **App Icon** to `app-icon.png` and save.
3. Open **Rich Presence › Art Assets** and click **Add Image(s)**. Select `hikari.png`, `editing.png`, `timing.png`, `translation.png`, `typesetting.png` and `karaoke.png`.
4. Check that each asset's key is its file name without `.png`, then save. Newly uploaded assets can take a few minutes to appear in presence.
