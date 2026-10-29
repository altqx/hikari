#pragma once

// HSL and HSV on 0-255 channels; a hue of 0-255 is a full turn.

#include <string>
#include <string_view>

namespace hikari::core::legacy {

struct Channels {
    int a = 0; // red, or hue
    int b = 0; // green, or saturation
    int c = 0; // blue, or lightness / value
    bool operator==(const Channels &) const = default;
};

int clipColourValue(int value);

Channels hslToRgb(int h, int s, int l);
Channels hsvToRgb(int h, int s, int v);
Channels rgbToHsl(int r, int g, int b);
Channels rgbToHsv(int r, int g, int b);
// Outputs past 255 wrap to their low byte.
Channels hsvToHsl(int h, int s, int v);
// Outputs past 255 wrap to their low byte.
Channels hslToHsv(int h, int s, int l);

// "#RRGGBB".
std::u16string colourToHtml(int r, int g, int b);
// ASCII spaces trimmed and one leading "#" dropped, then six or three hex digits; anything else
// is black. A negative or wide value wraps to its low byte.
Channels htmlToColour(std::u16string_view html);

} // namespace hikari::core::legacy
