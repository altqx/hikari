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
            "Comment: 0,0:00:00.00,0:00:00.00,Romaji,,0,0,0,template syl,{\\pos($scenter,$smiddle)}\n"
            "Comment: 0,0:00:01.00,0:00:02.00,Romaji,,0,0,0,karaoke,{\\k50}ki{\\k50}mi\n"
            "Comment: 0,0:00:02.00,0:00:03.00,Romaji,,0,0,0,karaoke,{\\k50}no{\\k50}na\n"
            "Comment: 0,0:00:03.00,0:00:04.00,Romaji,,0,0,0,karaoke,{\\k100}mae\n"
            "Dialogue: 0,0:00:01.00,0:00:04.00,English,,0,0,0,,Your name\n"
            "Dialogue: 0,0:00:00.80,0:00:01.50,Romaji,,0,0,0,fx,ki\n"
            "Dialogue: 0,0:00:01.30,0:00:02.00,Romaji,,0,0,0,fx,mi\n"
            "Dialogue: 0,0:00:01.80,0:00:02.50,Romaji,,0,0,0,fx,no\n"
            "Dialogue: 0,0:00:02.30,0:00:03.10,Romaji,,0,0,0,fx,na\n"
            "Dialogue: 0,0:00:02.80,0:00:04.20,English,,0,0,0,fx,mae marigold\n"
            "Dialogue: 0,0:00:05.00,0:00:06.00,Romaji,,0,0,0,,{\\k50}ha{\\k50}ru\n"
            "Dialogue: 0,0:00:05.00,0:00:05.50,Romaji,,0,0,0,fx,ha\n"
            "Dialogue: 0,0:00:05.50,0:00:06.00,Romaji,,0,0,0,Effector [fx],ru\n");
    return path;
}

}

class KaraokePresetTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;

    void start()
    {
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
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
    void verifyKaraokeArrangement()
    {
        QTRY_COMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "templateDock", "gridDock"}));
        for (const char *name : {"videoDock", "audioDock", "editorDock", "templateDock", "gridDock"})
            QVERIFY2(!isFloating(name), name);
        QTRY_VERIFY(item("templatePanel")->isVisible());
        QVERIFY(!item("editorPanel")->isVisible());
        const QRectF video = sceneRect("videoPanel");
        const QRectF templates = sceneRect("templatePanel");
        const QRectF audio = sceneRect("audioPanel");
        const QRectF grid = sceneRect("gridPanel");
        const auto where = [&] {
            return qPrintable(QStringLiteral("video %1 template %2 audio %3 grid %4")
                                  .arg(rectText(video), rectText(templates), rectText(audio), rectText(grid)));
        };
        QVERIFY2(templates.left() > video.right() - 1, where());
        QVERIFY2(qAbs(templates.bottom() - video.bottom()) < 2, where());
        QVERIFY2(video.width() < templates.width() + 1, where());
        QVERIFY2(audio.top() > video.bottom() - 1 && audio.top() > templates.bottom() - 1, where());
        QVERIFY2(qAbs(audio.left() - grid.left()) < 2 && qAbs(audio.right() - grid.right()) < 2, where());
        QVERIFY2(audio.left() < video.left() + 2 && audio.right() > templates.right() - 2, where());
        QVERIFY2(grid.top() > audio.bottom() - 1, where());
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
        for (const char *name : {"layout.json", "layout.json.bak", "layouts.json", "last-session.json", "settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

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

    void karaokeIsAnArrangementOfItsOwn()
    {
        start();
        QCOMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "gridDock"}));
        const QRectF editingAudio = sceneRect("audioPanel");
        applyPreset("Karaoke");
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Karaoke"));
        verifyKaraokeArrangement();
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

    void leavingKeepsWhatTheUserChose()
    {
        start();
        audio().toggleKaraoke();
        applyPreset("Karaoke");
        QVERIFY(audio().karaoke());
        applyPreset("Editing");
        QVERIFY(audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        audio().toggleKaraoke();
        applyPreset("Karaoke");
        audio().toggleKaraoke();
        audio().toggleKaraoke();
        shell().setFoldGenerated(false);
        applyPreset("Editing");
        QVERIFY(audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        audio().toggleKaraoke();
        applyPreset("Karaoke");
        applyPreset("Karaoke");
        applyPreset("Editing");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
    }

    void leavingFromAnotherTab()
    {
        start();
        application->addPage();
        application->selectTab(0);
        applyPreset("Karaoke");
        QVERIFY(audio().karaoke());
        QVERIFY(shell().foldGenerated());
        application->selectTab(1);
        QVERIFY(audio().karaoke());
        applyPreset("Timing");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
    }

    void leavingAfterARestart()
    {
        start();
        applyPreset("Karaoke");
        QVERIFY(audio().karaoke());
        QVERIFY(shell().foldGenerated());
        stop();
        start();
        QVERIFY(audio().karaoke());
        QVERIFY(shell().foldGenerated());
        applyPreset("Timing");
        QVERIFY(!audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
        start();
        QVERIFY(!audio().karaoke());
        audio().toggleKaraoke();
        applyPreset("Karaoke");
        stop();
        start();
        applyPreset("Editing");
        QVERIFY(audio().karaoke());
        QVERIFY(!shell().foldGenerated());
        stop();
    }

    void foldingIsTheGridsSetting()
    {
        start();
        auto *menu = item<QObject>("panelOptionsMenu");
        QVERIFY(QMetaObject::invokeMethod(menu, "openFor", Q_ARG(QVariant, QVariant::fromValue(dock("gridDock"))),
                                          Q_ARG(QVariant, QVariant::fromValue<QObject *>(item("gridPanel")))));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *fold = window()->findChild<QQuickItem *>(QStringLiteral("panelOptions_foldGenerated"));
        QTRY_VERIFY(fold);
        QCOMPARE(fold->property("text").toString(), QStringLiteral("Fold generated lines"));
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
        QCOMPARE(cell(5, ui::LineTableModel::TextColumn), QStringLiteral("5 generated lines"));
        QCOMPARE(cell(11, ui::LineTableModel::NumberColumn), QStringLiteral("12–13"));
        QCOMPARE(cell(11, ui::LineTableModel::EffectColumn), QStringLiteral("fx, Effector [fx]"));
        QCOMPARE(cell(11, ui::LineTableModel::TextColumn), QStringLiteral("2 generated lines"));
        QCOMPARE(gridModel()->index(gridRowOf(5), 0).data(ui::LineTableModel::FoldCountRole).toInt(), 5);
        QCOMPARE(session()->document().lines().size(), std::size_t(13));
        QVERIFY(!session()->canUndo());
        shell().setFoldGenerated(false);
        QCOMPARE(gridRows().size(), 13);
        QCOMPARE(fold(5), 0);
        stop();
    }

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
        application->clickLine(lineId(4).value, Qt::NoModifier);
        press(Qt::Key_Down);
        QTRY_COMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9}));
        press(Qt::Key_Down);
        QTRY_COMPARE(active(), std::optional(lineId(10)));
        QCOMPARE(selected(), ids({10}));
        application->clickLine(lineId(11).value, Qt::NoModifier);
        QVERIFY(QMetaObject::invokeMethod(application.get(), "deleteLines"));
        QCOMPARE(session()->document().lines().size(), std::size_t(11));
        QCOMPARE(gridRows(), (QList<int>{0, 1, 2, 3, 4, 5, 10}));
        stop();
    }

    void aFoldedRowActsOnAllItsLines()
    {
        start();
        openSong();
        shell().setFoldGenerated(true);
        application->clickLine(lineId(5).value, Qt::NoModifier);
        QCOMPARE(application->selectedLineCount(true), 5);
        QVERIFY(application->deleteText());
        for (int row = 5; row <= 9; ++row)
            QVERIFY2(session()->document().lines()[std::size_t(row)]->text.empty(), QByteArray::number(row));
        QVERIFY(session()->document().lines()[10]->text == u8"{\\k50}ha{\\k50}ru");
        stop();
    }

    void foldingSelectsTheRunAsItsFirstLine()
    {
        start();
        openSong();
        shell().setFoldGenerated(true);
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(5).value)));
        QCOMPARE(fold(5), 2);
        application->clickLine(lineId(5).value, Qt::NoModifier);
        application->clickLine(lineId(7).value, Qt::ControlModifier);
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(5).value)));
        QCOMPARE(fold(5), 1);
        QCOMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9}));
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(5).value)));
        application->clickLine(lineId(7).value, Qt::NoModifier);
        application->clickLine(lineId(4).value, Qt::ControlModifier);
        QCOMPARE(active(), std::optional(lineId(4)));
        QCOMPARE(selected(), ids({4, 7}));
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(5).value)));
        QCOMPARE(selected(), ids({4}));
        stop();
    }

    void turningFoldingOnSelectsRunsWhole()
    {
        start();
        openSong();
        application->clickLine(lineId(11).value, Qt::NoModifier);
        application->clickLine(lineId(7).value, Qt::ControlModifier);
        QCOMPARE(active(), std::optional(lineId(7)));
        QCOMPARE(selected(), ids({7, 11}));
        shell().setFoldGenerated(true);
        QCOMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9, 11, 12}));
        QCOMPARE(gridRows(), (QList<int>{0, 1, 2, 3, 4, 5, 10, 11}));
        shell().setFoldGenerated(false);
        application->clickLine(lineId(11).value, Qt::NoModifier);
        applyPreset("Karaoke");
        QCOMPARE(selected(), ids({11, 12}));
        stop();
    }

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
        QVERIFY(shell().foldGenerated());
        QCOMPARE(fold(11), 1);
        application->clickLine(lineId(7).value, Qt::NoModifier);
        press(Qt::Key_Left);
        QCOMPARE(gridRows().size(), 12);
        QVERIFY(QMetaObject::invokeMethod(application.get(), "toggleFold", Q_ARG(qulonglong, lineId(5).value)));
        QCOMPARE(gridRows().size(), 8);
        QCOMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5, 6, 7, 8, 9}));
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

    void aSelectionOfPartOfARunUnfoldsIt()
    {
        start();
        openSong();
        shell().setFoldGenerated(true);
        application->clickLine(lineId(4).value, Qt::NoModifier);
        application->runFindReplace(QStringLiteral("find"),
                                    {{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("ki")}});
        QTRY_COMPARE(active(), std::optional(lineId(5)));
        QCOMPARE(selected(), ids({5}));
        QCOMPARE(fold(5), 2);
        QCOMPARE(gridRows(), (QList<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
        QCOMPARE(fold(11), 1);
        stop();
    }

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
