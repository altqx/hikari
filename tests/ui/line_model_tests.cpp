// V1-M: read-only Qt projection over core Lines.

#include "line_table_model.h"
#include "hikari/core/ass_load.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>

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

    void legacyColumnsAndMeasures()
    {
        LineTableModel model;
        model.setDocument(load("[Events]\n"
                               "Dialogue: 3,0:0:1.5,0:00:03.50,Sign,Ann,1,2,3,fx,Hello\\Nworld\n"
                               "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a note\n"
                               "Dialogue: 0,0:00:05.00,0:00:05.50,Default,,0,0,0,,much too fast here\n"));
        QCOMPARE(model.headerData(LineTableModel::LayerColumn, Qt::Horizontal).toString(), QStringLiteral("L."));
        // An abbreviated heading's hover text is its full name; a word has none.
        QCOMPARE(model.headerData(LineTableModel::LayerColumn, Qt::Horizontal, Qt::ToolTipRole).toString(),
                 QStringLiteral("Layer"));
        QCOMPARE(model.headerData(LineTableModel::MarginVerticalColumn, Qt::Horizontal, Qt::ToolTipRole).toString(),
                 QStringLiteral("Vertical margin"));
        QCOMPARE(model.headerData(LineTableModel::CpsColumn, Qt::Horizontal, Qt::ToolTipRole).toString(),
                 QStringLiteral("Characters per second"));
        QCOMPARE(model.headerData(LineTableModel::StartColumn, Qt::Horizontal, Qt::ToolTipRole).toString(), QString());
        QCOMPARE(model.headerData(LineTableModel::NumberColumn, Qt::Horizontal, Qt::ToolTipRole).toString(), QString());
        QCOMPARE(model.index(0, LineTableModel::LayerColumn).data().toString(), QStringLiteral("3"));
        // Times are shown from their values, as legacy SubsTime::raw does: the
        // positional parse reads "0:0:1.5" as 0 ms, so legacy shows 0:00:00.00.
        QCOMPARE(model.index(0, LineTableModel::StartColumn).data().toString(), QStringLiteral("0:00:00.00"));
        QCOMPARE(model.index(0, LineTableModel::MarginVerticalColumn).data().toString(), QStringLiteral("3"));
        QCOMPARE(model.index(0, LineTableModel::EffectColumn).data().toString(), QStringLiteral("fx"));
        QCOMPARE(model.index(0, LineTableModel::WrapsColumn).data().toString(), QStringLiteral("5/5"));
        QCOMPARE(model.index(0, LineTableModel::CpsColumn).data().toString(), QStringLiteral("2")); // 10 in 3.5 s
        QVERIFY(!model.index(0, 0).data(LineTableModel::CpsTooHighRole).toBool());
        // Comments get no measures.
        QCOMPARE(model.index(1, LineTableModel::CpsColumn).data().toString(), QString());
        QCOMPARE(model.index(1, LineTableModel::WrapsColumn).data().toString(), QString());
        // 15 letters in half a second.
        QCOMPARE(model.index(2, LineTableModel::CpsColumn).data().toString(), QStringLiteral("30"));
        QVERIFY(model.index(2, 0).data(LineTableModel::CpsTooHighRole).toBool());
    }

    void formatsAndHidingChooseTheColumns()
    {
        LineTableModel model;
        const std::string_view srt = "1\n00:00:01,000 --> 00:00:02,500\nhi\n";
        std::vector<std::byte> bytes(srt.size());
        std::memcpy(bytes.data(), srt.data(), srt.size());
        model.setDocument(core::loadSrt(bytes).document);
        auto shown = [&](int c) { return model.headerData(c, Qt::Horizontal, LineTableModel::ColumnShownRole).toBool(); };
        QVERIFY(!shown(LineTableModel::LayerColumn));
        QVERIFY(!shown(LineTableModel::StyleColumn));
        QVERIFY(shown(LineTableModel::EndColumn));
        QVERIFY(shown(LineTableModel::CpsColumn));
        QCOMPARE(model.index(0, LineTableModel::StartColumn).data().toString(), QStringLiteral("00:00:01,000"));
        // TMPlayer has no End and no CPS.
        const std::string_view tmp = "0:00:01:hi\n0:00:03:there\n";
        bytes.assign(tmp.size(), std::byte{});
        std::memcpy(bytes.data(), tmp.data(), tmp.size());
        model.setDocument(core::loadLineFormats(bytes).document);
        QVERIFY(!shown(LineTableModel::EndColumn));
        QVERIFY(!shown(LineTableModel::CpsColumn));
        QVERIFY(shown(LineTableModel::WrapsColumn));
        // GRID_HIDE_COLUMNS bits.
        model.setDocument(load(kLines));
        QSignalSpy header(&model, &QAbstractItemModel::headerDataChanged);
        model.setHiddenColumns(LineTableModel::hideBit(LineTableModel::ActorColumn) |
                               LineTableModel::hideBit(LineTableModel::WrapsColumn));
        QCOMPARE(header.size(), 1);
        QVERIFY(!shown(LineTableModel::ActorColumn));
        QVERIFY(!shown(LineTableModel::WrapsColumn));
        QVERIFY(shown(LineTableModel::StyleColumn));
        QVERIFY(shown(LineTableModel::NumberColumn)); // # and Text are always shown
        QVERIFY(shown(LineTableModel::TextColumn));
    }

    // GR3 (Aegisub's convention): with auto-hide on, Layer, Actor, Effect
    // and each margin hide when the whole Document leaves them empty
    // (comments included), and come back as soon as one Line fills them.
    // Manual hides apply on top; the other columns never hide themselves.
    void emptyColumnsHideThemselves()
    {
        using M = LineTableModel;
        LineTableModel model;
        auto shown = [&](int c) { return model.headerData(c, Qt::Horizontal, M::ColumnShownRole).toBool(); };
        const int all = M::hideBit(M::LayerColumn) | M::hideBit(M::ActorColumn) | M::hideBit(M::MarginLeftColumn) |
                        M::hideBit(M::MarginRightColumn) | M::hideBit(M::MarginVerticalColumn) |
                        M::hideBit(M::EffectColumn);
        constexpr std::string_view empty = "[Events]\n"
                                           "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                                           "Comment: 0,0:00:02.00,0:00:03.00,Sign,  ,0000,0,0,,two\n";
        model.setDocument(load(empty));
        QVERIFY(!model.autoHideColumns()); // off until the application sets it
        QCOMPARE(model.emptyColumns(), 0);
        for (int c = 0; c < M::TranslationColumn; ++c)
            QVERIFY(shown(c));
        QSignalSpy header(&model, &QAbstractItemModel::headerDataChanged);
        model.setAutoHideColumns(true);
        QCOMPARE(header.size(), 1);
        QCOMPARE(model.emptyColumns(), all);
        for (const int c : {M::LayerColumn, M::ActorColumn, M::MarginLeftColumn, M::MarginRightColumn,
                            M::MarginVerticalColumn, M::EffectColumn})
            QVERIFY2(!shown(c), qPrintable(model.headerData(c, Qt::Horizontal).toString()));
        for (const int c : {M::NumberColumn, M::StartColumn, M::EndColumn, M::StyleColumn, M::CpsColumn,
                            M::WrapsColumn, M::TextColumn})
            QVERIFY2(shown(c), qPrintable(model.headerData(c, Qt::Horizontal).toString()));

        // Each column comes back on its own, a Comment's value included.
        const std::pair<std::string_view, M::Column> filled[] = {
            {"Comment: 2,0:00:02.00,0:00:03.00,Sign,,0,0,0,,two\n", M::LayerColumn},
            {"Comment: 0,0:00:02.00,0:00:03.00,Sign,Ann,0,0,0,,two\n", M::ActorColumn},
            {"Comment: 0,0:00:02.00,0:00:03.00,Sign,,10,0,0,,two\n", M::MarginLeftColumn},
            {"Comment: 0,0:00:02.00,0:00:03.00,Sign,,0,10,0,,two\n", M::MarginRightColumn},
            {"Comment: 0,0:00:02.00,0:00:03.00,Sign,,0,0,10,,two\n", M::MarginVerticalColumn},
            {"Comment: 0,0:00:02.00,0:00:03.00,Sign,,0,0,0,fx,two\n", M::EffectColumn},
        };
        for (const auto &[line, column] : filled) {
            model.setDocument(load(std::string("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n") +
                                   std::string(line)));
            QCOMPARE(model.emptyColumns(), all & ~M::hideBit(column));
            QVERIFY(shown(column));
        }

        // Manual hides stay on top: a filled column hidden by hand stays
        // hidden, and turning auto-hide off brings back only the empty ones.
        model.setDocument(load(std::string("[Events]\n") + std::string(filled[0].first)));
        model.setHiddenColumns(M::hideBit(M::LayerColumn) | M::hideBit(M::StyleColumn));
        QVERIFY(!shown(M::LayerColumn));
        QVERIFY(!shown(M::StyleColumn));
        QVERIFY(!shown(M::ActorColumn));
        header.clear();
        model.setAutoHideColumns(false);
        QCOMPARE(header.size(), 1);
        QVERIFY(!shown(M::LayerColumn));
        QVERIFY(!shown(M::StyleColumn));
        QVERIFY(shown(M::ActorColumn));
        model.setHiddenColumns(0);

        // GR2: a draft that fills an empty column shows it until it is
        // reverted; the committed Lines decide again after it.
        model.setAutoHideColumns(true);
        model.setDocument(load(empty));
        core::LineRecord draft = *model.recordAt(0);
        draft.actor = u8"Ann";
        header.clear();
        model.setDraft(draft);
        QCOMPARE(header.size(), 1);
        QVERIFY(shown(M::ActorColumn));
        QCOMPARE(model.index(0, M::ActorColumn).data().toString(), QStringLiteral("Ann"));
        draft.text = u8"typing on";
        header.clear();
        model.setDraft(draft);
        QCOMPARE(header.size(), 0); // nothing new to show
        model.setDraft(std::nullopt);
        QVERIFY(!shown(M::ActorColumn));
    }

    // E5: the translation column (legacy SubsGridWindow.cpp:329-330 and
    // 370-416). Without the original shown, a translated Line shows its
    // translation in "Text"; with it, "Original text" and "Translation".
    void translationColumnFollowsShowOriginal()
    {
        const auto document = load("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,Gate\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                                   "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,Wall\n"
                                   "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,\n");
        LineTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDocument(document);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.columnShown(LineTableModel::TranslationColumn));
        QCOMPARE(model.headerData(LineTableModel::TextColumn, Qt::Horizontal).toString(), QStringLiteral("Text"));
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data().toString(), QStringLiteral("Brama"));
        QCOMPARE(model.index(1, LineTableModel::TextColumn).data().toString(), QStringLiteral("Wall"));

        model.setShowOriginal(true);
        model.setDocument(document);
        QVERIFY(model.columnShown(LineTableModel::TranslationColumn));
        QCOMPARE(model.headerData(LineTableModel::TextColumn, Qt::Horizontal).toString(), QStringLiteral("Original text"));
        QCOMPARE(model.headerData(LineTableModel::TranslationColumn, Qt::Horizontal).toString(),
                 QStringLiteral("Translation"));
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data().toString(), QStringLiteral("Gate"));
        QCOMPARE(model.index(0, LineTableModel::TranslationColumn).data().toString(), QStringLiteral("Brama"));
        QCOMPARE(model.index(1, LineTableModel::TranslationColumn).data().toString(), QString());
        // Hide columns never hides the text columns.
        model.setHiddenColumns(0xFFFF);
        QVERIFY(model.columnShown(LineTableModel::TextColumn));
        QVERIFY(model.columnShown(LineTableModel::TranslationColumn));

        // Outside translation mode the Text column is the text, even with a
        // translation left in the record (isTl needs hasTLMode).
        model.setShowOriginal(false);
        model.setHiddenColumns(0);
        auto off = document;
        off.removeScriptInfo(u8"TLMode");
        model.setDocument(off);
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data().toString(), QStringLiteral("Gate"));
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

    // R1: a Document row's comparison state and its differing characters
    // come from the table by row (legacy Comparison->at(key),
    // SubsGridWindow.cpp:419-420); rows past the table, or with no table,
    // have none. The colours are header data, the default theme's at first.
    void comparisonRolesFollowTheTable()
    {
        LineTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        std::vector<application::LineComparison> table(2);
        table[0].differences = false;                    // a match
        table[1].marks = {1, 0, 1, 4, 4};                // a mismatch with two runs
        model.setDocument(load(kLines), &table);
        QCOMPARE(model.index(0, 0).data(LineTableModel::ComparisonRole).toInt(), 1);
        QCOMPARE(model.index(1, 0).data(LineTableModel::ComparisonRole).toInt(), 2);
        QCOMPARE(model.index(1, 0).data(LineTableModel::ComparisonMarksRole).toList(), (QVariantList{0, 1, 4, 4}));
        QCOMPARE(model.index(2, 0).data(LineTableModel::ComparisonRole).toInt(), 0); // past the table
        QVERIFY(model.index(0, 0).data(LineTableModel::ComparisonMarksRole).toList().isEmpty());
        // A text contained in the other is still a mismatch (the leading 1).
        table[0] = {};
        table[0].marks = {1};
        model.setDocument(load(kLines), &table);
        QCOMPARE(model.index(0, 0).data(LineTableModel::ComparisonRole).toInt(), 2);
        QVERIFY(model.index(0, 0).data(LineTableModel::ComparisonMarksRole).toList().isEmpty());
        // An unpaired Line keeps its usual colour.
        table[0] = {};
        model.setDocument(load(kLines), &table);
        QCOMPARE(model.index(0, 0).data(LineTableModel::ComparisonRole).toInt(), 0);
        model.setDocument(load(kLines));
        QCOMPARE(model.index(1, 0).data(LineTableModel::ComparisonRole).toInt(), 0);

        const auto colours = model.headerData(0, Qt::Horizontal, LineTableModel::ComparisonColoursRole).toList();
        QCOMPARE(colours, (QVariantList{QColor(0x27, 0x00, 0xFF), QColor(0x27, 0x2B, 0x32), QColor(0x3A, 0x3E, 0x45),
                                        QColor(0x00, 0x31, 0x76), QColor(0x36, 0x62, 0xA1)}));
        QSignalSpy header(&model, &QAbstractItemModel::headerDataChanged);
        model.setComparisonColours({QColor(1, 2, 3), QColor(4, 5, 6), QColor(7, 8, 9), QColor(10, 11, 12),
                                    QColor(13, 14, 15)});
        QCOMPARE(header.count(), 1);
        QCOMPARE(model.headerData(0, Qt::Horizontal, LineTableModel::ComparisonColoursRole).toList().value(4),
                 QVariant(QColor(13, 14, 15)));
    }

    // E6: GLOBAL_HIDE_TAGS. Legacy SubsGrid::TagsPattern (SubsGridWindow.cpp:
    // 34-39) replaces each "{...}" (SRT: "<...>") with GRID_TAGS_SWAP_CHARACTER
    // through wxRegEx::ReplaceAll; "[^{]*" is greedy, so a block runs to the
    // last "}" before the next "{".
    void hiddenTagsAreSwappedInTheTextColumn()
    {
        LineTableModel model;
        model.setDocument(load("[Events]\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}Hello{\\i0} there\n"
                               "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{a}b}c{d\n"
                               "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,<i>kept</i>\n"));
        const auto text = [&](int row) { return model.index(row, LineTableModel::TextColumn).data().toString(); };
        QCOMPARE(text(0), QStringLiteral("{\\i1}Hello{\\i0} there"));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.setHideTags(true, QStringLiteral("\u2600"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(text(0), QStringLiteral("\u2600Hello\u2600 there"));
        QCOMPARE(text(1), QStringLiteral("\u2600c{d")); // greedy to "b}", an unclosed "{d" stays
        QCOMPARE(text(2), QStringLiteral("<i>kept</i>")); // ASS: only braces
        // wxRegEx's replacement: "&" is the whole match.
        model.setHideTags(true, QStringLiteral("[&]"));
        QCOMPARE(text(0), QStringLiteral("[{\\i1}]Hello[{\\i0}] there"));
        model.setHideTags(false, QStringLiteral("\u2600"));
        QCOMPARE(text(0), QStringLiteral("{\\i1}Hello{\\i0} there"));
        // SRT: "<...>".
        const char *srt = "1\n00:00:01,000 --> 00:00:02,000\n<i>Hi</i> {x}\n\n";
        std::vector<std::byte> bytes(std::strlen(srt));
        std::memcpy(bytes.data(), srt, bytes.size());
        model.setDocument(core::loadSrt(bytes).document);
        model.setHideTags(true, QStringLiteral("*"));
        QCOMPARE(text(0), QStringLiteral("*Hi* {x}"));
        // Translation mode: the Text column is the original, which legacy's
        // Original column shows with its tags (SubsGridWindow.cpp:405-410).
        model.setDocument(load("[Script Info]\nTLMode: Yes\n[Events]\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\b1}x\n"));
        QCOMPARE(text(0), QStringLiteral("{\\b1}x"));
    }

    // E6: spelling marks of the swapped text (TextData::Init's tagReplaceLen).
    void hiddenTagsGiveTheSpellingTheSwapLength()
    {
        LineTableModel model;
        int seenLength = -2;
        model.setSpelling(LineTableModel::TagSpelling(
            [&](std::u16string_view, core::SubtitleFormat, bool, int replaceTagsLen) {
                seenLength = replaceTagsLen;
                return core::legacy::SpellMarks{{1, 2}, {}};
            }));
        model.setDocument(load("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}ab\n"));
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data(LineTableModel::SpellMarksRole).toList().size(), 2);
        QCOMPARE(seenLength, -1);
        model.setHideTags(true, QStringLiteral("ab"));
        model.index(0, LineTableModel::TextColumn).data(LineTableModel::SpellMarksRole);
        QCOMPARE(seenLength, 2);
    }

    // E6: the label's State (Dialogue::GetState): the changed-Line mark from
    // the session, 4 Unconfirmed, 8 bookmarked; the label slot by
    // SubsGridWindow.cpp:478-479 and legacy's theme colours (config.cpp:422-425).
    void lineStateAndLabelColours()
    {
        LineTableModel model;
        model.setChangeState([](const core::LineRecord &l) { return l.id.value == 1 ? 1 : l.id.value == 2 ? 2 : 0; });
        auto document = load("[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,b\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,[bookmark],0,0,0,,c\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,d\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e\n");
        // Unconfirmed (legacy State 4, read from a translation pair's "\f"
        // "D" effect in TLMode) set directly.
        QVERIFY(document.setLineUnconfirmed(document.lines()[3]->id, true));
        model.setDocument(document);
        QList<int> states;
        for (int row = 0; row < model.rowCount(); ++row)
            states << model.index(row, 0).data(LineTableModel::LineStateRole).toInt();
        QCOMPARE(states, (QList<int>{1, 2, 8, 4, 0}));
        QCOMPARE(LineTableModel::labelSlot(0), 0);
        QCOMPARE(LineTableModel::labelSlot(1), 1);
        QCOMPARE(LineTableModel::labelSlot(2), 2);
        for (const int doubtful : {4, 5, 6, 8, 9, 12})
            QCOMPARE(LineTableModel::labelSlot(doubtful), 3);
        const auto colours = model.headerData(0, Qt::Horizontal, LineTableModel::LabelColoursRole).toList();
        QCOMPARE(colours.size(), 4);
        QCOMPARE(colours[0].value<QColor>(), QColor(0x2F, 0x31, 0x36));
        QCOMPARE(colours[1].value<QColor>(), QColor(0x32, 0x2F, 0x4E));
        QCOMPARE(colours[2].value<QColor>(), QColor(0x20, 0x22, 0x25));
        QCOMPARE(colours[3].value<QColor>(), QColor(0x92, 0x5B, 0x1F));
        QCOMPARE(LineTableModel::themeLabelColours(false)[1], QColor(0xB0, 0xAD, 0xD8));
    }

    // GR2: the Line editor's draft shows in its row, live, until it is
    // committed or reverted. Only the shown cells follow it: the record, the
    // Grid filter and the changed-Line state stay the committed Line's.
    void draftShowsInItsRowUncommitted()
    {
        LineTableModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        LineFilterModel filter;
        filter.setLineModel(&model);
        // Shows the Lines whose committed text is not "zulu".
        filter.setPredicate([](const core::LineRecord &l) { return l.text != u8"zulu"; });
        model.setDocument(load(kLines));
        const auto text = [&](int row) { return model.index(row, LineTableModel::TextColumn).data().toString(); };
        const auto draftAt = [&](int row) { return model.index(row, 0).data(LineTableModel::DraftRole).toBool(); };
        core::LineRecord draft = *model.recordAt(2);
        draft.text = u8"zulu, longer and faster";
        draft.end.value = core::DocumentTime(5'500'000);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.setDraft(draft);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 2);
        QCOMPARE(text(2), QStringLiteral("zulu, longer and faster"));
        QCOMPARE(model.index(2, LineTableModel::EndColumn).data().toString(), QStringLiteral("0:00:05.50"));
        QVERIFY(model.index(2, 0).data(LineTableModel::CpsTooHighRole).toBool()); // measured from the draft
        QVERIFY(draftAt(2));
        QVERIFY(!draftAt(0) && !draftAt(1));
        QCOMPARE(model.recordAt(2)->text, std::u8string(u8"bravo")); // committed
        QCOMPARE(filter.rowCount(), 3);                              // the filter reads the committed text
        QCOMPARE(model.index(2, 0).data(LineTableModel::LineStateRole).toInt(), 0);
        QCOMPARE(text(0), QStringLiteral("charlie"));

        // Another Line's draft moves the row; none restores it.
        draft = *model.recordAt(0);
        draft.text = u8"charlie!";
        changed.clear();
        model.setDraft(draft);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(text(2), QStringLiteral("bravo"));
        QVERIFY(!draftAt(2));
        QCOMPARE(text(0), QStringLiteral("charlie!"));
        model.setDraft(std::nullopt);
        QCOMPARE(text(0), QStringLiteral("charlie"));
        QVERIFY(!draftAt(0));

        // A new snapshot drops it; a draft of a Line not in the Document shows nothing.
        model.setDraft(draft);
        model.setDocument(load(kLines));
        QCOMPARE(text(0), QStringLiteral("charlie"));
        QVERIFY(!draftAt(0));
        draft.id = core::LineId{99};
        model.setDraft(draft);
        for (int row = 0; row < model.rowCount(); ++row)
            QVERIFY(!draftAt(row));
    }

    // GR2 with R1: the comparison and its marks are the committed Line's. A
    // draft that leaves the compared text, times and style keeps them; one
    // that changes any of them shows none until it is committed.
    void draftDropsAStaleComparison()
    {
        LineTableModel model;
        std::vector<application::LineComparison> table(2);
        table[0].differences = false; // a match
        table[1].marks = {1, 0, 1};   // a mismatch
        model.setDocument(load(kLines), &table);
        const auto comparison = [&](int row) { return model.index(row, 0).data(LineTableModel::ComparisonRole).toInt(); };
        const auto marks = [&](int row) {
            return model.index(row, 0).data(LineTableModel::ComparisonMarksRole).toList();
        };

        core::LineRecord draft = *model.recordAt(1);
        draft.actor = u8"Someone"; // not compared
        model.setDraft(draft);
        QCOMPARE(comparison(1), 2);
        QCOMPARE(marks(1), (QVariantList{0, 1}));

        draft.text += u8"!";
        model.setDraft(draft);
        QCOMPARE(comparison(1), 0);
        QVERIFY(marks(1).isEmpty());
        QCOMPARE(comparison(0), 1); // the other rows keep theirs

        for (const auto change : {+[](core::LineRecord &l) { l.start.value = core::DocumentTime(1); },
                                  +[](core::LineRecord &l) { l.end.value = core::DocumentTime(9'000'000); },
                                  +[](core::LineRecord &l) { l.style = u8"Other"; }}) {
            draft = *model.recordAt(0);
            change(draft);
            model.setDraft(draft);
            QCOMPARE(comparison(0), 0);
        }
        // Reverted, the committed comparison is back.
        model.setDraft(std::nullopt);
        QCOMPARE(comparison(0), 1);
        QCOMPARE(comparison(1), 2);
        QCOMPARE(marks(1), (QVariantList{0, 1}));
        draft = *model.recordAt(0);
        draft.translation = u8"other"; // compared only in translation mode
        model.setDraft(draft);
        QCOMPARE(comparison(0), 1);

        // Translation mode compares a translated Line's translation, and an
        // untranslated one's text (comparedText).
        model.setDocument(load("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,Gate\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                               "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,Wall\n"
                               "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,\n"),
                          &table);
        QVERIFY(!model.recordAt(0)->translation.empty());
        QVERIFY(model.recordAt(1)->translation.empty());
        draft = *model.recordAt(0);
        draft.text += u8"!"; // the original of a translated Line
        model.setDraft(draft);
        QCOMPARE(comparison(0), 1);
        draft.translation += u8"!";
        model.setDraft(draft);
        QCOMPARE(comparison(0), 0);
        draft = *model.recordAt(1);
        draft.text += u8"!"; // an untranslated Line's text
        model.setDraft(draft);
        QCOMPARE(comparison(1), 0);
    }

    // GR2: the drafted row's cached shown text follows hidden tags as the
    // others do.
    void draftFollowsHiddenTags()
    {
        LineTableModel model;
        model.setDocument(load(kLines));
        core::LineRecord draft = *model.recordAt(0);
        draft.text = u8"{\\b1}charlie";
        model.setDraft(draft);
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data().toString(), QStringLiteral("{\\b1}charlie"));
        model.setHideTags(true, QStringLiteral("*"));
        QCOMPARE(model.index(0, LineTableModel::TextColumn).data().toString(), QStringLiteral("*charlie"));
    }

    // KP1 (#286): a folded run names its first eight Styles, then "…", so a
    // run of many distinct Styles costs one lookup per Line.
    void aLongRunNamesItsFirstStyles()
    {
        std::string text = "[Events]\n";
        for (int i = 0; i < 20; ++i)
            text += "Dialogue: 0,0:00:01.00,0:00:02.00,S" + std::to_string(i) + ",,0,0,0,fx,x\n";
        LineTableModel model;
        model.setDocument(load(text));
        model.setFoldGenerated(true);
        QCOMPARE(model.index(0, LineTableModel::StyleColumn).data().toString(),
                 QStringLiteral("S0, S1, S2, S3, S4, S5, S6, S7, …"));
        QCOMPARE(model.index(0, LineTableModel::EffectColumn).data().toString(), QStringLiteral("fx"));
    }

};

QTEST_MAIN(LineModelTest)
#include "line_model_tests.moc"
