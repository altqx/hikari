# Carrying ASS override tags from the Original text into the Translated text

This is a reference for how Translation mode could carry ASS override tags from a Line's Original text
into its Translated text, so that the translator types only the translation. It answers the owner's
request of 2026-10-20: tags from the original are applied automatically, and lines with several tagged
runs, such as `text0{\i1}text1{\i0\b1}text2{\b0}text3`, need a design that stays natural when word order
changes in translation.

Sources read:

- Legacy HikariSub and the Qt rewrite at worktree commit `1989b6636` (branch `w6-tm1`). Paths such as
  `HikariSub/EditBox.cpp:1811` refer to that commit.
- libass at `f61db567e6` (2026-09-17), Subtitle Edit `main` at `e88d1677b0` (SE 5) and `se4-legacy` at
  `96e6d30265`, Aegisub upstream `master` at `6f546951b4` and the TypesettingTools fork at `5bc77e4f61`.
  Short links such as [Formatting.cs] point to these commits in the list at the end.
- Vendor documentation, API references, the XLIFF 2.1 specification and two papers. All web pages were
  fetched on **2026-10-20**. Trados Studio pages on docs.rws.com refused automated fetches (HTTP 403), so
  the Trados claims rest on the search engine's excerpts of those pages. They are marked "(excerpt)".

Terms follow [CONTEXT.md](../../CONTEXT.md): Line, Document, Line editor, Grid, Original text, Translated
text, ASS override tag. "Block" means one `{...}` group in a Line's text. "Run" means a stretch of visible
text that carries one formatting state.

## 0. Summary

- **Legacy "Moving tags" moves tags, it does not copy them.** It pulls every `{...}` block out of the
  Original text, joins them into the Translated field and leaves the Original field as plain text. On
  commit, both fields are saved, so the Original text loses its tags. Inner blocks lose their positions:
  for the hard case the Translated field becomes `{\i1}{\i0\b1}{\b0}` with the caret at 0, so whatever
  the translator types lands before all three blocks and is neither italic nor bold. The Qt rewrite
  ports this behaviour faithfully.
- **ASS tags fall into two kinds.** Line-wide tags (`\pos`, `\move`, `\org`, `\fad`, `\fade`, `\clip`,
  `\iclip`, and in practice `\an`, `\a`, `\q`) act on the whole Line wherever they stand. All others act
  on the text after them until they are overridden. A block before all visible text is also effectively
  line-wide. These can be transferred with no interaction at all.
- **CAT tools converge on one gesture** for inline formatting: the target field shows text with tag
  placeholders, and the translator **selects target text and presses one key** to wrap it in the next
  (or a numbered) source tag pair. memoQ (F9), Phrase (F8), Trados (QuickInsert, excerpt) and Smartcat
  (Alt+number) document it. Missing tags are flagged by QA. Changed tag order is usually allowed or only
  warned about.
- **Subtitle tools only handle the easy cases.** Subtitle Edit strips leading, trailing and line-wide
  blocks and re-attaches them. Mid-line tags go to the translation engine as text. Aegisub's Translation
  Assistant asks for a translation of each plain segment between blocks, in source order.
- **Automatic placement is not reliable enough to be silent.** Word-alignment transfer placed tags
  correctly 72–83 % of the time with off-the-shelf aligners, and 94.7 % only with a supervised model
  trained for the task (Zenkel et al. 2021). DeepL and Google place tags "to the extent possible" with no
  guarantee.
- **Recommendation:** transfer line-wide, leading, trailing and whole-line-wrapper tags silently. For
  inner runs, show the Translated field with formatting rendered and the runs as numbered marks, and let
  the translator **select text and press a run key** (design A). Offer automatic suggestions only where a
  run's text reappears unchanged in the translation (design B, as an assist). Store the Translated text as
  real ASS, and derive the plain text and run placements from it on load. No sidecar data is needed.

## 1. Legacy HikariSub

### 1.1 How a Translation-mode Line is saved

- Turning Translation mode on adds a Style named `TLmode` (or `TLmode1`, …), a copy of `Default` with
  alignment 8. It is recorded in Script Info as `TLMode Style`, and `TLMode: Yes` is set
  (`HikariSub/SubsGridBase.cpp:1313-1330`).
- On save, a Line with a translation (or marked Unconfirmed) is written as two ASS events: first the
  Original text with the `TLMode Style`, then the Translated text with the Line's own Style
  (`HikariSub/SubsGridBase.cpp:337-350`, `HikariSub/SubsDialogue.cpp:826-855`).
- The original event is a **Dialogue**, so it renders at the top of the frame, unless the Line is a
  comment or "hide original on video" (`TL_MODE_HIDE_ORIGINAL_ON_VIDEO`) is on. Then it is a Comment
  (`HikariSub/SubsDialogue.cpp:831-832`). The rewrite does the same (`src/core/ass_save.cpp:184-187`,
  `214-218`).
- On load, an event in the `TLMode Style` is paired with the event after it, which becomes the
  Translated text (`src/core/ass_load.cpp:746-797`).
- Turning the mode off replaces each Line's text with its translation (`HikariSub/SubsGridBase.cpp:1354-1368`).

So the saved translation event is an ordinary Dialogue. It must contain real override tags to render
as intended. The original event keeps whatever tags the Original text holds.

### 1.2 "Moving tags" (`AutoMoveTags`)

- A toggle button "Moving tags" in the Line editor, shown only in Translation mode
  (`HikariSub/EditBox.cpp:250-251`, `1143-1144`). Its state is the setting
  `AUTO_MOVE_TAGS_FROM_ORIGINAL` (`HikariSub/EditBox.cpp:1802-1807`).
- `EditBox::SetTextWithTags` (`HikariSub/EditBox.cpp:1809-1874`) runs when a Line is shown. It acts only
  when Translation mode is on, the Line has **no** translation yet, the toggle is on, and no visual tool
  other than the crosshair is active (line 1811).
- It first deletes every `}{`, so adjacent blocks merge (line 1813). It then walks the text: every
  `{...}` block is appended to the Translated field, and every piece of text between blocks is appended
  to the Original field (lines 1814-1847). Comments in braces are treated as blocks too.
- The caret goes after the block only when the text **starts** with `{`. Otherwise it stays at 0
  (lines 1821-1823, 1856).
- It sets `splittedTags` (line 1855). On commit, `EditBox::Send` writes **both** fields while that flag
  is set (`HikariSub/EditBox.cpp:612-629`), so the stripped Original field replaces the Line's text. The
  tags have moved, not been copied.

Worked through for the hard case `text0{\i1}text1{\i0\b1}text2{\b0}text3`:

| Field | After SetTextWithTags |
| --- | --- |
| Original text | `text0text1text2text3` |
| Translated text | `{\i1}{\i0\b1}{\b0}`, caret at 0 |

Typing a translation gives `translation{\i1}{\i0\b1}{\b0}`. None of the text is italic or bold, and the
Original text has lost its formatting. `Gate {\i1}keeper{\i0}` behaves the same way: the case is recorded
in `tests/application/translation_mode_tests.cpp:343-344`.

### 1.3 The other translation actions

- **Paste all** (`EDITBOX_PASTE_ALL_TO_TRANSLATION`, `EditBox::OnCopyAll`,
  `HikariSub/EditBox.cpp:1153-1157`): replaces the Translated field with the Original field's text,
  tags included.
- **Paste the selected** (`EditBox::OnCopySelection`, `HikariSub/EditBox.cpp:1159-1173`): inserts the
  Original field's selected characters at the Translated field's caret.
- **Hide original** (`EditBox::OnHideOriginal`, `HikariSub/EditBox.cpp:1496-1502`): wraps the whole
  Original text in `{` `}`, turning it into a brace comment that does not render.
- **Paste translation** from a file (`SubsGrid::OnPasteTextTl`, `HikariSub/SubsGrid.cpp:945-1029`): each
  Line's Translated text becomes the next file line's text verbatim. No tag handling.

### 1.4 The Qt rewrite today

- `application::moveTagsFromOriginal` is a byte-faithful port of `SetTextWithTags`, quirks included
  (`src/application/translation_mode.cpp:100-135`). The tests and a 34-case legacy capture confirm it
  (`tests/application/translation_mode_tests.cpp:332-368`,
  `tests/fixtures/legacy-observations/local-e5-tag-move-20261012/README.md`).
- `LineEditorController::evaluateSplit` applies the same conditions (`src/ui/line_editor_controller.cpp:96-116`).
  A change to either field while split writes both fields, as legacy does (`src/ui/line_editor_controller.cpp:344-351`).
- `pasteAllToTranslation`, `pasteSelectionToTranslation` and `commentOutOriginal` port the three
  buttons (`src/ui/line_editor_controller.cpp:1338-1372`). The setting is
  `translation.autoMoveTagsFromOriginal` (`src/application/settings.cpp:81`).
- The rewrite already has a **hidden-tag projection** of a Line's raw text (`src/core/include/hikari/core/text_projection.h:1-60`).
  It maps each visible span to its source span, shows `\N` as a newline, and keeps every hidden token in
  order on edits. The accepted boundary rule is that text typed at a hidden-tag boundary goes **after**
  the tags (`docs/ux/ass-editor.md:7`). Any new design should build on this, not replace it.

## 2. ASS tag semantics (the classification's basis)

From the Aegisub manual's tag reference [Aegisub tags] and libass [ass_parse.c]:

- Aegisub divides tags into two categories. "`\pos`, `\move`, `\clip`, `\iclip`, `\org`, `\fade` and
  `\fad` are those in the first category; all others are in the second." First-category tags "should
  appear at most once in a line, and where in the line they appear is unimportant." Second-category tags
  "modify all text after the tag until the end of the line or until the property is re-overridden"
  [Aegisub tags].
- libass applies only the **first** `\an`/`\a` ([ass_parse.c] 584-605), the first `\pos` or `\move`
  (489-530, 606-620), the first `\fad`/`\fade` (650-656) and the first `\org` (657-668). `\clip` and
  `\iclip` take the last value (405-424, 729-746). `\q` sets the Line's wrap style (905-909) and is read
  once at layout, so it is line-wide too, although Aegisub does not list `\an`, `\a` or `\q` as
  first-category.
- `\be`, `\blur`, `\bord`, `\shad`, colours, alpha, font, size, scale, spacing, rotation and shear are
  **per glyph**: libass copies them into each glyph from the state at that point
  ([ass_render.c] 2132-2133 for `\be`/`\blur`). They are second-category tags that often happen to sit
  in the leading block.
- `\i` with no argument resets to the Style's value ([ass_parse.c] 831-835). `\r` resets every
  override, or switches to a named Style, for the text after it (805-811).
- `\t(...)` animates the tags inside it for the text after it. `\k` and its variants mark a karaoke
  syllable before each syllable [Aegisub tags].
- `\N`, `\n` and `\h` are "written in the middle of the text, and not inside override blocks"
  [Aegisub tags]. `\p1` turns the text after the block into drawing commands.
- "Any unrecognized text within override blocks is silently ignored, so they are also commonly used for
  inline comments." [Aegisub tags]. Aegisub's own parser types a `{...}` without a backslash as a
  COMMENT block and the text after `\p` as a DRAWING block ([ass_dialogue.cpp] 199-267).

## 3. CAT tools

### 3.1 memoQ (docs.memoq.com, "current" = memoQ 12.6)

- **Display.** Inline tags show as numbered placeholders. Opening, closing and empty tags have distinct
  shapes. Paired tags share a number in the short view. Bold, italic and underline are shown as
  formatting, not tags [memoQ tags], [memoQ grid].
- **Insertion.** F9 "Copy Next Tag Sequence" inserts the next unused run of source tags. **With target
  text selected, F9 wraps the selection** in a tag before and after. Ctrl+F10 "Quick insert tag" lists
  the allowed tags and inserts pairs together. Alt+F8 inserts all tags at once ("not recommended").
  Alt+F6 "Arrange Tags" fixes their order [memoQ edit ribbon], [memoQ shortcuts].
- **Paired vs standalone.** Tags pair by name and id. Special tags are never paired [memoQ grid],
  [memoQ tags].
- **QA.** Options to warn for missing or extra tags, for tags that are not well-formed against the source,
  and for changed tag order. Warning codes 2010, 2011, 2015 and 2016. All are warnings unless set to
  errors [memoQ QA settings], [memoQ QA warnings]. For inline tags "you are free to rearrange, add or drop
  them", but required formatting tags cannot be dropped [memoQ tags].
- **Automatic placement.** TM hits get the current source's tags substituted. With the Custom.MT plugin,
  tags are not sent to MT by default, and "memoQ will insert all the tags at the end of the
  machine-translated text" [memoQ tag strictness], [memoQ Custom.MT].

### 3.2 Trados Studio (docs.rws.com, excerpts)

- **Display.** Formatting tags are hidden by default and the text is shown formatted. Display modes are
  No Tag Text, Partial Tag Text, Full Tag Text and Tag ID [Trados show tags] (excerpt).
- **Insertion.** Ctrl+click a source tag to insert it, or the whole pair around selected target text.
  QuickPlace (Ctrl+,) lists source placeables. QuickInsert buttons insert a pair at the caret **or wrap
  the selected text** [Trados copy tags], [Trados QuickInsert] (excerpts). Auto-Insert Tags
  (Ctrl+Alt+Insert) transfers missing source tags using a TM with fragment alignment [Trados auto-insert]
  (excerpt).
- **Paired vs standalone.** Copying one half of a pair leaves a "ghost tag" placeholder for the other
  half until it is placed [Trados copy tags] (excerpt).
- **QA.** The Tag Verifier checks for added and deleted tags, ghost tags, spacing around tags and, as an
  option, changed order. "Ignore formatting tags" is on by default [Trados tag verifier] (excerpt).

### 3.3 OmegaT (manual 6.1)

- Tags are protected and greyed, written as short codes such as `<t0/>` [OmegaT quick start].
- Edit > Insert Next Missing Tag (Ctrl+T) and Insert Missing Tags (Ctrl+Shift+T, "Inserts the remaining
  missing source tags at the cursor position") [OmegaT menus]. No selection-wrap command is documented.
- Tools > Check Issues detects "missing or misplaced tags". A preference "Allow translated tags to be in
  a different order" keeps reordering off the issues list, and another can block creating translated
  files that have tag issues [OmegaT menus], [OmegaT preferences].

### 3.4 Phrase TMS

- Paired tags display as `{1>…<1}` in the editor [Phrase tags].
- **F8 inserts a tag; with text selected, F8 inserts a paired tag around it.** Ctrl+Shift+F8 inserts all
  tags. Ctrl+Alt+number inserts tag n. Ctrl+, opens "Insert Tag by Selection" [Phrase tags],
  [Phrase shortcuts].
- QA "Tags & formatting" flags missing or invalid tags. "Nested tags" (misplaced pairs) is off by
  default. No built-in check flags reordered standalone tags. Missing tags are auto-completed at export.
  Translators should place tags "in as close a position and order as possible" [Phrase QA], [Phrase tags].

### 3.5 Smartcat

- F8 "Insert tags in series"; Alt+number inserts the tag with that number at the caret or around the
  selected target phrase. Shift+F8 hides tags, and the Tag Aligner reinserts them [Smartcat hotkeys],
  [Smartcat tags].
- "All errors related to tags are critical by default and cannot be disabled." Tags "should be placed in
  the same order they are placed in the source text" [Smartcat QA], [Smartcat tags].
- Some MT engines, for example Google Neural, support tag auto-insertion [Smartcat tags].

### 3.6 Crowdin

- Tags can be hidden and then show as indexed placeholders: `<a href=…>Sample</a>` becomes
  `<0>Sample</0>` [Crowdin editor].
- No tag hotkey is documented. "Copy Source Skeleton" copies the untranslatable elements into the
  translation, "useful when translating strings with … HTML tags" [Crowdin editor].
- QA "Tags mismatch": translation tags must match the source. Order is not mentioned [Crowdin QA].
- Custom MT: tags become `<span class="notranslate">n</span>` before MT, and an MT result that drops or
  changes them is discarded [Crowdin custom MT].

### 3.7 What the CAT tools agree on

- The translator sees **text plus small numbered markers**, never raw codes. Formatting-only tags are
  often rendered as formatting.
- The main gesture is **select target text, press one key**, which wraps it in the next or the numbered
  source pair. A second key inserts the next missing tag at the caret.
- Paired codes are inserted and validated as pairs. A half-placed pair is shown (Trados ghost tags) or
  flagged (memoQ well-formedness).
- Missing tags are flagged. Changed order is allowed (memoQ, OmegaT option) or only warned about.
  Reordering paired formatting tags is a normal consequence of translation.
- No tool silently places inline tags with confidence. MT-based placement is an assist with a check
  afterwards.

## 4. Subtitle tools

### 4.1 Subtitle Edit

- **SE 5 (`main`).** Before machine translation, `Formatting.SetTagsAndReturnTrimmed` removes all
  leading `{\…}` blocks and all trailing ones. It moves mid-line blocks made only of line-global tags
  (`\an \a \q \pos \move \org \fad \fade \clip \iclip`) to the front. It also strips a whole-line `<i>`,
  `<font>` or `[…]` wrapper ([Formatting.cs] 59-193). `\r` is protected as a placeholder (111-119).
  Drawing rows (`{\p…}`) are not sent at all (68-76).
- After translation, `ReAddFormatting` re-wraps the line: start tags, text, end tags ([Formatting.cs]
  246-317). **Mid-line tags such as `{\i1}` or `<b>` are sent to the engine as text**, and no check
  follows the reply. The DeepL request sets no `tag_handling` ([DeepLTranslate.cs] 320-334). Google is
  called with `format=text` ([GoogleTranslateV2.cs] 264).
- `\N` becomes a real newline on load ([AdvancedSubStationAlpha.cs] 1293-1296), so engines never see it.
- SE 4 (`se4-legacy`) stripped only the first leading block and re-added it as a prefix
  ([Formatting.cs SE4] 61-70, 219-220).
- Both versions show original and translation side by side in a two-column grid
  ([AutoTranslateWindow.cs] 413-421).

### 4.2 Aegisub Translation Assistant

- The read-only "Original" box shows the whole Line, override blocks raw, with the current segment
  highlighted. The "Translation" box starts empty ([dialog_translation.cpp] 79-105, 237-264).
- It works **one plain block at a time**: `NextBlock`/`PrevBlock` skip override, comment and drawing
  blocks ([dialog_translation.cpp] 59-63, 195-235). `Commit` replaces only the current plain block and
  rebuilds the Line, so override blocks are never edited. Newlines typed become `\N` (266-287).
  "Insert original" (Insert key) copies the current block's text at the caret (289-292).
- Effect: tags stay exactly where they were, but the translator must translate segment by segment in
  source order. A sentence whose italic word moves to another position cannot be expressed without
  editing raw text. The TypesettingTools fork keeps the same design ([dialog_translation.cpp fork]).

### 4.3 Subtitle Workshop

No official documentation of its translator mode or tag handling was found online.

## 5. Machine translation and research

### 5.1 DeepL API

- `tag_handling` = `xml` or `html`. "Without `tag_handling`, tags are treated as regular text."
  `tag_handling_version` = `v1` (current default for older accounts) or `v2` (October 2025, default for
  accounts that first used tag handling after 2025-12-01) [DeepL tag handling v2], [DeepL translate].
- `ignore_tags` marks text not to translate. `non_splitting_tags` "never split sentences".
  `splitting_tags` always split. `outline_detection=false` turns off automatic structure detection
  [DeepL translate].
- Placement wording: the API "extracts the text from the XML structure, translates it, and places the
  translation back into the structure". For inline markup "tags stay attached to the words they wrap,
  and placeholder tags are placed next to the translation of the words that precede or follow them".
  One example notes that tags "are duplicated (which is expected here)" when a wrapped phrase splits in
  German [DeepL tag handling v2], [DeepL HTML].

### 5.2 Google Cloud Translation

- v3 `mimeType` defaults to `text/html` when blank [Google v3 REST]. In HTML mode the API "doesn't
  translate any HTML tags in the input, only text that appears between the tags", and the output keeps
  the tags "with the translated text between the tags to the extent possible due to differences between
  the source and target languages" [Google advanced].
- `<span translate="no">` or `class="notranslate"` protect text, in HTML mode only [Google troubleshooting].

### 5.3 Papers

- **Hanneman & Dinu 2020**, "How Should Markup Tags Be Translated?" (WMT 2020) [Hanneman 2020].
  Compared detag-and-project (strip tags, translate, reinsert by FastAlign word alignment), masked tags
  with data augmentation, and raw tags (Sec. 4). Detag-and-project "is not capable of changing,
  dropping, mutilating, or adding tags" by design (Sec. 6.3). In human evaluation, alignment projection
  did best for EN–DE and EN–HU, and masking came a consistent second (Sec. 6.4, Table 5). For EN–DE tags
  with index 3 or higher, 83.6–87.3 % were judged correctly placed across the three methods.
- **Zenkel, Wuebker & DeNero 2021**, "Automatic Bilingual Markup Transfer" (Findings of EMNLP 2021)
  [Zenkel 2021]. Exactly our task: place source markup into a target translation already written without
  markup. Exact-position accuracy averaged over six language pairs (Table 1): FastAlign Min-Max 72.3 %,
  FastAlign Inside-Outside 78.1 %, neural aligner Inside-Outside 83.2 %, supervised seq2seq with neural
  aligner 94.7 %. A single bad alignment link can throw a Min-Max span far off (Sec. 4.3).
- **Aligners.** fast_align needs parallel training data [fast_align]. SimAlign needs no parallel data but
  uses multilingual embedding models [SimAlign]. Neither is something a subtitle editor ships by default.

## 6. XLIFF 2.1 inline codes (the standard vocabulary)

From [XLIFF 2.1] Sec. 4.2.3 and 4.7.2:

- `<ph/>` is a standalone code (a line break, an image, a variable). `<pc>…</pc>` is a paired code
  around content. `<sc/>`/`<ec/>` are the start and end of a pair that may not nest well (for example
  overlapping). Extractors must not use `sc`/`ec` for standalone codes.
- Editing hints: `canCopy` (default yes), `canDelete` (default yes), `canReorder` (`yes`, `firstNo`,
  `no`; default yes), `canOverlap`. "Modifiers MUST NOT delete inline codes that have their attribute
  canDelete set to no." A run of `firstNo` then `no` codes is a sequence whose number and order must not
  change, though the whole sequence may move.
- `subType` reserves `xlf:b`, `xlf:i`, `xlf:u` and `xlf:lb` (line break); `equiv` and `disp` give a
  plain-text equivalent and a display form.

In those terms, ASS styling runs are `pc` (or `sc`/`ec` when runs overlap), `\N` is `ph` with
`subType="xlf:lb"`, and line-wide tags are codes with `canReorder="no"` that never need to be shown.

## 7. What HikariSub could take

### 7.1 Classification of ASS content in the Original text

| Class | Content | Handling in the Translated text |
| --- | --- | --- |
| **Line-wide** | `\pos`, `\move`, `\org`, `\fad`, `\fade`, `\clip`, `\iclip`, `\an`, `\a`, `\q`, in any block. | Copied automatically to the leading block of the translation, in their original order. Never shown to the translator. |
| **Leading state** | Every tag in blocks before the first visible character. This includes per-glyph tags such as `\be`, `\blur`, `\bord`, `\fs`, `\c`, `\t(...)`, `\fr`, `\fsc*`. | Copied automatically as the translation's leading block. They apply to the whole visible text, whatever it is. |
| **Trailing** | Blocks after the last visible character. | Copied automatically to the end. They have no visible effect except line-wide tags, which already go to the front. |
| **Whole-line wrapper** | A run that covers all visible text, for example `{\i1}text{\i0}`. | Automatic: the opening tags join the leading block, the closing tags join the trailing block. |
| **Positional, paired (styling runs)** | Per-glyph tags in a block **between** visible characters: `\i \b \u \s`, `\c \1c-\4c`, `\alpha \1a-\4a`, `\fn \fs \fscx \fscy \fsp`, `\fr* \fax \fay`, `\bord \xbord \ybord \shad \xshad \yshad`, `\be \blur`, `\fe`, an inner `\t(...)`, and `\rStyle`. | Each one starts a run that lasts until the property is changed again or reset. Shown as numbered runs that the translator places (7.2). XLIFF `pc`, or `sc`/`ec` when runs overlap. |
| **Positional, standalone** | `\N` (hard break), `\n`, `\h`; a bare `\r` (closes every run); brace comments `{note}`. | `\N`, `\n`, `\h`: not copied. The translator breaks lines for the target language (Shift+Enter gives `\N` in the projection). `\r`: becomes a run boundary. Comments: kept in the Original text only. They annotate the source (see 7.5). |
| **Out of scope** | Karaoke (`\k`, `\K`, `\kf`, `\ko`, `\kt`); drawings (`\p1`+, `\pbo`); malformed braces. | No automatic transfer. Karaoke syllables belong to source syllables and must be re-timed in karaoke mode. Leading line-wide tags are still copied, `\k` tags are dropped, and the Line shows a notice. A Line that is all drawing is not translated: Paste all copies it verbatim. Malformed text falls back to raw editing, as the projection already does. |

Run boundaries come from **state**, not from tag names. A run is a maximal stretch of visible text with
one formatting state. Its key is the set of (property, value) pairs that differ from the leading state.
`text0{\i1}text1{\i0\b1}text2{\b0}text3` therefore has a base run (`text0`, `text3`), run 1 = italic
(`text1`) and run 2 = bold (`text2`). This copes with blocks that close one run and open the next
(`{\i0\b1}`), with overlap, and with `\r`.

### 7.2 Interaction designs for the hard case, ranked

#### A. Rendered runs placed by selection (recommended)

- The **Original field** shows its text with tags hidden (the existing projection). Each inner run is
  rendered in its formatting and carries a small number badge with a light tint. A run strip under the
  field lists the runs as chips: "① *italic* text1", "② **bold** text2". Colour, size and font runs show
  a colour swatch or short label, because their rendering alone can be hard to read.
- The **Translated field** shows plain text, also with tags hidden. Placed runs are **rendered**: an
  italic run is italic, a bold run is bold. A thin underline in the run's tint carries its number.
- **Placing a run.** Select target text and press the run's key (Alt+1 … Alt+9 as in Smartcat, subject
  to the shortcut table), or click its chip. "Place next run" applies the lowest unplaced run to the
  selection, like memoQ F9 or Phrase F8. This is the same gesture as Ctrl+I in a word processor. Indeed,
  Ctrl+I over a selection could simply place the italic run when the Original text has one.
- **Removing or moving a run.** Select the run's text and press its key again, or press the key on a new
  selection: a run is placed once. Ctrl+Z works as for any draft edit.
- **Order is free.** Runs may appear in any order, may be nested or adjacent, and run 2 may come before
  run 1. Reordering is what translation needs (OmegaT and memoQ allow it, section 3).
- **Unplaced runs** stay as hollow chips at the end of the Translated field. The Line gets a "runs not
  placed" mark in the Grid, and a filter finds such Lines. They are never written as empty tag pairs, and
  they never block commit.
- **Trade-offs.** Deterministic, keyboard-only and fast: one keystroke per run, often zero (7.3). It
  matches the convention that CAT tools converged on, and it extends the hidden-tag projection rather
  than adding a new editor. Cost: the translator must notice runs. The chip strip and the Grid mark make
  them hard to miss. Needs rich rendering in the Translated field, which the Line editor's highlighter
  can do.

#### B. Automatic placement with easy correction (an assist on top of A)

- After the translator pauses or commits, HikariSub suggests placements and shows them as dotted
  underlines. Tab or the run key accepts a suggestion, and Escape discards it.
- **Cheap, reliable signals** are enough for many subtitles: a run whose text reappears unchanged in the
  translation (names, numbers, quoted titles, loanwords, songs left untranslated), and a run that covers
  the whole of a sentence or of one `\N` line in the original, when the translation has the same number
  of sentences or lines.
- **Full alignment** (SimAlign-style embeddings, or an MT engine with `tag_handling=xml` used as a
  placer) is possible later. It needs a model or a network service and an API key, and published
  accuracy is 72–95 % (section 5.3). It must remain a suggestion.
- **Trade-offs.** Zero keystrokes when right. When wrong, the translator must spot a mistake in
  formatting they did not apply, which is harder than placing it themselves. Not suitable as the only
  mechanism, and never silent.

#### C. Per-run mini fields (not recommended)

- The Translated area splits into one field per run, in source order: `[text0][i: text1][b: text2][text3]`.
  This is what Aegisub's Translation Assistant effectively does (section 4.2).
- **Trade-offs.** Simple and impossible to get "wrong", and good when word order is unchanged (many
  English→Polish lines). But it forces source order. English→Japanese or English→German verb-final
  sentences cannot put the italic word where the target language needs it. Spell checking and typing
  across fields are awkward, and the translator thinks in fields rather than sentences. At most an
  optional view.

**Most natural and easiest: A, with B's cheap suggestions.** The translator writes the sentence as the
target language wants it, then marks "this word is italic" with the same select-and-press gesture used
in every word processor and CAT tool. Nothing about tags, braces or order needs to be understood.

### 7.3 Default behaviour for the easy cases

With no interaction at all:

1. On showing an untranslated Line, the Translated field is empty and the caret is in it. Leading,
   line-wide, trailing and whole-line-wrapper tags are **already applied**: they are hidden and shown
   only as a small "tags from original" indicator that can expand to show them raw.
2. The Original text keeps all its tags. This is a departure from legacy "Moving tags", which strips them
   (1.2). It would be a named departure for the owner to approve.
3. On commit the Dialogue text is: leading block (leading state plus hoisted line-wide tags) + typed
   text with placed runs + trailing block.
4. A Line whose Original text has no inner runs needs no further action. Most dialogue lines and most
   signs are in this group.
5. The "Moving tags" toggle becomes "Apply tags from original" and defaults to on. Turned off, the
   Translated text is free raw text, as with Moving tags off today.

### 7.4 When the Original text's tags change after translation

The Original text owns the line-wide and leading tags, and the translation follows it.

- **Line-wide and leading tags.** When the Original text's leading state changes (a visual tool drags
  `\pos`, the typesetter changes `\fad`), the translation's leading block is regenerated, if it still
  equals what was generated from the old Original text. If the translator edited it by hand, the Line is
  marked "tags differ from original" and nothing is overwritten.
- **Runs.** Placed runs are matched to the new original's runs by number and property set. A changed
  value (a different `\c` colour) updates in place. A run removed from the original becomes a stale run
  in the translation: its tags are kept and it is marked. A new run appears as an unplaced chip.
- This needs no stored history: it compares the translation's actual state against what the old and new
  original would generate, inside the same edit command (one undo step).

### 7.5 Save and load round-trip

- **Saved form.** Unchanged file format. The original event holds the Original text with its tags
  (TLMode Style, Dialogue or Comment as today). The translation event holds real ASS:
  `{leading}text {\i1}word{\i0} text{trailing}`. Every other renderer and editor shows it correctly.
- **Tag text.** At each run boundary, emit the tags that move from the previous state to the next one.
  Reuse the original's literal spellings (`\c&H0000FF&`, not a re-serialised value). Return to the
  leading state with the original's closing tags where they exist, and otherwise with an empty-argument
  reset, which libass reads as "the Style's value" ([ass_parse.c] 831-835). Never emit `\r` unless the
  original used it, since `\r` would also cancel the leading state.
- **Load.** Project the translation (existing projection) to get the plain text, compute the state of
  each visible span, and match each non-base state to the original's runs. Matches become placed runs.
  States with no match are "own runs", shown with a neutral badge and left untouched. So plain text plus
  placements are always derived from the saved Dialogue. Nothing extra is stored.
- **Legacy files.** A file saved after legacy "Moving tags" has an untagged original and a tagged
  translation. Every translation state is then an "own run", and nothing is lost.
- **Comments in braces.** Kept in the Original text only, and shown read-only in the Original field. A
  translator's own `{note}` typed into the Translated field stays where it is typed, as an own token.

### 7.6 Open questions for the owner

1. **Keep tags in the Original text?** Legacy strips them on commit (1.2). Recommended: keep them. This
   is a legacy departure and needs approval.
2. **Run keys.** Alt+1…9 plus "place next run", or another set? Should Ctrl+B/Ctrl+I/Ctrl+U in the
   Translated field place the matching run when one exists?
3. **Show the original's tags in the Original field** as rendered runs with badges (recommended), or as
   raw text when "Show tags" is on?
4. **Suggestions (design B).** Ship only the cheap verbatim-match suggestions first? Is an online MT
   placer (DeepL `tag_handling=xml`) ever wanted?
5. **Comments.** Should brace comments in the leading block, often typesetter notes, be copied into the
   translation, or kept with the original only (recommended)?
6. **`\N` hint.** When the original has a `\N` and the translation has none, show a passive hint, or
   nothing?
7. **Karaoke and drawing Lines.** Confirm both are out of scope: drawing Lines copied verbatim on demand,
   karaoke Lines get leading tags without `\k` and a notice.
8. **Unplaced runs.** Grid mark only (recommended), or should they also set the Line's Unconfirmed flag?
   Unconfirmed is defined as an independent review flag, so a separate mark seems right.

## 8. Links

- [Aegisub tags]: https://aegisub.org/docs/latest/ass_tags/
- [ass_parse.c]: https://github.com/libass/libass/blob/f61db567e6593df3470e91594bcd4ad2d0473aff/libass/ass_parse.c
- [ass_render.c]: https://github.com/libass/libass/blob/f61db567e6593df3470e91594bcd4ad2d0473aff/libass/ass_render.c
- [ass_dialogue.cpp]: https://github.com/Aegisub/Aegisub/blob/6f546951b4f004da16ce19ba638bf3eedefb9f31/src/ass_dialogue.cpp#L199-L267
- [dialog_translation.cpp]: https://github.com/Aegisub/Aegisub/blob/6f546951b4f004da16ce19ba638bf3eedefb9f31/src/dialog_translation.cpp
- [dialog_translation.cpp fork]: https://github.com/TypesettingTools/Aegisub/blob/5bc77e4f61543bfa56132f99c87535ad6319d02e/src/dialog_translation.cpp
- [Formatting.cs]: https://github.com/SubtitleEdit/subtitleedit/blob/e88d1677b0cd97afff1e4207da4afdbdebb475c1/src/libuilogic/Translate/Formatting.cs
- [Formatting.cs SE4]: https://github.com/SubtitleEdit/subtitleedit/blob/96e6d302659e94e77564b98805d37e52c80d6501/src/libse/Translate/Formatting.cs
- [DeepLTranslate.cs]: https://github.com/SubtitleEdit/subtitleedit/blob/e88d1677b0cd97afff1e4207da4afdbdebb475c1/src/libuilogic/AutoTranslate/DeepLTranslate.cs
- [GoogleTranslateV2.cs]: https://github.com/SubtitleEdit/subtitleedit/blob/e88d1677b0cd97afff1e4207da4afdbdebb475c1/src/libuilogic/AutoTranslate/GoogleTranslateV2.cs
- [AdvancedSubStationAlpha.cs]: https://github.com/SubtitleEdit/subtitleedit/blob/e88d1677b0cd97afff1e4207da4afdbdebb475c1/src/libse/SubtitleFormats/AdvancedSubStationAlpha.cs
- [AutoTranslateWindow.cs]: https://github.com/SubtitleEdit/subtitleedit/blob/e88d1677b0cd97afff1e4207da4afdbdebb475c1/src/ui/Features/Translate/AutoTranslateWindow.cs
- [memoQ tags]: https://docs.memoq.com/current/en/Concepts/concepts-tags.html
- [memoQ grid]: https://docs.memoq.com/current/en/Workspace/the-grid.html
- [memoQ edit ribbon]: https://docs.memoq.com/current/en/Workspace/ribbons-edit.html
- [memoQ shortcuts]: https://docs.memoq.com/current/en/Workspace/keyboard-shortcuts.html
- [memoQ QA settings]: https://docs.memoq.com/current/en/Workspace/edit-qa-settings.html
- [memoQ QA warnings]: https://docs.memoq.com/current/en/Concepts/concepts-quality-assurance-qa-warnings.html
- [memoQ tag strictness]: https://docs.memoq.com/current/en/Concepts/concepts-tag-strictness-and-match-rates.html
- [memoQ Custom.MT]: https://docs.memoq.com/current/en/Workspace/custommt-plugin-settings.html
- [Trados show tags]: https://docs.rws.com/en-US/trados-studio-2022-980998/showing-and-hiding-formatting-tags-354291
- [Trados copy tags]: https://docs.rws.com/en-US/trados-studio-2022-980998/copying-and-pasting-tags-354385
- [Trados QuickInsert]: https://docs.rws.com/en-US/trados-studio-2022-980998/inserting-tags-using-quickinsert-521194
- [Trados auto-insert]: https://docs.rws.com/813470/853391/trados-studio-2021-sr2/placing-tags-using--auto-insert-tags-/procedure
- [Trados tag verifier]: https://docs.rws.com/en-US/sdl-trados-studio-2021-sr1-813470/specifying-settings-for-the-tag-verifier-521441
- [OmegaT quick start]: https://omegat.sourceforge.io/manual-standard/en/chapter.instant.start.guide.html
- [OmegaT menus]: https://omegat.sourceforge.io/manual-standard/en/chapter.menus.html
- [OmegaT preferences]: https://omegat.sourceforge.io/manual-latest/en/chapter.dialogs.preferences.html
- [Phrase tags]: https://support.phrase.com/hc/en-us/articles/5709695024412-Tags-TMS
- [Phrase shortcuts]: https://support.phrase.com/hc/en-us/articles/5709700557468-Editor-Keyboard-Shortcuts-TMS
- [Phrase QA]: https://support.phrase.com/hc/en-us/articles/5709703799324-Quality-Assurance-QA-TMS
- [Smartcat tags]: https://help.smartcat.com/tags/
- [Smartcat hotkeys]: https://help.smartcat.com/115002172551-editor-hotkeys/
- [Smartcat QA]: https://help.smartcat.com/quality-assurance/
- [Crowdin editor]: https://support.crowdin.com/online-editor/
- [Crowdin QA]: https://support.crowdin.com/project-settings/qa-checks/
- [Crowdin custom MT]: https://support.crowdin.com/custom-mt
- [DeepL translate]: https://developers.deepl.com/api-reference/translate/request-translation
- [DeepL tag handling v2]: https://developers.deepl.com/docs/xml-and-html-handling/tag-handling-v2
- [DeepL HTML]: https://developers.deepl.com/docs/xml-and-html-handling/html
- [Google advanced]: https://docs.cloud.google.com/translate/docs/advanced/translate-text-advance
- [Google v3 REST]: https://docs.cloud.google.com/translate/docs/reference/rest/v3/projects/translateText
- [Google troubleshooting]: https://docs.cloud.google.com/translate/troubleshooting
- [Hanneman 2020]: https://aclanthology.org/2020.wmt-1.138/
- [Zenkel 2021]: https://aclanthology.org/2021.findings-emnlp.299/
- [fast_align]: https://aclanthology.org/N13-1073/
- [SimAlign]: https://aclanthology.org/2020.findings-emnlp.147/
- [XLIFF 2.1]: https://docs.oasis-open.org/xliff/xliff-core/v2.1/os/xliff-core-v2.1-os.html
