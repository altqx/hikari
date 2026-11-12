#include "hikari/backends/icu_text.h"
#include "hikari/core/graphemes.h"
#include "hikari/core/text_projection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

using namespace hikari;
using namespace hikari::application::text_commands;

namespace {

std::set<std::size_t> graphemeBoundaries(std::u16string_view text)
{
    std::set<std::size_t> out{0};
    for (std::size_t at = 0; at < text.size();)
        out.insert(at = core::graphemeEnd(text, at));
    return out;
}

double shownLength(std::u8string_view prefix)
{
    return static_cast<double>(core::project(core::toUtf16(prefix)).text.size());
}

}

TEST(IcuText, CaseMappingIsFullAndLocaleFree)
{
    const auto s = backends::icuTextServices();
    EXPECT_EQ(s.upper(u"straße i"), u"STRASSE I");
    EXPECT_EQ(s.lower(u"ÉLAN ΟΔΟΣ"), u"élan οδος");
    EXPECT_EQ(changeCase(u8"élan vital of the ẞ", Case::Title, s), u8"Élan Vital of the ẞ");
}

TEST(IcuText, ThaiAndCjkBreaksFallBetweenGraphemes)
{
    const auto s = backends::icuTextServices();
    for (const std::u16string_view text : {std::u16string_view(u"ภาษาไทยง่ายนิดเดียวครับ"), std::u16string_view(u"日本語のテキストです")}) {
        const auto breaks = s.lineBreaks(text);
        ASSERT_FALSE(breaks.empty());
        const auto graphemes = graphemeBoundaries(text);
        for (const auto b : breaks) {
            EXPECT_GT(b, 0u);
            EXPECT_LT(b, text.size());
            EXPECT_TRUE(graphemes.contains(b)) << b;
        }
    }
}

TEST(IcuText, BalancedBreakInThaiKeepsTheText)
{
    const auto s = backends::icuTextServices();
    const std::u8string thai = u8"ภาษาไทยง่ายนิดเดียวครับ";
    const auto broken = balancedBreak(thai, s, shownLength);
    const auto at = broken.find(u8"\\N");
    ASSERT_NE(at, std::u8string::npos);
    EXPECT_EQ(broken.find(u8"\\N", at + 2), std::u8string::npos);
    EXPECT_EQ(std::u8string(broken).erase(at, 2), thai);
    const auto before = core::toUtf16(std::u8string_view(broken).substr(0, at));
    EXPECT_TRUE(graphemeBoundaries(core::toUtf16(thai)).contains(before.size()));
    EXPECT_EQ(removeBreaks(broken, s), thai);
}

TEST(IcuText, RemovingABreakAddsASpaceOnlyWhereTheScriptNeedsOne)
{
    const auto s = backends::icuTextServices();
    EXPECT_EQ(removeBreaks(u8"日本\\N語", s), u8"日本語");
    EXPECT_EQ(removeBreaks(u8"one\\Ntwo", s), u8"one two");
    EXPECT_EQ(moveBreak(u8"日本語です", Direction::Right, s).find(u8"\\N") != std::u8string::npos, true);
}
