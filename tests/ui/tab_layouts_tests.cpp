// WS1 (ADR 0020): each Document tab keeps its own panel arrangement in the
// shell. Switching tabs swaps it with the content (docked, floating, sizes),
// and leaves the panels alone between tabs that look the same; a new tab is
// a copy of the current one, a TLMode file's tab starts with the Translation
// preset; a preset changes the current tab only; the Session keeps each
// tab's arrangement, also as the window closes; a tab whose arrangement is
// invalid or restores only in part shows the Editing layout with a notice.

#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/application/session_file.h"

#include "docking.h"
#include "legacy_session.h"
#include "workspace_layout.h"

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/FloatingWindow.h>

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

// An ASS file of three Lines, with TLMode: Yes when `translation`.
QString writeSubtitles(const QString &path, bool translation = false)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n");
    if (translation)
        f.write("TLMode: Yes\n");
    f.write("\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    for (int i = 0; i < 3; ++i)
        f.write(QStringLiteral("Dialogue: 0,0:00:0%1.00,0:00:0%2.00,Default,,0,0,0,,line %1\n").arg(i + 1).arg(i + 2).toUtf8());
    return path;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

application::Session readSession(const QString &path)
{
    const QByteArray bytes = readAll(path);
    return app::readSession(std::string_view(bytes.constData(), bytes.size())).value_or(application::Session{});
}

// A Session tab's arrangement as written ("Layout: ").
ui::WorkspaceArrangement layoutOf(const application::SessionTab &tab)
{
    const auto arrangement = ui::WorkspaceArrangement::parse(QByteArray::fromStdString(tab.layout));
    return arrangement.value_or(ui::WorkspaceArrangement{QStringLiteral("none"), {}});
}

int floatingWindows()
{
    return static_cast<int>(KDDockWidgets::DockRegistry::self()->floatingWindows().size());
}

} // namespace

class TabLayoutsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString layoutFile() const { return dir.filePath(QStringLiteral("layout.json")); }
    void start()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        // The first tab, which takes the arrangement the window shows.
        application->addPage();
        QCOMPARE(application->currentTab(), 0);
    }
    void stop()
    {
        application->workspaceLayout().save();
        application->endSession(); // a closed session: the next start asks nothing
        engine.reset();
        application.reset();
    }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    QObject *dock(const char *name) const { return window()->findChild<QObject *>(QLatin1String(name)); }
    bool isOpen(const char *name) const { return dock(name)->property("isOpen").toBool(); }
    bool isFloating(const char *name) const { return dock(name)->property("isFloating").toBool(); }
    void close(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(dock(name), "close"));
        QTRY_VERIFY(!isOpen(name));
    }
    void float_(const char *name, bool on)
    {
        QVERIFY(dock(name)->setProperty("isFloating", on));
        QTRY_COMPARE(isFloating(name), on);
    }
    void applyPreset(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(window(), "applyPreset", Q_ARG(QVariant, QLatin1String(name))));
    }
    QString preset() const { return application->workspaceLayout().preset(); }
    void openVideo()
    {
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        dismissMismatch();
    }
    // The resolution question a video opened may ask a moment later (a
    // message box since PU6), kept as it is.
    void dismissMismatch()
    {
        auto *host = window()->findChild<QObject *>(QStringLiteral("messages"));
        QQuickWindow *box = nullptr;
        if (host && QTest::qWaitFor([&] {
                box = qvariant_cast<QQuickWindow *>(host->property("box"));
                return box && box->isVisible() && box->title() == QLatin1String("Open video");
            }, 3000)) {
            QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, QStringLiteral("keep")));
            QTRY_VERIFY(!box->isVisible());
        }
    }
    void loadSession(const QString &path)
    {
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(path));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layout.json.unrestored", "layout.json.tab.unrestored",
                                 "last-session.json", "settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

    // The tab shown gets its own arrangement back: which panels are open,
    // docked or floating, and a docked panel's size; every panel is still
    // there, each floating panel in a window of its own.
    void switchingTabsSwapsTheirArrangements()
    {
        start();
        auto &layout = application->workspaceLayout();
        float_("audioDock", true);
        // A narrow Video panel beside the Line editor.
        QVERIFY(layout.resizePanel(dock("videoDock"), 470, layout.panelSize(dock("videoDock")).height()));
        QTRY_VERIFY(qAbs(layout.panelSize(dock("videoDock")).width() - 470) <= 2);

        // A new tab is a copy of the current tab's arrangement.
        application->addPage();
        QCOMPARE(application->currentTab(), 1);
        QVERIFY(isFloating("audioDock"));
        QCOMPARE(layout.panelSize(dock("videoDock")).width(), 470);
        // ...and then its own.
        float_("audioDock", false);
        float_("editorDock", true);
        close("gridDock");
        QVERIFY(layout.resizePanel(dock("videoDock"), 700, layout.panelSize(dock("videoDock")).height()));
        QTRY_VERIFY(layout.panelSize(dock("videoDock")).width() > 600);
        const int wide = layout.panelSize(dock("videoDock")).width();

        QSignalSpy swapped(&layout, &ui::WorkspaceLayoutController::arrangementSwapped);
        application->selectTab(0);
        QCOMPARE(swapped.size(), 1);
        QVERIFY(isFloating("audioDock"));
        QVERIFY(!isFloating("editorDock"));
        for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(name), name);
        QCOMPARE(floatingWindows(), 1);
        QTRY_VERIFY(qAbs(layout.panelSize(dock("videoDock")).width() - 470) <= 2);

        application->selectTab(1);
        QVERIFY(!isOpen("gridDock"));
        QVERIFY(!isFloating("audioDock"));
        QVERIFY(isFloating("editorDock"));
        QCOMPARE(floatingWindows(), 1);
        QTRY_VERIFY(qAbs(layout.panelSize(dock("videoDock")).width() - wide) <= 2);
        QVERIFY(KDDockWidgets::DockRegistry::self()->isSane());
        QCOMPARE(KDDockWidgets::DockRegistry::self()->dockwidgets().size(), ui::WorkspaceLayoutController::panelIds().size());
        stop();
    }

    // No flicker between tabs that look the same: their arrangement is not
    // restored, so the floating panel keeps its window.
    void tabsThatLookTheSameLeaveThePanelsAlone()
    {
        start();
        float_("audioDock", true);
        QCOMPARE(floatingWindows(), 1);
        auto *before = KDDockWidgets::DockRegistry::self()->floatingWindows().first();
        application->addPage();
        application->selectTab(0);
        application->selectTab(1);
        QCOMPARE(floatingWindows(), 1);
        QCOMPARE(KDDockWidgets::DockRegistry::self()->floatingWindows().first(), before);
        stop();
    }

    // A tab whose file has TLMode: Yes starts with the Translation preset:
    // opened into a new tab, or into a background tab first shown later.
    // Other files' tabs copy the current tab.
    void aTranslationFileStartsWithTheTranslationPreset()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("a.ass")))));
        QCOMPARE(application->currentTab(), 0); // into the untouched tab
        float_("audioDock", true);
        application->addPage();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("tl.ass")), true)));
        QCOMPARE(application->currentTab(), 1);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QTRY_VERIFY(!isOpen("audioDock"));
        for (const char *name : {"videoDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(name) && !isFloating(name), name);
        QCOMPARE(floatingWindows(), 0);

        application->addPage();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("plain.ass")))));
        QCOMPARE(application->currentTab(), 2);
        QCOMPARE(preset(), QStringLiteral("Translation")); // a copy of the Translation tab
        QVERIFY(!isOpen("audioDock"));

        application->selectTab(0);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QVERIFY(isOpen("audioDock"));
        QVERIFY(isFloating("audioDock"));

        // Opened beside the current tab, shown later.
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("tl2.ass")), true)));
        QCOMPARE(application->tabs().size(), qsizetype(4));
        QCOMPARE(application->currentTab(), 0);
        QVERIFY(isFloating("audioDock"));
        application->selectTab(3);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(!isOpen("audioDock"));
        stop();
    }

    // A file opened beside the current tab (it has a file) starts as the
    // current tab was then, in the Session written at once as well, however
    // the current tab changes before it is first shown.
    void aBackgroundTabCopiesTheArrangementItOpenedWith()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("a.ass")))));
        float_("audioDock", true);
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("b.ass")))));
        QCOMPARE(application->tabs().size(), qsizetype(2));
        QCOMPARE(application->currentTab(), 0);
        QVERIFY(application->saveLastSession());
        const auto session = readSession(application->lastSessionPath());
        QCOMPARE(session.tabs.size(), std::size_t(2));
        QCOMPARE(layoutOf(session.tabs[1]).preset, QStringLiteral("Editing"));
        QCOMPARE(QJsonDocument::fromJson(layoutOf(session.tabs[1]).payload).object().value(QStringLiteral("floatingWindows")).toArray().size(), 1);
        applyPreset("Timing");
        QTRY_VERIFY(!isOpen("videoDock"));
        application->selectTab(1);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QVERIFY(isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        stop();
    }

    // Several files dropped at once: the first goes into the untouched tab,
    // which keeps its arrangement, also in the Session written meanwhile;
    // the last, a TLMode file in a new tab, is shown with Translation.
    void aMultiFileOpenKeepsTheReusedTabsArrangement()
    {
        start();
        float_("audioDock", true);
        const QVariantMap opened = application->openDropped(
            {QUrl::fromLocalFile(writeSubtitles(dir.filePath(QStringLiteral("a.ass")))),
             QUrl::fromLocalFile(writeSubtitles(dir.filePath(QStringLiteral("b.ass")), true))});
        QCOMPARE(opened.value(QStringLiteral("kind")).toString(), QStringLiteral("files"));
        QCOMPARE(application->tabs().size(), qsizetype(2));
        QCOMPARE(application->currentTab(), 1);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(application->saveLastSession());
        const auto session = readSession(application->lastSessionPath());
        QCOMPARE(session.tabs.size(), std::size_t(2));
        QCOMPARE(layoutOf(session.tabs[0]).preset, QStringLiteral("Editing"));
        QCOMPARE(QJsonDocument::fromJson(layoutOf(session.tabs[0]).payload).object().value(QStringLiteral("floatingWindows")).toArray().size(), 1);
        application->selectTab(0);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QVERIFY(isOpen("audioDock"));
        QVERIFY(isFloating("audioDock"));
        stop();
    }

    // A preset, Reset layout included, changes the current tab only.
    void presetsChangeTheCurrentTabOnly()
    {
        start();
        application->addPage();
        applyPreset("Timing");
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QTRY_VERIFY(!isOpen("videoDock"));
        application->selectTab(0);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QVERIFY(isOpen("videoDock"));
        close("audioDock");
        applyPreset("Editing"); // Reset layout
        QTRY_VERIFY(isOpen("audioDock"));
        application->selectTab(1);
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QVERIFY(!isOpen("videoDock"));
        QVERIFY(isOpen("audioDock"));
        stop();
    }

    // The Session writes each tab's arrangement and brings it back; a start
    // without a Session shows the arrangement shown last.
    void theSessionKeepsEachTabsArrangement()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("a.ass")))));
        float_("audioDock", true);
        application->addPage();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("b.ass")))));
        applyPreset("Timing");
        QTRY_VERIFY(!isOpen("videoDock"));
        float_("audioDock", false);
        const QString kls = dir.filePath(QStringLiteral("saved.hksession"));
        QCOMPARE(application->saveSessionTo(QUrl::fromLocalFile(kls)), QString());
        const auto saved = readSession(kls);
        QCOMPARE(saved.tabs.size(), std::size_t(2));
        QCOMPARE(layoutOf(saved.tabs[0]).preset, QStringLiteral("Editing"));
        QCOMPARE(QJsonDocument::fromJson(layoutOf(saved.tabs[0]).payload).object().value(QStringLiteral("floatingWindows")).toArray().size(), 1);
        QCOMPARE(layoutOf(saved.tabs[1]).preset, QStringLiteral("Timing"));
        QVERIFY(!layoutOf(saved.tabs[1]).payload.isEmpty());
        stop();

        start();
        QCOMPARE(preset(), QStringLiteral("Timing")); // the layout file: the tab shown last
        QVERIFY(!isOpen("videoDock"));
        loadSession(kls);
        QCOMPARE(application->tabs().size(), qsizetype(2));
        QCOMPARE(application->currentTab(), 1);
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QVERIFY(!isOpen("videoDock"));
        QVERIFY(!isFloating("audioDock"));
        application->selectTab(0);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QVERIFY(isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QVERIFY(application->workspaceLayout().notice().isEmpty());
        stop();
    }

    // The docking engine closes every panel when the main window's close
    // goes ahead; the Session written then names the arrangement shown.
    void theClosingSessionNamesTheArrangementShown()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("c.ass")))));
        float_("audioDock", true);
        // The arrangement shown: the panels it leaves closed (AM1's
        // Automation, NT1's Log, PU6's Spellchecker, TP1's Styles, PU2's Fix
        // minor errors, KE2's Template editor and, outside Translation mode,
        // TM1's Original text among them), not every panel.
        int closedShown = 0;
        for (const char *dock : {"videoDock", "audioDock", "editorDock", "gridDock", "referenceDock", "timingDock",
                                 "searchDock", "automationDock", "originalDock", "logDock", "spellDock", "stylesDock",
                                 "misspellDock", "templateDock"})
            closedShown += isOpen(dock) ? 0 : 1;
        QVERIFY(closedShown < 13);
        window()->close();
        QTRY_VERIFY(!window()->isVisible());
        const auto session = readSession(application->lastSessionPath());
        QVERIFY(session.closed);
        QCOMPARE(session.tabs.size(), std::size_t(1));
        const QJsonObject payload = QJsonDocument::fromJson(layoutOf(session.tabs[0]).payload).object();
        QCOMPARE(payload.value(QStringLiteral("floatingWindows")).toArray().size(), 1);
        QCOMPARE(payload.value(QStringLiteral("closedDockWidgets")).toArray().size(), closedShown);
        engine.reset();
        application.reset();
    }

    // A tab whose Session arrangement does not validate, or that the engine
    // restores only in part, shows the Editing layout; the notice names the
    // tab and why, and the rejected text is kept. The other tabs keep theirs.
    void anUnusableTabArrangementShowsEditing_data()
    {
        QTest::addColumn<QByteArray>("layout");
        QTest::addColumn<QString>("reason");
        QTest::addColumn<QString>("named");
        QTest::newRow("corrupt") << QByteArray("{\"schema\": 1, \"payl") << QStringLiteral("not a layout file")
                                 << QStringLiteral("b.ass");
        ui::WorkspaceArrangement refused{QStringLiteral("Typesetting"),
                                         R"({"serializationVersion":1,"mainWindows":[{"uniqueName":"Classic"}],"allDockWidgets":[]})"};
        QJsonObject newer = QJsonDocument::fromJson(refused.serialise()).object();
        newer.insert(QStringLiteral("schema"), 7);
        QTest::newRow("newer") << QJsonDocument(newer).toJson(QJsonDocument::Compact) << QStringLiteral("newer version")
                               << QStringLiteral("b.ass");
        QJsonObject spy = QJsonDocument::fromJson(refused.serialise()).object();
        spy.insert(QStringLiteral("panels"), QJsonArray{QStringLiteral("Spy")});
        QTest::newRow("unknown panel") << QJsonDocument(spy).toJson(QJsonDocument::Compact) << QStringLiteral("Spy")
                                       << QStringLiteral("b.ass");
        QTest::newRow("engine refuses") << refused.serialise() << QStringLiteral("could not restore")
                                        << QStringLiteral("this tab");
    }
    void anUnusableTabArrangementShowsEditing()
    {
        QFETCH(QByteArray, layout);
        QFETCH(QString, reason);
        QFETCH(QString, named);
        start();
        close("videoDock");
        float_("audioDock", true);
        application::SessionTab first, second;
        first.subtitles = QDir::toNativeSeparators(writeSubtitles(dir.filePath(QStringLiteral("a.ass")))).toStdString();
        first.layout = application->workspaceLayout().tabArrangement(application->workspace().editingTarget()->value)
                           ->serialise()
                           .toStdString();
        second.subtitles = QDir::toNativeSeparators(writeSubtitles(dir.filePath(QStringLiteral("b.ass")))).toStdString();
        second.layout = layout.toStdString();
        const QString kls = dir.filePath(QStringLiteral("bad.kls"));
        {
            QFile f(kls);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QByteArray::fromStdString(testing::writeLegacySession("HikariSub v1", false, {first, second})));
        }
        close("editorDock");
        loadSession(kls);
        QCOMPARE(application->currentTab(), 1);
        const QString notice = application->workspaceLayout().notice();
        QVERIFY2(notice.contains(reason) && notice.contains(named), qPrintable(notice));
        QVERIFY2(notice.contains(QStringLiteral("layout.json.tab.unrestored")), qPrintable(notice));
        QCOMPARE(readAll(layoutFile() + QStringLiteral(".tab.unrestored")), layout);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(isOpen(name) && !isFloating(name), name);
        // The Session now names the Editing layout for that tab.
        const auto written = readSession(application->lastSessionPath());
        QCOMPARE(written.tabs.size(), std::size_t(2));
        QCOMPARE(layoutOf(written.tabs[1]).preset, QStringLiteral("Editing"));
        // The other tab keeps its own.
        application->selectTab(0);
        QVERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QVERIFY(isOpen("editorDock"));
        stop();
    }
};

QTEST_MAIN(TabLayoutsTest)
#include "tab_layouts_tests.moc"
