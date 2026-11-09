#include "hikari/backends/ai_sidecar_host.h"

#include "fake_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

using namespace hikari::backends::ai;
using hikari::api::Json;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

std::string textOf(const Json &result)
{
    std::string out;
    for (const Json &c : result.find("content")->asArray())
        if (const Json *text = c.find("text"))
            out += text->asString();
    return out;
}

bool isError(const Json &result)
{
    const Json *error = result.find("isError");
    return error && error->asBool();
}

// The real hikari-ai over the real Hikari API, as an agent in Hikari uses it: no provider, so no network.
class AiToolsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_tools_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override
    {
        fake.api = &api;
        AiSidecarHost::Options o;
        o.program = QStringLiteral(HIKARI_AI_RUNTIME_FILE);
        o.arguments = {QStringLiteral(HIKARI_AI_BUNDLE_FILE)};
        o.workingDirectory = dir.filePath(QStringLiteral("work"));
        o.buildId = HIKARI_AI_BUILD_ID;
        o.handshakeMs = 30'000;
        sidecar = std::make_unique<AiSidecarHost>(std::move(o), &api, hikari::api::Caller{"ai", "Hikari AI", false});
    }

    void TearDown() override { sidecar->shutdown(); }

    Json request(const std::string &method, Json params)
    {
        std::optional<SidecarResult> result;
        sidecar->request(method, std::move(params), [&](SidecarResult r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); })) << method;
        if (!result || !*result) {
            ADD_FAILURE() << method << ": " << (result ? result->error().message : std::string("no reply"));
            return Json::object({{"content", Json::array()}});
        }
        return **result;
    }

    Json tool(const std::string &name, Json arguments = Json::object())
    {
        return request("_hikari/tools.call", Json::object({{"name", Json(name)}, {"arguments", std::move(arguments)}}));
    }

    Json answer(const std::string &name, Json arguments = Json::object())
    {
        const Json result = tool(name, std::move(arguments));
        EXPECT_FALSE(isError(result)) << name << ": " << textOf(result);
        return Json::parse(textOf(result)).value_or(Json());
    }

    QTemporaryDir dir;
    hikari::api::testing::FakeHost fake;
    hikari::api::Api api{fake.host()};
    std::unique_ptr<AiSidecarHost> sidecar;
};

TEST_F(AiToolsTest, EveryToolCarriesItsOperationsReadOrWriteMarkFromHa1)
{
    const Json listed = request("_hikari/tools.list", Json::object({{"all", Json(true)}}));
    const auto &tools = listed.find("tools")->asArray();
    std::size_t checked = 0;
    for (const hikari::api::Operation &op : hikari::api::Api::registry().operations()) {
        std::string name = op.name == "commands.run" ? "run_command" : op.name;
        std::replace(name.begin(), name.end(), '.', '_');
        const auto tool = std::find_if(tools.begin(), tools.end(), [&](const Json &t) { return *t.find("name") == Json(name); });
        if (tool == tools.end())
            continue;
        EXPECT_EQ(tool->find("annotations")->find("readOnlyHint")->asBool(), op.access == hikari::api::Access::Read) << name;
        ++checked;
    }
    EXPECT_GE(checked, 25u);

    const Json offered = request("_hikari/tools.list", Json::object());
    EXPECT_GE(offered.find("tools")->asArray().size(), 15u);
    EXPECT_LE(offered.find("tools")->asArray().size(), 25u);
}

TEST_F(AiToolsTest, AScriptedAgentReadsEditsAndRunsACommandThroughTheTools)
{
    const Json documents = answer("documents_list");
    ASSERT_EQ(documents.find("documents")->asArray().size(), 1u);

    const Json found = answer("lines_find", *Json::parse(R"({"text": "Kenobi", "fields": ["text"]})"));
    ASSERT_EQ(*found.find("total"), Json(1));
    const Json line = *found.find("lines")->asArray()[0].find("id");
    const Json revision = *answer("documents_get").find("revision");

    const auto apply = [&](const Json &at) {
        return tool("document_apply", Json::object({{"revision", at}, {"name", "Finish the greeting"},
            {"edits", Json::array({Json::object({{"op", "set_line"}, {"line", line},
                {"fields", Json::object({{"text", "Hello there, General Kenobi"}})}})})}}));
    };
    const Json applied = apply(revision);
    ASSERT_FALSE(isError(applied)) << textOf(applied);
    EXPECT_EQ(fake.target()->document().lines()[2]->text, u8"Hello there, General Kenobi");
    EXPECT_EQ(fake.commits, 1);

    const Json stale = apply(revision);
    EXPECT_TRUE(isError(stale));
    EXPECT_NE(textOf(stale).find("documents_get"), std::string::npos) << textOf(stale);
    EXPECT_EQ(fake.commits, 1);

    const Json commands = answer("search_commands", *Json::parse(R"({"query": "save subs"})"));
    ASSERT_GE(commands.find("commands")->asArray().size(), 1u);
    EXPECT_EQ(*commands.find("commands")->asArray()[0].find("command"), Json("GLOBAL_SAVE_SUBS"));
    answer("run_command", *Json::parse(R"({"command": "GLOBAL_SAVE_SUBS"})"));
    EXPECT_EQ(fake.ran, std::vector<std::string>{"GLOBAL_SAVE_SUBS"});
}

TEST_F(AiToolsTest, SearchToolsOffersADeferredToolThatAnswersAnImage)
{
    const Json found = answer("search_tools", *Json::parse(R"({"query": "video frame"})"));
    EXPECT_EQ(*found.find("tools")->asArray()[0].find("name"), Json("video_frame"));
    const Json offered = request("_hikari/tools.list", Json::object());
    const auto &tools = offered.find("tools")->asArray();
    EXPECT_TRUE(std::any_of(tools.begin(), tools.end(), [](const Json &t) { return *t.find("name") == Json("video_frame"); }));

    const Json frame = tool("video_frame", *Json::parse(R"({"frame": 4, "maxEdge": 64})"));
    ASSERT_FALSE(isError(frame)) << textOf(frame);
    const Json &image = frame.find("content")->asArray().at(1);
    EXPECT_EQ(*image.find("type"), Json("image"));
    EXPECT_EQ(*image.find("mimeType"), Json("image/png"));
    EXPECT_TRUE(image.find("data")->asString().starts_with("iVBORw0KGgo"));
}

TEST_F(AiToolsTest, ToolsReachTheActiveDocumentAndTheAttachedOnesOnly)
{
    const Json refused = tool("lines_list", *Json::parse(R"({"document": 2})"));
    EXPECT_TRUE(isError(refused));
    EXPECT_NE(textOf(refused).find("attach this one with @"), std::string::npos) << textOf(refused);

    sidecar->setAttachedDocuments({hikari::application::DocumentId{2}});
    const Json lines = answer("lines_list", *Json::parse(R"({"document": 2, "fields": ["text"]})"));
    EXPECT_EQ(*lines.find("lines")->asArray()[0].find("text"), Json("other"));
    EXPECT_EQ(answer("documents_list").find("documents")->asArray().size(), 2u);
}

TEST_F(AiToolsTest, AnUnknownToolAndBadArgumentsComeBackWordedForTheModel)
{
    const Json unknown = tool("format_disk");
    EXPECT_TRUE(isError(unknown));
    EXPECT_EQ(textOf(unknown), "No tool is named \"format_disk\". search_tools finds more tools.");
    const Json bad = tool("lines_get", *Json::parse(R"({"ids": "three"})"));
    EXPECT_TRUE(isError(bad));
    EXPECT_NE(textOf(bad).find("Check the tool's parameters"), std::string::npos) << textOf(bad);
}

}
