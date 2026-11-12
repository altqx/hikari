#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace hikari::api {

inline std::string lowered(std::string_view s)
{
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    return out;
}

inline bool reservedOnWindows(std::string_view part)
{
    static constexpr std::array kDevices{"con", "prn", "aux", "nul"};
    const std::string stem = lowered(part.substr(0, part.find('.')));
    if (std::ranges::find(kDevices, stem) != kDevices.end())
        return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

}
