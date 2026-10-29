#include "hikari/core/colour_space.h"
#include "hikari/core/editor_font_colour.h"

#include <gtest/gtest.h>

using namespace hikari::core::legacy;

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
    EXPECT_EQ(htmlToColour(u"#ff800"), (Channels{}));
    EXPECT_EQ(htmlToColour(u"#-1+f00"), (Channels{255, 15, 0})); // wcstol signs, wrapped by wxColour
    EXPECT_EQ(htmlToColour(u"0x0000"), (Channels{}));
    EXPECT_EQ(assColourText({255, 128, 0, 0x80}, false, false), u"&H0080FF&");
}
