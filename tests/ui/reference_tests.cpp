// R2: reference navigation, linked matching and GRID_SHOW_PREVIEW through the
// composition and the real shell (legacy SubsGridPreview, SubsGrid::
// OnShowPreview, ShowPreviewWindow and ShowSecondComparedLine at 20d647c4).
// The matching itself is pinned in tests/application/reference_navigation_tests.cpp.
//
// Fixtures (times in s): the editing tab edit.ass, the tab after it ref.ass
// and a third tab third.ass.
//   edit.ass   e1 1-2   e2 2-3   e3 8-9
//   ref.ass    0: 1-2   1: 5-6   2: 1.5-2.5   3: 2.5-3.5   4: 10-11
//   third.ass  0: 1-2
// e1 overlaps ref rows 0 and 2, which row 1 separates: two candidates. e2
// overlaps rows 2 and 3: one run. e3 overlaps nothing; legacy's nearest Line
// is row 4 (start 10 is 2 s after e3's 8, end 11 is 2 s after 9; the start's
// row on the tie).

#include "hikari/app/application.h"
#include "docking.h"
#include "line_table_model.h"

#include <QAccessible>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString write(const QTemporaryDir &dir, const char *name, const QByteArray &events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
            events);
    return path;
}

QByteArray hash(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256) : QByteArray();
}

} // namespace

class ReferenceTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString edit, ref, third;
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
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *item(const char *name) const
    {
        if (auto *found = qobject_cast<QQuickItem *>(named(name)))
            return found;
        return findItem(window->contentItem(), QLatin1String(name));
    }
    application::DocumentId tab(int index) const { return application->workspace().tabs()[std::size_t(index)]; }
    application::EditSession &session(application::DocumentId id) const { return *application->files().session(id); }
    // The Document row of a session's active Line, or -1.
    static int activeRow(const application::EditSession &s)
    {
        const auto lines = s.document().lines();
        for (std::size_t r = 0; r < lines.size(); ++r)
            if (s.selection().active == lines[r]->id)
                return int(r);
        return -1;
    }
    int referenceRow() const
    {
        const auto reference = application->workspace().reference();
        return reference ? activeRow(session(*reference)) : -2;
    }
    qulonglong lineId(application::DocumentId id, int row) const
    {
        return session(id).document().lines()[std::size_t(row)]->id.value;
    }
    const ui::ShellController &shell() const { return application->shell(); }
    QString status() const { return item("referenceMatchStatus")->property("text").toString(); }
    void press(Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(window, key, mods);
        QCoreApplication::processEvents();
    }
    static void click(QQuickItem *item, Qt::MouseButton button = Qt::LeftButton,
                      Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QVERIFY(item);
        const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(item->window(), button, mods, at);
    }
    // The scene point a few pixels into row `row` of a Grid (its rowAt).
    QPoint rowPoint(const char *grid, int row) const
    {
        QQuickItem *g = item(grid);
        int found = -1;
        for (int y = 0; g && y < int(g->height()); ++y) {
            int r = -1;
            QMetaObject::invokeMethod(g, "rowAt", Q_RETURN_ARG(int, r), Q_ARG(qreal, qreal(y)));
            if (r == row) {
                found = y;
                break;
            }
        }
        return g ? g->mapToScene(QPointF(g->width() / 2, found + 3)).toPoint() : QPoint();
    }
    void clickRow(const char *grid, int row, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        // A second click soon after the first would be a double click.
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window, Qt::LeftButton, mods, rowPoint(grid, row));
        QCoreApplication::processEvents();
    }
    QString accessibleName(const char *name) const
    {
        QAccessibleInterface *a = QAccessible::queryAccessibleInterface(item(name));
        return a ? a->text(QAccessible::Name) : QString();
    }
    void showPreviewByKey()
    {
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Q, Qt::ControlModifier); // GRID_SHOW_PREVIEW's default Ctrl-Q
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        edit = write(dir, "edit.ass",
                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e1\n"
                     "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,e2\n"
                     "Dialogue: 0,0:00:08.00,0:00:09.00,Default,,0,0,0,,e3\n");
        ref = write(dir, "ref.ass",
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,r0\n"
                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,r1\n"
                    "Dialogue: 0,0:00:01.50,0:00:02.50,Default,,0,0,0,,r2\n"
                    "Dialogue: 0,0:00:02.50,0:00:03.50,Default,,0,0,0,,r3\n"
                    "Dialogue: 0,0:00:10.00,0:00:11.00,Default,,0,0,0,,r4\n");
        third = write(dir, "third.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,t0\n");
    }
    void init()
    {
        application = new app::Application;
        // O2: CheckLastKeyEvent ignores a repeated action within 100 ms.
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(application->openFile(edit));
        QVERIFY(application->openFile(ref));
        QVERIFY(application->openFile(third));
        application->selectTab(0);
        QCOMPARE(application->currentTab(), 0);
        QCOMPARE(activeRow(session(tab(0))), 0); // e1
    }
    void cleanup()
    {
        delete engine;
        delete application;
    }

    // SubsGrid.cpp:277 and 884-887: "Show subtitles preview" with more than
    // one tab and no preview; the menu item shows its binding.
    void previewNeedsAnotherTabAndNoReference()
    {
        QVERIFY(application->canShowPreview());
        auto *menu = named("gridMenu");
        QTest::mouseClick(window, Qt::RightButton, {}, rowPoint("editingGrid", 0));
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(named("showPreview")->property("enabled").toBool());
        QCOMPARE(named("showPreview")->property("text").toString(), QStringLiteral("Show subtitles preview\tCtrl-Q"));
        QMetaObject::invokeMethod(menu, "close");
        QVERIFY(application->showPreview());
        QVERIFY(!application->canShowPreview()); // a reference is shown
        QVERIFY(!application->showPreview());
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window, Qt::RightButton, {}, rowPoint("editingGrid", 0));
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(!named("showPreview")->property("enabled").toBool());
        QMetaObject::invokeMethod(menu, "close");
        // One tab: nothing to preview.
        delete engine;
        engine = nullptr;
        delete application;
        application = new app::Application;
        QVERIFY(application->openFile(edit));
        QVERIFY(!application->canShowPreview());
        QVERIFY(!application->showPreview());
        QVERIFY(!application->workspace().reference());
        engine = new QQmlApplicationEngine; // cleanup() deletes one
    }

    // Ctrl+Q: legacy drew the first other tab's grid in the editing Grid
    // (NewSeeking(false), SubsGridPreview.cpp:917-924). Here that tab's
    // Document is the protected reference in the tray, still a tab, linked,
    // at the first candidate of the editing Line; nothing is written.
    void ctrlQShowsTheNextTabAsTheLinkedReference()
    {
        const QByteArray before = hash(ref);
        const auto refId = tab(1);
        const auto history = session(refId).historySize();
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), refId);
        QVERIFY(application->workspace().referenceIsTab());
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3)); // it stays a tab
        QCOMPARE(application->workspace().editingTarget(), tab(0));
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QVERIFY(shell().referenceLinked());
        QCOMPARE(shell().referenceMatchCount(), 2);
        QCOMPARE(shell().referenceMatchIndex(), 0);
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(status(), QStringLiteral("Match 1 of 2"));
        QVERIFY(item("referenceLinked")->property("checked").toBool());
        // The tab is marked for assistive technology.
        auto *tabItem = item("documentTab1");
        QVERIFY(tabItem);
        QCOMPARE(QAccessible::queryAccessibleInterface(tabItem)->text(QAccessible::Description),
                 QStringLiteral("Protected reference"));
        QVERIFY(!application->workspace().checkContentCommand(refId));
        QCOMPARE(session(refId).historySize(), history);
        QVERIFY(!session(refId).isDirty());
        QCOMPARE(hash(ref), before);
    }

    // NewSeeking from every line change of the editing Grid: a click, the
    // arrows (SubsGridWindow.cpp:1638-1639, 1872-1873) and NextLine
    // (SubsGridBase.cpp:1415-1416). A Line overlapping nothing shows the
    // empty no-match state; legacy's nearest Line only on request.
    void theLinkedReferenceFollowsTheEditingLine()
    {
        showPreviewByKey();
        QCOMPARE(referenceRow(), 0);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down); // e2
        QCOMPARE(activeRow(session(tab(0))), 1);
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(shell().referenceMatchCount(), 1);
        QCOMPARE(status(), QStringLiteral("Match 1 of 1"));
        clickRow("editingGrid", 2); // e3
        QCOMPARE(activeRow(session(tab(0))), 2);
        QVERIFY(shell().referenceNoMatch());
        QCOMPARE(referenceRow(), 2); // left where it was, no substitute
        QCOMPARE(status(), QStringLiteral("No matching Line"));
        QVERIFY(shell().referenceHasNearest());
        QTRY_VERIFY(item("referenceNearest")->isVisible());
        click(item("referenceNearest"));
        QCOMPARE(referenceRow(), 4);
        QVERIFY(shell().referenceNoMatch()); // still no match: it was asked for
        clickRow("editingGrid", 0); // e1
        QCOMPARE(referenceRow(), 0);
        QVERIFY(!item("referenceNearest")->isVisible());
        // An edit of the same Line seeks nothing (only line changes do).
        application->selectReferenceLine(lineId(tab(1), 3));
        QCOMPARE(referenceRow(), 3);
        application->editor().textEdited(QStringLiteral("e1 edited"), 9);
        QVERIFY(application->editor().commit());
        QVERIFY(session(tab(0)).document().lines()[0]->text == std::u8string(u8"e1 edited"));
        QCOMPARE(referenceRow(), 3);
        // The editor's commit and next Line (NextLine) seeks.
        application->editor().textEdited(QStringLiteral("e1 again"), 8);
        QVERIFY(application->editor().commitAndAdvance());
        QCOMPARE(activeRow(session(tab(0))), 1);
        QCOMPARE(referenceRow(), 2);
    }

    // The candidates of a linked Line: counted, Previous/Next match, ends
    // disabled; the buttons are named for assistive technology.
    void candidatesAreCountedAndStepped()
    {
        showPreviewByKey();
        QTRY_VERIFY(item("referenceNextMatch")->isVisible());
        QVERIFY(!item("referencePreviousMatch")->isEnabled());
        QVERIFY(item("referenceNextMatch")->isEnabled());
        click(item("referenceNextMatch"));
        QCOMPARE(shell().referenceMatchIndex(), 1);
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(status(), QStringLiteral("Match 2 of 2"));
        QVERIFY(!item("referenceNextMatch")->isEnabled());
        QVERIFY(!application->stepReferenceMatch(1));
        click(item("referencePreviousMatch"));
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(status(), QStringLiteral("Match 1 of 2"));
        QCOMPARE(accessibleName("referenceLinked"), QStringLiteral("Follow the editing Line"));
        QCOMPARE(accessibleName("referencePreviousMatch"), QStringLiteral("Previous match"));
        QCOMPARE(accessibleName("referenceNextMatch"), QStringLiteral("Next match"));
        QCOMPARE(accessibleName("referenceMatchStatus"), QStringLiteral("Match 1 of 2"));
        QCOMPARE(accessibleName("referenceGrid"), QStringLiteral("Reference Lines (protected, read-only)"));
    }

    // translation-comparison.md: navigation starts independent; linking is
    // explicit and one way. The tray's clicks and keys move the reference's
    // selection only, never the editing target's.
    void navigationIsIndependentUntilLinked()
    {
        QVERIFY(application->openReference(ref)); // opened as a reference: not linked
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QVERIFY(!shell().referenceLinked());
        QCOMPARE(status(), QStringLiteral("Independent navigation"));
        QCOMPARE(referenceRow(), 0);
        const auto target = *application->workspace().editingTarget();
        clickRow("editingGrid", 1);
        QCOMPARE(activeRow(session(target)), 1);
        QCOMPARE(referenceRow(), 0); // not followed
        clickRow("referenceGrid", 1);
        QCOMPARE(referenceRow(), 1);
        QCOMPARE(activeRow(session(target)), 1); // the editing Line stays
        item("referenceGrid")->forceActiveFocus();
        press(Qt::Key_Down);
        QCOMPARE(referenceRow(), 2);
        press(Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(session(*application->workspace().reference()).selection().selected.size(), std::size_t(2));
        QCOMPARE(activeRow(session(target)), 1);
        // Ctrl+A selects every reference Line and leaves the editing target's
        // selection as it was.
        press(Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(session(*application->workspace().reference()).selection().selected.size(), std::size_t(5));
        QCOMPARE(session(target).selection().selected.size(), std::size_t(1));
        QCOMPARE(activeRow(session(target)), 1);
        QCOMPARE(application->workspace().editingTarget(), target);
        // Linking is explicit, and follows from then on.
        click(item("referenceLinked"));
        QVERIFY(shell().referenceLinked());
        QCOMPARE(referenceRow(), 2); // e2's run
        QCOMPARE(shell().referenceMatchCount(), 1);
        clickRow("editingGrid", 0);
        QCOMPARE(referenceRow(), 0);
        click(item("referenceLinked"));
        QVERIFY(!shell().referenceLinked());
        clickRow("editingGrid", 1);
        QCOMPARE(referenceRow(), 0);
    }

    // A reference never becomes writable because it has focus: Ctrl+V
    // (legacy pasted into the previewed grid) is refused, and the Grid's
    // editing keys pressed in the tray (Ctrl+D, Ctrl+X, Shift+Delete) still
    // go to the editing target; Ctrl+C copies the reference's Lines
    // (PREVIEW_COPY).
    void focusNeverMakesTheReferenceWritable()
    {
        showPreviewByKey();
        const auto refId = tab(1), target = tab(0);
        const QByteArray before = hash(ref);
        const auto lines = session(refId).document().lines().size();
        const auto history = session(refId).historySize();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        item("referenceGrid")->forceActiveFocus();
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QGuiApplication::clipboard()->setText(QStringLiteral("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,pasted"));
        press(Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(shell().statusText(), QStringLiteral("The reference is protected (read-only)."));
        QCOMPARE(session(refId).document().lines().size(), lines);
        QCOMPARE(session(target).document().lines().size(), std::size_t(3));
        press(Qt::Key_C, Qt::ControlModifier);
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral(",r0")));
        // The Grid's other keys pressed in the tray act on the editing target,
        // as legacy's did: the preview was a child of the grid it was drawn
        // on (SubsGridPreview.cpp:33-35) whose own table held only Ctrl+C and
        // Ctrl+V (61-65), so the rest reached that grid's accelerators
        // (TabPanel::SetAccels, TabPanel.cpp:100-193) and the frame's.
        // GRID_DUPLICATE_LINES (Ctrl-D, Hotkeys.cpp:173) through runGridHotkey:
        press(Qt::Key_D, Qt::ControlModifier);
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QCOMPARE(session(target).document().lines().size(), std::size_t(4));
        QCOMPARE(session(refId).document().lines().size(), lines);
        // GRID_CUT (Ctrl+X, TabPanel.cpp:104) cuts the editing target's Lines:
        QGuiApplication::clipboard()->clear();
        press(Qt::Key_X, Qt::ControlModifier);
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QCOMPARE(session(target).document().lines().size(), std::size_t(3));
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral(",e1")));
        QVERIFY(!QGuiApplication::clipboard()->text().contains(QStringLiteral(",r")));
        QCOMPARE(session(refId).document().lines().size(), lines);
        // GLOBAL_REMOVE_LINES (Shift-Delete, Hotkeys.cpp:156), the frame's:
        press(Qt::Key_Delete, Qt::ShiftModifier);
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QCOMPARE(session(target).document().lines().size(), std::size_t(2));
        QCOMPARE(session(refId).document().lines().size(), lines);
        QCOMPARE(application->workspace().editingTarget(), target);
        QVERIFY(!application->workspace().checkContentCommand(refId));
        QCOMPARE(session(refId).historySize(), history);
        QVERIFY(!session(refId).isDirty());
        QCOMPARE(hash(ref), before);
    }

    // Close reference, the tray header menu's Close (legacy's close mark,
    // DestroyPreview; D3): the tab stays; a later reference starts
    // independent again. The header has no close button of its own.
    void closingTheReferenceKeepsItsTab()
    {
        showPreviewByKey();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QVERIFY(!item("referenceClose"));
        QObject *menu = named("panelOptionsMenu");
        click(item("referenceMatchStatus"), Qt::RightButton); // the header's menu, from its toolbar
        QTRY_VERIFY(menu->property("visible").toBool());
        QObject *close = named("panelOptionsClose");
        QCOMPARE(close->property("text").toString(), QStringLiteral("Close reference"));
        QVERIFY(QMetaObject::invokeMethod(close, "triggered"));
        QVERIFY(!application->workspace().reference());
        QVERIFY(!shell().referenceLinked());
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3));
        QTRY_VERIFY(!item("referenceDock")->property("isOpen").toBool());
        QVERIFY(application->canShowPreview());
        QVERIFY(application->openReference(third));
        QVERIFY(!shell().referenceLinked());
    }

    // Choosing the reference's tab makes it the editing target: it is no
    // longer the reference (the explicit operation of workspaces.md).
    void choosingThePreviewedTabEndsTheReference()
    {
        showPreviewByKey();
        application->selectTab(1);
        QCOMPARE(application->workspace().editingTarget(), tab(1));
        QVERIFY(!application->workspace().reference());
        QVERIFY(!shell().referenceLinked());
        QVERIFY(application->workspace().checkContentCommand(tab(1)).has_value());
        QTRY_VERIFY(!item("referenceDock")->property("isOpen").toBool());
    }

    // R2-reference-tray: legacy's preview belonged to the grid it was drawn
    // on, so changing tabs left it behind (and it was editable there). The
    // tray is workspace-wide: switching the editing target to another tab
    // keeps the same Protected reference open, linked to the new target's
    // active Line, and switching back follows the first tab's Line again.
    void theTrayStaysAcrossTabSwitches()
    {
        const QByteArray before = hash(ref);
        const auto refId = tab(1);
        showPreviewByKey();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down); // e2
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(shell().referenceMatchCount(), 1);
        application->selectTab(2); // third.ass: t0, 1-2 s
        QCOMPARE(application->workspace().editingTarget(), tab(2));
        QCOMPARE(application->workspace().reference(), refId);
        QVERIFY(application->workspace().referenceIsTab());
        QVERIFY(shell().referenceLinked());
        QVERIFY(item("referenceDock")->property("isOpen").toBool());
        QCOMPARE(shell().referenceMatchCount(), 2); // r0, then r2
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(status(), QStringLiteral("Match 1 of 2"));
        QVERIFY(!application->workspace().checkContentCommand(refId));
        application->selectTab(0); // back to edit.ass, still at e2
        QCOMPARE(application->workspace().reference(), refId);
        QVERIFY(item("referenceDock")->property("isOpen").toBool());
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(status(), QStringLiteral("Match 1 of 1"));
        QVERIFY(!session(refId).isDirty());
        QCOMPARE(hash(ref), before);
    }

    // SubsGridPreview::ContextMenu: every occurrence of the editing Line in
    // the other tabs, "SubsName (lineRangeStart lineRangeLen)", the shown one
    // checked; choosing one shows that tab and Line.
    void theMenuListsTheOccurrencesInEveryOtherTab()
    {
        showPreviewByKey();
        auto texts = [](const QVariantList &rows) {
            QStringList out;
            for (const auto &r : rows)
                out << r.toMap().value(QStringLiteral("text")).toString() +
                           (r.toMap().value(QStringLiteral("checked")).toBool() ? QStringLiteral("*") : QString());
            return out;
        };
        QCOMPARE(texts(application->referenceOccurrences()),
                 (QStringList{"ref.ass (0 1)*", "ref.ass (2 1)", "third.ass (0 1)"}));
        // Through the real menu: a right click on the tray's Grid.
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QTest::mouseClick(window, Qt::RightButton, {}, rowPoint("referenceGrid", 0));
        QTRY_VERIFY(named("referenceOccurrenceMenu")->property("visible").toBool());
        QObject *thirdItem = named("referenceOccurrence2");
        QVERIFY(thirdItem);
        QCOMPARE(thirdItem->property("text").toString(), QStringLiteral("third.ass (0 1)"));
        QVERIFY(QMetaObject::invokeMethod(thirdItem, "triggered"));
        QCOMPARE(application->workspace().reference(), tab(2));
        QVERIFY(application->workspace().referenceIsTab());
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(shell().referenceMatchCount(), 1);
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3)); // ref.ass is an ordinary tab again
        QVERIFY(application->workspace().checkContentCommand(tab(1)).error() ==
                application::TargetRefusal::NotEditingTarget);
        // A Line overlapping nothing: legacy's nearest Line of each tab, length 0.
        clickRow("editingGrid", 2);
        QCOMPARE(texts(application->referenceOccurrences()), (QStringList{"ref.ass (4 0)", "third.ass (0 0)*"}));
        QVERIFY(application->chooseReferenceOccurrence(0));
        QCOMPARE(application->workspace().reference(), tab(1));
        QCOMPARE(referenceRow(), 4);
    }

    // OnShowPreview on a compared grid (SubsGrid.cpp:1416-1420):
    // ShowSecondComparedLine shows the compared partner at the paired Line,
    // and nothing for an unpaired Line; a linked partner follows the pairs,
    // and its first row the editing Grid's (setViaScroll).
    void aComparedGridPreviewsItsPartner()
    {
        QVERIFY(application->compareWithTab(2)); // third: e1 pairs with t0 (in order, no criteria)
        clickRow("editingGrid", 1); // e2: unpaired
        showPreviewByKey();
        QVERIFY(!application->workspace().reference()); // legacy returned before showing
        clickRow("editingGrid", 0); // e1
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), tab(2)); // the partner, not the next tab
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(shell().referenceMatchCount(), 1);
        clickRow("editingGrid", 1); // unpaired: nothing moves
        QVERIFY(shell().referenceNoMatch());
        QVERIFY(!shell().referenceHasNearest());
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(application->comparedReferenceRow(0), 0);
        QCOMPARE(application->comparedReferenceRow(1), -1);
        application->setReferenceLinked(false);
        QCOMPARE(application->comparedReferenceRow(0), -1);
    }

    // The compared grid's scroll (SubsGridWindow.cpp:1462-1465, 1719-1729):
    // ShowSecondComparedLine(scrollPosition, ..., true) gives the linked
    // partner the paired Line as its first row. Compared by times
    // (SubsGridBase.cpp:1757-1790), long.ass row i pairs with partner row
    // i + 5: the partner's first five Lines have times no Line of long.ass has.
    void aLinkedPartnerScrollsWithTheComparedGrid()
    {
        auto at = [](int s) {
            return QStringLiteral("%1:%2:%3.00").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
                .arg(s % 60, 2, 10, QLatin1Char('0')).toLatin1();
        };
        QByteArray longLines, partnerLines;
        for (int i = 0; i < 5; ++i)
            partnerLines += "Dialogue: 0," + at(3000 + i) + "," + at(3000 + i) + ",Default,,0,0,0,,lead\n";
        for (int i = 0; i < 200; ++i) {
            const QByteArray line = "Dialogue: 0," + at(10 + i) + "," + at(11 + i) + ",Default,,0,0,0,,l" +
                                    QByteArray::number(i) + "\n";
            longLines += line;
            partnerLines += line;
        }
        QVERIFY(application->openFile(write(dir, "long.ass", longLines)));
        QVERIFY(application->openFile(write(dir, "partner.ass", partnerLines)));
        application->selectTab(3);
        application->settingsStore()->set("comparison.type", 1); // by times
        QVERIFY(application->compareWithTab(4));
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), tab(4));
        QVERIFY(shell().referenceLinked());
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QQuickItem *editing = item("editingGrid");
        QQuickItem *reference = item("referenceGrid");
        const qreal rh = editing->property("rowHeight").toReal();
        QVERIFY(rh > 0);
        QCOMPARE(reference->property("rowHeight").toReal(), rh);
        // The editing Grid scrolled (its scroll bar writes contentY) to row 30:
        // the partner's first row is 35, the selection untouched.
        const int active = referenceRow();
        editing->setProperty("contentY", 30 * rh);
        QCOMPARE(editing->property("contentY").toReal(), 30 * rh);
        QCOMPARE(reference->property("contentY").toReal(), 35 * rh);
        QCOMPARE(referenceRow(), active);
        editing->setProperty("contentY", 31.5 * rh); // part way into row 31
        QCOMPARE(reference->property("contentY").toReal(), 36 * rh);
        // Unlinked, the partner keeps its own scroll.
        application->setReferenceLinked(false);
        editing->setProperty("contentY", 60 * rh);
        QCOMPARE(reference->property("contentY").toReal(), 36 * rh);
    }
};

QTEST_MAIN(ReferenceTest)
#include "reference_tests.moc"
