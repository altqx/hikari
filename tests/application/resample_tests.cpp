// Y4: resampling against legacy SubsGrid::ResizeSubs, GetASSRes and the
// resample dialogs' OK at 20d647c4.

#include "hikari/application/resample.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/style.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return core::loadAss(b).document;
}

std::string saved(const core::Document &d)
{
    const auto bytes = core::encodeAss(d);
    return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string text(const core::Document &d, std::size_t row)
{
    const auto &t = d.lines()[row]->text;
    return std::string(t.begin(), t.end());
}

constexpr std::string_view kScript =
    "[Script Info]\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,1.5,0,1,2,1,2,10,15,10,1\n\n"
    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(100,50)\\bord2\\fs20\\clip(m 0 0 l 10 0 10 10)}a{\\p1}m 0 0 l 4 4{\\p0}\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,10,0,7,,{\\fscx100\\xbord2\\yshad2\\move(10, 20,30,40,0,500)\\clip(1,2,3,4)}b\n"
    "Comment: 0,0:00:01.00,0:00:02.00,Default,,10,0,0,,{\\pos(100,50)}c\n";

} // namespace

TEST(Resample, ScalesStylesTagsDrawingsAndMargins)
{
    auto d = load(kScript);
    resizeSubtitles(d, 2.f, 2.f, false);
    const auto style = core::decodeStyles(d).front();
    EXPECT_EQ(style.fontsize, u8"40");
    EXPECT_EQ(style.outlineWidth, u8"4");
    EXPECT_EQ(style.shadow, u8"2");
    EXPECT_EQ(style.spacing, u8"3");
    EXPECT_EQ(style.marginLeft, u8"20");
    EXPECT_EQ(style.marginRight, u8"30");
    EXPECT_EQ(style.marginVertical, u8"20");
    EXPECT_EQ(style.scaleX, u8"100");
    EXPECT_EQ(text(d, 0), "{\\pos(200,100)\\bord4\\fs40\\clip(m 0 0 l 20 0 20 20)}a{\\p1}m 0 0 l 8 8{\\p0}");
    // Tokens are trimmed; times after the four coordinates stay.
    EXPECT_EQ(text(d, 1), "{\\fscx100\\xbord4\\yshad4\\move(20,40,60,80,0,500)\\clip(2,4,6,8)}b");
    EXPECT_EQ(d.lines()[1]->marginLeft.value, 20);
    EXPECT_EQ(d.lines()[1]->marginVertical.value, 14);
    EXPECT_EQ(text(d, 2), "{\\pos(100,50)}c"); // comments stay
    EXPECT_EQ(d.lines()[2]->marginLeft.value, 10);
    // The Style is written as legacy Styles::GetRaw writes it, in place.
    const auto file = saved(d);
    EXPECT_NE(file.find("Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,3,0,1,4,2,2,20,30,20,1\n"),
              std::string::npos)
        << file;
}

TEST(Resample, StretchScalesWidthThroughFscxAndFontSizeByTheLargerFactor)
{
    auto d = load(kScript);
    resizeSubtitles(d, 2.f, 1.f, true);
    const auto style = core::decodeStyles(d).front();
    EXPECT_EQ(style.fontsize, u8"20"); // the smaller factor for Styles
    EXPECT_EQ(style.scaleX, u8"200");
    EXPECT_EQ(style.outlineWidth, u8"2");
    EXPECT_EQ(style.marginLeft, u8"20");
    EXPECT_EQ(style.marginVertical, u8"10");
    // \fs takes the larger factor (legacy val1); drawings keep their width
    // (the stretch goes through \fscx).
    EXPECT_EQ(text(d, 0), "{\\pos(200,50)\\bord2\\fs40\\clip(m 0 0 l 20 0 20 10)}a{\\p1}m 0 0 l 4 4{\\p0}");
    EXPECT_EQ(text(d, 1), "{\\fscx200\\xbord4\\yshad2\\move(20,20,60,40,0,500)\\clip(2,2,6,4)}b");
    // Without stretch the aspect change leaves \fscx and Style ScaleX.
    auto plain = load(kScript);
    resizeSubtitles(plain, 2.f, 1.f, false);
    EXPECT_EQ(core::decodeStyles(plain).front().scaleX, u8"100");
    EXPECT_EQ(text(plain, 1).substr(0, 10), "{\\fscx100\\");
}

TEST(Resample, UnscalableValuesAreKeptAndReported)
{
    auto d = load(std::string(kScript.substr(0, kScript.find("Dialogue:"))) +
                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(a,5)\\clip(m 1 n 2 3)}x\n");
    std::vector<std::string> warnings;
    resizeSubtitles(d, 2.f, 2.f, false, [&](int row, const std::u16string &value, const std::u16string &tag) {
        warnings.push_back(std::to_string(row) + ":" + std::string(value.begin(), value.end()) + ":" +
                           std::string(tag.begin(), tag.end()));
    });
    EXPECT_EQ(text(d, 0), "{\\pos(a,10)\\clip(m 2 n 4 6)}x");
    EXPECT_EQ(warnings, (std::vector<std::string>{"1:n:clip", "1:a:pos"}));
}

TEST(Resample, TheTranslationIsWhatChanges)
{
    auto d = load("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[Events]\n"
                  "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                  "Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,{\\pos(1,1)}o\n"
                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(1,1)}t\n");
    resizeSubtitles(d, 2.f, 2.f, false);
    EXPECT_EQ(text(d, 0), "{\\pos(1,1)}o");
    const auto &tl = d.lines()[0]->translation;
    EXPECT_EQ(std::string(tl.begin(), tl.end()), "{\\pos(2,2)}t");
}

TEST(Resample, ScriptResolutionDefaults)
{
    EXPECT_EQ(scriptResolution(load("[Script Info]\nTitle: x\n")), (Resolution{1280, 720}));
    EXPECT_EQ(scriptResolution(load("[Script Info]\nPlayResY: 480\n")), (Resolution{853, 480}));
    EXPECT_EQ(scriptResolution(load("[Script Info]\nPlayResX: 1920\n")), (Resolution{1920, 1080}));
}

TEST(Resample, ChangeResolutionIsOneStep)
{
    EditSession session{load(std::string(kScript).insert(std::string(kScript).find("\n\n"), "\nLayoutResX: 640\nLayoutResY: 360"))};
    ASSERT_TRUE(changeResolution(session, {640, 360}, {1280, 720}, true, false));
    EXPECT_EQ(session.history().back().name, "Changing subtitles resolution");
    const auto &d = session.document();
    EXPECT_EQ(d.scriptInfo(u8"PlayResX"), u8"1280");
    EXPECT_EQ(d.scriptInfo(u8"PlayResY"), u8"720");
    EXPECT_EQ(d.scriptInfo(u8"LayoutResX"), u8"1280");
    EXPECT_EQ(d.scriptInfo(u8"LayoutResY"), u8"720");
    EXPECT_EQ(text(d, 0).substr(0, 15), "{\\pos(200,100)\\");
    // Only the resolution ("Change only the subtitle resolution").
    EditSession only{load(kScript)};
    ASSERT_TRUE(changeResolution(only, {640, 360}, {1920, 1080}, false, false));
    EXPECT_EQ(only.document().scriptInfo(u8"PlayResX"), u8"1920");
    EXPECT_FALSE(only.document().scriptInfo(u8"LayoutResX"));
    EXPECT_EQ(text(only.document(), 0).substr(0, 14), "{\\pos(100,50)\\");
}
