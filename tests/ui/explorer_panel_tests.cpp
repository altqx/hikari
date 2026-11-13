#include "hikari/app/application.h"
#include "hikari/application/hotkeys.h"
#include "docking.h"
#include "explorer_model.h"
#include "settings_store.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
#include <utility>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {


void writeAll(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray ass(const char *text)
{
    return QByteArray("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                      "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                      "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,") +
           text + "\n";
}

}

class ExplorerPanelTests : public QObject {
    Q_OBJECT

    // Off the home volume (a tmpfs /tmp) Qt trashes into a top-level .Trash-<uid> outside the test.
    QTemporaryDir home{QDir::current().filePath(QStringLiteral("explorer-XXXXXX"))};
    QString project;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    QStringList revealed;

    void start()
    {
        app::Application::Options o;
        o.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        o.playbackAudio = false;
        o.revealInFolder = [this](const QString &path) { revealed << path; };
        application = new app::Application(o);
        engine = new QQmlApplicationEngine;
        ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->property("arrangementSettled").toBool());
    }
    void stop()
    {
        application->cancelClose();
        const QVariantList rows = application->reviewClose(QStringLiteral("quit"));
        QVariantList discard;
        for (const QVariant &r : rows)
            discard << QVariantMap{{QStringLiteral("id"), r.toMap().value(QStringLiteral("id"))},
                                   {QStringLiteral("save"), false},
                                   {QStringLiteral("path"), QString()}};
        if (!discard.isEmpty())
            application->resolveClose(discard);
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        window = nullptr;
    }
    void openProject()
    {
        const QVariantMap opened = application->openFolder(project, 0);
        QVERIFY(opened.value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE(model()->folder(), QDir::cleanPath(project));
        QTRY_VERIFY(dockOpen());
        auto *explorer = qobject_cast<QQuickItem *>(named("explorerPanel"));
        auto *home = qobject_cast<QQuickItem *>(named("homePage"));
        QTRY_VERIFY(!home->isVisible()
                    || home->mapRectToScene(home->boundingRect()).left()
                           >= explorer->mapRectToScene(explorer->boundingRect()).right() - 1);
    }
    QString at(const QString &relative) const { return QDir(project).filePath(relative); }
    QObject *named(const char *name) const { return window->findChild<QObject *>(QLatin1String(name)); }
    bool dockOpen() const { return named("explorerDock")->property("isOpen").toBool(); }
    ui::ExplorerModel *model() const { return window->findChild<ui::ExplorerModel *>(); }
    static QQuickItem *findShown(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name && from->isVisible())
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findShown(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *shown(const char *name) const { return findShown(window->contentItem(), QLatin1String(name)); }
    QQuickItem *row(const QString &name) const { return findShown(window->contentItem(), QStringLiteral("explorerRow_") + name); }
    int rowOf(const QString &name) const { return model()->rowOf(at(name)); }
    QPoint centre(QQuickItem *item) const { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    void type(const QString &text)
    {
        for (const QChar c : text)
            QTest::keyClick(window, c.toLatin1());
    }
    void click(QQuickItem *item) { QTest::mouseClick(window, Qt::LeftButton, {}, centre(item)); }
    void doubleClick(const QString &name)
    {
        QTRY_VERIFY(row(name));
        QPoint at(-1, -1);
        QTRY_VERIFY([&] {
            const QPoint now = centre(row(name));
            return std::exchange(at, now) == now;
        }());
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 20);
        QTest::mouseDClick(window, Qt::LeftButton, {}, centre(row(name)));
    }
    QStringList titles() const
    {
        QStringList out;
        for (const QVariant &tab : application->windowTabs(0))
            out << tab.toMap().value(QStringLiteral("title")).toString();
        return out;
    }
    QString currentTitle() const
    {
        for (const QVariant &tab : application->windowTabs(0))
            if (tab.toMap().value(QStringLiteral("current")).toBool())
                return tab.toMap().value(QStringLiteral("title")).toString();
        return {};
    }
    void menuAction(const QString &name, const char *item)
    {
        QQuickItem *r = row(name);
        QVERIFY2(r, qPrintable(name));
        QVERIFY(QMetaObject::invokeMethod(named("explorerTool"), "showMenu", Q_ARG(QVariant, rowOf(name)),
                                          Q_ARG(QVariant, QVariant::fromValue(r)), Q_ARG(QVariant, 4), Q_ARG(QVariant, 4)));
        QObject *menu = named("explorerRowMenu");
        QTRY_VERIFY(menu->property("visible").toBool());
        QObject *entry = named(item);
        QVERIFY2(entry && entry->property("enabled").toBool(), item);
        QVERIFY(QMetaObject::invokeMethod(entry, "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
    }
    void answer(const QString &choice)
    {
        QQuickWindow *box = nullptr;
        QTRY_VERIFY((box = qvariant_cast<QQuickWindow *>(named("messages")->property("box"))) && box->isVisible());
        QVERIFY(QMetaObject::invokeMethod(box, "answer", Q_ARG(QVariant, choice)));
        QTRY_VERIFY(!box->isVisible());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(home.isValid());
        const QString xdg = home.filePath(QStringLiteral("xdg"));
        QVERIFY(QDir().mkpath(xdg));
        qputenv("XDG_DATA_HOME", xdg.toUtf8());
        QCOMPARE(application::hotkeyIdOf("GLOBAL_SHOW_EXPLORER"), 5337);
    }

    void init()
    {
        project = home.filePath(QString::fromLatin1(QTest::currentTestFunction()));
        writeAll(at("ep01.ass"), ass("one"));
        writeAll(at("ep02.ass"), ass("two"));
        writeAll(at("ep01_keyframes.txt"), "# keyframe format v1\nfps 0\n0\n12\n");
        writeAll(at("ep01.mkv.lwi"), "index");
        writeAll(at("readme.md"), "notes");
        writeAll(at("fonts/Font.ttf"), "font");
        writeAll(at(".hikari/project.toml"), "version = 1\n");
        revealed.clear();
        start();
    }

    void cleanup()
    {
        stop();
        QDir(project).removeRecursively();
    }

    void theExplorerOpensLeftOfTheGridWithTheProject()
    {
        QVERIFY(!dockOpen());
        QVERIFY(QMetaObject::invokeMethod(named("showExplorer"), "triggered"));
        QTRY_VERIFY(shown("explorerEmpty"));
        QVERIFY(QMetaObject::invokeMethod(named("explorerDock"), "close"));
        QTRY_VERIFY(!dockOpen());
        openProject();
        QTRY_VERIFY(dockOpen());
        auto *explorer = qobject_cast<QQuickItem *>(named("explorerPanel"));
        auto *grid = qobject_cast<QQuickItem *>(named("gridPanel"));
        QVERIFY(explorer && grid);
        const QRectF e = explorer->mapRectToScene(explorer->boundingRect());
        const QRectF g = grid->mapRectToScene(grid->boundingRect());
        QVERIFY2(e.right() <= g.left() + 1, qPrintable(QStringLiteral("%1 vs %2").arg(e.right()).arg(g.left())));
        QVERIFY(e.width() >= 180);
        auto *home = qobject_cast<QQuickItem *>(named("homePage"));
        QVERIFY(home && home->isVisible());
        QTRY_VERIFY(home->mapRectToScene(home->boundingRect()).left() >= e.right() - 1);
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        QVERIFY(row(QStringLiteral("Project settings")));
        QVERIFY(!row(QStringLiteral("readme.md")));
        QVERIFY(!row(QStringLiteral("ep01.mkv.lwi")));
        QVERIFY(QMetaObject::invokeMethod(window, "closeProject"));
        QTRY_VERIFY(!dockOpen());
        QCOMPARE(model()->count(), 0);
    }

    void theCommandShowsTheExplorer()
    {
        openProject();
        QTRY_VERIFY(dockOpen());
        QVERIFY(QMetaObject::invokeMethod(named("explorerDock"), "close"));
        QTRY_VERIFY(!dockOpen());
        QObject *item = named("showExplorer");
        QCOMPARE(item->property("command").toString(), QStringLiteral("GLOBAL_SHOW_EXPLORER"));
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QTRY_VERIFY(dockOpen());
    }

    void doubleClickOpensSubtitlesInATabOnce()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        doubleClick(QStringLiteral("ep01.ass"));
        QTRY_VERIFY(titles().contains(QStringLiteral("ep01.ass")));
        doubleClick(QStringLiteral("ep02.ass"));
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep02.ass"));
        const int tabs = int(titles().size());
        doubleClick(QStringLiteral("ep01.ass"));
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep01.ass"));
        QCOMPARE(int(titles().size()), tabs);
    }

    void openInThisTabReplacesTheTab()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        menuAction(QStringLiteral("ep01.ass"), "explorerMenuOpenNewTab");
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep01.ass"));
        const int tabs = int(titles().size());
        menuAction(QStringLiteral("ep02.ass"), "explorerMenuOpenHere");
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep02.ass"));
        QCOMPARE(int(titles().size()), tabs);
        QVERIFY(!titles().contains(QStringLiteral("ep01.ass")));
    }

    void openInTabOpensANewCurrentTabOrReplacesAnUntouchedUntitled()
    {
        openProject();
        application->addPage();
        QTRY_COMPARE(int(titles().size()), 1);
        QVERIFY(application->targetUntitled());
        QVERIFY(application->openInTab(at("ep01.ass")));
        QTRY_COMPARE(titles(), QStringList{QStringLiteral("ep01.ass")});
        QVERIFY(application->openInTab(at("ep02.ass")));
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep02.ass"));
        QCOMPARE(titles(), (QStringList{QStringLiteral("ep01.ass"), QStringLiteral("ep02.ass")}));
    }

    void rightStepsAFrameUnlessTheExplorerHasFocus()
    {
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), at("raw.mkv")));
        openProject();
        doubleClick(QStringLiteral("raw.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QQuickItem *video = nullptr;
        QTRY_VERIFY((video = shown("videoPanel")));
        video->forceActiveFocus();
        const int frame = application->video().frame();
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(application->video().frame(), frame + 1);
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        click(row(QStringLiteral("ep01.ass")));
        QTRY_VERIFY(shown("explorerList")->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Right);
        QTest::qWait(200);
        QCOMPARE(application->video().frame(), frame + 1);
    }

    void homeBesideTheExplorerScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        QTest::qWait(300);
        QVERIFY(window->grabWindow().save(QDir(out).filePath(QStringLiteral("explorer-home-after.png"))));
        QVERIFY(window->setProperty("homeInset", 0));
        QTest::qWait(300);
        QVERIFY(window->grabWindow().save(QDir(out).filePath(QStringLiteral("explorer-home-before.png"))));
    }

    void mediaFilesBecomeTheTabsMedia()
    {
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), at("raw.mkv")));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"), at("music.mka")));
        openProject();
        QTRY_VERIFY(row(QStringLiteral("raw.mkv")));
        doubleClick(QStringLiteral("raw.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        doubleClick(QStringLiteral("ep01_keyframes.txt"));
        QTRY_COMPARE(application->video().session().keyframes(), (std::vector<int>{0, 12}));
        menuAction(QStringLiteral("music.mka"), "explorerMenuOpenHere");
        QTRY_VERIFY_WITH_TIMEOUT(application->audio().ready(), 20000);
        QVERIFY(application->audio().path().endsWith(QStringLiteral("music.mka")));
    }

    void theProjectSettingsRowOpensTheProjectTab()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("Project settings")));
        doubleClick(QStringLiteral("Project settings"));
        QObject *settings = nullptr;
        QTRY_VERIFY((settings = named("settingsLoader")->property("item").value<QObject *>()) &&
                    settings->property("visible").toBool());
        QVERIFY(settings->property("projectTab").toBool());
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
    }

    void renameWithF2()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep02.ass")));
        click(row(QStringLiteral("ep02.ass")));
        QTest::keyClick(window, Qt::Key_F2);
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = shown("explorerRenameField")) && field->hasActiveFocus());
        QCOMPARE(field->property("selectedText").toString(), QStringLiteral("ep02"));
        type(QStringLiteral("ep01"));
        QTRY_VERIFY(shown("explorerProblem") && !shown("explorerProblem")->property("text").toString().isEmpty());
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(QFileInfo::exists(at("ep02.ass")));
        QTest::keyClick(window, Qt::Key_Backspace);
        type(QStringLiteral("3"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo::exists(at("ep03.ass")));
        QVERIFY(!QFileInfo::exists(at("ep02.ass")));
        QTRY_VERIFY(row(QStringLiteral("ep03.ass")));
        QCOMPARE(model()->currentPath(), at("ep03.ass"));
    }

    void deleteMovesToTheTrashAfterAsking()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep02.ass")));
        click(row(QStringLiteral("ep02.ass")));
        QTest::keyClick(window, Qt::Key_Delete);
        answer(QStringLiteral("cancel"));
        QVERIFY(QFileInfo::exists(at("ep02.ass")));
        menuAction(QStringLiteral("ep02.ass"), "explorerMenuTrash");
        answer(QStringLiteral("trash"));
        QTRY_VERIFY(!QFileInfo::exists(at("ep02.ass")));
        QTRY_VERIFY(!row(QStringLiteral("ep02.ass")));
    }

    void newSubtitleFileHereCreatesAndOpensIt()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("fonts")));
        menuAction(QStringLiteral("fonts"), "explorerMenuNewFile");
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = shown("explorerNewFileName")) && field->hasActiveFocus());
        QCOMPARE(field->property("selectedText").toString(), QStringLiteral("untitled"));
        type(QStringLiteral("ep05"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo::exists(at("fonts/ep05.ass")));
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep05.ass"));
        QTRY_VERIFY(row(QStringLiteral("ep05.ass")));
    }

    void showInFolderAndCopyPathUseTheNativePath()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        menuAction(QStringLiteral("ep01.ass"), "explorerMenuShowInFolder");
        QCOMPARE(revealed, QStringList{QDir::toNativeSeparators(at("ep01.ass"))});
        menuAction(QStringLiteral("ep01.ass"), "explorerMenuCopyPath");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QDir::toNativeSeparators(at("ep01.ass")));
    }

    void theHeaderTogglesAreTheUsersAndTheExcludesTheProjects()
    {
        writeAll(at(".hikari/project.toml"), "version = 1\n[explorer]\nexclude = [\"*.md\"]\n");
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        QCOMPARE(model()->excludes(), QStringList{QStringLiteral("*.md")});
        auto *allFiles = shown("explorerAllFiles");
        QTRY_VERIFY(allFiles && allFiles->isVisible());
        click(allFiles);
        QTRY_VERIFY(row(QStringLiteral("ep01.mkv.lwi")));
        QVERIFY(!row(QStringLiteral("readme.md")));
        QCOMPARE(application->settingsStore()->value(QStringLiteral("explorer.allFiles")).toBool(), true);
        click(shown("explorerGroupByName"));
        QTRY_VERIFY(model()->groupByName());
        QCOMPARE(application->settingsStore()->value(QStringLiteral("explorer.groupByName")).toBool(), true);
        QTRY_VERIFY(!row(QStringLiteral("ep01_keyframes.txt")));
        QVERIFY(model()->rowAt(rowOf(QStringLiteral("ep01.ass"))).value(QStringLiteral("expandable")).toBool());
    }

    void theTreeFollowsTheDisk()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("ep01.ass")));
        writeAll(at("ep06.ass"), ass("six"));
        QTRY_VERIFY(row(QStringLiteral("ep06.ass")));
        QVERIFY(QFile::remove(at("ep01.ass")));
        QTRY_VERIFY(!row(QStringLiteral("ep01.ass")));
    }

    void keysWalkTheTree()
    {
        openProject();
        QTRY_VERIFY(row(QStringLiteral("fonts")));
        click(row(QStringLiteral("fonts")));
        QCOMPARE(model()->currentPath(), at("fonts"));
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(row(QStringLiteral("Font.ttf")));
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(model()->currentPath(), at("fonts/Font.ttf"));
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(model()->currentPath(), at("fonts"));
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(!row(QStringLiteral("Font.ttf")));
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(model()->currentPath(), at("ep01.ass"));
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(currentTitle(), QStringLiteral("ep01.ass"));
    }
};

QTEST_MAIN(ExplorerPanelTests)
#include "explorer_panel_tests.moc"
