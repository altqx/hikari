#include "hikari/application/tag_removal.h"
#include "hikari/core/ass_load.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

namespace app = hikari::application;
namespace core = hikari::core;
namespace tc = core::tagcleanup;

app::EditSession sessionWith(std::string_view events)
{
    std::string data = "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                       "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                       "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                       "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                       "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                       "10,10,10,1\n\n[Events]\n"
                       "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    data += events;
    std::vector<std::byte> bytes(data.size());
    std::memcpy(bytes.data(), data.data(), bytes.size());
    return app::EditSession(core::loadAss(bytes).document);
}

void select(app::EditSession &session, std::initializer_list<int> rows)
{
    app::Selection selection;
    const auto lines = session.document().lines();
    for (const int row : rows)
        selection.selected.insert(lines[row - 1]->id);
    selection.active = lines[*rows.begin() - 1]->id;
    session.setSelection(selection);
}

std::vector<std::string> texts(const app::EditSession &session)
{
    std::vector<std::string> out;
    for (const auto *l : session.document().lines())
        out.emplace_back(reinterpret_cast<const char *>(l->text.data()), l->text.size());
    return out;
}

tc::Options blurOnly()
{
    tc::Options options;
    options.families.set(static_cast<std::size_t>(tc::TagFamily::Blur));
    return options;
}

constexpr std::string_view kEvents = "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\blur2}A\n"
                                     "Comment: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\blur2}B\n"
                                     "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\blur2}C\n"
                                     "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,D\n";

TEST(TagRemoval, SelectedLinesIncludeComments)
{
    auto session = sessionWith(kEvents);
    select(session, {2, 3});
    const auto changes = app::tagRemovalChanges(session, blurOnly(), app::TagRemovalLines::Selected);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].row, 2);
    EXPECT_EQ(changes[0].before, u8"{\\blur2}B");
    EXPECT_EQ(changes[0].after, u8"B");
    EXPECT_EQ(changes[1].row, 3);
}

TEST(TagRemoval, TheFileSkipsCommentsAsCleanTagsDoes)
{
    auto session = sessionWith(kEvents);
    select(session, {4});
    const auto steps = session.historySize();
    EXPECT_EQ(app::removeTags(session, blurOnly(), app::TagRemovalLines::File), 2);
    EXPECT_EQ(texts(session), (std::vector<std::string>{"A", "{\\blur2}B", "C", "D"}));
    EXPECT_EQ(session.historySize(), steps + 1);
    EXPECT_EQ(session.history().back().name, "Remove tags");
}

TEST(TagRemoval, NothingToChangeIsRefused)
{
    auto session = sessionWith(kEvents);
    select(session, {4});
    EXPECT_FALSE(app::removeTags(session, blurOnly(), app::TagRemovalLines::Selected));
}

TEST(TagRemoval, DeletesLinesLeftEmpty)
{
    auto session = sessionWith("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\blur2}\n"
                               "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\blur2}B\n"
                               "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,\n");
    select(session, {1, 2, 3});
    auto options = blurOnly();
    options.cleanup.deleteEmptyLines = true;
    const auto changes = app::tagRemovalChanges(session, options, app::TagRemovalLines::Selected);
    ASSERT_EQ(changes.size(), 3u);
    EXPECT_TRUE(changes[0].deleted);
    EXPECT_FALSE(changes[1].deleted);
    EXPECT_TRUE(changes[2].deleted) << "an empty Line in scope goes too";
    EXPECT_EQ(app::removeTags(session, options, app::TagRemovalLines::Selected), 3);
    EXPECT_EQ(texts(session), std::vector<std::string>{"B"});
    const auto lines = session.document().lines();
    EXPECT_EQ(session.selection().active, lines[0]->id);
    EXPECT_EQ(session.selection().selected, std::set<core::LineId>{lines[0]->id});
}

}
