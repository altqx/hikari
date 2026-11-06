// Ported from YTSubConverter 1.6.5, Copyright (c) 2018 arcusmaximus, MIT licence (see THIRDPARTY).
#define PCRE2_CODE_UNIT_WIDTH 16
#include "srv3_net.h"

#include <pcre2.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace hikari::core::srv3 {

Color Color::fromArgb(int a, int r, int g, int b)
{
    for (int v : {a, r, g, b})
        if (v < 0 || v > 255)
            throw ConversionError("Color component out of range.");
    return Color(static_cast<std::uint32_t>(a) << 24 | static_cast<std::uint32_t>(r) << 16 | static_cast<std::uint32_t>(g) << 8 |
                 static_cast<std::uint32_t>(b));
}

Color changeAlpha(Color color, int alpha)
{
    return Color::fromArgb(static_cast<std::uint32_t>(alpha) << 24 | (color.toArgb() & 0xFFFFFF));
}

bool isDark(Color color)
{
    return std::max({color.r(), color.g(), color.b()}) < 128;
}

Color brighten(Color color)
{
    const int r = std::max(color.r(), 1), g = std::max(color.g(), 1), b = std::max(color.b(), 1);
    const float factor = 255.0f / static_cast<float>(std::max({r, g, b}));
    auto scaled = [&](int v) { return static_cast<int>(static_cast<std::uint8_t>(std::nearbyint(static_cast<double>(v * factor)))); };
    return Color::fromArgb(color.a(), scaled(r), scaled(g), scaled(b));
}

u16 toHtml(Color color)
{
    if (color.isEmpty())
        return {};
    static constexpr char kHex[] = "0123456789ABCDEF";
    u16 out = u"#";
    const std::uint32_t rgb = color.toArgb() & 0xFFFFFF;
    for (int shift = 20; shift >= 0; shift -= 4)
        out += static_cast<char16_t>(kHex[(rgb >> shift) & 0xF]);
    return out;
}

NetRandom::NetRandom(int seed)
{
    constexpr int kBig = std::numeric_limits<int>::max();
    constexpr int kSeed = 161803398;
    const int subtraction = seed == std::numeric_limits<int>::min() ? kBig : std::abs(seed);
    int mj = kSeed - subtraction;
    m_seeds[55] = mj;
    int mk = 1;
    int ii = 0;
    for (int i = 1; i < 55; ++i) {
        if ((ii += 21) >= 55)
            ii -= 55;
        m_seeds[ii] = mk;
        mk = mj - mk;
        if (mk < 0)
            mk += kBig;
        mj = m_seeds[ii];
    }
    for (int k = 1; k < 5; ++k)
        for (int i = 1; i < 56; ++i) {
            int n = i + 30;
            if (n >= 55)
                n -= 55;
            m_seeds[i] -= m_seeds[1 + n];
            if (m_seeds[i] < 0)
                m_seeds[i] += kBig;
        }
}

double NetRandom::sample()
{
    constexpr int kBig = std::numeric_limits<int>::max();
    int next = m_next + 1;
    if (next >= 56)
        next = 1;
    int nextP = m_nextP + 1;
    if (nextP >= 56)
        nextP = 1;
    int value = m_seeds[next] - m_seeds[nextP];
    if (value == kBig)
        --value;
    if (value < 0)
        value += kBig;
    m_seeds[next] = value;
    m_next = next;
    m_nextP = nextP;
    return value * (1.0 / kBig);
}

namespace {

bool isNumberWhite(char16_t c)
{
    return c == u' ' || (c >= 0x09 && c <= 0x0D);
}

bool isDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

u16v trimNumberWhite(u16v s)
{
    while (!s.empty() && isNumberWhite(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && (isNumberWhite(s.back()) || s.back() == 0))
        s.remove_suffix(1);
    return s;
}

bool equalsIgnoreCase(u16v s, std::string_view ascii)
{
    if (s.size() != ascii.size())
        return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c - u'A' + u'a');
        if (c != static_cast<unsigned char>(ascii[i]))
            return false;
    }
    return true;
}

bool isIgnorable(char16_t c)
{
    return c <= 0x08 || (c >= 0x0E && c <= 0x1F) || (c >= 0x7F && c <= 0x9F) || c == 0xAD || c == 0x034F || c == 0x061C ||
           c == 0x180E || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x206F) ||
           (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF;
}

bool isCombining(char16_t c)
{
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F) || c == 0x3099 || c == 0x309A;
}

}

std::optional<int> parseInt(u16v s)
{
    s = trimNumberWhite(s);
    bool negative = false;
    if (!s.empty() && (s.front() == u'-' || s.front() == u'+')) {
        negative = s.front() == u'-';
        s.remove_prefix(1);
    }
    if (s.empty())
        return std::nullopt;
    std::int64_t value = 0;
    for (char16_t c : s) {
        if (!isDigit(c))
            return std::nullopt;
        value = value * 10 + (c - u'0');
        if (value > static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1)
            return std::nullopt;
    }
    if (negative)
        value = -value;
    if (value > std::numeric_limits<int>::max() || value < std::numeric_limits<int>::min())
        return std::nullopt;
    return static_cast<int>(value);
}

std::optional<float> parseFloat(u16v s, bool any)
{
    s = trimNumberWhite(s);
    const u16v whole = s;
    bool negative = false;
    bool parenthesised = false;
    if (any && !s.empty() && s.front() == u'(' && s.back() == u')') {
        parenthesised = true;
        negative = true;
        s = s.substr(1, s.size() - 2);
    }
    if (!s.empty() && (s.front() == u'-' || s.front() == u'+')) {
        if (parenthesised)
            return std::nullopt;
        negative = s.front() == u'-';
        s.remove_prefix(1);
    } else if (any && !parenthesised && !s.empty() && (s.back() == u'-' || s.back() == u'+')) {
        negative = s.back() == u'-';
        s.remove_suffix(1);
    }

    std::string text;
    bool digits = false, point = false;
    std::size_t i = 0;
    for (; i < s.size(); ++i) {
        const char16_t c = s[i];
        if (isDigit(c)) {
            digits = true;
            text += static_cast<char>(c);
        } else if (c == u'.' && !point) {
            point = true;
            text += '.';
        } else if (any && c == u',' && digits && !point) {
            continue;
        } else {
            break;
        }
    }
    if (!digits) {
        for (u16v name : {whole}) {
            if (equalsIgnoreCase(name, "infinity") || equalsIgnoreCase(name, "+infinity"))
                return std::numeric_limits<float>::infinity();
            if (equalsIgnoreCase(name, "-infinity"))
                return -std::numeric_limits<float>::infinity();
            if (equalsIgnoreCase(name, "nan") || equalsIgnoreCase(name, "-nan") || equalsIgnoreCase(name, "+nan"))
                return std::numeric_limits<float>::quiet_NaN();
        }
        return std::nullopt;
    }
    if (i < s.size() && (s[i] == u'e' || s[i] == u'E')) {
        std::size_t j = i + 1;
        std::string exponent = "e";
        if (j < s.size() && (s[j] == u'-' || s[j] == u'+'))
            exponent += static_cast<char>(s[j++]);
        const std::size_t start = j;
        while (j < s.size() && isDigit(s[j]))
            exponent += static_cast<char>(s[j++]);
        if (j == start)
            return std::nullopt;
        text += exponent;
        i = j;
    }
    if (i != s.size())
        return std::nullopt;

    float value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec == std::errc::result_out_of_range) {
        const double wide = std::strtod(text.c_str(), nullptr);
        value = std::abs(wide) > 1 ? std::numeric_limits<float>::infinity() : 0.0f;
    } else if (ec != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

std::optional<std::uint32_t> parseHex(u16v s, bool white)
{
    if (white)
        s = trimNumberWhite(s);
    if (s.empty())
        return std::nullopt;
    std::uint64_t value = 0;
    for (char16_t c : s) {
        int digit;
        if (isDigit(c))
            digit = c - u'0';
        else if (c >= u'a' && c <= u'f')
            digit = c - u'a' + 10;
        else if (c >= u'A' && c <= u'F')
            digit = c - u'A' + 10;
        else
            return std::nullopt;
        value = value << 4 | static_cast<std::uint64_t>(digit);
        if (value > 0xFFFFFFFFull)
            return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

bool isWhiteSpace(char16_t c)
{
    return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

u16v trim(u16v s)
{
    while (!s.empty() && isWhiteSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isWhiteSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

u16 replaceAll(u16v s, u16v from, u16v to)
{
    u16 out;
    std::size_t start = 0;
    for (std::size_t at; (at = s.find(from, start)) != u16v::npos; start = at + from.size()) {
        out.append(s.substr(start, at - start));
        out.append(to);
    }
    out.append(s.substr(start));
    return out;
}

std::vector<u16> splitMax(u16v s, u16v separator, std::optional<std::size_t> maxItems)
{
    std::vector<u16> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.size();
        if (start < s.size() && (!maxItems || out.size() + 1 < *maxItems)) {
            end = s.find(separator, start);
            if (end == u16v::npos)
                end = s.size();
        }
        out.emplace_back(s.substr(start, end - start));
        start = end + separator.size();
    }
    return out;
}

std::vector<u16> split(u16v s, char16_t separator)
{
    std::vector<u16> out;
    std::size_t start = 0;
    for (std::size_t at; (at = s.find(separator, start)) != u16v::npos; start = at + 1)
        out.emplace_back(s.substr(start, at - start));
    out.emplace_back(s.substr(start));
    return out;
}

bool startsWithLinguistic(u16v s, u16v prefix)
{
    std::size_t i = 0;
    for (char16_t p : prefix) {
        while (i < s.size() && isIgnorable(s[i]))
            ++i;
        if (i == s.size() || s[i] != p)
            return false;
        ++i;
    }
    while (i < s.size() && isIgnorable(s[i]))
        ++i;
    return i == s.size() || !isCombining(s[i]);
}

bool endsWithLinguistic(u16v s, u16v suffix)
{
    std::size_t i = s.size();
    for (auto it = suffix.rbegin(); it != suffix.rend(); ++it) {
        while (i > 0 && isIgnorable(s[i - 1]))
            --i;
        if (i == 0 || s[i - 1] != *it)
            return false;
        --i;
    }
    return true;
}

u16 toLowerInvariant(u16v s)
{
    u16 out(s);
    for (char16_t &c : out)
        if (c >= u'A' && c <= u'Z')
            c = static_cast<char16_t>(c - u'A' + u'a');
    return out;
}

u16 toU16(std::string_view ascii)
{
    return u16(ascii.begin(), ascii.end());
}

u16 decimalString(std::int64_t v)
{
    return toU16(std::to_string(v));
}

struct NetRegex::Impl {
    pcre2_code *code = nullptr;
    pcre2_match_data *data = nullptr;
    ~Impl()
    {
        pcre2_match_data_free(data);
        pcre2_code_free(code);
    }
};

NetRegex::NetRegex(u16v pattern) : m_impl(std::make_unique<Impl>())
{
    pcre2_compile_context *context = pcre2_compile_context_create(nullptr);
    pcre2_set_newline(context, PCRE2_NEWLINE_LF);
    int error = 0;
    PCRE2_SIZE offset = 0;
    m_impl->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
                                 PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF, &error, &offset, context);
    pcre2_compile_context_free(context);
    if (!m_impl->code)
        throw std::logic_error("srv3: bad regular expression");
    m_impl->data = pcre2_match_data_create_from_pattern(m_impl->code, nullptr);
}

NetRegex::~NetRegex() = default;

std::optional<NetRegex::Match> NetRegex::match(u16v subject, std::size_t from) const
{
    if (from > subject.size())
        return std::nullopt;
    const int rc = pcre2_match(m_impl->code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), from, 0, m_impl->data, nullptr);
    if (rc < 0)
        return std::nullopt;
    const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(m_impl->data);
    const std::uint32_t pairs = pcre2_get_ovector_count(m_impl->data);
    Match m;
    m.subject = subject;
    m.index = ovector[0];
    m.length = ovector[1] - ovector[0];
    for (std::uint32_t i = 0; i < pairs; ++i) {
        if (static_cast<int>(i) >= rc || ovector[2 * i] == PCRE2_UNSET)
            m.groups.emplace_back(std::nullopt);
        else
            m.groups.emplace_back(std::pair{ovector[2 * i], ovector[2 * i + 1] - ovector[2 * i]});
    }
    return m;
}

std::vector<NetRegex::Match> NetRegex::matches(u16v subject) const
{
    std::vector<Match> out;
    std::size_t from = 0;
    while (auto m = match(subject, from)) {
        from = m->length == 0 ? m->index + 1 : m->index + m->length;
        out.push_back(std::move(*m));
    }
    return out;
}

std::size_t NetRegex::groupNumber(std::string_view name) const
{
    const u16 wide = toU16(name);
    const int n = pcre2_substring_number_from_name(m_impl->code, reinterpret_cast<PCRE2_SPTR>(wide.c_str()));
    if (n < 0)
        throw std::logic_error("srv3: unknown group");
    return static_cast<std::size_t>(n);
}

u16 NetRegex::replace(u16v subject, const std::function<u16(const Match &)> &evaluator) const
{
    u16 out;
    std::size_t copied = 0;
    for (const Match &m : matches(subject)) {
        out.append(subject.substr(copied, m.index - copied));
        out.append(evaluator(m));
        copied = m.index + m.length;
    }
    out.append(subject.substr(copied));
    return out;
}

}
