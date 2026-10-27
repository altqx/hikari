// KP1 (#286): the Karaoke preset (the owner-approved mockup A, 2026-10-26):
// Video and the Template editor, the Line editor the tab behind it, in the
// upper row, Audio across the window under them, a short Grid below, on a
// fresh profile and after Reset this layout. Choosing it turns the audio
// panel's Karaoke mode and the Grid's folding on; leaving it puts back what
// it changed, and only that. The Grid folds each run of generated Lines into
// one row: selected whole, unfolded by its arrow or →, folded by ←, and
// unfolded by Find landing in it.

#include "hikari/app/application.h"
#include "hikari/application/edit_session.h"

#include "audio_controller.h"
#include "docking.h"
#include "line_grid.h"
#include "line_table_model.h"
#include "settings_store.h"
#include "shell_controller.h"
#include "theme.h"
#include "workspace_layout.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <set>

using namespace hikari;

namespace {

// A song after a templater run: its templates, three karaoke Lines (the
// originals, commented), a translation, a run of five generated Lines, a
// karaoke Line added since, and a run of two (one Kara Effector's).
QString writeSong(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
            "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
            "MarginR, MarginV, Encoding\n"
            "Style: Romaji,Arial,40,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,0,0,0,100,100,0,0,1,2,1,8,10,10,10,1\n"
            "Style: English,Arial,36,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
            "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,template syl,{\\pos($scenter,$smiddle)}\n"   // 0
            "Comment: 0,0:00:01.00,0:00:02.00,Romaji,,0,0,0,karaoke,{\\k50}ki{\\k50}mi\n"                // 1
            "Comment: 0,0:00:02.00,0:00:03.00,Romaji,,0,0,0,karaoke,{\\k50}no{\\k50}na\n"                // 2
            "Comment: 0,0:00:03.00,0:00:04.00,Romaji,,0,0,0,karaoke,{\\k100}mae\n"                       // 3
            "Dialogue: 0,0:00:01.00,0:00:04.00,English,,0,0,0,,Your name\n"                              // 4
            "Dialogue: 0,0:00:00.80,0:00:01.50,Romaji,,0,0,0,fx,ki\n"                                    // 5
            "Dialogue: 0,0:00:01.30,0:00:02.00,Romaji,,0,0,0,fx,mi\n"                                    // 6
            "Dialogue: 0,0:00:01.80,0:00:02.50,Romaji,,0,0,0,fx,no\n"                                    // 7
            "Dialogue: 0,0:00:02.30,0:00:03.10,Romaji,,0,0,0,fx,na\n"                                    // 8
            "Dialogue: 0,0:00:02.80,0:00:04.20,English,,0,0,0,fx,mae marigold\n"                         // 9
            "Dialogue: 0,0:00:05.00,0:00:06.00,Romaji,,0,0,0,,{\\k50}ha{\\k50}ru\n"                      // 10
            "Dialogue: 0,0:00:05.00,0:00:05.50,Romaji,,0,0,0,fx,ha\n"                                    // 11
            "Dialogue: 0,0:00:05.50,0:00:06.00,Romaji,,0,0,0,Effector [fx],ru\n");                       // 12
    return path;
}

} // namespace

class KaraokePresetTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    void start()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("hikari.ini"));
        options.playbackAudio = false;
        application = std::make_unique<app::Application>(options);
        application->applySettings({{QStringLiteral("editor.spellchecker"), false}});
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
    }
    void stop()
    {
        application->endSession();
        engine.reset();
        application.reset();
    }
    QQuickWindow *window() const { return qobject_cast<QQuickWindow *>(engine->rootObjects().first()); }
    template <typename T = QQuickItem> T *item(const char *name) const
    {
        return window()->findChild<T *>(QLatin1String(name));
    }
    QObject *dock(const char *name) const { return item<QObject>(name); }
    bool isOpen(const char *name) const { return dock(name)->property("isOpen").toBool(); }
    bool isFloating(const char *name) const { return dock(name)->property("isFloating").toBool(); }
    void applyPreset(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(window(), "applyPreset", Q_ARG(QVariant, QLatin1String(name))));
    }
    void openSong() { QVERIFY(application->openFile(writeSong(dir.filePath(QStringLiteral("song.ass"))))); }
    application::EditSession *session() const
    {
        return application->files().session(*application->workspace().editingTarget());
    }
    core::LineId lineId(int row) const { return session()->document().lines()[std::size_t(row)]->id; }
    std::set<core::LineId> ids(std::initializer_list<int> rows) const
    {
        std::set<core::LineId> out;
        for (int row : rows)
            out.insert(lineId(row));
        return out;
    }
    std::set<core::LineId> selected() const { return session()->selection().selected; }
    std::optional<core::LineId> active() const { return session()->selection().active; }
    ui::AudioController &audio() const { return application->audio(); }
    ui::ShellController &shell() const { return application->shell(); }
    QAbstractItemModel *gridModel() const { return shell().lines(); }
    // The Grid's rows, each its Line's Document row (from 0).
    QList<int> gridRows() const
    {
        QList<int> out;
        for (int row = 0; row < gridModel()->rowCount(); ++row)
            out << gridModel()->index(row, 0).data(ui::LineTableModel::DocumentRowRole).toInt();
        return out;
    }
    int gridRowOf(int documentRow) const { return int(gridRows().indexOf(documentRow)); }
    QString cell(int documentRow, int column) const
    {
        return gridModel()->index(gridRowOf(documentRow), column).data().toString();
    }
    int fold(int documentRow) const
    {
        return gridModel()->index(gridRowOf(documentRow), 0).data(ui::LineTableModel::FoldRole).toInt();
    }
    QQuickItem *grid() const { return item("editingGrid"); }
    QRectF sceneRect(const char *panel) const
    {
        auto *it = item(panel);
        return QRectF(it->mapToScene(QPointF(0, 0)), QSizeF(it->width(), it->height()));
    }
    QStringList openPanels() const
    {
        QStringList out;
        for (const char *name : {"videoDock", "audioDock", "originalDock", "editorDock", "templateDock", "gridDock",
                                 "referenceDock", "timingDock", "searchDock", "automationDock", "stylesDock"})
            if (isOpen(name))
                out << QLatin1String(name);
        return out;
    }
    // The Karaoke arrangement: its panels, where they are and their sizes
    // (mockup A: the Template editor right of the video, the Line editor the
    // tab behind it, Audio the window's width under them, a short Grid).
    void verifyKaraokeArrangement()
    {
        QTRY_COMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "templateDock", "gridDock"}));
        for (const char *name : {"videoDock", "audioDock", "editorDock", "templateDock", "gridDock"})
            QVERIFY2(!isFloating(name), name);
        QTRY_VERIFY(item("templatePanel")->isVisible());
        QVERIFY(!item("editorPanel")->isVisible()); // the tab behind it
        const QRectF video = sceneRect("videoPanel");
        const QRectF templates = sceneRect("templatePanel");
        const QRectF audio = sceneRect("audioPanel");
        const QRectF grid = sceneRect("gridPanel");
        const auto where = [&] {
            return qPrintable(QStringLiteral("video %1 template %2 audio %3 grid %4")
                                  .arg(rectText(video), rectText(templates), rectText(audio), rectText(grid)));
        };
        // The Template editor on the video's right, the video the narrower.
        QVERIFY2(templates.left() > video.right() - 1, where());
        QVERIFY2(qAbs(templates.bottom() - video.bottom()) < 2, where());
        QVERIFY2(video.width() < templates.width() + 1, where());
        // Audio under both, as wide as the Grid below it.
        QVERIFY2(audio.top() > video.bottom() - 1 && audio.top() > templates.bottom() - 1, where());
        QVERIFY2(qAbs(audio.left() - grid.left()) < 2 && qAbs(audio.right() - grid.right()) < 2, where());
        QVERIFY2(audio.left() < video.left() + 2 && audio.right() > templates.right() - 2, where());
        QVERIFY2(grid.top() > audio.bottom() - 1, where());
        // The upper row the tallest; the Grid short: its heading and seven rows.
        const double rowHeight = this->grid()->property("rowHeight").toDouble();
        QVERIFY2(video.height() > audio.height() && video.height() > grid.height(), where());
        QVERIFY2(this->grid()->height() < rowHeight * 9 + 1, where());
        QVERIFY2(this->grid()->height() > rowHeight * 6, where());
    }
    static QString rectText(const QRectF &r)
    {
        return QStringLiteral("%1,%2 %3x%4").arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height());
    }
    void press(Qt::Key key)
    {
        grid()->forceActiveFocus();
        QTest::keyClick(window(), key);
    }

private slots:
    void cleanup()
    {
        if (application)
            application->endSession();
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layouts.json", "LastSession.txt", "hikari.ini"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

    // Karaoke is the fifth preset, and carries Karaoke mode.
    void karaokeIsAPresetWithAMode()
    {
        QCOMPARE(ui::WorkspaceLayoutController::presets(),
                 (QStringList{"Editing", "Timing", "Translation", "Typesetting", "Karaoke"}));
        QCOMPARE(ui::WorkspaceLayoutController::presetLabel(QStringLiteral("Karaoke")), QStringLiteral("Karaoke"));
        QCOMPARE(ui::WorkspaceLayoutController::presetMode(QStringLiteral("Karaoke")), QStringLiteral("Karaoke"));
        start();
        for (const char *prefix : {"viewLayout", "switcher"})
            QVERIFY2(window()->findChild<QObject *>(QLatin1String(prefix) + QStringLiteral("PresetKaraoke")), prefix);
        stop();
    }

    // Choosing Karaoke on a fresh profile gives its own arrangement; Reset
    // this layout and a restart give it back; Editing is untouched.
    void karaokeIsAnArrangementOfItsOwn()
    {
        start();
        QCOMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "gridDock"})); // Editing
        const QRectF editingAudio = sceneRect("audioPanel");
        applyPreset("Karaoke");
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Karaoke"));
        verifyKaraokeArrangement();
        // Changed by hand, then reset.
        QVERIFY(QMetaObject::invokeMethod(dock("templateDock"), "close"));
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("timingDock")))));
        QTRY_VERIFY(isOpen("timingDock"));
        QVERIFY(application->workspaceLayout().resetCurrentLayout());
        verifyKaraokeArrangement();
        applyPreset("Editing");
        QTRY_COMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "gridDock"}));
        QTRY_COMPARE(sceneRect("audioPanel"), editingAudio);
        applyPreset("Karaoke");
        verifyKaraokeArrangement();
        stop();
        start();
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Karaoke"));
        verifyKaraokeArrangement();
        stop();
    }

    // Choosing Karaoke turns Karaoke mode and the Grid's folding on; leaving
    // it for another arrangement puts both back. Reset this layout and a new
    // tab (a copy of this one's arrangement) change nothing.
    void choosingKaraokeTurnsItsModeOn()
    {
        start();
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        applyPreset("Karaoke");
        QVERIFY(audio().karaoke());
        QVERIFY(shell().foldGenerated());
        QVERIFY(application->workspaceLayout().resetCurrentLayout());
        QVERIFY(audio().karaoke());
        applyPreset("Timing");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        // A saved layout made from Karaoke carries it too.
        applyPreset("Karaoke");
        QVERIFY(application->workspaceLayout().saveLayoutAs(QStringLiteral("My karaoke")));
        applyPreset("Editing");
        QVERIFY(!audio().karaoke());
        QVERIFY(application->workspaceLayout().applySavedLayout(QStringLiteral("My karaoke")));
        QVERIFY(audio().karaoke());
        QVERIFY(shell().foldGenerated());
        applyPreset("Translation");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
    }

    // Leaving Karaoke puts back only what Karaoke changed: a mode that was
    // on before stays on, and a mode or fold the user changed while in
    // Karaoke stays as they set it.
    void leavingKeepsWhatTheUserChose()
    {
        start();
        audio().toggleKaraoke(); // on before Karaoke
        applyPreset("Karaoke");
        QVERIFY(audio().karaoke());
        applyPreset("Editing");
        QVERIFY(audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        audio().toggleKaraoke();
        // Turned off and on again by hand while in Karaoke: the user's.
        applyPreset("Karaoke");
        audio().toggleKaraoke();
        audio().toggleKaraoke();
        shell().setFoldGenerated(false);
        applyPreset("Editing");
        QVERIFY(audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        audio().toggleKaraoke();
        // Choosing Karaoke again while in it keeps what it changed first.
        applyPreset("Karaoke");
        applyPreset("Karaoke");
        applyPreset("Editing");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
    }

    // The modes are the application's, not a tab's: a preset chosen in
    // another tab after Karaoke leaves Karaoke all the same.
    void leavingFromAnotherTab()
    {
        start();
        application->addPage(); // a second Editing tab
        application->selectTab(0);
        applyPreset("Karaoke");
        QVERIFY(audio().karaoke());
        QVERIFY(shell().foldGenerated());
        application->selectTab(1);
        QVERIFY(audio().karaoke()); // showing a tab leaves nothing
        applyPreset("Timing");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
    }

    // Folding is a setting: Fold generated Lines in the Grid's ⋯ menu, kept
    // across a restart.
    void foldingIsTheGridsSetting()
    {
        start();
        auto *menu = item<QObject>("panelOptionsMenu");
        QVERIFY(QMetaObject::invokeMethod(menu, "openFor", Q_ARG(QVariant, QVariant::fromValue(dock("gridDock"))),
                                          Q_ARG(QVariant, QVariant::fromValue<QObject *>(item("gridPanel")))));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *fold = window()->findChild<QQuickItem *>(QStringLiteral("panelOptions_foldGenerated"));
        QTRY_VERIFY(fold);
        QCOMPARE(fold->property("text").toString(), QStringLiteral("Fold generated Lines"));
        QVERIFY(fold->property("checkable").toBool());
        QVERIFY(!fold->property("checked").toBool());
        QTRY_VERIFY(fold->isVisible() && fold->width() > 0);
        QTest::mouseClick(window(), Qt::LeftButton, {},
                          fold->mapToScene(QPointF(fold->width() / 2, fold->height() / 2)).toPoint());
        QTRY_VERIFY(shell().foldGenerated());
        QTRY_VERIFY(fold->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        stop();
        start();
        QVERIFY(shell().foldGenerated());
        stop();
    }

    // Each run of generated Lines folds into its first row: the Line numbers
    // it covers, its times, Styles and Effects, and how many Lines it holds.
    void generatedLinesFoldIntoOneRowPerRun()
    {
        start();
        openSong();
        QCOMPARE(gridRows().size(), 13);
        shell().setFoldGenerated(true);
        QCOMPARE(gridRows(), (QList<int>{0, 1, 2, 3, 4, 5, 10, 11}));
        QCOMPARE(fold(5), 1);
        QCOMPARE(fold(11), 1);
        QCOMPARE(fold(4), 0);
        QCOMPARE(cell(5, ui::LineTableModel::NumberColumn), QStringLiteral("6–10"));
        QCOMPARE(cell(5, ui::LineTableModel::StartColumn), QStringLiteral("0:00:00.80"));
        QCOMPARE(cell(5, ui::LineTableModel::EndColumn), QStringLiteral("0:00:04.20"));
        QCOMPARE(cell(5, ui::LineTableModel::StyleColumn), QStringLiteral("Romaji, English"));
        QCOMPARE(cell(5, ui::LineTableModel::EffectColumn), QStringLiteral("fx"));
        QCOMPARE(cell(5, ui::LineTableModel::TextColumn), QStringLiteral("5 generated Lines"));
        QCOMPARE(cell(11, ui::LineTableModel::NumberColumn), QStringLiteral("12–13"));
        QCOMPARE(cell(11, ui::LineTableModel::EffectColumn), QStringLiteral("fx, Effector [fx]"));
        QCOMPARE(cell(11, ui::LineTableModel::TextColumn), QStringLiteral("2 generated Lines"));
        QCOMPARE(gridModel()->index(gridRowOf(5), 0).data(ui::LineTableModel::FoldCountRole).toInt(), 5);
        // The Document is untouched, and the video still shows the Lines.
        QCOMPARE(session()->document().lines().size(), std::size_t(13));
        QVERIFY(!session()->canUndo());
        // Folding off shows every Line again.
        shell().setFoldGenerated(false);
        QCOMPARE(gridRows().size(), 13);
        QCOMPARE(fold(5), 0);
        stop();
    }

    // A folded row is selected whole: a click selects every Line of its
    // run, so Delete, Copy and Shift times act on all of them; Ctrl+click
    // takes the whole run out again. The status counts no Line as hidden.
    void aFoldedRowIsSelectedWhole()
    {
        start();
        openSong();
        shell().setFoldGenerated(true);
        application->clickLine(lineId(5).value, Qt::NoModifier);
        QCOMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9}));
        QCOMPARE(shell().selectionStatus(), QStringLiteral("5 lines selected"));
        application->clickLine(lineId(4).value, Qt::ShiftModifier);
        QCOMPARE(selected(), ids({4, 5, 6, 7, 8, 9}));
        application->clickLine(lineId(11).value, Qt::ControlModifier);
        QCOMPARE(selected(), ids({4, 5, 6, 7, 8, 9, 11, 12}));
        application->clickLine(lineId(5).value, Qt::ControlModifier);
        QCOMPARE(selected(), ids({4, 11, 12}));
        // Down from the Line before a run lands on its row, the run selected.
        application->clickLine(lineId(4).value, Qt::NoModifier);
        press(Qt::Key_Down);
        QTRY_COMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9}));
        press(Qt::Key_Down);
        QTRY_COMPARE(active(), std::optional(lineId(10)));
        QCOMPARE(selected(), ids({10}));
        // Delete removes the whole run, as one undo step.
        application->clickLine(lineId(11).value, Qt::NoModifier);
        QVERIFY(QMetaObject::invokeMethod(application.get(), "deleteLines"));
        QCOMPARE(session()->document().lines().size(), std::size_t(11));
        QCOMPARE(gridRows(), (QList<int>{0, 1, 2, 3, 4, 5, 10}));
        stop();
    }

    // → unfolds the current folded run, ← folds it again from its first
    // row; elsewhere they move between the cells. The arrow at the Text
    // cell's start folds and unfolds with a click.
    void arrowsFoldAndUnfold()
    {
        start();
        openSong();
        shell().setFoldGenerated(true);
        application->clickLine(lineId(5).value, Qt::NoModifier);
        press(Qt::Key_Right);
        QTRY_COMPARE(gridRows().size(), 12);
        QCOMPARE(fold(5), 2);
        QCOMPARE(cell(5, ui::LineTableModel::TextColumn), QStringLiteral("ki"));
        QVERIFY(shell().foldGenerated()); // the other run stays folded
        QCOMPARE(fold(11), 1);
        // Inside the run ← moves between cells; on its first row it folds,
        // and a run folded over the active Line leaves its first one active.
        application->clickLine(lineId(7).value, Qt::NoModifier);
        press(Qt::Key_Left);
        QCOMPARE(gridRows().size(), 12);
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(5).value)));
        QCOMPARE(gridRows().size(), 8);
        QCOMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9}));
        // A click on the arrow unfolds; elsewhere on the row it selects.
        auto *lineGrid = qobject_cast<ui::LineGrid *>(grid());
        QVERIFY(lineGrid);
        QTRY_VERIFY(lineGrid->lastPaintedRowCount() > 0);
        int textColumn = -1;
        for (int c = 0; c < lineGrid->columnCount(); ++c)
            if (lineGrid->columnTitle(c) == QStringLiteral("Text"))
                textColumn = c;
        QVERIFY(textColumn >= 0);
        const QRectF text = lineGrid->cellRect(gridRowOf(5), textColumn);
        QTest::mouseClick(window(), Qt::LeftButton, Qt::NoModifier,
                          grid()->mapToScene(QPointF(text.left() + 8, text.center().y())).toPoint());
        QTRY_COMPARE(fold(5), 2);
        press(Qt::Key_Left);
        QTRY_COMPARE(fold(5), 1);
        stop();
    }

    // Find landing on a Line inside a folded run unfolds that run.
    void findUnfoldsTheRunItLandsIn()
    {
        start();
        openSong();
        shell().setFoldGenerated(true);
        application->clickLine(lineId(4).value, Qt::NoModifier);
        application->runFindReplace(QStringLiteral("find"),
                                    {{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("marigold")}});
        QTRY_COMPARE(active(), std::optional(lineId(9)));
        QCOMPARE(fold(5), 2);
        QVERIFY(gridRows().contains(9));
        QCOMPARE(fold(11), 1);
        stop();
    }

    // The Karaoke arrangement and its Grid (a folded run selected, the other
    // unfolded) in the four themes, for review: written to
    // HIKARI_SURFACE_SHOT_DIR when it is set (karaoke-<part>-<theme>.png).
    void karaokeScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        openSong();
        applyPreset("Karaoke");
        verifyKaraokeArrangement();
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(11).value)));
        application->clickLine(lineId(5).value, Qt::NoModifier);
        QCOMPARE(fold(11), 2);
        for (const auto code : ui::theme::kCodes) {
            const QString theme = ui::theme::codeName(code);
            settings.setValue(QStringLiteral("appearance.theme"), theme);
            auto *controls = window()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
            // Both runs in view: the folded one selected, the unfolded one under it.
            QVERIFY(QMetaObject::invokeMethod(grid(), "scrollToTopRow", Q_ARG(int, gridRowOf(4))));
            QTest::qWait(300);
            const QImage image = window()->grabWindow();
            const auto save = [&](const QString &part, const QImage &shot) {
                QVERIFY(shot.save(out + QStringLiteral("/karaoke-") + part + QLatin1Char('-') + theme
                                  + QStringLiteral(".png")));
            };
            save(QStringLiteral("arrangement"), image);
            const QRectF gridRect = sceneRect("gridPanel");
            const qreal dpr = image.devicePixelRatio();
            const QRect part = QRectF(gridRect.left(), gridRect.top() - 32, gridRect.width(), gridRect.height() + 32)
                                   .toAlignedRect() & QRect(QPoint(), window()->size());
            save(QStringLiteral("grid"), image.copy(QRectF(part.topLeft() * dpr, part.size() * dpr).toAlignedRect()));
        }
        stop();
    }
};

QTEST_MAIN(KaraokePresetTest)
#include "karaoke_preset_tests.moc"
