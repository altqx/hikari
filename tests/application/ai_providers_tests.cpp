#include "hikari/application/ai_providers.h"

#include <gtest/gtest.h>

#include <deque>

using namespace hikari::application::ai;

namespace {

struct MemoryConfigs final : ConfigStore {
    Config stored;
    int saves = 0;
    bool failSave = false;
    Config load() override { return stored; }
    bool save(const Config &config) override
    {
        if (failSave)
            return false;
        ++saves;
        stored = config;
        return true;
    }
};

struct FakeKeys final : KeyStore {
    std::map<std::string, std::string> keys;
    std::optional<KeyError> failWith;
    bool deferReads = false;
    std::deque<std::function<void()>> deferred;
    int reads = 0;

    void read(const std::string &id, std::function<void(KeyResult)> done) override
    {
        ++reads;
        auto answer = [this, id, done] {
            if (failWith)
                done(*failWith);
            else if (const auto it = keys.find(id); it != keys.end())
                done(it->second);
            else
                done(KeyError::NotFound);
        };
        if (deferReads)
            deferred.push_back(answer);
        else
            answer();
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<KeyError>)> done) override
    {
        if (failWith) {
            done(failWith);
            return;
        }
        keys[id] = key;
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<KeyError>)> done) override
    {
        done(keys.erase(id) ? std::nullopt : std::optional(KeyError::NotFound));
    }
};

struct FakePort final : ProviderPort {
    struct Call {
        Provider provider;
        std::string key;
        Request request;
        EventSink sink;
    };
    std::vector<Call> calls;
    std::vector<StreamId> cancelled;
    ModelsResult models = std::vector<Model>{};

    StreamId stream(const Provider &provider, const std::string &key, const Request &request, EventSink sink) override
    {
        calls.push_back({provider, key, request, std::move(sink)});
        return calls.size();
    }
    void cancel(StreamId id) override
    {
        cancelled.push_back(id);
        calls[id - 1].sink(Failed{{.kind = ErrorKind::Cancelled}});
    }
    void listModels(const Provider &, const std::string &, std::function<void(ModelsResult)> done) override
    {
        done(models);
    }

    struct CacheCreate {
        std::string key;
        Request prefix;
        std::chrono::seconds ttl;
    };
    std::vector<CacheCreate> cacheCreates;
    std::vector<std::string> cacheDeletes;
    std::optional<Error> cacheFailure;
    void createCache(const Provider &, const std::string &key, const Request &prefix, std::chrono::seconds ttl,
                     std::function<void(CacheResult)> done) override
    {
        cacheCreates.push_back({key, prefix, ttl});
        if (cacheFailure)
            done(*cacheFailure);
        else
            done("cachedContents/" + std::to_string(cacheCreates.size()));
    }
    void deleteCache(const Provider &, const std::string &, const std::string &name) override
    {
        cacheDeletes.push_back(name);
    }
};

Provider openRouter()
{
    Provider p = presetProvider(Preset::OpenRouter, "or", "OpenRouter");
    p.models = {{.id = "a/model", .name = "A", .capabilities = Capabilities{.tools = true, .vision = false}}};
    return p;
}

struct AiProvidersTest : ::testing::Test {
    MemoryConfigs configs;
    FakeKeys keys;
    FakePort port;

    std::optional<SaveError> add(Providers &providers, Provider p, std::string key)
    {
        std::optional<std::optional<SaveError>> result;
        providers.add(std::move(p), std::move(key), [&](std::optional<SaveError> e) { result = e; });
        EXPECT_TRUE(result.has_value());
        return result.value_or(SaveError::Config);
    }
};

}

TEST(AiConfig, IsOffUntilAProviderIsAdded)
{
    Config config;
    EXPECT_FALSE(config.enabled());
    config.providers.push_back(openRouter());
    EXPECT_TRUE(config.enabled());
}

TEST(AiConfig, ARoleWithoutItsOwnModelUsesTheChatModel)
{
    Config config;
    config.providers.push_back(openRouter());
    EXPECT_EQ(config.modelFor(Role::Judge), std::nullopt);
    config.roles[Role::Chat] = {"or", "a/model"};
    config.roles[Role::Translator] = {"or", "b/model"};
    EXPECT_EQ(config.modelFor(Role::Translator), (ModelRef{"or", "b/model"}));
    EXPECT_EQ(config.modelFor(Role::Judge), (ModelRef{"or", "a/model"}));
    config.roles[Role::Reviewer] = {"gone", "x"};
    EXPECT_EQ(config.modelFor(Role::Reviewer), (ModelRef{"or", "a/model"}));
}

TEST(AiConfig, CapabilitiesComeFromTheUserThenTheCatalogueThenTheProvider)
{
    Provider p = openRouter();
    p.defaults = {.tools = false, .vision = true};
    EXPECT_EQ(capabilitiesOf(p, "a/model"), (Capabilities{.tools = true, .vision = false}));
    EXPECT_EQ(capabilitiesOf(p, "unknown"), (Capabilities{.tools = false, .vision = true}));
    p.capabilityOverrides["a/model"] = {.tools = false, .vision = true};
    EXPECT_EQ(capabilitiesOf(p, "a/model"), (Capabilities{.tools = false, .vision = true}));
}

TEST(AiConfig, CostIsTheProvidersFigureElseThePriceTimesTheTokens)
{
    const Usage usage{.inputTokens = 1'000'000, .outputTokens = 500'000, .cachedInputTokens = 400'000};
    EXPECT_EQ(costOf(usage, std::nullopt), std::nullopt);
    const Price price{.input = 3.0, .output = 15.0, .cachedInput = 0.3};
    EXPECT_DOUBLE_EQ(*costOf(usage, price), 0.6 * 3.0 + 0.4 * 0.3 + 0.5 * 15.0);
    EXPECT_DOUBLE_EQ(*costOf(usage, Price{.input = 2.0, .output = 4.0}), 2.0 + 2.0);
    Usage reported = usage;
    reported.reportedCost = 0.0123;
    EXPECT_DOUBLE_EQ(*costOf(reported, price), 0.0123);
}

TEST(AiConfig, CacheWritesCostTheWritePriceAndReadsTheCachedPrice)
{
    const Usage usage{.inputTokens = 1'000'000, .outputTokens = 0, .cachedInputTokens = 500'000, .cacheWriteTokens = 200'000};
    const Price price{.input = 3.0, .output = 15.0, .cachedInput = 0.3, .cacheWrite = 3.75};
    EXPECT_DOUBLE_EQ(*costOf(usage, price), 0.3 * 3.0 + 0.5 * 0.3 + 0.2 * 3.75);
    EXPECT_DOUBLE_EQ(*costOf(usage, Price{.input = 2.0, .output = 4.0}), 2.0);
    const Usage hour{.inputTokens = 1'000'000, .cacheWriteTokens = 1'000'000, .cacheWriteHourTokens = 400'000};
    const Price hourPrice{.input = 3.0, .output = 15.0, .cacheWrite = 3.75, .cacheWriteHour = 6.0};
    EXPECT_DOUBLE_EQ(*costOf(hour, hourPrice), 0.6 * 3.75 + 0.4 * 6.0);
    const Usage uncached{.inputTokens = 1'000'000};
    EXPECT_GT(*costOf(uncached, price), *costOf(Usage{.inputTokens = 1'000'000, .cachedInputTokens = 900'000}, price));
}

TEST(AiConfig, IdsRoundTrip)
{
    for (const Role role : kRoles)
        EXPECT_EQ(roleFromId(roleId(role)), role);
    for (const Wire wire : {Wire::ChatCompletions, Wire::Responses, Wire::AnthropicMessages})
        EXPECT_EQ(wireFromId(wireId(wire)), wire);
    for (const Preset preset : {Preset::OpenRouter, Preset::OpenAI, Preset::Anthropic, Preset::Custom})
        EXPECT_EQ(presetFromId(presetId(preset)), preset);
    EXPECT_EQ(roleFromId("nope"), std::nullopt);
}

TEST_F(AiProvidersTest, WithoutAProviderNothingIsSent)
{
    Providers providers(configs, keys, port);
    std::vector<Event> events;
    providers.stream(Role::Chat, Request{}, [&](const Event &e) { events.push_back(e); });
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<Failed>(events[0]).error.kind, ErrorKind::NoProvider);
    EXPECT_TRUE(port.calls.empty());
    EXPECT_EQ(keys.reads, 0);
}

TEST_F(AiProvidersTest, AProviderIsAddedOnlyOnceItsKeyIsInTheKeychain)
{
    Providers providers(configs, keys, port);
    keys.failWith = KeyError::Unavailable;
    EXPECT_EQ(add(providers, openRouter(), "sk-secret"), SaveError::KeychainUnavailable);
    EXPECT_FALSE(providers.enabled());
    EXPECT_EQ(configs.saves, 0);

    keys.failWith = KeyError::Denied;
    EXPECT_EQ(add(providers, openRouter(), "sk-secret"), SaveError::KeychainDenied);
    EXPECT_FALSE(providers.enabled());

    keys.failWith.reset();
    EXPECT_EQ(add(providers, openRouter(), ""), SaveError::Key);
    EXPECT_EQ(add(providers, openRouter(), "sk-secret"), std::nullopt);
    EXPECT_TRUE(providers.enabled());
    EXPECT_EQ(keys.keys.at("or"), "sk-secret");
    EXPECT_EQ(configs.stored.providers.size(), 1u);
    EXPECT_EQ(configs.stored.roles.at(Role::Chat), (ModelRef{"or", "a/model"}));
}

TEST_F(AiProvidersTest, AKeylessLocalServerNeedsNoKeychain)
{
    Providers providers(configs, keys, port);
    keys.failWith = KeyError::Unavailable;
    Provider local = presetProvider(Preset::Custom, "ollama", "Ollama");
    local.baseUrl = "http://127.0.0.1:11434/v1";
    EXPECT_EQ(add(providers, local, ""), std::nullopt);
    ASSERT_TRUE(providers.assign(Role::Chat, {"ollama", "llama"}));
    providers.stream(Role::Chat, Request{}, [](const Event &) {});
    ASSERT_EQ(port.calls.size(), 1u);
    EXPECT_EQ(port.calls[0].key, "");
    EXPECT_EQ(keys.reads, 0);
}

TEST_F(AiProvidersTest, RequestsUseTheRolesModelAndTheStoredKey)
{
    keys.keys["or"] = "sk-stored";
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    configs.stored.roles[Role::Translator] = {"or", "t/model"};
    Providers providers(configs, keys, port);

    std::vector<Event> events;
    providers.stream(Role::Translator, Request{.system = "s"}, [&](const Event &e) { events.push_back(e); });
    ASSERT_EQ(port.calls.size(), 1u);
    EXPECT_EQ(port.calls[0].key, "sk-stored");
    EXPECT_EQ(port.calls[0].request.model, "t/model");
    EXPECT_EQ(port.calls[0].request.system, "s");
    port.calls[0].sink(TextDelta{"hi"});
    port.calls[0].sink(Finished{});
    EXPECT_EQ(events.size(), 2u);

    providers.stream(Role::Judge, Request{}, [](const Event &) {});
    EXPECT_EQ(port.calls.at(1).request.model, "a/model");
    EXPECT_EQ(keys.reads, 1);
}

TEST_F(AiProvidersTest, AMissingKeyFailsTheRequestWithoutSendingIt)
{
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    Providers providers(configs, keys, port);
    std::vector<Event> events;
    providers.stream(Role::Chat, Request{}, [&](const Event &e) { events.push_back(e); });
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<Failed>(events[0]).error.kind, ErrorKind::Key);
    EXPECT_TRUE(port.calls.empty());
}

TEST_F(AiProvidersTest, CancelBeforeTheKeyArrivesSendsNothing)
{
    keys.keys["or"] = "sk";
    keys.deferReads = true;
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    Providers providers(configs, keys, port);
    std::vector<Event> events;
    const StreamId id = providers.stream(Role::Chat, Request{}, [&](const Event &e) { events.push_back(e); });
    providers.cancel(id);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<Failed>(events[0]).error.kind, ErrorKind::Cancelled);
    keys.deferred.front()();
    EXPECT_TRUE(port.calls.empty());
    EXPECT_EQ(events.size(), 1u);
}

TEST_F(AiProvidersTest, CancelAfterSendingCancelsTheNetworkStream)
{
    keys.keys["or"] = "sk";
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    Providers providers(configs, keys, port);
    std::vector<Event> events;
    const StreamId id = providers.stream(Role::Chat, Request{}, [&](const Event &e) { events.push_back(e); });
    providers.cancel(id);
    EXPECT_EQ(port.cancelled, std::vector<StreamId>{1});
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<Failed>(events[0]).error.kind, ErrorKind::Cancelled);
}

TEST_F(AiProvidersTest, RemovingAProviderDropsItsKeyAndRoles)
{
    Providers providers(configs, keys, port);
    ASSERT_EQ(add(providers, openRouter(), "sk"), std::nullopt);
    ASSERT_TRUE(providers.assign(Role::Vision, {"or", "v"}));
    EXPECT_FALSE(providers.assign(Role::Vision, {"nobody", "v"}));
    std::optional<std::optional<SaveError>> removed;
    providers.remove("or", [&](std::optional<SaveError> e) { removed = e; });
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, std::nullopt);
    EXPECT_FALSE(providers.enabled());
    EXPECT_TRUE(configs.stored.roles.empty());
    EXPECT_FALSE(keys.keys.contains("or"));
}

TEST_F(AiProvidersTest, RefreshingTheCatalogueKeepsTheUsersCorrections)
{
    Providers providers(configs, keys, port);
    ASSERT_EQ(add(providers, openRouter(), "sk"), std::nullopt);
    ASSERT_TRUE(providers.setCapabilities("or", "b/model", {.tools = true, .vision = true}));
    port.models = std::vector<Model>{{.id = "b/model", .name = "B"}};
    std::optional<std::optional<Error>> done;
    providers.refreshModels("or", [&](std::optional<Error> e) { done = e; });
    ASSERT_TRUE(done.has_value());
    EXPECT_EQ(*done, std::nullopt);
    const Provider &p = *providers.config().provider("or");
    ASSERT_EQ(p.models.size(), 1u);
    EXPECT_EQ(p.models[0].id, "b/model");
    EXPECT_EQ(capabilitiesOf(p, "b/model"), (Capabilities{.tools = true, .vision = true}));

    port.models = Error{.kind = ErrorKind::Unauthorized, .httpStatus = 401};
    providers.refreshModels("or", [&](std::optional<Error> e) { done = e; });
    EXPECT_EQ((*done)->kind, ErrorKind::Unauthorized);
    EXPECT_EQ(providers.config().provider("or")->models.size(), 1u);
}

Request scriptTurn(std::string context = "The whole script, 1200 Lines.")
{
    return Request{.system = "Translate.",
                   .tools = {{.name = "set_text", .description = "d", .parametersJson = "{}"}},
                   .context = std::move(context),
                   .cacheKey = "thread-1",
                   .cacheLifetime = CacheLifetime::Hour,
                   .explicitCache = true};
}

TEST_F(AiProvidersTest, AnExplicitCacheIsCreatedReusedAndDeleted)
{
    auto now = std::chrono::steady_clock::time_point{};
    keys.keys["or"] = "sk";
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    Providers providers(configs, keys, port);
    providers.setClock([&] { return now; });

    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    ASSERT_EQ(port.cacheCreates.size(), 1u);
    EXPECT_EQ(port.cacheCreates[0].key, "sk");
    EXPECT_EQ(port.cacheCreates[0].ttl, std::chrono::hours(1));
    EXPECT_EQ(port.cacheCreates[0].prefix.context, "The whole script, 1200 Lines.");
    EXPECT_EQ(port.cacheCreates[0].prefix.model, "a/model");
    EXPECT_EQ(port.calls.at(0).request.providerCache, "cachedContents/1");

    now += std::chrono::minutes(50);
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    EXPECT_EQ(port.cacheCreates.size(), 1u);
    EXPECT_EQ(port.calls.at(1).request.providerCache, "cachedContents/1");

    now += std::chrono::minutes(10);
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    EXPECT_EQ(port.cacheCreates.size(), 2u) << "an expiring cache is made again";
    EXPECT_EQ(port.cacheDeletes, std::vector<std::string>{"cachedContents/1"});
    EXPECT_EQ(port.calls.at(2).request.providerCache, "cachedContents/2");

    providers.stream(Role::Chat, scriptTurn("Another script."), [](const Event &) {});
    EXPECT_EQ(port.cacheCreates.size(), 3u) << "a new prefix needs a new cache";
    EXPECT_EQ(port.cacheDeletes.back(), "cachedContents/2");

    providers.endConversation("thread-1");
    EXPECT_EQ(port.cacheDeletes.back(), "cachedContents/3");
    providers.endConversation("thread-1");
    EXPECT_EQ(port.cacheDeletes.size(), 3u);
}

TEST_F(AiProvidersTest, AFailedCacheCreateFallsBackToAWholeRequest)
{
    keys.keys["or"] = "sk";
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    Providers providers(configs, keys, port);

    port.cacheFailure = Error{.kind = ErrorKind::Network};
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    EXPECT_EQ(port.calls.at(0).request.providerCache, "");
    EXPECT_EQ(port.calls.at(0).request.context, "The whole script, 1200 Lines.");
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    EXPECT_EQ(port.cacheCreates.size(), 2u) << "a network failure is tried again";

    port.cacheFailure = Error{.kind = ErrorKind::Rejected, .httpStatus = 400};
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    EXPECT_EQ(port.cacheCreates.size(), 3u) << "a refusal isn't";
    EXPECT_EQ(port.calls.at(3).request.providerCache, "");

    port.cacheFailure.reset();
    providers.stream(Role::Chat, scriptTurn("Another script."), [](const Event &) {});
    EXPECT_EQ(port.calls.at(4).request.providerCache, "cachedContents/4");
    EXPECT_TRUE(port.cacheDeletes.empty());

    Request plain = scriptTurn();
    plain.explicitCache = false;
    providers.stream(Role::Chat, plain, [](const Event &) {});
    EXPECT_EQ(port.cacheCreates.size(), 4u);
    EXPECT_EQ(port.calls.at(5).request.providerCache, "");
}

TEST_F(AiProvidersTest, ARequestThatFailsOnItsCacheDropsTheCache)
{
    keys.keys["or"] = "sk";
    configs.stored.providers.push_back(openRouter());
    configs.stored.roles[Role::Chat] = {"or", "a/model"};
    Providers providers(configs, keys, port);
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    port.calls.at(0).sink(Failed{{.kind = ErrorKind::Rejected, .httpStatus = 404}});
    EXPECT_EQ(port.cacheDeletes, std::vector<std::string>{"cachedContents/1"});
    providers.stream(Role::Chat, scriptTurn(), [](const Event &) {});
    EXPECT_EQ(port.calls.at(1).request.providerCache, "cachedContents/2");

    std::optional<std::optional<SaveError>> removed;
    providers.remove("or", [&](std::optional<SaveError> e) { removed = e; });
    EXPECT_EQ(port.cacheDeletes.back(), "cachedContents/2");
}

TEST_F(AiProvidersTest, AConfigThatCannotBeSavedChangesNothing)
{
    Providers providers(configs, keys, port);
    configs.failSave = true;
    EXPECT_EQ(add(providers, openRouter(), "sk"), SaveError::Config);
    EXPECT_FALSE(providers.enabled());
}
