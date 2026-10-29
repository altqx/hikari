#pragma once

#include "hikari/application/indexed_source.h"
#include "hikari/application/subtitle_render.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// A snapshot: tightly packed 8-bit R, G, B, top-down.
struct SnapshotImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgb;
};

// Source-over: each channel overlay + (frame * (255 - alpha) + 127) / 255.
// `overlay` (premultiplied BGRA at the frame's size) may be null or empty.
SnapshotImage snapshotImage(const IndexedFrame &frame, const OverlayFrame *overlay);

// The smallest number from 1 that no file name in `paths` (full paths matching
// snapshotPattern) holds as "<video name>_<digits>_".
int nextSnapshotNumber(std::string_view videoPath, const std::vector<std::string> &paths);

// snapshotPath with nextSnapshotNumber, raised while `exists` reports the path taken.
std::string nextSnapshotPath(std::string_view videoPath, const std::vector<std::string> &paths, int ms,
                             const std::function<bool(const std::string &)> &exists);

// The video's path up to its last '.', then "_<number>_<hh;mm;ss,mmm>.png" for
// the frame's start time.
std::string snapshotPath(std::string_view videoPath, int number, int ms);

// The folder listing's wildcard: "<video file name without extension>_*_*.png".
std::string snapshotPattern(std::string_view videoPath);

} // namespace hikari::application
