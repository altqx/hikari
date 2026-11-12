#include "hikari/app/automation_reset.h"
#include "hikari/app/script_packages.h"
#include "hikari/app/user_folder.h"
#include "hikari/application/hotkeys.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace hikari;
using Outcome = app::AutomationReset::Outcome;

namespace {

const QDate kToday(2026, 11, 12);

bool write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size();
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray sha256(const QByteArray &content)
{
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
}

QJsonObject record(const char *type, const char *feed = "https://example.invalid/feed.json")
{
    return {{QStringLiteral("recordType"), QLatin1String(type)},
            {QStringLiteral("feed"), feed ? QJsonValue(QLatin1String(feed)) : QJsonValue()},
            {QStringLiteral("name"), QStringLiteral("A script")},
            {QStringLiteral("version"), QStringLiteral("0.0.1")}};
}

bool canDenyWrites()
{
#ifdef _WIN32
    return false;
#else
    return geteuid() != 0;
#endif
}

}

class AutomationResetTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir;
    QString root() const { return dir->filePath(QStringLiteral("user")); }
    QString at(const QString &relative) const { return root() + QLatin1Char('/') + relative; }
    QString backup(const QString &suffix = {}) const { return at(QStringLiteral("automation-backup-2026-11-12") + suffix); }

    void fillUserFolder()
    {
        QVERIFY(write(at(QStringLiteral("automation/autoload/mine.lua")), "-- mine"));
        QVERIFY(write(at(QStringLiteral("automation/include/lib.lua")), "-- lib"));
        QVERIFY(write(at(QStringLiteral("automation/notes.txt")), "notes"));
        QVERIFY(write(at(QStringLiteral("config/l0.DependencyControl.json")), "{}"));
        QVERIFY(write(at(QStringLiteral("settings.toml")), "[automation]\n"));
    }

private slots:
    void init()
    {
        dir = std::make_unique<QTemporaryDir>();
        QVERIFY(dir->isValid());
        QVERIFY(QDir().mkpath(root()));
    }

    void theGateIsTheTemplaterRelease()
    {
        QVERIFY(!app::automationResetDue(false, QStringLiteral("0.1.0-beta.4")));
        QVERIFY(!app::automationResetDue(true, QStringLiteral("0.1.0-beta.3")));
        QVERIFY(app::automationResetDue(true, QString::fromLatin1(app::kAutomationResetVersion)));
        QVERIFY(app::automationResetDue(true, QStringLiteral("0.1.0-beta.10")));
        QVERIFY(app::automationResetDue(true, QStringLiteral("0.1.0")));
        QVERIFY(!app::automationResetDue(true, QStringLiteral("not a version")));
    }

    void theFirstRunMovesEverythingAndRecordsIt()
    {
        fillUserFolder();
        const auto report = app::resetAutomationFolder(root(), kToday);
        QCOMPARE(report.outcome, Outcome::Moved);
        QCOMPARE(report.backup, backup());
        QCOMPARE(report.moved, (QStringList{QStringLiteral("automation/autoload"), QStringLiteral("automation/include"),
                                            QStringLiteral("automation/notes.txt"), QStringLiteral("config")}));
        QCOMPARE(read(backup(QStringLiteral("/automation/autoload/mine.lua"))), QByteArray("-- mine"));
        QCOMPARE(read(backup(QStringLiteral("/automation/include/lib.lua"))), QByteArray("-- lib"));
        QCOMPARE(read(backup(QStringLiteral("/automation/notes.txt"))), QByteArray("notes"));
        QCOMPARE(read(backup(QStringLiteral("/config/l0.DependencyControl.json"))), QByteArray("{}"));
        QVERIFY(QFileInfo(at(QStringLiteral("automation"))).isDir());
        QVERIFY(QDir(at(QStringLiteral("automation"))).isEmpty());
        QVERIFY(!QFileInfo::exists(at(QStringLiteral("config"))));
        QCOMPARE(read(at(QStringLiteral("settings.toml"))), QByteArray("[automation]\n"));
        QVERIFY(QFileInfo::exists(at(QStringLiteral(".automation-reset"))));
    }

    void theSecondRunDoesNothing()
    {
        fillUserFolder();
        QCOMPARE(app::resetAutomationFolder(root(), kToday).outcome, Outcome::Moved);
        QVERIFY(write(at(QStringLiteral("automation/autoload/new.lua")), "-- new"));
        const auto again = app::resetAutomationFolder(root(), kToday.addDays(1));
        QCOMPARE(again.outcome, Outcome::None);
        QVERIFY(QFileInfo::exists(at(QStringLiteral("automation/autoload/new.lua"))));
        QVERIFY(!QFileInfo::exists(at(QStringLiteral("automation-backup-2026-11-13"))));
    }

    void anEmptyOrMissingFolderIsLeftAndRecorded()
    {
        QCOMPARE(app::resetAutomationFolder(root(), kToday).outcome, Outcome::None);
        QVERIFY(QFileInfo::exists(at(QStringLiteral(".automation-reset"))));
        QVERIFY(QFile::remove(at(QStringLiteral(".automation-reset"))));
        QVERIFY(QDir().mkpath(at(QStringLiteral("automation"))));
        QVERIFY(write(at(QStringLiteral("config/l0.DependencyControl.json")), "{}"));
        QCOMPARE(app::resetAutomationFolder(root(), kToday).outcome, Outcome::None);
        QVERIFY(QFileInfo::exists(at(QStringLiteral("config/l0.DependencyControl.json"))));
        QVERIFY(!QFileInfo::exists(backup()));
    }

    void aTakenBackupNameGetsANumber()
    {
        fillUserFolder();
        QVERIFY(QDir().mkpath(backup()));
        QVERIFY(QDir().mkpath(backup(QStringLiteral("-2"))));
        const auto report = app::resetAutomationFolder(root(), kToday);
        QCOMPARE(report.outcome, Outcome::Moved);
        QCOMPARE(report.backup, backup(QStringLiteral("-3")));
        QVERIFY(QDir(backup()).isEmpty());
    }

    void aFailedMoveLeavesEverythingAndRetries()
    {
        if (!canDenyWrites())
            QSKIP("needs a folder this user can't move");
        fillUserFolder();
        const QString automation = at(QStringLiteral("automation"));
        const auto permissions = QFile::permissions(automation);
        QVERIFY(QFile::setPermissions(automation, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const auto failed = app::resetAutomationFolder(root(), kToday);
        QVERIFY(QFile::setPermissions(automation, permissions));
        QCOMPARE(failed.outcome, Outcome::Failed);
        QVERIFY(!failed.reason.isEmpty());
        QVERIFY(failed.moved.isEmpty());
        QVERIFY(!QFileInfo::exists(failed.backup));
        QCOMPARE(read(at(QStringLiteral("automation/autoload/mine.lua"))), QByteArray("-- mine"));
        QVERIFY(QFileInfo::exists(at(QStringLiteral("config/l0.DependencyControl.json"))));
        QVERIFY(!QFileInfo::exists(at(QStringLiteral(".automation-reset"))));

        QCOMPARE(app::resetAutomationFolder(root(), kToday).outcome, Outcome::Moved);
    }

    void aFailedConfigMovePutsTheAutomationFolderBack()
    {
        if (!canDenyWrites())
            QSKIP("needs a folder this user can't move");
        fillUserFolder();
        const QString config = at(QStringLiteral("config"));
        const auto permissions = QFile::permissions(config);
        QVERIFY(QFile::setPermissions(config, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const auto failed = app::resetAutomationFolder(root(), kToday);
        QVERIFY(QFile::setPermissions(config, permissions));
        QCOMPARE(failed.outcome, Outcome::Failed);
        QCOMPARE(read(at(QStringLiteral("automation/autoload/mine.lua"))), QByteArray("-- mine"));
        QVERIFY(QFileInfo::exists(at(QStringLiteral("config/l0.DependencyControl.json"))));
        QVERIFY(!QFileInfo::exists(failed.backup));
        QVERIFY(!QFileInfo::exists(at(QStringLiteral(".automation-reset"))));
    }

    void onlySeededFilesAreDeletedWithoutABackup()
    {
        const QByteArray templater = "-- kara-templater 1";
        const QByteArray karaskel = "-- karaskel 1";
        QVERIFY(write(at(QStringLiteral("automation/autoload/kara-templater.lua")), templater));
        QVERIFY(write(at(QStringLiteral("automation/include/karaskel.lua")), karaskel));
        QVERIFY(write(at(QStringLiteral(".bundled-files")),
                      "0 *\n" + sha256(templater) + " automation/autoload/kara-templater.lua\n" + sha256(karaskel) +
                          " automation/include/karaskel.lua\n"));
        QVERIFY(write(at(QStringLiteral("config/l0.DependencyControl.json")), "{}"));
        const auto report = app::resetAutomationFolder(root(), kToday);
        QCOMPARE(report.outcome, Outcome::Cleared);
        QVERIFY(report.backup.isEmpty());
        QVERIFY(!QFileInfo::exists(backup()));
        QVERIFY(QDir(at(QStringLiteral("automation"))).isEmpty());
        QVERIFY(QFileInfo::exists(at(QStringLiteral("config/l0.DependencyControl.json"))));
        QVERIFY(QFileInfo::exists(at(QStringLiteral(".automation-reset"))));

        const QString program = dir->filePath(QStringLiteral("program"));
        const QByteArray newKaraskel = "-- karaskel 2";
        QVERIFY(write(program + QStringLiteral("/Automation/automation/Include/karaskel.lua"), newKaraskel));
        QVERIFY(write(program + QLatin1Char('/') + app::UserFolder::manifestName(),
                      sha256(newKaraskel) + " Automation/automation/Include/karaskel.lua\n"));
        app::UserFolder(root(), dir->filePath(QStringLiteral("cache"))).seed(program);
        QCOMPARE(read(at(QStringLiteral("automation/include/karaskel.lua"))), newKaraskel);
        QVERIFY(!QFileInfo::exists(at(QStringLiteral("automation/autoload/kara-templater.lua"))));
    }

    void aChangedSeededFileMovesTheWholeFolder()
    {
        const QByteArray karaskel = "-- karaskel 1";
        QVERIFY(write(at(QStringLiteral("automation/include/karaskel.lua")), karaskel + " edited"));
        QVERIFY(write(at(QStringLiteral("automation/autoload/kara-templater.lua")), "-- t"));
        QVERIFY(write(at(QStringLiteral(".bundled-files")),
                      "0 *\n" + sha256(karaskel) + " automation/include/karaskel.lua\n" + sha256("-- t") +
                          " automation/autoload/kara-templater.lua\n"));
        const auto report = app::resetAutomationFolder(root(), kToday);
        QCOMPARE(report.outcome, Outcome::Moved);
        QCOMPARE(read(backup(QStringLiteral("/automation/include/karaskel.lua"))), karaskel + " edited");
        QCOMPARE(read(backup(QStringLiteral("/automation/autoload/kara-templater.lua"))), QByteArray("-- t"));
    }

    void thePlanQueuesManagedScriptsOnly()
    {
        const QString file = dir->filePath(QStringLiteral("l0.DependencyControl.json"));
        QJsonObject managed = record("managed");
        managed.insert(QStringLiteral("name"), QStringLiteral("Git Signs"));
        const QJsonObject macros{
            {QStringLiteral("arch.GitSigns"), managed},
            {QStringLiteral("test.Unmanaged"), record("unmanaged", nullptr)},
            {QStringLiteral("test.NoFeed"), record("managed", nullptr)},
            {QStringLiteral("a-mo.Aegisub-Motion"), record("managed")},
            {QStringLiteral("arch.Line2Fbf"), record("managed")},
        };
        const QJsonObject modules{{QStringLiteral("l0.Functional"), record("managed")}};
        QVERIFY(write(file, QJsonDocument(QJsonObject{{QStringLiteral("macros"), macros},
                                                      {QStringLiteral("modules"), modules}})
                                .toJson()));
        const auto plan = app::reinstallPlan(file, {QStringLiteral("a-mo.Aegisub-Motion")});
        QCOMPARE(plan.queued.size(), std::size_t(1));
        QCOMPARE(plan.queued[0].nameSpace, QStringLiteral("arch.GitSigns"));
        QCOMPARE(plan.queued[0].name, QStringLiteral("Git Signs"));
        QCOMPARE(plan.queued[0].feed, QStringLiteral("https://example.invalid/feed.json"));
        QCOMPARE(plan.builtIn.size(), std::size_t(1));
        QCOMPARE(QLatin1String(plan.builtIn[0].nameSpace), QLatin1String("arch.Line2Fbf"));
    }

    void aMissingOrCorruptConfigPlansNothing()
    {
        const QString file = dir->filePath(QStringLiteral("l0.DependencyControl.json"));
        QVERIFY(app::reinstallPlan(file, {}).queued.empty());
        QVERIFY(write(file, "{ not json"));
        const auto plan = app::reinstallPlan(file, {});
        QVERIFY(plan.queued.empty() && plan.builtIn.empty());
    }

    void eachBuiltInScriptNamesACommand()
    {
        QVERIFY(!app::nativeScripts().empty());
        for (const auto &native : app::nativeScripts()) {
            QVERIFY2(application::hotkeyIdOf(native.command) != 0, native.command);
            QVERIFY2(std::ranges::none_of(app::recommendedScripts(),
                                          [&](const auto &s) { return s.nameSpace == native.nameSpace; }),
                     native.nameSpace);
        }
    }
};

QTEST_MAIN(AutomationResetTests)
#include "automation_reset_tests.moc"
