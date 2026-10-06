#pragma once

// Y7: the part of wxString (wxWidgets 3.3, wide characters) that legacy
// colorspace.cpp uses, with wx's own semantics: Trim strips wxSafeIsspace
// characters (below 127 and iswspace), ToLong is wxStringToIntType over
// wcstol (true only when the whole, non-empty text is read without ERANGE).

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <string>

#ifndef _T
#define _T(x) L##x
#endif

class wxString {
public:
    wxString() = default;
    wxString(const wchar_t *text) : m_text(text) {}
    wxString(std::wstring text) : m_text(std::move(text)) {}

    static wxString Format(const wchar_t *format, ...)
    {
        wchar_t buffer[256];
        va_list args;
        va_start(args, format);
        std::vswprintf(buffer, 256, format, args);
        va_end(args);
        return wxString(buffer);
    }

    std::size_t size() const { return m_text.size(); }
    const std::wstring &str() const { return m_text; }

    wxString &Trim(bool fromRight = true)
    {
        auto space = [](wchar_t c) { return c < 127 && std::iswspace(static_cast<wint_t>(c)); };
        if (fromRight) {
            while (!m_text.empty() && space(m_text.back()))
                m_text.pop_back();
        } else {
            std::size_t i = 0;
            while (i < m_text.size() && space(m_text[i]))
                ++i;
            m_text.erase(0, i);
        }
        return *this;
    }

    bool StartsWith(const wchar_t *prefix) const { return m_text.starts_with(prefix); }

    wxString &Remove(std::size_t pos, std::size_t len)
    {
        m_text.erase(pos, len);
        return *this;
    }

    wxString Mid(std::size_t first, std::size_t count) const
    {
        return first >= m_text.size() ? wxString() : wxString(m_text.substr(first, count));
    }

    bool ToLong(long *value, int base) const
    {
        const wchar_t *start = m_text.c_str();
        wchar_t *end = nullptr;
        errno = 0;
        *value = std::wcstol(start, &end, base);
        return !*end && end != start && errno != ERANGE;
    }

private:
    std::wstring m_text;
};
