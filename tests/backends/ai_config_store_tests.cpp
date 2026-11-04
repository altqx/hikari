#include "hikari/backends/ai_config_store.h"

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <map>

namespace app = hikari::application::ai;
using hikari::backends::ai::JsonConfigStore;

namespace {

constexpr const char *kKey = "sk-test-0123456789abcdef";

struct Keys final : app::KeyStore {
    std::map<std::string, std::string> keys;
    void read(const std::string &id, std::function<void(app::KeyResult)> done) override { done(keys.at(id)); }
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

}

TEST(AiConfigStore, TheConfigFileKeepsEverythingButTheKey)
{
    Keys keys;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ai/providers.json"));
    JsonConfigStore store(path);
    app::Providers providers(store, keys);

    app::Provider p = app::presetProvider(app::Preset::Custom, "mock", "Mock");
    p.baseUrl = "https://llm.example/v1";
    p.signedIn = true;
    p.models = {{.id = "m1", .name = "M1", .capabilities = app::Capabilities{.tools = true}, .price = app::Price{.input = 1, .output = 2, .cachedInput = 0.5, .cacheWrite = 1.25, .cacheWriteHour = 2}, .contextTokens = 8000}};
    p.capabilityOverrides["m2"] = {.tools = false, .vision = true, .video = true};
    std::optional<std::optional<app::SaveError>> saved;
    providers.add(p, kKey, [&](std::optional<app::SaveError> e) { saved = e; });
    ASSERT_TRUE(saved.has_value());
    ASSERT_EQ(*saved, std::nullopt);
    ASSERT_TRUE(providers.assign(app::Role::Judge, {"mock", "m1"}));
    EXPECT_EQ(keys.keys.at("mock"), kKey);

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    EXPECT_FALSE(text.contains(kKey));
    EXPECT_FALSE(text.contains("sk-"));
    EXPECT_EQ(JsonConfigStore(path).load(), providers.config());
}

TEST(AiConfigStore, WithoutAProviderNoFileIsWritten)
{
    struct NoKeys final : app::KeyStore {
        void read(const std::string &, std::function<void(app::KeyResult)> done) override { done(app::KeyError::NotFound); }
        void write(const std::string &, const std::string &, std::function<void(std::optional<app::KeyError>)> done) override
        {
            done(app::KeyError::Unavailable);
        }
        void remove(const std::string &, std::function<void(std::optional<app::KeyError>)> done) override { done(std::nullopt); }
    } keys;
    QTemporaryDir dir;
    JsonConfigStore store(dir.filePath(QStringLiteral("providers.json")));
    app::Providers providers(store, keys);
    std::optional<std::optional<app::SaveError>> saved;
    providers.add(app::presetProvider(app::Preset::OpenRouter, "or", "OpenRouter"), kKey,
                  [&](std::optional<app::SaveError> e) { saved = e; });
    EXPECT_EQ(saved, std::optional<std::optional<app::SaveError>>(app::SaveError::KeychainUnavailable));
    EXPECT_FALSE(providers.enabled());
    EXPECT_FALSE(QFile::exists(dir.filePath(QStringLiteral("providers.json"))));
}
