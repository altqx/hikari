#pragma once

// Offsets are UTF-16 code units; word segmentation and checking come from the caller.

#include "hikari/core/document.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::core::legacy {

// A word-boundary segment as ICU rules give it: `letters` for letters, kana or ideographs,
// `number` for numbers; spaces and punctuation have neither.
struct WordSegment {
    std::size_t start = 0;
    std::size_t length = 0;
    bool letters = false;
    bool number = false;
};
using WordSegmenter = std::function<std::vector<WordSegment>(std::u16string_view)>;
// True when the word is correct.
using WordCheck = std::function<bool(std::u16string_view)>;

// One UTF-16 unit at a time.
struct CaseMapping {
    std::function<bool(char16_t)> isUpper;
    std::function<char16_t(char16_t)> toUpper;
    std::function<char16_t(char16_t)> toLower;
};

// Offsets inclusive. A bracket error has no word and -1 offsets.
struct Misspell {
    std::u16string word;
    int start = -1;
    int end = -1;
    bool operator==(const Misspell &) const = default;
};

// `errors`: inclusive offset pairs to mark, one Misspell per pair.
struct SpellMarks {
    std::vector<int> errors;
    std::vector<Misspell> misspells;
};

// Misspelled words outside override blocks and drawings, and bracket errors. An empty `check`
// reports bracket errors only. With replaceTagsLen >= 0 each block counts as that many characters
// and bracket errors are not reported.
SpellMarks checkTextAndBrackets(std::u16string_view text, SubtitleFormat format, const WordSegmenter &segment,
                                const WordCheck &check, int replaceTagsLen = -1);

// Misspelled words only.
std::vector<Misspell> checkText(std::u16string_view text, SubtitleFormat format, const WordSegmenter &segment,
                                const WordCheck &check);

// Every letter word equal to `find` ignoring case.
std::vector<Misspell> findMisspells(std::u16string_view text, std::u16string_view find, SubtitleFormat format,
                                    const WordSegmenter &segment, const CaseMapping &cases);

// Replaces text[start..end]; override blocks inside the word stay where they were. Returns the
// offset after the replacement.
int replaceMisspell(std::u16string_view misspell, std::u16string_view replacement, int start, int end,
                    std::u16string &text);

// `replacement` in the case pattern of `misspell`.
std::u16string rightCase(std::u16string_view replacement, std::u16string_view misspell, const CaseMapping &cases);
bool isAllUpperCase(std::u16string_view word, const CaseMapping &cases);

// True for empty, or an optional sign and ASCII digits only.
bool legacyIsNumber(std::u16string_view text);

// User dictionary (Dictionary/UserDic.udic) content: empty and number lines skipped.
std::vector<std::u16string> userDictionaryWords(std::u16string_view content);
// Every line.
std::vector<std::u16string> userDictionaryEntries(std::u16string_view content);
// The file's new text; the word alone when the file was unreadable or empty.
std::u16string appendUserWord(std::optional<std::u16string_view> content, std::u16string_view word);
// Remaining lines each followed by CRLF, and the removed lines in file order.
struct RemovedUserWords {
    std::u16string content;
    std::vector<std::u16string> removed;
};
RemovedUserWords removeUserWords(std::u16string_view content, const std::vector<std::u16string> &words);

}
