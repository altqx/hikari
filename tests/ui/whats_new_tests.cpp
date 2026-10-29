#include "hikari/app/update_checker.h"
#include "hikari/app/update_installer.h"
#include "hikari/app/whats_new.h"
#include "desktop_guard.h"
#include "settings_store.h"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <tuple>

using hikari::app::UpdateChecker;
using hikari::app::UpdateEnvironment;
using hikari::app::UpdateFeed;
using hikari::app::WhatsNew;
using hikari::application::PackageKind;
using hikari::ui::SettingsStore;
namespace desktop = hikari::test::desktop;

namespace {

QByteArray recordedList()
{
    QFile f(QStringLiteral(HIKARI_GITHUB_RELEASES));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QStringList versions(const WhatsNew &whatsNew)
{
    QStringList out;
    for (const QVariant &release : whatsNew.releases())
        out << release.toMap().value(QStringLiteral("version")).toString();
    return out;
}

}

class WhatsNewTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    int runs = 0;

    struct Copy {
        QString root;
        std::unique_ptr<SettingsStore> settings;
        std::unique_ptr<UpdateChecker> updates;
        std::unique_ptr<WhatsNew> whatsNew;
    };
    std::unique_ptr<Copy> start(const QString &name, const QString &version, bool autoCheck, bool online = true,
                                const QString &lastVersion = {})
    {
        auto copy = std::make_unique<Copy>();
        copy->root = dir.filePath(name);
        QDir().mkpath(copy->root + QStringLiteral("/feed"));
        QFile list(copy->root + QStringLiteral("/feed/releases.json"));
        if (online) {
            if (!list.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return nullptr;
            list.write(recordedList());
            list.close();
        } else {
            list.remove();
        }
        copy->settings = std::make_unique<SettingsStore>(copy->root + QStringLiteral("/settings.toml"));
        if (!lastVersion.isEmpty() && !QFile::exists(cacheOf(copy->root)))
            setHistory(copy->root, lastVersion, QString());
        copy->settings->set("updater.autoCheck", autoCheck);
        copy->settings->set("updater.nextCheck", QDateTime::currentSecsSinceEpoch() + 86400);
        const UpdateFeed feed = UpdateFeed::local(QUrl::fromLocalFile(copy->root + QStringLiteral("/feed/")));
        copy->updates = std::make_unique<UpdateChecker>(*copy->settings, feed, version);
        copy->whatsNew = std::make_unique<WhatsNew>(*copy->updates, cacheOf(copy->root));
        copy->whatsNew->start();
        ++runs;
        return copy;
    }
    static QString cacheOf(const QString &root) { return root + QStringLiteral("/cache/whats-new.json"); }
    static QJsonObject cacheIn(const QString &root)
    {
        QFile file(cacheOf(root));
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }
    static void setHistory(const QString &root, const QString &installed, const QString &since)
    {
        QDir().mkpath(root + QStringLiteral("/cache"));
        QFile file(cacheOf(root));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(QJsonObject{{QStringLiteral("installed"), installed}, {QStringLiteral("since"), since}})
                           .toJson(QJsonDocument::Compact));
    }
    std::unique_ptr<Copy> restart(std::unique_ptr<Copy> copy, const QString &version, bool online = true)
    {
        const QString name = QDir(dir.path()).relativeFilePath(copy->root);
        const bool autoCheck = copy->settings->boolean("updater.autoCheck");
        copy.reset();
        return start(name, version, autoCheck, online);
    }
    static bool settled(WhatsNew &whatsNew)
    {
        if (whatsNew.state() != QLatin1String("fetching"))
            return true;
        QSignalSpy spy(&whatsNew, &WhatsNew::changed);
        return spy.wait(5000) && whatsNew.state() != QLatin1String("fetching");
    }

private slots:
    void init() { desktop::clear(); }

    void nothingIsFetchedWhileUpdateChecksAreOff()
    {
        auto copy = start(QStringLiteral("off"), QStringLiteral("0.1.3"), false, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("off"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.1"));
        QCOMPARE(cacheIn(copy->root).value(QStringLiteral("installed")).toString(), QStringLiteral("0.1.3"));
        QCOMPARE(cacheIn(copy->root).value(QStringLiteral("since")).toString(), QStringLiteral("0.1.1"));
        copy->whatsNew->fetchIfNeeded();
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("off"));
        QVERIFY(!cacheIn(copy->root).contains(QStringLiteral("releases")));
        copy->updates->setAutoCheck(true);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
    }

    void aFirstStartShowsTheInstalledVersion()
    {
        auto copy = start(QStringLiteral("first"), QStringLiteral("0.1.3"), true);
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.3")});
        QCOMPARE(copy->whatsNew->since(), QString());
        const QVariantMap release = copy->whatsNew->releases().first().toMap();
        QCOMPARE(release.value(QStringLiteral("name")).toString(), QStringLiteral("HikariSub 0.1.3"));
        QCOMPARE(release.value(QStringLiteral("date")).toDate(), QDate(2026, 10, 21));
        QCOMPARE(release.value(QStringLiteral("prerelease")).toBool(), false);
        QVERIFY(release.value(QStringLiteral("notes")).toString().startsWith(QStringLiteral("## New")));
        QVERIFY(copy->settings->integer64("updater.nextCheck") > QDateTime::currentSecsSinceEpoch() + 86000);
    }

    void anUpdateShowsTheReleasesSinceTheVersionRunBefore()
    {
        auto copy = start(QStringLiteral("update"), QStringLiteral("0.1.3"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.1"));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        QCOMPARE(copy->whatsNew->more(), 0);
        QVERIFY(QFile::exists(copy->root + QStringLiteral("/cache/whats-new.json")));
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), false);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.1"));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        copy->settings->set("updater.autoCheck", false);
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), false);
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        copy->settings->set("updater.autoCheck", true);
        copy = restart(std::move(copy), QStringLiteral("0.1.4"), true);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.3"));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("none"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
    }

    void aDowngradeNamesNoEarlierVersion()
    {
        auto copy = start(QStringLiteral("downgrade"), QStringLiteral("0.1.3"), true, true, QStringLiteral("0.2.0"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.3")});
        QCOMPARE(copy->whatsNew->since(), QString());
    }

    void theChannelFollowsTheUpdateChecker()
    {
        auto copy = start(QStringLiteral("channel"), QStringLiteral("0.1.3"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QFile::remove(copy->root + QStringLiteral("/feed/releases.json"));
        QSignalSpy changed(copy->whatsNew.get(), &WhatsNew::changed);
        copy->updates->setStableOnly(false);
        QVERIFY(changed.count() >= 1);
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.3-beta.2"),
                                                          QStringLiteral("0.1.2"), QStringLiteral("0.1.2-rc.1")}));
        const QVariantMap beta = copy->whatsNew->releases().at(1).toMap();
        QVERIFY(beta.value(QStringLiteral("prerelease")).toBool());
        copy->updates->setStableOnly(true);
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        copy->updates->setStableOnly(false);
        setHistory(copy->root, QStringLiteral("0.1.0"), QString());
        copy = restart(std::move(copy), QStringLiteral("0.1.2"), true);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.2"), QStringLiteral("0.1.2-rc.1"),
                                                          QStringLiteral("0.1.1")}));
        QCOMPARE(copy->whatsNew->releases().at(2).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("HikariSub 0.1.1"));
    }

    void offlineWithNothingCachedHidesTheCard()
    {
        auto copy = start(QStringLiteral("offline"), QStringLiteral("0.1.3"), true, false, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("failed"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        QVERIFY(!cacheIn(copy->root).contains(QStringLiteral("releases")));
        QFile list(copy->root + QStringLiteral("/feed/releases.json"));
        QVERIFY(list.open(QIODevice::WriteOnly));
        list.write(recordedList());
        list.close();
        copy->whatsNew->fetchIfNeeded();
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        copy->whatsNew->fetchIfNeeded();
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
    }

    void aListThatDoesNotReadIsAFailedFetch()
    {
        auto copy = start(QStringLiteral("garbage"), QStringLiteral("0.1.3"), true, false, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QFile list(copy->root + QStringLiteral("/feed/releases.json"));
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Truncate));
        list.write("{\"message\": \"API rate limit exceeded\"}");
        list.close();
        copy->whatsNew->fetchIfNeeded();
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("failed"));
        QVERIFY(!cacheIn(copy->root).contains(QStringLiteral("releases")));
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Truncate));
        list.write("[null]");
        list.close();
        copy->whatsNew->fetchIfNeeded();
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("failed"));
        QVERIFY(!cacheIn(copy->root).contains(QStringLiteral("releases")));
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), true);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
    }

    void aCacheForAnotherRangeIsNotUsed()
    {
        auto copy = start(QStringLiteral("stale"), QStringLiteral("0.1.2"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.2")});
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), false);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("failed"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        QFile cache(copy->root + QStringLiteral("/cache/whats-new.json"));
        QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
        cache.write("{not json");
        cache.close();
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), true);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->since(), QString());
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.3")});
    }

    void eachCopyKeepsItsOwnHistory()
    {
        const QString shared = dir.filePath(QStringLiteral("shared.ini"));
        const auto run = [&](const QString &name, const QString &version, bool online) {
            const QString root = dir.filePath(name);
            QDir().mkpath(root + QStringLiteral("/feed"));
            QFile list(root + QStringLiteral("/feed/releases.json"));
            if (online && list.open(QIODevice::WriteOnly | QIODevice::Truncate))
                list.write(recordedList());
            else if (!online)
                list.remove();
            list.close();
            SettingsStore settings(shared);
            settings.set("updater.autoCheck", true);
            settings.set("updater.nextCheck", QDateTime::currentSecsSinceEpoch() + 86400);
            UpdateChecker updates(settings, UpdateFeed::local(QUrl::fromLocalFile(root + QStringLiteral("/feed/"))), version);
            WhatsNew whatsNew(updates, cacheOf(root));
            whatsNew.start();
            const bool done = settled(whatsNew);
            return std::make_tuple(done, whatsNew.since(), versions(whatsNew));
        };
        setHistory(dir.filePath(QStringLiteral("package")), QStringLiteral("0.1.1"), QString());
        setHistory(dir.filePath(QStringLiteral("appimage")), QStringLiteral("0.1.1"), QString());
        const auto package = run(QStringLiteral("package"), QStringLiteral("0.1.3"), true);
        QVERIFY(std::get<0>(package));
        QCOMPARE(std::get<1>(package), QStringLiteral("0.1.1"));
        const auto appImage = run(QStringLiteral("appimage"), QStringLiteral("0.1.2"), true);
        QVERIFY(std::get<0>(appImage));
        QCOMPARE(std::get<1>(appImage), QStringLiteral("0.1.1"));
        QCOMPARE(std::get<2>(appImage), QStringList{QStringLiteral("0.1.2")});
        const auto again = run(QStringLiteral("package"), QStringLiteral("0.1.3"), false);
        QCOMPARE(std::get<1>(again), QStringLiteral("0.1.1"));
        QCOMPARE(std::get<2>(again), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
    }

    void eachCopyHasItsOwnCacheFile()
    {
        const auto file = [](PackageKind kind, const QString &executable, const QString &appImage = {}) {
            UpdateEnvironment environment;
            environment.cacheDir = QStringLiteral("/cache");
            environment.package.kind = kind;
            environment.package.appImage = appImage;
            environment.executable = executable;
            return WhatsNew::cacheFileIn(environment);
        };
        const QString portable = file(PackageKind::Portable, QStringLiteral("/home/u/A/hikarisub"));
        QVERIFY(portable.startsWith(QStringLiteral("/cache/whats-new-")));
        QVERIFY(portable != file(PackageKind::Portable, QStringLiteral("/home/u/B/hikarisub")));
        QCOMPARE(file(PackageKind::Portable, QStringLiteral("/home/u/A/hikarisub")), portable);
        const QString image = QStringLiteral("/home/u/Apps/HikariSub.AppImage");
        QCOMPARE(file(PackageKind::AppImage, QStringLiteral("/tmp/.mount_a/usr/bin/hikarisub"), image),
                 file(PackageKind::AppImage, QStringLiteral("/tmp/.mount_b/usr/bin/hikarisub"), image));
        QVERIFY(file(PackageKind::AppImage, QString(), image) !=
                file(PackageKind::AppImage, QString(), QStringLiteral("/home/u/Apps/Other.AppImage")));
    }

    void theCacheStaysWithinWhatItsReaderTakes()
    {
        std::vector<hikari::application::ReleaseEntry> releases;
        for (const char *tag : {"v0.1.3", "v0.1.2", "v0.1.1"}) {
            hikari::application::ReleaseEntry release;
            release.tag = tag;
            release.name = std::string("HikariSub ") + (tag + 1);
            release.notes = std::string(1000, 'x');
            releases.push_back(release);
        }
        const auto entries = [](const QByteArray &content) {
            QStringList out;
            for (const QJsonValue &r : QJsonDocument::fromJson(content).object().value(QStringLiteral("releases")).toArray())
                out << r.toObject().value(QStringLiteral("tag")).toString() + QLatin1Char(':') +
                           QString::number(r.toObject().value(QStringLiteral("notes")).toString().size());
            return out;
        };
        const QString installed = QStringLiteral("0.1.3");
        const QString since = QStringLiteral("0.1.0");
        const QByteArray all = WhatsNew::cacheContent(installed, since, releases, WhatsNew::kMaxCacheBytes);
        QCOMPARE(entries(all), (QStringList{QStringLiteral("v0.1.3:1000"), QStringLiteral("v0.1.2:1000"), QStringLiteral("v0.1.1:1000")}));
        const QByteArray two = WhatsNew::cacheContent(installed, since, releases, all.size() - 1);
        QVERIFY(two.size() <= all.size() - 1);
        QCOMPARE(entries(two), (QStringList{QStringLiteral("v0.1.3:1000"), QStringLiteral("v0.1.2:1000"), QStringLiteral("v0.1.1:0")}));
        const QByteArray one = WhatsNew::cacheContent(installed, since, releases, 1500);
        QVERIFY(one.size() <= 1500);
        QCOMPARE(entries(one), (QStringList{QStringLiteral("v0.1.3:1000"), QStringLiteral("v0.1.2:0"), QStringLiteral("v0.1.1:0")}));
        QCOMPARE(QJsonDocument::fromJson(one).object().value(QStringLiteral("since")).toString(), since);
        const QByteArray none = WhatsNew::cacheContent(installed, since, releases, 200);
        QVERIFY(none.size() <= 200);
        QVERIFY(entries(none).size() < 3);
        std::vector<hikari::application::ReleaseEntry> channels;
        const auto add = [&](const std::string &tag, bool prerelease) {
            hikari::application::ReleaseEntry release;
            release.tag = tag;
            release.prerelease = prerelease;
            release.notes = std::string(1000, 'x');
            channels.push_back(release);
        };
        add("v0.2.0", false);
        for (int beta = 12; beta >= 1; --beta)
            add("v0.2.0-beta." + std::to_string(beta), true);
        add("v0.1.9", false);
        const QByteArray whole = WhatsNew::cacheContent(QStringLiteral("0.2.0"), since, channels, WhatsNew::kMaxCacheBytes);
        const QStringList kept = entries(WhatsNew::cacheContent(QStringLiteral("0.2.0"), since, channels, whole.size() - 1));
        QCOMPARE(kept.size(), 14);
        QCOMPARE(kept.last(), QStringLiteral("v0.1.9:1000"));
        QCOMPARE(kept.at(12), QStringLiteral("v0.2.0-beta.1:0"));
        QCOMPARE(kept.at(10), QStringLiteral("v0.2.0-beta.3:1000"));
        QVERIFY(WhatsNew::cacheContent(installed, since, releases, 10).isEmpty());
    }

    void aTrimmedCacheKeepsTheEarlierVersionsCount()
    {
        auto copy = start(QStringLiteral("trimmed"), QStringLiteral("0.1.13"), true, false, QStringLiteral("0.1.0"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        std::vector<hikari::application::ReleaseEntry> releases;
        for (int patch = 13; patch >= 1; --patch) {
            hikari::application::ReleaseEntry release;
            release.tag = "v0.1." + std::to_string(patch);
            release.notes = std::string(1000, 'x');
            releases.push_back(release);
        }
        const QByteArray full = WhatsNew::cacheContent(QStringLiteral("0.1.13"), QStringLiteral("0.1.0"), releases,
                                                       WhatsNew::kMaxCacheBytes);
        const QByteArray trimmed = WhatsNew::cacheContent(QStringLiteral("0.1.13"), QStringLiteral("0.1.0"), releases,
                                                          full.size() - 2000);
        QFile cache(copy->root + QStringLiteral("/cache/whats-new.json"));
        QVERIFY(QDir().mkpath(copy->root + QStringLiteral("/cache")));
        QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
        cache.write(trimmed);
        cache.close();
        copy = restart(std::move(copy), QStringLiteral("0.1.13"), false);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(copy->whatsNew->releases().size(), 10);
        QCOMPARE(copy->whatsNew->more(), 3);
    }

    void theUpdateCheckAndWhatsNewShareTheList()
    {
        auto copy = start(QStringLiteral("shared"), QStringLiteral("0.1.2"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(!copy->updates->checking());
        QSignalSpy finished(copy->updates.get(), &UpdateChecker::finished);
        copy->updates->checkNow();
        QVERIFY(copy->updates->checking());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("available"));
        QCOMPARE(finished.at(0).at(1).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.1.3"));
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.2")});
        QSignalSpy read(copy->updates.get(), &UpdateChecker::releasesRead);
        copy->updates->checkNow();
        QVERIFY(read.wait(5000));
        QCOMPARE(read.at(0).at(0).toBool(), true);
    }

    void aReadDuringACheckPastItsListTakesThatList()
    {
        auto copy = start(QStringLiteral("late"), QStringLiteral("0.1.3"), false, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("off"));
        bool asked = false;
        connect(copy->updates.get(), &UpdateChecker::releasesRead, copy->whatsNew.get(), [&](bool ok) {
            if (asked || !ok)
                return;
            asked = true;
            copy->updates->setAutoCheck(true);
            copy->whatsNew->fetchIfNeeded();
        });
        copy->updates->checkNow();
        QTRY_VERIFY(asked);
        QTRY_COMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
    }

    void releaseNotesRenderAsPlainRichText()
    {
        auto copy = start(QStringLiteral("render"), QStringLiteral("0.1.3"), true);
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        const QString notes = copy->whatsNew->releases().first().toMap().value(QStringLiteral("notes")).toString();
        const QColor muted(QStringLiteral("#526170"));
        const QColor link(QStringLiteral("#145c4c"));
        const QColor code(QStringLiteral("#edf0f3"));
        const QString html = copy->whatsNew->render(notes, muted, link, code);
        QVERIFY(html.contains(QStringLiteral("Translation mode:")));
        QVERIFY(html.contains(QStringLiteral("Fixed")));
        for (int level = 1; level <= 6; ++level)
            QVERIFY2(!html.contains(QStringLiteral("<h%1").arg(level), Qt::CaseInsensitive), qPrintable(html));
        QVERIFY(html.contains(muted.name(), Qt::CaseInsensitive));
        QVERIFY(html.contains(link.name(), Qt::CaseInsensitive));
        QVERIFY(html.contains(code.name(), Qt::CaseInsensitive));
        QVERIFY(html.contains(QStringLiteral("href=\"https://github.com/altqx/hikari/pull/287\"")));
        QVERIFY2(html.contains(QStringLiteral("href=\"https://github.com/altqx/hikari/compare/v0.1.2...v0.1.3\"")), qPrintable(html));
        QVERIFY2(!html.contains(QStringLiteral("<img"), Qt::CaseInsensitive), qPrintable(html));
        QVERIFY(html.contains(QStringLiteral("The karaoke check")));
        QVERIFY(!html.contains(QStringLiteral("karaoke-check.png")));
        QVERIFY2(!html.contains(QStringLiteral("tracker.gif")), qPrintable(html));
        QVERIFY(!html.contains(QStringLiteral("<script"), Qt::CaseInsensitive));
        QVERIFY2(!html.contains(QStringLiteral("&lt;script"), Qt::CaseInsensitive), qPrintable(html));
        QVERIFY2(!html.contains(QStringLiteral("alert(1)")), qPrintable(html));
        const QString scripted = copy->whatsNew->render(
            QStringLiteral("Keep <SCRIPT type=\"x\">gone()</script> this\n<style>\np { color: red }\n</style >\nand this"),
            muted, link, code);
        QVERIFY2(scripted.contains(QStringLiteral("Keep  this")), qPrintable(scripted));
        QVERIFY(scripted.contains(QStringLiteral("and this")));
        QVERIFY(!scripted.contains(QStringLiteral("gone()")));
        QVERIFY(!scripted.contains(QStringLiteral("color: red")));
        const QString ticked = copy->whatsNew->render(
            QStringLiteral("A <!-- run `foo` before publishing --> B\n<script>x = `bar`\n`baz`</script> C `kept`"),
            muted, link, code);
        QVERIFY2(!ticked.contains(QStringLiteral("foo")), qPrintable(ticked));
        QVERIFY(!ticked.contains(QStringLiteral("bar")));
        QVERIFY(!ticked.contains(QStringLiteral("baz")));
        QVERIFY(ticked.contains(QStringLiteral("kept")));
        QVERIFY(ticked.contains(QStringLiteral("C")));
        const QString fenced = copy->whatsNew->render(
            QStringLiteral("````\n```\n<b>in four</b>\n~~~\n````\n\n"
                           "Use ``a `<i>tick</i>` here`` and ``two\n<u>lines</u>`` then <s>gone</s> ` lone <q>dropped</q>"),
            muted, link, code);
        QVERIFY2(fenced.contains(QStringLiteral("&lt;b&gt;in four&lt;/b&gt;")), qPrintable(fenced));
        QVERIFY(fenced.contains(QStringLiteral("&lt;i&gt;tick&lt;/i&gt;")));
        QVERIFY(fenced.contains(QStringLiteral("&lt;u&gt;lines&lt;/u&gt;")));
        QVERIFY(!fenced.contains(QStringLiteral("&lt;s&gt;")));
        QVERIFY(fenced.contains(QStringLiteral("gone")));
        QVERIFY(!fenced.contains(QStringLiteral("&lt;q&gt;")));
        QVERIFY(fenced.contains(QStringLiteral("dropped")));
        const QString indented = copy->whatsNew->render(
            QStringLiteral("Run:\n\n    <b>literal</b> https://example.com/x\n\n- item\n\n    more <i>of it</i>\n"),
            muted, link, code);
        QVERIFY2(indented.contains(QStringLiteral("&lt;b&gt;literal&lt;/b&gt;")), qPrintable(indented));
        QVERIFY(!indented.contains(QStringLiteral("href=\"https://example.com/x")));
        QVERIFY(!indented.contains(QStringLiteral("&lt;i&gt;")));
        QVERIFY(indented.contains(QStringLiteral("of it")));
        const QString contained = copy->whatsNew->render(
            QStringLiteral("- item\n\n    ```\n    <b>in item</b>\n\n    https://example.com/y <u>u</u>\n    ```\n\n"
                           "> ```\n> <i>quoted</i>\n>\n> <s>s</s>\n> ```\n"),
            muted, link, code);
        QVERIFY2(contained.contains(QStringLiteral("&lt;b&gt;in item&lt;/b&gt;")), qPrintable(contained));
        QVERIFY(contained.contains(QStringLiteral("&lt;u&gt;u&lt;/u&gt;")));
        QVERIFY(!contained.contains(QStringLiteral("href=\"https://example.com/y")));
        QVERIFY(contained.contains(QStringLiteral("&lt;i&gt;quoted&lt;/i&gt;")));
        QVERIFY(contained.contains(QStringLiteral("&lt;s&gt;s&lt;/s&gt;")));
        const QString alone = copy->whatsNew->render(QStringLiteral("One\n\n<img src=\"x.gif\"> <br>\n\nTwo"), muted, link, code);
        QCOMPARE(alone.count(QStringLiteral("<p")), 2);
        const QString split = copy->whatsNew->render(
            QStringLiteral("Before <script\ntype=\"x\">hidden()</script> after"), muted, link, code);
        QVERIFY2(!split.contains(QStringLiteral("hidden()")), qPrintable(split));
        QVERIFY(split.contains(QStringLiteral("after")));
        const QString selfClosed = copy->whatsNew->render(
            QStringLiteral("Before <script/> after\n\nNext paragraph"), muted, link, code);
        QVERIFY2(selfClosed.contains(QStringLiteral("Before  after")), qPrintable(selfClosed));
        QVERIFY(selfClosed.contains(QStringLiteral("Next paragraph")));
        const QString escaped = copy->whatsNew->render(
            QStringLiteral("Use \\<video\\> or &lt;audio&gt; or &#60;x&#x3E; but <b>gone</b>, \\\\<i>y</i>\n\n"
                           "`\\<v\\> &lt;a&gt;` <!-- a --\\> b --> after"),
            muted, link, code);
        QVERIFY2(escaped.contains(QStringLiteral("Use &lt;video&gt; or &lt;audio&gt; or &lt;x&gt; but gone, \\y")),
                 qPrintable(escaped));
        QVERIFY(escaped.contains(QStringLiteral("\\&lt;v\\&gt; &amp;lt;a&amp;gt;")));
        QVERIFY(escaped.contains(QStringLiteral("after")));
        QVERIFY(!escaped.contains(QStringLiteral(" b ")));
        const QString badge = copy->whatsNew->render(
            QStringLiteral("[![Build status](badge.svg)](https://example.com/build)"), muted, link, code);
        QVERIFY2(badge.contains(QStringLiteral("href=\"https://example.com/build\"")), qPrintable(badge));
        QVERIFY(badge.contains(QStringLiteral("Build status")));
        const QString badges = copy->whatsNew->render(
            QStringLiteral("[![Use \\<x\\>](b.svg \"t\")](<https://example.com/y> 'u') and "
                           "`[![a](b.svg)](https://example.com/c)`\n\n"
                           "```\n[![d](e.svg)](https://example.com/f) \\<g\\>\n```\n"),
            muted, link, code);
        QVERIFY2(badges.contains(QStringLiteral("href=\"https://example.com/y\"")), qPrintable(badges));
        QVERIFY(badges.contains(QStringLiteral("Use &lt;x&gt;")));
        QVERIFY(badges.contains(QStringLiteral("[![a](b.svg)](https://example.com/c)")));
        QVERIFY(!badges.contains(QStringLiteral("href=\"https://example.com/c")));
        QVERIFY(badges.contains(QStringLiteral("[![d](e.svg)](https://example.com/f) \\&lt;g\\&gt;")));
        QVERIFY(!badges.contains(QStringLiteral("href=\"https://example.com/f")));
        const QString parens = copy->whatsNew->render(
            QStringLiteral("[![Build](b_(1).svg)](https://example.com/build_(linux)) and "
                           "[![Test](t.svg)](https://example.com/t\\(x)"),
            muted, link, code);
        QVERIFY2(parens.contains(QStringLiteral("href=\"https://example.com/build_(linux)\"")), qPrintable(parens));
        QVERIFY(parens.contains(QStringLiteral("href=\"https://example.com/t(x\"")));
        const QString referenced = copy->whatsNew->render(
            QStringLiteral("[![Build](badge.svg)][build] and [![Test][tbadge]][test] and [![Lint][]][test]\n\n"
                           "[build]: https://example.com/build\n[test]: https://example.com/test\n[tbadge]: t.svg\n"
                           "[Lint]: l.svg\n"),
            muted, link, code);
        QVERIFY2(referenced.contains(QStringLiteral("href=\"https://example.com/build\"")), qPrintable(referenced));
        QVERIFY(referenced.contains(QStringLiteral("href=\"https://example.com/test\"")));
        QVERIFY(referenced.contains(QStringLiteral("Build")));
        QVERIFY(referenced.contains(QStringLiteral("Test")));
        QVERIFY(referenced.contains(QStringLiteral("Lint")));
        QCOMPARE(referenced.count(QStringLiteral("href=\"https://example.com/test\"")), 2);
        QString many;
        for (int i = 0; i < 5000; ++i)
            many += QStringLiteral("[![b](%1.svg)](https://example.com/%1) \\<v\\> &lt;a&gt; ").arg(i);
        QElapsedTimer marked;
        marked.start();
        const QString manyHtml = copy->whatsNew->render(many, muted, link, code);
        QVERIFY2(marked.elapsed() < 2000, qPrintable(QString::number(marked.elapsed())));
        QVERIFY(manyHtml.contains(QStringLiteral("href=\"https://example.com/4999\"")));
        QString linked;
        for (int i = 0; i < 20000; ++i)
            linked += QStringLiteral("[![b](%1.svg)](https://example.com/%1) \\<%1\\> ").arg(i);
        QElapsedTimer restored;
        restored.start();
        const QString linkedHtml = copy->whatsNew->render(linked, muted, link, code);
        QVERIFY2(restored.elapsed() < 2000, qPrintable(QString::number(restored.elapsed())));
        QVERIFY(linkedHtml.contains(QStringLiteral("&lt;19999&gt;")));
        const QString alts = copy->whatsNew->render(
            QStringLiteral("Before ![Use \\<video\\>](a.png) mid ![x \\<script\\>](b.png) after\n\nNext"),
            muted, link, code);
        QVERIFY2(alts.contains(QStringLiteral("Use &lt;video&gt;")), qPrintable(alts));
        QVERIFY(alts.contains(QStringLiteral("x &lt;script&gt;")));
        QVERIFY(alts.contains(QStringLiteral("after")));
        QVERIFY(alts.contains(QStringLiteral("Next")));
        const QString closed = copy->whatsNew->render(
            QStringLiteral("<script>const x = `</script>` kept\n\nNext paragraph\n\n"
                           "<!-- note `-->` after\n\nLast paragraph"),
            muted, link, code);
        QVERIFY2(closed.contains(QStringLiteral("Next paragraph")), qPrintable(closed));
        QVERIFY(closed.contains(QStringLiteral("Last paragraph")));
        QVERIFY(closed.contains(QStringLiteral("kept")));
        QVERIFY(closed.contains(QStringLiteral("after")));
        QVERIFY(!closed.contains(QStringLiteral("const x")));
        QVERIFY(!closed.contains(QStringLiteral("note")));
        const QString declared = copy->whatsNew->render(
            QStringLiteral("A <!DOCTYPE html> B <?xml version=\"1.0\"?> C <![CDATA[x < y]]> D <?php  \necho 1;\n?> E "
                           "`<?kept?>`"),
            muted, link, code);
        QVERIFY2(declared.contains(QStringLiteral("A  B  C  D")), qPrintable(declared));
        QVERIFY(declared.contains(QStringLiteral("E")));
        QVERIFY(!declared.contains(QStringLiteral("&lt;!DOCTYPE")));
        QVERIFY(!declared.contains(QStringLiteral("xml")));
        QVERIFY(!declared.contains(QStringLiteral("CDATA")));
        QVERIFY(!declared.contains(QStringLiteral("x &lt; y")));
        QVERIFY(!declared.contains(QStringLiteral("echo")));
        QVERIFY(declared.contains(QStringLiteral("&lt;?kept?&gt;")));
        const QString wrapped = copy->whatsNew->render(
            QStringLiteral("Text <div\nclass=\"note\"\nid=x>inside</div> more <span  \ntitle=y>z</span>"), muted,
            link, code);
        QVERIFY2(wrapped.contains(QStringLiteral("Text inside more z")), qPrintable(wrapped));
        QVERIFY(!wrapped.contains(QStringLiteral("class=")));
        QVERIFY(!wrapped.contains(QStringLiteral("title=")));
        const QString wrappedEscaped = copy->whatsNew->render(
            QStringLiteral("Use \\<div\nclass=x> literally, not \\\\<span\ntitle=y>this</span>"), muted, link, code);
        QVERIFY2(wrappedEscaped.contains(QStringLiteral("Use &lt;div class=x&gt; literally, not \\this")),
                 qPrintable(wrappedEscaped));
        const QString wrappedCode = copy->whatsNew->render(
            QStringLiteral("```\n<div\nclass=a>\n```\n\nUse `<span\ntitle=b>` here\n\nSee <q\n> quoted"), muted, link,
            code);
        QVERIFY2(wrappedCode.contains(QStringLiteral("&lt;div")), qPrintable(wrappedCode));
        QVERIFY(wrappedCode.contains(QStringLiteral("class=a&gt;")));
        QVERIFY(!wrappedCode.contains(QStringLiteral("&lt;div class")));
        QVERIFY(wrappedCode.contains(QStringLiteral("&lt;span title=b&gt;")));
        QVERIFY(wrappedCode.contains(QStringLiteral("quoted")));
        const QString quoted = copy->whatsNew->render(
            QStringLiteral("A <span title=\"1 > 0\" data-x='a>b'>kept</span> B"), muted, link, code);
        QVERIFY2(quoted.contains(QStringLiteral("A kept B")), qPrintable(quoted));
        QVERIFY(!quoted.contains(QStringLiteral("1 &gt; 0")));
        QVERIFY(!quoted.contains(QStringLiteral("a&gt;b")));
        QVERIFY(!html.contains(QStringLiteral("&lt;img"), Qt::CaseInsensitive));
        QVERIFY(!html.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive));
        QVERIFY(!html.contains(QStringLiteral("file:///etc/passwd")));
        QVERIFY(html.contains(QStringLiteral("Run this")));
        QVERIFY(html.contains(QStringLiteral("AppImage")));
        const QString inCode = copy->whatsNew->render(
            QStringLiteral("Run `curl https://example.com/a...b` or\n```\nhttps://example.com/c\n```\n"
                           "[the compare](https://example.com/d...e)."),
            muted, link, code);
        QVERIFY2(!inCode.contains(QStringLiteral("href=\"https://example.com/a")), qPrintable(inCode));
        QVERIFY(!inCode.contains(QStringLiteral("href=\"https://example.com/c")));
        QVERIFY(inCode.contains(QStringLiteral("href=\"https://example.com/d...e\"")));
        QVERIFY(!inCode.contains(QStringLiteral("&lt;https")));
        const QString ends = copy->whatsNew->render(
            QStringLiteral("See https://en.wikipedia.org/wiki/Foo_(bar) and (https://example.com/f) or "
                           "https://example.com/g_(h)_i?x=(1)(2)). Also \"https://example.com/j\"; "
                           "https://example.com/k;"),
            muted, link, code);
        QVERIFY2(ends.contains(QStringLiteral("href=\"https://en.wikipedia.org/wiki/Foo_(bar)\"")), qPrintable(ends));
        QVERIFY(ends.contains(QStringLiteral("href=\"https://example.com/f\"")));
        QVERIFY(ends.contains(QStringLiteral("href=\"https://example.com/g_(h)_i?x=(1)(2)\"")));
        QVERIFY(ends.contains(QStringLiteral("href=\"https://example.com/j\"")));
        QVERIFY(ends.contains(QStringLiteral("href=\"https://example.com/k\"")));
        QElapsedTimer timer;
        timer.start();
        const QString tail = copy->whatsNew->render(
            QStringLiteral("https://example.com/x") + QString(100000, u')') + QString(100000, u';'), muted, link, code);
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(tail.contains(QStringLiteral("href=\"https://example.com/x\"")));
        const QString commented = copy->whatsNew->render(
            QStringLiteral("Before <!-- one --> after\n<!-- two\nlines -->\nlast `<b>kept</b>`"), muted, link, code);
        QVERIFY2(commented.contains(QStringLiteral("Before  after")), qPrintable(commented));
        QVERIFY(!commented.contains(QStringLiteral("one")));
        QVERIFY(!commented.contains(QStringLiteral("lines")));
        QVERIFY(commented.contains(QStringLiteral("last")));
        QVERIFY(commented.contains(QStringLiteral("&lt;b&gt;kept&lt;/b&gt;")));
        QVERIFY(!html.contains(QStringLiteral("<body style"), Qt::CaseInsensitive));
    }

    void onlyWebLinksOpen()
    {
        auto copy = start(QStringLiteral("links"), QStringLiteral("0.1.3"), false);
        QVERIFY(copy);
        QVERIFY(copy->whatsNew->openLink(QStringLiteral("https://github.com/altqx/hikari/pull/287")));
        QVERIFY(!copy->whatsNew->openLink(QStringLiteral("javascript:alert(1)")));
        QVERIFY(!copy->whatsNew->openLink(QStringLiteral("file:///etc/passwd")));
        QVERIFY(!copy->whatsNew->openLink(QStringLiteral("https://user:pw@example.com/")));
        QVERIFY(!copy->whatsNew->openLink(QString()));
        copy->whatsNew->openAllReleaseNotes();
        QCOMPARE(desktop::opened(),
                 (QList<QUrl>{QUrl(QStringLiteral("https://github.com/altqx/hikari/pull/287")),
                              QUrl::fromLocalFile(copy->root + QStringLiteral("/feed/releases"))}));
        QCOMPARE(WhatsNew::allReleaseNotesUrl(UpdateFeed::github()),
                 QUrl(QStringLiteral("https://github.com/altqx/hikari/releases")));
    }
};

QTEST_MAIN(WhatsNewTest)
#include "whats_new_tests.moc"
