#include "hikari/backends/ai_antigravity.h"
#include "hikari/backends/user_alone.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <map>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif
#include <optional>
#include <string>

using namespace hikari::backends::ai;
namespace app = hikari::application::ai;

namespace {

void ensureApp()
{
    static int argc = 1;
    static char name[] = "ai_antigravity_tests";
    static char *argv[] = {name, nullptr};
    if (!QCoreApplication::instance())
        new QCoreApplication(argc, argv);
}

struct Keys final : app::KeyStore {
    std::map<std::string, std::string> keys;
    bool locked = false;
    void read(const std::string &id, std::function<void(app::KeyResult)> done) override
    {
        const auto it = keys.find(id);
        if (it == keys.end())
            return done(app::KeyError::NotFound);
        done(it->second);
    }
    void write(const std::string &id, const std::string &key, std::function<void(std::optional<app::KeyError>)> done) override
    {
        if (locked)
            return done(app::KeyError::Unavailable);
        keys[id] = key;
        done(std::nullopt);
    }
    void remove(const std::string &id, std::function<void(std::optional<app::KeyError>)> done) override
    {
        keys.erase(id);
        done(std::nullopt);
    }
};

QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
}

bool inside(const QString &path, const QString &dir)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return clean == dir || clean.startsWith(dir + u'/');
}

class TokenFileLeaseTest : public ::testing::Test {
protected:
    QString acquire()
    {
        std::optional<QString> error;
        lease.acquire([&](QString e) { error = std::move(e); });
        EXPECT_TRUE(error);
        return error.value_or(QStringLiteral("no answer"));
    }
    std::string stored(const QByteArray &token) const { return token.toBase64().toStdString(); }

    QTemporaryDir dir;
    Keys keys;
    QString file = dir.filePath(QStringLiteral("gemini/token"));
    TokenFileLease lease{keys, "agy", file, QStringLiteral("sign in first")};
};

}

TEST(AntigravitySpec, EveryPathAgyGetsStaysInsideItsProfile)
{
    QTemporaryDir dir;
    const QString program = dir.filePath(QStringLiteral("bin/agy"));
    writeFile(program, "ELF");
    const QString profile = dir.filePath(QStringLiteral("ai/agents/antigravity"));
    const AgentSpec spec = antigravityAgent(program, profile, dir.filePath(QStringLiteral("app/bin/hikarisub")), nullptr);

    EXPECT_EQ(spec.arguments.first(), QStringLiteral("acp"));
    EXPECT_TRUE(spec.arguments.contains(QStringLiteral("--disable-telemetry")));
    EXPECT_TRUE(spec.arguments.contains(QStringLiteral("--include-default-customizations=false")));
    EXPECT_FALSE(spec.arguments.contains(QStringLiteral("--dangerously-skip-permissions")));
    EXPECT_FALSE(spec.arguments.filter(QStringLiteral("--ide-mcp-port")).size());
    const auto value = [&](const char *flag) { return spec.arguments.at(spec.arguments.indexOf(QLatin1String(flag)) + 1); };
    EXPECT_TRUE(inside(value("--gemini-dir"), profile));
    EXPECT_TRUE(QDir::isRelativePath(value("--app-data-dir")));

    EXPECT_TRUE(inside(spec.environment.value(QStringLiteral("HOME")), profile));
    for (const QString &name : spec.environment.keys())
        if (name != QLatin1String("HOME"))
            EXPECT_TRUE(inside(spec.environment.value(name), profile)) << name.toStdString();
    EXPECT_TRUE(inside(spec.workingDirectory, profile));
    ASSERT_FALSE(spec.profile->readWrite.isEmpty());
    for (const QString &path : spec.profile->readWrite)
        EXPECT_TRUE(inside(path, profile)) << path.toStdString();
    EXPECT_FALSE(spec.before);
    for (const QString &folder : {profile, profile + QStringLiteral("/gemini"), profile + QStringLiteral("/gemini/app-data"),
                                  profile + QStringLiteral("/home")})
        EXPECT_TRUE(hikari::backends::isForUserAlone(folder)) << folder.toStdString();
}

#ifndef Q_OS_WIN
TEST_F(TokenFileLeaseTest, TheLentFileAndItsFolderAreTheUsersAloneFromTheirFirstByte)
{
    const mode_t before = ::umask(0);
    keys.keys["agy"] = stored("first");
    const QString acquired = acquire();
    ::umask(before);
    ASSERT_EQ(acquired, QString());
    EXPECT_EQ(contents(file), "first");
    EXPECT_EQ(QFileInfo(file).permissions() & ~(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser),
              QFileDevice::Permissions());
    EXPECT_TRUE(hikari::backends::isForUserAlone(QFileInfo(file).absolutePath()));
    EXPECT_FALSE(QFileInfo::exists(file + QStringLiteral(".new")));
}
#endif

TEST_F(TokenFileLeaseTest, AgyGetsTheKeychainSignInAndItsRefreshComesBackAfterTheLastProcess)
{
    keys.keys["agy"] = stored("first");
    ASSERT_EQ(acquire(), QString());
    EXPECT_EQ(contents(file), "first");

    ASSERT_EQ(acquire(), QString());
    writeFile(file, "rotated");
    lease.release();
    EXPECT_TRUE(QFileInfo::exists(file));
    EXPECT_EQ(keys.keys.at("agy"), stored("first"));
    lease.release();
    EXPECT_EQ(keys.keys.at("agy"), stored("rotated"));
    EXPECT_FALSE(QFileInfo::exists(file));
}

TEST_F(TokenFileLeaseTest, WithoutASignInAgyDoesntStartAndALeftoverFileGoes)
{
    writeFile(file, "left");
    EXPECT_EQ(acquire(), QStringLiteral("sign in first"));
    EXPECT_FALSE(QFileInfo::exists(file));
    lease.release();
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(TokenFileLeaseTest, ASignOutWhileAgyRunsStaysSignedOutAndACrashesFileGoes)
{
    keys.keys["agy"] = stored("first");
    ASSERT_EQ(acquire(), QString());
    writeFile(file, "rotated");
    keys.keys.clear();
    lease.release();
    EXPECT_TRUE(keys.keys.empty());
    EXPECT_FALSE(QFileInfo::exists(file));

    writeFile(file, "left by a crash");
    TokenFileLease next{keys, "agy", file, QStringLiteral("sign in first")};
    EXPECT_FALSE(QFileInfo::exists(file));
}

TEST_F(TokenFileLeaseTest, ALockedKeychainFailsTheSignInAndTheFileStillGoes)
{
    keys.locked = true;
    writeFile(file, "fresh");
    std::optional<QString> error;
    lease.keep([&](QString e) { error = std::move(e); });
    ASSERT_TRUE(error);
    EXPECT_TRUE(error->contains(QStringLiteral("keychain"))) << error->toStdString();
    EXPECT_FALSE(QFileInfo::exists(file));
    EXPECT_TRUE(keys.keys.empty());
}

TEST_F(TokenFileLeaseTest, ASignInsFileMovesIntoTheKeychain)
{
    writeFile(file, "fresh");
    std::optional<QString> error;
    lease.keep([&](QString e) { error = std::move(e); });
    ASSERT_EQ(error, QString());
    EXPECT_EQ(keys.keys.at("agy"), stored("fresh"));
    EXPECT_FALSE(QFileInfo::exists(file));

    lease.keep([&](QString e) { error = std::move(e); });
    EXPECT_FALSE(error->isEmpty());
}

#ifdef Q_OS_LINUX
TEST_F(TokenFileLeaseTest, SignInShowsGooglesPageTakesItsCodeAndKeepsTheSignIn)
{
    ensureApp();
    const QString program = dir.filePath(QStringLiteral("bin/agy"));
    writeFile(program, "#!/bin/sh\n"
                       "while [ \"$1\" != --gemini-dir ]; do shift; done\n"
                       "gemini=\"$2\"\n"
                       "echo 'Open this link in a browser to log in with Google:'\n"
                       "echo '  https://accounts.google.com/o/oauth2/auth?client_id=x&state=y'\n"
                       "read code\n"
                       "[ \"$code\" = good ] || { echo 'login failed: bad code'; exit 1; }\n"
                       "echo 'consumerOAuth: token acquired'\n"
                       "echo 'Failed keyring SaveToken, falling back to file when applicable: permission denied'\n"
                       "mkdir -p \"$gemini/app-data\" && printf '{\"token\":' > \"$gemini/app-data/antigravity-oauth-token\"\n"
                       "printf '\"t\"}' >> \"$gemini/app-data/antigravity-oauth-token\"\n"
                       "while read line; do :; done\n");
    QFile::setPermissions(program, QFile::permissions(program) | QFile::ExeOwner);
    const QString profile = dir.filePath(QStringLiteral("profile"));
    auto shared = std::make_shared<TokenFileLease>(keys, "agy", profile + QStringLiteral("/") + QLatin1String(kAntigravityTokenFile),
                                                   QStringLiteral("sign in first"));
    for (const char *code : {"bad", "good"}) {
        AgentSpec spec = antigravityAgent(program, profile, program, nullptr);
        spec.profile->readExecute << QStringLiteral("/usr/bin") << QStringLiteral("/bin");
        AntigravitySignIn signIn(std::move(spec), shared);
        QSignalSpy linked(&signIn, &AntigravitySignIn::linkReady);
        QSignalSpy finished(&signIn, &AntigravitySignIn::finished);
        QSignalSpy entered(&signIn, &AntigravitySignIn::codeEntered);
        signIn.start();
        ASSERT_TRUE(linked.wait(10000)) << (finished.count() ? finished.at(0).at(0).toString().toStdString() : "still running");
        EXPECT_EQ(linked.at(0).at(0).toUrl().host(), QStringLiteral("accounts.google.com"));
        signIn.enterCode(QLatin1String(code));
        EXPECT_EQ(entered.count(), 1);
        ASSERT_TRUE(finished.count() || finished.wait(10000));
        const QString error = finished.at(0).at(0).toString();
        if (QLatin1String(code) == QLatin1String("bad")) {
            EXPECT_EQ(error, QStringLiteral("login failed: bad code"));
            EXPECT_TRUE(keys.keys.empty());
        } else {
            EXPECT_EQ(error, QString());
            EXPECT_EQ(keys.keys.at("agy"), stored(R"({"token":"t"})"));
            EXPECT_FALSE(QFileInfo::exists(shared->file()));
            EXPECT_FALSE(signIn.running());
        }
    }
}
#endif
