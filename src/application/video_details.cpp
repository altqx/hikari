#include "hikari/application/video_details.h"

#include "hikari/application/video_session.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace hikari::application {

namespace {

Ratio reduced(std::int64_t num, std::int64_t den)
{
    if (num <= 0 || den <= 0)
        return {};
    const std::int64_t g = std::gcd(num, den);
    return {num / g, den / g};
}

bool variable(const VideoSession &session)
{
    std::int64_t shortest = 0, longest = 0;
    for (int i = 1; i < session.frameCount(); i++) {
        const auto a = session.frameStart(i - 1), b = session.frameStart(i);
        if (!a || !b)
            return false;
        const std::int64_t us = b->microseconds() - a->microseconds();
        shortest = i == 1 ? us : std::min(shortest, us);
        longest = i == 1 ? us : std::max(longest, us);
    }
    return longest - shortest > 1000;
}

}

std::optional<VideoDetails> videoDetails(const VideoSession &session)
{
    if (session.state() != VideoSession::State::Ready)
        return std::nullopt;
    VideoDetails d;
    d.path = session.path();
    const auto geometry = session.sourceGeometry();
    d.width = geometry.width;
    d.height = geometry.height;
    const std::int64_t shown = geometry.sarNum > 0 && geometry.sarDen > 0
                                   ? std::int64_t(geometry.width) * geometry.sarNum / geometry.sarDen
                                   : geometry.width;
    d.aspect = reduced(shown, geometry.height);
    const auto [num, den] = session.nominalFps();
    d.fps = reduced(num, den);
    d.variableRate = variable(session);
    d.frames = session.frameCount();
    d.lastFrameMs = session.durationMs();
    d.pixelFormat = session.pixelFormat();
    const auto &colour = session.colourMatrix();
    d.tagged = colour.sourceTagged();
    d.matrix = colour.source();
    if (!session.matrix().empty() && session.matrix() != d.matrix)
        d.scriptMatrix = session.matrix();
    return d;
}

std::string rateDecimal(Ratio fps)
{
    if (fps.num <= 0 || fps.den <= 0)
        return {};
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", double(fps.num) / double(fps.den));
    std::string s = text;
    while (s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    return s;
}

bool rateIsRounded(Ratio fps)
{
    return fps.num > 0 && fps.den > 0 && (fps.num * 1000) % fps.den != 0;
}

std::string chromaSubsampling(const PixelFormat &format)
{
    if (format.rgb || format.components < 3)
        return {};
    switch (format.chromaShiftX * 10 + format.chromaShiftY) {
    case 0: return "4:4:4";
    case 1: return "4:4:0";
    case 10: return "4:2:2";
    case 11: return "4:2:0";
    case 20: return "4:1:1";
    case 22: return "4:1:0";
    default: return {};
    }
}

std::string lengthText(int ms)
{
    ms = std::max(ms, 0);
    char text[32];
    std::snprintf(text, sizeof text, "%d:%02d:%02d.%02d", ms / 3'600'000, ms / 60'000 % 60, ms / 1000 % 60,
                  ms % 1000 / 10);
    return text;
}

}
