#include "hikari/app/api_host.h"
#include "hikari/app/application.h"
#include "hikari/app/forwarded_launches.h"
#include "hikari/app/hotkeys_controller.h"

#include "docking.h"
#include "qtest_parts.h"
#include "status_bar_controller.h"
#include "workspace_layout.h"

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>
#include <QtTest>

#include <functional>
#include <memory>
#include <string_view>

using namespace hikari;

namespace {

const char *const kPanels[] = {"videoDock",     "audioDock",      "originalDock",  "editorDock", "gridDock",
                               "referenceDock", "timingDock",     "searchDock",    "automationDock",
                               "spellDock",     "stylesDock",     "templateDock",  "logDock",
                               "postprocessorDock", "qcNotesDock", "reviewDock", "agentDock"};

QString writeSubtitles(const QString &path, const char *text, int width, const char *more = "")
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(QStringLiteral("[Script Info]\nScriptType: v4.00+\nPlayResX: %1\nPlayResY: %2\n\n")
                .arg(width)
                .arg(width * 9 / 16)
                .toUtf8());
    const QByteArray style = ",Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n";
    f.write("[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
            "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
            "Alignment, MarginL, MarginR, MarginV, Encoding\n");
    f.write("Style: Default" + style + "Style: " + text + "Style" + style + "\n[Events]\n");
    f.write("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    f.write(QByteArray("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,") + text + "\n" + more);
    return path;
}

}

class ShellWindowsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QPointer<QQuickWindow> second;
    application::DocumentId firstTab;
    application::DocumentId secondTab;

    QQuickWindow *first() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    static QQuickItem *item(QQuickWindow *window, const char *name)
    {
        return window->findChild<QQuickItem *>(QLatin1String(name));
    }
    static QObject *dock(QQuickWindow *window, const char *name) { return window->findChild<QObject *>(QLatin1String(name)); }
    QVariantList rows(int window) const { return application->windowTabs(window); }
    bool modified(application::DocumentId id) const
    {
        for (const auto &row : application->windowTabs(0) + application->windowTabs(1))
            if (row.toMap().value(QStringLiteral("id")).toULongLong() == id.value)
                return row.toMap().value(QStringLiteral("modified")).toBool();
        return false;
    }
    std::optional<application::DocumentId> target() const { return application->workspace().editingTarget(); }
    static QObject *controller(QQuickWindow *window, const char *name) { return window->property(name).value<QObject *>(); }
    static QString editorText(QQuickWindow *window) { return controller(window, "editor")->property("text").toString(); }
    static QStringList statusFields(QQuickWindow *window)
    {
        return controller(window, "statusBar")->property("fields").toStringList();
    }
    static QVariantList overlay(QQuickWindow *window) { return controller(window, "visualTools")->property("overlay").toList(); }
    QQuickWindow *shellWindow(int window) const { return window == 0 ? first() : second.data(); }
    void focus(int window) { focus(shellWindow(window)); }
    void focus(QQuickWindow *window)
    {
        window->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), window);
        QTRY_COMPARE(application->workspace().activeWindow(), shellId(window));
    }
    static QList<QQuickItem *> itemsNamed(QQuickWindow *window, const QString &name)
    {
        QList<QQuickItem *> out;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
            if (i->objectName() == name && i->isVisible())
                out << i;
            for (QQuickItem *c : i->childItems())
                walk(c);
        };
        walk(window->contentItem());
        return out;
    }
    static QQuickItem *header(const QString &panel)
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *qw = qobject_cast<QQuickWindow *>(w); qw && qw->isVisible())
                for (QQuickItem *h : itemsNamed(qw, QStringLiteral("dockHeader")))
                    if (h->property("currentName").toString() == panel)
                        return h;
        return nullptr;
    }
    static QQuickItem *childNamed(QQuickItem *from, const QString &name)
    {
        for (QQuickItem *c : from->childItems()) {
            if (c->objectName() == name)
                return c;
            if (QQuickItem *found = childNamed(c, name))
                return found;
        }
        return nullptr;
    }
    static void sendMouse(QWindow *w, QEvent::Type type, QPoint global, Qt::MouseButtons buttons)
    {
        QCursor::setPos(global);
        const QPointF local = QPointF(global - w->position());
        const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, local, local, QPointF(global), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &event);
        QCoreApplication::processEvents();
    }
    static bool dragOverGrid(QWindow *floating, QQuickItem *title, QQuickWindow *over)
    {
        QQuickItem *grid = item(over, "gridDock");
        const QPoint start = floating->mapToGlobal(title->mapToScene(QPointF(6, title->height() / 2)).toPoint());
        const QPoint to = over->mapToGlobal(grid->mapToScene(QPointF(grid->width() / 8, grid->height() / 2)).toPoint());
        sendMouse(floating, QEvent::MouseButtonPress, start, Qt::LeftButton);
        for (int i = 1; i <= 4; ++i)
            sendMouse(floating, QEvent::MouseMove, start + QPoint(4 * i, 0), Qt::LeftButton);
        const QPoint from = start + QPoint(16, 0);
        for (int i = 1; i <= 12; ++i)
            sendMouse(floating, QEvent::MouseMove, from + (to - from) * i / 12, Qt::LeftButton);
        const bool highlighted = QTest::qWaitFor([&] { return !itemsNamed(over, QStringLiteral("dockDropHighlight")).isEmpty(); }, 1000);
        sendMouse(floating, QEvent::MouseButtonRelease, to, Qt::NoButton);
        return highlighted;
    }
    static void click(QQuickWindow *window, QQuickItem *on)
    {
        QVERIFY(on);
        QTest::mouseClick(window, Qt::LeftButton, {}, on->mapToScene(QPointF(on->width() / 2, on->height() / 2)).toPoint());
    }
    static void showPanel(QQuickWindow *window, const char *name)
    {
        QObject *panel = dock(window, name);
        QVERIFY2(panel, name);
        QVERIFY(QMetaObject::invokeMethod(window, "showPanel", Q_ARG(QVariant, QVariant::fromValue(panel))));
        QTRY_VERIFY2(panel->property("isOpen").toBool(), name);
    }
    void openToolPanels()
    {
        for (const int window : {0, 1}) {
            focus(window);
            QQuickWindow *shell = shellWindow(window);
            for (const char *name : {"stylesDock", "templateDock", "timingDock", "spellDock"})
                showPanel(shell, name);
            QVERIFY(QMetaObject::invokeMethod(shell, "openFind", Q_ARG(QVariant, false)));
            QTRY_VERIFY(item(shell, "findText"));
            QVERIFY(QMetaObject::invokeMethod(shell, "openKanjiTimer"));
        }
    }
    static QStringList styleRows(QQuickWindow *window)
    {
        QStringList out;
        for (const QVariant &row : item(window, "stylesTool")->property("rows").toList())
            out << row.toMap().value(QStringLiteral("name")).toString() + u':' +
                       QString::number(row.toMap().value(QStringLiteral("lines")).toInt());
        return out;
    }
    void raise(const char *name, int active)
    {
        for (QQuickWindow *window : {first(), second.data()})
            showPanel(window, name);
        focus(active);
    }
    void raiseFind(int active)
    {
        raise("searchDock", active);
        QTest::qWait(300);
        for (QQuickWindow *window : {first(), second.data()})
            item(window, "findText")->setProperty("text", QStringLiteral("i"));
    }
    static QString findCount(QQuickWindow *window) { return item(window, "findCount")->property("text").toString(); }
    static QString shiftedText(QQuickWindow *window)
    {
        const QVariantList shown = item(window, "shiftTimesPanel")->property("plan").toMap().value(QStringLiteral("rows")).toList();
        return shown.isEmpty() ? QString() : shown.first().toMap().value(QStringLiteral("text")).toString();
    }
    static QString misspelling(QQuickWindow *window) { return item(window, "spellMisspell")->property("text").toString(); }
    static QStringList kanjiStyles(QQuickWindow *window) { return controller(window, "kanjiTimer")->property("styles").toStringList(); }
    static int templates(QQuickWindow *window) { return controller(window, "templateEditor")->property("templateCount").toInt(); }
    void loadFirstWindow()
    {
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(first()));
        QTRY_VERIFY(first()->property("arrangementSettled").toBool());
    }
    static int shellId(QQuickWindow *window) { return window->property("shellWindow").toInt(); }
    QStringList titles(int window) const
    {
        QStringList out;
        for (const QVariant &row : application->windowTabs(window))
            out << row.toMap().value(QStringLiteral("title")).toString() +
                       (row.toMap().value(QStringLiteral("current")).toBool() ? u"*" : u"");
        return out;
    }
    QQuickWindow *otherShellWindow(QWindow *besides) const
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *shell = qobject_cast<QQuickWindow *>(w); shell && shell != besides && shell->isVisible() && shellId(shell) > 0)
                return shell;
        return nullptr;
    }
    QJsonObject saveSession(const QString &file)
    {
        if (application->saveSessionTo(QUrl::fromLocalFile(file)) != QString())
            return {};
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    }
    static void writeJson(const QString &file, const QJsonObject &json)
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(json).toJson());
    }
    void restartWith(const QJsonObject &lastSession)
    {
        const QString last = application->lastSessionPath();
        second->close();
        QTRY_VERIFY(!second);
        application->endSession();
        engine.reset();
        application.reset();
        writeJson(last, lastSession);
        relaunch();
    }
    void relaunch()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        loadFirstWindow();
    }
    void edit(application::DocumentId id)
    {
        auto *session = application->files().session(id);
        QVERIFY(session);
        QVERIFY(session->editDraftText(session->document().lines().front()->id, u8"edited"));
        QVERIFY(session->commitDraft());
        QTRY_VERIFY(modified(id));
    }
    static QObject *unsaved(QQuickWindow *window) { return window->findChild<QObject *>(QStringLiteral("unsavedChanges")); }
    static QVariantList reviewed(QQuickWindow *window) { return unsaved(window)->property("rows").toList(); }
    void closeDiscarding(QQuickWindow *window)
    {
        QVariantList choices;
        for (const QVariant &row : application->reviewCloseWindow(shellId(window)))
            choices << QVariantMap{{QStringLiteral("id"), row.toMap().value(QStringLiteral("id"))}, {QStringLiteral("save"), false}};
        if (choices.isEmpty())
            application->finishClose();
        else
            application->resolveClose(choices);
    }
    static QWindow *floatingOf(const QWindow *main)
    {
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w != main && w->isVisible() && w->transientParent() == main)
                return w;
        return nullptr;
    }
    void tearDownAfterQuit()
    {
        delete second.data();
        engine.reset();
        application.reset();
    }
    void hideFirst()
    {
        first()->close();
        QTRY_VERIFY(!first()->isVisible());
    }
    void quitFrom(QQuickWindow *window)
    {
        QVariant ran;
        QVERIFY(QMetaObject::invokeMethod(window, "globalHotkey", Q_RETURN_ARG(QVariant, ran),
                                          Q_ARG(QVariant, QStringLiteral("GLOBAL_QUIT"))));
        QVERIFY(ran.toBool());
    }
    QQuickWindow *newestShellWindow() const
    {
        QQuickWindow *newest = nullptr;
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *shell = qobject_cast<QQuickWindow *>(w);
                shell && shell->isVisible() && shellId(shell) > (newest ? shellId(newest) : 0))
                newest = shell;
        return newest;
    }
    application::DocumentId openTab(int window, const char *name)
    {
        focus(window);
        [&] { QVERIFY(application->openFile(writeSubtitles(dir.filePath(QLatin1String(name) + u".ass"), name, 640))); }();
        for (const QVariant &row : application->windowTabs(window))
            if (row.toMap().value(QStringLiteral("title")).toString() == QLatin1String(name) + u".ass")
                return application::DocumentId{row.toMap().value(QStringLiteral("id")).toULongLong()};
        return {};
    }
    static QQuickItem *tab(QQuickWindow *window, int index)
    {
        const auto found = itemsNamed(window, QStringLiteral("documentTab%1").arg(index));
        return found.isEmpty() ? nullptr : found.front();
    }
    QPoint tabCentre(QQuickWindow *window, int index) const
    {
        QQuickTest::qWaitForPolish(window);
        QQuickItem *tab = ShellWindowsTest::tab(window, index);
        return tab ? window->mapToGlobal(tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2))).toPoint() : QPoint();
    }
    void sideBySide()
    {
        first()->setGeometry(0, 0, 790, 470);
        second->setGeometry(490, 480, 790, 470);
        QTRY_COMPARE(second->position(), QPoint(490, 480));
        QTRY_VERIFY(second->property("arrangementSettled").toBool());
    }
    static void dragTab(QQuickWindow *window, QPoint from, QPoint to)
    {
        const QPoint below = from + QPoint(0, 80);
        sendMouse(window, QEvent::MouseButtonPress, from, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
            sendMouse(window, QEvent::MouseMove, from + (below - from) * i / 8, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
            sendMouse(window, QEvent::MouseMove, below + (to - below) * i / 8, Qt::LeftButton);
        sendMouse(window, QEvent::MouseButtonRelease, to, Qt::NoButton);
    }
    QStringList menuTitles(QObject *menu) const
    {
        QStringList out;
        for (int i = 0;; ++i) {
            QObject *row = menu->findChild<QObject *>(QStringLiteral("moveTabToWindow%1").arg(i));
            if (!row)
                return out;
            out << row->property("text").toString();
        }
    }
    void loadSession(const QString &file)
    {
        const QPointer<QQuickWindow> before = second;
        const QVariantMap review = application->reviewSession(QUrl::fromLocalFile(file));
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QTRY_VERIFY(!before);
        QTRY_VERIFY((second = otherShellWindow(first())));
        QTRY_VERIFY(second->property("arrangementSettled").toBool());
    }

private slots:
    void init()
    {
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("Dictionary")));
        for (const char *file : {"Dictionary/en_US.aff", "Dictionary/en_US.dic"}) {
            QFile f(dir.filePath(QLatin1String(file)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            if (QByteArrayView(file).endsWith(".dic"))
                f.write("1\nHello\n");
        }
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("first.ass")), "first", 640,
                                                     "Comment: 0,0:00:00.00,0:00:00.00,Default,,0,0,0,template line,"
                                                     "{\\fad(100,100)}\n")));
        firstTab = *target();
        loadFirstWindow();

        second = application->openShellWindow(*engine);
        QVERIFY(second);
        second->show();
        QVERIFY(QTest::qWaitForWindowExposed(second));
        QTRY_VERIFY(second->property("arrangementSettled").toBool());
        QCOMPARE(application->workspace().activeWindow(), 1);
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("second.ass")), "second", 1280)));
        secondTab = *target();
        QVERIFY(secondTab != firstTab);
    }
    void cleanup()
    {
        while (QPointer<QQuickWindow> other = application->quitApproved() ? nullptr : newestShellWindow()) {
            closeDiscarding(other);
            QTRY_VERIFY(!other);
        }
        application->setSessionRestore(0);
        application->endSession();
        engine.reset();
        application.reset();
    }

    void theSecondWindowIsAFullShell()
    {
        for (const char *name : kPanels) {
            QObject *panel = dock(second, name);
            QVERIFY2(panel, name);
            QCOMPARE(panel->property("uniqueName").toString(), dock(first(), name)->property("uniqueName").toString() + u"~1");
        }
        QVERIFY(item(second, "mainTitleBar"));
        QObject *menus = second->findChild<QObject *>(QStringLiteral("appMenus"));
        QVERIFY(menus);
        QCOMPARE(menus->property("count").toInt(), first()->findChild<QObject *>(QStringLiteral("appMenus"))->property("count").toInt());
        QVERIFY(item(second, "documentTabBar")->property("shown").toBool());
        for (const char *own : {"shell", "editor", "visualTools", "videoView", "statusBar", "templateEditor", "kanjiTimer"})
            QVERIFY2(controller(second, own) != controller(first(), own), own);
        QCOMPARE(second->property("hotkeys").value<QObject *>(), first()->property("hotkeys").value<QObject *>());
        QVERIFY(second->property("workspaceLayout").value<QObject *>() != first()->property("workspaceLayout").value<QObject *>());
    }

    void eachWindowsPanelsShowItsOwnTab()
    {
        for (const int active : {0, 1, 0}) {
            application->activateShellWindow(active);
            QCOMPARE(application->workspace().activeWindow(), active);
            QCOMPARE(editorText(first()), QStringLiteral("first"));
            QCOMPARE(editorText(second), QStringLiteral("second"));
            QCOMPARE(controller(first(), "shell")->property("editingTitle").toString(), QStringLiteral("first.ass"));
            QCOMPARE(controller(second, "shell")->property("editingTitle").toString(), QStringLiteral("second.ass"));
            QVERIFY2(statusFields(first()).contains(QStringLiteral("640 x 360")), qPrintable(statusFields(first()).join(u'|')));
            QVERIFY2(statusFields(second).contains(QStringLiteral("1280 x 720")), qPrintable(statusFields(second).join(u'|')));
        }
    }

    void eachWindowsStatusBarNamesItsTabsTimecodesAndEveryEditorIsTimed()
    {
        const QString timecodes = dir.filePath(QStringLiteral("second-timecodes.txt"));
        {
            QFile file(timecodes);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("# timecode format v2\n0\n100\n200\n300\n");
        }
        const auto fpsTip = [](QQuickWindow *window) {
            return controller(window, "statusBar")->property("tooltips").toStringList()[ui::StatusBarController::FramesPerSecond];
        };
        const auto timed = [](QQuickWindow *window) { return controller(window, "editor")->property("framesShown").toBool(); };
        for (QQuickWindow *window : {first(), second.data()})
            QVERIFY(controller(window, "editor")->setProperty("showFrames", true));
        QCOMPARE(application->openTimecodesFile(timecodes), QString());
        for (const int active : {1, 0, 1}) {
            application->activateShellWindow(active);
            QCOMPARE(application->workspace().activeWindow(), active);
            QVERIFY2(fpsTip(second).contains(QStringLiteral("second-timecodes.txt")), qPrintable(fpsTip(second)));
            QVERIFY2(!fpsTip(first()).contains(QStringLiteral("second-timecodes.txt")), qPrintable(fpsTip(first())));
            QTRY_COMPARE(timed(first()), active == 1);
            QTRY_COMPARE(timed(second), active == 1);
        }
    }

    void anInactiveWindowsOverlayHidesWhileThePictureShowsAnotherTab()
    {
        const QString dummy = QStringLiteral("?dummy:25.000000:250:1280:720:47:163:254:");
        for (const int window : {0, 1}) {
            focus(window);
            application->video().openVideo(dummy);
            QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        }
        for (QQuickWindow *window : {first(), second.data()})
            QVERIFY(QMetaObject::invokeMethod(controller(window, "visualTools"), "selectFamily", Q_ARG(int, 1)));
        for (const int active : {1, 0, 1}) {
            focus(active);
            QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
            QVERIFY(application->video().showFrameAt(37));
            QQuickWindow *shown = shellWindow(active);
            QQuickWindow *hidden = shellWindow(1 - active);
            QTRY_VERIFY_WITH_TIMEOUT(!overlay(shown).isEmpty(), 20000);
            QVERIFY(overlay(hidden).isEmpty());
        }
    }

    void anEditReachesAnotherWindowOnlyWhenItChangesThatWindowsTab()
    {
        QCOMPARE(application->workspace().activeWindow(), 1);
        QSignalSpy firstGrid(controller(first(), "shell"), SIGNAL(targetsChanged()));
        QSignalSpy firstStatus(controller(first(), "statusBar"), SIGNAL(changed()));
        auto *editor = controller(second, "editor");
        QVERIFY(QMetaObject::invokeMethod(editor, "setActorText", Q_ARG(QString, QStringLiteral("Actor"))));
        bool committed = false;
        QVERIFY(QMetaObject::invokeMethod(editor, "commit", Q_RETURN_ARG(bool, committed)));
        QVERIFY(committed);
        QTRY_VERIFY(modified(secondTab));
        QCOMPARE(firstGrid.count(), 0);
        QCOMPARE(firstStatus.count(), 0);
        QCOMPARE(editorText(first()), QStringLiteral("first"));

        auto *firstSession = application->files().session(firstTab);
        QVERIFY(firstSession);
        const auto line = firstSession->document().lines().front()->id;
        QVERIFY(firstSession->editDraftText(line, u8"first, changed elsewhere"));
        QVERIFY(firstSession->commitDraft());
        QVERIFY(QMetaObject::invokeMethod(editor, "setActorText", Q_ARG(QString, QStringLiteral("Other"))));
        QVERIFY(QMetaObject::invokeMethod(editor, "commit", Q_RETURN_ARG(bool, committed)));
        QCOMPARE(application->workspace().activeWindow(), 1);
        QTRY_COMPARE(editorText(first()), QStringLiteral("first, changed elsewhere"));
        QVERIFY(firstGrid.count() > 0);
    }

    void eachWindowsToolPanelsShowItsOwnTab()
    {
        openToolPanels();
        for (const int active : {1, 0, 1}) {
            focus(active);
            QTRY_COMPARE(styleRows(first()), (QStringList{QStringLiteral("Default:2"), QStringLiteral("firstStyle:0")}));
            QTRY_COMPARE(styleRows(second), (QStringList{QStringLiteral("Default:1"), QStringLiteral("secondStyle:0")}));
            QCOMPARE(templates(first()), 1);
            QCOMPARE(templates(second), 0);
            QCOMPARE(kanjiStyles(first()), (QStringList{QStringLiteral("Default"), QStringLiteral("firstStyle")}));
            QCOMPARE(kanjiStyles(second), (QStringList{QStringLiteral("Default"), QStringLiteral("secondStyle")}));
            raiseFind(active);
            QTRY_COMPARE(findCount(first()), QStringLiteral("1 match(es)"));
            QTRY_COMPARE(findCount(second), QStringLiteral("No matches"));
            raise("timingDock", active);
            QTRY_COMPARE(shiftedText(first()), QStringLiteral("first"));
            QTRY_COMPARE(shiftedText(second), QStringLiteral("second"));
            raise("spellDock", active);
            QTRY_COMPARE(misspelling(first()), QStringLiteral("first"));
            QTRY_COMPARE(misspelling(second), QStringLiteral("second"));
        }
    }

    void anEditReachesAnotherWindowsToolPanelsOnlyForItsDocument()
    {
        openToolPanels();
        focus(1);
        const auto edit = [](application::EditSession *session, const char8_t *text, const char8_t *style) {
            QVERIFY(session);
            const auto line = session->document().lines().front()->id;
            application::DraftChange change;
            change.text = text;
            change.style = style;
            QVERIFY(session->editDraft(line, change));
            QVERIFY(session->commitDraft());
        };
        const auto commitActor = [this](const QString &actor) {
            auto *editor = controller(second, "editor");
            QVERIFY(QMetaObject::invokeMethod(editor, "setActorText", Q_ARG(QString, actor)));
            bool committed = false;
            QVERIFY(QMetaObject::invokeMethod(editor, "commit", Q_RETURN_ARG(bool, committed)));
            QVERIFY(committed);
        };
        edit(application->files().session(secondTab), u8"in it is", u8"secondStyle");
        application->editor().reloadFromSession();
        commitActor(QStringLiteral("Actor"));
        raiseFind(1);
        QTRY_COMPARE(findCount(second), QStringLiteral("3 match(es)"));
        QTRY_COMPARE(styleRows(second), (QStringList{QStringLiteral("Default:0"), QStringLiteral("secondStyle:1")}));
        raise("timingDock", 1);
        QTRY_COMPARE(shiftedText(second), QStringLiteral("in it is"));
        QTest::qWait(300);
        QCOMPARE(shiftedText(first()), QStringLiteral("first"));
        raiseFind(1);
        QTest::qWait(300);
        QCOMPARE(findCount(first()), QStringLiteral("1 match(es)"));
        QCOMPARE(styleRows(first()), (QStringList{QStringLiteral("Default:2"), QStringLiteral("firstStyle:0")}));
        QCOMPARE(templates(first()), 1);

        raise("timingDock", 1);
        edit(application->files().session(firstTab), u8"it is", u8"firstStyle");
        commitActor(QStringLiteral("Other"));
        QCOMPARE(application->workspace().activeWindow(), 1);
        QTRY_COMPARE(shiftedText(first()), QStringLiteral("it is"));
        QTRY_COMPARE(styleRows(first()), (QStringList{QStringLiteral("Default:1"), QStringLiteral("firstStyle:1")}));
        QCOMPARE(shiftedText(second), QStringLiteral("in it is"));
        raiseFind(1);
        QTRY_COMPARE(findCount(first()), QStringLiteral("2 match(es)"));
        QCOMPARE(findCount(second), QStringLiteral("3 match(es)"));
    }

    void eachWindowShowsItsOwnTabs()
    {
        QCOMPARE(rows(0).size(), 1);
        QCOMPARE(rows(1).size(), 1);
        QCOMPARE(rows(0).first().toMap().value(QStringLiteral("id")).toULongLong(), firstTab.value);
        QCOMPARE(rows(1).first().toMap().value(QStringLiteral("id")).toULongLong(), secondTab.value);
        QCOMPARE(application->windowCurrentTab(0), 0);
        QVERIFY(!application->windowHome(0));

        application->activateShellWindow(0);
        QCOMPARE(target(), firstTab);
        application->activateShellWindow(1);
        QCOMPARE(target(), secondTab);
    }

    void aToolInTheInactiveWindowsPanelActsOnThatWindowsTab()
    {
        application->activateShellWindow(0);
        QCOMPARE(target(), firstTab);
        QQuickItem *lineText = item(second, "lineText");
        click(second, lineText);
        QCOMPARE(target(), secondTab);
        QTRY_VERIFY(lineText->hasActiveFocus() || second->activeFocusItem());
        QTest::keyClick(second, Qt::Key_End);
        for (const char c : std::string_view(" edited"))
            QTest::keyClick(second, c);
        QTest::keyClick(second, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(modified(secondTab));
        QVERIFY(!modified(firstTab));
    }

    void aMenuCommandActsOnItsWindowsTab()
    {
        application->activateShellWindow(0);
        QCOMPARE(target(), firstTab);
        click(second, item(second, "fileMenuBarItem"));
        QCOMPARE(target(), secondTab);
        QQuickItem *close = nullptr;
        QTRY_VERIFY((close = item(second, "closeMenuItem")) && close->isVisible());
        click(second, close);
        QTRY_VERIFY(rows(1).isEmpty());
        QCOMPARE(rows(0).size(), 1);
    }

    void aThemeChangeAppliesToBothWindows()
    {
        const QColor before = first()->color();
        QCOMPARE(second->color(), before);
        const bool light = before.lightness() > 127;
        application->applySettings({{QStringLiteral("appearance.followSystem"), false},
                                    {QStringLiteral("appearance.theme"), light ? QStringLiteral("dark") : QStringLiteral("light")}});
        QTRY_VERIFY(first()->color() != before);
        QCOMPARE(second->color(), first()->color());
    }

    void focusingAWindowRetargetsItsHotkeys()
    {
        first()->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), first());
        QTRY_COMPARE(target(), firstTab);
        second->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), second.get());
        QTRY_COMPARE(target(), secondTab);
        QTest::keyClick(second, Qt::Key_W, Qt::ControlModifier);
        QTRY_VERIFY(rows(1).isEmpty());
        QCOMPARE(rows(0).size(), 1);
        QVERIFY(application->windowHome(1));
        QVERIFY(!application->windowHome(0));
    }

    void eachWindowFloatsAndDocksItsOwnPanels()
    {
        QObject *video = dock(second, "videoDock");
        QVERIFY(video->setProperty("isFloating", true));
        QTRY_VERIFY(video->property("isFloating").toBool());
        QVERIFY(!dock(first(), "videoDock")->property("isFloating").toBool());
        QVERIFY(video->setProperty("isFloating", false));
        QTRY_VERIFY(!video->property("isFloating").toBool());
        QVERIFY(dock(second, "videoDock")->property("isOpen").toBool());
    }

    void aPanelDoesNotDockIntoTheOtherWindow()
    {
        if (QGuiApplication::platformName() != QLatin1String("offscreen"))
            QSKIP("a synthetic drag between windows needs the offscreen platform");
        second->setPosition(first()->x() + first()->width() + 40, first()->y());
        QObject *audio = dock(second, "audioDock");
        QVERIFY(audio->setProperty("isFloating", true));
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = header(QStringLiteral("Audio~1"))) && bar->window() != second);
        QWindow *floating = bar->window();
        QTRY_VERIFY(floating->isExposed());
        QQuickItem *title = childNamed(bar, QStringLiteral("dockTitleText"));
        QVERIFY(title);

        QVERIFY(!dragOverGrid(floating, title, first()));
        QTest::qWait(200);
        QVERIFY(audio->property("isFloating").toBool());
        QCOMPARE(header(QStringLiteral("Audio~1"))->window(), floating);

        QVERIFY(dragOverGrid(floating, title, second));
        QTRY_VERIFY(!audio->property("isFloating").toBool());
        QCOMPARE(header(QStringLiteral("Audio~1"))->window(), second.get());
    }

    void savedLayoutsUseTheFirstWindowsNames()
    {
        auto *layouts = qobject_cast<ui::WorkspaceLayoutController *>(second->property("workspaceLayout").value<QObject *>());
        QVERIFY(layouts);
        QVERIFY(layouts->saveLayoutAs(QStringLiteral("From the second")));
        QFile saved(layouts->savedLayoutsFile());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray text = saved.readAll();
        saved.close();
        QVERIFY(text.contains("From the second"));
        QVERIFY(!text.contains("~1"));
        QVERIFY(!text.contains("\"w1\""));
        QVERIFY(!application->workspaceLayout().savedLayoutNamed(QStringLiteral("From the second")).isEmpty());
        QVERIFY(application->workspaceLayout().applySavedLayout(QStringLiteral("From the second")));
    }

    void aHotkeyChangeAppliesToBothWindows()
    {
        QObject *firstClose = first()->findChild<QObject *>(QStringLiteral("closeMenuItem"));
        QObject *secondClose = second->findChild<QObject *>(QStringLiteral("closeMenuItem"));
        QVERIFY(firstClose && secondClose);
        const QString before = firstClose->property("keysText").toString();
        QCOMPARE(secondClose->property("keysText").toString(), before);
        auto &hotkeys = application->hotkeys();
        const QVariantMap close = hotkeys.gestureTarget(QStringLiteral("GLOBAL_CLOSE_PAGE"));
        const QString defaultKeys = hotkeys.accelOf(QStringLiteral("GLOBAL_CLOSE_PAGE"), 0);
        QVERIFY(!defaultKeys.isEmpty());
        const auto bind = [&](const QString &accel) {
            hotkeys.gestureMap(close.value(QStringLiteral("id")).toInt(), close.value(QStringLiteral("name")).toString(),
                               accel, 0, QStringLiteral("cancel"));
        };
        bind(QStringLiteral("Ctrl-Alt-K"));
        QTRY_VERIFY(firstClose->property("keysText").toString() != before);
        QCOMPARE(secondClose->property("keysText").toString(), firstClose->property("keysText").toString());

        bind(defaultKeys);
        QCOMPARE(hotkeys.accelOf(QStringLiteral("GLOBAL_CLOSE_PAGE"), 0), defaultKeys);
        QTRY_COMPARE(firstClose->property("keysText").toString(), before);
        QCOMPARE(secondClose->property("keysText").toString(), before);
    }

    void aSessionReopensEachWindowWithItsTabsAndPlace()
    {
        const QRect firstPlace(30, 60, 900, 650);
        const QRect secondPlace(100, 120, 900, 650);
        first()->setGeometry(firstPlace);
        second->setGeometry(secondPlace);
        QTRY_COMPARE(first()->geometry(), firstPlace);
        QTRY_COMPARE(second->geometry(), secondPlace);
        application->activateShellWindow(0);
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("third.ass")), "third", 640)));
        application->selectTab(0);
        QCOMPARE(titles(0), (QStringList{"first.ass*", "third.ass"}));
        application->activateShellWindow(1);
        const QString file = dir.filePath(QStringLiteral("two.hksession"));
        const QJsonObject saved = saveSession(file);
        QCOMPARE(saved.value(QStringLiteral("windows")).toArray().size(), 2);

        first()->setGeometry(QRect(10, 10, 700, 500));
        application->activateShellWindow(0);
        loadSession(file);
        QTRY_COMPARE(first()->geometry(), firstPlace);
        QTRY_COMPARE(second->geometry(), secondPlace);
        QCOMPARE(titles(0), (QStringList{"first.ass*", "third.ass"}));
        QCOMPARE(titles(shellId(second)), (QStringList{"second.ass*"}));
        QCOMPARE(application->workspace().activeWindow(), shellId(second));
        QCOMPARE(application->workspace().title(*target()) ? *application->workspace().title(*target()) : std::string(),
                 std::string("second.ass"));
    }

    void quittingBringsEveryWindowBack()
    {
        const QRect secondPlace(100, 120, 900, 650);
        second->setGeometry(secondPlace);
        QTRY_COMPARE(second->geometry(), secondPlace);
        application->setSessionRestore(2);
        QVERIFY(application->reviewClose(QStringLiteral("quit")).isEmpty());
        application->finishClose();
        QVERIFY(application->quitApproved());
        QTRY_VERIFY(!first()->isVisible());
        QTRY_VERIFY(!second || !second->isVisible());
        tearDownAfterQuit();

        relaunch();
        QTRY_VERIFY((second = otherShellWindow(first())));
        QTRY_COMPARE(titles(shellId(second)), (QStringList{"second.ass*"}));
        QTRY_COMPARE(second->geometry(), secondPlace);
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
    }

    void aCrashRestoreReopensEveryWindow()
    {
        const QRect secondPlace(100, 120, 900, 650);
        second->setGeometry(secondPlace);
        QTRY_COMPARE(second->geometry(), secondPlace);
        QJsonObject crashed = saveSession(dir.filePath(QStringLiteral("crashed.hksession")));
        QVERIFY(!crashed.isEmpty());
        crashed.insert(QStringLiteral("closed"), false);
        restartWith(crashed);

        QCOMPARE(application->startupOffer(), QStringLiteral("crash"));
        const QVariantMap review = application->reviewStartupSession();
        QVERIFY(review.value(QStringLiteral("ok")).toBool());
        application->finishClose();
        QTRY_VERIFY((second = otherShellWindow(first())));
        QTRY_COMPARE(second->geometry(), secondPlace);
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
        QCOMPARE(titles(shellId(second)), (QStringList{"second.ass*"}));
        QCOMPARE(application->workspace().activeWindow(), shellId(second));
    }

    void startingWithTheLastSessionOpensEachWindowOnce()
    {
        application->setSessionRestore(2);
        QJsonObject closed = saveSession(dir.filePath(QStringLiteral("closed.hksession")));
        QVERIFY(!closed.isEmpty());
        closed.insert(QStringLiteral("closed"), true);
        restartWith(closed);

        QTRY_VERIFY((second = otherShellWindow(first())));
        QTRY_COMPARE(titles(shellId(second)), (QStringList{"second.ass*"}));
        const int reopened = shellId(second);
        QTest::qWait(500);
        QCOMPARE(otherShellWindow(first()), second.data());
        QCOMPARE(shellId(second), reopened);
        QVERIFY(!otherShellWindow(second));
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
    }

    void aWindowOffEveryScreenComesBackWholeOnThePrimary()
    {
        const QString file = dir.filePath(QStringLiteral("gone.hksession"));
        QJsonObject session = saveSession(file);
        QJsonArray windows = session.value(QStringLiteral("windows")).toArray();
        QCOMPARE(windows.size(), 2);
        const QRect primary = QGuiApplication::primaryScreen()->availableGeometry();
        QJsonObject gone = windows[1].toObject();
        gone.insert(QStringLiteral("x"), primary.right() + 2000);
        gone.insert(QStringLiteral("y"), primary.top() + 100);
        windows[1] = gone;
        QJsonObject maximized = windows[0].toObject();
        maximized.insert(QStringLiteral("maximized"), true);
        windows[0] = maximized;
        session.insert(QStringLiteral("windows"), windows);
        writeJson(file, session);

        loadSession(file);
        QTRY_VERIFY(primary.contains(second->frameGeometry()));
        QVERIFY(!(second->windowStates() & Qt::WindowMaximized));
        QTRY_VERIFY(first()->windowStates() & Qt::WindowMaximized);
    }

    void quittingKeepsTheActiveWindowsArrangementForTheNextStart()
    {
        auto *secondLayout = qobject_cast<ui::WorkspaceLayoutController *>(second->property("workspaceLayout").value<QObject *>());
        QVERIFY(secondLayout);
        QVERIFY(secondLayout->applyPreset(QStringLiteral("Timing")));
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Editing"));
        QCOMPARE(application->workspace().activeWindow(), 1);
        QVERIFY(application->saveLayoutForQuit());
        QByteArray text;
        {
            QFile layout(dir.filePath(QStringLiteral("layout.json")));
            QVERIFY(layout.open(QIODevice::ReadOnly));
            text = layout.readAll();
        }
        QVERIFY(QFile::remove(dir.filePath(QStringLiteral("layout.json"))));
        QString problem, preset;
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(text, &problem, &preset));
        QCOMPARE(preset, QStringLiteral("Timing"));
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Editing"));
    }

    void closingAWindowReviewsOnlyItsTabs()
    {
        edit(firstTab);
        edit(secondTab);
        second->close();
        QTRY_COMPARE(reviewed(second).size(), 1);
        QCOMPARE(reviewed(second).first().toMap().value(QStringLiteral("id")).toULongLong(), secondTab.value);
        QCOMPARE(application->closeKind(), QStringLiteral("window"));
        QVERIFY(reviewed(first()).isEmpty());
        QVERIFY(QMetaObject::invokeMethod(unsaved(second), "cancel"));
        QVERIFY(QMetaObject::invokeMethod(unsaved(second), "close"));
        QVERIFY(second && second->isVisible());
        QVERIFY(dock(second, "gridDock")->property("isOpen").toBool());
        QVERIFY(modified(secondTab));

        second->close();
        QTRY_COMPARE(reviewed(second).size(), 1);
        QVERIFY(QMetaObject::invokeMethod(unsaved(second), "discardAll"));
        QTRY_VERIFY(!second);
        QVERIFY(first()->isVisible());
        QVERIFY(!application->quitApproved());
        QCOMPARE(application->workspace().allTabs(), (std::vector{firstTab}));
        QCOMPARE(application->workspace().activeWindow(), 0);
        QVERIFY(modified(firstTab));
    }

    void aClosedWindowLeavesTheCrashSession()
    {
        second->close();
        QTRY_VERIFY(!second);
        QFile file(application->lastSessionPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(saved.value(QStringLiteral("closed")).toBool(true), false);
        QVERIFY(saved.value(QStringLiteral("windows")).toArray().size() <= 1);
        const QJsonArray tabs = saved.value(QStringLiteral("tabs")).toArray();
        QCOMPARE(tabs.size(), 1);
        QVERIFY(tabs.first().toObject().value(QStringLiteral("subtitles")).toString().endsWith(QStringLiteral("first.ass")));
    }

    void closingTheLastWindowQuits()
    {
        second->close();
        QTRY_VERIFY(!second);
        edit(firstTab);
        first()->close();
        QTRY_COMPARE(reviewed(first()).size(), 1);
        QCOMPARE(application->closeKind(), QStringLiteral("quit"));
        QVERIFY(QMetaObject::invokeMethod(unsaved(first()), "discardAll"));
        QTRY_VERIFY(application->quitApproved());
        QTRY_VERIFY(!first()->isVisible());
    }

    void quitFromAnyWindowReviewsEveryWindowOnce()
    {
        application->setSessionRestore(2);
        edit(firstTab);
        edit(secondTab);
        const int reviews = application->quitReviews();
        quitFrom(second);
        QTRY_COMPARE(reviewed(second).size(), 2);
        QCOMPARE(application->closeKind(), QStringLiteral("quit"));
        QCOMPARE(application->quitReviews(), reviews + 1);
        QVERIFY(reviewed(first()).isEmpty());
        QVERIFY(first()->isVisible());
        QVERIFY(QMetaObject::invokeMethod(unsaved(second), "discardAll"));
        QTRY_VERIFY(application->quitApproved());
        QTRY_VERIFY(!first()->isVisible());
        QTRY_VERIFY(!second->isVisible());
        tearDownAfterQuit();

        relaunch();
        QTRY_VERIFY((second = otherShellWindow(first())));
        QTRY_COMPARE(titles(shellId(second)), (QStringList{"second.ass*"}));
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
    }

    void closingTheFirstWindowHidesItAsTheHost()
    {
        application->activateShellWindow(0);
        edit(firstTab);
        first()->close();
        QTRY_COMPARE(reviewed(first()).size(), 1);
        QCOMPARE(application->closeKind(), QStringLiteral("window"));
        QVERIFY(QMetaObject::invokeMethod(unsaved(first()), "discardAll"));
        QTRY_VERIFY(!first()->isVisible());
        QVERIFY(QGuiApplication::focusWindow() != first());
        QAccessibleInterface *a11y = QAccessible::queryAccessibleInterface(first());
        QVERIFY(!a11y || a11y->state().invisible);
        QVERIFY(second->isVisible());
        QVERIFY(!application->quitApproved());
        QCOMPARE(application->workspace().activeWindow(), shellId(second));
        QCOMPARE(application->workspace().allTabs(), (std::vector{secondTab}));

        application->activateShellWindow(0);
        QCOMPARE(application->workspace().activeWindow(), shellId(second));
        application->addPage(false);
        QCOMPARE(application->workspace().tabs(shellId(second)).size(), 2u);
        QVERIFY(application->workspace().tabs(0).empty());
        QVERIFY(!first()->isVisible());
    }

    void aHiddenFirstWindowLeavesTheSessionAndTheLastVisibleWindowQuits()
    {
        hideFirst();
        application->setSessionRestore(2);
        const QJsonObject saved = saveSession(dir.filePath(QStringLiteral("hidden.hksession")));
        QVERIFY(saved.value(QStringLiteral("windows")).toArray().size() <= 1);
        QCOMPARE(saved.value(QStringLiteral("tabs")).toArray().size(), 1);
        QVERIFY(!saved.value(QStringLiteral("tabs")).toArray().first().toObject().contains(QStringLiteral("window")));

        second->close();
        QTRY_VERIFY(application->quitApproved());
        QTRY_VERIFY(!second->isVisible());
        for (QWindow *window : QGuiApplication::topLevelWindows())
            QVERIFY2(!window->isVisible(), qPrintable(window->title()));
        tearDownAfterQuit();

        relaunch();
        QTest::qWait(500);
        QVERIFY(!otherShellWindow(first()));
        QCOMPARE(titles(0), (QStringList{"second.ass*"}));
    }

    void aSessionOpenedLaterShowsTheFirstWindowWithItsFloatingPanels()
    {
        QVERIFY(dock(first(), "audioDock")->setProperty("isFloating", true));
        QWindow *floating = nullptr;
        QTRY_VERIFY((floating = floatingOf(first())));
        const QString twoWindows = dir.filePath(QStringLiteral("two.hksession"));
        QVERIFY(!saveSession(twoWindows).isEmpty());
        hideFirst();
        QVERIFY(!floating->isVisible());
        QVERIFY(!floatingOf(first()));

        loadSession(twoWindows);
        QTRY_VERIFY(first()->isVisible());
        QTRY_VERIFY(floatingOf(first()));
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
        QCOMPARE(titles(shellId(second)), (QStringList{"second.ass*"}));
    }

    void theApiActsOnTheWindowLastUsed()
    {
        auto *api = application->apiHost();
        application->activateShellWindow(0);
        QVERIFY(api->setOpen("Log", true));
        QTRY_VERIFY(dock(first(), "logDock")->property("isOpen").toBool());
        QVERIFY(api->setOpen("Log", false));
        QTRY_VERIFY(!dock(first(), "logDock")->property("isOpen").toBool());

        hideFirst();
        QVERIFY(api->setOpen("Log", true));
        QTRY_VERIFY(dock(second, "logDock")->property("isOpen").toBool());
        QVERIFY(api->focus("Log"));
        QVERIFY(api->run("GLOBAL_SEARCH"));
        QTRY_VERIFY(dock(second, "searchDock")->property("isOpen").toBool());
        const auto panels = api->panels();
        QVERIFY(std::ranges::any_of(panels, [](const api::PanelState &p) { return p.id == "Log" && p.open; }));
        for (const char *name : {"logDock", "searchDock"})
            QVERIFY2(!dock(first(), name)->property("isOpen").toBool(), name);
        QVERIFY(!first()->isVisible());
        QVERIFY(!floatingOf(first()));
    }

    void aForwardedLaunchOpensInTheWindowLastUsed()
    {
        app::ForwardedLaunches forwarded(*application);
        forwarded.setWindow(first());
        hideFirst();
        const QString third = writeSubtitles(dir.filePath(QStringLiteral("third.ass")), "third", 640);
        forwarded.take({QStringList{third}, QString()});
        QTRY_COMPARE(titles(shellId(second)), (QStringList{"second.ass", "third.ass*"}));
        QVERIFY(application->workspace().tabs(0).empty());
        QVERIFY(!first()->isVisible());
    }

    void aTabDraggedOutOfEveryWindowOpensANewWindowHoldingIt()
    {
        sideBySide();
        const auto third = openTab(0, "third");
        application->selectTab(1);
        QCOMPARE(titles(0), (QStringList{"first.ass", "third.ass*"}));
        edit(third);
        auto *session = application->files().session(third);
        application::Selection selection = session->selection();
        selection.active = session->document().lines().front()->id;
        selection.selected = {*selection.active};
        session->setSelection(selection);
        application->scrollRestored();
        application->setTargetScroll(3);
        QVERIFY(QMetaObject::invokeMethod(first(), "applyPreset", Q_ARG(QVariant, QStringLiteral("Timing"))));
        const auto *before = first()->property("workspaceLayout").value<ui::WorkspaceLayoutController *>();
        QTRY_COMPARE(before->preset(), QStringLiteral("Timing"));
        QTRY_VERIFY(!dock(first(), "videoDock")->property("isOpen").toBool());
        application->selectTab(0);
        const QString firstPreset = before->preset();
        application->selectTab(1);
        QTRY_COMPARE(before->preset(), QStringLiteral("Timing"));
        const auto cursor = session->historyCursor();
        const auto revision = session->revision();
        const int reviews = application->quitReviews();
        QSignalSpy written(application.get(), &app::Application::documentWritten);
        QSignalSpy shown(application.get(), &app::Application::tabShown);

        dragTab(first(), tabCentre(first(), 1), QPoint(1200, 120));
        QQuickWindow *opened = nullptr;
        QTRY_VERIFY((opened = newestShellWindow()) && opened != second);
        QTRY_VERIFY(opened->property("arrangementSettled").toBool());
        QCOMPARE(titles(shellId(opened)), (QStringList{"third.ass*"}));
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
        QCOMPARE(titles(1), (QStringList{"second.ass*"}));
        QCOMPARE(application->workspace().activeWindow(), shellId(opened));
        QCOMPARE(opened->width(), first()->width());
        QVERIFY(opened->height() >= first()->height());
        QCOMPARE(session->historyCursor(), cursor);
        QCOMPARE(session->revision(), revision);
        QCOMPARE(session->selection(), selection);
        QVERIFY(rows(shellId(opened)).front().toMap().value(QStringLiteral("modified")).toBool());
        QVERIFY(!shown.isEmpty());
        QCOMPARE(shown.last().first().toInt(), 3);
        QCOMPARE(editorText(opened), QStringLiteral("edited"));
        QTRY_COMPARE(opened->property("workspaceLayout").value<ui::WorkspaceLayoutController *>()->preset(),
                     QStringLiteral("Timing"));
        QTRY_COMPARE(before->preset(), firstPreset);
        QVERIFY(written.isEmpty());
        QCOMPARE(application->quitReviews(), reviews);
        QVERIFY(reviewed(first()).isEmpty());
    }

    void aTabDroppedOnAnotherWindowsBarLandsAtTheDropIndex()
    {
        sideBySide();
        const auto third = openTab(0, "third");
        openTab(1, "fourth");
        focus(0);
        dragTab(first(), tabCentre(first(), 1), tabCentre(second, 1) - QPoint(20, 0));
        QTRY_COMPARE(titles(1), (QStringList{"second.ass", "third.ass*", "fourth.ass"}));
        QCOMPARE(titles(0), (QStringList{"first.ass*"}));
        QCOMPARE(application->workspace().window(third), 1);
        QVERIFY(!newestShellWindow() || newestShellWindow() == second);
    }

    void aTabDroppedOverAWindowButOffItsBarStays()
    {
        sideBySide();
        openTab(0, "third");
        dragTab(first(), tabCentre(first(), 1), second->mapToGlobal(QPoint(150, 90)));
        QTest::qWait(300);
        QCOMPARE(titles(0), (QStringList{"first.ass", "third.ass*"}));
        QCOMPARE(titles(1), (QStringList{"second.ass*"}));
        QCOMPARE(newestShellWindow(), second.data());
    }

    void aTabDraggedOutAndBackOntoItsOwnBarReorders()
    {
        sideBySide();
        openTab(0, "third");
        const QPoint from = tabCentre(first(), 1);
        const QPoint back = tabCentre(first(), 0) - QPoint(20, 0);
        sendMouse(first(), QEvent::MouseButtonPress, from, Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
            sendMouse(first(), QEvent::MouseMove, from + QPoint(0, 10 * i), Qt::LeftButton);
        for (int i = 1; i <= 8; ++i)
            sendMouse(first(), QEvent::MouseMove, from + QPoint(0, 80) + (back - from - QPoint(0, 80)) * i / 8, Qt::LeftButton);
        sendMouse(first(), QEvent::MouseButtonRelease, back, Qt::NoButton);
        QTRY_COMPARE(titles(0), (QStringList{"third.ass*", "first.ass"}));
        QCOMPARE(newestShellWindow(), second.data());
    }

    void theMoveCommandsWorkFromTheTabMenuAndTheirHotkeys()
    {
        const auto third = openTab(0, "third");
        QObject *menu = first()->findChild<QObject *>(QStringLiteral("documentTabMenu"));
        QVERIFY(QMetaObject::invokeMethod(item(first(), "documentTabBar"), "openTabMenu", Q_ARG(QVariant, 1),
                                          Q_ARG(QVariant, QVariant::fromValue(tab(first(), 1))),
                                          Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
        QTRY_VERIFY(menu->property("visible").toBool());
        QObject *moveMenu = menu->findChild<QObject *>(QStringLiteral("moveTabToWindowMenu"));
        QVERIFY(moveMenu->property("enabled").toBool());
        QCOMPARE(menuTitles(moveMenu), (QStringList{"second.ass"}));
        QObject *toSecond = moveMenu->findChild<QObject *>(QStringLiteral("moveTabToWindow0"));
        QVERIFY(QMetaObject::invokeMethod(toSecond, "triggered"));
        QTRY_COMPARE(titles(1), (QStringList{"second.ass", "third.ass*"}));

        QVariant ran;
        focus(1);
        QVERIFY(QMetaObject::invokeMethod(second, "globalHotkey", Q_RETURN_ARG(QVariant, ran),
                                          Q_ARG(QVariant, QStringLiteral("GLOBAL_MOVE_TAB_TO_NEW_WINDOW"))));
        QVERIFY(ran.toBool());
        QQuickWindow *opened = nullptr;
        QTRY_VERIFY((opened = newestShellWindow()) && opened != second);
        QTRY_COMPARE(titles(shellId(opened)), (QStringList{"third.ass*"}));
        QCOMPARE(titles(1), (QStringList{"second.ass*"}));

        QTRY_VERIFY(opened->property("arrangementSettled").toBool());
        focus(opened);
        QVERIFY(QMetaObject::invokeMethod(opened, "globalHotkey", Q_RETURN_ARG(QVariant, ran),
                                          Q_ARG(QVariant, QStringLiteral("GLOBAL_MOVE_TAB_TO_WINDOW"))));
        QObject *popup = opened->findChild<QObject *>(QStringLiteral("moveTabToWindowPopup"));
        QTRY_VERIFY(popup->property("visible").toBool());
        QCOMPARE(menuTitles(popup), (QStringList{"first.ass", "second.ass"}));
        QVERIFY(QMetaObject::invokeMethod(popup->findChild<QObject *>(QStringLiteral("moveTabToWindow0")), "triggered"));
        QTRY_COMPARE(titles(0), (QStringList{"first.ass", "third.ass*"}));
        QTRY_VERIFY(!opened->isVisible());
        QCOMPARE(application->workspace().window(third), 0);
    }

    void theMoveCommandsAreReachableByKeyboardWithAccessibleNames()
    {
        QAccessible::setActive(true);
        const auto inactive = qScopeGuard([] { QAccessible::setActive(false); });
        openTab(0, "third");
        QQuickItem *current = tab(first(), 1);
        QVERIFY(current);
        current->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(current->hasActiveFocus());
        QTest::keyClick(first(), Qt::Key_Menu);
        QObject *menu = first()->findChild<QObject *>(QStringLiteral("documentTabMenu"));
        QTRY_VERIFY(menu->property("opened").toBool());
        const auto highlight = [&](QObject *in, const std::function<bool(QQuickItem *)> &wanted) -> QQuickItem * {
            for (int i = 0; i < 30; ++i) {
                QQuickItem *at = nullptr;
                QMetaObject::invokeMethod(in, "itemAt", Q_RETURN_ARG(QQuickItem *, at),
                                          Q_ARG(int, in->property("currentIndex").toInt()));
                if (at && wanted(at))
                    return at;
                QTest::keyClick(first(), Qt::Key_Down);
            }
            return nullptr;
        };
        const auto name = [](QQuickItem *row) { return QAccessible::queryAccessibleInterface(row)->text(QAccessible::Name); };
        QQuickItem *toNew = highlight(menu, [](QQuickItem *row) { return row->objectName() == u"tabMenuMoveToNewWindow"; });
        QVERIFY(toNew);
        QCOMPARE(name(toNew), QStringLiteral("Move tab to new window"));
        QObject *sub = menu->findChild<QObject *>(QStringLiteral("moveTabToWindowMenu"));
        QQuickItem *toWindow = highlight(menu, [&](QQuickItem *row) { return row->property("subMenu").value<QObject *>() == sub; });
        QVERIFY(toWindow);
        QCOMPARE(name(toWindow), QStringLiteral("Move tab to window"));
        QTest::keyClick(first(), Qt::Key_Right);
        QTRY_VERIFY(sub->property("opened").toBool());
        QQuickItem *row = highlight(sub, [](QQuickItem *r) { return r->objectName() == u"moveTabToWindow0"; });
        QVERIFY(row);
        QCOMPARE(name(row), QStringLiteral("second.ass"));
        QTest::keyClick(first(), Qt::Key_Return);
        QTRY_COMPARE(titles(1), (QStringList{"second.ass", "third.ass*"}));
    }

    void movingAWindowsLastTabAwayClosesIt()
    {
        QVERIFY(application->moveTab(secondTab.value, 0, 0));
        QTRY_VERIFY(!second);
        QCOMPARE(titles(0), (QStringList{"second.ass*", "first.ass"}));
        QVERIFY(first()->isVisible());

        QVERIFY(application->moveTabToNewWindow(firstTab.value, {}, false));
        QQuickWindow *opened = nullptr;
        QTRY_VERIFY((opened = newestShellWindow()));
        QVERIFY(application->moveTab(secondTab.value, shellId(opened), -1));
        QTRY_VERIFY(!first()->isVisible());
        QCOMPARE(titles(shellId(opened)), (QStringList{"first.ass", "second.ass*"}));
        QVERIFY(!application->quitApproved());
    }
};

HIKARI_QTEST_MAIN_IN_PARTS(ShellWindowsTest)
#include "shell_windows_tests.moc"
