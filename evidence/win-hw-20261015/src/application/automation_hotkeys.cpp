#include "hikari/application/automation_hotkeys.h"

#include <charconv>

namespace hikari::application {

std::optional<std::string> aliasOfLegacyName(std::string_view name)
{
    constexpr std::string_view prefix = "Script ";
    if (!name.starts_with(prefix))
        return std::nullopt;
    name.remove_prefix(prefix.size());
    const auto dash = name.rfind('-');
    if (dash == std::string_view::npos || dash == 0 || dash + 1 == name.size())
        return std::nullopt;
    int ordinal = 0;
    const auto digits = name.substr(dash + 1);
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), ordinal);
    if (ec != std::errc() || end != digits.data() + digits.size() || ordinal < 0)
        return std::nullopt;
    return std::string(name.substr(0, dash)) + ":" + std::to_string(ordinal);
}

std::string legacyNameOf(std::string_view fileName, int ordinal)
{
    return "Script " + std::string(fileName) + "-" + std::to_string(ordinal);
}

std::string portableKeys(std::string_view accel)
{
    // wx joins modifiers with '-'; a trailing "-" key is written "--".
    std::string out;
    std::size_t start = 0;
    while (start < accel.size()) {
        auto dash = accel.find('-', start);
        if (dash == start) { // the '-' key itself
            out += '-';
            break;
        }
        if (dash == std::string_view::npos || dash + 1 == accel.size()) {
            out += std::string(accel.substr(start));
            if (dash != std::string_view::npos)
                out += '-';
            break;
        }
        out += std::string(accel.substr(start, dash - start)) + "+";
        start = dash + 1;
    }
    return out;
}

std::vector<MacroBinding> parseLegacyScriptHotkeys(std::string_view text)
{
    std::vector<MacroBinding> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!line.starts_with("Script "))
            continue;
        const auto equals = line.rfind('=');
        if (equals == std::string_view::npos || equals + 1 == line.size())
            continue;
        MacroBinding b;
        b.legacyName = std::string(line.substr(0, equals));
        if (!aliasOfLegacyName(b.legacyName))
            continue;
        b.keys = portableKeys(line.substr(equals + 1));
        out.push_back(std::move(b));
    }
    return out;
}

} // namespace hikari::application
