#include "fake_host.h"
#include "script_client.h"

#include "hikari/api/folder_skills.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace hikari::api;
using namespace hikari::api::testing;

namespace fs = std::filesystem;

namespace {

const fs::path kScripts = HIKARI_API_SCRIPTS_DIR;
const fs::path kSkills = HIKARI_API_SKILLS_DIR;

void put(const fs::path &file, std::string_view text = "---\nname: x\ndescription: x\n---\n")
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

class TempFolder {
public:
    TempFolder()
        : path(fs::temp_directory_path() /
               ("hikari-skills-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempFolder() { fs::remove_all(path); }
    const fs::path path;
};

std::vector<std::string> names(const SkillHost &host)
{
    std::vector<std::string> out;
    for (const SkillFolder &skill : host.skills())
        out.push_back(skill.name);
    return out;
}

}

TEST(Skills, Script)
{
    FakeHost fake;
    FolderSkills skills(kSkills);
    Host host = fake.host();
    host.skills = &skills;
    Api api(host);
    ScriptClient client(api);
    EXPECT_GT(client.run(kScripts / "skills.jsonl"), 0);
}

TEST(Skills, WithoutAFolderTheyAreUnavailable)
{
    FakeHost fake;
    Host host = fake.host();
    host.skills = nullptr;
    Api api(host);
    ScriptClient client(api);
    const Result result = client.call(aiCaller(), "skills.list", Json::object());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kUnavailable);
}

TEST(Skills, NamesFollowTheAgentSkillsFormat)
{
    for (const std::string_view good : {"a", "ass-typesetting", "k2", "a1-b2-c3"})
        EXPECT_TRUE(isSkillName(good)) << good;
    for (const std::string_view bad : {"", "-a", "a-", "a--b", "A", "a_b", "a.b", "a b", "é"})
        EXPECT_FALSE(isSkillName(bad)) << bad;
    EXPECT_TRUE(isSkillName(std::string(64, 'a')));
    EXPECT_FALSE(isSkillName(std::string(65, 'a')));
}

TEST(Skills, HiddenEntriesAndFoldersWithoutSkillMdAreLeftOut)
{
    TempFolder root;
    put(root.path / "b-skill" / "SKILL.md");
    put(root.path / "b-skill" / ".secret");
    put(root.path / "b-skill" / ".git" / "config");
    put(root.path / "a-skill" / "SKILL.md");
    put(root.path / ".hidden" / "SKILL.md");
    put(root.path / "notes" / "README.md");
    put(root.path / "loose.md");
    const FolderSkills skills(root.path);
    EXPECT_EQ(names(skills), (std::vector<std::string>{"a-skill", "b-skill"}));
    EXPECT_EQ(skills.skills()[1].files, std::vector<std::string>{"SKILL.md"});
    EXPECT_FALSE(skills.read("b-skill", ".secret", 100));
}

TEST(Skills, FilesStopAtTheDepthAndCountLimits)
{
    TempFolder root;
    put(root.path / "deep" / "SKILL.md");
    put(root.path / "deep" / "a" / "b" / "c" / "kept.md");
    put(root.path / "deep" / "a" / "b" / "c" / "d" / "dropped.md");
    for (int i = 0; i < 120; ++i)
        put(root.path / "many" / "files" / ("f" + std::to_string(1000 + i)));
    put(root.path / "many" / "SKILL.md");
    const FolderSkills skills(root.path);
    const std::vector<SkillFolder> found = skills.skills();
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0].files, (std::vector<std::string>{"SKILL.md", "a/b/c/kept.md"}));
    EXPECT_FALSE(skills.read("deep", "a/b/c/d/dropped.md", 100));
    EXPECT_EQ(found[1].files.size(), FolderSkills::kMaxFiles);
}

TEST(Skills, ReadsOnlyListedFilesAndStopsAtTheLimit)
{
    TempFolder root;
    put(root.path / "s" / "SKILL.md", "0123456789");
    put(root.path / "outside.md", "secret");
    const FolderSkills skills(root.path);
    EXPECT_EQ(skills.read("s", "SKILL.md", 4), "0123");
    EXPECT_FALSE(skills.read("s", "../outside.md", 100));
    EXPECT_FALSE(skills.read("s", "./SKILL.md", 100));
    EXPECT_FALSE(skills.read("..", "outside.md", 100));
}

#ifndef _WIN32
TEST(Skills, SymlinksAreNotFollowed)
{
    TempFolder root;
    put(root.path / "outside" / "secret.md", "secret");
    put(root.path / "real" / "SKILL.md");
    fs::create_directory_symlink(root.path / "outside", root.path / "real" / "linked");
    fs::create_symlink(root.path / "outside" / "secret.md", root.path / "real" / "secret.md");
    fs::create_directory_symlink(root.path / "real", root.path / "alias");
    const FolderSkills skills(root.path);
    EXPECT_EQ(names(skills), std::vector<std::string>{"real"});
    EXPECT_EQ(skills.skills()[0].files, std::vector<std::string>{"SKILL.md"});
    EXPECT_FALSE(skills.read("real", "secret.md", 100));
}
#endif

TEST(Skills, AtMostAHundredSkillsAreListed)
{
    TempFolder root;
    for (int i = 0; i < 105; ++i)
        put(root.path / ("s" + std::to_string(1000 + i)) / "SKILL.md");
    EXPECT_EQ(FolderSkills(root.path).skills().size(), FolderSkills::kMaxSkills);
}

TEST(Skills, AFileOver256KiBIsRefused)
{
    TempFolder root;
    put(root.path / "big" / "SKILL.md", std::string(256 * 1024 + 1, 'a'));
    FakeHost fake;
    FolderSkills skills(root.path);
    Host host = fake.host();
    host.skills = &skills;
    Api api(host);
    ScriptClient client(api);
    const Result result = client.call(aiCaller(), "skills.read", *Json::parse(R"({"skill": "big", "file": "SKILL.md"})"));
    ASSERT_FALSE(result);
    EXPECT_EQ(*result.error().data.find("reason"), Json("too_large"));
}
