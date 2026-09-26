# Workspace tools: placement follow-up (throwaway)

Issue: [Prototype the information architecture: panels, workspaces, home screen](https://github.com/altqx/hikari/issues/31).

Question: where should Styles, Search, Timing and History live within the accepted **shared workspace**, and should each tool follow editing or stay pinned to a document? Compact Studio D supplies the existing visual treatment. Hikari-owned Qt, shared layout ownership, optional protected comparison and the accepted visual direction are not reopened here.

Open `workspace-tools.html` directly, or reuse the prototype server:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory docs/prototypes
```

- [A — constrained preset zones](http://127.0.0.1:8765/workspace-tools.html?variant=A): tools share a tabbed right zone or bottom drawer. Width/height sliders resize the zones. Presets constrain placement to those two zones.
- [B — movable tool panels](http://127.0.0.1:8765/workspace-tools.html?variant=B): tools are independent cards with explicit Left, Right, Bottom and Float placement choices. Floating is an overlay **inside this HTML page**, not a native detached window or proof of KDDockWidgets, multi-monitor handling, saved geometry, accessibility or performance.

The floating bottom switcher changes the URL's variant parameter. Arrow keys outside editable controls/table rows switch variants; F6/Shift+F6 traverse displayed editing/reference/tool regions. Alt+1 through Alt+4 open Styles, Search, Timing and History. Native select controls provide keyboard placement and target-mode choices; no dragging is required. Grid rows support Up/Down. Responsive fallback stacks regions on narrow windows; this is a desktop placement study, not a mobile design.

## What to try

1. Open **All tools**. In A, switch the right/bottom tabs and resize both zones. In B, move Search from Float to Bottom and Timing from Left to Right. Which arrangement leaves enough room for subtitle text?
2. On Styles, choose **Preview**, inspect the named document/line, then **Apply**. Search performs literal case-sensitive replacement on the selected line only; Timing shifts its synthetic start time; History can preview/apply undo of sample changes. These are deliberately tiny examples to make tool placement concrete.
3. Select **Follow editing document**, preview, then switch document tabs. The layout stays shared and previews clear; the target label changes. Preview again before applying.
4. Select **Pin current document**, switch tabs, and inspect the fixed target label. Pinned tools can affect an inactive document while comparison is off, so the target remains prominent. Enable **Protected comparison**: the pinned reference can be previewed but Apply is disabled. The reference selection has its own Next button. Switching to the reference tab explicitly swaps which document is editing versus protected.
5. Hide a tool and reopen it from the toolbar or keyboard. Restore a preset; this restores placements, visibility and zone sizes while preserving tool target bindings, tool inputs, previews and document edits. Variant switches similarly reset the arrangement while preserving those inputs/context. Optional Home keeps everything in memory.

Every action updates the event line and the full state inspector: shared layout, active zone tabs, panel visibility/placement, follow/pin bindings, pending previews with document/line/revision, editing/reference roles, document selections, history and contents. Target, selection or document mutations invalidate previews so Apply cannot use an old editing target. Reload resets all sample state except the URL-selected variant; there is no persistence.

## Feedback still needed

- Which tools should be permanently visible, tabbed together, in the bottom drawer, or opened as dialogs?
- Are constrained zones enough, or is the extra freedom of movable/floating panels valuable?
- Which presets should open Styles, Search, Timing and History by default?
- Is per-tool Follow/Pin understandable, including applying to an inactive editable document? Should some tools always follow editing instead?
- Does the target label give enough confidence before Preview/Apply, including when comparison is protected?
- Should Home remain an optional view?

These are review questions, not assertions of approval. The sample intentionally omits a complete style editor, full search scopes, postprocessing algorithms, a real undo engine, file I/O, media decoding, docking persistence and native Qt validation. Figma Starter remains optional for occasional handoff; this artifact is the review surface.

Validation: inline JavaScript parsed with `node --check`; source diff checked for whitespace. Browser checks exercised pinned versus following targets, protected-reference Apply blocking, stale-preview invalidation on document switch, applying only to the named document, both variants, placement changes, hide/reopen retention, Home return and keyboard zone resizing. These HTML checks do not establish native Qt docking or screen-reader behavior. [Constrained-zone capture](workspace-tools-preview.png) and [simulated-floating capture](workspace-tools-floating-preview.png) show the review artifact.
