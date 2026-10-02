// V1-M: read-only Qt projection over core Lines.

#include "line_table_model.h"
#include "hikari/core/ass_load.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>

#include <cstring>
#include <string_view>

using namespace hikari;
using namespace hikari::ui;

namespace {

core::Document load(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return core::loadAss(bytes).document;
}

constexpr std::string_view kLines = "[Events]\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,charlie\n"
                                    "Comment: 0,0:00:01.00,0:00:02.00,Default,Note,0,0,0,,alpha\n"
                                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,bravo\n";

qulonglong idAt(const QAbstractItemModel &m, int row)
{
    return m.index(row, 0).data(LineTableModel::LineIdRole).toULongLong();
}

} // namespace

class LineModelTest : public QObject {
    Q_OBJECT
private slots:
    void passesTheModelTester()
    {
        LineTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        LineFilterModel filter;
        filter.setLineModel(&model);
        QAbstractItemModelTester filterTester(&filter, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDocument(load(kLines));
        filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        model.setSelection({core::LineId{1}, {core::LineId{1}, core::LineId{2}}}, core::LineId{1});
        model.setDocument(load(kLines));
        filter.setPredicate({});
    }

    void rowsFollowDocumentOrderWithStableIds()
    {
        LineTableModel model;
        model.setDocument(load(kLines));
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.columnCount(), int(LineTableModel::ColumnCount));
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data().toString(), QStringLiteral("charlie"));
        QCOMPARE(model.index(1, LineTableModel::StartColumn).data().toString(), QStringLiteral("0:00:01.00"));
        QCOMPARE(model.index(1, 0).data(LineTableModel::CommentRole).toBool(), true);
        QCOMPARE(model.index(2, 0).data(LineTableModel::StartMicrosecondsRole).toLongLong(), 5'000'000LL);
        QCOMPARE(idAt(model, 0), 1ULL);
        QCOMPARE(model.rowOf(core::LineId{3}).value(), 2);
        QVERIFY(!model.rowOf(core::LineId{42}));
    }

    void identitySurvivesFilteringAndSorting()
    {
        LineTableModel model;
        model.setDocument(load(kLines));
        LineFilterModel filter;
        filter.setLineModel(&model);
        filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(filter.rowCount(), 2);
        QCOMPARE(idAt(filter, 0), 1ULL);
        QCOMPARE(idAt(filter, 1), 3ULL); // the Comment (id 2) is filtered out, ids unchanged
        // A sort proxy over the filter keeps identities too.
        QSortFilterProxyModel byStart;
        byStart.setSourceModel(&filter);
        byStart.setSortRole(LineTableModel::StartMicrosecondsRole);
        byStart.sort(0);
        QCOMPARE(idAt(byStart, 0), 1ULL);
        filter.setPredicate({});
        QCOMPARE(byStart.rowCount(), 3);
        QCOMPARE(idAt(byStart, 0), 2ULL); // alpha starts first
        QCOMPARE(idAt(model, 0), 1ULL);   // the source still follows document order
    }

    void selectionIsStateNotContent()
    {
        LineTableModel model;
        model.setDocument(load(kLines));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        model.setSelection({core::LineId{3}, {core::LineId{2}, core::LineId{3}}}, core::LineId{2});
        QVERIFY(changed.count() >= 2);
        QCOMPARE(reset.count(), 0); // selection never resets the model
        QCOMPARE(model.index(2, 0).data(LineTableModel::ActiveRole).toBool(), true);
        QCOMPARE(model.index(1, 0).data(LineTableModel::SelectedRole).toBool(), true);
        QCOMPARE(model.index(1, 0).data(LineTableModel::AnchorRole).toBool(), true);
        QCOMPARE(model.index(0, 0).data(LineTableModel::SelectedRole).toBool(), false);
    }

    void hiddenSelectionIsKeptAndCounted()
    {
        LineTableModel model;
        model.setDocument(load(kLines));
        LineFilterModel filter;
        filter.setLineModel(&model);
        model.setSelection({core::LineId{1}, {core::LineId{1}, core::LineId{2}}}, std::nullopt);
        filter.setPredicate([](const core::LineRecord &l) { return !l.comment; });
        QCOMPARE(filter.hiddenSelectedCount(), 1);                    // the Comment is selected but hidden
        QCOMPARE(model.selection().selected.size(), std::size_t{2}); // still selected
        filter.setPredicate({});
        QCOMPARE(filter.hiddenSelectedCount(), 0);
    }

    void newSnapshotDropsSelectionOfRemovedLines()
    {
        LineTableModel model;
        model.setDocument(load(kLines));
        model.setSelection({core::LineId{3}, {core::LineId{3}}}, core::LineId{3});
        // A Document with only two Lines: id 3 no longer exists.
        model.setDocument(load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,D,,0,0,0,,a\n"
                               "Dialogue: 0,0:00:02.00,0:00:03.00,D,,0,0,0,,b\n"));
        QVERIFY(!model.selection().active);
        QVERIFY(model.selection().selected.empty());
    }
};

QTEST_MAIN(LineModelTest)
#include "line_model_tests.moc"
