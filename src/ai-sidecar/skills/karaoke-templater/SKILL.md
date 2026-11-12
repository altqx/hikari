---
name: karaoke-templater
description: Writing, explaining and fixing karaoke templates for the karaoke templater, and timing the karaoke Lines they run on. Use it for template and code Lines (Effect "template syl", "code once" and the like), their modifiers, $variables, !expressions!, for applying templates to timed karaoke Lines with \k tags, and for splitting and timing syllables.
---

# Karaoke templater

The karaoke templater turns timed karaoke Lines into effect Lines. Template and code Lines are Comment Lines whose Effect field starts with `template` or `code`. Each karaoke Line, a Dialogue Line timed with `\k` tags whose Effect is empty or holds `karaoke`, is run through every template of its Style. The [language reference](references/language.md) has the classes, modifiers, variables and functions.

## Steps

1. **Read the karaoke.** Find the karaoke Lines and their Styles with `lines_find`. Run `karaoke_check`; when Lines aren't timed yet, time them first (below).
2. **Write the template** as a Comment Line with the karaoke Lines' Style, using `document_apply`. Start from the simplest template that does what the user wants, such as one `template syl` Line, and add `code` Lines only for values used more than once.
3. **Preview it.** `karaoke_template_preview` on one karaoke Line answers the Lines the templater would generate from it, without changing anything. A `templater_failed` error carries the templater's message, with the `Template Line: N` or `Code Line: N` to fix.
4. **Apply it.** `karaoke_apply_template` with the Document's revision runs the templater on every karaoke Line, or the Lines you name, as one undo step that waits in the user's review.
5. **Check the result.** Read a few generated Lines and look at a frame in the middle of a syllable with `video_frame`. Fix the template, not the generated Lines, and apply again.

## Timing

1. **Split.** `karaoke_split` answers each Line split into syllables with the user's language rules, the Line's time shared by character; `bySyllable: false` splits at spaces. Write the texts with `document_apply`. Lines that already have `\k` tags keep them.
2. **Time.** `karaoke_onsets` lists where sung notes start in the audio. `karaoke_snap` moves each syllable boundary to the nearest onset; `karaoke_time` sets each syllable's end in ms yourself. Both answer texts to write with `document_apply`.
3. **Align, only when asked.** `karaoke_align` runs WhisperX or the Montreal Forced Aligner, if the user installed one, on the Lines' audio and answers each word's start and end. The user approves every run. A `missing` error means the program isn't installed: tell the user, don't retry.
4. **Kanji.** With a timed romaji Line, `karaoke_kanji` copies its syllables onto the kanji Line; check any answer whose `clean` is false. `karaoke_furigana` suggests readings for a Line's kanji when a dictionary is installed.
5. Run `karaoke_check` again before the templates.

## Rules

- Applying again replaces the Lines the last run generated, so change the template, not the generated Lines.
- Templates run Lua. Write only what the effect needs, and never use `io`, `os` or `require` in them.
- `!…!` expressions and `code` Lines see the code environment: `line`, `syl`, `orgline`, `meta`, `string`, `math`, and the functions in the reference.
- Keep `\pos` in template text, built from the syllable's position variables, so each syllable sits where the karaoke Line put it.
- A template's effect times are relative: `\t` and `\move` times count from the generated Line's start, which `retime` moves.
- Name each edit for the user, such as "Added a syllable glow template".
