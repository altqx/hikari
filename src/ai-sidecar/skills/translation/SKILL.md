---
name: translation
description: Translating subtitles from one language to another, or checking a translation. Use it to translate Lines, to keep names, terms and the way each character speaks consistent, to keep override tags and line breaks in place, and to keep each Line short enough to read in its time.
---

# Translation

A subtitle translation carries the meaning, tone and register of each Line in words a viewer can read before the Line ends.

## Steps

1. **Read before you translate.** Read the Lines around the ones you translate, with `lines_list`, so you know who speaks to whom and what came before. The Actor field often names the speaker.
2. **Gather the terms.** Note the names, places, titles and repeated phrases, and how earlier Lines translated them. Ask the user when a choice matters and the earlier Lines don't settle it, such as whether to keep honorifics.
3. **Translate in passes** of a scene or about 50 Lines, each as one `document_apply` named for the user, such as "Translated Lines 1–48".
   - In Translation mode each Line has `text`, the original, and `translation`; write only `translation`.
   - Otherwise replace `text`, unless the user asks to keep the original as a Comment Line.
4. **Check the pass.** Read the Lines again for terms used two ways, Lines that are too long, and tags out of place.

## Rules

- Keep every `{…}` override block, and put it before the words it belongs to in the translation. Keep `\N` breaks, moving them to a natural place in the new text.
- Keep the text short: about 17 characters a second, and two rows of about 42 characters at most. Shorten wording before you split a Line or change its times.
- Never change times, Styles, Actors or Effects, unless the user asks.
- Leave Comment Lines, karaoke template Lines and Lines in another language than the source as they are.
- Translate signs and songs only when the user asks; they often need typesetting or timing too.
- Keep each character's voice: formal or casual, short or long-winded, a dialect or a verbal habit.
- Translate what is meant, not word for word: idioms, jokes and wordplay get an equivalent that works in the target language.
- Don't add notes or explanations to the Lines. Tell the user about a hard choice in your answer instead.
