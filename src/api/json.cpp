#include "hikari/api/json.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>

namespace hikari::api {

Json::Json() = default;
Json::Json(std::nullptr_t) {}
Json::Json(bool value) : m_value(value) {}
Json::Json(int value) : m_value(std::int64_t(value)) {}
Json::Json(std::int64_t value) : m_value(value) {}
Json::Json(std::uint64_t value)
{
    if (value <= std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        m_value = std::int64_t(value);
    else
        m_value = double(value);
}
Json::Json(double value) : m_value(value) {}
Json::Json(const char *value) : m_value(std::string(value)) {}
Json::Json(std::string_view value) : m_value(std::string(value)) {}
Json::Json(std::string value) : m_value(std::move(value)) {}
Json::Json(Array value) : m_value(std::move(value)) {}
Json::Json(Object value) : m_value(std::move(value)) {}
Json::Json(const Json &) = default;
Json::Json(Json &&) noexcept = default;
Json &Json::operator=(const Json &) = default;
Json &Json::operator=(Json &&) noexcept = default;
Json::~Json() = default;

Json Json::array(std::initializer_list<Json> items)
{
    return Json(Array(items));
}

Json Json::object(std::initializer_list<Member> members)
{
    Json result{Object{}};
    for (const Member &m : members)
        result[m.key] = m.value;
    return result;
}

double Json::asNumber() const
{
    return isInt() ? double(asInt()) : std::get<double>(m_value);
}

const Json *Json::find(std::string_view key) const
{
    if (!isObject())
        return nullptr;
    for (const Member &m : asObject())
        if (m.key == key)
            return &m.value;
    return nullptr;
}

Json &Json::operator[](std::string_view key)
{
    if (isNull())
        m_value = Object{};
    Object &members = asObject();
    for (Member &m : members)
        if (m.key == key)
            return m.value;
    members.push_back(Member{std::string(key), Json()});
    return members.back().value;
}

void Json::push(Json value)
{
    if (isNull())
        m_value = Array{};
    asArray().push_back(std::move(value));
}

bool operator==(const Json &a, const Json &b)
{
    return a.m_value == b.m_value;
}

namespace {

void escape(std::string &out, const std::string &s)
{
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xf];
                out += hex[c & 0xf];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void number(std::string &out, double value)
{
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char buffer[32];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, end);
}

void write(std::string &out, const Json &value, int indent, int depth)
{
    const auto newline = [&](int level) {
        if (indent < 0)
            return;
        out += '\n';
        out.append(std::size_t(indent * level), ' ');
    };
    switch (value.type()) {
    case Json::Type::Null: out += "null"; break;
    case Json::Type::Bool: out += value.asBool() ? "true" : "false"; break;
    case Json::Type::Int: out += std::to_string(value.asInt()); break;
    case Json::Type::Double: number(out, value.asNumber()); break;
    case Json::Type::String: escape(out, value.asString()); break;
    case Json::Type::Array: {
        const auto &items = value.asArray();
        out += '[';
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i)
                out += ',';
            newline(depth + 1);
            write(out, items[i], indent, depth + 1);
        }
        if (!items.empty())
            newline(depth);
        out += ']';
        break;
    }
    case Json::Type::Object: {
        const auto &members = value.asObject();
        out += '{';
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (i)
                out += ',';
            newline(depth + 1);
            escape(out, members[i].key);
            out += indent < 0 ? ":" : ": ";
            write(out, members[i].value, indent, depth + 1);
        }
        if (!members.empty())
            newline(depth);
        out += '}';
        break;
    }
    }
}

// The bytes of one well-formed UTF-8 sequence starting at `at`, or 0.
std::size_t utf8SequenceLength(std::string_view text, std::size_t at)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    std::size_t length = 0;
    char32_t c = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        c = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        c = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        c = lead & 0x07;
    } else {
        return 0;
    }
    if (at + length > text.size())
        return 0;
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(at + i) & 0xC0) != 0x80)
            return 0;
        c = (c << 6) | (byte(at + i) & 0x3F);
    }
    const char32_t minimum = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return 0;
    return length;
}

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    std::expected<Json, Json::ParseError> document()
    {
        auto value = parseValue(0);
        if (!value)
            return value;
        skipSpace();
        if (m_at != m_text.size())
            return fail("unexpected text after the value");
        return value;
    }

private:
    std::unexpected<Json::ParseError> fail(std::string message) const
    {
        return std::unexpected(Json::ParseError{m_at, std::move(message)});
    }

    void skipSpace()
    {
        while (m_at < m_text.size() &&
               (m_text[m_at] == ' ' || m_text[m_at] == '\t' || m_text[m_at] == '\n' || m_text[m_at] == '\r'))
            ++m_at;
    }

    bool literal(std::string_view word)
    {
        if (m_text.substr(m_at, word.size()) != word)
            return false;
        m_at += word.size();
        return true;
    }

    std::expected<Json, Json::ParseError> parseValue(int depth)
    {
        if (depth > Json::kMaxDepth)
            return fail("nested too deeply");
        skipSpace();
        if (m_at >= m_text.size())
            return fail("expected a value");
        switch (m_text[m_at]) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': {
            auto s = parseString();
            if (!s)
                return std::unexpected(s.error());
            return Json(std::move(*s));
        }
        case 't':
            if (literal("true"))
                return Json(true);
            break;
        case 'f':
            if (literal("false"))
                return Json(false);
            break;
        case 'n':
            if (literal("null"))
                return Json(nullptr);
            break;
        default:
            if (m_text[m_at] == '-' || (m_text[m_at] >= '0' && m_text[m_at] <= '9'))
                return parseNumber();
        }
        return fail("expected a value");
    }

    std::expected<Json, Json::ParseError> parseObject(int depth)
    {
        ++m_at;
        Json result{Json::Object{}};
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == '}') {
            ++m_at;
            return result;
        }
        for (;;) {
            skipSpace();
            if (m_at >= m_text.size() || m_text[m_at] != '"')
                return fail("expected a key");
            auto key = parseString();
            if (!key)
                return std::unexpected(key.error());
            if (result.find(*key))
                return fail("duplicate key \"" + *key + "\"");
            skipSpace();
            if (m_at >= m_text.size() || m_text[m_at] != ':')
                return fail("expected ':'");
            ++m_at;
            auto value = parseValue(depth + 1);
            if (!value)
                return value;
            result.asObject().push_back(Json::Member{std::move(*key), std::move(*value)});
            skipSpace();
            if (m_at < m_text.size() && m_text[m_at] == ',') {
                ++m_at;
                continue;
            }
            if (m_at < m_text.size() && m_text[m_at] == '}') {
                ++m_at;
                return result;
            }
            return fail("expected ',' or '}'");
        }
    }

    std::expected<Json, Json::ParseError> parseArray(int depth)
    {
        ++m_at;
        Json result{Json::Array{}};
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == ']') {
            ++m_at;
            return result;
        }
        for (;;) {
            auto value = parseValue(depth + 1);
            if (!value)
                return value;
            result.asArray().push_back(std::move(*value));
            skipSpace();
            if (m_at < m_text.size() && m_text[m_at] == ',') {
                ++m_at;
                continue;
            }
            if (m_at < m_text.size() && m_text[m_at] == ']') {
                ++m_at;
                return result;
            }
            return fail("expected ',' or ']'");
        }
    }

    std::optional<unsigned> hex4()
    {
        if (m_at + 4 > m_text.size())
            return std::nullopt;
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_at++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= unsigned(c - 'A' + 10);
            else
                return std::nullopt;
        }
        return value;
    }

    static void appendUtf8(std::string &out, char32_t c)
    {
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 0x3F));
            out += char(0x80 | ((c >> 6) & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }

    std::size_t utf8Length() const { return utf8SequenceLength(m_text, m_at); }

    std::expected<std::string, Json::ParseError> parseString()
    {
        ++m_at;
        std::string out;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '"') {
                ++m_at;
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in a string");
            if (c == '\\') {
                if (++m_at >= m_text.size())
                    break;
                const char e = m_text[m_at++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto unit = hex4();
                    if (!unit)
                        return fail("bad \\u escape");
                    char32_t code = *unit;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (!literal("\\u"))
                            return fail("lone surrogate");
                        auto low = hex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF)
                            return fail("lone surrogate");
                        code = 0x10000 + ((code - 0xD800) << 10) + (*low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        return fail("lone surrogate");
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: return fail("bad escape");
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x80) {
                out += c;
                ++m_at;
                continue;
            }
            const std::size_t length = utf8Length();
            if (!length)
                return fail("invalid UTF-8");
            out.append(m_text.substr(m_at, length));
            m_at += length;
        }
        return fail("unterminated string");
    }

    std::expected<Json, Json::ParseError> parseNumber()
    {
        const std::size_t start = m_at;
        const auto digits = [&] {
            const std::size_t from = m_at;
            while (m_at < m_text.size() && m_text[m_at] >= '0' && m_text[m_at] <= '9')
                ++m_at;
            return m_at - from;
        };
        if (m_text[m_at] == '-')
            ++m_at;
        if (m_at < m_text.size() && m_text[m_at] == '0')
            ++m_at;
        else if (!digits())
            return fail("bad number");
        bool integral = true;
        if (m_at < m_text.size() && m_text[m_at] == '.') {
            ++m_at;
            integral = false;
            if (!digits())
                return fail("bad number");
        }
        if (m_at < m_text.size() && (m_text[m_at] == 'e' || m_text[m_at] == 'E')) {
            ++m_at;
            integral = false;
            if (m_at < m_text.size() && (m_text[m_at] == '+' || m_text[m_at] == '-'))
                ++m_at;
            if (!digits())
                return fail("bad number");
        }
        const char *first = m_text.data() + start;
        const char *last = m_text.data() + m_at;
        if (integral) {
            std::int64_t value = 0;
            if (const auto [end, ec] = std::from_chars(first, last, value); ec == std::errc() && end == last)
                return Json(value);
        }
        double value = 0;
        const auto [end, ec] = std::from_chars(first, last, value);
        if (ec != std::errc() || end != last || !std::isfinite(value))
            return fail("bad number");
        return Json(value);
    }

    std::string_view m_text;
    std::size_t m_at = 0;
};

}

std::string Json::dump(int indent) const
{
    std::string out;
    write(out, *this, indent, 0);
    return out;
}

std::expected<Json, Json::ParseError> Json::parse(std::string_view text)
{
    return Parser(text).document();
}

bool isUtf8(std::string_view text)
{
    for (std::size_t at = 0; at < text.size();) {
        if (static_cast<unsigned char>(text[at]) < 0x80) {
            ++at;
            continue;
        }
        const std::size_t length = utf8SequenceLength(text, at);
        if (length == 0)
            return false;
        at += length;
    }
    return true;
}

}
