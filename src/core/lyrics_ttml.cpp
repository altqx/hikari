// Lyrics TTML (Apple Music, AMLL) agrees with itself only when <p> and <span> times are read as
// absolute, not relative to the parent as TTML says, so they are read that way.

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

enum class Namespace : std::uint8_t {
    None, // unprefixed attribute, or element without a default namespace
    Xml,
    Xmlns,
    Ttml,
    TtmlMetadata, // ttm:
    Other, // any other namespace, or an undeclared prefix
};

struct XmlNode {
    using Attribute = std::pair<std::u8string, std::u8string>;
    std::u8string name; // as written, with its prefix; empty for text
    std::vector<Attribute> attributes;
    std::vector<std::size_t> attributeLines; // the line each value starts on
    std::vector<Namespace> attributeNamespaces;
    std::vector<XmlNode> children;
    std::u8string text;
    std::size_t line = 0;
    Namespace ns = Namespace::None;

    bool isText() const { return name.empty(); }
};

u8sv localName(u8sv name)
{
    const std::size_t colon = name.rfind(u8':');
    return colon == u8sv::npos ? name : name.substr(colon + 1);
}

constexpr u8sv kXmlNamespace = u8"http://www.w3.org/XML/1998/namespace";
constexpr u8sv kXmlnsNamespace = u8"http://www.w3.org/2000/xmlns/";
constexpr u8sv kTtmlUri = u8"http://www.w3.org/ns/ttml";
constexpr u8sv kTtmlMetadataUri = u8"http://www.w3.org/ns/ttml#metadata";

// TTML timing and AMLL meta attributes are unprefixed; ttm:agent and ttm:role are in TTML's
// metadata namespace, xml:id in XML's. So "foo:begin" is not a begin.
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

// The element's line when the attribute is missing.
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

// Each prefix ("" for the default) to the URIs open elements declare for it, innermost last.
class NamespaceScope {
public:
    // kUndeclared for a prefix no one declares.
    struct Place {
        Namespace ns = Namespace::None;
        std::uint32_t uri = 0;
    };
    static constexpr std::uint32_t kUndeclared = UINT32_MAX;

    // Namespaces in XML: xml only for its own namespace, never xmlns, no empty namespace for a
    // prefix (XML 1.1 only). False, with nothing changed, on a breach.
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
    void leave(const XmlNode &node)
    {
        for (const auto &[key, value] : node.attributes)
            if (const auto prefix = declares(key))
                m_declared.find(*prefix)->second.pop_back();
    }

    // Unprefixed attributes have no namespace, unprefixed elements the default one. An undeclared
    // ttm: is TTML's metadata, as lyrics files mean it; any other undeclared prefix is kUndeclared.
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
    // "" for the default namespace.
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
    std::vector<Namespace> m_kinds;
};

// TTML's content leaves another namespace's element out with all it holds.
bool foreign(const XmlNode &node)
{
    return !node.isText() && node.ns != Namespace::Ttml && node.ns != Namespace::None;
}

// In TTML's namespace, or in none (files that leave it out).
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

// XML's Char: tab, LF, CR and U+0020 on, without surrogates, U+FFFE and U+FFFF.
bool xmlChar(std::uint32_t cp)
{
    return cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) ||
           (cp >= 0x10000 && cp <= 0x10FFFF);
}

// Valid UTF-8 only.
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

// Namespaces in XML: a local name, or prefix:local, each an XML name.
bool qualifiedName(u8sv name)
{
    const std::size_t colon = name.find(u8':');
    if (colon == u8sv::npos)
        return xmlName(name);
    const u8sv local = name.substr(colon + 1);
    return xmlName(name.substr(0, colon)) && xmlName(local) && local.find(u8':') == u8sv::npos;
}

// If XML allows the character "#65" or "#x41" names.
std::optional<std::uint32_t> characterReference(u8sv reference)
{
    if (reference.size() < 2 || reference[0] != u8'#')
        return std::nullopt;
    const bool hex = reference[1] == u8'x';
    const u8sv digits = reference.substr(hex ? 2 : 1);
    if (digits.empty())
        return std::nullopt;
    // Leading zeros allowed; saturates past U+10FFFF.
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

// Each "&" starts "&name;" or a character reference; which entity is checked where it is used.
bool referencesWritten(u8sv text)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != u8'&')
            continue;
        const std::size_t semi = text.find(u8';', i);
        if (semi == u8sv::npos)
            return false;
        const u8sv reference = text.substr(i + 1, semi - i - 1);
        if (!(reference.starts_with(u8'#') ? characterReference(reference).has_value() : xmlName(reference)))
            return false;
        i = semi;
    }
    return true;
}

// Non-CDATA attribute types: spaces trimmed, runs made one (XML 1.0 3.3.3).
std::u8string tokens(u8sv value)
{
    std::u8string out;
    for (std::size_t i = 0; i < value.size(); ++i)
        if (value[i] != u8' ' || (!out.empty() && i + 1 < value.size() && value[i + 1] != u8' '))
            out += value[i];
    return out;
}

// XML's PubidChar: what a public identifier may hold.
bool pubidChar(char8_t c)
{
    return c == u8' ' || c == u8'\r' || c == u8'\n' || (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') ||
           (c >= u8'0' && c <= u8'9') || u8sv(u8"-'()+,./:=?;!*#@$_%").find(c) != u8sv::npos;
}

// "U+0001": a forbidden character, named for a message.
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

// Character references replaced, entity references kept for use (XML 1.0 4.5). Line ends are LF;
// a "&#13;" stays a CR.
std::u8string replacementText(u8sv value)
{
    std::u8string out;
    for (std::size_t i = 0; i < value.size();) {
        if (value.substr(i).starts_with(u8"&#")) {
            const std::size_t semi = value.find(u8';', i);
            appendUtf8(out, *characterReference(value.substr(i + 1, semi - i - 1)));
            i = semi + 1;
        } else if (value[i] == u8'\r') {
            out += u8'\n';
            i += value.substr(i).starts_with(u8"\r\n") ? 2 : 1;
        } else {
            out += value[i++];
        }
    }
    return out;
}

class XmlReader {
public:
    explicit XmlReader(u8sv text)
        : m_s(text), m_nodesLeft(text.size() / kTtmlBytesPerNode + 4096), m_expansionLeft(detail::lyricsBudget(text.size()))
    {
    }

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
        // LF, CRLF or a lone CR ends a line. An entity's text is all on its reference's line.
        for (std::size_t end = std::min(m_s.size(), m_pos + n); m_pos < end; ++m_pos)
            if (m_inputs.empty() &&
                (m_s[m_pos] == u8'\n' || (m_s[m_pos] == u8'\r' && (m_pos + 1 == m_s.size() || m_s[m_pos + 1] != u8'\n'))))
                ++m_line;
    }

    void skipSpace()
    {
        while (!done() && isSpace(m_s[m_pos]))
            advance(1);
    }

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

    // It may not hold "--" or end with "-".
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

    // A target name, then a space or "?>". The target "xml" (any case) belongs to the XML
    // declaration, lower case at the very start.
    bool skipPi()
    {
        const bool first = m_pos == m_start && m_inputs.empty();
        advance(2);
        const auto target = name();
        if (!target || target->find(u8':') != std::u8string::npos) // no colon in a target (Namespaces in XML)
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

    // After "<?xml": version="1.n", optional encoding and standalone in that order, then "?>".
    // A 1.n document is read as 1.0 (XML 1.0 2.8), so 1.1-only references stay broken.
    bool xmlDeclaration()
    {
        // 0 absent, 1 well formed (`value` set), -1 broken.
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
        auto lower = [](char8_t c) { return c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c; };
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
        // The file is read as UTF-8, so it may name no other encoding.
        if (found > 0 && !std::ranges::equal(value, u8sv(u8"utf-8"), {}, [&](char8_t c) { return lower(c); })) {
            m_otherEncoding = true;
            return false;
        }
        found = field(u8"standalone", value);
        if (found < 0 || (found > 0 && value != u8"yes" && value != u8"no"))
            return false;
        m_standalone = found > 0 && value == u8"yes";
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

    // Whitespace, comments and PIs between top-level parts; in the prolog, one DOCTYPE.
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
                // doctypedecl: an external id, an internal subset in "[ ]", then ">". Literals and comments
                // in the subset may hold "]" and ">".
                if (ok && space()) {
                    if (at(u8"SYSTEM") || at(u8"PUBLIC")) {
                        ok = externalId(false);
                        m_complete = false;
                    }
                    skipSpace();
                }
                // intSubset: an internal parameter entity's text is read in place of its reference, whole
                // declarations only ("PE Between Declarations").
                if (ok && at(u8"[")) {
                    advance(1);
                    for (skipSpace(); ok; skipSpace()) {
                        if (done() && !m_inputs.empty()) {
                            leaveEntity();
                            continue;
                        }
                        if (at(u8"]") && m_inputs.empty())
                            break;
                        if (done() || at(u8"]")) {
                            ok = false;
                        } else if (at(u8"<!--")) {
                            ok = skipComment();
                        } else if (at(u8"<?")) {
                            ok = skipPi();
                        } else if (at(u8"%")) { // "%name;"
                            const std::size_t reference = m_pos;
                            advance(1);
                            const std::size_t from = m_pos;
                            ok = name().has_value() && at(u8";");
                            if (!ok)
                                continue;
                            advance(1);
                            const u8sv entity = m_s.substr(from, m_pos - 1 - from);
                            const auto found = m_parameters.find(entity);
                            m_complete = false;
                            if (m_standalone && found == m_parameters.end()) {
                                ok = false; // a standalone file declares each entity it names
                            } else if (!m_declarations || found == m_parameters.end() || !found->second) {
                                // Never read, so its declarations are unknown: unless standalone, later ones are only checked
                                // (XML 1.0 5.1).
                                m_declarations = m_declarations && m_standalone;
                            } else if (std::ranges::contains(m_inputs, entity, &Input::entity)) {
                                ok = false; // an entity naming itself
                            } else if (m_inputs.size() >= kMaxTtmlDepth || !spend(found->second->size())) {
                                m_overBudget = true;
                                ok = false;
                            } else {
                                m_inputs.push_back({m_s, reference, m_pos, m_line, 0, entity});
                                m_s = *found->second;
                                m_pos = 0;
                            }
                        } else {
                            ok = markupDeclaration();
                        }
                    }
                    ok = ok && !recursive(m_entities, u8'&') && !recursive(m_parameters, u8'%');
                    // "Entity Declared": with nothing external, or standalone, each named entity must be declared.
                    if (m_complete || m_standalone)
                        ok = ok && !undeclared(m_entities, u8'&') && (!m_standalone || !undeclared(m_parameters, u8'%'));
                    if (ok)
                        advance(1);
                    skipSpace();
                }
                ok = ok && at(u8">");
                if (ok)
                    advance(1);
            } else
                return {};
            if (!ok)
                leaveEntities();
            if (!ok && m_otherEncoding)
                return std::unexpected(LyricsProblem{Kind::NotUtf8, start.second, {}});
            if (!ok && m_overBudget)
                return std::unexpected(tooLarge()); // entities past the budget
            if (!ok) {
                std::tie(m_pos, m_line) = start;
                return std::unexpected(broken());
            }
        }
    }

    // Each by its XML 1.0 production, so a malformed one is not well-formed XML.
    bool markupDeclaration()
    {
        bool ok = false;
        if (keyword(u8"<!ELEMENT"))
            ok = space() && qname() && space() && contentSpec();
        else if (keyword(u8"<!ATTLIST"))
            ok = attlistDeclaration();
        else if (keyword(u8"<!ENTITY"))
            ok = entityDeclaration();
        else if (keyword(u8"<!NOTATION"))
            ok = space() && ncname() && space() && externalId(true);
        skipSpace();
        return ok && keyword(u8">");
    }
    // Element and attribute names are QNames; entity and notation names have no colon.
    bool qname()
    {
        const auto n = name();
        return n && qualifiedName(*n);
    }
    bool ncname()
    {
        const auto n = name();
        return n && n->find(u8':') == std::u8string::npos;
    }

    bool keyword(u8sv word)
    {
        if (!at(word))
            return false;
        advance(word.size());
        return true;
    }
    bool space()
    {
        const bool any = !done() && isSpace(m_s[m_pos]);
        skipSpace();
        return any;
    }
    // SystemLiteral: anything; PubidLiteral: PubidChars; AttValue: no "<", "&" only in
    // references; EntityValue: as AttValue, and no "%" (internal subset).
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
                             : literal == Literal::AttValue ? text.find(u8'<') == u8sv::npos && referencesWritten(text)
                             // No "%": no parameter-entity reference inside an internal-subset declaration.
                             : literal == Literal::EntityValue ? text.find(u8'%') == u8sv::npos && referencesWritten(text)
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
    // With `notation`, PUBLIC "id" alone is allowed too.
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
    // EMPTY, ANY, Mixed ("(#PCDATA|a|b)*") or children.
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
            if (!qname())
                return false;
        }
        if (!keyword(u8")"))
            return false;
        return keyword(u8"*") || !names;
    }
    // After "(": particles split by "|" or ",", not both, then ")" and "?", "*" or "+";
    // nested no deeper than elements may be.
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
            } else if (!qname()) {
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
    // The first declaration of an attribute binds: whether it is tokenized and its normalized default.
    bool attlistDeclaration()
    {
        if (!space())
            return false;
        const auto element = name();
        if (!element || !qualifiedName(*element))
            return false;
        for (;;) {
            const bool spaced = space();
            if (at(u8">"))
                return true;
            const auto attribute = spaced ? name() : std::nullopt;
            if (!attribute || !qualifiedName(*attribute) || !space())
                return false;
            const bool tokenized = !at(u8"CDATA");
            if (!attributeType() || !space())
                return false;
            std::optional<std::u8string> value;
            if (!keyword(u8"#REQUIRED") && !keyword(u8"#IMPLIED")) {
                if (keyword(u8"#FIXED") && !space())
                    return false;
                const std::size_t literal = m_pos;
                if (!quoted(Literal::AttValue))
                    return false;
                if (m_declarations) {
                    const auto decoded = decode(m_s.substr(literal + 1, m_pos - literal - 2), true);
                    if (!decoded)
                        return false;
                    value = tokenized ? tokens(*decoded) : *decoded;
                }
            }
            if (m_declarations)
                m_attributes[*element].try_emplace(*attribute, AttributeDeclaration{tokenized, std::move(value)});
        }
    }
    bool attributeType()
    {
        // Longer names first where they share a start.
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
            if (notation ? !ncname() : !nmtoken())
                return false;
            skipSpace();
            if (keyword(u8")"))
                return true;
            if (!keyword(u8"|"))
                return false;
        }
    }
    // General or "%" parameter entity; the first declaration binds. External ones keep no text.
    bool entityDeclaration()
    {
        if (!space())
            return false;
        const bool parameter = keyword(u8"%");
        if (parameter && !space())
            return false;
        auto entity = name();
        if (!entity || entity->find(u8':') != std::u8string::npos || !space())
            return false;
        // The predefined five may be declared only as XML 1.0 4.6 writes them.
        const std::optional<char32_t> predefined = parameter          ? std::nullopt
                                                   : *entity == u8"lt"   ? std::optional(U'<')
                                                   : *entity == u8"amp"  ? std::optional(U'&')
                                                   : *entity == u8"gt"   ? std::optional(U'>')
                                                   : *entity == u8"apos" ? std::optional(U'\'')
                                                   : *entity == u8"quot" ? std::optional(U'"')
                                                                         : std::nullopt;
        auto declare = [&](std::optional<std::u8string> text) {
            if (predefined) {
                // lt and amp as a character reference, the others as the character or one.
                const u8sv r = text ? u8sv(*text) : u8sv();
                const bool reference = r.starts_with(u8"&#") && r.ends_with(u8';') &&
                                       characterReference(r.substr(1, r.size() - 2)) == std::optional<std::uint32_t>(*predefined);
                return reference || (*predefined != U'<' && *predefined != U'&' && r.size() == 1 && r[0] == *predefined);
            }
            if (!m_declarations)
                return true;
            if (parameter)
                m_parameters.try_emplace(std::move(*entity), std::move(text));
            else
                m_entities.try_emplace(std::move(*entity), std::move(text));
            return true;
        };
        const std::size_t literal = m_pos;
        if (quoted(Literal::EntityValue))
            return declare(replacementText(m_s.substr(literal + 1, m_pos - literal - 2)));
        if (!externalId(false) || !declare(std::nullopt))
            return false;
        const auto mark = std::pair{m_pos, m_line};
        if (!parameter && space() && keyword(u8"NDATA"))
            return space() && ncname();
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

    // Or where the unreadable entity starts. An `attribute` value's whitespace becomes spaces and
    // internal entities are replaced too, holding no "<" (XML 1.0 3.3.3). Past the expansion
    // budget sets m_overBudget.
    std::expected<std::u8string, std::size_t> decode(u8sv raw, bool attribute = false, std::size_t depth = 0)
    {
        std::u8string out;
        for (std::size_t i = 0; i < raw.size();) {
            if (attribute && (raw[i] == u8'\t' || raw[i] == u8'\n' || raw[i] == u8'\r')) {
                if (depth == 0 && raw.substr(i).starts_with(u8"\r\n"))
                    ++i;
                out += u8' ';
                ++i;
                continue;
            }
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
            else if (const auto *text = attribute ? declared(entity) : nullptr;
                     text && text->find(u8'<') == u8sv::npos && depth < kMaxTtmlDepth && spend(text->size())) {
                const auto inner = decode(*text, true, depth + 1);
                if (!inner)
                    return std::unexpected(i);
                out += *inner;
            } else
                return std::unexpected(i);
            i = semi + 1;
        }
        return out;
    }

    using Entities = std::map<std::u8string, std::optional<std::u8string>, std::less<>>;

    // XML's "No Recursion", used or not: `sign` is "&" or "%". Iterative, each text read once.
    static bool recursive(const Entities &entities, char8_t sign)
    {
        enum : std::uint8_t { Unseen, OnPath, Done };
        std::map<u8sv, std::uint8_t, std::less<>> state;
        for (const auto &[root, rootText] : entities) {
            if (state[root] != Unseen)
                continue;
            state[root] = OnPath;
            std::vector<std::pair<u8sv, std::size_t>> path{{root, 0}}; // each entity and where in its text
            while (!path.empty()) {
                const auto [entity, at] = path.back();
                const auto &text = entities.find(entity)->second;
                const u8sv t = text ? u8sv(*text) : u8sv();
                const std::size_t i = t.find(sign, at);
                const std::size_t semi = i == u8sv::npos ? u8sv::npos : t.find(u8';', i);
                if (semi == u8sv::npos) {
                    state[entity] = Done;
                    path.pop_back();
                    continue;
                }
                const std::size_t again = t.find(sign, i + 1); // a lone sign names nothing
                path.back().second = again < semi ? again : semi + 1;
                if (again < semi)
                    continue;
                const auto next = entities.find(t.substr(i + 1, semi - i - 1));
                if (next == entities.end())
                    continue;
                std::uint8_t &seen = state[next->first];
                if (seen == OnPath)
                    return true;
                if (seen == Unseen) {
                    seen = OnPath;
                    path.emplace_back(next->first, 0);
                }
            }
        }
        return false;
    }

    // The five predefined entities and character references aside.
    static bool undeclared(const Entities &entities, char8_t sign)
    {
        for (const auto &[name, text] : entities) {
            const u8sv t = text ? u8sv(*text) : u8sv();
            for (std::size_t i = t.find(sign); i != u8sv::npos; i = t.find(sign, i + 1)) {
                const std::size_t semi = t.find(u8';', i);
                if (semi == u8sv::npos)
                    break;
                const u8sv entity = t.substr(i + 1, semi - i - 1);
                if (entity.find(sign) != u8sv::npos || entity.starts_with(u8'#'))
                    continue; // a lone sign, or a character reference
                const bool predefined = sign == u8'&' && (entity == u8"lt" || entity == u8"gt" || entity == u8"amp" ||
                                                          entity == u8"quot" || entity == u8"apos");
                if (!predefined && !entities.contains(entity))
                    return true;
                i = semi;
            }
        }
        return false;
    }

    void leaveEntity()
    {
        const Input outer = m_inputs.back();
        m_inputs.pop_back();
        m_s = outer.text;
        m_pos = outer.resume;
        m_line = outer.line;
    }
    // Back at the reference of the outermost entity being read.
    void leaveEntities()
    {
        if (m_inputs.empty())
            return;
        const Input outer = m_inputs.front();
        m_inputs.clear();
        m_s = outer.text;
        m_pos = outer.reference;
        m_line = outer.line;
    }

    const std::u8string *declared(u8sv entity) const
    {
        const auto found = m_entities.find(entity);
        return found == m_entities.end() || !found->second ? nullptr : &*found->second;
    }

    // The expansion budget keeps entities naming entities from growing without end.
    bool spend(std::size_t bytes)
    {
        bytes = std::max<std::size_t>(bytes, 1);
        if (bytes > m_expansionLeft) {
            m_overBudget = true;
            return false;
        }
        m_expansionLeft -= bytes;
        return true;
    }

    // The first reference to a non-predefined entity, npos for none before one decode() cannot read.
    static std::size_t entityReference(u8sv raw)
    {
        for (std::size_t i = raw.find(u8'&'); i != u8sv::npos; i = raw.find(u8'&', i + 1)) {
            const std::size_t semi = raw.find(u8';', i);
            if (semi == u8sv::npos)
                return u8sv::npos;
            const u8sv entity = raw.substr(i + 1, semi - i - 1);
            if (!entity.starts_with(u8'#') && entity != u8"lt" && entity != u8"gt" && entity != u8"amp" &&
                entity != u8"quot" && entity != u8"apos")
                return i;
        }
        return u8sv::npos;
    }

    // A problem in an entity's text is reported at its reference in the file.
    std::expected<XmlNode, LyricsProblem> element()
    {
        auto root = elementTree();
        if (!root && !m_inputs.empty()) {
            leaveEntities();
            if (root.error().kind != Kind::TooLarge)
                root.error().excerpt = broken().excerpt;
        }
        return root;
    }

    // Iterative; nesting past kMaxTtmlDepth is refused so tree walks and destruction stay shallow.
    // An entity's text closes what it opens, and no more (XML 1.0 4.4.3).
    std::expected<XmlNode, LyricsProblem> elementTree()
    {
        std::vector<XmlNode> open;
        for (;;) {
            if (done() && !m_inputs.empty() && open.size() == m_inputs.back().depth) {
                leaveEntity();
                continue;
            }
            if (done())
                return std::unexpected(broken());
            const std::size_t line = m_line;
            if (at(u8"</")) {
                const auto start = std::pair{m_pos, m_line};
                advance(2);
                const auto closing = name();
                skipSpace();
                if (!closing || open.empty() || *closing != open.back().name || !at(u8">") ||
                    (!m_inputs.empty() && open.size() <= m_inputs.back().depth)) {
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
                    const auto value = decode(raw, true);
                    if (!value && m_overBudget)
                        return std::unexpected(tooLarge());
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
                // Declared tokenized values normalized, defaults added; the budget pays per declaration.
                if (const auto declared = m_attributes.find(node.name); declared != m_attributes.end()) {
                    if (!spend(declared->second.size()))
                        return std::unexpected(tooLarge());
                    for (auto &[key, value] : node.attributes)
                        if (const auto it = declared->second.find(key); it != declared->second.end() && it->second.tokenized)
                            value = tokens(value);
                    std::vector<const std::pair<const std::u8string, AttributeDeclaration> *> defaults;
                    for (const auto &attribute : declared->second)
                        if (attribute.second.value && !std::ranges::binary_search(keys, u8sv(attribute.first)))
                            defaults.push_back(&attribute);
                    for (const auto *attribute : defaults) {
                        // All it takes: its name, its value and its entries.
                        if (!spend(attribute->first.size() + attribute->second.value->size() + sizeof(XmlNode::Attribute) +
                                   sizeof(std::size_t) + sizeof(Namespace)))
                            return std::unexpected(tooLarge());
                        node.attributes.emplace_back(attribute->first, *attribute->second.value);
                        node.attributeLines.push_back(line);
                    }
                }
                // No element in the xmlns namespace.
                if (node.name.starts_with(u8"xmlns:") || !m_scope.enter(node)) {
                    std::tie(m_pos, m_line) = start;
                    return std::unexpected(broken());
                }
                // Every prefix declared (ttm: aside), and no attribute twice by expanded name.
                std::vector<std::tuple<Namespace, std::uint32_t, u8sv>> expanded;
                for (std::size_t i = 0; i < node.attributes.size(); ++i) {
                    const auto place = m_scope.of(node.attributes[i].first, false);
                    if (place.uri == NamespaceScope::kUndeclared) {
                        std::tie(m_pos, m_line) = start;
                        return std::unexpected(broken()); // a prefix no one declares
                    }
                    node.attributeNamespaces.push_back(place.ns);
                    expanded.emplace_back(place.ns, place.ns == Namespace::Other ? place.uri : 0,
                                          localName(node.attributes[i].first));
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
                u8sv raw = m_s.substr(m_pos, end - m_pos);
                // An internal entity's text is read next, in its place.
                if (const std::size_t reference = entityReference(raw); reference != u8sv::npos) {
                    const std::size_t semi = raw.find(u8';', reference);
                    const u8sv entity = raw.substr(reference + 1, semi - reference - 1);
                    const std::u8string *text = declared(entity);
                    if (!text || open.empty() || std::ranges::contains(m_inputs, entity, &Input::entity)) {
                        advance(lineStartIn(raw, reference)); // from the start of the entity's line
                        return std::unexpected(broken());
                    }
                    if (reference == 0) {
                        if (m_inputs.size() >= kMaxTtmlDepth || !spend(text->size()))
                            return std::unexpected(tooLarge());
                        m_inputs.push_back({m_s, m_pos, m_pos + semi + 1, m_line, open.size(), entity});
                        m_s = *text;
                        m_pos = 0;
                        continue;
                    }
                    raw = raw.substr(0, reference);
                }
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
                advance(raw.size());
            }
        }
    }

    // Takes kTtmlBytesPerNode from the budget; false when it is gone.
    bool takeNode()
    {
        if (m_nodesLeft == 0)
            return false;
        --m_nodesLeft;
        return true;
    }
    LyricsProblem tooLarge() const { return LyricsProblem{Kind::TooLarge, m_line, {}}; }

    struct Input {
        u8sv text;
        std::size_t reference = 0, resume = 0, line = 0;
        std::size_t depth = 0; // the elements open at the reference
        u8sv entity;
    };
    struct AttributeDeclaration {
        bool tokenized = false;
        std::optional<std::u8string> value; // the default, normalized
    };

    u8sv m_s; // the file, or the text of the entity being read
    std::size_t m_pos = 0;
    std::size_t m_line = 1;
    std::size_t m_nodesLeft = 0;
    std::size_t m_expansionLeft = 0;
    bool m_overBudget = false;
    bool m_otherEncoding = false; // the XML declaration names an encoding but UTF-8
    bool m_standalone = false;
    bool m_declarations = true; // false past a parameter-entity reference (unless standalone)
    bool m_complete = true;     // no external subset nor parameter-entity reference: the subset declares all
    // None for an external entity.
    Entities m_entities;
    Entities m_parameters;
    // First declaration of each wins.
    std::map<std::u8string, std::map<std::u8string, AttributeDeclaration, std::less<>>, std::less<>> m_attributes;
    std::vector<Input> m_inputs; // the entities being read, innermost last
    std::size_t m_start = 0; // after the byte order mark
    bool m_doctype = false;
    std::optional<std::u8string> m_doctypeName; // the root the DOCTYPE names
    NamespaceScope m_scope;
};

// Clock time ("hh:mm:ss.fff", or "mm:ss.fff" and "ss.fff" as lyrics files write) or an
// offset in h, m, s or ms. Frame and tick times need rates lyrics files lack.
std::optional<std::int64_t> ttmlTime(u8sv text)
{
    text = detail::trim(text);
    auto decimal = [](u8sv s) -> std::optional<double> {
        if (s.empty())
            return std::nullopt;
        // A fraction needs digits on both sides ("1.5", not "1." or ".5"); saturates past 10^15.
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
                    whole = std::min(whole * 10 + (c - u8'0'), 1e15);
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

// TTML's default xml:space handling.
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

// Its xml:space, else `inherited`.
bool preserves(const XmlNode &node, bool inherited)
{
    for (const auto &[name, value] : node.attributes)
        if (name == u8"xml:space")
            return value == u8"preserve" ? true : value == u8"default" ? false : inherited;
    return inherited;
}

// MUST KILL: U+0001 stands for a preserved space until the Line is assembled
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

// At the Line's ends whitespace goes and kept spaces become \h, since ASS trims a Line's text.
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

// Kept spaces as spaces, ends trimmed.
std::u8string readable(u8sv text)
{
    std::u8string out(text);
    std::ranges::replace(out, kKeptSpace, u8' ');
    return std::u8string(detail::trim(out));
}

// Collapsed (TTML's default), or under xml:space="preserve" as written, a line end a break.
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
        if (auto head = readHead(root); !head)
            return std::unexpected(head.error());
        for (const XmlNode &child : root.children)
            if (isTtml(child, u8"body"))
                if (auto ok = paragraphs(child, 0, std::nullopt, preserves(root, false)); !ok) // the body starts at the document's origin
                    return std::unexpected(ok.error());
        if (m_sheet.lines.empty())
            return std::unexpected(LyricsProblem{Kind::NoLyrics, 0, {}});
        return std::move(m_sheet);
    }

private:
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
    // Its parent's xml:space, and the nearest timed span's times, which time it instead of its
    // paragraph's.
    struct Background {
        const XmlNode *node;
        bool preserve;
        std::optional<std::int64_t> begin, end;
    };
    using Backgrounds = std::vector<Background>;

    // A <br/> is a space.
    std::u8string allText(const XmlNode &node)
    {
        std::u8string out;
        for (const XmlNode &child : node.children)
            if (child.isText()) {
                if (charge(child.text.size(), child.line))
                    out += child.text;
            } else if (isTtml(child, u8"br")) {
                out += u8' ';
            } else if (!foreign(child) && !attribute(child, u8"role", Namespace::TtmlMetadata)) {
                const std::u8string inner = allText(child); // another namespace's element holds no text of TTML's
                if (charge(inner.size(), child.line))
                    out += inner;
            }
        return out;
    }

    // Charged before the text is made; when the budget runs out the read ends as TooLarge at
    // m_overAt.
    bool charge(std::size_t bytes, std::size_t line)
    {
        m_sheet.spent += bytes;
        if (!m_overAt && m_sheet.spent > m_sheet.budget)
            m_overAt = line;
        return !m_overAt;
    }
    LyricsProblem overBudget() const { return LyricsProblem{Kind::TooLarge, m_overAt.value_or(0), {}}; }

    // Charged up front at 4 bytes per byte (a "\\" gains a word joiner), times `copies`.
    std::u8string lyric(const XmlNode &text, bool preserve, std::size_t copies)
    {
        if (!charge((1 + copies) * (4 * text.text.size() + 2), text.line))
            return {};
        return lyricText(text.text, preserve);
    }

    // TTML collapses whitespace across the whole paragraph, not each node.
    static void append(std::u8string &to, u8sv text, bool preserve)
    {
        for (char8_t c : text)
            if (preserve || c != u8' ' || !to.ends_with(u8' '))
                to += c;
    }

    // A <br/> inside is a line break.
    std::u8string spanText(const XmlNode &node, bool preserve)
    {
        preserve = preserves(node, preserve);
        std::u8string out;
        for (const XmlNode &child : node.children)
            if (child.isText()) {
                append(out, lyric(child, preserve, 1), preserve);
            } else if (isTtml(child, u8"br")) {
                out += u8"\\N";
            } else if (!foreign(child) && !attribute(child, u8"role", Namespace::TtmlMetadata)) {
                const std::u8string inner = spanText(child, preserve);
                if (charge(inner.size(), child.line))
                    append(out, inner, preserves(child, preserve));
            }
        return out;
    }

    // Charged to the budget, so names nested in names cannot copy a large text over and over.
    std::expected<void, LyricsProblem> readHead(const XmlNode &root)
    {
        std::optional<std::u8string> musicName;
        auto textOf = [&](const XmlNode &node) {
            std::u8string text = m_overAt ? std::u8string() : allText(node);
            return charge(text.size() + 64, node.line) ? text : std::u8string();
        };
        // ttm:title, ttm:agent and ttm:name, or the same names unprefixed or in TTML's namespace, as such
        // files write them. AMLL's meta is read under any prefix.
        auto metadata = [](const XmlNode &node, u8sv local) {
            return (node.ns == Namespace::TtmlMetadata || node.ns == Namespace::Ttml || node.ns == Namespace::None) &&
                   localName(node.name) == local;
        };
        auto visit = [&](auto &self, const XmlNode &node) -> void {
            const u8sv local = localName(node.name);
            // A title counts once it has words: an empty one leaves the
            // next title, or musicName, to name the file.
            if (metadata(node, u8"title") && !m_sheet.title)
                if (const auto title = collapsed(textOf(node)); !detail::trim(title).empty())
                    m_sheet.title = detail::trim(title);
            if (local == u8"meta" && attribute(node, u8"key") == u8sv(u8"musicName") && !musicName && !m_overAt) {
                const u8sv value = attribute(node, u8"value").value_or(u8"");
                if (charge(value.size() + 64, node.line)) // an entity may have made it long
                    if (const auto title = collapsed(value); !detail::trim(title).empty())
                        musicName = detail::trim(title);
            }
            if (metadata(node, u8"agent")) {
                const auto id = attribute(node, u8"id", Namespace::Xml);
                for (const XmlNode &child : node.children)
                    if (id && metadata(child, u8"name"))
                        m_agents[std::u8string(*id)] = detail::trim(collapsed(textOf(child)));
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
        if (m_overAt)
            return std::unexpected(overBudget());
        if (!m_sheet.title && musicName)
            m_sheet.title = musicName;
        return {};
    }

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

    // A dur counts from its own begin, else `inheritedBegin`; with an end too the earlier ends it
    // (TTML2 active duration).
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

    // Its own end cut at the inherited one, else the inherited one.
    static std::optional<std::int64_t> endWithin(std::optional<std::int64_t> own, std::optional<std::int64_t> inherited)
    {
        return own && inherited ? std::min(*own, *inherited) : own ? own : inherited;
    }

    // Clamped into the inherited interval; OutsideParent when wholly outside it.
    static std::expected<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>, LyricsProblem>
    within(const XmlNode &node, std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> own,
           std::optional<std::int64_t> begin, std::optional<std::int64_t> end)
    {
        const auto from = own.first && begin ? std::max(*own.first, *begin) : own.first ? own.first : begin;
        const auto to = endWithin(own.second, end);
        if (from && to && *to < *from) {
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

    // `preserve`: xml:space. `span`, `spanBegin`, `spanEnd`: the nearest timed span holding timed
    // spans, whose own text is a word it times and whose end ends an endless word inside it.
    std::expected<void, LyricsProblem> collect(const XmlNode &node, Words &into, Backgrounds &backgrounds, bool preserve,
                                               const XmlNode *span, std::optional<std::int64_t> spanBegin,
                                               std::optional<std::int64_t> spanEnd)
    {
        for (const XmlNode &child : node.children) {
            if (foreign(child))
                continue; // not TTML's: no text, times or words
            if (child.isText()) {
                const std::u8string text = this->lyric(child, preserve, 2); // into the plain text and a word
                if (m_overAt)
                    return std::unexpected(overBudget());
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
                if (!charge(2 * text.size(), child.line)) // into the plain text and a word
                    return std::unexpected(overBudget());
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

    // Untimed own text joins the neighbouring word as written: text no earlier than the next word
    // ("hello<span begin=\"2\">world" stays "helloworld"), or after the last timed word. `end`: the
    // Line's end.
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

    // `begin`, `end`: the nearest timed ancestor's, which bound its times. `inherited`: the
    // parent's xml:space. `background`: an x-bg span keeps its own times, as AMLL writes them
    // past the main Line.
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
    std::optional<std::size_t> m_overAt; // where the Lines' budget ran out (charge)
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
