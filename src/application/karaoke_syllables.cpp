#include "hikari/application/karaoke_syllables.h"

#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cstdlib>

namespace hikari::application {

namespace {

// String arithmetic over UTF-16 units, as wxString holds them on Windows.

using u16 = std::u16string;

// Out of range gives empty; the count is clamped.
u16 wxMid(const u16 &s, std::size_t first, std::size_t count)
{
    const std::size_t len = s.size();
    if (first > len)
        return {};
    if (count > len - first)
        count = len - first;
    return s.substr(first, count);
}

// The last position, or -1.
int wxFindLast(const u16 &s, char16_t ch)
{
    const auto at = s.rfind(ch);
    return at == u16::npos ? -1 : static_cast<int>(at);
}

void wxReplaceAll(u16 &s, const u16 &from, const u16 &to)
{
    u16 out;
    std::size_t at = 0;
    for (std::size_t hit; (hit = s.find(from, at)) != u16::npos; at = hit + from.size())
        out += s.substr(at, hit - at) + to;
    s = out + s.substr(at);
}

u16 withoutBlocks(const u16 &s)
{
    u16 out;
    std::size_t at = 0;
    for (;;) {
        const auto open = s.find(u'{', at);
        const auto close = open == u16::npos ? u16::npos : s.find(u'}', open);
        if (close == u16::npos)
            break;
        out += s.substr(at, open - at);
        at = close + 1;
    }
    return out + s.substr(at);
}

// Whether the whole text is a C-locale number.
bool wxToCDouble(const u16 &text)
{
    std::string ascii;
    for (const char16_t c : text) {
        if (c > 0x7F)
            return false;
        ascii += static_cast<char>(c);
    }
    if (ascii.empty())
        return false;
    char *end = nullptr;
    std::strtod(ascii.c_str(), &end);
    return end != ascii.c_str() && *end == '\0';
}

struct KaraokeTag {
    u16 name, value;
    unsigned int startTextPos = 0; // where the value starts
};

std::vector<KaraokeTag> karaokeTags(const u16 &txt)
{
    static const u16 names[] = {u"kf", u"ko", u"k", u"K"};
    std::vector<KaraokeTag> out;
    const std::size_t len = txt.size();
    std::size_t pos = 0;
    bool tagsBlock = false;
    if (len < 1)
        return out;
    while (pos < len) {
        const char16_t ch = txt[pos];
        if (ch == u'}') {
            tagsBlock = false;
        } else if (ch == u'{' || pos >= len - 1) {
            tagsBlock = true;
            if (pos >= len - 1)
                ++pos;
        } else if (tagsBlock && ch == u'\\') {
            ++pos;
            const std::size_t slash = txt.find(u'\\', pos), bracket = txt.find(u'}', pos);
            const std::size_t tagEnd = slash == u16::npos && bracket == u16::npos ? len
                                       : slash == u16::npos                     ? bracket
                                       : bracket == u16::npos                   ? slash
                                                                                : std::min(slash, bracket);
            u16 tag = txt.substr(pos, tagEnd - pos);
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16 &name : names) {
                if (tag.size() <= name.size() || tag.compare(0, name.size(), name) != 0)
                    continue;
                const char16_t first = tag[name.size()];
                if (!(first == u'(' || (first >= u'0' && first <= u'9') || first == u'.' || first == u'-' ||
                      first == u'+'))
                    continue;
                KaraokeTag data{name, {}, static_cast<unsigned int>(pos + name.size())};
                u16 value = tag.substr(name.size());
                if (first == u'(') {
                    ++data.startTextPos;
                    const u16 after = value.substr(value.find(u'(') + 1);
                    data.value = after.substr(0, after.find(u')'));
                } else {
                    if (!wxToCDouble(value)) {
                        u16 digits;
                        for (const char16_t c : value) {
                            if (!(c >= u'0' && c <= u'9') && c != u'.' && c != u'-' && c != u'+')
                                break;
                            digits += c;
                        }
                        value = digits;
                    }
                    data.value = value;
                }
                out.push_back(std::move(data));
                pos = tagEnd - 1;
                break;
            }
        }
        ++pos;
    }
    return out;
}

std::string utf8Of(const u16 &s)
{
    const auto u8 = core::toUtf8(s);
    return std::string(u8.begin(), u8.end());
}

}

std::vector<KaraokeSyllable> karaokeSyllables(std::string_view raw, std::int64_t startMs, std::int64_t endMs)
{
    const u16 text = core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(raw.data()), raw.size()));
    std::vector<KaraokeSyllable> out;
    int kcount = 0;
    int ktime = 0;
    out.push_back({});
    ++kcount;
    const auto tags = karaokeTags(text);
    std::size_t lastPosition = 0;
    const std::size_t tagssize = tags.size();
    for (std::size_t i = 0; i < tagssize; i++) {
        const KaraokeTag &tdata = tags[i];
        long long nextKstart = static_cast<long long>(lastPosition);
        if (i < tagssize - 1) {
            nextKstart = tags[i + 1].startTextPos;
            const u16 newtxt = wxMid(text, lastPosition, static_cast<std::size_t>(nextKstart) - lastPosition);
            const std::size_t bracketstartpos = static_cast<std::size_t>(wxFindLast(newtxt, u'{'));
            // A part without '{' ends before the first '\' after its last '}', taken as a position in the line.
            if (bracketstartpos == static_cast<std::size_t>(-1)) {
                const std::size_t bracketendpos = static_cast<std::size_t>(wxFindLast(newtxt, u'}'));
                const std::size_t firstSlash = newtxt.find(u'\\', bracketendpos + 1);
                if (firstSlash != u16::npos)
                    nextKstart = static_cast<long long>(firstSlash - 1);
            } else {
                nextKstart = static_cast<long long>(lastPosition + bracketstartpos - 1);
            }
        } else {
            nextKstart = static_cast<long long>(text.length());
        }
        int kdur = std::atoi(utf8Of(tdata.value).c_str());
        kdur *= 10;
        u16 ktext;
        if (nextKstart >= 0) {
            ktext = wxMid(text, lastPosition, tdata.startTextPos - lastPosition - tdata.name.length() - 1);
            const std::size_t newStart = tdata.startTextPos + tdata.value.length();
            ktext += wxMid(text, newStart, static_cast<std::size_t>(nextKstart) - newStart + 1);
            wxReplaceAll(ktext, u"{}", u"");
        }
        const int start = ktime;
        ktime += kdur;
        out.push_back({kdur, start, ktime, utf8Of(tdata.name), utf8Of(ktext), utf8Of(withoutBlocks(ktext))});
        ++kcount;
        lastPosition = static_cast<std::size_t>(nextKstart + 1);
    }
    if (kcount < 2) {
        const int start = static_cast<int>(startMs), end = static_cast<int>(endMs);
        out.push_back({end - start, start, end, "k", utf8Of(text), utf8Of(withoutBlocks(text))});
    }
    return out;
}

}
