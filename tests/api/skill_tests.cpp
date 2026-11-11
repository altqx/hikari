#include "fake_host.h"
#include "script_client.h"

#include "hikari/api/folder_skills.h"
#include "hikari/api/skill_check.h"

#include <gtest/gtest.h>

#include <fstream>
#include <map>

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

std::vector<std::string> names(const FolderSkills &host)
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
    SkillTiers skills(kSkills);
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

TEST(Skills, TheFileCapKeepsSkillMdAndTheFirstFilesByName)
{
    TempFolder root;
    for (int i = 0; i < 120; ++i)
        put(root.path / "many" / "A" / ("f" + std::to_string(1000 + i)));
    put(root.path / "many" / "SKILL.md");
    const std::vector<SkillFolder> found = FolderSkills(root.path).skills();
    ASSERT_EQ(found.size(), 1u);
    std::vector<std::string> expected;
    for (int i = 0; i < 99; ++i)
        expected.push_back("A/f" + std::to_string(1000 + i));
    expected.push_back("SKILL.md");
    EXPECT_EQ(found[0].files, expected);
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
    SkillTiers skills(root.path);
    Host host = fake.host();
    host.skills = &skills;
    Api api(host);
    ScriptClient client(api);
    const Result result = client.call(aiCaller(), "skills.read", *Json::parse(R"({"skill": "big", "file": "SKILL.md"})"));
    ASSERT_FALSE(result);
    EXPECT_EQ(*result.error().data.find("reason"), Json("too_large"));
}

namespace {

std::vector<std::pair<std::string, SkillOrigin>> tiers(const SkillTiers &skills, const fs::path &project)
{
    std::vector<std::pair<std::string, SkillOrigin>> out;
    for (const SkillFolder &skill : skills.skills(project))
        out.emplace_back(skill.name, skill.origin);
    return out;
}

}

TEST(Skills, AProjectSkillHidesTheUsersOfTheSameName)
{
    TempFolder root;
    const fs::path mine = root.path / "mine";
    const fs::path project = root.path / "show";
    put(mine / "shared" / "SKILL.md", "mine");
    put(mine / "only-mine" / "SKILL.md");
    put(mine / "broken-there" / "SKILL.md", "mine");
    put(project / ".hikari" / "skills" / "shared" / "SKILL.md", "project");
    put(project / ".hikari" / "skills" / "honorifics" / "SKILL.md");
    put(project / ".hikari" / "skills" / "broken-there" / "README.md");
    const SkillTiers skills(mine);
    using enum SkillOrigin;
    EXPECT_EQ(tiers(skills, project), (std::vector<std::pair<std::string, SkillOrigin>>{
                                          {"broken-there", Mine}, {"honorifics", Project}, {"only-mine", Mine}, {"shared", Project}}));
    EXPECT_EQ(skills.read(project, "shared", "SKILL.md", 100), "project");
    EXPECT_EQ(skills.read(project, "broken-there", "SKILL.md", 100), "mine");
    EXPECT_EQ(skills.read({}, "shared", "SKILL.md", 100), "mine");
    EXPECT_FALSE(skills.read({}, "honorifics", "SKILL.md", 100));
    EXPECT_EQ(tiers(skills, {}), (std::vector<std::pair<std::string, SkillOrigin>>{
                                     {"broken-there", Mine}, {"only-mine", Mine}, {"shared", Mine}}));
}

#ifndef _WIN32
TEST(Skills, ASymlinkedProjectSkillsFolderGivesNoProjectSkills)
{
    TempFolder root;
    put(root.path / "elsewhere" / "skills" / "leak" / "SKILL.md");
    fs::create_directories(root.path / "a");
    fs::create_directory_symlink(root.path / "elsewhere", root.path / "a" / ".hikari");
    fs::create_directories(root.path / "b" / ".hikari");
    fs::create_directory_symlink(root.path / "elsewhere" / "skills", root.path / "b" / ".hikari" / "skills");
    const SkillTiers skills(root.path / "mine");
    EXPECT_TRUE(skills.skills(root.path / "a").empty());
    EXPECT_TRUE(skills.skills(root.path / "b").empty());
    EXPECT_FALSE(skills.read(root.path / "a", "leak", "SKILL.md", 100));
}
#endif

TEST(Skills, TheListMarksEachSkillsOriginForTheCallersProject)
{
    TempFolder root;
    put(root.path / "mine" / "notes" / "SKILL.md");
    put(root.path / "show" / ".hikari" / "skills" / "honorifics" / "SKILL.md", "ours");
    FakeHost fake;
    SkillTiers skills(root.path / "mine");
    Host host = fake.host();
    host.skills = &skills;
    Api api(host);
    ScriptClient client(api);
    Caller caller = aiCaller();
    caller.project = (root.path / "show").string();
    const Result listed = client.call(caller, "skills.list", Json::object());
    ASSERT_TRUE(listed);
    EXPECT_EQ(*listed, *Json::parse(R"({"skills": [{"skill": "honorifics", "origin": "project", "files": ["SKILL.md"]},
                                                  {"skill": "notes", "origin": "yours", "files": ["SKILL.md"]}]})"));
    const Result read = client.call(caller, "skills.read", *Json::parse(R"({"skill": "honorifics", "file": "SKILL.md"})"));
    ASSERT_TRUE(read);
    EXPECT_EQ(*read->find("data"), Json("ours"));
    EXPECT_EQ(names(FolderSkills(root.path / "mine")), std::vector<std::string>{"notes"});
}

TEST(Skills, TheCheckAnswersTheCasesSharedWithTheSidecar)
{
    const std::map<std::string, SkillProblem> problems{{"noFrontmatter", SkillProblem::NoFrontmatter},
                                                       {"nameMismatch", SkillProblem::NameMismatch},
                                                       {"noDescription", SkillProblem::NoDescription}};
    std::ifstream in(kScripts / "skill_texts.jsonl", std::ios::binary);
    int cases = 0;
    for (std::string line; std::getline(in, line); ++cases) {
        const auto json = Json::parse(line);
        ASSERT_TRUE(json) << line;
        const std::string &folder = json->find("folder")->asString();
        const SkillCheck check = checkSkill(folder, json->find("text")->asString());
        if (const Json *description = json->find("description")) {
            EXPECT_EQ(check.problem, SkillProblem::None) << folder;
            EXPECT_EQ(check.description, description->asString()) << folder;
        } else {
            EXPECT_EQ(check.problem, problems.at(json->find("problem")->asString())) << folder;
            if (const Json *named = json->find("namedAs"))
                EXPECT_EQ(check.namedAs, named->asString()) << folder;
        }
    }
    EXPECT_EQ(cases, 15);
}

TEST(Skills, TheReportSaysWhyEachFolderIsntOffered)
{
    TempFolder root;
    put(root.path / "Kara FX" / "SKILL.md");
    put(root.path / "glossary" / "README.md");
    put(root.path / "kfx-style" / "SKILL.md", "---\nname: kfx\ndescription: x\n---\n");
    put(root.path / "notes" / "SKILL.md", "---\nname: notes\n---\n");
    put(root.path / "binary" / "SKILL.md", std::string("---\ndescription: \xff\n---\n"));
    put(root.path / "good" / "SKILL.md", "---\ndescription: Fine.\n---\n");
    for (int i = 0; i < 120; ++i)
        put(root.path / "good" / "refs" / ("r" + std::to_string(1000 + i)));
    put(root.path / "loose.md");
    const std::vector<SkillReport> report = FolderSkills(root.path).report();
    std::vector<std::pair<std::string, SkillProblem>> got;
    for (const SkillReport &r : report)
        got.emplace_back(r.folder, r.check.problem);
    using enum SkillProblem;
    EXPECT_EQ(got, (std::vector<std::pair<std::string, SkillProblem>>{{"Kara FX", NotASkillName},
                                                                      {"binary", NotText},
                                                                      {"glossary", NoSkillFile},
                                                                      {"good", None},
                                                                      {"kfx-style", NameMismatch},
                                                                      {"notes", NoDescription}}));
    EXPECT_EQ(report[3].check.description, "Fine.");
    EXPECT_EQ(report[3].files, FolderSkills::kMaxFiles);
    EXPECT_EQ(report[3].found, 121u);
    EXPECT_EQ(report[4].check.namedAs, "kfx");
}

TEST(Skills, TheTiersReportTheProjectsFoldersFirst)
{
    TempFolder root;
    put(root.path / "mine" / "a" / "SKILL.md", "---\ndescription: Mine.\n---\n");
    put(root.path / "show" / ".hikari" / "skills" / "z" / "SKILL.md", "---\ndescription: Ours.\n---\n");
    const std::vector<SkillReport> report = SkillTiers(root.path / "mine").report(root.path / "show");
    ASSERT_EQ(report.size(), 2u);
    EXPECT_EQ(report[0].folder, "z");
    EXPECT_EQ(report[0].origin, SkillOrigin::Project);
    EXPECT_EQ(report[1].origin, SkillOrigin::Mine);
}
