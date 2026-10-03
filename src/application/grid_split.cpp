#include "hikari/application/grid_split.h"

#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <vector>

namespace hikari::application {

namespace {

constexpr std::int64_t kUsPerMs = 1000;

int zeroIt(int ms)
{
    return ms / 10 * 10;
}

int msOf(core::DocumentTime t)
{
    return static_cast<int>(t.microseconds() / kUsPerMs);
}

std::vector<const core::LineRecord *> shownSelected(const EditSession &s, const LineVisible &visible)
{
    std::vector<const core::LineRecord *> out;
    for (const auto *l : s.document().lines())
        if (s.selection().selected.contains(l->id) && l->visibility != core::LineVisibility::Hidden &&
            (!visible || visible(l->id)))
            out.push_back(l);
    return out;
}

} // namespace

namespace legacy {

std::u8string floatText(float value)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%5.3f", static_cast<double>(value));
    std::string s(buf);
    // Trailing zeros, then the point (legacy getfloat).
    std::size_t remove = 0;
    for (std::size_t i = s.size() - 1; i > 0; --i) {
        if (s[i] == '0')
            ++remove;
        else if (s[i] == '.') {
            ++remove;
            break;
        } else
            break;
    }
    s.resize(s.size() - remove);
    const auto first = s.find_first_not_of(' ');
    s = first == std::string::npos ? std::string() : s.substr(first);
    return std::u8string(s.begin(), s.end());
}

std::u8string moveToPos(std::u8string_view text, std::int64_t lineStartMs, std::int64_t lineEndMs, int ms)
{
    // The first \move( or \pos( inside an override block.
    std::size_t at = std::u8string_view::npos;
    bool isMove = false;
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{')
            block = true;
        else if (text[i] == u8'}')
            block = false;
        else if (block && text[i] == u8'\\') {
            if (text.substr(i + 1, 5) == u8"move(") {
                at = i;
                isMove = true;
                break;
            }
            if (text.substr(i + 1, 4) == u8"pos(") {
                at = i;
                break;
            }
        }
    }
    if (at == std::u8string_view::npos)
        return std::u8string(text);
    const std::size_t open = text.find(u8'(', at), close = text.find(u8')', at);
    if (close == std::u8string_view::npos)
        return std::u8string(text);
    if (!isMove)
        return std::u8string(text); // a \pos stays where it is
    // GetMultiValueFloat: up to six comma-separated values.
    double v[6] = {0, 0, 0, 0, 0, 0};
    int count = 0;
    std::string values(text.begin() + static_cast<std::ptrdiff_t>(open + 1), text.begin() + static_cast<std::ptrdiff_t>(close));
    std::size_t p = 0;
    while (count < 6 && p <= values.size()) {
        const auto comma = values.find(',', p);
        v[count++] = std::strtod(values.substr(p, comma - p).c_str(), nullptr);
        if (comma == std::string::npos)
            break;
        p = comma + 1;
    }
    float t1 = static_cast<float>(v[4]), t2 = static_cast<float>(v[5]);
    if (!t1 && !t2) {
        t1 = static_cast<float>(lineStartMs);
        t2 = static_cast<float>(lineEndMs);
    } else {
        t1 += static_cast<float>(lineStartMs);
        t2 += static_cast<float>(lineStartMs);
    }
    // CalcMovePosition.
    float x = static_cast<float>(v[0]), y = static_cast<float>(v[1]);
    const float progress = (static_cast<float>(ms) - t1) / (t2 - t1);
    if (ms < t1) {
        // the start position
    } else if (ms > t2) {
        x = static_cast<float>(v[2]);
        y = static_cast<float>(v[3]);
    } else {
        x = x - (x - static_cast<float>(v[2])) * progress;
        y = y - (y - static_cast<float>(v[3])) * progress;
    }
    std::u8string out(text.substr(0, at));
    out += u8"\\pos(" + floatText(x) + u8"," + floatText(y) + u8")";
    out += text.substr(close + 1);
    return out;
}

} // namespace legacy

std::expected<void, CommandRefusal> splitAtVideoTime(EditSession &session, const LegacyTimebase &timebase,
                                                     std::int64_t videoMs, const LineVisible &visible)
{
    const auto selected = shownSelected(session, visible);
    if (selected.size() != 1 || timebase.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const int time = zeroIt(timebase.endTimeFor(timebase.frameAt(static_cast<int>(videoMs))));
    const core::LineId id = selected.front()->id;
    core::LineRecord copy = *selected.front();
    copy.start.value = core::DocumentTime(std::int64_t{time} * kUsPerMs);
    const auto ran = session.run(Command{"Splitting lines", session.revision(), {id}, [&](core::Document &d) {
                                             if (!d.editLine(id, [&](core::LineRecord &l) {
                                                     l.end.value = core::DocumentTime(std::int64_t{time} * kUsPerMs);
                                                 }))
                                                 return false;
                                             return d.insertLineAfter(id, copy).has_value();
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

std::expected<void, CommandRefusal> splitIntoFrames(EditSession &session, const LegacyTimebase &timebase,
                                                    const LineVisible &visible)
{
    if (!timebase.exact())
        return std::unexpected(CommandRefusal::Invalid);
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    struct Plan {
        core::LineId id;
        std::vector<core::LineRecord> frames; // the first replaces the Line
    };
    std::vector<Plan> plans;
    for (const auto *l : selected) {
        const int startMs = msOf(l->start.value), endMs = msOf(l->end.value);
        const int frameStart = timebase.frameAt(startMs), frameEnd = timebase.frameAt(endMs);
        Plan plan{l->id, {}};
        for (int j = frameStart; j < frameEnd; ++j) {
            core::LineRecord part = *l;
            part.text = legacy::moveToPos(l->text, startMs, endMs, timebase.msAt(j));
            part.start.value = core::DocumentTime(std::int64_t{zeroIt(timebase.startTimeFor(j))} * kUsPerMs);
            part.end.value = core::DocumentTime(std::int64_t{zeroIt(timebase.endTimeFor(j))} * kUsPerMs);
            plan.frames.push_back(std::move(part));
        }
        if (!plan.frames.empty())
            plans.push_back(std::move(plan));
    }
    if (plans.empty())
        return {};
    std::set<core::LineId> touched;
    for (const auto &p : plans)
        touched.insert(p.id);
    const auto ran = session.run(Command{"Splitting lines", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto &p : plans) {
                                                 const auto &first = p.frames.front();
                                                 if (!d.editLine(p.id, [&](core::LineRecord &l) {
                                                         l.text = first.text;
                                                         l.start.value = first.start.value;
                                                         l.end.value = first.end.value;
                                                     }))
                                                     return false;
                                                 core::LineId previous = p.id;
                                                 for (std::size_t k = 1; k < p.frames.size(); ++k) {
                                                     const auto id = d.insertLineAfter(previous, p.frames[k]);
                                                     if (!id)
                                                         return false;
                                                     previous = *id;
                                                 }
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}


namespace legacy {

using u16 = std::u16string;
using u16v = std::u16string_view;

std::vector<u16> splitByChar(u16v txt, bool addSpaces)
{
    std::vector<u16> out;
    const std::size_t len = txt.size();
    bool inBrackets = false;
    u16 charWithTags;
    std::size_t i = 0;
    while (i < len) {
        const char16_t ch = txt[i];
        if (i == len - 1) {
            charWithTags += ch;
            out.push_back(charWithTags);
            break;
        }
        if (!inBrackets && ch == u'\\') {
            const char16_t nch = txt[i + 1];
            if (nch == u'N' || nch == u'n') {
                out.push_back(u"\\N");
                i += 2;
                continue;
            }
        }
        charWithTags += ch;
        if (ch == u'{') {
            inBrackets = true;
        } else if (ch == u'}') {
            inBrackets = false;
        } else if (!inBrackets) {
            if (!addSpaces && (ch == u' ' || ch == u'\t' || ch == u'\n' || ch == u'\r')) {
                charWithTags.clear();
                ++i;
                continue;
            }
            out.push_back(charWithTags);
            charWithTags.clear();
        }
        ++i;
    }
    return out;
}

namespace {

bool isSpace16(char16_t c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\v' || c == u'\f' || c == 0x3000 || c == 0xA0;
}

// Dialogue::SplitWords: word segments and punctuation join a word; a space
// outside a block ends it and is a piece of its own.
void splitWords(std::vector<u16> &out, const u16 &wordsText, std::size_t offset, u16v text, const WordSegments &words)
{
    std::size_t curWordPos = 0, wordOffset = offset, start = 0;
    bool block = false;
    for (const auto &[end, isWord] : words(wordsText)) {
        const u16v word = u16v(wordsText).substr(start, end - start);
        start = end;
        const std::size_t wordLen = word.size();
        if (wordLen == 0)
            continue;
        if (isWord) {
            curWordPos += wordLen;
        } else if (word[0] == u'{') {
            block = true;
            curWordPos += wordLen;
        } else if (word[0] == u'}') {
            block = false;
            curWordPos += wordLen;
        } else if (!block && isSpace16(word[0])) {
            if (curWordPos) {
                out.emplace_back(text.substr(wordOffset, curWordPos));
                wordOffset += curWordPos;
                curWordPos = 0;
            }
            curWordPos = wordLen;
            if (curWordPos) {
                out.emplace_back(text.substr(wordOffset, curWordPos));
                wordOffset += curWordPos;
                curWordPos = 0;
            }
        } else {
            curWordPos += wordLen;
        }
    }
    if (curWordPos && wordOffset < text.size())
        out.emplace_back(text.substr(wordOffset, curWordPos));
}

} // namespace

std::vector<u16> splitByWord(u16v txt, const WordSegments &words)
{
    std::vector<u16> out;
    const std::size_t len = txt.size();
    u16 wordsText;
    std::size_t i = 0, offset = 0;
    while (i < len) {
        const char16_t ch = txt[i];
        if (i == len - 1) {
            wordsText += ch;
            splitWords(out, wordsText, offset, txt, words);
            break;
        }
        if (ch == u'\\') {
            const char16_t nch = txt[i + 1];
            if (nch == u'N' || nch == u'n') {
                splitWords(out, wordsText, offset, txt, words);
                wordsText.clear();
                out.push_back(u"\\N");
                offset += wordsText.size(); // legacy adds the cleared length: 0
                i += 2;
                continue;
            }
        }
        wordsText += ch;
        ++i;
    }
    return out;
}

std::vector<u16> splitByWrap(u16v txt)
{
    std::vector<u16> out;
    const std::size_t len = txt.size();
    bool inBrackets = false;
    u16 wrap;
    std::size_t i = 0;
    while (i < len) {
        const char16_t ch = txt[i];
        if (i == len - 1) {
            wrap += ch;
            out.push_back(wrap);
            break;
        }
        if (!inBrackets && ch == u'\\') {
            const char16_t nch = txt[i + 1];
            if (nch == u'N' || nch == u'n') {
                out.push_back(wrap);
                wrap.clear();
                i += 2;
                continue;
            }
        }
        wrap += ch;
        if (ch == u'{')
            inBrackets = true;
        else if (ch == u'}')
            inBrackets = false;
        ++i;
    }
    return out;
}

} // namespace legacy

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;
using legacy::isSpace16;

// The positional ASS v4+ Style fields (Name first), as the measure port takes them.
enum StyleField { Name, Fontname, Fontsize, Primary, Secondary, Outline, Back, Bold, Italic, Underline, StrikeOut,
                  ScaleX, ScaleY, Spacing, Angle, BorderStyle, OutlineWidth, Shadow, Alignment, MarginL, MarginR,
                  MarginV, Encoding, FieldCount };

std::string u8s(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::vector<std::string> fieldsOf(const core::StyleValues &v)
{
    auto colour = [](const core::Colour &c) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "&H%02X%02X%02X%02X", static_cast<unsigned>(c.a), static_cast<unsigned>(c.b),
                      static_cast<unsigned>(c.g), static_cast<unsigned>(c.r));
        return std::string(buf);
    };
    std::vector<std::string> f(FieldCount);
    f[Name] = u8s(v.name);
    f[Fontname] = u8s(v.fontname);
    f[Fontsize] = u8s(v.fontsize);
    f[Primary] = colour(v.primary);
    f[Secondary] = colour(v.secondary);
    f[Outline] = colour(v.outline);
    f[Back] = colour(v.back);
    f[Bold] = v.bold ? "-1" : "0";
    f[Italic] = v.italic ? "-1" : "0";
    f[Underline] = v.underline ? "-1" : "0";
    f[StrikeOut] = v.strikeOut ? "-1" : "0";
    f[ScaleX] = u8s(v.scaleX);
    f[ScaleY] = u8s(v.scaleY);
    f[Spacing] = u8s(v.spacing);
    f[Angle] = u8s(v.angle);
    f[BorderStyle] = v.borderStyle ? "3" : "1";
    f[OutlineWidth] = u8s(v.outlineWidth);
    f[Shadow] = u8s(v.shadow);
    f[Alignment] = u8s(v.alignment);
    f[MarginL] = u8s(v.marginLeft);
    f[MarginR] = u8s(v.marginRight);
    f[MarginV] = u8s(v.marginVertical);
    f[Encoding] = u8s(v.encoding);
    return f;
}

std::string toU8(u16v s)
{
    return u8s(core::toUtf8(u16(s)));
}

struct Tag {
    std::u16string name;
    std::u16string value;
};

// Dialogue::ParseTags with these tag names: the tags found in blocks, and
// with `plain`, the text outside blocks as "plain" entries between them.
std::vector<Tag> parseTags(u16v txt, const std::vector<u16v> &names, bool plain)
{
    std::vector<Tag> out;
    const std::size_t len = txt.size();
    std::size_t pos = 0, plainStart = 0;
    bool tagsBlock = false, drawing = false;
    if (len < 1)
        return out;
    while (pos < len) {
        const char16_t ch = txt[pos];
        if (ch == u'}') {
            tagsBlock = false;
            plainStart = pos + 1;
        } else if (ch == u'{' || pos >= len - 1) {
            tagsBlock = true;
            if (pos >= len - 1)
                ++pos;
            if ((plain || drawing) && plainStart + 1 <= pos)
                out.push_back({drawing ? u"pvector" : u"plain", u16(txt.substr(plainStart, pos - plainStart))});
        } else if (tagsBlock && ch == u'\\') {
            ++pos;
            const std::size_t slash = txt.find(u'\\', pos), bracket = txt.find(u'}', pos);
            const std::size_t tagEnd = slash == u16v::npos && bracket == u16v::npos ? len
                                       : slash == u16v::npos                     ? bracket
                                       : bracket == u16v::npos                   ? slash
                                                                                 : std::min(slash, bracket);
            u16 tag(txt.substr(pos, tagEnd - pos));
            if (!tag.empty() && tag.back() == u')')
                tag.pop_back();
            for (const u16v name : names) {
                if (tag.size() > name.size() && u16v(tag).substr(0, name.size()) == name) {
                    const char16_t first = tag[name.size()];
                    if (first == u'(' || (first >= u'0' && first <= u'9') || name == u"fn" || first == u'.' ||
                        first == u'-' || first == u'+') {
                        u16 value = tag.substr(name.size());
                        if (name == u"p") {
                            drawing = value != u"0";
                        } else if (!value.empty() && value[0] == u'(') {
                            const auto close = value.find(u')');
                            value = value.substr(1, close == u16::npos ? u16::npos : close - 1);
                        } else if (name != u"fn") {
                            // ToCDouble failing: keep the leading number characters.
                            u16 number;
                            for (const char16_t c : value) {
                                if (!((c >= u'0' && c <= u'9') || c == u'.' || c == u'-' || c == u'+'))
                                    break;
                                number += c;
                            }
                            value = number;
                        }
                        out.push_back({u16(name), value});
                        pos = tagEnd - 1;
                        break;
                    }
                }
            }
        }
        ++pos;
    }
    return out;
}

std::optional<u16> findTag(const std::vector<Tag> &tags, u16v name)
{
    for (const auto &t : tags)
        if (t.name == name)
            return t.value;
    return std::nullopt;
}

// Styles::SetStyleFromParseData for the measured tags.
void applyTags(std::vector<std::string> &style, const std::vector<Tag> &tags, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i <= to && i < tags.size(); ++i) {
        const auto &t = tags[i];
        const std::string v = toU8(t.value);
        if (t.name == u"fs")
            style[Fontsize] = v;
        else if (t.name == u"fsp")
            style[Spacing] = v;
        else if (t.name == u"fscx")
            style[ScaleX] = v;
        else if (t.name == u"fscy")
            style[ScaleY] = v;
        else if (t.name == u"fn")
            style[Fontname] = v;
        else if (t.name == u"b")
            style[Bold] = v == "1" ? "-1" : "0";
        else if (t.name == u"i")
            style[Italic] = v == "1" ? "-1" : "0";
    }
}

// Dialogue::GetTaggedTextExtents with a Style the tags keep changing (legacy
// passes copyStyle = false here). Width sums, height is the largest.
bool taggedExtents(TextMeasurePort &measure, std::vector<std::string> &style, u16v text, float &width, float &height)
{
    static const std::vector<u16v> names{u"fscx", u"fscy", u"fsp", u"fs", u"fn", u"b", u"i"};
    const auto tags = parseTags(text, names, true);
    float fullWidth = 0, fullHeight = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (tags[i].name != u"plain")
            continue;
        if (i > 0)
            applyTags(style, tags, start, i - 1);
        const auto e = measure.measure(style, toU8(tags[i].value));
        if (!e)
            return false;
        fullWidth += static_cast<float>(e->width);
        fullHeight = std::max(fullHeight, static_cast<float>(e->height));
        start = i + 1;
    }
    width = fullWidth;
    height = fullHeight;
    return true;
}

// Dialogue::GetTextStripped(..., removeFirstBlock = true): from the first
// character outside the leading blocks.
u16 afterFirstBlock(u16v text)
{
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c == u'{')
            block = true;
        else if (block && c == u'}') {
            block = false;
            continue;
        }
        if (!block && c != u'{')
            return u16(text.substr(i));
    }
    return {};
}

u16 stripped(u16v text)
{
    u16 out;
    bool block = false;
    for (const char16_t c : text) {
        if (c == u'{')
            block = true;
        else if (c == u'}' && block)
            block = false;
        else if (!block)
            out += c;
    }
    return out;
}

// Dialogue::GetFirstTagsBlock: the leading blocks.
u16 firstTagsBlock(u16v text)
{
    u16 out;
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c == u'{')
            block = true;
        else if (c == u'}') {
            out += c;
            block = false;
        }
        if (block)
            out += c;
        else if (i + 1 < text.size() && text[i + 1] != u'{')
            break;
    }
    return out;
}

char16_t lower16(char16_t c)
{
    return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c;
}

bool startsWithIcase(u16v text, std::size_t at, u16v what)
{
    if (at + what.size() > text.size())
        return false;
    for (std::size_t i = 0; i < what.size(); ++i)
        if (lower16(text[at + i]) != lower16(what[i]))
            return false;
    return true;
}

std::size_t count16(u16v text, char16_t c)
{
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), c));
}

// wxString::Find(c, true) within text[0, end).
long lastIndexOf(u16v text, std::size_t end, char16_t c)
{
    for (std::size_t i = std::min(end, text.size()); i > 0; --i)
        if (text[i - 1] == c)
            return static_cast<long>(i - 1);
    return -1;
}

// Legacy FindBrackets (TagFindReplace.cpp) at position 0.
std::pair<long, long> bracketsAtStart(u16v text)
{
    const long from = 0;
    bool haveStart = false, haveEnd = false;
    long endPos = -1, startPos = -1;
    const std::size_t len = text.size();
    std::size_t i = 1;
    for (; i < len; ++i) {
        const char16_t ch = text[i];
        if (ch == u'}') {
            haveEnd = true;
            endPos = static_cast<long>(i);
        } else if (ch == u'{' && static_cast<long>(i) + 1 > from && static_cast<long>(i) != from) {
            if (!haveEnd)
                break;
            haveEnd = false;
        } else if (haveEnd) {
            break;
        }
    }
    if (len > 0 && text[0] == u'{') {
        haveStart = true;
        startPos = 0;
    }
    if (haveEnd && i >= len && static_cast<std::size_t>(endPos) + 1 < len)
        endPos = static_cast<long>(len) - 1;
    if (startPos != -1 && endPos == -1)
        endPos = static_cast<long>(len) - 1;
    if (startPos == -1 && endPos != -1)
        endPos = -1;
    (void)haveStart;
    return {startPos, endPos};
}

// The tags Split looks up: "^an([0-9]*)", "^pos\((.+)\)", "^move\((.+)\)".
// Returns the regex replacement (the capture plus the rest) when it matches.
std::optional<u16> matchTag(u16v ftag, u16v name)
{
    if (name == u"an") {
        if (ftag.substr(0, 2) != u"an")
            return std::nullopt;
        return u16(ftag.substr(2));
    }
    const u16 open = u16(name) + u"(";
    if (ftag.substr(0, open.size()) != open)
        return std::nullopt;
    const auto close = ftag.rfind(u')');
    if (close == u16v::npos || close < open.size() + 1)
        return std::nullopt;
    return u16(ftag.substr(open.size(), close - open.size())) + u16(ftag.substr(close + 1));
}

struct TagPlace {
    long x = 0, y = 0;
    bool inBracket = false;
};

// TagFindReplace::FindTag(pattern, text, 0) without a tab (from = to = 0).
TagPlace findTagPlace(u16v txt, u16v name)
{
    TagPlace result;
    const long from = 0;
    if (txt.empty())
        return result;
    auto [bracketStart, bracketEnd] = bracketsAtStart(txt);
    bool brkt = true;
    if (bracketStart == -1 || bracketStart > bracketEnd + 1) {
        result.inBracket = false;
        bracketEnd = from;
        brkt = false;
    } else {
        result.inBracket = true; // bracketStart == bracketEnd + 1 cannot happen at 0
    }
    result.x = result.y = bracketEnd;
    bool isT = false, placedInT = false, hasR = false;
    long endT, lastT = endT = bracketEnd - 1;
    long lslash = bracketEnd + 1;
    long lastTag = -1;
    bool found[2] = {false, false};
    TagPlace points[2];
    const long txtlen = static_cast<long>(txt.size());
    if (bracketEnd == txtlen)
        --bracketEnd;
    if (bracketEnd > txtlen)
        bracketEnd = txtlen - 1;
    if (bracketStart > txtlen)
        bracketStart = txtlen - 1;
    for (long i = bracketEnd; i >= 0; --i) {
        const char16_t ch = txt[static_cast<std::size_t>(i)];
        if (ch == u'\\' && brkt) {
            if (i >= bracketStart)
                lastTag = i;
            const long tagEnd = std::min(lslash - 1, txtlen - 1);
            u16 ftag = tagEnd >= i + 1 ? u16(txt.substr(static_cast<std::size_t>(i + 1),
                                                        static_cast<std::size_t>(tagEnd - i)))
                                       : u16();
            if (!ftag.empty() && ftag.back() == u')') {
                if (ftag.substr(0, 2) == u"t(") {
                    isT = true;
                    endT = lslash - 1;
                } else if (count16(ftag, u')') > count16(ftag, u'(')) {
                    const u16v textToCheck = txt.substr(0, static_cast<std::size_t>(std::max(lslash, 0L)));
                    const auto lastTpos = txt.rfind(u"\\t(");
                    const long lastBS = lastIndexOf(textToCheck, textToCheck.size(), u'(');
                    const long lastBE = lastIndexOf(textToCheck, textToCheck.size(), u')');
                    if (lastTpos != u16v::npos && static_cast<unsigned long>(lastBS) < static_cast<unsigned long>(lastBE)) {
                        isT = true;
                        endT = lslash - 1;
                    }
                }
            }
            if (ftag == u"r")
                hasR = true;
            if (ftag.substr(0, 2) == u"t(") {
                if (endT == -1)
                    endT = lastT;
                if (i <= from && from <= endT) {
                    if (found[1] && points[1].y <= endT) {
                        result.x = points[1].x;
                        result.y = points[1].y;
                        return result;
                    } else if (found[0]) {
                        if (points[0].y <= endT)
                            break;
                    } else {
                        result.x = result.y = endT;
                        result.inBracket = true;
                        placedInT = true;
                    }
                }
                isT = false;
                lslash = i;
                endT = -1;
                lastT = i;
                continue;
            }
            const bool isFN = ftag.substr(0, 2) == u"fn";
            if (auto replaced = matchTag(ftag, name)) {
                ftag = *replaced;
                if ((!ftag.empty() && ftag.back() == u')' && !isFN &&
                     (ftag.front() != u'(' || count16(ftag, u')') >= 2)) ||
                    (!ftag.empty() && ftag.back() == u'}')) {
                    ftag.pop_back();
                    --lslash;
                }
                if (!found[0] && !isT) {
                    found[0] = true;
                    points[0].x = i < lastTag ? lastTag : i;
                    points[0].y = i < lastTag ? lastTag : lslash - 1;
                } else {
                    found[1] = true;
                    points[1].x = i;
                    points[1].y = lslash - 1;
                }
                if (!isT && found[0] && i <= from)
                    break;
            }
            lslash = i;
        } else if (ch == u'{' && i > 0) {
            const long startBracket = lastIndexOf(txt, static_cast<std::size_t>(i), u'{');
            const long endBracket = lastIndexOf(txt, static_cast<std::size_t>(i), u'}');
            if (endBracket >= startBracket) {
                brkt = false;
                if (txt[static_cast<std::size_t>(i - 1)] != u'}' && hasR)
                    break;
            } else {
                lslash = i - 1;
            }
        } else if (ch == u'}' && i > 0) {
            const long startBracket = lastIndexOf(txt, static_cast<std::size_t>(i), u'{');
            const long endBracket = lastIndexOf(txt, static_cast<std::size_t>(i), u'}');
            if (endBracket < startBracket) {
                lslash = i;
                brkt = true;
            }
        }
    }
    if (!isT && found[0]) {
        if (result.inBracket && !placedInT) {
            result.x = points[0].x;
            result.y = points[0].y;
        }
        return result;
    }
    if (lastTag >= 0 && result.inBracket && !placedInT)
        result.x = result.y = lastTag;
    return result;
}

// TFR.FindTag(name...) + TFR.Replace(tagText) on the tags block.
void setTag(u16 &block, u16v name, u16v tagText)
{
    const TagPlace place = findTagPlace(block, name);
    if (block.empty()) {
        block = u"{" + u16(tagText) + u"}";
        return;
    }
    if (!place.inBracket) {
        block.insert(static_cast<std::size_t>(place.x), u"{" + u16(tagText) + u"}");
        return;
    }
    if (place.x < place.y) {
        if (static_cast<std::size_t>(place.y) + 1 >= block.size())
            block.erase(static_cast<std::size_t>(place.x));
        else
            block.erase(static_cast<std::size_t>(place.x), static_cast<std::size_t>(place.y - place.x + 1));
    }
    block.insert(static_cast<std::size_t>(place.x), tagText);
}

// Dialogue::GetTagName.
u16 tagName(u16v tagWithValue)
{
    if (tagWithValue.substr(0, 2) == u"fn")
        return u"fn";
    if (tagWithValue.substr(0, 3) == u"\\fn")
        return u"\\fn";
    if (tagWithValue.empty())
        return {};
    static constexpr u16v delims = u"0123456789-.(&";
    u16 name(1, tagWithValue[0]);
    for (std::size_t i = 1; i < tagWithValue.size() && delims.find(tagWithValue[i]) == u16v::npos; ++i)
        name += tagWithValue[i];
    return name;
}

// Dialogue::MergeTagBlocks: each tag of `merged` replaces the first match of
// its pattern in `output` when that is the same tag (else it is dropped), or
// is added at the end when nothing matches.
void mergeTagBlocks(u16 &output, u16v merged)
{
    if (merged.empty())
        return;
    if (output.size() < 3) {
        output = u16(merged);
        return;
    }
    const u16v inner = merged.substr(1, merged.size() >= 2 ? merged.size() - 2 : 0);
    std::size_t p = 0;
    while (p < inner.size()) {
        const auto next = inner.find(u'\\', p);
        const u16v token = inner.substr(p, next == u16v::npos ? u16v::npos : next - p);
        p = next == u16v::npos ? inner.size() : next + 1;
        if (token.empty())
            continue;
        const u16 tag = tagName(token);
        // The leftmost match of "\\1?c&", "\\frz?" or "\\<tag>" (ignoring case).
        std::size_t at = u16::npos;
        for (std::size_t i = 0; i < output.size() && at == u16::npos; ++i) {
            if (output[i] != u'\\')
                continue;
            if (tag == u"c" || tag == u"1c") {
                if (startsWithIcase(output, i + 1, u"c&") || startsWithIcase(output, i + 1, u"1c&"))
                    at = i;
            } else if (tag == u"fr" || tag == u"frz") {
                if (startsWithIcase(output, i + 1, u"fr"))
                    at = i;
            } else if (startsWithIcase(output, i + 1, tag)) {
                at = i;
            }
        }
        if (at != u16::npos) {
            std::size_t end = at + 1;
            while (end < output.size() && output[end] != u'\\' && output[end] != u'}')
                ++end;
            if (tagName(u16v(output).substr(at, end - at)) == u"\\" + tag)
                output.replace(at, end - at, u"\\" + u16(token));
        } else {
            output.insert(output.size() - 1, u"\\" + u16(token));
        }
    }
}

u16 numberText(float v)
{
    const auto t = legacy::floatText(v);
    return core::toUtf16(t);
}

} // namespace

std::expected<void, CommandRefusal> splitByText(EditSession &session, SplitText kind, TextMeasurePort &measure,
                                                const WordSegments &words, const LineVisible &visible)
{
    const auto selected = shownSelected(session, visible);
    if (selected.empty())
        return std::unexpected(CommandRefusal::Invalid);
    const auto &document = session.document();
    // SubsGrid::GetASSRes (without writing the fallbacks into Script Info).
    auto info = [&](const char8_t *key) {
        const auto v = document.scriptInfo(key).value_or(std::u8string());
        return std::atoi(u8s(v).c_str());
    };
    int resX = info(u8"PlayResX"), resY = info(u8"PlayResY");
    if (resX < 1 && resY < 1) {
        resX = 1280;
        resY = 720;
    } else if (resX < 1) {
        resX = static_cast<int>(static_cast<float>(resY) * (16.0 / 9.0));
    } else if (resY < 1) {
        resY = static_cast<int>(static_cast<float>(resX) * (9.0 / 16.0));
    }
    const auto styles = core::decodeStyles(document);
    const bool byWraps = kind == SplitText::Wraps;
    struct Plan {
        core::LineId id;
        std::vector<std::u8string> texts; // the first replaces the Line's text
    };
    std::vector<Plan> plans;
    for (const auto *line : selected) {
        const u16 text = core::toUtf16(line->translation.empty() ? line->text : line->translation);
        if (stripped(text).empty())
            continue;
        u16 firstBlock = firstTagsBlock(text);
        // GetStyle(0, name): the first Style of that name, else the first Style.
        core::StyleValues style;
        bool found = false;
        for (const auto &s : styles)
            if (s.name == line->style) {
                style = s;
                found = true;
                break;
            }
        if (!found && !styles.empty())
            style = styles.front();
        const auto fields = fieldsOf(style);
        static const std::vector<u16v> posNames{u"pos", u"move", u"an", u"fscx", u"fscy", u"fsp", u"fs", u"fn", u"b", u"i"};
        const auto tags = parseTags(text, posNames, false);
        int an = 0;
        if (const auto v = findTag(tags, u"an"))
            an = std::atoi(toU8(*v).c_str());
        else
            an = std::atoi(fields[Alignment].c_str());
        float posx = 0, posy = 0, distx = 0, disty = 0;
        bool hasMove = false;
        u16 times;
        if (const auto v = findTag(tags, u"move")) {
            // GetMultiValueFloat(value, 4, &times): empty tokens are skipped;
            // `times` is the rest after the fourth value.
            float values[4] = {0, 0, 0, 0};
            std::size_t p = 0;
            int count = 0;
            while (p < v->size()) {
                if ((*v)[p] == u',') {
                    ++p;
                    continue;
                }
                if (count == 4) {
                    times = v->substr(p);
                    break;
                }
                const auto comma = v->find(u',', p);
                values[count++] = std::strtof(toU8(v->substr(p, comma == u16::npos ? u16::npos : comma - p)).c_str(), nullptr);
                p = comma == u16::npos ? v->size() : comma + 1;
            }
            posx = values[0];
            posy = values[1];
            distx = values[2] - posx;
            disty = values[3] - posy;
            hasMove = true;
        } else if (const auto v = findTag(tags, u"pos")) {
            // GetTwoValueFloat: both values must parse whole.
            auto whole = [](const u16 &t, float &out) {
                const std::string u = toU8(t);
                char *end = nullptr;
                out = std::strtof(u.c_str(), &end);
                return !u.empty() && end && *end == '\0';
            };
            const auto comma = v->find(u',');
            float x = 0, y = 0;
            if (whole(v->substr(0, comma), x) && comma != u16::npos && whole(v->substr(comma + 1), y)) {
                posx = x;
                posy = y;
            }
        } else {
            // Dialogue::GetDefaultPosition.
            const int marginL = line->marginLeft.value ? static_cast<int>(line->marginLeft.value) : std::atoi(fields[MarginL].c_str());
            const int marginR = line->marginRight.value ? static_cast<int>(line->marginRight.value) : std::atoi(fields[MarginR].c_str());
            const int marginV = line->marginVertical.value ? static_cast<int>(line->marginVertical.value) : std::atoi(fields[MarginV].c_str());
            if (an % 3 == 2)
                posx = (resX + marginL - marginR) / 2.f;
            else if (an % 3 == 0)
                posx = static_cast<float>(resX - marginR);
            else
                posx = static_cast<float>(marginL);
            if (an < 4)
                posy = static_cast<float>(resY - marginV);
            else if (an < 7)
                posy = resY / 2.f;
            else
                posy = static_cast<float>(marginV);
        }
        setTag(firstBlock, u"an", u"\\an" + core::toUtf16(std::u8string(reinterpret_cast<const char8_t *>(std::to_string(an).c_str()))));
        const auto wrapsTable = legacy::splitByWrap(text);
        const int wraps = static_cast<int>(wrapsTable.size()) - 1;
        float y = posy;
        bool first = true;
        std::vector<std::pair<float, float>> sizes;
        float fullHeight = 0;
        {
            auto stylec = fields;
            for (const auto &wrap : wrapsTable) {
                u16 trimmed = wrap;
                while (!trimmed.empty() && isSpace16(trimmed.back()))
                    trimmed.pop_back();
                float w = 0, h = 0;
                // An empty text measures the whole Line (legacy falls back to it).
                taggedExtents(measure, stylec, trimmed.empty() ? text : trimmed, w, h); // a failure is only logged
                sizes.emplace_back(w, h);
                fullHeight += h;
            }
        }
        auto stylec = fields;
        u16 current = text;
        Plan plan{line->id, {}};
        for (std::size_t k = 0; k < wrapsTable.size(); ++k) {
            std::vector<u16> pieces;
            const u16 &wrapText = wrapsTable[k];
            if (kind == SplitText::Characters)
                pieces = legacy::splitByChar(wrapText, true);
            else if (kind == SplitText::Words)
                pieces = legacy::splitByWord(wrapText, words);
            else
                pieces.push_back(wrapText);
            float x = posx;
            float w = sizes[k].first, h = sizes[k].second;
            if (!byWraps) {
                if (an % 3 == 2)
                    x -= w / 2;
                else if (an % 3 == 0)
                    x -= w;
            }
            if ((byWraps || wraps) && k == 0) {
                if (!wraps)
                    continue;
                if (an < 4)
                    y -= fullHeight - h;
                else if (an < 7)
                    y -= (fullHeight - h) / 2;
            }
            bool hasWrap = false;
            for (std::size_t j = 0; j < pieces.size(); ++j) {
                // Legacy reads an empty piece's tags and size from the Line's
                // text as last set.
                const u16 &piece = pieces[j].empty() ? current : pieces[j];
                u16 txt = afterFirstBlock(pieces[j]);
                if (k > 0)
                    mergeTagBlocks(firstBlock, firstTagsBlock(piece));
                if (j == 0 && k > 0)
                    hasWrap = true;
                taggedExtents(measure, stylec, piece, w, h);
                while (!txt.empty() && isSpace16(txt.back()))
                    txt.pop_back();
                if (hasWrap) {
                    y += h;
                    if (!byWraps)
                        hasWrap = false;
                }
                if (!byWraps) {
                    if (an % 3 == 2)
                        x += w / 2;
                    else if (an % 3 == 0)
                        x += w;
                }
                if (!txt.empty()) {
                    if (first)
                        first = false;
                    if (hasMove)
                        setTag(firstBlock, u"move", u"\\move(" + numberText(x) + u"," + numberText(y) + u"," +
                                                        numberText(x + distx) + u"," + numberText(y + disty) + u"," +
                                                        times + u")");
                    else
                        setTag(firstBlock, u"pos", u"\\pos(" + numberText(x) + u"," + numberText(y) + u")");
                    current = firstBlock + txt;
                    plan.texts.push_back(core::toUtf8(current));
                }
                if (!byWraps) {
                    if (an % 3 == 2)
                        x += w / 2;
                    else if (an % 3 == 1)
                        x += w;
                }
            }
        }
        if (!plan.texts.empty())
            plans.push_back(std::move(plan));
    }
    if (plans.empty())
        return {};
    std::set<core::LineId> touched;
    for (const auto &p : plans)
        touched.insert(p.id);
    const auto ran = session.run(Command{"Splitting lines", session.revision(), touched, [&](core::Document &d) {
                                             for (const auto &p : plans) {
                                                 const core::LineRecord *source = nullptr;
                                                 for (const auto *l : d.lines())
                                                     if (l->id == p.id)
                                                         source = l;
                                                 if (!source)
                                                     return false;
                                                 const core::LineRecord copy = *source;
                                                 const bool translated = !copy.translation.empty();
                                                 auto withText = [&](core::LineRecord l, const std::u8string &t) {
                                                     (translated ? l.translation : l.text) = t;
                                                     return l;
                                                 };
                                                 if (!d.editLine(p.id, [&](core::LineRecord &l) { l = withText(l, p.texts.front()); }))
                                                     return false;
                                                 core::LineId previous = p.id;
                                                 for (std::size_t k = 1; k < p.texts.size(); ++k) {
                                                     const auto id = d.insertLineAfter(previous, withText(copy, p.texts[k]));
                                                     if (!id)
                                                         return false;
                                                     previous = *id;
                                                 }
                                             }
                                             return true;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    return {};
}

} // namespace hikari::application
