#include "hikari/app/application.h"

#include "docking.h"
#include "workspace_layout.h"

#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>
#include <string_view>

using namespace hikari;

namespace {

const char *const kPanels[] = {"videoDock",     "audioDock",      "originalDock",  "editorDock", "gridDock",
                               "referenceDock", "timingDock",     "searchDock",    "automationDock",
                               "spellDock",     "stylesDock",     "templateDock",  "logDock"};

QString writeSubtitles(const QString &path, const char *text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    f.write(QByteArray("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,") + text + "\n");
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

private slots:
    void init()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("first.ass")), "first")));
        firstTab = *target();
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(first()));
        QTRY_VERIFY(first()->property("arrangementSettled").toBool());

        second = application->openShellWindow(*engine);
        QVERIFY(second);
        second->show();
        QVERIFY(QTest::qWaitForWindowExposed(second));
        QTRY_VERIFY(second->property("arrangementSettled").toBool());
        QCOMPARE(application->workspace().activeWindow(), 1);
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("second.ass")), "second")));
        secondTab = *target();
        QVERIFY(secondTab != firstTab);
    }
    void cleanup()
    {
        if (second)
            second->close();
        QTRY_VERIFY(!second);
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
        QCOMPARE(second->property("shell").value<QObject *>(), first()->property("shell").value<QObject *>());
        QCOMPARE(second->property("hotkeys").value<QObject *>(), first()->property("hotkeys").value<QObject *>());
        QVERIFY(second->property("workspaceLayout").value<QObject *>() != first()->property("workspaceLayout").value<QObject *>());
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
        QVERIFY(text.contains("From the second"));
        QVERIFY(!text.contains("~1"));
        QVERIFY(!text.contains("\"w1\""));
        QVERIFY(!application->workspaceLayout().savedLayoutNamed(QStringLiteral("From the second")).isEmpty());
        QVERIFY(application->workspaceLayout().applySavedLayout(QStringLiteral("From the second")));
    }
};

QTEST_MAIN(ShellWindowsTest)
#include "shell_windows_tests.moc"
