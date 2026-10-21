#include "hikari/application/audio_karaoke.h"

#include "hikari/application/audio_timing.h"
#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

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

// Legacy's regex {([^}]*)\\kf?o?[0-9]([^}]*)} (wxRE_ADVANCED) on the
// lower-cased text: a \k, \kf, \ko or \kfo followed by a digit, inside a
// block that opens before it (any "{" with no "}" between) and closes after.
bool hasKaraokeTag(const u16 &low)
{
    // one pass (KA1: a long unclosed block made the backward searches
    // quadratic): whether a "{" is open here, and where the last "}" is
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

// KA1-grapheme-letters: where the letter at `at` ends: a whole grapheme, or
// \N \n \h as one. The grapheme is read without the blocks in it, so one
// that goes on after a block (a{\b1}\u0301) takes the block in and no split
// comes between a letter and its marks.
std::size_t letterEnd(std::u16string_view text, std::size_t at)
{
    if (text[at] == u'\\' && at + 1 < text.size() && (text[at + 1] == u'N' || text[at + 1] == u'n' || text[at + 1] == u'h'))
        return at + 2;
    // the grapheme up to a block (searched for only within it: long Lines stay linear)
    const auto begin = text.begin();
    std::size_t end = static_cast<std::size_t>(
        std::find(begin + static_cast<std::ptrdiff_t>(at) + 1, begin + static_cast<std::ptrdiff_t>(core::graphemeEnd(text, at)), u'{') -
        begin);
    if (end == text.size() || text[end] != u'{')
        return end;
    // it goes on past the block when the text after it continues it
    u16 seen(text.substr(at, end - at)); // the grapheme so far, without its blocks
    std::size_t window = 64;
    while (end < text.size() && text[end] == u'{') {
        std::size_t from = end; // past the block, and any right after it
        while (from < text.size() && text[from] == u'{') {
            const auto shut = text.find(u'}', from + 1);
            if (shut == u16::npos)
                return end; // an unclosed "{" is text, and no mark
            from = shut + 1;
        }
        // the text after them up to the next block, a window of it at a time,
        // wider while the grapheme comes too near its end to be sure of it
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
            return end; // it ends at the block
        end = from + (length - seen.size());
        if (length < probe.size())
            return end;
        seen = std::move(probe); // on to the next block
    }
    return end;
}

// The letters of a stripped text, each one's [begin, end).
std::vector<std::pair<std::size_t, std::size_t>> letterSpans(std::u16string_view text)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t end = letterEnd(text, i);
        out.emplace_back(i, end);
        i = end;
    }
    return out;
}

} // namespace

AudioKaraoke::Place AudioKaraoke::newPlace(const std::u16string &syl)
{
    // legacy GetText: into a block the syllable starts with, else a block of its own
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
    m_places.clear();
}

void AudioKaraoke::split(const Line &line, bool autoSplit, const KaraokeSplitRules &rules,
                         const KaraokeCharClass &classes)
{
    clear();
    m_classes = classes;
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
        while (m_places.size() < m_syls.size())
            m_places.push_back(newPlace(m_syls[m_places.size()]));
        m_places.resize(m_syls.size());
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

            // KA1-kt-tag: \kt (an absolute start) is no syllable; legacy read it as \k0
            const bool kt = nch == u'k' && static_cast<std::size_t>(i) + 2 < Text.size() &&
                            Text[static_cast<std::size_t>(i) + 2] == u't';
            if (inBrackets && ch == u'\\' && (nch == u'k' || nch == u'K') && !kt) {
                if (!m_tags.empty()) {
                    m_syls.push_back(wxMid(res, 0, lastStartBracket));
                    res = wxMid(res, lastStartBracket);
                    lastStartBracket = 0;
                }
                // KA1-k-round-trip: the tag goes back here
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
                    // legacy int arithmetic (wraps like the 32-bit original)
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
        return;
    }

    if (autoSplit) {
        // KA1-auto-split-language
        const auto starts = karaokeAutoSplit(Text, rules, classes);
        for (std::size_t s = 0; s < starts.size(); s++)
            m_syls.push_back(Text.substr(starts[s], s + 1 < starts.size() ? starts[s + 1] - starts[s] : u16::npos));
    } else {
        const auto space = [&](char16_t c) { return classes.space && classes.space(c); };
        const u16 low = textlow + u" X";
        // the X is never reached: it only makes the last split
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
        // A5-auto-unclosed (approved): inside an unclosed "{" the closing split
        // never comes and legacy lost the rest; it is the last syllable instead
        if (!m_syls.empty() && start < Text.size())
            m_syls.push_back(Text.substr(start));
        if (m_syls.empty())
            m_syls.push_back(Text);
    }
    // KA1-time-by-character: the Line's duration shared out by the
    // syllables' characters (legacy: an equal share each)
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

std::u16string AudioKaraoke::text(int curStartMs) const
{
    // KA1-k-round-trip: the syllables' texts in a row, each tag put back
    // where it was read (legacy: always first in the syllable's first block)
    u16 all;
    std::vector<std::pair<std::size_t, u16>> tags;
    for (std::size_t i = 0; i < m_syls.size(); i++) {
        int time = i == 0 ? m_times[i] - curStartMs : m_times[i] - m_times[i - 1];
        time /= 10;
        const Place place = i < m_places.size() ? m_places[i] : newPlace(m_syls[i]);
        tags.emplace_back(all.size() + place.at,
                          u"\\" + m_tags[i] + (place.cs == time && !place.number.empty() ? place.number : number(time)));
        all += m_syls[i];
    }
    std::ranges::stable_sort(tags, {}, &std::pair<std::size_t, u16>::first);
    // where the "{" and "}" are, read in one pass: the last of each before a
    // place is a binary search (Codex: a search for each tag made long
    // blocks of tags quadratic, and a table for every unit cost memory)
    std::vector<std::size_t> opens, shuts;
    for (std::size_t p = 0; p < all.size(); p++)
        if (all[p] == u'{')
            opens.push_back(p);
        else if (all[p] == u'}')
            shuts.push_back(p);
    const auto lastBefore = [](const std::vector<std::size_t> &at, std::size_t place) {
        const auto it = std::ranges::lower_bound(at, place);
        return it == at.begin() ? u16::npos : *std::prev(it);
    };
    const std::size_t lastShut = shuts.empty() ? u16::npos : shuts.back();
    u16 text;
    std::size_t from = 0;
    for (auto &[wanted, tag] : tags) {
        std::size_t at = std::clamp(wanted, from, all.size());
        // into the block it is in, else a block of its own
        const auto open = lastBefore(opens, at);
        const auto shut = lastBefore(shuts, at);
        bool inside = open != u16::npos && (shut == u16::npos || shut < open);
        if (inside && (lastShut == u16::npos || lastShut < at)) {
            inside = false; // an unclosed "{" is text: the tag goes before it (A5-kara-unclosed)
            at = std::max(open, from);
        }
        text += all.substr(from, at - from);
        if (inside)
            text += tag;
        else
            text += u"{" + tag + u"}";
        from = at;
    }
    text += all.substr(from);
    return text;
}

bool AudioKaraoke::join(int i)
{
    // A5-join-last (R3, approved): legacy joined the last syllable with the
    // one past the end (an out-of-range read); nothing happens instead.
    if (i < 0 || static_cast<std::size_t>(i) + 1 >= m_syls.size())
        return false;
    const auto at = static_cast<std::size_t>(i);
    const u16 joined = m_syls[at] + m_syls[at + 1];
    // every "{}" removed (wxString::Replace); the tag's place moves with the
    // text before it, and to where an emptied block was when it was in it
    u16 out;
    std::size_t place = m_places[at].at, removed = 0;
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
            c = letterEnd(syl, c); // KA1-grapheme-letters
            continue;
        }
        ++c;
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

// Syllable `i` cut into `first` (ending at `boundary`) and `second`. A tag
// read after leading text the cut passes goes to the front, as legacy's.
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
    // KA1-time-by-character (legacy: half way)
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
    // the letter: the share of its characters (spaces and punctuation stay
    // with the letter before them)
    const u16 text = stripped(i);
    const auto spans = letterSpans(text);
    std::vector<int> weights;
    int total = 0;
    for (const auto &[from, to] : spans) {
        weights.push_back(karaokeWeight(std::u16string_view(text).substr(from, to - from), m_classes) > 0 ? 1 : 0);
        total += weights.back();
    }
    const auto target =
        static_cast<int>((std::int64_t(total) * (boundary - start) * 2 + (std::int64_t(end) - start)) /
                         ((std::int64_t(end) - start) * 2)); // rounded
    int nletters = 0;
    if (target >= total) {
        nletters = static_cast<int>(spans.size());
    } else if (target > 0) {
        int seen = 0;
        while (nletters < static_cast<int>(spans.size()) && seen < target)
            seen += weights[static_cast<std::size_t>(nletters++)];
        while (nletters < static_cast<int>(spans.size()) && weights[static_cast<std::size_t>(nletters)] == 0)
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
        if (!lead(0)) { // an empty lead syllable before the first one; one there grows
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
    // KA1-grapheme-letters: legacy measured each UTF-16 unit
    const auto spans = letterSpans(text);
    for (std::size_t i = 0; i < spans.size(); i++) {
        tw = legacyTextExtent(measure, std::u16string_view(text).substr(spans[i].first, spans[i].second - spans[i].first));
        center += tw / 2;
        if (x < center)
            return std::pair(syl, static_cast<int>(i));
        center += tw / 2;
    }
    return std::pair(syl, static_cast<int>(spans.size()));
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

std::optional<std::u16string> karaokeKeepSyllables(std::u16string_view text, int oldStartMs, int newStartMs)
{
    if (oldStartMs == newStartMs || !hasKaraokeTag(lowered(u16(text), KaraokeCharClass::ascii())))
        return std::nullopt;
    AudioKaraoke karaoke;
    karaoke.split({u16(text), oldStartMs, oldStartMs}, false, {});
    karaoke.moveStart(oldStartMs, newStartMs);
    return karaoke.text(newStartMs);
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
                                    : legacyTextExtent(measure, wxMid(acsyl, 0, karaokeGraphemePrefix(acsyl, marks.character)));
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
