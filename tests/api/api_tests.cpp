#include "fake_host.h"
#include "script_client.h"

#include "hikari/api/schema.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <regex>
#include <set>

using namespace hikari::api;
using namespace hikari::api::testing;

namespace {

const std::filesystem::path kScripts = HIKARI_API_SCRIPTS_DIR;

TEST(ApiRegistry, EveryOperationIsWellFormed)
{
    const Registry &registry = Api::registry();
    ASSERT_FALSE(registry.operations().empty());
    for (const Json::Member &def : registry.defs().asObject())
        EXPECT_FALSE(checkSchema(def.value, registry.defs())) << def.key << ": " << *checkSchema(def.value, registry.defs());
    const std::regex name("[a-z]+(\\.[a-z_]+)+");
    std::set<std::string> seen;
    for (const Operation &op : registry.operations()) {
        EXPECT_TRUE(std::regex_match(op.name, name)) << op.name;
        EXPECT_TRUE(seen.insert(op.name).second) << op.name;
        EXPECT_FALSE(op.summary.empty()) << op.name;
        EXPECT_TRUE(op.handler) << op.name;
        EXPECT_GE(op.since, 1) << op.name;
        EXPECT_LE(op.since, std::max(kApiVersion, 1)) << op.name;
        for (const Json *schema : {&op.params, &op.result}) {
            EXPECT_FALSE(checkSchema(*schema, registry.defs())) << op.name << ": " << *checkSchema(*schema, registry.defs());
            ASSERT_TRUE(schema->find("type")) << op.name;
            EXPECT_EQ(schema->find("type")->asString(), "object") << op.name;
        }
        EXPECT_TRUE(op.params.find("additionalProperties") && *op.params.find("additionalProperties") == Json(false))
            << op.name << " must refuse unknown parameters";
    }
}

TEST(ApiRegistry, ReadsAreMarkedRead)
{
    for (const char *name : {"documents.list", "documents.get", "lines.list", "lines.get", "lines.find", "styles.list",
                             "info.list", "selection.get", "commands.list", "panels.list", "settings.list",
                             "settings.get", "api.version"}) {
        const Operation *op = Api::registry().find(name);
        ASSERT_TRUE(op) << name;
        EXPECT_EQ(op->access, Access::Read) << name;
    }
}

class ApiScripts : public ::testing::Test {
protected:
    FakeHost host;
    Api api{host.host()};
    ScriptClient client{api};
};

TEST_F(ApiScripts, Reads)
{
    EXPECT_GT(client.run(kScripts / "reads.jsonl"), 0);
}

TEST_F(ApiScripts, Scope)
{
    EXPECT_GT(client.run(kScripts / "scope.jsonl"), 0);
}

TEST_F(ApiScripts, Errors)
{
    EXPECT_GT(client.run(kScripts / "errors.jsonl"), 0);
}

TEST_F(ApiScripts, EveryOperationAnswersEmptyParams)
{
    for (const Operation &op : Api::registry().operations()) {
        const Result result = client.call(aiCaller(), op.name, Json::object());
        if (!result)
            EXPECT_EQ(result.error().code, errors::kInvalidParams) << op.name << ": " << result.error().message;
    }
}

TEST_F(ApiScripts, ReadsChangeNothing)
{
    const auto *session = host.session(hikari::application::DocumentId{1});
    const auto revision = session->revision();
    const auto history = session->historySize();
    const auto selection = session->selection();
    client.run(kScripts / "reads.jsonl");
    EXPECT_EQ(session->revision(), revision);
    EXPECT_EQ(session->historySize(), history);
    EXPECT_EQ(session->selection(), selection);
}

TEST_F(ApiScripts, DocumentsWithoutAHost)
{
    Api bare{Host{}};
    ScriptClient none{bare};
    for (const char *name : {"documents.list", "lines.list", "commands.list", "panels.list", "settings.list"}) {
        const Result result = none.call(aiCaller(), name, Json());
        ASSERT_FALSE(result) << name;
        EXPECT_EQ(result.error().code, errors::kUnavailable) << name;
    }
}

TEST_F(ApiScripts, NoDocumentOpen)
{
    host.workspace_.remove(hikari::application::DocumentId{1});
    host.workspace_.remove(hikari::application::DocumentId{2});
    const Result result = client.call(aiCaller(), "lines.list", Json());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kNotFound);
}

TEST_F(ApiScripts, TheDraftIsReportedNotCommitted)
{
    auto *session = host.session(hikari::application::DocumentId{1});
    ASSERT_TRUE(session->editDraftText(hikari::core::LineId{3}, u8"typing"));
    const Result summary = client.call(aiCaller(), "documents.get", Json());
    ASSERT_TRUE(summary);
    EXPECT_EQ(*summary->find("draftLine"), Json(3));
    const Result line = client.call(aiCaller(), "lines.get", *Json::parse(R"({"ids": [3], "fields": ["text"]})"));
    ASSERT_TRUE(line);
    EXPECT_EQ(line->find("lines")->asArray()[0].find("text")->asString(), "General Kenobi");
    EXPECT_TRUE(session->draftLine());
}

}
