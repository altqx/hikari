#include "hikari/application/search_suggestions.h"

#include <charconv>
#include <cstdint>
#include <cctype>
#include <optional>

namespace hikari::application::ai {

namespace {

void appendUtf8(std::string &out, char32_t code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x110000) {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::optional<char32_t> numericEntity(std::string_view body)
{
    const bool hex = body.starts_with("#x") || body.starts_with("#X");
    const std::string_view digits = body.substr(hex ? 2 : 1);
    std::uint32_t code = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
    if (digits.empty() || error != std::errc() || end != digits.data() + digits.size())
        return std::nullopt;
    return static_cast<char32_t>(code);
}

std::string decoded(std::string_view text)
{
    static constexpr std::pair<std::string_view, std::string_view> kNamed[] = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "}};
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::size_t semi = text[i] == '&' ? text.find(';', i) : std::string_view::npos;
        if (semi == std::string_view::npos || semi - i > 10) {
            out += text[i];
            continue;
        }
        const std::string_view body = text.substr(i + 1, semi - i - 1);
        bool known = false;
        if (body.starts_with('#')) {
            if (const auto code = numericEntity(body)) {
                appendUtf8(out, *code);
                known = true;
            }
        } else {
            for (const auto &[name, value] : kNamed)
                if (body == name) {
                    out += value;
                    known = true;
                }
        }
        if (known)
            i = semi;
        else
            out += text[i];
    }
    return out;
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string lowered(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string plainText(std::string_view inner)
{
    std::string stripped;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] != '<') {
            stripped += inner[i];
            continue;
        }
        const std::size_t close = inner.find('>', i);
        if (close == std::string_view::npos)
            break;
        i = close;
    }
    std::string text;
    for (const char c : decoded(stripped)) {
        if (!isSpace(c))
            text += c;
        else if (!text.empty() && text.back() != ' ')
            text += ' ';
    }
    if (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

std::string attribute(std::string_view tag, std::string_view name)
{
    const std::string lower = lowered(tag);
    for (std::size_t at = lower.find(name); at != std::string::npos; at = lower.find(name, at + 1)) {
        const bool starts = at == 0 || !(std::isalnum(static_cast<unsigned char>(lower[at - 1])) || lower[at - 1] == '-');
        std::size_t i = at + name.size();
        while (i < tag.size() && isSpace(tag[i]))
            ++i;
        if (!starts || i >= tag.size() || tag[i] != '=')
            continue;
        ++i;
        while (i < tag.size() && isSpace(tag[i]))
            ++i;
        if (i >= tag.size() || (tag[i] != '"' && tag[i] != '\''))
            continue;
        const std::size_t close = tag.find(tag[i], i + 1);
        if (close == std::string_view::npos)
            return {};
        return std::string(tag.substr(i + 1, close - i - 1));
    }
    return {};
}

bool hasClass(std::string_view tag, std::string_view cls)
{
    const std::string classes = attribute(tag, "class");
    std::size_t i = 0;
    while (i < classes.size()) {
        while (i < classes.size() && isSpace(classes[i]))
            ++i;
        std::size_t j = i;
        while (j < classes.size() && !isSpace(classes[j]))
            ++j;
        if (std::string_view(classes).substr(i, j - i) == cls)
            return true;
        i = j;
    }
    return false;
}

struct Element {
    std::string_view tag;
    std::string_view inner;
    std::size_t end = 0;
};

std::optional<Element> nextElement(std::string_view html, const std::string &lower, std::string_view name, std::size_t from)
{
    const std::string open = "<" + std::string(name);
    const std::string close = "</" + std::string(name);
    for (std::size_t at = lower.find(open, from); at != std::string::npos; at = lower.find(open, at + 1)) {
        const std::size_t after = at + open.size();
        if (after >= lower.size() || !(isSpace(lower[after]) || lower[after] == '>'))
            continue;
        const std::size_t tagEnd = lower.find('>', after);
        if (tagEnd == std::string::npos)
            return std::nullopt;
        for (std::size_t c = lower.find(close, tagEnd); c != std::string::npos; c = lower.find(close, c + 1)) {
            std::size_t i = c + close.size();
            while (i < lower.size() && isSpace(lower[i]))
                ++i;
            if (i < lower.size() && lower[i] == '>')
                return Element{html.substr(at, tagEnd + 1 - at), html.substr(tagEnd + 1, c - tagEnd - 1), i + 1};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string logo(std::string_view html, const std::string &lower, std::string_view cls)
{
    for (auto svg = nextElement(html, lower, "svg", 0); svg; svg = nextElement(html, lower, "svg", svg->end))
        if (attribute(svg->tag, "class") == cls) {
            const std::size_t at = static_cast<std::size_t>(svg->tag.data() - html.data());
            return std::string(html.substr(at, svg->end - at));
        }
    return {};
}

}

std::optional<SearchSuggestions> parseSearchSuggestions(std::string_view html)
{
    const std::string lower = lowered(html);
    SearchSuggestions parts;
    for (auto a = nextElement(html, lower, "a", 0); a && parts.chips.size() < kMaxSearchSuggestions;
         a = nextElement(html, lower, "a", a->end)) {
        if (!hasClass(a->tag, "chip"))
            continue;
        std::string url = decoded(attribute(a->tag, "href"));
        std::string text = plainText(a->inner);
        if (url.starts_with("https://") && !text.empty())
            parts.chips.push_back({std::move(text), std::move(url)});
    }
    if (parts.chips.empty())
        return std::nullopt;
    parts.lightLogo = logo(html, lower, "logo-light");
    parts.darkLogo = logo(html, lower, "logo-dark");
    return parts;
}

}
