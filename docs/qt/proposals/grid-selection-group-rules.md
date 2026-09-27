# Grid range selection and Line-group boundaries

**Source characterization and closure proposal, 2026-09-27; not an accepted behavior change.** Supports [Prototype Grid selection, row operations, filters, Line groups and History](https://github.com/altqx/hikari/issues/56). The [accepted Grid contract](../ux/subtitle-grid.md), [Document model](../document-model.md) and [compatibility policy](../compatibility.md) take precedence. The accepted toolbar/task-review placement does not approve every demonstration rule in the HTML study.

Evidence is canonical `main` at **20d647c4c769ab7f5d383cf3c1c33f03876a94e9**. Cited local files were compared with that commit's GitHub contents and matched after line-ending normalization. Findings below trace source, not an executed legacy interaction, save round trip or native accessibility test. Examples assume no pending Editor draft; draft reconciliation has its shared owner.

## Selection: destination and membership are different

Legacy selection stores integer source-row keys. `lastRow` and `extendRow` are integers, not persistent Line identities. Mouse Shift selection takes the inclusive numeric interval between `lastRow` and the clicked destination, passing `skipHidden=false`; Ctrl+Shift adds that interval, otherwise it replaces selection. Keyboard Shift also passes `skipHidden=false`. The range helper inserts every stored row in that interval, without a separate Line-kind check. Ctrl+A likewise includes hidden rows. [Mouse range][mouse] · [Keyboard range][keys] · [Range helper][selections]

Keyboard destinations, however, count visible rows through `GetKeyFromPosition`: arrows move one displayed row, Page moves a viewport count, and Home/End request the extremes. With **Change active on selection** enabled, Shift moves current while keeping the extent anchor; disabled, it moves the extent while current stays fixed. A fresh keyboard Shift range initializes from current. There is no hidden-anchor fallback test in these range paths. The plain-arrow branch computes an extent-based `curLine` but actually navigates from `currentLine`; preserve the executed expression as evidence rather than treating its comment as behavior. [Navigation][navigation] · [Keyboard branches][keys]

Two hiding operations differ:

- **Collapse:** `OpenCloseTree` changes member flags/visibility without clearing selection. Clicking the description may choose a visible selected row as current; when the only selected row is the newly hidden current member, it can leave that member selected/current. The click returns before ordinary anchor handling. A subsequent Shift-click can therefore extend from a hidden `lastRow` through hidden rows. [Collapse][collapse] · [Description click][treeclick]
- **Filter/hide/reveal:** `FilteringFinalize` calls `RefreshSubsOnVideo`, which clears all selections, finds a visible replacement for current, and selects it. `FindVisibleKey` searches backward first, then forward; with no visible row it returns zero. These calls do not explicitly reset `lastRow`/`extendRow`; `SetModified` uses `redit=false`. Do not describe filtering as preserving selected identities in legacy. [Filter finalization][filter] · [Refresh][refresh] · [Visible fallback][visible]

Selection membership is also not a universal command scope. `GetSelections` defaults to visible rows; Duplicate and Split consume such lists. Copy explicitly includes hidden selections; Delete erases the stored selected keys; selected Sort uses that stored set. Availability guards can still require visible selections. Every migrated command needs its own captured eligibility rule, not a global “selected means visible” conversion. [Selection API][api] · [Duplicate][duplicate] · [Copy][copy] · [Dispatch][dispatch] · [Delete/sort][sort]

### Suggested preserve rule, without inventing a fallback

Use stable IDs for the anchor, current and selected set. While the displayed order is the filtered/collapsed projection of Document order, preserve the legacy **inclusive Document-order interval** between anchor and displayed destination, including hidden Line IDs, with visible hidden-selection counts and explicit command scope. A hidden anchor still exists: neither destination fallback nor visible-current fallback is needed. Navigation destinations continue to follow displayed order. This is compatible with identity preservation and retained hidden selections; retaining selections does not mean range replacement must union every old selection.

The accepted contract already replaces filter-induced selection clearing and positional identity transfer. Implement those accepted outcomes; do not ask again whether filtering may silently forget the hidden selection. Empty results must retain IDs and avoid invalid current-row seeking. Do not map opaque/non-Line source records into invented editable Line IDs.

**Interpretation to make explicit when closing the ticket:** the Grid contract's “displayed order” determines navigation destinations; it does not explicitly say whether hidden interior Lines enter a range. If it was intended to require displayed-only range membership, that is a departure from the source behavior above. The HTML's displayed-only ranges and two fallback choices are proposals, not discovered parity. A separate display-only sort that differs from Document order would also need a defined range order; the HTML demonstration does not itself accept that feature. Deleted anchors are a distinct identity-lifetime case, not evidence for a hidden-anchor fallback.

## Groups: local flag propagation, no general repair

The legacy representation is a description flag followed by opened/closed member flags; it has no group ID or explicit member list. ASS Actor markers parse/serialize those flags, and `Dialogue::Copy` retains them. Consumers infer a run ending at an ordinary row or another description. Moving flags can therefore change apparent membership without an explicit group operation. [Parsed flags][flags] · [Serialized flags][serialized] · [Copy fields][copyfields] · [Group consumers][groupops]

| Operation | Source behavior and migration implication |
| --- | --- |
| Create | Creates a description for each selected visible run and marks members closed. Hidden/non-dialogue rows are skipped without terminating the run. Selecting visible A and B around an ordinary hidden X can yield `description, A(member), X(ordinary), B(member)`: no valid single contiguous group. Preserve ordinary contiguous creation; this cross-hidden case needs disposition. [Creation][create] |
| Insert / duplicate / split | Before/after insertion copies current; Duplicate copies selected Lines; video-time Split copies the member into the adjacent fragment. All retain group flags. Inserting a member beside members can extend a valid run. A normal duplicate inside a group is **not inherently group-breaking**, unlike the study's blanket demonstration gate. New copied Lines get new IDs under the accepted model. [Insertion][insert] · [Duplicate][duplicate] · [Split][split] |
| Paste | Before a member, ordinary row paste inherits neighboring member state; before an ordinary row after a group it does not automatically join that group. Parsed pasted markers can survive where no local override is assigned. `visibleBefore = isVisible` does not read the preceding row's visibility, so do not claim symmetric visibility inheritance. This is a local heuristic, not a global repair. [Paste][paste] |
| Move / swap / sort | Move deletes/reinserts copied Lines; swap/sort permute records and their flags. Insertion helpers merely insert. No group-contiguity validator or repair appears in these paths or `SetModified`. They can orphan a member, split a run, or attach it to another description. [Move][move] · [Sort/swap][sort] · [Insertion helper][inserthelper] · [SetModified][modified] |
| Delete / join | Delete removes chosen records without repairing group flags. Join retains the first record's flags; its text/time input list and physical deletion interval differ. Both join variants delete from the second selected row through the last, including intervening unselected/hidden rows. That potential loss needs a controlled fixture and named disposition; do not silently encode it as either required parity or an approved fix. [Deletion][sort] · [Join][join] |
| Explicit group commands | Remove group clears flags, reveals hidden members, **deletes the description row**, then records history. Select group clears selection, opens closed members and selects members, excluding the description. Add Lines explicitly relocates selected Lines beside the group. Keeping the description as a plain comment during removal is the HTML's alternative, not legacy parity. [Group commands][groupops] |

### Closure route for group edits

Preserve characterized valid outcomes: within-group text/timing edits, member duplication/insertion/splitting that keeps contiguity, member deletion that leaves a valid run, and explicit rename/select/remove. Update the member-ID list for valid structural changes; unchanged membership is not an invariant. Do not require annotation removal merely because the member list changes. A standalone description left after deleting all members must remain representable as source data; the model does not authorize deleting it automatically.

There is a real contradiction for edits that leave malformed runs: **preserving raw flags cannot simultaneously guarantee accepted contiguous groups without defined command behavior.** Imported malformed flags remain provenance/diagnostic data; that does not authorize generating new malformed groups.

Suggested bounded decision **G56-contiguity**: allow commands whose result preserves valid groups; for an actual group break, reject before mutation, identify the affected description/members, and offer Cancel or the existing explicit Remove group operation followed by a rebuilt command. Removal must show its legacy description deletion. This proposed eligibility restriction needs approval; accepted **C07-atomic-rejection** determines what happens once a command is rejected, not which legacy commands may newly be rejected. Automatically moving whole groups, splitting groups, dropping flags or retaining the description as a comment are different unapproved outcomes. Apply the same validator to every structural command, including staged automation results, without changing the pending Lua transaction contract.

The cross-hidden creation and Join deletion-interval hazards are exact source cases to resolve alongside this boundary, not a request to approve all row-edit fixes. A preview must expose the actual deletion set before anyone can choose its disposition.

## Minimal characterization fixtures

These are **specified checks, not run results**. Use stable synthetic IDs in the rewrite and source positions in the old program; preserve input copies and record current/anchor/selected IDs, visibility, group flags, actual deletions and Undo restoration.

| Fixture | Source-derived expectation / required comparison |
| --- | --- |
| Visible A, hidden H, visible B; anchor A; Shift-click B | Legacy selects A/H/B. Compare ordinary replacement and Ctrl+Shift addition; distinguish destination navigation from hidden membership. |
| `D(description), A(member), B(member), C`; only B selected/current; collapse D, then Shift-click C | On the no-draft path, B remains the hidden anchor; range is B/C. Repeat Shift+Down with both active-on-selection settings and an existing extent. |
| Select A/H, apply a filter hiding H | Legacy refresh clears the old set and selects visible fallback; accepted rewrite retains A/H. All-hidden results must not issue an invalid seek. |
| Duplicate A in `D,A,B,C(ordinary)`; then Undo | Legacy result is `D,A,A-copy,B,C`, with a valid member run. Rewrite keeps A's ID and gives the copy a new ID; no automatic ungroup. |
| Move A outside that run, swap an ordinary row into it, sort across two groups, delete D | Capture flags and actual inferred groups. Current source has no universal repair; G56 must specify accepted result or rejection before parity assertions. |
| Create from A/B separated by hidden ordinary X; separately Join selected A/B/C in Document order A,B,X,C | Creation can leave B outside its description's inferred run. Three-selected Join deletes B/X/C while collecting only A/B/C, so X can disappear without contributing text. Two-selected A/X/B is a necessary control: deletion begins at the second selected row B, so X survives. These potential loss/inconsistency cases are not golden behavior by default. |
| Remove group vs delete all members | Remove deletes D and opens/unflags members; deleting members leaves D. Keep those operations distinct, including Undo and original/translated text. |

Native keyboard/accessibility qualification stays with [the painted-grid feasibility and performance work](https://github.com/altqx/hikari/issues/45). No source inference here proves screen-reader announcements, every shortcut, clipboard serialization or production performance.

[mouse]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp#L1685-L1713
[keys]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp#L1831-L1922
[selections]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L503-L529
[navigation]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1932-L1967
[collapse]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L699-L746
[treeclick]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridWindow.cpp#L1469-L1494
[filter]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridFiltering.cpp#L218-L227
[refresh]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1898-L1919
[visible]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L538-L563
[api]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.h#L204-L209
[copy]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L660-L664
[dispatch]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L821-L852
[duplicate]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L380-L412
[flags]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L705-L736
[serialized]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L828-L846
[copyfields]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsDialogue.cpp#L1055-L1081
[groupops]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1763-L1896
[create]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridFiltering.cpp#L164-L198
[insert]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L346-L378
[split]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L1516-L1531
[paste]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L582-L652
[move]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L822-L925
[sort]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L442-L501
[inserthelper]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsFile.cpp#L990-L1018
[modified]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGridBase.cpp#L1125-L1162
[join]: https://github.com/altqx/hikari/blob/20d647c4c769ab7f5d383cf3c1c33f03876a94e9/HikariSub/SubsGrid.cpp#L415-L488
