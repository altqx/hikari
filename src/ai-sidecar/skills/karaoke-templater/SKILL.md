---
name: karaoke-templater
description: Writing, explaining and fixing karaoke templates for the karaoke templater. Use it for template and code Lines (Effect "template syl", "code once" and the like), their modifiers, $variables, !expressions!, and for applying templates to timed karaoke Lines with \k tags.
---

# Karaoke templater

The karaoke templater turns timed karaoke Lines into effect Lines. Template and code Lines are Comment Lines whose Effect field starts with `template` or `code`. Each karaoke Line, a Dialogue Line timed with `\k` tags whose Effect is empty or holds `karaoke`, is run through every template of its Style. The [language reference](references/language.md) has the classes, modifiers, variables and functions.

## Steps

1. **Read the karaoke.** Find the karaoke Lines and their Styles with `lines_find`, and check that their syllables are timed with `\k`, `\kf` or `\ko`.
2. **Write the template** as a Comment Line with the karaoke Lines' Style. Start from the simplest template that does what the user wants, such as one `template syl` Line, and add `code` Lines only for values used more than once.
3. **Have it applied.** Ask the user to run "Apply karaoke template" from the Automation menu, since no tool runs Automation macros yet.
4. **Check the result.** Read a few generated Lines and look at a frame in the middle of a syllable. The templater logs each problem with a `Template Line: N` or `Code Line: N` note; ask the user for the log when the result is wrong, fix that Line and have it applied again.

## Rules

- Applying again replaces the Lines the last run generated, so change the template, not the generated Lines.
- Templates run Lua. Write only what the effect needs, and never use `io`, `os` or `require` in them.
- `!…!` expressions and `code` Lines see the code environment: `line`, `syl`, `orgline`, `meta`, `string`, `math`, and the functions in the reference.
- Keep `\pos` in template text, built from the syllable's position variables, so each syllable sits where the karaoke Line put it.
- A template's effect times are relative: `\t` and `\move` times count from the generated Line's start, which `retime` moves.
- Name each edit for the user, such as "Added a syllable glow template".
