#pragma once

// F6: keyframe files (legacy GLOBAL_OPEN_KEYFRAMES, KeyframeLoader at 20d647c4).

#include <string_view>
#include <vector>

namespace hikari::application {

// The keyframe frame numbers a file lists, empty for an unknown format
// ("Invalid keyframes format"). Formats by their first line:
// "# keyframe format v1" (Aegisub: one frame number per line; legacy also
// reads the "fps ..." line, as frame 0), XviD/ffmpeg/avconv 2-pass stats
// (lines starting with i/p/b), x264 stats ("type:" I/P/B per frame) and DivX
// "##map version" (legacy looks for the text "IPB" in each line, so it finds
// no frame type and gives no keyframes; kept).
std::vector<int> parseKeyframes(std::string_view text);

} // namespace hikari::application
