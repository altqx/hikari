// R1: subtitle comparison through the composition and the real tab menu
// (legacy Notebook::ContextMenu's "Subtitle comparison", its ID_CHECK_EVENT
// handler and SubsGrid::SubsComparison, RemoveComparison and the calls that
// run them again, at 20d647c4). The menu's items are triggered as a click
// would; the criteria themselves are pinned in
// tests/application/subtitle_comparison_tests.cpp.

#include "hikari/app/application.h"
#include "docking.h"
#include "line_table_model.h"

#include <QCryptographicHash>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;
using ui::LineTableModel;

namespace {

constexpr const char *kStyles =
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n";

QString write(const QTemporaryDir &dir, const char *name, const QByteArray &styles, const QByteArray &events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    QByteArray styleLines(kStyles);
    for (const QByteArray &style : styles.split(','))
        styleLines += "Style: " + style + ",Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,"
                                          "2,2,2,10,10,10,1\n";
    f.write("[Script Info]\nScriptType: v4.00+\n\n" + styleLines +
            "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" + events);
    return path;
}

QByteArray hash(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256) : QByteArray();
}

} // namespace

class ComparisonTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString first, second;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    QObject *named(const char *name) const
    {
        auto *root = engine->rootObjects().first();
        if (auto *found = root->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QObject *o : root->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        return nullptr;
    }
    // The tab menu opened on tab `index` (-1: not on a tab), as a right click does.
    QObject *openTabMenu(int index)
    {
        auto *bar = named("documentTabBar");
        if (!bar)
            return nullptr;
        QMetaObject::invokeMethod(bar, "openTabMenu", Q_ARG(QVariant, index), Q_ARG(QVariant, QVariant::fromValue(bar)),
                                  Q_ARG(QVariant, 10), Q_ARG(QVariant, 5));
        // The style items made for an earlier opening go now.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return named("subtitleComparisonMenu");
    }
    void closeTabMenu()
    {
        if (auto *menu = named("documentTabMenu"))
            QMetaObject::invokeMethod(menu, "close");
    }
    bool enabled(const char *name) const { return named(name)->property("enabled").toBool(); }
    bool checked(const char *name) const { return named(name)->property("checked").toBool(); }
    // A menu item triggered as a click does: a checkable one toggles first.
    void trigger(const char *name)
    {
        QObject *item = named(name);
        QVERIFY2(item, name);
        QVERIFY(QMetaObject::invokeMethod(item, "toggle") || !item->property("checkable").toBool());
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
    }
    QAbstractItemModel *grid(const char *name) const
    {
        return named(name)->property("model").value<QAbstractItemModel *>();
    }
    // The editing Grid's rows: '=' match, 'x' mismatch, '.' neither.
    QString states(const char *gridName = "editingGrid") const
    {
        QString out;
        auto *model = grid(gridName);
        for (int r = 0; r < model->rowCount(); ++r) {
            const int state = model->index(r, 0).data(LineTableModel::ComparisonRole).toInt();
            out += state == 2 ? QLatin1Char('x') : state == 1 ? QLatin1Char('=') : QLatin1Char('.');
        }
        return out;
    }
    application::EditSession &session(int tab) const
    {
        return *application->files().session(application->workspace().tabs()[std::size_t(tab)]);
    }
    int type() const { return application->settingsStore()->integer("comparison.type"); }

    // Repeater delegates are not QObject children of the window: walk the items.
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    // The menu item that opens `menu` (made by its parent's delegate).
    QQuickItem *entryOf(QObject *menu) const
    {
        for (QQuickItem *item : engine->rootObjects().first()->findChildren<QQuickItem *>())
            if (item->property("subMenu").value<QObject *>() == menu)
                return item;
        return nullptr;
    }
    // A left (or right) click in the middle of `item`, in its window.
    static void click(QQuickItem *item, Qt::MouseButton button = Qt::LeftButton)
    {
        QVERIFY(item);
        const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(item->window(), button, {}, at);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        // The active tab (CG1) and the tab the menu is opened on (CG2).
        first = write(dir, "first.ass", "Default,Sign",
                      "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                      "Dialogue: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,two\n"
                      "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n");
        second = write(dir, "second.ass", "Sign,Other,Default",
                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                       "Dialogue: 0,0:00:03.50,0:00:04.00,Sign,,0,0,0,,too\n"
                       "Dialogue: 0,0:00:05.00,0:00:06.00,Sign,,0,0,0,,three\n"
                       "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,four\n");
    }
    void init()
    {
        application = new app::Application;
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(application->openFile(first));
        QVERIFY(application->openFile(second));
        application->selectTab(0);
        QCOMPARE(application->currentTab(), 0);
    }
    void cleanup()
    {
        closeTabMenu();
        delete engine;
        delete application;
    }

    // Notebook.cpp:929-939: the menu is enabled on another tab, or while a
    // comparison is on; Compare only on another tab, Turn off only while on.
    void menuIsEnabledOnAnotherTab()
    {
        QObject *menu = openTabMenu(-1);
        QVERIFY(menu);
        QVERIFY(!menu->property("enabled").toBool());
        closeTabMenu();
        openTabMenu(0); // the active tab
        QVERIFY(!menu->property("enabled").toBool());
        QVERIFY(!enabled("compareSubtitles"));
        closeTabMenu();
        openTabMenu(1);
        QTRY_VERIFY(named("documentTabMenu")->property("opened").toBool());
        QVERIFY(menu->property("enabled").toBool());
        for (const char *name : {"compareByTimes", "compareByVisible", "compareByStyles", "compareSubtitles"})
            QVERIFY2(enabled(name), name);
        QVERIFY(!enabled("turnOffComparison"));
        // "Compare by selections" needs selections in both files (a file
        // opens with its first Line selected).
        QVERIFY(!session(1).selection().selected.empty());
        QVERIFY(enabled("compareBySelections"));
        closeTabMenu();
        session(1).setSelection({}); // no selection in the second file
        QVERIFY(session(1).selection().selected.empty());
        openTabMenu(1);
        QVERIFY(!enabled("compareBySelections"));
        QVERIFY(enabled("compareByTimes"));
        trigger("compareSubtitles");
        // On: the menu stays enabled on the active tab, for Turn off only.
        openTabMenu(0);
        QVERIFY(menu->property("enabled").toBool());
        QVERIFY(!enabled("compareSubtitles"));
        QVERIFY(enabled("turnOffComparison"));
        QVERIFY(!enabled("compareByTimes"));
        closeTabMenu();
        openTabMenu(-1);
        QVERIFY(menu->property("enabled").toBool());
    }

    // Notebook.cpp:917-928, 932-936: the styles both share, in the active
    // tab's order, checked as SUBS_COMPARISON_STYLES has them; each check
    // item shows its SUBS_COMPARISON_TYPE bit.
    void menuShowsTheSavedCriteria()
    {
        application->settingsStore()->set("comparison.type", 1 | 8);
        application->settingsStore()->set("comparison.styles", QStringList{"Sign"});
        openTabMenu(1);
        QVERIFY(checked("compareByTimes"));
        QVERIFY(checked("compareByVisible"));
        QVERIFY(!checked("compareBySelections"));
        QVERIFY(!checked("compareByStyles"));
        QVERIFY(named("compareStyle_Default"));
        QVERIFY(!checked("compareStyle_Default"));
        QVERIFY(checked("compareStyle_Sign"));
        QVERIFY(!named("compareStyle_Other")); // only in the second file
        QVERIFY(checked("compareByChosenStyles"));
        const auto items = application->openComparisonMenu(1).value(QStringLiteral("styleItems")).toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Default"));
        QCOMPARE(items[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Sign"));
    }

    // Notebook.cpp:85-123: each check item flips its bit and saves it; a
    // style's check saves the chosen styles, and the ChosenStyles bit and
    // "Compare by selected styles" follow whether any is chosen.
    void criteriaAreSavedAsTheyChange()
    {
        openTabMenu(1);
        QCOMPARE(type(), 0);
        trigger("compareByTimes");
        QCOMPARE(type(), 1);
        openTabMenu(1);
        QVERIFY(checked("compareByTimes"));
        trigger("compareByStyles");
        QCOMPARE(type(), 1 | 2);
        openTabMenu(1);
        trigger("compareByVisible");
        openTabMenu(1);
        trigger("compareByTimes");
        QCOMPARE(type(), 2 | 8);
        openTabMenu(1);
        QVERIFY(!checked("compareByChosenStyles"));
        trigger("compareStyle_Sign");
        QCOMPARE(application->settingsStore()->list("comparison.styles"), QStringList{"Sign"});
        QCOMPARE(type(), 2 | 4 | 8);
        QVERIFY(checked("compareByChosenStyles"));
        trigger("compareStyle_Sign"); // unchecked again
        QVERIFY(application->settingsStore()->list("comparison.styles").isEmpty());
        QCOMPARE(type(), 2 | 8);
        QVERIFY(!checked("compareByChosenStyles"));
    }

    // MENU_COMPARE: the active tab against the other; each Grid shows its
    // own Document's table (legacy each grid's Comparison). Comparing never
    // writes either Document: no history step, nothing modified, the files
    // unchanged.
    void compareColoursBothDocumentsAndWritesNeither()
    {
        const QByteArray before1 = hash(first), before2 = hash(second);
        const auto history1 = session(0).historySize(), history2 = session(1).historySize();
        openTabMenu(1);
        trigger("compareSubtitles");
        const auto &c = application->comparison();
        QVERIFY(c.active());
        QCOMPARE(c.first(), application->workspace().tabs()[0]);
        QCOMPARE(c.second(), application->workspace().tabs()[1]);
        QCOMPARE(states(), QStringLiteral("=x="));
        auto *model = grid("editingGrid");
        QCOMPARE(model->index(1, 0).data(LineTableModel::ComparisonMarksRole).toList(), (QVariantList{1, 1}));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("=x=."));
        application->selectTab(0);
        QCOMPARE(session(0).historySize(), history1);
        QCOMPARE(session(1).historySize(), history2);
        QVERIFY(!session(0).isDirty());
        QVERIFY(!session(1).isDirty());
        QCOMPARE(hash(first), before1);
        QCOMPARE(hash(second), before2);
        // The menu again: Turn off removes both tables.
        openTabMenu(1);
        trigger("turnOffComparison");
        QVERIFY(!c.active());
        QCOMPARE(states(), QStringLiteral("..."));
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral("...."));
    }

    // The criteria in the menu decide the pairs (SubsGridBase.cpp:1757-1790).
    void criteriaChooseThePairs()
    {
        openTabMenu(1);
        trigger("compareByTimes");
        openTabMenu(1);
        trigger("compareSubtitles");
        QCOMPARE(states(), QStringLiteral("=.="));
        // Compare by styles too: only "one" pairs.
        openTabMenu(1);
        trigger("compareByStyles");
        openTabMenu(1);
        trigger("compareSubtitles");
        QCOMPARE(states(), QStringLiteral("=.."));
        // Selected styles alone: the Sign Lines.
        openTabMenu(1);
        trigger("compareByTimes");
        openTabMenu(1);
        trigger("compareByStyles");
        openTabMenu(1);
        trigger("compareStyle_Sign");
        openTabMenu(1);
        trigger("compareSubtitles");
        QCOMPARE(states(), QStringLiteral(".x."));
    }

    // SetModified and DoUndo run the comparison again (SubsGridBase.cpp:
    // 1000-1002, 1136-1138); a selection change alone does not.
    void editsAndUndoCompareAgain()
    {
        application->settingsStore()->set("comparison.type", 16); // by selections
        const auto lines = session(0).document().lines();
        application->selectLine(lines[1]->id.value);
        application->selectTab(1);
        application->selectLine(session(1).document().lines()[1]->id.value);
        application->selectTab(0);
        QVERIFY(application->compareWithTab(1));
        QCOMPARE(states(), QStringLiteral(".x."));
        // Another selection: the table stays as it was.
        application->selectLine(lines[0]->id.value);
        QCOMPARE(states(), QStringLiteral(".x."));
        // An edit of the active Line ("one" becomes "too"): compared again
        // with the selection of now, Line 0 against the second's Line 1.
        auto &editor = application->editor();
        editor.textEdited(QStringLiteral("too"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral("=.."));
        QVERIFY(editor.undo());
        QCOMPARE(states(), QStringLiteral("x.."));
        // The other Document's edit also compares again (its grid has a
        // table): its selected "too" becomes "one".
        application->selectTab(1);
        QCOMPARE(states(), QStringLiteral(".x.."));
        editor.textEdited(QStringLiteral("one"), 3);
        QVERIFY(editor.commit());
        QCOMPARE(states(), QStringLiteral(".=.."));
        application->selectTab(0);
        QCOMPARE(states(), QStringLiteral("=.."));
    }

    // FilterPartial (a +/- mark) runs RefreshSubsOnVideo only, which
    // compares again with "Compare by visible lines" alone
    // (SubsGrid.cpp:1914); hiding Lines is a SetModified, which always does.
    void openingAHiddenBlockComparesAgainOnlyByVisibleLines()
    {
        for (const bool visible : {false, true}) {
            cleanup();
            init();
            application->settingsStore()->set("comparison.type", 16 | (visible ? 8 : 0));
            const auto lines = session(0).document().lines();
            application->selectLine(lines[1]->id.value);
            application->selectTab(1);
            application->selectAllLines();
            application->selectTab(0);
            QVERIFY(application->compareWithTab(1));
            QCOMPARE(states(), QStringLiteral(".x."));
            QVERIFY(application->hideSelectedLines()); // "two"; it stays selected
            // Another selection, then the block opened with its mark: without
            // the criterion the table is the one made when "two" was hidden.
            application->selectLine(lines[0]->id.value);
            QVERIFY(application->toggleHiddenBlock(0));
            QVERIFY(session(0).document().lines()[1]->visibility != core::LineVisibility::Hidden);
            QCOMPARE(states(), visible ? QStringLiteral("=..") : QStringLiteral(".x."));
        }
    }

    // DeletePage removes the comparison whichever tab closes
    // (Notebook.cpp:241-242).
    void closingATabTurnsComparisonOff()
    {
        application->openFile(write(dir, "third.ass", "Default", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n"));
        application->selectTab(0);
        QVERIFY(application->compareWithTab(1));
        QVERIFY(application->comparison().active());
        QVERIFY(application->reviewCloseTab(2).isEmpty()); // the third tab, unmodified
        application->finishClose();
        QVERIFY(!application->comparison().active());
        QCOMPARE(states(), QStringLiteral("..."));
    }

    // The menu through the pointer, as a user reaches it: a right click on
    // the other tab, "Subtitle comparison", "Compare by selected styles" (a
    // checked item that opens the styles, Notebook.cpp:936), a style, then
    // Compare.
    void rightClickOnATabComparesIt()
    {
        click(findItem(window->contentItem(), QStringLiteral("documentTab1")), Qt::RightButton);
        QObject *tabMenu = named("documentTabMenu");
        QTRY_VERIFY(tabMenu->property("opened").toBool());
        QObject *menu = named("subtitleComparisonMenu");
        QCOMPARE(menu->property("tabIndex").toInt(), 1);
        click(entryOf(menu));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *styles = named("compareByChosenStyles");
        QVERIFY(!styles->property("checked").toBool());
        click(qobject_cast<QQuickItem *>(styles));
        QObject *stylesMenu = named("compareStylesMenu");
        QTRY_VERIFY(stylesMenu->property("opened").toBool());
        QVERIFY(!styles->property("checked").toBool()); // opening it checks nothing
        // A submenu opened by a click takes the next click once its parent
        // menu has settled (its cascade timer), as a user's pointer would.
        QTest::qWait(300);
        click(qobject_cast<QQuickItem *>(named("compareStyle_Sign")));
        QCOMPARE(application->settingsStore()->list("comparison.styles"), QStringList{"Sign"});
        QTRY_VERIFY(!tabMenu->property("visible").toBool());
        // Again from the tab: the style stays checked, then Compare.
        click(findItem(window->contentItem(), QStringLiteral("documentTab1")), Qt::RightButton);
        QTRY_VERIFY(tabMenu->property("opened").toBool());
        click(entryOf(menu));
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(named("compareByChosenStyles")->property("checked").toBool());
        click(qobject_cast<QQuickItem *>(named("compareSubtitles")));
        QTRY_VERIFY(application->comparison().active());
        QCOMPARE(states(), QStringLiteral(".x."));
    }

    // GRID_COMPARISON_*: the Themes page's colours reach both Grids.
    void coloursAreThemeSettings()
    {
        auto &settings = *application->settingsStore();
        QCOMPARE(settings.text("grid.comparisonOutline"), QStringLiteral("#2700FF"));
        QCOMPARE(settings.text("grid.comparisonMismatch"), QStringLiteral("#272B32"));
        QCOMPARE(settings.text("grid.comparisonMatch"), QStringLiteral("#3A3E45"));
        QCOMPARE(settings.text("grid.comparisonCommentMismatch"), QStringLiteral("#003176"));
        QCOMPARE(settings.text("grid.comparisonCommentMatch"), QStringLiteral("#3662A1"));
        // The models of the editing Grid and the reference tray.
        const auto colours = [this](const char *gridName) {
            auto *model = QByteArray(gridName) == "editingGrid" ? application->shell().lines()
                                                                 : application->shell().referenceLines();
            return model->headerData(0, Qt::Horizontal, LineTableModel::ComparisonColoursRole).toList();
        };
        QCOMPARE(colours("editingGrid").value(1), QVariant(QColor(0x27, 0x2B, 0x32)));
        // As the Themes page's OK saves it (shell_tests lists the rows).
        settings.set("grid.comparisonMismatch", QStringLiteral("#112233"));
        QCOMPARE(settings.text("grid.comparisonMismatch"), QStringLiteral("#112233"));
        QCOMPARE(colours("editingGrid").value(1), QVariant(QColor(0x11, 0x22, 0x33)));
        QCOMPARE(colours("referenceGrid").value(1), QVariant(QColor(0x11, 0x22, 0x33)));
    }
};

QTEST_MAIN(ComparisonTest)
#include "comparison_tests.moc"
