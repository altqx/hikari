#include "hikari/application/setting_link.h"

#include <algorithm>
#include <cctype>

namespace hikari::application {

namespace {

bool startsWithIgnoringCase(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

bool validId(std::string_view id)
{
    return !id.empty() && id.size() <= 200 && std::ranges::all_of(id, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-';
    });
}

}

std::optional<SettingLink> parseSettingLink(std::string_view link)
{
    constexpr std::string_view prefix = "hikarisub://settings";
    if (!startsWithIgnoringCase(link, prefix))
        return std::nullopt;
    std::string_view rest = link.substr(prefix.size());
    std::string_view query;
    if (const auto at = rest.find('?'); at != std::string_view::npos) {
        query = rest.substr(at + 1);
        rest = rest.substr(0, at);
    }
    if (rest.ends_with('/'))
        rest.remove_suffix(1);
    SettingLink out;
    if (!rest.empty()) {
        if (!rest.starts_with('/') || !validId(rest.substr(1)))
            return std::nullopt;
        out.setting = std::string(rest.substr(1));
    }
    while (!query.empty()) {
        const auto end = query.find('&');
        if (query.substr(0, end) == "scope=project")
            out.project = true;
        query = end == std::string_view::npos ? std::string_view() : query.substr(end + 1);
    }
    return out;
}

}
