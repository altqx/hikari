#include "hikari/application/session_file.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace hikari::application {

namespace {

void appendUtf8(std::string &out, char32_t c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

bool validUtf8(std::string_view s)
{
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        std::size_t n = 0;
        char32_t c = 0;
        if (b < 0x80) {
            ++i;
            continue;
        } else if ((b & 0xE0) == 0xC0) {
            n = 1;
            c = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            n = 2;
            c = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            n = 3;
            c = b & 0x07;
        } else {
            return false;
        }
        for (std::size_t k = 1; k <= n; ++k) {
            if (i + k >= s.size())
                return false;
            const auto t = static_cast<unsigned char>(s[i + k]);
            if ((t & 0xC0) != 0x80)
                return false;
            c = (c << 6) | (t & 0x3F);
        }
        // Overlong forms, surrogates and values past U+10FFFF are invalid.
        if ((n == 1 && c < 0x80) || (n == 2 && c < 0x800) || (n == 3 && c < 0x10000) || c > 0x10FFFF ||
            (c >= 0xD800 && c <= 0xDFFF))
            return false;
        i += n + 1;
    }
    return true;
}

std::string latin1(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char ch : s)
        appendUtf8(out, static_cast<unsigned char>(ch));
    return out;
}

std::string utf16(std::string_view s, bool bigEndian)
{
    std::string out;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        const auto a = static_cast<unsigned char>(s[i]);
        const auto b = static_cast<unsigned char>(s[i + 1]);
        char32_t unit = bigEndian ? (a << 8 | b) : (b << 8 | a);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < s.size()) {
            const auto c = static_cast<unsigned char>(s[i + 2]);
            const auto d = static_cast<unsigned char>(s[i + 3]);
            const char32_t low = bigEndian ? (c << 8 | d) : (d << 8 | c);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(out, unit);
    }
    return out;
}

std::string utf32(std::string_view s, bool bigEndian)
{
    std::string out;
    for (std::size_t i = 0; i + 3 < s.size(); i += 4) {
        std::uint32_t c = 0;
        for (int k = 0; k < 4; ++k) {
            const auto byte = static_cast<unsigned char>(s[i + (bigEndian ? k : 3 - k)]);
            c = (c << 8) | byte;
        }
        if (c <= 0x10FFFF)
            appendUtf8(out, c);
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix, std::string *rest)
{
    if (s.substr(0, prefix.size()) != prefix)
        return false;
    *rest = std::string(s.substr(prefix.size()));
    return true;
}

// wxAtoi: wxStrtol(text, nullptr, 10) as an int.
int atoi(const std::string &text)
{
    return static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
}

} // namespace

std::optional<std::string> decodeSessionBytes(std::string_view bytes, SessionPlatform platform)
{
    std::string text;
    auto has = [&](std::string_view bom) { return bytes.substr(0, bom.size()) == bom; };
    using namespace std::string_view_literals;
    if (has("\x00\x00\xFE\xFF"sv)) {
        text = utf32(bytes.substr(4), true);
    } else if (has("\xFF\xFE\x00\x00"sv)) {
        text = utf32(bytes.substr(4), false);
    } else if (has("\xFE\xFF"sv)) {
        text = utf16(bytes.substr(2), true);
    } else if (has("\xFF\xFE"sv)) {
        text = utf16(bytes.substr(2), false);
    } else {
        std::string raw(bytes);
        // The Windows build's text-mode read folded CRLF before converting.
        if (platform == SessionPlatform::Windows) {
            std::string folded;
            folded.reserve(raw.size());
            for (std::size_t i = 0; i < raw.size(); ++i)
                if (!(raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n'))
                    folded += raw[i];
            raw = std::move(folded);
        }
        if (raw.starts_with("\xEF\xBB\xBF"))
            text = raw.substr(3);
        else if (validUtf8(raw))
            text = std::move(raw);
        else
            text = latin1(raw); // wxConvAuto's fallback encoding
    }
    if (text.empty())
        return std::nullopt;
    return text;
}

std::optional<Session> parseSession(std::string_view text)
{
    // wxStringTokenizer(text, "\n", wxTOKEN_STRTOK): no empty tokens.
    std::vector<std::string> tokens;
    for (std::size_t start = 0; start <= text.size();) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        if (end > start)
            tokens.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    std::size_t next = 0;
    auto nextToken = [&]() -> std::string { return next < tokens.size() ? tokens[next++] : std::string(); };
    auto hasMoreTokens = [&] { return next < tokens.size(); };

    Session session;
    session.header = nextToken();
    if (!session.header.starts_with("[HikariSub"))
        return std::nullopt;
    session.closed = sessionClosed(text);

    SessionTab tab;
    std::string rest;
    while (true) {
        const std::string token = nextToken();
        if (startsWith(token, "Video: ", &rest))
            tab.video = rest;
        else if (startsWith(token, "Position: ", &rest))
            tab.position = atoi(rest);
        else if (startsWith(token, "Subtitles: ", &rest))
            tab.subtitles = rest;
        else if (startsWith(token, "Active: ", &rest))
            tab.active = atoi(rest);
        else if (startsWith(token, "Scroll: ", &rest))
            tab.scroll = atoi(rest);
        else if (startsWith(token, "FFMS2: ", &rest))
            tab.ffms2 = atoi(rest) != 0;
        else if (startsWith(token, "Editor: ", &rest))
            tab.editor = atoi(rest) != 0;
        else if (startsWith(token, "Audio: ", &rest))
            tab.audio = rest;
        else if (startsWith(token, "Keyframes: ", &rest))
            tab.keyframes = rest;
        // Checked after every token, as legacy did; `rest` keeps the last
        // field's value when this token is not a "Tab: " line.
        const bool last = !hasMoreTokens();
        if (startsWith(token, "Tab: ", &rest) || last) {
            if (rest != "0" || last) {
                session.tabs.push_back(tab);
                // C05-audio-association: legacy reset every field here but Audio.
                tab = SessionTab{};
            }
        }
        if (last)
            break;
    }
    return session;
}

bool sessionClosed(std::string_view text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            normalized += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        } else {
            normalized += text[i];
        }
    }
    return normalized.find("]\n[Close session]\n") != std::string::npos;
}

std::string writeSession(std::string_view program, bool closed, const std::vector<SessionTab> &tabs)
{
    std::string out = "\xEF\xBB\xBF"; // FileWrite(utf = true): a BOM, then UTF-8
    out += "[";
    out += program;
    out += "]\r\n";
    if (closed)
        out += "[Close session]\r\n";
    int number = 0;
    for (const auto &tab : tabs) {
        out += "Tab: " + std::to_string(number) + "\r\nVideo: " + tab.video + "\r\nPosition: " +
               std::to_string(tab.position) + "\r\nFFMS2: " + (tab.ffms2 ? "1" : "0") + "\r\nSubtitles: " + tab.subtitles +
               "\r\nActive: " + std::to_string(tab.active) + "\r\nScroll: " + std::to_string(tab.scroll) +
               "\r\nEditor: " + (tab.editor ? "1" : "0") + "\r\n";
        if (!tab.audio.empty() && tab.audio != tab.video)
            out += "Audio: " + tab.audio + "\r\n";
        if (!tab.keyframes.empty())
            out += "Keyframes: " + tab.keyframes + "\r\n";
        ++number;
    }
    return out;
}

} // namespace hikari::application
