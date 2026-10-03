#include "hikari/core/legacy_regex.h"

#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#include <vector>

namespace hikari::core {

namespace {

using u16 = std::u16string;
using u16v = std::u16string_view;

bool isAsciiDigit(char16_t c)
{
    return c >= u'0' && c <= u'9';
}

// SkipBracketExpression: from the '[' to its matching ']' (or the end).
std::size_t skipBracketExpression(u16v s, std::size_t it)
{
    const std::size_t end = s.size();
    ++it;
    if (it != end && s[it] == u'^')
        ++it;
    if (it != end && s[it] == u']')
        ++it;
    for (; it != end; ++it) {
        const char16_t c = s[it];
        if (c == u']')
            break;
        if (c == u'[') {
            if (++it == end)
                break;
            const char16_t c2 = s[it];
            if (c2 == u':' || c2 == u'.' || c2 == u'=') {
                for (++it; it != end; ++it) {
                    if (s[it] == c2) {
                        if (++it == end)
                            break;
                        if (s[it] == u']')
                            break;
                    }
                }
                if (it == end)
                    break;
            }
        }
    }
    return it;
}

u16 pcreOptionsToString(int opts)
{
    u16 s;
    if (opts & PCRE2_CASELESS)
        s += u'i';
    if (opts & PCRE2_MULTILINE)
        s += u'm';
    if (opts & PCRE2_DOTALL)
        s += u's';
    if (opts & PCRE2_EXTENDED)
        s += u'x';
    return s;
}

// ConvertMetasyntax: directors and embedded options.
u16 convertMetasyntax(u16 expr, int &flags)
{
    using F = LegacyRegex;
    constexpr std::size_t prefix = 3;
    if (expr.size() > prefix && expr.starts_with(u"***")) {
        switch (expr[prefix]) {
        case u':':
            flags &= ~F::Basic;
            flags |= F::Advanced;
            expr.erase(0, prefix + 1);
            break;
        case u'=':
            flags &= ~(F::Basic | F::Advanced);
            expr.replace(0, prefix + 1, u"\\Q");
            break;
        default:
            break;
        }
    }
    if ((flags & F::Advanced) && expr.starts_with(u"(?")) {
        u16 optsString;
        int opts = 0, negopts = 0;
        enum Syntax { None, BasicSyntax, ExtendedSyntax, Literal } syntax = None;
        const std::size_t start = 2;
        for (std::size_t it = start; it < expr.size(); ++it) {
            if (expr[it] == u')') {
                optsString += pcreOptionsToString(opts);
                if (negopts) {
                    optsString += u'-';
                    optsString += pcreOptionsToString(negopts);
                }
                std::size_t posAfterOpts;
                if (optsString.empty()) {
                    expr.erase(0, it + 1);
                    posAfterOpts = 0;
                } else {
                    expr.replace(start, it - start, optsString);
                    posAfterOpts = optsString.size() + 3;
                }
                flags &= ~F::Advanced;
                switch (syntax) {
                case None: flags |= F::Advanced; break;
                case BasicSyntax: flags |= F::Basic; break;
                case ExtendedSyntax: flags |= F::Extended; break;
                case Literal: expr.insert(posAfterOpts, u"\\Q"); break;
                }
                break;
            }
            if (expr[it] < u'a' || expr[it] > u'z')
                break;
            switch (expr[it]) {
            case u'b': syntax = BasicSyntax; break;
            case u'e': syntax = ExtendedSyntax; break;
            case u'q': syntax = Literal; break;
            case u'm':
            case u'n':
                negopts &= ~PCRE2_MULTILINE;
                opts |= PCRE2_MULTILINE;
                [[fallthrough]];
            case u'p':
                negopts |= PCRE2_DOTALL;
                break;
            case u'w':
                negopts &= ~(PCRE2_MULTILINE | PCRE2_DOTALL);
                opts |= PCRE2_MULTILINE | PCRE2_DOTALL;
                break;
            case u'c': negopts |= PCRE2_CASELESS; break;
            case u't': negopts |= PCRE2_EXTENDED; break;
            case u's':
                negopts &= ~PCRE2_DOTALL;
                opts |= PCRE2_DOTALL;
                break;
            case u'i':
                negopts &= ~PCRE2_CASELESS;
                opts |= PCRE2_CASELESS;
                break;
            case u'x':
                negopts &= ~PCRE2_EXTENDED;
                opts |= PCRE2_EXTENDED;
                break;
            default:
                optsString += expr[it];
                break;
            }
        }
    }
    return expr;
}

// ConvertWordBoundaries: \m \M \y \Y.
u16 convertWordBoundaries(u16v expr)
{
    u16 out;
    out.reserve(expr.size());
    for (std::size_t it = 0; it < expr.size(); ++it) {
        if (expr[it] == u'\\') {
            ++it;
            if (it == expr.size()) {
                out += u'\\';
                break;
            }
            const char16_t *replacement = nullptr;
            switch (expr[it]) {
            case u'm': replacement = u"[[:<:]]"; break;
            case u'M': replacement = u"[[:>:]]"; break;
            case u'y': replacement = u"\\b"; break;
            case u'Y': replacement = u"\\B"; break;
            default: break;
            }
            if (replacement) {
                out += replacement;
                continue;
            }
            out += u'\\';
        }
        out += expr[it];
    }
    return out;
}

} // namespace

struct LegacyRegex::Impl {
    pcre2_code *code = nullptr;
    pcre2_match_data *data = nullptr;
    std::size_t groups = 0; // pcre2_get_ovector_count, 0 with NoSub
    // The last match, as offsets into the subject matches() was given.
    mutable std::vector<std::pair<std::size_t, std::size_t>> last;
    mutable bool matched = false;
    // The last pcre2_match error other than "no match" (wx_regexec's
    // wxLogError path), 0 when there was none.
    mutable int error = 0;

    ~Impl()
    {
        pcre2_match_data_free(data);
        pcre2_code_free(code);
    }

    // wx_regexec on text[offset, end): offsets are relative to `offset`.
    int exec(u16v text, std::size_t offset, int flags) const
    {
        uint32_t options = 0;
        if (flags & NotBol)
            options |= PCRE2_NOTBOL;
        if (flags & NotEol)
            options |= PCRE2_NOTEOL;
        if (flags & NotEmpty)
            options |= PCRE2_NOTEMPTY;
        const auto *subject = reinterpret_cast<PCRE2_SPTR>(text.data() + offset);
        const int rc = pcre2_match(code, subject, text.size() - offset, 0, options, data, nullptr);
        error = rc < 0 && rc != PCRE2_ERROR_NOMATCH ? rc : 0;
        if (rc < 0)
            return rc;
        const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(data);
        const std::size_t actual = static_cast<std::size_t>(rc);
        last.assign(groups, {u16::npos, u16::npos});
        for (std::size_t n = 0; n < groups; ++n)
            if (n < actual && ovector[n * 2] != PCRE2_UNSET)
                last[n] = {ovector[n * 2], ovector[n * 2 + 1]};
        return rc;
    }
};

LegacyRegex::LegacyRegex() = default;
LegacyRegex::LegacyRegex(std::u16string_view pattern, int flags)
{
    compile(pattern, flags);
}
LegacyRegex::~LegacyRegex() = default;
LegacyRegex::LegacyRegex(LegacyRegex &&) noexcept = default;
LegacyRegex &LegacyRegex::operator=(LegacyRegex &&) noexcept = default;

std::u16string LegacyRegex::convertFromBasic(std::u16string_view bre)
{
    u16 ere;
    ere.reserve(bre.size());
    enum SinceStart { NoneYet, OnlyCaret, Some };
    struct State {
        bool isBackslash = false;
        SinceStart sinceStart;
    };
    State previous{false, NoneYet};
    for (std::size_t it = 0; it < bre.size(); ++it) {
        const char16_t c = bre[it];
        enum Disposition { Skip, Append, Escape } disposition = Append;
        State current{false, Some};
        if (previous.isBackslash) {
            disposition = Escape;
            switch (c) {
            case u'(':
                current.sinceStart = NoneYet;
                [[fallthrough]];
            case u')':
            case u'{':
            case u'}':
                disposition = Append;
                break;
            case u'<':
            case u'>':
                ere += u"[[:";
                ere += c;
                ere += u":]]";
                disposition = Skip;
                break;
            default:
                break;
            }
        } else {
            switch (c) {
            case u'\\':
                current.isBackslash = true;
                disposition = Skip;
                break;
            case u'^':
                if (previous.sinceStart == NoneYet)
                    current.sinceStart = OnlyCaret;
                else
                    disposition = Escape;
                break;
            case u'*':
                if (previous.sinceStart == NoneYet || previous.sinceStart == OnlyCaret)
                    disposition = Escape;
                break;
            case u'$': {
                disposition = Escape;
                std::size_t next = it + 1;
                if (next == bre.size()) {
                    disposition = Append;
                } else if (bre[next] == u'\\') {
                    ++next;
                    if (next != bre.size() && bre[next] == u')')
                        disposition = Append;
                }
                break;
            }
            case u'|':
            case u'+':
            case u'?':
            case u'(':
            case u')':
            case u'{':
            case u'}':
                disposition = Escape;
                break;
            case u'[': {
                const std::size_t start = it;
                it = skipBracketExpression(bre, it);
                ere += bre.substr(start, it - start);
                if (it == bre.size())
                    return ere;
                break;
            }
            default:
                break;
            }
        }
        switch (disposition) {
        case Skip:
            break;
        case Escape:
            ere += u'\\';
            [[fallthrough]];
        case Append:
            ere += bre[it];
            break;
        }
        previous = current;
    }
    if (previous.isBackslash)
        ere += u'\\';
    return ere;
}

bool LegacyRegex::compile(std::u16string_view pattern, int flags)
{
    m_impl.reset();
    m_error.clear();
    u16 expr = convertMetasyntax(u16(pattern), flags);
    if (flags & Basic) {
        expr = convertFromBasic(expr);
        flags &= ~Basic;
    } else if (flags & Advanced) {
        expr = convertWordBoundaries(expr);
    }
    m_converted = expr;
    uint32_t options = PCRE2_UTF | PCRE2_ALT_BSUX;
    if (flags & IgnoreCase)
        options |= PCRE2_CASELESS;
    if (flags & Newline)
        options |= PCRE2_MULTILINE;
    else
        options |= PCRE2_DOTALL;
    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    auto impl = std::make_unique<Impl>();
    impl->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(expr.data()), expr.size(), options, &errorCode,
                               &errorOffset, nullptr);
    if (!impl->code) {
        PCRE2_UCHAR buffer[256];
        const int len = pcre2_get_error_message(errorCode, buffer, 256);
        m_error = len < 0 ? u16(u"PCRE error") : u16(reinterpret_cast<const char16_t *>(buffer), static_cast<std::size_t>(len));
        return false;
    }
    impl->data = pcre2_match_data_create_from_pattern(impl->code, nullptr);
    impl->groups = (flags & NoSub) ? 0 : pcre2_get_ovector_count(impl->data);
    m_impl = std::move(impl);
    return true;
}

bool LegacyRegex::isValid() const
{
    return m_impl != nullptr;
}

bool LegacyRegex::matches(std::u16string_view text, int flags) const
{
    if (!m_impl)
        return false;
    m_impl->matched = m_impl->exec(text, 0, flags) >= 0;
    return m_impl->matched;
}

std::optional<std::pair<std::size_t, std::size_t>> LegacyRegex::match(std::size_t index) const
{
    if (!m_impl || !m_impl->matched || index >= m_impl->groups)
        return std::nullopt;
    const auto [start, end] = m_impl->last[index];
    if (start == u16::npos)
        return std::pair{u16::npos, std::size_t{0}};
    return std::pair{start, end - start};
}

std::u16string LegacyRegex::matchError() const
{
    if (!m_impl || !m_impl->error)
        return {};
    PCRE2_UCHAR buffer[256];
    const int len = pcre2_get_error_message(m_impl->error, buffer, 256);
    return len < 0 ? u16(u"PCRE error") : u16(reinterpret_cast<const char16_t *>(buffer), static_cast<std::size_t>(len));
}

std::size_t LegacyRegex::matchCount() const
{
    return m_impl ? m_impl->groups : 0;
}

int LegacyRegex::replace(std::u16string &text, std::u16string_view replacement, std::size_t maxMatches) const
{
    if (!m_impl)
        return -1;
    const u16 subject = text;
    u16 result;
    result.reserve(5 * subject.size() / 4);
    const bool mayHaveBackrefs = replacement.find_first_of(u"\\&") != u16v::npos;
    std::size_t matchStart = 0;
    int count = 0;
    while ((!maxMatches || static_cast<std::size_t>(count) < maxMatches) && matchStart <= subject.size() &&
           m_impl->exec(subject, matchStart, count ? NotBol : 0) >= 0) {
        m_impl->matched = true;
        u16 textNew;
        if (!mayHaveBackrefs) {
            textNew = u16(replacement);
        } else {
            for (std::size_t p = 0; p < replacement.size(); ++p) {
                std::size_t index = u16::npos;
                if (replacement[p] == u'\\') {
                    ++p;
                    if (p < replacement.size() && isAsciiDigit(replacement[p])) {
                        // wxStrtoul: every following digit.
                        index = 0;
                        while (p < replacement.size() && isAsciiDigit(replacement[p]))
                            index = index * 10 + static_cast<std::size_t>(replacement[p++] - u'0');
                        --p;
                    } else if (p >= replacement.size()) {
                        textNew += u'\\';
                        break;
                    }
                } else if (replacement[p] == u'&') {
                    index = 0;
                }
                if (index != u16::npos) {
                    // An invalid back reference is eaten (wxFAIL_MSG in a debug build).
                    if (m_impl->groups && index < m_impl->groups && m_impl->last[index].first != u16::npos) {
                        const auto [s, e] = m_impl->last[index];
                        textNew += subject.substr(matchStart + s, e - s);
                    }
                } else {
                    textNew += replacement[p];
                }
            }
        }
        const auto [start, end] = m_impl->last.empty() ? std::pair<std::size_t, std::size_t>{0, 0} : m_impl->last[0];
        result.append(subject, matchStart, start);
        matchStart += start;
        result += textNew;
        ++count;
        const std::size_t len = end - start;
        matchStart += len;
        if (len == 0) {
            // R3-hang-crash-loss: move past an empty match (one code point).
            if (matchStart >= subject.size()) {
                matchStart = subject.size() + 1;
                break;
            }
            std::size_t step = 1;
            if (subject[matchStart] >= 0xD800 && subject[matchStart] <= 0xDBFF && matchStart + 1 < subject.size())
                step = 2;
            result.append(subject, matchStart, step);
            matchStart += step;
        }
    }
    if (matchStart <= subject.size())
        result.append(subject, matchStart, u16::npos);
    text = std::move(result);
    return count;
}

} // namespace hikari::core
