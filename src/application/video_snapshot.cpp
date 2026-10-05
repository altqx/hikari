#include "hikari/application/video_snapshot.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

namespace {

// wxString::BeforeLast: empty when the character is not there.
std::string_view beforeLast(std::string_view text, char c)
{
    const auto at = text.rfind(c);
    return at == std::string_view::npos ? std::string_view() : text.substr(0, at);
}

// The name after the last separator ('/' or '\\').
std::string_view fileName(std::string_view path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// The number a snapshot's file name holds: the digits between "<stem>_" and
// the next '_', or 0 when the name does not continue the stem that way.
int snapshotNumberOf(std::string_view name, std::string_view stem)
{
    if (name.size() <= stem.size() + 1 || name.substr(0, stem.size()) != stem || name[stem.size()] != '_')
        return 0;
    const std::string_view rest = name.substr(stem.size() + 1);
    const auto end = rest.find_first_not_of("0123456789");
    if (end == 0 || end == std::string_view::npos || rest[end] != '_')
        return 0;
    // wxAtoi: strtol's int, as legacy read it.
    return static_cast<int>(std::strtol(std::string(rest.substr(0, end)).c_str(), nullptr, 10));
}

} // namespace

int nextSnapshotNumber(std::string_view videoPath, const std::vector<std::string> &paths)
{
    // RendererVideo.cpp:1238-1258 took the first "_<digits>_" of each whole
    // path; V4-snapshot-number reads it from the file name after the video's
    // name. Then the smallest number from 1 that the sorted list does not hold.
    const std::string_view stem = beforeLast(fileName(videoPath), '.');
    std::vector<int> nums;
    for (const std::string &file : paths) {
        if (const int n = snapshotNumberOf(fileName(file), stem); n > 0)
            nums.push_back(n);
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
    return std::string(beforeLast(fileName(videoPath), '.')) + "_*_*.png";
}

std::string nextSnapshotPath(std::string_view videoPath, const std::vector<std::string> &paths, int ms,
                             const std::function<bool(const std::string &)> &exists)
{
    // V4-snapshot-number: a taken path (a file the listing missed) raises
    // the number rather than being replaced.
    for (int number = nextSnapshotNumber(videoPath, paths);; ++number) {
        std::string path = snapshotPath(videoPath, number, ms);
        if (!exists || !exists(path))
            return path;
    }
}

} // namespace hikari::application
