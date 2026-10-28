# Workspace panels and presets follow-up (throwaway)

Issue: [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31).

**Accepted:** B movable/floating tool panels, the shared application workspace, optional protected comparison, Compact Studio D and the Hikari-owned Qt direction. Tools follow the editing document by default with optional pinning. Home is optional; the workspace is the normal editing destination. B is now the default; A remains a historical constrained-zone reference. The user then requested a default closer to the current program, using a supplied screenshot as the layout reference. **Editing is now the default:** video occupies the upper-left approximately 44%; audio/spectrum sits above the text editor at upper-right; the subtitle grid spans the entire width below. All four core panels are visible and auxiliary Styles/Search/Timing/History tools start closed. The screenshot is not copied into the repository; all displayed content is synthetic. Optional task-preset memberships and comparison navigation remain available for review.

Open `workspace-tools.html` directly, or reuse the prototype server:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory docs/prototypes
```

- [B — accepted movable panels](http://127.0.0.1:8765/workspace-tools.html?variant=B): independent tools can occupy Left, Right, Bottom or Float. Empty tool zones collapse; when there are no docked tools, the editing panels use the available width. Floating is simulated **inside this HTML page**. This does not establish native detached windows, docking-library selection, multi-monitor behavior, persisted geometry or native accessibility/performance.
- [A — constrained reference](http://127.0.0.1:8765/workspace-tools.html?variant=A): tabbed right/bottom zones with size controls remain available for comparison; this is not a request to reopen the accepted B decision.

## Default Editing and optional presets

Editing is the normal starting workspace, following the requested current-program geometry. The other named presets are accepted optional presets, not forced defaults. Every panel/tool remains freely showable or hideable. The five legacy visibility equivalents preserve their broad combinations; **subtitles means Editor + Grid**.

| Preset | Visible core panels | Open tools | Optional additions |
|---|---|---|---|
| **Editing — default** | **Video left; Audio above Editor right; Grid full width below** | **None** | Any auxiliary tool; protected comparison |
| Timing — optional preset | Editor, Audio, Grid | Timing, History | Video |
| Translation — optional preset | Video, Editor, Grid | Search | Audio; protected comparison |
| Typesetting — optional preset | Video, Editor, Grid | Styles | Audio |
| All | Video, Editor, Audio, Grid | Styles, Search, Timing, History | Protected comparison |
| Video + subtitles | Video, Editor, Grid | None | Any tool or Audio |
| Audio + subtitles | Editor, Audio, Grid | None | Any tool or Video |
| Video only | Video | None | Any panel/tool |
| Subtitles only | Editor, Grid | None | Any tool, Video or Audio |

Comparison is independent of presets: choosing or restoring a preset does not enable or disable it. Home remains the accepted independent optional view; the workspace opens normally. Presets do not change the accepted follow-editing default or remove optional pinning.

## What to try

1. Start with Editing. The video spans the audio/editor rows on the left, and the dense ten-row subtitle grid stays full-width below both columns. The grid includes Layer, Start, End, Style, Actor, Effect and CPS. Previous/Next line controls beneath the synthetic video change the same selected sample line used by the grid and tools. Optionally try Timing, Translation, Typesetting or a legacy visibility equivalent. Use the panel toolbar or panel-header Hide buttons for Video, Editor, Audio and Grid. A compact hidden-panel strip offers Show controls; hiding every core panel presents a recoverable placeholder. Document contents, selection, revisions and undo samples remain unchanged when visibility changes.
2. Open tools from the toolbar. Move Search from Float to Bottom, hide History, then reopen it. Empty B zones disappear rather than reserving blank columns or rows. Main panel visibility and tool placement belong to the shared application workspace and remain in place across document switches.
3. On Styles, choose **Preview**, inspect the named document/line, then **Apply**. Search performs literal case-sensitive replacement on the selected line only; Timing shifts its synthetic start time; History previews/applies undo of sample changes. These deliberately small examples make placement and targeting concrete.
4. Select **Follow editing document**, preview, then switch tabs. The target label changes and previews clear. Preview again before applying. Select **Pin current document**, switch tabs, and inspect the fixed label. With comparison off a pinned tool can change an inactive editable document; with comparison enabled the protected reference can be previewed but Apply is disabled. Follow-editing with optional pinning is accepted. The specific comparison-navigation interaction shown here remains available for reaction.
5. Restore a preset. Main panel visibility, tool visibility/placement and zone sizes reset to that preset. Editing restores all four core panels and closes the auxiliary tools; optional presets restore their proposed membership. Tool bindings, inputs, pending previews, document contents, selections and history are preserved. Variant changes similarly restore that preset while keeping document/tool context. Optional Home keeps everything in memory.

Keyboard controls:

- Alt+1–4 open Styles, Search, Timing and History.
- Alt+5–8 open Video, Editor, Audio and Grid, respectively. These shortcuts show panels rather than toggling an already visible panel off.
- Alt+R restores the current preset and returns from Home.
- F6/Shift+F6 traverse visible core panels, reference and tools; Tab reaches their controls. Up/Down selects grid rows. Native select controls operate placement and target binding without dragging.
- The floating switcher or Left/Right outside editable controls/table rows changes `?variant=A|B`. B is the default after reload without a parameter.

The event line and full state inspector expose variant, current preset, core-panel visibility, shared tool placement, active zone tabs, target bindings, preview document/line/revision, editing/reference roles, selections and sample document/history content. Target, selection or document mutations invalidate previews, preserving the existing operation safety. Visibility changes do not retarget operations. Reload resets in-memory state except the URL-selected variant.

## Feedback still needed

- Does the Editing geometry now reflect the current program clearly, including audio above the editor and the full-width dense grid? The requested Editing default is not replaced by a task-specific preset.
- Are any of the optional task presets useful as shortcuts, or do their memberships need adjusting?
- Do the five legacy visibility equivalents retain the combinations you need?
- When selecting the protected reference document tab, is swapping editing/reference roles clear enough, or would an explicit swap action be easier?
- Can you tell which document is editing and which stays protected while navigating the comparison?

Movable panels, shared ownership, follow-editing with optional pinning, optional Home and the accepted visual direction are not reopened. The prototype omits complete style/search/timing editors, real undo and file/media engines, docking persistence and native Qt validation. Figma Starter remains optional for occasional handoff.

Validation: inline JavaScript parses with `node --check`; source whitespace checked. An earlier in-memory probe confirmed the eight optional preset memberships, unchanged sample documents across preset changes, and collapsed empty B tool zones. Parent browser checks of the previous version exercised follow/pin targets, protected-reference Apply blocking, stale-preview invalidation, named-document-only changes, placement, hide/reopen, Home and keyboard zone sizing. Those checks predate the new visibility/preset controls; fresh browser QA for this update remains with the parent agent. No native claim follows from HTML checks. Previous captures: [constrained zones](workspace-tools-preview.png), [simulated floating](workspace-tools-floating-preview.png).

Browser follow-up checked all eight preset memberships, grid hide/restore retaining the selected line, and explicit editing/reference role swap with the other document protected. Native docking remains unverified.

Current-layout follow-up: inline JavaScript syntax and an in-memory layout probe were checked after adding default Editing, ten synthetic rows and dynamic core-grid areas. Earlier browser checks predate this geometry update; parent browser QA should verify default panel bounds, full-width grid, hidden-panel reflow and retained operation safety. No attachment bytes or media were copied into the public artifact.

Fresh browser checks after the current-program layout correction: Editing opens with all four panels and no auxiliary tools; selecting the next line, hiding every panel and restoring Editing retains that selected line; F6 from Video reaches Audio. The revised visual-language study also verified Video → Audio and Editor → Grid / reverse focus traversal, with measured panel geometry showing the grid spans both columns. Current capture: [default Editing layout](workspace-tools-preview.png). These remain HTML observations.
