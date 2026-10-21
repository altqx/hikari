// KA6: TTML lyrics. The core has no XML library, so a small reader builds
// the element tree: elements, attributes, text, CDATA and the predefined and
// numeric entities; comments, processing instructions and the DOCTYPE are
// skipped. Names keep their prefixes and are matched by local name.
//
// Lyrics TTML (Apple Music, AMLL TTML Tool) times every <p> and <span> on
// the media timeline, although TTML's own rule makes a child's times
// relative to its parent's begin; the files agree with themselves only when
// read as absolute times, so they are read that way. Lyric text collapses
// whitespace runs unless xml:space="preserve" keeps them; head names (the
// title, agents) are single-line fields and always collapse. A <p> without times
// of its own takes its nearest timed ancestor's (a <div>, the <body>).

#include "hikari/core/lyrics_import.h"

#include "lyrics_lines.h"
#include "text_util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace hikari::core {

namespace {

using detail::LyricLine;
using detail::LyricWord;
using detail::u8sv;
using Kind = LyricsProblem::Kind;

// The namespaces the reader asks for, and "another": an element or
// attribute keeps which one its name is in, never the URI itself, so a
// long namespace on many attributes costs nothing per attribute.
enum class Namespace : std::uint8_t {
    None,         // no namespace: an unprefixed attribute, or an element with no default one
    Xml,          // xml:
    Xmlns,        // xmlns:
    Ttml,         // http://www.w3.org/ns/ttml
    TtmlMetadata, // http://www.w3.org/ns/ttml#metadata (ttm:)
    Other,        // any other namespace, or a prefix no one declares
};

struct XmlNode {
    std::u8string name; // as written, with its prefix; empty for text
    std::vector<std::pair<std::u8string, std::u8string>> attributes;
    std::vector<std::size_t> attributeLines; // the line each attribute's value starts on
    std::vector<Namespace> attributeNamespaces; // the namespace each attribute's prefix names
    std::vector<XmlNode> children;
    std::u8string text;
    std::size_t line = 0;
    Namespace ns = Namespace::None; // the element's

    bool isText() const { return name.empty(); }
};

// Whether a name is a QName (Namespaces in XML): a local name, or a
// prefix, one colon and a local name, neither empty.
bool qualifiedName(u8sv name)
{
    const std::size_t colon = name.find(u8':');
    return colon == u8sv::npos || (colon > 0 && colon + 1 < name.size() && name.find(u8':', colon + 1) == u8sv::npos);
}

u8sv localName(u8sv name)
{
    const std::size_t colon = name.rfind(u8':');
    return colon == u8sv::npos ? name : name.substr(colon + 1);
}

constexpr u8sv kXmlNamespace = u8"http://www.w3.org/XML/1998/namespace";
constexpr u8sv kXmlnsNamespace = u8"http://www.w3.org/2000/xmlns/";
constexpr u8sv kTtmlUri = u8"http://www.w3.org/ns/ttml";
constexpr u8sv kTtmlMetadataUri = u8"http://www.w3.org/ns/ttml#metadata";

// An attribute by its namespace and local name. TTML's timing attributes
// and AMLL's meta fields have none (unprefixed); ttm:agent and ttm:role are
// in TTML's metadata namespace, xml:id in XML's. So "foo:begin" is not a
// begin.
std::optional<std::size_t> attributeAt(const XmlNode &node, u8sv local, Namespace ns)
{
    for (std::size_t i = 0; i < node.attributes.size(); ++i)
        if (node.attributeNamespaces[i] == ns && localName(node.attributes[i].first) == local)
            return i;
    return std::nullopt;
}

std::optional<u8sv> attribute(const XmlNode &node, u8sv local, Namespace ns = Namespace::None)
{
    const auto i = attributeAt(node, local, ns);
    return i ? std::optional(u8sv(node.attributes[*i].second)) : std::nullopt;
}

// The line an attribute's value starts on, or the element's when it has none.
std::size_t attributeLine(const XmlNode &node, u8sv local, Namespace ns = Namespace::None)
{
    const auto i = attributeAt(node, local, ns);
    return i ? node.attributeLines[*i] : node.line;
}

Namespace knownNamespace(u8sv uri)
{
    return uri.empty()                ? Namespace::None
           : uri == kTtmlUri          ? Namespace::Ttml
           : uri == kTtmlMetadataUri  ? Namespace::TtmlMetadata
           : uri == kXmlNamespace     ? Namespace::Xml
           : uri == kXmlnsNamespace   ? Namespace::Xmlns
                                      : Namespace::Other;
}

// The namespace declarations in scope while reading (Namespaces in XML):
// each prefix ("" for the default namespace) to the URIs the open elements
// declare for it, innermost last, so resolving a name is one lookup however
// many declarations and attributes a file has. Each URI is kept once, by
// number.
class NamespaceScope {
public:
    // Where a name is: its namespace and, for another one, its URI's number
    // (kUndeclared for a prefix no one declares).
    struct Place {
        Namespace ns = Namespace::None;
        std::uint32_t uri = 0;
    };
    static constexpr std::uint32_t kUndeclared = UINT32_MAX;

    // An element's xmlns and xmlns:prefix attributes come into scope, if
    // Namespaces in XML allows them: xml only for its own namespace, never
    // xmlns, neither namespace for another prefix or the default, and no
    // empty namespace for a prefix (only XML 1.1 may undeclare one). False,
    // with nothing changed, when one breaks those rules.
    bool enter(const XmlNode &node)
    {
        for (const auto &[key, value] : node.attributes)
            if (const auto prefix = declares(key)) {
                const bool reserved = value == kXmlNamespace || value == kXmlnsNamespace;
                if (*prefix == u8"xml" ? value != kXmlNamespace
                                       : *prefix == u8"xmlns" || reserved || (!prefix->empty() && value.empty()))
                    return false;
            }
        for (const auto &[key, value] : node.attributes)
            if (const auto prefix = declares(key)) {
                auto [uri, added] = m_uris.try_emplace(value, static_cast<std::uint32_t>(m_kinds.size()));
                if (added)
                    m_kinds.push_back(knownNamespace(value));
                auto it = m_declared.find(*prefix);
                if (it == m_declared.end())
                    it = m_declared.try_emplace(std::u8string(*prefix)).first;
                it->second.push_back(uri->second);
            }
        return true;
    }
    // ...and leave it with the element.
    void leave(const XmlNode &node)
    {
        for (const auto &[key, value] : node.attributes)
            if (const auto prefix = declares(key))
                m_declared.find(*prefix)->second.pop_back();
    }

    // xml and xmlns are their own; another prefix is in the namespace
    // declared for it. An unprefixed attribute is in none; an unprefixed
    // element in the default namespace, if one is declared. A ttm: no one
    // declares is TTML's metadata namespace, as such files mean; another
    // undeclared prefix is kUndeclared, which the reader refuses.
    Place of(u8sv name, bool element) const
    {
        const std::size_t colon = name.find(u8':');
        if (colon == u8sv::npos && !element)
            return {};
        const u8sv prefix = colon == u8sv::npos ? u8sv() : name.substr(0, colon);
        if (prefix == u8"xml")
            return {Namespace::Xml};
        if (prefix == u8"xmlns")
            return {Namespace::Xmlns};
        if (const auto it = m_declared.find(prefix); it != m_declared.end() && !it->second.empty())
            return {m_kinds[it->second.back()], it->second.back()};
        if (prefix.empty())
            return {};
        return prefix == u8"ttm" ? Place{Namespace::TtmlMetadata} : Place{Namespace::Other, kUndeclared};
    }

private:
    // The prefix an attribute declares, "" for the default namespace.
    static std::optional<u8sv> declares(u8sv key)
    {
        if (key == u8"xmlns")
            return u8sv();
        if (key.starts_with(u8"xmlns:"))
            return key.substr(6);
        return std::nullopt;
    }

    struct Hash {
        using is_transparent = void;
        std::size_t operator()(u8sv s) const { return std::hash<u8sv>{}(s); }
    };
    std::unordered_map<std::u8string, std::vector<std::uint32_t>, Hash, std::equal_to<>> m_declared;
    std::unordered_map<std::u8string, std::uint32_t, Hash, std::equal_to<>> m_uris;
    std::vector<Namespace> m_kinds; // each URI's, by number
};

// Whether a node is another namespace's element, which TTML's content
// leaves out with all it holds: its text, its times and its spans.
bool foreign(const XmlNode &node)
{
    return !node.isText() && node.ns != Namespace::Ttml && node.ns != Namespace::None;
}

// Whether an element is TTML's `local`: in TTML's namespace, or in none
// (files that leave the namespace out), never in another.
bool isTtml(const XmlNode &node, u8sv local)
{
    return !node.isText() && (node.ns == Namespace::Ttml || node.ns == Namespace::None) && localName(node.name) == local;
}

bool isSpace(char8_t c)
{
    return c == u8' ' || c == u8'\t' || c == u8'\r' || c == u8'\n';
}

bool isDigit(char8_t c)
{
    return c >= u8'0' && c <= u8'9';
}

// XML's Char: tab, line feed, carriage return and U+0020 on, without the
// surrogates and the noncharacters U+FFFE and U+FFFF.
bool xmlChar(std::uint32_t cp)
{
    return cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) ||
           (cp >= 0x10000 && cp <= 0x10FFFF);
}

// The code point at `i` of valid UTF-8 and its length in bytes.
std::pair<std::uint32_t, std::size_t> codePointAt(u8sv s, std::size_t i)
{
    const auto lead = static_cast<unsigned char>(s[i]);
    const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    std::uint32_t cp = length == 1 ? lead : lead & (0x7F >> length);
    for (std::size_t k = 1; k < length && i + k < s.size(); ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    return {cp, length};
}

// XML's NameStartChar and NameChar.
bool nameStartChar(std::uint32_t cp)
{
    return cp == u8':' || cp == u8'_' || (cp >= u8'A' && cp <= u8'Z') || (cp >= u8'a' && cp <= u8'z') ||
           (cp >= 0xC0 && cp <= 0xD6) || (cp >= 0xD8 && cp <= 0xF6) || (cp >= 0xF8 && cp <= 0x2FF) ||
           (cp >= 0x370 && cp <= 0x37D) || (cp >= 0x37F && cp <= 0x1FFF) || (cp >= 0x200C && cp <= 0x200D) ||
           (cp >= 0x2070 && cp <= 0x218F) || (cp >= 0x2C00 && cp <= 0x2FEF) || (cp >= 0x3001 && cp <= 0xD7FF) ||
           (cp >= 0xF900 && cp <= 0xFDCF) || (cp >= 0xFDF0 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0xEFFFF);
}

bool nameChar(std::uint32_t cp)
{
    return nameStartChar(cp) || cp == u8'-' || cp == u8'.' || (cp >= u8'0' && cp <= u8'9') || cp == 0xB7 ||
           (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x203F && cp <= 0x2040);
}

// XML's Name: a NameStartChar, then NameChars.
bool xmlName(u8sv text)
{
    for (std::size_t i = 0; i < text.size();) {
        const auto [cp, length] = codePointAt(text, i);
        if (!(i == 0 ? nameStartChar(cp) : nameChar(cp)))
            return false;
        i += length;
    }
    return !text.empty();
}

// The character a reference's "#65" or "#x41" names, if it is one XML
// allows.
std::optional<std::uint32_t> characterReference(u8sv reference)
{
    if (reference.size() < 2 || reference[0] != u8'#')
        return std::nullopt;
    const bool hex = reference[1] == u8'x';
    const u8sv digits = reference.substr(hex ? 2 : 1);
    if (digits.empty())
        return std::nullopt;
    // Any number of digits, leading zeros too ("&#00000065;"); past
    // U+10FFFF the value stops growing, as no character is there.
    std::uint32_t cp = 0;
    for (char8_t c : digits) {
        const int v = c >= u8'0' && c <= u8'9'           ? c - u8'0'
                      : hex && c >= u8'a' && c <= u8'f' ? c - u8'a' + 10
                      : hex && c >= u8'A' && c <= u8'F' ? c - u8'A' + 10
                                                         : -1;
        if (v < 0)
            return std::nullopt;
        cp = std::min<std::uint32_t>(cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(v), 0x110000);
    }
    return xmlChar(cp) ? std::optional(cp) : std::nullopt;
}

// Whether a DTD literal's references follow XML's grammar: each "&" starts
// "&name;" or a character reference, and with `parameters` (an
// EntityValue) each "%" starts "%name;". Which entities they name is not
// checked, as none is ever expanded.
bool referencesWritten(u8sv text, bool parameters)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != u8'&' && !(parameters && text[i] == u8'%'))
            continue;
        const std::size_t semi = text.find(u8';', i);
        if (semi == u8sv::npos)
            return false;
        const u8sv reference = text.substr(i + 1, semi - i - 1);
        if (!(text[i] == u8'&' && reference.starts_with(u8'#') ? characterReference(reference).has_value() : xmlName(reference)))
            return false;
        i = semi;
    }
    return true;
}

// XML's PubidChar: what a public identifier may hold.
bool pubidChar(char8_t c)
{
    return c == u8' ' || c == u8'\r' || c == u8'\n' || (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') ||
           (c >= u8'0' && c <= u8'9') || u8sv(u8"-'()+,./:=?;!*#@$_%").find(c) != u8sv::npos;
}

// "U+0001": a character XML forbids, named for the message.
std::u8string codePointName(std::uint32_t cp)
{
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "U+%04X", static_cast<unsigned>(cp));
    return std::u8string(reinterpret_cast<const char8_t *>(buffer));
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
    explicit XmlReader(u8sv text) : m_s(text), m_nodesLeft(text.size() / kTtmlBytesPerNode + 4096) {}

    std::expected<XmlNode, LyricsProblem> document()
    {
        if (m_s.starts_with(u8"\uFEFF"))
            m_pos = 3;
        m_start = m_pos;
        if (auto skipped = skipMisc(true); !skipped)
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
        // A line ends at LF, CRLF or a lone CR, as XML ends lines.
        for (std::size_t end = std::min(m_s.size(), m_pos + n); m_pos < end; ++m_pos)
            if (m_s[m_pos] == u8'\n' || (m_s[m_pos] == u8'\r' && (m_pos + 1 == m_s.size() || m_s[m_pos + 1] != u8'\n')))
                ++m_line;
    }

    void skipSpace()
    {
        while (!done() && isSpace(m_s[m_pos]))
            advance(1);
    }

    // The text here, up to the end of its line or tag, as the excerpt.
    // Where in `raw` the line holding raw[at] starts.
    static std::size_t lineStartIn(u8sv raw, std::size_t at)
    {
        const std::size_t breakAt = raw.substr(0, at).find_last_of(u8"\r\n");
        return breakAt == u8sv::npos ? 0 : breakAt + 1;
    }

    LyricsProblem broken() const
    {
        u8sv rest = m_s.substr(std::min(m_pos, m_s.size()));
        std::size_t n = std::min<std::size_t>(rest.find_first_of(u8">\r\n"), 40);
        if (n < rest.size() && rest[n] == u8'>')
            ++n;
        while (n < rest.size() && n > 0 && (static_cast<unsigned char>(rest[n]) & 0xC0) == 0x80)
            --n; // whole characters only
        return LyricsProblem{Kind::NotWellFormed, m_line, std::u8string(detail::trimRight(rest.substr(0, n)))};
    }

    // A comment, from its "<!--": it may not hold "--" or end with "-".
    bool skipComment()
    {
        const std::size_t end = m_s.find(u8"-->", m_pos + 4);
        if (end == u8sv::npos)
            return false;
        const u8sv body = m_s.substr(m_pos + 4, end - m_pos - 4);
        if (body.find(u8"--") != u8sv::npos || body.ends_with(u8'-'))
            return false;
        advance(end + 3 - m_pos);
        return true;
    }

    // A processing instruction, from its "<?": a target name, then a space
    // or "?>". The target "xml" (in any case) is the XML declaration's,
    // written in lower case at the very start of the file.
    bool skipPi()
    {
        const bool first = m_pos == m_start;
        advance(2);
        const auto target = name();
        if (!target)
            return false;
        std::u8string lower = *target;
        for (char8_t &c : lower)
            c = c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c;
        if (lower == u8"xml" && (!first || *target != u8"xml"))
            return false;
        if (lower == u8"xml")
            return xmlDeclaration();
        if (!at(u8"?>") && (done() || !isSpace(m_s[m_pos])))
            return false;
        return skipPast(u8"?>");
    }

    // The rest of an XML declaration after "<?xml": version="1.n", then
    // optionally encoding="name" and standalone="yes" or "no", each after
    // whitespace and in that order, then "?>". This reader is an XML 1.0
    // processor, and XML 1.0 (section 2.8) has one read a 1.n document as
    // 1.0: it is accepted when it uses nothing 1.0 lacks, so a reference
    // such as "&#1;", which only XML 1.1 allows, is still broken XML here.
    bool xmlDeclaration()
    {
        // A field: absent (0, nothing read), well formed (1, `value` set)
        // or broken (-1).
        auto field = [&](u8sv key, u8sv &value) {
            const auto start = std::pair{m_pos, m_line};
            if (done() || !isSpace(m_s[m_pos]))
                return 0;
            skipSpace();
            if (!at(key)) {
                std::tie(m_pos, m_line) = start;
                return 0;
            }
            advance(key.size());
            skipSpace();
            if (!at(u8"="))
                return -1;
            advance(1);
            skipSpace();
            const char8_t quote = done() ? u8'\0' : m_s[m_pos];
            const std::size_t close = quote == u8'"' || quote == u8'\'' ? m_s.find(quote, m_pos + 1) : u8sv::npos;
            if (close == u8sv::npos)
                return -1;
            value = m_s.substr(m_pos + 1, close - m_pos - 1);
            advance(close + 1 - m_pos);
            return 1;
        };
        auto letter = [](char8_t c) { return (c >= u8'A' && c <= u8'Z') || (c >= u8'a' && c <= u8'z'); };
        auto digit = [](char8_t c) { return c >= u8'0' && c <= u8'9'; };
        u8sv value;
        if (field(u8"version", value) != 1 || value.size() < 3 || !value.starts_with(u8"1.") ||
            !std::ranges::all_of(value.substr(2), digit))
            return false;
        int found = field(u8"encoding", value);
        if (found < 0 || (found > 0 && (value.empty() || !letter(value.front()) ||
                                        !std::ranges::all_of(value, [&](char8_t c) {
                                            return letter(c) || digit(c) || c == u8'.' || c == u8'_' || c == u8'-';
                                        }))))
            return false;
        found = field(u8"standalone", value);
        if (found < 0 || (found > 0 && value != u8"yes" && value != u8"no"))
            return false;
        skipSpace();
        if (!at(u8"?>"))
            return false;
        advance(2);
        return true;
    }

    bool skipPast(u8sv terminator)
    {
        const std::size_t end = m_s.find(terminator, m_pos);
        if (end == u8sv::npos)
            return false;
        advance(end + terminator.size() - m_pos);
        return true;
    }

    // Whitespace, comments and processing instructions between the
    // top-level parts of the file, and in the prolog (before the root
    // element) one DOCTYPE: "<!DOCTYPE", a space and the root's name, then
    // an external id and an internal subset as XML allows them.
    std::expected<void, LyricsProblem> skipMisc(bool prolog)
    {
        for (;;) {
            skipSpace();
            const auto start = std::pair{m_pos, m_line};
            bool ok = true;
            if (at(u8"<?"))
                ok = skipPi();
            else if (at(u8"<!--"))
                ok = skipComment();
            else if (at(u8"<!DOCTYPE")) {
                ok = prolog && !m_doctype;
                m_doctype = true;
                if (ok) {
                    advance(9);
                    ok = !done() && isSpace(m_s[m_pos]);
                    skipSpace();
                    if (ok)
                        m_doctypeName = name();
                    ok = ok && m_doctypeName.has_value();
                }
                // Then XML's doctypedecl: an external id (SYSTEM "uri", or
                // PUBLIC "id" "uri"), an internal subset in "[ ]", and ">".
                // Quoted literals (entity values) and comments in the subset
                // may hold "]" and ">".
                if (ok && space()) {
                    if (at(u8"SYSTEM") || at(u8"PUBLIC"))
                        ok = externalId(false);
                    skipSpace();
                }
                // The subset (XML's intSubset): markup declarations, PIs,
                // comments, parameter-entity references and whitespace.
                if (ok && at(u8"[")) {
                    advance(1);
                    for (skipSpace(); ok && !at(u8"]"); skipSpace()) {
                        if (done()) {
                            ok = false;
                        } else if (at(u8"<!--")) {
                            ok = skipComment();
                        } else if (at(u8"<?")) {
                            ok = skipPi();
                        } else if (at(u8"%")) { // a parameter-entity reference, "%name;"
                            advance(1);
                            ok = name().has_value() && at(u8";");
                            if (ok)
                                advance(1);
                        } else {
                            ok = markupDeclaration();
                        }
                    }
                    if (ok)
                        advance(1); // the "]"
                    skipSpace();
                }
                ok = ok && at(u8">");
                if (ok)
                    advance(1);
            } else
                return {};
            if (!ok) {
                std::tie(m_pos, m_line) = start;
                return std::unexpected(broken());
            }
        }
    }

    // A name, by XML's grammar: a NameStartChar, then NameChars. What
    // follows it is the caller's to check ("<a$>" is broken there).
    // The internal subset's markup declarations, each read by its
    // production in XML 1.0 (elementdecl, AttlistDecl, EntityDecl,
    // NotationDecl), so a malformed one is not well-formed XML. They are
    // checked, never used: entities are not expanded.
    bool markupDeclaration()
    {
        bool ok = false;
        if (keyword(u8"<!ELEMENT"))
            ok = space() && name() && space() && contentSpec();
        else if (keyword(u8"<!ATTLIST"))
            ok = attlistDeclaration();
        else if (keyword(u8"<!ENTITY"))
            ok = entityDeclaration();
        else if (keyword(u8"<!NOTATION"))
            ok = space() && name() && space() && externalId(true);
        skipSpace();
        return ok && keyword(u8">");
    }

    bool keyword(u8sv word)
    {
        if (!at(word))
            return false;
        advance(word.size());
        return true;
    }
    // Required whitespace (S), all of it.
    bool space()
    {
        const bool any = !done() && isSpace(m_s[m_pos]);
        skipSpace();
        return any;
    }
    // A quoted literal; an attribute value (AttValue) holds no "<".
    // A quoted literal of the DTD, held to its grammar: a SystemLiteral
    // holds anything, a PubidLiteral only PubidChars, an AttValue no "<"
    // and only references as "&" starts them, an EntityValue the same with
    // "%" too.
    enum class Literal : std::uint8_t { System, Pubid, AttValue, EntityValue };
    bool quoted(Literal literal)
    {
        if (done() || (m_s[m_pos] != u8'"' && m_s[m_pos] != u8'\''))
            return false;
        const std::size_t close = m_s.find(m_s[m_pos], m_pos + 1);
        if (close == u8sv::npos)
            return false;
        const u8sv text = m_s.substr(m_pos + 1, close - m_pos - 1);
        const bool written = literal == Literal::Pubid      ? std::ranges::all_of(text, pubidChar)
                             : literal == Literal::AttValue ? text.find(u8'<') == u8sv::npos && referencesWritten(text, false)
                             : literal == Literal::EntityValue ? referencesWritten(text, true)
                                                               : true;
        if (!written)
            return false;
        advance(close + 1 - m_pos);
        return true;
    }
    bool nmtoken()
    {
        const std::size_t from = m_pos;
        while (!done()) {
            const auto [cp, length] = codePointAt(m_s, m_pos);
            if (!nameChar(cp))
                break;
            advance(length);
        }
        return m_pos != from;
    }
    // ExternalID: SYSTEM "uri", or PUBLIC "id" "uri"; a notation's PUBLIC
    // "id" alone (PublicID) with `notation`.
    bool externalId(bool notation)
    {
        if (keyword(u8"SYSTEM"))
            return space() && quoted(Literal::System);
        if (!keyword(u8"PUBLIC") || !space() || !quoted(Literal::Pubid))
            return false;
        const auto mark = std::pair{m_pos, m_line};
        if (space() && quoted(Literal::System))
            return true;
        std::tie(m_pos, m_line) = mark;
        return notation;
    }
    // contentspec: EMPTY, ANY, Mixed ("(#PCDATA|a|b)*") or children.
    bool contentSpec()
    {
        if (keyword(u8"EMPTY") || keyword(u8"ANY"))
            return true;
        if (!keyword(u8"("))
            return false;
        skipSpace();
        if (!keyword(u8"#PCDATA"))
            return contentGroup(1);
        bool names = false;
        for (skipSpace(); keyword(u8"|"); skipSpace(), names = true) {
            skipSpace();
            if (!name())
                return false;
        }
        if (!keyword(u8")"))
            return false;
        return keyword(u8"*") || !names;
    }
    // A choice or seq after its "(": content particles split by "|" or by
    // ",", not both, then ")" and an optional "?", "*" or "+". Nested no
    // deeper than elements may be.
    bool contentGroup(std::size_t depth)
    {
        if (depth > kMaxTtmlDepth)
            return false;
        char8_t separator = 0;
        for (;;) {
            if (keyword(u8"(")) {
                skipSpace();
                if (!contentGroup(depth + 1))
                    return false;
            } else if (!name()) {
                return false;
            } else {
                occurrence();
            }
            skipSpace();
            if (keyword(u8")"))
                break;
            const char8_t c = done() ? char8_t(0) : m_s[m_pos];
            if ((c != u8'|' && c != u8',') || (separator != 0 && c != separator))
                return false;
            separator = c;
            advance(1);
            skipSpace();
        }
        occurrence();
        return true;
    }
    void occurrence()
    {
        if (!done() && (m_s[m_pos] == u8'?' || m_s[m_pos] == u8'*' || m_s[m_pos] == u8'+'))
            advance(1);
    }
    // AttlistDecl after its keyword: the element's name, then each AttDef
    // (a name, an AttType and a DefaultDecl).
    bool attlistDeclaration()
    {
        if (!space() || !name())
            return false;
        for (;;) {
            const bool spaced = space();
            if (at(u8">"))
                return true;
            if (!spaced || !name() || !space() || !attributeType() || !space())
                return false;
            if (keyword(u8"#REQUIRED") || keyword(u8"#IMPLIED"))
                continue;
            if (keyword(u8"#FIXED") && !space())
                return false;
            if (!quoted(Literal::AttValue))
                return false;
        }
    }
    bool attributeType()
    {
        // The longer of two names that share a start first (IDREFS before
        // IDREF before ID, ENTITIES before ENTITY).
        for (u8sv type : {u8sv(u8"CDATA"), u8sv(u8"IDREFS"), u8sv(u8"IDREF"), u8sv(u8"ID"), u8sv(u8"ENTITIES"),
                          u8sv(u8"ENTITY"), u8sv(u8"NMTOKENS"), u8sv(u8"NMTOKEN")})
            if (keyword(type))
                return true;
        const bool notation = keyword(u8"NOTATION");
        if (notation && !space())
            return false;
        if (!keyword(u8"("))
            return false;
        // An Enumeration of name tokens, or a NotationType of names.
        for (;;) {
            skipSpace();
            if (notation ? !name() : !nmtoken())
                return false;
            skipSpace();
            if (keyword(u8")"))
                return true;
            if (!keyword(u8"|"))
                return false;
        }
    }
    // EntityDecl after its keyword: a general entity's name and value or
    // external id (NDATA too), or "%", a parameter entity's.
    bool entityDeclaration()
    {
        if (!space())
            return false;
        const bool parameter = keyword(u8"%");
        if ((parameter && !space()) || !name() || !space())
            return false;
        if (quoted(Literal::EntityValue))
            return true;
        if (!externalId(false))
            return false;
        const auto mark = std::pair{m_pos, m_line};
        if (!parameter && space() && keyword(u8"NDATA"))
            return space() && name();
        std::tie(m_pos, m_line) = mark;
        return true;
    }

    std::optional<std::u8string> name()
    {
        const std::size_t from = m_pos;
        while (!done()) {
            const auto [cp, length] = codePointAt(m_s, m_pos);
            if (!(m_pos == from ? nameStartChar(cp) : nameChar(cp)))
                break;
            advance(length);
        }
        if (m_pos == from)
            return std::nullopt;
        return std::u8string(m_s.substr(from, m_pos - from));
    }

    // Text with its entities replaced, or where in it the entity that
    // cannot be read starts.
    std::expected<std::u8string, std::size_t> decode(u8sv raw) const
    {
        std::u8string out;
        for (std::size_t i = 0; i < raw.size();) {
            if (raw[i] != u8'&') {
                out += raw[i++];
                continue;
            }
            const std::size_t semi = raw.find(u8';', i);
            if (semi == u8sv::npos)
                return std::unexpected(i);
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
            else if (const auto cp = characterReference(entity))
                appendUtf8(out, *cp);
            else
                return std::unexpected(i);
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
                m_scope.leave(open.back());
                XmlNode node = std::move(open.back());
                open.pop_back();
                if (open.empty()) {
                    if (auto skipped = skipMisc(false); !skipped)
                        return std::unexpected(skipped.error());
                    if (!done())
                        return std::unexpected(broken());
                    return node;
                }
                open.back().children.push_back(std::move(node));
            } else if (at(u8"<!--") || at(u8"<?")) {
                const auto start = std::pair{m_pos, m_line};
                if (!(at(u8"<?") ? skipPi() : skipComment())) {
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
                if (!takeNode())
                    return std::unexpected(tooLarge());
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
                if (!takeNode())
                    return std::unexpected(tooLarge());
                advance(1);
                XmlNode node;
                node.line = line;
                const auto tag = name();
                if (!tag || !qualifiedName(*tag)) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                if (open.empty() && m_doctypeName && *tag != *m_doctypeName) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken()); // the root is not the one the DOCTYPE names
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
                    if (!key || !qualifiedName(*key) || !at(u8"=")) {
                        std::tie(m_pos, m_line) = attributeStart;
                        return std::unexpected(broken());
                    }
                    advance(1);
                    skipSpace();
                    const char8_t quote = done() ? u8'\0' : m_s[m_pos];
                    const std::size_t end = quote == u8'"' || quote == u8'\'' ? m_s.find(quote, m_pos + 1) : u8sv::npos;
                    const u8sv raw = end == u8sv::npos ? u8sv() : m_s.substr(m_pos + 1, end - m_pos - 1);
                    if (end == u8sv::npos || raw.find(u8'<') != u8sv::npos) {
                        std::tie(m_pos, m_line) = attributeStart;
                        return std::unexpected(broken());
                    }
                    const auto value = decode(raw);
                    if (!value) {
                        // From the start of the entity's line, the attribute's on its first.
                        if (const std::size_t from = lineStartIn(raw, value.error()); from > 0)
                            advance(1 + from);
                        else
                            std::tie(m_pos, m_line) = attributeStart;
                        return std::unexpected(broken());
                    }
                    node.attributes.emplace_back(*key, *value);
                    node.attributeLines.push_back(m_line);
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
                // Its declarations within Namespaces in XML's rules, and no
                // element in the xmlns namespace.
                if (node.name.starts_with(u8"xmlns:") || !m_scope.enter(node)) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                // Every prefix declared (ttm: aside, as such files mean TTML's
                // metadata by it), and no attribute twice by expanded name,
                // the namespace and the local name, as a:role and b:role with
                // one namespace are.
                std::vector<std::tuple<Namespace, std::uint32_t, u8sv>> expanded;
                for (const auto &[key, value] : node.attributes) {
                    const auto place = m_scope.of(key, false);
                    if (place.uri == NamespaceScope::kUndeclared) {
                        std::tie(m_pos, m_line) = start;
                        return std::unexpected(broken()); // a prefix no one declares
                    }
                    node.attributeNamespaces.push_back(place.ns);
                    expanded.emplace_back(place.ns, place.ns == Namespace::Other ? place.uri : 0, localName(key));
                }
                const auto place = m_scope.of(node.name, true);
                if (place.uri == NamespaceScope::kUndeclared) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken()); // a prefix no one declares
                }
                node.ns = place.ns;
                std::ranges::sort(expanded);
                if (std::ranges::adjacent_find(expanded) != expanded.end()) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken()); // an attribute given twice
                }
                if (empty)
                    m_scope.leave(node);
                if (!empty) {
                    open.push_back(std::move(node));
                } else if (open.empty()) {
                    if (auto skipped = skipMisc(false); !skipped)
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
                if (!text) {
                    advance(lineStartIn(raw, text.error())); // from the start of the entity's line
                    return std::unexpected(broken());
                }
                if (open.empty()) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                if (!takeNode())
                    return std::unexpected(tooLarge());
                XmlNode node;
                node.text = *text;
                node.line = line;
                open.back().children.push_back(std::move(node));
                advance(end - m_pos);
            }
        }
    }

    // One more node in the tree, if the budget has room (kTtmlBytesPerNode).
    bool takeNode()
    {
        if (m_nodesLeft == 0)
            return false;
        --m_nodesLeft;
        return true;
    }
    LyricsProblem tooLarge() const { return LyricsProblem{Kind::TooLarge, m_line, {}}; }

    u8sv m_s;
    std::size_t m_pos = 0;
    std::size_t m_line = 1;
    std::size_t m_nodesLeft = 0;
    std::size_t m_start = 0; // after the byte order mark
    bool m_doctype = false;
    std::optional<std::u8string> m_doctypeName; // the root the DOCTYPE names
    NamespaceScope m_scope;
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
        // Digits, then a "." and digits if there is a fraction ("1.5", not
        // "1." or ".5").
        bool dot = false, wholeDigit = false, fractionDigit = false;
        double whole = 0, scale = 1, fraction = 0;
        for (char8_t c : s) {
            if (c == u8'.' && !dot) {
                dot = true;
            } else if (c >= u8'0' && c <= u8'9') {
                (dot ? fractionDigit : wholeDigit) = true;
                if (dot)
                    fraction += (c - u8'0') * (scale /= 10);
                else
                    whole = whole * 10 + (c - u8'0');
            } else {
                return std::nullopt;
            }
        }
        return wholeDigit && fractionDigit == dot ? std::optional(whole + fraction) : std::nullopt;
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
        if (!n || (i > 0 && *n >= 60)) // a clock's minutes and seconds
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

// Whether text under `node` keeps its whitespace: its xml:space, or else
// its parent's (`inherited`).
bool preserves(const XmlNode &node, bool inherited)
{
    for (const auto &[name, value] : node.attributes)
        if (name == u8"xml:space")
            return value == u8"preserve" ? true : value == u8"default" ? false : inherited;
    return inherited;
}

// A space xml:space="preserve" keeps, until its Line is put together. No
// text of a well-formed file can hold U+0001, so it stands for one alone.
constexpr char8_t kKeptSpace = 1;

bool blank(char8_t c)
{
    return isSpace(c) || c == kKeptSpace;
}

u8sv trimBlankLeft(u8sv text)
{
    while (!text.empty() && blank(text.front()))
        text.remove_prefix(1);
    return text;
}

// A Line's text, or a word of it, put together. At the Line's ends
// (`leading`, `trailing`) ordinary whitespace goes, and kept spaces become
// hard spaces (\h): ASS trims a Line's text, so plain spaces there would be
// lost. Inside the Line a kept space is a space.
std::u8string finished(u8sv text, bool leading, bool trailing)
{
    std::size_t from = 0, to = text.size();
    while (leading && from < to && blank(text[from]))
        ++from;
    while (trailing && to > from && blank(text[to - 1]))
        --to;
    std::u8string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool edge = i < from || i >= to;
        if (text[i] == kKeptSpace)
            out += edge ? u8"\\h" : u8" ";
        else if (!edge)
            out += text[i];
    }
    return out;
}

// The text as a message shows it: kept spaces as spaces, ends trimmed.
std::u8string readable(u8sv text)
{
    std::u8string out(text);
    std::ranges::replace(out, kKeptSpace, u8' ');
    return std::u8string(detail::trim(out));
}

// A text node's lyrics, literal: whitespace runs as one space (TTML's
// default), or under xml:space="preserve" as written, a space or tab a kept
// space and a line end a line break.
std::u8string lyricText(u8sv raw, bool preserve)
{
    if (!preserve)
        return detail::literalText(collapsed(raw));
    std::u8string out;
    for (std::size_t from = 0; from <= raw.size();) {
        const auto [end, next] = detail::lineAt(raw, from);
        std::u8string piece(raw.substr(from, end - from));
        std::ranges::replace(piece, u8' ', kKeptSpace);
        std::ranges::replace(piece, u8'\t', kKeptSpace);
        if (from > 0)
            out += u8"\\N";
        out += detail::literalText(piece);
        from = next;
    }
    return out;
}

class TtmlReader {
public:
    explicit TtmlReader(std::size_t budget) { m_sheet.budget = budget; }

    std::expected<detail::LyricSheet, LyricsProblem> read(const XmlNode &root)
    {
        if (!isTtml(root, u8"tt"))
            return std::unexpected(LyricsProblem{Kind::NotTtml, root.line, u8"<" + root.name + u8">"});
        readHead(root);
        for (const XmlNode &child : root.children)
            if (isTtml(child, u8"body"))
                if (auto ok = paragraphs(child, std::nullopt, std::nullopt, preserves(root, false)); !ok)
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
        std::vector<bool> own;                    // each word: whether it is a span's or paragraph's own text
        std::u8string leading;                    // text before the first word
        std::u8string plain;                      // all the text, for a Line without words
        const XmlNode *textOf = nullptr;          // the timed span whose own text the last word is
        std::optional<std::int64_t> timedBegin;   // the last timed span's word's begin (own text sets no order)
        std::optional<std::int64_t> lineBegin;    // the Line's begin, which a dur inside it counts from
    };
    // A background vocal span: whether its parent keeps whitespace, and the
    // times of the nearest timed span around it, which time it in place of
    // its paragraph's.
    struct Background {
        const XmlNode *node;
        bool preserve;
        std::optional<std::int64_t> begin, end;
    };
    using Backgrounds = std::vector<Background>;

    // The text of a head element (a title, an agent's name); a <br/> is a space.
    static std::u8string allText(const XmlNode &node)
    {
        std::u8string out;
        for (const XmlNode &child : node.children)
            if (child.isText())
                out += child.text;
            else if (isTtml(child, u8"br"))
                out += u8' ';
            else if (!foreign(child) && !attribute(child, u8"role", Namespace::TtmlMetadata))
                out += allText(child); // another namespace's element holds no text of TTML's
        return out;
    }

    // Text appended with whitespace collapsed across the join too: TTML's
    // default whitespace handling is over the whole paragraph, not each
    // node. Preserved text is appended as it is.
    static void append(std::u8string &to, u8sv text, bool preserve)
    {
        for (char8_t c : text)
            if (preserve || c != u8' ' || !to.ends_with(u8' '))
                to += c;
    }

    // A timed span's word: its text, literal, with a <br/> inside it a line
    // break as under the paragraph.
    static std::u8string spanText(const XmlNode &node, bool preserve)
    {
        preserve = preserves(node, preserve);
        std::u8string out;
        for (const XmlNode &child : node.children)
            if (child.isText())
                append(out, lyricText(child.text, preserve), preserve);
            else if (isTtml(child, u8"br"))
                out += u8"\\N";
            else if (!foreign(child) && !attribute(child, u8"role", Namespace::TtmlMetadata))
                append(out, spanText(child, preserve), preserves(child, preserve));
        return out;
    }

    void readHead(const XmlNode &root)
    {
        std::optional<std::u8string> musicName;
        // TTML's metadata (ttm:title, ttm:agent, ttm:name), or the same
        // names with no namespace or TTML's, as such files write them;
        // another namespace's are not, nor what they hold. AMLL's meta is
        // read under whatever prefix the file gives it.
        auto metadata = [](const XmlNode &node, u8sv local) {
            return (node.ns == Namespace::TtmlMetadata || node.ns == Namespace::Ttml || node.ns == Namespace::None) &&
                   localName(node.name) == local;
        };
        auto visit = [&](auto &self, const XmlNode &node) -> void {
            const u8sv local = localName(node.name);
            // A title counts once it has words: an empty one leaves the
            // next title, or musicName, to name the file.
            if (metadata(node, u8"title") && !m_sheet.title)
                if (const auto title = collapsed(allText(node)); !detail::trim(title).empty())
                    m_sheet.title = detail::trim(title);
            if (local == u8"meta" && attribute(node, u8"key") == u8sv(u8"musicName") && !musicName)
                if (const auto title = collapsed(attribute(node, u8"value").value_or(u8"")); !detail::trim(title).empty())
                    musicName = detail::trim(title);
            if (metadata(node, u8"agent")) {
                const auto id = attribute(node, u8"id", Namespace::Xml);
                for (const XmlNode &child : node.children)
                    if (id && metadata(child, u8"name"))
                        m_agents[std::u8string(*id)] = detail::trim(collapsed(allText(child)));
            }
            if (foreign(node))
                return; // another namespace's element (xml: too): nothing inside it is TTML's
            for (const XmlNode &child : node.children)
                if (!child.isText() && !isTtml(child, u8"body"))
                    self(self, child);
        };
        for (const XmlNode &child : root.children)
            if (isTtml(child, u8"head"))
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
            return std::unexpected(
                LyricsProblem{Kind::UnreadableTime, attributeLine(node, which), std::u8string(*value)});
        return t;
    }

    // An element's own begin and end; a dur counts from its own begin, or
    // from `inheritedBegin` (its nearest timed ancestor's) when it has none,
    // and with an end too the earlier of the two ends it (TTML2's active
    // duration, the lesser of dur and end - begin).
    static std::expected<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>, LyricsProblem>
    times(const XmlNode &node, std::optional<std::int64_t> inheritedBegin)
    {
        const auto begin = time(node, u8"begin");
        if (!begin)
            return std::unexpected(begin.error());
        auto end = time(node, u8"end");
        if (!end)
            return std::unexpected(end.error());
        const auto dur = time(node, u8"dur"); // read even with no begin to count from
        if (!dur)
            return std::unexpected(dur.error());
        if (const auto from = *begin ? *begin : inheritedBegin; from) {
            if (*dur) {
                const std::int64_t byDur = *from + **dur;
                if (byDur > detail::kLatestLyricsTime && !*end)
                    return std::unexpected(LyricsProblem{Kind::UnreadableTime, attributeLine(node, u8"dur"),
                                                         std::u8string(*attribute(node, u8"dur"))});
                *end = *end ? std::min(**end, byDur) : byDur;
            }
        }
        if (*begin && *end && **end < **begin) {
            const u8sv which = attribute(node, u8"end") ? u8"end" : u8"dur";
            return std::unexpected(LyricsProblem{Kind::EndBeforeBegin, attributeLine(node, which),
                                                 std::u8string(attribute(node, which).value_or(u8""))});
        }
        return std::pair{*begin, *end};
    }

    // The end an element ends by: its own, cut at the end it inherits
    // (a child's interval lies within its parent's), or the inherited one.
    static std::optional<std::int64_t> endWithin(std::optional<std::int64_t> own, std::optional<std::int64_t> inherited)
    {
        return own && inherited ? std::min(*own, *inherited) : own ? own : inherited;
    }

    // An element's begin and end within the ones it inherits, as a child's
    // interval lies within its parent's: its own begin no earlier than the
    // inherited one, its own end no later. OutsideParent, at its begin or
    // its end, when it lies wholly outside them.
    static std::expected<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>, LyricsProblem>
    within(const XmlNode &node, std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> own,
           std::optional<std::int64_t> begin, std::optional<std::int64_t> end)
    {
        const auto from = own.first && begin ? std::max(*own.first, *begin) : own.first ? own.first : begin;
        const auto to = endWithin(own.second, end);
        if (from && to && *to < *from) {
            // Its own begin after the end it inherits, or its own end before
            // the begin it inherits.
            const u8sv which = own.first && *from == *own.first ? u8"begin" : attribute(node, u8"end") ? u8"end" : u8"dur";
            return std::unexpected(LyricsProblem{Kind::OutsideParent, attributeLine(node, which),
                                                 std::u8string(attribute(node, which).value_or(u8""))});
        }
        return std::pair{from, to};
    }

    // Whether an element has times of its own: a begin, or an end or dur
    // (counted from the begin it inherits).
    static bool timed(const XmlNode &node)
    {
        return attribute(node, u8"begin") || attribute(node, u8"end") || attribute(node, u8"dur");
    }

    static bool hasTimedSpan(const XmlNode &node)
    {
        for (const XmlNode &child : node.children)
            if (!child.isText() && !foreign(child) && !attribute(child, u8"role", Namespace::TtmlMetadata) &&
                (timed(child) || hasTimedSpan(child)))
                return true;
        return false;
    }

    // The background vocals inside a word's span, which spanText leaves out;
    // each is a Line of its own as beside the word, timed by the word's span.
    static void backgroundsIn(const XmlNode &node, Backgrounds &backgrounds, bool preserve,
                              std::optional<std::int64_t> begin, std::optional<std::int64_t> end)
    {
        for (const XmlNode &child : node.children) {
            if (child.isText() || foreign(child))
                continue;
            const auto role = attribute(child, u8"role", Namespace::TtmlMetadata);
            if (role == u8sv(u8"x-bg"))
                backgrounds.push_back(Background{&child, preserve, begin, end});
            else if (!role)
                backgroundsIn(child, backgrounds, preserves(child, preserve), begin, end);
        }
    }

    static void addText(Words &into, u8sv text, bool preserve)
    {
        append(into.plain, text, preserve);
        if (into.words.empty())
            append(into.leading, text, preserve);
        else
            append(into.words.back().text, text, preserve); // spaces after a word stay with it
    }

    // The words of a <p> (or of a background span), and the background
    // vocals inside it, each a Line of its own.
    // `preserve`: whether the node keeps its whitespace (xml:space).
    // `span`, `spanBegin`, `spanEnd`: the nearest timed span around the node
    // that holds timed spans too, and its times. Its own text is a word it
    // times, and its end ends a word inside it that has no end of its own.
    std::expected<void, LyricsProblem> collect(const XmlNode &node, Words &into, Backgrounds &backgrounds, bool preserve,
                                               const XmlNode *span, std::optional<std::int64_t> spanBegin,
                                               std::optional<std::int64_t> spanEnd)
    {
        for (const XmlNode &child : node.children) {
            if (foreign(child))
                continue; // not TTML's: no text, times or words
            if (child.isText()) {
                const std::u8string text = lyricText(child.text, preserve);
                const u8sv lyric = trimBlankLeft(text);
                // Blank text, and the span's text going on through untimed
                // elements ("hel<span>lo</span>"), stay with the word before.
                if (!spanBegin || lyric.empty() || (into.textOf == span && !into.words.empty())) {
                    addText(into, text, preserve);
                    continue;
                }
                // Text in a timed span beside its timed words: a word from the
                // span's begin, or from the end of the word before it if that
                // is later ("hello <span>world</span> again"), to the span's end.
                if (lyric.size() < text.size())
                    addText(into, u8sv(text).substr(0, text.size() - lyric.size()), false);
                std::int64_t begin = *spanBegin;
                if (!into.words.empty())
                    begin = std::max(begin, into.ends.back().value_or(into.words.back().begin));
                append(into.plain, lyric, preserve);
                into.words.push_back(LyricWord{begin, 0, std::u8string(lyric)});
                into.ends.push_back(spanEnd);
                into.lines.push_back(child.line);
                into.own.push_back(true);
                into.textOf = span;
                continue;
            }
            const auto role = attribute(child, u8"role", Namespace::TtmlMetadata);
            if (role == u8sv(u8"x-bg")) {
                backgrounds.push_back(Background{&child, preserve, spanBegin, spanEnd});
                continue;
            }
            if (role)
                continue; // translations, romanisations and other untimed companions
            if (isTtml(child, u8"br")) {
                addText(into, u8"\\N", preserve);
                continue;
            }
            const auto inheritedBegin = spanBegin ? spanBegin : into.lineBegin;
            if (isTtml(child, u8"span") && (attribute(child, u8"begin") || (timed(child) && inheritedBegin)) &&
                !hasTimedSpan(child)) {
                const auto own = times(child, inheritedBegin);
                if (!own)
                    return std::unexpected(own.error());
                const auto t = within(child, *own, inheritedBegin, spanEnd);
                if (!t)
                    return std::unexpected(t.error());
                // Without a begin of its own a word starts at the begin it
                // inherits, or where the word before it ends if that is later.
                std::int64_t begin = *t->first;
                if (!own->first && !into.words.empty())
                    begin = std::max(begin, into.ends.back().value_or(into.words.back().begin));
                const bool kept = preserves(child, preserve);
                const std::u8string text = spanText(child, preserve);
                const u8sv word = trimBlankLeft(text);
                // Spaces before a word stay with the text before it, kept
                // ones as whichever scope inside the span kept them.
                if (word.size() < text.size())
                    addText(into, u8sv(text).substr(0, text.size() - word.size()), false);
                backgroundsIn(child, backgrounds, kept, begin, t->second);
                if (word.empty())
                    continue; // a timed span with no lyric is no word; the gaps come from the words
                if (own->first && into.timedBegin && begin < *into.timedBegin)
                    return std::unexpected(LyricsProblem{Kind::TimeGoesBack, attributeLine(child, u8"begin"),
                                                         std::u8string(*attribute(child, u8"begin"))});
                append(into.plain, word, kept);
                into.words.push_back(LyricWord{begin, 0, std::u8string(word)});
                into.ends.push_back(t->second);
                into.lines.push_back(child.line);
                into.own.push_back(false);
                into.timedBegin = begin;
                into.textOf = nullptr;
                continue;
            }
            const XmlNode *innerSpan = span;
            std::optional<std::int64_t> innerBegin = spanBegin, innerEnd = spanEnd;
            if (timed(child)) {
                const auto own = times(child, inheritedBegin);
                if (!own)
                    return std::unexpected(own.error());
                const auto t = within(child, *own, inheritedBegin, spanEnd);
                if (!t)
                    return std::unexpected(t.error());
                // Without a begin of its own the span starts at the begin it
                // inherits, so its own text is a word from there.
                if (t->first) {
                    innerSpan = &child;
                    innerBegin = t->first;
                }
                innerEnd = t->second;
            }
            if (auto ok = collect(child, into, backgrounds, preserves(child, preserve), innerSpan, innerBegin, innerEnd); !ok)
                return ok;
        }
        return {};
    }

    // Own text with no time of its own joins the word beside it as
    // written: text that begins no earlier than the word after it
    // ("hello<span begin=\"2\">world" from 2 s stays "helloworld"), and
    // text after the last timed word with nothing left to time it. `end`:
    // the Line's end. One pass, however many words join.
    static void joinUntimed(Words &words, std::optional<std::int64_t> end)
    {
        std::size_t n = words.words.size();
        if (n > 1 && words.own[n - 1]) {
            const auto lastEnd = words.ends[n - 1] ? words.ends[n - 1] : end;
            if (!lastEnd || *lastEnd <= words.words[n - 1].begin) {
                words.words[n - 2].text += words.words[n - 1].text;
                --n;
            }
        }
        // Each word that joins the one kept after it, found from the back.
        std::vector<bool> joins(n);
        for (std::size_t i = n, next = n; i-- > 0;) {
            if (next < n && words.own[i] && words.words[i].begin >= words.words[next].begin)
                joins[i] = true;
            else
                next = i;
        }
        std::size_t kept = 0;
        std::u8string before;
        for (std::size_t i = 0; i < n; ++i) {
            if (joins[i]) {
                before += words.words[i].text;
                continue;
            }
            words.words[i].text.insert(0, before);
            before.clear();
            if (kept != i) {
                words.words[kept] = std::move(words.words[i]);
                words.ends[kept] = words.ends[i];
                words.lines[kept] = words.lines[i];
                words.own[kept] = words.own[i];
            }
            ++kept;
        }
        words.words.resize(kept);
        words.ends.resize(kept);
        words.lines.resize(kept);
        words.own.resize(kept);
    }

    // A <p>, or an x-bg span, as Lines. `begin` and `end` are its nearest
    // timed ancestor's: they time a plain paragraph without times of its own,
    // and its own times and its words' lie within them. `inherited`: whether
    // its parent keeps whitespace (xml:space). `background`: an x-bg span,
    // whose own times stand, as AMLL writes background vocals over their
    // main Line, starting before it or ending after it.
    std::expected<void, LyricsProblem> line(const XmlNode &node, std::optional<std::int64_t> begin,
                                            std::optional<std::int64_t> end, const std::u8string &actor, bool inherited,
                                            bool background = false)
    {
        const auto own = times(node, begin);
        if (!own)
            return std::unexpected(own.error());
        const auto t = background ? within(node, *own, own->first ? std::nullopt : begin, own->second ? std::nullopt : end)
                                  : within(node, *own, begin, end);
        if (!t)
            return std::unexpected(t.error());
        begin = t->first;
        end = t->second;
        Words words;
        words.lineBegin = begin;
        Backgrounds backgrounds;
        const bool preserve = preserves(node, inherited);
        // A paragraph with timed words times its own text as a span does:
        // "hello <span>world</span>" sings hello from the paragraph's begin.
        const bool spans = begin && hasTimedSpan(node);
        if (auto ok = collect(node, words, backgrounds, preserve, spans ? &node : nullptr, spans ? begin : std::nullopt, end);
            !ok)
            return ok;
        joinUntimed(words, end);
        LyricLine out;
        out.actor = actor;
        if (words.words.empty()) {
            const bool text = !std::ranges::all_of(words.plain, blank);
            out.text = finished(words.plain, true, true);
            if (text) {
                if (!begin || !end)
                    return std::unexpected(LyricsProblem{Kind::MissingTime, node.line, readable(words.plain)});
                out.start = *begin;
                out.end = *end;
                if (auto ok = add(std::move(out), node.line); !ok)
                    return ok;
            }
        } else {
            for (std::size_t i = 0; i < words.words.size(); ++i) {
                const bool last = i + 1 == words.words.size();
                const auto wordEnd = words.ends[i] ? words.ends[i] : last ? end : std::optional(words.words[i + 1].begin);
                if (!wordEnd || *wordEnd < words.words[i].begin)
                    return std::unexpected(LyricsProblem{Kind::MissingTime, words.lines[i], words.words[i].text});
                words.words[i].end = *wordEnd;
            }
            // The text before the first word, joined to it as written:
            // "hello<span>world" stays one word, "hello <span>" two. Kept
            // spaces at the Line's edges are hard spaces.
            words.words.front().text.insert(0, words.leading);
            for (std::size_t i = 0; i < words.words.size(); ++i)
                words.words[i].text = finished(words.words[i].text, i == 0, i + 1 == words.words.size());
            out.start = own->first ? std::min(*begin, words.words.front().begin) : words.words.front().begin;
            // Its own end, cut at the one it inherits, if the words end before it.
            out.end = own->second ? std::max(*end, words.words.back().end) : words.words.back().end;
            out.words = std::move(words.words);
            if (auto ok = add(std::move(out), node.line); !ok)
                return ok;
        }
        for (const Background &background : backgrounds)
            if (auto ok = line(*background.node, background.begin ? background.begin : begin,
                               background.end ? background.end : end, actorOf(*background.node, actor), background.preserve,
                               true);
                !ok)
                return ok;
        return {};
    }

    // The Actor of a <p> or a background span: its agent's name, or the
    // agent's id when it has no name; without an agent, `inherited` (a
    // background vocal is its paragraph's singer unless it names its own).
    std::u8string actorOf(const XmlNode &node, const std::u8string &inherited) const
    {
        const auto agent = attribute(node, u8"agent", Namespace::TtmlMetadata);
        if (!agent)
            return inherited;
        const auto name = m_agents.find(std::u8string(*agent));
        return name != m_agents.end() && !name->second.empty() ? name->second : std::u8string(*agent);
    }

    // A Line, if the budget has room for it.
    std::expected<void, LyricsProblem> add(LyricLine line, std::size_t at)
    {
        std::size_t wordBytes = 0;
        for (const LyricWord &word : line.words)
            wordBytes += word.text.size();
        if (!detail::spend(m_sheet, line.text.size() + wordBytes, line.actor.size(), line.words.size()))
            return std::unexpected(LyricsProblem{Kind::TooLarge, at, {}});
        m_sheet.lines.push_back(std::move(line));
        return {};
    }

    // The <p>s under the body and its divs. A <p> without its own begin or
    // end takes its nearest ancestor's (read as absolute, as its own are).
    std::expected<void, LyricsProblem> paragraphs(const XmlNode &node, std::optional<std::int64_t> begin,
                                                  std::optional<std::int64_t> end, bool inherited)
    {
        const bool preserve = preserves(node, inherited);
        const auto own = times(node, begin);
        if (!own)
            return std::unexpected(own.error());
        const auto t = within(node, *own, begin, end);
        if (!t)
            return std::unexpected(t.error());
        begin = t->first;
        end = t->second;
        for (const XmlNode &child : node.children) {
            if (child.isText())
                continue;
            if (isTtml(child, u8"p")) {
                if (auto ok = line(child, begin, end, actorOf(child, {}), preserve); !ok)
                    return ok;
            } else if (foreign(child)) {
                continue; // another namespace's element is not TTML's, nor what it holds
            } else if (auto ok = paragraphs(child, begin, end, preserve); !ok) {
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
    if (bytes.size() > kMaxLyricsFileSize)
        return std::unexpected(LyricsProblem{Kind::TooLarge, 0, {}});
    const u8sv text(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size());
    std::size_t line = 1;
    for (std::size_t from = 0; from <= text.size(); ++line) {
        const auto [newline, next] = detail::lineAt(text, from);
        if (!detail::validUtf8(bytes.data() + from, newline - from))
            return std::unexpected(LyricsProblem{Kind::NotUtf8, line, {}});
        // Characters XML forbids, written as they are: control characters
        // and U+FFFE or U+FFFF (EF BF BE, EF BF BF).
        for (std::size_t i = from; i < newline; ++i) {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte < 0x20 && byte != 0x9 && byte != 0xD)
                return std::unexpected(LyricsProblem{Kind::NotWellFormed, line, codePointName(byte)});
            if (byte == 0xEF && i + 2 < newline && static_cast<unsigned char>(text[i + 1]) == 0xBF &&
                (static_cast<unsigned char>(text[i + 2]) & 0xFE) == 0xBE)
                return std::unexpected(LyricsProblem{Kind::NotWellFormed, line,
                                                     codePointName(static_cast<unsigned char>(text[i + 2]) == 0xBE ? 0xFFFE : 0xFFFF)});
        }
        from = next;
    }
    const auto root = XmlReader(text).document();
    if (!root)
        return std::unexpected(root.error());
    auto sheet = TtmlReader(detail::lyricsBudget(bytes.size())).read(*root);
    if (!sheet)
        return std::unexpected(sheet.error());
    return detail::lyricsDocument(std::move(*sheet));
}

} // namespace hikari::core
