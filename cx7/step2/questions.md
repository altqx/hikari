# CX7 #810, step 2: owner questions for the site and README mockup

Mockup: `index.html`, with shots in `shots/`. Answer each question with "yes" or a change. Each question states its recommendation, so "yes" accepts it.

1. **Homepage pitch.** The outline's pitch says "projects, several windows and an AI that only changes what you keep" twice. The mockup draws it as written. Recommendation: "Everything you do in Aegisub, plus projects, several windows and an AI that only changes what you keep. The subtitle editor for typesetting, timing, karaoke and translation, for Windows and Linux." The README uses the same text. Yes?

2. **Plus tile wording.** The mockup draws the eight Q488 tiles in order with one line each, for example "Hikari AI with every edit in review: The Agent and inline actions suggest edits, and each one waits in Review until you keep or undo it. One box turns all AI off." Recommendation: keep these eight lines as drawn. Yes?

3. **The 54 not-yet rows.** Coming from Aegisub lists every row by area, with a status pill and a plain sentence on what is missing. Not-yet rows are tinted, and an "All / Not yet only / Better only" filter sits at the top. The homepage gives the count and a link in one band under "Everything you do in Aegisub": "173 Aegisub features checked · 54 not yet →". Recommendation: keep the full list on Coming from Aegisub, and put the count with a link on the homepage, as drawn. Yes?

4. **README feature list.** "Every feature" keeps one heading per area. Under each heading it has a "Same as Aegisub:" run-on line and a "Better:" list, and it claims no not-yet row. Both are generated from parity.md. Recommendation: keep this per-area same/better layout, collapsed in `<details>` as today. Yes?

5. **Coming from Aegisub in the guide navigation.** The page is under Get started at `/en/docs/get-started/coming-from-aegisub`, in this order: Install, Getting started window, Tour, Coming from Aegisub, Moving from legacy. The guide's first page links it from Start here and from "If you know Aegisub", with a "New" badge. Recommendation: use this order. It moves Tour ahead of Moving from legacy, which today's `meta.json` doesn't do. Yes?

6. **Keyframe generation and the official plugin.** parity.md marks it "same, with the official Keyframe generator plugin (separate install)". Should a feature that needs a separate official-plugin install count as "same" in the parity claim and on Coming from Aegisub? The alternative is to list it as "not yet in the app, available as the official Keyframe generator plugin". This is still open. The mockup draws the "same" option only to show it. Recommendation: count it as same, and on Coming from Aegisub show a "with the official plugin" note that links to the Plugins page, as drawn in the Video area. Yes?

7. **Status words on the public page.** parity.md has five statuses, and the outline names only three. The mockup uses Same, Better, Different on purpose (with a "Why" link to the decision), Not yet, and "Not checked yet" for the one unverified row (`.y4m` video). Recommendation: use these five words. Also check the `.y4m` row before beta.4, so that "Not checked yet" never ships. Yes?

8. **One credit line per page.** The outline asks for the film credit as the hero caption and again in the footer. The mockup keeps the hero caption plain ("Editing a Project in the Editing layout.") and puts one line in the footer: "Sprite Fright, © Blender Foundation | studio.blender.org · Subtitles: Jibrill (node effects)". The guide's first page and the README do the same. Recommendation: use this one footer line per page. Yes?

9. **No Commons text in the Agent shot.** The Hikari AI tile's shot could show Commons subtitle lines, and that would add the CC BY-SA line. Recommendation: use only our own lines in that shot, so that no page needs the Commons line. Yes?

10. **"Your Lua and MoonScript macros run unchanged".** The Automation tile says this, but three automation rows are not yet: `register_filter`, `set_undo_point` and invalid macros greyed out, and MoonScript 0.7.0. Recommendation: "Your Lua and MoonScript macros run, DependencyControl included", with the details left to Coming from Aegisub. Yes?

11. **The legacy band and the high-contrast look.** The outline drops today's "Used HikariSub 0.0.x?" band from the homepage, and Moving from legacy stays under Start here in the guide. The site has no high-contrast mode, so the high-contrast frames propose one: black, white 2 px borders, underlined links, and the dark accent `#9cdbc9`. Recommendation: drop the band as the outline does, and take the high-contrast look as drawn if the site adds that mode. Yes?
