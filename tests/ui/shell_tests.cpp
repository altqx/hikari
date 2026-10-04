// V1-S: the Classic shell bound to real Lines. Target labels, the protected
// reference, F6/Shift+F6 panel traversal and focus restoration.

#include "hikari/app/application.h"
#include "hikari/application/options_dialog.h"
#include "docking.h"
#include "line_grid.h"

#include <QAccessible>
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cstring>
#include <functional>
#include <vector>
#include <set>
#include <optional>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString writeFile(const QTemporaryDir &dir, const char *name, const char *events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    f.write(events);
    return path;
}

} // namespace

class ShellTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString episode, original;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    // Docked panels may live in a floating window: look in every window.
    template <typename T = QQuickItem> T *item(const char *name) const
    {
        if (T *found = window->findChild<T *>(QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (T *found = w->findChild<T *>(QLatin1String(name)))
                return found;
        return nullptr;
    }
    // Repeater delegates are not QObject children of the window: walk the items.
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *visualItem(const char *name) const { return findItem(window->contentItem(), QLatin1String(name)); }
    QString panelTitle(const char *panel) const
    {
        auto *label = window->findChild<QObject *>(QLatin1String(panel) + QLatin1String("Title"));
        return label ? label->property("text").toString() : QString();
    }
    // The panel that contains the active focus item.
    QString focusedPanel() const
    {
        for (QQuickItem *p = window->activeFocusItem(); p; p = p->parentItem())
            if (p->objectName().endsWith(QLatin1String("Panel")))
                return p->objectName();
        return {};
    }
    void press(Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(window, key, mods);
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        episode = writeFile(dir, "episode.ass",
                            "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,first\n"
                            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n");
        original = writeFile(dir, "original.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,ref\n");
    }
    void init()
    {
        application = new app::Application;
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void cleanup()
    {
        delete engine;
        delete application;
    }

    void zeroDocumentState()
    {
        QCOMPARE(panelTitle("gridPanel"), QStringLiteral("No document open"));
        // D1: the Reference panel's dock is closed while there is no reference.
        QVERIFY(!item<QObject>("referenceDock")->property("isOpen").toBool());
        QCOMPARE(item<QObject>("statusTargets")->property("text").toString(), QStringLiteral("No editing target"));
    }

    void labelsNameTheEditingTargetAndTheProtectedReference()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        QCOMPARE(panelTitle("gridPanel"), QStringLiteral("Editing: episode.ass"));
        QTRY_VERIFY(item<QObject>("referenceDock")->property("isOpen").toBool());
        QVERIFY(item("referencePanel")->isVisible());
        QCOMPARE(panelTitle("referencePanel"), QStringLiteral("Reference (protected, read-only): original.ass"));
        QCOMPARE(item<QObject>("statusTargets")->property("text").toString(),
                 QStringLiteral("Editing: episode.ass  |  Reference (protected): original.ass"));
        // Panels are named for assistive technology too.
        QCOMPARE(QAccessible::queryAccessibleInterface(item("gridPanel"))->text(QAccessible::Name),
                 QStringLiteral("Editing: episode.ass"));
        // The Grids show the real Lines of each Document.
        QCOMPARE(item("editingGrid")->property("model").value<QAbstractItemModel *>()->rowCount(), 2);
        QCOMPARE(item("referenceGrid")->property("model").value<QAbstractItemModel *>()->rowCount(), 1);
    }

    void focusingTheReferenceDoesNotRetarget()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        auto &workspace = application->workspace();
        const auto a = *workspace.editingTarget();
        const auto ref = *workspace.reference();
        item("referenceGrid")->forceActiveFocus();
        QCOMPARE(focusedPanel(), QStringLiteral("referencePanel"));
        QCOMPARE(workspace.editingTarget(), a);
        QCOMPARE(panelTitle("gridPanel"), QStringLiteral("Editing: episode.ass"));
        QVERIFY(!workspace.checkContentCommand(ref));
        QVERIFY(workspace.checkContentCommand(a).has_value());
    }

    void f6TraversesTheMajorPanels()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        item("editingGrid")->forceActiveFocus();
        QStringList forward;
        for (int i = 0; i < 5; ++i) {
            press(Qt::Key_F6);
            forward << focusedPanel();
        }
        QCOMPARE(forward, (QStringList{"referencePanel", "videoPanel", "audioPanel", "editorPanel", "gridPanel"}));
        QStringList backward;
        for (int i = 0; i < 5; ++i) {
            press(Qt::Key_F6, Qt::ShiftModifier);
            backward << focusedPanel();
        }
        QCOMPARE(backward, (QStringList{"editorPanel", "audioPanel", "videoPanel", "referencePanel", "gridPanel"}));
    }

    void f6SkipsAHiddenReferenceAndWorksFromText()
    {
        QVERIFY(application->openFile(episode));
        item("lineText")->forceActiveFocus();
        QCOMPARE(focusedPanel(), QStringLiteral("editorPanel"));
        press(Qt::Key_F6);
        QCOMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        press(Qt::Key_F6);
        QCOMPARE(focusedPanel(), QStringLiteral("videoPanel"));
    }

    void focusReturnsToWhereItWasInAPanel()
    {
        QVERIFY(application->openFile(episode));
        item("showTags")->forceActiveFocus();
        press(Qt::Key_F6); // to the Grid
        QCOMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        press(Qt::Key_F6, Qt::ShiftModifier); // back to the editor
        QVERIFY(item("showTags")->hasActiveFocus());
        press(Qt::Key_F6);
        QVERIFY(item("editingGrid")->hasActiveFocus());
    }

    void gridActivationShowsTheLineInTheEditor()
    {
        QVERIFY(application->openFile(episode));
        auto *grid = item("editingGrid");
        // Legacy LoadSubtitles: the first Line (no "Active Line") is active at once.
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(item<QObject>("startField")->property("text").toString(), QStringLiteral("0:00:01.00"));
        QVERIFY(!item<QObject>("lineText")->property("readOnly").toBool());
        grid->forceActiveFocus();
        press(Qt::Key_Down); // keyboard navigation asks for the next Line
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("second"));
    }

    void boldWrapsTheSelectionWithTagsHidden()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home); // the first Line, "first"
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        QMetaObject::invokeMethod(text, "select", Q_ARG(int, 0), Q_ARG(int, 5));
        press(Qt::Key_B, Qt::ControlModifier);
        // The field still shows the text; the raw draft has the legacy tags,
        // and the same five characters stay selected.
        QCOMPARE(text->property("text").toString(), QStringLiteral("first"));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const std::u8string draft = session->draftText().value_or(u8"");
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(draft.data()), qsizetype(draft.size())),
                 QStringLiteral("{\\b1}first{\\b0}"));
        QCOMPARE(text->property("selectionStart").toInt(), 0);
        QCOMPARE(text->property("selectionEnd").toInt(), 5);
        // Draft Undo restores the exact previous raw text.
        press(Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(!session->draftLine());
    }

    // #99: Undo and Redo put the caret at the end of what changed, so typing
    // continues where the change was (the field used to keep its old index).
    void undoAndRedoPlaceTheCaretAtTheChange()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home); // the first Line, "first"
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 2);
        QTest::keyClick(window, 'X');
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("fiXrst"));
        QTRY_COMPARE(application->editor().text(), QStringLiteral("fiXrst"));
        text->setProperty("cursorPosition", 6); // the caret moved away meanwhile
        press(Qt::Key_Z, Qt::ControlModifier); // draft Undo
        QCOMPARE(text->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(text->property("cursorPosition").toInt(), 2);
        press(Qt::Key_Y, Qt::ControlModifier); // draft Redo
        QCOMPARE(text->property("text").toString(), QStringLiteral("fiXrst"));
        QCOMPARE(text->property("cursorPosition").toInt(), 3);
        // Committed, then Document Undo and Redo on the same Line.
        press(Qt::Key_Return, Qt::ControlModifier);
        text->setProperty("cursorPosition", 0);
        press(Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(text->property("cursorPosition").toInt(), 2);
        press(Qt::Key_Y, Qt::ControlModifier);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("fiXrst"));
        QCOMPARE(text->property("cursorPosition").toInt(), 3);
    }

    // G1: Grid gestures through the shell; the application owns the selection.
    void gridGesturesSelectRanges()
    {
        const QString path = writeFile(dir, "five.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,b\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,c\n"
                                       "Dialogue: 0,0:00:04.00,0:00:05.00,Default,,0,0,0,,d\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,e\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto lines = session->document().lines();
        QCOMPARE(lines.size(), std::size_t(5));
        std::vector<core::LineId> id;
        for (const auto *l : lines)
            id.push_back(l->id);
        const auto selected = [&](std::initializer_list<int> rows) {
            std::set<core::LineId> want;
            for (int r : rows)
                want.insert(id[static_cast<std::size_t>(r)]);
            return session->selection().selected == want;
        };
        auto *grid = item("editingGrid");
        grid->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_COMPARE(session->selection().active, std::optional(id[0]));
        // Keyboard Shift extends from the anchor and moves the active Line.
        press(Qt::Key_Down, Qt::ShiftModifier);
        press(Qt::Key_Down, Qt::ShiftModifier);
        QVERIFY(selected({0, 1, 2}));
        QCOMPARE(session->selection().active, std::optional(id[2]));
        // The accessible table reports the gesture's multi-selection.
        if (QAccessibleInterface *table = QAccessible::queryAccessibleInterface(grid); table && table->tableInterface())
            QCOMPARE(table->tableInterface()->selectedRows(), (QList<int>{0, 1, 2}));
        else
            QFAIL("the Grid exposes no accessible table");
        QTRY_COMPARE(item("lineText")->property("text").toString(), QStringLiteral("c"));
        press(Qt::Key_Up, Qt::ShiftModifier);
        QVERIFY(selected({0, 1}));
        // Ctrl+A selects every Line; a plain arrow returns to one.
        press(Qt::Key_A, Qt::ControlModifier);
        QVERIFY(selected({0, 1, 2, 3, 4}));
        press(Qt::Key_Down);
        QVERIFY(selected({2}));

        // Mouse: the centre of each row in window coordinates.
        const auto rowPoint = [&](int row) {
            int found = -1;
            for (int y = 0; y < int(grid->height()); ++y) {
                int r = -1;
                QMetaObject::invokeMethod(grid, "rowAt", Q_RETURN_ARG(int, r), Q_ARG(qreal, qreal(y)));
                if (r == row) {
                    found = y;
                    break;
                }
            }
            return grid->mapToScene(QPointF(grid->width() / 2, found + 3)).toPoint();
        };
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, rowPoint(4));
        QTRY_VERIFY(selected({4}));
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, rowPoint(1));
        QTRY_VERIFY(selected({1, 4}));
        QCOMPARE(session->selection().active, std::optional(id[1]));
        QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, rowPoint(3));
        QTRY_VERIFY(selected({1, 2, 3})); // anchor 1 to 3, replacing
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, rowPoint(0));
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier, rowPoint(1));
        QTRY_VERIFY(selected({0, 1}));
        // Drag selects a block.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, rowPoint(2));
        QTest::mouseMove(window, rowPoint(4));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, rowPoint(4));
        QTRY_VERIFY(selected({2, 3, 4}));

        // Moving the active Line commits a pending draft first (accepted policy).
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("a"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 1);
        QTest::keyClick(window, 'Z');
        QTRY_VERIFY(session->draftLine().has_value());
        grid->forceActiveFocus();
        press(Qt::Key_Down, Qt::ShiftModifier);
        QVERIFY(!session->draftLine());
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->document().lines()[0]->text.data())),
                 QStringLiteral("aZ"));
        QVERIFY(selected({0, 1}));
    }

    // G10: the History window and Undo to last save.
    void historyWindowJumpsBetweenSteps()
    {
        // Its own file: this test saves.
        const QString path = writeFile(dir, "history.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,first\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n");
        QVERIFY(application->openFile(path));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home); // "first"
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        const auto commitText = [&](const char *suffix) {
            text->forceActiveFocus();
            text->setProperty("cursorPosition", text->property("text").toString().size());
            for (const char *c = suffix; *c; ++c)
                QTest::keyClick(window, *c);
            QTRY_VERIFY(text->property("text").toString().endsWith(QLatin1String(suffix)));
            press(Qt::Key_Return, Qt::ControlModifier); // commit, staying on the Line
        };
        commitText("1");
        QVERIFY(application->editor().save());
        application->waitForWrites();
        commitText("2");
        auto &editor = application->editor();
        QCOMPARE(editor.history().size(), qsizetype(3));
        QCOMPARE(editor.history().value(1), QStringLiteral("Edit Line, active line 1"));
        QCOMPARE(editor.historyCursor(), 2);
        QVERIFY(editor.canUndoToLastSave());

        // Ctrl+Shift+H opens the window with the current step selected.
        press(Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
        auto *history = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("historyWindow"));
        QVERIFY(history);
        QTRY_VERIFY(history->isVisible());
        auto *list = history->findChild<QQuickItem *>(QStringLiteral("historyList"));
        QVERIFY(list);
        QCOMPARE(list->property("currentIndex").toInt(), 2);
        QCOMPARE(history->title(), QStringLiteral("History (3 elements)"));
        list->setProperty("currentIndex", 0);
        QVERIFY(QMetaObject::invokeMethod(history->findChild<QObject *>(QStringLiteral("historySet")), "clicked"));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        QVERIFY(history->isVisible()); // Set keeps the window open
        QVERIFY(QMetaObject::invokeMethod(history->findChild<QObject *>(QStringLiteral("historyCancel")), "clicked"));
        QTRY_VERIFY(!history->isVisible());

        // Undo to last save: forward to the saved step; redo steps stay.
        QVERIFY(editor.undoToLastSave());
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first1"));
        QVERIFY(!editor.dirty());
        QVERIFY(!editor.canUndoToLastSave());
        QVERIFY(editor.redo());
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first12"));
    }

    // G3: the Grid context menu, Ctrl+D and Shift+Delete.
    void gridMenuInsertsDuplicatesAndDeletes()
    {
        const QString path = writeFile(dir, "g3.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,b\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto texts = [&] {
            QStringList out;
            for (const auto *l : session->document().lines())
                out << QString::fromUtf8(reinterpret_cast<const char *>(l->text.data()), qsizetype(l->text.size()));
            return out;
        };
        auto *grid = item("editingGrid");
        grid->forceActiveFocus();
        press(Qt::Key_Home);
        // Right click opens the menu; Insert after fills the gap up to b.
        const QPoint centre = grid->mapToScene(QPointF(grid->width() / 2, 30)).toPoint();
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centre);
        auto *menu = grid->findChild<QObject *>(QStringLiteral("gridMenu"));
        QVERIFY(menu);
        QTRY_VERIFY(menu->property("visible").toBool());
        auto *insertAfter = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("insertAfter"));
        QVERIFY(insertAfter);
        QVERIFY(QMetaObject::invokeMethod(insertAfter, "triggered"));
        QTRY_COMPARE(texts(), (QStringList{"a", "", "b"}));
        QCOMPARE(session->document().lines()[1]->start.value.microseconds(), 2'000'000);
        QCOMPARE(session->document().lines()[1]->end.value.microseconds(), 5'000'000);
        QCOMPARE(item<QObject>("lineText")->property("text").toString(), QString()); // the new Line is shown
        // Ctrl+D in the Grid duplicates the selection; Shift+Delete deletes it.
        grid->forceActiveFocus();
        press(Qt::Key_End);
        press(Qt::Key_D, Qt::ControlModifier);
        QTRY_COMPARE(texts(), (QStringList{"a", "", "b", "b"}));
        press(Qt::Key_Delete, Qt::ShiftModifier);
        QTRY_COMPARE(texts(), (QStringList{"a", "", "b"}));
        QVERIFY(application->editor().undo()); // through the editor, so the views follow
        QCOMPARE(texts(), (QStringList{"a", "", "b", "b"}));
        // G5: F4 merges the active Line with the one before it; the menu joins.
        grid->forceActiveFocus();
        press(Qt::Key_End);
        press(Qt::Key_F4);
        QTRY_COMPARE(texts(), (QStringList{"a", "", "b\\Nb"}));
        press(Qt::Key_Home);
        press(Qt::Key_End, Qt::ShiftModifier);
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("joinFirst")),
                                          "triggered"));
        QTRY_COMPARE(texts(), (QStringList{"a"}));
    }

    void editMenuSortsAllOrSelectedLines()
    {
        const QString path = writeFile(dir, "g6.ass",
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,b,0,0,0,,late\n"
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,c,0,0,0,,early\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,a,0,0,0,,middle\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto texts = [&] {
            QStringList out;
            for (const auto *l : session->document().lines())
                out << QString::fromUtf8(reinterpret_cast<const char *>(l->text.data()), qsizetype(l->text.size()));
            return out;
        };
        auto *root = engine->rootObjects().first();
        auto *byStart = root->findChild<QObject *>(QStringLiteral("sortAll_start"));
        QVERIFY(byStart);
        QVERIFY(QMetaObject::invokeMethod(byStart, "triggered"));
        QTRY_COMPARE(texts(), (QStringList{"early", "middle", "late"}));
        // The Grid shows the new order.
        auto *grid = item("editingGrid");
        grid->forceActiveFocus();
        press(Qt::Key_Home);
        QCOMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("early"));
        // Sort the first two rows by Actor (c, a): they swap; "late" stays.
        press(Qt::Key_Down, Qt::ShiftModifier);
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("sortSelected_actor")), "triggered"));
        QTRY_COMPARE(texts(), (QStringList{"middle", "early", "late"}));
    }

    void fpsWindowScalesEveryTime()
    {
        const QString path = writeFile(dir, "g11.ass", "Dialogue: 0,0:00:10.00,0:00:20.00,Default,,0,0,0,,x\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *fps = root->findChild<QQuickWindow *>(QStringLiteral("fpsWindow"));
        QVERIFY(fps);
        fps->show();
        auto *oldFps = fps->findChild<QObject *>(QStringLiteral("oldFps"));
        auto *newFps = fps->findChild<QObject *>(QStringLiteral("newFps"));
        QVERIFY(oldFps && newFps);
        oldFps->setProperty("editText", QStringLiteral("25"));
        newFps->setProperty("editText", QStringLiteral("50"));
        QVERIFY(QMetaObject::invokeMethod(fps->findChild<QObject *>(QStringLiteral("fpsOk")), "clicked"));
        QTRY_COMPARE(session->document().lines()[0]->start.value.microseconds(), 5'000'000);
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 10'000'000);
        QVERIFY(!fps->isVisible());
        // Text that is not a number keeps the window open and changes nothing.
        fps->show();
        newFps->setProperty("editText", QStringLiteral("."));
        QVERIFY(QMetaObject::invokeMethod(fps->findChild<QObject *>(QStringLiteral("fpsOk")), "clicked"));
        QVERIFY(fps->isVisible());
        QCOMPARE(session->document().lines()[0]->start.value.microseconds(), 5'000'000);
        fps->close();
    }

    void aFailedOpenPopsUpTheLogWindow()
    {
        auto *root = engine->rootObjects().first();
        auto *logWindow = root->findChild<QQuickWindow *>(QStringLiteral("logWindow"));
        QVERIFY(logWindow);
        QVERIFY(!logWindow->isVisible());
        QVERIFY(QMetaObject::invokeMethod(root, "openSubtitles", Q_ARG(QVariant, dir.filePath(QStringLiteral("none.ass")))));
        QTRY_VERIFY(logWindow->isVisible());
        QVERIFY(logWindow->findChild<QObject *>(QStringLiteral("logLastMessage"))
                    ->property("text").toString().contains(QStringLiteral("none.ass")));
        QVERIFY(QMetaObject::invokeMethod(logWindow->findChild<QObject *>(QStringLiteral("logClose")), "clicked"));
        QTRY_VERIFY(!logWindow->isVisible());
        // File > Show / Hide log window: the whole log.
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("logMenuItem")), "click"));
        QTRY_VERIFY(logWindow->isVisible());
        QVERIFY(logWindow->findChild<QObject *>(QStringLiteral("logHistory"))
                    ->property("text").toString().contains(QStringLiteral("none.ass")));
        application->log().close();
        // A video that cannot be opened is logged too.
        application->video().openVideo(dir.filePath(QStringLiteral("missing.mkv")));
        QTRY_VERIFY_WITH_TIMEOUT(application->log().lastMessage().startsWith(QStringLiteral("Video unavailable")), 20000);
        QVERIFY(application->log().shown());
        application->log().close();
    }

    void tagButtonsInsertTheirTags()
    {
        const QString path = writeFile(dir, "e2.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,def\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto &buttons = application->tagButtons();
        buttons.setCount(2);
        buttons.edit(0, QStringLiteral("Blur"), QStringLiteral("\\blur3"), 0);
        buttons.edit(1, QStringLiteral("Note"), QStringLiteral("{note}"), 2);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("abc"));
        application->editor().setShowTags(true);
        text->forceActiveFocus();
        QMetaObject::invokeMethod(text, "select", Q_ARG(int, 0), Q_ARG(int, 3));
        QQuickItem *blur = nullptr;
        QTRY_VERIFY((blur = visualItem("tagButton0")));
        QCOMPARE(blur->property("text").toString(), QStringLiteral("Blur"));
        QVERIFY(QMetaObject::invokeMethod(blur, "click"));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\blur3}abc{\\blur0}"));
        // Plain text with both Lines selected: into each at the caret, one step.
        QVERIFY(application->editor().commit());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_A, Qt::ControlModifier);
        const auto steps = session->historySize();
        text->setProperty("cursorPosition", 0);
        QVERIFY(QMetaObject::invokeMethod(visualItem("tagButton1"), "click"));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Editing multiple lines"));
        const auto lines = session->document().lines();
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(lines[1]->text.data()), qsizetype(lines[1]->text.size())),
                 QStringLiteral("{note}def"));
    }

    // E1: a control inside a dialog (popups live in the overlay).
    QQuickItem *dialogItem(const char *dialog, const char *name) const
    {
        auto *popup = engine->rootObjects().first()->findChild<QObject *>(QLatin1String(dialog));
        auto *content = popup ? popup->property("contentItem").value<QQuickItem *>() : nullptr;
        return content ? findItem(content, QLatin1String(name)) : nullptr;
    }

    void fontDialogTagsTheSelectionAndCancelTakesItBack()
    {
        const QString path = dir.filePath(QStringLiteral("font.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[V4+ Styles]\n"
                    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                    "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                    "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                    "10,10,10,1\n"
                    "[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,def\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("abc"));
        auto *dialog = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("fontDialog"));
        QVERIFY(dialog);

        // Bold on the selection with tags hidden: the tag, and the Style's
        // value after it, in the raw text; the field still shows "abc".
        text->forceActiveFocus();
        QMetaObject::invokeMethod(text, "select", Q_ARG(int, 0), Q_ARG(int, 3));
        QVERIFY(QMetaObject::invokeMethod(visualItem("changeFont"), "click"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialogItem("fontDialog", "fontName")->property("text").toString(), QStringLiteral("Arial"));
        QCOMPARE(dialogItem("fontDialog", "fontSize")->property("text").toString(), QStringLiteral("20"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontBold"), "click"));
        QTRY_COMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->draftText().value_or(u8"").c_str())),
                     QStringLiteral("{\\b1}abc{\\b0}"));
        QCOMPARE(text->property("text").toString(), QStringLiteral("abc"));
        application->editor().setShowTags(true);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\b1}abc{\\b0}"));
        // A later change in the same dialog replaces the tag in place.
        dialogItem("fontDialog", "fontSize")->setProperty("text", QStringLiteral("30"));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\fs30\\b1}abc{\\fs20\\b0}"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!dialog->property("visible").toBool());

        // Cancel takes the dialog's changes back and leaves no draft behind.
        QVERIFY(application->editor().commit());
        const auto steps = session->historySize();
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        QVERIFY(QMetaObject::invokeMethod(visualItem("changeFont"), "click"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialogItem("fontDialog", "fontSize")->property("text").toString(), QStringLiteral("30"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontItalic"), "click"));
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\i1")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\fs30\\b1}abc{\\fs20\\b0}"));
        QVERIFY(!session->draftLine());
        QCOMPARE(session->historySize(), steps);
    }

    void colourPickerSetsColourAndAlphaAndRemembersIt()
    {
        QVERIFY(application->openFile(episode)); // "first" and "second", no Styles
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        application->editor().setShowTags(true);
        auto *dialog = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("colourDialog"));
        QVERIFY(dialog);
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        QVERIFY(QMetaObject::invokeMethod(visualItem("changeColour3"), "click"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("red").toInt(), 255); // white without a Style
        QVERIFY(QMetaObject::invokeMethod(dialog, "setRgb", Q_ARG(QVariant, 255), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\3c&H0000FF&}first"));
        auto *alpha = dialogItem("colourDialog", "alpha");
        QVERIFY(alpha);
        alpha->setProperty("value", 128);
        QVERIFY(QMetaObject::invokeMethod(alpha, "valueModified"));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\3a&H80&\\3c&H0000FF&}first"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        const auto recent = application->colourPicker().recent();
        QCOMPARE(recent.first().toMap().value(QStringLiteral("r")).toInt(), 255);
        QCOMPARE(recent.first().toMap().value(QStringLiteral("a")).toInt(), 128);
        QCOMPARE(application->colourPicker().storeToString().section(QLatin1Char(' '), 0, 0), QStringLiteral("&H800000FF&"));

        // Several Lines: each change is one step on all of them; Cancel undoes it.
        QVERIFY(application->editor().commit());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_A, Qt::ControlModifier);
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(visualItem("changeColour1"), "click"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "setRgb", Q_ARG(QVariant, 0), Q_ARG(QVariant, 255), Q_ARG(QVariant, 0)));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Editing multiple lines"));
        const auto lines = session->document().lines();
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(lines[1]->text.data()), qsizetype(lines[1]->text.size())),
                 QStringLiteral("{\\1c&H00FF00&}second"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_COMPARE(session->historyCursor(), steps - 1);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->document().lines()[1]->text.data())),
                 QStringLiteral("second"));
    }

    void pasteTranslationAndTheShiftingWindow()
    {
        const QString path = dir.filePath(QStringLiteral("tl.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                    "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                    "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                    "10,10,10,1\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n"
                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,C\n");
        }
        const QString tl = dir.filePath(QStringLiteral("tl.txt"));
        {
            QFile f(tl);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("a\nb\nc\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        QVERIFY(application->canPasteTranslation());
        QVERIFY(!application->canShiftTranslation());
        QVERIFY(application->pasteTranslationFile(QUrl::fromLocalFile(tl)));
        QCOMPARE(session->history().back().name, std::string("Pasting translation"));
        QTRY_VERIFY(application->editor().translationMode());
        QVERIFY(application->canShiftTranslation());
        const auto tr = [&](int row) {
            const auto &t = session->document().lines()[static_cast<std::size_t>(row)]->translation;
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        QCOMPARE(tr(0), QStringLiteral("a"));
        QCOMPARE(tr(2), QStringLiteral("c"));

        // The window's "Delete line" (Translation) on the second Line.
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Down);
        QTRY_COMPARE(session->selection().active, session->document().lines()[1]->id);
        auto *window = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("translationShiftWindow"));
        QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(window, "show"));
        auto *button = findItem(window->contentItem(), QStringLiteral("translationMove0"));
        QVERIFY(button);
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(button, "click"));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Moving translation text"));
        QCOMPARE(tr(1), QStringLiteral("c"));
        QVERIFY(QMetaObject::invokeMethod(window, "close"));
        // One Undo takes the move back.
        QVERIFY(session->undo());
        QCOMPARE(tr(1), QStringLiteral("b"));
    }

    // D1: View > Panels floats, docks, hides and shows panels; a draft and
    // the editing target survive, and F6 reaches a floating panel.
    void panelsFloatDockHideAndKeepTheirState()
    {
        QVERIFY(application->openFile(episode));
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        press(Qt::Key_End);
        press(Qt::Key_Exclam, Qt::ShiftModifier);
        QTRY_VERIFY(session->draftLine());
        auto *root = engine->rootObjects().first();
        auto *editorDock = root->findChild<QObject *>(QStringLiteral("editorDock"));
        auto *gridDock = root->findChild<QObject *>(QStringLiteral("gridDock"));
        auto menuItem = [&](const char *name) {
            auto *found = root->findChild<QObject *>(QLatin1String(name));
            if (!found)
                for (QObject *o : root->findChildren<QObject *>())
                    if (o->objectName() == QLatin1String(name))
                        found = o;
            return found;
        };

        QObject *floatEditor = menuItem("panelFloatEditor");
        QVERIFY(floatEditor);
        QVERIFY(QMetaObject::invokeMethod(floatEditor, "triggered"));
        QTRY_VERIFY(editorDock->property("isFloating").toBool());
        QTRY_VERIFY(item("editorPanel")->window() != window); // its own floating window
        QCOMPARE(text->property("text").toString(), QStringLiteral("first!"));
        QVERIFY(session->draftLine()); // rearranging does not commit the draft
        QVERIFY(application->workspace().editingTarget());

        QVERIFY(QMetaObject::invokeMethod(menuItem("panelDockEditor"), "triggered"));
        QTRY_VERIFY(!editorDock->property("isFloating").toBool());
        QTRY_COMPARE(item("editorPanel")->window(), window);
        QCOMPARE(text->property("text").toString(), QStringLiteral("first!"));

        // Hide closes the view only; Show brings it back focused.
        QVERIFY(QMetaObject::invokeMethod(menuItem("panelHideGrid"), "triggered"));
        QTRY_VERIFY(!gridDock->property("isOpen").toBool());
        QVERIFY(application->workspace().editingTarget());
        QVERIFY(QMetaObject::invokeMethod(menuItem("panelShowGrid"), "triggered"));
        QTRY_VERIFY(gridDock->property("isOpen").toBool());
        QTRY_VERIFY(item("gridPanel")->hasActiveFocus());
        QCOMPARE(item("editingGrid")->property("model").value<QAbstractItemModel *>()->rowCount(), 2);
        application->editor().discard();
    }

    // D1: F6 and the Classic shortcuts reach a floating panel. They need the
    // platform to activate the floating window (X without a window manager does not).
    void f6AndShortcutsReachAFloatingPanel()
    {
        QVERIFY(application->openFile(episode));
        auto *root = engine->rootObjects().first();
        auto *editorDock = root->findChild<QObject *>(QStringLiteral("editorDock"));
        QVERIFY(editorDock->setProperty("isFloating", true));
        QTRY_VERIFY(item("editorPanel")->window() != window);
        QWindow *floating = item("editorPanel")->window();
        // F6 order: Video, Audio, Editor, Grid; back from the Grid is the floating Editor.
        item("editingGrid")->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(root, "cyclePanels", Q_ARG(QVariant, -1)));
        if (!QTest::qWaitForWindowActive(floating, 3000))
            QSKIP("The platform did not activate the floating panel's window (no window manager)");
        if (!QTest::qWaitFor([&] { return item("editorPanel")->hasActiveFocus(); }, 5000)) {
            // Without a window manager X11 may hand activation back to the main window.
            if (QGuiApplication::focusWindow() != floating)
                QSKIP("The platform moved activation away from the floating panel's window (no window manager)");
            auto *quick = qobject_cast<QQuickWindow *>(floating);
            const auto *focused = quick ? quick->activeFocusItem() : nullptr;
            QFAIL(qPrintable(QStringLiteral("The floating window is active but focus is on %1")
                                 .arg(focused ? focused->metaObject()->className() + QStringLiteral(" ") + focused->objectName()
                                              : QStringLiteral("nothing"))));
        }
        QTRY_VERIFY(root->property("floatingPanelActive").toBool());
        auto *history = root->findChild<QQuickWindow *>(QStringLiteral("historyWindow"));
        QVERIFY(history && !history->isVisible());
        QTest::keyClick(floating, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(history->isVisible());
        history->close();
    }

    // D1: the keyboard placement window expresses drag placements and resizes.
    void placementWindowMovesTabsAndResizes()
    {
        auto *root = engine->rootObjects().first();
        auto *placement = root->findChild<QQuickWindow *>(QStringLiteral("placementWindow"));
        QVERIFY(placement);
        auto *audioDock = root->findChild<QObject *>(QStringLiteral("audioDock"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("movePanel")), "triggered"));
        QTRY_VERIFY(placement->isVisible());
        auto *panelBox = findItem(placement->contentItem(), QStringLiteral("placementPanel"));
        auto *kindBox = findItem(placement->contentItem(), QStringLiteral("placementKind"));
        auto *targetBox = findItem(placement->contentItem(), QStringLiteral("placementTarget"));
        QVERIFY(panelBox && kindBox && targetBox);
        panelBox->setProperty("currentIndex", 1);  // Audio
        kindBox->setProperty("currentIndex", 1);   // Left of
        targetBox->setProperty("currentIndex", 3); // Grid
        QVERIFY(QMetaObject::invokeMethod(findItem(placement->contentItem(), QStringLiteral("placementMove")), "click"));
        // Audio sits left of the Grid, in the Grid's row.
        auto *audio = item("audioPanel");
        auto *gridPanel = item("gridPanel");
        QTRY_VERIFY(audio->mapToScene(QPointF(0, 0)).x() < gridPanel->mapToScene(QPointF(0, 0)).x());
        QCOMPARE(qRound(audio->mapToScene(QPointF(0, 0)).y()), qRound(gridPanel->mapToScene(QPointF(0, 0)).y()));
        QTRY_VERIFY(audio->hasActiveFocus()); // the moved panel keeps the focus

        // Numeric resize: wider by 60 px.
        const QSize before = application->workspaceLayout().panelSize(audioDock);
        QVERIFY(before.isValid());
        auto *width = findItem(placement->contentItem(), QStringLiteral("placementWidth"));
        auto *height = findItem(placement->contentItem(), QStringLiteral("placementHeight"));
        QCOMPARE(width->property("value").toInt(), before.width());
        width->setProperty("value", before.width() + 60);
        QVERIFY(QMetaObject::invokeMethod(findItem(placement->contentItem(), QStringLiteral("placementResize")), "click"));
        QTRY_COMPARE(application->workspaceLayout().panelSize(audioDock).width(), before.width() + 60);
        QCOMPARE(height->property("value").toInt(), application->workspaceLayout().panelSize(audioDock).height());

        // Tab with the Grid: both in one place.
        kindBox->setProperty("currentIndex", 0);
        QVERIFY(QMetaObject::invokeMethod(findItem(placement->contentItem(), QStringLiteral("placementMove")), "click"));
        QTRY_COMPARE(audio->mapToScene(QPointF(0, 0)), gridPanel->mapToScene(QPointF(0, 0)));
        placement->close();
    }

    // Y3: Subtitles > ASS file properties writes the changed fields as one step.
    void scriptPropertiesDialogWritesTheChangedFields()
    {
        const QString path = dir.filePath(QStringLiteral("props.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTitle: Show\nPlayResX: 640\nPlayResY: 360\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("scriptPropertiesDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("assProperties")), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialogItem("scriptPropertiesDialog", "propTitle")->property("text").toString(), QStringLiteral("Show"));
        QCOMPARE(dialogItem("scriptPropertiesDialog", "propWidth")->property("value").toInt(), 640);
        auto *translator = dialogItem("scriptPropertiesDialog", "propTranslator");
        translator->setProperty("text", QStringLiteral("Someone"));
        QVERIFY(QMetaObject::invokeMethod(translator, "textEdited"));
        auto *width = dialogItem("scriptPropertiesDialog", "propWidth");
        width->setProperty("value", 1280);
        QVERIFY(QMetaObject::invokeMethod(width, "valueModified"));
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Changing the subtitle header"));
        const auto &d = session->document();
        QCOMPARE(d.scriptInfo(u8"Original Translation"), std::optional<std::u8string>(u8"Someone"));
        QCOMPARE(d.scriptInfo(u8"PlayResX"), std::optional<std::u8string>(u8"1280"));
        QCOMPARE(d.scriptInfo(u8"PlayResY"), std::optional<std::u8string>(u8"360")); // height not edited
        QCOMPARE(d.scriptInfo(u8"Title"), std::optional<std::u8string>(u8"Show"));
        application->editor().discard();
    }

    // Y1/Y2: the Style manager edits the Document's Styles and the catalog.
    void styleManagerEditsStylesAndTheCatalog()
    {
        const QString path = dir.filePath(QStringLiteral("styles.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
                    "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                    "MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
                    "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n\n"
                    "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,a\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto &styles = application->styleManager();
        auto *root = engine->rootObjects().first();
        auto *window = root->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("styleManagerMenuItem")), "triggered"));
        QTRY_VERIFY(window->isVisible());
        // The active Line's Style is selected.
        QCOMPARE(window->property("assSelected").toList(), QVariantList{1});
        QCOMPARE(styles.documentStyles(), (QStringList{QStringLiteral("Default"), QStringLiteral("Sign")}));
        // Edit Sign: rename it to Title, answer Yes to renaming the Lines.
        auto values = styles.beginEdit(false, 1);
        values.insert(QStringLiteral("name"), QStringLiteral("Title"));
        values.insert(QStringLiteral("fontsize"), QStringLiteral("36"));
        QCOMPARE(styles.commitQuestions(values).value(QStringLiteral("rename")).toBool(), true);
        QCOMPARE(styles.commitEdit(values, {1}, false, true), QString());
        QCOMPARE(session->history().back().name, std::string("Style editing"));
        QCOMPARE(session->document().lines()[0]->style, std::u8string(u8"Title"));
        // A new Style with a taken name is refused with the legacy message.
        auto fresh = styles.beginNew(false);
        QCOMPARE(fresh.value(QStringLiteral("name")).toString(), QStringLiteral("New Style"));
        fresh.insert(QStringLiteral("name"), QStringLiteral("Default"));
        QCOMPARE(styles.commitEdit(fresh, {}, false, false), QStringLiteral("Style named \"Default\" already exists."));
        styles.endEdit();
        // Into the catalog and back: Title is copied to a new catalog, then Clear
        // removes the unused Default from the Document.
        QVERIFY(styles.createCatalog(QStringLiteral("Show")));
        QCOMPARE(styles.transferConflicts(true, {1}), QStringList());
        QCOMPARE(styles.addToStore({1}, {}), QVariantList{0});
        QCOMPARE(styles.storeStyles(), QStringList{QStringLiteral("Title")});
        QVERIFY(styles.cleanStyles().contains(QStringLiteral("Styles deleted:\nDefault\n")));
        QCOMPARE(styles.documentStyles(), QStringList{QStringLiteral("Title")});
        // Adding it back asks; "no" keeps the Document's.
        QCOMPARE(styles.transferConflicts(false, {0}), QStringList{QStringLiteral("Title")});
        styles.addToDocument({0}, {QStringLiteral("no")});
        QCOMPARE(styles.documentStyles(), QStringList{QStringLiteral("Title")});
        // The preview renders the edited values through libass.
        styles.beginEdit(false, 0);
        styles.renderPreview(styles.beginEdit(false, 0), 200, 80, QStringLiteral("Preview"));
        QCOMPARE(styles.preview().size(), QSize(200, 80));
        styles.endEdit();
        QVERIFY(QMetaObject::invokeMethod(window, "closeManager"));
        QTRY_VERIFY(!window->isVisible());
        styles.deleteCatalog(QStringLiteral("Show"));
        application->editor().discard();
    }

    // Y5: Convert to SRT previews its losses, applies the plan as one step,
    // refuses a stale plan, and Save then asks for a file.
    void conversionPreviewsAndAppliesThePlan()
    {
        const QString path = dir.filePath(QStringLiteral("convert.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,note\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\i1}two{\\i0}\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        QCOMPARE(application->conversionTargets(),
                 (QStringList{QStringLiteral("srt"), QStringLiteral("mdvd"), QStringLiteral("mpl2"), QStringLiteral("tmp")}));
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("conversionDialog"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QStringLiteral("srt")),
                                          Q_ARG(QVariant, QStringLiteral("Convert to SRT"))));
        QTRY_VERIFY(dialog->property("visible").toBool());
        const QStringList losses = dialog->property("preview").toMap().value(QStringLiteral("losses")).toStringList();
        QVERIFY2(losses.contains(QStringLiteral("1 comment line(s) removed.")) ||
                     losses.join(u'|').contains(QStringLiteral("comment")),
                 qPrintable(losses.join(u'|')));
        QVERIFY(losses.join(u'|').contains(QStringLiteral("script properties")));
        // A Document changed after the preview refuses the plan.
        QVERIFY(application->selectLines({{QStringLiteral("find"), QStringLiteral("two")}, {QStringLiteral("field"), 0},
                                          {QStringLiteral("mode"), 0}, {QStringLiteral("action"), 5}, {QStringLiteral("comments"), false}},
                                         false)
                    .startsWith(QStringLiteral("1 ")));
        QVERIFY(!application->acceptConversion());
        QVERIFY(application->editor().undo());
        QVERIFY(QMetaObject::invokeMethod(dialog, "refresh"));
        const auto cursor = session->historyCursor();
        QVERIFY(QMetaObject::invokeMethod(dialogItem("conversionDialog", "conversionAccept"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(session->historyCursor(), cursor + 1);
        QCOMPARE(session->history().back().name, std::string("Subtitles conversion"));
        QCOMPARE(session->document().format(), core::SubtitleFormat::Srt);
        QCOMPARE(session->document().lines().size(), std::size_t(1));
        const auto &text = session->document().lines()[0]->text;
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size())), QStringLiteral("<i>two</i>"));
        QCOMPARE(application->saveRoute(), QStringLiteral("dialog")); // the file is still .ass
        QVERIFY(application->editor().undo());
        QCOMPARE(session->document().format(), core::SubtitleFormat::Ass);
        application->editor().discard();
    }

    // Y4: a video of another size offers to match it; Resample subtitles changes it.
    void resolutionMismatchAndResample()
    {
        const QString path = dir.filePath(QStringLiteral("resample.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nPlayResX: 100\nPlayResY: 100\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(10,10)}a\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *mismatch = root->findChild<QObject *>(QStringLiteral("mismatchDialog"));
        QVERIFY(mismatch);
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(mismatch->property("visible").toBool(), 20000);
        const auto values = application->resampleValues();
        const int width = values.value(QStringLiteral("videoWidth")).toInt();
        const int height = values.value(QStringLiteral("videoHeight")).toInt();
        QVERIFY(width > 0 && height > 0);
        QVERIFY(dialogItem("mismatchDialog", "mismatchText")->property("text").toString().contains(
            QStringLiteral("Video resolution: %1 x %2\nSubtitle resolution: 100 x 100").arg(width).arg(height)));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("mismatchDialog", "mismatchChange"), "click"));
        QTRY_VERIFY(!mismatch->property("visible").toBool());
        QCOMPARE(session->history().back().name, std::string("Changing subtitles resolution"));
        QCOMPARE(session->document().scriptInfo(u8"PlayResX"), std::optional(std::u8string(
            reinterpret_cast<const char8_t *>(QByteArray::number(width).constData()))));
        const auto &text = session->document().lines()[0]->text;
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size())),
                 QStringLiteral("{\\pos(%1,%2)}a").arg(width / 10).arg(height / 10));
        // Resample subtitles back to 100x100 through the dialog.
        auto *resample = root->findChild<QObject *>(QStringLiteral("resampleDialog"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("resampleMenuItem")), "triggered"));
        QTRY_VERIFY(resample->property("visible").toBool());
        QCOMPARE(dialogItem("resampleDialog", "resampleSubsWidth")->property("value").toInt(), width);
        dialogItem("resampleDialog", "resampleWidth")->setProperty("value", 100);
        dialogItem("resampleDialog", "resampleHeight")->setProperty("value", 100);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("resampleDialog", "resampleOk"), "click"));
        QTRY_VERIFY(!resample->property("visible").toBool());
        QCOMPARE(session->document().scriptInfo(u8"PlayResY"), std::optional(std::u8string(u8"100")));
        // "Disable warning" stops the question.
        application->setAskForBadResolution(false);
        QVERIFY(!application->askForBadResolution());
        application->editor().discard();
    }

    // P8: Help > About and Credits show the legacy notices with this build's version.
    void aboutAndCreditsShowTheNotices()
    {
        auto *root = engine->rootObjects().first();
        auto *about = root->findChild<QObject *>(QStringLiteral("aboutDialog"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("aboutMenuItem"))->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(about->property("visible").toBool());
        const QString text = dialogItem("aboutDialog", "aboutText")->property("text").toString();
        QVERIFY2(text.contains(QStringLiteral("version %1").arg(application->updates().version())), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Based on Kainote by Marcin Drob")));
        QVERIFY(text.contains(QStringLiteral("Libass - Copyright")));
        QVERIFY(QMetaObject::invokeMethod(about, "close"));
        auto *credits = root->findChild<QObject *>(QStringLiteral("creditsDialog"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("creditsMenuItem"))->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(credits->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(credits, "close"));
    }

    // P7: the legacy Save routes: the video name, the extension, Save
    // translation, Save all and read-only files.
    void saveVariantsFollowTheLegacySave()
    {
        const QString path = dir.filePath(QStringLiteral("variants.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,original\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,translated\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        QCOMPARE(application->saveRoute(), QString());
        // With the video name, a differently named Document asks, starting at the video.
        application->setSaveWithVideoName(true);
        QCOMPARE(application->saveRoute(), QString()); // no video yet
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->exactTimebase(), 20000);
        QCOMPARE(application->saveRoute(), QStringLiteral("dialog"));
        const auto values = application->saveDialogValues();
        QCOMPARE(values.value(QStringLiteral("file")).toUrl(),
                 QUrl::fromLocalFile(QFileInfo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv")).absolutePath() + QStringLiteral("/cfr")));
        QCOMPARE(values.value(QStringLiteral("filter")).toString(), QStringLiteral("Subtitle file (*.ass)"));
        application->setSaveWithVideoName(false);
        // Save translation: translator mode off as one step, then the chosen
        // file gets the extension.
        const auto steps = session->historySize();
        QVERIFY(application->turnOffTranslationMode());
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Turning off translator mode"));
        QVERIFY(!application->turnOffTranslationMode());
        QCOMPARE(application->saveChosen(QUrl::fromLocalFile(dir.filePath(QStringLiteral("translation")))), QString());
        QTRY_VERIFY(!session->isDirty());
        QFile written(dir.filePath(QStringLiteral("translation.ass")));
        QVERIFY(written.open(QIODevice::ReadOnly));
        const QByteArray bytes = written.readAll();
        QVERIFY2(bytes.contains(",translated") && !bytes.contains("TLMode:") && !bytes.contains(",original"), bytes.constData());
        written.close(); // Windows cannot replace a file that is open
        // Save all writes the modified Document to its file.
        QVERIFY(application->selectLines({{QStringLiteral("find"), QString()}, {QStringLiteral("with"), false},
                                          {QStringLiteral("field"), 0}, {QStringLiteral("mode"), 0},
                                          {QStringLiteral("action"), 5}},
                                         false)
                    .startsWith(QStringLiteral("1 ")));
        QVERIFY(session->isDirty());
        QVERIFY(!application->saveAll());
        QTRY_VERIFY(!session->isDirty());
        QVERIFY(written.open(QIODevice::ReadOnly));
        QVERIFY(written.readAll().contains("Comment: 0,0:00:01.00"));
        // A read-only file is refused and asked again.
        const QString locked = dir.filePath(QStringLiteral("locked.ass"));
        {
            QFile f(locked);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ReadUser);
        if (QFileInfo(locked).isWritable()) // root (CI containers) writes read-only files
            QSKIP("Read-only files are writable for this user");
        QCOMPARE(application->saveChosen(QUrl::fromLocalFile(locked)), QStringLiteral("readonly"));
        QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    // F2: Edit > Select lines selects by the dialog's settings and reports the count.
    void selectLinesDialogSelectsAndActs()
    {
        const QString path = dir.filePath(QStringLiteral("select.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Alpha\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Sign,,0,0,0,,beta\n"
                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,ALPHABET\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("selectLinesDialog"));
        QVERIFY(dialog);
        auto *menuItem = root->findChild<QObject *>(QStringLiteral("selectLinesMenuItem"));
        QVERIFY(QMetaObject::invokeMethod(menuItem->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *find = dialogItem("selectLinesDialog", "selectFindText");
        QVERIFY(find);
        find->setProperty("editText", QStringLiteral("alpha"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("selectLinesDialog", "selectRun"), "click"));
        auto *result = root->findChild<QObject *>(QStringLiteral("selectResult"));
        QTRY_VERIFY(result->property("visible").toBool());
        QCOMPARE(result->property("text").toString(), QStringLiteral("2 lines selected."));
        const auto lines = session->document().lines();
        QCOMPARE(session->selection().selected, (std::set<core::LineId>{lines[0]->id, lines[2]->id}));
        QCOMPARE(session->selection().active, std::optional(lines[0]->id));
        QCOMPARE(dialog->property("recent").toStringList(), QStringList{QStringLiteral("alpha")});
        // F2-style-cancel: Cancel in "Choose styles" leaves the search as it is.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("selectLinesDialog", "selectChooseStyles"), "click"));
        auto *styles = root->findChild<QObject *>(QStringLiteral("selectStylesDialog"));
        QTRY_VERIFY(styles->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(styles, "reject"));
        QTRY_VERIFY(!styles->property("visible").toBool());
        QCOMPARE(find->property("editText").toString(), QStringLiteral("alpha"));
        QVERIFY(!dialogItem("selectLinesDialog", "selectRegex")->property("checked").toBool());
        // "Close" in the message closes the Select dialog too.
        QVERIFY(QMetaObject::invokeMethod(result, "accept"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        // Delete by style, as one "Selecting lines" step; Without and Comments too.
        const auto steps = session->historySize();
        const QString message = application->selectLines(
            {{QStringLiteral("find"), QStringLiteral("Sign")}, {QStringLiteral("field"), 1},
             {QStringLiteral("mode"), 0}, {QStringLiteral("action"), 6}},
            false);
        QCOMPARE(message, QStringLiteral("1 lines selected."));
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Selecting lines"));
        QCOMPARE(session->document().lines().size(), std::size_t(2));
        QCOMPARE(application->selectLinesSettings().value(QStringLiteral("recent")).toStringList(),
                 (QStringList{QStringLiteral("Sign"), QStringLiteral("alpha")}));
        // Copy puts the raw Lines on the clipboard.
        application->selectLines(
            {{QStringLiteral("find"), QStringLiteral("BET")}, {QStringLiteral("field"), 0}, {QStringLiteral("action"), 1}}, false);
        QCOMPARE(QGuiApplication::clipboard()->text(),
                 QStringLiteral("Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,ALPHABET\r\n"));
        application->editor().discard();
    }

    // F5: Ctrl+I opens the Timing tool; Shift moves the Lines; start-only asks first.
    void timingPanelShiftsTimes()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 and 3.00-4.00
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *timingDock = root->findChild<QObject *>(QStringLiteral("timingDock"));
        QVERIFY(timingDock);
        QVERIFY(!timingDock->property("isOpen").toBool()); // tools start closed
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_I, Qt::ControlModifier);
        QTRY_VERIFY(timingDock->property("isOpen").toBool());
        QTRY_VERIFY(item("timingPanel")->hasActiveFocus());
        // The panel starts from the legacy options: 2 s backward (SHIFT_TIMES_TIME
        // 2000, SHIFT_TIMES_OPTIONS unset), storing nothing until it changes.
        QCOMPARE(application->shiftTimesSettings().settings().timeMs, 2000);
        QVERIFY(item("shiftBackward")->property("checked").toBool());
        QVERIFY(!application->settingsStore()->contains("shiftTimes.time"));
        QVERIFY(!application->settingsStore()->contains("shiftTimes.options"));
        QVERIFY(QMetaObject::invokeMethod(item("shiftForward"), "click"));
        QVERIFY(application->shiftTimesSettings().settings().forward);
        auto *time = item("shiftTime");
        time->setProperty("text", QStringLiteral("0:00:01.50"));
        QVERIFY(QMetaObject::invokeMethod(time, "editingFinished"));
        QCOMPARE(application->shiftTimesSettings().settings().timeMs, 1500);
        QVERIFY(QMetaObject::invokeMethod(item("shiftApply"), "click"));
        QTRY_COMPARE(session->history().back().name, std::string("Shifting times"));
        auto ms = [&](std::size_t row, bool start) {
            const auto *l = session->document().lines()[row];
            return (start ? l->start : l->end).value.microseconds() / 1000;
        };
        QCOMPARE(ms(0, true), 2500);
        QCOMPARE(ms(1, false), 5500);
        // Start times only: confirmed first.
        auto settings = application->shiftTimesSettings().settingsMap();
        settings.insert(QStringLiteral("whichTimes"), 1);
        application->shiftTimesSettings().setSettingsMap(settings);
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(item("shiftApply"), "click"));
        auto *confirm = root->findChild<QObject *>(QStringLiteral("shiftConfirm"));
        QTRY_VERIFY(confirm->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(ms(0, true), 4000);
        QCOMPARE(ms(0, false), 3500); // the end stayed
        // A profile keeps the settings by name.
        application->shiftTimesSettings().saveProfile(QStringLiteral("Start only"));
        settings.insert(QStringLiteral("whichTimes"), 0);
        application->shiftTimesSettings().setSettingsMap(settings);
        application->shiftTimesSettings().loadProfile(QStringLiteral("Start only"));
        QCOMPARE(application->shiftTimesSettings().settings().whichTimes, 1);
        QCOMPARE(application->shiftTimesSettings().profileNames(), QStringList{QStringLiteral("Start only")});
        application->editor().discard();
    }

    // F6: with a real (indexed) video, Shift runs the postprocessor instead.
    void timingPanelRunsThePostprocessor()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 and 3.00-4.00
        auto *session = application->files().session(*application->workspace().editingTarget());
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->exactTimebase(), 20000);
        auto settings = application->shiftTimesSettings().settingsMap();
        settings.insert(QStringLiteral("postprocessor"), 16 | 1 | 2);
        settings.insert(QStringLiteral("leadIn"), 200);
        settings.insert(QStringLiteral("leadOut"), 300);
        settings.insert(QStringLiteral("timeMs"), 5000); // not applied while the postprocessor runs
        application->shiftTimesSettings().setSettingsMap(settings);
        QVERIFY(application->shiftTimes().isEmpty());
        QCOMPARE(session->history().back().name, std::string("Shifting times"));
        const auto *a = session->document().lines()[0];
        QCOMPARE(a->start.value.microseconds() / 1000, 800);
        QCOMPARE(a->end.value.microseconds() / 1000, 2300);
    }

    // F6: a keyframe file opened before the video applies when the video is ready.
    void keyframeFilesReplaceTheVideosKeyframes()
    {
        const QString kf = dir.filePath(QStringLiteral("kf.txt"));
        {
            QFile f(kf);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# keyframe format v1\nfps 25\n0\n10\n20\n");
        }
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openKeyframes(QUrl::fromLocalFile(kf)).isEmpty()); // kept until a video opens
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().keyframes() == (std::vector<int>{0, 10, 20}), 20000);
        const QString bad = dir.filePath(QStringLiteral("bad.txt"));
        {
            QFile f(bad);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("nothing here\n");
        }
        QCOMPARE(application->openKeyframes(QUrl::fromLocalFile(bad)), QStringLiteral("Invalid keyframes format"));
        QCOMPARE(application->video().session().keyframes(), (std::vector<int>{0, 10, 20}));
    }

    void hideColumnsMenuTogglesGridColumns()
    {
        QVERIFY(application->openFile(episode));
        auto *root = engine->rootObjects().first();
        auto *grid = item("editingGrid");
        auto *table = QAccessible::queryAccessibleInterface(grid)->tableInterface();
        QCOMPARE(table->columnCount(), 13);
        QObject *hideCps = nullptr;
        for (QObject *o : root->findChildren<QObject *>())
            if (o->objectName() == QLatin1String("hideColumn512"))
                hideCps = o;
        QVERIFY(hideCps);
        QVERIFY(QMetaObject::invokeMethod(hideCps, "triggered"));
        QCOMPARE(application->shell().hiddenColumns(), 512);
        QTRY_COMPARE(table->columnCount(), 12);
        QVERIFY(hideCps->property("checked").toBool());
        application->shell().setHiddenColumns(0);
    }

    void hiddenLinesLeaveTheGridAndMarksReopenThem()
    {
        const QString path = writeFile(dir, "g8.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,b\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,c\n");
        QVERIFY(application->openFile(path));
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        QVERIFY(grid);
        auto *table = QAccessible::queryAccessibleInterface(grid)->tableInterface();
        QCOMPARE(table->rowCount(), 3);
        grid->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Down); // b
        auto *root = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("hideSelectedLines")), "triggered"));
        QTRY_COMPARE(table->rowCount(), 2);
        QVERIFY(application->shell().filtered());
        // b stays selected, hidden; the active Line moved to a shown one.
        QVERIFY(application->shell().selectionStatus().contains(QStringLiteral("hidden")));
        QCOMPARE(grid->markWidth(), 11.0);
        // The + mark on the border below a: a click there reveals b.
        const double y = 2 * grid->property("rowHeight").toDouble(); // the header is one row high; below row 1
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, grid->mapToScene(QPointF(5, y)).toPoint());
        QTRY_COMPARE(table->rowCount(), 3);
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("turnOffFiltering")), "triggered"));
        QTRY_VERIFY(!application->shell().filtered());
        QCOMPARE(grid->markWidth(), 0.0);
    }

    void groupDescriptionsOpenCloseAndBreaksAreRefused()
    {
        const QString path = writeFile(dir, "g9.ass",
                                       "Dialogue: 0,0:00:04.50,0:00:10.00,Default,,0,0,0,,a\n"
                                       "Comment: 0,0:00:00.00,0:00:00.00,Default,[tree_description],0,0,0,,Signs\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,[tree_opened],0,0,0,,m1\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,[tree_opened],0,0,0,,m2\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        auto *table = QAccessible::queryAccessibleInterface(grid)->tableInterface();
        QCOMPARE(table->rowCount(), 4);
        // A click on the description row closes the group.
        const double rh = grid->property("rowHeight").toDouble();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, grid->mapToScene(QPointF(100, rh * 2.5)).toPoint());
        QTRY_COMPARE(table->rowCount(), 2);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, grid->mapToScene(QPointF(100, rh * 2.5)).toPoint());
        QTRY_COMPARE(table->rowCount(), 4);
        // Sorting by Start would orphan the members: refused, with the offer.
        auto *root = engine->rootObjects().first();
        auto *breakDialog = root->findChild<QQuickWindow *>(QStringLiteral("groupBreakDialog"));
        QVERIFY(breakDialog);
        const auto steps = session->historySize();
        QVERIFY(!application->sortLines(QStringLiteral("start"), false));
        QTRY_VERIFY(breakDialog->isVisible());
        QCOMPARE(session->historySize(), steps);
        // Remove group deletes the description; the sort then works (a lands between m1 and m2).
        QVERIFY(QMetaObject::invokeMethod(breakDialog->findChild<QObject *>(QStringLiteral("groupBreakRemove")), "clicked"));
        QTRY_COMPARE(table->rowCount(), 3);
        QVERIFY(application->sortLines(QStringLiteral("start"), false));
    }

    void enterOnTheLastLineAppendsOne()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End); // the last Line, "second" at 0:00:03-0:00:04
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("second"));
        text->forceActiveFocus();
        press(Qt::Key_Return);
        // Legacy SubsGrid::NextLine: a copy starting at its End, five seconds long.
        auto *grid = item("editingGrid");
        QTRY_COMPARE(grid->property("model").value<QAbstractItemModel *>()->rowCount(), 3);
        QCOMPARE(text->property("text").toString(), QString());
        QCOMPARE(item<QObject>("startField")->property("text").toString(), QStringLiteral("0:00:04.00"));
        QCOMPARE(item<QObject>("endField")->property("text").toString(), QStringLiteral("0:00:09.00"));
        press(Qt::Key_Z, Qt::ControlModifier); // its own undo step
        QTRY_COMPARE(grid->property("model").value<QAbstractItemModel *>()->rowCount(), 2);
    }

    void translationModeStacksOriginalAboveTranslated()
    {
        const QString path = dir.filePath(QStringLiteral("tl.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,Gate\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n");
        }
        QVERIFY(application->openFile(path));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Gate"));
        QVERIFY(translated->isVisible());
        QTRY_VERIFY(original->y() < translated->y()); // Original above Translated, once laid out
        translated->forceActiveFocus();
        for (char c : std::string("Brama"))
            QTest::keyClick(window, c);
        QTRY_COMPARE(translated->property("text").toString(), QStringLiteral("Brama"));
        press(Qt::Key_Return); // commit (the last Line: a new Line follows)
        QVERIFY(application->editor().save());
        application->waitForWrites();
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray bytes = saved.readAll();
        // The pair is written as the original line in the TLMode Style plus
        // the translation line.
        QVERIFY2(bytes.contains("Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,Gate\n"
                                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"),
                 bytes.constData());
    }

    // #101: the legacy translation-mode buttons.
    void translationButtonsFollowTheLegacyEditBox()
    {
        const QString path = dir.filePath(QStringLiteral("tl-buttons.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,Gate {\\i1}keeper\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n");
        }
        QVERIFY(application->openFile(path));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Gate keeper"));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto raw = [&](bool translation) {
            const auto r = session->draftRecord();
            const std::u8string t = r ? (translation ? r->translation : r->text) : std::u8string();
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        // Paste the selected: the Original's selection goes in at the Translated caret.
        QMetaObject::invokeMethod(original, "select", Q_ARG(int, 5), Q_ARG(int, 11));
        translated->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("pasteSelectionToTranslation"), "clicked"));
        QTRY_COMPARE(translated->property("text").toString(), QStringLiteral("keeper"));
        QCOMPARE(translated->property("cursorPosition").toInt(), 6);
        // Paste all: the Original's raw text, tags included.
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("pasteAllToTranslation"), "clicked"));
        QTRY_COMPARE(raw(true), QStringLiteral("Gate {\\i1}keeper"));
        // Comment out original wraps the raw Original in braces.
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("commentOutOriginal"), "clicked"));
        QTRY_COMPARE(raw(false), QStringLiteral("{Gate {\\i1}keeper}"));
    }

    // #101: Ctrl+, and Ctrl+. write the video time's distance from Start and End.
    void timeDifferenceMeasuresFromTheVideoFrame()
    {
        QVERIFY(application->openFile(episode)); // the first Line runs 1.00 to 2.00 s
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        press(Qt::Key_Comma, Qt::ControlModifier); // no video: refused
        QCOMPARE(text->property("text").toString(), QStringLiteral("first"));

        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        // The active Line's start frame: 24 at 1.001 s (24000/1001 fps).
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame() == std::optional<int>(24), 20000);
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        press(Qt::Key_Comma, Qt::ControlModifier);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("1first"));
        QCOMPARE(text->property("cursorPosition").toInt(), 1);
        QMetaObject::invokeMethod(text, "select", Q_ARG(int, 0), Q_ARG(int, 1));
        press(Qt::Key_Period, Qt::ControlModifier); // |1001 - 2000| replaces the selection
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("999first"));
    }

    void fpsFromVideoMovesTheSecondSelectedLineToTheVideoTime()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 s and 3.00-4.00 s
        auto *session = application->files().session(*application->workspace().editingTarget());
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame().has_value(), 20000);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_A, Qt::ControlModifier); // both selected
        auto shownMs = [&]() -> std::int64_t {
            const auto &video = application->video().session();
            const auto frame = video.shownFrame();
            const auto start = frame ? video.frameStart(*frame) : std::nullopt;
            return start ? start->microseconds() / 1000 : -1;
        };
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(shownMs() - 1000) < 50, 20000); // the first Line's frame
        const auto videoMs = shownMs();
        auto *menuItem = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("setFpsFromVideo"));
        QVERIFY(menuItem);
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        QTRY_COMPARE(session->document().lines()[1]->start.value.microseconds() / 1000, videoMs);
        QCOMPARE(session->document().lines()[0]->start.value.microseconds(), 1'000'000); // the first stays
    }

    void splitAtVideoTimeUsesTheShownFramesEndTime()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 s and 3.00-4.00 s
        auto *session = application->files().session(*application->workspace().editingTarget());
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        // The first Line's frame: 24 at 1001 ms; the next starts at 1042 ms.
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame() == std::optional<int>(24), 20000);
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("splitAtVideoTime")),
                                          "triggered"));
        QTRY_COMPARE(session->document().lines().size(), std::size_t(3));
        // EndTimeFor(24) = 1001 + (1042 - 1001) / 2 + 5 = 1026, to centiseconds 1020.
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 1'020'000);
        QCOMPARE(session->document().lines()[1]->start.value.microseconds(), 1'020'000);
        QCOMPARE(session->document().lines()[1]->end.value.microseconds(), 2'000'000);
    }

    void splitIntoCharactersMakesALinePerCharacter()
    {
        QVERIFY(application->openFile(episode)); // "first" and "second"
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("splitIntoCharacters")),
                                          "triggered"));
        QTRY_COMPARE(session->document().lines().size(), std::size_t(6));
        const auto lines = session->document().lines();
        QVERIFY(lines[0]->text.starts_with(u8"{\\pos("));
        QVERIFY(lines[4]->text.ends_with(u8"}t"));
        QCOMPARE(lines[5]->text, std::u8string(u8"second"));
        QCOMPARE(session->history().back().name, std::string("Splitting lines"));
    }

    void editorShortcutsFollowTheLegacyDefaults()
    {
        const QString path = dir.filePath(QStringLiteral("keys.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTLMode: Yes\nTLMode Style: O\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,,one two\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,jeden\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,three\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,\n");
        }
        QVERIFY(application->openFile(path));
        auto &editor = application->editor();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *original = item("lineText");
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("one two"));
        auto *session = application->files().session(*application->workspace().editingTarget());

        // Shift+Enter: a hard break replaces the space at the caret.
        original->forceActiveFocus();
        original->setProperty("cursorPosition", 4);
        press(Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->draftText()->c_str())),
                 QStringLiteral("one\\Ntwo"));
        QCOMPARE(original->property("text").toString(), QStringLiteral("one\ntwo"));

        // Ctrl+R: the next untranslated Line is the second pair.
        press(Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("three"));
        // Alt+Down: Unconfirmed on, then the next Line (appended after the last).
        press(Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(session->document().lines()[1]->unconfirmed, true);
        QCOMPARE(session->document().lines().size(), 3u);
        // Ctrl+D finds it again.
        press(Qt::Key_D, Qt::ControlModifier);
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("three"));
        QVERIFY(editor.problem().isEmpty());
    }

    void enterOnTheLastMicroDvdLineCountsFrames()
    {
        for (const bool withRate : {true, false}) {
            const QString path = dir.filePath(withRate ? QStringLiteral("rate.sub") : QStringLiteral("norate.sub"));
            {
                QFile f(path);
                QVERIFY(f.open(QIODevice::WriteOnly));
                f.write("{10}{20}abc|def\n{30}{40}x\n");
            }
            QVERIFY(application->openFile(path));
            auto *session = application->files().session(*application->workspace().editingTarget());
            if (withRate)
                QVERIFY(session->run(application::Command{"Set frame rate", session->revision(), {},
                                                          [](core::Document &d) {
                                                              d.setFrameRate(*core::FrameRate::make(24000, 1001));
                                                              return true;
                                                          }}));
            application->shell().refresh(&session->document(), nullptr);
            item("editingGrid")->forceActiveFocus();
            press(Qt::Key_End);
            auto *text = item("lineText");
            QTRY_COMPARE(text->property("text").toString(), QStringLiteral("x"));
            text->forceActiveFocus();
            press(Qt::Key_Return);
            QTRY_COMPARE(session->document().lines().size(), 3u);
            QVERIFY(application->editor().save());
            application->waitForWrites();
            QFile saved(path);
            QVERIFY(saved.open(QIODevice::ReadOnly));
            // Legacy capture 37034136343 appended {40}{160} at its 23.976 fps.
            QCOMPARE(saved.readAll(), withRate ? QByteArray("{10}{20}abc|def\n{30}{40}x\n{40}{160}\n")
                                               : QByteArray("{10}{20}abc|def\n{30}{40}x\n{40}{}\n"));
            QVERIFY(application->closeEditingTarget());
        }
    }

    // O1: File > Settings, the legacy Options dialog over the settings registry.
    QQuickItem *settingsButton(const char *name) const
    {
        auto *popup = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsDialog"));
        auto *footer = popup ? popup->property("footer").value<QQuickItem *>() : nullptr;
        return footer ? findItem(footer, QLatin1String(name)) : nullptr;
    }
    QObject *openSettings()
    {
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("settingsDialog"));
        auto *menuItem = root->findChild<QObject *>(QStringLiteral("settingsMenuItem"));
        if (!dialog || !menuItem || !QMetaObject::invokeMethod(menuItem->property("action").value<QObject *>(), "trigger"))
            return nullptr;
        return dialog;
    }
    QVariantMap settingsValues(QObject *dialog) const { return dialog->property("values").toMap(); }

    void settingsDialogAppliesChangedValuesLive()
    {
        auto &settings = *application->settingsStore();
        settings.set("video.ffms2Seeking", 9);
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        // Opening fixes a seeking method outside the four choices at once.
        QCOMPARE(settings.integer("video.ffms2Seeking"), 2);
        QCOMPARE(dialogItem("settingsDialog", "setting_video.ffms2Seeking")->property("currentIndex").toInt(), 2);
        QCOMPARE(dialogItem("settingsDialog", "setting_autosave.maxFiles")->property("value").toInt(), 3);
        QCOMPARE(dialogItem("settingsDialog", "setting_program.tabTextMaxChars")->property("value").toInt(), 40);
        QCOMPARE(dialogItem("settingsDialog", "setting_video.zoomPercent")->property("text").toString(), QStringLiteral("200"));
        QVERIFY(dialogItem("settingsDialog", "setting_grid.changeActiveOnSelection")->property("checked").toBool());
        // "Do not warn about resolution mismatch" applies on Apply, not before.
        QSignalSpy askChanged(application, &app::Application::askForBadResolutionChanged);
        auto *noWarning = dialogItem("settingsDialog", "setting_video.dontAskForBadResolution");
        QVERIFY(noWarning);
        QVERIFY(QMetaObject::invokeMethod(noWarning, "click"));
        QVERIFY(noWarning->property("checked").toBool());
        QVERIFY(application->askForBadResolution());
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsApply"), "click"));
        QVERIFY(!application->askForBadResolution());
        QCOMPARE(askChanged.count(), 1);
        QVERIFY(dialog->property("visible").toBool());
        // Only changed values are written: untouched options stay unset, but
        // the language is written as "en" (legacy writes the chosen tag) and
        // the shown fallbacks are written.
        QVERIFY(!settings.contains("grid.loadSortedSubs"));
        QVERIFY(!settings.contains("autosave.maxFiles"));
        QCOMPARE(settings.text("program.language"), QStringLiteral("en"));
        QCOMPARE(settings.integer("program.tabTextMaxChars"), 40);
        QCOMPARE(settings.integer("video.zoomPercent"), 200);
        // Numbers are clamped to the NumCtrl range on OK.
        QVERIFY(QMetaObject::invokeMethod(dialog, "put", Q_ARG(QVariant, QStringLiteral("autosave.maxFiles")),
                                          Q_ARG(QVariant, 1)));
        QVERIFY(QMetaObject::invokeMethod(dialog, "put", Q_ARG(QVariant, QStringLiteral("fonts.externalDirectory")),
                                          Q_ARG(QVariant, QStringLiteral("/fonts"))));
        QVERIFY(QMetaObject::invokeMethod(dialog, "put",
                                          Q_ARG(QVariant, QStringLiteral("grid.duplicationDontChangeSelection")),
                                          Q_ARG(QVariant, true)));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(settings.integer("autosave.maxFiles"), 2);
        QCOMPARE(settings.text("fonts.externalDirectory"), QStringLiteral("/fonts") + QDir::separator());
        // Live: duplicating now keeps the selection (SubsGrid::OnDuplicate).
        QVERIFY(application->openFile(episode));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto first = session->document().lines()[0]->id;
        QVERIFY(application->duplicateLines());
        QCOMPARE(session->selection().selected, (std::set<core::LineId>{first}));
        // Cancel drops staged changes.
        QVERIFY(openSettings());
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *changeActive = dialogItem("settingsDialog", "setting_grid.changeActiveOnSelection");
        QVERIFY(QMetaObject::invokeMethod(changeActive, "click"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(settings.boolean("grid.changeActiveOnSelection"));
        // Set default resets every option at once, even when then cancelled;
        // the recent list is kept, as legacy writes it back at exit.
        const auto recent = application->recentEntries();
        QVERIFY(openSettings());
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsDefault"), "click"));
        QVERIFY(!dialogItem("settingsDialog", "setting_video.dontAskForBadResolution")->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(application->askForBadResolution());
        QCOMPARE(askChanged.count(), 2);
        QCOMPARE(settings.integer("autosave.maxFiles"), 3);
        QVERIFY(!settings.contains("grid.duplicationDontChangeSelection"));
        QVERIFY(!settings.contains("program.tabTextMaxChars"));
        QCOMPARE(application->recentEntries(), recent);
        QCOMPARE(settings.list("recent.subtitles").size(), qsizetype(recent.size()));
        QVERIFY(application->closeEditingTarget());
    }

    // O1: every legacy ConOpt control is on its page, in the legacy order, with
    // the legacy label (OptionsDialog.cpp opts[] arrays, OptionsPanels.cpp).
    void settingsDialogPagesBindTheLegacyOptions()
    {
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        struct Control {
            const char *setting;
            const char *label; // a check box's text (nullptr: not checked)
        };
        const std::vector<std::pair<const char *, std::vector<Control>>> pages{
            {"settingsPageEditor",
             {{"program.language", nullptr},
              {"editor.dictionaryLanguage", nullptr},
              {"grid.loadSortedSubs", "Open sorted subtitles"},
              {"editor.spellchecker", "Turn spell checking"},
              {"grid.autoSelectLinesFromLastTab", "Select the line with the time line\nof the previous active tab"},
              {"editor.suggestionsOnDoubleClick", "Show suggestions by double-clicking on misspell"},
              {"subtitles.openInNewTab", "Always open subtitles in a new tab"},
              {"editor.dontGoToNextLineOnTimesEdit", "Stay on selected line when editing times"},
              {"video.disableLiveEditing", "Turn off edits preview on video\n(re-opening tab is required)"},
              {"grid.setVisibleLineAfterFullScreen", "Turn searching of visible line\nafter switching from full screen"},
              {"shiftTimes.changeValuesWithTab", "Synchronize time shifting window in all tabs"},
              {"grid.changeActiveOnSelection", "Change active line after add to selection"},
              {"translation.showOriginal", "Show original in translator mode"},
              {"translation.hideOriginalOnVideo", "Hide original on video in translator mode"},
              {"grid.duplicationDontChangeSelection", "Do not change selections when duplicating dialogue lines"},
              {"grid.dontCenterActiveLine", "Do not vertically center the active line in the subtitle grid"},
              {"editor.allowNumpadHotkeys", "Use numpad shortcuts in text fields"},
              {"video.visualWarningsOff", "Turn off visual tools warning"},
              {"video.dontAskForBadResolution", "Do not warn about resolution mismatch"},
              {"automation.oldScriptsCompatibility", "Compatibility with older HikariSub scripts"}}},
            {"settingsPageConversion",
             {{"convert.styleCatalog", nullptr},
              {"convert.style", nullptr},
              {"convert.fps", nullptr},
              {"convert.fpsFromVideo", "FPS from video"},
              {"convert.newEndTimes", "New end times"},
              {"convert.showSettings", "Show window before conversion"},
              {"convert.timePerCharacter", nullptr},
              {"convert.resolutionWidth", nullptr},
              {"convert.resolutionHeight", nullptr},
              {"convert.assTagsToInsertInLine", nullptr}}},
            {"settingsPageEditorAdvanced",
             {{"grid.calcSpacesAndPunctuationForWraps", "Calculate spaces and punctation characters for wraps"},
              {"grid.calcSpacesAndPunctuationForCps", "Calculate spaces and punctation characters for CPS"},
              {"editor.saveAfterCharacterCount", nullptr},
              {"autosave.maxFiles", nullptr},
              {"grid.insertStartOffset", nullptr},
              {"grid.insertEndOffset", nullptr},
              {"grid.tagsSwapCharacter", nullptr},
              {"program.tabTextMaxChars", nullptr},
              {"automation.traceLevel", nullptr},
              {"grid.font", nullptr},
              {"grid.fontSize", nullptr},
              {"program.font", nullptr},
              {"program.fontSize", nullptr},
              {"automation.loadingMethod", nullptr},
              {"fonts.externalDirectory", nullptr}}},
            {"settingsPageVideo",
             {{"video.fullScreenOnStart", "Open video from context menu on full screen"},
              {"video.pauseOnClick", "Left mouse button pauses video"},
              {"video.openAtActiveLine", "Open video with time of active line"},
              {"video.gpuConversion", "Convert video colours on the GPU (requires reloading)"},
              {"video.acceptedAudioStream", nullptr},
              {"video.ffms2Seeking", nullptr},
              {"video.zoomPercent", nullptr}}},
            {"settingsPageAudio",
             {{"audio.drawTimeCursor", "Show time next to cursor"},
              {"audio.drawSecondaryLines", "Show seconds markers"},
              {"audio.drawSelectionBackground", "Show background selection"},
              {"audio.drawVideoPosition", "Show video position"},
              {"audio.drawKeyframes", "Show keyframes"},
              {"audio.lockScrollOnCursor", "Follow audio during playback"},
              {"audio.autoFocus", "Activate the audio when hover"},
              {"audio.snapToKeyframes", "Snap to keyframe"},
              {"audio.snapToOtherLines", "Snap to other lines"},
              {"audio.dontPlayWhenLineChanges", "Do not play audio after changing the line"},
              {"audio.mergeEveryNWithSyllable", "Merge all the \"n\" with the previous syllable"},
              {"audio.karaokeMoveOnClick", "Move syllable line after click"},
              {"audio.ramCache", "Load audio into RAM"}}},
            {"settingsPageAudioAdvanced",
             {{"audio.delay", nullptr},
              {"audio.markPlayTime", nullptr},
              {"audio.leadInValue", nullptr},
              {"audio.leadOutValue", nullptr},
              {"audio.lineBoundariesThickness", nullptr},
              {"audio.cacheFilesLimit", nullptr},
              {"audio.inactiveLinesDisplayMode", nullptr}}},
            {"settingsPageSubtitleProperties",
             {{"scriptProperties.title", nullptr},
              {"scriptProperties.titleOn", nullptr},
              {"scriptProperties.script", nullptr},
              {"scriptProperties.scriptOn", nullptr},
              {"scriptProperties.translation", nullptr},
              {"scriptProperties.translationOn", nullptr},
              {"scriptProperties.editing", nullptr},
              {"scriptProperties.editingOn", nullptr},
              {"scriptProperties.timing", nullptr},
              {"scriptProperties.timingOn", nullptr},
              {"scriptProperties.update", nullptr},
              {"scriptProperties.updateOn", nullptr},
              {"scriptProperties.askForChange", "Always ask before changing subtitle information"}}},
        };
        std::set<std::string> shown;
        for (const auto &[pageName, controls] : pages) {
            auto *page = dialogItem("settingsDialog", pageName);
            QVERIFY2(page, pageName);
            std::vector<QQuickItem *> found;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (item->objectName().startsWith(QLatin1String("setting_")))
                    found.push_back(item);
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            walk(page);
            QCOMPARE(found.size(), controls.size());
            for (std::size_t i = 0; i < controls.size(); ++i) {
                QCOMPARE(found[i]->objectName(), QLatin1String("setting_") + QLatin1String(controls[i].setting));
                if (controls[i].label)
                    QCOMPARE(found[i]->property("text").toString(), QString::fromUtf8(controls[i].label));
                shown.insert(controls[i].setting);
            }
        }
        // Every bound option has its control, and the dialog holds nothing else.
        std::set<std::string> bound;
        for (const auto &b : application::optionsBindings()) {
            bound.insert(std::string(b.setting));
            if (!b.sizeSetting.empty())
                bound.insert(std::string(b.sizeSetting));
        }
        QCOMPARE(shown, bound);
        std::set<std::string> held;
        for (const auto &id : settingsValues(dialog).keys())
            held.insert(id.toStdString());
        QCOMPARE(held, bound);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
    }

    // O1: legacy defects the dialog reproduces, and what it leaves alone.
    void settingsDialogKeepsLegacyChoiceAndResetDefects()
    {
        auto &settings = *application->settingsStore();
        // A stored index past its list shows nothing and OK writes -1
        // (HikariChoice::SetSelection returns early; GetSelection is -1).
        settings.set("automation.loadingMethod", 7);
        settings.set("audio.inactiveLinesDisplayMode", -2);
        settings.set("updater.nextCheck", 100);
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialogItem("settingsDialog", "setting_automation.loadingMethod")->property("currentIndex").toInt(), -1);
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("audio.inactiveLinesDisplayMode")).toInt(), -2);
        // A value written elsewhere while the dialog is open is not reverted
        // (the update check's next date, say).
        settings.set("updater.nextCheck", 200);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsApply"), "click"));
        QCOMPARE(settings.integer("automation.loadingMethod"), -1);
        QCOMPARE(settings.integer("audio.inactiveLinesDisplayMode"), -2);
        QCOMPARE(settings.integer("updater.nextCheck"), 200);
        // Apply leaves the controls as they are.
        QCOMPARE(dialogItem("settingsDialog", "setting_automation.loadingMethod")->property("currentIndex").toInt(), -1);
        // Choosing an entry stages it.
        auto *method = dialogItem("settingsDialog", "setting_automation.loadingMethod");
        QVERIFY(QMetaObject::invokeMethod(method, "activated", Q_ARG(int, 3)));
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("automation.loadingMethod")).toInt(), 3);
        // The shift times panel keeps its values through Set default.
        auto shift = application->shiftTimesSettings().settingsMap();
        shift.insert(QStringLiteral("forward"), true);
        shift.insert(QStringLiteral("timeMs"), 750);
        application->shiftTimesSettings().setSettingsMap(shift);
        // Set default: the controls refresh as ResetDefault refreshes them,
        // and OK then writes what they show.
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsDefault"), "click"));
        QCOMPARE(settings.integer("updater.nextCheck"), 0);
        QVERIFY(!settings.contains("automation.loadingMethod"));
        QCOMPARE(dialogItem("settingsDialog", "setting_automation.loadingMethod")->property("currentIndex").toInt(), -1);
        QCOMPARE(dialogItem("settingsDialog", "setting_video.ffms2Seeking")->property("currentIndex").toInt(), -1);
        QCOMPARE(dialogItem("settingsDialog", "setting_audio.inactiveLinesDisplayMode")->property("currentIndex").toInt(), -1);
        QCOMPARE(dialogItem("settingsDialog", "setting_program.tabTextMaxChars")->property("value").toInt(), 20);
        QCOMPARE(dialogItem("settingsDialog", "setting_video.zoomPercent")->property("text").toString(), QString());
        QCOMPARE(dialogItem("settingsDialog", "setting_program.language")->property("currentIndex").toInt(), 0);
        QVERIFY(application->shiftTimesSettings().settings().forward);
        QCOMPARE(application->shiftTimesSettings().settings().timeMs, 750);
        QVERIFY(!settings.contains("shiftTimes.time"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(settings.integer("automation.loadingMethod"), -1);
        QCOMPARE(settings.integer("video.ffms2Seeking"), -1);
        QCOMPARE(settings.integer("audio.inactiveLinesDisplayMode"), -1);
        QCOMPARE(settings.integer("program.tabTextMaxChars"), 20);
        QVERIFY(!settings.contains("video.zoomPercent"));
        QCOMPARE(settings.text("program.language"), QStringLiteral("en"));
    }

    // O1: the conversion catalog and style choose from the style catalogs (Y2).
    void settingsDialogChoosesConversionCatalogAndStyle()
    {
        auto &settings = *application->settingsStore();
        auto &styles = application->styleManager();
        QVERIFY(styles.createCatalog(QStringLiteral("Conv")));
        styles.saveCatalog();
        QVERIFY(styles.chooseCatalog(QStringLiteral("Default")));
        settings.set("convert.styleCatalog", QStringLiteral("Missing"));
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        // "does not exist" for the catalog: the current one is shown.
        auto *warning = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsWarning"));
        QVERIFY(warning);
        QTRY_VERIFY(warning->property("visible").toBool());
        const auto catalogs = dialog->property("catalogs").toStringList();
        QVERIFY(catalogs.contains(QStringLiteral("Conv")));
        auto *catalog = dialogItem("settingsDialog", "setting_convert.styleCatalog");
        QCOMPARE(catalog->property("currentText").toString(), QStringLiteral("Default"));
        QVERIFY(QMetaObject::invokeMethod(warning, "close"));
        // Choosing a catalog loads it and lists its Styles (none in a new one).
        QVERIFY(QMetaObject::invokeMethod(catalog, "activated", Q_ARG(int, int(catalogs.indexOf(QStringLiteral("Conv"))))));
        QCOMPARE(styles.catalog(), QStringLiteral("Conv"));
        QVERIFY(dialog->property("styles").toStringList().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(settings.text("convert.styleCatalog"), QStringLiteral("Conv"));
        // The style choice kept its index past the empty list: an empty style.
        QCOMPARE(settings.text("convert.style"), QString());
        QVERIFY(styles.chooseCatalog(QStringLiteral("Default")));
        QVERIFY(styles.deleteCatalog(QStringLiteral("Conv")));
    }

    // O1: HikariChoice::FindString (wxArrayString::Index without case) is
    // CmpNoCase, the C runtime's under the process locale: "C" (ASCII
    // letters only) for English, every letter once a translation language
    // initialized wxLocale at startup.
    void settingsChoicesIgnoreCaseAsTheLegacyLocaleDoes()
    {
        QTemporaryDir own;
        app::Application::Options options;
        options.settingsFile = own.filePath(QStringLiteral("hikari.ini"));
        options.catalogDir = own.filePath(QStringLiteral("Catalog"));
        const QString accented = QStringLiteral(u"Ćwiczenia");
        const QString lower = QStringLiteral(u"ćwiczenia");
        const QString missing = tr("The selected %1 for conversion does not exist\nand will be changed to the default").arg(tr("catalog for style"));
        {
            app::Application a(options);
            auto &styles = a.styleManager();
            QVERIFY(styles.createCatalog(accented));
            styles.saveCatalog();
            QVERIFY(styles.createCatalog(QStringLiteral("Abc")));
            styles.saveCatalog();
            QVERIFY(styles.chooseCatalog(QStringLiteral("Default")));
            auto &settings = *a.settingsStore();
            settings.set("convert.styleCatalog", lower);
            auto open = a.openSettingsDialog();
            QVERIFY(open.value(QStringLiteral("warnings")).toStringList().contains(missing));
            settings.set("convert.styleCatalog", QStringLiteral("aBC"));
            open = a.openSettingsDialog();
            QVERIFY(!open.value(QStringLiteral("warnings")).toStringList().contains(missing));
            QCOMPARE(open.value(QStringLiteral("values")).toMap().value(QStringLiteral("convert.styleCatalog")).toInt(),
                     int(open.value(QStringLiteral("catalogs")).toStringList().indexOf(QStringLiteral("Abc"))));
            settings.set("program.language", QStringLiteral("pl"));
        }
        app::Application b(options);
        b.settingsStore()->set("convert.styleCatalog", lower);
        const auto open = b.openSettingsDialog();
        QVERIFY(!open.value(QStringLiteral("warnings")).toStringList().contains(missing));
        QCOMPARE(open.value(QStringLiteral("values")).toMap().value(QStringLiteral("convert.styleCatalog")).toInt(),
                 int(open.value(QStringLiteral("catalogs")).toStringList().indexOf(accented)));
    }

    // R6-dictionary-location: the Main page lists the settings folder's
    // Dictionary first (user dictionaries), then the program folder's.
    void settingsDictionariesComeFromTheSettingsFolder()
    {
        QTemporaryDir own;
        QVERIFY(QDir(own.path()).mkdir(QStringLiteral("Dictionary")));
        for (const char *name : {"pl_PL.dic", "pl_PL.aff", "en_US.dic", "en_US.aff", "de_DE.dic"}) {
            QFile file(own.filePath(QStringLiteral("Dictionary/") + QLatin1String(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x\n");
        }
        app::Application::Options options;
        options.settingsFile = own.filePath(QStringLiteral("hikari.ini"));
        app::Application a(options);
        a.settingsStore()->set("editor.dictionaryLanguage", QStringLiteral("pl_PL"));
        const auto open = a.openSettingsDialog();
        // AvailableDics pairs the i-th .dic with the i-th .aff: de_DE.dic
        // shifts every pair, so none matches (the pairing stops at the
        // shorter list, R3-hang-crash-loss) and the placeholder is shown.
        QCOMPARE(open.value(QStringLiteral("dictionaries")).toStringList(),
                 (QStringList{QStringLiteral("Put files .dic and .aff to \"Dictionary\" folder")}));
        QVERIFY(QFile::remove(own.filePath(QStringLiteral("Dictionary/de_DE.dic"))));
        const auto again = a.openSettingsDialog();
        QCOMPARE(again.value(QStringLiteral("dictionaries")).toStringList(),
                 (QStringList{QStringLiteral("English"), QStringLiteral("Polski")}));
        QCOMPARE(again.value(QStringLiteral("values")).toMap().value(QStringLiteral("editor.dictionaryLanguage")).toInt(), 1);
    }

    // O1: "Set default" resets the registry at once; what long-lived legacy
    // objects hold survives and is written back by them later.
    void setDefaultKeepsWhatLegacyWindowsHold()
    {
        QTemporaryDir own;
        app::Application::Options options;
        options.settingsFile = own.filePath(QStringLiteral("hikari.ini"));
        options.catalogDir = own.filePath(QStringLiteral("Catalog"));
        app::Application a(options);
        auto &settings = *a.settingsStore();
        // HikariSubFrame's recent lists: subtitles, video and audio are kept;
        // the keyframes list is not written back at exit, so it resets.
        settings.set("recent.video", QStringList{QStringLiteral("/v.mkv")});
        settings.set("recent.audio", QStringList{QStringLiteral("/a.wav")});
        settings.set("recent.keyframes", QStringList{QStringLiteral("/k.txt")});
        // Select lines, not opened yet: the dialog will read the defaults.
        QVariantMap select{{QStringLiteral("find"), QStringLiteral("abc")}, {QStringLiteral("matchCase"), true}};
        a.saveSelectLinesSettings(select);
        QVERIFY(a.selectLinesSettings().value(QStringLiteral("matchCase")).toBool());
        // The shown tag buttons, the colour picker and the Grid.
        a.tagButtons().setCount(2);
        a.tagButtons().edit(0, QStringLiteral("Bold"), QStringLiteral("\\b1"), 0);
        a.gridFilter().setIgnoreInActions(true);
        a.gridFilter().setInverted(true);
        a.shell().toggleColumn(1);
        const QVariantMap red{{QStringLiteral("r"), 255}, {QStringLiteral("g"), 0}, {QStringLiteral("b"), 0}, {QStringLiteral("a"), 0}};
        a.colourPicker().addRecent(red); // no picker yet: the option is the list

        a.resetSettings({});
        QCOMPARE(settings.list("recent.video"), QStringList{QStringLiteral("/v.mkv")});
        QCOMPARE(settings.list("recent.audio"), QStringList{QStringLiteral("/a.wav")});
        QVERIFY(!settings.contains("recent.keyframes"));
        QVERIFY(!a.selectLinesSettings().value(QStringLiteral("matchCase")).toBool());
        QCOMPARE(a.colourPicker().recent().first().toMap().value(QStringLiteral("r")).toInt(), 0);
        // EditBox keeps the buttons SetTagButtons built; pressing one reads
        // its reset option (wxBell); "Change number of buttons" starts from
        // the stored 0 and adds buttons past the shown ones from their options.
        QCOMPARE(a.tagButtons().count(), 2);
        QCOMPARE(a.tagButtons().buttons().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Bold"));
        QVERIFY(a.tagButtons().pressed(0).isEmpty());
        QCOMPARE(a.tagButtons().storedCount(), 0);
        a.tagButtons().setCount(3);
        QCOMPARE(settings.integer("editor.tagButtons"), 3);
        QCOMPARE(a.tagButtons().buttons().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Bold"));
        QCOMPARE(a.tagButtons().buttons().last().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("T3"));
        QVERIFY(!settings.contains("editor.tagButton1"));
        // SubsGrid keeps ignoreFiltered and its hidden columns; the filter
        // itself takes the defaults; each toggle writes its own option.
        QVERIFY(a.gridFilter().ignoreInActions());
        QVERIFY(!a.gridFilter().inverted());
        QVERIFY(!settings.contains("grid.ignoreFiltering"));
        a.gridFilter().setInverted(true);
        QVERIFY(!settings.contains("grid.ignoreFiltering"));
        QCOMPARE(a.shell().hiddenColumns(), 1);
        QVERIFY(!settings.contains("grid.hideColumns"));
        a.shell().toggleColumn(8192);
        QCOMPARE(settings.integer("grid.hideColumns"), 8193);

        // Once opened, SelectLines keeps its options and recent searches:
        // the options go back when it closes, the recent searches at the
        // next selection.
        a.openSelectLines();
        QVERIFY(a.selectLines(select, false).size() > 0);
        QCOMPARE(settings.list("selectLines.recentSelections"), QStringList{QStringLiteral("abc")});
        // A created DialogColorPicker keeps its recent colours.
        a.colourPickerOpened();
        a.colourPicker().addRecent(red);
        a.resetSettings({});
        QVERIFY(!settings.contains("selectLines.options"));
        QVERIFY(!settings.contains("selectLines.recentSelections"));
        auto shown = a.openSelectLines();
        QVERIFY(shown.value(QStringLiteral("matchCase")).toBool());
        QCOMPARE(shown.value(QStringLiteral("recent")).toStringList(), QStringList{QStringLiteral("abc")});
        a.saveSelectLinesSettings(select); // closed: the options only
        QVERIFY(settings.contains("selectLines.options"));
        QVERIFY(!settings.contains("selectLines.recentSelections"));
        select.insert(QStringLiteral("find"), QStringLiteral("def"));
        a.selectLines(select, false);
        QCOMPARE(settings.list("selectLines.recentSelections"), (QStringList{QStringLiteral("def"), QStringLiteral("abc")}));
        QCOMPARE(a.colourPicker().recent().first().toMap().value(QStringLiteral("r")).toInt(), 255);
        QVERIFY(!settings.contains("colourPicker.recentColours"));
        // Opened for another tab's Line editor, the picker is created again
        // from the option.
        QVERIFY(a.openFile(episode));
        a.colourPickerOpened();
        QCOMPARE(a.colourPicker().recent().first().toMap().value(QStringLiteral("r")).toInt(), 0);
        QVERIFY(a.closeEditingTarget());

        // A changed program font runs DestroyDialogs: SelectLines saves its
        // options and is gone, so it reads the settings again.
        a.resetSettings({});
        QVERIFY(!settings.contains("selectLines.options"));
        QSignalSpy destroyed(&a, &app::Application::selectLinesDestroyed);
        a.applySettings({{QStringLiteral("program.font"), QStringLiteral("Hikari Test Face")}});
        QCOMPARE(destroyed.count(), 1);
        QVERIFY(app::Application(options).selectLinesSettings().value(QStringLiteral("matchCase")).toBool());
        QVERIFY(a.openSelectLines().value(QStringLiteral("recent")).toStringList().isEmpty());
    }

    void theReferenceIsNeverEdited()
    {
        QVERIFY(application->openReference(original)); // the only Document is protected
        QVERIFY(!application->workspace().editingTarget());
        QVERIFY(!application->editor().editable());
        QVERIFY(item<QObject>("lineText")->property("readOnly").toBool());
    }
};

QTEST_MAIN(ShellTest)
#include "shell_tests.moc"
