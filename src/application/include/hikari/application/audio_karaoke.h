#pragma once

// Positions count UTF-16 units.

#include "hikari/application/audio_display.h"
#include "hikari/application/karaoke_split.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hikari::application {

// ascii() answers ASCII only. `symbol` characters are read as digits are.
struct KaraokeCharClass {
    std::function<bool(char16_t)> space;
    std::function<bool(char32_t)> punct;
    std::function<bool(char32_t)> symbol;
    std::function<char16_t(char16_t)> lower;
    static KaraokeCharClass ascii();
};

// Width in the label font, leaving out leading and trailing spaces (D3DX DT_CALCRECT).
using KaraokeMeasure = std::function<int(std::u16string_view)>;
// The measured width, plus 4 for a leading space and 4 for a trailing one.
int legacyTextExtent(const KaraokeMeasure &measure, std::u16string_view text);

// Leading white space, a sign and digits; 0 without.
int legacyAtoi(std::u16string_view text);

class AudioKaraoke {
public:
    // `text` is the translation when the Line has one.
    struct Line {
        std::u16string text;
        int startMs = 0, endMs = 0;
    };

    // By \k tags (\kt is no syllable) when the Line has them; otherwise
    // `autoSplit` or at spaces and \N \h \n, time shared by character.
    void split(const Line &line, bool autoSplit, const KaraokeSplitRules &rules,
               const KaraokeCharClass &classes = KaraokeCharClass::ascii());
    // Reads `line` as tagged even when this piece has no tag. `carried`: tag
    // letters read past the previous piece, taken first; this piece's are left there.
    void splitTags(const Line &line, std::vector<std::u16string> *carried = nullptr);
    void clear();
    // `times`: each syllable's end in ms. The first syllable becomes current.
    void assign(std::vector<std::u16string> syllables, std::vector<std::u16string> tags, std::vector<int> times);
    // Each tag goes back where it was read, its number as written while its
    // time is the same.
    std::u16string text(int curStartMs) const;
    // Syllable `i` takes the next one's text and end, keeping its tag; "{}"
    // is removed. False for the last syllable. A joined # syllable loses its #.
    bool join(int i);
    // Splits before the `letters`th visible character; the second part is a
    // \k and the time is divided by the parts' characters.
    bool splitSyllable(int i, int letters, int curStartMs);
    // The text splits at the letter the time's share gives; either part may
    // be empty. False when `ms` is not inside syllable `i`.
    bool splitAt(int i, int ms, int curStartMs);
    // A letter is a grapheme; past the last one the split is right after it.
    std::pair<std::u16string, std::u16string> letters(int i, int letters) const;
    std::u16string stripped(int i) const;
    // Syllables stay at their audio times: an earlier start adds or grows an
    // empty lead syllable, a later one cuts what it passes.
    void moveStart(int oldStartMs, int newStartMs);
    // Long \k syllables become \kf; with none left as \k, they go back to \k.
    // False when no syllable is that long.
    bool switchLongToKf(int thresholdMs, int curStartMs);

    std::pair<int, int> syllableTimes(int i, int curStartMs) const;
    std::pair<int, int> visibleTimes(int i, int curStartMs, int curEndMs) const;
    // Within 6 columns of x; -1 for none.
    int boundaryAt(int x, const AudioView &view) const;
    // 2 columns of slack; -1 for none.
    int syllableAt(int x, const AudioView &view, int curStartMs) const;
    // The syllable and the grapheme of its stripped text before which x falls.
    std::optional<std::pair<int, int>> letterAt(int x, const AudioView &view, int curStartMs,
                                                const KaraokeMeasure &measure) const;

    int count() const { return static_cast<int>(m_syls.size()); }
    const std::vector<std::u16string> &syllables() const { return m_syls; }
    const std::vector<std::u16string> &tags() const { return m_tags; }
    const std::vector<int> &times() const { return m_times; }
    std::vector<int> &times() { return m_times; }

    int current = 0;
    int hover = -1;
    int character = -1;

private:
    // `at` counts from the syllable's text start; `cs` is `number`'s value,
    // none for a new syllable.
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

// moveStart over a Line's text. nullopt without \k tags or a move, with an
// unclosed last block holding a \k tag, or past 16 × max(pieceTags, 1024)
// tags in one block. `pieceTags`: \k tags read at a time.
std::optional<std::u16string> karaokeKeepSyllables(std::u16string_view text, int oldStartMs, int newStartMs,
                                                   int endMs, int pieceTags = 1024);

std::size_t karaokeGraphemePrefix(std::u16string_view text, int graphemes);
// A grapheme read without blocks inside it, or \N \n \h.
std::size_t karaokeLetterEnd(std::u16string_view text, std::size_t at);

// On remembers the slider and zooms in by 20 (not below 30); off goes back
// to it, or zooms out by 20 (not past 70) when never on. Answers the new zoom.
int legacyKaraokeZoom(bool on, int slider, int &lastHorizontalZoom);

// `lineStart` is the start boundary's column.
struct AudioKaraokeMarks {
    std::vector<int> times;
    std::vector<std::u16string> stripped;
    int current = 0, hover = -1, character = -1;
    int curStartMs = 0;
};
void karaokeShapes(std::vector<AudioShape> &out, const AudioView &view, std::int64_t lineStart,
                   const AudioKaraokeMarks &marks, const KaraokeMeasure &measure, int textHeight,
                   std::uint32_t boundaryColour, std::uint32_t textColour);

KaraokeMeasure karaokeLabelMeasure(const AudioTextWidth &textWidth);

}
