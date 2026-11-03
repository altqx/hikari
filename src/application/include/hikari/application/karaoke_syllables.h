#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hikari::application {

// As aegisub.parse_karaoke_data gives it: times in ms from the Line's start.
struct KaraokeSyllable {
    int duration = 0, start = 0, end = 0;
    std::string tag;
    std::string text;     // with its override blocks
    std::string stripped; // without them
};

// Index 0 is an empty syllable; without a karaoke tag, index 1 is the whole Line as one "k"
// syllable timed from `startMs` to `endMs`.
std::vector<KaraokeSyllable> karaokeSyllables(std::string_view text, std::int64_t startMs, std::int64_t endMs);

}
