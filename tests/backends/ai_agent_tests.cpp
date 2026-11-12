#include "fake_host.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/settings.h"
#include "hikari/backends/ai_agent.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <set>

using namespace hikari::backends::ai;
namespace app = hikari::application::ai;
using hikari::api::Json;
using Answer = AgentThreads::Answer;

namespace {

constexpr const char *kBuildId = "0.1.0+test";

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (done())
            return true;
        if (t.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

std::string kindOf(const Json &item)
{
    const Json *kind = item.find("kind");
    return kind && kind->isString() ? kind->asString() : std::string();
}

std::string at(const Json &item, std::string_view key)
{
    const Json *value = item.find(key);
    return value && value->isString() ? value->asString() : std::string();
}

Json call(std::string operation, Json params = Json::object())
{
    return Json::object({{"operation", std::move(operation)}, {"params", std::move(params)}});
}

std::string script(Json calls, Json extra = Json::object())
{
    extra["calls"] = std::move(calls);
    return extra.dump();
}

struct MemoryConfigs final : app::ConfigStore {
    app::Config stored;
    app::Config load() override { return stored; }
    bool save(const app::Config &config) override
    {
        stored = config;
        return true;
    }
};

struct NoKeys final : app::KeyStore {
    void read(const std::string &, std::function<void(app::KeyResult)> done) override { done(std::string("sk-test")); }
    void write(const std::string &, const std::string &, std::function<void(std::optional<app::KeyError>)> done) override
    {
        done(std::nullopt);
    }
    void remove(const std::string &, std::function<void(std::optional<app::KeyError>)> done) override
    {
        done(std::nullopt);
    }
};

TEST(AgentPolicy, ReadsAndViewChangesNeverAsk)
{
    EXPECT_FALSE(asksPermission("lines.list"));
    EXPECT_FALSE(asksPermission("selection.set"));
    EXPECT_FALSE(asksPermission("panels.focus"));
    EXPECT_TRUE(asksPermission("document.apply"));
    EXPECT_TRUE(asksPermission("settings.set"));
    EXPECT_TRUE(asksPermission("commands.run"));
}

TEST(AgentPolicy, EachModeDecidesWrites)
{
    EXPECT_EQ(decide(AgentMode::Manual, "document.apply", false, false), Decision::Propose);
    EXPECT_EQ(decide(AgentMode::Manual, "document.apply", false, true), Decision::Propose);
    EXPECT_EQ(decide(AgentMode::Manual, "commands.run", false, false), Decision::Ask);
    EXPECT_EQ(decide(AgentMode::Manual, "commands.run", false, true), Decision::Run);
    EXPECT_EQ(decide(AgentMode::Auto, "document.apply", false, false), Decision::Judge);
    EXPECT_EQ(decide(AgentMode::Auto, "commands.run", false, true), Decision::Run);
    EXPECT_EQ(decide(AgentMode::FullAccess, "commands.run", false, false), Decision::Run);
    for (const AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess})
        EXPECT_EQ(decide(mode, "commands.run", true, true), Decision::AlwaysAsk);
}

const std::vector<std::string> kSafe{"/home/u/subs", "/home/u/.local/share/HikariSub"};

std::optional<std::string> pathOf(std::string_view handle)
{
    if (handle == "inside")
        return "/home/u/subs/b.ass";
    if (handle == "outside")
        return "/home/u/subsidiary/b.ass";
    return std::nullopt;
}

Risk riskOf(const std::string &operation, const Json &params)
{
    return alwaysAsk(operation, params, pathOf, kSafe).risk;
}

Risk commandRisk(std::string_view command)
{
    return riskOf("commands.run", Json::object({{"command", std::string(command)}}));
}

Risk settingRisk(std::string_view setting, Json value = true)
{
    return riskOf("settings.set", Json::object({{"setting", std::string(setting)}, {"value", std::move(value)}}));
}

TEST(AgentPolicy, AiWrittenLuaAsksOnEveryRunInEveryMode)
{
    for (const AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess})
        for (const bool allowed : {false, true})
            EXPECT_EQ(decide(mode, "automation.run", false, allowed), Decision::Script);
}

TEST(AgentPolicy, FilesOutsideTheSubtitlesFoldersAndTheUserFolderAsk)
{
    EXPECT_EQ(riskOf("files.write", Json::object({{"file", "inside"}})), Risk::None);
    EXPECT_EQ(riskOf("files.write", Json::object({{"file", "outside"}})), Risk::Files);
    EXPECT_EQ(riskOf("files.write", Json::object({{"file", "unknown"}})), Risk::Files);
    EXPECT_EQ(commandRisk("GLOBAL_DELETE_TEMPORARY_FILES"), Risk::Files);
    EXPECT_EQ(settingRisk("fontCollector.directory", "/home/u/subs/fonts"), Risk::None);
    EXPECT_EQ(settingRisk("fontCollector.directory", "/home/u/subsidiary"), Risk::Files);
    EXPECT_EQ(settingRisk("fontCollector.directory", "/home/u/subs/../elsewhere"), Risk::Files);
    for (const char *save : {"GLOBAL_SAVE_SUBS", "GLOBAL_SAVE_ALL_SUBS", "GLOBAL_SAVE_SUBS_AS", "GLOBAL_EXPORT_SUBS_AS",
                             "VIDEO_SAVE_FRAME_TO_PNG", "VIDEO_DELETE_FILE"})
        EXPECT_EQ(commandRisk(save), Risk::None) << save;
}

TEST(AgentPolicy, RunningScriptsOrProgramsAsks)
{
    for (const char *command : {"GLOBAL_AUTOMATION_LOAD_SCRIPT", "GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT",
                                "GLOBAL_AUTOMATION_REPEAT_LAST_RUN", "GLOBAL_AUTOMATION_RELOAD_AUTOLOAD", "plugin.x.run"})
        EXPECT_EQ(commandRisk(command), Risk::Programs) << command;
    for (const char *setting : {"automation.scriptEditor", "automation.lastScript", "automation.disabledAutoload",
                                "updater.autoInstall"})
        EXPECT_EQ(settingRisk(setting), Risk::Programs) << setting;
}

TEST(AgentPolicy, InstallingOrTrustingPluginsAndScriptsAsks)
{
    for (const char *setting : {"plugins.autoUpdate", "plugins.network", "automation.trustedFolders"})
        EXPECT_EQ(settingRisk(setting), Risk::Plugins) << setting;
    EXPECT_EQ(commandRisk("GLOBAL_PLUGINS"), Risk::None) << "the Plugins window installs only on the user's click";
}

TEST(AgentPolicy, SendingToTheNetworkAndAiSettingsAsk)
{
    for (const char *setting : {"privacy.alwaysSendCrashReports", "privacy.usageData", "privacy.discordFileName"})
        EXPECT_EQ(settingRisk(setting), Risk::Network) << setting;
    EXPECT_EQ(settingRisk("ai.off"), Risk::AiSettings);
    EXPECT_EQ(settingRisk("video.zoom", 1), Risk::None);
}

TEST(AgentPolicy, EveryWriteOperationIsClassified)
{
    std::set<std::string> writes;
    for (const hikari::api::Operation &op : hikari::api::Api::registry().operations())
        if (op.access == hikari::api::Access::Write)
            writes.insert(op.name);
    EXPECT_EQ(writes, (std::set<std::string>{"automation.run", "commands.run", "document.apply", "files.write", "panels.close",
                                             "panels.focus", "panels.open", "selection.set", "settings.set",
                                             "translation.project_write"}))
        << "Classify the new write in alwaysAsk and docs/ai-agent.md, then add it here.";
}

TEST(AgentPolicy, EveryAutomationOrDeleteCommandIsClassified)
{
    const std::set<std::string_view> reviewedSafe{"GLOBAL_AUTOMATION_OPEN_HOTKEYS_WINDOW", "VIDEO_DELETE_FILE",
                                                  "VIDEO_VECTOR_CLIP_DELETE"};
    for (const hikari::application::HotkeyAction &action : hikari::application::hotkeyActions()) {
        const std::string_view symbol = action.symbol;
        if (symbol.find("AUTOMATION") == std::string_view::npos && symbol.find("DELETE") == std::string_view::npos)
            continue;
        if (!hikari::application::isRetiredHotkey(action.id))
            EXPECT_NE(commandRisk(symbol) != Risk::None, reviewedSafe.contains(symbol)) << symbol;
    }
}

TEST(AgentPolicy, EverySettingUnderARiskyPrefixIsClassified)
{
    const std::set<std::string_view> reviewedSafe{
        "automation.loadingMethod",     "automation.oldScriptsCompatibility", "automation.traceLevel",
        "automation.scriptResolutionFromVideo", "automation.runLogTimes", "automation.fillLastAnswers",
        "automation.dialogAnswers",     "automation.dialogPresets",           "fontCollector.action",
        "fontCollector.fromMkv",        "fontCollector.useSubsDirectory",     "fonts.externalDirectory",
        "updater.autoCheck",            "updater.checkForStable",             "updater.nextCheck",
        "updater.skippedVersion",       "plugins.checkUpdates"};
    for (const hikari::application::SettingDefinition &d : hikari::application::settingDefinitions()) {
        const std::string_view id = d.id;
        if (d.disposition != hikari::application::SettingDisposition::Mapped)
            continue;
        if (!(id.starts_with("automation.") || id.starts_with("plugins.") || id.starts_with("privacy.") ||
              id.starts_with("updater.") || id.starts_with("fontCollector.") || id.starts_with("fonts.")))
            continue;
        const bool risky = settingRisk(id, "/elsewhere") != Risk::None;
        EXPECT_NE(risky, reviewedSafe.contains(id)) << id;
    }
}

TEST(AgentPolicy, AllowingInAThreadCoversTheOperationOrTheOneCommand)
{
    EXPECT_EQ(allowanceKey("settings.set", Json::object({{"setting", "a"}})), "settings.set");
    EXPECT_EQ(allowanceKey("commands.run", Json::object({{"command", "GLOBAL_UNDO"}})), "commands.run:GLOBAL_UNDO");
}

TEST(AgentPolicy, TheJudgeSeesTheActionAndTheUsersMessagesOnly)
{
    const Json request = judgeRequest("commands.run", Json::object({{"command", "GLOBAL_UNDO"}}), {"first", "second"});
    ASSERT_EQ(request.find("messages")->asArray().size(), 1u);
    const std::string text = request.find("messages")->asArray()[0].find("parts")->asArray()[0].find("text")->asString();
    EXPECT_LT(text.find("1. first"), text.find("2. second"));
    EXPECT_NE(text.find("commands_run"), std::string::npos);
    EXPECT_NE(text.find("GLOBAL_UNDO"), std::string::npos);
    EXPECT_FALSE(at(request, "system").empty());
}

TEST(AgentPolicy, AnythingButAClearApprovalEscalates)
{
    EXPECT_TRUE(judgeVerdict(R"(Fine: {"decision": "approve"})").approved);
    const Verdict escalated = judgeVerdict(R"({"decision": "escalate", "reason": "Too broad."})");
    EXPECT_FALSE(escalated.approved);
    EXPECT_EQ(escalated.reason, "Too broad.");
    EXPECT_FALSE(judgeVerdict("approve").approved);
    EXPECT_FALSE(judgeVerdict(R"({"decision": "Approve"})").approved);
    EXPECT_FALSE(judgeVerdict("").reason.empty());
}

TEST(AgentThreadStore, AThreadAndTheDaysSpendingSurviveARestart)
{
    QTemporaryDir dir;
    const QString folder = dir.filePath(QStringLiteral("threads"));
    AgentThread thread;
    thread.id = "t1";
    thread.title = "Fix the timing";
    thread.mode = AgentMode::Manual;
    thread.model = app::ModelRef{"or", "m"};
    thread.history.push(Json::object({{"author", "user"}, {"parts", Json::array()}}));
    thread.items.push(Json::object({{"kind", "user"}, {"text", "Fix"}}));
    thread.cost = 0.25;
    thread.cap = 2;
    thread.allowed = {"commands.run:GLOBAL_UNDO"};
    const QDate day(2026, 11, 9);
    {
        ThreadStore store(folder);
        ASSERT_TRUE(store.save(thread));
        store.spend(day.addDays(-40), 3);
        store.spend(day, 0.5);
        store.spend(day, 0.25);
    }
    ThreadStore store(folder);
    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].toJson(), thread.toJson());
    EXPECT_DOUBLE_EQ(store.spentOn(day), 0.75);
    EXPECT_DOUBLE_EQ(store.spentOn(day.addDays(-40)), 0);
    store.remove("t1");
    EXPECT_TRUE(store.load().empty());
}

class AgentThreadsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_agent_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override
    {
        app::Provider p = app::presetProvider(app::Preset::OpenRouter, "or", "OpenRouter");
        p.models = {{.id = "m", .name = "M", .price = app::Price{.input = 0, .output = 0}},
                    {.id = "approve", .name = "Yes", .price = app::Price{.input = 0.5, .output = 0}},
                    {.id = "escalate", .name = "No", .price = app::Price{.input = 0, .output = 0}}};
        configs.stored.providers.push_back(p);
        configs.stored.roles[app::Role::Chat] = {"or", "m"};
        configs.stored.roles[app::Role::Judge] = {"or", "approve"};
        providers = std::make_unique<app::Providers>(configs, keys);
        requests = std::make_unique<AiRequests>(*providers, [this] { return sidecar(); });
        store = std::make_unique<ThreadStore>(dir.filePath(QStringLiteral("threads")));
        fake.api = &api;
        threads = makeThreads();
    }

    void TearDown() override
    {
        threads.reset();
        requests.reset();
        host.reset();
    }

    std::unique_ptr<AgentThreads> makeThreads(bool withoutProvider = false)
    {
        AgentThreads::Wiring w;
        w.requests = [this, withoutProvider] { return withoutProvider ? nullptr : requests.get(); };
        w.agentHost = [this] { return sidecar(); };
        w.config = [this] { return &providers->config(); };
        w.api = &api;
        w.defaults = [this] { return defaults; };
        w.beginTurn = [this](const std::string &caller, const std::string &label) {
            return fake.review.beginTurn(caller, label);
        };
        w.editingTarget = [this] { return fake.workspace().editingTarget(); };
        w.documentTitle = [](hikari::application::DocumentId) { return std::string("a.ass"); };
        w.safeFolders = [] { return std::vector<std::string>{"/subs"}; };
        w.now = [this] { return ++clock; };
        w.today = [] { return QDate(2026, 11, 9); };
        return std::make_unique<AgentThreads>(std::move(w), store.get());
    }

    AiSidecarHost *sidecar()
    {
        if (!host) {
            AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {QStringLiteral("--build-id"), QString::fromLatin1(kBuildId), QStringLiteral("--api-version"),
                           QString::number(hikari::api::kApiVersion), QStringLiteral("--mode"), sidecarMode};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            o.buildId = kBuildId;
            o.shutdownMs = 600;
            host = std::make_unique<AiSidecarHost>(std::move(o), &api, hikari::api::Caller{"ai", "AI"});
        }
        return host.get();
    }

    Json received()
    {
        std::optional<SidecarResult> result;
        sidecar()->request("received", {}, [&](SidecarResult r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result && *result ? **result : Json::array();
    }

    std::vector<Json> receivedAs(std::string_view method)
    {
        std::vector<Json> out;
        for (const Json &m : received().asArray())
            if (at(m, "method") == method)
                out.push_back(*m.find("params"));
        return out;
    }

    const AgentThread &thread(const std::string &id) { return *threads->thread(id); }

    std::vector<Json> items(const std::string &id, std::string_view kind)
    {
        std::vector<Json> out;
        for (const Json &item : thread(id).items.asArray())
            if (kindOf(item) == kind)
                out.push_back(item);
        return out;
    }

    std::string notice(const std::string &id)
    {
        const auto notices = items(id, "notice");
        return notices.empty() ? std::string() : at(notices.back(), "notice");
    }

    bool idle(const std::string &id)
    {
        return waitFor([&] { return !threads->running(id); });
    }

    std::uint64_t openAsk()
    {
        EXPECT_TRUE(waitFor([&] { return !threads->asks().empty(); }));
        return threads->asks().empty() ? 0 : threads->asks().begin()->first;
    }

    std::string withMode(AgentMode mode)
    {
        const std::string id = threads->create();
        threads->setMode(id, mode);
        return id;
    }

    Json apply(std::string text)
    {
        return call("document.apply",
                    Json::object({{"revision", static_cast<std::int64_t>(fake.target()->revision())},
                                  {"name", "Fix"},
                                  {"edits", Json::array({Json::object({{"op", "set_line"},
                                                                       {"line", 1},
                                                                       {"fields", Json::object({{"text", std::move(text)}})}})})}}));
    }

    QTemporaryDir dir;
    hikari::api::testing::FakeHost fake;
    hikari::api::Api api{fake.host()};
    MemoryConfigs configs;
    NoKeys keys;
    std::unique_ptr<app::Providers> providers;
    std::unique_ptr<AiSidecarHost> host;
    std::unique_ptr<AiRequests> requests;
    std::unique_ptr<ThreadStore> store;
    AgentThreads::Defaults defaults;
    std::int64_t clock = 0;
    QString sidecarMode = QStringLiteral("ok");
    std::unique_ptr<AgentThreads> threads;
};

TEST_F(AgentThreadsTest, ATurnStreamsTheAnswerKeepsTheHistoryAndTitlesTheThread)
{
    const std::string id = threads->create();
    threads->send(id, "Hello\nthere");
    EXPECT_TRUE(threads->running(id));
    ASSERT_TRUE(idle(id));
    const auto texts = items(id, "text");
    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(at(texts[0], "text"), "ok: Hello\nthere");
    EXPECT_EQ(thread(id).title, "Hello");
    EXPECT_EQ(thread(id).history.asArray().size(), 2u);
    EXPECT_EQ(notice(id), "");
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(at(opened[0], "sessionId"), id);
    EXPECT_EQ(*opened[0].find("model"), Json::object({{"providerId", "or"}, {"model", "m"}}));
    EXPECT_EQ(fake.review.turns().back().caller, "ai:" + id);
}

TEST_F(AgentThreadsTest, AThreadOnClaudeOpensItsSessionOnTheAgentWithoutAProvider)
{
    threads = makeThreads(true);

    const std::string id = threads->create();
    threads->setModel(id, {"agent:claude", ""});
    ASSERT_TRUE(threads->modelOf(thread(id)));
    EXPECT_EQ(threads->modelOf(thread(id))->providerId, "agent:claude");
    threads->send(id, "Fix the typo");
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(notice(id), "");
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(at(opened[0], "agent"), "claude");
    EXPECT_EQ(*opened[0].find("model"), Json::object({{"providerId", "agent:claude"}, {"model", ""}}));

    const std::string plain = threads->create();
    threads->send(plain, "Hello");
    EXPECT_EQ(notice(plain), "noSidecar");
}

TEST_F(AgentThreadsTest, FullAccessRunsAWriteIntoTheReviewAndShowsTheChange)
{
    const std::string id = withMode(AgentMode::FullAccess);
    threads->send(id, script(Json::array({apply("Hi"), call("commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}))})));
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(threads->asks().empty());
    EXPECT_EQ(fake.target()->document().lines()[0]->text, u8"Hi");
    const auto changes = items(id, "change");
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(at(changes[0], "name"), "Fix");
    EXPECT_EQ(at(changes[0], "title"), "a.ass");
    EXPECT_EQ(thread(id).files, std::vector<std::string>{"a.ass"});
    const auto targetId = fake.workspace().editingTarget();
    ASSERT_TRUE(targetId);
    ASSERT_EQ(fake.review.changes(*targetId).size(), 1u);
    EXPECT_EQ(fake.review.changes(*targetId)[0]->caller, "AI");
    const auto tools = items(id, "tool");
    ASSERT_EQ(tools.size(), 1u);
    EXPECT_EQ(at(tools[0], "tool"), "commands_run");
    EXPECT_EQ(at(tools[0], "status"), "done");
}

TEST_F(AgentThreadsTest, ManualAsksForOtherWritesUntilAllowedInTheThread)
{
    const std::string id = withMode(AgentMode::Manual);
    const Json save = call("commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}));
    threads->send(id, script(Json::array({save, save})));
    const std::uint64_t ask = openAsk();
    ASSERT_NE(ask, 0u);
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::Ask);
    threads->answer(ask, Answer::AllowInThread);
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(threads->asks().empty());
    const auto asks = items(id, "ask");
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(at(asks[0], "status"), "allowedInThread");
    EXPECT_TRUE(thread(id).allowed.contains("commands.run:GLOBAL_SAVE_SUBS"));
    for (const Json &answer : receivedAs("answer"))
        EXPECT_TRUE(answer.find("result")) << answer.dump();
}

TEST_F(AgentThreadsTest, ManualWritesNothingUntilTheUserAppliesTheProposal)
{
    const std::string id = withMode(AgentMode::Manual);
    const std::u8string before = fake.target()->document().lines()[0]->text;
    threads->send(id, script(Json::array({apply("Hi")})));
    const std::uint64_t ask = openAsk();
    ASSERT_NE(ask, 0u);
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::Propose);
    EXPECT_EQ(fake.target()->document().lines()[0]->text, before);
    const auto targetId = fake.workspace().editingTarget();
    ASSERT_TRUE(targetId);
    EXPECT_TRUE(fake.review.changes(*targetId).empty());
    const auto proposals = items(id, "proposal");
    ASSERT_EQ(proposals.size(), 1u);
    EXPECT_EQ(at(proposals[0], "status"), "open");
    EXPECT_EQ(at(proposals[0], "name"), "Fix");
    const Json &row = proposals[0].find("rows")->asArray().at(0);
    EXPECT_EQ(at(row, "change"), "edited");
    EXPECT_EQ(at(row, "before"), std::string(before.begin(), before.end()));
    EXPECT_EQ(at(row, "after"), "Hi");
    EXPECT_EQ(*row.find("row"), Json(1));

    threads->answer(ask, Answer::AllowInThread);
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(fake.target()->document().lines()[0]->text, u8"Hi");
    EXPECT_EQ(fake.review.changes(*targetId).size(), 1u);
    EXPECT_EQ(at(items(id, "proposal")[0], "status"), "applied");
    EXPECT_EQ(items(id, "change").size(), 1u);
    EXPECT_FALSE(thread(id).allowed.contains("document.apply"));
}

TEST_F(AgentThreadsTest, ADeclinedProposalWritesNothingAndTellsTheAgentWhatTheUserSaid)
{
    const std::string id = withMode(AgentMode::Manual);
    const std::u8string before = fake.target()->document().lines()[0]->text;
    threads->send(id, script(Json::array({apply("Hi")})));
    threads->answer(openAsk(), Answer::Deny, "Keep line one");
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(fake.target()->document().lines()[0]->text, before);
    EXPECT_TRUE(fake.review.changes(*fake.workspace().editingTarget()).empty());
    const auto answers = receivedAs("answer");
    ASSERT_EQ(answers.size(), 1u);
    const std::string error = answers[0].dump();
    EXPECT_NE(error.find("declined these subtitle edits"), std::string::npos);
    EXPECT_NE(error.find("Keep line one"), std::string::npos);
    EXPECT_EQ(at(items(id, "proposal")[0], "status"), "declined");
}

TEST_F(AgentThreadsTest, AProposalGoesOutOfDateWhenTheDocumentChangesAndAskAgainSendsTheAgentBackToRead)
{
    const std::string id = withMode(AgentMode::Manual);
    threads->send(id, script(Json::array({apply("Hi")})));
    const std::uint64_t ask = openAsk();
    std::optional<hikari::api::Result> other;
    api.call(hikari::api::Caller{"plugin:x", "X", true}, "document.apply",
             Json::object({{"revision", static_cast<std::int64_t>(fake.target()->revision())},
                           {"name", "Other"},
                           {"edits", Json::array({Json::object({{"op", "set_line"},
                                                                {"line", 2},
                                                                {"fields", Json::object({{"text", "Mine"}})}})})}}),
             [&](hikari::api::Result r) { other = std::move(r); });
    ASSERT_TRUE(other && *other);
    EXPECT_TRUE(threads->asks().at(ask).stale);
    EXPECT_EQ(at(items(id, "proposal")[0], "status"), "stale");

    threads->answer(ask, Answer::AllowOnce);
    ASSERT_TRUE(threads->asks().contains(ask));
    threads->answer(ask, Answer::AskAgain);
    ASSERT_TRUE(idle(id));
    EXPECT_NE(fake.target()->document().lines()[0]->text, u8"Hi");
    const auto answers = receivedAs("answer");
    ASSERT_EQ(answers.size(), 1u);
    EXPECT_NE(answers[0].dump().find(hikari::api::errors::kStaleRevision), std::string::npos);
    EXPECT_EQ(at(items(id, "proposal")[0], "status"), "retried");
}

TEST_F(AgentThreadsTest, StopWithdrawsAnOpenProposalWithNothingWritten)
{
    const std::string id = withMode(AgentMode::Manual);
    const std::u8string before = fake.target()->document().lines()[0]->text;
    threads->send(id, script(Json::array({apply("Hi")})));
    openAsk();
    threads->stop(id);
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(threads->asks().empty());
    EXPECT_EQ(fake.target()->document().lines()[0]->text, before);
    EXPECT_EQ(at(items(id, "proposal")[0], "status"), "withdrawn");
}

TEST_F(AgentThreadsTest, ADenialGoesToTheAgentAsARefusalWithTheUsersMessage)
{
    const std::string id = withMode(AgentMode::Manual);
    threads->send(id, script(Json::array({call("commands.run", Json::object({{"command", "GLOBAL_UNDO"}}))})));
    threads->answer(openAsk(), Answer::Deny, "Not now");
    ASSERT_TRUE(idle(id));
    const auto answers = receivedAs("answer");
    ASSERT_EQ(answers.size(), 1u);
    const std::string error = answers[0].dump();
    EXPECT_NE(error.find("Not now"), std::string::npos);
    EXPECT_NE(error.find(hikari::api::errors::kRefused), std::string::npos);
    EXPECT_EQ(at(items(id, "ask")[0], "status"), "denied");
}

TEST_F(AgentThreadsTest, AutoAsksTheJudgeWithTheUsersMessagesButNoToolOutputAndChargesTheThread)
{
    const std::string id = withMode(AgentMode::Auto);
    threads->send(id, script(Json::array({call("lines.list"), call("commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}))})));
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(threads->asks().empty());
    std::vector<Json> judged;
    for (const Json &c : receivedAs("_hikari/complete"))
        judged.push_back(c);
    ASSERT_EQ(judged.size(), 1u);
    const std::string sent = judged[0].dump();
    EXPECT_NE(sent.find("GLOBAL_SAVE_SUBS"), std::string::npos);
    EXPECT_NE(sent.find("lines.list"), std::string::npos) << "the user's message is the script";
    EXPECT_EQ(sent.find("General Kenobi"), std::string::npos);
    EXPECT_DOUBLE_EQ(thread(id).cost, 0.5);
    EXPECT_DOUBLE_EQ(threads->spentToday(), 0.5);
}

TEST_F(AgentThreadsTest, AnEscalatingJudgeAsksTheUserWithItsReason)
{
    ASSERT_TRUE(providers->assign(app::Role::Judge, {"or", "escalate"}));
    const std::string id = withMode(AgentMode::Auto);
    threads->send(id, script(Json::array({call("commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}))})));
    const std::uint64_t ask = openAsk();
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::Judge);
    EXPECT_EQ(threads->asks().at(ask).reason, "The user didn't ask for that.");
    threads->answer(ask, Answer::AllowOnce);
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(thread(id).allowed.empty());
}

TEST_F(AgentThreadsTest, TheAlwaysAskListAsksEvenInFullAccessAndCantBeAllowedForTheThread)
{
    const std::string id = withMode(AgentMode::FullAccess);
    const Json risky = call("commands.run", Json::object({{"command", "GLOBAL_DELETE_TEMPORARY_FILES"}}));
    threads->send(id, script(Json::array({risky, risky})));
    std::uint64_t ask = openAsk();
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::AlwaysAsk);
    threads->answer(ask, Answer::AllowInThread);
    ASSERT_TRUE(waitFor([&] { return !threads->asks().empty() && threads->asks().begin()->first != ask; }));
    EXPECT_TRUE(thread(id).allowed.empty());
    threads->answer(threads->asks().begin()->first, Answer::Deny);
    ASSERT_TRUE(idle(id));
}

TEST_F(AgentThreadsTest, ALuaScriptShowsInFullAndAsksOnEveryRunInEveryMode)
{
    const std::string lua = "script_name = 'Fade'\naegisub.register_macro('Fade', '', function() end)\n";
    const Json run = call("automation.run", Json::object({{"name", "Fade"}, {"script", lua}}));
    for (const AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess}) {
        fake.automation.runs.clear();
        const std::string id = withMode(mode);
        threads->send(id, script(Json::array({run, run})));
        const std::uint64_t first = openAsk();
        ASSERT_NE(first, 0u);
        EXPECT_EQ(threads->asks().at(first).kind, Decision::Script);
        const Json card = items(id, "script").at(0);
        EXPECT_EQ(at(card, "script"), lua);
        EXPECT_EQ(*card.find("lines"), Json(2));
        EXPECT_EQ(at(card, "title"), "a.ass");
        threads->answer(first, Answer::AllowInThread);
        EXPECT_TRUE(threads->asks().contains(first)) << "a script has no Allow in this thread";
        threads->answer(first, Answer::AllowOnce);
        ASSERT_TRUE(waitFor([&] { return bool(fake.automation.pending); }));
        EXPECT_EQ(at(items(id, "script").at(0), "status"), "running");
        fake.automation.finish();
        ASSERT_TRUE(waitFor([&] { return !threads->asks().empty() && threads->asks().begin()->first != first; }));
        EXPECT_EQ(threads->asks().begin()->second.kind, Decision::Script) << "a rerun asks again";
        threads->answer(threads->asks().begin()->first, Answer::Deny, "Not now");
        ASSERT_TRUE(idle(id));
        EXPECT_EQ(fake.automation.runs.size(), 1u);
        EXPECT_TRUE(thread(id).allowed.empty());
        const auto cards = items(id, "script");
        ASSERT_EQ(cards.size(), 2u);
        EXPECT_EQ(at(cards[0], "status"), "ran");
        EXPECT_EQ(at(cards[1], "status"), "declined");
        const auto answers = receivedAs("answer");
        ASSERT_GE(answers.size(), 2u);
        EXPECT_NE(answers.back().dump().find("Not now"), std::string::npos);
    }
}

TEST_F(AgentThreadsTest, QtRefusesWritesWithoutAThreadOrAfterTheTurnStopped)
{
    const std::string id = threads->create();
    threads->send(id, "Hi");
    ASSERT_TRUE(idle(id));
    std::optional<SidecarResult> result;
    sidecar()->request("api", apply("Sneaky"), [&](SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    ASSERT_TRUE(*result);
    EXPECT_NE((*result)->dump().find("A write needs an agent thread."), std::string::npos);
    result.reset();
    Json write = apply("Sneaky");
    write["session"] = "nobody";
    sidecar()->request("api", write, [&](SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_NE((*result)->dump().find(hikari::api::errors::kNotGranted), std::string::npos);
    result.reset();
    write["session"] = id;
    sidecar()->request("api", write, [&](SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_NE((*result)->dump().find("This turn has stopped"), std::string::npos);
    EXPECT_NE(fake.target()->document().lines()[0]->text, u8"Sneaky");
}

TEST_F(AgentThreadsTest, AThreadMayReadTheUsersSkillsAndACallWithoutOneMayNot)
{
    const std::string id = threads->create();
    threads->send(id, "Hi");
    ASSERT_TRUE(idle(id));
    std::optional<SidecarResult> result;
    Json list = call("skills.list");
    list["session"] = id;
    sidecar()->request("api", list, [&](SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    ASSERT_TRUE(*result);
    EXPECT_NE((*result)->dump().find("\"skills\":[]"), std::string::npos) << (*result)->dump();
    result.reset();
    sidecar()->request("api", call("skills.list"), [&](SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    EXPECT_NE((*result)->dump().find(hikari::api::errors::kNotGranted), std::string::npos);
}

TEST_F(AgentThreadsTest, StopWithdrawsTheOpenAskAndEndsTheTurn)
{
    const std::string id = withMode(AgentMode::Manual);
    threads->send(id, script(Json::array({call("commands.run", Json::object({{"command", "GLOBAL_UNDO"}}))})));
    openAsk();
    threads->stop(id);
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(threads->asks().empty());
    EXPECT_EQ(at(items(id, "ask")[0], "status"), "withdrawn");
    EXPECT_EQ(notice(id), "stopped");
    EXPECT_EQ(receivedAs("session/cancel").size(), 1u);
}

TEST_F(AgentThreadsTest, StopBeforeTheSidecarAnswersItsHandshakeEndsTheTurn)
{
    sidecarMode = QStringLiteral("silent");
    const std::string id = threads->create();
    threads->send(id, "Hello");
    ASSERT_TRUE(threads->running(id));
    ASSERT_EQ(sidecar()->state(), AiSidecarHost::State::Starting);
    threads->stop(id);
    EXPECT_FALSE(threads->running(id));
    EXPECT_EQ(notice(id), "stopped");
    EXPECT_TRUE(items(id, "text").empty());
}

TEST_F(AgentThreadsTest, TheThreadCapStopsTheTurnAndRaisingItLetsTheNextOneRun)
{
    defaults.threadCap = 1.0;
    const std::string id = threads->create();
    const Json usage = Json::object({{"inputTokens", 10}, {"outputTokens", 1}, {"reportedCost", 1.5}});
    threads->send(id, Json::object({{"hang", true}, {"usage", usage}}).dump());
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(notice(id), "cap");
    EXPECT_DOUBLE_EQ(thread(id).cost, 1.5);
    threads->send(id, "Again");
    EXPECT_FALSE(threads->running(id));
    EXPECT_EQ(notice(id), "cap");
    threads->raiseCap(id);
    EXPECT_DOUBLE_EQ(thread(id).cap, 2.5);
    threads->send(id, "Again");
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(at(items(id, "text").back(), "text"), "ok: Again");
}

TEST_F(AgentThreadsTest, TheDailyCapStopsEveryThreadForTheDay)
{
    defaults.dailyCap = 1.0;
    defaults.threadCap = 0;
    const std::string first = threads->create();
    threads->send(first, Json::object({{"hang", true}, {"usage", Json::object({{"reportedCost", 1.2}})}}).dump());
    ASSERT_TRUE(idle(first));
    EXPECT_EQ(notice(first), "dailyCap");
    const std::string second = threads->create();
    threads->send(second, "Hi");
    EXPECT_FALSE(threads->running(second));
    EXPECT_EQ(notice(second), "dailyCap");
}

TEST_F(AgentThreadsTest, ASidecarCrashMidTurnShowsLostAndRetryResendsTheMessageOnTheEarlierHistory)
{
    const std::string id = threads->create();
    threads->send(id, "First");
    ASSERT_TRUE(idle(id));
    const QString marker = dir.filePath(QStringLiteral("crashed"));
    const std::string text = Json::object({{"crashUnless", marker.toStdString()}}).dump();
    threads->send(id, text);
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(notice(id), "lost");
    EXPECT_TRUE(QFile::exists(marker));
    EXPECT_EQ(thread(id).history.asArray().size(), 3u);
    threads->retry(id);
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(at(items(id, "text").back(), "text"), "recovered");
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0].find("history")->asArray().size(), 2u);
    EXPECT_EQ(thread(id).history.asArray().size(), 4u);
}

TEST_F(AgentThreadsTest, ASteerJoinsTheRunningTurnAndAQueuedMessageRunsWhenItEnds)
{
    const std::string id = withMode(AgentMode::Manual);
    const Json run = call("commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}));
    threads->send(id, script(Json::array({run, run})));
    std::uint64_t ask = openAsk();
    threads->send(id, "Only Line 3");
    EXPECT_EQ(threads->queued(id), "Only Line 3");
    threads->steer(id);
    EXPECT_EQ(threads->queued(id), "");
    ASSERT_TRUE(waitFor([&] { return items(id, "user").size() == 2; }));
    EXPECT_EQ(*items(id, "user")[1].find("steered"), Json(true));
    threads->answer(ask, Answer::AllowOnce);
    ask = openAsk();
    threads->send(id, "Then this");
    threads->answer(ask, Answer::AllowOnce);
    ASSERT_TRUE(waitFor([&] { return items(id, "text").size() == 1; }));
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(at(items(id, "text").back(), "text"), "ok: Then this");
    EXPECT_EQ(threads->queued(id), "");
}

TEST_F(AgentThreadsTest, AReopenedThreadReplaysItsHistoryIntoANewSession)
{
    const std::string id = threads->create();
    threads->send(id, "Remember me");
    ASSERT_TRUE(idle(id));
    threads = makeThreads();
    ASSERT_TRUE(threads->thread(id));
    EXPECT_EQ(thread(id).title, "Remember me");
    threads->send(id, "Again");
    ASSERT_TRUE(idle(id));
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 2u);
    EXPECT_EQ(opened[1].find("history")->asArray().size(), 2u);
    EXPECT_EQ(thread(id).history.asArray().size(), 4u);
    threads->remove(id);
    EXPECT_FALSE(threads->thread(id));
    threads = makeThreads();
    EXPECT_FALSE(threads->thread(id));
}

TEST_F(AgentThreadsTest, AThreadSearchesWhileSettingsAndItsGlobeBothSaySoAndKeepsItsGlobe)
{
    const auto searched = [&](std::string_view method) {
        const auto sent = receivedAs(method);
        const Json *on = sent.empty() ? nullptr : sent.back().find("webSearch");
        return on && on->isBool() && on->asBool();
    };
    requests->setWebSearch(true, std::nullopt);
    const std::string id = threads->create();
    EXPECT_TRUE(thread(id).webSearch);
    threads->send(id, "a");
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(searched("session/new"));
    EXPECT_TRUE(searched("session/prompt"));
    threads->setWebSearch(id, false);
    threads->send(id, "b");
    ASSERT_TRUE(idle(id));
    EXPECT_FALSE(searched("session/prompt"));
    threads = makeThreads();
    EXPECT_FALSE(thread(id).webSearch);
    threads->setWebSearch(id, true);
    requests->setWebSearch(false, std::nullopt);
    threads->send(id, "c");
    ASSERT_TRUE(idle(id));
    EXPECT_FALSE(searched("session/new"));
    EXPECT_FALSE(searched("session/prompt"));
}

TEST_F(AgentThreadsTest, AnAnswersSourcesBecomeItsCitationsAndItsSearchesARowBeforeIt)
{
    const std::string answer = "Aimer\xF0\x9F\x98\x80 sings.";
    const Json sources = Json::object(
        {{"sessionUpdate", "sources"},
         {"textLength", 14},
         {"citations", Json::array({Json::object({{"index", 1}, {"url", "https://a.example/"}, {"title", "A"},
                                                  {"quote", "Aimer sings it"}, {"start", 0}, {"end", 7}}),
                                    Json::object({{"index", 2}, {"url", "https://b.example/"}, {"title", ""},
                                                  {"start", 0}, {"end", 99}})})},
         {"searchSuggestions", "<div>chips</div>"},
         {"searches", Json::array({Json::object({{"kind", "search"}, {"query", "aimer"}})})}});
    const Json usage = Json::object({{"sessionUpdate", "usage"},
                                     {"usage", Json::object({{"inputTokens", 1}, {"outputTokens", 1}})}});
    const std::string id = threads->create();
    threads->send(id, Json::object({{"say", answer}, {"calls", Json::array()},
                                    {"updates", Json::array({sources, usage})}}).dump());
    ASSERT_TRUE(idle(id));
    const auto &all = thread(id).items.asArray();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(kindOf(all[0]), "user");
    EXPECT_EQ(kindOf(all[1]), "search");
    EXPECT_EQ(at(all[1], "by"), "provider");
    EXPECT_EQ(*all[1].find("searches"), *sources.find("searches"));
    const Json text = all[2];
    EXPECT_EQ(at(text, "text"), answer);
    EXPECT_EQ(at(text, "searchSuggestions"), "<div>chips</div>");
    EXPECT_EQ(*text.find("citations"),
              Json::array({Json::object({{"index", 1}, {"url", "https://a.example/"}, {"title", "A"},
                                         {"quote", "Aimer sings it"}, {"start", 0}, {"end", 7}}),
                           Json::object({{"index", 2}, {"url", "https://b.example/"}, {"title", "https://b.example/"}})}));
    threads = makeThreads();
    EXPECT_EQ(*thread(id).items.asArray()[2].find("citations"), *text.find("citations"));
}

TEST_F(AgentThreadsTest, SearchServiceRowsKeepTheirPagesAndTheFinishedTurnDropsTheResults)
{
    requests->setWebSearch(true, app::SearchService::Brave);
    const std::string results = R"({"results":[{"url":"https://a.example/","title":"A","snippet":"secret text"}]})";
    const Json failed = Json::object({{"sessionUpdate", "search_failed"}, {"service", "brave"}, {"reason", "usedUp"},
                                      {"httpStatus", 402}});
    const Json updates = Json::array(
        {Json::object({{"sessionUpdate", "tool_call"}, {"toolCallId", "w1"}, {"title", "web_search"}, {"kind", "read"},
                       {"status", "in_progress"}, {"rawInput", Json::object({{"query", "aimer"}})}}),
         Json::object({{"sessionUpdate", "tool_call_update"}, {"toolCallId", "w1"}, {"status", "completed"},
                       {"content", Json::array({Json::object({{"type", "content"},
                                                              {"content", Json::object({{"type", "text"}, {"text", results}})}})})}}),
         Json::object({{"sessionUpdate", "history"},
                       {"message", Json::object({{"author", "user"},
                                                 {"parts", Json::array({Json::object({{"type", "toolResult"}, {"callId", "w1"},
                                                                                      {"content", results}})})}})}}),
         failed, failed});
    const std::string id = threads->create();
    threads->send(id, Json::object({{"say", "I couldn't search."}, {"calls", Json::array()}, {"updates", updates}}).dump());
    ASSERT_TRUE(idle(id));
    const auto tools = items(id, "tool");
    ASSERT_EQ(tools.size(), 1u);
    EXPECT_EQ(at(tools[0], "service"), "brave");
    EXPECT_EQ(*tools[0].find("urls"), Json::array({"https://a.example/"}));
    EXPECT_FALSE(tools[0].find("output"));
    const Json &result = thread(id).history.asArray().at(1).find("parts")->asArray().at(0);
    EXPECT_EQ(at(result, "callId"), "w1");
    EXPECT_EQ(at(result, "content"), "The search results aren't kept after the turn.");
    const auto notices = items(id, "notice");
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_EQ(at(notices[0], "notice"), "searchUsedUp");
    EXPECT_EQ(at(notices[0], "service"), "brave");
    EXPECT_EQ(*notices[0].find("status"), Json(std::int64_t{402}));
    EXPECT_EQ(at(items(id, "text").back(), "text"), "I couldn't search.");
}

}
