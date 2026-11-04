#include "hikari/core/srv3.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using hikari::core::Srv3StyleOptions;

const std::filesystem::path kExport = HIKARI_EXPORT_FIXTURES;
const std::filesystem::path kSrv3 = kExport / "srv3";

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::u16string script(const std::filesystem::path &path)
{
    const std::string bytes = readFile(path);
    return hikari::core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size()));
}

std::string converted(const std::filesystem::path &input, const std::vector<Srv3StyleOptions> &options = {})
{
    const auto result = hikari::core::srv3FromAss(script(input), options);
    if (!result)
        return "refused: " + result.error();
    return {reinterpret_cast<const char *>(result->data()), result->size()};
}

std::string element(std::string_view block, std::string_view name)
{
    const std::string open = "<" + std::string(name) + ">", close = "</" + std::string(name) + ">";
    const auto start = block.find(open);
    if (start == std::string_view::npos)
        return {};
    const auto end = block.find(close, start);
    return std::string(block.substr(start + open.size(), end - start - open.size()));
}

std::optional<std::uint32_t> colour(const std::string &html)
{
    if (html.empty())
        return std::nullopt;
    return static_cast<std::uint32_t>(std::stoul(html.substr(1), nullptr, 16));
}

std::vector<Srv3StyleOptions> testOptions()
{
    const std::string xml = readFile(kSrv3 / "StyleOptions.xml");
    std::vector<Srv3StyleOptions> out;
    for (std::size_t at = 0; (at = xml.find("<Style>", at)) != std::string::npos;) {
        const std::size_t end = xml.find("</Style>", at);
        const std::string_view block(xml.data() + at, end - at);
        Srv3StyleOptions o;
        o.style = hikari::core::toUtf16(std::u8string_view(reinterpret_cast<const char8_t *>(element(block, "Name").data()), element(block, "Name").size()));
        const std::string shadows = element(block, "ShadowType");
        for (const auto &[word, bit] : {std::pair{"Glow", hikari::core::Srv3Glow}, std::pair{"HardShadow", hikari::core::Srv3HardShadow},
                                        std::pair{"SoftShadow", hikari::core::Srv3SoftShadow}, std::pair{"Bevel", hikari::core::Srv3Bevel}})
            if (shadows.find(word) != std::string::npos)
                o.shadows |= bit;
        o.karaoke = element(block, "IsKaraoke") == "true";
        o.currentWordText = colour(element(block, "CurrentWordTextColor"));
        o.currentWordOutline = colour(element(block, "CurrentWordOutlineColor"));
        o.currentWordShadow = colour(element(block, "CurrentWordShadowColor"));
        out.push_back(std::move(o));
        at = end;
    }
    return out;
}

std::vector<std::filesystem::path> inputs()
{
    std::vector<std::filesystem::path> out;
    for (const auto &entry : std::filesystem::directory_iterator(kSrv3 / "inputs"))
        out.push_back(entry.path());
    std::sort(out.begin(), out.end());
    return out;
}

TEST(Srv3, YtSubConverterSamplesAndTestsMatchItsOutputByteForByte)
{
    ASSERT_EQ(inputs().size(), 21u);
    for (const auto &input : inputs()) {
        SCOPED_TRACE(input.filename().string());
        EXPECT_EQ(converted(input), readFile(kSrv3 / "golden" / (input.stem().string() + ".ytt")));
    }
}

TEST(Srv3, StyleOptionsMatchYtSubConverterWithTheSameOptions)
{
    const auto options = testOptions();
    ASSERT_EQ(options.size(), 38u);
    int withOptions = 0;
    for (const auto &input : inputs()) {
        SCOPED_TRACE(input.filename().string());
        const auto golden = kSrv3 / "golden" / (input.stem().string() + ".options.ytt");
        const auto plain = kSrv3 / "golden" / (input.stem().string() + ".ytt");
        EXPECT_EQ(converted(input, options), readFile(std::filesystem::exists(golden) ? golden : plain));
        withOptions += std::filesystem::exists(golden);
    }
    EXPECT_EQ(withOptions, 5);
}

TEST(Srv3, HikariExportFixturesMatchYtSubConverter)
{
    for (const char *name : {"plain", "karaoke", "typesetting"}) {
        SCOPED_TRACE(name);
        EXPECT_EQ(converted(kExport / (std::string(name) + ".ass")), readFile(kSrv3 / "golden" / ("hikari-" + std::string(name) + ".ytt")));
    }
}

TEST(Srv3, OptionsForABuiltInStyleNameReplaceTheBuiltInOnes)
{
    const std::u16string ass = u"[Script Info]\nPlayResX: 1280\nPlayResY: 720\n\n[V4+ Styles]\n"
                               u"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
                               u"Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
                               u"MarginR, MarginV, Encoding\n"
                               u"Style: YTGlow,Roboto,38,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,2,2,10,10,10,1\n\n"
                               u"[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                               u"Dialogue: 0,0:00:01.00,0:00:03.00,YTGlow,,0,0,0,,Hello\n";
    const auto builtIn = hikari::core::srv3FromAss(ass);
    ASSERT_TRUE(builtIn);
    const std::string glow(reinterpret_cast<const char *>(builtIn->data()), builtIn->size());
    EXPECT_NE(glow.find("et=\"3\""), std::string::npos);

    const auto soft = hikari::core::srv3FromAss(ass, {{.style = u"YTGlow", .shadows = hikari::core::Srv3SoftShadow}});
    ASSERT_TRUE(soft);
    const std::string text(reinterpret_cast<const char *>(soft->data()), soft->size());
    EXPECT_EQ(text.find("et=\"3\""), std::string::npos);
    EXPECT_NE(text.find("et=\"4\""), std::string::npos);
}

TEST(Srv3, ScriptsYtSubConverterRefusesAreRefusedWithItsReason)
{
    EXPECT_EQ(hikari::core::srv3FromAss(u"[Script Info]\nPlayResX: 640\n").error(), "The given key was not present in the dictionary.");
    EXPECT_EQ(hikari::core::srv3FromAss(u"[Script Info]\nnot an item\n").error(), "Unrecognized line in .ass");
}

}
