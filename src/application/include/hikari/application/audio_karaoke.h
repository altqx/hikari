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

#include "hikari/application/audio_display.h"

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
// punctuation and symbol categories and its lower case.
struct KaraokeCharClass {
    std::function<bool(char16_t)> space;
    std::function<bool(char16_t)> punct;
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
    // {([^}]*)\\kf?o?[0-9]([^}]*)} on the lower-cased text) the tags' times;
    // otherwise split automatically (AUDIO_KARAOKE_SPLIT_MODE, `autoSplit`:
    // after a vowel, n, space or punctuation not followed by space or
    // punctuation, with AUDIO_MERGE_EVERY_N_WITH_SYLLABLE's `everyN`) or at
    // spaces and \N \h \n, the Line's duration shared out. A5-auto-unclosed:
    // what an unclosed "{" kept from the last split stays in the last syllable.
    void split(const Line &line, bool autoSplit, bool everyN, const KaraokeCharClass &classes = KaraokeCharClass::ascii());
    void clear();
    // Legacy GetText: each syllable with its \k tag (the time from the
    // previous syllable's end, or from `curStartMs`, in centiseconds).
    std::u16string text(int curStartMs) const;
    // Legacy Join: syllable `i` takes the next one's text (every "{}" in the
    // result removed), its end and keeps its own tag. False (nothing done)
    // for the last syllable (A5-join-last).
    bool join(int i);
    // Legacy SplitSyl: syllable `i` split before its `letters`th visible
    // character, the new boundary half way (ZEROIT), the second half a \k.
    bool splitSyllable(int i, int letters, int curStartMs);
    // Legacy GetLetters (A5-split-last-letter: after the last letter the split
    // is right after it, not at the raw position) and GetTextStripped.
    std::pair<std::u16string, std::u16string> letters(int i, int letters) const;
    std::u16string stripped(int i) const;

    // Legacy GetSylTimes and GetSylVisibleTimes.
    std::pair<int, int> syllableTimes(int i, int curStartMs) const;
    std::pair<int, int> visibleTimes(int i, int curStartMs, int curEndMs) const;
    // Legacy CheckIfOver: the boundary within 6 columns of x (-1: none).
    int boundaryAt(int x, const AudioView &view) const;
    // Legacy GetSylAtX: the syllable under x (2 columns of slack; -1: none).
    int syllableAt(int x, const AudioView &view, int curStartMs) const;
    // Legacy GetLetterAtX: the syllable under x and the letter of its
    // stripped text before which x falls (its length past the last one).
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
    std::vector<std::u16string> m_syls;
    std::vector<std::u16string> m_tags;
    std::vector<int> m_times; // each syllable's end, in ms
};

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
