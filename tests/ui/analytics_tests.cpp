// AN1 (#404): anonymous usage data against a fake PostHog endpoint on the
// loopback; nothing leaves the machine. What is sent, when nothing is (no
// key, the switch off, DO_NOT_TRACK), the queue's bounds offline, the switch
// dropping the queue and the id. (A crash's record: crash_report_tests.)

#include "analytics_endpoint.h"

#include "hikari/app/analytics.h"
#include "settings_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::Analytics;
using hikari::test::AnalyticsEndpoint;

class AnalyticsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    Analytics::Config config(const AnalyticsEndpoint &endpoint, const QString &key = QStringLiteral("phc_test"))
    {
        Analytics::Config c;
        c.key = key;
        c.endpoint = endpoint.url();
        c.queueFile = dir.filePath(QStringLiteral("analytics-queue.json"));
        c.version = QStringLiteral("0.1.0-beta.1");
        c.firstFlushMs = 60 * 60 * 1000; // the tests send
        return c;
    }
    static Analytics::StartFacts facts()
    {
        Analytics::StartFacts f;
        f.package = QStringLiteral("appimage");
        f.screens = 2;
        f.scale = 1.5;
        f.startupMs = 700;
        f.layout = QStringLiteral("Karaoke");
        return f;
    }

private slots:
    void init()
    {
        QFile::remove(dir.filePath(QStringLiteral("analytics-queue.json")));
    }

    // A fresh profile with a key and the switch on sends app_started: the
    // batch's shape, the anonymous id, no person profile, no GeoIP, and
    // only properties of the table.
    void sendsAppStarted()
    {
        AnalyticsEndpoint endpoint;
        QVERIFY(endpoint.listening());
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, config(endpoint));
        QVERIFY(analytics.active());
        analytics.appStarted(facts());
        QSignalSpy sent(&analytics, &Analytics::sent);
        analytics.flush();
        QVERIFY(sent.wait(5000));
        QCOMPARE(endpoint.requests.size(), 1);
        const AnalyticsEndpoint::Request &request = endpoint.requests.first();
        QCOMPARE(request.path, QStringLiteral("/batch/"));
        QCOMPARE(request.method, QStringLiteral("POST"));
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        QCOMPARE(body.value(QStringLiteral("api_key")).toString(), QStringLiteral("phc_test"));
        const QJsonArray batch = body.value(QStringLiteral("batch")).toArray();
        QCOMPARE(batch.size(), 1);
        const QJsonObject event = batch.first().toObject();
        QCOMPARE(event.value(QStringLiteral("event")).toString(), QStringLiteral("app_started"));
        const QJsonObject p = event.value(QStringLiteral("properties")).toObject();
        const QString id = p.value(QStringLiteral("distinct_id")).toString();
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")).match(id).hasMatch());
        QCOMPARE(event.value(QStringLiteral("distinct_id")).toString(), id);
        QCOMPARE(settings.text("privacy.usageDataId"), id);
        QCOMPARE(p.value(QStringLiteral("$process_person_profile")), QJsonValue(false));
        QCOMPARE(p.value(QStringLiteral("$geoip_disable")), QJsonValue(true));
        QCOMPARE(p.value(QStringLiteral("version")).toString(), QStringLiteral("0.1.0-beta.1"));
        QCOMPARE(p.value(QStringLiteral("channel")).toString(), QStringLiteral("prerelease"));
        QCOMPARE(p.value(QStringLiteral("package")).toString(), QStringLiteral("appimage"));
        QCOMPARE(p.value(QStringLiteral("layout")).toString(), QStringLiteral("karaoke"));
        QCOMPARE(p.value(QStringLiteral("screens")).toString(), QStringLiteral("2"));
        QCOMPARE(p.value(QStringLiteral("scale")).toString(), QStringLiteral("1.5x"));
        QCOMPARE(p.value(QStringLiteral("startup_time")).toString(), QStringLiteral("0.5-1s"));
        QCOMPARE(p.value(QStringLiteral("language")).toString(), QStringLiteral("en"));
        QCOMPARE(p.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
        QString why;
        QVERIFY2(AnalyticsEndpoint::closedOnly(batch, &why), qPrintable(why));
        QVERIFY(analytics.queued().isEmpty());
    }

    // No key (every build but a release's), the switch off, or DO_NOT_TRACK:
    // nothing is queued and nothing is sent.
    void nothingWithoutKeySwitchOrConsent_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("doNotTrack");
        QTest::newRow("no key") << QString() << true << false;
        QTest::newRow("switch off") << QStringLiteral("phc_test") << false << false;
        QTest::newRow("DO_NOT_TRACK") << QStringLiteral("phc_test") << true << true;
    }
    void nothingWithoutKeySwitchOrConsent()
    {
        QFETCH(QString, key);
        QFETCH(bool, enabled);
        QFETCH(bool, doNotTrack);
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", enabled);
        Analytics::Config c = config(endpoint, key);
        c.doNotTrack = doNotTrack;
        Analytics analytics(settings, c);
        QVERIFY(!analytics.active());
        analytics.appStarted(facts());
        analytics.commandUsed(QStringLiteral("GLOBAL_SAVE_SUBS"), QStringLiteral("key"));
        analytics.panelOpened(QStringLiteral("Audio"));
        analytics.flush();
        analytics.quit(200);
        QTest::qWait(200);
        QVERIFY(analytics.queued().isEmpty());
        QVERIFY(endpoint.requests.isEmpty());
        QVERIFY(!QFile::exists(c.queueFile));
        QVERIFY(settings.text("privacy.usageDataId").isEmpty());
    }

    void doNotTrackValues()
    {
        QVERIFY(!Analytics::doNotTrack(QByteArray()));
        QVERIFY(!Analytics::doNotTrack("0"));
        QVERIFY(Analytics::doNotTrack("1"));
        QVERIFY(Analytics::doNotTrack("yes"));
    }

    // Turning the switch off drops the queue (memory and file) and the id
    // and stops sending; turning it on again starts with a new id.
    void switchOffDropsQueueAndId()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        const Analytics::Config c = config(endpoint);
        Analytics analytics(settings, c);
        analytics.appStarted(facts());
        analytics.quit(0); // writes the file; the endpoint is not answered in time
        QTRY_VERIFY(QFile::exists(c.queueFile));
        const QString first = analytics.distinctId();
        QVERIFY(!first.isEmpty());
        QSignalSpy active(&analytics, &Analytics::activeChanged);
        settings.set("privacy.usageData", false);
        QCOMPARE(active.size(), 1);
        QVERIFY(!analytics.active());
        QVERIFY(analytics.queued().isEmpty());
        QVERIFY(!QFile::exists(c.queueFile));
        QVERIFY(settings.text("privacy.usageDataId").isEmpty());
        QVERIFY(!analytics.capture(QStringLiteral("getting_started_skipped")));
        settings.set("privacy.usageData", true);
        QVERIFY(analytics.active());
        QVERIFY(analytics.capture(QStringLiteral("getting_started_skipped")));
        const QString second = analytics.distinctId();
        QVERIFY(!second.isEmpty());
        QVERIFY(second != first);
    }

    // Offline: sends fail fast and wait for the next flush, the queue stays
    // within its bounds (oldest dropped), nothing waits on the network.
    void offlineQueueIsBounded()
    {
        quint16 closedPort = 0;
        {
            AnalyticsEndpoint gone;
            closedPort = gone.port();
        }
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics::Config c = config(endpoint);
        c.endpoint = QUrl(QStringLiteral("http://127.0.0.1:%1/batch/").arg(closedPort));
        Analytics analytics(settings, c);
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < Analytics::kMaxEvents + 500; ++i) {
            QVERIFY(analytics.capture(QStringLiteral("layout_changed"), {{QStringLiteral("layout"), QStringLiteral("timing")}}));
            if (i % 200 == 0)
                analytics.flush();
        }
        QVERIFY(clock.elapsed() < 20000);
        QCOMPARE(analytics.queued().size(), Analytics::kMaxEvents);
        QVERIFY(QJsonDocument(analytics.queued()).toJson(QJsonDocument::Compact).size() <= Analytics::kMaxBytes);
        clock.restart();
        analytics.flush();
        QVERIFY(clock.elapsed() < 100); // starts a send, never waits for it
        QTest::qWait(300);
        QCOMPARE(analytics.queued().size(), Analytics::kMaxEvents); // failed: kept for later
        analytics.quit(500);
        QVERIFY(clock.elapsed() < 3000);
        // The next start takes the file's queue up again.
        Analytics next(settings, c);
        QCOMPARE(next.queued().size(), Analytics::kMaxEvents);
    }

    // A send the endpoint answers later; the queue keeps what came meanwhile.
    void sendsInBatches()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, config(endpoint));
        for (int i = 0; i < Analytics::kBatchSize + 5; ++i)
            analytics.capture(QStringLiteral("layout_changed"), {{QStringLiteral("layout"), QStringLiteral("editing")}});
        QSignalSpy sent(&analytics, &Analytics::sent);
        analytics.flush();
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().first().toInt(), Analytics::kBatchSize);
        QCOMPARE(analytics.queued().size(), 5);
        analytics.flush();
        QVERIFY(sent.wait(5000));
        QVERIFY(analytics.queued().isEmpty());
        QCOMPARE(endpoint.requests.size(), 2);
    }

    // The table: anything outside it is dropped, free text above all.
    void onlyTheTableGoes()
    {
        QVERIFY(Analytics::conforms(QStringLiteral("panel_opened"), {{QStringLiteral("panel"), QStringLiteral("audio")}}));
        QVERIFY(!Analytics::conforms(QStringLiteral("panel_opened"), {{QStringLiteral("panel"), QStringLiteral("My panel")}}));
        QVERIFY(!Analytics::conforms(QStringLiteral("panel_opened"),
                                     {{QStringLiteral("panel"), QStringLiteral("audio")}, {QStringLiteral("file"), QStringLiteral("a.ass")}}));
        QVERIFY(!Analytics::conforms(QStringLiteral("something_else"), {}));
        QVERIFY(!Analytics::conforms(QStringLiteral("automation_error"),
                                     {{QStringLiteral("kind"), QStringLiteral("failed")},
                                      {QStringLiteral("script"), QStringLiteral("/home/me/My Macro.lua")}}));
        QVERIFY(Analytics::conforms(QStringLiteral("automation_error"),
                                    {{QStringLiteral("kind"), QStringLiteral("failed")}, {QStringLiteral("script"), QStringLiteral("user script")}}));
        QVERIFY(!Analytics::conforms(QStringLiteral("getting_started_finished"),
                                     {{QStringLiteral("pages_changed"), QJsonArray{QStringLiteral("look"), QStringLiteral("Japanese")}}}));
        // Only audio comes from a video (#453).
        const auto opened = [](const char *kind) {
            return QJsonObject{{QStringLiteral("open_time"), QStringLiteral("<250ms")},
                               {QStringLiteral("outcome"), QStringLiteral("ready")},
                               {QStringLiteral("kind"), QLatin1String(kind)}};
        };
        QVERIFY(Analytics::conforms(QStringLiteral("audio_opened"), opened("from_video")));
        QVERIFY(!Analytics::conforms(QStringLiteral("video_opened"), opened("from_video")));
        QVERIFY(Analytics::conforms(QStringLiteral("video_opened"), opened("file")));
        QVERIFY(Analytics::conforms(QStringLiteral("video_opened"), opened("dummy")));
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, config(endpoint));
        analytics.commandUsed(QStringLiteral("My macro: do things"), QStringLiteral("key")); // a script's macro
        analytics.commandUsed(QStringLiteral("GLOBAL_SAVE_SUBS"), QStringLiteral("menu"));   // no such way
        analytics.dialogOpened(QStringLiteral("someScript.lua/Settings"));
        analytics.panelOpened(QStringLiteral("NoSuchPanel"));
        analytics.layoutChanged(QStringLiteral("My layout"));
        QVERIFY(analytics.queued().isEmpty());
        // Once per command and way, panel and dialog in a session.
        analytics.commandUsed(QStringLiteral("GLOBAL_SAVE_SUBS"), QStringLiteral("key"));
        analytics.commandUsed(QStringLiteral("GLOBAL_SAVE_SUBS"), QStringLiteral("key"));
        analytics.commandUsed(QStringLiteral("GLOBAL_SAVE_SUBS"), QStringLiteral("pointer"));
        analytics.panelOpened(QStringLiteral("FixMinorErrors"));
        analytics.panelOpened(QStringLiteral("FixMinorErrors"));
        analytics.dialogOpened(QStringLiteral("styleManager"));
        analytics.dialogOpened(QStringLiteral("styleManager"));
        QCOMPARE(analytics.queued().size(), 4);
        QCOMPARE(analytics.queued().at(2).toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("panel")).toString(),
                 QStringLiteral("fix_minor_errors"));
        for (int i = 0; i < Analytics::kMaxLayoutChanges + 5; ++i)
            analytics.layoutChanged(QStringLiteral("saved"));
        QCOMPARE(analytics.queued().size(), 4 + Analytics::kMaxLayoutChanges);
        // A bundled script's name with a space still goes.
        analytics.automationError(QStringLiteral("failed"), QStringLiteral("skew gradient.lua"));
        QCOMPARE(analytics.queued().last().toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("script")).toString(),
                 QStringLiteral("skew_gradient.lua"));
    }

    void buckets()
    {
        QCOMPARE(Analytics::timeBucket(0), QStringLiteral("<250ms"));
        QCOMPARE(Analytics::timeBucket(250), QStringLiteral("250-500ms"));
        QCOMPARE(Analytics::timeBucket(1999), QStringLiteral("1-2s"));
        QCOMPARE(Analytics::timeBucket(3600000), QStringLiteral("60s+"));
        QCOMPARE(Analytics::sessionBucket(59), QStringLiteral("<1m"));
        QCOMPARE(Analytics::sessionBucket(3600), QStringLiteral("1-3h"));
        QCOMPARE(Analytics::sessionBucket(100000), QStringLiteral("3h+"));
        QCOMPARE(Analytics::scaleBucket(1.0), QStringLiteral("1x"));
        QCOMPARE(Analytics::scaleBucket(1.25), QStringLiteral("1.25x"));
        QCOMPARE(Analytics::scaleBucket(2.0), QStringLiteral("2x"));
        QCOMPARE(Analytics::scaleBucket(3.0), QStringLiteral("3x+"));
        QCOMPARE(Analytics::screensBucket(5), QStringLiteral("3+"));
        QCOMPARE(Analytics::frameBucket(4), 0);
        QCOMPARE(Analytics::frameBucket(20), 2);
        QCOMPARE(Analytics::frameBucket(500), 5);
    }

    // app_closed: the session's length and the frame-time buckets' counts.
    void appClosedCountsFrames()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, config(endpoint));
        analytics.frameRendered(3);
        analytics.frameRendered(500);
        // Frames while it is off are not counted, and turning it off drops
        // those before.
        settings.set("privacy.usageData", false);
        analytics.frameRendered(500);
        settings.set("privacy.usageData", true);
        analytics.frameRendered(3);
        analytics.frameRendered(3);
        analytics.frameRendered(40);
        analytics.quit(3000);
        QCOMPARE(endpoint.requests.size(), 1);
        const QJsonArray batch = QJsonDocument::fromJson(endpoint.requests.first().body).object().value(QStringLiteral("batch")).toArray();
        const QJsonObject p = batch.last().toObject().value(QStringLiteral("properties")).toObject();
        QCOMPARE(batch.last().toObject().value(QStringLiteral("event")).toString(), QStringLiteral("app_closed"));
        QCOMPARE(p.value(QStringLiteral("session_length")).toString(), QStringLiteral("<1m"));
        QCOMPARE(p.value(QStringLiteral("render_frames_lt8ms")).toInt(), 2);
        QCOMPARE(p.value(QStringLiteral("render_frames_33_66ms")).toInt(), 1);
        QCOMPARE(p.value(QStringLiteral("render_frames_200ms_plus")).toInt(), 0);
        QVERIFY(!QFile::exists(config(endpoint).queueFile)); // all sent
    }

    // A script's name goes only while its bytes are the bundled manifest's,
    // wherever it lies: one dropped into a writable program folder is the
    // user's.
    void bundledScriptsByTheManifest()
    {
        const QByteArray shipped = "-- bundled\n";
        const QString program = dir.filePath(QStringLiteral("program/Automation/automation/autoload"));
        QVERIFY(QDir().mkpath(program));
        auto write = [](const QString &file, const QByteArray &text) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(text);
        };
        const QString manifest = dir.filePath(QStringLiteral("program/bundled-files.txt"));
        write(manifest, QCryptographicHash::hash(shipped, QCryptographicHash::Sha256).toHex() +
                            " Automation/automation/autoload/skew gradient.lua\n");
        const QString bundled = program + QStringLiteral("/skew gradient.lua");
        write(bundled, shipped);
        QCOMPARE(Analytics::bundledScript(bundled, manifest), QStringLiteral("skew gradient.lua"));
        const QString added = program + QStringLiteral("/PrivateCustomer.lua");
        write(added, "-- mine\n");
        QCOMPARE(Analytics::bundledScript(added, manifest), QString());
        // Same bytes, another name; a bundled name, other bytes.
        write(added, shipped);
        QCOMPARE(Analytics::bundledScript(added, manifest), QString());
        write(bundled, "-- edited\n");
        QCOMPARE(Analytics::bundledScript(bundled, manifest), QString());
        QCOMPARE(Analytics::bundledScript(bundled, dir.filePath(QStringLiteral("no-manifest.txt"))), QString());
    }
};

QTEST_GUILESS_MAIN(AnalyticsTest)
#include "analytics_tests.moc"
