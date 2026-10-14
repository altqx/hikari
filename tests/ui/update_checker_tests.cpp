// P8: the update check against a fixture release feed (local files, so
// nothing leaves the machine), as legacy UpdateChecker reads GitHub's list.
// ADR 0019: the manifests, offer_to, the offer by package kind and the
// automatic download.

#include "hikari/app/update_checker.h"
#include "desktop_guard.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::PackageInfo;
using hikari::app::UpdateChecker;
using hikari::app::UpdateEnvironment;
using hikari::app::UpdateFeed;
using hikari::application::PackageKind;

namespace {

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

} // namespace

// Some tests use Linux's package kinds (deb, rpm, AppImage, AUR); on
// Windows the same choice is UpdateOffer.ByPackageKind's, with NSIS.
#ifdef Q_OS_WIN
#define linuxKindsOnly() QSKIP("Linux package kinds; Windows' choice is tested by UpdateOffer.ByPackageKind")
#else
#define linuxKindsOnly() (void)0
#endif

class UpdateCheckerTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    // A feed in its own folder: releases.json, download/v<V>/<asset>.
    UpdateFeed feed(const QByteArray &json, const QString &name = QStringLiteral("feed"))
    {
        const QString base = dir.filePath(name);
        write(base + QStringLiteral("/releases.json"), json);
        return UpdateFeed::local(QUrl::fromLocalFile(base + QLatin1Char('/')));
    }
    static const QByteArray kFeed;

    // A release of `version` with a deb, an AppImage and, when given, a manifest.
    struct Published {
        QJsonObject release;
        QByteArray deb, appImage;
    };
    Published publish(const QString &feedDir, const QString &version, const QJsonValue &offerTo = QJsonArray{QStringLiteral("qt")},
                      bool withManifest = true, const QString &minGlibc = QStringLiteral("2.34"))
    {
        Published p;
        p.deb = "deb package " + version.toUtf8();
        p.appImage = "appimage " + version.toUtf8();
        const QString deb = QStringLiteral("hikarisub-%1-linux-amd64.deb").arg(version);
        const QString appImage = QStringLiteral("HikariSub-%1-linux-x86_64.AppImage").arg(version);
        const QString root = dir.filePath(feedDir) + QStringLiteral("/download/v") + version + QLatin1Char('/');
        write(root + deb, p.deb);
        write(root + appImage, p.appImage);
        QJsonArray assets{QJsonObject{{"name", deb}, {"size", qint64(p.deb.size())}, {"digest", "sha256:" + sha256(p.deb)}},
                          QJsonObject{{"name", appImage}, {"size", qint64(p.appImage.size())}, {"digest", "sha256:" + sha256(p.appImage)}}};
        if (withManifest) {
            const QJsonObject manifest{
                {"schema", 1},
                {"product", "HikariSub"},
                {"version", version},
                {"codebase", "qt"},
                {"offer_to", offerTo},
                {"packages",
                 QJsonObject{{"deb", QJsonObject{{"name", deb}, {"size", qint64(p.deb.size())}, {"sha256", sha256(p.deb)}, {"min_glibc", minGlibc}}},
                             {"appimage", QJsonObject{{"name", appImage}, {"size", qint64(p.appImage.size())}, {"sha256", sha256(p.appImage)}}}}}};
            const QByteArray bytes = QJsonDocument(manifest).toJson();
            write(root + QStringLiteral("hikarisub-update.json"), bytes);
            assets.append(QJsonObject{{"name", "hikarisub-update.json"}, {"size", qint64(bytes.size())}, {"digest", ""}});
        }
        p.release = QJsonObject{{"tag_name", "v" + version}, {"name", "HikariSub " + version}, {"draft", false},
                                {"prerelease", version.contains(QLatin1Char('-'))}, {"body", "Notes " + version},
                                {"html_url", "https://example.invalid/not-used"}, {"assets", assets}};
        return p;
    }

    UpdateEnvironment environment(PackageKind kind, const QString &cache)
    {
        UpdateEnvironment env;
        env.package = PackageInfo{kind, {}};
        env.cacheDir = cache;
        env.glibcVersion = QStringLiteral("2.39");
        env.pkexec = QStringLiteral("/bin/false"); // never run by these tests
        return env;
    }

    static QVariantMap checkedOffer(UpdateChecker &checker)
    {
        QSignalSpy spy(&checker, &UpdateChecker::finished);
        checker.checkNow();
        if (!spy.wait(5000))
            return {};
        return spy.at(0).at(1).toMap();
    }

private slots:
    void reportsANewerStableReleaseOrAPrerelease()
    {
        const UpdateFeed f = feed(kFeed);
        UpdateChecker checker({}, f, QStringLiteral("0.1.0"));
        QSignalSpy spy(&checker, &UpdateChecker::finished);
        checker.checkNow();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("available"));
        const auto release = spy.at(0).at(1).toMap();
        QCOMPARE(release.value(QStringLiteral("tag")).toString(), QStringLiteral("v0.1.2"));
        QCOMPARE(release.value(QStringLiteral("name")).toString(), QStringLiteral("v0.1.2")); // no name: the tag
        QCOMPARE(release.value(QStringLiteral("notes")).toString(), QStringLiteral("Fixes"));
        // The page is built from the tag, never taken from the reply.
        QCOMPARE(release.value(QStringLiteral("url")).toString(), f.pageBase + QStringLiteral("v0.1.2"));
        QCOMPARE(release.value(QStringLiteral("offer")).toString(), QStringLiteral("notify")); // portable
        checker.setStableOnly(false);
        checker.checkNow();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(1).at(1).toMap().value(QStringLiteral("tag")).toString(), QStringLiteral("v0.2.0-rc.1"));
        QVERIFY(spy.at(1).at(1).toMap().value(QStringLiteral("prerelease")).toBool());
        QCOMPARE(checker.versionLabel(), QStringLiteral("0.1.0 (portable)"));
        QVERIFY(!checker.installsUpdates());
    }

    void upToDateAndFailuresOnlyWhenAsked()
    {
        UpdateChecker current({}, feed(kFeed), QStringLiteral("0.1.2"));
        QSignalSpy spy(&current, &UpdateChecker::finished);
        current.checkNow();
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("current"));
        UpdateChecker offline({}, UpdateFeed::local(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/")))),
                              QStringLiteral("0.1.0"));
        QSignalSpy failed(&offline, &UpdateChecker::finished);
        offline.checkNow();
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("failed"));
        UpdateChecker garbage({}, feed("{\"message\": \"rate limited\"}", QStringLiteral("garbage")), QStringLiteral("0.1.0"));
        QSignalSpy none(&garbage, &UpdateChecker::finished);
        garbage.checkNow();
        QVERIFY(none.wait(5000));
        QCOMPARE(none.at(0).at(0).toString(), QStringLiteral("current")); // legacy: no array, nothing newer
    }

    void automaticCheckFollowsTheOptionAndTheSchedule()
    {
        const QString ini = dir.filePath(QStringLiteral("hikari.ini"));
        const UpdateFeed missing = UpdateFeed::local(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/"))));
        UpdateChecker checker(ini, missing, QStringLiteral("0.1.0"));
        QVERIFY(!checker.autoCheck()); // legacy default: off
        QVERIFY(checker.stableOnly());  // legacy default: stable only
        QVERIFY(checker.autoInstall()); // ADR 0019: on by default
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
        checker.setAutoInstall(false);
        UpdateChecker again(ini, {}, QStringLiteral("0.1.0"));
        QVERIFY(again.autoCheck());
        QVERIFY(!again.autoInstall());
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

    // The highest version is chosen, not the first listed; a release not
    // offered to this codebase or with a broken manifest is passed over.
    void manifestsChooseTheReleaseAndItsPackage()
    {
        linuxKindsOnly();
        const QString name = QStringLiteral("manifests");
        const auto legacyOnly = publish(name, QStringLiteral("0.4.0"), QJsonArray{QStringLiteral("legacy")});
        const auto broken = publish(name, QStringLiteral("0.3.0"));
        write(dir.filePath(name) + QStringLiteral("/download/v0.3.0/hikarisub-update.json"), "{\"schema\": 1, \"version\": ");
        const auto wanted = publish(name, QStringLiteral("0.2.0"));
        const auto older = publish(name, QStringLiteral("0.1.5"));
        const UpdateFeed f = feed(QJsonDocument(QJsonArray{older.release, legacyOnly.release, wanted.release, broken.release}).toJson(), name);
        const QString cache = dir.filePath(QStringLiteral("cache-manifests"));
        UpdateChecker checker({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Deb, cache));
        const auto offer = checkedOffer(checker);
        QCOMPARE(offer.value(QStringLiteral("tag")).toString(), QStringLiteral("v0.2.0"));
        QCOMPARE(offer.value(QStringLiteral("offer")).toString(), QStringLiteral("install"));
        QCOMPARE(checker.versionLabel(), QStringLiteral("0.1.0 (deb)"));
        QVERIFY(checker.installsUpdates());
        // "Download and install": the deb lands in the cache, verified.
        auto &installer = checker.installer();
        QSignalSpy ready(&installer, &hikari::app::UpdateInstaller::ready);
        checker.downloadOffer();
        QVERIFY(ready.wait(5000));
        QCOMPARE(installer.state(), QStringLiteral("ready"));
        QCOMPARE(installer.readyVersion(), QStringLiteral("0.2.0"));
        const QString file = cache + QStringLiteral("/deb/0.2.0/hikarisub-0.2.0-linux-amd64.deb");
        QCOMPARE(installer.readyFile(), file);
        QFile downloaded(file);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), wanted.deb);
        QVERIFY(QFileInfo::exists(cache + QStringLiteral("/pending-deb.json")));
    }

    // Without a manifest (every release before them): the assets' digests.
    void releasesWithoutManifestUseTheirDigests()
    {
        linuxKindsOnly();
        const QString name = QStringLiteral("fallback");
        const auto plain = publish(name, QStringLiteral("0.2.0"), {}, false);
        const UpdateFeed f = feed(QJsonDocument(QJsonArray{plain.release}).toJson(), name);
        UpdateChecker checker({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Deb, dir.filePath(QStringLiteral("cache-fallback"))));
        QCOMPARE(checkedOffer(checker).value(QStringLiteral("offer")).toString(), QStringLiteral("install"));
    }

    void theOfferFollowsThePackageKind()
    {
        linuxKindsOnly();
        const QString name = QStringLiteral("kinds");
        const auto r = publish(name, QStringLiteral("0.2.0"), QJsonArray{QStringLiteral("qt")}, true, QStringLiteral("2.40"));
        const UpdateFeed f = feed(QJsonDocument(QJsonArray{r.release}).toJson(), name);
        // The deb needs glibc 2.40: the system's 2.39 is not enough.
        UpdateChecker deb({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Deb, dir.filePath(QStringLiteral("cache-kinds"))));
        const auto requirement = checkedOffer(deb);
        QCOMPARE(requirement.value(QStringLiteral("offer")).toString(), QStringLiteral("requirement"));
        QCOMPARE(requirement.value(QStringLiteral("requirement")).toString(), QStringLiteral("2.40"));
        QVERIFY(!requirement.value(QStringLiteral("requirementIsOs")).toBool());
        // An rpm copy finds no rpm in this release: the notice only.
        UpdateChecker rpm({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Rpm, dir.filePath(QStringLiteral("cache-kinds"))));
        QCOMPARE(checkedOffer(rpm).value(QStringLiteral("offer")).toString(), QStringLiteral("notify"));
        // The AUR: its own notice, and nothing is downloaded, even by the
        // automatic check with "Install updates automatically" on.
        const QString aurCache = dir.filePath(QStringLiteral("cache-aur"));
        UpdateChecker aur({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Arch, aurCache));
        QVERIFY(!aur.installsUpdates());
        QCOMPARE(aur.versionLabel(), QStringLiteral("0.1.0 (AUR)"));
        QCOMPARE(checkedOffer(aur).value(QStringLiteral("offer")).toString(), QStringLiteral("aur"));
        aur.setAutoCheck(true);
        aur.setNextCheck(0);
        QSignalSpy automatic(&aur, &UpdateChecker::finished);
        QVERIFY(aur.checkOnStartup());
        QVERIFY(automatic.wait(5000));
        QCOMPARE(automatic.at(0).at(1).toMap().value(QStringLiteral("offer")).toString(), QStringLiteral("aur"));
        QCOMPARE(aur.installer().state(), QStringLiteral("idle"));
        QVERIFY(!QFileInfo::exists(aurCache));
    }

    // The automatic check downloads an installable update without asking
    // and says nothing until it is ready; with the option off it only tells.
    void automaticCheckDownloadsWhenInstallingIsOn()
    {
        linuxKindsOnly();
        const QString name = QStringLiteral("automatic");
        const auto r = publish(name, QStringLiteral("0.2.0"));
        const UpdateFeed f = feed(QJsonDocument(QJsonArray{r.release}).toJson(), name);
        const QString cache = dir.filePath(QStringLiteral("cache-automatic"));
        UpdateChecker checker({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Deb, cache));
        checker.setAutoCheck(true);
        checker.setNextCheck(0);
        QSignalSpy told(&checker, &UpdateChecker::finished);
        QSignalSpy ready(&checker.installer(), &hikari::app::UpdateInstaller::ready);
        QVERIFY(checker.checkOnStartup());
        QVERIFY(ready.wait(5000));
        QCOMPARE(told.count(), 0);
        QCOMPARE(checker.installer().state(), QStringLiteral("ready"));

        UpdateChecker off({}, f, QStringLiteral("0.1.0"), environment(PackageKind::Deb, dir.filePath(QStringLiteral("cache-off"))));
        off.setAutoInstall(false);
        off.setAutoCheck(true);
        off.setNextCheck(0);
        QSignalSpy offTold(&off, &UpdateChecker::finished);
        QVERIFY(off.checkOnStartup());
        QVERIFY(offTold.wait(5000));
        QCOMPARE(offTold.at(0).at(1).toMap().value(QStringLiteral("offer")).toString(), QStringLiteral("install"));
        QCOMPARE(off.installer().state(), QStringLiteral("idle"));
    }

    void releasePagesAreOnlyTheFeedsOwn()
    {
        const UpdateFeed github = UpdateFeed::github();
        QCOMPARE(github.releases.toString(), QStringLiteral("https://api.github.com/repos/altqx/hikari/releases?per_page=50"));
        QVERIFY(github.paginate);
        UpdateChecker checker({}, github, QStringLiteral("0.1.0"));
        QCOMPARE(checker.releasePageOf(QStringLiteral("0.2.0")), QStringLiteral("https://github.com/altqx/hikari/releases/tag/v0.2.0"));
        QCOMPARE(checker.releasePageOf(QStringLiteral("../x")), QString());
        // Only the feed's own page goes to the browser (38e: recorded; it
        // never reaches the desktop's browser).
        hikari::test::desktop::clear();
        checker.openReleasePage(QStringLiteral("https://example.invalid/altqx/hikari/releases/tag/v0.2.0"));
        checker.openReleasePage(QString());
        QVERIFY(hikari::test::desktop::opened().isEmpty());
        checker.openReleasePage(checker.releasePageOf(QStringLiteral("0.2.0")));
        QCOMPARE(hikari::test::desktop::opened(),
                 QList<QUrl>{QUrl(QStringLiteral("https://github.com/altqx/hikari/releases/tag/v0.2.0"))});
    }
};

const QByteArray UpdateCheckerTest::kFeed = R"([
  {"tag_name": "v0.3.0", "name": "Draft", "draft": true, "prerelease": false, "html_url": "d", "body": ""},
  {"tag_name": "v0.1.1", "name": "Old", "draft": false, "prerelease": false, "html_url": "o", "body": ""},
  {"tag_name": "v0.2.0-rc.1", "name": "RC", "draft": false, "prerelease": false, "html_url": "rc", "body": ""},
  {"tag_name": "v0.1.5", "name": "Flagged", "draft": false, "prerelease": true, "html_url": "f", "body": ""},
  {"tag_name": "v0.1.2", "name": null, "draft": false, "prerelease": false,
   "html_url": "https://example.invalid/r/0.1.2", "body": "Fixes"}
])";

QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "update_checker_tests.moc"
