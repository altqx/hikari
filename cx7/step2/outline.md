# Outline: homepage, the guide's first page and the README (draft for the design gate)

Draft for altqx/hikari#810, 2026-11-13 by the hikari-site thread, re-checked by CX7 on 2026-11-16 against main 8dbf88f41, and updated with the owner's gate answers Q486–Q491 (https://github.com/altqx/hikari/issues/810#issuecomment-6848103301). Nothing here is built or published. The build-out waits until every wave-8 card has merged. It goes to the owner with [parity.md](parity.md), and draws on [additions.md](additions.md) and [screenshots.md](screenshots.md).

**Footage:**
- Non-karaoke shots use *Sprite Fright* frames (CC BY 4.0: "Sprite Fright, © Blender Foundation | studio.blender.org").
- Where a shot shows the Commons subtitle lines, it adds one line per page: "Subtitles: Wikimedia Commons TimedText contributors, CC BY-SA 4.0" (Q485).
- Karaoke shots use the Ray MV, credited "Subtitles: Jibrill" (Q489). Its MKV, ASS and fonts are never published. The 24 pictures already live with Ray MV stay until the beta.4 recapture, when the non-karaoke ones move to Sprite Fright (Q490).
- Thai lines shown in shots are our own, and the owner checks them at screenshot review (Q491). The owner's settings and data are never used.

## The headline (Q486)

**"Everything you do in Aegisub, plus …"**. The parity table, re-checked against main 8dbf88f41 (173 Aegisub rows: 64 same, 50 better, 4 different on purpose, 54 not yet, 1 unverified; out/cx7/parity/parity.md) backs "everything you do" only together with the *Coming from Aegisub* page, which names every "not yet" row (Q487). No page claims a "not yet" row.

## 1. Homepage (`/en`)

**Pitch:** Everything you do in Aegisub, plus projects, several windows and an AI that only changes what you keep. The subtitle editor for typesetting, timing, karaoke and translation, with projects, several windows and an AI that only changes what you keep.

| # | Section | Content | Screenshot (footage) |
| --- | --- | --- | --- |
| 1 | Hero | H1 "HikariSub"; the pitch; Download (platform-aware, as now); a link to What's new | **Hero:** the Editing layout with a Project open. Explorer, Grid, Line editor, video with a typeset sign, and the audio waveform. Light and dark. (Sprite Fright, our own typeset lines, no Commons text.) Caption: the film credit. |
| 2 | "Everything you do in Aegisub" | Eight compact tiles, one line each, each linking to its guide. Timing and audio; Visual typesetting tools; Styles and fonts; Karaoke (split, join, Kanji timer, and the native Karaoke Templater with its Template editor); Translation mode, with a line for Thai support (marks stay on their letters in splits, karaoke and templates); Checking; Automation ("your Lua and MoonScript macros run unchanged"); Files and formats. A closing line links to *Coming from Aegisub*: "What's the same, what's better, and what isn't in HikariSub yet". | None. Icons only, as today's small tiles. |
| 3 | "Plus" | Eight wide tiles (Q488), in this order: 1. Projects and the Explorer; 2. Several windows; 3. Hikari AI with every edit in review; 4. Node effects; 5. Motion and perspective tracking; 6. Typesetting tools (Perspective, Gradient, Text on path); 7. Checking (grammar, every dictionary at once, QC notes); 8. Plugins. | One shot per tile, light and dark: the Explorer with an episode group open (Sprite Fright); two windows on one desktop, each with its own video (Sprite Fright); an Agent panel thread with Review showing a kept and an undone line (Sprite Fright, Commons text possible → CC BY-SA line); the node graph with its preview (Ray MV, credited "Subtitles: Jibrill"); motion data applied, with a sign tracking a moving object (Sprite Fright); the Perspective tool on a sign (Sprite Fright); the Spellchecker panel with grammar flags (Sprite Fright, our own lines); the Plugins window with the Marketplace (no video). |
| 4 | Live preview | Keep the AkariSub `AssPreview` tile, with a line built with a HikariSub-only tool | None. It renders live over the Sprite Fright frames already credited on Credits. |
| 5 | Free and open | GPL-3.0, no account, what is sent and Turn off all AI features; links to Install, Updates and privacy, and Source | None |
| 6 | Footer credits | One line: the film credit, "Subtitles: Jibrill" for the node-effects tile, and the Commons subtitle line if section 3 shows their text | None |

**Done separately:** the live Checking tile's "minor-error fixes" was removed in hikari-site#217 (f24a196).

## 2. The guide's first page (`/en/docs`, content/docs/index.mdx)

**Pitch:** How to do everything in HikariSub, from a quick timing fix to a full typeset episode, and where it differs from Aegisub.

| # | Section | Content | Screenshot (footage) |
| --- | --- | --- | --- |
| 1 | Intro | Two sentences: what the guides cover (0.1, beta.4), and that ASS lines render live with AkariSub. Keep the `AssPreview` line. | None |
| 2 | Start here | Install; Getting started window; Tour; Coming from Aegisub (new); Moving from legacy (the path stays the same) | **Tour shot:** the window with labelled areas. (Sprite Fright, our own lines.) |
| 3 | "If you know Aegisub" | Five lines on what's the same (files, styles, tags, timing keys, scripts), then a link to *Coming from Aegisub* for the full table, including what isn't there yet | None |
| 4 | What HikariSub adds | The additions grouped by area, as links: Projects and the Explorer; side bars and layouts; several windows; Hikari AI; node effects; native tools; plugins; Thai and translation | None. The pages carry the pictures. |
| 5 | By task | Time a show; Typeset signs; Make karaoke; Translate; Check before release; Automate. Each is a short path of three or four guide links. | None |

**New page, ships with beta.4 (Q487):** `/en/docs/get-started/coming-from-aegisub`, built from parity.md. It has the three statuses and names every "not yet" row in plain words, with no screenshots. It's updated as rows close.

## 3. README (altqx/hikari)

**Pitch:** the same line as the homepage, included.

| # | Section | Content | Screenshot (footage) |
| --- | --- | --- | --- |
| 1 | Header | Icon, name, the pitch, and Download and Website links | **Hero**, the same shot as the homepage hero. (Sprite Fright.) |
| 2 | "Everything you do in Aegisub" | The eight areas as one compact list, linking to the site's *Coming from Aegisub* | None |
| 3 | "Plus" | A two-column picture grid, with one picture and one line per addition. It replaces today's grid. | Explorer; several windows; Agent and Review; node effects (Ray MV, "Subtitles: Jibrill"); motion tracking; Perspective; themes (no video, all four themes); Home page (no video, a fixture user folder) |
| 4 | Feature list | Today's per-area list, rewritten as "same as Aegisub" or "better", and checked against parity.md. Nothing in the "not yet" rows is claimed. | None |
| 5 | Download, Updates and privacy, Build from source, License | As now, checked against the beta.4 candidate | None |
| 6 | Credits | One line for the film, "Subtitles: Jibrill" for the node-effects picture, and the Commons subtitle line if any README shot shows their text | None |

**Today's README and homepage hero both show a karaoke MV.** Its alt text says "editing a karaoke music video", and the homepage caption says "Timing a karaoke MV … Subtitles: Jibrill". [screenshots.md](screenshots.md) says which footage each existing shot uses. They stay until the beta.4 recapture. Then the hero and other non-karaoke shots move to Sprite Fright, and karaoke shots keep Ray MV (Q489, Q490).

## Owner answers (2026-11-13)

| Q | Answer |
| --- | --- |
| Q486 | Headline: "Everything you do in Aegisub, plus …" |
| Q487 | *Coming from Aegisub* ships with beta.4 at /en/docs/get-started/coming-from-aegisub, built from parity.md, naming every "not yet" row |
| Q488 | Eight Plus tiles: Projects and the Explorer; Several windows; Hikari AI with every edit in review; Node effects; Motion and perspective tracking; Typesetting tools; Checking; Plugins. The native Karaoke Templater sits in the Karaoke tile, and Thai support is a line under Translation. |
| Q489 | Ray MV only for karaoke shots, credited "Subtitles: Jibrill". Sprite Fright for every other shot. The MKV, ASS and fonts are never published. |
| Q490 | The 24 live Ray MV pictures stay until the beta.4 recapture. Then the non-karaoke ones move to Sprite Fright. |
| Q491 | We write the Thai lines in shots, and the owner checks them at screenshot review. |
