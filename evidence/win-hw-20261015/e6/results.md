# E6 #188: tag popup with a real IME on Windows (2026-10-15)

Build: `hikarisub-qt-windows-payload` from run 38686574527 (evidence head `3e2b145d3`); setup in [../setup/host.md](../setup/host.md).
IMEs: Microsoft Korean IME (TIP `0412:{A028AE76…}`) and Microsoft Japanese IME (TIP `0411:{03B5835F…}`), as installed by the owner.
NVDA 2026.2 portable (OneCore voice), restarted with `--log-level=12`; speech excerpt: [nvda-speech-17-24_17-35.txt](nvda-speech-17-24_17-35.txt).

How the keys reached the app: the keystrokes were real key events (SendInput virtual keys, the same path as a keyboard), so they went
through the Windows text services and the real Microsoft IMEs; no QInputMethodEvent was synthesised. The owner also typed the Korean
sequence on the physical keyboard (see "Owner run"). Each step has a capture of the Line editor (physical pixels) and a
`NN-state.json` with the keyboard layout, the IME's open/conversion state (IMM32) and the UI Automation value of the focused field.

The default Line editor hides tags; typing `\` there is refused with "Braces and backslashes are ASS syntax: edit them with tags shown."
The tests below ran in the raw view (Hide tags off), as the Windows gate's `tags` step does.

## Korean (Microsoft IME, Hangul mode)

| # | Item | Result | Evidence |
| --- | --- | --- | --- |
| K1 | `{\` opens the tag list (IME on, Hangul mode) | **observed**: list opens at the caret (alpha, be, blur…); focus stays in "Line text" | [01-popup-open-raw-view.png](01-popup-open-raw-view.png), [01-state.json](01-state.json) |
| K2 | preedit shows inline | **observed**: ㅎ, 하, 한 drawn inline after `{\` while composing | [02-preedit-h.png](02-preedit-h.png), [03-preedit-ha.png](03-preedit-ha.png), [04-preedit-han.png](04-preedit-han.png) |
| K3 | the list does not steal or commit the composition early | **observed**: list stays open through ㅎ→하→한; the field's UIA value stays `{\` (nothing committed) | 02–04 `*-state.json` |
| K4 | committing puts 한 in the text | **observed**: Space commits, value `{\한 ` | [18-after-space-commit.png](18-after-space-commit.png), [18-state.json](18-state.json) |
| K5 | the list after the commit | **observed**: it closes (no tag name starts with 한); it does not reopen on Backspace to `{\` | 18, [05-down-during-composition.png](05-down-during-composition.png), [06-after-backspace.png](06-after-backspace.png) |
| K6 | Down during composition | **observed**: the IME commits 한 (value `{\한`), the list closes; the caret stays after 한 | 05 |
| K7 | Enter during composition, list open | **observed, needs a decision**: the IME commits 한 and the same Enter reaches the app: the Line is committed as `{\한` and the editor advances to a new Line 2. The list did not insert its selection ("alpha"). This is the Microsoft Korean IME's own behaviour (it commits and passes Enter on); compare J4 | [16-before-enter.png](16-before-enter.png), [17-after-enter.png](17-after-enter.png), 16/17 state |
| K8 | Esc during composition, list open | **observed, suspected defect**: one Esc removes the preedit, closes the list **and reverts the whole Line draft** (`{\` + 한 → empty, the Line's stored text). Without a composition, Esc with the list open only closes the list and keeps `{\` (K9). The IME does not consume Esc; the app's draft revert takes it | [07-recompose-han.png](07-recompose-han.png), [08-esc-during-composition.png](08-esc-during-composition.png), 07/08 state |
| K9 | control: Esc with the list open, no composition | **observed**: closes the list only, `{\` kept | [09-popup-no-composition.png](09-popup-no-composition.png), [10-esc-no-composition.png](10-esc-no-composition.png) |
| K10 | control: Esc during composition without the list; Esc on a plain draft | **observed**: both revert the draft (`12`+한 → empty; `12` → empty). So Esc = revert draft whether or not a Korean composition is active | [11-…](11-compose-no-popup.png) to [15-esc-plain-draft.png](15-esc-plain-draft.png) |

## Japanese (Microsoft IME, hiragana, romaji input)

The list was opened with the IME off (direct input), then the IME turned on with Alt+\` (typing `{` with the IME on would compose a full-width `｛`).

| # | Item | Result | Evidence |
| --- | --- | --- | --- |
| J1 | preedit inline under the open list | **observed**: ｋ → か → かん → かんｊ → かんじ inline; the IME's prediction window ("Press Tab to select") opens over the list; value stays `{\` | [22-ja-k.png](22-ja-k.png) to [26-ja-kanji.png](26-ja-kanji.png) |
| J2 | early commit | **observed once, not reproduced**: in the first Japanese run (typed fast right after switching) the composition was committed as-is (`かんｊ`, the `i` lost) and the list was gone. NVDA logged focus moving to the "Windows Input Experience" window at that instant (the IME's first prediction window), so the commit followed a focus change to the IME's own window. Three later runs (slow, fast, and toggle-then-fast) kept the preedit | [21-ja-preedit-kanji.png](21-ja-preedit-kanji.png), [21-state.json](21-state.json), [35-ja-fast-kanji.png](35-ja-fast-kanji.png), [36-ja-toggle-then-fast.png](36-ja-toggle-then-fast.png), NVDA 17:30:43 |
| J3 | conversion | **observed**: Space converts to 感じ inline; the tag list is still open under it | [27-ja-converted.png](27-ja-converted.png) |
| J4 | Enter during composition (candidate confirmation stays with the IME) | **observed**: Enter confirms 感じ (value `{\感じ`); the Line is **not** committed and the editor does not advance; the list closes (no match) | [28-ja-enter-confirm.png](28-ja-enter-confirm.png), [28-state.json](28-state.json) |
| J5 | Esc during composition | **observed**: the IME consumes it and cancels か; the draft `{\感じ\` is kept and the list stays open; a second Esc closes the list | [29-…](29-ja-ka-before-esc.png), [30-ja-esc.png](30-ja-esc.png), [31-ja-second-esc.png](31-ja-second-esc.png) |
| J6 | Down during composition | **observed**: the IME consumes it (selects its candidate かな); after cancelling, the tag list's selection is still "alpha" | [32-ja-down-during-composition.png](32-ja-down-during-composition.png), [33-…](33-ja-esc-after-down.png), [34-ja-popup-selection-after-ime-down.png](34-ja-popup-selection-after-ime-down.png) |

## NVDA with the IME active

| # | Item | Result | Evidence |
| --- | --- | --- | --- |
| N1 | the list announced with the Korean IME on (Hangul mode) | **observed**: "\alpha, Transparency, 1 of 20" each time `{\` opened it (17:25:08, 17:25:35, 17:27:00, 17:27:27) | NVDA excerpt |
| N2 | Down in the list with the Japanese IME on, not composing | **observed**: "\be, Edge blur, 2 of 20"; the selection moved | [37-ja-ime-on-down-in-popup.png](37-ja-ime-on-down-in-popup.png), NVDA 17:34:38 |
| N3 | composition spoken | observed: NVDA reads the preedit (ㅎ, 하, 한; ｋ, か…) and the IME's prediction window | NVDA excerpt |

## Owner run (physical keyboard, Korean)

The owner typed `{` `\`, ㅎㅏㄴ, waited, then Space on Line 3 and reported the expected result (inline 한 under the open list, Space commits, list closes).
**End state captured**: Line 3 text `{\한 ` with the Korean IME active at 17:45:11 ([owner-run/end-state-17-45-11.png](owner-run/end-state-17-45-11.png), [json](owner-run/end-state-17-45-11.json)); the owner also sent a screenshot of `{\한` in Line 3.
**Composition phase: owner-reported only**; my watcher started after the owner had finished, so no capture shows the preedit from the physical run. The two earlier attempts' captures showed no input in HikariSub and are not used.

## Still missing / for the card

- K7 (Korean Enter commits the composition and the Line) and K8 (Korean Esc during composition reverts the whole draft) need a decision: accept as the Korean IME's native key flow, or make the editor hold Enter/Esc that arrive right after a commit. A "no" reopens E28-ime through a decision ticket.
- J2 is one unreproduced early commit tied to the IME's own window taking focus; recorded, not a pass.
- The ledger note on completion fixtures (traced through legacy's source) is unchanged by this session.
