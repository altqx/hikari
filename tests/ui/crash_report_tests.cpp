// CR1 (#452): crash, freeze and issue reports, against a fake PostHog
// endpoint on the loopback; nothing leaves the machine. A forced crash and a
// forced freeze each leave a report the next start offers; Send works with
// usage data off and Don't send sends nothing; no subtitle text, path or
// file name is in a report; the GitHub links match the repository's issue
// forms and a long one goes to the clipboard.

#include "analytics_endpoint.h"

#include "hikari/app/analytics.h"
#include "hikari/app/crash_record.h"
#include "hikari/app/crash_report.h"
#include "hikari/app/crash_reports.h"
#include "log_controller.h"
#include "settings_store.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using hikari::app::Analytics;
using hikari::app::CrashReports;
using hikari::test::AnalyticsEndpoint;
namespace crash = hikari::app::crash;
namespace report = hikari::app::report;

namespace {

const QString kStart = QStringLiteral("start");

#ifdef _WIN32
const QString kProbeModule = QStringLiteral("hikari_crash_probe.exe");
#else
const QString kProbeModule = QStringLiteral("hikari_crash_probe");
#endif

// The probe, started by a relative path as from a shell.
void startProbe(QProcess &probe, const QString &record, const QString &how)
{
    const QFileInfo program(QStringLiteral(HIKARI_CRASH_PROBE));
    probe.setWorkingDirectory(program.absolutePath());
    probe.start(QStringLiteral("./") + program.fileName(), {record, how});
}

QStringList modulesOf(const crash::Record &record)
{
    QStringList modules;
    for (const auto &frame : record.frames)
        modules << frame.module;
    return modules;
}

QJsonObject propertiesOf(const QJsonArray &events, const QString &name)
{
    for (const QJsonValue &e : events)
        if (e.toObject().value(QStringLiteral("event")) == QJsonValue(name))
            return e.toObject().value(QStringLiteral("properties")).toObject();
    return {};
}

} // namespace

class CrashReportTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QList<QUrl> opened;
    QStringList copied;

    QString path(const char *name) const { return dir.filePath(QString::fromLatin1(name)); }

    Analytics::Config analyticsConfig(const QUrl &endpoint, const QString &key = QStringLiteral("phc_test")) const
    {
        Analytics::Config c;
        c.key = key;
        c.endpoint = endpoint;
        c.queueFile = path("analytics-queue.json");
        c.version = QStringLiteral("0.1.0-beta.1");
        c.firstFlushMs = 60 * 60 * 1000; // the tests send
        return c;
    }
    CrashReports::Config config()
    {
        CrashReports::Config c;
        c.crashRecord = path("crash-record.txt");
        c.freezeRecord = path("hang-record.txt");
        c.pendingFile = path("crash-report.json");
        c.logTail = path("log-tail.txt");
        c.version = QStringLiteral("0.1.0-beta.1");
        c.openUrl = [this](const QUrl &url) {
            opened << url;
            return true;
        };
        c.copy = [this](const QString &text) { copied << text; };
        return c;
    }
    void writeRecord(const char *file, const QByteArray &text)
    {
        QFile f(path(file));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }
    static QByteArray crashRecordText()
    {
        return "hikari-crash 1\nversion 0.1.0-beta.1\ntype SIGSEGV\nmechanism signal\n"
               "frame hikarisub 0x1f3a2c\nframe libQt6Core.so.6 0x2c41b8\nframe other 0x4d2a0\n";
    }
    // The fixture's private words: none may be in what goes.
    static QStringList secrets()
    {
        return {QStringLiteral("Secret Show"), QStringLiteral("Episode 03"), QStringLiteral("altq"),
                QStringLiteral("meet again at the station"), QStringLiteral("Kenta"), QStringLiteral("Signs"),
                QStringLiteral("Comic Hand"), QStringLiteral("/home/"), QStringLiteral("C:\\")};
    }
    // A Log as a session could leave it: paths, the open files' names, a
    // Line's text, a quoted font and quoted text.
    static void fillLog(hikari::ui::LogController &log)
    {
        log.log(QStringLiteral("Opened /home/altq/Secret Show/Episode 03.ass (412 lines)"));
        log.log(QStringLiteral("Opened C:\\Users\\altq\\Videos\\Episode 03 [1080p].mkv"));
        log.notice(hikari::ui::LogController::Warning, QStringLiteral("Fonts"),
                   QStringLiteral("Font \u201CComic Hand\u201D is not installed; using Liberation Sans"));
        log.log(QStringLiteral("Autosaved Signs.ass into /home/altq/.cache/HikariSub/autosave"));
        log.log(QStringLiteral("macro printed: We'll meet again at the station, Kenta"), hikari::ui::LogController::Info,
                QStringLiteral("Apply karaoke template"));
        log.log(QStringLiteral("Search: no match for \"Kenta\""));
        log.log(QStringLiteral("Video: 1920\u00D71080, 23.976 fps, 1/2 of the frames are keyframes"));
    }
    static void setKnown(CrashReports &reports)
    {
        reports.setKnown(
            [] {
                return QStringList{QStringLiteral("Episode 03.ass"), QStringLiteral("Signs.ass"),
                                   QStringLiteral("/home/altq/Videos/Episode 03 [1080p].mkv")};
            },
            [] {
                return QStringList{QStringLiteral("{\\an8}We'll meet again at the station, Kenta"),
                                   QStringLiteral("We'll meet again at the station, Kenta")};
            });
    }

private slots:
    void init()
    {
        for (const char *f : {"crash-record.txt", "hang-record.txt", "crash-report.json", "log-tail.txt",
                              "analytics-queue.json"})
            QFile::remove(path(f));
        opened.clear();
        copied.clear();
    }

    // A forced crash leaves a record of module + offset frames; the next
    // start offers it with the system, the settings and the last run's Log.
    void crashLeadsToReportAtNextStart_data()
    {
        QTest::addColumn<QString>("how");
        QTest::newRow("access violation") << QStringLiteral("segv");
        QTest::newRow("abort") << QStringLiteral("abort");
    }
    void crashLeadsToReportAtNextStart()
    {
        QFETCH(QString, how);
        QProcess probe;
        startProbe(probe, path("crash-record.txt"), how);
        QVERIFY(probe.waitForFinished(30000));
        QVERIFY(probe.exitStatus() == QProcess::CrashExit || probe.exitCode() != 0);
        const auto record = crash::read(path("crash-record.txt"));
        QVERIFY2(record, QFile::exists(path("crash-record.txt")) ? "unreadable" : "no record");
        QCOMPARE(record->version, QStringLiteral("0.1.0-test.1"));
#ifdef _WIN32
        QCOMPARE(record->type, how == QLatin1String("segv") ? QStringLiteral("EXCEPTION_ACCESS_VIOLATION") : QStringLiteral("SIGABRT"));
        QCOMPARE(record->mechanism, how == QLatin1String("segv") ? QStringLiteral("seh") : QStringLiteral("signal"));
#else
        QCOMPARE(record->type, how == QLatin1String("segv") ? QStringLiteral("SIGSEGV") : QStringLiteral("SIGABRT"));
        QCOMPARE(record->mechanism, QStringLiteral("signal"));
#endif
        QVERIFY2(modulesOf(*record).contains(kProbeModule), qPrintable(modulesOf(*record).join(u',')));
        writeRecord("log-tail.txt", "14:28:03 Info    Opened file1.ass\n14:32:07 Info    Visual tool: Perspective\n");

        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        QCOMPARE(reports.pending(), QStringLiteral("crash"));
        QVERIFY(!QFile::exists(path("crash-record.txt")));
        QVERIFY(QFile::exists(path("crash-report.json")));
        reports.started(QStringLiteral("appimage"), 1, 1.5, QStringLiteral("typesetting"));
        const QString text = reports.text(kStart, QStringLiteral("I had just switched tools."));
        QVERIFY2(text.contains(QStringLiteral("Note\n  I had just switched tools.")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Crash\n  ") + record->type));
        QVERIFY(text.contains(kProbeModule));
        QVERIFY(text.contains(QStringLiteral("System\n  HikariSub 0.1.0-beta.1 (prerelease) · appimage")));
        QVERIFY(text.contains(QStringLiteral("Settings\n  theme")));
        QVERIFY(text.contains(QStringLiteral("14:32:07 Info    Visual tool: Perspective")));
        // Usage data is on: the record alone is queued at start, as AN1 sent it.
        const QJsonObject exception = propertiesOf(analytics.queued(), QStringLiteral("$exception"));
        QCOMPARE(exception.value(QStringLiteral("crashed_version")).toString(), QStringLiteral("0.1.0-test.1"));
        QCOMPARE(exception.value(QStringLiteral("$exception_level")).toString(), QStringLiteral("fatal"));
        QString why;
        QVERIFY2(AnalyticsEndpoint::closedOnly(analytics.queued(), &why), qPrintable(why));
        const QByteArray payload = QJsonDocument(analytics.queued()).toJson();
        QVERIFY(!payload.contains(QFileInfo(QStringLiteral(HIKARI_CRASH_PROBE)).absolutePath().toUtf8()));

        // The window shows again until it is answered.
        CrashReports again(settings, analytics, log, config());
        QCOMPARE(again.pending(), QStringLiteral("crash"));
    }

    // A forced freeze: the watchdog records the window thread's stack
    // after its threshold and drops the record when the thread answers.
    void freezeIsRecordedAndRecovered()
    {
        QProcess probe;
        startProbe(probe, path("hang-record.txt"), QStringLiteral("hang"));
        QVERIFY(probe.waitForFinished(30000));
        const QString out = QString::fromLocal8Bit(probe.readAllStandardOutput());
        QVERIFY2(probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0,
                 qPrintable(QStringLiteral("exit %1: %2 %3").arg(probe.exitCode()).arg(out, probe.readAllStandardError())));
        const QStringList parts = out.trimmed().split(u' ');
        QCOMPARE(parts.value(0), QStringLiteral("recovered"));
        QVERIFY2(parts.value(1).toInt() >= 1, qPrintable(out));
        QVERIFY2(parts.value(2).split(u',').contains(kProbeModule), qPrintable(out));
        QVERIFY(!QFile::exists(path("hang-record.txt")));
    }

    // A freeze the app is closed in leaves its record; the next start
    // offers it as a freeze.
    void freezeKilledLeadsToReportAtNextStart()
    {
        QProcess probe;
        startProbe(probe, path("hang-record.txt"), QStringLiteral("hang-kill"));
        QVERIFY(probe.waitForStarted(30000));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path("hang-record.txt")), 20000);
        QTest::qWait(300); // the record is written whole (QSaveFile)
        probe.kill();
        QVERIFY(probe.waitForFinished(10000));
        const auto record = crash::read(path("hang-record.txt"));
        QVERIFY(record);
        QCOMPARE(record->type, QStringLiteral("APP_HANG"));
        QCOMPARE(record->mechanism, QStringLiteral("watchdog"));
        QVERIFY(record->freeze());
        QVERIFY2(modulesOf(*record).contains(kProbeModule), qPrintable(modulesOf(*record).join(u',')));

        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        QCOMPARE(reports.pending(), QStringLiteral("freeze"));
        reports.started(QStringLiteral("deb"), 1, 1, QStringLiteral("editing"));
        QVERIFY(reports.text(kStart, {}).startsWith(QStringLiteral("Freeze\n  The window did not respond for ")));
        QVERIFY(analytics.queued().isEmpty()); // usage data off: nothing at start
    }

    // Send report with usage data off: the record and the report go once,
    // under an id kept nowhere, holding what the window showed.
    void sendWorksWithUsageDataOff()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        QVERIFY(!analytics.active());
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("installer"), 2, 1.25, QStringLiteral("timing"));
        QVERIFY(analytics.queued().isEmpty());
        QVERIFY(reports.canSend());
        QSignalSpy sent(&reports, &CrashReports::sent);
        const QString note = QStringLiteral("It closed when I pressed Play.");
        QVERIFY(reports.send(kStart, note));
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(1).toBool(), true);
        QCOMPARE(endpoint.requests.size(), 1);
        const QJsonArray events = endpoint.events();
        QCOMPARE(events.size(), 2);
        const QJsonObject exception = propertiesOf(events, QStringLiteral("$exception"));
        const QJsonObject crashReport = propertiesOf(events, QStringLiteral("crash_report"));
        QVERIFY(!exception.isEmpty());
        QCOMPARE(crashReport.value(QStringLiteral("note")).toString(), note);
        QCOMPARE(crashReport.value(QStringLiteral("kind")).toString(), QStringLiteral("crash"));
        QCOMPARE(crashReport.value(QStringLiteral("report_id")), exception.value(QStringLiteral("report_id")));
        QCOMPARE(crashReport.value(QStringLiteral("package")).toString(), QStringLiteral("installer"));
        QCOMPARE(crashReport.value(QStringLiteral("$process_person_profile")).toBool(true), false);
        // No id is kept, and nothing else is queued.
        QVERIFY(settings.text("privacy.usageDataId").isEmpty());
        QVERIFY(analytics.queued().isEmpty());
        QCOMPARE(reports.pending(), QString());
        QVERIFY(!QFile::exists(path("crash-report.json")));
        for (const auto &[event, properties] : {std::pair{QStringLiteral("$exception"), exception},
                                                std::pair{QStringLiteral("crash_report"), crashReport}}) {
            QJsonObject own = properties;
            for (const char *common : {"distinct_id", "$process_person_profile", "$geoip_disable", "$lib", "$lib_version"})
                own.remove(QString::fromLatin1(common));
            QVERIFY2(Analytics::conforms(event, own), qPrintable(event));
        }
    }

    // Usage data on: the $exception went at start, so Send adds only crash_report.
    void sendAfterExceptionAddsOnlyTheReport()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const QString id = propertiesOf(analytics.queued(), QStringLiteral("$exception")).value(QStringLiteral("report_id")).toString();
        QVERIFY(!id.isEmpty());
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(kStart, {}));
        QVERIFY(sent.wait(5000));
        const QJsonArray events = endpoint.events();
        QCOMPARE(events.size(), 1);
        QCOMPARE(propertiesOf(events, QStringLiteral("crash_report")).value(QStringLiteral("report_id")).toString(), id);
        QVERIFY(!propertiesOf(events, QStringLiteral("crash_report")).contains(QStringLiteral("note")));
    }

    // Don't send: nothing goes, with usage data off, and the report is gone.
    void dontSendSendsNothing()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        {
            CrashReports reports(settings, analytics, log, config());
            reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
            reports.dismiss(kStart);
            QCOMPARE(reports.pending(), QString());
        }
        QTest::qWait(500);
        QVERIFY(endpoint.requests.isEmpty());
        QVERIFY(!QFile::exists(path("crash-report.json")));
        CrashReports next(settings, analytics, log, config());
        QCOMPARE(next.pending(), QString());
        QVERIFY(opened.isEmpty());
    }

    // A send that fails keeps the report for the next start; nothing tries again.
    void failedSendKeepsTheReport()
    {
        writeRecord("crash-record.txt", crashRecordText());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        quint16 port = 0;
        {
            AnalyticsEndpoint closed; // a port nothing listens on any more
            port = closed.port();
        }
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:%1/batch/").arg(port))));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(kStart, {}));
        QVERIFY(sent.wait(10000));
        QCOMPARE(sent.first().at(1).toBool(), false);
        QCOMPARE(reports.pending(), QStringLiteral("crash"));
        QVERIFY(QFile::exists(path("crash-report.json")));
    }

    // A note or a Log longer than the event's fields take still sends: the
    // note cut at 4,000 characters, the Log at its last lines within 64 KB,
    // a long line cut. The window shows what goes.
    void longNoteAndLogStillSend()
    {
        writeRecord("crash-record.txt", crashRecordText());
        writeRecord("log-tail.txt", (QByteArray("14:00:00 Info    ") + QByteArray(2000, 'x') + '\n').repeated(200));
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const QString note = QString(5000, u'n');
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(kStart, note));
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(1).toBool(), true);
        const QJsonObject crashReport = propertiesOf(endpoint.events(), QStringLiteral("crash_report"));
        QCOMPARE(crashReport.value(QStringLiteral("note")).toString().size(), report::kNoteChars);
        const QString logField = crashReport.value(QStringLiteral("log")).toString();
        QVERIFY(logField.size() <= report::kLogChars);
        QVERIFY(logField.size() > report::kLogChars - report::kLogLineChars - 1);
        for (const QString &line : logField.split(u'\n'))
            QVERIFY(line.size() <= report::kLogLineChars);
        QVERIFY(reports.sentText().contains(QString(report::kNoteChars, u'n')));
        QVERIFY(!reports.sentText().contains(QString(report::kNoteChars + 1, u'n')));
    }

    // A freeze recovered while an earlier one's report is on its way is
    // kept: the answer is the earlier one's.
    void newerFreezeOutlivesASend()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const auto freeze = [](int seconds) {
            return crash::Record{QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0-beta.1"),
                                 {{QStringLiteral("hikarisub"), 0x31c0a8}}, seconds};
        };
        reports.freezeRecovered(freeze(12));
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(QStringLiteral("freeze"), {}));
        reports.freezeRecovered(freeze(30));
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(1).toBool(), true);
        QVERIFY(reports.freezeOffered());
        QCOMPARE(reports.seconds(QStringLiteral("freeze")), 30);
        QCOMPARE(endpoint.events().size(), 2);
        QCOMPARE(propertiesOf(endpoint.events(), QStringLiteral("$exception")).value(QStringLiteral("freeze_seconds")).toInt(), 12);
    }

    // Usage data turned off drops the $exception it had queued: Send report
    // then sends the record again.
    void usageDataOffResendsTheRecord()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        QVERIFY(!propertiesOf(analytics.queued(), QStringLiteral("$exception")).isEmpty());
        settings.set("privacy.usageData", false);
        QVERIFY(analytics.queued().isEmpty());
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(kStart, {}));
        QVERIFY(sent.wait(5000));
        QCOMPARE(endpoint.requests.size(), 1);
        QCOMPARE(endpoint.events().size(), 2);
        QVERIFY(!propertiesOf(endpoint.events(), QStringLiteral("$exception")).isEmpty());
    }

    // The report's files go in a folder of the user's alone, whatever the
    // umask, and never through a link.
    void reportFilesAreTheUsersAlone()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix permissions");
#else
        writeRecord("crash-record.txt", crashRecordText());
        CrashReports::Config c = config();
        c.pendingFile = path("reports/crash-report.json");
        c.logTail = path("reports/log-tail.txt");
        c.attachments = path("linked");
        QTemporaryDir elsewhere;
        QVERIFY(QFile::link(elsewhere.path(), path("linked")));
        writeRecord("Episode 03.ass", "[Script Info]\n");
        // A cache folder others can write to is closed to them.
        QVERIFY(QFile::setPermissions(dir.path(), QFile::permissions(dir.path()) | QFileDevice::WriteGroup | QFileDevice::WriteOther));
        const mode_t was = ::umask(0);
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:9/batch/"))));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, c);
        reports.setCurrentFile([this] { return path("Episode 03.ass"); });
        log.log(QStringLiteral("a line of the Log"));
        QTRY_VERIFY(QFile::exists(path("reports/log-tail.txt")));
        reports.beginIssue();
        const QString zip = reports.zipSubtitleFile();
        ::umask(was);
        QVERIFY(QFile::exists(path("reports/crash-report.json")));
        QCOMPARE(QFileInfo(path("reports")).permissions() & ~(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ExeUser),
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(!(QFile::permissions(dir.path()) & (QFileDevice::WriteGroup | QFileDevice::WriteOther)));
        QVERIFY(zip.isEmpty());
        QVERIFY(QDir(elsewhere.path()).isEmpty());
        QFile::remove(path("linked"));
        QFile::remove(path("Episode 03.ass"));
        QDir(path("reports")).removeRecursively();
#endif
    }

    // A Log tail or a kept report that is a link is not read: another
    // account's file never goes into a report.
    void linkedReportFilesAreNotRead()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix links");
#else
        QTemporaryDir elsewhere;
        QFile secret(elsewhere.filePath(QStringLiteral("secret.txt")));
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.write("14:00:00 Info    Secret Show\n");
        secret.close();
        QVERIFY(QFile::link(secret.fileName(), path("log-tail.txt")));
        writeRecord("crash-record.txt", crashRecordText());
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:9/batch/"))));
        hikari::ui::LogController log;
        {
            CrashReports reports(settings, analytics, log, config());
            QCOMPARE(reports.pending(), QStringLiteral("crash"));
            QVERIFY(!reports.text(kStart, {}).contains(QStringLiteral("Secret Show")));
        }
        QFile::remove(path("crash-report.json"));
        QVERIFY(QFile::link(secret.fileName(), path("crash-report.json")));
        CrashReports next(settings, analytics, log, config());
        QCOMPARE(next.pending(), QString());
        QFile::remove(path("crash-report.json"));
#endif
    }

    // A freeze report that fails while the start's report still waits is
    // kept for this run, and the failure names it.
    void failedFreezeSendIsKept()
    {
        writeRecord("crash-record.txt", crashRecordText());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        quint16 port = 0;
        {
            AnalyticsEndpoint closed;
            port = closed.port();
        }
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:%1/batch/").arg(port))));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        reports.freezeRecovered({QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0-beta.1"),
                                 {{QStringLiteral("hikarisub"), 0x31c0a8}}, 12});
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(QStringLiteral("freeze"), {}));
        QVERIFY(sent.wait(10000));
        QCOMPARE(sent.first().at(0).toString(), QStringLiteral("freeze"));
        QCOMPARE(sent.first().at(1).toBool(), false);
        QVERIFY(reports.freezeOffered());
        QCOMPARE(reports.seconds(QStringLiteral("freeze")), 12);
        QCOMPARE(reports.pending(), QStringLiteral("crash"));
    }

    // "Always send" with a send still on its way: the newer freeze gets the notice.
    void alwaysSendBusyOffersTheFreeze()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        settings.set(CrashReports::kAlwaysSend, true);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const auto freeze = [](int seconds) {
            return crash::Record{QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0-beta.1"),
                                 {{QStringLiteral("hikarisub"), 0x31c0a8}}, seconds};
        };
        QSignalSpy noticed(&reports, &CrashReports::freezeNoticed);
        QSignalSpy autoSent(&reports, &CrashReports::autoSent);
        reports.freezeRecovered(freeze(12));
        QVERIFY(reports.sending());
        QCOMPARE(noticed.size(), 0);
        reports.freezeRecovered(freeze(30));
        QCOMPARE(noticed.size(), 1);
        QCOMPARE(noticed.first().at(0).toInt(), 30);
        QVERIFY(autoSent.wait(5000));
        QVERIFY(reports.freezeOffered());
        QCOMPARE(reports.seconds(QStringLiteral("freeze")), 30);
    }

    // Report an issue shows and sends the Log and the file as the form
    // opened, whatever comes after.
    void issueTakesWhatTheFormShowed()
    {
        writeRecord("first.ass", "[Script Info]\n");
        writeRecord("second.ass", "[Script Info]\n");
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:9/batch/"))));
        hikari::ui::LogController log;
        CrashReports::Config c = config();
        c.attachments = path("issue");
        CrashReports reports(settings, analytics, log, c);
        QString current = path("first.ass");
        reports.setCurrentFile([&current] { return current; });
        log.log(QStringLiteral("a line before"));
        reports.beginIssue();
        current = path("second.ass");
        log.log(QStringLiteral("a line after"));
        QCOMPARE(reports.subtitleFileName(), QStringLiteral("first.ass"));
        QVERIFY(reports.issueLogText().contains(QStringLiteral("a line before")));
        QVERIFY(!reports.issueLogText().contains(QStringLiteral("a line after")));
        const QString md = reports.issueMarkdown(QStringLiteral("bug"), QStringLiteral("A bug"),
                                                 {{QStringLiteral("what-happened"), QStringLiteral("It froze")}}, true);
        QVERIFY(!md.contains(QStringLiteral("a line after")));
        QCOMPARE(QFileInfo(reports.zipSubtitleFile()).fileName(), QStringLiteral("first.zip"));
        reports.discardAttachments();
        QFile::remove(path("first.ass"));
        QFile::remove(path("second.ass"));
    }

    // A build without a key can't send; Send report does nothing.
    void noKeyNoSend()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url(), QString()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        QVERIFY(!reports.canSend());
        QVERIFY(!reports.send(kStart, {}));
        QTest::qWait(300);
        QVERIFY(endpoint.requests.isEmpty());
    }

    // "Always send crash reports": the start's report goes by itself, without a note.
    void alwaysSendSendsAtStart()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        settings.set(CrashReports::kAlwaysSend, true);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        QSignalSpy autoSent(&reports, &CrashReports::autoSent);
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        QVERIFY(autoSent.wait(5000));
        QCOMPARE(autoSent.first().at(0).toString(), QStringLiteral("crash"));
        QCOMPARE(autoSent.first().at(1).toBool(), true);
        QCOMPARE(endpoint.events().size(), 2);
        QVERIFY(!propertiesOf(endpoint.events(), QStringLiteral("crash_report")).contains(QStringLiteral("note")));
        QCOMPARE(reports.pending(), QString());
    }

    // A freeze the window came back from is offered (the notice), and with
    // usage data on its record is queued as an error.
    void recoveredFreezeIsOffered()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        QSignalSpy noticed(&reports, &CrashReports::freezeNoticed);
        reports.freezeRecovered({QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0-beta.1"),
                                 {{QStringLiteral("hikarisub"), 0x31c0a8}}, 12});
        QCOMPARE(noticed.size(), 1);
        QCOMPARE(noticed.first().at(0).toInt(), 12);
        QVERIFY(reports.freezeOffered());
        QCOMPARE(reports.seconds(QStringLiteral("freeze")), 12);
        const QJsonObject exception = propertiesOf(analytics.queued(), QStringLiteral("$exception"));
        QCOMPARE(exception.value(QStringLiteral("$exception_level")).toString(), QStringLiteral("error"));
        QCOMPARE(exception.value(QStringLiteral("freeze_seconds")).toInt(), 12);
        QString why;
        QVERIFY2(AnalyticsEndpoint::closedOnly(analytics.queued(), &why), qPrintable(why));
        reports.dismiss(QStringLiteral("freeze"));
        QVERIFY(!reports.freezeOffered());
    }

    // No subtitle text, path or file name is in a report: not in the text
    // the window shows, not in what Send sends, not in the GitHub issue, and
    // not in the Log tail kept on disk.
    void noSubtitleTextPathOrFileName()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        setKnown(reports);
        fillLog(log);
        const QString tail = reports.logTail().join(u'\n');
        QVERIFY2(tail.contains(QStringLiteral("Opened file1.ass (412 lines)")), qPrintable(tail));
        QVERIFY2(tail.contains(QStringLiteral("Opened file2.mkv")), qPrintable(tail));
        QVERIFY2(tail.contains(QStringLiteral("Font \u201C\u2039font\u203A\u201D is not installed")), qPrintable(tail));
        QVERIFY2(tail.contains(QStringLiteral("Autosaved file3.ass into \u2039folder\u203A")), qPrintable(tail));
        QVERIFY2(tail.contains(QStringLiteral("1/2 of the frames")), qPrintable(tail)); // a fraction is no path
        QTRY_VERIFY(QFile::exists(path("log-tail.txt")));
        QFile kept(path("log-tail.txt"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        const QString onDisk = QString::fromUtf8(kept.readAll());
        kept.close();

        // The next start's report holds that Log.
        writeRecord("crash-record.txt", crashRecordText());
        CrashReports next(settings, analytics, log, config());
        next.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const QString text = next.text(kStart, {});
        QVERIFY2(text.contains(QStringLiteral("Opened file1.ass")), qPrintable(text));
        QSignalSpy sent(&next, &CrashReports::sent);
        const QVariantMap issueFields{{QStringLiteral("what-happened"), QStringLiteral("It froze")}};
        reports.beginIssue();
        const QString issue = reports.issueMarkdown(QStringLiteral("bug"), QStringLiteral("A bug"), issueFields, true);
        QVERIFY(issue.contains(QStringLiteral("Opened file1.ass")));
        QVERIFY(next.send(kStart, {}));
        QVERIFY(sent.wait(5000));
        const QString payload = QString::fromUtf8(QJsonDocument(endpoint.events()).toJson());
        QVERIFY(payload.contains(QStringLiteral("file1.ass")));
        for (const QString &where : {text, payload, issue, onDisk, tail})
            for (const QString &secret : secrets())
                QVERIFY2(!where.contains(secret, Qt::CaseInsensitive),
                         qPrintable(QStringLiteral("\u201C%1\u201D in:\n%2").arg(secret, where)));
    }

    // The scrubber's rules one by one.
    void scrubberRules_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("unix path") << QStringLiteral("Saved /home/me/My Subs/ep 1.ass") << QStringLiteral("Saved file1.ass");
        QTest::newRow("windows path") << QStringLiteral("Read D:\\Anime\\Show\\op.mkv now") << QStringLiteral("Read file1.mkv now");
        QTest::newRow("unc path") << QStringLiteral("Read \\\\nas\\share\\a.wav") << QStringLiteral("Read file1.wav");
        QTest::newRow("file url") << QStringLiteral("Open file:///home/me/x.ass") << QStringLiteral("Open file1.ass");
        QTest::newRow("folder") << QStringLiteral("Folder /home/me/stuff is empty") << QStringLiteral("Folder \u2039folder\u203A is empty");
        QTest::newRow("home folder") << QStringLiteral("In ~/Subs") << QStringLiteral("In \u2039folder\u203A");
        QTest::newRow("bare name") << QStringLiteral("Loaded karaoke.lua") << QStringLiteral("Loaded file1.lua");
        QTest::newRow("same name twice") << QStringLiteral("a.ass then A.ASS") << QStringLiteral("file1.ass then file1.ass");
        QTest::newRow("quoted") << QStringLiteral("No match for \u201Chello there\u201D") << QStringLiteral("No match for \u201C\u2039text\u203A\u201D");
        QTest::newRow("quoted font") << QStringLiteral("Font 'Arial Black' missing") << QStringLiteral("Font '\u2039font\u203A' missing");
        QTest::newRow("apostrophe") << QStringLiteral("Can't open it") << QStringLiteral("Can't open it");
        QTest::newRow("fraction") << QStringLiteral("1/2 done") << QStringLiteral("1/2 done");
        QTest::newRow("version") << QStringLiteral("Qt 6.11.2 ready") << QStringLiteral("Qt 6.11.2 ready");
        QTest::newRow("url") << QStringLiteral("See https://example.org/x") << QStringLiteral("See https://example.org/x");
    }
    void scrubberRules()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        report::Scrubber scrubber;
        QCOMPARE(scrubber.scrub(in), out);
    }

    // Open GitHub issue: a Bug issue with the report; too long for a link,
    // the text goes to the clipboard and the blank form opens.
    void githubIssueFromReport()
    {
        writeRecord("crash-record.txt", crashRecordText());
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        writeRecord("log-tail.txt", QByteArray("14:00:00 Info    a line of the Log\n").repeated(200));
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const QVariantMap result = reports.openOnGitHub(kStart, QStringLiteral("It closed."));
        QCOMPARE(result.value(QStringLiteral("complete")).toBool(), false);
        QCOMPARE(opened.size(), 1);
        const QUrlQuery query(opened.first());
        QCOMPARE(query.queryItemValue(QStringLiteral("template")), QStringLiteral("bug.yml"));
        QCOMPARE(query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded),
                 QStringLiteral("Crash: SIGSEGV in 0.1.0-beta.1"));
        QVERIFY(!query.hasQueryItem(QStringLiteral("log")));
        QCOMPARE(copied.size(), 1);
        QVERIFY(copied.first().contains(QStringLiteral("### What happened\nIt closed.")));
        QVERIFY(copied.first().contains(QStringLiteral("### Crash or freeze report\n```\nCrash\n  SIGSEGV")));
        QVERIFY(copied.first().contains(QStringLiteral("a line of the Log")));
        QCOMPARE(reports.pending(), QString()); // taken to GitHub: done
        QTest::qWait(200);
        QVERIFY(endpoint.requests.isEmpty());
    }

    // No browser opened: the issue is on the clipboard and the report kept.
    void githubWithoutBrowserKeepsTheReport()
    {
        writeRecord("crash-record.txt", crashRecordText());
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:9/batch/"))));
        hikari::ui::LogController log;
        CrashReports::Config c = config();
        c.openUrl = [this](const QUrl &url) {
            opened << url;
            return false;
        };
        CrashReports reports(settings, analytics, log, c);
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        const QVariantMap result = reports.openOnGitHub(kStart, QStringLiteral("It closed."));
        QCOMPARE(result.value(QStringLiteral("opened")).toBool(), false);
        QCOMPARE(result.value(QStringLiteral("complete")).toBool(), true);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(copied.size(), 1);
        QVERIFY(copied.first().contains(QStringLiteral("### What happened\nIt closed.")));
        QCOMPARE(reports.pending(), QStringLiteral("crash"));
        QVERIFY(QFile::exists(path("crash-report.json")));
    }

    // Help › Report an issue: each kind's link fills the matching issue
    // form, its field ids being the form's own; Copy gives the Markdown.
    void issueLinksMatchIssueForms_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("form");
        QTest::addColumn<QStringList>("ids");
        QTest::newRow("bug") << QStringLiteral("bug") << QStringLiteral("bug.yml")
                             << QStringList{QStringLiteral("what-happened"), QStringLiteral("steps"),
                                            QStringLiteral("expected"), QStringLiteral("system"), QStringLiteral("log")};
        QTest::newRow("feature") << QStringLiteral("feature") << QStringLiteral("feature.yml")
                                 << QStringList{QStringLiteral("wanted"), QStringLiteral("why"), QStringLiteral("system")};
        QTest::newRow("question") << QStringLiteral("question") << QStringLiteral("question.yml")
                                  << QStringList{QStringLiteral("question"), QStringLiteral("system")};
    }
    void issueLinksMatchIssueForms()
    {
        QFETCH(QString, kind);
        QFETCH(QString, form);
        QFETCH(QStringList, ids);
        QFile yml(QStringLiteral(HIKARI_SOURCE_DIR "/.github/ISSUE_TEMPLATE/") + form);
        QVERIFY2(yml.open(QIODevice::ReadOnly), qPrintable(yml.fileName()));
        const QString formText = QString::fromUtf8(yml.readAll());
        QStringList formIds;
        static const QRegularExpression idLine(QStringLiteral("^\\s+id: ([a-z-]+)$"), QRegularExpression::MultilineOption);
        for (auto it = idLine.globalMatch(formText); it.hasNext();)
            formIds << it.next().captured(1);

        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        log.log(QStringLiteral("Opened file1.ass"));
        reports.beginIssue();
        QVariantMap fields;
        for (const QString &id : ids)
            fields.insert(id, QStringLiteral("text for ") + id);
        const QVariantMap result = reports.openIssue(kind, QStringLiteral("A title"), fields, true);
        QVERIFY(result.value(QStringLiteral("complete")).toBool());
        QCOMPARE(opened.size(), 1);
        const QUrl url = opened.first();
        QCOMPARE(url.toString(QUrl::RemoveQuery), QStringLiteral("https://github.com/altqx/hikari/issues/new"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("template")), form);
        QCOMPARE(query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), QStringLiteral("A title"));
        for (const auto &[key, value] : query.queryItems()) {
            if (key == QLatin1String("template") || key == QLatin1String("title"))
                continue;
            QVERIFY2(formIds.contains(key), qPrintable(key + QStringLiteral(" not in ") + form));
            QVERIFY2(ids.contains(key), qPrintable(key));
        }
        for (const QString &id : ids)
            QVERIFY2(query.hasQueryItem(id), qPrintable(id));
        QVERIFY(query.queryItemValue(QStringLiteral("system"), QUrl::FullyDecoded).startsWith(QStringLiteral("System\n  HikariSub ")));
        QVERIFY(copied.isEmpty());
        reports.copyIssue(kind, QStringLiteral("A title"), fields, true);
        QCOMPARE(copied.size(), 1);
        QVERIFY(copied.first().startsWith(QStringLiteral("## A title\n\n### ")));
    }

    // A record that could carry a path or text is no record; a freeze's
    // seconds go only with a freeze.
    void recordReadsOnlyItsForm()
    {
        const auto check = [&](const QByteArray &text) {
            writeRecord("crash-record.txt", text);
            return bool(crash::read(path("crash-record.txt")));
        };
        QVERIFY(check("hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism signal\nframe HikariSub 0x1a2b\n"));
        QVERIFY(!check("hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism signal\nframe /home/me/lib.so 0x1a2b\n"));
        QVERIFY(!check("hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism signal\nnote hello\n"));
        QVERIFY(!check("hikari-crash 1\nversion 0.1.0\ntype my words\nmechanism signal\n"));
        QVERIFY(check("hikari-crash 1\nversion 0.1.0\ntype APP_HANG\nmechanism watchdog\nseconds 12\nframe hikarisub 0x1\n"));
        QVERIFY(!check("hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism signal\nseconds 12\n"));
        QVERIFY(!check("hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism watchdog\n"));
        QVERIFY(!check("hikari-crash 1\nversion 0.1.0\ntype APP_HANG\nmechanism watchdog\nseconds twelve\n"));
        // What write() writes, read() takes.
        const crash::Record freeze{QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0"),
                                   {{QStringLiteral("hikarisub"), 0x31c0a8}, {QStringLiteral("other"), 0}}, 40};
        QVERIFY(crash::write(path("hang-record.txt"), freeze));
        const auto back = crash::read(path("hang-record.txt"));
        QVERIFY(back);
        QCOMPARE(back->seconds, 40);
        QCOMPARE(back->frames.size(), 2);
        QCOMPARE(back->frames.first().offset, quint64(0x31c0a8));
    }
};

QTEST_MAIN(CrashReportTest)
#include "crash_report_tests.moc"
