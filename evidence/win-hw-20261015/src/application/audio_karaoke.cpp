#include "hikari/application/audio_karaoke.h"

#include "hikari/application/audio_timing.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

namespace hikari::application {

namespace {

using u16 = std::u16string;

// wxString::Mid: past the end gives "", the count is clamped.
u16 wxMid(const u16 &s, std::size_t first, std::size_t count = u16::npos)
{
    if (first > s.size())
        return {};
    return s.substr(first, std::min(count, s.size() - first));
}

// wxString::SubString(from, to): from..to inclusive.
u16 wxSubString(const u16 &s, std::size_t from, std::size_t to)
{
    return wxMid(s, from, to - from + 1);
}

// wxString::Replace(old, new) for every occurrence, left to right.
void wxReplaceAll(u16 &s, std::u16string_view from, std::u16string_view to)
{
    u16 out;
    std::size_t at = 0;
    for (std::size_t hit; (hit = s.find(from, at)) != u16::npos; at = hit + from.size()) {
        out += s.substr(at, hit - at);
        out += to;
    }
    s = out + s.substr(at);
}

char16_t lowerAscii(char16_t c)
{
    return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

// wxString::Lower, one UTF-16 unit to one (U1-unicode-case: by Unicode, the
// classes' lower case; ASCII without one).
u16 lowered(u16 s, const KaraokeCharClass &classes)
{
    for (auto &c : s)
        c = classes.lower ? classes.lower(c) : lowerAscii(c);
    return s;
}

bool contains(std::u16string_view set, char16_t c)
{
    return set.find(c) != std::u16string_view::npos;
}

// Legacy's regex {([^}]*)\\kf?o?[0-9]([^}]*)} (wxRE_ADVANCED) on the
// lower-cased text: a \k, \kf, \ko or \kfo followed by a digit, inside a
// block that opens before it (any "{" with no "}" between) and closes after.
bool hasKaraokeTag(const u16 &low)
{
    for (std::size_t q = low.find(u"\\k"); q != u16::npos; q = low.find(u"\\k", q + 1)) {
        std::size_t r = q + 2;
        if (r < low.size() && low[r] == u'f')
            ++r;
        if (r < low.size() && low[r] == u'o')
            ++r;
        if (r >= low.size() || low[r] < u'0' || low[r] > u'9')
            continue;
        const std::size_t open = q == 0 ? u16::npos : low.find_last_of(u'{', q - 1);
        const std::size_t close = q == 0 ? u16::npos : low.find_last_of(u'}', q - 1);
        if (open == u16::npos || (close != u16::npos && close > open))
            continue;
        if (low.find(u'}', r + 1) != u16::npos)
            return true;
    }
    return false;
}

// Legacy Karaoke::GetNextChar: the next character outside {} blocks from
// `j` (a block counts from a "{" seen on the way), `j` past it; at the end
// '\t' with `j` at the end.
char16_t nextChar(std::size_t &j, const u16 &text)
{
    bool block = false;
    for (std::size_t i = j; i < text.size(); i++) {
        const char16_t ch = text[i];
        if (ch == u'{')
            block = true;
        else if (ch == u'}')
            block = false;
        else if (!block) {
            j = i + 1;
            return ch;
        }
    }
    j = text.size();
    return u'\t';
}

std::u16string number(int value)
{
    const auto text = std::to_string(value);
    return u16(text.begin(), text.end());
}

} // namespace

KaraokeCharClass KaraokeCharClass::ascii()
{
    KaraokeCharClass classes;
    classes.space = [](char16_t c) { return c == u' ' || (c >= 9 && c <= 13); };
    classes.punct = [](char16_t c) {
        return c > 32 && c < 127 && !(c >= u'0' && c <= u'9') && !(c >= u'a' && c <= u'z') && !(c >= u'A' && c <= u'Z');
    };
    classes.lower = lowerAscii;
    return classes;
}

int legacyTextExtent(const KaraokeMeasure &measure, std::u16string_view text)
{
    int x = measure ? measure(text) : 0;
    if (!text.empty() && text.front() == u' ')
        x += 4;
    if (!text.empty() && text.back() == u' ')
        x += 4;
    return x;
}

int legacyAtoi(std::u16string_view text)
{
    std::size_t i = 0;
    while (i < text.size() && (text[i] == u' ' || (text[i] >= 9 && text[i] <= 13)))
        ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == u'+' || text[i] == u'-'))
        negative = text[i++] == u'-';
    std::int64_t value = 0;
    for (; i < text.size() && text[i] >= u'0' && text[i] <= u'9'; ++i)
        value = std::min<std::int64_t>(value * 10 + (text[i] - u'0'), std::int64_t(INT_MAX) + 1);
    if (negative)
        value = -value;
    // _wtoi saturates at the int range
    return static_cast<int>(std::clamp<std::int64_t>(value, INT_MIN, INT_MAX));
}

void AudioKaraoke::clear()
{
    m_syls.clear();
    m_times.clear();
    m_tags.clear();
}

void AudioKaraoke::split(const Line &line, bool autoSplit, bool everyN, const KaraokeCharClass &classes)
{
    clear();
    u16 Text = line.text;
    const int len = static_cast<int>(Text.size());
    int stime = line.startMs;
    const u16 textlow = lowered(Text, classes);

    // A5-kara-unclosed (R3, approved): a text ending inside a \k tag left
    // legacy with fewer times (and tags) than syllables, which GetText, the
    // drawing and the mouse then read past; the missing ones end at the
    // Line's end, as a \k.
    auto pad = [&] {
        while (m_times.size() < m_syls.size())
            m_times.push_back(line.endMs);
        while (m_tags.size() < m_syls.size())
            m_tags.push_back(u"k");
    };

    if (hasKaraokeTag(textlow)) {
        Text += u'{'; // the last character's next one
        bool inBrackets = false;
        std::size_t lastStartBracket = 0;
        bool kpart = false;
        u16 res, kres;
        for (int i = 0; i < len; i++) {
            const char16_t ch = Text[static_cast<std::size_t>(i)];
            const char16_t nch = Text[static_cast<std::size_t>(i) + 1];
            if (i == len - 1) {
                res += ch;
                m_syls.push_back(res);
                if (m_times.empty()) {
                    m_times.push_back(line.endMs);
                    m_tags.push_back(u"k");
                }
                pad();
                return;
            }
            if (ch == u'{')
                inBrackets = true;
            else if (ch == u'}')
                inBrackets = false;

            if (inBrackets && ch == u'\\' && (nch == u'k' || nch == u'K')) {
                if (!m_tags.empty()) {
                    m_syls.push_back(wxMid(res, 0, lastStartBracket));
                    res = wxMid(res, lastStartBracket);
                    lastStartBracket = 0;
                }
                kpart = true;
                continue;
            } else if (kpart) {
                if ((nch == u'o' || nch == u'f') && ch == u'k') {
                    m_tags.push_back(u16(u"k") + nch);
                    continue;
                } else if (ch == u'k' || ch == u'K') {
                    m_tags.push_back(u16(1, ch));
                }
                if (nch == u'}' || nch == u'\\') {
                    // legacy int arithmetic (wraps like the 32-bit original)
                    stime = static_cast<int>(static_cast<std::uint32_t>(stime) +
                                             static_cast<std::uint32_t>(legacyAtoi(kres)) * 10u);
                    m_times.push_back(stime);
                    kres.clear();
                    kpart = false;
                    continue;
                }
                kres += nch;
            } else {
                res += ch;
                if (nch == u'{')
                    lastStartBracket = res.size();
            }
        }
        pad();
        return;
    }

    static constexpr std::u16string_view aoi = u"aeioun", aoi1 = u"aeiouy", aoi2 = u"aeiou";
    const auto spaceOrPunct = [&](char16_t c) {
        return (classes.space && classes.space(c)) || (classes.punct && classes.punct(c));
    };
    const auto space = [&](char16_t c) { return classes.space && classes.space(c); };
    const u16 low = textlow + u" X";
    // the X is never reached: it only makes the last split
    const std::size_t end = low.size() - 1;
    std::size_t start = 0, i = 0;
    while (i < end) {
        std::size_t previ = i > 0 ? i - 1 : 0;
        const char16_t pch = nextChar(previ, low);
        const char16_t ch = nextChar(i, low);
        std::size_t newi = i;
        const char16_t nch = nextChar(newi, low);
        const char16_t nnch = nextChar(newi, low);

        if ((autoSplit && (spaceOrPunct(ch) || contains(aoi, ch)) && !spaceOrPunct(nch)) ||
            (pch == u'\\' && ch == u'h') ||
            (!autoSplit && (space(ch) || (pch == u'\\' && (ch == u'h' || ch == u'n'))))) {
            if (autoSplit &&
                ((ch == u'n' && contains(aoi1, nch)) || // n before a vowel starts the next syllable
                 (contains(aoi2, ch) && nch == u'n' && (nnch == u' ' || (everyN && !contains(aoi1, nnch)))) ||
                 (nch == u'"' && ch != u' ') || // #7: a closing quote stays with its syllable
                 (nch == u'\\' && (nnch == u'n' || nnch == u'h')) || (ch == u'\\' && (nch == u'n' || nch == u'h'))))
                continue;
            m_syls.push_back(wxSubString(Text, start, i - 1));
            m_tags.push_back(u"k");
            start = i;
        }
    }
    // A5-auto-unclosed (approved): inside an unclosed "{" the closing split
    // never comes and legacy lost the rest; it is the last syllable instead
    if (!m_syls.empty() && start < Text.size()) {
        m_syls.push_back(Text.substr(start));
        m_tags.push_back(u"k");
    }
    const int dur = line.endMs - line.startMs;
    // legacy divides before it checks for no syllables (the result is unused then)
    const int times = m_syls.empty() ? 0 : static_cast<int>(float(dur) / float(m_syls.size()));
    if (m_syls.empty()) {
        m_syls.push_back(Text);
        m_tags.push_back(u"k");
    }
    for (std::size_t s = 0; s < m_syls.size(); s++) {
        if (s == m_syls.size() - 1) {
            m_times.push_back(line.endMs);
            break;
        }
        stime += times;
        m_times.push_back(legacyZeroIt(stime));
    }
}

std::u16string AudioKaraoke::text(int curStartMs) const
{
    u16 text;
    for (std::size_t i = 0; i < m_syls.size(); i++) {
        int time = i == 0 ? m_times[i] - curStartMs : m_times[i] - m_times[i - 1];
        time /= 10;
        const u16 &syl = m_syls[i];
        text += u"{\\";
        text += m_tags[i];
        text += number(time);
        if (!syl.empty() && syl[0] == u'{' && syl.find(u'}', 1) != u16::npos)
            text += wxMid(syl, 1); // After('{'): after its first "{"
        else {
            text += u'}';
            text += syl;
        }
    }
    return text;
}

bool AudioKaraoke::join(int i)
{
    // A5-join-last (R3, approved): legacy joined the last syllable with the
    // one past the end (an out-of-range read); nothing happens instead.
    if (i < 0 || static_cast<std::size_t>(i) + 1 >= m_syls.size())
        return false;
    const auto at = static_cast<std::size_t>(i);
    m_syls[at] += m_syls[at + 1];
    wxReplaceAll(m_syls[at], u"{}", u"");
    m_syls.erase(m_syls.begin() + i + 1);
    m_times[at] = m_times[at + 1];
    m_times.erase(m_times.begin() + i + 1);
    m_tags.erase(m_tags.begin() + i + 1);
    return true;
}

std::pair<std::u16string, std::u16string> AudioKaraoke::letters(int i, int nletters) const
{
    const u16 &syl = m_syls[static_cast<std::size_t>(i)];
    if (nletters == 0)
        return {u16(), syl};
    bool block = false;
    int counter = 0;
    for (std::size_t c = 0; c < syl.size(); c++) {
        const char16_t ch = syl[c];
        if (counter == nletters)
            return {wxMid(syl, 0, c), wxMid(syl, c)};
        if (ch == u'{')
            block = true;
        else if (ch == u'}')
            block = false;
        else if (!block)
            counter++;
    }
    // A5-split-last-letter (approved): legacy split at the raw position here,
    // inside the tags; the split is right after the last letter instead (all
    // the syllable before it when it has none)
    std::size_t after = syl.size();
    block = false;
    for (std::size_t c = 0; c < syl.size(); c++) {
        const char16_t ch = syl[c];
        if (ch == u'{')
            block = true;
        else if (ch == u'}')
            block = false;
        else if (!block)
            after = c + 1;
    }
    return {wxMid(syl, 0, after), wxMid(syl, after)};
}

std::u16string AudioKaraoke::stripped(int i) const
{
    const u16 &syl = m_syls[static_cast<std::size_t>(i)];
    bool block = false;
    u16 out;
    for (const char16_t ch : syl) {
        if (ch == u'{')
            block = true; // (legacy meant to remember where; it never does)
        else if (ch == u'}')
            block = false;
        else if (!block)
            out += ch;
    }
    // "made for { without }": the whole syllable again, from its start
    if (block && !syl.empty() && 0 < syl.size() - 1)
        out += syl;
    return out;
}

bool AudioKaraoke::splitSyllable(int i, int nletters, int curStartMs)
{
    if (i < 0 || i >= count() || m_syls[static_cast<std::size_t>(i)].empty())
        return false;
    auto [first, second] = letters(i, nletters);
    m_syls[static_cast<std::size_t>(i)] = std::move(first);
    m_syls.insert(m_syls.begin() + i + 1, std::move(second));
    m_tags.insert(m_tags.begin() + i + 1, u"k");
    const auto [start, end] = syllableTimes(i, curStartMs);
    m_times.insert(m_times.begin() + i, legacyZeroIt(start + ((end - start) / 2)));
    return true;
}

std::pair<int, int> AudioKaraoke::syllableTimes(int i, int curStartMs) const
{
    const int start = i < 1 ? curStartMs : m_times[static_cast<std::size_t>(i) - 1];
    return {start, m_times[static_cast<std::size_t>(i)]};
}

std::pair<int, int> AudioKaraoke::visibleTimes(int i, int curStartMs, int curEndMs) const
{
    const int start = i < 1 ? curStartMs : m_times[static_cast<std::size_t>(i) - 1];
    const int end = static_cast<std::size_t>(i) + 1 < m_times.size() ? m_times[static_cast<std::size_t>(i) + 1] : curEndMs;
    return {start, end};
}

int AudioKaraoke::boundaryAt(int x, const AudioView &view) const
{
    for (std::size_t i = 0; i < m_syls.size(); i++)
        if (std::abs(float(x) - view.xAtMs(m_times[i])) < 6)
            return static_cast<int>(i);
    return -1;
}

int AudioKaraoke::syllableAt(int x, const AudioView &view, int curStartMs) const
{
    for (std::size_t i = 0; i < m_syls.size(); i++) {
        const int from = i == 0 ? curStartMs : m_times[i - 1];
        if (float(x + 2) > view.xAtMs(from) && float(x - 2) < view.xAtMs(m_times[i]))
            return static_cast<int>(i);
    }
    return -1;
}

std::optional<std::pair<int, int>> AudioKaraoke::letterAt(int x, const AudioView &view, int curStartMs,
                                                          const KaraokeMeasure &measure) const
{
    const int syl = syllableAt(x, view, curStartMs);
    if (syl < 0)
        return std::nullopt;
    const u16 text = stripped(syl);
    int tw = legacyTextExtent(measure, text);
    auto [start, end] = syllableTimes(syl, curStartMs);
    start = static_cast<int>(view.xAtMs(start));
    end = static_cast<int>(view.xAtMs(end));
    int center = start + (((end - start) - tw) / 2);
    for (std::size_t i = 0; i < text.size(); i++) {
        tw = legacyTextExtent(measure, std::u16string_view(text).substr(i, 1));
        center += tw / 2;
        if (x < center)
            return std::pair(syl, static_cast<int>(i));
        center += tw / 2;
    }
    return std::pair(syl, static_cast<int>(text.size()));
}

int legacyKaraokeZoom(bool on, int slider, int &lastHorizontalZoom)
{
    if (on) {
        lastHorizontalZoom = slider;
        return std::max(lastHorizontalZoom - 20, 30);
    }
    if (lastHorizontalZoom > -1)
        return lastHorizontalZoom;
    return std::min(slider + 20, 70);
}

KaraokeMeasure karaokeLabelMeasure(const AudioTextWidth &textWidth)
{
    return [textWidth](std::u16string_view text) {
        const auto first = text.find_first_not_of(u' ');
        if (first == std::u16string_view::npos || !textWidth)
            return 0;
        const auto last = text.find_last_not_of(u' ');
        const auto utf8 = core::toUtf8(text.substr(first, last - first + 1));
        return textWidth(AudioShape::Font::Label,
                         std::string_view(reinterpret_cast<const char *>(utf8.data()), utf8.size()));
    };
}

void karaokeShapes(std::vector<AudioShape> &out, const AudioView &view, std::int64_t lineStart,
                   const AudioKaraokeMarks &marks, const KaraokeMeasure &measure, int textHeight,
                   std::uint32_t boundaryColour, std::uint32_t textColour)
{
    const int h = view.height();
    auto line = [&](float x1, float y1, float x2, float y2, std::uint32_t colour, float width) {
        AudioShape s;
        s.kind = AudioShape::Kind::Line;
        s.colour = colour;
        s.x1 = x1;
        s.y1 = y1;
        s.x2 = x2;
        s.y2 = y2;
        s.width = width;
        out.push_back(std::move(s));
    };
    int karstart = static_cast<int>(lineStart);
    for (std::size_t j = 0; j < marks.times.size(); j++) {
        const u16 acsyl = j < marks.stripped.size() ? marks.stripped[j] : u16();
        int fw = 0, fh = 0;
        if (!acsyl.empty()) {
            fw = legacyTextExtent(measure, acsyl);
            fh = textHeight;
        }
        const float XX = view.xAtMs(marks.times[j]);
        if (XX >= 0)
            line(XX, 0, XX, float(h), boundaryColour, 1);
        if (fh != 0) {
            const int center = static_cast<int>(((XX - karstart) - fw) / 2);
            const float barY = float(fh / 2 + 1);
            line(float(center + karstart - 1), barY, float(center + karstart + fw + 2), barY, boundaryColour, float(fh));
            AudioShape label;
            label.kind = AudioShape::Kind::Text;
            label.colour = textColour;
            label.x1 = float(center + karstart);
            label.y1 = 0;
            label.x2 = float(center + karstart + fw);
            label.y2 = float(fh);
            const auto utf8 = core::toUtf8(acsyl);
            label.text.assign(reinterpret_cast<const char *>(utf8.data()), utf8.size());
            label.font = AudioShape::Font::Label;
            label.align = AudioShape::Align::TopLeft;
            out.push_back(std::move(label));
            // the hovered letter's mark
            if (marks.character >= 0 && marks.hover >= 0 && static_cast<std::size_t>(marks.hover) == j) {
                const int fwl = marks.character == 0
                                    ? 0
                                    : legacyTextExtent(measure, wxMid(acsyl, 0, static_cast<std::size_t>(marks.character)));
                const int from = j == 0 ? marks.curStartMs : marks.times[j - 1];
                const int start = static_cast<int>(view.xAtMs(from));
                const int end = static_cast<int>(view.xAtMs(marks.times[j]));
                const int c = start + ((end - start - fw) / 2);
                line(float(c + fwl), 1, float(c + fwl), float(fh), textColour, 1);
            }
        }
        // the current syllable's frame
        if (static_cast<int>(j) == marks.current) {
            const float l = float(karstart + 2), r = XX - 2, t = 1, b = float(h - 2);
            line(l, t, l, b, textColour, 1);
            line(l, b, r, b, textColour, 1);
            line(r, b, r, t, textColour, 1);
            line(r, t, l, t, textColour, 1);
        }
        karstart = static_cast<int>(XX);
    }
}

} // namespace hikari::application
