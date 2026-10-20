// KA6: TTML lyrics. The core has no XML library, so a small reader builds
// the element tree: elements, attributes, text, CDATA and the predefined and
// numeric entities; comments, processing instructions and the DOCTYPE are
// skipped. Names keep their prefixes and are matched by local name.
//
// Lyrics TTML (Apple Music, AMLL TTML Tool) times every <p> and <span> on
// the media timeline, although TTML's own rule makes a child's times
// relative to its parent's begin; the files agree with themselves only when
// read as absolute times, so they are read that way.

#include "hikari/core/lyrics_import.h"

#include "lyrics_lines.h"
#include "text_util.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace hikari::core {

namespace {

using detail::LyricLine;
using detail::LyricWord;
using detail::u8sv;
using Kind = LyricsProblem::Kind;

struct XmlNode {
    std::u8string name; // as written, with its prefix; empty for text
    std::vector<std::pair<std::u8string, std::u8string>> attributes;
    std::vector<XmlNode> children;
    std::u8string text;
    std::size_t line = 0;

    bool isText() const { return name.empty(); }
};

u8sv localName(u8sv name)
{
    const std::size_t colon = name.rfind(u8':');
    return colon == u8sv::npos ? name : name.substr(colon + 1);
}

std::optional<u8sv> attribute(const XmlNode &node, u8sv local)
{
    for (const auto &[name, value] : node.attributes)
        if (!name.starts_with(u8"xmlns") && localName(name) == local)
            return u8sv(value);
    return std::nullopt;
}

bool isSpace(char8_t c)
{
    return c == u8' ' || c == u8'\t' || c == u8'\r' || c == u8'\n';
}

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

void appendUtf8(std::u8string &out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char8_t>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char8_t>(0xC0 | (cp >> 6));
        out += static_cast<char8_t>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char8_t>(0xE0 | (cp >> 12));
        out += static_cast<char8_t>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char8_t>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char8_t>(0xF0 | (cp >> 18));
        out += static_cast<char8_t>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char8_t>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char8_t>(0x80 | (cp & 0x3F));
    }
}

class XmlReader {
public:
    explicit XmlReader(u8sv text) : m_s(text) {}

    std::expected<XmlNode, LyricsProblem> document()
    {
        if (m_s.starts_with(u8"\xEF\xBB\xBF"))
            m_pos = 3;
        if (auto skipped = skipMisc(); !skipped)
            return std::unexpected(skipped.error());
        if (at(u8"<") && !at(u8"</"))
            return element();
        return std::unexpected(broken());
    }

private:
    bool at(u8sv s) const { return m_s.substr(m_pos).starts_with(s); }
    bool done() const { return m_pos >= m_s.size(); }

    void advance(std::size_t n)
    {
        for (std::size_t end = std::min(m_s.size(), m_pos + n); m_pos < end; ++m_pos)
            if (m_s[m_pos] == u8'\n')
                ++m_line;
    }

    void skipSpace()
    {
        while (!done() && isSpace(m_s[m_pos]))
            advance(1);
    }

    // The text here, up to the end of its line or tag, as the excerpt.
    LyricsProblem broken() const
    {
        u8sv rest = m_s.substr(std::min(m_pos, m_s.size()));
        std::size_t n = std::min<std::size_t>(rest.find_first_of(u8">\n"), 40);
        if (n < rest.size() && rest[n] == u8'>')
            ++n;
        while (n < rest.size() && n > 0 && (static_cast<unsigned char>(rest[n]) & 0xC0) == 0x80)
            --n; // whole characters only
        return LyricsProblem{Kind::NotWellFormed, m_line, std::u8string(detail::trimRight(rest.substr(0, n)))};
    }

    bool skipPast(u8sv terminator)
    {
        const std::size_t end = m_s.find(terminator, m_pos);
        if (end == u8sv::npos)
            return false;
        advance(end + terminator.size() - m_pos);
        return true;
    }

    // Whitespace, comments, processing instructions and a DOCTYPE between
    // the top-level parts of the file.
    std::expected<void, LyricsProblem> skipMisc()
    {
        for (;;) {
            skipSpace();
            const auto start = std::pair{m_pos, m_line};
            bool ok = true;
            if (at(u8"<?"))
                ok = skipPast(u8"?>");
            else if (at(u8"<!--"))
                ok = skipPast(u8"-->");
            else if (at(u8"<!DOCTYPE")) {
                int depth = 0;
                for (ok = false; !done(); advance(1)) {
                    const char8_t c = m_s[m_pos];
                    depth += c == u8'[' ? 1 : c == u8']' ? -1 : 0;
                    if (c == u8'>' && depth <= 0) {
                        advance(1);
                        ok = true;
                        break;
                    }
                }
            } else
                return {};
            if (!ok) {
                std::tie(m_pos, m_line) = start;
                return std::unexpected(broken());
            }
        }
    }

    // A name; it may not start with a digit, '-' or '.'.
    std::optional<std::u8string> name()
    {
        const std::size_t from = m_pos;
        if (done() || isDigit(m_s[m_pos]) || m_s[m_pos] == u8'-' || m_s[m_pos] == u8'.')
            return std::nullopt;
        while (!done() && !isSpace(m_s[m_pos]) && u8sv(u8"/>=<\"'").find(m_s[m_pos]) == u8sv::npos)
            advance(1);
        if (m_pos == from)
            return std::nullopt;
        return std::u8string(m_s.substr(from, m_pos - from));
    }

    // Text with its entities replaced.
    std::optional<std::u8string> decode(u8sv raw) const
    {
        std::u8string out;
        for (std::size_t i = 0; i < raw.size();) {
            if (raw[i] != u8'&') {
                out += raw[i++];
                continue;
            }
            const std::size_t semi = raw.find(u8';', i);
            if (semi == u8sv::npos)
                return std::nullopt;
            const u8sv entity = raw.substr(i + 1, semi - i - 1);
            if (entity == u8"lt")
                out += u8'<';
            else if (entity == u8"gt")
                out += u8'>';
            else if (entity == u8"amp")
                out += u8'&';
            else if (entity == u8"quot")
                out += u8'"';
            else if (entity == u8"apos")
                out += u8'\'';
            else if (entity.size() > 1 && entity[0] == u8'#') {
                const bool hex = entity[1] == u8'x';
                const u8sv digits = entity.substr(hex ? 2 : 1);
                std::uint32_t cp = 0;
                if (digits.empty() || digits.size() > 8)
                    return std::nullopt;
                for (char8_t c : digits) {
                    const int v = c >= u8'0' && c <= u8'9' ? c - u8'0'
                                  : hex && c >= u8'a' && c <= u8'f' ? c - u8'a' + 10
                                  : hex && c >= u8'A' && c <= u8'F' ? c - u8'A' + 10
                                                                     : -1;
                    if (v < 0)
                        return std::nullopt;
                    cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(v);
                }
                if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                    return std::nullopt;
                appendUtf8(out, cp);
            } else
                return std::nullopt;
            i = semi + 1;
        }
        return out;
    }

    // An element and everything in it, from its '<'. Open elements are kept
    // on a stack, so reading costs no recursion; nesting past kMaxTtmlDepth
    // is refused, so the walks over the tree (and its destruction) stay
    // shallow too.
    std::expected<XmlNode, LyricsProblem> element()
    {
        std::vector<XmlNode> open;
        for (;;) {
            if (done())
                return std::unexpected(broken());
            const std::size_t line = m_line;
            if (at(u8"</")) {
                const auto start = std::pair{m_pos, m_line};
                advance(2);
                const auto closing = name();
                skipSpace();
                if (!closing || open.empty() || *closing != open.back().name || !at(u8">")) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                advance(1);
                XmlNode node = std::move(open.back());
                open.pop_back();
                if (open.empty()) {
                    if (auto skipped = skipMisc(); !skipped)
                        return std::unexpected(skipped.error());
                    if (!done())
                        return std::unexpected(broken());
                    return node;
                }
                open.back().children.push_back(std::move(node));
            } else if (at(u8"<!--") || at(u8"<?")) {
                const auto start = std::pair{m_pos, m_line};
                if (!skipPast(at(u8"<?") ? u8"?>" : u8"-->")) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
            } else if (at(u8"<![CDATA[")) {
                const auto start = std::pair{m_pos, m_line};
                advance(9);
                const std::size_t end = m_s.find(u8"]]>", m_pos);
                if (end == u8sv::npos || open.empty()) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                XmlNode text;
                text.text = std::u8string(m_s.substr(m_pos, end - m_pos));
                text.line = line;
                open.back().children.push_back(std::move(text));
                advance(end + 3 - m_pos);
            } else if (at(u8"<")) {
                const auto start = std::pair{m_pos, m_line};
                if (open.size() >= kMaxTtmlDepth) {
                    LyricsProblem problem = broken();
                    problem.kind = Kind::TooDeep;
                    return std::unexpected(std::move(problem));
                }
                advance(1);
                XmlNode node;
                node.line = line;
                const auto tag = name();
                if (!tag) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                node.name = *tag;
                bool empty = false;
                for (;;) {
                    skipSpace();
                    if (at(u8"/>")) {
                        advance(2);
                        empty = true;
                        break;
                    }
                    if (at(u8">")) {
                        advance(1);
                        break;
                    }
                    const auto attributeStart = std::pair{m_pos, m_line};
                    const auto key = name();
                    skipSpace();
                    if (!key || !at(u8"=")) {
                        std::tie(m_pos, m_line) = attributeStart;
                        return std::unexpected(broken());
                    }
                    advance(1);
                    skipSpace();
                    const char8_t quote = done() ? u8'\0' : m_s[m_pos];
                    const std::size_t end = quote == u8'"' || quote == u8'\'' ? m_s.find(quote, m_pos + 1) : u8sv::npos;
                    const u8sv raw = end == u8sv::npos ? u8sv() : m_s.substr(m_pos + 1, end - m_pos - 1);
                    const auto value = end == u8sv::npos || raw.find(u8'<') != u8sv::npos ? std::nullopt : decode(raw);
                    if (!value) {
                        std::tie(m_pos, m_line) = attributeStart;
                        return std::unexpected(broken());
                    }
                    node.attributes.emplace_back(*key, *value);
                    advance(end + 1 - m_pos);
                    if (!done() && !isSpace(m_s[m_pos]) && !at(u8">") && !at(u8"/>"))
                        return std::unexpected(broken()); // attributes run together
                }
                std::vector<u8sv> keys;
                for (const auto &[key, value] : node.attributes)
                    keys.emplace_back(key);
                std::ranges::sort(keys);
                if (std::ranges::adjacent_find(keys) != keys.end()) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken()); // an attribute given twice
                }
                if (!empty) {
                    open.push_back(std::move(node));
                } else if (open.empty()) {
                    if (auto skipped = skipMisc(); !skipped)
                        return std::unexpected(skipped.error());
                    if (!done())
                        return std::unexpected(broken());
                    return node;
                } else {
                    open.back().children.push_back(std::move(node));
                }
            } else {
                const auto start = std::pair{m_pos, m_line};
                const std::size_t end = std::min(m_s.find(u8'<', m_pos), m_s.size());
                const u8sv raw = m_s.substr(m_pos, end - m_pos);
                if (const std::size_t cdataEnd = raw.find(u8"]]>"); cdataEnd != u8sv::npos) {
                    advance(cdataEnd); // only a CDATA section may end with "]]>"
                    return std::unexpected(broken());
                }
                const auto text = decode(raw);
                if (!text || open.empty()) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                XmlNode node;
                node.text = *text;
                node.line = line;
                open.back().children.push_back(std::move(node));
                advance(end - m_pos);
            }
        }
    }

    u8sv m_s;
    std::size_t m_pos = 0;
    std::size_t m_line = 1;
};

// A TTML time: clock time ("hh:mm:ss.fff", and the "mm:ss.fff" and
// "ss.fff" lyrics files use) or an offset with h, m, s or ms. Frame and
// tick times need rates lyrics files don't give, so they are not read.
std::optional<std::int64_t> ttmlTime(u8sv text)
{
    text = detail::trim(text);
    auto decimal = [](u8sv s) -> std::optional<double> {
        if (s.empty() || s.size() > 15)
            return std::nullopt;
        bool dot = false, digit = false;
        double whole = 0, scale = 1, fraction = 0;
        for (char8_t c : s) {
            if (c == u8'.' && !dot) {
                dot = true;
            } else if (c >= u8'0' && c <= u8'9') {
                digit = true;
                if (dot)
                    fraction += (c - u8'0') * (scale /= 10);
                else
                    whole = whole * 10 + (c - u8'0');
            } else {
                return std::nullopt;
            }
        }
        return digit ? std::optional(whole + fraction) : std::nullopt;
    };
    auto ms = [](double seconds) -> std::optional<std::int64_t> {
        if (seconds * 1000 > static_cast<double>(detail::kLatestLyricsTime))
            return std::nullopt;
        return static_cast<std::int64_t>(std::llround(seconds * 1000));
    };
    static constexpr std::pair<u8sv, double> kUnits[] = {{u8"ms", 0.001}, {u8"h", 3600.0}, {u8"m", 60.0}, {u8"s", 1.0}};
    for (const auto &[unit, factor] : kUnits)
        if (text.ends_with(unit)) {
            const auto n = decimal(text.substr(0, text.size() - unit.size()));
            return n ? ms(*n * factor) : std::nullopt;
        }
    std::vector<u8sv> parts;
    for (std::size_t from = 0;;) {
        const std::size_t colon = text.find(u8':', from);
        parts.push_back(text.substr(from, colon == u8sv::npos ? u8sv::npos : colon - from));
        if (colon == u8sv::npos)
            break;
        from = colon + 1;
    }
    if (parts.size() > 3)
        return std::nullopt; // hh:mm:ss:frames
    double seconds = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const bool last = i + 1 == parts.size();
        if (!last && parts[i].find(u8'.') != u8sv::npos)
            return std::nullopt;
        const auto n = decimal(parts[i]);
        if (!n)
            return std::nullopt;
        seconds = seconds * 60 + *n;
    }
    return ms(seconds);
}

// Whitespace runs as one space (TTML's default xml:space handling).
std::u8string collapsed(u8sv text)
{
    std::u8string out;
    bool space = false;
    for (char8_t c : text) {
        if (isSpace(c)) {
            space = true;
            continue;
        }
        if (space)
            out += u8' ';
        space = false;
        out += c;
    }
    if (space)
        out += u8' ';
    return out;
}

class TtmlReader {
public:
    std::expected<detail::LyricSheet, LyricsProblem> read(const XmlNode &root)
    {
        if (localName(root.name) != u8"tt")
            return std::unexpected(LyricsProblem{Kind::NotTtml, root.line, u8"<" + root.name + u8">"});
        readHead(root);
        for (const XmlNode &child : root.children)
            if (localName(child.name) == u8"body")
                if (auto ok = paragraphs(child); !ok)
                    return std::unexpected(ok.error());
        if (m_sheet.lines.empty())
            return std::unexpected(LyricsProblem{Kind::NoLyrics, 0, {}});
        return std::move(m_sheet);
    }

private:
    // Words as they are read, with the untimed text around them.
    struct Words {
        std::vector<LyricWord> words;
        std::vector<std::size_t> lines;           // each word's line in the file
        std::vector<std::optional<std::int64_t>> ends;
        std::u8string leading;                    // text before the first word
        std::u8string plain;                      // all the text, for a Line without words
    };

    static std::u8string allText(const XmlNode &node)
    {
        std::u8string out;
        for (const XmlNode &child : node.children)
            if (child.isText())
                out += child.text;
            else if (!attribute(child, u8"role"))
                out += allText(child);
        return out;
    }

    void readHead(const XmlNode &root)
    {
        std::optional<std::u8string> musicName;
        auto visit = [&](auto &self, const XmlNode &node) -> void {
            const u8sv local = localName(node.name);
            if (local == u8"title" && !m_sheet.title)
                m_sheet.title = detail::trim(collapsed(allText(node)));
            if (local == u8"meta" && attribute(node, u8"key") == u8sv(u8"musicName") && !musicName)
                musicName = std::u8string(attribute(node, u8"value").value_or(u8""));
            if (local == u8"agent") {
                const auto id = attribute(node, u8"id");
                for (const XmlNode &child : node.children)
                    if (id && localName(child.name) == u8"name")
                        m_agents[std::u8string(*id)] = detail::trim(collapsed(allText(child)));
            }
            for (const XmlNode &child : node.children)
                if (localName(child.name) != u8"body")
                    self(self, child);
        };
        for (const XmlNode &child : root.children)
            if (localName(child.name) == u8"head")
                visit(visit, child);
        if (!m_sheet.title && musicName)
            m_sheet.title = musicName;
    }

    // The begin, end or dur of an element, if it has one.
    static std::expected<std::optional<std::int64_t>, LyricsProblem> time(const XmlNode &node, u8sv which)
    {
        const auto value = attribute(node, which);
        if (!value)
            return std::nullopt;
        const auto t = ttmlTime(*value);
        if (!t)
            return std::unexpected(LyricsProblem{Kind::UnreadableTime, node.line, std::u8string(*value)});
        return t;
    }

    static std::expected<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>, LyricsProblem>
    times(const XmlNode &node)
    {
        const auto begin = time(node, u8"begin");
        if (!begin)
            return std::unexpected(begin.error());
        auto end = time(node, u8"end");
        if (!end)
            return std::unexpected(end.error());
        if (!*end && *begin) {
            const auto dur = time(node, u8"dur");
            if (!dur)
                return std::unexpected(dur.error());
            if (*dur)
                *end = **begin + **dur;
        }
        if (*begin && *end && **end < **begin)
            return std::unexpected(LyricsProblem{Kind::EndBeforeBegin, node.line,
                                                 std::u8string(attribute(node, u8"end").value_or(attribute(node, u8"dur").value_or(u8"")))});
        return std::pair{*begin, *end};
    }

    static bool hasTimedSpan(const XmlNode &node)
    {
        for (const XmlNode &child : node.children)
            if (!child.isText() && !attribute(child, u8"role") && (attribute(child, u8"begin") || hasTimedSpan(child)))
                return true;
        return false;
    }

    static void addText(Words &into, u8sv text)
    {
        into.plain += text;
        if (into.words.empty()) {
            into.leading += text;
            return;
        }
        // Spaces after a word stay with it, one at most.
        std::u8string &word = into.words.back().text;
        for (char8_t c : text)
            if (c != u8' ' || !word.ends_with(u8' '))
                word += c;
    }

    // The words of a <p> (or of a background span), and the background
    // vocals inside it, each a Line of its own.
    std::expected<void, LyricsProblem> collect(const XmlNode &node, Words &into, std::vector<const XmlNode *> &backgrounds)
    {
        for (const XmlNode &child : node.children) {
            if (child.isText()) {
                addText(into, detail::literalText(collapsed(child.text)));
                continue;
            }
            const u8sv local = localName(child.name);
            const auto role = attribute(child, u8"role");
            if (role == u8sv(u8"x-bg")) {
                backgrounds.push_back(&child);
                continue;
            }
            if (role)
                continue; // translations, romanisations and other untimed companions
            if (local == u8"br") {
                addText(into, u8"\\N");
                continue;
            }
            if (local == u8"span" && attribute(child, u8"begin") && !hasTimedSpan(child)) {
                const auto t = times(child);
                if (!t)
                    return std::unexpected(t.error());
                const std::int64_t begin = *t->first;
                if (!into.words.empty() && begin < into.words.back().begin)
                    return std::unexpected(LyricsProblem{Kind::TimeGoesBack, child.line,
                                                         std::u8string(*attribute(child, u8"begin"))});
                const std::u8string text = detail::literalText(collapsed(allText(child)));
                const u8sv word = detail::trimLeft(text);
                if (word.size() < text.size())
                    addText(into, u8" ");
                into.plain += word;
                into.words.push_back(LyricWord{begin, 0, std::u8string(word)});
                into.ends.push_back(t->second);
                into.lines.push_back(child.line);
                continue;
            }
            if (auto ok = collect(child, into, backgrounds); !ok)
                return ok;
        }
        return {};
    }

    std::expected<void, LyricsProblem> line(const XmlNode &node, std::optional<std::int64_t> begin,
                                            std::optional<std::int64_t> end, const std::u8string &actor)
    {
        const auto own = times(node);
        if (!own)
            return std::unexpected(own.error());
        begin = own->first ? own->first : begin;
        end = own->second ? own->second : end;
        Words words;
        std::vector<const XmlNode *> backgrounds;
        if (auto ok = collect(node, words, backgrounds); !ok)
            return ok;
        LyricLine out;
        out.actor = actor;
        if (words.words.empty()) {
            out.text = detail::trim(words.plain);
            if (!out.text.empty()) {
                if (!begin || !end)
                    return std::unexpected(LyricsProblem{Kind::MissingTime, node.line, out.text});
                out.start = *begin;
                out.end = *end;
                m_sheet.lines.push_back(std::move(out));
            }
        } else {
            for (std::size_t i = 0; i < words.words.size(); ++i) {
                const bool last = i + 1 == words.words.size();
                const auto wordEnd = words.ends[i] ? words.ends[i] : last ? end : std::optional(words.words[i + 1].begin);
                if (!wordEnd || *wordEnd < words.words[i].begin)
                    return std::unexpected(LyricsProblem{Kind::MissingTime, words.lines[i], words.words[i].text});
                words.words[i].end = *wordEnd;
            }
            const u8sv leading = detail::trim(words.leading);
            if (!leading.empty())
                words.words.front().text.insert(0, std::u8string(leading) + u8" ");
            out.start = std::min(begin.value_or(words.words.front().begin), words.words.front().begin);
            out.end = std::max(end.value_or(words.words.back().end), words.words.back().end);
            out.words = std::move(words.words);
            m_sheet.lines.push_back(std::move(out));
        }
        for (const XmlNode *background : backgrounds)
            if (auto ok = line(*background, std::nullopt, std::nullopt, actor); !ok)
                return ok;
        return {};
    }

    std::expected<void, LyricsProblem> paragraphs(const XmlNode &node)
    {
        for (const XmlNode &child : node.children) {
            if (child.isText())
                continue;
            if (localName(child.name) == u8"p") {
                const auto agent = attribute(child, u8"agent");
                const auto name = agent ? m_agents.find(std::u8string(*agent)) : m_agents.end();
                const std::u8string actor = name != m_agents.end() && !name->second.empty() ? name->second
                                            : agent                                         ? std::u8string(*agent)
                                                                                            : std::u8string();
                if (auto ok = line(child, std::nullopt, std::nullopt, actor); !ok)
                    return ok;
            } else if (auto ok = paragraphs(child); !ok) {
                return ok;
            }
        }
        return {};
    }

    detail::LyricSheet m_sheet;
    std::map<std::u8string, std::u8string> m_agents;
};

} // namespace

std::expected<LoadResult, LyricsProblem> importTtml(std::span<const std::byte> bytes)
{
    const u8sv text(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size());
    std::size_t line = 1;
    for (std::size_t from = 0; from <= text.size(); ++line) {
        const std::size_t newline = std::min(text.find(u8'\n', from), text.size());
        if (!detail::validUtf8(bytes.data() + from, newline - from))
            return std::unexpected(LyricsProblem{Kind::NotUtf8, line, {}});
        from = newline + 1;
    }
    const auto root = XmlReader(text).document();
    if (!root)
        return std::unexpected(root.error());
    auto sheet = TtmlReader().read(*root);
    if (!sheet)
        return std::unexpected(sheet.error());
    return detail::lyricsDocument(std::move(*sheet));
}

} // namespace hikari::core
