# Proposed domain vocabulary

For [Settle the domain glossary for the rewrite](https://github.com/altqx/hikari/issues/30). **Draft for human decision; not yet canonical.** This proposes names, not new capabilities or parity decisions. Underlined terms are proposed label changes or disambiguations.

The four accepted terms in [CONTEXT.md](../../../CONTEXT.md)—**Workspace**, **Editing target**, **Protected reference**, **Tool target**—remain unchanged. Accepted layout, movable panels, optional Home, follow/pin targeting and technical choices are not reopened.

Evidence: [core inventory][core], [UI inventory][ui], [data inventory][data]. Section references below identify the supporting observations.

| Proposed term | Meaning and boundary | Evidence |
|---|---|---|
| <u>Line</u> | An entry in the subtitle list: speech, a sign, drawing or comment. Prefer this over generic “dialogue”; a displayed text wrap is not another subtitle line. | Core §1–2 |
| ASS event | A Dialogue or Comment record in ASS. Keep “event” and “Dialogue” for format-specific discussion, not as universal document terminology. | Core §1 |
| Document | The subtitle content being edited, including lines, styles and script properties. Distinct from its saved file, tab and linked media. Do not introduce “Project” as a synonym or a new container. | Core §1, §4; UI shell |
| Document tab | The navigation entry for an open document. A tab is not the content or the shared Workspace. | UI shell |
| Session | A restorable set of open documents and associated media/view positions. Not a Workspace layout, full undo archive or “project file.” | Data sessions |
| Style | A named set of subtitle appearance defaults used by lines; distinct from inline overrides and the application's theme. | Data catalogs; UI styles |
| Style catalog | A reusable collection of styles outside the current document. Qualify “catalog”; document styles and catalog styles are separate collections. | Data catalogs |
| Font catalog | A named collection of font-family names. Not a style catalog, embedded fonts or bundled font files. | Data catalogs |
| <u>Script properties</u> | Document-level subtitle information such as title, credits, resolutions and ASS options. “Script Info” remains the ASS section name; this is not automation-script configuration. | Data ASS metadata; UI ScriptInfo |
| ASS override tag | An instruction in subtitle text affecting presentation or timing. Not a grid filter or automation filter. | Core §2 |
| Visual tool | A tool for manipulating subtitle placement, motion, scale, rotation, clipping or drawings in the video view. Not a video effect. | UI visual tools |
| Keyframe | A video frame identified by media indexing or an imported keyframe list, usable for navigation/snapping. Do not assume every keyframe is a scene cut or an ASS animation point. | Core §3 |
| Time shift | An offset or alignment of chosen line start/end times. Distinct from the timing postprocessor. | Core §3; UI ShiftTimes |
| Timing postprocessor | Timing adjustment using lead-in/out, continuity and keyframe tolerances. “Postprocessor” alone is too broad. | Core §3 |
| <u>Unconfirmed</u> | The existing translation-review flag, also called “doubtful.” Clearing it does not mean saved, spellchecked or independently approved. Do not generalize it into a new review workflow. | Core §1; UI translation |
| Translation mode | Editing original and translated text together for a line. Distinct from switching documents or the protected comparison view. | Core §1; UI translation |
| Original text / Translated text | The two text roles in translation mode. An empty translation is not a second document or automatically a confirmed translation. | Core §1 |
| <u>Line group</u> | The legacy “tree”: a description followed by contiguous member lines, with expand/collapse behavior. This naming does not promise arbitrary nested groups. | Core §6 |
| <u>Grid filter</u> | Criteria controlling which lines are shown or exposed to scoped commands. Hiding is not deletion, and hidden lines are not universally excluded from operations. | Core §6 |
| Automation macro | A script-provided editing command. Separate the command from the script that registers it. | UI automation |
| Automation export filter | Export-processing terminology from the compatibility API. The current host registers a dummy filter handler; this is not an implemented export pipeline or a grid filter. | [Host source][filter] |

## Ambiguous situations

1. **Two documents, one layout.** Switching a tab changes the Editing target; rearranging a panel changes the Workspace. Saving a session records reopenable context, not every unsaved edit or the complete layout. Say which object is being saved.
2. **One translated sign, two visual wraps.** It is one Line with original/translated text; it need not contain spoken dialogue. Its Unconfirmed flag is independent of dirty/saved state. Two wraps do not create two Lines.
3. **“Filter this group.”** Specify Grid filter for visibility, ASS override tag for presentation, or Automation macro for a script command. Collapsing a Line group neither deletes its members nor guarantees every later command skips them.

## Minimum decisions

1. Accept this vocabulary, particularly **Line**, **Document / Document tab / Session**, and the underlined **Script properties / Line group / Grid filter** distinctions; retain format-specific names where discussing ASS or compatibility.
2. Use **Unconfirmed** for the existing translation flag instead of “Doubtful,” without expanding its meaning to saving, committing edits or general approval.

After acceptance, only concise definitions and avoided synonyms belong in CONTEXT.md; evidence, scenarios and implementation caveats stay here.

[core]: https://github.com/altqx/hikari/blob/5d392a10c98b97c9b2b92f4b3f9dc49cd429752c/docs/research/core-inventory.md
[ui]: https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md
[data]: https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md
[filter]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/Automation.cpp#L499
