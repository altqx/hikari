#pragma once

// The audio box's karaoke mode (A5; legacy HikariSub/KaraokeSplitting.cpp and
// AudioDisplay's karaoke parts at 20d647c4). Qt-free: the syllable model
// (Karaoke::Split, GetText, Join, SplitSyl), where its boundaries, syllables
// and letters are on the display (CheckIfOver, GetSylAtX, GetLetterAtX), the
// times the play commands and MakeDialogueVisible read (GetSylTimes,
// GetSylVisibleTimes), and the shapes the display draws for it. Texts are
// UTF-16, as legacy's wxString held them on Windows (the normative build), so
// every position counts UTF-16 units as legacy's did.
//
// Legacy quirks kept: see audio_karaoke.cpp and the A5 coverage row.
//
// KA1 (#276): Auto split by language (karaoke_split.h); new syllables take
// time by character; letters are graphemes; a syllable splits at a time
// (the waveform cursor) too; a Line's \k text comes back as it was read
// but for the times that changed; moving the start keeps the syllables at
// their audio times; long syllables switch between \k and \kf in one step.

#include "hikari/application/audio_display.h"
#include "hikari/application/karaoke_split.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

// Legacy iswctype(ch, _SPACE) and iswctype(ch, _SPACE | _PUNCT) as the
// Windows CRT answers them (GetStringTypeW's C1_SPACE and C1_PUNCT for
// characters past Latin-1), and the lower case Split compares (legacy
// wxString::Lower; U1-unicode-case: every letter by Unicode, one UTF-16 unit
// to one). The default answers ASCII only; the UI gives Unicode's space,
// punctuation and symbol categories and its lower case. `symbol` (Auto
// split's, KA1-auto-split-language): which of those are symbols (ASCII's $ +
// < = > ^ ` | ~, Unicode's symbol categories), read as digits are.
struct KaraokeCharClass {
    std::function<bool(char16_t)> space;
    std::function<bool(char32_t)> punct; // a code point (Auto split reads past the BMP)
    std::function<bool(char32_t)> symbol;
    std::function<char16_t(char16_t)> lower;
    static KaraokeCharClass ascii();
};

// D3DX's DT_CALCRECT width of a text in the label font (legacy verdana11),
// which leaves out leading and trailing spaces.
using KaraokeMeasure = std::function<int(std::u16string_view)>;
// Legacy AudioDisplay::GetTextExtentPixel: the measured width, plus 4 for a
// leading space and 4 for a trailing one.
int legacyTextExtent(const KaraokeMeasure &measure, std::u16string_view text);

// Legacy wxAtoi (_wtoi): leading white space, a sign and digits; 0 without.
int legacyAtoi(std::u16string_view text);

// Legacy Karaoke over one Line, with the display state legacy kept beside it
// (AudioDisplay::currentSyllable, syllableHover, currentCharacter).
class AudioKaraoke {
public:
    // The Line as Split reads it (legacy AudioDisplay::dialogue): its text,
    // or its translation when it has one, and its own times.
    struct Line {
        std::u16string text;
        int startMs = 0, endMs = 0;
    };

    // Legacy Split: with \k tags in an override block (the regex
    // {([^}]*)\\kf?o?[0-9]([^}]*)} on the lower-cased text) the tags' times
    // (\kt is no syllable, KA1-kt-tag); otherwise split automatically
    // (AUDIO_KARAOKE_SPLIT_MODE, `autoSplit`: KA1-auto-split-language,
    // karaokeAutoSplit with `rules`) or at spaces and \N \h \n, the Line's
    // duration shared out by character (KA1-time-by-character).
    void split(const Line &line, bool autoSplit, const KaraokeSplitRules &rules,
               const KaraokeCharClass &classes = KaraokeCharClass::ascii());
    // split() of a text with \k tags, read as tagged whether or not this
    // part of it has one (karaokeKeepSyllables reads a Line in pieces).
    // `carried`: the tag letters read past the previous piece's syllables
    // (legacy reads a "k" in a tag's number as another tag), taken first;
    // this piece's own are left there.
    void splitTags(const Line &line, std::vector<std::u16string> *carried = nullptr);
    void clear();
    // KA2-tap-to-time: the syllables a tap made (texts, tags and each one's
    // end in ms); the first one is current.
    void assign(std::vector<std::u16string> syllables, std::vector<std::u16string> tags, std::vector<int> times);
    // Legacy GetText: each syllable with its \k tag (the time from the
    // previous syllable's end, or from `curStartMs`, in centiseconds).
    // KA1-k-round-trip: each tag goes back where Split read it (a tag before
    // it in its block stays before it, a block of its own stays its own),
    // with the number as it was written while its time is the same.
    std::u16string text(int curStartMs) const;
    // Legacy Join: syllable `i` takes the next one's text (every "{}" in the
    // result removed), its end and keeps its own tag. False (nothing done)
    // for the last syllable (A5-join-last). KA8-join-multi-highlight: a #
    // syllable joined with any other loses its #; two # syllables give one.
    bool join(int i);
    // Legacy SplitSyl: syllable `i` split before its `letters`th visible
    // character, the second part a \k; KA1-time-by-character: the new
    // boundary divides the time by the parts' characters (ZEROIT).
    bool splitSyllable(int i, int letters, int curStartMs);
    // KA1: syllable `i` split at `ms` (the waveform cursor), its text at the
    // letter that share of its characters gives (all of it to one part at
    // either end, which leaves the other empty). False when `ms` is not
    // inside it.
    bool splitAt(int i, int ms, int curStartMs);
    // Legacy GetLetters (A5-split-last-letter: after the last letter the split
    // is right after it, not at the raw position) and GetTextStripped.
    // KA1-grapheme-letters: a letter is a grapheme, never half of one.
    std::pair<std::u16string, std::u16string> letters(int i, int letters) const;
    std::u16string stripped(int i) const;
    // KA1-lead-in-keeps-syllables: the Line's start moved from `oldStartMs`
    // to `newStartMs` with the syllables staying at their audio times. An
    // earlier start puts the time before the first syllable into an empty
    // lead syllable (a new one, or the one there); a later one cuts the
    // syllables it passes to nothing and drops a lead syllable it empties.
    void moveStart(int oldStartMs, int newStartMs);
    // KA1-kf-switch: every \k syllable at least `thresholdMs` long becomes
    // \kf; with none left as \k, those long ones go back to \k. False when
    // no syllable is that long.
    bool switchLongToKf(int thresholdMs, int curStartMs);

    // Legacy GetSylTimes and GetSylVisibleTimes.
    std::pair<int, int> syllableTimes(int i, int curStartMs) const;
    std::pair<int, int> visibleTimes(int i, int curStartMs, int curEndMs) const;
    // Legacy CheckIfOver: the boundary within 6 columns of x (-1: none).
    int boundaryAt(int x, const AudioView &view) const;
    // Legacy GetSylAtX: the syllable under x (2 columns of slack; -1: none).
    int syllableAt(int x, const AudioView &view, int curStartMs) const;
    // Legacy GetLetterAtX: the syllable under x and the letter (grapheme) of
    // its stripped text before which x falls (their count past the last one).
    std::optional<std::pair<int, int>> letterAt(int x, const AudioView &view, int curStartMs,
                                                const KaraokeMeasure &measure) const;

    int count() const { return static_cast<int>(m_syls.size()); }
    const std::vector<std::u16string> &syllables() const { return m_syls; }
    const std::vector<std::u16string> &tags() const { return m_tags; }
    const std::vector<int> &times() const { return m_times; }
    std::vector<int> &times() { return m_times; }

    // Legacy currentSyllable, syllableHover and currentCharacter.
    int current = 0;
    int hover = -1;
    int character = -1;

private:
    // KA1-k-round-trip: where a syllable's tag goes back. `at` counts from
    // the syllable's text (past its end: into the next one's, for two tags
    // in one block); `number` is the time as written and `cs` its value,
    // kept while the time is the same (none for a new syllable).
    struct Place {
        std::size_t at = 0;
        std::u16string number;
        std::optional<int> cs;
    };
    static Place newPlace(const std::u16string &syl);
    void insertSyllable(int at, std::u16string text, int endMs);
    void eraseSyllable(int at);
    void cutSyllable(int i, std::u16string first, std::u16string second, int boundary);

    std::vector<std::u16string> m_syls;
    std::vector<std::u16string> m_tags;
    std::vector<int> m_times; // each syllable's end, in ms
    std::vector<Place> m_places;
    KaraokeCharClass m_classes = KaraokeCharClass::ascii();
};

// KA1-lead-in-keeps-syllables over a Line's text: with \k tags, the text as
// moveStart leaves it for the start moved from `oldStartMs` to
// `newStartMs` (the Line ending at `endMs`, which a last tag left unfinished
// ends at, as when the panel reads it); none without tags or without a move,
// with a last block never closed that holds a \k tag, or with one block of
// more \k tags than 16 × max(pieceTags, 1024) (its text left as it is). `pieceTags`: how many \k tags it reads at a time (tests
// take a few).
std::optional<std::u16string> karaokeKeepSyllables(std::u16string_view text, int oldStartMs, int newStartMs,
                                                   int endMs, int pieceTags = 1024);

// The UTF-16 length of the first `graphemes` graphemes of `text`.
std::size_t karaokeGraphemePrefix(std::u16string_view text, int graphemes);
// KA1-grapheme-letters: where the letter at `at` (outside blocks) ends: its
// grapheme read without the blocks inside it, or \N \n \h.
std::size_t karaokeLetterEnd(std::u16string_view text, std::size_t at);

// Legacy AudioBox::OnKaraoke's zoom: switching on remembers the slider
// (lastHorizontalZoom) and zooms in by 20 (not below 30); switching off goes
// back to it, or (never switched on in this box) zooms out by 20, not past 70.
// Answers the new zoom; legacy then wrote it into AUDIO_VERTICAL_ZOOM.
int legacyKaraokeZoom(bool on, int slider, int &lastHorizontalZoom);

// What the display draws for karaoke (legacy DoUpdateImage's "Draw karaoke"):
// each boundary, each syllable's stripped text on a bar of the boundary
// colour centred between its boundaries, the hovered letter's mark and the
// current syllable's frame. `lineStart` is the start boundary's column.
struct AudioKaraokeMarks {
    std::vector<int> times;
    std::vector<std::u16string> stripped;
    int current = 0, hover = -1, character = -1;
    int curStartMs = 0;
};
void karaokeShapes(std::vector<AudioShape> &out, const AudioView &view, std::int64_t lineStart,
                   const AudioKaraokeMarks &marks, const KaraokeMeasure &measure, int textHeight,
                   std::uint32_t boundaryColour, std::uint32_t textColour);

// The label font's measure from the scene's text widths: D3DX measured a
// text without its leading and trailing spaces.
KaraokeMeasure karaokeLabelMeasure(const AudioTextWidth &textWidth);

} // namespace hikari::application
