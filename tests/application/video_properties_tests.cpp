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
