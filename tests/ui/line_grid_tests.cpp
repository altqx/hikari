// V1-G: painted Grid viewport geometry and visible-row painting.

#include "line_grid.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "theme.h"
#include "hikari/core/ass_load.h"

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QScopeGuard>
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
        std::snprintf(buf, sizeof buf, "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line %d\n", i);
        s += buf;
    }
    std::vector<std::byte> bytes(s.size());
    std::memcpy(bytes.data(), s.data(), s.size());
    return core::loadAss(bytes).document;
}

int paintOnce(LineGrid &grid)
{
    QImage image(grid.size().toSize(), QImage::Format_ARGB32);
    QPainter painter(&image);
    grid.paint(&painter);
    return grid.lastPaintedRowCount();
}

} // namespace

class LineGridTest : public QObject {
    Q_OBJECT
private slots:
    void geometryMapsScrollToRows()
    {
        const GridGeometry g{20, 20};
        QCOMPARE(g.firstVisibleRow(0), 0);
        QCOMPARE(g.firstVisibleRow(39.9), 1);
        QCOMPARE(g.visibleRowCount(0, 100, 1000), 4);  // 80 px of body: rows 0-3
        QCOMPARE(g.visibleRowCount(10, 100, 1000), 5); // partial rows at both edges
        QCOMPARE(g.visibleRowCount(0, 100, 2), 2);     // fewer rows than fit
        QCOMPARE(g.visibleRowCount(0, 15, 1000), 0);   // header only
        QCOMPARE(g.visibleRowCount(5000, 100, 10), 0); // scrolled past the end
        QCOMPARE(g.rowAt(10, 0, 1000), -1);            // header
        QCOMPARE(g.rowAt(25, 0, 1000), 0);
        QCOMPARE(g.rowAt(25, 40, 1000), 2);
        QCOMPARE(g.rowAt(99, 0, 3), -1); // below the last of three rows
        QCOMPARE(g.contentHeight(1000), 20020.0);
    }

    void paintsOnlyVisibleRowsOfALargeModel()
    {
        LineTableModel model;
        model.setDocument(generate(50'000));
        LineGrid grid;
        grid.setSize(QSizeF(800, 400));
        grid.setModel(&model);
        const int expected = grid.geometry().visibleRowCount(0, 400, 50'000);
        QVERIFY(expected > 0 && expected < 40);
        QCOMPARE(paintOnce(grid), expected);
        grid.setContentY(250'000);
        QCOMPARE(paintOnce(grid), grid.geometry().visibleRowCount(grid.contentY(), 400, 50'000));
        QVERIFY(grid.rowAt(grid.geometry().headerHeight + 1) >= 0);
    }

    void scrollingClampsToContent()
    {
        LineTableModel model;
        model.setDocument(generate(100));
        LineGrid grid;
        grid.setSize(QSizeF(400, 300));
        grid.setModel(&model);
        grid.setContentY(-50);
        QCOMPARE(grid.contentY(), 0.0);
        grid.setContentY(1e9);
        QCOMPARE(grid.contentY(), grid.contentHeight() - 300);
        // The last row is fully reachable.
        const int last = grid.rowAt(299);
        QCOMPARE(last, 99);
    }

    void savesAReviewFrame()
    {
        // A small frame with selection, the active Line, a Comment and mixed
        // scripts, kept as a test artifact for human review (not a reference).
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.50,Default,Alice,0,0,0,,{\\i1}Hello{\\i0} there\n"
                             "Comment: 0,0:00:02.50,0:00:04.00,Default,,0,0,0,,a comment line\n"
                             "Dialogue: 0,0:00:04.00,0:00:06.00,Sign,,0,0,0,,\u6e2f \u2014 \u0645\u064a\u0646\u0627\u0621\n"
                             "Dialogue: 0,0:00:06.00,0:00:07.00,Default,Bob,0,0,0,,selected line\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setDocument(core::loadAss(bytes).document);
        model.setSelection({core::LineId{4}, {core::LineId{4}, core::LineId{1}}}, core::LineId{1});
        LineGrid grid;
        grid.setSize(QSizeF(720, 160));
        grid.setModel(&model);
        QImage image(720, 160, QImage::Format_ARGB32);
        QPainter painter(&image);
        grid.paint(&painter);
        painter.end();
        QCOMPARE(grid.lastPaintedRowCount(), 4);
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        QVERIFY(image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/grid-review-frame.png")));
    }

    // R1: legacy SubsGridWindow.cpp:419-429: a compared row is painted in
    // the mismatch or match colour (the comment ones on a Comment), and a
    // selected one with GRID_SELECTION blended over it (GetColorWithAlpha).
    void comparisonBackgrounds()
    {
        const QVariantList colours{QColor(0x27, 0x00, 0xFF), QColor(0x27, 0x2B, 0x32), QColor(0x3A, 0x3E, 0x45),
                                   QColor(0x00, 0x31, 0x76), QColor(0x36, 0x62, 0xA1)};
        // legacy's dark GRID_SELECTION (config.cpp:422)
        const QColor legacySelection(0x87, 0x91, 0xFD, 75);
        QVERIFY(!comparisonBackground(0, false, false, colours, legacySelection));
        QCOMPARE(*comparisonBackground(2, false, false, colours, legacySelection), QColor(0x27, 0x2B, 0x32));
        QCOMPARE(*comparisonBackground(1, false, false, colours, legacySelection), QColor(0x3A, 0x3E, 0x45));
        QCOMPARE(*comparisonBackground(2, true, false, colours, legacySelection), QColor(0x00, 0x31, 0x76));
        QCOMPARE(*comparisonBackground(1, true, false, colours, legacySelection), QColor(0x36, 0x62, 0xA1));
        // #8791FD at alpha 75 over #272B32, in legacy's integer arithmetic:
        // r = 0x27 * 180 / 255 + (0x87 - 180 * 0x87 / 255) = 27 + 40 = 67.
        QCOMPARE(*comparisonBackground(2, false, true, colours, legacySelection), QColor(67, 73, 110));
        // K2: GRID_SELECTION is a selection-type mark, the theme's accent at
        // legacy's alpha 75 (Dark's green #9CDBC9): the same arithmetic.
        // r = 27 + (0x9C - 180 * 0x9C / 255) = 27 + 46 = 73.
        QCOMPARE(*comparisonBackground(2, false, true, colours, QColor(0x9C, 0xDB, 0xC9, 75)), QColor(73, 95, 95));
    }

    // R1: the painted rows: backgrounds per state, and the differing
    // characters outlined in GRID_COMPARISON_OUTLINE (SubsGridWindow.cpp:
    // 508-528); a row without a table is painted as before.
    void paintsComparisonColours()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,same\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,WWWW differs\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,alone\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        std::vector<application::LineComparison> table(3);
        table[0].differences = false;
        table[0].matchedRow = 0;
        table[1].marks = {1, 0, 3};
        table[1].matchedRow = 1;
        LineTableModel model;
        model.setDocument(core::loadAss(bytes).document, &table);
        model.setComparisonColours({QColor(0, 255, 0), QColor(200, 0, 0), QColor(0, 0, 200), QColor(1, 1, 1),
                                    QColor(2, 2, 2)});
        LineGrid grid;
        grid.setSize(QSizeF(720, 120));
        grid.setModel(&model);
        QImage image(720, 120, QImage::Format_ARGB32);
        QPainter painter(&image);
        grid.paint(&painter);
        painter.end();
        const double rh = grid.rowHeight();
        const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
        // A cell of the row away from any text (the margin column's right edge).
        const QRectF text1 = grid.cellRect(1, grid.columnCount() - 1);
        // The comparison colour from the second column on; the number column
        // keeps the row's own background, as legacy paints column 0 in its
        // label colour and only the others in kol (SubsGridWindow.cpp:495).
        const QRectF number0 = grid.cellRect(0, 0);
        QCOMPARE(grid.columnTitle(0), model.headerData(LineTableModel::NumberColumn, Qt::Horizontal).toString());
        const int afterNumber = int(number0.right()) + 2;
        QCOMPARE(image.pixelColor(afterNumber, rowY(0)), QColor(0, 0, 200));
        QCOMPARE(image.pixelColor(afterNumber, rowY(1)), QColor(200, 0, 0));
        QVERIFY(image.pixelColor(afterNumber, rowY(2)) != QColor(200, 0, 0) &&
                image.pixelColor(afterNumber, rowY(2)) != QColor(0, 0, 200));
        for (int row = 0; row < 3; ++row) {
            const QColor number = image.pixelColor(2, rowY(row));
            QVERIFY2(number != QColor(200, 0, 0) && number != QColor(0, 0, 200), qPrintable(number.name()));
        }
        // The outline colour around "WWWW" in the Text cell of row 1, and none
        // in row 0's equal text.
        int outline1 = 0, outline0 = 0;
        const QRectF text0 = grid.cellRect(0, grid.columnCount() - 1);
        for (int x = int(text1.left()); x < int(text1.left()) + 60; ++x)
            for (int y = int(text1.top()); y < int(text1.bottom()); ++y)
                outline1 += image.pixelColor(x, y) == QColor(0, 255, 0);
        for (int x = int(text0.left()); x < int(text0.right()); ++x)
            for (int y = int(text0.top()); y < int(text0.bottom()); ++y)
                outline0 += image.pixelColor(x, y) == QColor(0, 255, 0);
        QVERIFY2(outline1 > 10, qPrintable(QString::number(outline1)));
        QCOMPARE(outline0, 0);
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        QVERIFY(image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/grid-comparison-frame.png")));
    }

    // K2: the Grid paints with the theme layer's roles in every theme: the
    // header on the raised surface, rows on the panel with every other one
    // in the theme's alternate shade, a selected row on the selected
    // background with the 3-wide leading accent marker (visual-language.md,
    // "Selected row marker"), the active Line outlined in the accent, the
    // empty area below the rows on the panel. (The repaint on a theme change
    // is hikari_ui_shell_tests themeSwitchRetintsEverySurface's.)
    void paintsTheThemeRoles()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,plain\n"
                             "Comment: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,a comment\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,selected\n"
                             "Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,active\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setDocument(core::loadAss(bytes).document);
        model.setSelection({core::LineId{4}, {core::LineId{3}}}, core::LineId{3});
        LineGrid grid;
        grid.setSize(QSizeF(720, 160));
        grid.setModel(&model);
        SettingsStore store;
        auto restore = qScopeGuard([] { theme::useSettings(nullptr); });
        theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        const double rh = grid.rowHeight();
        const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
        // Between the Actor and Left columns' text (the Actor cells are empty).
        QCOMPARE(grid.columnTitle(6), QStringLiteral("Left"));
        const int right = int(grid.cellRect(0, 6).left()) - 6;
        for (const auto code : theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), theme::codeName(code));
            const auto &roles = theme::current().roles;
            const auto &content = theme::current().content;
            QImage image(720, 160, QImage::Format_ARGB32);
            QPainter painter(&image);
            grid.paint(&painter);
            painter.end();
            QCOMPARE(grid.lastPaintedRowCount(), 4);
            const auto expect = [&](int x, int y, const QColor &colour, const char *what) {
                QVERIFY2(image.pixelColor(x, y) == colour,
                         qPrintable(QStringLiteral("%1 %2: %3, expected %4")
                                        .arg(theme::codeName(code), QLatin1String(what), image.pixelColor(x, y).name(),
                                             colour.name())));
            };
            expect(right, int(grid.geometry().headerHeight / 2), roles.raised, "header");
            expect(right, rowY(0), roles.panel, "row");
            expect(right, rowY(1), content.gridAlternate, "alternate row");
            if (!theme::isHighContrast(code)) // high contrast does not shade every other row
                QVERIFY(content.gridAlternate != roles.panel);
            expect(right, rowY(2), roles.select, "selected row");
            expect(1, rowY(2), roles.accent, "selected marker");
            expect(4, rowY(2), roles.select, "past the 3-wide marker");
            expect(0, rowY(3), roles.accent, "active outline (left)");
            expect(719, rowY(3), roles.accent, "active outline (right)");
            expect(right, rowY(3), content.gridAlternate, "active row");
            expect(right, 155, roles.panel, "below the rows");
        }
    }

    // K2 focus (visual-language.md, "Keyboard focus"): while the Grid has
    // keyboard focus its focused cell, the current Line's row, carries the
    // focus ring, 2 wide in the text colour just inside the current Line's
    // accent outline; a selected current row keeps its 3-wide accent marker
    // over the ring's left side. Without focus no ring. Every theme; focus
    // stays apart from the selection's accent and background.
    void focusRingOnTheCurrentRow()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,plain\n"
                             "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,current\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,selected\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setDocument(core::loadAss(bytes).document);
        model.setSelection({core::LineId{2}, {core::LineId{3}}}, core::LineId{3});
        QQuickWindow window;
        window.resize(720, 160);
        LineGrid grid(window.contentItem());
        grid.setSize(QSizeF(720, 160));
        grid.setModel(&model);
        QQuickItem other(window.contentItem());
        other.setFlag(QQuickItem::ItemIsFocusScope, false);
        window.show();
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        SettingsStore store;
        auto restore = qScopeGuard([] { theme::useSettings(nullptr); });
        theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        const double rh = grid.rowHeight();
        const auto top = [&](int row) { return int(grid.geometry().headerHeight + row * rh); };
        const int middle = int(grid.cellRect(0, 6).left()) - 6; // between the Actor and Left columns' text
        for (const auto code : theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), theme::codeName(code));
            const auto &roles = theme::current().roles;
            const auto &content = theme::current().content;
            QVERIFY(roles.focus == roles.text && roles.focus != roles.accent && roles.focus != roles.select);
            const auto paint = [&] {
                QImage image(720, 160, QImage::Format_ARGB32);
                QPainter painter(&image);
                grid.paint(&painter);
                return image;
            };
            const auto expect = [&](const QImage &image, int x, int y, const QColor &colour, const char *what) {
                QVERIFY2(image.pixelColor(x, y) == colour,
                         qPrintable(QStringLiteral("%1 %2 at %3,%4: %5, expected %6")
                                        .arg(theme::codeName(code), QLatin1String(what)).arg(x).arg(y)
                                        .arg(image.pixelColor(x, y).name(), colour.name())));
            };
            const int mid = top(1) + int(rh / 2);
            // Unfocused: the current Line's accent outline, no ring.
            grid.setFocus(false);
            other.forceActiveFocus();
            QTRY_VERIFY(!grid.hasActiveFocus());
            QImage image = paint();
            expect(image, 0, mid, roles.accent, "current outline");
            expect(image, 1, mid, content.gridAlternate, "no ring");
            expect(image, middle, top(1) + 1, content.gridAlternate, "no ring (top)");
            // Focused: the ring inside the outline, on all four sides.
            grid.forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(grid.hasActiveFocus());
            image = paint();
            expect(image, 0, mid, roles.accent, "current outline (left)");
            expect(image, 1, mid, roles.focus, "ring (left)");
            expect(image, 2, mid, roles.focus, "ring (left, second)");
            expect(image, 3, mid, content.gridAlternate, "inside the ring");
            expect(image, 719, mid, roles.accent, "current outline (right)");
            expect(image, 718, mid, roles.focus, "ring (right)");
            expect(image, 717, mid, roles.focus, "ring (right, second)");
            expect(image, middle, top(1), roles.accent, "current outline (top)");
            expect(image, middle, top(1) + 1, roles.focus, "ring (top)");
            expect(image, middle, top(1) + 2, roles.focus, "ring (top, second)");
            expect(image, middle, top(2) - 2, roles.focus, "ring (bottom)");
            expect(image, middle, top(2) - 1, roles.accent, "current outline (bottom)");
            // only on the current row
            expect(image, 1, top(0) + int(rh / 2), roles.panel, "another row");
            expect(image, 1, top(2) + int(rh / 2), roles.accent, "a selected row's marker");
            expect(image, 4, top(2) + int(rh / 2), roles.select, "a selected row");
            // A selected current row: the marker over the ring's left side.
            model.setSelection({core::LineId{3}, {core::LineId{3}}}, core::LineId{3});
            image = paint();
            const int selectedMid = top(2) + int(rh / 2);
            expect(image, 1, selectedMid, roles.accent, "marker over the ring");
            expect(image, 3, selectedMid, roles.select, "past the marker");
            expect(image, middle, top(2) + 1, roles.focus, "ring (top) on a selected row");
            expect(image, 718, selectedMid, roles.focus, "ring (right) on a selected row");
            expect(image, 1, mid, content.gridAlternate, "the ring left with the current Line");
            model.setSelection({core::LineId{2}, {core::LineId{3}}}, core::LineId{3});
            if (QTest::currentTestFailed())
                return;
        }
    }

    void followsModelChanges()
    {
        LineTableModel model;
        model.setDocument(generate(10));
        LineGrid grid;
        grid.setSize(QSizeF(400, 1000));
        grid.setModel(&model);
        QCOMPARE(paintOnce(grid), 10);
        model.setDocument(generate(3)); // modelReset
        QCOMPARE(grid.contentHeight(), grid.geometry().contentHeight(3));
        QCOMPARE(paintOnce(grid), 3);
        grid.setModel(nullptr);
        QCOMPARE(paintOnce(grid), 0);
    }
};

QTEST_MAIN(LineGridTest)
#include "line_grid_tests.moc"
