// ADR 0019: the update's download, its checks, the pending record, the
// start's clean-up, and the hand-off per package kind (AppImage and pkexec
// with stand-in programs; NSIS needs Windows and its installer).

#include "hikari/app/update_installer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::PackageInfo;
using hikari::app::UpdateEnvironment;
using hikari::app::UpdateInstaller;
using hikari::application::PackageKind;
using hikari::application::UpdatePackage;

namespace {

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool write(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size())
        return false;
    f.close();
    if (executable)
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return true;
}

UpdatePackage package(const QString &name, const QByteArray &bytes)
{
    return {name.toStdString(), bytes.size(), sha256(bytes).toStdString(), {}, {}};
}

} // namespace

class UpdateInstallerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    int fixtures = 0;

    // A fresh folder for one test.
    QString folder()
    {
        const QString path = dir.filePath(QString::number(++fixtures));
        QDir().mkpath(path);
        return path;
    }
    // The package served as a file.
    static QUrl serve(const QString &root, const QString &name, const QByteArray &bytes)
    {
        const QString path = root + QStringLiteral("/served/") + name;
        write(path, bytes);
        return QUrl::fromLocalFile(path);
    }
    static UpdateEnvironment environment(PackageKind kind, const QString &root)
    {
        UpdateEnvironment env;
        env.package = PackageInfo{kind, {}};
        env.cacheDir = root + QStringLiteral("/cache");
        env.glibcVersion = QStringLiteral("2.39");
        env.executable = root + QStringLiteral("/old-hikarisub");
        env.pkexec = QStringLiteral("/bin/false");
        return env;
    }
    static bool download(UpdateInstaller &installer, const QString &version, const UpdatePackage &p, const QUrl &url)
    {
        QSignalSpy ready(&installer, &UpdateInstaller::ready);
        installer.download(version, p, url, true);
        return ready.count() > 0 || ready.wait(5000);
    }

private slots:
    void initTestCase()
    {
#ifdef _WIN32
        QSKIP("the hand-offs tested here are Linux's; NSIS is checked on the Windows VM");
#endif
    }

    void downloadsVerifiesAndRecords()
    {
        const QString root = folder();
        const QByteArray bytes = "the new deb";
        const QString name = QStringLiteral("hikarisub-0.2.0-linux-amd64.deb");
        UpdateInstaller installer(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QVERIFY(installer.canInstall());
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, bytes), serve(root, name, bytes)));
        QCOMPARE(installer.state(), QStringLiteral("ready"));
        const QString file = root + QStringLiteral("/cache/0.2.0/") + name;
        QCOMPARE(installer.readyFile(), file);
        QCOMPARE(read(file), bytes);
        QVERIFY(!QFileInfo::exists(file + QStringLiteral(".part")));
        const QJsonObject pending = QJsonDocument::fromJson(read(root + QStringLiteral("/cache/pending.json"))).object();
        QCOMPARE(pending.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(pending.value(QStringLiteral("version")).toString(), QStringLiteral("0.2.0"));
        QCOMPARE(pending.value(QStringLiteral("kind")).toString(), QStringLiteral("deb"));
        QCOMPARE(pending.value(QStringLiteral("file")).toString(), file);
        QCOMPARE(pending.value(QStringLiteral("sha256")).toString(), sha256(bytes));
        QCOMPARE(pending.value(QStringLiteral("size")).toInt(), bytes.size());
        QVERIFY(!pending.value(QStringLiteral("downloaded")).toString().isEmpty());
        // A verified copy is taken again without a download.
        UpdateInstaller again(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QVERIFY(download(again, QStringLiteral("0.2.0"), package(name, bytes), QUrl::fromLocalFile(root + QStringLiteral("/nowhere"))));
        // Never a version that is not newer.
        UpdateInstaller newer(environment(PackageKind::Deb, folder()), QStringLiteral("0.2.0"));
        newer.download(QStringLiteral("0.2.0"), package(name, bytes), serve(root, name, bytes), true);
        QCOMPARE(newer.state(), QStringLiteral("idle"));
    }

    void aMismatchIsDeletedAndReportedWhenAsked()
    {
        const QString root = folder();
        const QString name = QStringLiteral("hikarisub-0.2.0-linux-amd64.deb");
        const QUrl url = serve(root, name, "tampered");
        UpdateInstaller installer(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QSignalSpy failed(&installer, &UpdateInstaller::failed);
        installer.download(QStringLiteral("0.2.0"), package(name, "the real deb"), url, true);
        QVERIFY(failed.wait(5000));
        QCOMPARE(installer.state(), QStringLiteral("idle"));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/0.2.0/") + name));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/0.2.0/") + name + QStringLiteral(".part")));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/pending.json")));
        // The automatic download fails silently.
        UpdateInstaller quiet(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QSignalSpy quietFailed(&quiet, &UpdateInstaller::failed);
        quiet.download(QStringLiteral("0.2.0"), package(name, "the real deb"), url, false);
        QTRY_COMPARE(quiet.state(), QStringLiteral("idle"));
        QCOMPARE(quietFailed.count(), 0);
        // So does one the size check stops: more bytes than announced.
        UpdatePackage small = package(name, "the real deb");
        small.size = 3;
        UpdateInstaller longer(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QSignalSpy longerFailed(&longer, &UpdateInstaller::failed);
        longer.download(QStringLiteral("0.2.0"), small, url, true);
        QVERIFY(longerFailed.wait(5000));
    }

    void startDropsWhatAnInstalledUpdateLeft()
    {
        const QString root = folder();
        const QString cache = root + QStringLiteral("/cache");
        const auto record = [&](const QString &version, const QString &kind, const QString &file, const QByteArray &bytes) {
            write(file, bytes);
            write(cache + QStringLiteral("/pending.json"),
                  QJsonDocument(QJsonObject{{"schema", 1}, {"version", version}, {"kind", kind}, {"file", file},
                                            {"sha256", sha256(bytes)}, {"size", qint64(bytes.size())}})
                      .toJson());
        };
        // Installed: the record and its file go, with the folders of
        // versions this one has reached; a newer one's stays.
        record(QStringLiteral("0.2.0"), QStringLiteral("deb"), cache + QStringLiteral("/0.2.0/x.deb"), "x");
        QDir().mkpath(cache + QStringLiteral("/0.1.9"));
        QDir().mkpath(cache + QStringLiteral("/0.3.0"));
        QDir().mkpath(cache + QStringLiteral("/other"));
        write(cache + QStringLiteral("/last-result.json"),
              QJsonDocument(QJsonObject{{"version", "0.2.0"}, {"ok", false}, {"code", 4}, {"message", "It failed"}}).toJson());
        UpdateInstaller installed(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        QCOMPARE(installed.start(), QStringLiteral("It failed"));
        QCOMPARE(installed.state(), QStringLiteral("idle"));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/pending.json")));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/0.2.0")));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/0.1.9")));
        QVERIFY(QFileInfo::exists(cache + QStringLiteral("/0.3.0")));
        QVERIFY(QFileInfo::exists(cache + QStringLiteral("/other")));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/last-result.json")));
        QCOMPARE(installed.start(), QString()); // the message shows once
        // Still to install: ready again at this start.
        record(QStringLiteral("0.3.0"), QStringLiteral("deb"), cache + QStringLiteral("/0.3.0/y.deb"), "y");
        UpdateInstaller waiting(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        waiting.start();
        QCOMPARE(waiting.state(), QStringLiteral("ready"));
        QCOMPARE(waiting.readyVersion(), QStringLiteral("0.3.0"));
        // Without pkexec it is the file and its command instead.
        auto env = environment(PackageKind::Deb, root);
        env.pkexec.clear();
        UpdateInstaller manual(env, QStringLiteral("0.2.0"));
        manual.start();
        QCOMPARE(manual.state(), QStringLiteral("manual"));
        QCOMPARE(manual.manualCommand(), QStringLiteral("sudo apt install ") + cache + QStringLiteral("/0.3.0/y.deb"));
        // A record of another kind (the copy was reinstalled) is dropped.
        UpdateInstaller rpm(environment(PackageKind::Rpm, root), QStringLiteral("0.2.0"));
        rpm.start();
        QCOMPARE(rpm.state(), QStringLiteral("idle"));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/pending.json")));
    }

    void pkexecRunsTheHelper()
    {
        const QString root = folder();
        const QString name = QStringLiteral("hikarisub-0.2.0-linux-x86_64.rpm");
        const QByteArray bytes = "the new rpm";
        // pkexec's stand-in: records its arguments, exits with the code asked for.
        const QString pkexec = root + QStringLiteral("/pkexec");
        QVERIFY(write(pkexec, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$(dirname \"$0\")/args\"\nexit $(cat \"$(dirname \"$0\")/code\")\n", true));
        const auto run = [&](int code, bool restart) {
            write(root + QStringLiteral("/code"), QByteArray::number(code));
            auto env = environment(PackageKind::Rpm, root);
            env.pkexec = pkexec;
            auto installer = std::make_unique<UpdateInstaller>(env, QStringLiteral("0.1.0"));
            installer->start();
            if (installer->state() != QLatin1String("ready"))
                download(*installer, QStringLiteral("0.2.0"), package(name, bytes), serve(root, name, bytes));
            if (restart)
                installer->restartNow();
            bool busy = false;
            const auto action = installer->installAtExit([&busy] {
                busy = true;
                return std::function<void()>();
            });
            return std::make_tuple(std::move(installer), action, busy);
        };
        // Installed, then this process becomes the new version.
        auto [installed, action, busy] = run(0, true);
        QVERIFY(busy);
        QCOMPARE(action.program, QStringLiteral("/usr/bin/hikarisub"));
        QCOMPARE(action.arguments, QStringList{QStringLiteral("--hikari-updated-from=0.1.0")});
        const QString file = root + QStringLiteral("/cache/0.2.0/") + name;
        QCOMPARE(QString::fromUtf8(read(root + QStringLiteral("/args"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                 (QStringList{QStringLiteral("/opt/hikarisub/libexec/hikarisub-update-helper"), QStringLiteral("install"),
                              QStringLiteral("rpm"), file, sha256(bytes), QStringLiteral("0.2.0")}));
        // Dismissed: still pending; a restart brings back the version installed.
        auto [dismissed, again, busy2] = run(126, true);
        QCOMPARE(again.program, root + QStringLiteral("/old-hikarisub"));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/cache/pending.json")));
        // At quit without a restart: nothing to run afterwards.
        auto [later, none, busy3] = run(126, false);
        QVERIFY(none.program.isEmpty());
        // The helper refused the package: reported at the next start, not retried.
        auto [refused, after, busy4] = run(3, false);
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/pending.json")));
        UpdateInstaller next(environment(PackageKind::Rpm, root), QStringLiteral("0.1.0"));
        QVERIFY(next.start().contains(QStringLiteral("0.2.0")));
        QCOMPARE(next.state(), QStringLiteral("idle"));
    }

    void anAppImageReplacesItselfKeepingTheOld()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/apps/My HikariSub.AppImage");
        QVERIFY(write(image, "#!/bin/sh\nexit 0\n", true));
        const QByteArray fresh = "#!/bin/sh\n# the new one\nexit 0\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        QVERIFY(installer.canInstall());
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        // Beside the image, hidden, executable.
        const QString staged = root + QStringLiteral("/apps/.HikariSub-0.2.0.AppImage");
        QCOMPARE(installer.readyFile(), staged);
        QVERIFY(QFileInfo(staged).isExecutable());
        // Later, at quit: swapped, the old one kept as .old until the new
        // version's start drops it.
        installer.installAtExit();
        QCOMPARE(read(image), fresh);
        QVERIFY(QFileInfo(image).isExecutable());
        QCOMPARE(read(image + QStringLiteral(".old")), QByteArray("#!/bin/sh\nexit 0\n"));
        QVERIFY(!QFileInfo::exists(staged));
        UpdateInstaller started(env, QStringLiteral("0.2.0"));
        started.start();
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/pending.json")));
    }

    void theWatchdogKeepsAnUpdateThatStarts()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        QVERIFY(write(image, "#!/bin/sh\nexit 0\n", true));
        // The new version reports its window through the health file.
        const QByteArray fresh = "#!/bin/sh\nfor a; do case \"$a\" in --hikari-health=*) : > \"${a#--hikari-health=}\";; esac; done\nsleep 1\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        installer.restartNow();
        QVERIFY(installer.restartRequested());
        installer.installAtExit();
        QCOMPARE(read(image), fresh);
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/health-0.2.0")));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/last-result.json")));
    }

    void theWatchdogPutsTheOldOneBack()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        const QByteArray old = "#!/bin/sh\nexit 0\n";
        QVERIFY(write(image, old, true));
        const QByteArray broken = "#!/bin/sh\nexit 1\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        env.healthTimeoutMs = 10 * 1000;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, broken), serve(root, name, broken)));
        installer.restartNow();
        QElapsedTimer took;
        took.start();
        installer.installAtExit();
        QVERIFY(took.elapsed() < 9000); // the exit, not the timeout, ended the wait
        QCOMPARE(read(image), old);
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
        UpdateInstaller next(env, QStringLiteral("0.1.0"));
        QVERIFY(next.start().contains(QStringLiteral("0.2.0")));
    }

    void healthIsReportedOnlyInTheCache()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        write(image, "new");
        write(image + QStringLiteral(".old"), "old");
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        QDir().mkpath(env.cacheDir);
        UpdateInstaller installer(env, QStringLiteral("0.2.0"));
        installer.reportHealthy(root + QStringLiteral("/health-0.2.0"));
        installer.reportHealthy(env.cacheDir + QStringLiteral("/other"));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/health-0.2.0")));
        QVERIFY(!QFileInfo::exists(env.cacheDir + QStringLiteral("/other")));
        QVERIFY(QFileInfo::exists(image + QStringLiteral(".old")));
        installer.reportHealthy(env.cacheDir + QStringLiteral("/health-0.2.0"));
        QVERIFY(QFileInfo::exists(env.cacheDir + QStringLiteral("/health-0.2.0")));
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
    }

    // A tampered file is not installed: the check before the hand-off.
    void theFileIsCheckedAgainBeforeTheHandOff()
    {
        const QString root = folder();
        const QString name = QStringLiteral("hikarisub-0.2.0-linux-amd64.deb");
        const QByteArray bytes = "the new deb";
        const QString pkexec = root + QStringLiteral("/pkexec");
        QVERIFY(write(pkexec, "#!/bin/sh\n: > \"$(dirname \"$0\")/ran\"\n", true));
        auto env = environment(PackageKind::Deb, root);
        env.pkexec = pkexec;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, bytes), serve(root, name, bytes)));
        write(installer.readyFile(), "the new deb, changed");
        installer.installAtExit();
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/ran")));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/pending.json")));
        QCOMPARE(installer.state(), QStringLiteral("idle"));
    }

    void theAurAndPortableCopiesInstallNothing()
    {
        const QString root = folder();
        for (const PackageKind kind : {PackageKind::Arch, PackageKind::Portable}) {
            UpdateInstaller installer(environment(kind, root), QStringLiteral("0.1.0"));
            QVERIFY(!installer.canInstall());
            installer.download(QStringLiteral("0.2.0"), package(QStringLiteral("x"), "x"), QUrl(), true);
            QCOMPARE(installer.state(), QStringLiteral("idle"));
        }
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache")));
    }
};

QTEST_GUILESS_MAIN(UpdateInstallerTest)
#include "update_installer_tests.moc"
