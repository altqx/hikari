#include "hikari/application/audio_karaoke.h"

#include "hikari/application/audio_timing.h"
#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace hikari::application {

namespace {

using u16 = std::u16string;

u16 wxMid(const u16 &s, std::size_t first, std::size_t count = u16::npos)
{
    if (first > s.size())
        return {};
    return s.substr(first, std::min(count, s.size() - first));
}

// from..to inclusive.
u16 wxSubString(const u16 &s, std::size_t from, std::size_t to)
{
    return wxMid(s, from, to - from + 1);
}

char16_t lowerAscii(char16_t c)
{
    return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c - u'A' + u'a') : c;
}

u16 lowered(u16 s, const KaraokeCharClass &classes)
{
    for (auto &c : s)
        c = classes.lower ? classes.lower(c) : lowerAscii(c);
    return s;
}

// A \k, \kf, \ko or \kfo and a digit inside a block that closes after it.
bool hasKaraokeTag(const u16 &low)
{
    const std::size_t lastClose = low.find_last_of(u'}');
    if (lastClose == u16::npos)
        return false;
    bool open = false;
    for (std::size_t q = 0; q < low.size(); q++) {
        if (low[q] == u'{')
            open = true;
        else if (low[q] == u'}')
            open = false;
        if (!open || low[q] != u'\\' || q + 1 >= low.size() || low[q + 1] != u'k')
            continue;
        std::size_t r = q + 2;
        if (r < low.size() && low[r] == u'f')
            ++r;
        if (r < low.size() && low[r] == u'o')
            ++r;
        if (r < low.size() && low[r] >= u'0' && low[r] <= u'9' && lastClose > r)
            return true;
    }
    return false;
}

// The next character outside {} blocks, `j` past it; '\t' at the end.
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

// The grapheme is read without the blocks in it, so a{\b1}\u0301 is one letter.
std::size_t letterEnd(std::u16string_view text, std::size_t at)
{
    if (text[at] == u'\\' && at + 1 < text.size() && (text[at + 1] == u'N' || text[at + 1] == u'n' || text[at + 1] == u'h'))
        return at + 2;
    const auto begin = text.begin();
    std::size_t end = static_cast<std::size_t>(
        std::find(begin + static_cast<std::ptrdiff_t>(at) + 1, begin + static_cast<std::ptrdiff_t>(core::graphemeEnd(text, at)), u'{') -
        begin);
    if (end == text.size() || text[end] != u'{')
        return end;
    constexpr int kBlocks = 16;
    u16 seen(text.substr(at, end - at));
    std::size_t window = 64;
    int blocks = 0;
    while (end < text.size() && text[end] == u'{') {
        std::size_t from = end;
        while (from < text.size() && text[from] == u'{') {
            const auto shut = text.find(u'}', from + 1);
            if (shut == u16::npos || ++blocks > kBlocks)
                return end; // an unclosed "{" is text
            from = shut + 1;
        }
        u16 probe;
        std::size_t length = 0;
        for (;; window *= 2) {
            const auto first = begin + static_cast<std::ptrdiff_t>(from);
            const auto last = begin + static_cast<std::ptrdiff_t>(std::min(text.size(), from + window));
            const auto stop = std::find(first, last, u'{');
            probe = seen + u16(first, stop);
            length = core::graphemeEnd(probe, 0);
            if (stop != last || last == text.end() || length + 2 <= probe.size())
                break;
        }
        if (length <= seen.size())
            return end;
        end = from + (length - seen.size());
        if (length < probe.size())
            return end;
        seen = std::move(probe);
    }
    return end;
}

// The lone "#" or "＃" outside blocks; none for any other syllable.
std::optional<std::size_t> hashIn(const u16 &syl)
{
    std::optional<std::size_t> at;
    bool block = false;
    for (std::size_t c = 0; c < syl.size(); ++c) {
        if (syl[c] == u'{')
            block = true;
        else if (syl[c] == u'}')
            block = false;
        else if (!block) {
            if (at || (syl[c] != u'#' && syl[c] != u'＃'))
                return std::nullopt;
            at = c;
        }
    }
    return block ? std::nullopt : at;
}

}

AudioKaraoke::Place AudioKaraoke::newPlace(const std::u16string &syl)
{
    // into a block the syllable starts with, else a block of its own
    Place place;
    place.at = !syl.empty() && syl[0] == u'{' && syl.find(u'}', 1) != u16::npos ? 1 : 0;
    return place;
}

void AudioKaraoke::insertSyllable(int at, std::u16string text, int endMs)
{
    m_places.insert(m_places.begin() + at, newPlace(text));
    m_syls.insert(m_syls.begin() + at, std::move(text));
    m_tags.insert(m_tags.begin() + at, u"k");
    m_times.insert(m_times.begin() + at, endMs);
}

void AudioKaraoke::eraseSyllable(int at)
{
    m_syls.erase(m_syls.begin() + at);
    m_tags.erase(m_tags.begin() + at);
    m_times.erase(m_times.begin() + at);
    m_places.erase(m_places.begin() + at);
}

KaraokeCharClass KaraokeCharClass::ascii()
{
    KaraokeCharClass classes;
    classes.space = [](char16_t c) { return c == u' ' || (c >= 9 && c <= 13); };
    classes.punct = [](char32_t c) {
        return c > 32 && c < 127 && !(c >= u'0' && c <= u'9') && !(c >= u'a' && c <= u'z') && !(c >= u'A' && c <= u'Z');
    };
    classes.symbol = [](char32_t c) { return std::u32string_view(U"$+<=>^`|~").find(c) != std::u32string_view::npos; };
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
    // saturates like _wtoi
    return static_cast<int>(std::clamp<std::int64_t>(value, INT_MIN, INT_MAX));
}

void AudioKaraoke::clear()
{
    m_syls.clear();
    m_times.clear();
    m_tags.clear();
    m_places.clear();
}

void AudioKaraoke::split(const Line &line, bool autoSplit, const KaraokeSplitRules &rules,
                         const KaraokeCharClass &classes)
{
    clear();
    m_classes = classes;
    hover = -1;
    character = -1;
    const u16 &Text = line.text;
    const u16 textlow = lowered(Text, classes);

    if (hasKaraokeTag(textlow)) {
        splitTags(line);
        return;
    }

    const auto starts = autoSplit ? karaokeAutoSplit(Text, rules, classes) : std::vector<std::size_t>();
    if (!starts.empty()) {
        for (std::size_t s = 0; s < starts.size(); s++)
            m_syls.push_back(Text.substr(starts[s], s + 1 < starts.size() ? starts[s + 1] - starts[s] : u16::npos));
    } else {
        const auto space = [&](char16_t c) { return classes.space && classes.space(c); };
        // MUST KILL: sentinel " X" appended only to force the last split
        const u16 low = textlow + u" X";
        const std::size_t end = low.size() - 1;
        std::size_t start = 0, i = 0;
        while (i < end) {
            std::size_t previ = i > 0 ? i - 1 : 0;
            const char16_t pch = nextChar(previ, low);
            const char16_t ch = nextChar(i, low);
            if ((pch == u'\\' && ch == u'h') || space(ch) || (pch == u'\\' && (ch == u'h' || ch == u'n'))) {
                m_syls.push_back(wxSubString(Text, start, i - 1));
                start = i;
            }
        }
        if (!m_syls.empty() && start < Text.size())
            m_syls.push_back(Text.substr(start));
        if (m_syls.empty())
            m_syls.push_back(Text);
    }
    std::vector<std::int64_t> weights;
    std::int64_t total = 0;
    for (const auto &syl : m_syls) {
        weights.push_back(std::max(1, karaokeWeight(syl, classes)));
        total += weights.back();
    }
    const std::int64_t dur = std::int64_t(line.endMs) - line.startMs;
    std::int64_t before = 0;
    for (std::size_t s = 0; s < m_syls.size(); s++) {
        m_tags.push_back(u"k");
        m_places.push_back(newPlace(m_syls[s]));
        if (s == m_syls.size() - 1) {
            m_times.push_back(line.endMs);
            break;
        }
        before += weights[s];
        m_times.push_back(legacyZeroIt(static_cast<int>(line.startMs + dur * before / total)));
    }
}

void AudioKaraoke::splitTags(const Line &line, std::vector<std::u16string> *carried)
{
    clear();
    if (carried)
        m_tags = std::exchange(*carried, {});
    u16 Text = line.text;
    const int len = static_cast<int>(Text.size());
    int stime = line.startMs;

    auto pad = [&] {
        while (m_times.size() < m_syls.size())
            m_times.push_back(line.endMs);
        while (m_tags.size() < m_syls.size())
            m_tags.push_back(u"k");
        while (m_places.size() < m_syls.size())
            m_places.push_back(newPlace(m_syls[m_places.size()]));
        m_places.resize(m_syls.size());
    };
    auto carry = [&] {
        if (carried && m_tags.size() > m_syls.size()) {
            carried->assign(std::make_move_iterator(m_tags.begin() + static_cast<std::ptrdiff_t>(m_syls.size())),
                            std::make_move_iterator(m_tags.end()));
            m_tags.resize(m_syls.size());
        }
    };

    // MUST KILL: sentinel "{" appended so Text[i + 1] stays in range
    Text += u'{';
    bool inBrackets = false;
    std::size_t lastStartBracket = 0;
    bool kpart = false;
    bool tagged = false;
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
            carry();
            return;
        }
        if (ch == u'{')
            inBrackets = true;
        else if (ch == u'}')
            inBrackets = false;

        // \kt sets an absolute start; it is no syllable
        const bool kt = nch == u'k' && static_cast<std::size_t>(i) + 2 < Text.size() &&
                        Text[static_cast<std::size_t>(i) + 2] == u't';
        if (inBrackets && ch == u'\\' && (nch == u'k' || nch == u'K') && !kt) {
            if (tagged) {
                m_syls.push_back(wxMid(res, 0, lastStartBracket));
                res.erase(0, std::min(lastStartBracket, res.size()));
                lastStartBracket = 0;
            }
            tagged = true;
            Place place;
            place.at = res.size();
            m_places.push_back(std::move(place));
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
                // MUST KILL: unsigned casts emulate 32-bit int wraparound
                stime = static_cast<int>(static_cast<std::uint32_t>(stime) +
                                         static_cast<std::uint32_t>(legacyAtoi(kres)) * 10u);
                m_times.push_back(stime);
                if (!m_places.empty()) {
                    m_places.back().number = kres;
                    m_places.back().cs = legacyAtoi(kres);
                }
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
    carry();
}

void AudioKaraoke::assign(std::vector<std::u16string> syllables, std::vector<std::u16string> tags,
                          std::vector<int> times)
{
    m_syls = std::move(syllables);
    m_tags = std::move(tags);
    m_times = std::move(times);
    m_places.clear();
    for (const auto &syl : m_syls)
        m_places.push_back(newPlace(syl));
    current = 0;
    hover = -1;
    character = -1;
}

std::u16string AudioKaraoke::text(int curStartMs) const
{
    const auto placeOf = [&](std::size_t i) {
        return i < m_places.size() ? m_places[i] : newPlace(m_syls[i]);
    };
    const auto placeAt = [&](std::size_t i) { return i < m_places.size() ? m_places[i].at : newPlace(m_syls[i]).at; };
    const auto tagOf = [&](std::size_t i) {
        int time = i == 0 ? m_times[i] - curStartMs : m_times[i] - m_times[i - 1];
        time /= 10;
        const Place place = placeOf(i);
        return u"\\" + m_tags[i] + (place.cs == time && !place.number.empty() ? place.number : number(time));
    };
    u16 all;
    std::vector<std::pair<std::size_t, std::size_t>> order; // place, syllable
    bool ordered = true;
    for (std::size_t i = 0, last = 0; i < m_syls.size(); i++) {
        const std::size_t wanted = all.size() + placeAt(i);
        ordered = ordered && wanted >= last;
        last = wanted;
        all += m_syls[i];
    }
    if (!ordered) {
        for (std::size_t i = 0, base = 0; i < m_syls.size(); base += m_syls[i++].size())
            order.emplace_back(base + placeAt(i), i);
        std::ranges::stable_sort(order, {}, &std::pair<std::size_t, std::size_t>::first);
    }
    std::size_t scanned = 0, open = u16::npos, shut = u16::npos;
    const auto scanTo = [&](std::size_t at) {
        for (; scanned < at; scanned++)
            if (all[scanned] == u'{')
                open = scanned;
            else if (all[scanned] == u'}')
                shut = scanned;
    };
    const std::size_t lastShut = all.rfind(u'}');
    u16 text;
    std::size_t from = 0, base = 0;
    for (std::size_t k = 0; k < m_syls.size(); k++) {
        const std::size_t i = ordered ? k : order[k].second;
        const std::size_t wanted = ordered ? base + placeAt(i) : order[k].first;
        base += m_syls[k].size();
        const u16 tag = tagOf(i);
        std::size_t at = std::clamp(wanted, from, all.size());
        scanTo(at);
        bool inside = open != u16::npos && (shut == u16::npos || shut < open);
        if (inside && (lastShut == u16::npos || lastShut < at)) {
            inside = false; // an unclosed "{" is text
            at = std::max(open, from);
        }
        text.append(all, from, at - from);
        if (inside)
            text += tag;
        else
            text += u"{" + tag + u"}";
        from = at;
    }
    text.append(all, from);
    return text;
}

bool AudioKaraoke::join(int i)
{
    if (i < 0 || static_cast<std::size_t>(i) + 1 >= m_syls.size())
        return false;
    const auto at = static_cast<std::size_t>(i);
    u16 joined = m_syls[at] + m_syls[at + 1];
    std::size_t place = m_places[at].at, removed = 0;
    const auto second = hashIn(m_syls[at + 1]);
    const auto first = second ? std::nullopt : hashIn(m_syls[at]);
    if (const auto drop = second ? std::optional(m_syls[at].size() + *second) : first) {
        joined.erase(*drop, 1);
        if (place > *drop)
            --place;
    }
    u16 out;
    std::optional<std::size_t> moved;
    for (std::size_t c = 0; c < joined.size();) {
        if (joined[c] == u'{' && c + 1 < joined.size() && joined[c + 1] == u'}') {
            if (place == c + 1)
                moved = c - removed;
            else if (place >= c + 2)
                removed += 2;
            c += 2;
            continue;
        }
        out += joined[c++];
    }
    m_places[at].at = moved ? *moved : place - removed;
    m_syls[at] = std::move(out);
    m_times[at] = m_times[at + 1];
    m_syls.erase(m_syls.begin() + i + 1);
    m_times.erase(m_times.begin() + i + 1);
    m_tags.erase(m_tags.begin() + i + 1);
    m_places.erase(m_places.begin() + i + 1);
    return true;
}

std::pair<std::u16string, std::u16string> AudioKaraoke::letters(int i, int nletters) const
{
    const u16 &syl = m_syls[static_cast<std::size_t>(i)];
    if (nletters == 0)
        return {u16(), syl};
    bool block = false;
    int counter = 0;
    for (std::size_t c = 0; c < syl.size();) {
        const char16_t ch = syl[c];
        if (counter == nletters)
            return {wxMid(syl, 0, c), wxMid(syl, c)};
        if (ch == u'{') {
            block = true;
        } else if (ch == u'}') {
            block = false;
        } else if (!block) {
            counter++;
            c = letterEnd(syl, c);
            continue;
        }
        ++c;
    }
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
            block = true;
        else if (ch == u'}')
            block = false;
        else if (!block)
            out += ch;
    }
    // MUST KILL: an unclosed "{" appends the whole syllable again
    if (block && !syl.empty() && 0 < syl.size() - 1)
        out += syl;
    return out;
}

// A tag read after the leading text the cut passes goes to the front.
void AudioKaraoke::cutSyllable(int i, u16 first, u16 second, int boundary)
{
    const auto at = static_cast<std::size_t>(i);
    if (m_places[at].at >= first.size())
        m_places[at].at = 0;
    m_syls[at] = std::move(first);
    insertSyllable(i + 1, std::move(second), m_times[at]);
    m_times[at] = boundary;
}

bool AudioKaraoke::splitSyllable(int i, int nletters, int curStartMs)
{
    if (i < 0 || i >= count() || m_syls[static_cast<std::size_t>(i)].empty())
        return false;
    auto [first, second] = letters(i, nletters);
    const auto [start, end] = syllableTimes(i, curStartMs);
    const std::int64_t w1 = std::max(1, karaokeWeight(first, m_classes));
    const std::int64_t w2 = std::max(1, karaokeWeight(second, m_classes));
    const int boundary = legacyZeroIt(start + static_cast<int>((std::int64_t(end) - start) * w1 / (w1 + w2)));
    cutSyllable(i, std::move(first), std::move(second), boundary);
    return true;
}

bool AudioKaraoke::splitAt(int i, int ms, int curStartMs)
{
    if (i < 0 || i >= count())
        return false;
    const auto [start, end] = syllableTimes(i, curStartMs);
    const int boundary = legacyZeroIt(ms);
    if (boundary <= start || boundary >= end)
        return false;
    const u16 text = stripped(i);
    std::size_t at = 0, next = 0;
    const auto weighs = [&] {
        next = letterEnd(text, at);
        return karaokeWeight(std::u16string_view(text).substr(at, next - at), m_classes) > 0 ? 1 : 0;
    };
    int count = 0, total = 0;
    for (; at < text.size(); at = next, count++)
        total += weighs();
    const auto target =
        static_cast<int>((std::int64_t(total) * (boundary - start) * 2 + (std::int64_t(end) - start)) /
                         ((std::int64_t(end) - start) * 2));
    int nletters = 0;
    if (target >= total) {
        nletters = count;
    } else if (target > 0) {
        int seen = 0;
        for (at = 0; nletters < count && seen < target; at = next, nletters++)
            seen += weighs();
        for (; nletters < count && weighs() == 0; at = next)
            ++nletters;
    }
    auto [first, second] = letters(i, nletters);
    cutSyllable(i, std::move(first), std::move(second), boundary);
    return true;
}

void AudioKaraoke::moveStart(int oldStartMs, int newStartMs)
{
    if (m_syls.empty() || oldStartMs == newStartMs)
        return;
    const auto lead = [&](int i) { return stripped(i).empty(); };
    if (newStartMs < oldStartMs) {
        if (!lead(0)) {
            insertSyllable(0, u16(), oldStartMs);
            ++current;
        }
        return;
    }
    const int leadBefore = m_times.front() - oldStartMs;
    for (auto &time : m_times)
        time = std::max(time, newStartMs);
    if (m_syls.size() > 1 && lead(0) && leadBefore > 0 && m_times.front() == newStartMs) {
        eraseSyllable(0);
        current = std::max(0, current - 1);
    }
}

bool AudioKaraoke::switchLongToKf(int thresholdMs, int curStartMs)
{
    std::vector<std::size_t> longOnes;
    bool anyK = false;
    for (int i = 0; i < count(); i++) {
        const auto [start, end] = syllableTimes(i, curStartMs);
        const auto &tag = m_tags[static_cast<std::size_t>(i)];
        if (end - start >= thresholdMs && (tag == u"k" || tag == u"kf")) {
            longOnes.push_back(static_cast<std::size_t>(i));
            anyK = anyK || tag == u"k";
        }
    }
    for (const std::size_t i : longOnes)
        m_tags[i] = anyK ? u"kf" : u"k";
    return !longOnes.empty();
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
    int letter = 0;
    for (std::size_t at = 0; at < text.size(); letter++) {
        const std::size_t end = letterEnd(text, at);
        tw = legacyTextExtent(measure, std::u16string_view(text).substr(at, end - at));
        center += tw / 2;
        if (x < center)
            return std::pair(syl, letter);
        center += tw / 2;
        at = end;
    }
    return std::pair(syl, letter);
}

std::size_t karaokeLetterEnd(std::u16string_view text, std::size_t at)
{
    return at < text.size() ? letterEnd(text, at) : text.size();
}

std::size_t karaokeGraphemePrefix(std::u16string_view text, int graphemes)
{
    std::size_t at = 0;
    for (int g = 0; g < graphemes && at < text.size(); g++)
        at = letterEnd(text, at);
    return at;
}

namespace {

// Past `tags` \k tags, at the next "{" a \k tag opens; nullopt past `most` tags.
std::optional<std::size_t> karaokePieceEnd(std::u16string_view text, std::size_t from, int tags, std::int64_t most)
{
    const auto tagAt = [&](std::size_t i) {
        return i + 1 < text.size() && text[i] == u'\\' && (text[i + 1] == u'k' || text[i + 1] == u'K') &&
               !(text[i + 1] == u'k' && i + 2 < text.size() && text[i + 2] == u't');
    };
    bool inBrackets = false;
    std::int64_t seen = 0;
    for (std::size_t i = from; i < text.size(); i++) {
        if (text[i] == u'{') {
            if (!inBrackets && i > from && seen >= tags && tagAt(i + 1))
                return i;
            inBrackets = true;
        } else if (text[i] == u'}') {
            inBrackets = false;
        } else if (inBrackets && tagAt(i)) {
            if (++seen > most)
                return std::nullopt;
        }
    }
    return text.size();
}

}

std::optional<std::u16string> karaokeKeepSyllables(std::u16string_view text, int oldStartMs, int newStartMs,
                                                   int endMs, int pieceTags)
{
    if (oldStartMs == newStartMs || !hasKaraokeTag(lowered(u16(text), KaraokeCharClass::ascii())))
        return std::nullopt;
    if (const auto open = text.rfind(u'{'); open != std::u16string_view::npos && text.find(u'}', open) == text.npos) {
        const std::u16string_view rest = text.substr(open);
        for (std::size_t i = rest.find(u'\\'); i != rest.npos; i = rest.find(u'\\', i + 1))
            if (i + 1 < rest.size() && (rest[i + 1] == u'k' || rest[i + 1] == u'K'))
                return std::nullopt;
    }
    // MUST KILL: at least 2 tags so the first piece's lead syllable is never its only one
    pieceTags = std::max(pieceTags, 2);
    const std::int64_t most = std::int64_t(std::max(pieceTags, 1024)) * 16;
    std::u16string out;
    out.reserve(text.size());
    int read = oldStartMs;
    int written = newStartMs;
    std::vector<u16> carried;
    for (std::size_t from = 0; from < text.size();) {
        const auto end = karaokePieceEnd(text, from, pieceTags, most);
        if (!end)
            return std::nullopt;
        const std::size_t to = *end;
        AudioKaraoke karaoke;
        karaoke.splitTags({u16(text.substr(from, to - from)), read, endMs}, &carried);
        read = karaoke.times().back();
        if (from == 0)
            karaoke.moveStart(oldStartMs, newStartMs);
        else
            for (auto &time : karaoke.times())
                time = std::max(time, newStartMs);
        out += karaoke.text(written);
        written = karaoke.times().back();
        from = to;
    }
    return out;
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
            if (marks.character >= 0 && marks.hover >= 0 && static_cast<std::size_t>(marks.hover) == j) {
                const int fwl = marks.character == 0
                                    ? 0
                                    : legacyTextExtent(measure, wxMid(acsyl, 0, karaokeGraphemePrefix(acsyl, marks.character)));
                const int from = j == 0 ? marks.curStartMs : marks.times[j - 1];
                const int start = static_cast<int>(view.xAtMs(from));
                const int end = static_cast<int>(view.xAtMs(marks.times[j]));
                const int c = start + ((end - start - fw) / 2);
                line(float(c + fwl), 1, float(c + fwl), float(fh), textColour, 1);
            }
        }
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

}
