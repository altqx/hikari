#include "hikari/app/application.h"
#include "hikari/application/side_bars.h"
#include "docking.h"
#include "line_grid.h"
#include "settings_store.h"
#include "shown_objects.h"
#include "side_bar_model.h"
#include "theme.h"

#include <QDir>
#include <QJSValue>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QScreen>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

class SideBarsShellTests : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    void start(const QString &sideBars = {}, int width = 0, const QString &beforeTheFirstFrame = {})
    {
        app::Application::Options o;
        o.settingsFile = home->filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        o.legacyAutosaveDir = home->filePath(QStringLiteral("Subs"));
        application = new app::Application(o);
        if (!sideBars.isEmpty())
            application->settingsStore()->setValue(QStringLiteral("workspace.sideBars"), sideBars);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        QVariantMap properties = application->qmlProperties();
        if (width > 0)
            properties.insert(QStringLiteral("width"), width);
        engine->setInitialProperties(properties);
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        if (!beforeTheFirstFrame.isEmpty()) {
            QVERIFY(!root()->property("sideBarsLoaded").toBool());
            shell(beforeTheFirstFrame);
        }
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(root()->property("sideBarsLoaded").toBool());
    }
    QObject *root() const { return engine->rootObjects().first(); }
    template <typename T = QQuickItem> T *item(const char *name) const
    {
        if (T *found = root()->findChild<T *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *quick = qobject_cast<QQuickWindow *>(w))
                if (auto *found = qobject_cast<T *>(findItem(quick->contentItem(), QLatin1String(name))))
                    return found;
        return nullptr;
    }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (!from)
            return nullptr;
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *panel(const char *name) const
    {
        QQuickItem *found = item(name);
        if (!found)
            qWarning("no item %s", name);
        return found;
    }
    QQuickItem *parentOf(const char *name) const
    {
        QQuickItem *found = panel(name);
        return found ? found->parentItem() : nullptr;
    }
    bool shown(const char *name) const
    {
        QQuickItem *found = panel(name);
        return found && found->isVisible();
    }
    QQuickItem *in(const char *bar, const QString &name) const { return findItem(item(bar), name); }
    QQuickItem *slotOf(const char *bar) const { return in(bar, QStringLiteral("sideBarSlot")); }
    QJsonObject saved() const
    {
        return QJsonDocument::fromJson(application->sideBars(shellWindow()).toUtf8()).object();
    }
    int shellWindow() const { return root()->property("shellWindow").toInt(); }
    QStringList panelsOf(const QJsonObject &bar) const
    {
        QStringList names;
        for (const QJsonValue &v : bar.value(QStringLiteral("panels")).toArray())
            names << v.toString();
        return names;
    }
    template <typename... Args> void call(const char *function, const Args &...args)
    {
        QVERIFY2(QMetaObject::invokeMethod(root(), function, Q_ARG(QVariant, QVariant::fromValue(args))...), function);
    }
    QVariant dock(const char *name) const { return QVariant::fromValue(item<QObject>(name)); }
    int middleLeast() const { return shell(QStringLiteral("Docking.layoutMinimumSize(dockingArea.uniqueName).width")).toInt(); }
    bool screenFitsBothSideBars() const
    {
        return QTest::qWaitFor([&] { return middleLeast() > 0; }) && shell(QStringLiteral("bothSideBarsFit()")).toBool();
    }
    QVariant shell(const QString &code) const
    {
        QQmlExpression expression(qmlContext(root()), root(), code);
        const QVariant out = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error();
        return out;
    }
    QString writeSubtitles() const
    {
        const QString path = home->filePath(QStringLiteral("episode.ass"));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                       "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                       "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                       "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                       "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
                       "[Events]\n"
                       "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A line with {\\b1}tags{\\b0}\n");
        return path;
    }

private slots:
    void initTestCase() { hikari::ui::SideBarModel::setFirstStartEmpty(false); }
    void init()
    {
        home = std::make_unique<QTemporaryDir>();
        QVERIFY(home->isValid());
    }
    void cleanup()
    {
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
        home.reset();
    }

    void theToolsSideBarStartsOnTheRightWithItsPanels()
    {
        start();
        auto *right = item("rightSideBar");
        QVERIFY(right);
        QVERIFY(right->isVisible());
        for (const char *panel : {"sideBarIcon_Search", "sideBarIcon_Timing", "sideBarIcon_Postprocessor",
                                  "sideBarIcon_Styles", "sideBarIcon_QcNotes"})
            QVERIFY2(in("rightSideBar", QLatin1String(panel)), panel);
        QCOMPARE(parentOf("searchPanel"), slotOf("rightSideBar"));
        QVERIFY(!item<QObject>("searchDock")->property("isOpen").toBool());
        QVERIFY(!shown("timingPanel"));
        QCOMPARE(item("dockingArea")->x() + item("dockingArea")->width(), right->x());
        const QJsonObject bars = saved();
        QCOMPARE(panelsOf(bars.value(QStringLiteral("second")).toObject()).first(), QStringLiteral("Explorer"));
        QCOMPARE(application->settingsStore()->text("workspace.sideBars").isEmpty(), false);
    }

    void anIconShowsItsPanelAndTheShownOneHidesTheSideBar()
    {
        start();
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_COMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        QVERIFY(shown("timingPanel"));
        QVERIFY(!shown("searchPanel"));
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("shown")).toString(),
                 QStringLiteral("Timing"));
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_VERIFY(!shown("rightSideBar"));
        QVERIFY(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("hidden")).toBool());
        QVERIFY(!shown("timingPanel"));
    }

    void theToggleCommandsHideAndShowEachSideBar()
    {
        start();
        auto *toggle = item<QObject>("rightSideBarToggle");
        QVERIFY(toggle);
        QVERIFY(toggle->property("checked").toBool());
        call("toggleSideBar", false);
        QTRY_VERIFY(!shown("rightSideBar"));
        QVERIFY(!toggle->property("checked").toBool());
        QCOMPARE(item("dockingArea")->x() + item("dockingArea")->width(), window->contentItem()->width());
        call("toggleSideBar", false);
        QTRY_VERIFY(shown("rightSideBar"));
        QVERIFY(toggle->property("checked").toBool());
    }

    void showingASideBarPanelShowsItInItsSideBar()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"],"shown":"Search","hidden":true}})"));
        QVERIFY(!shown("rightSideBar"));
        call("showPanel", dock("timingDock"));
        QTRY_VERIFY(shown("rightSideBar"));
        QCOMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        QTRY_VERIFY(panel("timingPanel") && panel("timingPanel")->hasActiveFocus());
        QVERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
    }

    void aPanelMovesOutOfTheSideBarAndBack()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"],"shown":"Timing"}})"));
        QCOMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        call("moveOutOfSideBar", dock("timingDock"));
        QTRY_VERIFY(item<QObject>("timingDock")->property("isOpen").toBool());
        QVERIFY(parentOf("timingPanel") != slotOf("rightSideBar"));
        QVERIFY(shown("timingPanel"));
        QCOMPARE(panelsOf(saved().value(QStringLiteral("second")).toObject()), QStringList{QStringLiteral("Search")});
        QCOMPARE(parentOf("searchPanel"), slotOf("rightSideBar"));
        call("moveToSideBar", dock("timingDock"), true, QVariant());
        QTRY_VERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
        QCOMPARE(parentOf("timingPanel"), slotOf("leftSideBar"));
        QVERIFY(shown("leftSideBar"));
        QCOMPARE(panelsOf(saved().value(QStringLiteral("first")).toObject()), QStringList{QStringLiteral("Timing")});
    }

    void theMiddlePanelsNeverGoInASideBar()
    {
        start();
        const QJsonObject before = saved();
        call("moveToSideBar", dock("gridDock"), true, QVariant());
        QCOMPARE(saved(), before);
        QVERIFY(item<QObject>("gridDock")->property("isOpen").toBool());
    }

    void closeTakesTheIconOffTheStrip()
    {
        start();
        call("closePanel", dock("qcNotesDock"));
        QTRY_VERIFY(!in("rightSideBar", QStringLiteral("sideBarIcon_QcNotes")));
        QVERIFY(!panelsOf(saved().value(QStringLiteral("second")).toObject()).contains(QStringLiteral("QcNotes")));
        QVERIFY(!item<QObject>("qcNotesDock")->property("isOpen").toBool());
    }

    void swapPutsTheAiSideBarOnTheOtherEdge()
    {
        start(QStringLiteral(R"({"first":{"panels":["Log"]},"second":{"panels":["Search"]}})"));
        if (!screenFitsBothSideBars())
            QSKIP("needs a screen wide enough for both side bars, like screen-2560x1440.json");
        QCOMPARE(parentOf("logPanel"), slotOf("leftSideBar"));
        call("swapSideBars");
        QCOMPARE(application->settingsStore()->text("appearance.aiSideBar"), QStringLiteral("end"));
        QTRY_COMPARE(parentOf("logPanel"), slotOf("rightSideBar"));
        QCOMPARE(parentOf("searchPanel"), slotOf("leftSideBar"));
    }

    void lockLayoutKeepsPanelsInTheirSideBar()
    {
        start(QStringLiteral(R"({"first":{"panels":[]},"second":{"panels":["Search","Timing"]}})"));
        QVERIFY(QMetaObject::invokeMethod(root()->property("workspaceLayout").value<QObject *>(), "setLocked", Q_ARG(bool, true)));
        const QJsonObject before = saved();
        call("moveOutOfSideBar", dock("timingDock"));
        call("moveToSideBar", dock("searchDock"), true, QVariant());
        QCOMPARE(saved(), before);
        call("sideBarIconClicked", QStringLiteral("Timing"));
        QTRY_COMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
    }

    void f6GoesFromTheLeftSideBarThroughTheMiddleToTheRight()
    {
        start(QStringLiteral(R"({"first":{"panels":["Log"]},"second":{"panels":["Search"]}})"));
        if (!screenFitsBothSideBars())
            QSKIP("needs a screen wide enough for both side bars, like screen-2560x1440.json");
        QVariant order;
        QVERIFY(QMetaObject::invokeMethod(root(), "sideBarPanelCycle", Q_RETURN_ARG(QVariant, order)));
        const QVariantList panels = order.metaType() == QMetaType::fromType<QJSValue>() ? order.value<QJSValue>().toVariant().toList()
                                                                                         : order.toList();
        QVERIFY(panels.size() > 2);
        QCOMPARE(panels.first().value<QQuickItem *>(), panel("logPanel"));
        QCOMPARE(panels.last().value<QQuickItem *>(), panel("searchPanel"));
    }

    void aDraggedEdgeBelowTheMinimumHidesTheSideBar()
    {
        start();
        call("resizeSideBar", false, 400, true);
        QTRY_COMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("width")).toInt(), 400);
        call("resizeSideBar", false, 100, true);
        QTRY_VERIFY(!shown("rightSideBar"));
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("width")).toInt(), 400);
    }

    void eachCommandOpensItsPanelInTheToolsSideBar_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("panelName");
        QTest::addColumn<QString>("dockName");
        QTest::addColumn<QString>("control");
        QTest::newRow("Search") << "root.openFind(false)" << "searchPanel" << "searchDock" << "findText";
        QTest::newRow("Shift times") << "showShiftTimesAction.trigger()" << "timingPanel" << "timingDock" << "";
        QTest::newRow("Postprocessor") << "showPostprocessorAction.trigger()" << "postprocessorPanel" << "postprocessorDock" << "";
        QTest::newRow("Styles") << "root.showPanel(stylesDock)" << "stylesPanel" << "stylesDock" << "stylesTool";
        QTest::newRow("QC notes") << "showQcNotesAction.trigger()" << "qcNotesPanel" << "qcNotesDock" << "";
    }
    void eachCommandOpensItsPanelInTheToolsSideBar()
    {
        QFETCH(QString, command);
        QFETCH(QString, panelName);
        QFETCH(QString, dockName);
        QFETCH(QString, control);
        start();
        call("toggleSideBar", false);
        QTRY_VERIFY(!shown("rightSideBar"));
        shell(command);
        QTRY_VERIFY(shown("rightSideBar"));
        const QByteArray name = panelName.toLatin1();
        QTRY_COMPARE(parentOf(name.constData()), slotOf("rightSideBar"));
        QVERIFY(shown(name.constData()));
        QVERIFY(!item<QObject>(dockName.toLatin1().constData())->property("isOpen").toBool());
        if (!control.isEmpty()) {
            QTRY_VERIFY(in("rightSideBar", control) && in("rightSideBar", control)->isVisible());
            QVERIFY(ui::shownObjects(window).contains(in("rightSideBar", control)));
        }
        if (control == QLatin1String("findText"))
            QTRY_VERIFY(in("rightSideBar", control)->hasActiveFocus());
    }

    void aSecondFindHidesItsSideBar()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles()));
        shell(QStringLiteral("root.openFind(false)"));
        QTRY_VERIFY(in("rightSideBar", QStringLiteral("findText")) && in("rightSideBar", QStringLiteral("findText"))->hasActiveFocus());
        shell(QStringLiteral("root.openFind(false)"));
        QTRY_VERIFY(!shown("rightSideBar"));
        QCOMPARE(panelsOf(saved().value(QStringLiteral("second")).toObject()).contains(QStringLiteral("Search")), true);
        shell(QStringLiteral("root.openFind(false)"));
        QTRY_VERIFY(shown("rightSideBar"));
    }

    void stylesInTheToolsSideBarKeepsItsToolbar_data()
    {
        QTest::addColumn<bool>("minimum");
        QTest::newRow("start width") << false;
        QTest::newRow("minimum width") << true;
    }
    void stylesInTheToolsSideBarKeepsItsToolbar()
    {
        QFETCH(bool, minimum);
        start();
        QVERIFY(application->openFile(writeSubtitles()));
        call("sideBarIconClicked", QStringLiteral("Styles"));
        QTRY_COMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
        QQuickItem *header = in("rightSideBar", QStringLiteral("sideBarHeader"));
        QVERIFY(header);
        QTRY_VERIFY(findItem(header, QStringLiteral("stylesTools")));
        const auto inside = [&](const char *name) {
            QQuickItem *control = findItem(header, QLatin1String(name));
            if (!control || !control->isVisible() || control->width() <= 0)
                return false;
            const QRectF r = control->mapRectToItem(header, QRectF(0, 0, control->width(), control->height()));
            return r.left() >= 0 && r.right() <= header->width() + 0.5;
        };
        const auto where = [&] {
            QQuickItem *tools = findItem(header, QStringLiteral("stylesTools"));
            QQuickItem *title = findItem(header, QStringLiteral("sideBarTitle"));
            return QStringLiteral("header %1, title %2 (natural %3), tools %4 of %5")
                .arg(header->width()).arg(title ? title->width() : -1).arg(title ? title->implicitWidth() : -1)
                .arg(tools ? tools->width() : -1).arg(tools ? tools->implicitWidth() : -1);
        };
        if (!minimum) {
            QTRY_VERIFY2(inside("stylesNew"), qPrintable(where()));
            QTRY_VERIFY(findItem(header, QStringLiteral("stylesNew"))->isEnabled());
            return;
        }
        call("resizeSideBar", false, hikari::ui::SideBarModel().minimumWidth(), true);
        QTRY_COMPARE(item("rightSideBar")->width(), qreal(hikari::ui::SideBarModel().minimumWidth()));
        QTRY_VERIFY(inside("stylesMore"));
        QVERIFY(inside("sideBarMenuButton"));
        QVERIFY(!findItem(header, QStringLiteral("stylesButtons"))->isVisible());
        QVERIFY(findItem(header, QStringLiteral("sideBarTitle"))->width() >= 40);
    }

    void originalTextMovedIntoASideBarKeepsItsTools()
    {
        start();
        QVERIFY(application->openFile(writeSubtitles()));
        shell(QStringLiteral("translationModeAction.trigger()"));
        QTRY_VERIFY(application->editor().translationMode());
        call("moveToSideBar", dock("originalDock"), false, 0);
        QTRY_VERIFY(shown("rightSideBar"));
        QQuickItem *header = in("rightSideBar", QStringLiteral("sideBarHeader"));
        QVERIFY(header);
        const auto chain = [&] {
            QStringList names;
            for (QQuickItem *p = item("originalTextButtons"); p; p = p->parentItem())
                names << (p->objectName().isEmpty() ? QString::fromLatin1(p->metaObject()->className()) : p->objectName());
            return names.join(QLatin1String(" < "));
        };
        QTRY_VERIFY2(findItem(header, QStringLiteral("originalTextButtons")), qPrintable(chain()));
        const auto lineTextIn = [&] {
            for (QQuickItem *p = item("lineText"); p; p = p->parentItem())
                if (p->objectName().endsWith(QLatin1String("Slot")))
                    return p->objectName();
            return QString();
        };
        QTRY_COMPARE(lineTextIn(), QStringLiteral("originalSlot"));
        call("toggleSideBar", false);
        QTRY_COMPARE(lineTextIn(), QStringLiteral("lineTextSlot"));
        call("toggleSideBar", false);
        QTRY_COMPARE(lineTextIn(), QStringLiteral("originalSlot"));
        const auto shownTools = [&] {
            QQuickItem *buttons = findItem(header, QStringLiteral("originalTextButtons"));
            QQuickItem *more = findItem(header, QStringLiteral("originalTextMore"));
            return (buttons && buttons->isVisible()) || (more && more->isVisible());
        };
        QTRY_VERIFY(shownTools());
        call("moveOutOfSideBar", dock("originalDock"));
        QTRY_VERIFY(item<QObject>("originalDock")->property("isOpen").toBool());
        QTRY_VERIFY(!findItem(header, QStringLiteral("originalTextButtons")));
        QTRY_VERIFY(item("originalTextButtons") && item("originalTextButtons")->parentItem()
                    && item("originalTextButtons")->parentItem()->parentItem()
                    && item("originalTextButtons")->parentItem()->parentItem()->objectName() == QLatin1String("dockToolbarSlot"));
    }

    void aPanelShownBeforeTheSideBarsLoadStillOpensInItsSideBar()
    {
        start({}, 0, QStringLiteral("root.showPanel(timingDock)"));
        QTRY_COMPARE(parentOf("timingPanel"), slotOf("rightSideBar"));
        QVERIFY(!item<QObject>("timingDock")->property("isOpen").toBool());
        const QJsonObject tools = saved().value(QStringLiteral("second")).toObject();
        QCOMPARE(tools.value(QStringLiteral("shown")).toString(), QStringLiteral("Timing"));
        QCOMPARE(saved().value(QStringLiteral("first")).toObject().value(QStringLiteral("hidden")).toBool(), true);
    }

    void aNarrowWindowShrinksTheSideBarsBeforeTheMiddle()
    {
        start({}, 1280);
        QVERIFY(application->openFile(writeSubtitles()));
        if (!screenFitsBothSideBars())
            QSKIP("needs a screen wide enough for both side bars, like screen-2560x1440.json");
        if (!shown("leftSideBar"))
            call("toggleSideBar", true);
        QTRY_VERIFY(shown("leftSideBar") && shown("rightSideBar"));
        const int least = middleLeast();
        QVERIFY(least > 0);
        QQuickItem *middle = item("dockingArea");
        QQuickItem *right = item("rightSideBar");
        QTRY_VERIFY2(middle->width() >= least && right->x() + right->width() <= window->width(),
                     qPrintable(QStringLiteral("window %1, left %2, middle %3 (least %4), right %5 at %6")
                                    .arg(window->width()).arg(item("leftSideBar")->width()).arg(middle->width())
                                    .arg(least).arg(right->width()).arg(right->x())));
        QVERIFY(item("leftSideBar")->width() < hikari::application::kSideBarStartWidth);
    }

    void aScreenTooNarrowForBothShowsOneSideBarAtATime()
    {
        start({}, 1280);
        const int screenWidth = window->screen()->availableGeometry().width();
        QVERIFY(application->openFile(writeSubtitles()));
        QTRY_VERIFY(middleLeast() > 0);
        const int least = middleLeast();
        const int bar = hikari::ui::SideBarModel().minimumWidth();
        if (least + 2 * bar <= screenWidth || least + bar > screenWidth)
            QSKIP(qPrintable(QStringLiteral("needs a screen too narrow for both side bars but wide enough for one, like screen-1280x800.json (screen %1, middle %2)")
                                 .arg(screenWidth).arg(least)));
        QTRY_VERIFY(shown("rightSideBar") && !shown("leftSideBar"));
        const auto fits = [&] {
            QQuickItem *middle = item("dockingArea");
            return window->width() <= screenWidth && middle->width() >= middleLeast();
        };
        call("toggleSideBar", true);
        QTRY_VERIFY(shown("leftSideBar") && !shown("rightSideBar"));
        QTRY_VERIFY(fits());
        call("sideBarIconClicked", QStringLiteral("Styles"));
        QTRY_VERIFY(shown("rightSideBar") && !shown("leftSideBar"));
        QCOMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
        QTRY_VERIFY(fits());
        shell(QStringLiteral("showAiAgentAction.trigger()"));
        QTRY_VERIFY(shown("leftSideBar") && !shown("rightSideBar"));
        QTRY_VERIFY(fits());
    }

    void sideBarScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        QVERIFY(application->openFile(writeSubtitles()));
        shell(QStringLiteral("translationModeAction.trigger()"));
        QTRY_VERIFY(application->editor().translationMode());
        call("moveToSideBar", dock("originalDock"), true, 2);
        call("sideBarIconClicked", QStringLiteral("Styles"));
        QTRY_COMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
        if (shown("leftSideBar"))
            QTRY_COMPARE(parentOf("originalPanel"), slotOf("leftSideBar"));
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        const auto shoot = [&](const QString &set) {
            QTRY_VERIFY(window->width() >= window->minimumWidth() && window->contentItem()->width() == window->width());
            for (const auto code : ui::theme::kCodes) {
                settings.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(code));
                QTest::qWait(300);
                QVERIFY(window->grabWindow().save(out + QStringLiteral("/side-bars-") + set + ui::theme::codeName(code) + QStringLiteral(".png")));
            }
        };
        if (window->screen()->availableGeometry().width() >= 1600) {
            window->resize(1600, 900);
            QTRY_COMPARE(item("rightSideBar")->width(), qreal(hikari::application::kSideBarStartWidth));
            shoot(QStringLiteral("1600-"));
            call("resizeSideBar", true, hikari::ui::SideBarModel().minimumWidth(), true);
            call("resizeSideBar", false, hikari::ui::SideBarModel().minimumWidth(), true);
            QTRY_COMPARE(item("rightSideBar")->width(), qreal(hikari::ui::SideBarModel().minimumWidth()));
            shoot(QStringLiteral("1600-minimum-"));
        }
        window->resize(1280, 800);
        QTest::qWait(500);
        QTRY_VERIFY(window->width() >= window->minimumWidth());
        const bool both = shown("leftSideBar") && shown("rightSideBar");
        shoot(QString::number(window->width()) + (both ? QStringLiteral("-both-") : QStringLiteral("-")));
        if (both)
            return;
        call("toggleSideBar", !root()->property("leftSideBarShown").toBool());
        QTRY_VERIFY(shown("leftSideBar") != shown("rightSideBar"));
        shoot(QString::number(window->width()) + QStringLiteral("-other-"));
    }

    void theShippedMiddleKeepsItsAudioAndEditorControls_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<bool>("hideBoth");
        QTest::addColumn<bool>("aiShown");
        const int threshold = hikari::application::kBothSideBarsWindowWidth;
        QTest::newRow("1920: the tools side bar only") << 1920 << 1080 << false << false;
        QTest::newRow("just under the threshold: the tools side bar only") << threshold - 1 << 1200 << false << false;
        QTest::newRow("at the threshold: both side bars") << threshold << 1200 << false << true;
        QTest::newRow("1920: both side bars hidden") << 1920 << 1080 << true << false;
    }
    void theShippedMiddleKeepsItsAudioAndEditorControls()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(bool, hideBoth);
        QFETCH(bool, aiShown);
        if (QGuiApplication::primaryScreen()->availableGeometry().width() < width)
            QSKIP("needs a screen at least as wide as the row");
        start({}, width);
        window->setGeometry(0, 0, width, height);
        QTRY_COMPARE(window->width(), width);
        QTRY_COMPARE(shown("leftSideBar"), aiShown);
        QTRY_VERIFY(shown("rightSideBar"));
        if (hideBoth) {
            call("toggleSideBar", false);
            QTRY_VERIFY(!shown("rightSideBar"));
        }
        QVERIFY(application->openFile(writeSubtitles()));
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->audio().ready(), 20000);
        const auto widths = [&] {
            const auto w = [&](const char *name) { return item(name) ? item(name)->width() : -1.0; };
            return QStringLiteral("window %1, middle %2, left %3, right %4, audio panel %5, audio buttons %6 (folded %7, toggles folded %8)")
                .arg(window->width()).arg(w("dockingArea")).arg(w("leftSideBar")).arg(w("rightSideBar")).arg(w("audioPanel"))
                .arg(w("audioButtons")).arg(item("audioButtons")->property("folded").toBool())
                .arg(item("audioButtons")->property("togglesFolded").toBool());
        };
        QTRY_VERIFY2(item("audioSyncVideo") && item("audioSyncVideo")->isVisible() && item("audioSyncVideo")->width() > 0,
                     qPrintable(widths()));
        QVERIFY2(!item("audioButtons")->property("folded").toBool(), qPrintable(widths()));
        QTRY_VERIFY(item("addTagButton") && item("addTagButton")->isVisible() && item("addTagButton")->width() > 0);
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        QVERIFY(grid);
        QTRY_VERIFY(grid->columnCount() > 0);
        for (int c = 0; c < grid->columnCount(); ++c) {
            const double width = grid->cellRect(0, c).width();
            QVERIFY2(width > 0, qPrintable(grid->columnTitle(c)));
            if (grid->columnTitle(c) == QLatin1String("Style"))
                QVERIFY2(width >= 60 && width <= grid->width() / 3, qPrintable(QString::number(width)));
        }
    }

    void theSideBarsComeBackAfterARestart()
    {
        start();
        call("sideBarIconClicked", QStringLiteral("Styles"));
        QTRY_COMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
        cleanupEngineOnly();
        start();
        QTRY_COMPARE(parentOf("stylesPanel"), slotOf("rightSideBar"));
        QCOMPARE(saved().value(QStringLiteral("second")).toObject().value(QStringLiteral("shown")).toString(),
                 QStringLiteral("Styles"));
    }

private:
    void cleanupEngineOnly()
    {
        delete engine;
        delete application;
        engine = nullptr;
        application = nullptr;
    }
};

QTEST_MAIN(SideBarsShellTests)
#include "side_bars_shell_tests.moc"
