#include "hikari/app/application.h"
#include "hikari/application/guide_links.h"
#include "desktop_guard.h"
#include "docking.h"
#include "workspace_layout.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using application::GuideSurface;
namespace recorded = hikari::test::desktop;

namespace {

QUrl guide(GuideSurface kind, const QString &id)
{
    return QUrl(QString::fromStdString(application::guideUrl(kind, id.toStdString())));
}

QQuickItem *itemNamed(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (QQuickItem *child : item->childItems())
        if (QQuickItem *found = itemNamed(child, name))
            return found;
    return nullptr;
}

const QUrl kHome(QStringLiteral("https://hikarisub.altqx.com/en/docs"));

}

class UserGuideTests : public QObject {
    Q_OBJECT

    QTemporaryDir home;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    QObject *root() const { return engine->rootObjects().first(); }
    QVariant run(const QString &script) const
    {
        QQmlExpression expression(qmlContext(root()), root(), script);
        const QVariant value = expression.evaluate();
        if (expression.hasError())
            qWarning() << expression.error().toString();
        return value;
    }
    QObject *named(const QString &name) const
    {
        for (QObject *o : root()->findChildren<QObject *>())
            if (o->objectName() == name)
                return o;
        return nullptr;
    }
    QUrl openedAfter(qsizetype before) const
    {
        const QList<QUrl> opened = recorded::opened();
        return opened.size() == before + 1 ? opened.last() : QUrl();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(home.isValid());
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.spellingBackend = {};
        application = new app::Application(o);
        // MUST KILL: a fake key clock that steps 1 s per read to dodge the hotkey repeat filter
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(root());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
    }
    void cleanupTestCase()
    {
        delete engine;
        delete application;
    }

    void theTableNamesRealSurfaces()
    {
        const QStringList panels = ui::WorkspaceLayoutController::panelIds();
        for (const QString &panel : panels)
            QVERIFY2(!application::guidePage(GuideSurface::Panel, panel.toStdString()).empty(), qPrintable(panel));
        QSet<QString> windows;
        const QRegularExpression id(QStringLiteral(R"re((?:dialogId|guideId): "([A-Za-z]+)")re"));
        QDirIterator files(QStringLiteral(HIKARI_UI_SOURCE_DIR), {QStringLiteral("*.qml")}, QDir::Files);
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            for (auto it = id.globalMatch(text); it.hasNext();)
                windows.insert(it.next().captured(1));
        }
        QSet<QString> seen;
        for (const application::GuideLink &link : application::guideLinks()) {
            const QString surface = QString::fromUtf8(link.id.data(), qsizetype(link.id.size()));
            const QString key = QString::number(int(link.kind)) + QLatin1Char(':') + surface;
            QVERIFY2(!seen.contains(key), qPrintable(surface));
            seen.insert(key);
            QVERIFY2(link.path.starts_with("/en/docs/"), qPrintable(surface));
            if (link.kind == GuideSurface::Panel)
                QVERIFY2(panels.contains(surface.section(QLatin1Char('.'), 0, 0)), qPrintable(surface));
            else
                QVERIFY2(windows.contains(surface), qPrintable(surface));
        }
        QCOMPARE(guide(GuideSurface::Window, QStringLiteral("about")), kHome);
        QCOMPARE(guide(GuideSurface::Panel, QStringLiteral("Grid")),
                 QUrl(QStringLiteral("https://hikarisub.altqx.com/en/docs/basics/grid")));
    }

    void homeOpensTheGuidesHome()
    {
        QObject *homePage = named(QStringLiteral("homePage"));
        QVERIFY(homePage && homePage->property("visible").toBool());
        const qsizetype before = recorded::opened().size();
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_COMPARE(openedAfter(before), kHome);
        QFile file(home.filePath(QStringLiteral("tl.ass")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLMode Original\n\n[Events]\n"
                   "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                   "Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,line\n"
                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,linia\n");
        file.close();
        QVERIFY(application->openFile(file.fileName()));
        QTRY_VERIFY(!homePage->property("visible").toBool());
        QVERIFY(run(QStringLiteral("editor.translationMode")).toBool());
    }

    void f1OnEachPanelOpensItsPage()
    {
        const int count = run(QStringLiteral("panels.length")).toInt();
        QCOMPARE(count, int(ui::WorkspaceLayoutController::panelIds().size()));
        for (int i = 0; i < count; ++i) {
            const QString name = run(QStringLiteral("dockList[%1].uniqueName").arg(i)).toString();
            run(QStringLiteral("showPanel(dockList[%1])").arg(i));
            run(QStringLiteral("focusPanel(panels[%1], Qt.OtherFocusReason)").arg(i));
            auto *panel = qvariant_cast<QQuickItem *>(run(QStringLiteral("panels[%1]").arg(i)));
            QVERIFY2(panel, qPrintable(name));
            QQuickWindow *shown = panel->window();
            QVERIFY2(shown, qPrintable(name));
            shown->requestActivate();
            QTRY_COMPARE(QGuiApplication::focusWindow(), static_cast<QWindow *>(shown));
            QTRY_VERIFY2(run(QStringLiteral("panelOf(%1.activeFocusItem) === panels[%2]")
                                 .arg(shown == window ? QStringLiteral("root") : QStringLiteral("workspaceLayout.focusWindow"))
                                 .arg(i))
                             .toBool(),
                         qPrintable(name));
            const qsizetype before = recorded::opened().size();
            QTest::keyClick(shown, Qt::Key_F1);
            QTRY_COMPARE(openedAfter(before), guide(GuideSurface::Panel, name));
        }
    }

    void f1InTheGrammarModeOpensItsPage()
    {
        run(QStringLiteral("showPanel(spellDock); focusPanel(spellPanel, Qt.OtherFocusReason)"));
        auto *tool = qvariant_cast<QObject *>(run(QStringLiteral("spellTool")));
        QVERIFY(tool);
        QVERIFY(QMetaObject::invokeMethod(tool, "setMode", Q_ARG(QVariant, QStringLiteral("grammar"))));
        auto *panel = qvariant_cast<QQuickItem *>(run(QStringLiteral("spellPanel")));
        QVERIFY(panel && panel->window());
        QQuickWindow *shown = panel->window();
        shown->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), static_cast<QWindow *>(shown));
        qsizetype before = recorded::opened().size();
        QTest::keyClick(shown, Qt::Key_F1);
        QTRY_COMPARE(openedAfter(before),
                     QUrl(QStringLiteral("https://hikarisub.altqx.com/en/docs/checking/grammar")));
        QVERIFY(QMetaObject::invokeMethod(tool, "setMode", Q_ARG(QVariant, QStringLiteral("spelling"))));
        before = recorded::opened().size();
        QTest::keyClick(shown, Qt::Key_F1);
        QTRY_COMPARE(openedAfter(before), guide(GuideSurface::Panel, QStringLiteral("Spellchecker")));
    }

    void theMenuItemOpensTheFocusedPanelsPage()
    {
        run(QStringLiteral("showPanel(gridDock); focusPanel(gridPanel, Qt.OtherFocusReason)"));
        QObject *item = named(QStringLiteral("userGuideMenuItem"));
        QVERIFY(item);
        auto *action = item->property("action").value<QObject *>();
        QVERIFY(action);
        QCOMPARE(action->property("text").toString(), QStringLiteral("User &guide"));
        const qsizetype before = recorded::opened().size();
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_COMPARE(openedAfter(before), guide(GuideSurface::Panel, QStringLiteral("Grid")));
    }

    void windowsOpenTheirPage()
    {
        run(QStringLiteral("openSettings('general')"));
        auto *settings = qobject_cast<QQuickWindow *>(named(QStringLiteral("settingsDialog")));
        QVERIFY(settings);
        QTRY_VERIFY(settings->isVisible());
        settings->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), static_cast<QWindow *>(settings));
        auto *help = itemNamed(settings->contentItem(), QStringLiteral("dialogTitleBarHelp"));
        QVERIFY(help);
        const bool drawnBar = help->parentItem()->isVisible();
        QCOMPARE(help->isVisible(), drawnBar);
        QCOMPARE(help->property("text").toString(), QStringLiteral("Help"));
        QCOMPARE(help->property("iconRole").toString(), QStringLiteral("help"));
        QCOMPARE(help->property("focusPolicy").toInt(), int(Qt::NoFocus));
        qsizetype before = recorded::opened().size();
        QTest::keyClick(settings, Qt::Key_F1);
        QTRY_COMPARE(openedAfter(before), guide(GuideSurface::Window, QStringLiteral("settings")));
        if (drawnBar) {
            before = recorded::opened().size();
            QVERIFY(QMetaObject::invokeMethod(help, "clicked"));
            QTRY_COMPARE(openedAfter(before), guide(GuideSurface::Window, QStringLiteral("settings")));
        }
        settings->close();
        QTRY_VERIFY(!settings->isVisible());

        auto *about = qobject_cast<QQuickWindow *>(named(QStringLiteral("aboutDialog")));
        QVERIFY(about);
        QVERIFY(QMetaObject::invokeMethod(about, "open"));
        QTRY_VERIFY(about->isVisible());
        about->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), static_cast<QWindow *>(about));
        auto *none = itemNamed(about->contentItem(), QStringLiteral("dialogTitleBarHelp"));
        QVERIFY(none);
        QVERIFY(!none->isVisible());
        QVERIFY(!run(QStringLiteral("UserGuide.hasPage('window', 'about')")).toBool());
        QVERIFY(run(QStringLiteral("UserGuide.hasPage('window', 'settings')")).toBool());
        before = recorded::opened().size();
        QTest::keyClick(about, Qt::Key_F1);
        QTRY_COMPARE(openedAfter(before), kHome);
        about->close();
        QTRY_VERIFY(!about->isVisible());
    }
};

QTEST_MAIN(UserGuideTests)
#include "user_guide_tests.moc"
