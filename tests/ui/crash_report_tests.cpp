#include "analytics_endpoint.h"

#include "hikari/app/analytics.h"
#include "hikari/app/crash_record.h"
#include "hikari/app/crash_report.h"
#include "hikari/app/crash_reports.h"
#include "log_controller.h"
#include "settings_store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QUuid>
#include <QtTest>

#include <algorithm>

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
const crash::Frame kNamed{QStringLiteral("hikarisub"), 0x1f3a2c, 0x400000, QStringLiteral("3a4bd4d0-ff5b-4b31-3a40-e1b9d72f2ec8")};

#ifdef _WIN32
const QString kProbeModule = QStringLiteral("hikari_crash_probe.exe");
#else
const QString kProbeModule = QStringLiteral("hikari_crash_probe");
#endif

void startProbe(QProcess &probe, const QString &record, const QString &how)
{
    const QFileInfo program(QStringLiteral(HIKARI_CRASH_PROBE));
    probe.setWorkingDirectory(program.absolutePath());
    // Windows resolves a relative program against this process's folder, not the child's working directory.
    const QString here = QDir::currentPath();
    QDir::setCurrent(program.absolutePath());
    probe.start(QStringLiteral("./") + program.fileName(), {record, how});
    QDir::setCurrent(here);
}

#ifdef Q_OS_LINUX
QString debugIdFromReadelf(const QString &file)
{
    QProcess readelf;
    readelf.start(QStringLiteral("readelf"), {QStringLiteral("-n"), file});
    if (!readelf.waitForFinished(10000))
        return {};
    static const QRegularExpression buildId(QStringLiteral("Build ID: ([0-9a-f]+)"));
    QByteArray bytes = QByteArray::fromHex(buildId.match(QString::fromLatin1(readelf.readAllStandardOutput())).captured(1).toLatin1());
    if (bytes.isEmpty())
        return {};
    bytes = bytes.leftJustified(16, '\0', true);
    std::reverse(bytes.begin(), bytes.begin() + 4);
    std::reverse(bytes.begin() + 4, bytes.begin() + 6);
    std::reverse(bytes.begin() + 6, bytes.begin() + 8);
    const QString hex = QString::fromLatin1(bytes.toHex());
    return QStringList{hex.left(8), hex.mid(8, 4), hex.mid(12, 4), hex.mid(16, 4), hex.mid(20)}.join(u'-');
}
#endif

void verifyProbeHasDebugId(const crash::Record &record)
{
    const auto probe = std::find_if(record.frames.begin(), record.frames.end(),
                                    [](const crash::Frame &f) { return f.module == kProbeModule; });
    QVERIFY(probe != record.frames.end());
#ifdef Q_OS_LINUX
    QVERIFY(probe->base != 0);
    QCOMPARE(probe->debugId, debugIdFromReadelf(QStringLiteral(HIKARI_CRASH_PROBE)));
#else
    if (!probe->debugId.isEmpty())
        QVERIFY2(crash::debugIdPattern().match(probe->debugId).hasMatch(), qPrintable(probe->debugId));
#endif
    for (const crash::Frame &frame : record.frames)
        if (frame.module == QLatin1String("other"))
            QVERIFY(frame.debugId.isEmpty() && frame.base == 0);
}

QStringList modulesOf(const crash::Record &record)
{
    QStringList modules;
    for (const auto &frame : record.frames)
        modules << frame.module;
    return modules;
}

// Other programs on the machine may knock on the loopback port too (a GET /): count the POSTs only.
qsizetype posts(const AnalyticsEndpoint &endpoint)
{
    return std::count_if(endpoint.requests.begin(), endpoint.requests.end(),
                         [](const AnalyticsEndpoint::Request &r) { return r.method == QLatin1String("POST"); });
}

QJsonObject propertiesOf(const QJsonArray &events, const QString &name)
{
    for (const QJsonValue &e : events)
        if (e.toObject().value(QStringLiteral("event")) == QJsonValue(name))
            return e.toObject().value(QStringLiteral("properties")).toObject();
    return {};
}

}

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
        c.firstFlushMs = 60 * 60 * 1000;
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
    static QStringList secrets()
    {
        return {QStringLiteral("Secret Show"), QStringLiteral("Episode 03"), QStringLiteral("altq"),
                QStringLiteral("meet again at the station"), QStringLiteral("Kenta"), QStringLiteral("Signs"),
                QStringLiteral("Comic Hand"), QStringLiteral("/home/"), QStringLiteral("C:\\")};
    }
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
        verifyProbeHasDebugId(*record);
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
        const QJsonObject exception = propertiesOf(analytics.queued(), QStringLiteral("$exception"));
        QCOMPARE(exception.value(QStringLiteral("crashed_version")).toString(), QStringLiteral("0.1.0-test.1"));
        QCOMPARE(exception.value(QStringLiteral("$exception_level")).toString(), QStringLiteral("fatal"));
        QString why;
        QVERIFY2(AnalyticsEndpoint::closedOnly(analytics.queued(), &why), qPrintable(why));
#ifdef Q_OS_LINUX
        const QJsonArray images = exception.value(QStringLiteral("$debug_images")).toArray();
        QVERIFY(std::any_of(images.begin(), images.end(), [](const QJsonValue &i) {
            return i.toObject().value(QStringLiteral("code_file")) == QJsonValue(kProbeModule);
        }));
#endif
        const QByteArray payload = QJsonDocument(analytics.queued()).toJson();
        QVERIFY(!payload.contains(QFileInfo(QStringLiteral(HIKARI_CRASH_PROBE)).absolutePath().toUtf8()));

        CrashReports again(settings, analytics, log, config());
        QCOMPARE(again.pending(), QStringLiteral("crash"));
    }

    void theSystemIsNamedAsPeopleKnowIt()
    {
        using report::System;
        QCOMPARE(System::osNameOf(QStringLiteral("Omarchy"), QStringLiteral("4.0.0.r7433.g64833fa"), QStringLiteral("linux"),
                                  QStringLiteral("7.2.8-10-omarchy")),
                 QStringLiteral("Omarchy 4.0.0.r7433.g64833fa (Linux 7.2)"));
        QCOMPARE(System::osNameOf(QStringLiteral("Ubuntu 24.04.1 LTS"), QStringLiteral("24.04"), QStringLiteral("linux"),
                                  QStringLiteral("6.8.0-45-generic")),
                 QStringLiteral("Ubuntu 24.04.1 LTS (Linux 6.8)"));
        QCOMPARE(System::osNameOf(QStringLiteral("Arch Linux"), QStringLiteral("unknown"), QStringLiteral("linux"),
                                  QStringLiteral("6.17.1-arch1-1")),
                 QStringLiteral("Arch Linux (Linux 6.17)"));
        QCOMPARE(System::osNameOf(QStringLiteral("Windows 11 Version 24H2"), QStringLiteral("11"), QStringLiteral("winnt"),
                                  QStringLiteral("10.0.26100")),
                 QStringLiteral("Windows 11 Version 24H2"));
        QCOMPARE(System::osNameOf(QStringLiteral("Odd\nName"), {}, QStringLiteral("linux"), {}), QStringLiteral("Odd Name (Linux)"));
        const System current = System::current(QStringLiteral("0.1.0"), {}, 1, 1, {});
        QVERIFY(!current.osName.isEmpty());
        QVERIFY2(!current.osName.startsWith(current.os + u' '), qPrintable(current.osName));
    }

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

    void freezeKilledLeadsToReportAtNextStart()
    {
        QProcess probe;
        startProbe(probe, path("hang-record.txt"), QStringLiteral("hang-kill"));
        QVERIFY(probe.waitForStarted(30000));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path("hang-record.txt")), 20000);
        // MUST KILL: a fixed 300 ms wait after the record appears, before killing the probe
        QTest::qWait(300);
        probe.kill();
        QVERIFY(probe.waitForFinished(10000));
        const auto record = crash::read(path("hang-record.txt"));
        QVERIFY(record);
        QCOMPARE(record->type, QStringLiteral("APP_HANG"));
        QCOMPARE(record->mechanism, QStringLiteral("watchdog"));
        QVERIFY(record->freeze());
        QVERIFY2(modulesOf(*record).contains(kProbeModule), qPrintable(modulesOf(*record).join(u',')));
        verifyProbeHasDebugId(*record);

        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        QCOMPARE(reports.pending(), QStringLiteral("freeze"));
        reports.started(QStringLiteral("deb"), 1, 1, QStringLiteral("editing"));
        QVERIFY(reports.text(kStart, {}).startsWith(QStringLiteral("Freeze\n  The window did not respond for ")));
        QVERIFY(analytics.queued().isEmpty());
    }

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
        QCOMPARE(posts(endpoint), 1);
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
        QCOMPARE(posts(endpoint), 0);
        QVERIFY(!QFile::exists(path("crash-report.json")));
        CrashReports next(settings, analytics, log, config());
        QCOMPARE(next.pending(), QString());
        QVERIFY(opened.isEmpty());
    }

    void failedSendKeepsTheReport()
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
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(reports.send(kStart, {}));
        QVERIFY(sent.wait(10000));
        QCOMPARE(sent.first().at(1).toBool(), false);
        QCOMPARE(reports.pending(), QStringLiteral("crash"));
        QVERIFY(QFile::exists(path("crash-report.json")));
    }

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
        QCOMPARE(posts(endpoint), 1);
        QCOMPARE(endpoint.events().size(), 2);
        QVERIFY(!propertiesOf(endpoint.events(), QStringLiteral("$exception")).isEmpty());
    }

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

    void multilineEntriesAndBusyLogs()
    {
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:9/batch/"))));
        hikari::ui::LogController log;
        CrashReports reports(settings, analytics, log, config());
        reports.setKnown([] { return QStringList{}; },
                         [] { return QStringList{QStringLiteral("Kenta waits\\Nat the station")}; });
        log.log(QStringLiteral("Validation failed: \"private first line\nprivate second line\""));
        log.notice(hikari::ui::LogController::Info, {}, QStringLiteral("macro printed: Kenta waits\\Nat the station"),
                   QStringLiteral("details \u201Csecret one\nsecret two\u201D"));
        const QString tail = reports.logTail().join(u'\n');
        for (const char *secret : {"private", "secret", "Kenta"})
            QVERIFY2(!tail.contains(QLatin1String(secret)), qPrintable(tail));
        QFile::remove(path("log-tail.txt"));
        QElapsedTimer busy;
        busy.start();
        while (busy.elapsed() < 1500 && !QFile::exists(path("log-tail.txt"))) {
            log.log(QStringLiteral("a line every 100 ms"));
            QTest::qWait(100);
        }
        QVERIFY(QFile::exists(path("log-tail.txt")));
    }

    void zipTakesOnlyTheNamedFile()
    {
#ifndef Q_OS_UNIX
        QSKIP("Unix links");
#else
        writeRecord("named.ass", "[Script Info]\n");
        QTemporaryDir elsewhere;
        QFile other(elsewhere.filePath(QStringLiteral("other.txt")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("not the subtitle file");
        other.close();
        hikari::ui::SettingsStore settings;
        Analytics analytics(settings, analyticsConfig(QUrl(QStringLiteral("http://127.0.0.1:9/batch/"))));
        hikari::ui::LogController log;
        CrashReports::Config c = config();
        c.attachments = path("issue");
        CrashReports reports(settings, analytics, log, c);
        reports.setCurrentFile([this] { return path("named.ass"); });
        reports.beginIssue();
        QVERIFY(QFile::remove(path("named.ass")));
        QVERIFY(QFile::link(other.fileName(), path("named.ass")));
        QVERIFY(reports.zipSubtitleFile().isEmpty());
        QFile::remove(path("named.ass"));
        writeRecord("named.ass", "[Script Info]\n");
        reports.beginIssue();
        QCOMPARE(QFileInfo(reports.zipSubtitleFile()).fileName(), QStringLiteral("named.zip"));
        reports.discardAttachments();
        QFile::remove(path("named.ass"));
#endif
    }

    void freezeSendingAtQuitIsKept()
    {
        AnalyticsEndpoint endpoint;
        hikari::ui::SettingsStore settings;
        settings.set("privacy.usageData", false);
        Analytics analytics(settings, analyticsConfig(endpoint.url()));
        hikari::ui::LogController log;
        const crash::Record freeze{QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0-beta.1"),
                                   {{QStringLiteral("hikarisub"), 0x31c0a8}}, 12};
        {
            CrashReports reports(settings, analytics, log, config());
            reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
            reports.freezeRecovered(freeze);
            QVERIFY(reports.send(QStringLiteral("freeze"), {}));
            QVERIFY(QFile::exists(path("crash-report-sending.json")));
        }
        {
            CrashReports next(settings, analytics, log, config());
            QCOMPARE(next.pending(), QStringLiteral("freeze"));
            QCOMPARE(next.seconds(kStart), 12);
            QVERIFY(!QFile::exists(path("crash-report-sending.json")));
            next.dismiss(kStart);
        }
        CrashReports reports(settings, analytics, log, config());
        reports.started(QStringLiteral("appimage"), 1, 1, QStringLiteral("editing"));
        reports.freezeRecovered(freeze);
        QSignalSpy sent(&reports, &CrashReports::sent);
        QVERIFY(QTest::qWaitFor([&] { return reports.send(QStringLiteral("freeze"), {}); }, 5000));
        QVERIFY(sent.wait(5000));
        QCOMPARE(sent.first().at(1).toBool(), true);
        QVERIFY(!QFile::exists(path("crash-report-sending.json")));
        QCOMPARE(reports.pending(), QString());
    }

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
        QCOMPARE(posts(endpoint), 0);
    }

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
        QVERIFY2(tail.contains(QStringLiteral("1/2 of the frames")), qPrintable(tail));
        QTRY_VERIFY(QFile::exists(path("log-tail.txt")));
        QFile kept(path("log-tail.txt"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        const QString onDisk = QString::fromUtf8(kept.readAll());
        kept.close();

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

    void scrubberRules_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("unix path") << QStringLiteral("Saved /home/me/My Subs/ep 1.ass") << QStringLiteral("Saved file1.ass");
        QTest::newRow("windows path") << QStringLiteral("Read D:\\Anime\\Show\\op.mkv now") << QStringLiteral("Read file1.mkv now");
        QTest::newRow("unc path") << QStringLiteral("Read \\\\nas\\share\\a.wav") << QStringLiteral("Read file1.wav");
        QTest::newRow("file url") << QStringLiteral("Open file:///home/me/x.ass") << QStringLiteral("Open file1.ass");
        QTest::newRow("folder") << QStringLiteral("Folder /home/me/stuff is empty") << QStringLiteral("Folder \u2039folder\u203A is empty");
        QTest::newRow("folder with spaces") << QStringLiteral("Moved to /home/Alice Smith/.local/share/HikariSub now")
                                            << QStringLiteral("Moved to \u2039folder\u203A now");
        QTest::newRow("windows folder with spaces") << QStringLiteral("Moved to C:\\Users\\Alice Smith\\AppData\\Local\\HikariSub")
                                                    << QStringLiteral("Moved to \u2039folder\u203A");
        QTest::newRow("home folder") << QStringLiteral("In ~/Subs") << QStringLiteral("In \u2039folder\u203A");
        QTest::newRow("bare name") << QStringLiteral("Loaded karaoke.lua") << QStringLiteral("Loaded file1.lua");
        QTest::newRow("long unsupported extension")
            << QString(64 * 1024, u'-') + QStringLiteral(".q then karaoke.lua")
            << QString(64 * 1024, u'-') + QStringLiteral(".q then file1.lua");
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
        QCOMPARE(reports.pending(), QString());
        QTest::qWait(200);
        QCOMPARE(posts(endpoint), 0);
    }

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
        // \r?: a Windows checkout gives the forms CRLF line ends.
        static const QRegularExpression idLine(QStringLiteral("^\\s+id: ([a-z-]+)\\r?$"), QRegularExpression::MultilineOption);
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
        const crash::Record freeze{QStringLiteral("APP_HANG"), QStringLiteral("watchdog"), QStringLiteral("0.1.0"),
                                   {{QStringLiteral("hikarisub"), 0x31c0a8}, {QStringLiteral("other"), 0}}, 40};
        QVERIFY(crash::write(path("hang-record.txt"), freeze));
        const auto back = crash::read(path("hang-record.txt"));
        QVERIFY(back);
        QCOMPARE(back->seconds, 40);
        QCOMPARE(back->frames.size(), 2);
        QCOMPARE(back->frames.first().offset, quint64(0x31c0a8));

        const QByteArray head = "hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism signal\n";
        QVERIFY(check(head + "frame hikarisub 0x1a2b 0x400000 3a4bd4d0-ff5b-4b31-3a40-e1b9d72f2ec8\n"));
        QVERIFY(check(head + "frame HikariSub.exe 0x1a2b 0x7ff6a0000000 0c1033f7-8a6a-4e2c-9b0b-6f2b1f6f8b3e-1\n"));
        QVERIFY(!check(head + "frame other 0x1a2b 0x400000 3a4bd4d0-ff5b-4b31-3a40-e1b9d72f2ec8\n"));
        QVERIFY(!check(head + "frame hikarisub 0x1a2b 0x400000 3A4BD4D0-FF5B-4B31-3A40-E1B9D72F2EC8\n"));
        QVERIFY(!check(head + "frame hikarisub 0x1a2b 0x400000\n"));
        QVERIFY(!check(head + "frame hikarisub 0x1a2b 0x400000 /tmp/.mount_HikariX/usr/bin/hikarisub\n"));
        const crash::Record crashed{QStringLiteral("SIGSEGV"), QStringLiteral("signal"), QStringLiteral("0.1.0"),
                                    {kNamed, {QStringLiteral("other"), 0x4d2a0}}};
        QVERIFY(crash::write(path("crash-record.txt"), crashed));
        const auto read = crash::read(path("crash-record.txt"));
        QVERIFY(read);
        QCOMPARE(read->frames.first().base, kNamed.base);
        QCOMPARE(read->frames.first().debugId, kNamed.debugId);
        QVERIFY(read->frames.last().debugId.isEmpty());
    }

    void exceptionCarriesDebugImages()
    {
        const crash::Frame qt{QStringLiteral("libQt6Core.so.6"), 0x2c41b8, 0x7f3a10000000,
                              QStringLiteral("9b0c4f2e-1d3a-5e6f-7a8b-9c0d1e2f3a4b")};
        report::Report r;
        r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        r.record = crash::Record{QStringLiteral("SIGSEGV"), QStringLiteral("signal"), QStringLiteral("0.1.0"),
                                 {kNamed, qt, {QStringLiteral("other"), 0x4d2a0}, {kNamed.module, 0x10, kNamed.base, kNamed.debugId}}};
        const QJsonObject properties = r.exceptionProperties();
        QVERIFY(Analytics::conforms(QStringLiteral("$exception"), properties));
        const QJsonArray frames = properties.value(QStringLiteral("$exception_list"))[0][QStringLiteral("stacktrace")]
                                      [QStringLiteral("frames")].toArray();
        QCOMPARE(frames.size(), 4);
        QCOMPARE(frames[0][QStringLiteral("instruction_addr")].toString(), QStringLiteral("0x5f3a2c"));
        QCOMPARE(frames[0][QStringLiteral("image_addr")].toString(), QStringLiteral("0x400000"));
        QCOMPARE(frames[1][QStringLiteral("instruction_addr")].toString(), QStringLiteral("0x7f3a102c41b8"));
        QVERIFY(!frames[2].toObject().contains(QStringLiteral("instruction_addr")));
        QCOMPARE(frames[2].toObject().keys(), (QStringList{QStringLiteral("module"), QStringLiteral("offset"), QStringLiteral("platform")}));
        const QJsonArray images = properties.value(QStringLiteral("$debug_images")).toArray();
        QCOMPARE(images.size(), 2);
        QCOMPARE(images[0].toObject(), (QJsonObject{{QStringLiteral("code_file"), QStringLiteral("hikarisub")},
                                                    {QStringLiteral("debug_id"), kNamed.debugId},
                                                    {QStringLiteral("image_addr"), QStringLiteral("0x400000")}}));
        QCOMPARE(images[1][QStringLiteral("code_file")].toString(), qt.module);

        const auto withImage = [&](const char *key, const QJsonValue &value) {
            QJsonObject p = properties;
            QJsonArray list = p.value(QStringLiteral("$debug_images")).toArray();
            QJsonObject image = list[0].toObject();
            image.insert(QString::fromLatin1(key), value);
            list[0] = image;
            p.insert(QStringLiteral("$debug_images"), list);
            return Analytics::conforms(QStringLiteral("$exception"), p);
        };
        QVERIFY(withImage("code_file", QStringLiteral("hikarisub")));
        QVERIFY(!withImage("code_file", QStringLiteral("/tmp/.mount_HikariX/usr/bin/hikarisub")));
        QVERIFY(!withImage("code_file", QStringLiteral("C:\\Users\\me\\HikariSub.exe")));
        QVERIFY(!withImage("code_file", QStringLiteral("other")));
        QVERIFY(!withImage("debug_id", QStringLiteral("9B0C4F2E-1D3A-5E6F-7A8B-9C0D1E2F3A4B")));
        QVERIFY(!withImage("debug_id", QStringLiteral("my words")));
        QVERIFY(!withImage("image_addr", QStringLiteral("4194304")));
        QVERIFY(!withImage("arch", QStringLiteral("x86_64")));
        QJsonObject noImages = properties;
        noImages.insert(QStringLiteral("$debug_images"), QJsonArray{});
        QVERIFY(!Analytics::conforms(QStringLiteral("$exception"), noImages));

        const auto withFrame = [&](int index, const char *key, const QJsonValue &value) {
            QJsonObject p = properties;
            QJsonArray list = p.value(QStringLiteral("$exception_list")).toArray();
            QJsonObject e = list[0].toObject();
            QJsonObject stack = e.value(QStringLiteral("stacktrace")).toObject();
            QJsonArray fs = stack.value(QStringLiteral("frames")).toArray();
            QJsonObject f = fs[index].toObject();
            f.insert(QString::fromLatin1(key), value);
            fs[index] = f;
            stack.insert(QStringLiteral("frames"), fs);
            e.insert(QStringLiteral("stacktrace"), stack);
            list[0] = e;
            p.insert(QStringLiteral("$exception_list"), list);
            return Analytics::conforms(QStringLiteral("$exception"), p);
        };
        QVERIFY(withFrame(0, "image_addr", QStringLiteral("0x400000")));
        QVERIFY(!withFrame(2, "image_addr", QStringLiteral("0x400000")));
        QVERIFY(!withFrame(2, "instruction_addr", QStringLiteral("0x44d2a0")));
        QVERIFY(!withFrame(0, "filename", QStringLiteral("/home/me/hikari/src/main.cpp")));
        QVERIFY(!withFrame(0, "instruction_addr", QStringLiteral("/tmp/x")));

        const auto back = report::Report::fromJson(r.toJson());
        QVERIFY(back && back->record);
        QCOMPARE(back->exceptionProperties(), properties);
        QJsonObject stored = r.toJson();
        QJsonObject rec = stored.value(QStringLiteral("record")).toObject();
        QJsonArray fs = rec.value(QStringLiteral("frames")).toArray();
        fs[2] = QJsonArray{QStringLiteral("other"), QStringLiteral("0x4d2a0"), QStringLiteral("0x400000"), kNamed.debugId};
        rec.insert(QStringLiteral("frames"), fs);
        stored.insert(QStringLiteral("record"), rec);
        QVERIFY(!report::Report::fromJson(stored));
    }
};

QTEST_MAIN(CrashReportTest)
#include "crash_report_tests.moc"
