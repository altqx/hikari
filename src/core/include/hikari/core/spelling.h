#pragma once

// F3: the spell checker's text walks and its user dictionary file (legacy
// SpellChecker.cpp, SpellCheckerDialog.cpp and LineParse.h at 20d647c4), on
// UTF-16 text as legacy's wxString on Windows. Legacy segments words with
// boost::locale (ICU) and checks them with Hunspell; both come from the
// caller here (a WordSegmenter and a WordCheck), so this stays plain C++.

#include "hikari/core/document.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core::legacy {

// One boost::locale boundary::word segment of the checked text. `letters`:
// its rule is word_letters (letters, kana or ideographs); `number`:
// word_number. Spaces and punctuation have neither.
struct WordSegment {
    std::size_t start = 0;
    std::size_t length = 0;
    bool letters = false;
    bool number = false;
};
using WordSegmenter = std::function<std::vector<WordSegment>(std::u16string_view)>;
// SpellChecker::CheckWord: true when the word is correct.
using WordCheck = std::function<bool(std::u16string_view)>;

// Per-character case functions (wxString::MakeUpper/MakeLower, iswupper and
// wxToupper work one UTF-16 unit at a time).
struct CaseMapping {
    std::function<bool(char16_t)> isUpper;
    std::function<char16_t(char16_t)> toUpper;
    std::function<char16_t(char16_t)> toLower;
};

// MisspellData: the word and its first and last offset (inclusive) in the
// text. A bracket error has no word and -1 offsets.
struct Misspell {
    std::u16string word;
    int start = -1;
    int end = -1;
    bool operator==(const Misspell &) const = default;
};

// TextData::errors after CheckTextAndBrackets: offset pairs (inclusive) to
// mark, and the MisspellData pushed with each pair (TextEditor::misspells).
struct SpellMarks {
    std::vector<int> errors;
    std::vector<Misspell> misspells;
};

// CheckTextAndBrackets (the editor's TextData::Init2 and the Grid's
// TextData::Init): misspelled words outside override blocks and drawings,
// and bracket errors (unclosed or nested braces, a stray closing brace,
// "\\", unmatched parentheses). An empty `check` is spell checking off: only
// bracket errors. With replaceTagsLen >= 0 (the Grid with tags swapped for a
// GRID_TAGS_SWAP_CHARACTER of that length) offsets count each block as that
// many characters and bracket errors are not reported.
SpellMarks checkTextAndBrackets(std::u16string_view text, SubtitleFormat format, const WordSegmenter &segment,
                                const WordCheck &check, int replaceTagsLen = -1);

// CheckText (the Spellchecker window): the misspelled words only.
std::vector<Misspell> checkText(std::u16string_view text, SubtitleFormat format, const WordSegmenter &segment,
                                const WordCheck &check);

// FindMisspells (Replace all): every letter word equal to `find` ignoring
// case (wxString::CmpNoCase).
std::vector<Misspell> findMisspells(std::u16string_view text, std::u16string_view find, SubtitleFormat format,
                                    const WordSegmenter &segment, const CaseMapping &cases);

// ReplaceMisspell: replaces text[start..end] with `replacement`; when the
// word spans override blocks ({...} only) the blocks stay where they were.
// Returns the offset after the replacement (the editor's new caret).
int replaceMisspell(std::u16string_view misspell, std::u16string_view replacement, int start, int end,
                    std::u16string &text);

// SpellCheckerDialog::GetRightCase and IsAllUpperCase.
std::u16string rightCase(std::u16string_view replacement, std::u16string_view misspell, const CaseMapping &cases);
bool isAllUpperCase(std::u16string_view word, const CaseMapping &cases);

// wxString::IsNumber: empty, or an optional sign and ASCII digits only.
bool legacyIsNumber(std::u16string_view text);

// The user dictionary (Dictionary/UserDic.udic), as decoded text.
// Initialize: the words loaded into the dictionary (empty and number lines skipped).
std::vector<std::u16string> userDictionaryWords(std::u16string_view content);
// LoadAddedMisspels: the list "Remove from dictionary" offers (every line).
std::vector<std::u16string> userDictionaryEntries(std::u16string_view content);
// AddWord: the file's new text (the word alone when the file could not be
// read or reads as empty text, as OpenWrite::FileOpen fails for both).
std::u16string appendUserWord(std::optional<std::u16string_view> content, std::u16string_view word);
// RemoveWords: the remaining lines, each followed by CRLF, and the removed
// lines (in file order). Nothing is written when no line matched.
struct RemovedUserWords {
    std::u16string content;
    std::vector<std::u16string> removed;
};
RemovedUserWords removeUserWords(std::u16string_view content, const std::vector<std::u16string> &words);

} // namespace hikari::core::legacy
