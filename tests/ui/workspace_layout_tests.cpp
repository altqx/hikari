#include "hikari/app/application.h"

#include "docking.h"
#include "workspace_layout.h"

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/View.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}

class WorkspaceLayoutTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString layoutFile() const { return dir.filePath(QStringLiteral("layout.json")); }
    void start()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "buildMenus"));
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // On a real platform's render thread the first frame comes after the window is exposed.
        QTRY_VERIFY(window->property("arrangementSettled").toBool());
    }
    void stop()
    {
        application->workspaceLayout().save();
        engine.reset();
        application.reset();
    }
    QObject *dock(const char *name) const
    {
        return engine->rootObjects().first()->findChild<QObject *>(QLatin1String(name));
    }
    void writeLayout(const QByteArray &bytes)
    {
        QFile f(layoutFile());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        QFile::remove(layoutFile());
        QFile::remove(layoutFile() + QStringLiteral(".bak"));
        QFile::remove(layoutFile() + QStringLiteral(".unrestored"));
    }

    void envelopeValidation()
    {
        const QByteArray payload = R"({"serializationVersion":3,"mainWindows":[{"uniqueName":"Classic"}],"allDockWidgets":[{"uniqueName":"Grid"}]})";
        const QByteArray good = ui::WorkspaceLayoutController::envelope(payload);
        QString problem;
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(good, &problem));
        auto edited = [&](const char *key, const QJsonValue &value) {
            QJsonObject o = QJsonDocument::fromJson(good).object();
            o.insert(QLatin1String(key), value);
            return QJsonDocument(o).toJson();
        };
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf("{not json", &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(edited("schema", 2), &problem));
        QVERIFY(problem.contains(QStringLiteral("newer")));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(edited("engine", QStringLiteral("KDDockWidgets 9.0")), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(edited("engine", QStringLiteral("KDDockWidgets 2.4.0")), &problem));
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(edited("engine", QStringLiteral("KDDockWidgets 2.4.1")), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(edited("panels", QJsonArray{QStringLiteral("Mystery")}), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(edited("payload", QJsonObject{}), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(
            edited("payload", QJsonObject{{QStringLiteral("mainWindows"), QJsonArray{}}}), &problem));
        QVERIFY(problem.contains(QStringLiteral("Classic")));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(
            edited("payload", QJsonDocument::fromJson(R"({"mainWindows":[{"uniqueName":"Classic"}],"allDockWidgets":[{"uniqueName":"Spy"}]})").object()),
            &problem));
        QVERIFY(problem.contains(QStringLiteral("Spy")));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(QByteArray(ui::WorkspaceLayoutController::kMaximumBytes + 1, ' '),
                                                          &problem));
    }

    void pluginPanelsPassValidationAndKeepTheirTitles()
    {
        using Layout = ui::WorkspaceLayoutController;
        const QString board = Layout::pluginPanelId(QStringLiteral("com.example.line-tools"), QStringLiteral("timing-check"));
        QCOMPARE(board, QStringLiteral("plugin:com.example.line-tools/timing-check"));
        QVERIFY(Layout::isPluginPanel(board));
        for (const char *name : {"Grid", "plugin:", "plugin:com.example", "plugin:/board", "plugin:com.example/",
                                 "plugin:Com.Example/board", "plugin:com.example/a.b", "plugin:com.example/a/b"})
            QVERIFY2(!Layout::isPluginPanel(QLatin1String(name)), name);

        Layout::rememberPluginPanel(board, QStringLiteral("Timing check"), QStringLiteral("Line tools"));
        const QByteArray payload = QStringLiteral(R"({"serializationVersion":3,"mainWindows":[{"uniqueName":"Classic"}],
            "allDockWidgets":[{"uniqueName":"Grid"},{"uniqueName":"%1"}],"closedDockWidgets":["%1"]})").arg(board).toUtf8();
        const QJsonObject written = QJsonDocument::fromJson(Layout::envelope(payload)).object();
        QCOMPARE(written.value(QStringLiteral("pluginPanels")).toObject().value(board).toObject().value(QStringLiteral("title")),
                 QJsonValue(QStringLiteral("Timing check")));
        QString problem;
        QVERIFY2(Layout::payloadOf(Layout::envelope(payload), &problem), qPrintable(problem));

        const QString gone = Layout::pluginPanelId(QStringLiteral("com.example.gone"), QStringLiteral("notes"));
        QJsonObject read = QJsonDocument::fromJson(Layout::envelope(R"({"mainWindows":[{"uniqueName":"Classic"}]})")).object();
        read.insert(QStringLiteral("payload"), QJsonDocument::fromJson(QStringLiteral(
            R"({"mainWindows":[{"uniqueName":"Classic"}],"allDockWidgets":[{"uniqueName":"%1"}]})").arg(gone).toUtf8()).object());
        read.insert(QStringLiteral("pluginPanels"),
                    QJsonObject{{gone, QJsonObject{{QStringLiteral("title"), QStringLiteral("Waveform notes")},
                                                   {QStringLiteral("pluginName"), QStringLiteral("Karaoke helper")}}}});
        QVERIFY2(Layout::payloadOf(QJsonDocument(read).toJson(), &problem), qPrintable(problem));
        QCOMPARE(Layout::pluginPanelInfo(gone).value(QStringLiteral("pluginName")).toString(), QStringLiteral("Karaoke helper"));

        QJsonArray many;
        for (int i = 0; i <= Layout::kMaxPluginPanels; ++i)
            many.append(QJsonObject{{QStringLiteral("uniqueName"), Layout::pluginPanelId(QStringLiteral("com.example.many"),
                                                                                          QStringLiteral("p%1").arg(i))}});
        read.insert(QStringLiteral("payload"), QJsonObject{{QStringLiteral("mainWindows"), QJsonArray{QJsonObject{
                                                               {QStringLiteral("uniqueName"), QStringLiteral("Classic")}}}},
                                                           {QStringLiteral("allDockWidgets"), many}});
        QVERIFY(!Layout::payloadOf(QJsonDocument(read).toJson(), &problem));
        QVERIFY2(problem.contains(QStringLiteral("more windows or panels")), qPrintable(problem));

        read.insert(QStringLiteral("payload"), QJsonDocument::fromJson(
            R"({"mainWindows":[{"uniqueName":"Classic"}],"allDockWidgets":[{"uniqueName":"plugin:Bad/x"}]})").object());
        QVERIFY(!Layout::payloadOf(QJsonDocument(read).toJson(), &problem));
        QVERIFY2(problem.contains(QStringLiteral("plugin:Bad/x")), qPrintable(problem));
    }

    void anArrangementHasOneMainWindowInAnyShellWindow()
    {
        const auto payloadWith = [](const char *mainWindows) {
            return ui::WorkspaceLayoutController::envelope(
                QByteArray(R"({"serializationVersion":3,"mainWindows":)") + mainWindows +
                R"(,"allDockWidgets":[{"uniqueName":"Grid"}]})");
        };
        QString problem;
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(payloadWith(R"([{"uniqueName":"Classic"}])"), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(
            payloadWith(R"([{"uniqueName":"Classic"},{"uniqueName":"Classic~1"}])"), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(
            payloadWith(R"([{"uniqueName":"Classic"},{"uniqueName":"Classic"}])"), &problem));
        QVERIFY(!ui::WorkspaceLayoutController::payloadOf(payloadWith(R"([{"uniqueName":"Classic~1"}])"), &problem));

        const ui::ShellWindowNames second{1};
        const QByteArray stored = R"({"mainWindows":[{"uniqueName":"Classic","affinities":[]}],)"
                                  R"("allDockWidgets":[{"uniqueName":"Grid"}],"closedDockWidgets":["Grid"]})";
        const QJsonObject engine = QJsonDocument::fromJson(second.toEngine(stored)).object();
        const QJsonObject main = engine.value(QStringLiteral("mainWindows")).toArray().first().toObject();
        QCOMPARE(main.value(QStringLiteral("uniqueName")).toString(), QStringLiteral("Classic~1"));
        QCOMPARE(main.value(QStringLiteral("affinities")).toArray(), QJsonArray{QStringLiteral("w1")});
        QCOMPARE(engine.value(QStringLiteral("closedDockWidgets")).toArray(), QJsonArray{QStringLiteral("Grid~1")});
        QCOMPARE(QJsonDocument::fromJson(second.toStored(second.toEngine(stored))).object(),
                 QJsonDocument::fromJson(stored).object());
    }

    void theArrangementTheWindowClosedWithIsTheLastSaved()
    {
        start();
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(dock("audioDock")->setProperty("isFloating", true));
        QTRY_VERIFY(dock("audioDock")->property("isFloating").toBool());
        window->close();
        QTRY_VERIFY(!window->isVisible());
        // KDDockWidgets closes every panel when the window receives the close.
        for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY(QMetaObject::invokeMethod(dock(name), "close"));
        QTRY_VERIFY(!dock("gridDock")->property("isOpen").toBool());
        QVERIFY(!application->workspaceLayout().save());
        engine.reset();
        application.reset();
        QFile f(layoutFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject payload = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("payload")).toObject();
        QVERIFY(payload.value(QStringLiteral("mainWindows")).toArray().first().toObject().value(QStringLiteral("isVisible")).toBool());
        start();
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window->isVisible());
        QVERIFY(dock("audioDock")->property("isFloating").toBool());
        for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(dock(name)->property("isOpen").toBool(), name);
    }

    void aFloatedPanelComesBackNextSession()
    {
        start();
        QVERIFY(dock("editorDock")->setProperty("isFloating", true));
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        QVERIFY(dock("audioDock")->property("isOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(dock("audioDock"), "close"));
        stop();
        QFile f(layoutFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(saved.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(saved.value(QStringLiteral("engine")).toString(), QStringLiteral("KDDockWidgets 2.5.1"));
        QVERIFY(saved.value(QStringLiteral("payload")).isObject());

        start();
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        QVERIFY(!dock("audioDock")->property("isOpen").toBool());
        QVERIFY(application->workspaceLayout().notice().isEmpty());
        QVERIFY(application->workspaceLayout().resetLayout());
        QTRY_VERIFY(!dock("editorDock")->property("isFloating").toBool());
        QVERIFY(dock("audioDock")->property("isOpen").toBool());
        QVERIFY(application->workspaceLayout().hasBackup());
        QVERIFY(application->workspaceLayout().restoreBackup());
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        stop();
    }

    // KDDockWidgets ignores its area's resizes while a restore runs; X11 resizes afterwards, Wayland does not.
    void aFloatingPanelComesBackFillingItsWindow()
    {
        ui::dockchrome::overrideCompositing(true);
        const auto unset = qScopeGuard([] { ui::dockchrome::overrideCompositing(std::nullopt); });
        auto audio = [this] {
            return engine->rootObjects().first()->findChild<QQuickItem *>(QStringLiteral("audioPanel"));
        };
        auto bottomGap = [](QQuickItem *panel) {
            return panel->window()->height() - qRound(panel->mapToScene(QPointF(0, panel->height())).y());
        };
        auto layoutsFillTheirAreas = [] {
            QStringList short_;
            for (auto *floating : KDDockWidgets::DockRegistry::self()->floatingWindows()) {
                const QSize area = floating->dropArea()->view()->size();
                const QSize layout = floating->dropArea()->layoutSize();
                if (layout != area)
                    short_ << QStringLiteral("layout %1x%2 in an area of %3x%4")
                                  .arg(layout.width()).arg(layout.height()).arg(area.width()).arg(area.height());
            }
            return short_;
        };
        start();
        QVERIFY(dock("audioDock")->setProperty("isFloating", true));
        QTRY_VERIFY(dock("audioDock")->property("isFloating").toBool());
        QTRY_VERIFY(audio() && audio()->window() != engine->rootObjects().first() && audio()->height() > 0);
        QQuickWindow *floating = audio()->window();
        floating->resize(640, 260);
        QTRY_COMPARE(floating->size(), QSize(640, 260));
        QTRY_COMPARE(bottomGap(audio()), ui::dockchrome::floatingShadow() + 1);
        const QSizeF panelBefore = audio()->size();
        const QSize windowBefore = floating->size();
        stop();

        start();
        QTRY_VERIFY(dock("audioDock")->property("isFloating").toBool());
        QTRY_VERIFY(audio() && audio()->window() != engine->rootObjects().first());
        QTRY_COMPARE(audio()->window()->size(), windowBefore);
        QTRY_COMPARE(audio()->size(), panelBefore);
        QCOMPARE(bottomGap(audio()), ui::dockchrome::floatingShadow() + 1);

        QVERIFY(application->workspaceLayout().resetLayout());
        QTRY_VERIFY(!dock("audioDock")->property("isFloating").toBool());
        QVERIFY(application->workspaceLayout().restoreBackup());
        QVERIFY(dock("audioDock")->property("isFloating").toBool());
        const QStringList short_ = layoutsFillTheirAreas();
        QVERIFY2(short_.isEmpty(), qPrintable(short_.join(QStringLiteral("; "))));
        QTRY_COMPARE(audio()->size(), panelBefore);
        QCOMPARE(bottomGap(audio()), ui::dockchrome::floatingShadow() + 1);
        stop();
    }

    void aFloatingPanelSavedOffScreenComesBackOnScreen()
    {
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            QSKIP("On Wayland the compositor places windows (docs/docking.md)");
        start();
        QVERIFY(dock("editorDock")->setProperty("isFloating", true));
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        stop();
        QFile f(layoutFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonObject payload = saved.value(QStringLiteral("payload")).toObject();
        QJsonArray floating = payload.value(QStringLiteral("floatingWindows")).toArray();
        QCOMPARE(floating.size(), 1);
        // KDDockWidgets' restore accepts a window with only a sliver left on screen.
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        QJsonObject window = floating.at(0).toObject();
        for (const char *key : {"geometry", "normalGeometry"}) {
            QJsonObject g = window.value(QLatin1String(key)).toObject();
            QVERIFY2(!g.isEmpty() || qstrcmp(key, "normalGeometry") == 0, key);
            if (g.isEmpty())
                continue;
            g.insert(QStringLiteral("x"), screen.right() - 20);
            g.insert(QStringLiteral("y"), screen.top() + 100);
            window.insert(QLatin1String(key), g);
        }
        floating.replace(0, window);
        payload.insert(QStringLiteral("floatingWindows"), floating);
        saved.insert(QStringLiteral("payload"), payload);
        writeLayout(QJsonDocument(saved).toJson());

        start();
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        QQuickItem *panel = engine->rootObjects().first()->findChild<QQuickItem *>(QStringLiteral("editorPanel"));
        QVERIFY(panel);
        QWindow *w = panel->window();
        QVERIFY(w && w != engine->rootObjects().first());
        const QRect frame = w->frameGeometry();
        const QRect strip(frame.topLeft(), QSize(frame.width(), 30));
        bool reachable = false;
        for (QScreen *screen : QGuiApplication::screens())
            reachable = reachable || screen->availableGeometry().intersected(strip).width() >= 80;
        QVERIFY2(reachable, qPrintable(QStringLiteral("floating window at %1,%2").arg(frame.x()).arg(frame.y())));
        stop();
    }

    void presetsAreStartingArrangementsAndPersist()
    {
        start();
        auto *root = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root, "applyPreset", Q_ARG(QVariant, QStringLiteral("Timing"))));
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Timing"));
        QTRY_VERIFY(!dock("videoDock")->property("isOpen").toBool());
        for (const char *name : {"audioDock", "editorDock", "gridDock"})
            QVERIFY2(dock(name)->property("isOpen").toBool(), name);
        QVERIFY(QMetaObject::invokeMethod(dock("videoDock"), "open"));
        QVERIFY(QMetaObject::invokeMethod(root, "applyPreset", Q_ARG(QVariant, application->workspaceLayout().preset())));
        QTRY_VERIFY(!dock("videoDock")->property("isOpen").toBool());
        stop();
        start();
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Timing"));
        QVERIFY(!dock("videoDock")->property("isOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(root = engine->rootObjects().first(), "applyPreset", Q_ARG(QVariant, QStringLiteral("Translation"))));
        QTRY_VERIFY(!dock("audioDock")->property("isOpen").toBool());
        QVERIFY(dock("videoDock")->property("isOpen").toBool());
        stop();
    }

    void aSaveBeforeTheSavedLayoutIsRestoredKeepsIt()
    {
        start();
        auto *root = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root, "applyPreset", Q_ARG(QVariant, QStringLiteral("Timing"))));
        stop();
        QFile f(layoutFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray saved = f.readAll();
        f.close();

        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "buildMenus"));
        QVERIFY(!engine->rootObjects().first()->property("arrangementSettled").toBool());
        QVERIFY(!application->workspaceLayout().save());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), saved);
        f.close();
        QTRY_VERIFY(engine->rootObjects().first()->property("arrangementSettled").toBool());
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Timing"));
        QVERIFY(!dock("videoDock")->property("isOpen").toBool());
        stop();
    }

    void anUnusableLayoutIsKeptAndNamed_data()
    {
        QTest::addColumn<QByteArray>("file");
        QTest::addColumn<QString>("reason");
        QTest::newRow("corrupt") << QByteArray("{\"schema\": 1, \"payl") << QStringLiteral("not a layout file");
        const QByteArray good = ui::WorkspaceLayoutController::envelope(
            R"({"serializationVersion":1,"mainWindows":[{"uniqueName":"Classic"}],"allDockWidgets":[]})");
        QJsonObject newer = QJsonDocument::fromJson(good).object();
        newer.insert(QStringLiteral("schema"), 7);
        QTest::newRow("newer") << QJsonDocument(newer).toJson() << QStringLiteral("newer version");
        QJsonObject foreign = QJsonDocument::fromJson(good).object();
        foreign.insert(QStringLiteral("engine"), QStringLiteral("KDDockWidgets 3.0"));
        QTest::newRow("other engine") << QJsonDocument(foreign).toJson() << QStringLiteral("KDDockWidgets 3.0");
        QTest::newRow("engine refuses") << good << QStringLiteral("could not restore");
    }
    void anUnusableLayoutIsKeptAndNamed()
    {
        QFETCH(QByteArray, file);
        QFETCH(QString, reason);
        writeLayout(file);
        start();
        const QString notice = application->workspaceLayout().notice();
        QVERIFY2(notice.contains(reason), qPrintable(notice));
        QVERIFY(notice.contains(QStringLiteral("layout.json.unrestored")));
        for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(dock(name)->property("isOpen").toBool(), name);
        QVERIFY(!dock("editorDock")->property("isFloating").toBool());
        QFile kept(layoutFile() + QStringLiteral(".unrestored"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), file);
        // On Windows the notice's own re-layout may not have happened when the session ends.
        QVERIFY(QMetaObject::invokeMethod(dock("audioDock"), "close"));
        QTRY_VERIFY(!dock("audioDock")->property("isOpen").toBool());
        stop();
        QFile again(layoutFile());
        QVERIFY(again.open(QIODevice::ReadOnly));
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(again.readAll(), nullptr));
        QVERIFY(QFile::exists(layoutFile() + QStringLiteral(".unrestored")));
    }

    void aLockedLayoutComesBackLockedNextSession()
    {
        start();
        auto &layouts = application->workspaceLayout();
        QVERIFY(layouts.setLocked(true));
        QVERIFY(layouts.locked());
        QVERIFY(ui::layoutLocked());
        stop();
        QVERIFY(QJsonDocument::fromJson(readAll(layoutFile())).object().value(QStringLiteral("locked")).toBool());

        start();
        QVERIFY(application->workspaceLayout().locked());
        QVERIFY(ui::layoutLocked());
        QVERIFY(application->workspaceLayout().setLocked(false));
        stop();
        QVERIFY(!QJsonDocument::fromJson(readAll(layoutFile())).object().contains(QStringLiteral("locked")));
    }

    void theLockTravelsWithTheArrangement()
    {
        const ui::WorkspaceArrangement locked{QStringLiteral("Timing"), {}, QStringLiteral("Mine"), true};
        QCOMPARE(ui::WorkspaceArrangement::parse(locked.serialise()), locked);
        const ui::WorkspaceArrangement open{QStringLiteral("Timing"), {}, QStringLiteral("Mine"), false};
        QCOMPARE(ui::WorkspaceArrangement::parse(open.serialise()), open);
        QVERIFY(!open.serialise().contains("locked"));

        start();
        auto &layouts = application->workspaceLayout();
        QVERIFY(layouts.setLocked(true));
        QVERIFY(layouts.saveLayoutAs(QStringLiteral("Locked one")));
        QVERIFY(layouts.setLocked(false));
        QVERIFY(layouts.applyPreset(QStringLiteral("Timing")));
        QVERIFY(!layouts.locked());
        QVERIFY(layouts.applySavedLayout(QStringLiteral("Locked one")));
        QVERIFY(layouts.locked());
        QVERIFY(ui::layoutLocked());
        QVERIFY(layouts.setLocked(false));
        stop();
    }

    void aFloatingPanelBlocksTheLock()
    {
        start();
        auto &layouts = application->workspaceLayout();
        QObject *window = engine->rootObjects().first();
        QVERIFY(dock("editorDock")->setProperty("isFloating", true));
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        QTRY_COMPARE(window->property("lockReason").toString(), QStringLiteral("Dock floating panels to lock the layout"));
        QVERIFY(!layouts.setLocked(true));
        QVERIFY(!layouts.locked());
        auto *lockRow = window->findChild<QObject *>(QStringLiteral("viewLayoutLock"));
        QVERIFY(lockRow);
        QVERIFY(!lockRow->property("enabled").toBool());
        QCOMPARE(lockRow->property("reason").toString(), QStringLiteral("Dock floating panels to lock the layout"));

        QVERIFY(dock("editorDock")->setProperty("isFloating", false));
        QTRY_VERIFY(lockRow->property("enabled").toBool());
        QVERIFY(window->property("lockReason").toString().isEmpty());
        QVERIFY(!dock("editorDock")->property("isFloating").toBool());
        QVERIFY(layouts.setLocked(true));
        QVERIFY(lockRow->property("checked").toBool());
        QVERIFY(layouts.setLocked(false));
        stop();
    }

    void whileLockedTheRowsThatMovePanelsAreGreyed()
    {
        start();
        auto &layouts = application->workspaceLayout();
        QObject *window = engine->rootObjects().first();
        auto row = [&](const char *name) {
            auto *item = window->findChild<QObject *>(QLatin1String(name));
            return item && item->property("enabled").toBool();
        };
        const char *held[] = {"viewLayoutPresetTiming", "viewLayoutReset", "panelShowGrid", "panelCloseGrid",
                              "panelFloatGrid", "movePanel", "switcherPresetTiming", "switcherReset"};
        for (const char *name : held)
            QVERIFY2(row(name), name);
        QVERIFY(layouts.setLocked(true));
        for (const char *name : held)
            QVERIFY2(!row(name), name);
        QVERIFY(row("viewLayoutSaveAs"));
        QVERIFY(row("viewLayoutLock"));
        QVERIFY(row("hideTagsMenuItem"));
        QVERIFY(QMetaObject::invokeMethod(window, "applyPreset", Q_ARG(QVariant, QStringLiteral("Timing"))));
        QCOMPARE(layouts.preset(), QStringLiteral("Editing"));
        QVERIFY(layouts.setLocked(false));
        stop();
    }

    void aCommandStillShowsItsPanelWhileLocked()
    {
        start();
        auto &layouts = application->workspaceLayout();
        QObject *window = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(dock("searchDock"), "close"));
        QTRY_VERIFY(!dock("searchDock")->property("isOpen").toBool());
        QVERIFY(layouts.setLocked(true));
        QVERIFY(QMetaObject::invokeMethod(window, "openFind", Q_ARG(QVariant, false)));
        QTRY_VERIFY(dock("searchDock")->property("isOpen").toBool());
        QVERIFY(!dock("searchDock")->property("isFloating").toBool());
        QVERIFY(layouts.locked());
        QVERIFY(layouts.setLocked(false));
        stop();
    }
};

QTEST_MAIN(WorkspaceLayoutTest)
#include "workspace_layout_tests.moc"
