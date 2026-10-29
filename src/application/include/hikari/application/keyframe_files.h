#pragma once

#include <string_view>
#include <vector>

namespace hikari::application {

// Empty for an unknown format. Reads Aegisub's "# keyframe format v1" (its "fps" line counts as
// frame 0), XviD/ffmpeg/avconv 2-pass stats, x264 stats and DivX "##map version" (no keyframes).
std::vector<int> parseKeyframes(std::string_view text);

}
