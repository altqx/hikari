// D1: the WorkspaceLayout service in the shell: a floated panel is saved in
// Hikari's envelope and restored in the next session; a corrupt, newer or
// foreign-engine layout is refused, kept for diagnosis and named in a
// notice while the Editing layout is shown; Reset layout and the backup.

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

class WorkspaceLayoutTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    QString layoutFile() const { return dir.filePath(QStringLiteral("layout.json")); }
    void start()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("hikari.ini"));
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // The saved layout is restored once the arrangement settles (its
        // first frame, D3), which on a real platform's render thread comes
        // after the window is exposed.
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

    // Closing the main window saves the arrangement it showed, and nothing
    // after that does. The docking engine closes every panel when the window
    // receives the close, before the window's closing handler ran, so that
    // handler saved the docked panels closed (the next start showed an
    // empty main window), and a save during the teardown (the periodic one,
    // on the Windows gate at 22a2a452) wrote the window hidden as well.
    void theArrangementTheWindowClosedWithIsTheLastSaved()
    {
        start();
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(dock("audioDock")->setProperty("isFloating", true));
        QTRY_VERIFY(dock("audioDock")->property("isFloating").toBool());
        window->close();
        QTRY_VERIFY(!window->isVisible());
        // What the teardown does after the close, then a save.
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
        // Hikari's envelope around the engine's layout.
        QFile f(layoutFile());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(saved.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(saved.value(QStringLiteral("engine")).toString(), QStringLiteral("KDDockWidgets 2.4.1"));
        QVERIFY(saved.value(QStringLiteral("payload")).isObject());

        start();
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        QVERIFY(!dock("audioDock")->property("isOpen").toBool());
        QVERIFY(application->workspaceLayout().notice().isEmpty());
        // Reset layout returns to the Editing arrangement; the previous file is the backup.
        QVERIFY(application->workspaceLayout().resetLayout());
        QTRY_VERIFY(!dock("editorDock")->property("isFloating").toBool());
        QVERIFY(dock("audioDock")->property("isOpen").toBool());
        QVERIFY(application->workspaceLayout().hasBackup());
        QVERIFY(application->workspaceLayout().restoreBackup());
        QTRY_VERIFY(dock("editorDock")->property("isFloating").toBool());
        stop();
    }

    // D1 native gate (persist, sway, KWin and mutter): the floating Audio
    // window came back at its size, but the panel in it 35 pixels (the
    // header's height) shorter, an empty band below it. The engine rebuilds
    // a floating window's layout at its layout area's size with the
    // window's own title bar shown, hides the title bar once the layout has
    // its lone group (whose tab bar is the header), and ignores the area's
    // resizes while a restore runs, so the layout kept the area's height
    // under the title bar. X11 (and this offscreen platform) resize the
    // restored window's contents after the restore has ended, which fits
    // the layout again; a Wayland window has its restored size at once and
    // nothing follows. So the layout has to fill its area when the restore
    // returns, before any later resize could mend it. With the drawn
    // shadow, as on Wayland.
    void aFloatingPanelComesBackFillingItsWindow()
    {
        ui::dockchrome::overrideCompositing(true);
        const auto unset = qScopeGuard([] { ui::dockchrome::overrideCompositing(std::nullopt); });
        auto audio = [this] {
            return engine->rootObjects().first()->findChild<QQuickItem *>(QStringLiteral("audioPanel"));
        };
        // The panel's bottom edge against its window's: the frame's inset.
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

        // The same restore within the session (Reset layout, then Restore
        // the previous layout), checked as it returns.
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

    // D1 native gate (monitor removal): a layout saved with a floating panel
    // on a screen that is gone comes back with that panel's title bar on an
    // available screen.
    void aFloatingPanelSavedOffScreenComesBackOnScreen()
    {
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            QSKIP("On Wayland the compositor places windows (docs/qt/docking.md)");
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
        // Mostly where a second monitor right of the first used to be: a
        // sliver of the window stays on this screen, which the engine's own
        // restore accepts, but not enough of its title bar to take hold of.
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
        // Reset layout returns to the chosen preset, not to Editing.
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

    // Native gate (X11, D3): the saved layout is restored at the first
    // frame, so a save before then (the window closed at once) must not
    // replace it with the default arrangement.
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
        options.settingsFile = dir.filePath(QStringLiteral("hikari.ini"));
        application = std::make_unique<app::Application>(options);
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
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
        // Valid for Hikari, but in a serialization format the engine refuses.
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
        // The Editing layout is shown; the file is untouched and its copy kept.
        for (const char *name : {"videoDock", "audioDock", "editorDock", "gridDock"})
            QVERIFY2(dock(name)->property("isOpen").toBool(), name);
        QVERIFY(!dock("editorDock")->property("isFloating").toBool());
        QFile kept(layoutFile() + QStringLiteral(".unrestored"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), file);
        // Saving resumes once the layout changes and replaces the file with a
        // valid layout; the copy stays. The change is the user's (closing a
        // panel): the notice's own re-layout of the window may not have
        // happened yet when the session ends (it had not on Windows).
        QVERIFY(QMetaObject::invokeMethod(dock("audioDock"), "close"));
        QTRY_VERIFY(!dock("audioDock")->property("isOpen").toBool());
        stop();
        QFile again(layoutFile());
        QVERIFY(again.open(QIODevice::ReadOnly));
        QVERIFY(ui::WorkspaceLayoutController::payloadOf(again.readAll(), nullptr));
        QVERIFY(QFile::exists(layoutFile() + QStringLiteral(".unrestored")));
    }
};

QTEST_MAIN(WorkspaceLayoutTest)
#include "workspace_layout_tests.moc"
