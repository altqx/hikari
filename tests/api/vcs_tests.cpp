#include "fake_host.h"
#include "script_client.h"

#include <gtest/gtest.h>

using namespace hikari;
using namespace hikari::api;
using namespace hikari::api::testing;
using application::VcsFailure;
using application::VcsProblem;

namespace {

class FakeProject final : public ProjectHost {
public:
    std::optional<ProjectState> project() const override { return state; }
    std::vector<std::string> aiExclude(const std::string &) const override { return {}; }
    std::optional<ProjectState> state = ProjectState{"Show", "/work/show", true};
};

Caller chains(bool vcsRead = true)
{
    Caller caller{"plugin:example.chains", "Chains", false};
    caller.vcsRead = vcsRead;
    return caller;
}

class Vcs : public ::testing::Test {
protected:
    Result call(std::string_view operation, std::string_view params, const Caller &caller = chains())
    {
        auto parsed = Json::parse(params);
        EXPECT_TRUE(parsed) << params;
        return client.call(caller, operation, *parsed);
    }

    Host wired()
    {
        Host h = host.host();
        h.project = &project;
        return h;
    }

    FakeHost host;
    FakeVcs &vcs = host.vcs;
    FakeProject project;
    Api api{wired()};
    ScriptClient client{api};
};

std::string reasonOf(const Result &result)
{
    if (result)
        return "ok";
    const Json *reason = result.error().data.find("reason");
    return result.error().code + "/" + (reason ? reason->asString() : "");
}

TEST_F(Vcs, ANonOfficialPluginWithVcsReadReachesBothOperations)
{
    const Result refs = call("vcs.refs", R"({"limit": 2})");
    ASSERT_TRUE(refs) << refs.error().message;
    EXPECT_EQ(refs->find("root")->asString(), "/subs");
    EXPECT_EQ(refs->find("path")->asString(), "season1/a.ass");
    EXPECT_EQ(refs->find("head")->find("ref")->asString(), "main");
    EXPECT_TRUE(refs->find("branches")->asArray().at(0).find("current")->asBool());
    EXPECT_EQ(refs->find("tags")->asArray().at(0).find("commit")->asString(), "c1");
    EXPECT_EQ(refs->find("commits")->asArray().at(1).find("author")->asString(), "Bo");

    const Result shown = call("vcs.show", R"({"ref": "v1"})");
    ASSERT_TRUE(shown) << shown.error().message;
    EXPECT_EQ(shown->find("ref")->asString(), "v1");
    EXPECT_EQ(shown->find("commit")->asString(), "c1");
    EXPECT_EQ(shown->find("text")->asString(), "[Script Info]\n");
    EXPECT_EQ(vcs.asked, (std::vector<std::string>{"refs /subs 2", "show /subs/a.ass v1"}));
}

TEST_F(Vcs, OnlyAPluginWhoseManifestAsksForVcsReadIsGranted)
{
    EXPECT_EQ(reasonOf(call("vcs.refs", "{}", chains(false))), "not_granted/");
    EXPECT_EQ(reasonOf(call("vcs.show", R"({"ref": "HEAD"})", chains(false))), "not_granted/");
    Caller ai = aiCaller();
    ai.vcsRead = true;
    EXPECT_EQ(reasonOf(call("vcs.refs", "{}", ai)), "not_granted/");
    EXPECT_EQ(reasonOf(call("vcs.show", R"({"ref": "HEAD"})", ai)), "not_granted/");
    EXPECT_TRUE(vcs.asked.empty());
}

TEST_F(Vcs, AnUnsavedDocumentListsTheProjectsRepositoryButHasNoFileToShow)
{
    const Result refs = call("vcs.refs", R"({"document": 2})", [] {
        Caller c = chains();
        c.allDocuments = true;
        return c;
    }());
    ASSERT_TRUE(refs) << refs.error().message;
    EXPECT_TRUE(refs->find("path")->isNull());
    EXPECT_EQ(vcs.asked, std::vector<std::string>{"refs /work/show 50"});

    Caller all = chains();
    all.allDocuments = true;
    EXPECT_EQ(reasonOf(call("vcs.show", R"({"document": 2, "ref": "HEAD"})", all)), "not_found/no_path");
    project.state.reset();
    EXPECT_EQ(reasonOf(call("vcs.refs", R"({"document": 2})", all)), "not_found/no_path");
}

TEST_F(Vcs, ARefThatCouldBeAnOptionOrAPathIsRefusedBeforeGitRuns)
{
    for (const char *ref : {"", "-p", "--output=x", "HEAD:a.ass", "a b", "a\nb", ":/fix"})
        EXPECT_EQ(reasonOf(client.call(chains(), "vcs.show", Json::object({{"ref", Json(ref)}}))), "invalid_params/") << ref;
    EXPECT_EQ(reasonOf(call("vcs.show", R"({"ref": "HEAD~2"})")), "ok");
    EXPECT_EQ(reasonOf(call("vcs.show", R"({"ref": "@{1}"})")), "ok");
    EXPECT_EQ(vcs.asked.size(), 2u);
}

TEST_F(Vcs, EachGitProblemKeepsItsOwnReason)
{
    const std::pair<VcsProblem, std::string> cases[] = {
        {VcsProblem::NoGit, "unavailable/no_git"},
        {VcsProblem::TimedOut, "unavailable/timed_out"},
        {VcsProblem::NoRepository, "not_found/no_repository"},
        {VcsProblem::UnsafeRepository, "refused/unsafe_repository"},
        {VcsProblem::NoRef, "not_found/no_ref"},
        {VcsProblem::NoFile, "not_found/no_file"},
        {VcsProblem::TooLarge, "refused/too_large"},
        {VcsProblem::NotText, "refused/not_text"},
        {VcsProblem::Failed, "unavailable/failed"},
    };
    for (const auto &[problem, reason] : cases) {
        vcs.problem = VcsFailure{problem, "why"};
        const Result shown = call("vcs.show", R"({"ref": "HEAD"})");
        EXPECT_EQ(reasonOf(shown), reason);
        EXPECT_EQ(shown ? "" : shown.error().message, "why");
        EXPECT_EQ(reasonOf(call("vcs.refs", "{}")), reason);
    }
}

TEST(VcsUnwired, WithoutGitTheOperationsAreUnavailable)
{
    FakeHost host;
    Host unwired = host.host();
    unwired.vcs = nullptr;
    Api api{unwired};
    ScriptClient client{api};
    EXPECT_EQ(reasonOf(client.call(chains(), "vcs.refs", Json::object())), "unavailable/");
}

}
