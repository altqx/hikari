#include "line_grid.h"
#include "line_grid_accessible.h"
#include "line_table_model.h"
#include "hikari/core/ass_load.h"

#include <QAccessible>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <cstdio>
#include <cstring>
#include <string>

using namespace hikari;
using namespace hikari::ui;

namespace {

core::Document load(const std::string &s)
{
    std::vector<std::byte> bytes(s.size());
    std::memcpy(bytes.data(), s.data(), s.size());
    return core::loadAss(bytes).document;
}

core::Document generate(int lines)
{
    std::string s = "[Events]\n";
    char buf[128];
    for (int i = 0; i < lines; ++i) {
        std::snprintf(buf, sizeof buf, "%s: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line %d\n",
                      i % 10 == 1 ? "Comment" : "Dialogue", i + 1);
        s += buf;
    }
    return load(s);
}

core::Document withGeneratedRun()
{
    std::string s = "[Events]\n";
    for (int i = 0; i < 8; ++i)
        s += "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0," + std::string(i >= 3 ? "fx" : "") + ",line " +
             std::to_string(i + 1) + "\n";
    return load(s);
}

struct Captured {
    QAccessible::Event type;
    QString text;
};
QList<Captured> captured;

QList<int> expandedRows;

void captureExpandedRows(QAccessibleEvent *event)
{
    if (event->type() != QAccessible::StateChanged ||
        !static_cast<QAccessibleStateChangeEvent *>(event)->changedStates().expanded)
        return;
    if (auto *cell = event->accessibleInterface() ? event->accessibleInterface()->tableCellInterface() : nullptr)
        expandedRows.append(cell->rowIndex());
}

void captureUpdates(QAccessibleEvent *event)
{
    QString text;
    if (event->type() == QAccessible::Announcement)
        text = static_cast<QAccessibleAnnouncementEvent *>(event)->message();
    else if (QAccessibleInterface *iface = event->accessibleInterface())
        text = iface->text(QAccessible::Name);
    captured.append({event->type(), text});
}

struct Rig {
    QQuickWindow window;
    LineTableModel lines;
    LineFilterModel filter;
    LineGrid *grid = nullptr;

    explicit Rig(int count) : Rig(generate(count)) {}
    explicit Rig(core::Document document)
    {
        lines.setDocument(std::move(document));
        filter.setLineModel(&lines);
        window.resize(800, 400);
        grid = new LineGrid(window.contentItem());
        grid->setSize(QSizeF(800, 400));
        grid->setModel(&filter);
        QObject::connect(grid, &LineGrid::activeLineRequested, grid, [this](qulonglong id) {
            lines.setSelection({core::LineId{id}, lines.selection().selected}, core::LineId{id});
        });
        QObject::connect(grid, &LineGrid::activeLineFallbackRequested, grid, [this](qulonglong id) {
            lines.setSelection({core::LineId{id}, lines.selection().selected}, core::LineId{id});
        });
        window.show();
        if (!QTest::qWaitForWindowExposed(&window))
            qFatal("grid window was not exposed");
    }
    QAccessibleTableInterface *table() const
    {
        return QAccessible::queryAccessibleInterface(grid)->tableInterface();
    }
};

}

class LineGridA11yTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QAccessible::setActive(true); }

    void isATableOfAllLines()
    {
        Rig rig(50'000);
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(rig.grid);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Table);
        QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Subtitle lines"));
        QCOMPARE(rig.table()->rowCount(), 50'000);
        QCOMPARE(rig.table()->columnCount(), 13);
        QCOMPARE(rig.table()->columnDescription(12), QStringLiteral("Text"));
        QCOMPARE(rig.table()->columnDescription(10), QStringLiteral("CPS"));
        QAccessibleInterface *cell = rig.table()->cellAt(49'999, 12);
        QVERIFY(cell);
        QCOMPARE(cell->role(), QAccessible::Cell);
        QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("line 50000"));
    }

    // Qt's Windows UIA bridge gives an element its own host HWND when its parent has another window or none.
    void cellsAndTableAreFragmentsOfTheWindow()
    {
        Rig rig(10);
        const auto hostsAWindow = [](QAccessibleInterface *iface) {
            QWindow *w = iface->window();
            return w && (!iface->parent() || iface->parent()->window() != w);
        };
        QAccessibleInterface *table = QAccessible::queryAccessibleInterface(rig.grid);
        QAccessibleInterface *cell = rig.table()->cellAt(3, 12);
        QVERIFY(table && cell);
        QCOMPARE(table->window(), &rig.window);
        QCOMPARE(cell->window(), &rig.window);
        QCOMPARE(cell->role(), QAccessible::Cell);
        QCOMPARE(cell->parent(), table);
        QCOMPARE(table->role(), QAccessible::Table);
        QVERIFY(!hostsAWindow(cell));
        QVERIFY(!hostsAWindow(table));
        QAccessibleInterface *windowIface = QAccessible::queryAccessibleInterface(&rig.window);
        QAccessibleInterface *up = table;
        for (int steps = 0; up && up != windowIface && steps < 10; ++steps) {
            QCOMPARE(up->window(), &rig.window);
            QVERIFY(up->role() != QAccessible::Window && up->role() != QAccessible::Client);
            up = up->parent();
        }
        QCOMPARE(up, windowIface);
        QVERIFY(hostsAWindow(windowIface));

        rig.grid->forceActiveFocus();
        QTest::keyClick(&rig.window, Qt::Key_Down);
        QTest::keyClick(&rig.window, Qt::Key_Down);
        QCOMPARE(rig.grid->currentRow(), 1);
        QAccessibleInterface *focused = table->focusChild();
        QVERIFY(focused);
        QCOMPARE(focused->role(), QAccessible::Cell);
        QCOMPARE(focused->tableCellInterface()->rowIndex(), 1);
        QVERIFY(focused->state().focused);
        QVERIFY(!hostsAWindow(focused));
        QAccessibleInterface *fromWindow = windowIface->focusChild();
        for (int steps = 0; fromWindow && fromWindow->focusChild() && steps < 10; ++steps)
            fromWindow = fromWindow->focusChild();
        QCOMPARE(fromWindow, focused);
    }

    void viewportReportsTheShownRows()
    {
        Rig rig(1'000);
        QAccessibleViewportInterface *viewport = QAccessible::queryAccessibleInterface(rig.grid)->viewportInterface();
        QVERIFY(viewport);
        QVERIFY(viewport->isIndexed());
        QCOMPARE(viewport->contentSize(), QSizeF(13, 1'000));
        QCOMPARE(viewport->position(), QPointF(0, 0));
        const int shown = rig.grid->geometry().visibleRowCount(0, rig.grid->height(), 1'000);
        QVERIFY(shown > 1 && shown < 1'000);
        QCOMPARE(viewport->viewportSize(), QSizeF(1, shown / 1'000.0));

        rig.grid->scrollToTopRow(500);
        QCOMPARE(viewport->position(), QPointF(0, 0.5));
        viewport->setPosition(QPointF(0, 0.25));
        QCOMPARE(rig.grid->contentY(), 250 * rig.grid->rowHeight());

        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(viewport->contentSize(), QSizeF(13, 900));
    }

    void foldedRunsAreExpandableTextCells()
    {
        Rig rig(withGeneratedRun());
        rig.filter.changeFolds([&] { rig.lines.setFoldGenerated(true); });
        QCOMPARE(rig.table()->rowCount(), 4);
        QAccessibleInterface *plain = rig.table()->cellAt(2, 12);
        QAccessibleInterface *run = rig.table()->cellAt(3, 12);
        QVERIFY(!plain->state().expandable);
        QVERIFY(!rig.table()->cellAt(3, 0)->state().expandable);
        QVERIFY(run->state().expandable);
        QVERIFY(!run->state().expanded);

        expandedRows.clear();
        QAccessible::installUpdateHandler(captureExpandedRows);
        rig.filter.changeFolds([&] { rig.lines.setRunFolded(core::LineId{4}, false); });
        QAccessible::installUpdateHandler(nullptr);
        QCOMPARE(rig.table()->rowCount(), 8);
        QVERIFY(run->state().expandable);
        QVERIFY(run->state().expanded);
        QCOMPARE(expandedRows, QList<int>{3});
        QVERIFY(!rig.table()->cellAt(4, 12)->state().expandable);
        QCOMPARE(run->text(QAccessible::Description), QStringLiteral("4, Text, unfolded"));
    }

    void hiddenColumnsLeaveTheTable()
    {
        Rig rig(10);
        rig.lines.setHiddenColumns(1 | 512 | 8192);
        QCOMPARE(rig.table()->columnCount(), 10);
        QCOMPARE(rig.table()->columnDescription(1), QStringLiteral("Start"));
        QCOMPARE(rig.table()->columnDescription(9), QStringLiteral("Text"));
        QCOMPARE(rig.table()->cellAt(0, 9)->text(QAccessible::Name), QStringLiteral("line 1"));
        rig.lines.setHiddenColumns(0);
        QCOMPARE(rig.table()->columnCount(), 13);
    }

    void cellsKeepLineIdentityThroughFiltering()
    {
        Rig rig(100);
        QAccessibleInterface *cell = rig.table()->cellAt(20, 12);
        QCOMPARE(cell, rig.table()->cellAt(20, 12));
        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(cell->tableCellInterface()->rowIndex(), 18);
        QCOMPARE(cell, rig.table()->cellAt(18, 12));
        QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("line 21"));
    }

    void selectionIncludesOnlyShownRowsAndCountsHidden()
    {
        Rig rig(100);
        rig.lines.setSelection({core::LineId{3}, {core::LineId{2}, core::LineId{3}}}, core::LineId{2});
        QCOMPARE(rig.table()->selectedRows(), (QList<int>{1, 2}));
        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(rig.table()->selectedRows(), (QList<int>{1}));
        QCOMPARE(rig.grid->selectedCount(), 2);
        QCOMPARE(rig.grid->hiddenSelectedCount(), 1);
    }

    void cellCacheIsBounded()
    {
        Rig rig(1'000);
        auto *a11y = static_cast<LineGridAccessible *>(QAccessible::queryAccessibleInterface(rig.grid));
        const QAccessible::Id first = QAccessible::uniqueId(rig.table()->cellAt(0, 12));
        for (int row = 1; row < 1'000; ++row)
            QVERIFY(rig.table()->cellAt(row, 12));
        QCOMPARE(a11y->cellsCreated(), std::size_t{1'000});
        QCOMPARE(a11y->cachedCellCount(), LineGridAccessible::kCellCacheCapacity);
        QVERIFY(!QAccessible::accessibleInterface(first));
    }

    void announcesTheCellFirstAndCountsOnlyWhenTheyChange()
    {
        Rig rig(100);
        rig.grid->forceActiveFocus();
        QVERIFY(rig.grid->hasActiveFocus());
        captured.clear();
        QAccessible::installUpdateHandler(captureUpdates);
        rig.lines.setSelection({core::LineId{5}, {core::LineId{5}}}, core::LineId{5});
        rig.lines.setSelection({core::LineId{6}, {core::LineId{5}}}, core::LineId{5});
        QAccessible::installUpdateHandler(nullptr);
        QList<Captured> focus, announce;
        for (const auto &c : captured) {
            if (c.type == QAccessible::Focus)
                focus.append(c);
            if (c.type == QAccessible::Announcement)
                announce.append(c);
        }
        QCOMPARE(focus.size(), 2);
        QCOMPARE(focus[0].text, QStringLiteral("line 5"));
        QCOMPARE(focus[1].text, QStringLiteral("line 6"));
        QCOMPARE(announce.size(), 1);
        QCOMPARE(announce[0].text, QStringLiteral("1 selected"));
        int firstFocus = -1, firstAnnounce = -1;
        for (int i = 0; i < captured.size(); ++i) {
            if (captured[i].type == QAccessible::Focus && firstFocus < 0)
                firstFocus = i;
            if (captured[i].type == QAccessible::Announcement && firstAnnounce < 0)
                firstAnnounce = i;
        }
        QVERIFY(firstFocus < firstAnnounce);
    }

    void keyboardNavigationRequestsActiveLines()
    {
        Rig rig(100);
        rig.grid->forceActiveFocus();
        QSignalSpy requested(rig.grid, &LineGrid::activeLineRequested);
        QTest::keyClick(&rig.window, Qt::Key_Down);
        QTest::keyClick(&rig.window, Qt::Key_Down);
        QTest::keyClick(&rig.window, Qt::Key_End);
        QCOMPARE(requested.count(), 3);
        QCOMPARE(requested[0][0].toULongLong(), 1ULL);
        QCOMPARE(requested[1][0].toULongLong(), 2ULL);
        QCOMPARE(requested[2][0].toULongLong(), 100ULL);
        QCOMPARE(rig.grid->currentRow(), 99);
        QVERIFY(rig.grid->contentY() > 0);
    }

    void hiddenCurrentLineMovesToTheNearestShownRow()
    {
        Rig rig(100);
        rig.lines.setSelection({core::LineId{12}, {core::LineId{12}}}, core::LineId{12});
        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(rig.lines.selection().active->value, 13ULL);
        QVERIFY(rig.lines.selection().selected.contains(core::LineId{12}));
        QCOMPARE(rig.grid->hiddenSelectedCount(), 1);
    }
};

QTEST_MAIN(LineGridA11yTest)
#include "line_grid_a11y_tests.moc"
