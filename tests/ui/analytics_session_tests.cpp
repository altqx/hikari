// AN1 (#404): a scripted session through the whole window, its usage data
// sent to a fake endpoint on the loopback: every event the session makes is
// the table's, and none carries free text. The session's files, folder,
// Style, font, Line text, script and saved layout carry planted names that
// must appear nowhere in what is sent.
//
// HIKARI_ANALYTICS_PAYLOAD_OUT=<file>: the session's requests are written
// there too (the card's evidence).

#include "hikari/app/application.h"

#include "analytics_endpoint.h"
#include "docking.h"
#include "settings_store.h"
#include "theme.h"
#include "workspace_layout.h"

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
#include <QtQuickTest/quicktest.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

// Planted in the session; none may be sent.
const QStringList kPlanted{QStringLiteral("SecretUserFolder"), QStringLiteral("MyPrivateEpisode"),
                           QStringLiteral("SecretStyleName"),  QStringLiteral("SecretFontFace"),
                           QStringLiteral("Confidential"),     QStringLiteral("MySecretMacro"),
                           QStringLiteral("MySecretLayout"),   QStringLiteral("SecretVideoClip")};

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

QByteArray subtitles()
{
    return "[Script Info]\nTitle: MyPrivateEpisode title\nScriptType: v4.00+\n\n[V4+ Styles]\n"
           "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
           "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
           "Alignment, MarginL, MarginR, MarginV, Encoding\n"
           "Style: SecretStyleName,SecretFontFace,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,"
           "1,2,2,2,10,10,10,1\n\n"
           "[Events]\n"
           "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
           "Dialogue: 0,0:00:00.00,0:00:05.00,SecretStyleName,,0,0,0,,Confidential dialogue line\n";
}

} // namespace

class AnalyticsSessionTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString folder; // the session's planted folder
    std::unique_ptr<test::AnalyticsEndpoint> endpoint;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    void start(const app::Analytics::Config &analytics)
    {
        app::Application::Options options;
        options.settingsFile = folder + QStringLiteral("/settings.toml");
        options.playbackAudio = false;
        options.analytics = analytics;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
    }
    app::Analytics::Config config() const
    {
        app::Analytics::Config c;
        c.key = QStringLiteral("phc_test");
        c.endpoint = endpoint->url();
        c.queueFile = folder + QStringLiteral("/analytics-queue.json");
        c.version = QStringLiteral("0.1.0-beta.1");
        c.firstFlushMs = 60 * 60 * 1000;
        return c;
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
    QObject *settingsWindow() const
    {
        QVariant made;
        QMetaObject::invokeMethod(window(), "settingsWindow", Q_RETURN_ARG(QVariant, made));
        return made.value<QObject *>();
    }
    QQuickItem *in(QObject *dialog, const char *name) const
    {
        auto *content = dialog ? dialog->property("contentItem").value<QQuickItem *>() : nullptr;
        return content ? findItem(content, QLatin1String(name)) : nullptr;
    }
    bool trigger(const char *menuItem)
    {
        auto *item = window()->findChild<QObject *>(QLatin1String(menuItem));
        return item && QMetaObject::invokeMethod(item->property("action").value<QObject *>(), "trigger");
    }
    // HIKARI_SURFACE_SHOT_DIR: `dialog` in each theme as <name>-<theme>.png.
    void shoot(QObject *dialog, const QString &name)
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            return;
        QVERIFY(QDir().mkpath(out));
        auto *shown = qobject_cast<QQuickWindow *>(dialog);
        QVERIFY(shown);
        auto restore = qScopeGuard([] { ui::theme::endPreview(); });
        for (const auto code : ui::theme::kCodes) {
            ui::theme::preview({{QStringLiteral("appearance.theme"), ui::theme::codeName(code)},
                                {QStringLiteral("appearance.followSystem"), false}});
            QQuickTest::qWaitForPolish(shown);
            QTest::qWait(200);
            QVERIFY(shown->grabWindow().save(out + QLatin1Char('/') + name + QLatin1Char('-') +
                                             ui::theme::codeName(code) + QStringLiteral(".png")));
        }
    }
    // Runs `code` in Main's own scope (its ids: root, logDock, ...).
    QVariant shell(const QString &code)
    {
        QQmlExpression expression(qmlContext(window()), window(), code);
        const QVariant out = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return out;
    }

private slots:
    void initTestCase() { QVERIFY(root.isValid()); }
    void init()
    {
        folder = root.filePath(QStringLiteral("SecretUserFolder"));
        QDir(folder).removeRecursively();
        QVERIFY(QDir().mkpath(folder));
    }
    void cleanup()
    {
        engine.reset();
        application.reset();
        endpoint.reset();
    }

    void sessionSendsOnlyTheTable()
    {
        endpoint = std::make_unique<test::AnalyticsEndpoint>();
        QVERIFY(endpoint->listening());
        start(config());
        app::Analytics &analytics = application->analytics();
        QVERIFY(analytics.active());
        // What the composition does at the first frame.
        app::Analytics::StartFacts facts;
        facts.package = QStringLiteral("build_tree");
        facts.screens = 1;
        facts.scale = window()->devicePixelRatio();
        facts.startupMs = 1200;
        facts.layout = application->workspaceLayout().preset();
        analytics.appStarted(facts);

        // Getting started, skipped and finished with a changed look.
        application->skipGettingStarted();
        QVariantMap pages = application->openGettingStarted();
        pages.insert(QStringLiteral("appearance.theme"), QStringLiteral("light"));
        QVERIFY(application->finishGettingStarted(pages, false));

        // A Document with planted names, its video and blank audio.
        const QString subtitlesPath = folder + QStringLiteral("/MyPrivateEpisode 03.ass");
        QVERIFY(writeFile(subtitlesPath, subtitles()));
        QVERIFY(application->openFile(subtitlesPath));
        const QString video = folder + QStringLiteral("/SecretVideoClip.mkv");
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), video));
        application->video().openVideo(video);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 30000);
        // An audio open closed on the way is no failed open.
        application->audio().openAudio(video);
        application->audio().closeAudio();
        application->audio().openAudio(QStringLiteral("dummy"));
        QTRY_VERIFY_WITH_TIMEOUT(application->audio().loaded(), 30000);

        // Commands by key and from a menu, panels, dialogs, layouts.
        QVERIFY(shell(QStringLiteral("root.runGlobalHotkey(\"GLOBAL_OPEN_STYLE_MANAGER\")")).toBool());
        shell(QStringLiteral("root.runItem(root.globalActions().GLOBAL_ABOUT)"));
        shell(QStringLiteral("root.showPanel(logDock)"));
        shell(QStringLiteral("root.applyPreset(\"Karaoke\")"));
        QVERIFY(application->workspaceLayout().saveLayoutAs(QStringLiteral("MySecretLayout")));
        shell(QStringLiteral("root.applySavedLayout(\"MySecretLayout\")"));
        QTest::qWait(500);

        // A user's script that does not load.
        const QString script = folder + QStringLiteral("/MySecretMacro.lua");
        QVERIFY(writeFile(script, "script_name = \"MySecretMacro\"\nthis is not lua\n"));
        QSignalSpy failed(&application->automation().manager(), &backends::AutomationManager::loadFailed);
        application->automation().load(script.toStdString());
        QVERIFY(failed.wait(30000));

        analytics.quit(5000);
        QVERIFY(analytics.queued().isEmpty());

        const QJsonArray events = endpoint->events();
        QStringList names;
        for (const QJsonValue &e : events)
            names << e.toObject().value(QStringLiteral("event")).toString();
        for (const char *expected : {"app_started", "getting_started_skipped", "getting_started_finished", "video_opened",
                                     "audio_opened", "command_used", "dialog_opened", "panel_opened", "layout_changed",
                                     "automation_error", "app_closed"})
            QVERIFY2(names.contains(QLatin1String(expected)), qPrintable(QLatin1String(expected) + QStringLiteral(" not in ") + names.join(u',')));

        QString why;
        QVERIFY2(test::AnalyticsEndpoint::closedOnly(events, &why), qPrintable(why));
        QByteArray sent;
        for (const auto &request : std::as_const(endpoint->requests))
            sent += request.body;
        for (const QString &planted : kPlanted)
            QVERIFY2(!sent.contains(planted.toUtf8()), qPrintable(planted));
        QVERIFY(!sent.contains(QDir::homePath().toUtf8()));
        QVERIFY(!sent.contains(root.path().toUtf8()));

        // What the session sent, in order, for a reader.
        auto propertiesOf = [&](const char *event) {
            for (const QJsonValue &e : events)
                if (e.toObject().value(QStringLiteral("event")) == QJsonValue(QLatin1String(event)))
                    return e.toObject().value(QStringLiteral("properties")).toObject();
            return QJsonObject();
        };
        QCOMPARE(propertiesOf("getting_started_finished").value(QStringLiteral("pages_changed")).toArray(),
                 QJsonArray{QStringLiteral("look")});
        QCOMPARE(propertiesOf("automation_error").value(QStringLiteral("kind")).toString(), QStringLiteral("load_failed"));
        for (const QJsonValue &e : events)
            if (e.toObject().value(QStringLiteral("event")) == QJsonValue(QStringLiteral("audio_opened")))
                QCOMPARE(e.toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("outcome")).toString(),
                         QStringLiteral("ready"));
        QCOMPARE(propertiesOf("automation_error").value(QStringLiteral("script")).toString(), QStringLiteral("user script"));
        QStringList vias;
        for (const QJsonValue &e : events)
            if (e.toObject().value(QStringLiteral("event")) == QJsonValue(QStringLiteral("command_used")))
                vias << e.toObject().value(QStringLiteral("properties")).toObject().value(QStringLiteral("via")).toString();
        QVERIFY2(vias.contains(QStringLiteral("key")) && vias.contains(QStringLiteral("pointer")), qPrintable(vias.join(u',')));

        if (const QString out = qEnvironmentVariable("HIKARI_ANALYTICS_PAYLOAD_OUT"); !out.isEmpty()) {
            QJsonArray requests;
            for (const auto &request : std::as_const(endpoint->requests))
                requests.append(QJsonDocument::fromJson(request.body).object());
            QVERIFY(writeFile(out, QJsonDocument(requests).toJson(QJsonDocument::Indented)));
        }
    }

    // The switch shows on Getting started's Updates page and in Settings >
    // Updates and privacy, both bound to privacy.usageData, on by default:
    // off in Getting started stops sending; Settings then shows it off and
    // turns it on again (a new id).
    void switchesShowAndShareTheSetting()
    {
        endpoint = std::make_unique<test::AnalyticsEndpoint>();
        start(config());
        app::Analytics &analytics = application->analytics();
        QVERIFY(analytics.active());
        analytics.capture(QStringLiteral("getting_started_skipped"));
        const QString firstId = analytics.distinctId();

        QVERIFY(trigger("gettingStartedMenuItem"));
        auto *setup = window()->findChild<QObject *>(QStringLiteral("gettingStartedDialog"));
        QVERIFY(setup);
        QTRY_VERIFY(setup->property("visible").toBool());
        setup->setProperty("page", int(setup->property("pages").toStringList().indexOf(QStringLiteral("updates"))));
        QTRY_COMPARE(setup->property("pageId").toString(), QStringLiteral("updates"));
        auto *setupSwitch = in(setup, "gettingStartedUsageData");
        QVERIFY(setupSwitch);
        QTRY_VERIFY(setupSwitch->isVisible());
        QCOMPARE(setupSwitch->property("text").toString(), QStringLiteral("Send anonymous usage data"));
        QVERIFY(setupSwitch->property("checked").toBool());
        QVERIFY(in(setup, "gettingStartedWhatIsSent"));
        shoot(setup, QStringLiteral("an1-getting-started"));
        QVERIFY(QMetaObject::invokeMethod(setupSwitch, "toggle"));
        QVERIFY(QMetaObject::invokeMethod(setupSwitch, "toggled"));
        QCOMPARE(setup->property("values").toMap().value(QStringLiteral("privacy.usageData")).toBool(), false);
        for (int i = 0; i < 4 && setup->property("visible").toBool(); ++i) // Next to the end, then Finish
            QVERIFY(QMetaObject::invokeMethod(in(setup, "gettingStartedNext"), "click"));
        QTRY_VERIFY(!setup->property("visible").toBool());
        QCOMPARE(application->settingsStore()->boolean("privacy.usageData"), false);
        QVERIFY(!analytics.active());
        QVERIFY(analytics.queued().isEmpty());
        QVERIFY(analytics.distinctId().isEmpty());

        QVERIFY(trigger("settingsMenuItem"));
        QObject *settings = settingsWindow();
        QVERIFY(settings);
        QTRY_VERIFY(settings->property("visible").toBool());
        auto *pages = in(settings, "settingsPages");
        const auto list = settings->property("pages").toList();
        for (qsizetype i = 0; i < list.size(); ++i)
            if (list[i].toMap().value(QStringLiteral("id")).toString() == QLatin1String("updates")) {
                QCOMPARE(list[i].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Updates and privacy"));
                pages->setProperty("currentIndex", int(i));
            }
        auto *settingsSwitch = in(settings, "setting_privacy.usageData");
        QVERIFY(settingsSwitch);
        QTRY_VERIFY(settingsSwitch->isVisible());
        QCOMPARE(settingsSwitch->property("text").toString(), QStringLiteral("Send anonymous usage data"));
        QVERIFY(!settingsSwitch->property("checked").toBool()); // the same setting
        QVERIFY(in(settings, "settingsWhatIsSent"));
        QVERIFY(in(settings, "setting_privacy.alwaysSendCrashReports")->isVisible()); // CR1: a build with the key
        QVERIFY(QMetaObject::invokeMethod(settingsSwitch, "click"));
        QTRY_VERIFY(settingsSwitch->property("checked").toBool());
        shoot(settings, QStringLiteral("an1-settings"));
        QVERIFY(QMetaObject::invokeMethod(in(settings, "settingsOk"), "click"));
        QTRY_VERIFY(!settings->property("visible").toBool());
        QCOMPARE(application->settingsStore()->boolean("privacy.usageData"), true);
        QVERIFY(analytics.active());
        QVERIFY(analytics.capture(QStringLiteral("getting_started_skipped")));
        QVERIFY(!analytics.distinctId().isEmpty());
        QVERIFY(analytics.distinctId() != firstId);
    }
    // CR1 (#468): a build without the PostHog key sends no crash report,
    // so Settings has no "Always send crash reports"; the usage data switch
    // still shows.
    void alwaysSendCrashReportsNeedsTheKey()
    {
        app::Analytics::Config keyless;
        keyless.queueFile = folder + QStringLiteral("/analytics-queue.json");
        start(keyless);
        QVERIFY(trigger("settingsMenuItem"));
        QObject *settings = settingsWindow();
        QVERIFY(settings);
        QTRY_VERIFY(settings->property("visible").toBool());
        const auto list = settings->property("pages").toList();
        for (qsizetype i = 0; i < list.size(); ++i)
            if (list[i].toMap().value(QStringLiteral("id")).toString() == QLatin1String("updates"))
                in(settings, "settingsPages")->setProperty("currentIndex", int(i));
        QTRY_VERIFY(in(settings, "setting_privacy.usageData")->isVisible());
        auto *always = in(settings, "setting_privacy.alwaysSendCrashReports");
        QVERIFY(always);
        QVERIFY(!always->isVisible());
        QVERIFY(QMetaObject::invokeMethod(in(settings, "settingsCancel"), "click"));
        QTRY_VERIFY(!settings->property("visible").toBool());
    }
};

QTEST_MAIN(AnalyticsSessionTest)
#include "analytics_session_tests.moc"
