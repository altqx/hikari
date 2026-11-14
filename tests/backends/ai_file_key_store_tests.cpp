#include "hikari/backends/ai_file_key_store.h"
#include "hikari/backends/sandbox.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <map>

namespace app = hikari::application::ai;
using hikari::backends::ai::FileKeyStore;
namespace sandbox = hikari::backends::sandbox;

namespace {

class MemoryKeyStore final : public app::KeyStore {
public:
    void read(const std::string &id, std::function<void(app::KeyResult)> done) override
    {
        const auto it = keys.find(id);
        it == keys.end() ? done(app::KeyError::NotFound) : done(it->second);
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<app::KeyError>)> done) override
    {
        keys[id] = key;
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<app::KeyError>)> done) override
    {
        done(keys.erase(id) ? std::nullopt : std::optional(app::KeyError::NotFound));
    }

    std::map<std::string, std::string> keys;
};

app::KeyResult readNow(app::KeyStore &store, const std::string &id)
{
    std::optional<app::KeyResult> result;
    store.read(id, [&](app::KeyResult r) { result = std::move(r); });
    return result.value_or(app::KeyError::Other);
}

std::optional<app::KeyError> writeNow(app::KeyStore &store, const std::string &id, const std::string &key)
{
    std::optional<std::optional<app::KeyError>> result;
    store.write(id, key, [&](std::optional<app::KeyError> e) { result = e; });
    return result.value_or(app::KeyError::Other);
}

std::optional<app::KeyError> removeNow(app::KeyStore &store, const std::string &id)
{
    std::optional<std::optional<app::KeyError>> result;
    store.remove(id, [&](std::optional<app::KeyError> e) { result = e; });
    return result.value_or(app::KeyError::Other);
}

class FileKeyStoreTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "ai_file_key_store_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    QString folder() const { return m_root.filePath(QStringLiteral("secrets")); }

    QTemporaryDir m_root;
};

}

TEST_F(FileKeyStoreTest, AKeyRoundTripsEncryptedAndGoesWhenRemoved)
{
    FileKeyStore store(folder());
    ASSERT_EQ(writeNow(store, "codex-chatgpt", "sk-round-trip"), std::nullopt);
    EXPECT_EQ(std::get<std::string>(readNow(store, "codex-chatgpt")), "sk-round-trip");
    const QStringList files = QDir(folder()).entryList(QDir::Files);
    ASSERT_EQ(files.size(), 1);
    QFile file(QDir(folder()).filePath(files.front()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_FALSE(file.readAll().contains("sk-round-trip"));
    file.close();

    EXPECT_EQ(removeNow(store, "codex-chatgpt"), std::nullopt);
    EXPECT_EQ(std::get<app::KeyError>(readNow(store, "codex-chatgpt")), app::KeyError::NotFound);
    EXPECT_EQ(removeNow(store, "codex-chatgpt"), app::KeyError::NotFound);
}

TEST_F(FileKeyStoreTest, AFolderThatCantBeMadePrivateGetsNoKey)
{
    ASSERT_TRUE(QDir().mkdir(folder()));
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ASSERT_TRUE(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;GRGX;;;OW)(A;OICI;GRGX;;;WD)",
                                                                     SDDL_REVISION_1, &descriptor, nullptr));
    BOOL present = FALSE, defaulted = FALSE;
    PACL acl = nullptr;
    GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted);
    std::wstring native = QDir::toNativeSeparators(folder()).toStdWString();
    ASSERT_EQ(SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                    acl, nullptr),
              DWORD(ERROR_SUCCESS));
    LocalFree(descriptor);

    FileKeyStore store(folder(), std::make_unique<MemoryKeyStore>());
    EXPECT_EQ(writeNow(store, "p", "sk-never-stored"), app::KeyError::NotPrivate);
    EXPECT_TRUE(QDir(folder()).entryList(QDir::Files).isEmpty());
}

TEST_F(FileKeyStoreTest, WithoutAFolderEveryCallIsUnavailable)
{
    FileKeyStore store({});
    EXPECT_EQ(writeNow(store, "p", "k"), app::KeyError::Unavailable);
    EXPECT_EQ(std::get<app::KeyError>(readNow(store, "p")), app::KeyError::Unavailable);
    EXPECT_EQ(removeNow(store, "p"), app::KeyError::Unavailable);
}

TEST_F(FileKeyStoreTest, AKeyLeftInTheCredentialStoreMovesHereOnce)
{
    auto earlier = std::make_unique<MemoryKeyStore>();
    MemoryKeyStore *credentials = earlier.get();
    credentials->keys["p"] = "sk-moved";
    FileKeyStore store(folder(), std::move(earlier));
    EXPECT_EQ(std::get<std::string>(readNow(store, "p")), "sk-moved");
    EXPECT_TRUE(credentials->keys.empty());
    EXPECT_EQ(std::get<std::string>(readNow(store, "p")), "sk-moved");
}

TEST_F(FileKeyStoreTest, LowRightsAgentsCantReadOrListTheKeys)
{
    FileKeyStore store(folder());
    ASSERT_EQ(writeNow(store, "p", "sk-private"), std::nullopt);
    const QString key = QDir(folder()).filePath(QDir(folder()).entryList(QDir::Files).front());

    QFile medium(key);
    EXPECT_TRUE(medium.open(QIODevice::ReadOnly));
    EXPECT_FALSE(QDir(folder()).entryList(QDir::Files).isEmpty());

    sandbox::Launcher agent(sandbox::Profile{.name = QStringLiteral("HikariSub.Test.Keys"),
                                             .readExecute = {QFileInfo(QStringLiteral(HIKARI_SANDBOX_PROBE)).absolutePath()},
                                             .network = sandbox::Network::Any,
                                             .lowRightsHelper = QStringLiteral(HIKARI_TEST_CONFINE_HELPER)});
    ASSERT_TRUE(agent.coverage().missing.isEmpty()) << agent.coverage().missing.toStdString();
    QProcess probe;
    probe.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
    probe.setArguments({QStringLiteral("low"), QStringLiteral("read"), key, QStringLiteral("list"), folder()});
    agent.prepare(probe);
    probe.start();
    ASSERT_TRUE(probe.waitForFinished(20'000)) << probe.errorString().toStdString();
    const QString said = QString::fromLocal8Bit(probe.readAllStandardOutput()).remove(QLatin1Char('\r'));
    EXPECT_TRUE(said.contains(QStringLiteral("low: ok"))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("read ") + key + QStringLiteral(": denied"))) << said.toStdString();
    EXPECT_TRUE(said.contains(QStringLiteral("list ") + folder() + QStringLiteral(": denied"))) << said.toStdString();
}
