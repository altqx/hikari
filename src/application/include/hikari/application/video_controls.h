#pragma once

#include <optional>
#include <string>

namespace hikari::application {

// Volume runs -86 (slider floor) to 0. Steps by 2 down to -90; nullopt when the step is refused.
std::optional<int> videoVolumeKeyStep(int volume, bool up);
// Three per wheel step, snapped to 0 or -86 within three of either end.
std::optional<int> videoVolumeWheelStep(int volume, int steps);
// 10^(-(volume * volume) / 100 / 20): the volume squared is hundredths of a dB.
double videoVolumeGain(int volume);

// `aspectRatio` is height / width; the slider value is it * 700000, truncated. The label number
// is 1 / aspectRatio as "%5.3f".
int aspectSliderValue(float aspectRatio);
float aspectFromSlider(int value);
std::string aspectLabelNumber(float aspectRatio);

// Clamped to [1, 10].
float wheelZoomPercent(float zoomPercent, int steps);

}
