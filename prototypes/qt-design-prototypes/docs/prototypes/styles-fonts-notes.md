# Styles, font catalogs and ASS colors — prototype #54

**Status: proposed interaction study; human reaction pending.** Open [styles-fonts.html](styles-fonts.html) directly in a browser. No server, dependencies or installation are required. `?variant=A` shows parallel Document/style-catalog lists; `?variant=B` shows one active library beside an owner inspector. The bottom switcher preserves state. Arrow keys switch variants only outside fields, buttons and task dialogs.

This applies the accepted Compact Studio palette, Classic menus/local controls and shared Workspace geometry. It explores the Styles tool's internal organization, not a new theme or a docking implementation. The video/audio/editor/grid above it are context illustrations. Everything lives in memory: refresh/reset discards it. No storage, file import/export, font installation/enumeration, screen capture, network request or renderer execution occurs. Profile “load” and Style “import” use explicit built-in samples.

## Review walkthrough

Choose a guided scenario to reset to its fixture. Each numbered action performs a real state change; task dialogs repeat the next walkthrough action in their footer. Free-play controls remain available outside a modal task. The expandable live-state panel shows owners, revisions, retained draft, Style names and reference text.

1. **Transfer & conflicts:** preview Common/Default into Harbor03.ass; choose Replace; apply. Inspect the destination size change from 36 to 42. Preview all open Documents: Song has another same-name conflict and protected Reference is explicitly excluded. Cancel or choose destinations independently; the catalog source remains intact.
2. **Draft ownership:** retain a Harbor Style draft, switch editing to Song, resume the draft and inspect its unchanged destination. Discard it; pin Reference. Its Styles can be inspected and copied to a writable style catalog, but content mutation controls are disabled. Pinning an inactive unprotected Document remains a separate named target.
3. **Rename & clean:** select Sign and review its line-style reference, original/translated inline resets and comment. Rename to Station and inspect those fixture references. Analyze cleanup: used Styles remain, Default is retained by the proposed rule, Unused is selectable, and Unresolved is retained as uncertain. The uncertainty is an explicit fixture ledger entry, not a parser discovery.
4. **Font & ASS alpha:** open a Style draft, choose the missing authored request, then open Primary color. Set transparency to 128, sample a coordinate from the built-in screen illustration, or use keyboard HSV/RGBA/ASS fields. Cancel sampling to restore the previous color; Keep sample retains RGB and the prior ASS alpha. Use color stages it in the retained draft; Resume draft / Apply commits to its named owner.

Also try new/empty style catalogs, copying/reordering Styles, deleting a used Style with an explicit replacement, profile filtering and Save filter, adding subtitle font requests, simple/full color pickers and recents. Font filtering updates on field change/Enter/blur. Font profiles are the UI term for named **Font catalogs**: collections of font-family names, not font files.

## Capability coverage and explicit remainder

| Inventory family | Working representative coverage | Remaining parity / evidence |
|---|---|---|
| Document Styles / style catalogs | Separate owners; new, edit, copy, rename, delete, synthetic import; copy either direction and catalog → all open ASS Documents; first/up/down/last/sort; used/clean review | Multi-selection breadth; real `.sty`/ASS readers and writers; malformed/unknown records; locale ordering; production transactions and persistence |
| Detailed Style editor | All 23 ASS V4+ fields: Name, Fontname, Fontsize, four colors, four decoration flags, ScaleX/Y, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL/R/V and Encoding; nine alignment positions; sample text | Native libass preview; SSA/extended fields; mixed-value bulk edits; complete range/format validation and unknown-value preservation |
| Font selection | Search family/alias/localized metadata, fixture coverage filter, requested versus candidate identity, collection faces, attachment/system examples, missing request and variable-instance uncertainty; sample text; simulated environment refresh | Actual provider enumeration, glyph/character filtering, font style/size matching, refresh/cancel/concurrency, document environment isolation, platform qualification |
| Font catalogs/profiles | New/copy/rename/delete/load sample/save current filter; add/remove selected names; gather style requests plus an explicit inline fixture request from the tool target | Actual catalog file formats/import/export; complete original/translated inline font/reset/weight/italic scan; large-list operations; no install/uninstall |
| ASS colors | Four slots; exact `&HAABBGGRR`; numeric RGB/alpha; keyboard HSV spectrum and transparency sliders; recents; simple/full alternatives; cancelable simulated screen-picker coordinates | Native spectrum geometry, actual screen permissions/capture, scaling, HDR/color management and renderer agreement |
| Font diagnostics | Clear labels for captured identity, requested font missing, glyph missing, fallback, simulation, incomplete reimport and unverified instance; link to native evidence | No live trace, bytes/hash, provider query or collector completeness in HTML; real-document enumeration/reimport remains its own gate |

No omitted depth is a decision to remove that capability. The grid/history, collector/exporter, visual tools and native docking have separate studies.

## Policies deliberately proposed here

- Property drafts bind an owner and revision. Switching editing does not retarget them; stale drafts stay unapplied. Renaming/deleting a style catalog with its retained draft is blocked until that draft is applied/discarded. These are study mechanics, not acceptance of the broader edit-transaction proposal.
- Transfers copy and preserve the source. Each target starts at Skip; Replace preserves the existing target identity/name and changes its properties; Rename copy creates a unique name. Protected targets are excluded. Applying a revision-stale preview is blocked.
- Rename updates only exact fixture references in the owning Document, including original/translated reset tags and comments. Deleting a used Style requires an existing replacement. Uncertain references block rename/delete. Cleanup requires explicit checked removals, retains Default and uncertain entries, and does not delete catalog Styles based on Document use. The tiny fixture scan is not a production ASS parser.
- Collection names use case-sensitive uniqueness; the demo retains one style catalog and one font profile. Sorting uses browser locale order. These are provisional safeguards, not approved cross-platform persistence rules.
- Font profile membership organizes requests. Choosing a missing family keeps that authored request. Localized labels/aliases do not silently rewrite it; a profile name does not imply files were resolved or collected.
- Palette/screen sampling changes RGB while retaining ASS transparency. Recents restore the complete ASS value and update only on Use, most recent first, capped at eight. Full and simple pickers share one value. The HSV illustration rounds RGB to integers; native color behavior remains unverified.
- “Undo demo change” restores the prior in-memory Documents/catalogs/profiles and clears the retained draft. It is a review convenience, not a production undo/draft-reconciliation contract. History is capped at 15 demo mutations.

## Accepted evidence boundary

The [accepted font contract](https://github.com/altqx/hikari/blob/dbea7dba3ad10689596e705806aa8a9f20f37893/docs/qt/fonts.md) makes the Document's libass environment authoritative. This browser sample deliberately uses browser sans-serif and only illustrates size, primary color/alpha and decoration; it does not resolve the requested family or render ASS geometry/shaping/outline/shadow/encoding.

The separate [native Windows experiment](https://github.com/altqx/hikari/blob/0f776e446df085d463fc82c1073a868a7dd24c51/HikariSub/prototypes/font-identity/README.md) observed 25/25 stock/hook sample agreement and 17/18 captured-set reimport agreement. The multilingual reimport differed despite all three captured files. Equal pixels may still reproduce missing requests/glyphs. Those bounded results do not describe the current HTML selection, prove complete collection, qualify Linux/fontconfig or variable instances, or establish native accessibility/performance.

Sources: [complete UI inventory](https://github.com/altqx/hikari/blob/6f91fd28eab4c42d2c00f848606b47585c2b6bc8/docs/research/ui-inventory.md), [complete data inventory](https://github.com/altqx/hikari/blob/1eea34cd5db8db276f2e71cdff3f43f345cefd5b/docs/research/data-inventory.md), [font research](https://github.com/altqx/hikari/blob/aff69e6c950a1ab03d02eb837ece9d47b90d1dae/docs/research/fonts.md), and the inventoried legacy `StyleChange`, `StyleStore`, `FontCatalogList` and `ColorPicker` families. The property list was checked against the local `HikariSub/StyleChange.cpp` implementation; catalog transfer and profile operations against their corresponding sources.

## Focused reaction and verification

Choose A/B or a combination; assess the named owner/conflict review, reference-impact review, and separation between authored request, illustration and diagnostic evidence. Confirm or revise the proposed cleanup/default retention and alpha/recents mechanics. These questions do not reopen accepted theme, protected-reference or renderer-authority decisions.

Inline JavaScript syntax was checked. The linked local native report exists. Browser interaction/layout QA and actual human reaction are still required; no native rendering, accessibility, filesystem or performance pass is claimed. No formal test suite accompanies this throwaway study.

## Root browser review

On 2026-09-27, browser checks exercised named-owner transfer/replacement and protected all-open exclusions; retained Style draft ownership after switching Documents; rename effects across exact fixture references; cleanup preserving Default/uncertain references; retained missing font requests; 128 ASS transparency; keyboard sampling and cancel restoring the prior color. Both arrangements were visually inspected and captured at the default 1280-wide viewport, without horizontal overflow in B. These observations do not prove native font rendering, complete ASS parsing, accessibility or every listed action. The local native report link uses the separately running 8766 report server; its published run guide remains available when that server is stopped. Human reaction remains open.
