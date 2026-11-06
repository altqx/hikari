// Ported from YTSubConverter 1.6.5, Copyright (c) 2018 arcusmaximus, MIT licence (see THIRDPARTY).
#include "srv3_xml.h"

#include <string>

namespace hikari::core::srv3::xml {

namespace {

bool isXmlSpace(char16_t c)
{
    return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n';
}

bool isNameStart(char16_t c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_' || c == u':' ||
           (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c != 0xFFFE && c != 0xFFFF);
}

bool isNameChar(char16_t c)
{
    return isNameStart(c) || (c >= u'0' && c <= u'9') || c == u'-' || c == u'.' || c == 0xB7;
}

void appendCodePoint(u16 &out, char32_t c)
{
    if (c < 0x10000) {
        out += static_cast<char16_t>(c);
        return;
    }
    c -= 0x10000;
    out += static_cast<char16_t>(0xD800 + (c >> 10));
    out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
}

struct Scope {
    u16 defaultNamespace;
    std::vector<std::pair<u16, u16>> prefixes;
};

class Parser {
public:
    explicit Parser(u16v text) : m_text(text) {}

    Node document()
    {
        if (startsWith(u"<?xml") && m_text.size() > 5 && isXmlSpace(m_text[5]))
            skipPast(u"?>", "Unexpected end of file while parsing XML declaration has occurred.");
        std::optional<Node> root;
        while (m_pos < m_text.size()) {
            if (isXmlSpace(m_text[m_pos])) {
                ++m_pos;
            } else if (startsWith(u"<!--")) {
                comment();
            } else if (startsWith(u"<?")) {
                processingInstruction();
            } else if (startsWith(u"<!DOCTYPE")) {
                throw ConversionError("Document type declarations aren't supported.");
            } else if (m_text[m_pos] == u'<' && m_pos + 1 < m_text.size() && isNameStart(m_text[m_pos + 1])) {
                if (root)
                    fail("There are multiple root elements.");
                std::vector<Scope> scopes{Scope{}};
                root = element(scopes);
            } else {
                fail("Data at the root level is invalid.");
            }
        }
        if (!root)
            fail("Root element is missing.");
        return std::move(*root);
    }

private:
    [[noreturn]] void fail(const std::string &what) const
    {
        std::size_t line = 1, column = 1;
        for (std::size_t i = 0; i < m_pos && i < m_text.size(); ++i) {
            if (m_text[i] == u'\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw ConversionError(what + " Line " + std::to_string(line) + ", position " + std::to_string(column) + ".");
    }

    bool startsWith(u16v s) const { return m_text.substr(m_pos).starts_with(s); }

    void skipPast(u16v end, const char *unclosed)
    {
        const std::size_t at = m_text.find(end, m_pos);
        if (at == u16v::npos)
            fail(unclosed);
        m_pos = at + end.size();
    }

    void comment()
    {
        m_pos += 4;
        const std::size_t at = m_text.find(u"--", m_pos);
        if (at == u16v::npos)
            fail("Unexpected end of file while parsing Comment has occurred.");
        m_pos = at;
        if (at + 2 >= m_text.size() || m_text[at + 2] != u'>')
            fail("An XML comment cannot contain '--', and '-' cannot be the last character.");
        m_pos = at + 3;
    }

    void processingInstruction()
    {
        m_pos += 2;
        const u16 target = name();
        if (toLowerInvariant(target) == u"xml")
            fail("Unexpected XML declaration. The XML declaration must be the first node in the document, and no whitespace "
                 "characters are allowed to appear before it.");
        skipPast(u"?>", "Unexpected end of file while parsing PI has occurred.");
    }

    u16 name()
    {
        if (m_pos >= m_text.size() || !isNameStart(m_text[m_pos]))
            fail("Name cannot begin with this character.");
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && isNameChar(m_text[m_pos]))
            ++m_pos;
        return u16(m_text.substr(start, m_pos - start));
    }

    void skipSpace()
    {
        while (m_pos < m_text.size() && isXmlSpace(m_text[m_pos]))
            ++m_pos;
    }

    void reference(u16 &out)
    {
        ++m_pos;
        const std::size_t end = m_text.find(u';', m_pos);
        if (end == u16v::npos)
            fail("An error occurred while parsing EntityName.");
        const u16v ref = m_text.substr(m_pos, end - m_pos);
        if (ref.starts_with(u'#')) {
            const bool hex = ref.size() > 1 && ref[1] == u'x';
            const u16v digits = ref.substr(hex ? 2 : 1);
            char32_t c = 0;
            if (digits.empty())
                fail("Invalid syntax for a decimal numeric entity reference.");
            for (char16_t d : digits) {
                int v = -1;
                if (d >= u'0' && d <= u'9')
                    v = d - u'0';
                else if (hex && d >= u'a' && d <= u'f')
                    v = d - u'a' + 10;
                else if (hex && d >= u'A' && d <= u'F')
                    v = d - u'A' + 10;
                if (v < 0)
                    fail(hex ? "Invalid syntax for a hexadecimal numeric entity reference." : "Invalid syntax for a decimal numeric entity reference.");
                c = c * (hex ? 16 : 10) + static_cast<char32_t>(v);
                if (c > 0x10FFFF)
                    fail("Invalid character in the given encoding.");
            }
            if (c == 0)
                fail("Invalid character in the given encoding.");
            appendCodePoint(out, c);
        } else if (ref == u"lt") {
            out += u'<';
        } else if (ref == u"gt") {
            out += u'>';
        } else if (ref == u"amp") {
            out += u'&';
        } else if (ref == u"apos") {
            out += u'\'';
        } else if (ref == u"quot") {
            out += u'"';
        } else {
            fail("Reference to undeclared entity '" + toUtf8String(ref) + "'.");
        }
        m_pos = end + 1;
    }

    static std::string toUtf8String(u16v s)
    {
        std::string out;
        for (char16_t c : s)
            out += c < 0x80 ? static_cast<char>(c) : '?';
        return out;
    }

    u16 attributeValue()
    {
        if (m_pos >= m_text.size() || (m_text[m_pos] != u'"' && m_text[m_pos] != u'\''))
            fail("'" + toUtf8String(m_text.substr(m_pos, 1)) + "' is an unexpected token. The expected token is '\"' or '''.");
        const char16_t quote = m_text[m_pos++];
        u16 value;
        while (true) {
            if (m_pos >= m_text.size())
                fail("Unexpected end of file has occurred.");
            const char16_t c = m_text[m_pos];
            if (c == quote) {
                ++m_pos;
                return value;
            }
            if (c == u'<')
                fail("'<', hexadecimal value 0x3C, is an invalid attribute character.");
            if (c == u'&') {
                reference(value);
                continue;
            }
            value += c;
            ++m_pos;
        }
    }

    static const u16 *lookup(const std::vector<Scope> &scopes, u16v prefix)
    {
        static const u16 kXml = u"http://www.w3.org/XML/1998/namespace";
        if (prefix == u"xml")
            return &kXml;
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
            for (const auto &[p, uri] : scope->prefixes)
                if (p == prefix)
                    return &uri;
        return nullptr;
    }

    Node element(std::vector<Scope> &scopes)
    {
        ++m_pos;
        Node node;
        node.name = name();
        Scope scope{scopes.back().defaultNamespace, {}};
        bool selfClosing = false;
        while (true) {
            const std::size_t before = m_pos;
            skipSpace();
            if (m_pos >= m_text.size())
                fail("Unexpected end of file has occurred.");
            if (startsWith(u"/>")) {
                m_pos += 2;
                selfClosing = true;
                break;
            }
            if (m_text[m_pos] == u'>') {
                ++m_pos;
                break;
            }
            if (before == m_pos)
                fail("'" + toUtf8String(m_text.substr(m_pos, 1)) + "' is an unexpected token. Expecting white space.");
            u16 attrName = name();
            skipSpace();
            if (m_pos >= m_text.size() || m_text[m_pos] != u'=')
                fail("'" + toUtf8String(m_text.substr(m_pos, 1)) + "' is an unexpected token. The expected token is '='.");
            ++m_pos;
            skipSpace();
            u16 value = attributeValue();
            for (const auto &[existing, _] : node.attributes)
                if (existing == attrName)
                    fail("'" + toUtf8String(attrName) + "' is a duplicate attribute name.");
            if (attrName == u"xmlns")
                scope.defaultNamespace = value;
            else if (attrName.starts_with(u"xmlns:"))
                scope.prefixes.emplace_back(attrName.substr(6), value);
            node.attributes.emplace_back(std::move(attrName), std::move(value));
        }
        scopes.push_back(std::move(scope));
        auto prefixOf = [](u16v qualified) { const std::size_t colon = qualified.find(u':'); return colon == u16v::npos ? u16v() : qualified.substr(0, colon); };
        const u16v prefix = prefixOf(node.name);
        if (!prefix.empty() && !lookup(scopes, prefix))
            fail("'" + toUtf8String(prefix) + "' is an undeclared prefix.");
        for (const auto &[attrName, _] : node.attributes) {
            const u16v attrPrefix = prefixOf(attrName);
            if (!attrPrefix.empty() && attrPrefix != u"xmlns" && !lookup(scopes, attrPrefix))
                fail("'" + toUtf8String(attrPrefix) + "' is an undeclared prefix.");
        }
        node.noNamespace = prefix.empty() && scopes.back().defaultNamespace.empty();
        if (!selfClosing)
            content(node, scopes);
        scopes.pop_back();
        return node;
    }

    void content(Node &node, std::vector<Scope> &scopes)
    {
        u16 text;
        bool significant = false;
        auto flush = [&] {
            if (text.empty())
                return;
            Node t;
            t.kind = significant ? Node::Kind::Text : Node::Kind::Whitespace;
            t.value = std::move(text);
            node.children.push_back(std::move(t));
            text.clear();
            significant = false;
        };
        while (true) {
            if (m_pos >= m_text.size())
                fail("Unexpected end of file has occurred. The following elements are not closed: " + toUtf8String(node.name) + ".");
            const char16_t c = m_text[m_pos];
            if (c == u'&') {
                reference(text);
                significant = true;
                continue;
            }
            if (c != u'<') {
                if (startsWith(u"]]>"))
                    fail("']]>' is not allowed in character data.");
                significant = significant || !isXmlSpace(c);
                text += c;
                ++m_pos;
                continue;
            }
            flush();
            if (startsWith(u"</")) {
                m_pos += 2;
                const std::size_t at = m_pos;
                const u16 closing = name();
                if (closing != node.name) {
                    m_pos = at;
                    fail("The '" + toUtf8String(node.name) + "' start tag does not match the end tag of '" + toUtf8String(closing) + "'.");
                }
                skipSpace();
                if (m_pos >= m_text.size() || m_text[m_pos] != u'>')
                    fail("Unexpected end of file has occurred.");
                ++m_pos;
                return;
            }
            if (startsWith(u"<!--")) {
                comment();
            } else if (startsWith(u"<![CDATA[")) {
                m_pos += 9;
                const std::size_t end = m_text.find(u"]]>", m_pos);
                if (end == u16v::npos)
                    fail("Unexpected end of file while parsing CDATA has occurred.");
                Node cdata;
                cdata.kind = Node::Kind::CData;
                cdata.value = u16(m_text.substr(m_pos, end - m_pos));
                node.children.push_back(std::move(cdata));
                m_pos = end + 3;
            } else if (startsWith(u"<?")) {
                processingInstruction();
            } else {
                node.children.push_back(element(scopes));
            }
        }
    }

    u16v m_text;
    std::size_t m_pos = 0;
};

void appendInnerText(const Node &node, u16 &out)
{
    for (const Node &child : node.children) {
        if (child.kind == Node::Kind::Element)
            appendInnerText(child, out);
        else
            out += child.value;
    }
}

}

u16v Node::localName() const
{
    const std::size_t colon = name.find(u':');
    return colon == u16::npos ? u16v(name) : u16v(name).substr(colon + 1);
}

const u16 *Node::attribute(u16v qualifiedName) const
{
    for (const auto &[n, v] : attributes)
        if (n == qualifiedName)
            return &v;
    return nullptr;
}

u16 Node::innerText() const
{
    u16 out;
    appendInnerText(*this, out);
    return out;
}

const Node *Node::child(u16v noNamespaceName) const
{
    for (const Node &c : children)
        if (c.kind == Kind::Element && c.noNamespace && c.name == noNamespaceName)
            return &c;
    return nullptr;
}

Node parse(u16v text)
{
    return Parser(text).document();
}

}
