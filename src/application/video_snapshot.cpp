#include "hikari/application/video_snapshot.h"

#include <algorithm>
#include <cstdio>
#include <regex>

namespace hikari::application {

SnapshotImage snapshotImage(const IndexedFrame &frame, const OverlayFrame *overlay)
{
    SnapshotImage out;
    out.width = frame.width;
    out.height = frame.height;
    out.rgb.resize(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3);
    const bool blend = overlay && !overlay->empty && overlay->width == frame.width && overlay->height == frame.height;
    std::uint8_t *dst = out.rgb.data();
    for (int y = 0; y < frame.height; ++y) {
        const auto *src = reinterpret_cast<const std::uint8_t *>(frame.bgra.data()) + static_cast<std::size_t>(y) * frame.stride;
        const std::uint8_t *over = blend ? overlay->pixels.data() + static_cast<std::size_t>(y) * overlay->stride : nullptr;
        for (int x = 0; x < frame.width; ++x) {
            int b = src[x * 4], g = src[x * 4 + 1], r = src[x * 4 + 2];
            if (over) {
                const std::uint8_t *q = over + x * 4;
                const int inv = 255 - q[3];
                b = q[0] + (b * inv + 127) / 255;
                g = q[1] + (g * inv + 127) / 255;
                r = q[2] + (r * inv + 127) / 255;
            }
            *dst++ = static_cast<std::uint8_t>(std::min(r, 255));
            *dst++ = static_cast<std::uint8_t>(std::min(g, 255));
            *dst++ = static_cast<std::uint8_t>(std::min(b, 255));
        }
    }
    return out;
}

int nextSnapshotNumber(const std::vector<std::string> &paths)
{
    // RendererVideo.cpp:1238-1258: the first "_<digits>_" of each path, then
    // the smallest number from 1 that the sorted list does not hold.
    static const std::regex findNum("_([0-9]+)_");
    std::vector<int> nums;
    for (const std::string &file : paths) {
        std::smatch match;
        if (std::regex_search(file, match, findNum)) {
            // wxAtoi: strtol's int, as legacy read it.
            nums.push_back(static_cast<int>(std::strtol(match[1].str().c_str(), nullptr, 10)));
        }
    }
    std::sort(nums.begin(), nums.end());
    int num = 1;
    for (const int n : nums) {
        if (num == n)
            num++;
        else if (num < n)
            break;
    }
    return num;
}

namespace {

// wxString::BeforeLast: empty when the character is not there.
std::string_view beforeLast(std::string_view text, char c)
{
    const auto at = text.rfind(c);
    return at == std::string_view::npos ? std::string_view() : text.substr(0, at);
}

} // namespace

std::string snapshotPath(std::string_view videoPath, int number, int ms)
{
    // RendererVideo.cpp:1259-1266.
    char time[32];
    const int sec = ms / 1000, min = ms / 60000, hours = ms / 3600000;
    std::snprintf(time, sizeof time, "%02i;%02i;%02i,%03i", hours, min % 60, sec % 60, ms % 1000);
    std::string path(beforeLast(videoPath, '.'));
    path += "_" + std::to_string(number) + "_" + time + ".png";
    return path;
}

std::string snapshotPattern(std::string_view videoPath)
{
    // HikariPathName: the name after the last separator (backslashes are
    // separators on Windows; elsewhere HikariNormalizePath makes them '/').
    const auto slash = videoPath.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? videoPath : videoPath.substr(slash + 1);
    return std::string(beforeLast(name, '.')) + "_*_*.png";
}

} // namespace hikari::application
