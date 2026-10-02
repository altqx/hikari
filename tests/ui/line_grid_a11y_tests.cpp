// V1-A: accessibility of the painted Grid (automated tree checks; human
// NVDA/Orca observation is V1-A-Q).

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

core::Document generate(int lines)
{
    std::string s = "[Events]\n";
    char buf[128];
    for (int i = 0; i < lines; ++i) {
        std::snprintf(buf, sizeof buf, "%s: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line %d\n",
                      i % 10 == 1 ? "Comment" : "Dialogue", i + 1);
        s += buf;
    }
    std::vector<std::byte> bytes(s.size());
    std::memcpy(bytes.data(), s.data(), s.size());
    return core::loadAss(bytes).document;
}

struct Captured {
    QAccessible::Event type;
    QString text;
};
QList<Captured> captured;

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

    explicit Rig(int count)
    {
        lines.setDocument(generate(count));
        filter.setLineModel(&lines);
        window.resize(800, 400);
        grid = new LineGrid(window.contentItem());
        grid->setSize(QSizeF(800, 400));
        grid->setModel(&filter);
        QObject::connect(grid, &LineGrid::activeLineRequested, grid, [this](qulonglong id) {
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

} // namespace

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
        QCOMPARE(rig.table()->columnCount(), 6);
        QCOMPARE(rig.table()->columnDescription(5), QStringLiteral("Text"));
        QAccessibleInterface *cell = rig.table()->cellAt(49'999, 5);
        QVERIFY(cell);
        QCOMPARE(cell->role(), QAccessible::Cell);
        QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("line 50000"));
    }

    void cellsKeepLineIdentityThroughFiltering()
    {
        Rig rig(100);
        QAccessibleInterface *cell = rig.table()->cellAt(20, 5); // Line 21
        QCOMPARE(cell, rig.table()->cellAt(20, 5));               // same object on repeat
        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        // Comments 2 and 12 are now hidden: Line 21 moved up two rows, same cell.
        QCOMPARE(cell->tableCellInterface()->rowIndex(), 18);
        QCOMPARE(cell, rig.table()->cellAt(18, 5));
        QCOMPARE(cell->text(QAccessible::Name), QStringLiteral("line 21"));
    }

    void selectionIncludesOnlyShownRowsAndCountsHidden()
    {
        Rig rig(100);
        rig.lines.setSelection({core::LineId{3}, {core::LineId{2}, core::LineId{3}}}, core::LineId{2});
        QCOMPARE(rig.table()->selectedRows(), (QList<int>{1, 2}));
        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(rig.table()->selectedRows(), (QList<int>{1})); // Line 2 is a hidden Comment
        QCOMPARE(rig.grid->selectedCount(), 2);
        QCOMPARE(rig.grid->hiddenSelectedCount(), 1);
    }

    void cellCacheIsBounded()
    {
        Rig rig(1'000);
        auto *a11y = static_cast<LineGridAccessible *>(QAccessible::queryAccessibleInterface(rig.grid));
        const QAccessible::Id first = QAccessible::uniqueId(rig.table()->cellAt(0, 5));
        for (int row = 1; row < 1'000; ++row)
            QVERIFY(rig.table()->cellAt(row, 5));
        QCOMPARE(a11y->cellsCreated(), std::size_t{1'000});
        QCOMPARE(a11y->cachedCellCount(), LineGridAccessible::kCellCacheCapacity);
        QVERIFY(!QAccessible::accessibleInterface(first)); // evicted and released
    }

    void announcesTheCellFirstAndCountsOnlyWhenTheyChange()
    {
        Rig rig(100);
        rig.grid->forceActiveFocus();
        QVERIFY(rig.grid->hasActiveFocus());
        captured.clear();
        QAccessible::installUpdateHandler(captureUpdates);
        rig.lines.setSelection({core::LineId{5}, {core::LineId{5}}}, core::LineId{5});
        rig.lines.setSelection({core::LineId{6}, {core::LineId{5}}}, core::LineId{5}); // move, same count
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
        QCOMPARE(announce.size(), 1); // only the first change altered the count
        QCOMPARE(announce[0].text, QStringLiteral("1 selected"));
        // Focus came before the count announcement.
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
        QTest::keyClick(&rig.window, Qt::Key_Down); // no current Line yet: first row
        QTest::keyClick(&rig.window, Qt::Key_Down);
        QTest::keyClick(&rig.window, Qt::Key_End);
        QCOMPARE(requested.count(), 3);
        QCOMPARE(requested[0][0].toULongLong(), 1ULL);
        QCOMPARE(requested[1][0].toULongLong(), 2ULL);
        QCOMPARE(requested[2][0].toULongLong(), 100ULL);
        QCOMPARE(rig.grid->currentRow(), 99);
        QVERIFY(rig.grid->contentY() > 0); // scrolled to keep the current row visible
    }

    void hiddenCurrentLineMovesToTheNearestShownRow()
    {
        Rig rig(100);
        rig.lines.setSelection({core::LineId{12}, {core::LineId{12}}}, core::LineId{12});
        rig.filter.setPredicate([](const core::LineRecord &l) { return !l.comment; }); // hides Line 12
        QCOMPARE(rig.lines.selection().active->value, 13ULL);   // following neighbour on a tie
        QVERIFY(rig.lines.selection().selected.contains(core::LineId{12})); // still selected
        QCOMPARE(rig.grid->hiddenSelectedCount(), 1);
    }
};

QTEST_MAIN(LineGridA11yTest)
#include "line_grid_a11y_tests.moc"
