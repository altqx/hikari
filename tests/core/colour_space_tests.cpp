// Y7: the colour picker's conversions against the legacy source itself.
// HikariSub/colorspace.cpp and colorspace.h (20d647c4) are copied unchanged
// into the build and compiled here against tests/core/legacy-colorspace (the
// MAX/MIN macros of config.h as written, and the parts of wxColour and
// wxString they use), so each port is compared with what legacy computes:
// every 0-255 input of the six HSL/HSV conversions (the picker's NumCtrl
// range), color_to_html, and html_to_color over generated field texts.

#include "hikari/core/colour_space.h"
#include "hikari/core/editor_font_colour.h"

#include "colorspace.h" // the legacy header, copied into the build

#include <gtest/gtest.h>

#include <random>
#include <string>

using namespace hikari::core::legacy;

namespace {

using LegacyConversion = void (*)(int, int, int, unsigned char *, unsigned char *, unsigned char *);

// The first input where the port differs from legacy, or an empty text.
std::string firstDifference(LegacyConversion legacy, Channels (*port)(int, int, int))
{
    for (int x = 0; x < 256; ++x)
        for (int y = 0; y < 256; ++y)
            for (int z = 0; z < 256; ++z) {
                unsigned char a, b, c;
                legacy(x, y, z, &a, &b, &c);
                const Channels mine = port(x, y, z);
                if (mine != Channels{a, b, c})
                    return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ": legacy " +
                           std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + ", port " +
                           std::to_string(mine.a) + "," + std::to_string(mine.b) + "," + std::to_string(mine.c);
            }
    return {};
}

Channels legacyHtml(const std::u16string &text)
{
    const wxColour c = html_to_color(wxString(std::wstring(text.begin(), text.end())));
    return {c.Red(), c.Green(), c.Blue()};
}

} // namespace

TEST(LegacyColourSpace, HslToRgbForEveryInput)
{
    EXPECT_EQ(firstDifference(hsl_to_rgb, hslToRgb), "");
}

TEST(LegacyColourSpace, HsvToRgbForEveryInput)
{
    EXPECT_EQ(firstDifference(hsv_to_rgb, hsvToRgb), "");
}

TEST(LegacyColourSpace, RgbToHslForEveryInput)
{
    EXPECT_EQ(firstDifference(rgb_to_hsl, rgbToHsl), "");
}

TEST(LegacyColourSpace, RgbToHsvForEveryInput)
{
    EXPECT_EQ(firstDifference(rgb_to_hsv, rgbToHsv), "");
}

TEST(LegacyColourSpace, HsvToHslForEveryInput)
{
    EXPECT_EQ(firstDifference(hsv_to_hsl, hsvToHsl), "");
}

TEST(LegacyColourSpace, HslToHsvForEveryInput)
{
    EXPECT_EQ(firstDifference(hsl_to_hsv, hslToHsv), "");
}

// Fixed values, readable without the legacy build: the special cases
// (colorspace.cpp:53-87, 152-186), the hue scale of 256 per turn, and
// rgb_to_hsv's saturation 255 for black (colorspace.cpp:303-304).
TEST(LegacyColourSpace, Fixtures)
{
    EXPECT_EQ(hslToRgb(171, 255, 128), (Channels{0, 0, 255}));
    EXPECT_EQ(hslToRgb(10, 0, 77), (Channels{77, 77, 77}));
    EXPECT_EQ(hsvToRgb(255, 255, 200), (Channels{200, 0, 0}));
    EXPECT_EQ(hsvToRgb(43, 255, 255), (Channels{255, 255, 0}));
    EXPECT_EQ(rgbToHsv(0, 0, 0), (Channels{0, 255, 0}));
    EXPECT_EQ(rgbToHsl(0, 0, 255), (Channels{170, 255, 127}));
    EXPECT_EQ(rgbToHsv(0, 0, 255), (Channels{170, 255, 255}));
    EXPECT_EQ(rgbToHsv(255, 128, 0), (Channels{21, 255, 255}));
    EXPECT_EQ(hsvToHsl(170, 255, 255), (Channels{170, 255, 127}));
    EXPECT_EQ(hslToHsv(170, 255, 127), (Channels{170, 255, 254}));
    EXPECT_EQ(colourToHtml(255, 128, 0), u"#FF8000");
    EXPECT_EQ(htmlToColour(u" #ff8000\t"), (Channels{255, 128, 0}));
    EXPECT_EQ(htmlToColour(u"#f80"), (Channels{255, 136, 0}));
    EXPECT_EQ(htmlToColour(u"#ff800"), (Channels{}));     // five digits: black
    EXPECT_EQ(htmlToColour(u"#-1+f00"), (Channels{255, 15, 0})); // wcstol signs, wrapped by wxColour
    EXPECT_EQ(htmlToColour(u"0x0000"), (Channels{}));     // "0x" alone is not read whole
    // The ASS field shows AssColor::GetAss(false, false) (ColorPicker.cpp:788).
    EXPECT_EQ(assColourText({255, 128, 0, 0x80}, false, false), u"&H0080FF&");
}

TEST(LegacyColourSpace, ColourToHtmlForEveryChannel)
{
    for (int v = 0; v < 256; ++v) {
        const wxString legacy = color_to_html(wxColour(v, 255 - v, v / 3));
        const std::u16string mine = colourToHtml(v, 255 - v, v / 3);
        ASSERT_EQ(std::wstring(mine.begin(), mine.end()), legacy.str()) << v;
    }
}

// html_to_color over every three-character text and a sample of six- and
// other-length texts built from digits, letters, signs, spaces and "#", with
// and without surrounding spaces.
TEST(LegacyColourSpace, HtmlToColourForGeneratedTexts)
{
    const std::u16string alphabet = u"0179aAfFgx+- \t#";
    std::u16string text(3, u' ');
    for (char16_t a : alphabet)
        for (char16_t b : alphabet)
            for (char16_t c : alphabet) {
                text = {a, b, c};
                for (const std::u16string &t : {text, u"#" + text, u" " + text + u"\n", u"##" + text})
                    ASSERT_EQ(htmlToColour(t), legacyHtml(t)) << std::string(t.begin(), t.end());
            }
    std::mt19937 random(190);
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
    std::uniform_int_distribution<int> length(0, 9);
    for (int i = 0; i < 200000; ++i) {
        const int n = i % 2 ? 6 : length(random);
        std::u16string t;
        for (int k = 0; k < n; ++k)
            t += alphabet[pick(random)];
        if (i % 3 == 0)
            t = u"#" + t;
        ASSERT_EQ(htmlToColour(t), legacyHtml(t)) << std::string(t.begin(), t.end());
    }
}
