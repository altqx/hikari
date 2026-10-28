// P3: autosave recovery bundles (accepted L58-recovery-copy and the
// 2026-09-29 retention choices): generations, verification, rotation,
// leftovers of ended sessions, pruning.

#include "hikari/application/recovery_store.h"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("hikari-recovery-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

RecoveryContent content(std::string_view text, std::int64_t at = 1'000)
{
    RecoveryContent c;
    c.bytes.resize(text.size());
    std::transform(text.begin(), text.end(), c.bytes.begin(), [](char ch) { return std::byte(ch); });
    c.extension = "ass";
    c.title = "Episode 1.ass";
    c.originalPath = "/subs/Episode 1.ass";
    c.writtenMs = at;
    return c;
}

std::string text(const RecoveryContent &c)
{
    return std::string(reinterpret_cast<const char *>(c.bytes.data()), c.bytes.size());
}

auto ended = [](const std::string &) { return true; };

} // namespace

TEST(RecoveryStore, KeepsTheLastThreeGenerations)
{
    TempDir dir;
    RecoveryStore store(dir.path, "first");
    for (int i = 1; i <= 4; ++i)
        ASSERT_TRUE(store.write("doc", content("v" + std::to_string(i), i)));
    RecoveryStore later(dir.path, "second");
    const auto left = later.leftovers(ended);
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].generations, (std::vector<std::uint64_t>{2, 3, 4}));
    EXPECT_EQ(text(left[0].latest), "v4");
    EXPECT_EQ(left[0].latest.title, "Episode 1.ass");
    EXPECT_FALSE(fs::exists(dir.path / "doc" / "1"));
    EXPECT_EQ(text(*later.read("doc", 2)), "v2");
}

TEST(RecoveryStore, DraftsAndTextSurviveExactly)
{
    TempDir dir;
    RecoveryStore store(dir.path, "first");
    auto c = content("[Events]\n");
    c.title = "line\nbreak";
    c.draftRow = 3;
    c.draft.text = u8"typed\nnot committed";
    c.draft.start = hikari::core::DocumentTime(1'500'000);
    c.draft.marginVertical = 12;
    c.frameRate = std::pair<std::int64_t, std::int64_t>(24000, 1001);
    ASSERT_TRUE(store.write("doc", c));
    const auto back = RecoveryStore(dir.path, "second").leftovers(ended).at(0).latest;
    EXPECT_EQ(back.title, "line\nbreak");
    EXPECT_EQ(back.draftRow, std::optional<std::size_t>(3));
    EXPECT_EQ(back.draft.text, std::optional<std::u8string>(u8"typed\nnot committed"));
    EXPECT_EQ(back.draft.start->microseconds(), 1'500'000);
    EXPECT_FALSE(back.draft.end);
    EXPECT_EQ(back.draft.marginVertical, std::optional<std::int64_t>(12));
    EXPECT_EQ(back.frameRate, (std::pair<std::int64_t, std::int64_t>(24000, 1001)));
}

// E4: the editor's metadata fields survive in the draft, a cleared MicroDVD
// frame as cleared and an untouched field as absent.
TEST(RecoveryStore, MetadataDraftFieldsSurviveExactly)
{
    TempDir dir;
    RecoveryStore store(dir.path, "first");
    auto c = content("[Events]\n");
    c.draftRow = 0;
    c.draft.comment = true;
    c.draft.layer = -3;
    c.draft.style = u8"Sign, top";
    c.draft.actor = u8"Ayumu";
    c.draft.effect = u8"";
    c.draft.startFrame = std::optional<std::int64_t>(42);
    c.draft.endFrame = std::optional<std::int64_t>();
    ASSERT_TRUE(store.write("doc", c));
    const auto back = RecoveryStore(dir.path, "second").leftovers(ended).at(0).latest;
    EXPECT_EQ(back.draft.comment, std::optional<bool>(true));
    EXPECT_EQ(back.draft.layer, std::optional<std::int64_t>(-3));
    EXPECT_EQ(back.draft.style, std::optional<std::u8string>(u8"Sign, top"));
    EXPECT_EQ(back.draft.actor, std::optional<std::u8string>(u8"Ayumu"));
    EXPECT_EQ(back.draft.effect, std::optional<std::u8string>(u8""));
    ASSERT_TRUE(back.draft.startFrame);
    EXPECT_EQ(*back.draft.startFrame, std::optional<std::int64_t>(42));
    ASSERT_TRUE(back.draft.endFrame);
    EXPECT_FALSE(*back.draft.endFrame);
    EXPECT_FALSE(back.draft.marginLeft);
}

TEST(RecoveryStore, ACorruptGenerationFallsBackToThePreviousOne)
{
    TempDir dir;
    RecoveryStore store(dir.path, "first");
    ASSERT_TRUE(store.write("doc", content("good")));
    ASSERT_TRUE(store.write("doc", content("newer")));
    std::ofstream(dir.path / "doc" / "2" / "content", std::ios::trunc) << "torn";
    const auto left = RecoveryStore(dir.path, "second").leftovers(ended);
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(text(left[0].latest), "good");
}

TEST(RecoveryStore, OnlyEndedOtherSessionsAreLeftovers)
{
    TempDir dir;
    RecoveryStore crashed(dir.path, "crashed");
    RecoveryStore running(dir.path, "running");
    ASSERT_TRUE(crashed.write("a", content("a")));
    ASSERT_TRUE(running.write("b", content("b")));
    RecoveryStore me(dir.path, "me");
    ASSERT_TRUE(me.write("c", content("c")));
    const auto left = me.leftovers([](const std::string &s) { return s == "crashed"; });
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].key, "a");
    me.discard("a");
    EXPECT_TRUE(me.leftovers([](const std::string &s) { return s == "crashed"; }).empty());
}

TEST(RecoveryStore, OldGenerationsArePruned)
{
    TempDir dir;
    RecoveryStore store(dir.path, "first");
    const auto now = std::chrono::system_clock::now();
    const auto ms = [](auto t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
    };
    ASSERT_TRUE(store.write("old", content("old", ms(now - std::chrono::hours(24 * 31)))));
    ASSERT_TRUE(store.write("mixed", content("older", ms(now - std::chrono::hours(24 * 40)))));
    ASSERT_TRUE(store.write("mixed", content("recent", ms(now - std::chrono::hours(1)))));
    RecoveryStore later(dir.path, "second");
    later.prune(now);
    const auto left = later.leftovers(ended);
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].key, "mixed");
    EXPECT_EQ(left[0].generations, (std::vector<std::uint64_t>{2}));
    EXPECT_EQ(left[0].session, "first"); // pruning keeps the bundle's session
}

TEST(RecoveryStore, CapacityZeroDisablesAutosave)
{
    TempDir dir;
    RecoveryStore store(dir.path, "first", 0);
    EXPECT_FALSE(store.enabled());
    EXPECT_FALSE(store.write("doc", content("x")));
}
