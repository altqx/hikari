# CX7 #810: context menu audit for beta.4

Read-only audit at `main` 8dbf88f41 (the beta.4 candidate; the owner said on 2026-11-16 that no more menu changes are coming). It covers every right-click menu, every ⋯ and » menu, and the dropdowns that wave 8 added, in the CX1 #557 format. The baseline is the CX2 #578 approved design, built by CX3–CX6 on top of 1d122c203. `tests/ui/context_menus.txt` didn't exist at 1d122c203, so "changed since" compares against the approved drawings in `docs/ux/context-menus.md` and the listing as CX3–CX6 left it.

The rules checked: `docs/ux/context-menus.md` (CX2 approved design), `docs/ux/text-style.md` (#596), and `docs/ux/menu-icon-gaps.md` (icons).

## Inventory

Four files, one per area, each with the CX1 tables, counts, what changed since CX2 (with the PR that added each row), and that area's raw findings:

| Area | File | Menus |
| --- | --- | --- |
| A. Grid and Line editor | `inventory/A-grid-editor.md` | 12 |
| B. Video, audio, visual tools, node effects | `inventory/B-video-audio-tools.md` | 19 |
| C. Panels, tabs, Home, Explorer, Styles, fonts, layouts | `inventory/C-panels-shell.md` | 23 |
| D. Automation, Hikari AI, Shift times presets, plugins | `inventory/D-automation-ai-plugins.md` | 12 (+11 dropdown groups) |

### What wave 8 changed, in short

- **Grid row menu:** it grew from 16 approved top-level rows (18 with AU18) to 31 (35 while AI Review is on), in 7 groups.
  - Review rows (AI2).
  - Typesetting graph… (AU9).
  - AI ▸ (AI8).
  - Remove tags… (AU4).
  - Interpolate across lines… (AU22).
  - Convert Aegisub folds (FA5).
  - A text group (AU20) and a position group (AU21).
  - Split ▸ gained Bake animations, At tag blocks and Every N… (FA15, AU10).
  - Add QC note… (#577).
- **Line editor text menu:** it gained project dictionary rows (PJ5d), the AU20 text rows, Remove tags…, Phantom ▸ and AI ▸.
- **Panel ⋯:** it gained side bar rows (DK3), Lock layout (DK4), QC notes rows, and plugin rows plus About (PL4).
- **Document tab:** it gained Move tab to new window and Move tab to window ▸ (MW2).
- **New menus:**
  - Home project row (PJ2), Explorer row (PJ4).
  - Project tier menus (PJ5a and others).
  - Align and distribute ▸ and Phantom ▸ (AU21).
  - Node effects Graph and Apply (NE2, NE4, TH5d, AU9).
  - Agent thread ⋯ and the attach, slash, model and mode menus (AI panel).
  - AI ▸ (AI8).
  - Shift times preset ⋯.
  - Shapes and All tags row menus.
- **Unchanged since CX2:**
  - Grid header, text fields, time fields, tag button, tag list.
  - Video and full-screen menus, Full screen bar ▸, speaker.
  - Styles, Style manager, Home file row, Reference tray.
  - Macro row and recommended script.

## Findings

75 raw findings in the four files. Merged and de-duplicated, they are the 16 menu sections and the cross-cutting rules below; each gate number matches a section of the mockup (`mockup/index.html`). "Visible: no" items need no approval and go straight into the fix PRs.

### Menus that change visibly (mockup sections 1–16)

1. **Grid row menu** (A1, A2, A8, A9, A11, A12).
   - It is flat again at 31 rows.
   - Add QC note… sits after the deletes.
   - Convert Aegisub folds is greyed in almost every file.
   - Disabled rows hide their reason in status-bar help, which never shows on a disabled row.

   Proposal (option A, recommended): 25 rows in 7 groups, never more than two levels deep.
   - Insert ▸, Duplicate, Cut, Copy, Paste, Paste special ▸
   - Join ▸, Split ▸, Swap lines, Make times continuous ▸, Make line group
   - Line breaks ▸, Change case ▸, Swap dialogue alternatives, Remove tags…
   - Typesetting ▸ (Blur and glow layers…, Build layers with the last values, Typesetting graph… | Interpolate across lines… | Position to margins, Margins to position), Change alignment, keep position ▸, Align and distribute ▸
   - AI actions ▸
   - Select lines visible on video, Show subtitles preview, Translation matching…, Add QC note…
   - Delete text, Delete N lines

   Convert Aegisub folds is shown only when the file has folds. Option B (the slice's proposal, 20 rows) folds the text rows into Text ▸ too, which puts Line breaks ▸ and Change case ▸ three levels deep. Not recommended.
2. **Line editor text menu** (A3, A8, A9, A10, A13).
   - "Check spelling" names both the as-you-type toggle and the Spellchecker panel command.
   - The two dictionary rows are worded and iconed differently.
   - The shared rows are ordered differently from the Grid.
   - A read-only field loses a separator.
3. **Line editor strip » and Original text »** (both are » buttons, tooltip "More") (A4, A5, A6, A7).
   - Text position ▸ uses check items for a one-of-nine choice, and they keep the menu open.
   - Original text ⋯ shows no keys.
   - "Add a tag button" has no "…".
   - Alignment names differ from the Grid's ("Top-left (an7)" against "Top left (\an7)") and are built from translated pieces.
4. **Explorer row and Explorer ⋯** (C2, C4).
   - Move to the trash sits mid-menu, without "…".
   - The narrow header hides the tool strip, and nothing replaces it.
5. **Empty tab bar** (C3). The MW2 Move rows show on the empty bar and act on the editing document.
6. **QC notes ⋯** (C1, C7, C8).
   - Remove all notes… isn't alone after a separator.
   - "Import notes from the clipboard" is also called "Paste notes", and it can ask without showing "…".
   - Remove versus Delete.
7. **Panel ⋯ rows** (C6, C9, C17, D-F7).
   - Show times / Show frames and the video follow choices are check items, not radio rows (approved 17 drew radios).
   - A missing plugin's Close is "Remove from the layout" in View › Panels.
   - "About %1…" has an ellipsis.
   - The side bar Move left / Move right read like the tab rows.
8. **Video menu** (B3, B4, B11).
   - Full screen on ▸ never offers the primary monitor.
   - Recently opened ▸ shows empty headings.
   - Change aspect ratio opens a dialog but has no "…".
9. **Audio overflow and Customise** (B1, B10, B12, B15).
   - The play commands have up to three names.
   - Customise rows show keys for commands they don't run.
   - The two link rows share an icon.
   - Snap to vocal onsets sits alone between separators.
10. **Node effects Graph menu** (B6, B7, B8, B13).
    - » is used where ⋯ belongs.
    - "From the library…" opens a submenu.
    - The export rows are check items that also act.
    - The first preset group has no heading.
11. **Align and distribute ▸ and Phantom ▸** (A14, B12, B16).
    - Icons have two meanings.
    - The rows repeat the parent's verbs.
12. **Shift times preset ⋯** (D-F3). Delete preset… isn't last.
13. **Automation script dialog ⋯ › Presets ▸** (D-F4). The project tier submenus come after Delete preset ▸.
14. **Agent panel** (D-F1, D-F8, D-F9, D-F12, D-F15, D-F18).
    - The thread ⋯ uses » with the tooltip "Thread menu".
    - Delete thread asks a question without showing "…".
    - The model and mode menus use check items for one-of-N.
    - The menus end in "…" rows that open Settings.
    - "Compact conversation" sits next to "Rename thread".
    - Thread rows have no right-click menu.
15. **Font catalogue menus** (C11, C12). The picker's menu and the Style editor's + list the same rows in different orders. "Manage catalogues" and "Manage layouts…" break the window-name rule in opposite directions.
16. **AI actions ▸** (A8, D-F8, D-F9, D-F17).
    - Its name differs across three places.
    - Custom actions that ask show no "…".
    - Nothing separates custom actions from the built-in ones.

### Rules across menus (mockup section 17, table only)

17. **One tooltip for ⋯: "More options"** (A15, B5, C10, D-F1, D-F2). There are now ten names: "%1 options", "More", "More audio options", "Answers and presets", "Preset commands", "Thread menu", "Graph menu", "Tag list options", "Catalogue tiers" and "Layout tiers". text-style.md already lists "More options" and names "%1 options" and "More audio options" under "Not". » overflow buttons that say what they hold ("More tools") keep their names. Where a list repeats ⋯ per row, the row's name goes into `Accessible.description`.
18. **One icon, one meaning** (A14, B12, C17, D-F14). Five new icon roles:
    - `align-distribute` (the submenu)
    - `phantom-keep-start`, `phantom-keep-end`
    - `audio-sliders` (Customise › Zoom and volume sliders)
    - `side-bar-off` (Move out of the side bar)
    - MCP Copy setup rows use the existing `edit-copy`
19. **Ellipsis and keys in labels** (A7, A18, B7, B11, B18, C7, C12, C15, B17, D-F7, D-F8, D-F9). Each row is in the mockup's table, with today's label and the new one. Two need a rule from the owner:
    - Rows named "<area> settings" or "Settings › AI" that open Settings at a page: recommend no "…", per decision 5.
    - "Manage layouts": recommend no "…", because its window acts at once.
20. **Delete versus Remove for QC notes** (C8). Notes are data stored in the file, so recommend "Delete all notes…" and the question button "Delete all".
21. **"Thread" or "conversation"** (D-F18). CONTEXT.md has neither term. Recommend "thread" everywhere: "Compact thread", in the menu, the slash menu and the role.
22. **New right-click menus** (C20, D-F12). Recommend one for Agent thread rows: the thread ⋯ rows, opened by right-click and the Menu key. Answers get the text menu (Copy, Select all). Recommend no QC notes row menu for beta.4: docs/ux/qc-notes.md doesn't ask for one, and every row action already has a key and a control.
23. **Menu bar items seen in passing** (B2, B14, D-F5, View › Toggle side bar): out of this card's right-click scope. Recommend fixing them in the same PRs, because they are the same names:
    - Video › Streams ▸ becomes "Audio track ▸" with radio rows.
    - Chapter times move to the detail column, without brackets.
    - Automation macros and plugin commands show their shortcut.
    - "Toggle left side bar" becomes "Left side bar" (a check item).

### No visible change (fix PRs only, no approval needed)

- **The context_menus.txt ledger** (A17, B19c, C19, D-F10):
  - add the Review rows, Filters ▸, the empty-tab-bar variant and one "Panel options (<panel>)" section per panel with own rows;
  - add the Automation script, Shapes row and All tags row menus.
- **Keyboard routes** (A16, C5, C13, D-F11):
  - Tab focus for the strip ⋯ and Original text ⋯;
  - `MenuKey` on side bar icons (Shift+F10);
  - `MenuKey` on the tier rows in Manage layouts and font catalogues, and on the Shapes and All tags lists.
- **Bugs:**
  - C14: the floating window ⋯ › Close bypasses `closePanel`, so the reference may stay loaded. Plausible; verify under vx11 first.
  - D-F16: hidden tier rows in the Shapes and All tags ⋯ may leave blank gaps. Verify on screen.
- **Code:**
  - C16: the recent-file tooltip is built from translated pieces.
  - B19a, b: a hand-made heading, and fixed objectNames on the shared Filters ▸.

## Fix PRs after approval (one area each)

1. Grid and Line editor menus: sections 1–3, the area-A rows of 16–19, and the area-A ledger and keyboard items.
2. Video, audio and node effects: sections 8–11 and the area-B rows of 17–19.
3. Panels, tabs, Explorer, QC notes and fonts: sections 4–7 and 15, the area-C rows, C14 and C16.
4. Automation, Hikari AI and presets: sections 12–14 and 16, 21–22, and the area-D rows.
5. New icons (rule 18): the five roles through the icon pipeline. PRs 1–4 use stand-ins until it lands, or wait for it.

Each PR:
- regenerates `context_menus.txt`;
- passes `everyMenuIsAShellMenu` and `contextMenusAreTheListedOnes`;
- carries the ko, pl, ta and th `.ts` changes (TR1);
- updates `docs/ux/context-menus.md` (a "What CX7 built" note) and the guide pages that name the rows.
