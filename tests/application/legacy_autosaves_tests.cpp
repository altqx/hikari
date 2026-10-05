// P9: the legacy Subs/ autosaves offered read only in Open auto save, listed
// as legacy AutoSaveOpen::GenerateList lists them (AutoSaveOpen.cpp:123-184,
// DateCompare in AutoSaveOpen.h:27-53, the names SubsGrid::OnBackupTimer
// writes in SubsGridBase.cpp:1636-1651, at 20d647c4).

#include "hikari/application/legacy_autosaves.h"

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <random>

using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

class LegacyAutosaves : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::random_device rd;
        folder = fs::temp_directory_path() / ("hikari-legacy-subs-" + std::to_string(rd()));
        fs::create_directories(folder);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
    // A file written with `text` whose local time is `when` (the fake clock
    // below maps each file's modification time to it).
    void write(const std::string &name, const std::string &text, LocalTime when)
    {
        std::ofstream(folder / name, std::ios::binary) << text;
        const auto t = fs::file_time_type::clock::now() - std::chrono::hours(1) + std::chrono::seconds(times.size());
        fs::last_write_time(folder / name, t);
        times[t] = when;
    }
    LocalTimeOf clock()
    {
        return [this](fs::file_time_type t) { return times.at(t); };
    }
    std::map<fs::file_time_type, LocalTime> times;
    fs::path folder;
};

std::string utf8(const std::u16string &s)
{
    return fs::path(s).string();
}

} // namespace

TEST_F(LegacyAutosaves, GroupsByNameNewestFirst)
{
    write("ep01_0_1.ass", "a", {2026, 9, 30, 10, 0, 5});
    write("ep01_0_2.ass", "b", {2026, 10, 1, 9, 0, 0});
    write("ep01_1_1.ass", "c", {2025, 12, 31, 23, 59, 59});
    write("Untitled_2_1.srt", "d", {2026, 1, 2, 3, 4, 5});
    write("ep01_0_3.srt", "e", {2026, 1, 1, 0, 0, 0}); // another extension: another file
    write("empty_0_1.ass", "", {2026, 1, 1, 0, 0, 0}); // nFileSizeLow == 0
    fs::create_directories(folder / "folder_0_1.ass");
    const auto files = listLegacyAutosaves(folder, u"Untitled", clock());
    std::map<std::string, std::vector<std::string>> byName;
    for (const auto &f : files)
        for (const auto &v : f.versions)
            byName[utf8(f.name)].push_back(v.written);
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(byName["ep01.ass"],
              (std::vector<std::string>{"10/01/2026   09:00:00", "09/30/2026   10:00:05", "12/31/2025   23:59:59"}));
    EXPECT_EQ(byName["Untitled.srt"], (std::vector<std::string>{"01/02/2026   03:04:05"}));
    EXPECT_EQ(byName["ep01.srt"], (std::vector<std::string>{"01/01/2026   00:00:00"}));
    for (const auto &f : files)
        for (const auto &v : f.versions)
            EXPECT_EQ(v.file.parent_path(), folder);
}

// The names as wxString::BeforeLast leaves them: nothing left before
// "_<tab>_<number>" is "Untitled" (DummySubs* skipped); no '.' takes the whole
// name as the extension.
TEST_F(LegacyAutosaves, NamesAsBeforeLastLeavesThem)
{
    write("_0_1.ass", "a", {2026, 1, 1, 0, 0, 1});
    write("one_1.ass", "b", {2026, 1, 1, 0, 0, 2});
    write("DummySubs_0_1.ass", "c", {2026, 1, 1, 0, 0, 3});
    write("DummySubs.ass", "d", {2026, 1, 1, 0, 0, 4});
    write("noextension", "e", {2026, 1, 1, 0, 0, 5});
    write("a_b_c_0_1.ass", "f", {2026, 1, 1, 0, 0, 6});
    const auto files = listLegacyAutosaves(folder, u"Bez nazwy", clock());
    std::map<std::string, std::size_t> versions;
    for (const auto &f : files)
        versions[utf8(f.name)] = f.versions.size();
    // "_0_1.ass", "one_1.ass" (tab "one") and "noextension" leave nothing.
    EXPECT_EQ(versions["Bez nazwy.ass"], 2u);
    EXPECT_EQ(versions["Bez nazwy.noextension"], 1u);
    // "DummySubs_0_1.ass" leaves "DummySubs" and is listed; "DummySubs.ass" leaves nothing and is skipped.
    EXPECT_EQ(versions["DummySubs.ass"], 1u);
    EXPECT_EQ(versions["a_b_c.ass"], 1u);
    EXPECT_FALSE(versions.contains("DummySubs_0_1.ass"));
}

// Two versions written in the same second: the map keeps the first listed.
TEST_F(LegacyAutosaves, OneVersionPerSecond)
{
    write("ep_0_1.ass", "a", {2026, 3, 4, 5, 6, 7});
    write("ep_0_2.ass", "b", {2026, 3, 4, 5, 6, 7});
    const auto files = listLegacyAutosaves(folder, u"Untitled", clock());
    ASSERT_EQ(files.size(), 1u);
    ASSERT_EQ(files[0].versions.size(), 1u);
    const auto kept = files[0].versions[0].file.filename();
    EXPECT_TRUE(kept == "ep_0_1.ass" || kept == "ep_0_2.ass"); // the first in listing order
}

// Reading only: the folder and its files stay exactly as they were.
TEST_F(LegacyAutosaves, NothingIsWritten)
{
    write("ep_0_1.ass", "content", {2026, 3, 4, 5, 6, 7});
    const auto before = fs::last_write_time(folder / "ep_0_1.ass");
    (void)listLegacyAutosaves(folder, u"Untitled", clock());
    EXPECT_EQ(fs::last_write_time(folder / "ep_0_1.ass"), before);
    std::ifstream in(folder / "ep_0_1.ass", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "content");
    EXPECT_EQ(std::distance(fs::directory_iterator(folder), fs::directory_iterator()), 1);
    EXPECT_TRUE(listLegacyAutosaves(folder / "missing", u"Untitled").empty());
}
