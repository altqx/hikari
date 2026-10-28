// TP1 (#274): the Typesetting preset is an arrangement of its own (the
// owner-approved mockup, 2026-10-22): Video leading the upper row with Styles
// on its right, the Line editor beside a short Grid below, Audio closed, on a
// fresh profile and after Reset this layout. The Styles panel lists the
// editing target's Styles, applies one to the selected Lines with Enter or a
// double click, filters as you type, and selects the Lines using a Style.

#include "hikari/app/application.h"
#include "hikari/application/edit_session.h"

#include "docking.h"
#include "line_editor_controller.h"
#include "settings_store.h"
#include "theme.h"
#include "workspace_layout.h"

#include <kddockwidgets/LayoutSaver.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <set>

using namespace hikari;

namespace {

// An ASS file with four Styles; "Sign" is used by two Lines, "Unused" by none.
QString writeSubtitles(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
            "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
            "MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
            "Style: Italic,Arial,48,&H00FFFFFF,&H000000FF,&H00101010,&H00000000,0,-1,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
            "Style: Sign,Times New Roman,64,&H00E2F0F4,&H000000FF,&H002B2B2B,&H00000000,-1,0,0,0,100,100,0,0,1,2,0,7,10,10,10,1\n"
            "Style: Unused,Arial,30,&H8000FFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
            "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
            "Dialogue: 1,0:00:01.00,0:00:03.00,Sign,,0,0,0,,{\\pos(200,90)}KITA-SENJU\n"
            "Dialogue: 0,0:00:02.00,0:00:03.00,Italic,,0,0,0,,second\n"
            "Dialogue: 1,0:00:02.00,0:00:04.00,Sign,,0,0,0,,Platform 3\n"
            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,third\n");
    return path;
}

} // namespace

class TypesettingPresetTest : public QObject {
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
    template <typename T = QQuickItem> T *item(const QString &name) const { return window()->findChild<T *>(name); }
    template <typename T = QQuickItem> T *item(const char *name) const { return item<T>(QLatin1String(name)); }
    // A list row's item: delegates are in the visual tree only.
    static QQuickItem *findVisual(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findVisual(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *rowItem(const char *name) const { return findVisual(item("stylesList"), QLatin1String(name)); }
    QObject *dock(const char *name) const { return item<QObject>(name); }
    bool isOpen(const char *name) const { return dock(name)->property("isOpen").toBool(); }
    bool isFloating(const char *name) const { return dock(name)->property("isFloating").toBool(); }
    void applyPreset(const char *name)
    {
        QVERIFY(QMetaObject::invokeMethod(window(), "applyPreset", Q_ARG(QVariant, QLatin1String(name))));
    }
    void openEpisode() { QVERIFY(application->openFile(writeSubtitles(dir.filePath(QStringLiteral("ts.ass"))))); }
    application::EditSession *session() const
    {
        return application->files().session(*application->workspace().editingTarget());
    }
    core::LineId lineId(int row) const { return session()->document().lines()[std::size_t(row)]->id; }
    QString lineStyle(int row) const
    {
        const auto &style = session()->document().lines()[std::size_t(row)]->style;
        return QString::fromUtf8(reinterpret_cast<const char *>(style.data()), qsizetype(style.size()));
    }
    // `rows` selected, the first active, as the Grid would leave them.
    void select(std::initializer_list<int> rows)
    {
        std::set<core::LineId> ids;
        for (int row : rows)
            ids.insert(lineId(row));
        const core::LineId active = lineId(*rows.begin());
        session()->setSelection(application::Selection{active, ids, active, {}});
        application->editor().reloadFromSession();
    }
    std::set<core::LineId> selected() const { return session()->selection().selected; }
    ui::Docking *docking() const { return engine->singletonInstance<ui::Docking *>("Hikari.Ui", "Docking"); }
    QRectF sceneRect(const char *panel) const
    {
        auto *it = item(panel);
        return QRectF(it->mapToScene(QPointF(0, 0)), QSizeF(it->width(), it->height()));
    }
    QStringList openPanels() const
    {
        QStringList out;
        for (const char *name : {"videoDock", "audioDock", "originalDock", "editorDock", "gridDock", "referenceDock",
                                 "timingDock", "searchDock", "automationDock", "stylesDock"})
            if (isOpen(name))
                out << QLatin1String(name);
        return out;
    }
    // The Typesetting arrangement: its panels, where they are and their
    // proportions (the approved mockup's A: Styles right of the video, the
    // Line editor beside the Grid below, the upper row the larger).
    void verifyTypesettingArrangement()
    {
        QTRY_COMPARE(openPanels(), (QStringList{"videoDock", "editorDock", "gridDock", "stylesDock"}));
        for (const char *name : {"videoDock", "editorDock", "gridDock", "stylesDock"})
            QVERIFY2(!isFloating(name), name);
        const QRectF video = sceneRect("videoPanel");
        const QRectF styles = sceneRect("stylesPanel");
        const QRectF editor = sceneRect("editorPanel");
        const QRectF grid = sceneRect("gridPanel");
        // Styles on the video's right, in the same row, narrower.
        QVERIFY2(styles.left() > video.right() - 1, qPrintable(QStringLiteral("%1 %2").arg(styles.left()).arg(video.right())));
        // (The bodies: the Styles header holds its tools and is the taller.)
        QVERIFY(qAbs(styles.bottom() - video.bottom()) < 2);
        QVERIFY(styles.width() < video.width() / 2);
        QVERIFY(styles.width() > 150);
        // The Line editor on the Grid's left, both under the upper row.
        QVERIFY(editor.right() < grid.left() + 1);
        QVERIFY(qAbs(editor.bottom() - grid.bottom()) < 2);
        QVERIFY(editor.top() > video.bottom() - 1);
        QVERIFY(editor.top() > styles.bottom() - 1);
        // The upper row is the larger, the lower one keeps the Line editor's rows.
        QVERIFY2(video.height() > 1.3 * editor.height(),
                 qPrintable(QStringLiteral("%1 vs %2").arg(video.height()).arg(editor.height())));
        QTRY_VERIFY(!item("editorScroll")->property("overflows").toBool()); // its rows whole
        QVERIFY2(item("lineInspector")->property("wide").toBool(), // LE1: its fields in two rows
                 qPrintable(QStringLiteral("%1 < %2").arg(item("lineInspector")->parentItem()->width())
                                .arg(item("lineInspector")->property("wideMinimum").toReal())));
    }

    // The default (Editing) arrangement, untouched by building Typesetting's:
    // Video on the left of the upper row, Audio above the Line editor on its
    // right, the Grid below.
    void verifyEditingArrangement()
    {
        const QRectF video = sceneRect("videoPanel");
        const QRectF audio = sceneRect("audioPanel");
        const QRectF editor = sceneRect("editorPanel");
        const QRectF grid = sceneRect("gridPanel");
        QVERIFY2(editor.left() > video.right() - 1,
                 qPrintable(QStringLiteral("video %1,%2 %3x%4 editor %5,%6 %7x%8")
                                .arg(video.left()).arg(video.top()).arg(video.width()).arg(video.height())
                                .arg(editor.left()).arg(editor.top()).arg(editor.width()).arg(editor.height())));
        QVERIFY(qAbs(audio.left() - editor.left()) < 2);
        QVERIFY(audio.bottom() < editor.top() + 1);
        QVERIFY(grid.top() > video.bottom() - 1);
        QVERIFY(grid.width() > video.width());
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layouts.json", "LastSession.txt", "hikari.ini"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

    // Choosing Typesetting on a fresh profile gives its own arrangement;
    // Reset this layout and a restart give it back.
    void typesettingIsAnArrangementOfItsOwn()
    {
        start();
        QCOMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "gridDock"})); // Editing
        verifyEditingArrangement();
        applyPreset("Typesetting");
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Typesetting"));
        verifyTypesettingArrangement();
        // Changed by hand, then reset.
        QVERIFY(QMetaObject::invokeMethod(dock("stylesDock"), "close"));
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("timingDock")))));
        QTRY_VERIFY(isOpen("timingDock"));
        QVERIFY(application->workspaceLayout().resetCurrentLayout());
        verifyTypesettingArrangement();
        // Editing is unchanged, and Typesetting comes back after it.
        applyPreset("Editing");
        QTRY_COMPARE(openPanels(), (QStringList{"videoDock", "audioDock", "editorDock", "gridDock"}));
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        stop();
        start();
        QCOMPARE(application->workspaceLayout().preset(), QStringLiteral("Typesetting"));
        verifyTypesettingArrangement();
        stop();
    }

    // Audio, opened in Typesetting, comes above the Line editor (its place
    // in Editing); the Styles panel opened in Editing tabs with the Line
    // editor, docked.
    void closedPanelsKeepTheirPlaces()
    {
        start();
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("stylesDock")))));
        QTRY_VERIFY(isOpen("stylesDock"));
        QVERIFY(!isFloating("stylesDock"));
        QTRY_VERIFY(item("stylesPanel")->isVisible());
        QCOMPARE(sceneRect("stylesPanel").left(), sceneRect("stylesPanel").left()); // laid out
        QVERIFY(!item("editorPanel")->isVisible()); // the tab behind it
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("audioDock")))));
        QTRY_VERIFY(isOpen("audioDock"));
        QVERIFY(!isFloating("audioDock"));
        QTRY_VERIFY(sceneRect("audioPanel").bottom() <= sceneRect("editorPanel").top() + 1);
        QVERIFY(qAbs(sceneRect("audioPanel").left() - sceneRect("editorPanel").left()) < 2);
        stop();
    }

    // An arrangement saved before the Styles panel existed has no place for
    // it; opening it then tabs it with the Line editor, as the default does.
    void stylesOpenWithTheEditorFromAnOlderArrangement()
    {
        start();
        QJsonObject payload = QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object();
        QVERIFY(!isOpen("stylesDock"));
        QJsonArray closed, all;
        for (const auto &name : payload.value(QStringLiteral("closedDockWidgets")).toArray())
            if (name.toString() != QLatin1String("Styles"))
                closed.append(name);
        for (const auto &d : payload.value(QStringLiteral("allDockWidgets")).toArray())
            if (d.toObject().value(QStringLiteral("uniqueName")).toString() != QLatin1String("Styles"))
                all.append(d);
        payload.insert(QStringLiteral("closedDockWidgets"), closed);
        payload.insert(QStringLiteral("allDockWidgets"), all);
        QVERIFY(KDDockWidgets::LayoutSaver().restoreLayout(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        QVERIFY(!docking()->hasPlace(QStringLiteral("Styles")));
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("stylesDock")))));
        QTRY_VERIFY(isOpen("stylesDock"));
        QVERIFY(!isFloating("stylesDock"));
        QTRY_VERIFY(item("stylesPanel")->isVisible());
        QVERIFY(!item("editorPanel")->isVisible()); // the tab behind it
        QCOMPARE(docking()->tabCount(QStringLiteral("Styles")), docking()->tabCount(QStringLiteral("Editor")));
        QVERIFY(docking()->tabCount(QStringLiteral("Styles")) > 1);
        stop();
    }

    // A preset is the window's arrangement at the window's size now:
    // chosen after a resize, Typesetting (built before, or not) and Timing
    // leave the main window as it is.
    void presetsKeepTheWindowsSize()
    {
        start();
        window()->resize(1280, 800);
        QTRY_COMPARE(window()->width(), 1280);
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        window()->resize(1100, 720);
        QTRY_COMPARE(window()->width(), 1100);
        applyPreset("Timing");
        QCOMPARE(window()->size(), QSize(1100, 720));
        window()->resize(1200, 760);
        QTRY_COMPARE(window()->width(), 1200);
        applyPreset("Typesetting");
        QCOMPARE(window()->size(), QSize(1200, 760));
        verifyTypesettingArrangement();
        QVERIFY(QMetaObject::invokeMethod(window(), "resetLayout"));
        QCOMPARE(window()->size(), QSize(1200, 760));
        verifyTypesettingArrangement();
        stop();
    }

    // A combo box over an array of objects with a text role (the Move
    // panel window's Panel and Next to) shows each entry's text in its popup.
    void arrayChoicesShowTheirTextRole()
    {
        start();
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("movePanel"), "triggered"));
        auto *placement = window()->findChild<QQuickWindow *>(QStringLiteral("placementWindow"));
        QVERIFY(placement);
        QTRY_VERIFY(placement->isVisible());
        QObject *box = findVisual(placement->contentItem(), QStringLiteral("placementPanel"));
        QVERIFY(box);
        auto *popup = box->property("popup").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *list = qobject_cast<QQuickItem *>(popup->property("contentItem").value<QObject *>());
        QVERIFY(list);
        QQuickItem *first = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0)) && first);
        QCOMPARE(first->property("text").toString(), dock("videoDock")->property("title").toString());
        QVERIFY(!first->property("text").toString().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        placement->close();
        stop();
    }

    // The panel follows the editing target: with no Document left (its
    // last tab closed) New is off and the empty state says why.
    void noDocumentTurnsNewOff()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        QTRY_VERIFY(item<QObject>("stylesNew")->property("enabled").toBool());
        // The last tab closed: no editing target, so no ASS Styles.
        QVERIFY(application->closeEditingTarget());
        QTRY_VERIFY(!item<QObject>("stylesNew")->property("enabled").toBool());
        QTRY_VERIFY(item("stylesEmpty")->property("text").toString().startsWith(QStringLiteral("No styles: the file")));
        stop();
    }

    // Another tab whose Styles lack the chosen one: the panel chooses the
    // active Line's Style there, not the first.
    void anotherTabChoosesItsActiveLinesStyle()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 4);
        panel->setProperty("chosen", QStringLiteral("Unused"));
        const QString path = dir.filePath(QStringLiteral("other.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: First,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
                "Style: Song,Georgia,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Song,,0,0,0,,la la\n");
        f.close();
        QVERIFY(application->openFile(path));
        // It opens in a tab of its own, behind; shown, it is the editing target.
        const int last = int(application->workspace().tabs().size()) - 1;
        QCOMPARE(application->tabDocument(last), application->workspace().tabs().back().value);
        application->selectTab(last);
        QTRY_COMPARE(panel->property("rows").toList().size(), 2);
        QCOMPARE(panel->property("chosen").toString(), QStringLiteral("Song"));
        stop();
    }

    // The chosen Style renamed in the Style manager stays chosen under its
    // new name, whatever Style the current line uses.
    void aRenamedStyleStaysChosen()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 4);
        panel->setProperty("chosen", QStringLiteral("Unused"));
        QCOMPARE(panel->property("chosenAt").toInt(), 3);
        auto &styles = application->styleManager();
        styles.refreshLists();
        auto values = styles.beginEdit(false, 3);
        values.insert(QStringLiteral("name"), QStringLiteral("Spare"));
        QCOMPARE(styles.commitEdit(values, {3}, false, false), QString());
        styles.endEdit();
        QTRY_COMPARE(panel->property("chosen").toString(), QStringLiteral("Spare"));
        QCOMPARE(panel->property("chosenAt").toInt(), 3);
        stop();
    }

    // A Style named "" is listed, but neither the panel nor the Line
    // editor's Style choice applies it.
    void anEmptyNamedStyleIsNotApplied()
    {
        start();
        const QString path = dir.filePath(QStringLiteral("blank.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
                "Style: ,Arial,30,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n");
        f.close();
        QVERIFY(application->openFile(path));
        applyPreset("Typesetting");
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 2);
        item("stylesList")->setProperty("currentIndex", 1);
        QCOMPARE(panel->property("chosenAt").toInt(), 1);
        QVERIFY(!item<QObject>("stylesApply")->property("enabled").toBool());
        item("stylesList")->setProperty("currentIndex", 0);
        QVERIFY(item<QObject>("stylesApply")->property("enabled").toBool());
        stop();
    }

    // Two Styles of one name (a file may hold them): the commands act on the
    // row chosen, not the first of that name.
    void aDuplicateNameActsOnTheRowChosen()
    {
        start();
        const QString path = dir.filePath(QStringLiteral("twice.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Sign,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
                "Style: Sign,Georgia,64,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n"
                "Style: Other,Arial,30,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Other,,0,0,0,,first\n");
        f.close();
        QVERIFY(application->openFile(path));
        applyPreset("Typesetting");
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 3);
        auto *list = item("stylesList");
        list->setProperty("currentIndex", 1);
        QCOMPARE(panel->property("chosenAt").toInt(), 1);
        QCOMPARE(panel->property("chosen").toString(), QStringLiteral("Sign"));
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("stylesDelete"), "clicked"));
        QTRY_COMPARE(panel->property("rows").toList().size(), 2);
        const auto rows = panel->property("rows").toList();
        QCOMPARE(rows.at(0).toMap().value("name").toString(), QStringLiteral("Sign"));
        QCOMPARE(rows.at(0).toMap().value("font").toString(), QStringLiteral("Arial")); // the first one stays
        QCOMPARE(rows.at(1).toMap().value("name").toString(), QStringLiteral("Other"));
        stop();
    }

    // The rows: the editing target's Styles in order, with their font, size,
    // colours and how many Lines use each; the current line's style marked.
    void theStylesPanelListsTheDocumentsStyles()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 4);
        const auto rows = panel->property("rows").toList();
        QStringList names;
        for (const auto &r : rows)
            names << r.toMap().value(QStringLiteral("name")).toString();
        QCOMPARE(names, (QStringList{"Default", "Italic", "Sign", "Unused"}));
        const auto sign = rows[2].toMap();
        QCOMPARE(sign.value(QStringLiteral("font")).toString(), QStringLiteral("Times New Roman"));
        QCOMPARE(sign.value(QStringLiteral("size")).toString(), QStringLiteral("64"));
        QVERIFY(sign.value(QStringLiteral("bold")).toBool());
        QCOMPARE(sign.value(QStringLiteral("lines")).toInt(), 2);
        QCOMPARE(sign.value(QStringLiteral("primary")).value<QColor>(), QColor(0xF4, 0xF0, 0xE2));
        QCOMPARE(rows[0].toMap().value(QStringLiteral("lines")).toInt(), 2);
        QCOMPARE(rows[3].toMap().value(QStringLiteral("lines")).toInt(), 0);
        QVERIFY(rows[1].toMap().value(QStringLiteral("italic")).toBool());
        QCOMPARE(rows[3].toMap().value(QStringLiteral("primary")).value<QColor>().alpha(), 0x7F); // &H80: half transparent
        // The current Line (the first, Default) marks its Style, and the
        // mark follows the active Line.
        const auto marked = [this](const char *mark) { return rowItem(mark) && rowItem(mark)->isVisible(); };
        QTRY_VERIFY(marked("stylesLineMark0"));
        select({2});
        QTRY_VERIFY(marked("stylesLineMark1")); // Italic
        QVERIFY(!marked("stylesLineMark0"));
        // The rows follow the Document: a Line's Style changed elsewhere.
        QVERIFY(application->editor().chooseStyle(QStringLiteral("Unused"))); // the active Line, Italic
        QTRY_COMPARE(panel->property("rows").toList()[3].toMap().value(QStringLiteral("lines")).toInt(), 1);
        QCOMPARE(panel->property("rows").toList()[1].toMap().value(QStringLiteral("lines")).toInt(), 0);
        stop();
    }

    // PV1 (#398): each row's sample draws the preview text (styles.previewText,
    // its characters without override tags) and follows a change at once.
    void theStylesPanelSamplesDrawThePreviewText()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        QTRY_COMPARE(item<QObject>("stylesTool")->property("rows").toList().size(), 4);
        // A row made again while the list is read again: asked each time.
        const auto sample = [this](const char *name) {
            QQuickItem *text = rowItem(name);
            return text ? text->property("text").toString() : QString();
        };
        QTRY_COMPARE(sample("stylesSampleText0"), QStringLiteral("光 Hikari 日本"));
        QTRY_COMPARE(sample("stylesSampleText3"), QStringLiteral("光 Hikari 日本"));
        application->settingsStore()->set("styles.previewText", QStringLiteral("{\\b1}Mine"));
        QTRY_COMPARE(sample("stylesSampleText0"), QStringLiteral("Mine"));
        QTRY_COMPARE(sample("stylesSampleText3"), QStringLiteral("Mine"));
        stop();
    }

    // Enter or a double click applies the chosen Style to the selected Lines
    // as one undo step; the row menu selects the Lines using a Style.
    void aStyleIsAppliedToTheSelectedLines()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 4);
        // Lines 1 and 3 (Default, Italic) selected.
        select({0, 2});
        auto *list = item("stylesList");
        list->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(list->hasActiveFocus());
        list->setProperty("currentIndex", 2); // Sign
        QCOMPARE(panel->property("chosen").toString(), QStringLiteral("Sign"));
        QTest::keyClick(window(), Qt::Key_Return);
        QTRY_COMPARE(lineStyle(0), QStringLiteral("Sign"));
        QCOMPARE(lineStyle(2), QStringLiteral("Sign"));
        QCOMPARE(lineStyle(4), QStringLiteral("Default")); // not selected
        QTRY_COMPARE(panel->property("rows").toList()[2].toMap().value(QStringLiteral("lines")).toInt(), 4);
        QVERIFY(application->editor().undo());
        QTRY_COMPARE(lineStyle(0), QStringLiteral("Default"));
        QCOMPARE(lineStyle(2), QStringLiteral("Italic"));
        // A double click on Unused.
        select({4});
        auto *row = rowItem("stylesRow3");
        QVERIFY(row);
        QTest::mouseDClick(window(), Qt::LeftButton, {}, row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QTRY_COMPARE(lineStyle(4), QStringLiteral("Unused"));
        // Select lines with this style.
        QVERIFY(application->selectLinesWithStyle(QStringLiteral("Sign")));
        QCOMPARE(selected(), (std::set<core::LineId>{lineId(1), lineId(3)}));
        // A new range: Shift extends from the active Line, not an older anchor.
        QCOMPARE(session()->selection().anchor, session()->selection().active);
        QVERIFY(!session()->selection().extent);
        // The active Line kept when it uses the Style, its old range dropped.
        select({3, 4});
        auto withRange = session()->selection();
        withRange.anchor = lineId(4);
        withRange.extent = lineId(4);
        session()->setSelection(withRange);
        QVERIFY(application->selectLinesWithStyle(QStringLiteral("Sign")));
        QCOMPARE(session()->selection().active, std::optional<core::LineId>(lineId(3)));
        QCOMPARE(session()->selection().anchor, std::optional<core::LineId>(lineId(3)));
        QVERIFY(!session()->selection().extent);
        // No Line uses it: as Select mode, the old selection goes and the
        // active Line stays.
        QVERIFY(!application->selectLinesWithStyle(QStringLiteral("Italic") + QStringLiteral("x")));
        QVERIFY(selected().empty());
        QCOMPARE(session()->selection().active, std::optional<core::LineId>(lineId(3)));
        stop();
    }

    // A Style's name and font are the file's text, never markup: an <img>
    // in them is shown as typed and fetches nothing (Codex security review).
    void stylesAreShownAsPlainText()
    {
        start();
        const QString path = dir.filePath(QStringLiteral("markup.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: <img src=\"http://127.0.0.1:9/n.png\">,<b>Arial</b>,48,&H00FFFFFF,&H000000FF,&H00000000,"
                "&H00000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,<img src=\"http://127.0.0.1:9/n.png\">,,0,0,0,,first\n");
        f.close();
        QVERIFY(application->openFile(path));
        applyPreset("Typesetting");
        QTRY_VERIFY(rowItem("stylesName0"));
        const auto plain = [](QQuickItem *label) { return label->property("textFormat").toInt() == Qt::PlainText; };
        QVERIFY(plain(rowItem("stylesName0")));
        QCOMPARE(rowItem("stylesName0")->property("text").toString(), QStringLiteral("<img src=\"http://127.0.0.1:9/n.png\">"));
        QVERIFY(plain(rowItem("stylesFont0")));
        QCOMPARE(rowItem("stylesFont0")->property("text").toString(), QStringLiteral("<b>Arial</b> · 48"));
        // The Line editor's Style choice (its popup's items) too.
        auto *choice = item<QObject>("styleChoice");
        QVERIFY(choice);
        QTRY_COMPARE(choice->property("currentText").toString(), QStringLiteral("<img src=\"http://127.0.0.1:9/n.png\">"));
        // Its popup's items show the names (a list of strings, no textRole).
        auto *popup = choice->property("popup").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *list = qobject_cast<QQuickItem *>(popup->property("contentItem").value<QObject *>());
        QVERIFY(list);
        QQuickItem *first = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0)) && first);
        QCOMPARE(first->property("text").toString(), QStringLiteral("<img src=\"http://127.0.0.1:9/n.png\">"));
        auto *firstText = qobject_cast<QQuickItem *>(first->property("contentItem").value<QObject *>());
        QVERIFY(plain(firstText));
        QCOMPARE(firstText->property("text").toString(), first->property("text").toString());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        // The Grid's Hide lines with styles menu: its items plain too.
        auto *grid = item("editingGrid");
        auto *gridMenu = item<QObject>("gridMenu");
        QTest::mouseClick(window(), Qt::RightButton, Qt::NoModifier, grid->mapToScene(QPointF(grid->width() / 2, 30)).toPoint());
        QTRY_VERIFY(gridMenu->property("opened").toBool());
        auto *filtering = item<QObject>("filteringMenu");
        QVERIFY(QMetaObject::invokeMethod(filtering, "open"));
        QTRY_VERIFY(filtering->property("opened").toBool());
        auto *stylesMenu = item<QObject>("filterStylesMenu");
        QVERIFY(QMetaObject::invokeMethod(stylesMenu, "open"));
        QTRY_VERIFY(stylesMenu->property("opened").toBool());
        QQuickItem *entry = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(stylesMenu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, 0)) && entry);
        auto *shown = entry->findChild<QQuickItem *>(QStringLiteral("menuItemPlainText"));
        QVERIFY(shown && shown->isVisible());
        QVERIFY(plain(shown));
        QCOMPARE(shown->property("text").toString(), QStringLiteral("<img src=\"http://127.0.0.1:9/n.png\">"));
        QVERIFY(QMetaObject::invokeMethod(gridMenu, "dismiss"));
        QTRY_VERIFY(!gridMenu->property("visible").toBool());
        // The panels' headers too: their titles carry the file's name.
        const auto titles = window()->findChildren<QQuickItem *>(QStringLiteral("dockTitleText"));
        QVERIFY(!titles.isEmpty());
        for (QQuickItem *title : titles)
            QVERIFY(plain(title));
        stop();
    }

    // Typing in the list filters it; Esc in the filter clears it.
    void typingFiltersTheStyles()
    {
        start();
        openEpisode();
        applyPreset("Typesetting");
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 4);
        auto *list = item("stylesList");
        list->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(list->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_I, Qt::NoModifier);
        QTest::keyClick(window(), Qt::Key_T, Qt::NoModifier);
        QTRY_COMPARE(item("stylesFilter")->property("text").toString(), QStringLiteral("it"));
        QVERIFY(item("stylesFilter")->hasActiveFocus());
        QCOMPARE(panel->property("shownRows").toList().size(), 1); // Italic
        // A chosen Style the filter hides is not acted on: the commands wait
        // for one that is shown.
        QVERIFY(item<QObject>("stylesDelete")->property("enabled").toBool());
        panel->setProperty("chosen", QStringLiteral("Unused"));
        QVERIFY(!item<QObject>("stylesApply")->property("enabled").toBool());
        for (const char *name : {"stylesEdit", "stylesDuplicate", "stylesDelete"})
            QVERIFY2(!item<QObject>(name)->property("enabled").toBool(), name);
        QTest::keyClick(window(), Qt::Key_Escape);
        QTRY_COMPARE(panel->property("shownRows").toList().size(), 4);
        QTRY_VERIFY(item<QObject>("stylesDelete")->property("enabled").toBool());
        stop();
    }

    // The header's buttons: icon-only, named on hover; Delete removes the
    // chosen Style, Edit opens the Style manager on it.
    void theHeaderHoldsTheStyleCommands()
    {
        start();
        openEpisode();
        // The default window's size, whatever the screen (a small one folds
        // the header, checked below).
        window()->resize(1280, 800);
        QTRY_COMPARE(window()->width(), 1280);
        applyPreset("Typesetting");
        auto *panel = item<QObject>("stylesTool");
        QTRY_COMPARE(panel->property("rows").toList().size(), 4);
        auto *buttons = item("stylesButtons");
        QTRY_VERIFY(buttons && buttons->isVisible());
        QVERIFY(!item("stylesMore")->isVisible());
        for (const char *name : {"stylesApply", "stylesNew", "stylesEdit", "stylesDuplicate", "stylesDelete", "stylesManager"}) {
            auto *button = item<QObject>(name);
            QVERIFY2(button, name);
            QVERIFY2(!button->property("text").toString().isEmpty(), name);
            QCOMPARE(button->property("display").toInt(), 0); // icon only (AbstractButton.IconOnly)
        }
        // A narrow panel folds them into More, which holds the same six.
        QVERIFY(docking()->resizeInLayout(QStringLiteral("Styles"), -(int(item("stylesPanel")->width()) - 150), 0, 0, 0));
        QTRY_VERIFY(item("stylesMore")->isVisible());
        QVERIFY(!buttons->isVisible());
        auto *more = item<QObject>("stylesMoreMenu");
        QCOMPARE(more->property("count").toInt(), 8); // and two separators
        QVERIFY(docking()->resizeInLayout(QStringLiteral("Styles"), 200, 0, 0, 0));
        QTRY_VERIFY(buttons->isVisible());
        panel->setProperty("chosen", QStringLiteral("Unused"));
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("stylesDelete"), "clicked"));
        QTRY_COMPARE(panel->property("rows").toList().size(), 3);
        panel->setProperty("chosen", QStringLiteral("Sign"));
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("stylesEdit"), "clicked"));
        auto *manager = item<QObject>("styleManager");
        if (!manager)
            manager = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("styleManager"));
        QVERIFY(manager);
        QTRY_VERIFY(manager->property("visible").toBool());
        QVERIFY(manager->property("editing").toBool());
        // Bringing the manager forward keeps the edit's selections.
        const auto list = [manager](const char *name) {
            const QVariant v = manager->property(name);
            return v.metaType() == QMetaType::fromType<QJSValue>() ? v.value<QJSValue>().toVariant().toList() : v.toList();
        };
        manager->setProperty("storeSelected", QVariant::fromValue(QVariantList{2, 5}));
        QCOMPARE(list("storeSelected"), (QVariantList{2, 5}));
        const auto ass = list("assSelected");
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("stylesManager"), "clicked"));
        QCOMPARE(list("storeSelected"), (QVariantList{2, 5}));
        QCOMPARE(list("assSelected"), ass);
        // The edit in progress is not replaced or changed under it from here.
        for (const char *name : {"stylesNew", "stylesEdit", "stylesDuplicate", "stylesDelete"})
            QVERIFY2(!item<QObject>(name)->property("enabled").toBool(), name);
        QVERIFY(!item<QObject>("stylesMenuDelete")->property("enabled").toBool());
        stop();
    }

    // The Typesetting arrangement and the Styles panel (its header tools and
    // the current Line's mark) in the four themes, for review: written to
    // HIKARI_SURFACE_SHOT_DIR when it is set (typesetting-<part>-<theme>.png).
    void typesettingScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        openEpisode();
        applyPreset("Typesetting");
        verifyTypesettingArrangement();
        QVERIFY(application->selectLinesWithStyle(QStringLiteral("Sign"))); // the two signs
        QTRY_VERIFY(rowItem("stylesLineMark2") && rowItem("stylesLineMark2")->isVisible());
        for (const auto code : ui::theme::kCodes) {
            const QString theme = ui::theme::codeName(code);
            settings.setValue(QStringLiteral("appearance.theme"), theme);
            auto *controls = window()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
            QTest::qWait(300);
            const QImage image = window()->grabWindow();
            const auto save = [&](const QString &part, const QImage &shot) {
                QVERIFY(shot.save(out + QStringLiteral("/typesetting-") + part + QLatin1Char('-') + theme
                                  + QStringLiteral(".png")));
            };
            save(QStringLiteral("arrangement"), image);
            // The panel with its header, a little around it.
            const QRectF styles = sceneRect("stylesPanel");
            const qreal dpr = image.devicePixelRatio();
            const QRect part = QRectF(styles.left() - 8, styles.top() - 40, styles.width() + 16, styles.height() + 48)
                                   .toAlignedRect() & QRect(QPoint(), window()->size());
            save(QStringLiteral("styles-panel"),
                 image.copy(QRectF(part.topLeft() * dpr, part.size() * dpr).toAlignedRect()));
        }
        stop();
    }
};

QTEST_MAIN(TypesettingPresetTest)
#include "typesetting_preset_tests.moc"
