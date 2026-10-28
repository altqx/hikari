// Y3: Script properties against legacy HikariSubFrame::OnAssProps and
// SubsGrid::GetASSRes/GetLayoutRes at 20d647c4.

#include "hikari/application/script_properties.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"

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

std::string saved(const EditSession &s)
{
    const auto b = core::encodeAss(s.document());
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

constexpr std::string_view kFull =
    "[Script Info]\nTitle: Show\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\nWrapStyle: 2\n"
    "Collisions: Normal\nScaledBorderAndShadow: yes\nYCbCr Matrix: TV.709\n\n[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n";

} // namespace

TEST(ScriptProperties, WhatTheDialogShows)
{
    const auto p = scriptProperties(load(kFull));
    EXPECT_EQ(p.title, u8"Show");
    EXPECT_EQ(p.playResX, 640);
    EXPECT_EQ(p.playResY, 360);
    EXPECT_EQ(p.layoutResX, 0);
    EXPECT_EQ(matrixNames()[static_cast<std::size_t>(p.matrix)], u8"TV.709");
    EXPECT_EQ(p.wrapStyle, 2);
    EXPECT_FALSE(p.reverseCollisions);
    EXPECT_TRUE(p.scaledBorderAndShadow);
    // Missing PlayRes is filled as legacy GetASSRes does.
    const auto bare = scriptProperties(load("[Script Info]\nPlayResY: 480\n\n[Events]\n"));
    EXPECT_EQ(bare.playResX, 853);
    EXPECT_EQ(bare.playResY, 480);
}

TEST(ScriptProperties, OnlyChangedFieldsAreWrittenAsOneStep)
{
    EditSession session{load(kFull)};
    auto p = scriptProperties(session.document());
    const auto steps = session.historySize();
    // OK without changes adds nothing.
    ASSERT_TRUE(applyScriptProperties(session, p, {}, false));
    EXPECT_EQ(session.historySize(), steps);
    p.originalTranslation = u8"Someone";
    p.playResX = 1920;
    p.playResY = 1080;
    p.wrapStyle = 0;
    p.matrix = 0; // None
    ScriptPropertiesEdits edits;
    edits.originalTranslation = edits.playResX = edits.playResY = true;
    ASSERT_TRUE(applyScriptProperties(session, p, edits, false));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Changing the subtitle header");
    const std::string file = saved(session);
    EXPECT_NE(file.find("PlayResX: 1920\nPlayResY: 1080\nWrapStyle: 0\n"), std::string::npos) << file;
    EXPECT_NE(file.find("YCbCr Matrix: None\nOriginal Translation: Someone\n"), std::string::npos) << file;
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(saved(session), std::string(kFull));
}

TEST(ScriptProperties, LegacyDefaultsOnOk)
{
    // A bare header: OK adds the PlayRes fallbacks, the default title and the
    // Collisions / ScaledBorderAndShadow values the dialog shows.
    EditSession session{load("[Script Info]\nScriptType: v4.00+\n\n[Events]\n")};
    const auto p = scriptProperties(session.document());
    ASSERT_TRUE(applyScriptProperties(session, p, {}, false));
    const auto &d = session.document();
    EXPECT_EQ(d.scriptInfo(u8"PlayResX"), u8"1280");
    EXPECT_EQ(d.scriptInfo(u8"PlayResY"), u8"720");
    EXPECT_EQ(d.scriptInfo(u8"Title"), u8"HikariSub Ass File");
    EXPECT_EQ(d.scriptInfo(u8"Collisions"), u8"Normal");
    EXPECT_EQ(d.scriptInfo(u8"ScaledBorderAndShadow"), u8"yes");
    EXPECT_FALSE(d.scriptInfo(u8"YCbCr Matrix")); // unchanged index: not written
}

TEST(ScriptProperties, LayoutResolutionRules)
{
    // Edited layout width only: the height is not written.
    EditSession a{load(kFull)};
    auto p = scriptProperties(a.document());
    p.layoutResX = 1280;
    ScriptPropertiesEdits edits;
    edits.layoutResX = true;
    ASSERT_TRUE(applyScriptProperties(a, p, edits, false));
    EXPECT_EQ(a.document().scriptInfo(u8"LayoutResX"), u8"1280");
    EXPECT_FALSE(a.document().scriptInfo(u8"LayoutResY")); // height not edited
    // Linked with a LayoutResX present: the layout follows the subtitle resolution.
    p = scriptProperties(a.document());
    p.playResX = 1920;
    p.playResY = 1080;
    edits = {};
    edits.playResX = edits.playResY = true;
    ASSERT_TRUE(applyScriptProperties(a, p, edits, true));
    EXPECT_EQ(a.document().scriptInfo(u8"LayoutResX"), u8"1920");
    EXPECT_EQ(a.document().scriptInfo(u8"LayoutResY"), u8"1080");
    // Edited layout height only, width absent: written as edited (legacy
    // "completes" the missing width from itself, which leaves it 0, unwritten).
    EditSession b{load(kFull)};
    p = scriptProperties(b.document());
    p.layoutResY = 720;
    edits = {};
    edits.layoutResY = true;
    ASSERT_TRUE(applyScriptProperties(b, p, edits, false));
    EXPECT_EQ(b.document().scriptInfo(u8"LayoutResY"), u8"720");
}
