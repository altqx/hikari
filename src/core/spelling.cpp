#include "hikari/core/spelling.h"

#include <algorithm>

namespace hikari::core::legacy {

namespace {

bool isDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

// The wrap mark and override brackets of a format: "|" ends a wrap after SRT
// in the format list (TMPlayer, MicroDVD, MPL2), "\N" otherwise; SRT tags are
// <...>, every other format's {...}.
struct Syntax {
    bool lineFormat = false;
    char16_t split = u'\\';
    char16_t open = u'{';
    char16_t close = u'}';
};

Syntax syntaxOf(SubtitleFormat format)
{
    Syntax s;
    s.lineFormat = static_cast<int>(format) > static_cast<int>(SubtitleFormat::Srt);
    s.split = s.lineFormat ? u'|' : u'\\';
    s.open = format == SubtitleFormat::Srt ? u'<' : u'{';
    s.close = format == SubtitleFormat::Srt ? u'>' : u'}';
    return s;
}

// wxSafeIsspace: ASCII whitespace only.
bool isSafeSpace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\v' || c == u'\f' || c == u'\r';
}

std::u16string trimRight(std::u16string_view s)
{
    std::size_t end = s.size();
    while (end > 0 && isSafeSpace(s[end - 1]))
        --end;
    return std::u16string(s.substr(0, end));
}

// wxStringTokenizer(text, "\n"): whitespace delimiters behave like strtok,
// so empty tokens are skipped.
std::vector<std::u16string_view> lines(std::u16string_view content)
{
    std::vector<std::u16string_view> out;
    std::size_t from = 0;
    while (from <= content.size()) {
        std::size_t at = content.find(u'\n', from);
        if (at == std::u16string_view::npos)
            at = content.size();
        if (at > from)
            out.push_back(content.substr(from, at - from));
        from = at + 1;
    }
    return out;
}

// The text CheckText and FindMisspells segment: blocks and drawings left
// out, "\N", "\n" and "\h" as two spaces, "|" as one; one offset per
// character of the checked text.
void collectText(std::u16string_view text, SubtitleFormat format, std::u16string &checkText,
                 std::vector<std::size_t> &offsets)
{
    const Syntax s = syntaxOf(format);
    bool block = false;
    bool drawing = false;
    const std::size_t textLen = text.size();
    std::size_t i = 0;
    while (i < textLen) {
        const char16_t ch = text[i];
        if (block && ch == u'p' && text[i - 1] == u'\\' && i + 1 < textLen && isDigit(text[i + 1]))
            drawing = text[i + 1] != u'0';
        if (ch == s.open) {
            block = true;
            ++i;
            continue;
        }
        if (ch == s.close) {
            block = false;
            ++i;
            continue;
        }
        if (!block && !drawing) {
            if (ch == s.split) {
                if (s.lineFormat) {
                    checkText += u' ';
                    offsets.push_back(i);
                } else {
                    // At the end of the text the backslash is its own "next" character.
                    const char16_t next = text[i + 1 < textLen ? i + 1 : i];
                    if (next == u'N' || next == u'n' || next == u'h') {
                        checkText += u"  ";
                    } else {
                        checkText += ch;
                        checkText += next;
                    }
                    offsets.push_back(i);
                    offsets.push_back(i + 1);
                    ++i;
                }
            } else {
                checkText += ch;
                offsets.push_back(i);
            }
        }
        ++i;
    }
}

} // namespace

SpellMarks checkTextAndBrackets(std::u16string_view text, SubtitleFormat format, const WordSegmenter &segment,
                                const WordCheck &check, int replaceTagsLen)
{
    SpellMarks out;
    auto bracketError = [&](int at) {
        out.errors.push_back(at);
        out.errors.push_back(at);
        out.misspells.push_back({});
    };
    std::u16string checkText;
    std::vector<std::size_t> offsets;
    // SpellChecker::Check: one wrap's words.
    auto checkWrap = [&] {
        if (checkText.size() == offsets.size() && check) {
            for (const WordSegment &seg : segment(checkText)) {
                if (!seg.letters || seg.length == 0)
                    continue;
                const std::u16string word = checkText.substr(seg.start, seg.length);
                if (check(word))
                    continue;
                const int counter = static_cast<int>(seg.start);
                const int wordLen = static_cast<int>(seg.length);
                const std::size_t start = offsets[seg.start];
                const std::size_t end = offsets[seg.start + seg.length - 1];
                const Misspell misspell{word, static_cast<int>(start), static_cast<int>(end)};
                if (end - start + 1 > seg.length) {
                    // Tags inside the word: one mark per run of adjacent characters.
                    const int counterEnd = counter + wordLen - 1;
                    int lastPos = static_cast<int>(start);
                    for (int k = counter; k < counterEnd; ++k) {
                        const std::size_t firstChar = offsets[static_cast<std::size_t>(k)];
                        const std::size_t secondChar = offsets[static_cast<std::size_t>(k) + 1];
                        if (firstChar + 1 != secondChar) {
                            out.errors.push_back(lastPos);
                            out.errors.push_back(static_cast<int>(firstChar));
                            out.misspells.push_back(misspell);
                            lastPos = static_cast<int>(secondChar);
                        }
                        if (k + 1 == counterEnd) {
                            out.errors.push_back(lastPos);
                            out.errors.push_back(static_cast<int>(secondChar));
                            out.misspells.push_back(misspell);
                        }
                    }
                } else {
                    out.errors.push_back(static_cast<int>(start));
                    out.errors.push_back(static_cast<int>(end));
                    out.misspells.push_back(misspell);
                }
            }
        }
        checkText.clear();
        offsets.clear();
    };

    const Syntax s = syntaxOf(format);
    const bool repltags = replaceTagsLen >= 0;
    bool block = false;
    bool drawing = false;
    int lastStartBracket = -1;
    int lastEndBracket = -1;
    int lastStartTBracket = -1;
    int lastStartCBracket = -1;
    int lastEndCBracket = -1;
    const std::size_t textLen = text.size();
    std::size_t i = 0;
    std::size_t tagReplaceI = 0;
    while (i < textLen) {
        const char16_t ch = text[i];
        if (block) {
            if (ch == u'p' && text[i - 1] == u'\\' && i + 1 < textLen && isDigit(text[i + 1])) {
                drawing = text[i + 1] != u'0';
            } else if (!repltags) {
                // A brace opened inside a block marks the block's own brace.
                if (ch == s.open)
                    bracketError(lastStartCBracket);
                if (ch == u'\\' && text[i == 0 ? 0 : i - 1] == u'\\')
                    bracketError(static_cast<int>(i));
                if (ch == u'(') {
                    // "\t(" and other one-letter tags: their parenthesis is tracked apart.
                    if (i > 1 && text[i - 2] == u'\\' && text[i - 1] != 0) {
                        lastStartTBracket = static_cast<int>(i);
                        ++i;
                        continue;
                    }
                    if (lastStartBracket > lastEndBracket)
                        bracketError(lastStartBracket);
                    lastStartBracket = static_cast<int>(i);
                }
                if (ch == u')') {
                    if (lastStartBracket < lastEndBracket || lastStartBracket < 0) {
                        if (lastStartTBracket > 0 &&
                            (lastStartTBracket < lastEndBracket || lastStartBracket < lastStartTBracket)) {
                            lastStartTBracket = -1;
                            ++i;
                            continue;
                        }
                        bracketError(static_cast<int>(i));
                    }
                    lastEndBracket = static_cast<int>(i);
                }
            }
        }
        if (lastStartTBracket >= 0 && !repltags && (ch == s.open || ch == s.close)) {
            bracketError(lastStartTBracket);
            lastStartTBracket = -1;
        }
        if (ch == s.open) {
            if (repltags)
                tagReplaceI += static_cast<std::size_t>(replaceTagsLen);
            block = true;
            lastStartCBracket = static_cast<int>(i);
        } else if (ch == s.close) {
            if (!block && !repltags)
                bracketError(static_cast<int>(i));
            block = false;
            lastEndCBracket = static_cast<int>(i);
            ++i;
            continue;
        }
        if (!block && !drawing) {
            if (ch == s.split) {
                if (s.lineFormat) {
                    checkText += u' ';
                    offsets.push_back(repltags ? tagReplaceI : i);
                    checkWrap();
                } else {
                    const char16_t next = text[i + 1 < textLen ? i + 1 : i];
                    const bool splitSecond = next == u'N' || next == u'n';
                    if (splitSecond || next == u'h') {
                        checkText += u"  ";
                    } else {
                        checkText += ch;
                        checkText += next;
                    }
                    offsets.push_back(repltags ? tagReplaceI : i);
                    offsets.push_back(repltags ? tagReplaceI + 1 : i + 1);
                    if (splitSecond)
                        checkWrap();
                    ++i;
                    ++tagReplaceI;
                }
            } else {
                checkText += ch;
                offsets.push_back(repltags ? tagReplaceI : i);
            }
            ++tagReplaceI;
        }
        ++i;
    }
    if (!checkText.empty())
        checkWrap();
    if (lastStartCBracket > lastEndCBracket)
        bracketError(lastStartCBracket);
    if (lastStartBracket > lastEndBracket)
        bracketError(lastStartBracket);
    if (lastStartTBracket >= 0)
        bracketError(lastStartTBracket);
    return out;
}

std::vector<Misspell> checkText(std::u16string_view text, SubtitleFormat format, const WordSegmenter &segment,
                                const WordCheck &check)
{
    std::u16string checkText;
    std::vector<std::size_t> offsets;
    collectText(text, format, checkText, offsets);
    std::vector<Misspell> out;
    if (!check || checkText.size() != offsets.size())
        return out;
    for (const WordSegment &seg : segment(checkText)) {
        if (!seg.letters || seg.length == 0)
            continue;
        std::u16string word = checkText.substr(seg.start, seg.length);
        if (!check(word))
            out.push_back({std::move(word), static_cast<int>(offsets[seg.start]),
                           static_cast<int>(offsets[seg.start + seg.length - 1])});
    }
    return out;
}

std::vector<Misspell> findMisspells(std::u16string_view text, std::u16string_view find, SubtitleFormat format,
                                    const WordSegmenter &segment, const CaseMapping &cases)
{
    std::u16string checkText;
    std::vector<std::size_t> offsets;
    collectText(text, format, checkText, offsets);
    std::vector<Misspell> out;
    if (checkText.size() != offsets.size())
        return out;
    // wxString::CmpNoCase: equal length, equal character by character in lower case.
    auto sameNoCase = [&](std::u16string_view a, std::u16string_view b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t k = 0; k < a.size(); ++k)
            if (cases.toLower(a[k]) != cases.toLower(b[k]))
                return false;
        return true;
    };
    for (const WordSegment &seg : segment(checkText)) {
        if (!seg.letters || seg.length == 0)
            continue;
        std::u16string word = checkText.substr(seg.start, seg.length);
        if (sameNoCase(word, find))
            out.push_back({std::move(word), static_cast<int>(offsets[seg.start]),
                           static_cast<int>(offsets[seg.start + seg.length - 1])});
    }
    return out;
}

int replaceMisspell(std::u16string_view misspell, std::u16string_view replacement, int start, int end,
                    std::u16string &text)
{
    if (start < 0 || end < start || static_cast<std::size_t>(start) > text.size())
        return start;
    // Legacy compares the length with end - start - 1 as size_t: a one-letter
    // word (-1) also takes the path that keeps override blocks.
    const bool hasTags = misspell.size() < static_cast<std::size_t>(end - start - 1);
    const std::size_t from = static_cast<std::size_t>(start);
    const std::size_t count = std::min(static_cast<std::size_t>(end - start + 1), text.size() - from);
    if (!hasTags) {
        text.replace(from, count, replacement);
        return start + static_cast<int>(replacement.size());
    }
    std::u16string newReplace;
    std::size_t k = 0;
    bool block = false;
    for (std::size_t j = from; j <= static_cast<std::size_t>(end) && j < text.size(); ++j) {
        const char16_t ch = text[j];
        if (block) {
            newReplace += ch;
        } else if (ch == u'{') {
            block = true;
            newReplace += ch;
        } else if (k < replacement.size()) {
            newReplace += replacement[k];
            ++k;
        }
        if (ch == u'}')
            block = false;
    }
    if (k < replacement.size())
        newReplace += replacement.substr(k);
    text.replace(from, count, newReplace);
    return start + static_cast<int>(newReplace.size());
}

std::u16string rightCase(std::u16string_view replacement, std::u16string_view misspell, const CaseMapping &cases)
{
    std::size_t upperCase = 0;
    for (const char16_t c : misspell)
        if (cases.isUpper(c))
            ++upperCase;
    std::u16string result(replacement);
    if (upperCase == misspell.size()) {
        for (char16_t &c : result)
            c = cases.toUpper(c);
    } else {
        for (char16_t &c : result)
            c = cases.toLower(c);
        if (upperCase > 0 && !result.empty())
            result[0] = cases.toUpper(result[0]);
    }
    return result;
}

bool isAllUpperCase(std::u16string_view word, const CaseMapping &cases)
{
    std::size_t upperCase = 0;
    for (const char16_t c : word)
        if (cases.isUpper(c))
            ++upperCase;
    return upperCase == word.size();
}

bool legacyIsNumber(std::u16string_view text)
{
    if (text.empty())
        return true;
    std::size_t i = 0;
    if (text[0] == u'-' || text[0] == u'+')
        ++i;
    for (; i < text.size(); ++i)
        if (!isDigit(text[i]))
            return false;
    return true;
}

std::vector<std::u16string> userDictionaryWords(std::u16string_view content)
{
    std::vector<std::u16string> out;
    for (const auto line : lines(content)) {
        std::u16string word = trimRight(line);
        if (word.empty() || legacyIsNumber(word))
            continue;
        out.push_back(std::move(word));
    }
    return out;
}

std::vector<std::u16string> userDictionaryEntries(std::u16string_view content)
{
    std::vector<std::u16string> out;
    for (const auto line : lines(content))
        out.push_back(trimRight(line));
    return out;
}

std::u16string appendUserWord(std::optional<std::u16string_view> content, std::u16string_view word)
{
    if (!content)
        return std::u16string(word);
    std::u16string out(*content);
    out += u'\n';
    out += word;
    return out;
}

RemovedUserWords removeUserWords(std::u16string_view content, const std::vector<std::u16string> &words)
{
    RemovedUserWords out;
    for (const auto line : lines(content)) {
        std::u16string current = trimRight(line);
        bool listed = false;
        for (const auto &w : words)
            listed = listed || w == current;
        if (listed) {
            out.removed.push_back(std::move(current));
            continue;
        }
        out.content += current;
        out.content += u"\r\n";
    }
    return out;
}

} // namespace hikari::core::legacy
