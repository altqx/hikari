#include "hikari/application/legacy_dir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

class LegacyDir : public ::testing::Test {
protected:
    void SetUp() override
    {
        std::random_device rd;
        folder = fs::temp_directory_path() / ("hikari-legacy-dir-" + std::to_string(rd()));
        fs::create_directories(folder);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
    void touch(const fs::path &name) { std::ofstream(folder / name) << "x"; }
    std::vector<std::string> names(const std::optional<std::vector<fs::path>> &listed) const
    {
        std::vector<std::string> out;
        if (listed)
            for (const auto &p : *listed) {
                EXPECT_EQ(p.parent_path(), folder);
                out.push_back(p.filename().string());
            }
        return out;
    }
    fs::path folder;
};

}

TEST(LegacyMatchWild, PortsWxMatchWild)
{
    using legacy_dir::matchWild;
    EXPECT_TRUE(matchWild(U"*.dic", U"en_US.dic", true));
    EXPECT_TRUE(matchWild(U"*.dic", U"a.b.dic", true));
    EXPECT_FALSE(matchWild(U"*.dic", U"x.dicx", true));
    EXPECT_FALSE(matchWild(U"*.dic", U"EN.DIC", true));
    EXPECT_FALSE(matchWild(U"*.dic", U".hidden.dic", true));
    EXPECT_TRUE(matchWild(U"*.dic", U".hidden.dic", false));
    EXPECT_TRUE(matchWild(U"?.dic", U"\U0001F600.dic", true)); // one code point, as Linux wchar_t
    EXPECT_FALSE(matchWild(U"?.dic", U".dic", false));
    EXPECT_TRUE(matchWild(U"a\\*", U"a*", true));
    EXPECT_FALSE(matchWild(U"a\\*", U"ab", true));
    EXPECT_FALSE(matchWild(U"a\\", U"a", true));
    EXPECT_TRUE(matchWild(U"a*", U"a", true));
    EXPECT_TRUE(matchWild(U"*", U"anything", true));
    EXPECT_FALSE(matchWild(U"*", U"", true));
    EXPECT_TRUE(matchWild(U"", U"", true));
    EXPECT_FALSE(matchWild(U"[a].ass", U"a.ass", true));
    EXPECT_TRUE(matchWild(U"[a].ass", U"[a].ass", true));
}

TEST_F(LegacyDir, AMissingFolderIsNotOpened)
{
    EXPECT_FALSE(legacy_dir::entries(folder / "missing", u"*.dic", legacy_dir::Files));
    touch("file.dic");
    EXPECT_FALSE(legacy_dir::entries(folder / "file.dic", u"", legacy_dir::Files));
}

#ifdef _WIN32

namespace {

std::vector<std::string> systemOrder(const fs::path &folder, const wchar_t *pattern)
{
    std::vector<std::string> out;
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW((folder / pattern).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return out;
    do {
        const std::wstring name = data.cFileName;
        if (name != L"." && name != L"..")
            out.push_back(fs::path(name).string());
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return out;
}

}

TEST_F(LegacyDir, WindowsListsAsFindFirstFileWithWxsLongNameCheck)
{
    touch("b.dic");
    touch("A.DIC");
    touch("x.dicx"); // short name X~1.DIC on volumes with 8.3 names
    touch("readme.txt");
    touch("hidden.dic");
    touch("system.dic");
    fs::create_directories(folder / "folder.dic");
    SetFileAttributesW((folder / "hidden.dic").c_str(), FILE_ATTRIBUTE_HIDDEN);
    SetFileAttributesW((folder / "system.dic").c_str(), FILE_ATTRIBUTE_SYSTEM);

    // FindFirstFileW also matches 8.3 short names; wx rechecks the long name.
    const auto raw = systemOrder(folder, L"*.dic");
    if (std::ranges::find(raw, "x.dicx") == raw.end())
        std::cout << "note: this volume has no 8.3 name for x.dicx\n";
    std::vector<std::string> expected;
    for (const auto &name : raw)
        if (name == "b.dic" || name == "A.DIC")
            expected.push_back(name);
    EXPECT_EQ(names(legacy_dir::entries(folder, u"*.dic", legacy_dir::Files)), expected);
    expected.clear();
    for (const auto &name : raw)
        if (name != "x.dicx" && name != "folder.dic")
            expected.push_back(name);
    EXPECT_EQ(names(legacy_dir::entries(folder, u"*.dic", legacy_dir::Files | legacy_dir::Hidden)), expected);
    EXPECT_EQ(names(legacy_dir::entries(folder, u"", legacy_dir::Dirs)), (std::vector<std::string>{"folder.dic"}));
}

#else

namespace {

std::vector<std::string> readdirOrder(const fs::path &folder)
{
    std::vector<std::string> out;
    if (DIR *d = ::opendir(folder.c_str())) {
        while (const dirent *e = ::readdir(d))
            if (std::string_view(e->d_name) != "." && std::string_view(e->d_name) != "..")
                out.push_back(e->d_name);
        ::closedir(d);
    }
    return out;
}

}

TEST_F(LegacyDir, LinuxListsInReaddirOrderWithWxMatchWild)
{
    for (const char *name : {"b.dic", "a.dic", "c.dic", "EN.DIC", "x.dicx", ".hidden.dic", "notes.txt", "z.dic"})
        touch(name);
    fs::create_directories(folder / "folder.dic");
    fs::create_directories(folder / ".hiddenfolder");
    fs::create_directory_symlink(folder / "folder.dic", folder / "link.dic");
    fs::create_symlink(folder / "nowhere", folder / "broken.dic");

    const auto order = readdirOrder(folder);
    const auto pick = [&](auto keep) {
        std::vector<std::string> out;
        std::ranges::copy_if(order, std::back_inserter(out), keep);
        return out;
    };
    EXPECT_EQ(names(legacy_dir::entries(folder, u"*.dic", legacy_dir::Files)), pick([](const std::string &n) {
                  return n == "b.dic" || n == "a.dic" || n == "c.dic" || n == "z.dic" || n == "broken.dic";
              }));
    EXPECT_EQ(names(legacy_dir::entries(folder, u"*.dic", legacy_dir::Files | legacy_dir::Hidden)),
              pick([](const std::string &n) {
                  return n == "b.dic" || n == "a.dic" || n == "c.dic" || n == "z.dic" || n == "broken.dic" ||
                         n == ".hidden.dic";
              }));
    EXPECT_EQ(names(legacy_dir::entries(folder, u"", legacy_dir::Dirs)),
              pick([](const std::string &n) { return n == "folder.dic" || n == "link.dic"; }));
    EXPECT_EQ(names(legacy_dir::entries(folder, u"", legacy_dir::Dirs | legacy_dir::Hidden)),
              pick([](const std::string &n) { return n == "folder.dic" || n == "link.dic" || n == ".hiddenfolder"; }));
    EXPECT_EQ(names(legacy_dir::entries(folder, u"", legacy_dir::Files | legacy_dir::Dirs | legacy_dir::Hidden)), order);
}

#endif
