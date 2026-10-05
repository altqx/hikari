#pragma once

// V4: the Video panel's volume and aspect ratio arithmetic, as legacy
// VideoBox and its VolSlider did at 20d647c4.
//
// The volume is legacy's video volume (VIDEO_VOLUME, video.volume): the
// VolSlider beside the times field, from -86 to 0, which the player's own
// output takes as -(pos * pos) hundredths of a dB (VideoBox::OnVolume,
// VideoBox.cpp:912-919; RendererGStreamer::SetVolumeInternal converts that
// to a linear factor, RendererGStreamer.cpp:635-645). The rewrite's general
// player owns playback audio as legacy's DirectShow/GStreamer renderer did
// (docs/qt/media.md), so it takes this volume.

#include <optional>
#include <string>

namespace hikari::application {

// VIDEO_VOLUME_PLUS / VIDEO_VOLUME_MINUS (VideoBox::OnSPlus / OnSMinus,
// VideoBox.cpp:1181-1233): two steps; the new value is kept only while it
// stays below 1 (up) or above -91 (down), so the keys reach -90, below the
// slider's -86. Nothing when the step is refused.
std::optional<int> videoVolumeKeyStep(int volume, bool up);
// The wheel over the panel (VolSlider::OnMouseEvent, VideoSlider.cpp:370-382):
// three per wheel step, snapped to 0 or -86 within three of either end.
std::optional<int> videoVolumeWheelStep(int volume, int steps);
// The player's linear gain for a volume: 10^(-(pos * pos) / 100 / 20).
double videoVolumeGain(int volume);

// AspectRatioDialog (VideoBox.cpp:106-125): a slider from 100000 to
// 1000000, inverted, at m_AspectRatio * 700000 (truncated); a value gives
// the aspect ratio value / 700000 (height / width); the label reads
// "Aspect ratio: %5.3f" of 1 / that ratio.
int aspectSliderValue(float aspectRatio);
float aspectFromSlider(int value);
std::string aspectLabelNumber(float aspectRatio);

// The wheel's zoom over the video (VideoBox::OnMouseEvent, VideoBox.cpp:510-516):
// MID(1, zoom + steps / 10, 10) in floats.
float wheelZoomPercent(float zoomPercent, int steps);

} // namespace hikari::application
