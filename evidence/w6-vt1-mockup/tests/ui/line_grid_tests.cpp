// V1-G: painted Grid viewport geometry and visible-row painting.

#include "line_grid.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "theme.h"
#include "hikari/core/ass_load.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>
#include <QWheelEvent>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
        // The outline is drawn glyphs: measure and paint in a font every
        // platform has (offscreen on Windows reads Qt's own font directory,
        // which holds none, and draws every character as a box).
        const int font = QFontDatabase::addApplicationFont(QStringLiteral(HIKARI_TEST_FONT));
        QVERIFY(font >= 0);
        const QFont before = QGuiApplication::font();
        auto restore = qScopeGuard([&] { QGuiApplication::setFont(before); });
        QFont testFont(QFontDatabase::applicationFontFamilies(font).value(0));
        testFont.setPixelSize(20);
        QGuiApplication::setFont(testFont);
        // Wide enough for every fixed column and the Text column after them.
        LineGrid grid;
        grid.setSize(QSizeF(1280, 200));
        grid.setModel(&model);
        QImage image(1280, 200, QImage::Format_ARGB32);
        QPainter painter(&image);
        painter.setFont(testFont);
        grid.paint(&painter);
        painter.end();
        const double rh = grid.rowHeight();
        const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
        // A cell of the row away from any text (the margin column's right edge).
        const QRectF text1 = grid.cellRect(1, grid.columnCount() - 1);
        // The comparison colour from the second column on; the number column
        // takes its label colour (E6), as legacy paints column 0 in its
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
            expect(right, rowY(1), content.gridComment, "comment row"); // GR4
            if (!theme::isHighContrast(code)) // high contrast does not shade every other row
                QVERIFY(content.gridAlternate != roles.panel);
            expect(right, rowY(2), roles.select, "selected row");
            expect(1, rowY(2), roles.accent, "selected marker");
            // E6: the number cell, at the row's left edge, is in its label colour.
            const QColor label = LineTableModel::themeLabelColours(theme::isDark(code))[0];
            expect(4, rowY(2), label, "past the 3-wide marker");
            expect(0, rowY(3), roles.accent, "active outline (left)");
            expect(719, rowY(3), roles.accent, "active outline (right)");
            expect(right, rowY(3), content.gridAlternate, "active row");
            expect(right, 155, roles.panel, "below the rows");
        }
    }

    // GR4: a Comment row is painted on the theme's comment background
    // (legacy GRID_COMMENT), whatever its parity, in secondary text that
    // reads on it; selected, the selection blends over it (GetColorWithAlpha),
    // with the accent marker. Every theme; screenshots for the card.
    void paintsCommentRows()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,dialogue\n"
                             "Comment: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,a comment\n"
                             "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,another comment\n"
                             "Comment: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,a selected comment\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,selected dialogue\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setDocument(core::loadAss(bytes).document);
        model.setSelection({core::LineId{4}, {core::LineId{4}, core::LineId{5}}}, core::LineId{4});
        LineGrid grid;
        grid.setSize(QSizeF(720, 160));
        grid.setModel(&model);
        SettingsStore store;
        auto restore = qScopeGuard([] { theme::useSettings(nullptr); });
        theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        const double rh = grid.rowHeight();
        const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
        const int right = int(grid.cellRect(0, 6).left()) - 6; // between the Actor and Left columns' text
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        for (const auto code : theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), theme::codeName(code));
            const auto &roles = theme::current().roles;
            const auto &content = theme::current().content;
            const QString name = theme::codeName(code);
            const QColor selectedComment = selectionOver(content.gridComment, content.comparisonSelection);
            // The comment colour stands apart from the rows around it and
            // from the selection, and both text roles read on it. Selected,
            // the Comment's ink (secondary, or the primary text where the
            // blend leaves secondary short) reads at 4.5:1 with every accent.
            for (const QColor &other : {roles.panel, content.gridAlternate, roles.select})
                QVERIFY2(content.gridComment != other, qPrintable(name));
            for (const QColor &ink : {roles.text, roles.muted})
                QVERIFY2(theme::contrastRatio(ink, content.gridComment) >= 4.5,
                         qPrintable(name + QLatin1Char(' ') + ink.name() + QLatin1Char(' ') + content.gridComment.name()));
            QCOMPARE(commentInk(roles, content.gridComment), roles.muted);
            const auto presets = theme::isHighContrast(code) ? std::vector<QColor>{roles.accent} : [&] {
                std::vector<QColor> out;
                for (const auto &a : theme::accents(theme::isDark(code)))
                    out.push_back(a.accent);
                return out;
            }();
            for (QColor accent : presets) {
                accent.setAlpha(content.comparisonSelection.alpha());
                const QColor blended = selectionOver(content.gridComment, accent);
                QVERIFY2(theme::contrastRatio(commentInk(roles, blended), blended) >= 4.5,
                         qPrintable(name + QLatin1Char(' ') + accent.name() + QLatin1Char(' ') + blended.name()));
            }
            QImage image(720, 160, QImage::Format_ARGB32);
            QPainter painter(&image);
            grid.paint(&painter);
            painter.end();
            const auto expect = [&](int x, int y, const QColor &colour, const char *what) {
                QVERIFY2(image.pixelColor(x, y) == colour,
                         qPrintable(QStringLiteral("%1 %2: %3, expected %4")
                                        .arg(name, QLatin1String(what), image.pixelColor(x, y).name(), colour.name())));
            };
            expect(right, rowY(0), roles.panel, "dialogue");
            expect(right, rowY(1), content.gridComment, "comment (odd row)");
            expect(right, rowY(2), content.gridComment, "comment (even row)");
            expect(right, rowY(3), selectedComment, "selected comment");
            QCOMPARE(commentInk(roles, selectedComment) == roles.muted,
                     theme::contrastRatio(roles.muted, selectedComment) >= 4.5);
            expect(1, rowY(3), roles.accent, "selected comment's marker");
            expect(right, rowY(4), roles.select, "selected dialogue");
            QVERIFY(image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/grid-comments-%1.png").arg(name)));
        }
    }

    // K2 focus (visual-language.md, "Keyboard focus"): while the Grid has
    // keyboard focus its focused cell, the current Line's row, carries the
    // focus ring, 1 wide in the text colour tight inside the current Line's
    // accent outline (the user's 2026-10-13 decision: "grid row should still
    // use the ring too", 1 wide like the controls'); a selected current row
    // keeps its 3-wide accent marker over the ring's left side. Without
    // focus no ring. Every theme; focus stays apart from the selection's
    // accent and background.
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
            // E6: the number cell, at the row's left edge, is in its label colour.
            const QColor label = LineTableModel::themeLabelColours(theme::isDark(code))[0];
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
            expect(image, 1, mid, label, "no ring");
            expect(image, middle, top(1) + 1, theme::current().content.gridAlternate, "no ring (top)");
            // Focused: the ring inside the outline, on all four sides.
            grid.forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(grid.hasActiveFocus());
            image = paint();
            expect(image, 0, mid, roles.accent, "current outline (left)");
            expect(image, 1, mid, roles.focus, "ring (left)");
            expect(image, 2, mid, label, "inside the ring (1 wide)");
            expect(image, 719, mid, roles.accent, "current outline (right)");
            expect(image, 718, mid, roles.focus, "ring (right)");
            QVERIFY2(image.pixelColor(717, mid) != roles.focus, "ring (right) 1 wide");
            expect(image, middle, top(1), roles.accent, "current outline (top)");
            expect(image, middle, top(1) + 1, roles.focus, "ring (top)");
            expect(image, middle, top(1) + 2, theme::current().content.gridAlternate, "inside the ring (top, 1 wide)");
            expect(image, middle, top(2) - 3, theme::current().content.gridAlternate, "inside the ring (bottom, 1 wide)");
            expect(image, middle, top(2) - 2, roles.focus, "ring (bottom)");
            expect(image, middle, top(2) - 1, roles.accent, "current outline (bottom)");
            // only on the current row
            expect(image, 1, top(0) + int(rh / 2), label, "another row");
            expect(image, 1, top(2) + int(rh / 2), roles.accent, "a selected row's marker");
            expect(image, 4, top(2) + int(rh / 2), label, "a selected row");
            expect(image, middle, top(2) + int(rh / 2), roles.select, "a selected row (its cells)");
            // A selected current row: the marker over the ring's left side.
            model.setSelection({core::LineId{3}, {core::LineId{3}}}, core::LineId{3});
            image = paint();
            const int selectedMid = top(2) + int(rh / 2);
            expect(image, 1, selectedMid, roles.accent, "marker over the ring");
            expect(image, 3, selectedMid, label, "past the marker");
            expect(image, middle, top(2) + 1, roles.focus, "ring (top) on a selected row");
            expect(image, middle, top(2) + 2, roles.select, "inside the ring on a selected row");
            expect(image, 718, selectedMid, roles.focus, "ring (right) on a selected row");
            expect(image, 1, mid, label, "the ring left with the current Line");
            model.setSelection({core::LineId{2}, {core::LineId{3}}}, core::LineId{3});
            if (QTest::currentTestFailed())
                return;
        }
    }

    // V6: a press and a double click report whether they are in the End
    // column (legacy SubsGrid::SetVideoLineTime seeks to the end there,
    // SubsGridWindow.cpp:1382-1395); a double click is reported as such.
    // GR1: a notch scrolls the system's lines per notch, whatever the
    // modifiers (legacy SubsGridWindow.cpp:1460-1466 ignores them, Ctrl
    // included); a touchpad's pixel deltas scroll by pixels; a sideways
    // delta is not the Grid's; the selection and active Line stay.
    void wheelScrollsTheGrid()
    {
        struct Grid : LineGrid {
            using LineGrid::wheelEvent;
        };
        LineTableModel model;
        model.setDocument(generate(200));
        Grid grid;
        grid.setSize(QSizeF(400, 300));
        grid.setModel(&model);
        const qreal row = grid.rowHeight();
        const qreal notch = QGuiApplication::styleHints()->wheelScrollLines() * row;
        QSignalSpy requests(&grid, &LineGrid::activeLineRequested);
        QSignalSpy clicks(&grid, &LineGrid::lineClicked);
        auto wheel = [&](QPoint pixels, QPoint angle, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            const QPointF at(200, 150);
            QWheelEvent event(at, at, pixels, angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
            event.setAccepted(false);
            grid.wheelEvent(&event);
            return event.isAccepted();
        };
        QVERIFY(wheel({}, {0, -120}));
        QCOMPARE(grid.contentY(), notch);
        QVERIFY(wheel({}, {0, -240}, Qt::ControlModifier));
        QCOMPARE(grid.contentY(), 3 * notch);
        QVERIFY(wheel({}, {0, -120}, Qt::ShiftModifier));
        QCOMPARE(grid.contentY(), 4 * notch);
        QVERIFY(wheel({}, {0, 120}));
        QCOMPARE(grid.contentY(), 3 * notch);
        // A high-resolution wheel's eighth of a notch moves an eighth.
        QVERIFY(wheel({}, {0, -15}));
        QCOMPARE(grid.contentY(), 3 * notch + notch / 8);
        // Pixel deltas win over the angle a touchpad also reports.
        const qreal before = grid.contentY();
        QVERIFY(wheel({0, -7}, {0, -56}));
        QCOMPARE(grid.contentY(), before + 7);
        // Sideways only: not handled, nothing moves.
        QVERIFY(!wheel({}, {-120, 0}));
        QVERIFY(!wheel({-30, 0}, {-240, 0}));
        QCOMPARE(grid.contentY(), before + 7);
        // Shift+wheel that the platform turned sideways scrolls as the wheel.
        QVERIFY(wheel({}, {-120, 0}, Qt::ShiftModifier));
        QCOMPARE(grid.contentY(), before + 7 + notch);
        QVERIFY(wheel({-5, 0}, {-40, 0}, Qt::ShiftModifier));
        QCOMPARE(grid.contentY(), before + 12 + notch);
        // Clamped at both ends.
        QVERIFY(wheel({}, {0, 120 * 1000}));
        QCOMPARE(grid.contentY(), 0.0);
        QVERIFY(wheel({}, {0, -120 * 1000}));
        QCOMPARE(grid.contentY(), grid.contentHeight() - 300);
        QCOMPARE(requests.size(), 0);
        QCOMPARE(clicks.size(), 0);
    }

    void pressesReportTheEndColumnAndDoubleClicks()
    {
        struct Grid : LineGrid {
            using LineGrid::mouseDoubleClickEvent;
            using LineGrid::mousePressEvent;
        };
        LineTableModel model;
        model.setDocument(generate(3));
        Grid grid;
        grid.setSize(QSizeF(800, 300));
        grid.setModel(&model);
        int end = -1, start = -1;
        for (int c = 0; c < grid.columnCount(); ++c) {
            if (grid.columnTitle(c) == QLatin1String("End"))
                end = c;
            if (grid.columnTitle(c) == QLatin1String("Start"))
                start = c;
        }
        QVERIFY(end >= 0 && start >= 0);
        QSignalSpy clicked(&grid, &LineGrid::lineClicked);
        auto send = [&](QEvent::Type type, QPointF at, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QMouseEvent event(type, at, at, at, Qt::LeftButton, Qt::LeftButton, mods);
            if (type == QEvent::MouseButtonDblClick)
                grid.mouseDoubleClickEvent(&event);
            else
                grid.mousePressEvent(&event);
        };
        send(QEvent::MouseButtonPress, grid.cellRect(1, end).center());
        send(QEvent::MouseButtonDblClick, grid.cellRect(1, end).center(), Qt::ControlModifier);
        send(QEvent::MouseButtonDblClick, grid.cellRect(2, start).center());
        QCOMPARE(clicked.size(), 3);
        QCOMPARE(clicked[0].at(0).toULongLong(), grid.lineAtRow(1)->value);
        QCOMPARE(clicked[0].at(2).toBool(), true);  // End column
        QCOMPARE(clicked[0].at(3).toBool(), false); // a press
        QCOMPARE(clicked[1].at(1).toInt(), int(Qt::ControlModifier));
        QCOMPARE(clicked[1].at(2).toBool(), true);
        QCOMPARE(clicked[1].at(3).toBool(), true); // a double click
        QCOMPARE(clicked[2].at(0).toULongLong(), grid.lineAtRow(2)->value);
        QCOMPARE(clicked[2].at(2).toBool(), false); // the Start column
        QCOMPARE(clicked[2].at(3).toBool(), true);
    }

    // The abbreviated headings name their column in full under the pointer
    // (legacy's names in Paste columns and Hide columns, SubsGrid.cpp:199-200
    // and 501-508); a heading that is a word, and the rows, have none.
    void abbreviatedHeadingsNameTheirColumnOnHover()
    {
        struct Grid : LineGrid {
            using LineGrid::hoverLeaveEvent;
            using LineGrid::hoverMoveEvent;
        };
        LineTableModel model;
        model.setDocument(generate(3));
        Grid grid;
        grid.setSize(QSizeF(1000, 300));
        grid.setModel(&model);
        QSignalSpy changed(&grid, &LineGrid::headerToolTipChanged);
        const auto hover = [&](QPointF at) {
            QHoverEvent event(QEvent::HoverMove, at, at, at);
            grid.hoverMoveEvent(&event);
            return grid.headerToolTip();
        };
        const qreal headerY = grid.rowHeight() / 2;
        QHash<QString, QString> tips;
        int vert = -1;
        for (int c = 0; c < grid.columnCount(); ++c) {
            tips.insert(grid.columnTitle(c), hover(QPointF(grid.cellRect(0, c).center().x(), headerY)));
            if (grid.columnTitle(c) == QLatin1String("Vert."))
                vert = c;
        }
        QCOMPARE(tips.value(QStringLiteral("L.")), QStringLiteral("Layer"));
        QCOMPARE(tips.value(QStringLiteral("Left")), QStringLiteral("Left margin"));
        QCOMPARE(tips.value(QStringLiteral("Right")), QStringLiteral("Right margin"));
        QCOMPARE(tips.value(QStringLiteral("Vert.")), QStringLiteral("Vertical margin"));
        QCOMPARE(tips.value(QStringLiteral("CPS")), QStringLiteral("Characters per second"));
        QCOMPARE(tips.value(QStringLiteral("Wraps")), QStringLiteral("Line wraps"));
        for (const char *word : {"#", "Start", "End", "Styles", "Actor", "Effect", "Text"}) {
            QVERIFY2(tips.contains(QLatin1String(word)), word);
            QVERIFY2(tips.value(QLatin1String(word)).isEmpty(), word);
        }
        // The tip sits under its heading's cell.
        QCOMPARE(hover(QPointF(grid.cellRect(0, vert).center().x(), headerY)), QStringLiteral("Vertical margin"));
        QCOMPARE(grid.headerToolTipCell().x(), grid.cellRect(0, vert).x());
        QCOMPARE(grid.headerToolTipCell().width(), grid.cellRect(0, vert).width());
        QCOMPARE(grid.headerToolTipCell().bottom(), grid.rowHeight());
        // Over a row, and after the pointer leaves: nothing.
        QCOMPARE(hover(grid.cellRect(1, vert).center()), QString());
        hover(QPointF(grid.cellRect(0, vert).center().x(), headerY));
        QHoverEvent leave(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF());
        grid.hoverLeaveEvent(&leave);
        QCOMPARE(grid.headerToolTip(), QString());
        QVERIFY(changed.size() >= 4);
        // A heading the model hides (GRID_HIDE_COLUMNS) takes its tip with it.
        hover(QPointF(grid.cellRect(0, vert).center().x(), headerY));
        QCOMPARE(grid.headerToolTip(), QStringLiteral("Vertical margin"));
        model.setHiddenColumns(LineTableModel::hideBit(LineTableModel::MarginVerticalColumn));
        QCOMPARE(grid.headerToolTip(), QString());
    }

    // E6: the number cell in its label colour by State (SubsGridWindow.cpp:
    // 478-479, 495), selected or not, with a dot for a changed Line and a
    // ring for a changed and saved one; the state named for assistive
    // technology.
    void paintsTheChangedLineMark()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,plain\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,changed\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,saved\n"
                             "Dialogue: 0,0:00:07.00,0:00:08.00,Default,[bookmark],0,0,0,,bookmarked\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setChangeState([](const core::LineRecord &l) { return l.id.value == 2 ? 1 : l.id.value == 3 ? 2 : 0; });
        model.setDocument(core::loadAss(bytes).document);
        model.setSelection({core::LineId{2}, {core::LineId{2}}}, core::LineId{2});
        LineGrid grid;
        grid.setSize(QSizeF(720, 140));
        grid.setModel(&model);
        QImage image(720, 140, QImage::Format_ARGB32);
        QPainter painter(&image);
        grid.paint(&painter);
        painter.end();
        const auto colours = LineTableModel::themeLabelColours(true);
        const double rh = grid.rowHeight();
        const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
        const QRectF number = grid.cellRect(0, 0);
        // A pixel of the cell left of the number's text, past a selected
        // row's 3-wide accent marker (K2).
        QCOMPARE(image.pixelColor(3, rowY(0)), colours[0]);
        QCOMPARE(image.pixelColor(3, rowY(1)), colours[1]); // selected: still its label colour
        QCOMPARE(image.pixelColor(3, rowY(2)), colours[2]);
        QCOMPARE(image.pixelColor(3, rowY(3)), colours[3]);
        // The mark's centre: filled for changed, the label colour inside a ring for saved.
        const int markX = int(number.right() - 6.5);
        QVERIFY(image.pixelColor(markX, rowY(1)) != colours[1]);
        QCOMPARE(image.pixelColor(markX, rowY(2)), colours[2]);
        int ring = 0;
        for (int x = markX - 3; x <= markX + 3; ++x)
            ring += image.pixelColor(x, rowY(2)) != colours[2];
        QVERIFY(ring > 0);
        QCOMPARE(image.pixelColor(markX, rowY(0)), colours[0]);
        QCOMPARE(grid.rowStateText(0), QString());
        QCOMPARE(grid.rowStateText(1), QStringLiteral("changed"));
        QCOMPARE(grid.rowStateText(2), QStringLiteral("changed, saved"));
        QCOMPARE(grid.rowStateText(3), QStringLiteral("bookmarked"));
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        QVERIFY(image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/grid-changed-lines.png")));
    }

    // GR2: a row showing the Line editor's uncommitted draft paints its text
    // live and a triangle in place of the changed-Line circle, on the
    // committed Line's label colour; reverting brings the circle back.
    void paintsTheDraftTriangle()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,plain\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,changed\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setChangeState([](const core::LineRecord &l) { return l.id.value == 2 ? 1 : 0; });
        model.setDocument(core::loadAss(bytes).document);
        LineGrid grid;
        grid.setSize(QSizeF(720, 100));
        grid.setModel(&model);
        const auto colours = LineTableModel::themeLabelColours(true);
        const double rh = grid.rowHeight();
        const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
        const int markX = int(grid.cellRect(0, 0).right() - 6.5);
        const auto render = [&] {
            QImage image(720, 100, QImage::Format_ARGB32);
            QPainter painter(&image);
            grid.paint(&painter);
            return image;
        };
        // The mark's ink around its centre: its box, and how tall it is at
        // the box's left and right columns. The triangle is 7 tall at its
        // left side and comes to a point; the 5-wide circle is symmetric.
        struct Shape {
            int height = 0, left = 0, right = 0;
        };
        const auto shape = [&](const QImage &image, int row, const QColor &label) {
            const auto ink = [&](int x, int y) {
                const QColor c = image.pixelColor(x, y);
                return std::abs(c.red() - label.red()) + std::abs(c.green() - label.green()) +
                           std::abs(c.blue() - label.blue()) > 90;
            };
            int x0 = 1000, x1 = -1, y0 = 1000, y1 = -1;
            for (int y = rowY(row) - 6; y <= rowY(row) + 6; ++y)
                for (int x = markX - 6; x <= markX + 6; ++x)
                    if (ink(x, y)) {
                        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
                    }
            Shape out;
            if (x1 < 0)
                return out;
            out.height = y1 - y0 + 1;
            for (int y = y0; y <= y1; ++y)
                out.left += ink(x0, y), out.right += ink(x1, y);
            return out;
        };
        const auto triangle = [&](const QImage &image, int row, const QColor &label) {
            const Shape s = shape(image, row, label);
            return s.height >= 6 && s.left >= s.right + 3;
        };
        const auto circle = [&](const QImage &image, int row, const QColor &label) {
            const Shape s = shape(image, row, label);
            return s.height >= 3 && s.height <= 6 && std::abs(s.left - s.right) <= 1;
        };
        QImage before = render();
        QVERIFY(circle(before, 1, colours[1]));
        QCOMPARE(before.pixelColor(markX, rowY(0)), colours[0]); // no mark

        auto first = *model.recordAt(0);
        first.text = u8"typed but not committed";
        model.setDraft(first);
        QCOMPARE(grid.cellText(0, grid.columnCount() - 1), QStringLiteral("typed but not committed"));
        QImage drafted = render();
        QVERIFY(triangle(drafted, 0, colours[0]));
        QCOMPARE(drafted.pixelColor(3, rowY(0)), colours[0]); // the committed Line's label
        QCOMPARE(grid.rowStateText(0), QStringLiteral("uncommitted"));
        QVERIFY(circle(drafted, 1, colours[1]));

        auto second = *model.recordAt(1);
        second.text = u8"changed again";
        model.setDraft(second);
        drafted = render();
        QVERIFY(triangle(drafted, 1, colours[1]));
        QCOMPARE(grid.rowStateText(1), QStringLiteral("changed, uncommitted"));
        QCOMPARE(drafted.pixelColor(markX, rowY(0)), colours[0]); // the first row is back to no mark
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        QVERIFY(drafted.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/grid-draft-line.png")));

        model.setDraft(std::nullopt);
        QVERIFY(circle(render(), 1, colours[1]));
        QCOMPARE(grid.rowStateText(1), QStringLiteral("changed"));
    }

    // GR2: typing in a 10k-Line Document repaints one row: a keystroke's
    // draft and the Grid's paint stay far below a frame.
    void draftKeystrokesStayCheapOnALargeDocument()
    {
        LineTableModel model;
        model.setDocument(generate(10000));
        LineGrid grid;
        grid.setSize(QSizeF(1200, 800));
        grid.setModel(&model);
        grid.setContentY(5000 * grid.rowHeight());
        auto draft = *model.recordAt(5005);
        QImage image(1200, 800, QImage::Format_ARGB32);
        QElapsedTimer timer;
        timer.start();
        constexpr int keystrokes = 200;
        for (int i = 0; i < keystrokes; ++i) {
            draft.text += u8"x";
            model.setDraft(draft);
            QPainter painter(&image);
            grid.paint(&painter);
        }
        const double perKeystroke = double(timer.elapsed()) / keystrokes;
        qInfo("GR2: %.2f ms per keystroke (draft and paint) on 10000 Lines", perKeystroke);
        QVERIFY2(perKeystroke < 16, qPrintable(QString::number(perKeystroke)));
        QCOMPARE(model.index(5005, LineTableModel::TextColumn).data().toString().size(), qsizetype(9 + keystrokes));
    }

    // E6 (E6-mark-shape): the dot and the ring take the theme layer's roles,
    // not a fixed colour: text, or field where text would not stand out from
    // the label colour, so the mark stays visible in every theme (K2).
    void changedLineMarkTakesThemeRoles()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,changed\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,saved\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setChangeState([](const core::LineRecord &l) { return l.id.value == 1 ? 1 : 2; });
        model.setDocument(core::loadAss(bytes).document);
        SettingsStore store;
        auto restore = qScopeGuard([] { theme::useSettings(nullptr); });
        theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        const auto distance = [](const QColor &a, const QColor &b) {
            return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
        };
        for (const auto code : theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), theme::codeName(code));
            const auto &roles = theme::current().roles;
            const auto colours = LineTableModel::themeLabelColours(theme::isDark(code));
            QCOMPARE(model.headerData(0, Qt::Horizontal, LineTableModel::LabelColoursRole).toList().value(2).value<QColor>(),
                     colours[2]); // the model follows the theme
            const auto markOn = [&](const QColor &label) {
                return theme::contrastRatio(roles.field, label) > theme::contrastRatio(roles.text, label) ? roles.field
                                                                                                         : roles.text;
            };
            LineGrid grid;
            grid.setSize(QSizeF(720, 100));
            grid.setModel(&model);
            QImage image(720, 100, QImage::Format_ARGB32);
            QPainter painter(&image);
            grid.paint(&painter);
            painter.end();
            const double rh = grid.rowHeight();
            const auto rowY = [&](int row) { return int(grid.geometry().headerHeight + row * rh + rh / 2); };
            const int markX = int(grid.cellRect(0, 0).right() - 6.5);
            const QString name = theme::codeName(code);
            // The dot's centre is the mark colour.
            QVERIFY2(distance(image.pixelColor(markX, rowY(0)), markOn(colours[1])) <= 6,
                     qPrintable(name + QLatin1Char(' ') + image.pixelColor(markX, rowY(0)).name()));
            // The ring: the label colour inside, the mark colour around it.
            QCOMPARE(image.pixelColor(markX, rowY(1)), colours[2]);
            int nearest = 1000;
            for (int x = markX - 3; x <= markX + 3; ++x)
                nearest = std::min(nearest, distance(image.pixelColor(x, rowY(1)), markOn(colours[2])));
            QVERIFY2(nearest < distance(colours[2], markOn(colours[2])) / 2, qPrintable(name + QLatin1Char(' ') + QString::number(nearest)));
        }
    }

    // UI polish: the number's digits sit on their label colour and read on
    // it in every theme, for every Line state and on a Comment: they take the
    // label's ink (text, or field where text contrasts less), at 4.5:1 or
    // more, and are painted in it.
    void numberDigitsReadOnTheirLabel()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,plain\n"
                             "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,changed\n"
                             "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,saved\n"
                             "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,doubtful\n"
                             "Comment: 0,0:00:09.00,0:00:10.00,Default,,0,0,0,,a comment\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setChangeState([](const core::LineRecord &l) {
            return l.id.value == 2 ? 1 : l.id.value == 3 ? 2 : l.id.value == 4 ? 4 : 0;
        });
        model.setDocument(core::loadAss(bytes).document);
        SettingsStore store;
        auto restore = qScopeGuard([] { theme::useSettings(nullptr); });
        theme::useSettings(&store);
        store.setValue(QStringLiteral("appearance.followSystem"), false);
        // Digits in a bold test font, whose stems are wider than a pixel: a
        // platform's thin default (Fedora's Noto Sans) is all antialiasing
        // and never reaches the ink itself.
        const int font = QFontDatabase::addApplicationFont(QStringLiteral(HIKARI_TEST_FONT));
        QVERIFY(font >= 0);
        const QFont before = QGuiApplication::font();
        auto restoreFont = qScopeGuard([&] { QGuiApplication::setFont(before); });
        QFont testFont(QFontDatabase::applicationFontFamilies(font).value(0));
        testFont.setPixelSize(16);
        testFont.setBold(true);
        QGuiApplication::setFont(testFont);
        const auto distance = [](const QColor &a, const QColor &b) {
            return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
        };
        for (const auto code : theme::kCodes) {
            store.setValue(QStringLiteral("appearance.theme"), theme::codeName(code));
            const auto &roles = theme::current().roles;
            const auto colours = LineTableModel::themeLabelColours(theme::isDark(code));
            LineGrid grid;
            grid.setSize(QSizeF(720, 240));
            grid.setModel(&model);
            QImage image(720, 240, QImage::Format_ARGB32);
            QPainter painter(&image);
            grid.paint(&painter);
            painter.end();
            const double rh = grid.rowHeight();
            const QRectF number = grid.cellRect(0, 0);
            const int labelOf[] = {0, 1, 2, 3, 0};
            for (int row = 0; row < 5; ++row) {
                const QColor label = colours[labelOf[row]];
                const QColor ink = theme::contrastRatio(roles.field, label) > theme::contrastRatio(roles.text, label)
                                       ? roles.field
                                       : roles.text;
                const QString what = theme::codeName(code) + QStringLiteral(" row %1").arg(row);
                QVERIFY2(theme::contrastRatio(ink, label) >= 4.5,
                         qPrintable(what + QStringLiteral(": %1 on %2").arg(ink.name(), label.name())));
                // The digit's nearest pixel to the ink, left of the mark.
                const int top = int(grid.geometry().headerHeight + row * rh);
                int nearest = 1000;
                for (int y = top + 2; y < int(top + rh) - 2; ++y)
                    for (int x = int(number.left()) + 4; x < int(number.right()) - 12; ++x)
                        nearest = std::min(nearest, distance(image.pixelColor(x, y), ink));
                QVERIFY2(nearest <= 24, qPrintable(what + QStringLiteral(": %1").arg(nearest)));
            }
        }
    }

    // E6: hidden tags as painted (the Text column's swapped text).
    void paintsHiddenTags()
    {
        const char *script = "[Events]\n"
                             "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8\\fs20}Top {\\i1}line\n";
        std::vector<std::byte> bytes(std::strlen(script));
        std::memcpy(bytes.data(), script, bytes.size());
        LineTableModel model;
        model.setDocument(core::loadAss(bytes).document);
        LineGrid grid;
        grid.setSize(QSizeF(720, 60));
        grid.setModel(&model);
        const int text = grid.columnCount() - 1;
        QCOMPARE(grid.cellText(0, text), QStringLiteral("{\\an8\\fs20}Top {\\i1}line"));
        model.setHideTags(true, QStringLiteral("\u2600"));
        QCOMPARE(grid.cellText(0, text), QStringLiteral("\u2600Top \u2600line"));
        QImage image(720, 60, QImage::Format_ARGB32);
        QPainter painter(&image);
        grid.paint(&painter);
        painter.end();
        QDir().mkpath(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR));
        QVERIFY(image.save(QStringLiteral(HIKARI_TEST_ARTIFACT_DIR "/grid-hidden-tags.png")));
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
