# Karaoke layout

The layout for timing syllables and running karaoke templates, in Quiet ([#1205](https://github.com/altqx/hikari/issues/1205)). The arrangement, its mode and the folding of generated Lines are [workspaces.md](workspaces.md)'s (KP1); the Template editor's behaviour is [karaoke-templater.md](../karaoke-templater.md#the-template-editor)'s (KE2). This document says how they look. Its Discord mode is Karaoke ([Discord presence settings](discord-presence.md#modes)).

The owner chose this design from the mode gate board on 2026-11-17 ("Lgtm", Q725–Q729; see [Decisions](#decisions)). The app doesn't change until the code card lands after the beta.4 tag.

## The window

| Region | Size | Holds |
| --- | --- | --- |
| Video, upper left | Half the width, about 46 % of the height | As in Editing |
| Template editor, upper right | The other half | Tabbed with the Line editor; see [The Template editor](#the-template-editor) |
| Audio | The full width | **Line \| Karaoke** in its header, then Split syllables and Tap to time; [the syllable bar](#the-syllable-bar) over the waveform; [the toolbar](editing-layout.md#the-audio-toolbar) |
| Grid | 200 high, the full width | Folded generated Lines; see [Folded rows](#folded-rows) |

## The Template editor

- **List and editor** (Q725). At the left, 220 wide, a list of the file's template and code Lines in Grid order: the Effect in semibold ("template syl", "code once"), under it the Style and the Line's own note in `muted` monospace. The chosen one has the `select` fill. At the right, its Effect and Style choices, Comment, and the text in a monospace field with tags in `accent` and comments in `muted`.
- **Apply templates** (Q726) is the primary button in the panel's header, before the ⋯ menu. It replaces the Apply at the bottom.
- **No templates.** The empty state: "No templates in this file", **New template** and **From a preset…** (quiet), and in `muted` "Templates turn karaoke lines into effect lines when you apply them."
- **Applying.** A 2-unit progress line under the header and "Applying templates · 1,204 lines so far" with **Stop** (quiet).

## The syllable bar

Q727. A 26-unit row over the waveform, one box per syllable, each as wide as its time. Boxes are `raised`, radius 3, the syllable in 12.5-unit text. Syllables before the playhead are `muted`; the current one has the `select` fill, a 1-unit `accent` edge and semibold text. A click selects a syllable as a click in the waveform does today.

## Folded rows

Q728. A folded run reads "▸ 52 generated lines" in semibold in the Text column, with the Line range in the # column ("47–98"), between dashed `line` rules. The # column is wide enough for a range of three-digit numbers. Unfolding and selection stay as [workspaces.md](workspaces.md) says.

## Template errors

Q729. When applying stops on an error:

- a row under the Template editor's header: the `notice-warning` glyph in `danger` and "Templates not applied: code once, line 3: attempt to call a nil value (global 'colr').", then **Show the line** and **Details** (the Log entry), both quiet;
- the failing template or code Line is chosen in the list, its Effect in `danger`;
- the failing line of its code is tinted `danger` at 18 %.

Nothing is generated from a run that failed.

## Discord

Presence shows **Karaoke** (`karaoke`), unchanged from #599.

## Decisions

The owner said "Lgtm" to the mode gate board on 2026-11-17 ([#1209](https://github.com/altqx/hikari/issues/1209#issuecomment-6940698713)):

| Q | Decision |
| --- | --- |
| Q725 | The Template editor lists the file's template and code Lines beside the editor of the chosen one |
| Q726 | Apply templates is the primary button in the Template editor's header |
| Q727 | A row of syllable boxes over the waveform: done muted, the current one selected with an accent edge |
| Q728 | Folded runs read "▸ N generated lines" with the Line range in the # column, between dashed rules |
| Q729 | A template error shows as one line in the Template editor with Show the line and Details; the failing code line is tinted danger |
