#include "hikari/backends/sandbox.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QProcess>
#include <QTcpServer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>

using namespace hikari::backends::sandbox;

namespace {

class Sandbox : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_sandbox_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        for (const char *dir : {"allowed", "readonly", "outside"})
            ASSERT_TRUE(QDir(m_root.path()).mkpath(QString::fromLatin1(dir)));
        for (const char *file : {"readonly/note.txt", "outside/secret.txt", "allowed/mine.txt"}) {
            QFile f(path(file));
            ASSERT_TRUE(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
    }

    QString path(const char *relative) const { return QDir(m_root.path()).filePath(QString::fromLatin1(relative)); }

    Profile profile(Network network = Network::None) const
    {
        Profile p;
        p.name = QStringLiteral("HikariSub.Test.Sandbox");
        p.readExecute = {QFileInfo(QStringLiteral(HIKARI_SANDBOX_PROBE)).absolutePath()};
        p.read = {path("readonly")};
        p.readWrite = {path("allowed")};
        p.network = network;
        return p;
    }

    /// Runs the probe confined to `launcher`'s profile; answers its report lines.
    QStringList run(Launcher &launcher, const QStringList &arguments)
    {
        QProcess process;
        process.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
        process.setArguments(arguments);
        launcher.prepare(process);
        process.start();
        EXPECT_TRUE(process.waitForFinished(20'000)) << process.errorString().toStdString();
        const QString out = QString::fromLocal8Bit(process.readAllStandardOutput()).remove(QLatin1Char('\r'));
        return out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    static bool ok(const QStringList &lines, const QString &action)
    {
        return lines.contains(action + QStringLiteral(": ok"));
    }

    static bool denied(const QStringList &lines, const QString &action)
    {
        return std::ranges::any_of(lines, [&](const QString &line) {
            return line.startsWith(action + QStringLiteral(": denied "));
        });
    }

    QTemporaryDir m_root;
};

TEST_F(Sandbox, TheSystemsCoverageIsWhatALauncherGets)
{
    for (Network network : {Network::None, Network::Any}) {
        Launcher launcher(profile(network));
        const Coverage system = systemCoverage(network);
        EXPECT_EQ(system.files, launcher.coverage().files);
        EXPECT_EQ(system.network, launcher.coverage().network);
        EXPECT_EQ(system.missing.toStdString(), launcher.coverage().missing.toStdString());
    }
}

TEST_F(Sandbox, FilesOutsideTheProfileAreRefused)
{
    Launcher launcher(profile());
    if (!launcher.coverage().files)
        GTEST_SKIP() << launcher.coverage().missing.toStdString();
    const QString secret = path("outside/secret.txt");
    const QString note = path("readonly/note.txt");
    const QString fresh = path("readonly/new.txt");
    const QString mine = path("allowed/mine.txt");
    const QString made = path("allowed/made.txt");
    const QStringList lines = run(launcher, {QStringLiteral("read"), secret, QStringLiteral("read"), note,
                                             QStringLiteral("write"), fresh, QStringLiteral("read"), mine,
                                             QStringLiteral("write"), made});
    const std::string report = (lines + QStringList{launcher.coverage().missing}).join(QLatin1Char('\n')).toStdString();
    ASSERT_EQ(lines.size(), 5) << report;
    EXPECT_TRUE(denied(lines, QStringLiteral("read ") + secret)) << report;
    EXPECT_TRUE(ok(lines, QStringLiteral("read ") + note)) << report;
    EXPECT_TRUE(denied(lines, QStringLiteral("write ") + fresh)) << report;
    EXPECT_TRUE(ok(lines, QStringLiteral("read ") + mine)) << report;
    EXPECT_TRUE(ok(lines, QStringLiteral("write ") + made)) << report;
}

TEST_F(Sandbox, ChildrenStayConfined)
{
    Launcher launcher(profile());
    if (!launcher.coverage().files)
        GTEST_SKIP() << launcher.coverage().missing.toStdString();
    const QString secret = path("outside/secret.txt");
    const QStringList lines = run(launcher, {QStringLiteral("spawn"), QStringLiteral("read"), secret});
    ASSERT_EQ(lines.size(), 1) << lines.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(denied(lines, QStringLiteral("read ") + secret));
}

TEST_F(Sandbox, NoNetworkReachesNoServer)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
    Launcher launcher(profile(Network::None));
    ASSERT_TRUE(launcher.coverage().network) << launcher.coverage().missing.toStdString();
    QStringList arguments{QStringLiteral("tcp"), QStringLiteral("127.0.0.1"), QString::number(server.serverPort())};
#ifndef _WIN32
    QLocalServer bus;
    bus.setSocketOptions(QLocalServer::UserAccessOption);
    ASSERT_TRUE(bus.listen(QDir(m_root.path()).filePath(QStringLiteral("outside/bus"))));
    arguments << QStringLiteral("unix") << bus.fullServerName() << QStringLiteral("uring");
#endif
    const QStringList lines = run(launcher, arguments);
    EXPECT_TRUE(denied(lines, QStringLiteral("tcp"))) << lines.join(QLatin1Char('\n')).toStdString();
#ifndef _WIN32
    EXPECT_TRUE(denied(lines, QStringLiteral("unix"))) << lines.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(denied(lines, QStringLiteral("uring"))) << lines.join(QLatin1Char('\n')).toStdString();
#endif
}

#ifndef _WIN32
TEST_F(Sandbox, AnyNetworkReachesTheNetwork)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
    QLocalServer bus;
    bus.setSocketOptions(QLocalServer::UserAccessOption);
    ASSERT_TRUE(bus.listen(path("outside/bus")));
    Launcher launcher(profile(Network::Any));
    ASSERT_TRUE(launcher.coverage().network) << launcher.coverage().missing.toStdString();
    const QStringList lines = run(launcher, {QStringLiteral("tcp"), QStringLiteral("127.0.0.1"),
                                             QString::number(server.serverPort()), QStringLiteral("unix"),
                                             bus.fullServerName()});
    EXPECT_TRUE(ok(lines, QStringLiteral("tcp"))) << lines.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(denied(lines, QStringLiteral("unix"))) << lines.join(QLatin1Char('\n')).toStdString();
}

TEST_F(Sandbox, NoNewNamespaces)
{
    Launcher launcher(profile(Network::Any));
    ASSERT_TRUE(launcher.coverage().network) << launcher.coverage().missing.toStdString();
    const QStringList lines = run(launcher, {QStringLiteral("userns")});
    EXPECT_TRUE(denied(lines, QStringLiteral("userns"))) << lines.join(QLatin1Char('\n')).toStdString();
}
#endif

#ifdef _WIN32
TEST_F(Sandbox, AnAgentWithTheNetworkStillReachesNothingOnLoopback)
{
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
    Launcher launcher(profile(Network::Any));
    ASSERT_FALSE(launcher.containerSid().isEmpty()) << launcher.coverage().missing.toStdString();
    const QStringList lines = run(launcher, {QStringLiteral("tcp"), QStringLiteral("127.0.0.1"),
                                             QString::number(server.serverPort())});
    EXPECT_TRUE(denied(lines, QStringLiteral("tcp"))) << lines.join(QLatin1Char('\n')).toStdString();
}

TEST_F(Sandbox, OnlyProcessesItStartedRunInItsJobs)
{
    Launcher launcher(profile());
    EXPECT_FALSE(launcher.runs(QCoreApplication::applicationPid()));
    QProcess process;
    process.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
    process.setArguments({QStringLiteral("sleep"), QStringLiteral("60")});
    launcher.prepare(process);
    process.start();
    ASSERT_TRUE(process.waitForStarted(10'000));
    EXPECT_TRUE(launcher.runs(process.processId()));
    EXPECT_FALSE(Launcher(profile()).runs(process.processId()));
    launcher.killTree();
    EXPECT_TRUE(process.waitForFinished(5'000));
}

TEST_F(Sandbox, KillTreeEndsTheChildrenToo)
{
    Launcher launcher(profile());
    QProcess process;
    process.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
    process.setArguments({QStringLiteral("spawn"), QStringLiteral("sleep"), QStringLiteral("60")});
    launcher.prepare(process);
    process.start();
    ASSERT_TRUE(process.waitForStarted(10'000));
    launcher.killTree();
    EXPECT_TRUE(process.waitForFinished(5'000));
}
#endif

}
