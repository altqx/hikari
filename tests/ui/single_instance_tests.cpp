// WS5: one HikariSub per user session (SingleInstance). The second start is
// a child process of this test (HIKARI_SINGLE_INSTANCE_CHILD): it hands its
// files to the holder, holds the instance itself and is killed (a crash), or
// holds it without ever answering. Every test has its own socket and lock,
// so nothing reaches a HikariSub the user runs.

#include "hikari/app/single_instance.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <QtTest>

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace hikari;
using Role = app::SingleInstance::Role;

namespace {

constexpr char kChild[] = "HIKARI_SINGLE_INSTANCE_CHILD";
constexpr char kServer[] = "HIKARI_SINGLE_INSTANCE_SERVER";
constexpr char kLock[] = "HIKARI_SINGLE_INSTANCE_LOCK";

// The child's exit code for each role.
int code(Role role)
{
    return role == Role::Primary ? 10 : role == Role::Forwarded ? 11 : 12;
}

// "send": claims with its arguments as the files and exits with the role;
// "hold": claims, prints "holding" and serves until killed; "hang": claims,
// prints "holding" and never runs its event loop.
int child(int argc, char **argv)
{
    QCoreApplication core(argc, argv);
    const QString mode = qEnvironmentVariable(kChild);
    app::SingleInstance instance({qEnvironmentVariable(kServer), qEnvironmentVariable(kLock)});
    const app::SingleInstance::Request request{QCoreApplication::arguments().mid(1),
                                               qEnvironmentVariable("HIKARI_SINGLE_INSTANCE_TOKEN")};
    const Role role = instance.claim(request, true, 3000);
    if (mode == QLatin1String("send"))
        return code(role);
    if (role != Role::Primary)
        return code(role);
    std::printf("holding\n");
    std::fflush(stdout);
    if (mode == QLatin1String("hang"))
        for (;;)
            QThread::sleep(1);
    return core.exec();
}

} // namespace

class SingleInstanceTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    app::SingleInstance::Location location;

    QProcess *start(const char *mode, const QStringList &files = {}, const QString &token = {})
    {
        auto *process = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QLatin1String(kChild), QLatin1String(mode));
        env.insert(QLatin1String(kServer), location.server);
        env.insert(QLatin1String(kLock), location.lock);
        env.insert(QStringLiteral("HIKARI_SINGLE_INSTANCE_TOKEN"), token);
        process->setProcessEnvironment(env);
        process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process->start(QCoreApplication::applicationFilePath(), files);
        return process;
    }
    // The child's role once it has exited.
    int run(const QStringList &files = {}, const QString &token = {})
    {
        QProcess *process = start("send", files, token);
        // The holder answers from this event loop while the child waits.
        QElapsedTimer waited;
        waited.start();
        while (process->state() != QProcess::NotRunning && waited.elapsed() < 15000)
            QTest::qWait(20);
        return process->state() == QProcess::NotRunning && process->exitStatus() == QProcess::NormalExit
                   ? process->exitCode()
                   : -1;
    }
    static bool holding(QProcess *process)
    {
        return process->waitForReadyRead(10000) && process->readLine().trimmed() == "holding";
    }

private slots:
    void initTestCase() { QVERIFY(dir.isValid()); }
    void init()
    {
        const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef _WIN32
        location = {QStringLiteral("hikari-test-") + name, QStringLiteral("Local\\hikari-test-") + name};
#else
        location = {dir.filePath(name + QStringLiteral(".socket")), dir.filePath(name + QStringLiteral(".lock"))};
#endif
    }

    // The arguments that are files: the program's switches are not, a file:
    // URL is its path, a relative path is the start's folder's.
    void launchFilesKeepFilesOnly()
    {
        const QString cwd = dir.filePath(QStringLiteral("work"));
        const QString absolute = dir.filePath(QStringLiteral("abs/b.ass"));
        const QStringList files = app::launchFiles(
            {QStringLiteral("a.ass"), QStringLiteral("--hikari-health=/x"), QStringLiteral("--hikari-updated-from=0.1.0"),
             QStringLiteral("sub/../c d.srt"), absolute, QUrl::fromLocalFile(dir.filePath(QStringLiteral("ü/é.ass"))).toString(),
             QString(), QStringLiteral("字幕.ass")},
            cwd);
        QCOMPARE(files, (QStringList{QDir(cwd).filePath(QStringLiteral("a.ass")), QDir(cwd).filePath(QStringLiteral("c d.srt")),
                                     QDir::cleanPath(absolute), QDir::cleanPath(dir.filePath(QStringLiteral("ü/é.ass"))),
                                     QDir(cwd).filePath(QStringLiteral("字幕.ass"))}));
        QVERIFY(app::launchFiles({QStringLiteral("--version")}, cwd).isEmpty());
    }

    // A later start hands its files and its activation token over and exits.
    void aLaterStartHandsItsFilesOver()
    {
        app::SingleInstance holder(location);
        QCOMPARE(holder.claim({}), Role::Primary);
        QVERIFY(holder.listening());
        QSignalSpy requested(&holder, &app::SingleInstance::requested);
        const QStringList files{dir.filePath(QStringLiteral("a.ass")), dir.filePath(QStringLiteral("字幕 b.ass"))};
        QCOMPARE(run(files, QStringLiteral("token-1")), code(Role::Forwarded));
        QTRY_COMPARE(requested.size(), 1);
        const auto request = requested.at(0).at(0).value<app::SingleInstance::Request>();
        QCOMPARE(request.files, files);
        QCOMPARE(request.activationToken, QStringLiteral("token-1"));
    }

    // A start without files still hands over: the holder's window activates.
    void aStartWithoutFilesHandsOverToo()
    {
        app::SingleInstance holder(location);
        QCOMPARE(holder.claim({}), Role::Primary);
        QSignalSpy requested(&holder, &app::SingleInstance::requested);
        QCOMPARE(run(), code(Role::Forwarded));
        QTRY_COMPARE(requested.size(), 1);
        QVERIFY(requested.at(0).at(0).value<app::SingleInstance::Request>().files.isEmpty());
    }

    // A crashed holder (killed) leaves its socket file behind; the next start
    // claims the instance all the same and serves the one after it.
    void aCrashedHolderDoesNotBlockTheNextStart()
    {
        QProcess *crashed = start("hold");
        QVERIFY(holding(crashed));
        crashed->kill();
        QVERIFY(crashed->waitForFinished(10000));
#ifndef _WIN32
        QVERIFY(QFile::exists(location.server)); // the socket a crash leaves
#endif
        app::SingleInstance next(location);
        QCOMPARE(next.claim({}, true, 3000), Role::Primary);
        QVERIFY(next.listening());
        QSignalSpy requested(&next, &app::SingleInstance::requested);
        QCOMPARE(run({dir.filePath(QStringLiteral("c.ass"))}), code(Role::Forwarded));
        QTRY_COMPARE(requested.size(), 1);
    }

    // Anything at the socket's place that is not a holder (a file left by
    // another program) is replaced by the start that holds the lock.
    void aStaleSocketFileIsReplaced()
    {
#ifdef _WIN32
        QSKIP("Named pipes leave nothing behind");
#else
        QFile stale(location.server);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        app::SingleInstance holder(location);
        QCOMPARE(holder.claim({}), Role::Primary);
        QVERIFY(holder.listening());
        QCOMPARE(run(), code(Role::Forwarded));
#endif
    }

    // A holder that never answers does not block startup: the start runs on
    // its own once the wait is over, without the instance.
    void aHolderThatDoesNotAnswerIsLeftAlone()
    {
        QProcess *hung = start("hang");
        QVERIFY(holding(hung));
        app::SingleInstance second(location);
        QElapsedTimer took;
        took.start();
        QCOMPARE(second.claim({{dir.filePath(QStringLiteral("d.ass"))}, {}}, true, 1500), Role::Alone);
        QVERIFY(took.elapsed() < 10000);
        QVERIFY(!second.listening());
        // A start that must not hand over (an update's new version) does not wait.
        took.restart();
        QCOMPARE(second.claim({}, false), Role::Alone);
        QVERIFY(took.elapsed() < 1000);
        hung->kill();
        hung->waitForFinished(10000);
    }

    // A start that gave up waiting (the holder's event loop was busy past the
    // wait) runs alone: once the holder answers, it does not open the files
    // a second time.
    void aStartThatGaveUpIsNotOpenedLater()
    {
        app::SingleInstance holder(location);
        QCOMPARE(holder.claim({}), Role::Primary);
        QSignalSpy requested(&holder, &app::SingleInstance::requested);
        // This event loop does not run while the start waits for its answer.
        QProcess *late = start("send", {dir.filePath(QStringLiteral("e.ass"))});
        QVERIFY(late->waitForFinished(15000));
        QCOMPARE(late->exitCode(), code(Role::Alone));
        QTest::qWait(1000);
        QCOMPARE(requested.size(), 0);
        const QString next = dir.filePath(QStringLiteral("f.ass"));
        QCOMPARE(run({next}), code(Role::Forwarded));
        QTRY_COMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(0).value<app::SingleInstance::Request>().files, QStringList{next});
        QTest::qWait(500);
        QCOMPARE(requested.size(), 1);
    }

    // A holder that answers the request but closes before taking the
    // commit (it quit or crashed) has not opened the files: the start does
    // not leave as if it had.
    void aHolderThatClosesBeforeTheCommitIsNotTrusted()
    {
        // The lock is held, and at the socket a holder that acks and drops.
        app::SingleInstance holder({location.server + QStringLiteral("-elsewhere"), location.lock});
        QCOMPARE(holder.claim({}), Role::Primary);
        QLocalServer dropping;
        QLocalServer::removeServer(location.server);
        QVERIFY(dropping.listen(location.server));
        int dropped = 0;
        connect(&dropping, &QLocalServer::newConnection, this, [&] {
            while (QLocalSocket *socket = dropping.nextPendingConnection()) {
                connect(socket, &QLocalSocket::readyRead, socket, [socket, &dropped] {
                    QByteArray buffer = socket->property("got").toByteArray() + socket->readAll();
                    socket->setProperty("got", buffer);
                    if (socket->property("acked").toBool()) {
                        ++dropped; // the commit: closed without the receipt
                        socket->abort();
                        return;
                    }
                    if (buffer.size() < 19 || buffer.size() < 19 + qFromBigEndian<quint32>(buffer.constData() + 15))
                        return;
                    socket->setProperty("acked", true);
                    socket->putChar('\1');
                    socket->flush();
                });
            }
        });
        QCOMPARE(run({dir.filePath(QStringLiteral("g.ass"))}), code(Role::Alone));
        QVERIFY(dropped > 0);
    }

    // A start that holds the lock but cannot listen (its socket or pipe name
    // is refused) gives the instance up and runs alone, so the next start is
    // the instance.
    void aStartThatCannotListenGivesTheInstanceUp()
    {
#ifdef _WIN32
        const QString refused = QStringLiteral("hikari-test-") + QString(300, QLatin1Char('x'));
#else
        const QString refused = dir.filePath(QString(200, QLatin1Char('x')) + QStringLiteral(".socket"));
#endif
        app::SingleInstance deaf({refused, location.lock});
        QCOMPARE(deaf.claim({}), Role::Alone);
        QVERIFY(!deaf.listening());
        app::SingleInstance next(location);
        QCOMPARE(next.claim({}, false), Role::Primary);
        QVERIFY(next.listening());
    }

    // The holder gives the instance up (at exit, before an update starts the
    // new version): the next start is the instance.
    void releaseLetsTheNextStartClaim()
    {
        app::SingleInstance holder(location);
        QCOMPARE(holder.claim({}), Role::Primary);
        holder.release();
        QVERIFY(!holder.listening());
        QCOMPARE(run(), code(Role::Primary));
        app::SingleInstance again(location);
        QCOMPARE(again.claim({}), Role::Primary);
    }

    // Requests that are not the protocol's are dropped unanswered.
    void malformedRequestsAreDropped()
    {
        app::SingleInstance holder(location);
        QCOMPARE(holder.claim({}), Role::Primary);
        QSignalSpy requested(&holder, &app::SingleInstance::requested);
        const QByteArray tooLong = QByteArray("HIKARISUB-OPEN\1") + QByteArray::fromHex("7fffffff");
        for (const QByteArray &bytes : {QByteArray("GET / HTTP/1.0\r\n\r\n"), tooLong,
                                        QByteArray("HIKARISUB-OPEN\1") + QByteArray::fromHex("00000002") + "xy"}) {
            QLocalSocket socket;
            socket.connectToServer(location.server);
            QVERIFY(socket.waitForConnected(3000));
            socket.write(bytes);
            socket.flush();
            QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
            QVERIFY(socket.readAll().isEmpty());
        }
        QCOMPARE(requested.size(), 0);
        QCOMPARE(run(), code(Role::Forwarded)); // still serving
    }

    // Each user session has its own instance; on Linux in a folder only the
    // user may enter.
    void eachSessionHasItsOwn()
    {
#ifdef _WIN32
        const auto here = app::SingleInstance::forSession();
        QVERIFY(here.lock.startsWith(QLatin1String("Local\\io.github.altqx.HikariSub.Instance-S-1-")));
        DWORD session = 0;
        QVERIFY(::ProcessIdToSessionId(::GetCurrentProcessId(), &session));
        QVERIFY(here.server.endsWith(QLatin1Char('-') + QString::number(session)));
        QVERIFY(here.server.startsWith(here.lock.mid(6)));
#else
        const QByteArray wayland = qgetenv("WAYLAND_DISPLAY"), display = qgetenv("DISPLAY"),
                         runtime = qgetenv("XDG_RUNTIME_DIR");
        auto restore = qScopeGuard([&] {
            for (const auto &[name, value] : {std::pair{"WAYLAND_DISPLAY", wayland}, std::pair{"DISPLAY", display},
                                              std::pair{"XDG_RUNTIME_DIR", runtime}})
                value.isNull() ? qunsetenv(name) : qputenv(name, value);
        });
        QTemporaryDir runtimeDir;
        QVERIFY(::chmod(QFile::encodeName(runtimeDir.path()).constData(), 0700) == 0);
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtimeDir.path()));
        qunsetenv("WAYLAND_DISPLAY");
        qputenv("DISPLAY", ":0");
        const auto x0 = app::SingleInstance::forSession();
        QVERIFY(x0.server.startsWith(runtimeDir.path() + QLatin1Char('/')));
        QVERIFY(x0.lock.startsWith(runtimeDir.path() + QLatin1Char('/')));
        qputenv("DISPLAY", ":1");
        const auto x1 = app::SingleInstance::forSession();
        qputenv("WAYLAND_DISPLAY", "wayland-1");
        const auto w1 = app::SingleInstance::forSession();
        QVERIFY(x0.server != x1.server && x0.lock != x1.lock);
        QVERIFY(w1.server != x1.server && w1.server != x0.server);
        // Nor one where the socket's path would be longer than a socket address holds.
        const QString deep = runtimeDir.path() + QLatin1Char('/') + QString(100, QLatin1Char('r'));
        QVERIFY(::mkdir(QFile::encodeName(deep).constData(), 0700) == 0);
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(deep));
        const auto tooLong = app::SingleInstance::forSession();
        QVERIFY(tooLong.server.startsWith(QDir::tempPath() + QStringLiteral("/hikarisub-")));
        QVERIFY(QFile::encodeName(tooLong.server).size() < 108);
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtimeDir.path()));
        // A runtime folder others may enter is not used: a private one in the temporary folder is.
        QVERIFY(::chmod(QFile::encodeName(runtimeDir.path()).constData(), 0755) == 0);
        const auto fallback = app::SingleInstance::forSession();
        const QString own = QDir::tempPath() + QStringLiteral("/hikarisub-") + QString::number(::getuid());
        QVERIFY(fallback.server.startsWith(own + QLatin1Char('/')));
        struct stat info{};
        QVERIFY(::lstat(QFile::encodeName(own).constData(), &info) == 0);
        QCOMPARE(int(info.st_mode & 0777), 0700);
#endif
    }
};

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet(kChild))
        return child(argc, argv);
    QCoreApplication core(argc, argv);
    SingleInstanceTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}

#include "single_instance_tests.moc"
