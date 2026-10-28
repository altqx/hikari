#include "hikari/application/video_controls.h"

#include <cmath>
#include <cstdio>

namespace hikari::application {

std::optional<int> videoVolumeKeyStep(int volume, bool up)
{
    // OnSPlus: pos < 1; OnSMinus: pos > -91.
    const int pos = volume + (up ? 2 : -2);
    if (up ? pos < 1 : pos > -91)
        return pos;
    return std::nullopt;
}

std::optional<int> videoVolumeWheelStep(int volume, int steps)
{
    int pos = volume + steps * 3;
    if (pos + 3 > 0)
        pos = 0;
    if (pos - 3 < -86)
        pos = -86;
    if (pos > 0 || pos < -86)
        return std::nullopt;
    return pos;
}

double videoVolumeGain(int volume)
{
    const double db = -static_cast<double>(volume * volume) / 100.0;
    return std::pow(10.0, db / 20.0);
}

int aspectSliderValue(float aspectRatio)
{
    return static_cast<int>(aspectRatio * 700000);
}

float aspectFromSlider(int value)
{
    return value / 700000.0f;
}

std::string aspectLabelNumber(float aspectRatio)
{
    char text[64];
    std::snprintf(text, sizeof text, "%5.3f", 1.f / aspectRatio);
    return text;
}

float wheelZoomPercent(float zoomPercent, int steps)
{
    // config.h MID(a,b,c): MAX(a, MIN(b, c)).
    const float step = static_cast<float>(steps);
    const float wanted = zoomPercent + (step / 10.f);
    const float lowest = wanted < 10.f ? wanted : 10.f;
    return 1.f > lowest ? 1.f : lowest;
}

} // namespace hikari::application
