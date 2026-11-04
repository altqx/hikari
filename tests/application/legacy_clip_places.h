#pragma once

#include "hikari/application/visual_vector.h"

#include <cstdlib>
#include <string>

namespace hikari::application::test {

template <typename String>
String legacyClipPlaces(const String &text)
{
    using Char = typename String::value_type;
    String out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t clip = text.find(String{Char('c'), Char('l'), Char('i'), Char('p'), Char('(')}, at);
        const std::size_t close = clip == String::npos ? String::npos : text.find(Char(')'), clip);
        if (close == String::npos)
            break;
        const std::size_t open = clip + 5;
        out += text.substr(at, open - at);
        const String body = text.substr(open, close - open);
        if (body.find(Char('m')) == String::npos) {
            out += body;
        } else {
            std::size_t token = 0;
            while (token <= body.size()) {
                std::size_t end = token;
                while (end < body.size() && body[end] != Char(' ') && body[end] != Char(','))
                    ++end;
                const String word = body.substr(token, end - token);
                std::string ascii(word.begin(), word.end());
                char *parsed = nullptr;
                const double value = ascii.empty() ? 0 : std::strtod(ascii.c_str(), &parsed);
                const bool scalePrefix = end < body.size() && body[end] == Char(',');
                const bool number = !ascii.empty() && parsed == ascii.c_str() + ascii.size() && !scalePrefix;
                if (number) {
                    const std::u16string rounded = visual::legacy::getfloat(static_cast<float>(value), "6.0f");
                    out += String(rounded.begin(), rounded.end());
                } else {
                    out += word;
                }
                if (end < body.size())
                    out += body[end];
                token = end + 1;
            }
        }
        at = close;
    }
    out += text.substr(at);
    return out;
}

template <typename String>
String legacyClipPlaces(const String &actual, const String &legacy)
{
    return actual == legacy ? actual : legacyClipPlaces(actual);
}

}
