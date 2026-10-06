#pragma once

// V4: the video snapshot actions (VIDEO_SAVE_FRAME_TO_PNG,
// VIDEO_COPY_FRAME_TO_CLIPBOARD, VIDEO_SAVE_SUBBED_FRAME_TO_PNG,
// VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD), as legacy RendererVideo::SaveFrame
// made them at 20d647c4 (RendererVideo.cpp:1207-1271): the paused frame,
// with the subtitles drawn on it for the "subbed" pair, as 24-bit RGB (the
// frame's alpha dropped), saved as PNG next to the video or put on the
// clipboard.

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

// The frame with the subtitle overlay (premultiplied BGRA at the frame's
// size, may be null or empty) composited source-over, the CPU-BGRA/libass
// reference the presenter is checked against (I2): each channel
// overlay + (frame * (255 - alpha) + 127) / 255. Legacy GetFrameWithSubs
// drew the subtitles into a copy of the frame (RendererFFMS2.cpp:676-701);
// SaveFrame then copied B, G, R of each pixel (RendererVideo.cpp:1218-1223).
SnapshotImage snapshotImage(const IndexedFrame &frame, const OverlayFrame *overlay);

// The number SaveFrame gives the next PNG: of the files of the video's
// folder matching "<name without extension>_*_*.png" (`paths`, full paths
// as listed), each whose name continues the video's name with
// "_<digits>_" holds that number; the smallest number from 1 not held.
// Legacy searched each whole path for the first "_<digits>_" (wxRegEx
// "_([0-9]+)_", RendererVideo.cpp:1238-1249), so a folder or video name
// holding one numbered every file alike and a save overwrote an earlier
// one; approved departure V4-snapshot-number reads the file name only.
int nextSnapshotNumber(std::string_view videoPath, const std::vector<std::string> &paths);

// The PNG SaveFrame writes: snapshotPath with nextSnapshotNumber, the
// number raised while `exists` reports that path taken, so a save never
// replaces a file (V4-snapshot-number).
std::string nextSnapshotPath(std::string_view videoPath, const std::vector<std::string> &paths, int ms,
                             const std::function<bool(const std::string &)> &exists);

// The PNG's path: the video's path up to its last '.', then
// "_<number>_<time>.png", the time the frame's start as SubsTime::raw(SRT)
// ("hh:mm:ss,mmm") with ':' made ';'.
std::string snapshotPath(std::string_view videoPath, int number, int ms);

// The listing's wildcard (HikariPathName(path).BeforeLast('.') << "_*_*.png")
// and folder (HikariPathDir).
std::string snapshotPattern(std::string_view videoPath);

} // namespace hikari::application
