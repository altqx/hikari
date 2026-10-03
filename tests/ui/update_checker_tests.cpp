// P8: the update check against a fixture release feed (a local file, so
// nothing leaves the machine), as legacy UpdateChecker reads GitHub's list.

#include "hikari/app/update_checker.h"

#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::UpdateChecker;

class UpdateCheckerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    QUrl feed(const QByteArray &json)
    {
        const QString path = dir.filePath(QStringLiteral("releases.json"));
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(json);
        return QUrl::fromLocalFile(path);
    }
    static const QByteArray kFeed;

private slots:
    void reportsANewerStableReleaseOrAPrerelease()
    {
        UpdateChecker checker({}, feed(kFeed), QStringLiteral("0.1.0"));
        QSignalSpy spy(&checker, &UpdateChecker::finished);
        checker.checkNow();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("available"));
        const auto release = spy.at(0).at(1).toMap();
        QCOMPARE(release.value(QStringLiteral("tag")).toString(), QStringLiteral("v0.1.2"));
        QCOMPARE(release.value(QStringLiteral("name")).toString(), QStringLiteral("v0.1.2")); // no name: the tag
        QCOMPARE(release.value(QStringLiteral("notes")).toString(), QStringLiteral("Fixes"));
        QCOMPARE(release.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.invalid/r/0.1.2"));
        checker.setStableOnly(false);
        checker.checkNow();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(1).at(1).toMap().value(QStringLiteral("tag")).toString(), QStringLiteral("v0.2.0-rc.1"));
    }

    void upToDateAndFailuresOnlyWhenAsked()
    {
        UpdateChecker current({}, feed(kFeed), QStringLiteral("0.1.2"));
        QSignalSpy spy(&current, &UpdateChecker::finished);
        current.checkNow();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("current"));
        UpdateChecker offline({}, QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.json"))), QStringLiteral("0.1.0"));
        QSignalSpy failed(&offline, &UpdateChecker::finished);
        offline.checkNow();
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("failed"));
        UpdateChecker garbage({}, feed("{\"message\": \"rate limited\"}"), QStringLiteral("0.1.0"));
        QSignalSpy none(&garbage, &UpdateChecker::finished);
        garbage.checkNow();
        QVERIFY(none.wait(5000));
        QCOMPARE(none.at(0).at(0).toString(), QStringLiteral("current")); // legacy: no array, nothing newer
    }

    void automaticCheckFollowsTheOptionAndTheSchedule()
    {
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        UpdateChecker checker(ini, QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.json"))), QStringLiteral("0.1.0"));
        QVERIFY(!checker.autoCheck()); // legacy default: off
        QVERIFY(checker.stableOnly());  // legacy default: stable only
        QVERIFY(!checker.checkOnStartup());
        checker.setAutoCheck(true);
        QSignalSpy spy(&checker, &UpdateChecker::finished);
        const qint64 before = QDateTime::currentSecsSinceEpoch();
        QVERIFY(checker.checkOnStartup());
        QTRY_VERIFY(!checker.checking());
        QCOMPARE(spy.count(), 0); // a failed automatic check is silent
        QVERIFY(checker.nextCheck() >= before + 24 * 60 * 60); // backs off a day
        QVERIFY(!checker.checkOnStartup());
        checker.remindInAWeek();
        QVERIFY(checker.nextCheck() >= before + 7 * 24 * 60 * 60);
        UpdateChecker again(ini, {}, QStringLiteral("0.1.0"));
        QVERIFY(again.autoCheck());
        QCOMPARE(again.nextCheck(), checker.nextCheck());
        // A due automatic check reports a newer release.
        UpdateChecker due(ini, feed(kFeed), QStringLiteral("0.1.0"));
        due.setNextCheck(0);
        QSignalSpy found(&due, &UpdateChecker::finished);
        QVERIFY(due.checkOnStartup());
        QVERIFY(found.wait(5000));
        QCOMPARE(found.at(0).at(0).toString(), QStringLiteral("available"));
        QCOMPARE(found.at(0).at(2).toBool(), false);
    }
};

const QByteArray UpdateCheckerTest::kFeed = R"([
  {"tag_name": "v0.3.0", "name": "Draft", "draft": true, "prerelease": false, "html_url": "d", "body": ""},
  {"tag_name": "v0.2.0-rc.1", "name": "RC", "draft": false, "prerelease": false, "html_url": "rc", "body": ""},
  {"tag_name": "v0.1.5", "name": "Flagged", "draft": false, "prerelease": true, "html_url": "f", "body": ""},
  {"tag_name": "v0.1.2", "name": null, "draft": false, "prerelease": false,
   "html_url": "https://example.invalid/r/0.1.2", "body": "Fixes"},
  {"tag_name": "v0.1.1", "name": "Old", "draft": false, "prerelease": false, "html_url": "o", "body": ""}
])";

QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "update_checker_tests.moc"
