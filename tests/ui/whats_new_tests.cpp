// SH3 (#223): What's new from the GitHub release notes, against a recorded
// answer of GitHub's release list (tests/fixtures/github-releases), served
// as a local feed so nothing leaves the machine. The owner-approved rules
// (2026-10-21): the notes after the version run before, up to the installed
// one, on the update checker's channel; fetched only while "Check for
// updates automatically" is on, once per installed version, and cached;
// Markdown without raw HTML or images, http(s) links only.

#include "hikari/app/update_checker.h"
#include "hikari/app/whats_new.h"
#include "desktop_guard.h"
#include "settings_store.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using hikari::app::UpdateChecker;
using hikari::app::UpdateFeed;
using hikari::app::WhatsNew;
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

} // namespace

class WhatsNewTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    int runs = 0;

    // One copy of the program: its settings, its feed (the recorded list,
    // when `online`) and its cache, in a folder of its own.
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
        copy->settings = std::make_unique<SettingsStore>(copy->root + QStringLiteral("/hikari.ini"));
        if (!lastVersion.isEmpty() && copy->settings->text("updater.lastVersion").isEmpty())
            copy->settings->set("updater.lastVersion", lastVersion);
        copy->settings->set("updater.autoCheck", autoCheck);
        // A day ahead: the start's own update check stays away.
        copy->settings->set("updater.nextCheck", QDateTime::currentSecsSinceEpoch() + 86400);
        const UpdateFeed feed = UpdateFeed::local(QUrl::fromLocalFile(copy->root + QStringLiteral("/feed/")));
        copy->updates = std::make_unique<UpdateChecker>(*copy->settings, feed, version);
        copy->whatsNew = std::make_unique<WhatsNew>(*copy->settings, *copy->updates,
                                                    copy->root + QStringLiteral("/cache/whats-new.json"));
        copy->whatsNew->start();
        ++runs;
        return copy;
    }
    // The same copy started again, as the next run of `version`.
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

    // Off: no request, nothing to show, the card hidden.
    void nothingIsFetchedWhileUpdateChecksAreOff()
    {
        auto copy = start(QStringLiteral("off"), QStringLiteral("0.1.3"), false, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("off"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        // The version run before is still recorded, for when checks go on.
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.1"));
        QCOMPARE(copy->settings->text("updater.lastVersion"), QStringLiteral("0.1.3"));
        // Help > What's new asks for nothing either.
        copy->whatsNew->fetchIfNeeded();
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("off"));
        QVERIFY(!QFile::exists(copy->root + QStringLiteral("/cache/whats-new.json")));
    }

    // The first start ever: the installed version alone.
    void aFirstStartShowsTheInstalledVersion()
    {
        auto copy = start(QStringLiteral("first"), QStringLiteral("0.1.3"), true);
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(copy->whatsNew->releases().isEmpty()); // hidden while the first fetch runs
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.3")});
        QCOMPARE(copy->whatsNew->since(), QString());
        const QVariantMap release = copy->whatsNew->releases().first().toMap();
        QCOMPARE(release.value(QStringLiteral("name")).toString(), QStringLiteral("HikariSub 0.1.3"));
        QCOMPARE(release.value(QStringLiteral("date")).toDate(), QDate(2026, 10, 21));
        QCOMPARE(release.value(QStringLiteral("prerelease")).toBool(), false);
        QVERIFY(release.value(QStringLiteral("notes")).toString().startsWith(QStringLiteral("## New")));
        // The list read for What's new is no update check: its time stays.
        QVERIFY(copy->settings->integer64("updater.nextCheck") > QDateTime::currentSecsSinceEpoch() + 86000);
    }

    // After an update: every release since the version run before, kept on
    // the next starts (from the cache, offline too) until the next update.
    void anUpdateShowsTheReleasesSinceTheVersionRunBefore()
    {
        auto copy = start(QStringLiteral("update"), QStringLiteral("0.1.3"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.1"));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        QCOMPARE(copy->whatsNew->more(), 0);
        QVERIFY(QFile::exists(copy->root + QStringLiteral("/cache/whats-new.json")));
        // The next start of the same version: from the cache, no request
        // (the feed is gone), the same notes.
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), false);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.1"));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        // Checks turned off later: the cached notes still show.
        copy->settings->set("updater.autoCheck", false);
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), false);
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        // The next update starts after 0.1.3 (none of the fixture's
        // releases lie between 0.1.3 and 0.1.4 but 0.1.4's draft).
        copy->settings->set("updater.autoCheck", true);
        copy = restart(std::move(copy), QStringLiteral("0.1.4"), true);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.3"));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("none"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
    }

    // The channel is the update checker's; switching needs no new fetch.
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
        // A release without a name is named for its version.
        copy->updates->setStableOnly(false);
        copy->settings->set("updater.lastVersion", QStringLiteral("0.1.0"));
        copy->settings->set("updater.notesSince", QString());
        copy = restart(std::move(copy), QStringLiteral("0.1.2"), true);
        // (lastVersion 0.1.0 ran last, so 0.1.2 starts after it.)
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.2"), QStringLiteral("0.1.2-rc.1"),
                                                          QStringLiteral("0.1.1")}));
        QCOMPARE(copy->whatsNew->releases().at(2).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("HikariSub 0.1.1"));
    }

    // Offline with nothing cached: failed, hidden, tried again at the next
    // start; Help > What's new tries again when asked.
    void offlineWithNothingCachedHidesTheCard()
    {
        auto copy = start(QStringLiteral("offline"), QStringLiteral("0.1.3"), true, false, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("failed"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        QVERIFY(!QFile::exists(copy->root + QStringLiteral("/cache/whats-new.json")));
        // Back online: Help > What's new fetches.
        QFile list(copy->root + QStringLiteral("/feed/releases.json"));
        QVERIFY(list.open(QIODevice::WriteOnly));
        list.write(recordedList());
        list.close();
        copy->whatsNew->fetchIfNeeded();
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), (QStringList{QStringLiteral("0.1.3"), QStringLiteral("0.1.2")}));
        // With notes cached, asking again fetches nothing.
        copy->whatsNew->fetchIfNeeded();
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("ready"));
    }

    // A cache written for another version or another starting point is
    // not used; one that does not read is ignored.
    void aCacheForAnotherRangeIsNotUsed()
    {
        auto copy = start(QStringLiteral("stale"), QStringLiteral("0.1.2"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.2")});
        // 0.1.3 offline: the 0.1.2 cache is not its notes.
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), false);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("failed"));
        QVERIFY(copy->whatsNew->releases().isEmpty());
        // A damaged cache reads as none.
        QFile cache(copy->root + QStringLiteral("/cache/whats-new.json"));
        QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
        cache.write("{not json");
        cache.close();
        copy = restart(std::move(copy), QStringLiteral("0.1.3"), true);
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(copy->whatsNew->since(), QStringLiteral("0.1.2"));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.3")});
    }

    // The automatic update check at start reads the list once for both; a
    // menu check during What's new's own read still reports.
    void theUpdateCheckAndWhatsNewShareTheList()
    {
        auto copy = start(QStringLiteral("shared"), QStringLiteral("0.1.2"), true, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("fetching"));
        QVERIFY(!copy->updates->checking()); // What's new's read is no check
        QSignalSpy finished(copy->updates.get(), &UpdateChecker::finished);
        copy->updates->checkNow(); // joins the read
        QVERIFY(copy->updates->checking());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("available"));
        QCOMPARE(finished.at(0).at(1).toMap().value(QStringLiteral("version")).toString(), QStringLiteral("0.1.3"));
        QVERIFY(settled(*copy->whatsNew));
        QCOMPARE(versions(*copy->whatsNew), QStringList{QStringLiteral("0.1.2")});
        // An update check's list refreshes the notes (edited on GitHub
        // since), checks on.
        QSignalSpy read(copy->updates.get(), &UpdateChecker::releasesRead);
        copy->updates->checkNow();
        QVERIFY(read.wait(5000));
        QCOMPARE(read.at(0).at(0).toBool(), true);
    }

    // What's new asking while a check is past its list (checking the
    // releases' manifests) takes the list that check read.
    void aReadDuringACheckPastItsListTakesThatList()
    {
        auto copy = start(QStringLiteral("late"), QStringLiteral("0.1.3"), false, true, QStringLiteral("0.1.1"));
        QVERIFY(copy);
        QCOMPARE(copy->whatsNew->state(), QStringLiteral("off"));
        // A menu check with checks off: once its list is read, checks go on
        // and Help > What's new opens.
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

    // Markdown as plain rich text: headings shrink to section labels, raw
    // HTML and images never come through, only http(s) links stay links.
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
        // No heading elements: section labels in the muted colour.
        for (int level = 1; level <= 6; ++level)
            QVERIFY2(!html.contains(QStringLiteral("<h%1").arg(level), Qt::CaseInsensitive), qPrintable(html));
        QVERIFY(html.contains(muted.name(), Qt::CaseInsensitive));
        // Links in the link colour, code on the code background.
        QVERIFY(html.contains(link.name(), Qt::CaseInsensitive));
        QVERIFY(html.contains(code.name(), Qt::CaseInsensitive));
        // Links: GitHub's, and a bare URL made a link.
        QVERIFY(html.contains(QStringLiteral("href=\"https://github.com/altqx/hikari/pull/287\"")));
        QVERIFY2(html.contains(QStringLiteral("href=\"https://github.com/altqx/hikari/compare/v0.1.2...v0.1.3\"")), qPrintable(html));
        // No image, the alt text instead; no raw HTML, no other scheme.
        QVERIFY2(!html.contains(QStringLiteral("<img"), Qt::CaseInsensitive), qPrintable(html));
        QVERIFY(html.contains(QStringLiteral("The karaoke check")));
        QVERIFY(!html.contains(QStringLiteral("karaoke-check.png")));
        QVERIFY2(!html.contains(QStringLiteral("tracker.gif")), qPrintable(html));
        QVERIFY(!html.contains(QStringLiteral("<script"), Qt::CaseInsensitive));
        QVERIFY2(!html.contains(QStringLiteral("&lt;script"), Qt::CaseInsensitive), qPrintable(html));
        // A script's or a style's text goes with it, over lines too.
        QVERIFY2(!html.contains(QStringLiteral("alert(1)")), qPrintable(html));
        const QString scripted = copy->whatsNew->render(
            QStringLiteral("Keep <SCRIPT type=\"x\">gone()</script> this\n<style>\np { color: red }\n</style >\nand this"),
            muted, link, code);
        QVERIFY2(scripted.contains(QStringLiteral("Keep  this")), qPrintable(scripted));
        QVERIFY(scripted.contains(QStringLiteral("and this")));
        QVERIFY(!scripted.contains(QStringLiteral("gone()")));
        QVERIFY(!scripted.contains(QStringLiteral("color: red")));
        QVERIFY(!html.contains(QStringLiteral("&lt;img"), Qt::CaseInsensitive));
        QVERIFY(!html.contains(QStringLiteral("javascript:"), Qt::CaseInsensitive));
        QVERIFY(!html.contains(QStringLiteral("file:///etc/passwd")));
        QVERIFY(html.contains(QStringLiteral("Run this")));
        // The table's cells come through.
        QVERIFY(html.contains(QStringLiteral("AppImage")));
        // A bare URL in code stays code; one in a link's syntax is that link.
        const QString inCode = copy->whatsNew->render(
            QStringLiteral("Run `curl https://example.com/a...b` or\n```\nhttps://example.com/c\n```\n"
                           "[the compare](https://example.com/d...e)."),
            muted, link, code);
        QVERIFY2(!inCode.contains(QStringLiteral("href=\"https://example.com/a")), qPrintable(inCode));
        QVERIFY(!inCode.contains(QStringLiteral("href=\"https://example.com/c")));
        QVERIFY(inCode.contains(QStringLiteral("href=\"https://example.com/d...e\"")));
        QVERIFY(!inCode.contains(QStringLiteral("&lt;https")));
        // An HTML comment (release templates leave them), over lines too.
        const QString commented = copy->whatsNew->render(
            QStringLiteral("Before <!-- one --> after\n<!-- two\nlines -->\nlast `<b>kept</b>`"), muted, link, code);
        QVERIFY2(commented.contains(QStringLiteral("Before  after")), qPrintable(commented));
        QVERIFY(!commented.contains(QStringLiteral("one")));
        QVERIFY(!commented.contains(QStringLiteral("lines")));
        QVERIFY(commented.contains(QStringLiteral("last")));
        QVERIFY(commented.contains(QStringLiteral("&lt;b&gt;kept&lt;/b&gt;"))); // code keeps its text
        // The body takes the view's font, not the document's.
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
        // GitHub's feed: the releases page.
        QCOMPARE(WhatsNew::allReleaseNotesUrl(UpdateFeed::github()),
                 QUrl(QStringLiteral("https://github.com/altqx/hikari/releases")));
    }
};

QTEST_MAIN(WhatsNewTest)
#include "whats_new_tests.moc"
