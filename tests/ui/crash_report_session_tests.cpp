// CR1 (#452): the crash, freeze and issue report windows through the whole
// window, sending to a fake endpoint on the loopback and opening links into
// a list: the window after a crash shows what goes and sends it only on
// Send report (usage data off too); Don't send sends nothing; the freeze
// notice opens Report a freeze; Report an issue opens the matching GitHub
// form, filled in. "Always send crash reports" opens no window.
//
// HIKARI_SURFACE_SHOT_DIR=<dir>: the windows in each theme (the card's
// evidence); HIKARI_CRASH_PAYLOAD_OUT=<file>: the report Send report sent.

#include "hikari/app/application.h"
#include "hikari/app/crash_record.h"

#include "analytics_endpoint.h"
#include "docking.h"
#include "settings_store.h"
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtQuickTest/quicktest.h>
#include <QtTest>

#include <functional>
#include <memory>

using namespace hikari;
namespace crash = hikari::app::crash;

namespace {

QByteArray crashRecordText()
{
    return "hikari-crash 1\nversion 0.1.0-beta.1\ntype SIGSEGV\nmechanism signal\n"
           "frame hikarisub 0x1f3a2c\nframe libQt6Core.so.6 0x2c41b8\nframe other 0x4d2a0\n";
}

} // namespace

class CrashReportSessionTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString folder;
    QList<QUrl> opened;
    QStringList copied;
    std::unique_ptr<test::AnalyticsEndpoint> endpoint;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    QString path(const char *name) const { return folder + u'/' + QLatin1String(name); }
    // `before`: on the application, before the window loads.
    void start(const std::function<void(app::Application &)> &before = {})
    {
        endpoint = std::make_unique<test::AnalyticsEndpoint>();
        QVERIFY(endpoint->listening());
        app::Application::Options options;
        options.settingsFile = path("settings.toml");
        options.playbackAudio = false;
        options.analytics.key = QStringLiteral("phc_test");
        options.analytics.endpoint = endpoint->url();
        options.analytics.queueFile = path("analytics-queue.json");
        options.analytics.version = QStringLiteral("0.1.0-beta.1");
        options.analytics.firstFlushMs = 60 * 60 * 1000;
        options.crashReports.crashRecord = path("crash-record.txt");
        options.crashReports.freezeRecord = path("hang-record.txt");
        options.crashReports.pendingFile = path("crash-report.json");
        options.crashReports.logTail = path("log-tail.txt");
        options.crashReports.attachments = path("issue-report");
        options.crashReports.version = QStringLiteral("0.1.0-beta.1");
        options.crashReports.openUrl = [this](const QUrl &url) {
            opened << url;
            return true;
        };
        options.crashReports.copy = [this](const QString &text) { copied << text; };
        application = std::make_unique<app::Application>(options);
        if (before)
            before(*application);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        // As runWindow does once the window shows.
        application->crashReports().started(QStringLiteral("appimage"), 1, 1.0, QStringLiteral("typesetting"));
    }
    void writeRecord(const char *name, const QByteArray &text)
    {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    QQuickWindow *dialog(const char *name) const
    {
        return window()->findChild<QQuickWindow *>(QLatin1String(name));
    }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *in(QQuickWindow *window, const char *name) const
    {
        return window ? findItem(window->contentItem(), QLatin1String(name)) : nullptr;
    }
    bool click(QQuickWindow *window, const char *name) const
    {
        QQuickItem *button = in(window, name);
        return button && button->isVisible() && QMetaObject::invokeMethod(button, "click");
    }
    QVariant shell(const QString &code)
    {
        QQmlExpression expression(qmlContext(window()), window(), code);
        const QVariant out = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return out;
    }
    // HIKARI_SURFACE_SHOT_DIR: `shown` in each theme as <name>-<theme>.png.
    void shoot(QQuickWindow *shown, const QString &name)
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            return;
        QVERIFY(QDir().mkpath(out));
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        for (const auto code : ui::theme::kCodes) {
            ui::theme::preview({{QStringLiteral("appearance.theme"), ui::theme::codeName(code)},
                                {QStringLiteral("appearance.followSystem"), false}});
            QQuickTest::qWaitForPolish(shown);
            QTest::qWait(200);
            QVERIFY(shown->grabWindow().save(out + u'/' + name + u'-' + ui::theme::codeName(code) +
                                             QStringLiteral(".png")));
        }
    }
    QStringList eventNames() const
    {
        QStringList names;
        for (const auto &e : endpoint->events())
            names << e.toObject().value(QStringLiteral("event")).toString();
        return names;
    }

private slots:
    void initTestCase() { QVERIFY(root.isValid()); }
    void init()
    {
        folder = root.filePath(QStringLiteral("profile"));
        QDir(folder).removeRecursively();
        QVERIFY(QDir().mkpath(folder));
        opened.clear();
        copied.clear();
    }
    void cleanup()
    {
        engine.reset();
        application.reset();
        endpoint.reset();
    }

    // The start after a crash: the window shows the whole report; Send
    // report with usage data off sends the record and the report, the note
    // in it, once; the notice thanks.
    void crashWindowSendsOnlyOnSendReport()
    {
        writeRecord("crash-record.txt", crashRecordText());
        start([](app::Application &a) { a.settingsStore()->setValue(QStringLiteral("privacy.usageData"), false); });
        QQuickWindow *report = dialog("crashReport");
        QVERIFY(report);
        QTRY_VERIFY(report->isVisible());
        QCOMPARE(report->title(), QStringLiteral("HikariSub closed unexpectedly"));
        QVERIFY(in(report, "crashReportHeadline")->property("text").toString().startsWith(QStringLiteral("HikariSub crashed ")));
        QVERIFY(in(report, "crashReportSend")->isVisible());
        QVERIFY(!in(report, "crashReportText")->isVisible()); // collapsed
        in(report, "crashReportNote")->setProperty("text", QStringLiteral("I had just switched to the Perspective tool."));
        report->setProperty("expanded", true);
        QTRY_VERIFY(in(report, "crashReportText")->isVisible());
        const QString shown = in(report, "crashReportText")->property("text").toString();
        QVERIFY2(shown.startsWith(QStringLiteral("Note\n  I had just switched")), qPrintable(shown));
        QVERIFY(shown.contains(QStringLiteral("SIGSEGV (signal) in 0.1.0-beta.1")));
        QVERIFY(shown.contains(QStringLiteral("#0  hikarisub")));
        shoot(report, QStringLiteral("crash-report"));
        QTest::qWait(200);
        QVERIFY(endpoint->events().isEmpty()); // nothing yet
        QVERIFY(click(report, "crashReportSend"));
        QTRY_COMPARE(eventNames(), (QStringList{QStringLiteral("$exception"), QStringLiteral("crash_report")}));
        QTRY_VERIFY(!report->isVisible());
        const QJsonObject sent = endpoint->events().at(1).toObject().value(QStringLiteral("properties")).toObject();
        QCOMPARE(sent.value(QStringLiteral("note")).toString(), QStringLiteral("I had just switched to the Perspective tool."));
        QCOMPARE(application->crashReports().pending(), QString());
        QVERIFY(!QFile::exists(path("crash-report.json")));
        QTRY_COMPARE(shell(QStringLiteral("notices.entries.length > 0 ? notices.entries[0].title : ''")).toString(),
                     QStringLiteral("Crash report sent. Thank you."));
        if (const QString out = qEnvironmentVariable("HIKARI_CRASH_PAYLOAD_OUT"); !out.isEmpty()) {
            QFile f(out);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(endpoint->events()).toJson(QJsonDocument::Indented));
        }
    }

    // Don't send: the record and the report go, nothing is sent; the next
    // start shows no window.
    void dontSendSendsNothing()
    {
        writeRecord("crash-record.txt", crashRecordText());
        start([](app::Application &a) { a.settingsStore()->setValue(QStringLiteral("privacy.usageData"), false); });
        QQuickWindow *report = dialog("crashReport");
        QTRY_VERIFY(report->isVisible());
        QVERIFY(click(report, "crashReportDontSend"));
        QTRY_VERIFY(!report->isVisible());
        QCOMPARE(application->crashReports().pending(), QString());
        QVERIFY(!QFile::exists(path("crash-report.json")));
        QVERIFY(!QFile::exists(path("crash-record.txt")));
        QTest::qWait(300);
        QVERIFY(endpoint->events().isEmpty());
        QVERIFY(opened.isEmpty());
    }

    // Open GitHub issue: a Bug issue with the report, nothing sent.
    void openGitHubIssueOpensTheBugForm()
    {
        writeRecord("crash-record.txt", crashRecordText());
        start([](app::Application &a) { a.settingsStore()->setValue(QStringLiteral("privacy.usageData"), false); });
        QQuickWindow *report = dialog("crashReport");
        QTRY_VERIFY(report->isVisible());
        QVERIFY(click(report, "crashReportGitHub"));
        QTRY_VERIFY(!report->isVisible());
        QCOMPARE(opened.size(), 1);
        const QUrlQuery query(opened.first());
        QCOMPARE(query.queryItemValue(QStringLiteral("template")), QStringLiteral("bug.yml"));
        QVERIFY(query.queryItemValue(QStringLiteral("crash-report"), QUrl::FullyDecoded).contains(QStringLiteral("SIGSEGV")));
        QTest::qWait(300);
        QVERIFY(endpoint->events().isEmpty());
    }

    // "Always send crash reports": no window, the report goes at the start
    // and a notice says so.
    void alwaysSendOpensNoWindow()
    {
        writeRecord("crash-record.txt", crashRecordText());
        start([](app::Application &a) {
            a.settingsStore()->setValue(QStringLiteral("privacy.usageData"), false);
            a.settingsStore()->setValue(QStringLiteral("privacy.alwaysSendCrashReports"), true);
        });
        QTRY_COMPARE(eventNames(), (QStringList{QStringLiteral("$exception"), QStringLiteral("crash_report")}));
        QQuickWindow *report = dialog("crashReport");
        QVERIFY(report && !report->isVisible());
        QTRY_COMPARE(shell(QStringLiteral("notices.entries.length > 0 ? notices.entries[0].title : ''")).toString(),
                     QStringLiteral("HikariSub closed unexpectedly. A crash report was sent."));
        // Show report: what went, read only.
        shell(QStringLiteral("notices.entries[0].actions[0].run()"));
        QTRY_VERIFY(report->isVisible());
        QVERIFY(in(report, "crashReportClose")->isVisible());
        QVERIFY(!in(report, "crashReportSend")->isVisible());
        QVERIFY(in(report, "crashReportText")->property("text").toString().contains(QStringLiteral("SIGSEGV")));
        report->close();
    }

    // A freeze the window came back from: a warning notice whose Report…
    // opens Report a freeze.
    void freezeNoticeOpensReportAFreeze()
    {
        start();
        crash::Record record;
        record.version = QStringLiteral("0.1.0-beta.1");
        record.type = QString::fromLatin1(crash::kFreezeType);
        record.mechanism = QStringLiteral("watchdog");
        record.seconds = 12;
        record.frames = {{QStringLiteral("libc.so.6"), 0x10b2fd}, {QStringLiteral("hikarisub"), 0x31c0a8}};
        application->crashReports().freezeRecovered(record);
        QTRY_COMPARE(shell(QStringLiteral("notices.entries.length > 0 ? notices.entries[0].title : ''")).toString(),
                     QStringLiteral("HikariSub stopped responding for 12 seconds"));
        QCOMPARE(shell(QStringLiteral("notices.entries[0].severity")).toString(), QStringLiteral("warning"));
        shell(QStringLiteral("notices.entries[0].actions[0].run()"));
        QQuickWindow *report = dialog("crashReport");
        QTRY_VERIFY(report->isVisible());
        QCOMPARE(report->title(), QStringLiteral("Report a freeze"));
        QVERIFY(in(report, "crashReportHeadline")->property("text").toString().startsWith(
            QStringLiteral("HikariSub stopped responding for 12 seconds")));
        report->setProperty("expanded", true);
        QTRY_VERIFY(in(report, "crashReportText")->property("text").toString().contains(
            QStringLiteral("The window did not respond for 12 s")));
        shoot(report, QStringLiteral("freeze-report"));
        QVERIFY(click(report, "crashReportDontSend"));
        QTRY_VERIFY(!report->isVisible());
        QVERIFY(!application->crashReports().freezeOffered());
    }

    // Help › Report an issue: the form for each kind; Open on GitHub opens
    // the matching form filled in, then Finish on GitHub; nothing is sent.
    void reportIssueOpensTheFilledForm()
    {
        start();
        auto *item = window()->findChild<QObject *>(QStringLiteral("reportIssueMenuItem"));
        QVERIFY(item);
        QVERIFY(QMetaObject::invokeMethod(item->property("action").value<QObject *>(), "trigger"));
        QQuickWindow *form = dialog("reportIssue");
        QVERIFY(form);
        QTRY_VERIFY(form->isVisible());
        // The thumbnail loads on its own thread; Windows removes no file
        // that thread still has open, so Done waits for it as a user would.
        QTRY_COMPARE(in(form, "reportIssueThumbnail")->property("status").toInt(), 1); // Image.Ready
        QVERIFY(!in(form, "reportIssueOpen")->isEnabled()); // no title yet
        in(form, "reportIssueTitle")->setProperty("text", QStringLiteral("The handles jump when zoomed"));
        in(form, "reportIssueFirst")->setProperty("text", QStringLiteral("Dragging a corner moves it twice as far."));
        in(form, "reportIssueSecond")->setProperty("text", QStringLiteral("1. Zoom to 200 %\n2. Drag a corner handle"));
        QVERIFY(in(form, "reportIssueOpen")->isEnabled());
        QVERIFY(in(form, "reportIssueWithLog")->property("checked").toBool());
        shoot(form, QStringLiteral("report-issue-bug"));
        QVERIFY(click(form, "reportIssueOpen"));
        QCOMPARE(opened.size(), 1);
        QUrlQuery query(opened.first());
        QCOMPARE(query.queryItemValue(QStringLiteral("template")), QStringLiteral("bug.yml"));
        QCOMPARE(query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), QStringLiteral("The handles jump when zoomed"));
        QCOMPARE(query.queryItemValue(QStringLiteral("steps"), QUrl::FullyDecoded),
                 QStringLiteral("1. Zoom to 200 %\n2. Drag a corner handle"));
        QVERIFY(query.queryItemValue(QStringLiteral("system"), QUrl::FullyDecoded).contains(QStringLiteral("HikariSub 0.1.0-beta.1")));
        QTRY_VERIFY(in(form, "reportIssueDone")->isVisible());
        QVERIFY(in(form, "reportIssueOpened")->isVisible());
        shoot(form, QStringLiteral("report-issue-finish"));
        QVERIFY(click(form, "reportIssueDone"));
        QTRY_VERIFY(!form->isVisible());
        QVERIFY2(!QDir(path("issue-report")).exists(), // the screenshot went with Done
                 qPrintable(QDir(path("issue-report")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).join(u' ')));

        // A feature request: its two fields and its form.
        QVERIFY(QMetaObject::invokeMethod(item->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(form->isVisible());
        QVERIFY(click(form, "reportIssueKind_feature"));
        QTRY_COMPARE(form->property("kind").toString(), QStringLiteral("feature"));
        QVERIFY(!in(form, "reportIssueWithLog")->isVisible());
        in(form, "reportIssueTitle")->setProperty("text", QStringLiteral("Snap handles to the grid"));
        in(form, "reportIssueFirst")->setProperty("text", QStringLiteral("A snap toggle."));
        shoot(form, QStringLiteral("report-issue-feature"));
        QVERIFY(click(form, "reportIssueOpen"));
        QCOMPARE(opened.size(), 2);
        query = QUrlQuery(opened.last());
        QCOMPARE(query.queryItemValue(QStringLiteral("template")), QStringLiteral("feature.yml"));
        QCOMPARE(query.queryItemValue(QStringLiteral("wanted"), QUrl::FullyDecoded), QStringLiteral("A snap toggle."));
        QVERIFY(!query.hasQueryItem(QStringLiteral("log")));
        QVERIFY(click(form, "reportIssueDone"));
        QTest::qWait(300);
        QVERIFY(!eventNames().contains(QStringLiteral("crash_report")));
    }
};

QTEST_MAIN(CrashReportSessionTest)
#include "crash_report_session_tests.moc"
