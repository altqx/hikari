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

Provider openRouter()
{
    Provider p = presetProvider(Preset::OpenRouter, "or", "OpenRouter");
    p.models = {{.id = "a/model", .name = "A", .capabilities = Capabilities{.tools = true, .vision = false}}};
    return p;
}

struct AiProvidersTest : ::testing::Test {
    MemoryConfigs configs;
    FakeKeys keys;

    std::optional<SaveError> add(Providers &providers, Provider p, std::string key)
    {
        std::optional<std::optional<SaveError>> result;
        providers.add(std::move(p), std::move(key), [&](std::optional<SaveError> e) { result = e; });
        EXPECT_TRUE(result.has_value());
        return result.value_or(SaveError::Config);
    }

    static std::optional<std::string> readKey(Providers &providers, const std::string &id)
    {
        std::optional<std::optional<std::string>> result;
        providers.readKey(id, [&](std::optional<std::string> key) { result = std::move(key); });
        EXPECT_TRUE(result.has_value());
        return result.value_or(std::nullopt);
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
    const Usage usage{.inputTokens = 1'000'000, .outputTokens = 0, .cachedInputTokens = 500'000,
                      .cacheWriteTokens = 300'000, .cacheWriteHourTokens = 100'000};
    const Price price{.input = 3.0, .output = 15.0, .cachedInput = 0.3, .cacheWrite = 3.75, .cacheWriteHour = 6.0};
    EXPECT_DOUBLE_EQ(*costOf(usage, price), 0.2 * 3.0 + 0.5 * 0.3 + 0.2 * 3.75 + 0.1 * 6.0);
    const Price noWritePrice{.input = 3.0, .output = 15.0, .cachedInput = 0.3};
    EXPECT_DOUBLE_EQ(*costOf(usage, noWritePrice), 0.5 * 3.0 + 0.5 * 0.3);
    Price minutesOnly = price;
    minutesOnly.cacheWriteHour.reset();
    EXPECT_DOUBLE_EQ(*costOf(usage, minutesOnly), 0.2 * 3.0 + 0.5 * 0.3 + 0.3 * 3.75);
}

TEST(AiConfig, IdsRoundTrip)
{
    for (const Role role : kRoles)
        EXPECT_EQ(roleFromId(roleId(role)), role);
    for (const Wire wire : {Wire::ChatCompletions, Wire::Responses, Wire::AnthropicMessages, Wire::Gemini})
        EXPECT_EQ(wireFromId(wireId(wire)), wire);
    for (const Preset preset : {Preset::OpenRouter, Preset::OpenAI, Preset::Anthropic, Preset::Gemini, Preset::Custom})
        EXPECT_EQ(presetFromId(presetId(preset)), preset);
    for (int k = 0; k <= static_cast<int>(ErrorKind::Cancelled); ++k)
        EXPECT_EQ(errorKindFromId(errorKindId(static_cast<ErrorKind>(k))), static_cast<ErrorKind>(k));
    EXPECT_EQ(errorKindId(ErrorKind::RateLimited), "rateLimited");
    EXPECT_EQ(stopReasonFromId("toolUse"), StopReason::ToolUse);
    EXPECT_EQ(roleFromId("nope"), std::nullopt);
}

TEST_F(AiProvidersTest, AProviderIsAddedOnlyOnceItsKeyIsInTheKeychain)
{
    Providers providers(configs, keys);
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
    Providers providers(configs, keys);
    keys.failWith = KeyError::Unavailable;
    Provider local = presetProvider(Preset::Custom, "ollama", "Ollama");
    local.baseUrl = "http://127.0.0.1:11434/v1";
    EXPECT_EQ(add(providers, local, ""), std::nullopt);
    ASSERT_TRUE(providers.assign(Role::Chat, {"ollama", "llama"}));
    EXPECT_EQ(readKey(providers, "ollama"), "");
    EXPECT_EQ(keys.reads, 0);
}

TEST_F(AiProvidersTest, AKeyIsReadFromTheKeychainOnce)
{
    keys.keys["or"] = "sk-stored";
    configs.stored.providers.push_back(openRouter());
    Providers providers(configs, keys);
    EXPECT_EQ(readKey(providers, "or"), "sk-stored");
    EXPECT_EQ(readKey(providers, "or"), "sk-stored");
    EXPECT_EQ(keys.reads, 1);
    EXPECT_EQ(readKey(providers, "nobody"), std::nullopt);
}

TEST_F(AiProvidersTest, AMissingKeyReadsAsNone)
{
    configs.stored.providers.push_back(openRouter());
    Providers providers(configs, keys);
    EXPECT_EQ(readKey(providers, "or"), std::nullopt);
}

TEST_F(AiProvidersTest, RemovingAProviderDropsItsKeyAndRoles)
{
    Providers providers(configs, keys);
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

TEST_F(AiProvidersTest, EditingAProviderKeepsItsPlaceRolesAndKeyUnlessGivenANewOne)
{
    Providers providers(configs, keys);
    ASSERT_EQ(add(providers, openRouter(), "sk-old"), std::nullopt);
    Provider local = presetProvider(Preset::Custom, "ollama", "Ollama");
    local.baseUrl = "http://127.0.0.1:11434/v1";
    ASSERT_EQ(add(providers, local, ""), std::nullopt);
    ASSERT_TRUE(providers.assign(Role::Judge, {"or", "a/model"}));

    Provider renamed = *providers.config().provider("or");
    renamed.name = "My OpenRouter";
    std::optional<std::optional<SaveError>> result;
    providers.update(renamed, std::nullopt, [&](std::optional<SaveError> e) { result = e; });
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, std::nullopt);
    EXPECT_EQ(providers.config().providers[0].name, "My OpenRouter");
    EXPECT_EQ(providers.config().providers[1].id, "ollama");
    EXPECT_EQ(keys.keys.at("or"), "sk-old");
    EXPECT_EQ(configs.stored.roles.at(Role::Judge), (ModelRef{"or", "a/model"}));

    result.reset();
    providers.update(renamed, std::string("sk-new"), [&](std::optional<SaveError> e) { result = e; });
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, std::nullopt);
    EXPECT_EQ(keys.keys.at("or"), "sk-new");
    EXPECT_EQ(readKey(providers, "or"), "sk-new");
    EXPECT_EQ(providers.config().providers[0].id, "or");

    result.reset();
    providers.update(presetProvider(Preset::OpenAI, "nobody", "Nobody"), std::string("sk"),
                     [&](std::optional<SaveError> e) { result = e; });
    EXPECT_EQ(result, std::optional<std::optional<SaveError>>(SaveError::Config));
    EXPECT_FALSE(keys.keys.contains("nobody"));
}

TEST_F(AiProvidersTest, UnassigningARoleFallsBackToTheChatModel)
{
    Providers providers(configs, keys);
    ASSERT_EQ(add(providers, openRouter(), "sk"), std::nullopt);
    ASSERT_TRUE(providers.assign(Role::Reviewer, {"or", "b/model"}));
    ASSERT_TRUE(providers.unassign(Role::Reviewer));
    EXPECT_FALSE(configs.stored.roles.contains(Role::Reviewer));
    EXPECT_EQ(providers.config().modelFor(Role::Reviewer), (ModelRef{"or", "a/model"}));
}

TEST_F(AiProvidersTest, TheFirstCatalogueFillsAnEmptyChatRole)
{
    Providers providers(configs, keys);
    Provider bare = presetProvider(Preset::Anthropic, "anthropic", "Anthropic");
    ASSERT_EQ(add(providers, bare, "sk"), std::nullopt);
    EXPECT_FALSE(providers.config().roles.contains(Role::Chat));
    ASSERT_TRUE(providers.setModels("anthropic", {{.id = "claude", .name = "Claude"}}));
    EXPECT_EQ(configs.stored.roles.at(Role::Chat), (ModelRef{"anthropic", "claude"}));
    ASSERT_TRUE(providers.assign(Role::Chat, {"anthropic", "other"}));
    ASSERT_TRUE(providers.setModels("anthropic", {{.id = "claude", .name = "Claude"}}));
    EXPECT_EQ(configs.stored.roles.at(Role::Chat), (ModelRef{"anthropic", "other"}));
}

TEST_F(AiProvidersTest, ANewCatalogueKeepsTheUsersCorrections)
{
    Providers providers(configs, keys);
    ASSERT_EQ(add(providers, openRouter(), "sk"), std::nullopt);
    ASSERT_TRUE(providers.setCapabilities("or", "b/model", {.tools = true, .vision = true}));
    ASSERT_TRUE(providers.setModels("or", {{.id = "b/model", .name = "B"}}));
    const Provider &p = *providers.config().provider("or");
    ASSERT_EQ(p.models.size(), 1u);
    EXPECT_EQ(p.models[0].id, "b/model");
    EXPECT_EQ(capabilitiesOf(p, "b/model"), (Capabilities{.tools = true, .vision = true}));
    EXPECT_EQ(configs.stored, providers.config());
    EXPECT_FALSE(providers.setModels("nobody", {}));
}

TEST_F(AiProvidersTest, AGeminiModelCanFillEveryRole)
{
    Providers providers(configs, keys);
    const Provider google = presetProvider(Preset::Gemini, "google", "Google");
    EXPECT_EQ(google.wire, Wire::Gemini);
    EXPECT_EQ(google.baseUrl, "https://generativelanguage.googleapis.com/v1beta");
    EXPECT_TRUE(google.needsKey);
    ASSERT_EQ(add(providers, google, "AIza-secret"), std::nullopt);
    ASSERT_TRUE(providers.setModels("google", {{.id = "gemini-x-flash", .name = "Gemini X Flash"}}));
    const Provider &stored = *providers.config().provider("google");
    EXPECT_EQ(capabilitiesOf(stored, "gemini-x-flash"), (Capabilities{.tools = true, .vision = true, .video = true}));

    for (const Role role : kRoles) {
        ASSERT_TRUE(providers.assign(role, {"google", "gemini-x-flash"}));
        EXPECT_EQ(providers.config().modelFor(role), (ModelRef{"google", "gemini-x-flash"}));
    }
    EXPECT_EQ(readKey(providers, "google"), "AIza-secret");
}

TEST_F(AiProvidersTest, AConfigThatCannotBeSavedChangesNothing)
{
    Providers providers(configs, keys);
    configs.failSave = true;
    EXPECT_EQ(add(providers, openRouter(), "sk"), SaveError::Config);
    EXPECT_FALSE(providers.enabled());
}
