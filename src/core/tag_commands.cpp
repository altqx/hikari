#include "hikari/core/tag_commands.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <regex>
#include <string>

namespace hikari::core::legacy {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

constexpr long npos = -1;

// wxString::Find(ch, true): last index or -1.
long findLast(u16v s, char16_t ch)
{
    const auto p = s.rfind(ch);
    return p == u16v::npos ? npos : static_cast<long>(p);
}

// wxString::SubString(from, to): inclusive, and Mid(from, to - from + 1)
// with a size_t count, so to < from - 1 reads to the end.
u16 subString(u16v s, long from, long to)
{
    if (from < 0 || static_cast<std::size_t>(from) > s.size())
        return {};
    const std::size_t count = static_cast<std::size_t>(to - from + 1);
    return u16(s.substr(static_cast<std::size_t>(from), count));
}

std::size_t freq(u16v s, char16_t ch)
{
    return static_cast<std::size_t>(std::ranges::count(s, ch));
}

bool startsWith(u16v s, u16v p)
{
    return s.substr(0, p.size()) == p;
}

bool endsWith(u16v s, u16v p)
{
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

// FindFromEnd: last occurrence as size_t, or (size_t)-1.
std::size_t findFromEnd(u16v text, u16v what)
{
    const auto p = text.rfind(what);
    return p == u16v::npos ? static_cast<std::size_t>(-1) : p;
}

// wxRegEx("^" + pattern).ReplaceAll(&s, "\\1"): the anchored match (if any)
// replaced by its first group.
bool replaceWithGroup(u16 &s, const std::regex &re)
{
    const std::string utf8(reinterpret_cast<const char *>(toUtf8(s).c_str()));
    std::smatch m;
    if (!std::regex_search(utf8, m, re, std::regex_constants::match_continuous))
        return false;
    const std::string replaced = m[1].str() + utf8.substr(static_cast<std::size_t>(m.length(0)));
    s = toUtf16(std::u8string(reinterpret_cast<const char8_t *>(replaced.data()), replaced.size()));
    return true;
}

} // namespace

std::pair<long, long> findBrackets(u16v text, long from)
{
    bool haveStartBracket = false;
    bool haveEndBracket = false;
    long endPos = -1;
    long startPos = -1;
    const std::size_t len = text.size();
    std::size_t i = (from - 1 < 1) ? 1 : static_cast<std::size_t>(from - 1);
    for (; i < len; i++) {
        const char16_t ch = text[i];
        if (ch == u'}') {
            haveEndBracket = true;
            endPos = static_cast<long>(i);
        } else if (ch == u'{' && static_cast<long>(i) + 1 > from && static_cast<long>(i) != from) {
            if (!haveEndBracket)
                break;
            haveEndBracket = false;
        } else if (haveEndBracket) {
            break;
        }
    }
    if (len == 0)
        return {-1, -1};
    std::size_t k = static_cast<std::size_t>(from) < len ? static_cast<std::size_t>(from) : len - 1;
    for (; k + 1 > 0; k--) {
        const char16_t ch = text[k];
        if (ch == u'{') {
            haveStartBracket = true;
            startPos = static_cast<long>(k);
        } else if (ch == u'}' && static_cast<long>(k) + 1 < from) {
            if (!haveStartBracket)
                break;
            haveStartBracket = false;
        } else if (haveStartBracket) {
            break;
        }
    }
    if (haveEndBracket && i >= len && static_cast<std::size_t>(endPos + 1) < len)
        endPos = static_cast<long>(len) - 1;
    if (startPos != -1 && endPos == -1)
        endPos = static_cast<long>(len) - 1;
    if (startPos == -1 && endPos != -1)
        endPos = -1;
    return {startPos, endPos};
}

bool TagEditor::findTag(u16v pattern, int mode, bool toEndOfSelection)
{
    m_finding.clear();
    m_posX = m_posY = 0;
    m_cursor = 0;
    m_inBracket = false;
    m_hasSelection = false;
    const u16 &txt = m_state.text;
    m_lastPattern = u16(pattern);
    const std::string pat(reinterpret_cast<const char *>(toUtf8(pattern).c_str()));
    const std::regex re(pat);

    if (mode != 1 && mode != 3) {
        m_from = m_state.selectionStart;
        m_to = m_state.selectionEnd;
    }
    if (mode == 2 && findBrackets(txt, m_from).first != 0)
        m_from = m_to = 0;
    if (mode == 1)
        m_from = m_to = 0;

    bool brkt = true;
    if (txt.empty()) {
        m_posX = m_posY = 0;
        m_inBracket = false;
        m_cursor = 0;
        if (toEndOfSelection)
            m_hasSelection = false;
        return false;
    }
    if (toEndOfSelection && m_from != m_to && mode == 0)
        m_hasSelection = true;

    auto [bracketStart, bracketEnd] = findBrackets(txt, m_from);
    if (bracketStart == -1 || bracketStart > bracketEnd + 1) {
        m_inBracket = false;
        bracketEnd = m_from;
        brkt = false;
    } else {
        if (bracketStart == bracketEnd + 1)
            std::tie(bracketStart, bracketEnd) = findBrackets(txt, bracketStart + 1);
        m_inBracket = true;
    }
    m_posX = m_posY = bracketEnd;
    if (toEndOfSelection && m_hasSelection) {
        m_cursor = m_to;
        if (m_inBracket)
            m_cursor--;
    } else {
        m_cursor = bracketEnd;
    }
    bool isT = false;
    bool hasR = false;
    bool placedInT = false;
    long lastT = bracketEnd - 1;
    long endT = lastT;
    long lslash = bracketEnd + 1;
    long lastTag = -1;
    u16 found[2];
    std::pair<long, long> fpoints[2];
    const std::size_t txtlen = txt.size();
    // Signed/unsigned comparisons as in the legacy code: a -1 start becomes
    // the last index.
    if (static_cast<std::size_t>(bracketEnd) == txtlen)
        bracketEnd--;
    if (static_cast<std::size_t>(bracketEnd) > txtlen)
        bracketEnd = static_cast<long>(txtlen) - 1;
    if (static_cast<std::size_t>(bracketStart) > txtlen)
        bracketStart = static_cast<long>(txtlen) - 1;
    const bool findTTag = startsWith(pattern, u"t");

    for (long i = bracketEnd; i >= 0; i--) {
        const char16_t ch = txt[static_cast<std::size_t>(i)];
        if (ch == u'\\' && brkt) {
            if (i >= bracketStart)
                lastTag = i;
            u16 ftag = subString(txt, i + 1, lslash - 1);
            if (ftag == u"r")
                hasR = true;
            if (endsWith(ftag, u")") && !findTTag) {
                if (startsWith(ftag, u"t(")) {
                    isT = true;
                    endT = lslash - 1;
                } else if (freq(ftag, u')') > freq(ftag, u'(')) {
                    const u16v textToCheck = u16v(txt).substr(0, static_cast<std::size_t>(lslash));
                    const std::size_t lastTPos = findFromEnd(txt, u"\\t(");
                    const std::size_t lastBS = static_cast<std::size_t>(findLast(textToCheck, u'('));
                    const std::size_t lastBE = static_cast<std::size_t>(findLast(textToCheck, u')'));
                    if (lastTPos != static_cast<std::size_t>(-1) && lastBS < lastBE) {
                        isT = true;
                        endT = lslash - 1;
                    }
                }
            }
            if (startsWith(ftag, u"t(") && !findTTag) {
                if (endT == -1)
                    endT = lastT;
                if (i <= m_from && m_from <= endT) {
                    if (!found[1].empty() && fpoints[1].second <= endT) {
                        m_posX = fpoints[1].first;
                        m_posY = fpoints[1].second;
                        m_finding = found[1];
                        return true;
                    } else if (!found[0].empty()) {
                        if (fpoints[0].second <= endT)
                            break;
                    } else {
                        m_posX = m_posY = endT;
                        m_inBracket = true;
                        placedInT = true;
                    }
                }
                isT = false;
                lslash = i;
                endT = -1;
                lastT = i;
                continue;
            }
            const bool isFN = startsWith(ftag, u"fn");
            if (replaceWithGroup(ftag, re)) {
                if ((endsWith(ftag, u")") && !isFN && (!startsWith(ftag, u"(") || freq(ftag, u')') >= 2)) ||
                    endsWith(ftag, u"}")) {
                    ftag.pop_back();
                    lslash--;
                }
                if (found[0].empty() && !isT) {
                    found[0] = ftag;
                    fpoints[0].first = (i < lastTag) ? lastTag : i;
                    fpoints[0].second = (i < lastTag) ? lastTag : lslash - 1;
                } else {
                    found[1] = ftag;
                    fpoints[1] = {i, lslash - 1};
                }
                if (!isT && !found[0].empty() && i <= m_from)
                    break;
            }
            lslash = i;
        } else if (ch == u'{' && i > 0) {
            const u16v before = u16v(txt).substr(0, static_cast<std::size_t>(i));
            if (findLast(before, u'}') >= findLast(before, u'{')) {
                brkt = false;
                if (txt[static_cast<std::size_t>(i - 1)] != u'}' && hasR)
                    break;
            } else {
                lslash = i - 1;
            }
        } else if (ch == u'}' && i > 0) {
            const u16v before = u16v(txt).substr(0, static_cast<std::size_t>(i));
            if (findLast(before, u'}') < findLast(before, u'{')) {
                lslash = i;
                brkt = true;
            }
        }
    }

    if (!isT && !found[0].empty()) {
        if (m_inBracket && !placedInT) {
            m_posX = fpoints[0].first;
            m_posY = fpoints[0].second;
        }
        m_finding = found[0];
        return true;
    }
    if (lastTag >= 0 && m_inBracket && !placedInT) {
        m_posX = m_posY = findTTag ? bracketEnd : lastTag;
    }
    return false;
}

void TagEditor::putTagInText(u16v tag, u16v resetTag, bool restoreSelection)
{
    u16 txt = m_state.text;
    long where;
    if (!m_inBracket) {
        txt.insert(static_cast<std::size_t>(m_posX), u"{" + u16(tag) + u"}");
        where = m_cursor + static_cast<long>(tag.size()) + 2;
    } else {
        if (m_posX < m_posY) {
            txt.erase(static_cast<std::size_t>(m_posX), static_cast<std::size_t>(m_posY - m_posX + 1));
            where = m_cursor + static_cast<long>(tag.size()) - (m_posY - m_posX);
        } else {
            where = m_cursor + 1 + static_cast<long>(tag.size());
        }
        txt.insert(static_cast<std::size_t>(m_posX), tag);
    }
    if (tag.empty()) {
        for (std::size_t p; (p = txt.find(u"{}")) != u16::npos;)
            txt.erase(p, 2);
        where--;
    }
    m_state.text = txt;
    if (m_hasSelection && !resetTag.empty()) {
        long addition = static_cast<long>(tag.size());
        if (!m_inBracket)
            addition += 2;
        else if (m_posY - m_posX)
            addition -= (m_posY - m_posX) + 1;
        m_from += addition;
        m_to += addition;
        m_lastSelection = {m_from, m_to};
        m_from = m_to;
        findTag(m_lastPattern, 3, false);
        putTagInText(resetTag, u"", true);
        return;
    }
    if (restoreSelection) {
        m_state.selectionStart = m_lastSelection.first;
        m_state.selectionEnd = m_lastSelection.second;
    } else {
        m_state.selectionStart = m_state.selectionEnd = where;
    }
}

EditorText toggleTag(EditorText state, char16_t tag, bool styleValue)
{
    TagEditor editor(std::move(state));
    u16 value = styleValue ? u"1" : u"0";
    u16 next = styleValue ? u"0" : u"1";
    const u16 pattern = u16(1, tag) + u"(0|1)";
    if (editor.findTag(pattern, 0, true)) {
        value = editor.finding();
        next = value == u"1" ? u"0" : u"1";
    }
    editor.putTagInText(u"\\" + u16(1, tag) + next, u"\\" + u16(1, tag) + value);
    return editor.state();
}

namespace {

char16_t lower(char16_t c)
{
    return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

// First case-insensitive occurrence of `needle` in `hay` at or after `from`.
std::size_t findNoCase(u16v hay, u16v needle, std::size_t from = 0)
{
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        std::size_t k = 0;
        while (k < needle.size() && lower(hay[i + k]) == lower(needle[k]))
            ++k;
        if (k == needle.size())
            return i;
    }
    return u16v::npos;
}

// wxRegEx("\\</?" + text + "\\>", icase).Matches(window): start and length.
bool matchSrtTag(u16v window, char16_t text, std::size_t &start, std::size_t &len)
{
    for (std::size_t i = 0; i < window.size(); ++i) {
        if (window[i] != u'<')
            continue;
        std::size_t j = i + 1;
        if (j < window.size() && window[j] == u'/')
            ++j;
        if (j + 1 < window.size() && lower(window[j]) == lower(text) && window[j + 1] == u'>') {
            start = i;
            len = j + 2 - i;
            return true;
        }
    }
    return false;
}

// txt.SubString(from - 4, from + 4): empty when from - 4 wraps below zero.
u16 window(u16v txt, long from)
{
    if (from < 4)
        return {};
    return subString(txt, from - 4, from + 4);
}

} // namespace

EditorText toggleNonAssTag(EditorText state, char16_t tag, bool srt)
{
    u16 txt = state.text;
    long from = state.selectionStart, to = state.selectionEnd;
    long where = from;
    if (srt) {
        const u16 open = u16(u"<") + tag + u">";
        const u16 close = u16(u"</") + tag + u">";
        std::size_t start = 0, len = 0;
        bool match = false;
        if (matchSrtTag(window(txt, from), tag, start, len) && len + start >= 4 && start <= 4) {
            where = from - 4 + static_cast<long>(start);
            txt.erase(static_cast<std::size_t>(where), len);
            txt.insert(static_cast<std::size_t>(where), open);
            where += 3;
            match = true;
        }
        if (!match) {
            txt.insert(static_cast<std::size_t>(from), open);
            from += 3;
            to += 3;
            where = from;
        }
        if (from != to) {
            match = false;
            if (matchSrtTag(window(txt, to), tag, start, len) && len + start >= 4 && start <= 4) {
                txt.erase(static_cast<std::size_t>(to - 4 + static_cast<long>(start)), len);
                txt.insert(static_cast<std::size_t>(to - 4 + static_cast<long>(start)), close);
                where = to + static_cast<long>(start);
                match = true;
            }
            if (!match) {
                txt.insert(static_cast<std::size_t>(to), close);
                where = to + 4;
            }
        }
    } else if (tag == u'b' || tag == u'i') {
        // MicroDVD: PutinNonass("y:b", "Y:b") and ("y:i", "Y:i").
        const u16 find = u16(u"{y:") + tag + u"}";
        const u16 put = u16(u"{Y:") + tag + u"}";
        long wheres = findLast(subString(txt, 0, from), u'|');
        if (wheres == -1)
            wheres = 0;
        const std::size_t found = findNoCase(txt, find, static_cast<std::size_t>(wheres));
        if (found != u16::npos) {
            where = static_cast<long>(found);
            txt.erase(found, find.size());
            txt.insert(found, put);
        } else {
            txt.insert(static_cast<std::size_t>(wheres), put);
            where = wheres + static_cast<long>(put.size());
        }
    } else {
        return state; // no legacy action for this tag in this format
    }
    return EditorText{txt, where, where};
}

} // namespace hikari::core::legacy
