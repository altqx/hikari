// Ported from Lua 5.1's lstrlib.c.
// Copyright (C) 1994-2012 Lua.org, PUC-Rio. MIT licence:
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "hikari/core/lua_pattern.h"

#include <array>
#include <stdexcept>

namespace hikari::core::luapattern {

namespace {

constexpr char kEsc = '%';
constexpr int kMaxCaptures = 32;
constexpr std::ptrdiff_t kUnfinished = -1;
constexpr std::ptrdiff_t kPosition = -2;

bool isAlpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(int c) { return c >= '0' && c <= '9'; }
bool isLower(int c) { return c >= 'a' && c <= 'z'; }
bool isUpper(int c) { return c >= 'A' && c <= 'Z'; }
bool isSpace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
bool isCntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
bool isPunct(int c) { return c > 32 && c < 127 && !isAlpha(c) && !isDigit(c); }
bool isXDigit(int c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int toLower(int c) { return isUpper(c) ? c - 'A' + 'a' : c; }

[[noreturn]] void malformed(const char *what)
{
    throw std::invalid_argument(what);
}

bool matchClass(int c, int cl)
{
    bool res;
    switch (toLower(cl)) {
    case 'a': res = isAlpha(c); break;
    case 'c': res = isCntrl(c); break;
    case 'd': res = isDigit(c); break;
    case 'l': res = isLower(c); break;
    case 'p': res = isPunct(c); break;
    case 's': res = isSpace(c); break;
    case 'u': res = isUpper(c); break;
    case 'w': res = isAlpha(c) || isDigit(c); break;
    case 'x': res = isXDigit(c); break;
    case 'z': res = c == 0; break;
    default: return cl == c;
    }
    return isLower(cl) ? res : !res;
}

struct Capture {
    const char *init = nullptr;
    std::ptrdiff_t len = 0;
};

class Matcher {
public:
    Matcher(std::string_view subject, std::string_view pattern)
        : m_srcInit(subject.data()), m_srcEnd(subject.data() + subject.size()), m_patEnd(pattern.data() + pattern.size())
    {
    }

    const char *match(const char *s, const char *p);
    void reset() { m_level = 0; }
    int level() const { return m_level; }
    std::string capture(int i, const char *s, const char *e) const;
    Captures captures(const char *s, const char *e, bool wholeWhenNone) const;

private:
    int at(const char *p) const { return p < m_patEnd ? static_cast<unsigned char>(*p) : 0; }
    const char *classEnd(const char *p) const;
    bool matchBracketClass(int c, const char *p, const char *ec) const;
    bool singleMatch(int c, const char *p, const char *ep) const;
    const char *matchBalance(const char *s, const char *p) const;
    const char *maxExpand(const char *s, const char *p, const char *ep);
    const char *minExpand(const char *s, const char *p, const char *ep);
    const char *startCapture(const char *s, const char *p, std::ptrdiff_t what);
    const char *endCapture(const char *s, const char *p);
    const char *matchCapture(const char *s, int l) const;
    int checkCapture(int l) const;
    int captureToClose() const;

    const char *m_srcInit;
    const char *m_srcEnd;
    const char *m_patEnd;
    int m_level = 0;
    std::array<Capture, kMaxCaptures> m_capture{};
};

int Matcher::checkCapture(int l) const
{
    l -= '1';
    if (l < 0 || l >= m_level || m_capture[l].len == kUnfinished)
        malformed("invalid capture index");
    return l;
}

int Matcher::captureToClose() const
{
    for (int level = m_level - 1; level >= 0; --level)
        if (m_capture[level].len == kUnfinished)
            return level;
    malformed("invalid pattern capture");
}

const char *Matcher::classEnd(const char *p) const
{
    switch (at(p++)) {
    case kEsc:
        if (p >= m_patEnd)
            malformed("malformed pattern (ends with '%')");
        return p + 1;
    case '[':
        if (at(p) == '^')
            ++p;
        do {
            if (p >= m_patEnd)
                malformed("malformed pattern (missing ']')");
            if (at(p++) == kEsc && p < m_patEnd)
                ++p;
        } while (at(p) != ']');
        return p + 1;
    default: return p;
    }
}

bool Matcher::matchBracketClass(int c, const char *p, const char *ec) const
{
    bool sig = true;
    if (at(p + 1) == '^') {
        sig = false;
        ++p;
    }
    while (++p < ec) {
        if (at(p) == kEsc) {
            ++p;
            if (matchClass(c, at(p)))
                return sig;
        } else if (at(p + 1) == '-' && p + 2 < ec) {
            p += 2;
            if (at(p - 2) <= c && c <= at(p))
                return sig;
        } else if (at(p) == c) {
            return sig;
        }
    }
    return !sig;
}

bool Matcher::singleMatch(int c, const char *p, const char *ep) const
{
    switch (at(p)) {
    case '.': return true;
    case kEsc: return matchClass(c, at(p + 1));
    case '[': return matchBracketClass(c, p, ep - 1);
    default: return at(p) == c;
    }
}

const char *Matcher::matchBalance(const char *s, const char *p) const
{
    if (p >= m_patEnd || p + 1 >= m_patEnd)
        malformed("unbalanced pattern");
    if (s >= m_srcEnd || *s != *p)
        return nullptr;
    const char b = *p, e = *(p + 1);
    int cont = 1;
    while (++s < m_srcEnd) {
        if (*s == e) {
            if (--cont == 0)
                return s + 1;
        } else if (*s == b) {
            ++cont;
        }
    }
    return nullptr;
}

const char *Matcher::maxExpand(const char *s, const char *p, const char *ep)
{
    std::ptrdiff_t i = 0;
    while (s + i < m_srcEnd && singleMatch(static_cast<unsigned char>(s[i]), p, ep))
        ++i;
    for (; i >= 0; --i)
        if (const char *res = match(s + i, ep + 1))
            return res;
    return nullptr;
}

const char *Matcher::minExpand(const char *s, const char *p, const char *ep)
{
    for (;;) {
        if (const char *res = match(s, ep + 1))
            return res;
        if (s < m_srcEnd && singleMatch(static_cast<unsigned char>(*s), p, ep))
            ++s;
        else
            return nullptr;
    }
}

const char *Matcher::startCapture(const char *s, const char *p, std::ptrdiff_t what)
{
    if (m_level >= kMaxCaptures)
        malformed("too many captures");
    m_capture[m_level] = Capture{s, what};
    ++m_level;
    const char *res = match(s, p);
    if (!res)
        --m_level;
    return res;
}

const char *Matcher::endCapture(const char *s, const char *p)
{
    const int l = captureToClose();
    m_capture[l].len = s - m_capture[l].init;
    const char *res = match(s, p);
    if (!res)
        m_capture[l].len = kUnfinished;
    return res;
}

const char *Matcher::matchCapture(const char *s, int l) const
{
    l = checkCapture(l);
    const auto len = m_capture[l].len;
    if (m_srcEnd - s >= len && std::string_view(m_capture[l].init, len) == std::string_view(s, len))
        return s + len;
    return nullptr;
}

const char *Matcher::match(const char *s, const char *p)
{
    for (;;) {
        if (p >= m_patEnd)
            return s;
        switch (at(p)) {
        case '(':
            if (at(p + 1) == ')')
                return startCapture(s, p + 2, kPosition);
            return startCapture(s, p + 1, kUnfinished);
        case ')': return endCapture(s, p + 1);
        case kEsc:
            if (at(p + 1) == 'b') {
                s = matchBalance(s, p + 2);
                if (!s)
                    return nullptr;
                p += 4;
                continue;
            }
            if (at(p + 1) == 'f') {
                p += 2;
                if (at(p) != '[')
                    malformed("missing '[' after '%f' in pattern");
                const char *ep = classEnd(p);
                const int previous = s == m_srcInit ? 0 : static_cast<unsigned char>(*(s - 1));
                const int current = s < m_srcEnd ? static_cast<unsigned char>(*s) : 0;
                if (matchBracketClass(previous, p, ep - 1) || !matchBracketClass(current, p, ep - 1))
                    return nullptr;
                p = ep;
                continue;
            }
            if (isDigit(at(p + 1))) {
                s = matchCapture(s, at(p + 1));
                if (!s)
                    return nullptr;
                p += 2;
                continue;
            }
            break;
        case '$':
            if (p + 1 == m_patEnd)
                return s == m_srcEnd ? s : nullptr;
            break;
        default: break;
        }
        const char *ep = classEnd(p);
        const bool m = s < m_srcEnd && singleMatch(static_cast<unsigned char>(*s), p, ep);
        switch (at(ep)) {
        case '?':
            if (m)
                if (const char *res = match(s + 1, ep + 1))
                    return res;
            p = ep + 1;
            continue;
        case '*': return maxExpand(s, p, ep);
        case '+': return m ? maxExpand(s + 1, p, ep) : nullptr;
        case '-': return minExpand(s, p, ep);
        default:
            if (!m)
                return nullptr;
            ++s;
            p = ep;
            continue;
        }
    }
}

std::string Matcher::capture(int i, const char *s, const char *e) const
{
    if (i >= m_level) {
        if (i == 0)
            return std::string(s, e);
        malformed("invalid capture index");
    }
    const auto &c = m_capture[i];
    if (c.len == kUnfinished)
        malformed("unfinished capture");
    if (c.len == kPosition)
        return std::to_string(c.init - m_srcInit + 1);
    return std::string(c.init, c.len);
}

Captures Matcher::captures(const char *s, const char *e, bool wholeWhenNone) const
{
    const int n = m_level == 0 && wholeWhenNone ? 1 : m_level;
    Captures out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(capture(i, s, e));
    return out;
}

template <typename Add>
Replaced substitute(std::string_view subject, std::string_view pattern, const Add &add)
{
    const bool anchor = !pattern.empty() && pattern.front() == '^';
    if (anchor)
        pattern.remove_prefix(1);
    Matcher ms(subject, pattern);
    Replaced out;
    const char *src = subject.data();
    const char *end = subject.data() + subject.size();
    for (;;) {
        ms.reset();
        const char *e = ms.match(src, pattern.data());
        if (e) {
            ++out.count;
            add(ms, out.text, src, e);
        }
        if (e && e > src)
            src = e;
        else if (src < end)
            out.text += *src++;
        else
            break;
        if (anchor)
            break;
    }
    out.text.append(src, end);
    return out;
}

}

std::optional<Match> find(std::string_view subject, std::string_view pattern, std::size_t from)
{
    if (from > subject.size())
        return std::nullopt;
    const bool anchor = !pattern.empty() && pattern.front() == '^';
    if (anchor)
        pattern.remove_prefix(1);
    Matcher ms(subject, pattern);
    const char *s1 = subject.data() + from;
    const char *end = subject.data() + subject.size();
    do {
        ms.reset();
        if (const char *e = ms.match(s1, pattern.data()))
            return Match{static_cast<std::size_t>(s1 - subject.data()), static_cast<std::size_t>(e - subject.data()),
                         ms.captures(s1, e, false)};
    } while (s1++ < end && !anchor);
    return std::nullopt;
}

Replaced gsub(std::string_view subject, std::string_view pattern, std::string_view replacement)
{
    return substitute(subject, pattern, [&](const Matcher &ms, std::string &out, const char *s, const char *e) {
        for (std::size_t i = 0; i < replacement.size(); ++i) {
            const char c = replacement[i];
            if (c != kEsc) {
                out += c;
                continue;
            }
            ++i;
            const int next = i < replacement.size() ? static_cast<unsigned char>(replacement[i]) : 0;
            if (!isDigit(next)) {
                if (i < replacement.size())
                    out += replacement[i];
            } else if (next == '0') {
                out.append(s, e);
            } else {
                out += ms.capture(next - '1', s, e);
            }
        }
    });
}

Replaced gsub(std::string_view subject, std::string_view pattern,
              const std::function<std::optional<std::string>(const Captures &)> &replacement)
{
    return substitute(subject, pattern, [&](const Matcher &ms, std::string &out, const char *s, const char *e) {
        if (auto value = replacement(ms.captures(s, e, true)))
            out += *value;
        else
            out.append(s, e);
    });
}

}
