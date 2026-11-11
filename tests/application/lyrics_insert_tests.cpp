#include "hikari/application/lyrics_insert.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/lyrics_import.h"
#include "hikari/core/subtitle_load.h"

#include <gtest/gtest.h>

#include <cstring>
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

core::Document lyrics(std::string_view lrc)
{
    auto read = core::readLyrics(bytes(lrc), core::LyricsFormat::Lrc);
    EXPECT_TRUE(read.has_value());
    return std::move(read->load.document);
}

struct Row {
    std::int64_t start = 0;
    std::u8string style;
    std::u8string text;
    bool operator==(const Row &) const = default;
};

std::vector<Row> rows(const EditSession &session)
{
    std::vector<Row> out;
    for (const auto *l : session.document().lines())
        out.push_back({l->start.value.microseconds() / 1000, l->style, l->text});
    return out;
}

constexpr std::string_view kOpen = "[Script Info]\nTitle: Mine\n\n[Events]\n"
                                   "Dialogue: 0,0:00:30.00,0:00:31.00,Default,,0,0,0,,mine\n";

}

TEST(LyricsInsert, LinesGoAfterTheLastLineInOneStepAndAreSelected)
{
    EditSession session(core::loadAss(bytes(kOpen)).document);
    const auto steps = session.historySize();
    ASSERT_TRUE(addLyricsLines(session, lyrics("[ti:Other]\n[00:01.00]<00:01.00>la <00:01.50>la\n[00:03.00]end\n"),
                               u8"Romaji"));
    EXPECT_EQ(rows(session), (std::vector<Row>{{30000, u8"Default", u8"mine"},
                                               {1000, u8"Romaji", u8"{\\k50}la {\\k150}la"},
                                               {3000, u8"Romaji", u8"end"}}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Import lyrics");
    EXPECT_EQ(session.document().scriptInfo(u8"Title"), u8"Mine");
    const auto lines = session.document().lines();
    EXPECT_EQ(session.selection().active, lines[1]->id);
    EXPECT_EQ(session.selection().selected, (std::set<core::LineId>{lines[1]->id, lines[2]->id}));
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(rows(session), (std::vector<Row>{{30000, u8"Default", u8"mine"}}));
}

TEST(LyricsInsert, OnlyAnAssDocumentTakesThem)
{
    const std::string_view srt = "1\n00:00:01,000 --> 00:00:02,000\nhello\n";
    auto loaded = core::loadSubtitle(bytes(srt), u8"srt");
    ASSERT_TRUE(loaded.has_value());
    EditSession session(std::move(loaded->document));
    const auto refused = addLyricsLines(session, lyrics("[00:01.00]x\n"), u8"Default");
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), CommandRefusal::Invalid);
}
