#pragma once

// Y7: the colour picker's colour spaces (legacy colorspace.cpp at 20d647c4,
// from Aegisub): HSL and HSV on 0-255 channels (hue 0-255 for a full turn),
// with the legacy integer and float steps, special cases and clipping, and
// the HTML text of the picker's "HTML:" field. Every value is 0-255.

#include <string>
#include <string_view>

namespace hikari::core::legacy {

struct Channels {
    int a = 0; // red, or hue
    int b = 0; // green, or saturation
    int c = 0; // blue, or lightness / value
    bool operator==(const Channels &) const = default;
};

// clip_colorval (colorspace.h)
int clipColourValue(int value);

// hsl_to_rgb (colorspace.cpp:44-142)
Channels hslToRgb(int h, int s, int l);
// hsv_to_rgb (colorspace.cpp:147-246)
Channels hsvToRgb(int h, int s, int v);
// rgb_to_hsl (colorspace.cpp:253-290)
Channels rgbToHsl(int r, int g, int b);
// rgb_to_hsv (colorspace.cpp:294-329)
Channels rgbToHsv(int r, int g, int b);
// hsv_to_hsl (colorspace.cpp:332-346); the outputs are unsigned char, so
// a value past 255 wraps as legacy's did.
Channels hsvToHsl(int h, int s, int v);
// hsl_to_hsv (colorspace.cpp:349-367), unsigned char outputs as above.
Channels hslToHsv(int h, int s, int l);

// color_to_html (colorspace.cpp:370-373): "#RRGGBB".
std::u16string colourToHtml(int r, int g, int b);
// html_to_color (colorspace.cpp:376-415): ASCII spaces trimmed, one leading
// "#" dropped, then six or three hex digits (each pair or digit read as
// wxString::ToLong(16) does: strtol over the whole text); anything else is
// black. A negative or wide value wraps to its low byte (wxColour channels).
Channels htmlToColour(std::u16string_view html);

} // namespace hikari::core::legacy
