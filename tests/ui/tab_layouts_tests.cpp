#include "hikari/app/application.h"
#include "hikari/app/session_json.h"
#include "hikari/application/session_file.h"

#include "docking.h"
#include "legacy_session.h"
#include "workspace_layout.h"

#include "qtest_parts.h"

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

#include <algorithm>
#include <memory>

using namespace hikari;

namespace {

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

ui::WorkspaceArrangement layoutOf(const application::SessionTab &tab)
{
    const auto arrangement = ui::WorkspaceArrangement::parse(QByteArray::fromStdString(tab.layout));
    return arrangement.value_or(ui::WorkspaceArrangement{QStringLiteral("none"), {}});
}

int floatingWindows()
{
    return static_cast<int>(KDDockWidgets::DockRegistry::self()->floatingWindows().size());
}

}

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
        application->addPage();
        QCOMPARE(application->currentTab(), 0);
    }
    void stop()
    {
        application->workspaceLayout().save();
        application->endSession();
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
    ui::Docking *docking() const { return engine->singletonInstance<ui::Docking *>("Hikari.Ui", "Docking"); }
    QRectF sceneRect(const char *name) const
    {
        auto *item = window()->findChild<QQuickItem *>(QLatin1String(name));
        return QRectF(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
    }
    void showPanel(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock(name)))));
        QTRY_VERIFY(isOpen(name));
        QVERIFY(!isFloating(name));
    }
    void openVideo()
    {
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        dismissMismatch();
    }
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

    void aPanelWithoutAPlaceOpensRightOfTheGrid_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("first");
        QTest::addColumn<bool>("engineOpen");
        for (const char *preset : {"Editing", "Timing", "Translation", "Typesetting", "Karaoke"})
            QTest::addRow("%s, Shift times", preset) << QString::fromLatin1(preset) << QStringLiteral("timing") << false;
        for (const char *first : {"search", "automation"})
            QTest::addRow("Editing, %s", first) << QStringLiteral("Editing") << QString::fromLatin1(first) << false;
        for (const char *first : {"timing", "search", "automation"})
            QTest::addRow("Editing, %s, the engine's open()", first) << QStringLiteral("Editing") << QString::fromLatin1(first) << true;
    }

    void aPanelWithoutAPlaceOpensRightOfTheGrid()
    {
        QFETCH(QString, preset);
        QFETCH(QString, first);
        QFETCH(bool, engineOpen);
        const auto open = [&](const char *name) {
            if (!engineOpen)
                return showPanel(name);
            QVERIFY(QMetaObject::invokeMethod(dock(name), "open"));
            QTRY_VERIFY(isOpen(name) && !isFloating(name));
        };
        start();
        applyPreset(qPrintable(preset));
        QTRY_COMPARE(this->preset(), preset);
        QStringList order{QStringLiteral("timing"), QStringLiteral("search"), QStringLiteral("automation")};
        order.move(order.indexOf(first), 0);
        const auto uniqueName = [&](const QString &panel) {
            return dock((panel + QLatin1String("Dock")).toLatin1().constData())->property("uniqueName").toString();
        };
        for (const QString &panel : order)
            QVERIFY2(!docking()->hasPlace(uniqueName(panel)), qPrintable(panel));
        const QRectF before = sceneRect("gridPanel");
        const QByteArray firstDock = (first + QLatin1String("Dock")).toLatin1();
        const QByteArray firstPanel = (first + QLatin1String("Panel")).toLatin1();
        const QString firstName = uniqueName(first);
        open(firstDock.constData());
        QTRY_VERIFY(window()->findChild<QQuickItem *>(QLatin1String(firstPanel))->isVisible());
        QTRY_VERIFY(sceneRect(firstPanel.constData()).left() > sceneRect("gridPanel").right() - 1);
        const QRectF grid = sceneRect("gridPanel");
        const QRectF column = sceneRect(firstPanel.constData());
        const QString where = QStringLiteral("grid %1,%2 %3x%4 (was %5 wide) %6 %7,%8 %9x%10")
                                  .arg(grid.x()).arg(grid.y()).arg(grid.width()).arg(grid.height()).arg(before.width())
                                  .arg(first).arg(column.x()).arg(column.y()).arg(column.width()).arg(column.height());
        QVERIFY2(qAbs(grid.left() - before.left()) < 1, qPrintable(where));
        QVERIFY2(qAbs(column.bottom() - grid.bottom()) < 2, qPrintable(where));
        QVERIFY2(column.top() < grid.top() + 1, qPrintable(where));
        QVERIFY2(column.width() <= std::max(qreal(docking()->minimumSize(firstName).width()), before.width() / 3) + 2,
                 qPrintable(where));
        QCOMPARE(docking()->tabCount(firstName), 1);
        QVERIFY(docking()->tabCount(QStringLiteral("Grid")) == 1);

        for (qsizetype i = 1; i < order.size(); ++i) {
            open((order[i] + QLatin1String("Dock")).toLatin1().constData());
            QTRY_COMPARE(docking()->tabCount(firstName), int(i + 1));
            QCOMPARE(docking()->tabCount(uniqueName(order[i])), int(i + 1));
        }
        const bool gridAtItsMinimum = docking()->placeInLayout(QStringLiteral("Grid")).width()
                                      <= docking()->minimumSize(QStringLiteral("Grid")).width() + 1;
        QVERIFY2(qAbs(sceneRect("gridPanel").left() - before.left()) < 1 || gridAtItsMinimum,
                 qPrintable(QStringLiteral("grid left %1, was %2; column %3 wide; grid group %4 wide, minimum %5")
                                .arg(sceneRect("gridPanel").left()).arg(before.left()).arg(sceneRect(firstPanel.constData()).width())
                                .arg(docking()->placeInLayout(QStringLiteral("Grid")).width())
                                .arg(docking()->minimumSize(QStringLiteral("Grid")).width())));

        for (const QString &panel : order)
            close((panel + QLatin1String("Dock")).toLatin1().constData());
        open("timingDock");
        QTRY_VERIFY(sceneRect("timingPanel").left() > sceneRect("gridPanel").right() - 1);

        QVERIFY(application->workspaceLayout().resetCurrentLayout());
        QTRY_VERIFY(!isOpen("timingDock"));
        QTRY_VERIFY(qAbs(sceneRect("gridPanel").width() - before.width()) < 2);
        stop();
    }

    void openingAPanelTwiceInARowLandsItOnce()
    {
        start();
        applyPreset("Editing");
        QTRY_COMPARE(preset(), QStringLiteral("Editing"));
        const QString name = dock("searchDock")->property("uniqueName").toString();
        QVERIFY(!docking()->hasPlace(name));
        const auto shownBesideTheMain = [this] {
            const QWindowList windows = QGuiApplication::topLevelWindows();
            return std::count_if(windows.begin(), windows.end(), [this](QWindow *w) { return w != window() && w->isVisible(); });
        };
        const auto before = shownBesideTheMain();
        QVERIFY(QMetaObject::invokeMethod(dock("searchDock"), "open"));
        QCOMPARE(shownBesideTheMain(), before);
        QVERIFY(QMetaObject::invokeMethod(dock("searchDock"), "open"));
        QCOMPARE(shownBesideTheMain(), before);
        QTRY_VERIFY(isOpen("searchDock") && !isFloating("searchDock"));
        QTRY_VERIFY(sceneRect("searchPanel").left() > sceneRect("gridPanel").right() - 1);
        QCOMPARE(docking()->dockRightOf(QStringLiteral("Grid")), name);
        QTest::qWait(50);
        QCOMPARE(docking()->dockRightOf(QStringLiteral("Grid")), name);
        QVERIFY(sceneRect("searchPanel").left() > sceneRect("gridPanel").right() - 1);
        QCOMPARE(docking()->tabCount(name), 1);
        QCOMPARE(floatingWindows(), 0);
        QCOMPARE(shownBesideTheMain(), before);
        stop();
    }

    void switchingTabsSwapsTheirArrangements()
    {
        start();
        auto &layout = application->workspaceLayout();
        float_("audioDock", true);
        QVERIFY(layout.resizePanel(dock("videoDock"), 470, layout.panelSize(dock("videoDock")).height()));
        QTRY_VERIFY(qAbs(layout.panelSize(dock("videoDock")).width() - 470) <= 2);

        application->addPage();
        QCOMPARE(application->currentTab(), 1);
        QVERIFY(isFloating("audioDock"));
        QCOMPARE(layout.panelSize(dock("videoDock")).width(), 470);
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

    void aTranslationFileStartsWithTheTranslationPreset()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("a.ass")))));
        QCOMPARE(application->currentTab(), 0);
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
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(!isOpen("audioDock"));

        application->selectTab(0);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QVERIFY(isOpen("audioDock"));
        QVERIFY(isFloating("audioDock"));

        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("tl2.ass")), true)));
        QCOMPARE(application->tabs().size(), qsizetype(4));
        QCOMPARE(application->currentTab(), 0);
        QVERIFY(isFloating("audioDock"));
        application->selectTab(3);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(!isOpen("audioDock"));
        stop();
    }

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
        applyPreset("Editing");
        QTRY_VERIFY(isOpen("audioDock"));
        application->selectTab(1);
        QCOMPARE(preset(), QStringLiteral("Timing"));
        QVERIFY(!isOpen("videoDock"));
        QVERIFY(isOpen("audioDock"));
        stop();
    }

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
        QCOMPARE(preset(), QStringLiteral("Timing"));
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

    // KDDockWidgets closes every panel when the main window's close goes ahead.
    void theClosingSessionNamesTheArrangementShown()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("c.ass")))));
        float_("audioDock", true);
        int closedShown = 0;
        for (const char *dock : {"videoDock", "audioDock", "editorDock", "gridDock", "referenceDock", "timingDock",
                                 "searchDock", "automationDock", "originalDock", "logDock", "spellDock", "stylesDock",
                                 "templateDock", "postprocessorDock", "qcNotesDock", "reviewDock", "agentDock",
                                 "nodeGraphDock", "nodeInspectorDock", "nodeSourcesDock", "explorerDock"})
            closedShown += isOpen(dock) ? 0 : 1;
        QVERIFY(closedShown < 18);
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
        const auto written = readSession(application->lastSessionPath());
        QCOMPARE(written.tabs.size(), std::size_t(2));
        QCOMPARE(layoutOf(written.tabs[1]).preset, QStringLiteral("Editing"));
        application->selectTab(0);
        QVERIFY(!isOpen("videoDock"));
        QVERIFY(isFloating("audioDock"));
        QVERIFY(isOpen("editorDock"));
        stop();
    }
};

HIKARI_QTEST_MAIN_IN_PARTS(TabLayoutsTest)
#include "tab_layouts_tests.moc"
