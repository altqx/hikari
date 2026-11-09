#include "hikari/application/search_suggestions.h"

#include <charconv>
#include <cstdint>
#include <regex>

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

std::string plainText(const std::string &inner)
{
    static const std::regex kTag("<[^>]*>");
    static const std::regex kSpace("\\s+");
    std::string text = std::regex_replace(decoded(std::regex_replace(inner, kTag, "")), kSpace, " ");
    const auto first = text.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

std::string attribute(const std::string &tag, const char *name)
{
    const std::regex pattern(std::string("\\b") + name + "\\s*=\\s*(\"([^\"]*)\"|'([^']*)')", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(tag, match, pattern))
        return {};
    return match[2].matched ? match[2].str() : match[3].str();
}

bool isChip(const std::string &tag)
{
    static const std::regex kChipClass("(^|\\s)chip(\\s|$)");
    return std::regex_search(attribute(tag, "class"), kChipClass);
}

std::string logo(const std::string &html, const char *cls)
{
    const std::regex pattern(std::string("<svg\\b[^>]*\\bclass\\s*=\\s*[\"']") + cls + "[\"'][^>]*>[\\s\\S]*?</svg>",
                             std::regex::icase);
    std::smatch match;
    return std::regex_search(html, match, pattern) ? match.str() : std::string();
}

}

std::optional<SearchSuggestions> parseSearchSuggestions(std::string_view html)
{
    const std::string source(html);
    static const std::regex kAnchor("(<a\\b[^>]*>)([\\s\\S]*?)</a\\s*>", std::regex::icase);
    SearchSuggestions parts;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), kAnchor);
         it != std::sregex_iterator() && parts.chips.size() < kMaxSearchSuggestions; ++it) {
        const std::string tag = (*it)[1].str();
        if (!isChip(tag))
            continue;
        std::string url = decoded(attribute(tag, "href"));
        std::string text = plainText((*it)[2].str());
        if (url.starts_with("https://") && !text.empty())
            parts.chips.push_back({std::move(text), std::move(url)});
    }
    if (parts.chips.empty())
        return std::nullopt;
    parts.lightLogo = logo(source, "logo-light");
    parts.darkLogo = logo(source, "logo-dark");
    return parts;
}

}
