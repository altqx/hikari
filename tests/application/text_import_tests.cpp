#include "hikari/application/text_import.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace hikari;
using namespace hikari::application;

namespace {

std::vector<std::byte> bytes(std::string_view text)
{
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::string text(const std::vector<std::byte> &b)
{
    return std::string(reinterpret_cast<const char *>(b.data()), b.size());
}

const std::vector<core::TextImportLine> kLines{{u8"Ana", u8"Hello", false}, {u8"", u8"A note", true}};

constexpr std::string_view kOpen = "[Script Info]\nTitle: Mine\n\n[Events]\n"
                                   "Dialogue: 0,0:00:30.00,0:00:31.00,Default,,0,0,0,,mine\n";

}

TEST(TextImportInsert, ANewFileStartsAsFileNewAndHoldsTheLinesUntimed)
{
    const core::LoadResult load = textImportFile(kLines);
    const std::string saved = text(core::encodeAss(load.document));
    EXPECT_NE(saved.find("Title: HikariSub Ass File"), std::string::npos) << saved;
    EXPECT_NE(saved.find("Style: Default,"), std::string::npos) << saved;
    EXPECT_NE(saved.find("Dialogue: 0,0:00:00.00,0:00:00.00,Default,Ana,0,0,0,,Hello"), std::string::npos) << saved;
    EXPECT_NE(saved.find("Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,,A note"), std::string::npos) << saved;
    EXPECT_EQ(load.document.lines().size(), 2u);
}

TEST(TextImportInsert, LinesGoAfterTheLastLineInOneStepAndAreSelected)
{
    EditSession session(core::loadAss(bytes(kOpen)).document);
    const auto steps = session.historySize();
    ASSERT_TRUE(addTextLines(session, kLines, u8"Signs"));
    const auto lines = session.document().lines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[1]->style, u8"Signs");
    EXPECT_EQ(lines[1]->actor, u8"Ana");
    EXPECT_EQ(lines[1]->text, u8"Hello");
    EXPECT_FALSE(lines[1]->comment);
    EXPECT_TRUE(lines[2]->comment);
    EXPECT_EQ(lines[2]->start.value.microseconds(), 0);
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Import text");
    EXPECT_EQ(session.selection().active, lines[1]->id);
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{lines[1]->id, lines[2]->id}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(session.document().lines().size(), 1u);
}

TEST(TextImportInsert, OnlyAnAssDocumentTakesThem)
{
    auto loaded = core::loadSubtitle(bytes("1\n00:00:01,000 --> 00:00:02,000\nhello\n"), u8"srt");
    ASSERT_TRUE(loaded.has_value());
    EditSession session(std::move(loaded->document));
    const auto refused = addTextLines(session, kLines, u8"Default");
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), CommandRefusal::Invalid);
}
