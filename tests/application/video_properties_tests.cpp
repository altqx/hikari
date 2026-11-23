#include "hikari/application/matrix_conversion.h"
#include "hikari/application/video_matrix.h"
#include "hikari/application/video_properties.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

using namespace hikari;
using namespace hikari::application;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

core::Document script(std::string_view info)
{
    return load(std::string("[Script Info]\nScriptType: v4.00+\n") + std::string(info) +
                "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n");
}

}

TEST(VideoProperties, RecommendedMatrixAsAegisub35)
{
    EXPECT_EQ(recommendedMatrix({1, 1}, 1920, 1080), "TV.709");
    EXPECT_EQ(recommendedMatrix({1, 2}, 1920, 1080), "PC.709");
    EXPECT_EQ(recommendedMatrix({6, 0}, 640, 480), "TV.601");
    EXPECT_EQ(recommendedMatrix({5, 2}, 640, 480), "PC.601");
    EXPECT_EQ(recommendedMatrix({2, 0}, 1280, 720), "TV.709");
    EXPECT_EQ(recommendedMatrix({2, 0}, 1024, 600), "TV.709");
    EXPECT_EQ(recommendedMatrix({2, 0}, 1024, 599), "TV.601");
    EXPECT_EQ(recommendedMatrix({9, 1}, 3840, 2160), "None");
    EXPECT_EQ(recommendedMatrix({4, 1}, 640, 480), "None");
    EXPECT_EQ(recommendedMatrix({0, 2}, 640, 480), "None");
}

TEST(VideoProperties, UntaggedAndHdrVideo)
{
    EXPECT_TRUE(untaggedMatrix({2, 0}));
    EXPECT_FALSE(untaggedMatrix({1, 0}));
    EXPECT_FALSE(hdrOrWideGamut({1, 1, 1, 1}));
    EXPECT_TRUE(hdrOrWideGamut({9, 1, 1, 16}));
    EXPECT_TRUE(hdrOrWideGamut({9, 1, 1, 18}));
    for (const int primaries : {9, 11, 12})
        EXPECT_TRUE(hdrOrWideGamut({1, 1, primaries, 1})) << primaries;
    EXPECT_FALSE(hdrOrWideGamut({9, 1, 2, 2}));
}

TEST(VideoProperties, MatrixQuestions)
{
    EXPECT_EQ(checkMatrix(script(""), "TV.709").question, MatrixQuestion::Missing);
    EXPECT_EQ(checkMatrix(script("YCbCr Matrix: TV.2020\n"), "TV.709").question, MatrixQuestion::Missing);
    EXPECT_EQ(checkMatrix(script("YCbCr Matrix: None\n"), "TV.709").question, MatrixQuestion::None);
    EXPECT_EQ(checkMatrix(script("YCbCr Matrix: TV.709\n"), "TV.709").question, MatrixQuestion::None);
    const auto mismatch = checkMatrix(script("YCbCr Matrix: TV.601\n"), "TV.709");
    EXPECT_EQ(mismatch.question, MatrixQuestion::Mismatch);
    EXPECT_EQ(mismatch.script, "TV.601");
    EXPECT_EQ(mismatch.recommended, "TV.709");
    EXPECT_TRUE(mismatch.convertible);
    EXPECT_FALSE(checkMatrix(script("YCbCr Matrix: TV.601\n"), "None").convertible);
}

TEST(VideoProperties, AFileWithoutAMatrixLeftAloneStillReadsAsTv601)
{
    EditSession session(script(""));
    const auto steps = session.historySize();
    ASSERT_EQ(checkMatrix(session.document(), "TV.709").question, MatrixQuestion::Missing);
    EXPECT_EQ(sessionVideoMatrix(session), "TV.601");
    EXPECT_EQ(session.historySize(), steps);
}

TEST(VideoProperties, SettingTheMatrixIsOneStep)
{
    EditSession session(script(""));
    const auto steps = session.historySize();
    ASSERT_TRUE(setMatrix(session, "TV.709"));
    EXPECT_EQ(session.document().scriptInfo(u8"YCbCr Matrix"), u8"TV.709");
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, kMatrixStep);
    EXPECT_EQ(sessionVideoMatrix(session), "TV.709");
    ASSERT_TRUE(setMatrix(session, "TV.709"));
    EXPECT_EQ(session.historySize(), steps + 1);
}

TEST(VideoProperties, LayoutResolutionQuestions)
{
    const auto missing = checkLayoutResolution(script("PlayResX: 640\nPlayResY: 360\n"), {1920, 1080},
                                               LayoutResMismatch::OnAspectChange);
    EXPECT_EQ(missing.question, LayoutQuestion::Missing);
    EXPECT_EQ(missing.play, (Resolution{640, 360}));
    const auto layout = script("PlayResX: 640\nPlayResY: 360\nLayoutResX: 1280\nLayoutResY: 720\n");
    EXPECT_EQ(checkLayoutResolution(layout, {1280, 720}, LayoutResMismatch::Always).question, LayoutQuestion::None);
    EXPECT_EQ(checkLayoutResolution(layout, {1920, 1080}, LayoutResMismatch::OnAspectChange).question,
              LayoutQuestion::None);
    EXPECT_EQ(checkLayoutResolution(layout, {1920, 1080}, LayoutResMismatch::Always).question,
              LayoutQuestion::Mismatch);
    const auto wide = checkLayoutResolution(layout, {1440, 1080}, LayoutResMismatch::OnAspectChange);
    EXPECT_EQ(wide.question, LayoutQuestion::Mismatch);
    EXPECT_TRUE(wide.aspectChanged);
    EXPECT_EQ(wide.layout, (Resolution{1280, 720}));
    EXPECT_EQ(checkLayoutResolution(layout, {1440, 1080}, LayoutResMismatch::Never).question, LayoutQuestion::None);
}

TEST(VideoProperties, SettingTheLayoutResolutionIsOneStep)
{
    EditSession session(script("PlayResX: 640\nPlayResY: 360\n"));
    const auto steps = session.historySize();
    ASSERT_TRUE(setLayoutResolution(session, {1920, 1080}));
    EXPECT_EQ(session.document().scriptInfo(u8"LayoutResX"), u8"1920");
    EXPECT_EQ(session.document().scriptInfo(u8"LayoutResY"), u8"1080");
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, kLayoutResolutionStep);
    EXPECT_FALSE(setLayoutResolution(session, {0, 1080}));
}

TEST(VideoProperties, AspectRatioChangeIsOverOnePercent)
{
    EXPECT_FALSE(aspectRatioChanged({1920, 1080}, {1280, 720}));
    EXPECT_FALSE(aspectRatioChanged({1920, 1090}, {1920, 1080}));
    EXPECT_TRUE(aspectRatioChanged({1920, 1100}, {1920, 1080}));
    EXPECT_TRUE(aspectRatioChanged({640, 0}, {640, 480}));
}

TEST(MatrixConversion, NeedsTwoDifferentMatrices)
{
    EXPECT_TRUE(MatrixConversion::between("TV.601", "TV.709"));
    EXPECT_TRUE(MatrixConversion::between("PC.FCC", "TV.240M"));
    EXPECT_FALSE(MatrixConversion::between("TV.709", "TV.709"));
    EXPECT_FALSE(MatrixConversion::between("None", "TV.709"));
    EXPECT_FALSE(MatrixConversion::between("TV.601", "None"));
    EXPECT_FALSE(MatrixConversion::between("TV.2020", "TV.709"));
    EXPECT_FALSE(MatrixConversion::between("tv.601", "TV.709"));
}

TEST(MatrixConversion, KeepsTheLookUnderTheNewMatrix)
{
    struct Case {
        const char *from, *to;
        core::Colour in, out;
    };
    const Case cases[] = {
        {"TV.601", "TV.709", {255, 0, 0, 0}, {255, 25, 0, 0}},
        {"TV.601", "TV.709", {0, 255, 0, 0}, {0, 215, 0, 0}},
        {"TV.601", "TV.709", {0, 0, 255, 0}, {0, 15, 255, 0}},
        {"TV.601", "TV.709", {200, 120, 40, 0}, {208, 123, 36, 0}},
        {"TV.709", "TV.601", {200, 120, 40, 0}, {192, 117, 44, 0}},
        {"TV.709", "TV.601", {16, 128, 200, 0}, {26, 135, 196, 0}},
        {"TV.601", "PC.601", {255, 255, 255, 0}, {235, 235, 235, 0}},
        {"TV.601", "PC.601", {0, 0, 0, 0}, {16, 16, 16, 0}},
        {"TV.601", "PC.601", {16, 128, 200, 0}, {28, 126, 190, 0}},
        {"TV.709", "TV.601", {255, 255, 255, 0x80}, {255, 255, 255, 0x80}},
    };
    for (const auto &c : cases) {
        const auto conversion = MatrixConversion::between(c.from, c.to);
        ASSERT_TRUE(conversion);
        EXPECT_EQ(conversion->convert(c.in), c.out) << c.from << " to " << c.to << ": " << c.in.r << "," << c.in.g
                                                    << "," << c.in.b;
    }
}

namespace {

core::Document styled(std::string_view events)
{
    return load(std::string("[Script Info]\nScriptType: v4.00+\nYCbCr Matrix: TV.601\n\n[V4+ Styles]\n"
                            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                            "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                            "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                            "Style: Default,Arial,20,&H000000FF,&H0000FF00,&H00FF0000,&H80FFFFFF,0,0,0,0,100,100,0,"
                            "0,1,2,2,2,10,10,10,1\n\n[Events]\n"
                            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n") +
                std::string(events));
}

std::u8string textOf(const core::Document &document, std::size_t index)
{
    return document.lines()[index]->text;
}

}

TEST(MatrixConversion, ConvertsStyleColoursAndColourTags)
{
    auto document = styled("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,"
                           "{\\c&H0000ff&}a{\\1c&HFF&\\3c&H00FF00&\\t(0,500,\\4c&HFF0000&)\\clip(0,0,10,10)}b\\c&H0000FF&\n"
                           "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\c&H802878C8&\\c\\2c}c\n"
                           "Comment: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\c&H0000FF&}note\n"
                           "Comment: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,template line,{\\c&H0000FF&}k\n"
                           "Comment: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,code once,{\\c&H0000FF&}k\n");
    document.editLine(document.lines()[1]->id, [](core::LineRecord &l) { l.translation = u8"{\\3c&HFF0000&}t"; });
    convertColours(document, *MatrixConversion::between("TV.601", "TV.709"));

    const auto style = core::decodeStyles(document).front();
    EXPECT_EQ(style.primary, (core::Colour{255, 25, 0, 0}));
    EXPECT_EQ(style.secondary, (core::Colour{0, 215, 0, 0}));
    EXPECT_EQ(style.outline, (core::Colour{0, 15, 255, 0}));
    EXPECT_EQ(style.back, (core::Colour{255, 255, 255, 0x80}));
    EXPECT_EQ(textOf(document, 0),
              u8"{\\c&H0019FF&}a{\\1c&H0019FF&\\3c&H00D700&\\t(0,500,\\4c&HFF0F00&)\\clip(0,0,10,10)}b\\c&H0000FF&");
    EXPECT_EQ(textOf(document, 1), u8"{\\c&H80247BD0&\\c\\2c}c");
    EXPECT_EQ(document.lines()[1]->translation, u8"{\\3c&HFF0F00&}t");
    EXPECT_EQ(textOf(document, 2), u8"{\\c&H0019FF&}note");
    EXPECT_EQ(textOf(document, 3), u8"{\\c&H0000FF&}k");
    EXPECT_EQ(textOf(document, 4), u8"{\\c&H0000FF&}k");
}

TEST(VideoProperties, ConvertingTheMatrixIsOneStep)
{
    EditSession session(styled("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\c&H0000FF&}a\n"));
    const auto steps = session.historySize();
    ASSERT_TRUE(convertMatrix(session, "TV.709"));
    EXPECT_EQ(session.document().scriptInfo(u8"YCbCr Matrix"), u8"TV.709");
    EXPECT_EQ(session.document().lines().front()->text, u8"{\\c&H0019FF&}a");
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, kMatrixConversionStep);
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().scriptInfo(u8"YCbCr Matrix"), u8"TV.601");
    EXPECT_EQ(session.document().lines().front()->text, u8"{\\c&H0000FF&}a");
    EXPECT_EQ(core::decodeStyles(session.document()).front().primary, (core::Colour{255, 0, 0, 0}));

    EXPECT_FALSE(convertMatrix(session, "TV.601"));
    EXPECT_FALSE(convertMatrix(session, "None"));
    EditSession unset(script(""));
    EXPECT_FALSE(convertMatrix(unset, "TV.709"));
    EditSession none(script("YCbCr Matrix: None\n"));
    EXPECT_FALSE(convertMatrix(none, "TV.709"));
}
