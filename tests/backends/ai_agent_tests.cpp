#include "fake_host.h"

#include "hikari/application/hotkeys.h"
#include "hikari/application/settings.h"
#include "hikari/backends/ai_agent.h"
#include "hikari/backends/ai_mcp_clients.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QDir>
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

TEST(AgentPolicy, InstallingAPluginAsksOnEveryInstallInEveryMode)
{
    for (const AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess})
        for (const bool allowed : {false, true})
            EXPECT_EQ(decide(mode, "plugins.install", false, allowed), Decision::Install);
}

TEST(AgentPolicy, ScaffoldingAPluginAsksLikeOtherWrites)
{
    EXPECT_EQ(riskOf("plugins.scaffold", Json::object({{"plugin", "org.example.a"}})), Risk::None);
    EXPECT_EQ(decide(AgentMode::Manual, "plugins.scaffold", false, false), Decision::Ask);
    EXPECT_EQ(decide(AgentMode::Auto, "plugins.scaffold", false, false), Decision::Judge);
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
    for (const char *program : {"whisperx", "mfa"})
        EXPECT_EQ(riskOf("karaoke.align", Json::object({{"program", program}})), Risk::Programs) << program;
    EXPECT_EQ(riskOf("karaoke.apply_template", Json::object({})), Risk::None);
    EXPECT_EQ(riskOf("plugins.run", Json::object({{"plugin", "org.example.a"}, {"command", "count"}})), Risk::Programs);
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

TEST(AgentPolicy, TypesetApplyAsksInManualModeLikeOtherWrites)
{
    const Json params = Json::object({{"revision", 1}, {"name", "Typeset the sign"}});
    ASSERT_EQ(riskOf("typeset.apply", params), Risk::None);
    EXPECT_EQ(decide(AgentMode::Manual, "typeset.apply", false, false), Decision::Ask);
    EXPECT_EQ(decide(AgentMode::Manual, "typeset.apply", false, true), Decision::Run);
    EXPECT_EQ(decide(AgentMode::Auto, "typeset.apply", false, false), Decision::Judge);
    EXPECT_EQ(decide(AgentMode::FullAccess, "typeset.apply", false, false), Decision::Run);
}

TEST(AgentPolicy, EffectsWritesAskInManualModeLikeOtherWrites)
{
    for (const std::string operation : {"effects.save", "effects.apply"}) {
        ASSERT_EQ(riskOf(operation, Json::object({{"line", 4}})), Risk::None) << operation;
        EXPECT_EQ(decide(AgentMode::Manual, operation, false, false), Decision::Ask) << operation;
        EXPECT_EQ(decide(AgentMode::Auto, operation, false, false), Decision::Judge) << operation;
        EXPECT_EQ(decide(AgentMode::FullAccess, operation, false, false), Decision::Run) << operation;
    }
}

TEST(AgentPolicy, EveryWriteOperationIsClassified)
{
    std::set<std::string> writes;
    for (const hikari::api::Operation &op : hikari::api::Api::registry().operations())
        if (op.access == hikari::api::Access::Write)
            writes.insert(op.name);
    EXPECT_EQ(writes, (std::set<std::string>{"automation.run", "commands.run", "document.apply", "effects.apply",
                                             "effects.save", "files.write",
                                             "karaoke.align", "karaoke.apply_template", "panels.close", "panels.focus",
                                             "panels.open", "plugins.install", "plugins.run",
                                             "plugins.scaffold", "selection.set", "settings.set",
                                             "translation.project_write", "typeset.apply"}))
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
    EXPECT_EQ(allowanceKey(std::string(kMcpCallOperation), Json::object({{"server", "notes"}, {"tool", "add"}})),
              "mcp.call:notes/add");
}

TEST(AgentPolicy, OnlyATrustedServersHintsLoosenAnything)
{
    const McpToolHints read{.readOnly = true, .destructive = false};
    const McpToolHints gentle{.readOnly = false, .destructive = false};
    EXPECT_TRUE(effectiveHints(read, true).readOnly);
    EXPECT_FALSE(effectiveHints(gentle, true).destructive);
    for (const McpToolHints &listed : {read, gentle}) {
        EXPECT_FALSE(effectiveHints(listed, false).readOnly);
        EXPECT_TRUE(effectiveHints(listed, false).destructive);
    }
    EXPECT_TRUE(effectiveHints(McpToolHints{}, true).destructive);
}

TEST(AgentPolicy, McpReadsRunDestructiveToolsAlwaysAskAndTheModeDecidesTheRest)
{
    for (AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess}) {
        EXPECT_EQ(decideMcp(mode, true, false, false, false), Decision::Run);
        EXPECT_EQ(decideMcp(mode, false, true, true, true), Decision::AlwaysAsk);
        EXPECT_EQ(decideMcp(mode, false, false, true, false), Decision::Run);
    }
    EXPECT_EQ(decideMcp(AgentMode::Manual, false, false, false, false), Decision::Ask);
    EXPECT_EQ(decideMcp(AgentMode::Manual, false, false, false, true), Decision::Run);
    EXPECT_EQ(decideMcp(AgentMode::Auto, false, false, false, false), Decision::Judge);
    EXPECT_EQ(decideMcp(AgentMode::FullAccess, false, false, false, false), Decision::Run);
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
    thread.project = "/work/Frieren S2";
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
        w.beginTurn = [this](const std::string &caller, const std::string &label, std::uint64_t parentTurn) {
            return fake.review.beginTurn(caller, label, parentTurn);
        };
        w.editingTarget = [this] { return fake.workspace().editingTarget(); };
        w.documentTitle = [](hikari::application::DocumentId) { return std::string("a.ass"); };
        w.safeFolders = [] { return std::vector<std::string>{"/subs"}; };
        w.project = [this] { return project; };
        w.instructions = [this](const std::string &folder) {
            return joinInstructions(agentsFileText(dir.filePath(QStringLiteral("user"))),
                                    agentsFileText(QString::fromStdString(folder)));
        };
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
    std::string project;
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

TEST_F(AgentThreadsTest, AnMcpToolRunsAsksOrAlwaysAsksAsItsHintsAndTheThreadsModeSay)
{
    McpServerEntry notes{.id = "notes", .name = "Notes", .command = QStringLiteral("notes-server")};
    std::vector<std::string> admitted;
    sidecar()->handle(kMcpServersAdmit, [&](const Json &params, std::shared_ptr<SidecarRequest> request) {
        const std::string tool = params.find("tool")->asString();
        const McpToolHints hints{.readOnly = tool == "read", .destructive = tool == "wipe"};
        sidecar()->gateSession(params.find("sessionId")->asString(), std::string(kMcpCallOperation),
                               mcpCallParams(notes, tool, *params.find("arguments"), hints), request,
                               [&admitted, tool](SidecarRequest &r) {
                                   admitted.push_back(tool);
                                   r.respond(Json::object());
                               });
    });
    const auto mcp = [](std::string tool) {
        return Json::object({{"method", kMcpServersAdmit},
                             {"params", Json::object({{"server", "notes"}, {"tool", std::move(tool)},
                                                      {"arguments", Json::object({{"id", 1}})}})}});
    };
    const std::string id = withMode(AgentMode::Manual);
    threads->send(id, script(Json::array({mcp("read"), mcp("add"), mcp("add"), mcp("wipe")})));
    std::uint64_t ask = openAsk();
    EXPECT_EQ(admitted, std::vector<std::string>{"read"});
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::Ask);
    threads->answer(ask, Answer::AllowInThread);
    ASSERT_TRUE(waitFor([&] { return !threads->asks().empty() && threads->asks().begin()->first != ask; }));
    ask = threads->asks().begin()->first;
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::AlwaysAsk);
    EXPECT_EQ(admitted, (std::vector<std::string>{"read", "add", "add"}));
    EXPECT_TRUE(thread(id).allowed.contains("mcp.call:notes/add"));
    threads->answer(ask, Answer::Deny);
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(admitted.size(), 3u);
    const auto asks = items(id, "ask");
    ASSERT_EQ(asks.size(), 2u);
    EXPECT_EQ(at(asks[0], "tool"), "Notes: add");
    EXPECT_EQ(at(asks[1], "type"), "always");
    const auto answers = receivedAs("answer");
    ASSERT_EQ(answers.size(), 4u);
    EXPECT_NE(answers[3].dump().find(hikari::api::errors::kRefused), std::string::npos) << answers[3].dump();
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

TEST_F(AgentThreadsTest, APluginInstallShowsItsCodeAndAsksOnEveryInstallInEveryMode)
{
    fake.fakePlugins.package = "/user/ai/plugins/org.example.word-count.hikariplugin";
    const Json install = call("plugins.install", Json::object({{"plugin", "org.example.word-count"}}));
    for (const AgentMode mode : {AgentMode::Manual, AgentMode::Auto, AgentMode::FullAccess}) {
        fake.fakePlugins.installs.clear();
        fake.fakePlugins.fullTrust = mode == AgentMode::FullAccess;
        const std::string id = withMode(mode);
        threads->send(id, script(Json::array({install, install})));
        const std::uint64_t first = openAsk();
        ASSERT_NE(first, 0u);
        EXPECT_EQ(threads->asks().at(first).kind, Decision::Install);
        const Json card = items(id, "install").at(0);
        EXPECT_EQ(at(card, "name"), "Word count");
        EXPECT_EQ(at(card, "tier"), mode == AgentMode::FullAccess ? "unrestricted" : "restricted");
        EXPECT_EQ(at(card.find("files")->asArray().at(0), "path"), "main.lua");
        threads->answer(first, Answer::AllowInThread);
        EXPECT_TRUE(threads->asks().contains(first)) << "an install has no Allow in this thread";
        threads->answer(first, Answer::AllowOnce);
        ASSERT_TRUE(waitFor([&] { return !threads->asks().empty() && threads->asks().begin()->first != first; }));
        EXPECT_EQ(threads->asks().begin()->second.kind, Decision::Install) << "a second install asks again";
        threads->answer(threads->asks().begin()->first, Answer::Deny, "Not this one");
        ASSERT_TRUE(idle(id));
        EXPECT_EQ(fake.fakePlugins.installs.size(), 1u);
        EXPECT_TRUE(thread(id).allowed.empty());
        const auto cards = items(id, "install");
        ASSERT_EQ(cards.size(), 2u);
        EXPECT_EQ(at(cards[0], "status"), "installed");
        EXPECT_EQ(at(cards[1], "status"), "declined");
        EXPECT_EQ(at(cards[1], "message"), "Not this one");
        const auto answers = receivedAs("answer");
        ASSERT_GE(answers.size(), 2u);
        EXPECT_NE(answers.back().dump().find("Not this one"), std::string::npos);
    }
}

TEST_F(AgentThreadsTest, APluginThatCantBeReviewedIsRefusedWithoutACard)
{
    const std::string id = withMode(AgentMode::FullAccess);
    threads->send(id, script(Json::array({call("plugins.install", Json::object({{"plugin", "org.example.none"}}))})));
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(items(id, "install").empty());
    EXPECT_TRUE(fake.fakePlugins.installs.empty());
}

TEST_F(AgentThreadsTest, RunningAPluginCommandAlwaysAsks)
{
    const std::string id = withMode(AgentMode::FullAccess);
    threads->send(id, script(Json::array({call("plugins.run", Json::object({{"plugin", "org.example.word-count"},
                                                                            {"command", "count"}}))})));
    const std::uint64_t ask = openAsk();
    ASSERT_NE(ask, 0u);
    EXPECT_EQ(threads->asks().at(ask).kind, Decision::AlwaysAsk);
    threads->answer(ask, Answer::AllowOnce);
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(fake.fakePlugins.runs, std::vector<std::string>{"org.example.word-count/count"});
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

TEST_F(AgentThreadsTest, AThreadKeepsTheProjectItStartedInAndReadsItsSkills)
{
    const QString show = dir.filePath(QStringLiteral("show"));
    ASSERT_TRUE(QDir().mkpath(show + QStringLiteral("/.hikari/skills/honorifics")));
    QFile skill(show + QStringLiteral("/.hikari/skills/honorifics/SKILL.md"));
    ASSERT_TRUE(skill.open(QIODevice::WriteOnly));
    skill.write("---\nname: honorifics\ndescription: Honorifics.\n---\n");
    skill.close();
    project = show.toStdString();
    const std::string inProject = threads->create();
    project.clear();
    const std::string outside = threads->create();
    EXPECT_EQ(thread(inProject).project, show.toStdString());
    const auto list = [&](const std::string &id) {
        threads->send(id, "Hi");
        EXPECT_TRUE(idle(id));
        std::optional<SidecarResult> result;
        Json request = call("skills.list");
        request["session"] = id;
        sidecar()->request("api", request, [&](SidecarResult r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result && *result ? (*result)->dump() : std::string();
    };
    const std::string listed = list(inProject);
    EXPECT_NE(listed.find(R"("honorifics")"), std::string::npos) << listed;
    EXPECT_NE(listed.find(R"("origin":"project")"), std::string::npos) << listed;
    EXPECT_EQ(list(outside).find("honorifics"), std::string::npos);
}

namespace {

void writeAgentsFile(const QString &folder, const QByteArray &text)
{
    ASSERT_TRUE(QDir().mkpath(folder));
    QFile file(folder + QStringLiteral("/AGENTS.md"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text);
}

}

TEST(AgentInstructions, TheUsersAgentsFileComesBeforeTheProjectsAndEmptyOnesDropOut)
{
    EXPECT_EQ(joinInstructions("Use Oxford commas.", "Keep honorifics."),
              "Instructions from the user's AGENTS.md:\n\nUse Oxford commas.\n\n"
              "Instructions from this Project's AGENTS.md:\n\nKeep honorifics.");
    EXPECT_EQ(joinInstructions("", "Keep honorifics."), "Instructions from this Project's AGENTS.md:\n\nKeep honorifics.");
    EXPECT_EQ(joinInstructions("Use Oxford commas.", ""), "Instructions from the user's AGENTS.md:\n\nUse Oxford commas.");
    EXPECT_EQ(joinInstructions("", ""), "");
}

TEST(AgentInstructions, AnAgentsFileStopsAtItsCapWithoutSplittingACharacter)
{
    QTemporaryDir dir;
    EXPECT_EQ(agentsFileText(dir.path()), "");
    EXPECT_EQ(agentsFileText(QString()), "");

    const QByteArray cut = QByteArray(qsizetype(kInstructionsCap - 1), 'a') + "\xE3\x81\x82tail";
    writeAgentsFile(dir.filePath(QStringLiteral("cut")), cut);
    EXPECT_EQ(agentsFileText(dir.filePath(QStringLiteral("cut"))), std::string(kInstructionsCap - 1, 'a'));

    const QByteArray whole = QByteArray(qsizetype(kInstructionsCap - 3), 'a') + "\xE3\x81\x82tail";
    writeAgentsFile(dir.filePath(QStringLiteral("whole")), whole);
    EXPECT_EQ(agentsFileText(dir.filePath(QStringLiteral("whole"))), whole.first(qsizetype(kInstructionsCap)).toStdString());

    writeAgentsFile(dir.filePath(QStringLiteral("short")), "Keep honorifics.");
    EXPECT_EQ(agentsFileText(dir.filePath(QStringLiteral("short"))), "Keep honorifics.");
}

TEST_F(AgentThreadsTest, ASessionGetsTheUsersThenItsProjectsAgentsFileAfterHikarisPrompt)
{
    writeAgentsFile(dir.filePath(QStringLiteral("user")), "Use Oxford commas.");
    const QString show = dir.filePath(QStringLiteral("show"));
    writeAgentsFile(show, "Keep honorifics.");
    project = show.toStdString();
    const std::string inProject = threads->create();
    project.clear();
    const std::string outside = threads->create();
    project = dir.filePath(QStringLiteral("other")).toStdString();

    threads->send(inProject, "Hi");
    ASSERT_TRUE(idle(inProject));
    threads->send(outside, "Hi");
    ASSERT_TRUE(idle(outside));
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 2u);
    const std::string system = at(opened[0], "system");
    EXPECT_TRUE(system.starts_with("You are Hikari AI")) << system;
    EXPECT_TRUE(system.ends_with("\n\nInstructions from the user's AGENTS.md:\n\nUse Oxford commas.\n\n"
                                 "Instructions from this Project's AGENTS.md:\n\nKeep honorifics."))
        << system;
    EXPECT_TRUE(at(opened[1], "system").ends_with("Use Oxford commas.")) << at(opened[1], "system");
}

TEST_F(AgentThreadsTest, AThreadInAProjectWithAKeyTalksToItsAliasAndKeepsTheUsersProvider)
{
    requests->setProjectKeys([](const std::string &folder, const std::string &providerId) -> std::optional<std::string> {
        if (folder == "/show" && providerId == "or")
            return "sk-project";
        return std::nullopt;
    });
    project = "/show";
    const std::string inProject = threads->create();
    project.clear();
    const std::string outside = threads->create();

    threads->send(inProject, "Hi");
    ASSERT_TRUE(idle(inProject));
    threads->send(outside, "Hi");
    ASSERT_TRUE(idle(outside));
    threads->remove(inProject);
    threads->remove(outside);

    const Json alias = Json::object({{"providerId", "or@p1"}, {"model", "m"}});
    const Json own = Json::object({{"providerId", "or"}, {"model", "m"}});
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 2u);
    EXPECT_EQ(*opened[0].find("model"), alias);
    EXPECT_EQ(*opened[1].find("model"), own);
    const auto prompts = receivedAs("session/prompt");
    ASSERT_EQ(prompts.size(), 2u);
    EXPECT_EQ(*prompts[0].find("model"), alias);
    EXPECT_EQ(*prompts[1].find("model"), own);
    const auto told = receivedAs("_hikari/providers.set");
    ASSERT_FALSE(told.empty());
    EXPECT_NE(told.front().dump().find(R"("id":"or@p1")"), std::string::npos) << told.front().dump();
    const auto ended = receivedAs("_hikari/conversation.end");
    ASSERT_EQ(ended.size(), 2u);
    EXPECT_EQ(*ended[0].find("providerId"), Json("or@p1"));
    EXPECT_EQ(*ended[1].find("providerId"), Json("or"));
    EXPECT_EQ(received().dump().find("sk-project"), std::string::npos);
    EXPECT_FALSE(threads->thread(inProject));
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

namespace {

Json textMessage(std::string author, std::string text)
{
    return Json::object({{"author", std::move(author)},
                         {"parts", Json::array({Json::object({{"type", "text"}, {"text", std::move(text)}})})}});
}

Json readCall(std::string id)
{
    return Json::object({{"author", "assistant"},
                         {"parts", Json::array({Json::object({{"type", "toolCall"},
                                                              {"id", id},
                                                              {"name", "lines_find"},
                                                              {"argumentsJson", "{}"}})})}});
}

Json readResult(std::string id, std::string content)
{
    return Json::object({{"author", "user"},
                         {"parts", Json::array({Json::object({{"type", "toolResult"},
                                                              {"callId", std::move(id)},
                                                              {"content", std::move(content)}})})}});
}

Json userItem(std::int64_t history)
{
    return Json::object({{"kind", "user"}, {"text", "x"}, {"history", history}});
}

Json compactedUpdate(std::string summary, std::int64_t headEnd, std::int64_t keptFrom, Json stubs = Json::array())
{
    return Json::object({{"sessionUpdate", "compacted"},
                         {"stage", 2},
                         {"before", 1000},
                         {"after", 100},
                         {"stubs", std::move(stubs)},
                         {"summary", summary},
                         {"message", textMessage("assistant", "Summary of the conversation so far\n\n" + summary)},
                         {"headEnd", headEnd},
                         {"keptFrom", keptFrom}});
}

}

TEST(AgentCompaction, StubsAndTheSummaryReplaceOnlyTheMiddleAndUserItemsFollowTheirMessages)
{
    AgentThread t;
    t.history = Json::array({textMessage("user", "first"), readCall("c1"), readResult("c1", "a long read"),
                             textMessage("assistant", "one"), textMessage("user", "second"), readCall("c2"),
                             readResult("c2", "kept read"), textMessage("user", "third"), textMessage("assistant", "three")});
    t.items = Json::array({userItem(0), userItem(4), userItem(7), Json::object({{"kind", "text"}, {"text", "one"}})});
    const Json original = t.history;

    applyCompaction(t, Json::object({{"sessionUpdate", "compacted"},
                                     {"stage", 1},
                                     {"stubs", Json::array({Json::object({{"callId", "c1"}, {"content", "[Cleared]"}})})}}));
    ASSERT_EQ(t.history.asArray().size(), 9u);
    EXPECT_EQ(t.history.asArray()[2].find("parts")->asArray()[0].find("content")->asString(), "[Cleared]");
    EXPECT_EQ(t.history.asArray()[6].dump(), original.asArray()[6].dump());

    applyCompaction(t, compactedUpdate("## Goal\n- QC", 1, 4));
    const auto &history = t.history.asArray();
    ASSERT_EQ(history.size(), 7u);
    EXPECT_EQ(history[0].dump(), original.asArray()[0].dump());
    EXPECT_EQ(history[1].dump(), textMessage("assistant", "Summary of the conversation so far\n\n## Goal\n- QC").dump());
    for (std::size_t i = 4; i < original.asArray().size(); ++i)
        EXPECT_EQ(history[i - 2].dump(), original.asArray()[i].dump());
    const auto &items = t.items.asArray();
    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0].find("history")->asInt(), 0);
    EXPECT_EQ(items[1].find("history")->asInt(), 2);
    EXPECT_EQ(items[2].find("history")->asInt(), 5);

    applyCompaction(t, compactedUpdate("second", 1, 5));
    ASSERT_EQ(t.history.asArray().size(), 4u);
    EXPECT_EQ(items[1].find("history"), nullptr);
    EXPECT_EQ(items[2].find("history")->asInt(), 2);
    EXPECT_EQ(at(items[3], "text"), "one");
}

TEST_F(AgentThreadsTest, CompactWaitsForTheRunningTurnThenPassesTheInstructionAndChargesTheThread)
{
    const std::string id = withMode(AgentMode::Manual);
    const Json usage = Json::object({{"sessionUpdate", "usage"},
                                     {"compaction", true},
                                     {"usage", Json::object({{"inputTokens", 5000}, {"outputTokens", 200}, {"reportedCost", 0.25}})}});
    const Json answer = Json::object({{"updates", Json::array({compactedUpdate("## Goal\n- QC", 1, 1), usage})},
                                      {"result", Json::object({{"compacted", true}, {"summary", "## Goal\n- QC"}})}});
    threads->send(id, script(Json::array({call("commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}))}),
                             Json::object({{"compact", Json::array({answer})}})));
    const std::uint64_t ask = openAsk();
    threads->compact(id, "keep the glossary decisions");
    threads->setQueued(id, "Carry on");
    EXPECT_TRUE(threads->compacting(id));
    EXPECT_TRUE(receivedAs("_hikari/session.compact").empty());

    threads->answer(ask, Answer::AllowOnce);
    ASSERT_TRUE(waitFor([&] { return items(id, "user").size() == 2 && !threads->running(id); }));
    EXPECT_FALSE(threads->compacting(id));
    const auto compactions = receivedAs("_hikari/session.compact");
    ASSERT_EQ(compactions.size(), 1u);
    EXPECT_EQ(at(compactions[0], "instruction"), "keep the glossary decisions");
    const auto all = received().asArray();
    const auto position = [&](std::string_view method, std::size_t nth) {
        std::size_t seen = 0;
        for (std::size_t i = 0; i < all.size(); ++i)
            if (at(all[i], "method") == method && seen++ == nth)
                return i;
        return all.size();
    };
    EXPECT_LT(position("_hikari/session.compact", 0), position("session/prompt", 1));

    const auto divider = items(id, "compaction");
    ASSERT_EQ(divider.size(), 1u);
    EXPECT_EQ(divider[0].find("before")->asInt(), 1000);
    EXPECT_EQ(divider[0].find("after")->asInt(), 100);
    EXPECT_EQ(at(divider[0], "summary"), "## Goal\n- QC");
    EXPECT_EQ(thread(id).history.asArray()[1].dump(),
              textMessage("assistant", "Summary of the conversation so far\n\n## Goal\n- QC").dump());
    EXPECT_DOUBLE_EQ(thread(id).cost, 0.25);
    ASSERT_EQ(items(id, "text").size(), 1u);
    EXPECT_EQ(at(items(id, "text")[0], "text"), "ok: Carry on");
}

TEST_F(AgentThreadsTest, AFailedCompactionOffersRetryAndANewThreadFromTheSummary)
{
    const Json failed = Json::object({{"error", Json::object({{"code", -32010},
                                                              {"message", "provider timeout"},
                                                              {"data", Json::object({{"kind", "timeout"},
                                                                                     {"httpStatus", 0},
                                                                                     {"detail", "provider timeout"}})}})}});
    const Json summary = Json::object({{"result", Json::object({{"compacted", false}, {"summary", "## Goal\n- QC"}})}});
    const std::string id = threads->create();
    threads->send(id, script(Json::array(), Json::object({{"say", "ok"}, {"compact", Json::array({failed, summary})}})));
    ASSERT_TRUE(idle(id));
    threads->compact(id);
    ASSERT_TRUE(waitFor([&] { return notice(id) == "compactFailed"; }));
    EXPECT_EQ(at(items(id, "notice").back(), "detail"), "provider timeout");
    EXPECT_EQ(thread(id).history.asArray().size(), 2u);

    std::string opened;
    QObject::connect(threads.get(), &AgentThreads::summarised, [&](const QString &, const QString &to) { opened = to.toStdString(); });
    threads->newThreadFromSummary(id);
    ASSERT_TRUE(waitFor([&] { return !opened.empty(); }));
    EXPECT_EQ(receivedAs("_hikari/session.compact").back().find("apply")->asBool(), false);
    const AgentThread &next = thread(opened);
    EXPECT_EQ(next.title, thread(id).title);
    ASSERT_EQ(next.history.asArray().size(), 2u);
    EXPECT_EQ(at(next.history.asArray()[1], "author"), "assistant");
    EXPECT_EQ(next.history.asArray()[1].find("parts")->asArray()[0].find("text")->asString(), "## Goal\n- QC");
    ASSERT_EQ(items(opened, "compaction").size(), 1u);
    EXPECT_EQ(at(items(opened, "compaction")[0], "from"), thread(id).title);
}

TEST_F(AgentThreadsTest, AnAutomaticCompactionThatFailsTwiceStopsTheTurnWithItsReason)
{
    const std::string id = threads->create();
    threads->send(id, script(Json::array(),
                             Json::object({{"reason", "compaction_failed"},
                                           {"updates", Json::array({Json::object({{"sessionUpdate", "compaction_failed"},
                                                                                  {"reason", "provider timeout"}})})}})));
    ASSERT_TRUE(idle(id));
    EXPECT_EQ(notice(id), "compactFailed");
    EXPECT_EQ(at(items(id, "notice").back(), "detail"), "provider timeout");
}

TEST_F(AgentThreadsTest, TheSessionLearnsTheContextWindowAndWhetherToCompactByItself)
{
    defaults.autoCompact = false;
    const std::string id = threads->create();
    threads->send(id, "Hello");
    ASSERT_TRUE(idle(id));
    const auto opened = receivedAs("session/new");
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0].find("contextTokens")->asInt(), kDefaultContextTokens);
    EXPECT_FALSE(opened[0].find("autoCompact")->asBool());

    defaults.autoCompact = true;
    threads->send(id, "Again");
    ASSERT_TRUE(idle(id));
    EXPECT_TRUE(receivedAs("session/prompt").back().find("autoCompact")->asBool());
}

namespace {

const hikari::application::DocumentId kDoc{1};

TaskAsk taskAsk(TaskScope scope = {kDoc, {1}})
{
    TaskAsk ask;
    ask.parentMode = AgentMode::Auto;
    ask.scope = std::move(scope);
    return ask;
}

Json linesEdit(std::string op, std::int64_t line)
{
    return Json::object({{"op", std::move(op)}, {"line", line}, {"fields", Json::object({{"text", "x"}})}});
}

Json applyTo(std::int64_t document, Json edits)
{
    return Json::object({{"document", document}, {"revision", 1}, {"name", "n"}, {"edits", std::move(edits)}});
}

}

TEST(AgentTasks, QtRefusesDepthAFifthChildABroaderModeTheCapsAndAnOverlap)
{
    EXPECT_EQ(checkTask(taskAsk()), AgentMode::Auto);
    TaskAsk ask = taskAsk();
    ask.mode = AgentMode::Manual;
    EXPECT_EQ(checkTask(ask), AgentMode::Manual) << "a narrower child is fine";
    ask.mode = AgentMode::FullAccess;
    EXPECT_EQ(checkTask(ask), std::unexpected(TaskRefusal::ModeEscalation));
    ask = taskAsk();
    ask.fromChild = true;
    EXPECT_EQ(checkTask(ask), std::unexpected(TaskRefusal::Depth));
    ask = taskAsk();
    ask.running = kMaxRunningChildren;
    EXPECT_EQ(checkTask(ask), std::unexpected(TaskRefusal::TooManyChildren));
    ask = taskAsk();
    ask.overCap = true;
    EXPECT_EQ(checkTask(ask), std::unexpected(TaskRefusal::CostCap));
    ask = taskAsk();
    ask.overDailyCap = true;
    EXPECT_EQ(checkTask(ask), std::unexpected(TaskRefusal::DailyCap));
    ask = taskAsk();
    ask.runningScopes = {{kDoc, {2, 3}}, {hikari::application::DocumentId{2}, {}}};
    EXPECT_EQ(checkTask(ask), AgentMode::Auto) << "disjoint Lines, and another Document";
    ask.runningScopes.push_back({kDoc, {}});
    EXPECT_EQ(checkTask(ask), std::unexpected(TaskRefusal::ScopeOverlap)) << "a whole-Document scope holds every Line";
    EXPECT_TRUE(scopesOverlap({kDoc, {1, 2}}, {kDoc, {2}}));
    EXPECT_FALSE(scopesOverlap({kDoc, {1}}, {kDoc, {2}}));
}

TEST(AgentTasks, AChildWritesOnlyItsLinesOfItsDocumentThroughDocumentApply)
{
    const TaskScope scope{kDoc, {1, 2}};
    EXPECT_FALSE(outsideScope(scope, "document.apply", applyTo(1, Json::array({linesEdit("set_line", 1)})), kDoc));
    EXPECT_FALSE(outsideScope(scope, "lines.list", Json::object(), kDoc)) << "reads are free";
    EXPECT_FALSE(outsideScope(scope, "selection.set", Json::object(), kDoc));
    EXPECT_TRUE(outsideScope(scope, "document.apply", applyTo(1, Json::array({linesEdit("set_line", 3)})), kDoc));
    EXPECT_TRUE(outsideScope(scope, "document.apply", applyTo(2, Json::array({linesEdit("set_line", 1)})), kDoc));
    Json noDocument = applyTo(1, Json::array({linesEdit("delete_line", 2)}));
    std::erase_if(noDocument.asObject(), [](const Json::Member &m) { return m.key == "document"; });
    EXPECT_FALSE(outsideScope(scope, "document.apply", noDocument, kDoc));
    EXPECT_TRUE(outsideScope(scope, "document.apply", noDocument, hikari::application::DocumentId{2}))
        << "the editing target moved to another tab";
    EXPECT_TRUE(outsideScope(scope, "document.apply",
                             applyTo(1, Json::array({Json::object({{"op", "set_style"}, {"name", "Default"}})})), kDoc));
    EXPECT_FALSE(outsideScope({kDoc, {}}, "document.apply",
                              applyTo(1, Json::array({Json::object({{"op", "set_style"}, {"name", "Default"}})})), kDoc));
    EXPECT_FALSE(outsideScope(scope, "document.apply",
                              applyTo(1, Json::array({Json::object({{"op", "insert_line"}, {"after", 2}})})), kDoc));
    EXPECT_TRUE(outsideScope(scope, "commands.run", Json::object({{"command", "GLOBAL_SAVE_SUBS"}}), kDoc));
    EXPECT_TRUE(outsideScope(scope, "settings.set", Json::object({{"setting", "video.zoom"}}), kDoc));
}

TEST(AgentTasks, TheJudgeSeesAChildsBriefAsTheAgentsWordsNotTheUsers)
{
    const std::string sent =
        judgeRequest("document.apply", Json::object({{"name", "n"}}), {"Translate it"}, "Translate Lines 1-3").dump();
    EXPECT_NE(sent.find("Translate it"), std::string::npos);
    EXPECT_NE(sent.find("Another AI agent wrote its task, not the user"), std::string::npos);
    EXPECT_NE(sent.find("Translate Lines 1-3"), std::string::npos);
    EXPECT_EQ(judgeRequest("document.apply", Json::object(), {"x"}).dump().find("sub-agent"), std::string::npos);
}

TEST(AgentTasks, AChildThreadKeepsItsParentAndTaskAndARunningOneComesBackInterrupted)
{
    AgentThread t;
    t.id = "c1";
    t.task = AgentTask{.parent = "p1", .id = "t2", .brief = "Do it", .role = "translator", .lines = {4, 5},
                       .status = TaskStatus::Running, .summary = "half", .target = "Codex"};
    const auto back = AgentThread::fromJson(t.toJson());
    ASSERT_TRUE(back && back->task);
    EXPECT_EQ(back->task->parent, "p1");
    EXPECT_EQ(back->task->id, "t2");
    EXPECT_EQ(back->task->brief, "Do it");
    EXPECT_EQ(back->task->lines, (std::vector<std::int64_t>{4, 5}));
    EXPECT_EQ(back->task->target, "Codex");
    EXPECT_EQ(back->task->status, TaskStatus::Interrupted);
    EXPECT_FALSE(back->task->scope) << "Document ids don't outlive the app";
}

namespace {

Json delegate(Json brief, Json scope, Json extra = Json::object())
{
    extra["task"] = brief.isString() ? brief : Json(brief.dump());
    extra["scope"] = std::move(scope);
    return Json::object({{"method", "_hikari/task.request"}, {"params", std::move(extra)}});
}

Json taskCall(std::string method, std::string task, Json extra = Json::object())
{
    extra["taskId"] = std::move(task);
    return Json::object({{"method", std::move(method)}, {"params", std::move(extra)}});
}

Json setLine(std::int64_t revision, std::int64_t line, std::string text, std::string name)
{
    return call("document.apply",
                Json::object({{"revision", revision},
                              {"name", std::move(name)},
                              {"edits", Json::array({Json::object({{"op", "set_line"},
                                                                   {"line", line},
                                                                   {"fields", Json::object({{"text", std::move(text)}})}})})}}));
}

Json scopeOf(std::int64_t document, std::vector<std::int64_t> lines = {})
{
    Json out = Json::object({{"document", document}});
    if (!lines.empty()) {
        Json ids = Json::array();
        for (const std::int64_t id : lines)
            ids.push(id);
        out["lines"] = std::move(ids);
    }
    return out;
}

const Json kHang = Json::object({{"calls", Json::array()}, {"hang", true}});

}

class AgentTasksTest : public AgentThreadsTest {
protected:
    void SetUp() override
    {
        AgentThreadsTest::SetUp();
        app::Provider other = app::presetProvider(app::Preset::OpenRouter, "or2", "Other");
        other.models = {{.id = "m2", .name = "M2", .price = app::Price{.input = 1, .output = 1}}};
        configs.stored.providers.push_back(other);
        threads.reset();
        requests.reset();
        providers = std::make_unique<app::Providers>(configs, keys);
        requests = std::make_unique<AiRequests>(*providers, [this] { return sidecar(); });
        threads = makeThreads();
        document = static_cast<std::int64_t>(fake.workspace().editingTarget()->value);
    }

    std::vector<const AgentThread *> kids(const std::string &parent) { return threads->children(parent); }

    std::string toolOutput(const std::string &id, std::size_t index)
    {
        const auto tools = items(id, "tool");
        return index < tools.size() ? at(tools[index], "output") : std::string();
    }

    std::int64_t revision() { return static_cast<std::int64_t>(fake.target()->revision()); }

    std::int64_t document = 0;
};

TEST_F(AgentTasksTest, TwoChildrenOnOtherProvidersEditDisjointLinesIntoTheirOwnChangeSetsUnderTheParentsTurn)
{
    const std::string parent = withMode(AgentMode::FullAccess);
    const std::int64_t r = revision();
    const Json first = Json::object({{"calls", Json::array({setLine(r, 1, "Hi", "Shortened 1")})}, {"say", "Did 1."}});
    const Json second = Json::object({{"calls", Json::array({setLine(r + 1, 3, "Hey", "Shortened 3")})}, {"say", "Did 3."}});
    threads->send(parent, script(Json::array(
                              {delegate(first, scopeOf(document, {1}),
                                        Json::object({{"title", "Part 1"}, {"target", Json::object({{"providerId", "or"}, {"model", "m"}})}})),
                               taskCall("_hikari/task.wait", "t1", Json::object({{"timeoutMs", 20000}})),
                               delegate(second, scopeOf(document, {3}),
                                        Json::object({{"title", "Part 3"}, {"target", Json::object({{"providerId", "or2"}, {"model", "m2"}})}}))})));
    ASSERT_TRUE(waitFor([&] {
        const auto k = kids(parent);
        return k.size() == 2 && k[0]->task->status != TaskStatus::Running && k[1]->task->status != TaskStatus::Running;
    }));
    ASSERT_TRUE(idle(parent));
    const auto k = kids(parent);
    EXPECT_EQ(k[0]->task->status, TaskStatus::Done);
    EXPECT_EQ(k[1]->task->status, TaskStatus::Done);
    EXPECT_EQ(k[0]->task->summary, "Did 1.");
    EXPECT_EQ(*k[1]->model, (app::ModelRef{"or2", "m2"}));
    EXPECT_EQ(k[1]->task->target, "Hikari · M2");
    EXPECT_TRUE(threads->threads().size() == 1u) << "children aren't in the threads list";
    EXPECT_NE(toolOutput(parent, 1).find("Did 1."), std::string::npos) << "the wait gave the result";

    const auto doc = fake.workspace().editingTarget();
    const auto changes = fake.review.changes(*doc);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0]->caller, "Part 1");
    EXPECT_EQ(changes[1]->caller, "Part 3");
    EXPECT_NE(changes[0]->turn, changes[1]->turn);
    const std::uint64_t parentTurn = fake.review.turn(changes[0]->turn)->parent;
    EXPECT_NE(parentTurn, 0u);
    EXPECT_EQ(fake.review.turn(changes[1]->turn)->parent, parentTurn);
    EXPECT_EQ(fake.review.turn(parentTurn)->caller, "ai:" + parent);

    auto *session = fake.session(*doc);
    ASSERT_TRUE(fake.review.undo(*doc, *session, {.change = changes[1]->id}, false));
    EXPECT_EQ(fake.target()->document().lines()[2]->text, u8"General Kenobi");
    EXPECT_EQ(fake.target()->document().lines()[0]->text, u8"Hi");
    ASSERT_TRUE(fake.review.undo(*doc, *session, {.change = changes[0]->id}, false));
    EXPECT_EQ(fake.target()->document().lines()[0]->text, u8"Hello there");

    const auto opened = receivedAs("session/new");
    ASSERT_GE(opened.size(), 3u);
    EXPECT_TRUE(opened[0].find("delegate"));
    EXPECT_FALSE(opened[1].find("delegate")) << "a child can't delegate";
    EXPECT_EQ(*opened[1].find("task")->find("scope"), scopeOf(document, {1}));
    EXPECT_NE(at(opened[1], "system").find("you can't hand it to another agent"), std::string::npos);
}

TEST_F(AgentTasksTest, AChildReadsItsParentsProjectSkillsWithTheSameGrant)
{
    const QString show = dir.filePath(QStringLiteral("show"));
    ASSERT_TRUE(QDir().mkpath(show + QStringLiteral("/.hikari/skills/honorifics")));
    QFile skill(show + QStringLiteral("/.hikari/skills/honorifics/SKILL.md"));
    ASSERT_TRUE(skill.open(QIODevice::WriteOnly));
    skill.write("---\nname: honorifics\ndescription: Honorifics.\n---\n");
    skill.close();
    project = show.toStdString();
    const std::string parent = withMode(AgentMode::FullAccess);
    project.clear();
    threads->send(parent, script(Json::array({delegate(kHang, scopeOf(document, {1}))}), Json::object({{"hang", true}})));
    ASSERT_TRUE(waitFor([&] { return kids(parent).size() == 1u; }));
    const std::string child = kids(parent)[0]->id;
    EXPECT_EQ(kids(parent)[0]->project, show.toStdString());

    std::optional<SidecarResult> result;
    Json request = call("skills.list");
    request["session"] = child;
    sidecar()->request("api", request, [&](SidecarResult r) { result = std::move(r); });
    ASSERT_TRUE(waitFor([&] { return result.has_value(); }));
    ASSERT_TRUE(*result);
    const std::string listed = (*result)->dump();
    EXPECT_NE(listed.find(R"("honorifics")"), std::string::npos) << listed;
    EXPECT_NE(listed.find(R"("origin":"project")"), std::string::npos) << listed;
    threads->stop(parent);
    ASSERT_TRUE(idle(parent));
}

TEST_F(AgentTasksTest, QtRefusesWhatTheSidecarAsksBeyondTheRules)
{
    const std::string parent = withMode(AgentMode::Auto);
    Json calls = Json::array({delegate(kHang, scopeOf(document, {1}), Json::object({{"mode", "full"}}))});
    for (const std::int64_t line : {1, 1})
        calls.push(delegate(kHang, scopeOf(document, {line})));
    threads->send(parent, script(std::move(calls), Json::object({{"hang", true}})));
    ASSERT_TRUE(waitFor([&] { return items(parent, "tool").size() == 3u && at(items(parent, "tool")[2], "status") != "running"; }));
    EXPECT_NE(toolOutput(parent, 0).find("mode_escalation_denied"), std::string::npos);
    EXPECT_EQ(at(items(parent, "tool")[1], "status"), "done");
    EXPECT_NE(toolOutput(parent, 2).find("scope_overlap"), std::string::npos);
    threads->stop(parent);
    ASSERT_TRUE(idle(parent));
    ASSERT_TRUE(waitFor([&] { return kids(parent)[0]->task->status == TaskStatus::Stopped; }));
    EXPECT_EQ(kids(parent)[0]->task->stoppedFor, "parent");

    const std::string other = withMode(AgentMode::Auto);
    const Json outside = Json::object({{"calls", Json::array({setLine(revision(), 3, "No", "Outside"),
                                                              delegate(kHang, scopeOf(document, {1}))})}});
    threads->send(other, script(Json::array({delegate(outside, scopeOf(document, {1}),
                                                      Json::object({{"title", "Edge"}, {"wait", true}})),
                                              taskCall("_hikari/task.wait", "t1", Json::object({{"timeoutMs", 20000}}))})));
    ASSERT_TRUE(idle(other));
    ASSERT_EQ(kids(other).size(), 1u);
    const std::string child = kids(other)[0]->id;
    const auto childTools = items(child, "tool");
    ASSERT_EQ(childTools.size(), 2u);
    EXPECT_NE(at(childTools[0], "output").find("outside this sub-agent"), std::string::npos);
    EXPECT_NE(at(childTools[1], "output").find("depth"), std::string::npos);
    EXPECT_EQ(fake.target()->document().lines()[2]->text, u8"General Kenobi") << "outside the child's scope";
}

TEST_F(AgentTasksTest, AFifthRunningChildIsRefusedAndStopStopsEveryChild)
{
    const std::string parent = withMode(AgentMode::FullAccess);
    const Json edits = Json::object({{"calls", Json::array({setLine(revision(), 1, "Hi", "Part")})}, {"hang", true}});
    Json calls = Json::array({delegate(edits, scopeOf(document, {1}))});
    for (std::int64_t i = 2; i <= 5; ++i)
        calls.push(delegate(kHang, scopeOf(i <= 3 ? document : 2, {i <= 3 ? i : i - 3})));
    threads->setAttached(parent, {hikari::application::DocumentId{2}});
    threads->send(parent, script(std::move(calls), Json::object({{"hang", true}})));
    const std::uint64_t estimate = [&] {
        EXPECT_TRUE(waitFor([&] { return !items(parent, "estimate").empty(); }));
        const auto e = items(parent, "estimate");
        return e.empty() ? 0u : static_cast<std::uint64_t>(e[0].find("ask")->asInt());
    }();
    ASSERT_NE(estimate, 0u);
    EXPECT_EQ(items(parent, "estimate")[0].find("children")->asInt(), 3);
    threads->answer(estimate, Answer::AllowOnce);
    ASSERT_TRUE(waitFor([&] { return items(parent, "tool").size() == 5u && at(items(parent, "tool")[4], "status") != "running"; }));
    EXPECT_EQ(items(parent, "estimate").size(), 1u) << "once per turn";
    EXPECT_NE(toolOutput(parent, 4).find("too_many_children"), std::string::npos);
    ASSERT_EQ(kids(parent).size(), 4u);
    threads->stop(parent);
    ASSERT_TRUE(idle(parent));
    ASSERT_TRUE(waitFor([&] {
        return std::ranges::all_of(kids(parent), [](const AgentThread *c) { return c->task->status == TaskStatus::Stopped; });
    }));
    EXPECT_EQ(fake.target()->document().lines()[0]->text, u8"Hi");
    EXPECT_EQ(fake.review.changes(*fake.workspace().editingTarget()).size(), 1u) << "a stopped child's edits stay";
}

TEST_F(AgentTasksTest, DecliningTheEstimateRefusesTheThirdChildAndTheRestOfTheTurn)
{
    const std::string parent = withMode(AgentMode::FullAccess);
    Json calls = Json::array();
    for (const std::int64_t line : {1, 2, 3})
        calls.push(delegate("ok", scopeOf(document, {line})));
    threads->send(parent, script(std::move(calls)));
    ASSERT_TRUE(waitFor([&] { return !items(parent, "estimate").empty(); }));
    threads->answer(static_cast<std::uint64_t>(items(parent, "estimate")[0].find("ask")->asInt()), Answer::Deny);
    ASSERT_TRUE(waitFor([&] { return items(parent, "tool").size() == 3u && at(items(parent, "tool")[2], "status") != "running"; }));
    EXPECT_NE(toolOutput(parent, 2).find("declined"), std::string::npos);
    EXPECT_EQ(at(items(parent, "estimate")[0], "status"), "declined");
    EXPECT_EQ(kids(parent).size(), 2u);
}

TEST_F(AgentTasksTest, AChildsQuestionIsTheUsersAndItsJudgeSeesTheRootsMessagesAndTheBrief)
{
    const std::string parent = withMode(AgentMode::Auto);
    const Json child = Json::object({{"calls", Json::array({setLine(revision(), 1, "Hi", "Fix")})}});
    threads->send(parent, script(Json::array({delegate(child, scopeOf(document, {1}), Json::object({{"mode", "manual"}}))})));
    ASSERT_TRUE(waitFor([&] { return !threads->asks().empty(); }));
    const auto &ask = threads->asks().begin()->second;
    ASSERT_EQ(kids(parent).size(), 1u);
    EXPECT_EQ(ask.thread, kids(parent)[0]->id) << "the child asks; the parent's model never sees it";
    EXPECT_EQ(ask.kind, Decision::Propose);
    threads->answer(threads->asks().begin()->first, Answer::AllowOnce);
    ASSERT_TRUE(waitFor([&] { return kids(parent)[0]->task->status == TaskStatus::Done; }));

    const std::string judged = withMode(AgentMode::Auto);
    const Json reads = Json::object({{"calls", Json::array({call("lines.list"), setLine(revision(), 1, "Yo", "Fix")})}});
    threads->send(judged, script(Json::array({delegate(reads, scopeOf(document, {1}), Json::object({{"title", "Judged"}}))})));
    ASSERT_TRUE(waitFor([&] {
        const auto k = kids(judged);
        return k.size() == 1 && k[0]->task->status != TaskStatus::Running;
    }));
    std::string sent;
    for (const Json &c : receivedAs("_hikari/complete"))
        sent = c.dump();
    EXPECT_NE(sent.find("Another AI agent wrote its task"), std::string::npos);
    EXPECT_NE(sent.find("_hikari/task.request"), std::string::npos) << "the root's user message is the script";
    EXPECT_EQ(sent.find("General Kenobi"), std::string::npos) << "no tool output";
}

TEST_F(AgentTasksTest, ChildrenCostCountsInTheParentsMeterAndCapAndASidecarCrashInterruptsThem)
{
    defaults.threadCap = 1.0;
    const std::string parent = withMode(AgentMode::FullAccess);
    const Json costly = Json::object({{"calls", Json::array()}, {"hang", true},
                                      {"usage", Json::object({{"inputTokens", 1}, {"reportedCost", 1.5}})}});
    threads->send(parent, script(Json::array({delegate(costly, scopeOf(document, {1})),
                                              delegate(kHang, scopeOf(document, {2}))}),
                                 Json::object({{"hang", true}})));
    ASSERT_TRUE(waitFor([&] { return kids(parent).size() == 1 && kids(parent)[0]->task->status == TaskStatus::Stopped; }));
    ASSERT_TRUE(idle(parent));
    EXPECT_DOUBLE_EQ(thread(parent).cost, 1.5);
    EXPECT_EQ(kids(parent)[0]->task->stoppedFor, "cap");
    EXPECT_EQ(notice(parent), "cap");
    EXPECT_NE(toolOutput(parent, 1).find("cost_cap"), std::string::npos);
    EXPECT_DOUBLE_EQ(threads->spentToday(), 1.5) << "counted once";

    defaults.threadCap = 0;
    const std::string other = withMode(AgentMode::FullAccess);
    const QString marker = dir.filePath(QStringLiteral("crashed"));
    const Json crash = Json::object({{"crashUnless", marker.toStdString()}});
    threads->send(other, script(Json::array({delegate(kHang, scopeOf(document, {1})),
                                             delegate(crash, scopeOf(document, {3}))}),
                                Json::object({{"hang", true}})));
    ASSERT_TRUE(waitFor([&] {
        const auto k = kids(other);
        return k.size() == 2 && std::ranges::all_of(k, [](const AgentThread *c) { return c->task->status == TaskStatus::Interrupted; });
    }));
    EXPECT_EQ(notice(other), "lost");
    EXPECT_FALSE(threads->running(kids(other)[0]->id)) << "never rerun";
}
