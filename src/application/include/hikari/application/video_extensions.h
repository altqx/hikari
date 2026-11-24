#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace hikari::application {

// Every extension HikariSub treats as a video file, lower case, in open-dialog order.
inline constexpr std::array<std::string_view, 18> kVideoExtensions{
    "mkv", "mp4", "avi", "mov", "webm", "ts", "m2ts", "wmv", "y4m",
    "m4v", "flv", "ogm", "asf", "rmvb", "rm", "3gp",  "mpg", "mpeg"};

// Exact match: callers lower-case first when their lookup ignores case.
constexpr bool isVideoExtension(std::string_view extension)
{
    return std::ranges::find(kVideoExtensions, extension) != kVideoExtensions.end();
}

}
