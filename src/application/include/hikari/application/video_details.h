#pragma once

#include "hikari/application/indexed_source.h"

#include <cstdint>
#include <optional>
#include <string>

namespace hikari::application {

class VideoSession;

// Reduced; {0, 1}: unknown.
struct Ratio {
    std::int64_t num = 0, den = 1;
    bool operator==(const Ratio &) const = default;
};

// What Video > Video details shows.
struct VideoDetails {
    std::string path;
    int width = 0, height = 0;
    Ratio aspect; // display aspect
    Ratio fps;    // nominal
    // Frame durations differ by more than a millisecond.
    bool variableRate = false;
    int frames = 0;
    int lastFrameMs = 0; // the last frame's start
    PixelFormat pixelFormat;
    // The source tags its matrix; else `matrix` is the converter's guess.
    bool tagged = false;
    std::string matrix; // "TV.709"
    // The Document's YCbCr Matrix when it is not `matrix`, else empty.
    std::string scriptMatrix;
    bool operator==(const VideoDetails &) const = default;
};

// nullopt without video.
std::optional<VideoDetails> videoDetails(const VideoSession &session);

// "23.976", "25": three decimals at most, trailing zeros dropped.
std::string rateDecimal(Ratio fps);
// The decimal is rounded.
bool rateIsRounded(Ratio fps);
// "4:2:0", "4:2:2", "4:4:4", "4:1:1", "4:1:0", "4:4:0"; empty otherwise.
std::string chromaSubsampling(const PixelFormat &format);
// "0:23:58.97", as the subtitles write times.
std::string lengthText(int ms);

}
