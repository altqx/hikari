#include "hikari/api/api.h"
#include "hikari/api/folder.h"

#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <sstream>

using namespace hikari::api;
namespace fs = std::filesystem;

namespace {

class FolderPaths : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_root = fs::temp_directory_path() / ("hikari-places-" + std::to_string(std::random_device{}()));
        ASSERT_TRUE(fs::create_directories(m_root / "data/notes"));
        ASSERT_TRUE(fs::create_directories(m_root / "outside"));
        std::ofstream(m_root / "outside/secret.txt") << "secret";
        std::ofstream(m_root / "data/notes/today.txt") << "today";
        m_data = fs::canonical(m_root / "data");
    }
    void TearDown() override { fs::remove_all(m_root); }

    bool link(const fs::path &target, const fs::path &at)
    {
        std::error_code error;
        if (fs::is_directory(target))
            fs::create_directory_symlink(target, at, error);
        else
            fs::create_symlink(target, at, error);
        return !error;
    }

    fs::path m_root;
    fs::path m_data;
};

}

TEST_F(FolderPaths, NamedPathsStayInsideTheFolder)
{
    EXPECT_EQ(pathInside(m_data, "").value(), m_data);
    EXPECT_EQ(pathInside(m_data, "notes/today.txt").value(), m_data / "notes" / "today.txt");
    EXPECT_EQ(pathInside(m_data, "notes/new/later.txt").value(), m_data / "notes" / "new" / "later.txt");
    EXPECT_EQ(pathInside(m_data, "\xE3\x83\x8E\xE3\x83\xBC\xE3\x83\x88.txt").value(), m_data / fs::path(u8"\u30CE\u30FC\u30C8.txt"));
}

TEST_F(FolderPaths, PathsThatClimbOutOrAreAbsoluteAreRefused)
{
    for (const std::string_view bad : {"..", "../outside/secret.txt", "notes/../../outside/secret.txt", "./notes", "notes/./today.txt",
                                       "/etc/passwd", "notes//today.txt", "notes/", "C:/Windows", "C:", "notes\\..\\..\\outside",
                                       "NUL", "con.txt", "notes/trailing.", "notes/trailing ", "a\nb", "what?"})
        EXPECT_FALSE(pathInside(m_data, bad).has_value()) << bad;
    EXPECT_FALSE(pathInside(m_data, std::string(1025, 'a')).has_value());
    EXPECT_FALSE(pathInside(m_data, "\xC3\x28").has_value());
    EXPECT_EQ(pathInside(m_data, "../outside/secret.txt").error(),
              "\xE2\x80\x9C../outside/secret.txt\xE2\x80\x9D has a part HikariSub refuses: \xE2\x80\x9C..\xE2\x80\x9D");
}

TEST_F(FolderPaths, LinksThatLeadOutsideAreRefused)
{
    if (!link(m_root / "outside", m_data / "door") || !link(m_root / "outside/secret.txt", m_data / "key.txt"))
        GTEST_SKIP() << "this system can't make symbolic links here";
    EXPECT_EQ(pathInside(m_data, "door/secret.txt").error(),
              "\xE2\x80\x9C" "door/secret.txt\xE2\x80\x9D leads outside the folder");
    EXPECT_FALSE(pathInside(m_data, "door").has_value());
    EXPECT_FALSE(pathInside(m_data, "door/new.txt").has_value());
    EXPECT_FALSE(pathInside(m_data, "key.txt").has_value());
    ASSERT_TRUE(link(m_data / "notes", m_data / "shortcut"));
    EXPECT_EQ(pathInside(m_data, "shortcut/today.txt").value(), m_data / "notes" / "today.txt");
}

TEST_F(FolderPaths, AMissingFolderIsRefused)
{
    EXPECT_FALSE(pathInside(m_root / "nowhere", "a.txt").has_value());
}

TEST_F(FolderPaths, EntriesComeInNameOrderWithoutLinksThatLeadOutside)
{
    std::ofstream(m_data / "b.txt") << "b";
    std::ofstream(m_data / "a.txt") << "a";
    const bool linked = link(m_root / "outside", m_data / "door") && link(m_root / "outside/secret.txt", m_data / "key.txt") &&
                        link(m_data / "notes", m_data / "shortcut");
    const auto listed = folderEntries(m_data, m_data);
    ASSERT_TRUE(listed.has_value());
    std::vector<std::pair<std::string, bool>> names;
    for (const auto &entry : listed->entries)
        names.emplace_back(entry.name, entry.folder);
    std::vector<std::pair<std::string, bool>> expected{{"a.txt", false}, {"b.txt", false}, {"notes", true}};
    if (linked)
        expected.emplace_back("shortcut", true);
    EXPECT_EQ(names, expected);
    EXPECT_FALSE(listed->truncated);
    EXPECT_FALSE(folderEntries(m_data, m_data / "a.txt").has_value());
}

namespace {

class DiskFiles final : public FileHost {
public:
    std::optional<std::string> read(const std::string &path, std::size_t atMost) const override
    {
        std::ifstream in(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        if (!in)
            return std::nullopt;
        std::stringstream text;
        text << in.rdbuf();
        return text.str().substr(0, atMost);
    }
    bool write(const std::string &path, std::string_view bytes) override
    {
        std::ofstream out(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary);
        return bool(out << bytes);
    }
};

class FakeProject final : public ProjectHost {
public:
    std::optional<ProjectState> project() const override { return state; }
    std::optional<ProjectState> state;
};

std::string utf8(const fs::path &p)
{
    const std::u8string text = p.u8string();
    return std::string(text.begin(), text.end());
}

class FolderGrants : public FolderPaths {
protected:
    Result call(std::string_view operation, std::string_view params)
    {
        Result out = fail("unanswered", "");
        m_api.call(m_caller, operation, *Json::parse(params), [&](Result r) { out = std::move(r); });
        return out;
    }
    std::string code(std::string_view operation, std::string_view params)
    {
        const Result r = call(operation, params);
        return r ? std::string("ok") : r.error().code;
    }
    std::string grant(bool writable) { return m_api.grantFolder(m_caller.id, utf8(m_data), writable); }

    DiskFiles m_files;
    FakeProject m_project;
    Api m_api{Host{.files = &m_files, .project = &m_project}};
    Caller m_caller{.id = "test.plugin", .label = "Test"};
};

}

TEST_F(FolderGrants, AFolderIsListedReadAndWrittenByRelativePaths)
{
    ASSERT_EQ(grant(true), "d1");
    const Result held = call("files.list", "{}");
    ASSERT_TRUE(held);
    EXPECT_EQ(*held->find("folders"), *Json::parse(R"([{"folder": "d1", "name": "data", "access": "write"}])"));
    const Result listed = call("files.list", R"({"folder": "d1", "path": "notes"})");
    ASSERT_TRUE(listed);
    EXPECT_EQ(*listed->find("entries"), *Json::parse(R"([{"path": "notes/today.txt", "folder": false}])"));
    const Result read = call("files.read", R"({"folder": "d1", "path": "notes/today.txt"})");
    ASSERT_TRUE(read);
    EXPECT_EQ(*read->find("data"), Json("today"));
    EXPECT_EQ(call("files.read", R"({"file": ")" + read->find("file")->asString() + R"("})")->find("data")->asString(),
              "today");
    EXPECT_EQ(code("files.write", R"({"folder": "d1", "path": "new/later.txt", "data": "later"})"), "ok");
    EXPECT_EQ(*m_files.read(utf8(m_data / "new/later.txt"), 100), "later");
}

TEST_F(FolderGrants, NothingOutsideTheFolderIsReached)
{
    grant(true);
    EXPECT_EQ(code("files.list", R"({"folder": "d1", "path": ".."})"), "not_granted");
    EXPECT_EQ(code("files.read", R"({"folder": "d1", "path": "../outside/secret.txt"})"), "not_granted");
    EXPECT_EQ(code("files.write", R"({"folder": "d1", "path": "../outside/secret.txt", "data": "x"})"), "not_granted");
    EXPECT_EQ(code("files.read", R"({"folder": "d2", "path": "notes/today.txt"})"), "not_granted");
    EXPECT_EQ(code("files.read", R"({"folder": "d1", "path": "notes"})"), "refused");
    EXPECT_EQ(code("files.read", R"({"folder": "d1", "path": ""})"), "invalid_params");
    EXPECT_EQ(code("files.read", R"({"folder": "d1"})"), "invalid_params");
    EXPECT_EQ(code("files.read", R"({"file": "f1", "folder": "d1", "path": "a"})"), "invalid_params");
    EXPECT_EQ(*m_files.read(utf8(m_root / "outside/secret.txt"), 100), "secret");
    m_caller.id = "another.plugin";
    EXPECT_EQ(code("files.list", R"({"folder": "d1"})"), "not_granted");
}

TEST_F(FolderGrants, AReadOnlyFolderRefusesWrites)
{
    grant(false);
    EXPECT_EQ(code("files.write", R"({"folder": "d1", "path": "notes/today.txt", "data": "x"})"), "not_granted");
    EXPECT_EQ(*m_files.read(utf8(m_data / "notes/today.txt"), 100), "today");
}

TEST_F(FolderGrants, LinksThatLeadOutsideAreRefusedForListReadAndWrite)
{
    if (!link(m_root / "outside", m_data / "door") || !link(m_root / "outside/secret.txt", m_data / "key.txt"))
        GTEST_SKIP() << "this system can't make symbolic links here";
    grant(true);
    const Result listed = call("files.list", R"({"folder": "d1"})");
    ASSERT_TRUE(listed);
    EXPECT_EQ(*listed->find("entries"), *Json::parse(R"([{"path": "notes", "folder": true}])"));
    EXPECT_EQ(code("files.list", R"({"folder": "d1", "path": "door"})"), "not_granted");
    EXPECT_EQ(code("files.read", R"({"folder": "d1", "path": "key.txt"})"), "not_granted");
    EXPECT_EQ(code("files.read", R"({"folder": "d1", "path": "door/secret.txt"})"), "not_granted");
    EXPECT_EQ(code("files.write", R"({"folder": "d1", "path": "door/secret.txt", "data": "x"})"), "not_granted");
    EXPECT_EQ(code("files.write", R"({"folder": "d1", "path": "key.txt", "data": "x"})"), "not_granted");
    EXPECT_EQ(*m_files.read(utf8(m_root / "outside/secret.txt"), 100), "secret");
}

TEST_F(FolderGrants, RevokingEndsTheFolder)
{
    grant(true);
    m_api.revokeFiles(m_caller.id);
    EXPECT_EQ(code("files.list", R"({"folder": "d1"})"), "not_granted");
}

TEST_F(FolderGrants, ProjectInfoAndProjectChanged)
{
    EXPECT_EQ(*call("project.info", "{}"), *Json::parse(R"({"project": null})"));
    std::vector<Json> heard;
    ASSERT_NE(m_api.subscribe(m_caller, {"project.changed"}, [&](const Event &e) { heard.push_back(e.data); }), 0u);
    m_project.state = ProjectState{"Show", utf8(m_data), false};
    m_api.projectChanged();
    const Json show = *Json::parse(R"({"project": {"name": "Show", "trusted": false}})");
    EXPECT_EQ(*call("project.info", "{}"), show);
    m_project.state.reset();
    m_api.projectChanged();
    ASSERT_EQ(heard.size(), 2u);
    EXPECT_EQ(heard[0], show);
    EXPECT_EQ(heard[1], *Json::parse(R"({"project": null})"));
}
