#include "hikari/backends/ai_agent_cli.h"

#include "local_release.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace hikari::backends::ai;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QString native(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

bool write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

class AgentCliTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_agent_cli_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    AgentHost host(bool windows = false) const
    {
        return {.home = QStringLiteral("/home/u"),
                .voltaHome = dir.filePath(QStringLiteral("volta")),
                .windows = windows,
                .findTool = [](const QString &name, const QStringList &) { return QStringLiteral("/tools/") + name; }};
    }

    QString fakeCli(const QString &agent, const QByteArray &version, const QByteArray &action)
    {
        const QString bin = dir.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
#ifdef Q_OS_WIN
        const QString path = bin + QLatin1Char('/') + agent + QStringLiteral(".exe");
#else
        const QString path = bin + QLatin1Char('/') + agent;
#endif
        QFile::remove(path);
        if (!QFile::copy(QStringLiteral(HIKARI_FAKE_AGENT_CLI), path))
            ADD_FAILURE() << "can't copy the fake CLI";
        const QString state = dir.filePath(QStringLiteral("state.txt"));
        if (!write(state, version + '\n' + action + '\n'))
            ADD_FAILURE() << "can't write " << state.toStdString();
        qputenv("HIKARI_FAKE_CLI_STATE", state.toLocal8Bit());
        return path;
    }

    struct Finished {
        AgentUpdate::Outcome outcome;
        QString version;
        QString lastLine;
    };
    std::optional<Finished> run(AgentUpdate &update, const QString &cli, const QString &before)
    {
        std::optional<Finished> finished;
        QObject::connect(&update, &AgentUpdate::finished, [&](AgentUpdate::Outcome o, const QString &v, const QString &l) {
            finished = Finished{o, v, l};
        });
        update.start({.method = AgentInstallMethod::Native, .program = cli, .arguments = {QStringLiteral("update")}},
                     cli, before);
        waitFor([&] { return finished.has_value(); });
        return finished;
    }

    QTemporaryDir dir;
};

}

TEST_F(AgentCliTest, ClaudesInstallerIsUpdatedWithClaudeUpdate)
{
    const AgentInstall install = detectAgentInstall(QStringLiteral("claude"), QStringLiteral("/home/u/.local/bin/claude"),
                                                    QStringLiteral("/home/u/.local/share/claude/versions/2.1.4"), host());
    EXPECT_EQ(install.method, AgentInstallMethod::Native);
    EXPECT_EQ(install.program, QStringLiteral("/home/u/.local/bin/claude"));
    EXPECT_EQ(install.arguments, QStringList{QStringLiteral("update")});
    EXPECT_EQ(install.manualCommand, QStringLiteral("claude update"));
    EXPECT_EQ(install.latest, AgentLatestSource::ClaudeChannel);
}

TEST_F(AgentCliTest, CodexsInstallerIsUpdatedWithTheHomeThatOwnsIt)
{
    const AgentInstall install =
        detectAgentInstall(QStringLiteral("codex"), QStringLiteral("/home/u/.local/bin/codex"),
                           QStringLiteral("/home/u/.codex/packages/standalone/releases/0.58.0/codex"), host());
    EXPECT_EQ(install.method, AgentInstallMethod::Native);
    EXPECT_EQ(install.arguments, QStringList{QStringLiteral("update")});
    EXPECT_EQ(install.environment, (std::map<QString, QString>{{QStringLiteral("CODEX_HOME"), native(QStringLiteral("/home/u/.codex"))}}));
    EXPECT_EQ(install.latest, AgentLatestSource::Npm);
    EXPECT_EQ(install.package, QStringLiteral("@openai/codex"));
}

TEST_F(AgentCliTest, AnNpmGlobalInstallUpdatesUnderItsOwnPrefix)
{
    const QString real = QStringLiteral("/home/u/.npm-global/lib/node_modules/@anthropic-ai/claude-code/cli.js");
    for (const QString &path : {QStringLiteral("/home/u/.npm-global/bin/claude"), QStringLiteral("/home/u/.local/bin/claude")}) {
        const AgentInstall install = detectAgentInstall(QStringLiteral("claude"), path, real, host());
        EXPECT_EQ(install.method, AgentInstallMethod::Npm) << path.toStdString();
        EXPECT_EQ(install.program, QStringLiteral("/tools/npm"));
        const QStringList arguments{QStringLiteral("install"), QStringLiteral("-g"), QStringLiteral("--prefix"),
                                    native(QStringLiteral("/home/u/.npm-global")), QStringLiteral("@anthropic-ai/claude-code@latest")};
        EXPECT_EQ(install.arguments, arguments);
        EXPECT_EQ(install.manualCommand, QStringLiteral("npm ") + arguments.join(QLatin1Char(' ')));
        EXPECT_EQ(install.package, QStringLiteral("@anthropic-ai/claude-code"));
    }
}

TEST_F(AgentCliTest, BunPnpmAndYarnGlobalsUseTheirOwnCommands)
{
    const struct {
        QString path;
        QString real;
        AgentInstallMethod method;
        QStringList arguments;
    } cases[] = {
        {QStringLiteral("/home/u/.bun/bin/codex"), {}, AgentInstallMethod::Bun,
         {QStringLiteral("/tools/bun"), QStringLiteral("i"), QStringLiteral("-g"), QStringLiteral("@openai/codex@latest")}},
        {QStringLiteral("/home/u/.local/share/pnpm/codex"), {}, AgentInstallMethod::Pnpm,
         {QStringLiteral("/tools/pnpm"), QStringLiteral("add"), QStringLiteral("-g"), QStringLiteral("@openai/codex@latest")}},
        {QStringLiteral("/home/u/.yarn/bin/codex"), QStringLiteral("/home/u/.config/yarn/global/node_modules/@openai/codex/bin/codex.js"),
         AgentInstallMethod::Yarn,
         {QStringLiteral("/tools/yarn"), QStringLiteral("global"), QStringLiteral("add"), QStringLiteral("@openai/codex@latest")}},
    };
    for (const auto &c : cases) {
        const AgentInstall install = detectAgentInstall(QStringLiteral("codex"), c.path, c.real, host());
        EXPECT_EQ(install.method, c.method) << c.path.toStdString();
        EXPECT_EQ(QStringList{install.program} + install.arguments, c.arguments) << c.path.toStdString();
    }
}

TEST_F(AgentCliTest, VoltaNeedsItsShimAndThePackageItInstalled)
{
    const QString shim = dir.filePath(QStringLiteral("volta/bin/volta-shim"));
    EXPECT_EQ(detectAgentInstall(QStringLiteral("codex"), QStringLiteral("/home/u/.volta/bin/codex"), shim, host()).method,
              AgentInstallMethod::Unknown);
    ASSERT_TRUE(QDir().mkpath(dir.filePath(QStringLiteral("volta/tools/image/packages/@openai/codex"))));
    const AgentInstall install = detectAgentInstall(QStringLiteral("codex"), QStringLiteral("/home/u/.volta/bin/codex"), shim, host());
    EXPECT_EQ(install.method, AgentInstallMethod::Volta);
    EXPECT_EQ(install.program, QStringLiteral("/tools/volta"));
    EXPECT_EQ(install.arguments, (QStringList{QStringLiteral("install"), QStringLiteral("@openai/codex@latest")}));
}

#ifndef Q_OS_WIN
TEST_F(AgentCliTest, HomebrewNeedsBrewInTheKegsPrefix)
{
    const QString prefix = dir.filePath(QStringLiteral("brew"));
    const QString cask = prefix + QStringLiteral("/Caskroom/claude-code/2.1.4/claude");
    EXPECT_EQ(detectAgentInstall(QStringLiteral("claude"), prefix + QStringLiteral("/bin/claude"), cask, host()).method,
              AgentInstallMethod::Unknown);
    ASSERT_TRUE(write(prefix + QStringLiteral("/bin/brew"), "#!/bin/sh\n"));
    QFile(prefix + QStringLiteral("/bin/brew")).setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    const AgentInstall fromCask = detectAgentInstall(QStringLiteral("claude"), prefix + QStringLiteral("/bin/claude"), cask, host());
    EXPECT_EQ(fromCask.method, AgentInstallMethod::Homebrew);
    EXPECT_EQ(fromCask.program, prefix + QStringLiteral("/bin/brew"));
    EXPECT_EQ(fromCask.arguments, (QStringList{QStringLiteral("upgrade"), QStringLiteral("--cask"), QStringLiteral("claude-code")}));
    EXPECT_EQ(fromCask.latest, AgentLatestSource::HomebrewCask);
    EXPECT_EQ(fromCask.package, QStringLiteral("claude-code"));
    const AgentInstall fromFormula = detectAgentInstall(QStringLiteral("codex"), prefix + QStringLiteral("/bin/codex"),
                                                        prefix + QStringLiteral("/Cellar/codex/0.58.0/bin/codex"), host());
    EXPECT_EQ(fromFormula.arguments, (QStringList{QStringLiteral("upgrade"), QStringLiteral("codex")}));
    EXPECT_EQ(fromFormula.latest, AgentLatestSource::HomebrewFormula);
}
#endif

TEST_F(AgentCliTest, WinGetChocolateyMiseAndUnknownInstallsAreNeverRun)
{
    const struct {
        QString agent;
        QString path;
        bool windows;
        AgentInstallMethod method;
        QString manual;
    } cases[] = {
        {QStringLiteral("claude"), QStringLiteral("C:\\Users\\u\\AppData\\Local\\Microsoft\\WinGet\\Links\\claude.exe"), true,
         AgentInstallMethod::WinGet, QStringLiteral("winget upgrade Anthropic.ClaudeCode")},
        {QStringLiteral("claude"), QStringLiteral("C:\\ProgramData\\chocolatey\\bin\\claude.exe"), true,
         AgentInstallMethod::Chocolatey, QStringLiteral("choco upgrade claude-code")},
        {QStringLiteral("claude"), QStringLiteral("/home/u/.local/share/mise/installs/node/22/bin/claude"), false,
         AgentInstallMethod::Mise, {}},
        {QStringLiteral("codex"), QStringLiteral("/usr/bin/codex"), false, AgentInstallMethod::Unknown, {}},
    };
    for (const auto &c : cases) {
        const AgentInstall install = detectAgentInstall(c.agent, c.path, {}, host(c.windows));
        EXPECT_EQ(install.method, c.method) << c.path.toStdString();
        EXPECT_TRUE(install.program.isEmpty()) << c.path.toStdString();
        EXPECT_EQ(install.manualCommand, c.manual) << c.path.toStdString();
    }
}

TEST_F(AgentCliTest, WindowsCmdShimsAreLeftToTheUser)
{
    const QString npm = dir.filePath(QStringLiteral("npm"));
    ASSERT_TRUE(write(npm + QStringLiteral("/node_modules/@anthropic-ai/claude-code/package.json"), "{}"));
    const AgentInstall shim = detectAgentInstall(QStringLiteral("claude"), npm + QStringLiteral("/claude.cmd"), {}, host(true));
    EXPECT_EQ(shim.method, AgentInstallMethod::Npm);
    EXPECT_TRUE(shim.program.isEmpty());
    EXPECT_EQ(shim.manualCommand, QStringLiteral("npm install -g @anthropic-ai/claude-code@latest"));
    const AgentInstall pnpm = detectAgentInstall(QStringLiteral("codex"), QStringLiteral("C:/Users/u/AppData/Local/pnpm/codex.cmd"),
                                                 {}, host(true));
    EXPECT_EQ(pnpm.method, AgentInstallMethod::Pnpm);
    EXPECT_TRUE(pnpm.program.isEmpty());
    EXPECT_EQ(pnpm.manualCommand, QStringLiteral("pnpm add -g @openai/codex@latest"));
}

TEST_F(AgentCliTest, VersionsAreReadFromEitherCliOutput)
{
    EXPECT_EQ(versionIn(QStringLiteral("2.1.4 (Claude Code)\n")), QStringLiteral("2.1.4"));
    EXPECT_EQ(versionIn(QStringLiteral("codex-cli 0.58.0")), QStringLiteral("0.58.0"));
    EXPECT_EQ(versionIn(QStringLiteral("codex-cli 0.59.0-alpha.2")), QStringLiteral("0.59.0-alpha.2"));
    EXPECT_EQ(versionIn(QStringLiteral("command not found")), QString());
}

TEST_F(AgentCliTest, BadgesComeFromTheLatestVersionAndTheManifest)
{
    const AgentCompat compat{.minimum = QStringLiteral("2.0.0"), .broken = {QStringLiteral("2.0.9")}};
    EXPECT_EQ(agentBadge(QStringLiteral("2.1.4"), QStringLiteral("2.2.0"), compat), AgentBadge::UpdateAvailable);
    EXPECT_EQ(agentBadge(QStringLiteral("2.2.0"), QStringLiteral("2.2.0"), compat), AgentBadge::Current);
    EXPECT_EQ(agentBadge(QStringLiteral("2.0.9"), QStringLiteral("2.2.0"), compat), AgentBadge::KnownBroken);
    EXPECT_EQ(agentBadge(QStringLiteral("1.9.0"), {}, compat), AgentBadge::Unsupported);
    EXPECT_EQ(agentBadge(QStringLiteral("2.1.4"), {}, compat), AgentBadge::Unknown);
    EXPECT_EQ(agentBadge({}, QStringLiteral("2.2.0"), compat), AgentBadge::Unknown);
}

TEST_F(AgentCliTest, ClaudesChannelComesFromItsSettings)
{
    const QString home = dir.filePath(QStringLiteral("home"));
    EXPECT_EQ(claudeUpdateChannel(home), QStringLiteral("latest"));
    ASSERT_TRUE(write(home + QStringLiteral("/.claude/settings.json"), R"({"autoUpdatesChannel": "stable"})"));
    EXPECT_EQ(claudeUpdateChannel(home), QStringLiteral("stable"));
    ASSERT_TRUE(write(home + QStringLiteral("/.claude/settings.json"), R"({"autoUpdatesChannel": "../x"})"));
    EXPECT_EQ(claudeUpdateChannel(home), QStringLiteral("latest"));
}

TEST_F(AgentCliTest, LatestVersionsAreReadOnceADayFromEachSource)
{
    hikari::test::LocalRelease server;
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.server.serverPort());
    server.files["/npm/@openai/codex/latest"] = R"({"name": "@openai/codex", "version": "0.60.0"})";
    server.files["/brew/cask/claude-code.json"] = R"({"version": "2.2.0,abc123"})";
    server.files["/brew/formula/codex.json"] = R"({"versions": {"stable": "0.61.0"}})";
    server.files["/claude/stable"] = "2.0.30\n";
    const QString cache = dir.filePath(QStringLiteral("agent-versions.json"));
    AgentLatestVersions latest(cache, {.npm = base + QStringLiteral("/npm/"), .homebrew = base + QStringLiteral("/brew/"),
                                       .claude = base + QStringLiteral("/claude/")});
    const auto ask = [&](const AgentInstall &install, const QString &channel = QStringLiteral("latest")) {
        std::optional<QString> answer;
        latest.latest(install, channel, [&](QString v) { answer = v; });
        waitFor([&] { return answer.has_value(); });
        return answer.value_or(QStringLiteral("no answer"));
    };
    const AgentInstall npm{.latest = AgentLatestSource::Npm, .package = QStringLiteral("@openai/codex")};
    EXPECT_EQ(ask(npm), QStringLiteral("0.60.0"));
    EXPECT_EQ(ask({.latest = AgentLatestSource::HomebrewCask, .package = QStringLiteral("claude-code")}), QStringLiteral("2.2.0"));
    EXPECT_EQ(ask({.latest = AgentLatestSource::HomebrewFormula, .package = QStringLiteral("codex")}), QStringLiteral("0.61.0"));
    EXPECT_EQ(ask({.latest = AgentLatestSource::ClaudeChannel}, QStringLiteral("stable")), QStringLiteral("2.0.30"));
    EXPECT_EQ(server.requested.size(), 4u);

    server.files["/npm/@openai/codex/latest"] = R"({"version": "0.62.0"})";
    EXPECT_EQ(ask(npm), QStringLiteral("0.60.0"));
    EXPECT_EQ(server.requested.size(), 4u);

    QFile file(cache);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const QString key = base + QStringLiteral("/npm/@openai/codex/latest");
    json.insert(key, QJsonObject{{QStringLiteral("version"), QStringLiteral("0.60.0")},
                                 {QStringLiteral("checked"), QDateTime::currentSecsSinceEpoch() - 2 * 24 * 60 * 60}});
    ASSERT_TRUE(write(cache, QJsonDocument(json).toJson()));
    EXPECT_EQ(ask(npm), QStringLiteral("0.62.0"));
    EXPECT_EQ(server.requested.size(), 5u);
}

TEST_F(AgentCliTest, AnUnreadableSourceKeepsTheLastAnswer)
{
    hikari::test::LocalRelease server;
    const QString base = QStringLiteral("http://127.0.0.1:%1/npm/").arg(server.server.serverPort());
    const QString cache = dir.filePath(QStringLiteral("agent-versions.json"));
    AgentLatestVersions latest(cache, {.npm = base});
    const AgentInstall npm{.latest = AgentLatestSource::Npm, .package = QStringLiteral("@openai/codex")};
    std::optional<QString> answer;
    latest.latest(npm, {}, [&](QString v) { answer = v; });
    ASSERT_TRUE(waitFor([&] { return answer.has_value(); }));
    EXPECT_EQ(*answer, QString());

    ASSERT_TRUE(write(cache, QJsonDocument(QJsonObject{{base + QStringLiteral("@openai/codex/latest"),
                                                        QJsonObject{{QStringLiteral("version"), QStringLiteral("0.58.0")},
                                                                    {QStringLiteral("checked"), 0}}}})
                                 .toJson()));
    answer.reset();
    latest.latest(npm, {}, [&](QString v) { answer = v; });
    ASSERT_TRUE(waitFor([&] { return answer.has_value(); }));
    EXPECT_EQ(*answer, QStringLiteral("0.58.0"));
}

TEST_F(AgentCliTest, TheProbeFindsTheCliAndAsksItsVersion)
{
    fakeCli(QStringLiteral("claude"), "2.1.4", "same");
    std::optional<std::optional<AgentCli>> found;
    probeAgentCli(QStringLiteral("claude"), [&](std::optional<AgentCli> cli) { found = cli; },
                  {dir.filePath(QStringLiteral("bin"))});
    ASSERT_TRUE(waitFor([&] { return found.has_value(); }));
    ASSERT_TRUE(found->has_value());
    EXPECT_EQ((*found)->version, QStringLiteral("2.1.4"));
    EXPECT_EQ((*found)->agent, QStringLiteral("claude"));

    found.reset();
    probeAgentCli(QStringLiteral("codex"), [&](std::optional<AgentCli> cli) { found = cli; },
                  {dir.filePath(QStringLiteral("bin"))});
    ASSERT_TRUE(waitFor([&] { return found.has_value(); }));
    EXPECT_FALSE(found->has_value());
}

#ifndef Q_OS_WIN
class AgentShimTest : public AgentCliTest {
protected:
    void executable(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(text);
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    }

    /// A fake `mise` that answers `mise which claude` with `answer`, and the shim dirs that point at it.
    void mise(const QByteArray &answer)
    {
        executable(dir.filePath(QStringLiteral("usr/bin/mise")), "#!/bin/sh\n" + answer + "\n");
        for (const QString &d : {wrappers, shims, linked}) {
            QDir().mkpath(d);
            QFile::remove(d + QStringLiteral("/claude"));
            ASSERT_TRUE(QFile::link(dir.filePath(QStringLiteral("usr/bin/mise")), d + QStringLiteral("/claude")));
        }
    }

    const QString wrappers = dir.filePath(QStringLiteral("mise/command-wrappers/bin"));
    const QString shims = dir.filePath(QStringLiteral("mise/shims"));
    const QString linked = dir.filePath(QStringLiteral("linked"));
    const QString asdfShims = dir.filePath(QStringLiteral(".asdf/shims"));
    const QString installed = dir.filePath(QStringLiteral("mise/installs/claude/2.1.4/bin/claude"));
};

TEST_F(AgentShimTest, AShimGivesWayToTheRealCliFurtherOnThePath)
{
    mise("exit 1");
    executable(asdfShims + QStringLiteral("/claude"), "#!/bin/sh\nexec asdf exec claude \"$@\"\n");
    const QString real = fakeCli(QStringLiteral("claude"), "2.1.4", "same");
    EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {wrappers, shims, asdfShims, linked, dir.filePath(QStringLiteral("bin"))}),
              real);
}

TEST_F(AgentShimTest, AShimAloneIsResolvedByItsVersionManager)
{
    executable(installed, "#!/bin/sh\n");
    mise("[ \"$1 $2\" = \"which claude\" ] && echo " + installed.toLocal8Bit());
    for (const QString &d : {wrappers, shims, linked})
        EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {d}), installed) << d.toStdString();

    const QString asdfBin = dir.filePath(QStringLiteral("asdf-bin"));
    executable(asdfBin + QStringLiteral("/asdf"), "#!/bin/sh\n[ \"$1 $2\" = \"which claude\" ] && echo " + installed.toLocal8Bit() + "\n");
    executable(asdfShims + QStringLiteral("/claude"), "#!/bin/sh\nexec asdf exec claude \"$@\"\n");
    EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {asdfShims, asdfBin}), installed);
}

TEST_F(AgentShimTest, AShimItsManagerCantResolveIsNotFoundAndSaysSo)
{
    static QStringList logged;
    logged.clear();
    const QtMessageHandler previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) {
        logged << m;
    });
    mise("exit 1");
    EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {shims}), QString());
    mise("echo " + shims.toLocal8Bit() + "/claude");
    EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {shims}), QString());
    mise("echo " + dir.filePath(QStringLiteral("nowhere/claude")).toLocal8Bit());
    EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {shims}), QString());
    mise("sleep 30");
    EXPECT_EQ(findAgentCli(QStringLiteral("claude"), {shims}), QString());
    qInstallMessageHandler(previous);
    ASSERT_EQ(logged.size(), 4);
    EXPECT_TRUE(logged[0].contains(QStringLiteral("couldn't resolve"))) << logged[0].toStdString();
}
#endif

TEST_F(AgentCliTest, AnUpdateCountsOnlyWhenTheVersionChanged)
{
    AgentUpdate updated;
    const auto bumped = run(updated, fakeCli(QStringLiteral("claude"), "2.1.4", "bump 2.2.0"), QStringLiteral("2.1.4"));
    ASSERT_TRUE(bumped);
    EXPECT_EQ(bumped->outcome, AgentUpdate::Outcome::Updated);
    EXPECT_EQ(bumped->version, QStringLiteral("2.2.0"));
    EXPECT_FALSE(updated.running());

    AgentUpdate unchanged;
    const auto same = run(unchanged, fakeCli(QStringLiteral("claude"), "2.1.4", "same"), QStringLiteral("2.1.4"));
    ASSERT_TRUE(same);
    EXPECT_EQ(same->outcome, AgentUpdate::Outcome::Unchanged);
    EXPECT_EQ(same->version, QStringLiteral("2.1.4"));
    EXPECT_EQ(same->lastLine, QStringLiteral("Up to date"));
}

TEST_F(AgentCliTest, AFailedUpdateKeepsItsLastLine)
{
    AgentUpdate update;
    const auto failed = run(update, fakeCli(QStringLiteral("codex"), "0.58.0", "fail"), QStringLiteral("0.58.0"));
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->outcome, AgentUpdate::Outcome::Failed);
    EXPECT_EQ(failed->lastLine, QStringLiteral("error: the download failed"));
}

TEST_F(AgentCliTest, AHangingUpdateTimesOutOrIsCancelled)
{
    AgentUpdate timed;
    timed.setTimeoutMs(300);
    const auto timedOut = run(timed, fakeCli(QStringLiteral("claude"), "2.1.4", "hang"), QStringLiteral("2.1.4"));
    ASSERT_TRUE(timedOut);
    EXPECT_EQ(timedOut->outcome, AgentUpdate::Outcome::TimedOut);

    AgentUpdate cancelled;
    const QString cli = fakeCli(QStringLiteral("claude"), "2.1.4", "hang");
    std::optional<AgentUpdate::Outcome> outcome;
    QObject::connect(&cancelled, &AgentUpdate::finished, [&](AgentUpdate::Outcome o) { outcome = o; });
    cancelled.start({.method = AgentInstallMethod::Native, .program = cli, .arguments = {QStringLiteral("update")}}, cli,
                    QStringLiteral("2.1.4"));
    EXPECT_TRUE(cancelled.running());
    QTimer::singleShot(200, &cancelled, [&] { cancelled.cancel(); });
    ASSERT_TRUE(waitFor([&] { return outcome.has_value(); }));
    EXPECT_EQ(*outcome, AgentUpdate::Outcome::Cancelled);
}
