#include "hikari/themes/theme.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

using namespace hikari::themes;

namespace {

std::string read(const std::string &name)
{
    std::ifstream file(std::string(HIKARI_BUILTIN_THEMES) + "/" + name + ".json", std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

Colour role(const Resolved &r, Role role)
{
    return at(r.roles, role);
}

// A minimal valid dark theme; `roles` and `extra` are spliced in.
std::string minimal(std::string_view type = "dark", std::string_view roles = "", std::string_view extra = "")
{
    std::string json = R"({"type": ")" + std::string(type) + R"(", "roles": {
        "background": "#181B20", "panel": "#1E2127", "raised": "#2A2E35", "field": "#15171B",
        "text": "#E4E7EB", "muted": "#A0A8B2", "line": "#2C3037", "disabled": "#737B85",
        "accent": "#9CDBC9", "select": "#304C47", "danger": "#FFADAD", "warning": "#E0A030", "success": "#4DBA4D")";
    json += roles;
    json += R"(}, )";
    json += extra;
    json += R"("content": {"spellcheck": "#FF6B6B", "grammar": "#6EA8FE",
        "grid": {"alternate": "#242A31", "warning": "#7A2E2E", "comment": "#253A57",
                 "comparison": ["#2700FF", "#272B32", "#3A3E45", "#003176", "#3662A1"],
                 "labels": {"normal": "#2F3136", "modified": "#322F4E", "saved": "#202225", "doubtful": "#925B1F"}},
        "syntax": {"curlyBraces": "#8791FD", "operators": "#00C3FF", "names": "#00C3FF", "values": "#0076FF",
                   "splitAndDrawings": "#ADABAB", "templateVariables": "#BDB76B", "templateCodeMarks": "#FB4544",
                   "templateFunctions": "#00C3FF", "templateKeywords": "#8D80D3", "templateStrings": "#11A243",
                   "drawingX": "#4EC9B0", "drawingY": "#E5A0D9"},
        "ports": {"units": "#C9A0FF", "tags": "#7FD6A8", "number": "#8EC5FF", "text": "#F2C46D",
                  "colour": "#FF9EB5", "point": "#FFB37A", "bool": "#B0B8C0", "drawing": "#66D9E8"},
        "audio": {"background": "#36393E", "lineStart": "#940000", "lineEnd": "#940000", "inactiveBoundary": "#00D77D",
                  "cursor": "#8791FD", "secondBoundaries": "#F4F4F437", "keyframe": "#F4F4F4",
                  "inactiveBackground": "#00000055", "waveform": "#202225", "waveformInactive": "#2F3136",
                  "waveformModified": "#FF6968", "timescaleBackground": "#202225", "timescaleText": "#AEAFB2",
                  "spectrumBackground": "#000000", "spectrumEcho": "#674FD7", "spectrumInner": "#F4F4F4",
                  "lineBoundaryMark": "#FFFFFF", "syllableBoundaries": "#202225", "syllableText": "#8791FD"},
        "visualTools": {"handle": "#121150AA", "handleSelected": "#FCE6B1AA", "handleBorder": "#BB0000"}}})";
    return json;
}

}

TEST(BuiltinThemes, PassTheSchemaAndTheContrastCheck)
{
    for (const auto &[name, type] : {std::pair{"light", Type::Light}, std::pair{"dark", Type::Dark},
                                     std::pair{"highContrastWhite", Type::HighContrastLight},
                                     std::pair{"highContrastBlack", Type::HighContrastDark}}) {
        const auto theme = load(read(name));
        ASSERT_TRUE(theme) << name << ": " << theme.error().path << " " << theme.error().message;
        EXPECT_EQ(theme->type, type) << name;
    }
}

TEST(BuiltinThemes, CarryQuietsTokens)
{
    const Resolved dark = resolve(*load(read("dark")));
    EXPECT_EQ(role(dark, Role::Background), 0xFF181B20u);
    EXPECT_EQ(role(dark, Role::Field), 0xFF15171Bu);
    EXPECT_EQ(role(dark, Role::Hover), 0x0FE4E7EBu);
    EXPECT_EQ(role(dark, Role::Pressed), 0x1EE4E7EBu);
    const Resolved light = resolve(*load(read("light")));
    EXPECT_EQ(role(light, Role::Panel), 0xFFFFFFFFu);
    EXPECT_EQ(role(light, Role::Line), 0xFFDCE0E5u);
    EXPECT_EQ(role(light, Role::Hover), 0x0D1E2329u);
}

TEST(BuiltinThemes, LightAndDarkCarryTheSevenAccentsWithGreenTheDefault)
{
    for (const char *name : {"light", "dark"}) {
        const ThemeFile file = *load(read(name));
        ASSERT_EQ(file.accents.size(), 7u) << name;
        EXPECT_EQ(defaultAccent(file)->id, "green") << name;
        EXPECT_TRUE(file.userColours.empty()) << name;
    }
    const Resolved blue = resolve(*load(read("light")), {"blue", {}});
    EXPECT_EQ(role(blue, Role::Accent), 0xFF1D5BA8u);
    EXPECT_EQ(role(blue, Role::Select), 0xFFD6DFEBu);
}

TEST(BuiltinThemes, HighContrastListsAccentTextAndLineAndNoAccents)
{
    for (const char *name : {"highContrastWhite", "highContrastBlack"}) {
        const ThemeFile file = *load(read(name));
        EXPECT_TRUE(file.accents.empty()) << name;
        EXPECT_EQ(file.userColours, (std::vector{Role::Accent, Role::Text, Role::Line})) << name;
    }
}

TEST(Theme, LeftOutKeysAreWorkedOut)
{
    const Resolved r = resolve(*parse(minimal("dark", R"(, "control": "#757E89")")));
    EXPECT_EQ(role(r, Role::Focus), role(r, Role::Text));
    EXPECT_EQ(role(r, Role::Hover), withAlpha(role(r, Role::Text), 15));
    EXPECT_EQ(role(r, Role::Pressed), withAlpha(role(r, Role::Text), 30));
    EXPECT_EQ(role(r, Role::OnAccent), 0xFF000000u);
    EXPECT_EQ(*r.content.syntax[0], role(r, Role::Text));
    EXPECT_EQ(*r.content.grid.comparisonSelection, withAlpha(role(r, Role::Accent), 75));
    EXPECT_EQ(*r.content.audio.selectionBackground, withAlpha(role(r, Role::Accent), 0x37));
    EXPECT_EQ(*r.content.audio.selectionModified, *r.content.audio.selectionBackground);
    EXPECT_EQ(*r.content.audio.waveformSelected, role(r, Role::Accent));
}

TEST(Theme, HighContrastControlIsTheLineAndHoverIsStronger)
{
    const Resolved r = resolve(*parse(minimal("highContrastDark")));
    EXPECT_EQ(role(r, Role::Control), role(r, Role::Line));
    EXPECT_EQ(role(r, Role::Hover) >> 24, 31u);
}

TEST(Theme, LightAndDarkNeedControl)
{
    EXPECT_FALSE(parse(minimal("dark")));
    EXPECT_FALSE(parse(minimal("light")));
}

TEST(Theme, UnknownKeysAndBadColoursAreRefused)
{
    EXPECT_FALSE(parse(minimal("dark", R"(, "control": "#757E89", "glow": "#FFFFFF")")));
    EXPECT_FALSE(parse(minimal("dark", R"(, "control": "#757E8")")));
    EXPECT_FALSE(parse(minimal("sepia", R"(, "control": "#757E89")")));
}

TEST(Theme, OneAccentChoiceMustBeTheRoles)
{
    const std::string other = R"("accents": [{"id": "a", "title": "A", "accent": "#F4A9A3", "select": "#4C3230"},
                                             {"id": "b", "title": "B", "accent": "#A6C8F2", "select": "#303D4C"}], )";
    const auto refused = parse(minimal("dark", R"(, "control": "#757E89")", other));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().path, "/accents");
    const std::string with = R"("accents": [{"id": "a", "title": "A", "accent": "#F4A9A3", "select": "#4C3230"},
                                            {"id": "g", "title": "G", "accent": "#9CDBC9", "select": "#304C47"}], )";
    const auto file = parse(minimal("dark", R"(, "control": "#757E89")", with));
    ASSERT_TRUE(file);
    EXPECT_EQ(defaultAccent(*file)->id, "g");
    EXPECT_EQ(role(resolve(*file, {"a", {}}), Role::Accent), 0xFFF4A9A3u);
    EXPECT_EQ(role(resolve(*file, {"gone", {}}), Role::Accent), 0xFF9CDBC9u);
}

TEST(Theme, UserColoursApplyOnlyToListedRoles)
{
    const auto file = parse(minimal("highContrastDark", "", R"("userColours": ["accent"], )"));
    ASSERT_TRUE(file);
    const Resolved r = resolve(*file, {"", {{Role::Accent, 0xFF0037B3u}, {Role::Text, 0xFFFF0000u}}});
    EXPECT_EQ(role(r, Role::Accent), 0xFF0037B3u);
    EXPECT_EQ(role(r, Role::OnAccent), 0xFFFFFFFFu);
    EXPECT_EQ(role(r, Role::Text), 0xFFE4E7EBu);
}

TEST(Theme, ContrastCheckNamesThePairAndTheRatio)
{
    const auto file = parse(minimal("dark", R"(, "control": "#757E89")"));
    ASSERT_TRUE(file);
    EXPECT_FALSE(checkContrast(*file));
    auto dim = *file;
    dim.roles[std::size_t(Role::Muted)] = 0xFF505050u;
    const auto problem = checkContrast(dim);
    ASSERT_TRUE(problem);
    EXPECT_EQ(problem->path, "/roles/muted");
    EXPECT_NE(problem->message.find("below 4.50:1"), std::string::npos) << problem->message;
}

TEST(Theme, HighContrastNeedsSevenToOne)
{
    const auto file = parse(minimal("highContrastDark"));
    ASSERT_TRUE(file);
    const auto problem = checkContrast(*file);
    ASSERT_TRUE(problem);
    EXPECT_NE(problem->message.find("below 7.00:1"), std::string::npos) << problem->message;
}

TEST(Theme, EachAccentChoiceIsChecked)
{
    const std::string accents = R"("accents": [{"id": "g", "title": "G", "accent": "#9CDBC9", "select": "#304C47"},
                                               {"id": "dim", "title": "Dim", "accent": "#303030", "select": "#304C47"}], )";
    const auto file = parse(minimal("dark", R"(, "control": "#757E89")", accents));
    ASSERT_TRUE(file);
    const auto problem = checkContrast(*file);
    ASSERT_TRUE(problem);
    EXPECT_NE(problem->message.find("with the accent dim"), std::string::npos) << problem->message;
}
