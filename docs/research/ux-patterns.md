# MuseScore 4 and Audacity 4 UX patterns

Research for [#8](https://github.com/altqx/hikari/issues/8), verified 2026-09-27. This is evidence and proposed prototype input, not a decision to adopt Muse's framework, assets, exact tokens, or layouts. The previous branch contained only a skeleton.

## Summary and scope

The transferable pattern is a document-centred application with a compact command surface, dockable task panels, explicit selection context, reusable controls, and adjustable density/theme. Keyboard navigation and configurable shortcuts are part of the interaction model. Visual similarity alone does not establish usability or performance.

Audacity 4 is no longer merely an upcoming alpha: its first-party changelog records 4.0.0 on 3 September 2026. It adds a Qt interface, configurable workspaces, direct clip selection, context-sensitive editing and a recent-project Home screen. Some individual manual pages still contain alpha wording; prefer the release changelog plus version-pinned source when they conflict. [Audacity changelog](https://www.audacityteam.org/changelog/)

The user's process is authoritative: remain on **Figma Starter**; agents produce runnable **QML or HTML mockups through prototype tickets** for human reaction; use Figma only for occasional hand-off. The durable specification and tokens must live in the repository. No Figma subscription, paid library or public Figma file is a prerequisite.

## Interaction inventory and transfer

| Area | Observed first-party pattern | Proposed HikariSub experiment, not an adopted decision |
|---|---|---|
| Home/project start | MuseScore separates Home, Score and Publish. Home offers new/open scores, account, plugins and learning; editing stays in Score. [UI handbook](https://handbook.musescore.org/navigation/the-user-interface) | A skippable recent-project/start view with Open subtitles, Open video and recovery entry; do not force an account or marketing step before local editing. |
| Workspaces | MuseScore saves panel/toolbars/palette arrangements automatically; document-specific zoom/display settings are distinguished from workspace state. [Workspaces](https://handbook.musescore.org/customization/workspaces) | Compare named Timing, Translation and Typesetting arrangements, with a visible workspace name and Reset. Specify what belongs to the document, tab, workspace and application. |
| Workspace defaults | Audacity offers Modern, Classic and Music. Modern exposes transport, selected edit tools and time/snapping, while clipboard and less common zoom actions remain available by menu/shortcut. [Modern](https://www.audacityteam.org/manual/workspaces/modern/) | Prototype a compact default without removing parity commands. A familiar layout can ease migration without reproducing every old control placement. |
| Properties/inspector | MuseScore's Properties changes with selection; common properties apply to multiple objects, with explicit reset/save-as-style paths. With nothing selected, it shows document settings. [Properties](https://handbook.musescore.org/basics/properties-panel) | Use an inspector for selected-line metadata, style and visual-tool parameters. Show selection count, mixed values, inherited versus override values and the target of every edit. Keep the text editor immediately accessible. |
| Toolbar customisation | MuseScore panels and toolbars are movable; Audacity separates project, tools and bottom selection readouts. [MuseScore UI](https://handbook.musescore.org/navigation/the-user-interface), [Audacity toolbars](https://www.audacityteam.org/manual/toolbars/) | Keep file/undo/workspace actions stable, transport adjacent to preview, and context tools adjacent to their canvas. Every hidden command needs a menu/shortcut route. |
| Palette/panel system | MuseScore palettes are categorized, searchable, customizable and usable by selecting a target then applying an item; keyboard navigation is documented. [Palettes](https://handbook.musescore.org/basics/using-the-palettes) | Test style/tag presets as a searchable panel. Selection plus Apply must work without dragging. Distinguish presets from ASS style definitions. |
| Status/readouts | MuseScore places selection information on the left and view/workspace controls on the right. Audacity has bottom selection readouts. [MuseScore UI](https://handbook.musescore.org/navigation/the-user-interface), [Audacity toolbars](https://www.audacityteam.org/manual/toolbars/) | Persistent line/selection count, start/end/duration and frame/time mode; indexing/save feedback must not displace essential timing state. |
| Preferences | MuseScore groups by purpose (general, appearance, canvas, input, audio, shortcuts, import and advanced) with Cancel/OK/reset semantics. [Preferences](https://handbook.musescore.org/customization/preferences) | Group by task and scope, expose search, and separate preferences from script properties. Define how Cancel restores previewed appearance changes. |
| Shortcut editor | MuseScore supports search, recording sequences, per-command reset/clear and import/export. Audacity describes reassignment and conflict resolution. [MuseScore shortcuts](https://handbook.musescore.org/customization/keyboard-shortcuts), [Audacity shortcuts](https://www.audacityteam.org/manual/preferences/shortcuts/) | Show action, context, current binding, default and conflict scope; preview import conflicts rather than silently dropping bindings. |
| First run | Audacity source constructs theme, clip-visualization and workspace pages; account/update pages are conditional. [Setup model](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/appshell/qml/Audacity/AppShell/FirstLaunchSetup/firstlaunchsetupmodel.cpp) | A short optional appearance/workspace/import step, with defaults and a way to revisit it. Recovery/file opening must work before onboarding is completed. |
| Empty states | MuseScore's unselected Properties offers document controls rather than an unexplained blank panel. [Properties](https://handbook.musescore.org/basics/properties-panel) | Distinguish no project, empty subtitles, no selection, missing media, indexing, failed media, no search results and no scripts; give each a next action. These states are proposals, not claimed upstream behavior. |
| Dialog style | Muse shares a themed dialog with a navigation section, margins and content sizing; frameless dialogs use a border/radius. [StyledDialogView](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/uicomponents/qml/Muse/UiComponents/StyledDialogView.qml) | A common contract: title, initial focus, labelled fields, validation, primary action, Cancel/Escape and return focus. Include resizable content and long translated/script-created fields. |

## Visual language and source-backed tokens

These are inspected implementation values, not a complete official design specification or recommended contrast ratios. Audacity source is pinned at `36146d838c934ae429cb164e8eb3d39af75a65ff`; its Muse submodule is `b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70`. Source HEAD can differ from the shipped 4.0.0 binary.

| Role | Audacity light | Audacity dark |
|---|---|---|
| Primary background | `#F8F8F9` | `#2F353B` |
| Secondary background | `#DFDFE3` | `#262B30` |
| Primary text | `#14151A` | `#F0F5FA` |
| Accent | `#A09EEF` | `#926BFF` |
| Stroke | `#D4D5D9` | `#444A4F` |
| Play / record | `#18A999` / `#EF476F` | same |
| Navigation border | `2.0` logical units | `2.0` logical units |

Source: [light.cfg](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/app/configs/light.cfg), [dark.cfg](https://github.com/audacity/audacity/blob/36146d838c934ae429cb164e8eb3d39af75a65ff/src/app/configs/dark.cfg). Separate roles also exist for selection, focus, waveform ruler, timeline, snapping and errors. This is evidence for **semantic roles**, not permission to substitute color for text, focus indication or accessible names. No contrast measurement was performed.

Muse's `UiConfiguration` separates background, popup, field, accent, text, link, focus, playback and record roles; light/dark and white/black high-contrast themes; configurable fonts; and scalable font roles. The code documents a body-size example of 12 with body-large 14, tab 16, header 22 and title 32, calculated from the user's body size. Icon sizing derives from body size. These are relative roles, not instructions to hard-code all labels at 12 pixels. [UiConfiguration](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/internal/uiconfiguration.cpp)

`FlatButton` uses theme fonts/colors, horizontal margins of 12 or 16, internal spacing of 4/8 and explicit normal/hover/pressed/disabled states. Its navigation control has an accessible button role and a name from visible text or tooltip title. This demonstrates the visual and keyboard/accessibility contracts together. It does **not** establish a universal spacing scale. [FlatButton](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/uicomponents/qml/Muse/UiComponents/FlatButton.qml)

Muse defaults to the `MusescoreIcon` icon font and provides named codepoints. Music glyphs are separate from ordinary text. HikariSub needs a consistent icon set with labels/tooltips and stable accessible names, not copied music glyphs. Review asset licences separately before reuse. [Icon codes](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/view/iconcodes.h), [UiConfiguration](https://github.com/musescore/muse_framework/blob/b1b09fa0343a40f6c5a31e9663ed3ed1ab796c70/framework/ui/internal/uiconfiguration.cpp)

MuseScore exposes font family/size, accent and high contrast in Appearance. Audacity separates theme, UI colours, UI font and clip style. A subtitle editor should similarly distinguish application chrome from subtitle rendering: changing the application theme must not alter ASS output. [MuseScore preferences](https://handbook.musescore.org/customization/preferences), [Audacity appearance](https://www.audacityteam.org/manual/preferences/appearance/)

## Redesign resistance, team statements and evidence limits

Audacity's transition guide directly addresses existing users: familiar Classic workspace/clip appearance, old-project loading and a map of moved interactions. The landing page explains trim versus stretch handles and direct sample editing. This supports explaining changes and retaining familiar routes; it does not prove universal preference for the redesign. [Transition guide](https://www.audacityteam.org/manual/new-in-audacity-4/audacity-3-to-4-transition-guide/), [Audacity 4](https://www.audacityteam.org/audacity-4/)

MuseScore's product lead described rebuilding/simplification tradeoffs during the beta and directed concrete reports to GitHub. The 2024 near-term plan describes professional-feature completeness with room for user opportunities, including Finale users. These are dated statements, not evidence of current personnel or delivery promises. [Beta discussion](https://musescore.org/en/comment/1147792), [Near-term plans](https://musescore.org/en/4.5-and-beyond)

The design lead's first-person post links the retrospective **How We Made MuseScore 4 — Music App Design is Challenging!**. A reliable transcript was not inspected, so this report does not invent regrets or attribute a “what we would do differently” list. That sub-question has an explicit negative finding: no verified retrospective claim beyond the cited written material was recovered. [Martin Keary's post](https://www.linkedin.com/posts/martin-keary-88a5a7159_how-we-made-musescore-4-music-app-design-activity-7135444757916356608-d_a7)

Searches for MuseScore/Audacity Figma community/design-system files did not recover a verified official, reusable public file with a known licence. Results included unrelated Figma pages and an unofficial mobile concept, unsuitable as canonical desktop assets. This is **not proof that none exists**. The pinned QML components and theme files are usable evidence. No paid upgrade is needed or proposed.

## Prototype acceptance questions

The design-system prototype should show the same subtitle task in light, dark and high-contrast variants, including normal/hover/focus/pressed/disabled/error/mixed-value states, and make these questions reviewable:

1. Can users move between grid, text, timing, video and properties without losing selection or committing unintended changes?
2. Can every action be reached by keyboard with visible focus and meaningful screen-reader labels? Region navigation is distinct from editing keys. QML must validate native behavior; HTML cannot prove it.
3. Can workspaces change/reset without changing subtitle content? Can a misplaced panel be recovered by keyboard?
4. Do long subtitle lines, Thai/non-Latin text, translated labels and enlarged fonts fit without hiding critical timing fields?
5. Do no-selection, mixed-selection, missing-video and loading states explain their next action?
6. Is each migrated command discoverable by its old name, alias or shortcut? Compact toolbars must preserve parity.

These require human reaction to runnable QML/HTML prototypes. No user test, benchmark, screen-reader run or production implementation was performed. Palette/density/navigation choices remain prototype/UX sign-off decisions; framework adoption is a separate architecture decision.
