# Legacy observations: A5 karaoke model (local probe, 2026-10-05)

Captured by the `legacy_karaoke_capture` probe ([`karaoke_capture.cpp`](../../../../tools/legacy-capture/karaoke_capture.cpp), target in [`tools/legacy-capture/CMakeLists.txt`](../../../../tools/legacy-capture/CMakeLists.txt)) on the cases in [`inputs/karaoke-cases.txt`](../../../../tools/legacy-capture/inputs/karaoke-cases.txt). The probe compiles the legacy `HikariSub/KaraokeSplitting.cpp` and `KaraokeSplitting.h` at `20d647c4` unchanged, with the legacy Linux compatibility header (`platform.h`, whose `iswctype(ch, _SPACE | _PUNCT)` is `iswspace || iswpunct`), against stand-ins for the GUI headers it includes ([`tools/legacy-capture/karaoke/`](../../../../tools/legacy-capture/karaoke)): the Line (Text, TextTl, Start, End), the AUDIO_MERGE_EVERY_N_WITH_SYLLABLE option, and a display whose GetXAtMS is legacy's arithmetic over the rewrite's test view (48 kHz at zoom 50: 1440 samples, 30 ms a column, from sample 0) and whose label font measures 7 pixels a character (GetTextExtentPixel's +4 for a leading or trailing space is legacy's own code in the stand-in).

Built locally with GCC 16.2.1 and the distribution's wxWidgets 3.2.11 (wxGTK, `wx-config --cxxflags base,core`); the probe never sets the C library's locale, as the legacy app with its English interface does not (see [local-f1f3-20261004](../local-f1f3-20261004/README.md)). Run:

```
cmake -S tools/legacy-capture -B <dir> -DLEGACY_SOURCE=<checkout at 20d647c4> -DWX_CONFIG=$(which wx-config)
cmake --build <dir> --target legacy_karaoke_capture
<dir>/legacy_karaoke_capture < tools/legacy-capture/inputs/karaoke-cases.txt > observations.jsonl
```

`observations.jsonl` holds one line per case: after Split and after each operation, the syllables, tags, times (each syllable's end, ms), stripped texts and GetText (left out where legacy would read past its times), and each query's answer (GetSylAtX, CheckIfOver, GetLetterAtX: found, syllable, letter). sha256 `fd8260d3…307bc`.

## Compared with the rewrite

`AudioKaraokeCapture.ReplaysTheLegacyObservations` (`hikari_application_audio_karaoke_tests`) replays every case through `AudioKaraoke` and compares every state and answer. All are the same except the proposed departure the capture confirms:

| Case | Legacy | Rewrite |
| --- | --- | --- |
| k-unclosed-end (`{\k20}ka{\k30`, 0-1000) | two syllables (`{}ka`, `{0`) with one time (200): GetText, the drawing and the mouse read past the times | the missing time is the Line's end: times 200, 1000 (A5-kara-unclosed, proposed) |

The capture also confirms these legacy quirks, kept:

- auto-unclosed (`ab{c`, automatic): one syllable `a`; `b{c` is lost from the text Commit writes (the final split never happens inside an unclosed block). Kept; proposed as A5-auto-unclosed.
- kf-ko-K `splitsyl 1 1` on `{}o`: a split after a syllable's last letter splits at the raw position, so `{}o` becomes `{` and `}o` and GetText writes `{\K25}{{\k25}}o`. Kept; proposed as A5-split-last-letter.
- stripped-unclosed (`{}ab{c`): the stripped text is `ab` followed by the whole syllable again (`ab{}ab{c`), legacy's "{ without }" branch.
- k-with-tags: a syllable's tags after its \k stay in the same block (`{\k20\fs20}`); tags before it move after it (`{\fs30\k30}` comes back as `{\k30\fs30}`); a Join removes every `{}` in the joined syllable.
- k-leading-text: text before the first \k joins the first syllable (`ab{}cd`).
- k-no-digit: `\k` without a digit is not karaoke (the regex wants a digit): the text is split at spaces and the old tag kept after the new one (`{\k50\k}ka `).
- auto modes: the vowel and n rules, AUDIO_MERGE_EVERY_N_WITH_SYLLABLE, punctuation (a syllable keeps the punctuation and the space after it; `"` stays with the syllable before it), `\N`/`\h`/`\n` kept with the syllable before; each syllable but the last ends at ZEROIT of the running sum of duration / count (float division, truncated), the last at the Line's end.
