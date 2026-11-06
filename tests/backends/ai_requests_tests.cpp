#include "hikari/backends/ai_http_relay.h"
#include "hikari/backends/ai_requests.h"
#include "hikari/backends/ai_sidecar_host.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <deque>

using namespace hikari::backends::ai;
namespace app = hikari::application::ai;
using hikari::api::Json;

namespace {

constexpr const char *kBuildId = "0.1.0+test";
constexpr const char *kKey = "sk-test-0123456789abcdef";

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

struct MemoryConfigs final : app::ConfigStore {
    app::Config stored;
    app::Config load() override { return stored; }
    bool save(const app::Config &config) override
    {
        stored = config;
        return true;
    }
};

struct Keys final : app::KeyStore {
    std::map<std::string, std::string> keys;
    int reads = 0;

    void read(const std::string &id, std::function<void(app::KeyResult)> done) override
    {
        ++reads;
        if (const auto it = keys.find(id); it != keys.end())
            done(it->second);
        else
            done(app::KeyError::NotFound);
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<app::KeyError>)> done) override
    {
        keys[id] = key;
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<app::KeyError>)> done) override
    {
        keys.erase(id);
        done(std::nullopt);
    }
};

struct Stream {
    std::vector<app::Event> events;
    bool done() const
    {
        return !events.empty() &&
               (std::holds_alternative<app::Finished>(events.back()) || std::holds_alternative<app::Failed>(events.back()));
    }
    app::EventSink sink()
    {
        return [this](const app::Event &e) { events.push_back(e); };
    }
    app::Error error() const { return std::get<app::Failed>(events.back()).error; }
};

class AiRequestsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_requests_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override
    {
        app::Provider p = app::presetProvider(app::Preset::OpenRouter, "or", "OpenRouter");
        p.models = {{.id = "a/model", .name = "A"}};
        openRouter = p;
    }

    AiSidecarHost *sidecar()
    {
        ++hostsAskedFor;
        if (!host) {
            AiSidecarHost::Options o;
            o.program = QStringLiteral(HIKARI_FAKE_AI_SIDECAR);
            o.arguments = {QStringLiteral("--build-id"), QString::fromLatin1(kBuildId), QStringLiteral("--api-version"),
                           QString::number(hikari::api::kApiVersion)};
            o.workingDirectory = dir.filePath(QStringLiteral("ai/work"));
            o.buildId = kBuildId;
            o.shutdownMs = 600;
            host = std::make_unique<AiSidecarHost>(std::move(o), nullptr, hikari::api::Caller{"ai", "AI"});
        }
        return host.get();
    }

    void configure(std::initializer_list<std::pair<app::Role, std::string>> roles)
    {
        configs.stored.providers.push_back(openRouter);
        for (const auto &[role, model] : roles)
            configs.stored.roles[role] = {"or", model};
        keys.keys["or"] = kKey;
    }

    Json received()
    {
        std::optional<SidecarResult> result;
        sidecar()->request("received", {}, [&](SidecarResult r) { result = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return result.has_value(); }));
        return result && *result ? **result : Json::array();
    }

    static Json chat() { return Json::object({{"messages", Json::array()}}); }

    QTemporaryDir dir;
    MemoryConfigs configs;
    Keys keys;
    app::Provider openRouter;
    std::unique_ptr<AiSidecarHost> host;
    int hostsAskedFor = 0;
};

}

TEST_F(AiRequestsTest, WithoutAProviderNothingIsSentAndNoSidecarStarts)
{
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    Stream run;
    requests.complete(app::Role::Chat, chat(), run.sink());
    ASSERT_EQ(run.events.size(), 1u);
    EXPECT_EQ(run.error().kind, app::ErrorKind::NoProvider);
    std::optional<app::Error> refreshed;
    requests.refreshModels("or", [&](std::optional<app::Error> e) { refreshed = e; });
    EXPECT_EQ(refreshed->kind, app::ErrorKind::NoProvider);
    EXPECT_EQ(hostsAskedFor, 0);
    EXPECT_EQ(keys.reads, 0);
}

TEST_F(AiRequestsTest, WithAiOffOrNoSidecarInstalledTheRequestFails)
{
    configure({{app::Role::Chat, "a/model"}});
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [] { return nullptr; });
    Stream run;
    requests.complete(app::Role::Chat, chat(), run.sink());
    ASSERT_EQ(run.events.size(), 1u);
    EXPECT_EQ(run.error().kind, app::ErrorKind::Network);
}

TEST_F(AiRequestsTest, TheRelayReachesAProviderWithItsKeychainKey)
{
    configure({{app::Role::Chat, "a/model"}});
    app::Provider local = app::presetProvider(app::Preset::Custom, "ollama", "Ollama");
    local.baseUrl = "http://127.0.0.1:11434/v1";
    local.needsKey = false;
    configs.stored.providers.push_back(local);
    app::Provider locked = app::presetProvider(app::Preset::OpenAI, "locked", "Locked");
    configs.stored.providers.push_back(locked);
    app::Providers providers(configs, keys);
    const RelayTargets targets = providerTargets(providers);
    const auto lookup = [&](Json target) {
        std::optional<RelayTargetResult> found;
        targets(target, [&](RelayTargetResult r) { found = std::move(r); });
        EXPECT_TRUE(waitFor([&] { return found.has_value(); }));
        return found.value_or(std::unexpected(app::Error{}));
    };
    const auto provider = [](const char *id) { return Json::object({{"kind", "provider"}, {"id", id}}); };

    const auto openRouterTarget = lookup(provider("or"));
    ASSERT_TRUE(openRouterTarget);
    EXPECT_EQ(openRouterTarget->base, QUrl(QString::fromStdString(openRouter.baseUrl)));
    EXPECT_EQ(openRouterTarget->key, kKey);
    const auto keyless = lookup(provider("ollama"));
    ASSERT_TRUE(keyless);
    EXPECT_EQ(keyless->key, "");
    EXPECT_EQ(lookup(provider("locked")).error().kind, app::ErrorKind::Key);
    EXPECT_EQ(lookup(provider("nobody")).error().kind, app::ErrorKind::NoProvider);
    EXPECT_EQ(lookup(Json::object({{"kind", "search"}, {"id", "or"}})).error().kind, app::ErrorKind::Rejected);
    EXPECT_EQ(keys.reads, 2);
}

TEST_F(AiRequestsTest, RequestsCarryTheRolesModelButNeverAKeyAndStreamTheAnswer)
{
    configure({{app::Role::Chat, "a/model"}, {app::Role::Translator, "t/model"}});
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    Stream run;
    const app::StreamId id = requests.complete(app::Role::Translator, chat(), run.sink());
    ASSERT_TRUE(waitFor([&] { return run.done(); }));
    ASSERT_EQ(run.events.size(), 2u);
    EXPECT_EQ(std::get<app::TextDelta>(run.events[0]).text, "Hi");
    const auto &finished = std::get<app::Finished>(run.events[1]);
    EXPECT_EQ(finished.reason, app::StopReason::End);
    EXPECT_EQ(finished.text, "Hi");
    EXPECT_EQ(finished.usage,
              (app::Usage{.inputTokens = 3, .outputTokens = 1, .cacheWriteTokens = 2, .cacheWriteHourTokens = 1}));

    const Json sent = received();
    ASSERT_EQ(sent.asArray().size(), 2u);
    EXPECT_EQ(sent.asArray()[0], Json::object({{"method", "_hikari/providers.set"},
                                               {"params", Json::object({{"providers", Json::array({Json::object({
                                                   {"id", "or"},
                                                   {"preset", "openrouter"},
                                                   {"wire", "chat-completions"},
                                                   {"baseUrl", openRouter.baseUrl},
                                               })})}})}}));
    const Json &complete = *sent.asArray()[1].find("params");
    EXPECT_EQ(*complete.find("stream"), Json(id));
    EXPECT_EQ(*complete.find("providerId"), Json("or"));
    EXPECT_FALSE(complete.find("key"));
    EXPECT_EQ(sent.dump().find(kKey), std::string::npos);
    EXPECT_EQ(*complete.find("request")->find("model"), Json("t/model"));

    Stream second;
    requests.complete(app::Role::Judge, chat(), second.sink());
    ASSERT_TRUE(waitFor([&] { return second.done(); }));
    EXPECT_EQ(*received().asArray()[3].find("params")->find("request")->find("model"), Json("a/model"));
    EXPECT_EQ(keys.reads, 0);
}

TEST_F(AiRequestsTest, AProviderFailureKeepsItsKindStatusAndMessage)
{
    configure({{app::Role::Chat, "fail"}});
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    Stream run;
    requests.complete(app::Role::Chat, chat(), run.sink());
    ASSERT_TRUE(waitFor([&] { return run.done(); }));
    ASSERT_EQ(run.events.size(), 1u);
    EXPECT_EQ(run.error(), (app::Error{.kind = app::ErrorKind::RateLimited, .httpStatus = 429, .detail = "slow down"}));
}

TEST_F(AiRequestsTest, CancelAfterSendingCancelsTheSidecarRequest)
{
    configure({{app::Role::Chat, "hang"}});
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    Stream run;
    const app::StreamId id = requests.complete(app::Role::Chat, chat(), run.sink());
    ASSERT_EQ(received().asArray().size(), 2u);
    requests.cancel(id);
    ASSERT_TRUE(waitFor([&] { return run.done(); }));
    ASSERT_EQ(run.events.size(), 1u);
    EXPECT_EQ(run.error().kind, app::ErrorKind::Cancelled);
}

TEST_F(AiRequestsTest, RefreshingTheCatalogueKeepsTheUsersCorrections)
{
    configure({{app::Role::Chat, "a/model"}});
    app::Providers providers(configs, keys);
    ASSERT_TRUE(providers.setCapabilities("or", "b/model", {.tools = true, .vision = true}));
    AiRequests requests(providers, [this] { return sidecar(); });
    std::optional<std::optional<app::Error>> done;
    requests.refreshModels("or", [&](std::optional<app::Error> e) { done = e; });
    ASSERT_TRUE(waitFor([&] { return done.has_value(); }));
    EXPECT_EQ(*done, std::nullopt);
    const app::Provider &p = *providers.config().provider("or");
    ASSERT_EQ(p.models.size(), 1u);
    EXPECT_EQ(p.models[0], (app::Model{.id = "b/model", .name = "B"}));
    EXPECT_EQ(app::capabilitiesOf(p, "b/model"), (app::Capabilities{.tools = true, .vision = true}));
    const Json list = received().asArray().back();
    EXPECT_EQ(list, Json::object({{"method", "_hikari/models.list"},
                                  {"params", Json::object({{"providerId", "or"}})}}));
}

TEST_F(AiRequestsTest, EndingAConversationAsksTheSidecarToDropItsCache)
{
    configure({{app::Role::Chat, "a/model"}});
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    requests.endConversation("nobody", "thread-1");
    requests.endConversation("or", "");
    EXPECT_EQ(hostsAskedFor, 0);
    requests.endConversation("or", "thread-1");
    const Json sent = received();
    ASSERT_EQ(sent.asArray().size(), 2u);
    EXPECT_EQ(sent.asArray()[1], Json::object({{"method", "_hikari/conversation.end"},
                                               {"params", Json::object({{"providerId", "or"},
                                                                        {"cacheKey", "thread-1"}})}}));
}

TEST_F(AiRequestsTest, TestingAProviderBeforeItIsAddedListsItsModelsAndStoresNothing)
{
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    app::Provider draft = app::presetProvider(app::Preset::Gemini, "gemini", "Google Gemini");
    std::optional<AiRequests::Probed> probed;
    requests.probe(draft, "AIza-typed", [&](AiRequests::Probed p) { probed = std::move(p); });
    const auto lookUp = [&] {
        std::optional<RelayTargetResult> found;
        requests.relayTargets()(Json::object({{"kind", "provider"}, {"id", "gemini"}}),
                                [&](RelayTargetResult r) { found = std::move(r); });
        return found;
    };
    const auto during = lookUp();
    ASSERT_TRUE(during && *during);
    EXPECT_EQ((*during)->key, "AIza-typed");
    EXPECT_EQ((*during)->base, QUrl(QString::fromStdString(draft.baseUrl)));
    ASSERT_TRUE(waitFor([&] { return probed.has_value(); }));
    const auto after = lookUp();
    ASSERT_TRUE(after && !*after);
    EXPECT_EQ(after->error().kind, app::ErrorKind::NoProvider);
    ASSERT_TRUE(std::holds_alternative<std::vector<app::Model>>(*probed));
    EXPECT_EQ(std::get<std::vector<app::Model>>(*probed), (std::vector<app::Model>{{.id = "b/model", .name = "B"}}));
    EXPECT_FALSE(providers.enabled());
    EXPECT_TRUE(keys.keys.empty());
    EXPECT_TRUE(configs.stored.providers.empty());

    const Json sent = received();
    ASSERT_EQ(sent.asArray().size(), 2u);
    const Json &set = *sent.asArray()[0].find("params")->find("providers");
    ASSERT_EQ(set.asArray().size(), 1u);
    EXPECT_EQ(*set.asArray()[0].find("id"), Json("gemini"));
    EXPECT_EQ(*set.asArray()[0].find("wire"), Json("gemini"));
    EXPECT_EQ(sent.asArray()[1], Json::object({{"method", "_hikari/models.list"},
                                               {"params", Json::object({{"providerId", "gemini"}})}}));

    std::optional<AiRequests::Probed> offline;
    AiRequests none(providers, [] { return nullptr; });
    none.probe(draft, "AIza-typed", [&](AiRequests::Probed p) { offline = std::move(p); });
    ASSERT_TRUE(offline.has_value());
    EXPECT_EQ(std::get<app::Error>(*offline).kind, app::ErrorKind::Network);
}

TEST_F(AiRequestsTest, WithCachingOffRequestsAskTheSidecarForNoCache)
{
    configure({{app::Role::Chat, "a/model"}});
    app::Providers providers(configs, keys);
    AiRequests requests(providers, [this] { return sidecar(); });
    Stream cached;
    requests.complete(app::Role::Chat, chat(), cached.sink());
    ASSERT_TRUE(waitFor([&] { return cached.done(); }));
    requests.setCaching(false);
    Stream plain;
    requests.complete(app::Role::Chat, chat(), plain.sink());
    ASSERT_TRUE(waitFor([&] { return plain.done(); }));
    const Json sent = received();
    EXPECT_EQ(sent.asArray()[1].find("params")->find("request")->find("caching"), nullptr);
    EXPECT_EQ(*sent.asArray()[3].find("params")->find("request")->find("caching"), Json(false));
}
