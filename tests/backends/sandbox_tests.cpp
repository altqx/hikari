#include "hikari/backends/sandbox.h"
#include "hikari/backends/user_alone.h"

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
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

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

#ifdef _WIN32
    Profile lowRights() const
    {
        Profile p = profile(Network::Any);
        p.lowRightsHelper = QStringLiteral(HIKARI_TEST_CONFINE_HELPER);
        return p;
    }
#endif

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

#ifdef _WIN32
bool aceFor(const QString &path, const QString &sidText)
{
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(reinterpret_cast<LPCWSTR>(sidText.utf16()), &sid))
        return false;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    bool found = false;
    if (GetNamedSecurityInfoW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS &&
        acl)
        for (DWORD i = 0; i < acl->AceCount && !found; ++i) {
            void *ace = nullptr;
            found = GetAce(acl, i, &ace) && static_cast<ACE_HEADER *>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                    EqualSid(&static_cast<ACCESS_ALLOWED_ACE *>(ace)->SidStart, sid);
        }
    LocalFree(descriptor);
    LocalFree(sid);
    return found;
}

bool setProtectedDacl(const QString &path, const wchar_t *sddl)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr))
        return false;
    BOOL present = FALSE, defaulted = FALSE;
    PACL acl = nullptr;
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const bool ok = GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) &&
                    SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                          nullptr, acl, nullptr) == ERROR_SUCCESS;
    LocalFree(descriptor);
    return ok;
}

bool allow(const QString &path, const wchar_t *sidText, DWORD access)
{
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(sidText, &sid))
        return false;
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PACL current = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    bool ok = GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &current,
                                    nullptr, &descriptor) == ERROR_SUCCESS;
    PACL updated = nullptr;
    if (ok) {
        EXPLICIT_ACCESSW entry{};
        entry.grfAccessPermissions = access;
        entry.grfAccessMode = GRANT_ACCESS;
        entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        ok = SetEntriesInAclW(1, &entry, current, &updated) == ERROR_SUCCESS &&
             SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, updated,
                                   nullptr) == ERROR_SUCCESS;
    }
    LocalFree(updated);
    LocalFree(descriptor);
    LocalFree(sid);
    return ok;
}
#endif

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

TEST_F(Sandbox, AFolderEveryAppContainerCanAlreadyReadGetsNoAceOfItsOwn)
{
    ASSERT_TRUE(QDir(m_root.path()).mkpath(QStringLiteral("packages")));
    ASSERT_TRUE(setProtectedDacl(path("packages"), L"D:P(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)(A;OICI;GRGX;;;AC)"));
    Profile p = profile();
    p.readExecute << path("packages");
    Launcher launcher(p);
    ASSERT_FALSE(launcher.containerSid().isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(launcher.coverage().missing.isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_FALSE(aceFor(path("packages"), launcher.containerSid()));
}

TEST_F(Sandbox, AFolderEveryAppContainerCanWriteStillGetsTheContainersOwnAce)
{
    ASSERT_TRUE(QDir(m_root.path()).mkpath(QStringLiteral("shared")));
    ASSERT_TRUE(setProtectedDacl(path("shared"), L"D:P(A;OICI;FA;;;OW)(A;OICI;FA;;;SY)(A;OICI;FA;;;AC)"));
    Profile p = profile();
    p.readWrite << path("shared");
    Launcher launcher(p);
    ASSERT_FALSE(launcher.containerSid().isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(launcher.coverage().missing.isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(aceFor(path("shared"), launcher.containerSid()));
}

TEST_F(Sandbox, AFolderTheContainerCantReachGetsItsAce)
{
    Launcher launcher(profile());
    ASSERT_FALSE(launcher.containerSid().isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(launcher.coverage().missing.isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(aceFor(path("readonly"), launcher.containerSid()));
    EXPECT_TRUE(aceFor(path("allowed"), launcher.containerSid()));
}

TEST_F(Sandbox, AFolderTheUserCantChangeIsReportedAsRefused)
{
    ASSERT_TRUE(QDir(m_root.path()).mkpath(QStringLiteral("locked")));
    ASSERT_TRUE(setProtectedDacl(path("locked"), L"D:P(A;OICI;GRGX;;;OW)(A;OICI;GRGX;;;WD)"));
    Profile p = profile();
    p.readExecute << path("locked");
    Launcher launcher(p);
    ASSERT_FALSE(launcher.containerSid().isEmpty()) << launcher.coverage().missing.toStdString();
    const QString missing = launcher.coverage().missing;
    EXPECT_TRUE(missing.contains(QDir::toNativeSeparators(path("locked")))) << missing.toStdString();
    EXPECT_TRUE(missing.contains(QStringLiteral("can't change its permissions"))) << missing.toStdString();
    EXPECT_FALSE(missing.contains(QDir::toNativeSeparators(path("readonly")))) << missing.toStdString();
    EXPECT_FALSE(aceFor(path("locked"), launcher.containerSid()));
}

TEST_F(Sandbox, HikariSubsOwnCodeOpensToEveryAppContainerRatherThanToOne)
{
    ASSERT_TRUE(QDir(m_root.path()).mkpath(QStringLiteral("runtime")));
    Profile p = profile();
    p.readExecute << path("runtime");
    Launcher launcher(p);
    ASSERT_FALSE(launcher.containerSid().isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(aceFor(path("runtime"), QStringLiteral("S-1-15-2-1")));
    EXPECT_FALSE(aceFor(path("runtime"), launcher.containerSid()));
}

TEST_F(Sandbox, ALowRightsProcessReadsTheUsersFilesButWritesOnlyItsOwnFolders)
{
    Launcher launcher(lowRights());
    ASSERT_TRUE(launcher.coverage().missing.isEmpty()) << launcher.coverage().missing.toStdString();
    EXPECT_TRUE(launcher.containerSid().isEmpty());
    const QString secret = path("outside/secret.txt");
    const QString mine = path("allowed/new.txt");
    const QString elsewhere = path("outside/new.txt");
    const QStringList lines = run(launcher, {QStringLiteral("low"), QStringLiteral("desktop"), QStringLiteral("read"),
                                             secret, QStringLiteral("write"), mine, QStringLiteral("write"), elsewhere});
    const std::string said = lines.join(QLatin1Char('\n')).toStdString();
    EXPECT_TRUE(ok(lines, QStringLiteral("low"))) << said;
    EXPECT_TRUE(ok(lines, QStringLiteral("desktop"))) << said;
    EXPECT_TRUE(ok(lines, QStringLiteral("read ") + secret)) << said;
    EXPECT_TRUE(ok(lines, QStringLiteral("write ") + mine)) << said;
    EXPECT_TRUE(denied(lines, QStringLiteral("write ") + elsewhere)) << said;
}

TEST_F(Sandbox, ALowRightsProcessesFoldersCloseToLowRightsOnceItsLauncherEnds)
{
    const QString mine = path("allowed/mine.txt");
    {
        Launcher launcher(lowRights());
        ASSERT_TRUE(ok(run(launcher, {QStringLiteral("read"), mine}), QStringLiteral("read ") + mine));
    }
    Profile other = lowRights();
    other.readWrite = {path("readonly")};
    Launcher launcher(other);
    const QStringList lines = run(launcher, {QStringLiteral("read"), mine});
    EXPECT_TRUE(denied(lines, QStringLiteral("read ") + mine)) << lines.join(QLatin1Char('\n')).toStdString();
}

TEST_F(Sandbox, AFolderGivenToTheUserAloneIsClosedToLowRights)
{
    ASSERT_TRUE(QDir(m_root.path()).mkpath(QStringLiteral("secrets")));
    ASSERT_TRUE(hikari::backends::keepForUserAlone(path("secrets")));
    QFile token(path("secrets/token"));
    ASSERT_TRUE(token.open(QIODevice::WriteOnly));
    token.close();
    Launcher launcher(lowRights());
    const QStringList lines = run(launcher, {QStringLiteral("read"), token.fileName()});
    EXPECT_TRUE(denied(lines, QStringLiteral("read ") + token.fileName())) << lines.join(QLatin1Char('\n')).toStdString();
}

TEST_F(Sandbox, KillTreeEndsALowRightsProcessAndItsChildren)
{
    Launcher launcher(lowRights());
    QProcess process;
    process.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
    process.setArguments({QStringLiteral("spawn"), QStringLiteral("sleep"), QStringLiteral("60")});
    launcher.prepare(process);
    process.start();
    ASSERT_TRUE(process.waitForStarted(10'000));
    EXPECT_TRUE(launcher.runs(process.processId()));
    launcher.killTree();
    EXPECT_TRUE(process.waitForFinished(5'000));
}

TEST_F(Sandbox, WithoutItsHelperALowRightsProcessDoesntStartAtAll)
{
    Profile p = lowRights();
    p.lowRightsHelper = path("missing/hikarisub-confine.exe");
    Launcher launcher(p);
    EXPECT_TRUE(launcher.coverage().missing.contains(QStringLiteral("low-rights helper is missing")))
        << launcher.coverage().missing.toStdString();
    QProcess process;
    process.setProgram(QStringLiteral(HIKARI_SANDBOX_PROBE));
    process.setArguments({QStringLiteral("low")});
    launcher.prepare(process);
    process.start();
    EXPECT_FALSE(process.waitForStarted(10'000));
}

TEST_F(Sandbox, AHelperThatCantStartTheCommandSaysWhyAndStartsNothing)
{
    Launcher launcher(lowRights());
    QProcess process;
    process.setProgram(path("outside/missing.exe"));
    launcher.prepare(process);
    process.start();
    ASSERT_TRUE(process.waitForFinished(10'000)) << process.errorString().toStdString();
    EXPECT_EQ(process.exitCode(), 0x4846);
    EXPECT_TRUE(process.readAllStandardError().contains("hikarisub-confine: starting the command failed"));
}
#endif

}

TEST(UserAlone, AFolderGivenToTheUserAloneKeepsEveryoneElseOut)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString folder = root.filePath(QStringLiteral("secret"));
    ASSERT_TRUE(QDir().mkdir(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                         QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                         QFileDevice::ExeOther));
    EXPECT_FALSE(hikari::backends::isForUserAlone(folder));
    ASSERT_TRUE(hikari::backends::keepForUserAlone(folder));
    EXPECT_TRUE(hikari::backends::isForUserAlone(folder));
    EXPECT_FALSE(hikari::backends::isForUserAlone(root.filePath(QStringLiteral("missing"))));
#ifdef Q_OS_WIN
    QFile file(folder + QStringLiteral("/token"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    EXPECT_TRUE(hikari::backends::isForUserAlone(file.fileName()));
#endif
}

#ifdef _WIN32
TEST(UserAlone, AFolderEveryAppContainerCanOpenIsNotTheUsersAlone)
{
    for (const wchar_t *packages : {L"S-1-15-2-1", L"S-1-15-2-2"}) {
        QTemporaryDir root;
        ASSERT_TRUE(root.isValid());
        const QString folder = root.filePath(QStringLiteral("secret"));
        ASSERT_TRUE(QDir().mkdir(folder));
        ASSERT_TRUE(hikari::backends::keepForUserAlone(folder));
        ASSERT_TRUE(allow(folder, packages, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE));
        EXPECT_FALSE(hikari::backends::isForUserAlone(folder));
        ASSERT_TRUE(hikari::backends::keepForUserAlone(folder));
        EXPECT_TRUE(hikari::backends::isForUserAlone(folder));
    }
}

TEST(UserAlone, OnlyHikariSubsOwnContainersKeepAFolderTheUsersAlone)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const QString own = root.filePath(QStringLiteral("own"));
    const QString other = root.filePath(QStringLiteral("other"));
    for (const QString &folder : {own, other}) {
        ASSERT_TRUE(QDir().mkdir(folder));
        ASSERT_TRUE(hikari::backends::keepForUserAlone(folder));
    }
    Launcher codex(Profile{.name = QStringLiteral("HikariSub.AI.Codex"), .readWrite = {own}});
    Launcher stranger(Profile{.name = QStringLiteral("HikariSub.Test.Stranger"), .readWrite = {other}});
    ASSERT_TRUE(aceFor(own, codex.containerSid())) << codex.coverage().missing.toStdString();
    ASSERT_TRUE(aceFor(other, stranger.containerSid())) << stranger.coverage().missing.toStdString();
    EXPECT_TRUE(hikari::backends::isForUserAlone(own));
    EXPECT_FALSE(hikari::backends::isForUserAlone(other));
}
#endif

TEST(OwnProfiles, EveryFixedProfileHasItsOwnValidName)
{
    const QStringList names = ownProfileNames();
    ASSERT_EQ(names.size(), int(OwnProfile::Count));
    for (const QString &name : names) {
        EXPECT_TRUE(name.startsWith(QStringLiteral("HikariSub."))) << name.toStdString();
        EXPECT_LE(name.size(), 64) << name.toStdString();
        EXPECT_TRUE(std::ranges::all_of(name, [](QChar c) {
            return c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('_');
        })) << name.toStdString();
        EXPECT_EQ(names.count(name), 1) << name.toStdString();
    }
}
