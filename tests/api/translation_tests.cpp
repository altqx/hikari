#include "fake_host.h"
#include "script_client.h"

#include <gtest/gtest.h>

using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

class Translation : public ::testing::Test {
protected:
    Translation()
    {
        fake.translation.folders[1] = "/show/ep01";
        fake.files.contents["/show/ep01/GLOSSARY.md"] = "# Glossary\n";
        fake.files.contents["/show/ep01/STYLE.md"] = "# Style\n";
        fake.files.contents["/show/ep01/notes.txt"] = "not a project file";
    }

    Result call(std::string_view operation, Json params = Json::object())
    {
        Host host = fake.host();
        Api api(host);
        ScriptClient client(api);
        return client.call(aiCaller(), operation, std::move(params));
    }

    FakeHost fake;
};

}

TEST_F(Translation, TheProjectGivesOnlyHonyasFilesThatExistAndNeverThePath)
{
    const Result result = call("translation.project", Json::object({{"document", 1}}));
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->dump(), Json::object({{"folder", "ep01"},
                                            {"files", Json::array({Json::object({{"name", "GLOSSARY.md"}, {"data", "# Glossary\n"}}),
                                                                   Json::object({{"name", "STYLE.md"}, {"data", "# Style\n"}})})}})
                                  .dump());
}

TEST_F(Translation, ADocumentWithoutAProjectFolderHasNone)
{
    const Result result = call("translation.project", Json::object({{"document", 2}}));
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_TRUE(result->find("folder")->isNull());
    EXPECT_TRUE(result->find("files")->asArray().empty());
    const Result write = call("translation.project_write", Json::object({{"document", 2}, {"file", "GLOSSARY.md"}, {"data", "x"}}));
    ASSERT_FALSE(write);
    EXPECT_EQ(write.error().code, errors::kNotFound);
}

TEST_F(Translation, AWriteReplacesOneProjectFileAndNothingElse)
{
    const Result result = call("translation.project_write", Json::object({{"document", 1}, {"file", "CHARACTERS.md"}, {"data", "# C\n"}}));
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(fake.files.contents.at("/show/ep01/CHARACTERS.md"), "# C\n");
    const Result other = call("translation.project_write", Json::object({{"document", 1}, {"file", "notes.txt"}, {"data", "x"}}));
    ASSERT_FALSE(other);
    EXPECT_EQ(other.error().code, errors::kInvalidParams);
    EXPECT_EQ(fake.files.contents.at("/show/ep01/notes.txt"), "not a project file");
}

TEST_F(Translation, AFailedWriteIsRefused)
{
    fake.translation.folders[1] = "/locked/show";
    const Result result = call("translation.project_write", Json::object({{"document", 1}, {"file", "GLOSSARY.md"}, {"data", "x"}}));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kRefused);
}

TEST_F(Translation, WithoutTheFolderPortProjectsAreUnavailable)
{
    Host host = fake.host();
    host.translation = nullptr;
    Api api(host);
    ScriptClient client(api);
    const Result result = client.call(aiCaller(), "translation.project", Json::object());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kUnavailable);
}
