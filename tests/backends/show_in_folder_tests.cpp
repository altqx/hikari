// P9: legacy SelectInFolder (HikariSub/config.cpp:1048-1062 at 20d647c4) as
// the "Open ... containing folder" items and a Ctrl+click on a recent file
// use it. On Linux the desktop's file manager is asked over D-Bus
// (org.freedesktop.FileManager1.ShowItems) and the folder opens when none
// answers; these tests run a private session bus with a fake file manager.
// Windows uses SHOpenFolderAndSelectItems as legacy did (observed on
// Windows, not here).

#include "hikari/backends/show_in_folder.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#ifndef Q_OS_WIN // not _WIN32: moc sees only WIN32 (and so Q_OS_WIN)
#include <QDBusConnection>
#include <QDBusConnectionInterface>

// The fake file manager: records each ShowItems call.
class FakeFileManager : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    QList<QStringList> shown;
    QStringList startupIds;
public slots:
    void ShowItems(const QStringList &uris, const QString &startupId)
    {
        shown << uris;
        startupIds << startupId;
    }
};
#endif

class ShowInFolderTests : public QObject {
    Q_OBJECT

private slots:
    // wxFileName(filename).GetPath(), or the path itself without a folder part.
    void containingFolder()
    {
        using hikari::backends::containingFolder;
        QCOMPARE(QDir::fromNativeSeparators(containingFolder(QStringLiteral("/media/show/ep01.mkv"))),
                 QStringLiteral("/media/show"));
        QCOMPARE(QDir::fromNativeSeparators(containingFolder(QStringLiteral("/ep01.mkv"))), QStringLiteral("/"));
        QCOMPARE(containingFolder(QStringLiteral("ep01.mkv")), QStringLiteral("ep01.mkv"));
    }

#ifndef Q_OS_WIN // not _WIN32: moc sees only WIN32 (and so Q_OS_WIN)
    void fileManagerShowsTheFileSelected()
    {
        const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
        if (daemon.isEmpty())
            QSKIP("dbus-daemon is not installed");
        // A private bus without service directories, so nothing is
        // activated (a real file manager never starts).
        QTemporaryDir busDir;
        const QString config = busDir.filePath(QStringLiteral("bus.conf"));
        {
            QFile f(config);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
                    " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                    "<busconfig><type>session</type><listen>unix:dir=" + busDir.path().toUtf8() + "</listen>"
                    "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
                    "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n");
        }
        QProcess bus;
        bus.start(daemon, {QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                           QStringLiteral("--print-address=1")});
        QVERIFY(bus.waitForStarted());
        QVERIFY(bus.waitForReadyRead(5000));
        const QString address = QString::fromUtf8(bus.readLine()).trimmed();
        QVERIFY(!address.isEmpty());
        auto cleanup = qScopeGuard([&] {
            QDBusConnection::disconnectFromBus(QStringLiteral("p9-server"));
            QDBusConnection::disconnectFromBus(QStringLiteral("p9-client"));
            bus.kill();
            bus.waitForFinished();
        });
        QDBusConnection server = QDBusConnection::connectToBus(address, QStringLiteral("p9-server"));
        QDBusConnection client = QDBusConnection::connectToBus(address, QStringLiteral("p9-client"));
        QVERIFY(server.isConnected() && client.isConnected());

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("ep 01 (ä).mkv"));
        QFile(file).open(QIODevice::WriteOnly);

        // No file manager on the bus: the folder opens instead (legacy Linux).
        QStringList fallbacks;
        hikari::backends::selectInFolder(file, client, [&](const QString &folder) { fallbacks << folder; });
        QTRY_COMPARE(fallbacks.size(), 1);
        QCOMPARE(QDir::fromNativeSeparators(fallbacks[0]), dir.path());

        FakeFileManager manager;
        QVERIFY(server.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), &manager,
                                      QDBusConnection::ExportAllSlots));
        QVERIFY(server.registerService(QStringLiteral("org.freedesktop.FileManager1")));
        hikari::backends::selectInFolder(file, client, [&](const QString &folder) { fallbacks << folder; });
        QTRY_COMPARE(manager.shown.size(), 1);
        QCOMPARE(manager.shown[0], QStringList{QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)});
        QCOMPARE(QUrl(manager.shown[0][0]).toLocalFile(), file); // the file itself, to be selected
        QCOMPARE(manager.startupIds[0], QString());
        QTest::qWait(100);
        QCOMPARE(fallbacks.size(), 1); // answered: no folder opened

        // Without a session bus at all: the folder.
        QDBusConnection none = QDBusConnection(QStringLiteral("p9-none"));
        hikari::backends::selectInFolder(file, none, [&](const QString &folder) { fallbacks << folder; });
        QCOMPARE(fallbacks.size(), 2);
    }
#endif
};

QTEST_GUILESS_MAIN(ShowInFolderTests)
#include "show_in_folder_tests.moc"
