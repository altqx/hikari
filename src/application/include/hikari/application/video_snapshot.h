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

// The number SaveFrame gives the next PNG: every file of the video's folder
// matching "<name without extension>_*_*.png" (wxDir::GetAllFiles, files
// only) is searched, as its whole path, for the first "_<digits>_"
// (wxRegEx "_([0-9]+)_"); the smallest number from 1 not among them.
// `paths` are those files' full paths as legacy listed them.
int nextSnapshotNumber(const std::vector<std::string> &paths);

// The PNG's path: the video's path up to its last '.', then
// "_<number>_<time>.png", the time the frame's start as SubsTime::raw(SRT)
// ("hh:mm:ss,mmm") with ':' made ';'.
std::string snapshotPath(std::string_view videoPath, int number, int ms);

// The listing's wildcard (HikariPathName(path).BeforeLast('.') << "_*_*.png")
// and folder (HikariPathDir).
std::string snapshotPattern(std::string_view videoPath);

} // namespace hikari::application
