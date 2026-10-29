#include "hikari/app/update_installer.h"
#include "desktop_guard.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif

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

// kill(pid, 0) succeeds on a zombie, so /proc's state is read too.
bool running(qint64 pid)
{
#ifndef _WIN32
    if (::kill(pid_t(pid), 0) != 0)
        return false;
    const QByteArray stat = read(QStringLiteral("/proc/%1/stat").arg(pid));
    const qsizetype close = stat.lastIndexOf(')');
    return close < 0 || close + 2 >= stat.size() || stat.at(close + 2) != 'Z';
#else
    Q_UNUSED(pid);
    return false;
#endif
}

QString copyFile(const QString &root, const QString &pattern, PackageKind kind, const QString &appImage = {})
{
    const std::string key = hikari::application::updateCopyKey(
        hikari::application::packageKindToken(kind, false), appImage.toStdString());
    return root + QStringLiteral("/cache/") + pattern.arg(QString::fromStdString(key));
}
QString pendingOf(const QString &root, PackageKind kind, const QString &appImage = {})
{
    return copyFile(root, QStringLiteral("pending-%1.json"), kind, appImage);
}
QString atStartOf(const QString &root, const QString &appImage)
{
    return copyFile(root, QStringLiteral("at-start-%1"), PackageKind::AppImage, appImage);
}
QString stagedOf(const QString &appImage, const QString &version)
{
    return QString::fromStdString(hikari::application::appImageStagingPath(appImage.toStdString(), version.toStdString()));
}

UpdatePackage package(const QString &name, const QByteArray &bytes)
{
    return {name.toStdString(), bytes.size(), sha256(bytes).toStdString(), {}, {}};
}

}

class UpdateInstallerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    int fixtures = 0;

    QString folder()
    {
        const QString path = dir.filePath(QString::number(++fixtures));
        QDir().mkpath(path);
        return path;
    }
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
        const QString file = root + QStringLiteral("/cache/deb/0.2.0/") + name;
        QCOMPARE(installer.readyFile(), file);
        QCOMPARE(read(file), bytes);
        hikari::test::desktop::clear();
        installer.showInFolder();
        QCOMPARE(hikari::test::desktop::revealed(), QStringList{file});
        QVERIFY(!QFileInfo::exists(file + QStringLiteral(".part")));
        const QJsonObject pending = QJsonDocument::fromJson(read(pendingOf(root, PackageKind::Deb))).object();
        QCOMPARE(pending.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(pending.value(QStringLiteral("version")).toString(), QStringLiteral("0.2.0"));
        QCOMPARE(pending.value(QStringLiteral("kind")).toString(), QStringLiteral("deb"));
        QCOMPARE(pending.value(QStringLiteral("file")).toString(), file);
        QCOMPARE(pending.value(QStringLiteral("sha256")).toString(), sha256(bytes));
        QCOMPARE(pending.value(QStringLiteral("size")).toInt(), bytes.size());
        QVERIFY(!pending.value(QStringLiteral("downloaded")).toString().isEmpty());
        UpdateInstaller again(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QVERIFY(download(again, QStringLiteral("0.2.0"), package(name, bytes), QUrl::fromLocalFile(root + QStringLiteral("/nowhere"))));
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
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/deb/0.2.0/") + name));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/cache/deb/0.2.0/") + name + QStringLiteral(".part")));
        QVERIFY(!QFileInfo::exists(pendingOf(root, PackageKind::Deb)));
        UpdateInstaller quiet(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QSignalSpy quietFailed(&quiet, &UpdateInstaller::failed);
        quiet.download(QStringLiteral("0.2.0"), package(name, "the real deb"), url, false);
        QTRY_COMPARE(quiet.state(), QStringLiteral("idle"));
        QCOMPARE(quietFailed.count(), 0);
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
        const QString debRecord = pendingOf(root, PackageKind::Deb);
        const QString debDir = cache + QStringLiteral("/deb");
        const auto record = [&](const QString &version, const QString &kind, const QString &file, const QByteArray &bytes,
                                const QString &at) {
            write(file, bytes);
            write(at,
                  QJsonDocument(QJsonObject{{"schema", 1}, {"version", version}, {"kind", kind}, {"file", file},
                                            {"sha256", sha256(bytes)}, {"size", qint64(bytes.size())}})
                      .toJson());
        };
        record(QStringLiteral("0.2.0"), QStringLiteral("deb"), debDir + QStringLiteral("/0.2.0/x.deb"), "x", debRecord);
        QDir().mkpath(debDir + QStringLiteral("/0.1.9"));
        QDir().mkpath(debDir + QStringLiteral("/0.3.0"));
        QDir().mkpath(debDir + QStringLiteral("/other"));
        QDir().mkpath(cache + QStringLiteral("/rpm/0.1.9"));
        write(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::Rpm), "{}");
        write(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::Deb),
              QJsonDocument(QJsonObject{{"version", "0.2.0"}, {"ok", false}, {"code", 4}, {"message", "It failed"}}).toJson());
        UpdateInstaller installed(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        QCOMPARE(installed.start(), QStringLiteral("It failed"));
        QCOMPARE(installed.state(), QStringLiteral("idle"));
        QVERIFY(!QFileInfo::exists(debRecord));
        QVERIFY(!QFileInfo::exists(debDir + QStringLiteral("/0.2.0")));
        QVERIFY(!QFileInfo::exists(debDir + QStringLiteral("/0.1.9")));
        QVERIFY(QFileInfo::exists(debDir + QStringLiteral("/0.3.0")));
        QVERIFY(QFileInfo::exists(debDir + QStringLiteral("/other")));
        QVERIFY(QFileInfo::exists(cache + QStringLiteral("/rpm/0.1.9")));
        QVERIFY(QFileInfo::exists(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::Rpm)));
        QVERIFY(!QFileInfo::exists(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::Deb)));
        QCOMPARE(installed.start(), QString());
        record(QStringLiteral("0.3.0"), QStringLiteral("deb"), debDir + QStringLiteral("/0.3.0/y.deb"), "y", debRecord);
        UpdateInstaller waiting(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        waiting.start();
        QCOMPARE(waiting.state(), QStringLiteral("ready"));
        QCOMPARE(waiting.readyVersion(), QStringLiteral("0.3.0"));
        auto env = environment(PackageKind::Deb, root);
        env.pkexec.clear();
        UpdateInstaller manual(env, QStringLiteral("0.2.0"));
        manual.start();
        QCOMPARE(manual.state(), QStringLiteral("manual"));
        QCOMPARE(manual.manualCommand(), QStringLiteral("sudo ") + env.helper + QStringLiteral(" install ") + debDir +
                                             QStringLiteral("/0.3.0/y.deb 0.3.0"));
        UpdateInstaller rpm(environment(PackageKind::Rpm, root), QStringLiteral("0.2.0"));
        rpm.start();
        QCOMPARE(rpm.state(), QStringLiteral("idle"));
        QVERIFY(QFileInfo::exists(debRecord));
        QVERIFY(QFileInfo::exists(debDir + QStringLiteral("/0.3.0/y.deb")));
        const QByteArray rpmBytes = "the new rpm";
        const QString rpmName = QStringLiteral("hikarisub-0.3.0-linux-x86_64.rpm");
        QVERIFY(download(rpm, QStringLiteral("0.3.0"), package(rpmName, rpmBytes), serve(root, rpmName, rpmBytes)));
        QVERIFY(QFileInfo::exists(pendingOf(root, PackageKind::Rpm)));
        QVERIFY(QFileInfo::exists(debRecord));
        UpdateInstaller debAgain(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        debAgain.start();
        QCOMPARE(debAgain.readyFile(), debDir + QStringLiteral("/0.3.0/y.deb"));
        record(QStringLiteral("0.3.0"), QStringLiteral("rpm"), debDir + QStringLiteral("/0.3.0/z.rpm"), "z", debRecord);
        UpdateInstaller deb(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        deb.start();
        QCOMPARE(deb.state(), QStringLiteral("idle"));
        QVERIFY(QFileInfo::exists(debRecord));
        record(QStringLiteral("0.3.0"), QStringLiteral("snap"), debDir + QStringLiteral("/0.3.0/y.deb"), "y", debRecord);
        UpdateInstaller unknown(environment(PackageKind::Deb, root), QStringLiteral("0.2.0"));
        unknown.start();
        QCOMPARE(unknown.state(), QStringLiteral("idle"));
        QVERIFY(!QFileInfo::exists(debRecord));
    }

    void pkexecRunsTheHelper()
    {
        const QString root = folder();
        const QString name = QStringLiteral("hikarisub-0.2.0-linux-x86_64.rpm");
        const QByteArray bytes = "the new rpm";
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
        auto [installed, action, busy] = run(0, true);
        QVERIFY(busy);
        QCOMPARE(action.program, QStringLiteral("/usr/bin/hikarisub"));
        QCOMPARE(action.arguments, QStringList{QStringLiteral("--hikari-updated-from=0.1.0")});
        const QString file = root + QStringLiteral("/cache/rpm/0.2.0/") + name;
        QCOMPARE(QString::fromUtf8(read(root + QStringLiteral("/args"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                 (QStringList{QStringLiteral("/opt/hikarisub/libexec/hikarisub-update-helper"), QStringLiteral("install"),
                              file, QStringLiteral("0.2.0")}));
        QVERIFY(!read(root + QStringLiteral("/args")).contains(sha256(bytes).toLatin1()));
        auto [dismissed, again, busy2] = run(126, true);
        QCOMPARE(again.program, root + QStringLiteral("/old-hikarisub"));
        QVERIFY(QFileInfo::exists(pendingOf(root, PackageKind::Rpm)));
        auto [later, none, busy3] = run(126, false);
        QVERIFY(none.program.isEmpty());
        auto [offline, after6, busy6] = run(6, false);
        QVERIFY(QFileInfo::exists(pendingOf(root, PackageKind::Rpm)));
        {
            UpdateInstaller told(environment(PackageKind::Rpm, root), QStringLiteral("0.1.0"));
            QVERIFY(told.start().contains(QStringLiteral("GitHub")));
        }
        auto [refused, after, busy4] = run(3, false);
        QVERIFY(!QFileInfo::exists(pendingOf(root, PackageKind::Rpm)));
        UpdateInstaller next(environment(PackageKind::Rpm, root), QStringLiteral("0.1.0"));
        QVERIFY(next.start().contains(QStringLiteral("0.2.0")));
        QCOMPARE(next.state(), QStringLiteral("idle"));
    }

    void anAppImageReplacesItselfKeepingTheOld()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/apps/My HikariSub.AppImage");
        const QByteArray old = "#!/bin/sh\nexit 0\n";
        QVERIFY(write(image, old, true));
        const QByteArray fresh = "#!/bin/sh\nfor a; do case \"$a\" in --hikari-health=*) : > \"${a#--hikari-health=}\";; esac; done\n"
                                 "printf '%s\\n' \"$@\" > \"" + root.toUtf8() + "/args\"\nsleep 1\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        QVERIFY(installer.canInstall());
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        const QString staged = root + QStringLiteral("/apps/.My HikariSub.AppImage.0.2.0.update");
        QCOMPARE(installer.readyFile(), staged);
        QVERIFY(QFileInfo(staged).isExecutable());
        installer.installAtExit();
        QCOMPARE(read(image), old);
        QVERIFY(QFileInfo::exists(staged));
        QCOMPARE(read(atStartOf(root, image)), QByteArray("0.2.0"));
        UpdateInstaller atStart(env, QStringLiteral("0.1.0"));
        QVERIFY(atStart.installAppImageAtStart({QStringLiteral("/subs/ep 1.ass")}));
        QCOMPARE(read(image), fresh);
        QVERIFY(QFileInfo(image).isExecutable());
        QCOMPARE(read(image + QStringLiteral(".old")), old);
        QVERIFY(!QFileInfo::exists(staged));
        QVERIFY(!QFileInfo::exists(atStartOf(root, image)));
        QVERIFY(!QFileInfo::exists(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::AppImage, image)));
        const QList<QByteArray> args = read(root + QStringLiteral("/args")).split('\n');
        QVERIFY(args.contains("--hikari-updated-from=0.1.0"));
        QVERIFY(args.contains("/subs/ep 1.ass"));
        UpdateInstaller again(env, QStringLiteral("0.1.0"));
        QVERIFY(!again.installAppImageAtStart({}));
        UpdateInstaller started(env, QStringLiteral("0.2.0"));
        started.start();
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
        QVERIFY(!QFileInfo::exists(pendingOf(root, PackageKind::AppImage, image)));
    }

    void aRestartThatCannotReplaceTheImageStartsTheOldOne()
    {
#ifndef _WIN32
        if (::geteuid() == 0)
            QSKIP("root writes to a read-only folder");
#endif
        const QString root = folder();
        const QString apps = root + QStringLiteral("/apps");
        const QString image = apps + QStringLiteral("/HikariSub.AppImage");
        const QByteArray old = "#!/bin/sh\nexit 0\n";
        QVERIFY(write(image, old, true));
        const QByteArray fresh = "#!/bin/sh\nexit 0\n# 0.2.0\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        QVERIFY(QFile::setPermissions(apps, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        installer.restartNow();
        const auto action = installer.installAtExit();
        QVERIFY(QFile::setPermissions(apps, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QCOMPARE(action.program, env.executable);
        QVERIFY(action.arguments.isEmpty());
        QCOMPARE(read(image), old);
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
        QVERIFY(read(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::AppImage, image)).contains("Cannot keep a copy"));
    }

    void anAppImageThatCannotStartIsPutBackAtTheNextStart()
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
        installer.installAtExit();
        QCOMPARE(read(image), old);
        UpdateInstaller atStart(env, QStringLiteral("0.1.0"));
        QElapsedTimer took;
        took.start();
        QVERIFY(atStart.installAppImageAtStart({}));
        QVERIFY(took.elapsed() < 9000);
        QCOMPARE(read(image), old);
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
        UpdateInstaller next(env, QStringLiteral("0.1.0"));
        QVERIFY(next.start().contains(QStringLiteral("0.2.0")));
        QCOMPARE(next.state(), QStringLiteral("idle"));
    }

    void theStartInstallsOnlyWhatTheQuitLeft()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        const QByteArray old = "#!/bin/sh\nexit 0\n";
        QVERIFY(write(image, old, true));
        const QByteArray fresh = "#!/bin/sh\nexit 0\n# new\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        UpdateInstaller noQuit(env, QStringLiteral("0.1.0"));
        QVERIFY(!noQuit.installAppImageAtStart({}));
        QVERIFY(write(atStartOf(root, image), "0.3.0"));
        UpdateInstaller other(env, QStringLiteral("0.1.0"));
        QVERIFY(!other.installAppImageAtStart({}));
        QVERIFY(!QFileInfo::exists(atStartOf(root, image)));
        installer.installAtExit();
        write(installer.readyFile(), "#!/bin/sh\nexit 0\n# changed\n", true);
        UpdateInstaller changed(env, QStringLiteral("0.1.0"));
        QVERIFY(!changed.installAppImageAtStart({}));
        QCOMPARE(read(image), old);
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
        QVERIFY(write(atStartOf(root, image), "0.2.0"));
        UpdateInstaller deb(environment(PackageKind::Deb, root), QStringLiteral("0.1.0"));
        QVERIFY(!deb.installAppImageAtStart({}));
    }

    void anAppImageCopyLeavesAnothersUpdateAlone()
    {
        const QString root = folder();
        const QString mine = root + QStringLiteral("/apps/HikariSub.AppImage");
        const QString other = root + QStringLiteral("/other/HikariSub.AppImage");
        const QString beside = root + QStringLiteral("/apps/HikariSub-test.AppImage");
        const QByteArray old = "#!/bin/sh\nexit 0\n";
        QVERIFY(write(mine, old, true));
        QVERIFY(write(other, old, true));
        QVERIFY(write(beside, old, true));
        const QByteArray fresh = "#!/bin/sh\nfor a; do case \"$a\" in --hikari-health=*) : > \"${a#--hikari-health=}\";; esac; done\nsleep 1\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = mine;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        installer.installAtExit();
        const QString staged = stagedOf(mine, QStringLiteral("0.2.0"));
        QCOMPARE(staged, root + QStringLiteral("/apps/.HikariSub.AppImage.0.2.0.update"));
        for (const QString &image : {other, beside}) {
            auto otherEnv = env;
            otherEnv.package.appImage = image;
            UpdateInstaller otherStart(otherEnv, QStringLiteral("0.1.0"));
            QVERIFY(!otherStart.installAppImageAtStart({}));
            otherStart.start();
            QCOMPARE(otherStart.state(), QStringLiteral("idle"));
            QCOMPARE(read(image), old);
        }
        QVERIFY(QFileInfo::exists(staged));
        QVERIFY(QFileInfo::exists(pendingOf(root, PackageKind::AppImage, mine)));
        QVERIFY(QFileInfo::exists(atStartOf(root, mine)));
        auto besideEnv = env;
        besideEnv.package.appImage = beside;
        UpdateInstaller besideCopy(besideEnv, QStringLiteral("0.1.0"));
        QVERIFY(download(besideCopy, QStringLiteral("0.2.0"), package(name, fresh), serve(root, name, fresh)));
        QCOMPARE(besideCopy.readyFile(), stagedOf(beside, QStringLiteral("0.2.0")));
        QVERIFY(QFileInfo::exists(pendingOf(root, PackageKind::AppImage, beside)));
        UpdateInstaller atStart(env, QStringLiteral("0.1.0"));
        QVERIFY(atStart.installAppImageAtStart({}));
        QCOMPARE(read(mine), fresh);
        QCOMPARE(read(other), old);
        QCOMPARE(read(beside), old);
        QVERIFY(QFileInfo::exists(stagedOf(beside, QStringLiteral("0.2.0"))));
        QVERIFY(QFileInfo::exists(pendingOf(root, PackageKind::AppImage, beside)));
    }

    void theWatchdogStopsAHungUpdateBeforePuttingTheOldOneBack()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        const QByteArray old = "#!/bin/sh\nexit 0\n";
        QVERIFY(write(image, old, true));
        const QByteArray hung = "#!/bin/sh\ntrap '' TERM\necho $$ > \"" + root.toUtf8() + "/pid\"\n"
                                "sleep 60 &\necho $! > \"" + root.toUtf8() + "/child\"\nwait\n";
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        env.healthTimeoutMs = 1500;
        UpdateInstaller installer(env, QStringLiteral("0.1.0"));
        const QString name = QStringLiteral("HikariSub-0.2.0-linux-x86_64.AppImage");
        QVERIFY(download(installer, QStringLiteral("0.2.0"), package(name, hung), serve(root, name, hung)));
        installer.restartNow();
        installer.installAtExit();
        QCOMPARE(read(image), old);
        for (const char *file : {"/pid", "/child"}) {
            const qint64 pid = read(root + QLatin1String(file)).trimmed().toLongLong();
            QVERIFY2(pid > 0, file);
            QVERIFY2(!running(pid), file);
        }
        UpdateInstaller next(env, QStringLiteral("0.1.0"));
        QVERIFY(next.start().contains(QStringLiteral("0.2.0")));
    }

    void theWatchdogKeepsAnUpdateThatStarts()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        QVERIFY(write(image, "#!/bin/sh\nexit 0\n", true));
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
        QVERIFY(!QFileInfo::exists(root + copyFile(root, QStringLiteral("health-%1-0.2.0"), PackageKind::AppImage, image)));
        QVERIFY(!QFileInfo::exists(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::AppImage, image)));
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
        QVERIFY(took.elapsed() < 9000);
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

    void aHandedOffUpdateThatDidNotInstallIsTold()
    {
        const QString root = folder();
        auto env = environment(PackageKind::NsisMachine, root);
        const QString marker = copyFile(root, QStringLiteral("installing-%1"), PackageKind::NsisMachine);
        QVERIFY(write(marker, "0.2.0"));
        UpdateInstaller failed(env, QStringLiteral("0.1.0"));
        const QString message = failed.start();
        QVERIFY(message.contains(QStringLiteral("0.2.0")));
        QVERIFY(message.contains(QStringLiteral("did not finish")));
        QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(write(marker, "0.2.0"));
        UpdateInstaller installed(env, QStringLiteral("0.2.0"));
        QCOMPARE(installed.start(), QString());
        QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(write(marker, "0.2.0"));
        QVERIFY(write(copyFile(root, QStringLiteral("last-result-%1.json"), PackageKind::NsisMachine),
                      R"({"version": "0.2.0", "ok": false, "code": 3, "message": "HikariSub is still running."})"));
        UpdateInstaller reported(env, QStringLiteral("0.1.0"));
        QCOMPARE(reported.start(), QStringLiteral("HikariSub is still running."));
        QVERIFY(write(marker, "../0.2.0"));
        UpdateInstaller junk(env, QStringLiteral("0.1.0"));
        QCOMPARE(junk.start(), QString());
    }

    void theOldImageStaysUntilTheReportIsWritten()
    {
        const QString root = folder();
        const QString image = root + QStringLiteral("/HikariSub.AppImage");
        QVERIFY(write(image, "new", true));
        QVERIFY(write(image + QStringLiteral(".old"), "old", true));
        auto env = environment(PackageKind::AppImage, root);
        env.package.appImage = image;
        QVERIFY(write(pendingOf(root, PackageKind::AppImage, image),
                      R"({"schema": 1, "version": "0.2.0", "kind": "appimage", "file": ")" + root.toUtf8() +
                          R"(/.HikariSub.AppImage.0.2.0.update", "sha256": "", "size": 3})"));
        UpdateInstaller watched(env, QStringLiteral("0.2.0"));
        watched.expectHealthReport();
        watched.start();
        QVERIFY(!QFileInfo::exists(pendingOf(root, PackageKind::AppImage, image)));
        QVERIFY(QFileInfo::exists(image + QStringLiteral(".old")));
#ifndef _WIN32
        if (::geteuid() != 0) {
            QVERIFY(QFile::setPermissions(env.cacheDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
            watched.reportHealthy(env.cacheDir + QStringLiteral("/health-0.2.0"));
            QVERIFY(QFile::setPermissions(env.cacheDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
            QVERIFY(!QFileInfo::exists(env.cacheDir + QStringLiteral("/health-0.2.0")));
            QVERIFY(QFileInfo::exists(image + QStringLiteral(".old")));
        }
#endif
        watched.reportHealthy(env.cacheDir + QStringLiteral("/health-0.2.0"));
        QVERIFY(!QFileInfo::exists(image + QStringLiteral(".old")));
    }

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
        QVERIFY(installer.installAtExit().program.isEmpty());
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/ran")));
        QVERIFY(!QFileInfo::exists(pendingOf(root, PackageKind::Deb)));
        QCOMPARE(installer.state(), QStringLiteral("idle"));
        {
            UpdateInstaller next(env, QStringLiteral("0.1.0"));
            QVERIFY(next.start().contains(QStringLiteral("changed or went missing")));
        }
        UpdateInstaller restarting(env, QStringLiteral("0.1.0"));
        QVERIFY(download(restarting, QStringLiteral("0.2.0"), package(name, bytes), serve(root, name, bytes)));
        QFile::remove(restarting.readyFile());
        restarting.restartNow();
        const auto action = restarting.installAtExit();
        QCOMPARE(action.program, env.executable);
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/ran")));
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
