#include "fake_host.h"
#include "script_client.h"

#include "hikari/api/schema.h"
#include "hikari/regex/regex.h"

#include <gtest/gtest.h>

#include <algorithm>
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
    const auto name = *hikari::regex::Pattern::compile("[a-z]+(\\.[a-z_]+)+");
    std::set<std::string> seen;
    for (const Operation &op : registry.operations()) {
        EXPECT_TRUE(name.match(op.name)) << op.name;
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
                             "settings.get", "api.version", "video.info", "video.frame_at_time",
                             "video.time_of_frame", "video.frame", "audio.info", "audio.peaks", "files.list",
                             "files.read", "nodes.catalogue", "nodes.validate"}) {
        const Operation *op = Api::registry().find(name);
        ASSERT_TRUE(op) << name;
        EXPECT_EQ(op->access, Access::Read) << name;
    }
}

TEST(ApiRegistry, WritesAreMarkedWrite)
{
    for (const char *name : {"document.apply", "selection.set", "commands.run", "panels.open", "panels.close",
                             "panels.focus", "settings.set", "files.write"}) {
        const Operation *op = Api::registry().find(name);
        ASSERT_TRUE(op) << name;
        EXPECT_EQ(op->access, Access::Write) << name;
    }
}

TEST(ApiRegistry, EveryEventIsWellFormed)
{
    const Registry &registry = Api::registry();
    std::set<std::string> names;
    for (const EventType &event : registry.events()) {
        names.insert(event.name);
        EXPECT_FALSE(event.summary.empty()) << event.name;
        EXPECT_FALSE(checkSchema(event.data, registry.defs())) << event.name;
    }
    EXPECT_EQ(names, (std::set<std::string>{"document.changed", "document.saved", "selection.changed",
                                            "documents.changed", "panel.changed", "command.ran", "project.changed",
                                            "macro.ran"}));
}

class ApiScripts : public ::testing::Test {
protected:
    ApiScripts() { host.api = &api; }

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

TEST_F(ApiScripts, Writes)
{
    EXPECT_GT(client.run(kScripts / "writes.jsonl"), 0);
}

TEST_F(ApiScripts, WriteScope)
{
    EXPECT_GT(client.run(kScripts / "scope_writes.jsonl"), 0);
}

TEST_F(ApiScripts, EveryOperationAnswersEmptyParams)
{
    Caller plugin = pluginCaller();
    plugin.vcsRead = true;
    for (const Operation &op : Api::registry().operations()) {
        const Result asPlugin = client.call(plugin, op.name, Json::object());
        const Result result = client.call(aiCaller(), op.name, Json::object());
        if (!result && !(asPlugin && result.error().code == errors::kNotGranted)) {
            EXPECT_EQ(result.error().code, errors::kInvalidParams) << op.name << ": " << result.error().message;
        }
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

Json params(std::string_view text)
{
    auto value = Json::parse(text);
    EXPECT_TRUE(value) << text;
    return value ? *value : Json();
}

std::string documentText(const hikari::application::EditSession &session)
{
    std::string out;
    for (const auto *line : session.document().lines())
        out += std::string(line->text.begin(), line->text.end()) + "|" + std::to_string(line->start.value.microseconds()) + "\n";
    return out;
}

Json applyAt(std::uint64_t revision, std::string_view edits)
{
    return params(R"({"revision": )" + std::to_string(revision) + R"(, "name": "Batch", "edits": )" + std::string(edits) + "}");
}

TEST_F(ApiScripts, OneWriteIsOneUndoStep)
{
    auto *session = host.target();
    const std::string before = documentText(*session);
    const auto steps = session->historySize();
    const Result applied = client.call(aiCaller(), "document.apply",
                                       applyAt(session->revision(), R"([
        {"op": "set_line", "line": 1, "fields": {"text": "one"}},
        {"op": "set_line", "line": 2, "fields": {"start": 0}},
        {"op": "insert_line", "before": 1, "fields": {"text": "zero"}},
        {"op": "delete_line", "line": 3},
        {"op": "set_info", "key": "Title", "value": "t"}])"));
    ASSERT_TRUE(applied) << applied.error().message;
    EXPECT_EQ(session->historySize(), steps + 1);
    EXPECT_EQ(session->history().back().name, "Test AI: Batch");
    EXPECT_EQ(host.commits, 1);
    EXPECT_NE(documentText(*session), before);
    ASSERT_TRUE(client.call(aiCaller(), "commands.run", params(R"({"command": "GLOBAL_UNDO"})")));
    EXPECT_EQ(documentText(*session), before);
    EXPECT_EQ(session->document().scriptInfo(u8"Title"), std::optional<std::u8string>(u8"alpha"));
    EXPECT_FALSE(session->canUndo());
}

TEST_F(ApiScripts, AReviewedCallersWriteWaitsInTheReviewAndAPluginsDoesNot)
{
    auto *session = host.target();
    const hikari::application::DocumentId target = *host.workspace().editingTarget();
    Caller reviewed = aiCaller();
    reviewed.reviewed = true;
    ASSERT_TRUE(client.call(reviewed, "document.apply",
                            applyAt(session->revision(), R"([{"op": "set_line", "line": 1, "fields": {"text": "one"}}])")));
    const auto changes = host.review.changes(target);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0]->caller, "Test AI");
    EXPECT_EQ(changes[0]->name, "Batch");
    EXPECT_EQ(host.review.pendingLines(target, session->document()), 1u);

    ASSERT_TRUE(client.call(pluginCaller(), "document.apply",
                            applyAt(session->revision(), R"([{"op": "set_line", "line": 2, "fields": {"text": "two"}}])")));
    EXPECT_EQ(host.review.changes(target).size(), 1u);
}

TEST_F(ApiScripts, AnAiCallerCannotReadPrivacyOrRecentSettingsAndAPluginCan)
{
    Caller ai = aiCaller();
    ai.reviewed = true;
    const auto ids = [&](const Caller &caller) {
        std::vector<std::string> out;
        const Result listed = client.call(caller, "settings.list", Json::object());
        EXPECT_TRUE(listed) << listed.error().message;
        for (const Json &entry : listed->find("settings")->asArray())
            out.push_back(entry.find("setting")->asString());
        return out;
    };
    const auto hidden = [](const std::string &id) { return id.starts_with("privacy.") || id.starts_with("recent."); };

    const auto aiIds = ids(ai);
    EXPECT_TRUE(std::none_of(aiIds.begin(), aiIds.end(), hidden));
    EXPECT_NE(std::find(aiIds.begin(), aiIds.end(), "fontCollector.directory"), aiIds.end());
    const auto pluginIds = ids(pluginCaller());
    EXPECT_NE(std::find(pluginIds.begin(), pluginIds.end(), "privacy.usageDataId"), pluginIds.end());
    EXPECT_NE(std::find(pluginIds.begin(), pluginIds.end(), "recent.subtitles"), pluginIds.end());
    EXPECT_TRUE(client.call(ai, "settings.list", params(R"({"prefix": "recent."})"))->find("settings")->asArray().empty());

    for (const char *id : {"privacy.usageDataId", "recent.subtitlesOpened"}) {
        const Json get = Json::object({{"settings", Json::array({Json("grid.copyColumns"), Json(id)})}});
        const Result refused = client.call(ai, "settings.get", get);
        ASSERT_FALSE(refused) << id;
        EXPECT_EQ(refused.error().code, errors::kRefused);
        EXPECT_EQ(*refused.error().data.find("reason"), Json("private"));
        EXPECT_NE(refused.error().message.find(id), std::string::npos);
        EXPECT_TRUE(client.call(pluginCaller(), "settings.get", get)) << id;
    }
    EXPECT_TRUE(client.call(ai, "settings.get", params(R"({"settings": ["fontCollector.directory"]})")));
}

TEST_F(ApiScripts, AStaleRevisionChangesNothing)
{
    auto *session = host.target();
    const auto stale = session->revision();
    ASSERT_TRUE(client.call(aiCaller(), "document.apply",
                            applyAt(stale, R"([{"op": "set_line", "line": 1, "fields": {"text": "first"}}])")));
    const std::string after = documentText(*session);
    const auto steps = session->historySize();
    const Result late = client.call(aiCaller(), "document.apply",
                                    applyAt(stale, R"([{"op": "set_line", "line": 2, "fields": {"text": "late"}}])"));
    ASSERT_FALSE(late);
    EXPECT_EQ(late.error().code, errors::kStaleRevision);
    EXPECT_EQ(*late.error().data.find("current"), Json(session->revision()));
    EXPECT_EQ(documentText(*session), after);
    EXPECT_EQ(session->historySize(), steps);
    EXPECT_EQ(host.commits, 1);
}

TEST_F(ApiScripts, ARefusedEditLeavesTheDocumentAsItWas)
{
    auto *session = host.target();
    const std::string before = documentText(*session);
    const auto revision = session->revision();
    const Result result = client.call(aiCaller(), "document.apply", applyAt(revision, R"([
        {"op": "set_line", "line": 1, "fields": {"text": "changed"}},
        {"op": "delete_style", "name": "Missing"}])"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kNotFound);
    EXPECT_EQ(documentText(*session), before);
    EXPECT_EQ(session->revision(), revision);
    EXPECT_EQ(host.commits, 0);
}

TEST_F(ApiScripts, AMacroHoldsTheDocument)
{
    auto *session = host.target();
    session->setReadOnly(true);
    const Result result = client.call(aiCaller(), "document.apply",
                                      applyAt(session->revision(), R"([{"op": "delete_line", "line": 1}])"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kReadOnly);
    EXPECT_EQ(session->document().lines().size(), 3u);
}

TEST_F(ApiScripts, TheReferenceIsProtected)
{
    ASSERT_TRUE(host.workspace_.setReference(hikari::application::DocumentId{2}));
    auto *reference = host.session(hikari::application::DocumentId{2});
    const Result result = client.call(aiCaller(), "document.apply",
                                      params(R"({"document": 2, "revision": )" + std::to_string(reference->revision()) +
                                             R"(, "name": "X", "edits": [{"op": "delete_line", "line": 1}]})"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, errors::kProtected);
    EXPECT_EQ(reference->document().lines().size(), 1u);
}

TEST_F(ApiScripts, OneWriteAppliesAtATime)
{
    auto *session = host.target();
    std::optional<Result> nested;
    const auto listener = api.subscribe(pluginCaller(), {"document.changed"}, [&](const Event &) {
        if (nested)
            return;
        api.call(pluginCaller(), "document.apply",
                 applyAt(session->revision(), R"([{"op": "delete_line", "line": 2}])"), [&](Result r) { nested = r; });
    });
    ASSERT_NE(listener, 0u);
    ASSERT_TRUE(client.call(aiCaller(), "document.apply",
                            applyAt(session->revision(), R"([{"op": "set_line", "line": 1, "fields": {"text": "a"}}])")));
    ASSERT_TRUE(nested);
    ASSERT_FALSE(*nested);
    EXPECT_EQ(nested->error().code, errors::kBusy);
    EXPECT_EQ(session->document().lines().size(), 3u);
    const Result later = client.call(pluginCaller(), "document.apply",
                                     applyAt(session->revision(), R"([{"op": "delete_line", "line": 2}])"));
    EXPECT_TRUE(later);
    api.unsubscribe(listener);
}

TEST_F(ApiScripts, ADraftOnATouchedLineIsCommittedFirstAndMakesTheWriteStale)
{
    auto *session = host.target();
    const auto revision = session->revision();
    ASSERT_TRUE(session->editDraftText(hikari::core::LineId{3}, u8"typing"));
    const Result untouched = client.call(aiCaller(), "document.apply",
                                         applyAt(revision, R"([{"op": "set_line", "line": 1, "fields": {"text": "x"}}])"));
    ASSERT_TRUE(untouched) << untouched.error().message;
    EXPECT_TRUE(session->draftLine());
    const Result touched = client.call(aiCaller(), "document.apply",
                                       applyAt(session->revision(), R"([{"op": "set_line", "line": 3, "fields": {"text": "y"}}])"));
    ASSERT_FALSE(touched);
    EXPECT_EQ(touched.error().code, errors::kStaleRevision);
    EXPECT_FALSE(session->draftLine());
    EXPECT_EQ(session->document().lines()[2]->text, u8"typing");
}

TEST_F(ApiScripts, SubscribingToAnUnknownEventFails)
{
    EXPECT_EQ(api.subscribe(aiCaller(), {"lines.exploded"}, [](const Event &) {}), 0u);
}

TEST_F(ApiScripts, AListenerMayUnsubscribeWhileHearing)
{
    int heard = 0;
    std::uint64_t self = 0;
    self = api.subscribe(aiCaller(), {"command.ran"}, [&](const Event &) {
        ++heard;
        api.unsubscribe(self);
    });
    api.commandRan("GLOBAL_SAVE_SUBS", true);
    api.commandRan("GLOBAL_SAVE_SUBS", true);
    EXPECT_EQ(heard, 1);
}

TEST_F(ApiScripts, AttachedDocumentsAreVisibleBesidesTheEditingTarget)
{
    Caller attaching = pluginCaller();
    attaching.attached = {hikari::application::DocumentId{2}};
    const Result before = client.call(pluginCaller(), "lines.list", params(R"({"document": 2})"));
    ASSERT_FALSE(before);
    EXPECT_EQ(before.error().code, errors::kNotGranted);
    const Result lines = client.call(attaching, "lines.list", params(R"({"document": 2, "fields": ["text"]})"));
    ASSERT_TRUE(lines) << lines.error().message;
    EXPECT_EQ(lines->find("lines")->asArray()[0].find("text")->asString(), "other");
    const Result listed = client.call(attaching, "documents.list", Json());
    ASSERT_TRUE(listed);
    EXPECT_EQ(listed->find("documents")->asArray().size(), 2u);

    std::vector<std::string> heard;
    const auto s = api.subscribe(attaching, {"document.changed", "documents.changed"},
                                 [&](const Event &e) { heard.push_back(e.name + " " + e.data.dump()); });
    api.documentChanged(hikari::application::DocumentId{2});
    api.documentsChanged();
    ASSERT_EQ(heard.size(), 2u);
    EXPECT_TRUE(heard[0].starts_with(R"(document.changed {"document":2,)")) << heard[0];
    EXPECT_EQ(heard[1], R"(documents.changed {"documents":[1,2],"editingTarget":1})");
    api.unsubscribe(s);
}

TEST_F(ApiScripts, DocumentSavedSaysWhereAndWhichRevisionWithinTheCallersScope)
{
    std::vector<std::string> ai, plugin;
    const auto a = api.subscribe(aiCaller(), {"document.saved"}, [&](const Event &e) { ai.push_back(e.data.dump()); });
    const auto p =
        api.subscribe(pluginCaller(), {"document.saved"}, [&](const Event &e) { plugin.push_back(e.data.dump()); });
    api.documentSaved(hikari::application::DocumentId{1}, "/subs/a.ass", 4);
    api.documentSaved(hikari::application::DocumentId{2}, "/subs/b.ass", 0);
    EXPECT_EQ(ai, (std::vector<std::string>{R"({"document":1,"path":"/subs/a.ass","revision":4})",
                                            R"({"document":2,"path":"/subs/b.ass","revision":0})"}));
    EXPECT_EQ(plugin, (std::vector<std::string>{R"({"document":1,"path":"/subs/a.ass","revision":4})"}));
    api.unsubscribe(a);
    api.unsubscribe(p);
}

TEST_F(ApiScripts, DocumentsChangedIsNarrowedWithoutTheGrant)
{
    std::vector<Json> ai, plugin;
    const auto a = api.subscribe(aiCaller(), {"documents.changed"}, [&](const Event &e) { ai.push_back(e.data); });
    const auto p = api.subscribe(pluginCaller(), {"documents.changed"}, [&](const Event &e) { plugin.push_back(e.data); });
    api.documentsChanged();
    ASSERT_EQ(ai.size(), 1u);
    ASSERT_EQ(plugin.size(), 1u);
    EXPECT_EQ(ai[0].dump(), R"({"documents":[1,2],"editingTarget":1})");
    EXPECT_EQ(plugin[0].dump(), R"({"documents":[1],"editingTarget":1})");
    api.unsubscribe(a);
    api.unsubscribe(p);
}

}
