#include "fake_host.h"
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
    std::vector<std::string> aiExclude(const std::string &) const override { return excludes; }
    std::optional<ProjectState> state;
    std::vector<std::string> excludes;
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

    hikari::application::MemorySettingsStorage m_storage;
    hikari::application::Settings m_settings{m_storage};
    DiskFiles m_files;
    FakeProject m_project;
    hikari::api::testing::FakeHost m_documents;
    hikari::application::AiReview m_review;
    Api m_api{Host{.documents = &m_documents, .settings = &m_settings, .files = &m_files, .review = &m_review,
                   .project = &m_project}};
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

TEST(AiExclude, GlobsMatchLikeGitignore)
{
    EXPECT_TRUE(globMatches(".env*", ".env"));
    EXPECT_TRUE(globMatches(".env*", ".ENV.local"));
    EXPECT_FALSE(globMatches("*.key", "a/b.key"));
    EXPECT_TRUE(globMatches("**.key", "a/b.key"));
    EXPECT_TRUE(globMatches("a?c", "abc"));
    EXPECT_FALSE(globMatches("a?c", "a/c"));
    const std::vector<std::string> defaults(std::begin(kAiExcludeDefaults), std::end(kAiExcludeDefaults));
    for (const auto &[path, folder] : std::vector<std::pair<std::string, bool>>{
             {".env", false}, {"show/.env.local", false}, {"keys/tls.key", false}, {".git", true}, {".git/config", false},
             {"sub/.git/HEAD", false}, {".hikari/project.local.toml", false}, {".hikari/credentials.toml", false}})
        EXPECT_TRUE(excludedPath(path, folder, defaults)) << path;
    for (const auto &[path, folder] : std::vector<std::pair<std::string, bool>>{
             {"notes.txt", false}, {".gitignore", false}, {".git", false}, {".hikari/project.toml", false},
             {"sub/.hikari/project.local.toml", false}, {"keys", true}})
        EXPECT_FALSE(excludedPath(path, folder, defaults)) << path;
}

namespace {

class AiFolder : public FolderGrants {
protected:
    void SetUp() override
    {
        FolderGrants::SetUp();
        for (const char *file : {".env", ".env.local", "keys/tls.key", ".git/config", ".hikari/project.local.toml",
                                 ".hikari/credentials.toml", ".hikari/project.toml", "drafts/x.txt", "old.bak",
                                 "raw/take1.WAV", "raw/ep01.mkv"}) {
            fs::create_directories((m_data / file).parent_path());
            std::ofstream(m_data / file) << "x";
        }
        m_caller.reviewed = true;
        grant(false);
    }
    std::string reason(std::string_view operation, std::string_view params)
    {
        const Result r = call(operation, params);
        const Json *why = r ? nullptr : r.error().data.find("reason");
        return r ? std::string("ok") : why ? why->asString() : r.error().code;
    }
    std::vector<std::string> listed(std::string_view path)
    {
        const Result r = call("files.list", R"({"folder": "d1", "path": ")" + std::string(path) + R"("})");
        std::vector<std::string> out;
        if (r)
            for (const Json &entry : r->find("entries")->asArray())
                out.push_back(entry.find("path")->asString());
        return out;
    }
};

}

TEST_F(AiFolder, TheDefaultsAndTheUsersAndTheProjectsGlobsAreNeitherListedNorRead)
{
    m_settings.set("ai.exclude", std::vector<std::string>{"*.bak"});
    m_project.excludes = {"drafts/"};
    EXPECT_EQ(listed(""), (std::vector<std::string>{".hikari", "keys", "notes", "raw"}));
    EXPECT_EQ(listed(".hikari"), std::vector<std::string>{".hikari/project.toml"});
    EXPECT_EQ(listed("keys"), std::vector<std::string>{});
    for (const char *path : {".env", ".env.local", "keys/tls.key", ".git/config", ".hikari/project.local.toml",
                             ".hikari/credentials.toml", "drafts/x.txt", "old.bak"})
        EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": ")" + std::string(path) + R"("})"), "excluded") << path;
    EXPECT_EQ(reason("files.list", R"({"folder": "d1", "path": ".git"})"), "excluded");
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": ".hikari/project.toml"})"), "ok");
    m_caller.reviewed = false;
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": ".env"})"), "ok");
}

TEST_F(AiFolder, AProjectsExcludeCannotRemoveADefault)
{
    m_project.excludes = {"!.env*", ""};
    m_settings.set("ai.exclude", std::vector<std::string>{"!*.key"});
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": ".env"})"), "excluded");
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": "keys/tls.key"})"), "excluded");
}

TEST_F(AiFolder, ALinkToAnExcludedFileIsRefused)
{
    if (!link(m_data / ".env", m_data / "settings.txt"))
        GTEST_SKIP() << "this system can't make symbolic links here";
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": "settings.txt"})"), "excluded");
}

TEST_F(AiFolder, MediaIsNeverSentAsBytes)
{
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": "raw/take1.WAV"})"), "media");
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": "raw/ep01.mkv", "encoding": "base64"})"), "media");
    m_caller.reviewed = false;
    EXPECT_EQ(reason("files.read", R"({"folder": "d1", "path": "raw/ep01.mkv", "encoding": "base64"})"), "ok");
}

TEST_F(AiFolder, AnAiCallerCannotChangeAiExclude)
{
    EXPECT_EQ(reason("settings.set", R"({"setting": "ai.exclude", "value": []})"), "private");
    m_caller.reviewed = false;
    EXPECT_EQ(reason("settings.set", R"({"setting": "ai.exclude", "value": ["*.tmp"]})"), "ok");
}

TEST_F(AiFolder, AnAiThreadReadsItsProjectWithoutAGrant)
{
    m_api.revokeFiles(m_caller.id);
    m_caller.project = utf8(m_data);
    const Result held = call("files.list", "{}");
    ASSERT_TRUE(held);
    EXPECT_EQ(*held->find("folders"), *Json::parse(R"([{"folder": "project", "name": "data", "access": "read"}])"));
    const Result read = call("files.read", R"({"folder": "project", "path": "notes/today.txt"})");
    ASSERT_TRUE(read);
    EXPECT_EQ(*read->find("data"), Json("today"));
    EXPECT_EQ(code("files.write", R"({"folder": "project", "path": "notes/today.txt", "data": "x"})"), "not_granted");
    EXPECT_EQ(*m_files.read(utf8(m_data / "notes/today.txt"), 100), "today");
}

TEST_F(AiFolder, TheProjectFolderKeepsTheExcludesAndTheMediaRule)
{
    m_api.revokeFiles(m_caller.id);
    m_caller.project = utf8(m_data);
    m_project.excludes = {"drafts/"};
    for (const char *path : {".env", "keys/tls.key", ".git/config", ".hikari/credentials.toml", "drafts/x.txt"})
        EXPECT_EQ(reason("files.read", R"({"folder": "project", "path": ")" + std::string(path) + R"("})"), "excluded")
            << path;
    EXPECT_EQ(reason("files.read", R"({"folder": "project", "path": "raw/ep01.mkv"})"), "media");
}

TEST_F(AiFolder, AnUntrustedProjectIsReadOnlyToo)
{
    m_api.revokeFiles(m_caller.id);
    m_project.state = ProjectState{.name = "data", .root = utf8(m_data), .trusted = false};
    m_caller.project = utf8(m_data);
    EXPECT_EQ(code("files.read", R"({"folder": "project", "path": "notes/today.txt"})"), "ok");
    EXPECT_EQ(code("files.write", R"({"folder": "project", "path": "new.txt", "data": "x"})"), "not_granted");
    EXPECT_FALSE(fs::exists(m_data / "new.txt"));
}

TEST_F(AiFolder, AnUntrustedProjectsExcludeOnlyAdds)
{
    m_api.revokeFiles(m_caller.id);
    m_project.state = ProjectState{.name = "data", .root = utf8(m_data), .trusted = false};
    m_caller.project = utf8(m_data);
    m_settings.set("ai.exclude", std::vector<std::string>{"*.bak"});
    m_project.excludes = {"!.env*", "!*.bak", "!.git/", "drafts/"};
    for (const char *path : {".env", ".git/config", "old.bak", "drafts/x.txt"})
        EXPECT_EQ(reason("files.read", R"({"folder": "project", "path": ")" + std::string(path) + R"("})"), "excluded")
            << path;
    EXPECT_EQ(reason("files.read", R"({"folder": "project", "path": ".hikari/project.toml"})"), "ok");
}

TEST_F(AiFolder, OnlyAnAiCallerWithAProjectGetsTheFolder)
{
    m_api.revokeFiles(m_caller.id);
    EXPECT_EQ(code("files.read", R"({"folder": "project", "path": "notes/today.txt"})"), "not_granted");
    m_caller.project = utf8(m_data);
    m_caller.reviewed = false;
    EXPECT_EQ(code("files.read", R"({"folder": "project", "path": "notes/today.txt"})"), "not_granted");
}

class AiWrites : public AiFolder {
protected:
    void SetUp() override
    {
        AiFolder::SetUp();
        m_api.revokeFiles(m_caller.id);
        m_caller.label = "AI";
        grant(true);
    }
};

TEST_F(AiWrites, AnAiWriteStagesAndTheDiskStaysUntilKept)
{
    const Result wrote = call("files.write", R"({"folder": "d2", "path": "notes/today.txt", "data": "tomorrow"})");
    ASSERT_TRUE(wrote);
    EXPECT_EQ(*wrote->find("staged"), Json(true));
    EXPECT_EQ(*m_files.read(utf8(m_data / "notes/today.txt"), 100), "today");
    ASSERT_EQ(m_review.pendingFiles().size(), 1u);
    const auto *staged = m_review.pendingFiles().front();
    EXPECT_EQ(staged->shown, "notes/today.txt");
    EXPECT_EQ(staged->before, std::optional<std::string>("today"));
    EXPECT_EQ(staged->after, "tomorrow");
    EXPECT_EQ(code("files.write", R"({"folder": "d2", "path": "new/later.txt", "data": "later"})"), "ok");
    EXPECT_FALSE(fs::exists(m_data / "new"));
}

TEST_F(AiWrites, TheAiReadsWhatItStagedAndUndoBringsTheDiskBack)
{
    call("files.write", R"({"folder": "d2", "path": "notes/today.txt", "data": "tomorrow"})");
    EXPECT_EQ(call("files.read", R"({"folder": "d2", "path": "notes/today.txt"})")->find("data")->asString(),
              "tomorrow");
    m_review.undoFile(m_review.pendingFiles().front()->id);
    EXPECT_EQ(call("files.read", R"({"folder": "d2", "path": "notes/today.txt"})")->find("data")->asString(), "today");
    m_caller.reviewed = false;
    call("files.write", R"({"folder": "d2", "path": "notes/today.txt", "data": "plugin"})");
    EXPECT_EQ(*m_files.read(utf8(m_data / "notes/today.txt"), 100), "plugin");
}

TEST_F(AiWrites, AWriteToAnOpenDocumentIsRefused)
{
    std::ofstream(m_data / "ep01.ass") << "x";
    m_documents.open("ep01.ass", hikari::api::testing::kAlpha, utf8(m_data / "ep01.ass"));
    EXPECT_EQ(reason("files.write", R"({"folder": "d2", "path": "ep01.ass", "data": "y"})"), "open_document");
    EXPECT_TRUE(m_review.pendingFiles().empty());
}

TEST_F(AiWrites, ExcludedPathsAreNeverStaged)
{
    EXPECT_EQ(reason("files.write", R"({"folder": "d2", "path": ".env", "data": "y"})"), "excluded");
    EXPECT_TRUE(m_review.pendingFiles().empty());
}
