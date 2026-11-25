#include "hikari/app/plugin_secrets.h"

#include <QSignalSpy>
#include <QtTest>

#include <functional>
#include <map>
#include <vector>

using hikari::api::Json;
using hikari::app::PluginSecrets;
namespace ai = hikari::application::ai;
namespace plugins = hikari::plugins;

namespace {

struct Keychain {
    std::map<std::string, std::string> keys;
    std::optional<ai::KeyError> failWith;
    bool held = false;
    std::vector<std::function<void()>> pending;
    std::vector<std::string> removed;

    void answer(std::function<void()> reply)
    {
        if (held)
            pending.push_back(std::move(reply));
        else
            reply();
    }
};

class MemoryKeys final : public ai::KeyStore {
public:
    explicit MemoryKeys(Keychain &chain) : m_chain(chain) {}
    void read(const std::string &id, std::function<void(ai::KeyResult)> done) override
    {
        m_chain.answer([this, id, done] {
            if (m_chain.failWith) {
                done(*m_chain.failWith);
                return;
            }
            const auto it = m_chain.keys.find(id);
            done(it == m_chain.keys.end() ? ai::KeyResult(ai::KeyError::NotFound) : ai::KeyResult(it->second));
        });
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        m_chain.answer([this, id, key, done] {
            if (m_chain.failWith) {
                done(m_chain.failWith);
                return;
            }
            m_chain.keys[id] = key;
            done(std::nullopt);
        });
    }
    void remove(const std::string &id, std::function<void(std::optional<ai::KeyError>)> done) override
    {
        m_chain.answer([this, id, done] {
            m_chain.removed.push_back(id);
            const bool had = m_chain.keys.erase(id) > 0;
            done(had ? std::nullopt : std::optional(ai::KeyError::NotFound));
        });
    }

private:
    Keychain &m_chain;
};

plugins::Manifest manifest()
{
    plugins::Manifest m;
    m.id = "com.example.kf";
    m.settings = {{"token", "Token", Json::object({{"type", Json("secret")}}), plugins::SettingType::Secret},
                  {"size", "Size", Json::object({{"type", Json("integer")}}), plugins::SettingType::Integer},
                  {"key", "Key", Json::object({{"type", Json("secret")}}), plugins::SettingType::Secret}};
    return m;
}

}

class PluginSecretsTests : public QObject {
    Q_OBJECT

private slots:
    void theKeychainNameHoldsThePluginAndTheSetting()
    {
        QCOMPARE(PluginSecrets::keychainName("com.example.kf", "token"), std::string("plugin/com.example.kf/token"));
    }

    void loadingReadsEachSecretOnceAndFillsTheValues()
    {
        Keychain chain;
        chain.keys["plugin/com.example.kf/token"] = "s3cret";
        PluginSecrets secrets(std::make_unique<MemoryKeys>(chain));
        QSignalSpy changed(&secrets, &PluginSecrets::changed);
        secrets.load(manifest());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(1).toString(), QStringLiteral("token"));
        secrets.load(manifest());
        QCOMPARE(changed.count(), 1);

        const Json filled = secrets.fill(manifest(), Json::object({{"size", Json(3)}}));
        QVERIFY(filled == Json::object({{"size", Json(3)}, {"token", Json("s3cret")}, {"key", Json()}}));
        QCOMPARE(secrets.text("com.example.kf", "token"), std::optional<std::string>("s3cret"));
        QVERIFY(!secrets.text("com.example.kf", "key"));
    }

    void settingTextKeepsItAndNullRemovesIt()
    {
        Keychain chain;
        PluginSecrets secrets(std::make_unique<MemoryKeys>(chain));
        QSignalSpy notKept(&secrets, &PluginSecrets::notKept);
        QCOMPARE(secrets.set("com.example.kf", "key", Json("abc")), QString());
        QCOMPARE(chain.keys.at("plugin/com.example.kf/key"), std::string("abc"));
        QCOMPARE(secrets.text("com.example.kf", "key"), std::optional<std::string>("abc"));

        QCOMPARE(secrets.set("com.example.kf", "key", Json()), QString());
        QVERIFY(!chain.keys.contains("plugin/com.example.kf/key"));
        QVERIFY(!secrets.text("com.example.kf", "key"));
        QCOMPARE(secrets.set("com.example.kf", "key", Json("")), QString());
        QCOMPARE(notKept.count(), 0);

        QVERIFY(!secrets.set("com.example.kf", "key", Json(3)).isEmpty());
        QVERIFY(!chain.keys.contains("plugin/com.example.kf/key"));
    }

    void aRefusedWriteBringsTheOldValueBack()
    {
        Keychain chain;
        chain.keys["plugin/com.example.kf/token"] = "old";
        PluginSecrets secrets(std::make_unique<MemoryKeys>(chain));
        secrets.load(manifest());
        chain.held = true;
        chain.failWith = ai::KeyError::Denied;
        QSignalSpy notKept(&secrets, &PluginSecrets::notKept);
        QCOMPARE(secrets.set("com.example.kf", "token", Json("new")), QString());
        QCOMPARE(secrets.text("com.example.kf", "token"), std::optional<std::string>("new"));
        for (auto &reply : std::exchange(chain.pending, {}))
            reply();
        QCOMPARE(notKept.count(), 1);
        QCOMPARE(secrets.text("com.example.kf", "token"), std::optional<std::string>("old"));
    }

    void withoutAKeychainSettingIsRefused()
    {
        Keychain chain;
        chain.failWith = ai::KeyError::Unavailable;
        PluginSecrets secrets(std::make_unique<MemoryKeys>(chain));
        secrets.load(manifest());
        QVERIFY(!secrets.set("com.example.kf", "token", Json("x")).isEmpty());
        QVERIFY(!secrets.text("com.example.kf", "token"));
    }

    void forgettingRemovesEverySecretOfThePlugin()
    {
        Keychain chain;
        chain.keys["plugin/com.example.kf/token"] = "t";
        chain.keys["plugin/com.example.kf/key"] = "k";
        chain.keys["plugin/com.example.other/token"] = "o";
        PluginSecrets secrets(std::make_unique<MemoryKeys>(chain));
        secrets.load(manifest());
        secrets.forget(manifest());
        QCOMPARE(chain.keys.size(), std::size_t{1});
        QVERIFY(chain.keys.contains("plugin/com.example.other/token"));
        QVERIFY(!secrets.text("com.example.kf", "token"));
    }
};

QTEST_GUILESS_MAIN(PluginSecretsTests)
#include "plugin_secrets_tests.moc"
