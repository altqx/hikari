#include "hikari/app/application.h"
#include "docking.h"
#include "line_grid.h"
#include "line_table_model.h"
#include "theme.h"

#include <QAccessible>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

QString write(const QTemporaryDir &dir, const char *name, const QByteArray &events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n" +
            events);
    return path;
}

QByteArray hash(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256) : QByteArray();
}

}

class ReferenceTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    QString edit, ref, third;
    app::Application *application = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;

    QObject *named(const char *name) const
    {
        auto *root = engine->rootObjects().first();
        if (auto *found = root->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QObject *o : root->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        return nullptr;
    }
    static QQuickItem *findItem(QQuickItem *from, const QString &name)
    {
        if (from->objectName() == name)
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *item(const char *name) const
    {
        if (auto *found = qobject_cast<QQuickItem *>(named(name)))
            return found;
        return findItem(window->contentItem(), QLatin1String(name));
    }
    application::DocumentId tab(int index) const { return application->workspace().tabs()[std::size_t(index)]; }
    application::EditSession &session(application::DocumentId id) const { return *application->files().session(id); }
    static int activeRow(const application::EditSession &s)
    {
        const auto lines = s.document().lines();
        for (std::size_t r = 0; r < lines.size(); ++r)
            if (s.selection().active == lines[r]->id)
                return int(r);
        return -1;
    }
    int referenceRow() const
    {
        const auto reference = application->workspace().reference();
        return reference ? activeRow(session(*reference)) : -2;
    }
    qulonglong lineId(application::DocumentId id, int row) const
    {
        return session(id).document().lines()[std::size_t(row)]->id.value;
    }
    const ui::ShellController &shell() const { return application->shell(); }
    QString states(const char *grid) const
    {
        QString out;
        auto *model = named(grid)->property("model").value<QAbstractItemModel *>();
        for (int r = 0; r < model->rowCount(); ++r) {
            const int state = model->index(r, 0).data(ui::LineTableModel::ComparisonRole).toInt();
            out += state == 2 ? QLatin1Char('x') : state == 1 ? QLatin1Char('=') : QLatin1Char('.');
        }
        return out;
    }
    QString status() const { return item("referenceMatchStatus")->property("text").toString(); }
    void press(Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(window, key, mods);
        QCoreApplication::processEvents();
    }
    static void click(QQuickItem *item, Qt::MouseButton button = Qt::LeftButton,
                      Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QVERIFY(item);
        const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::mouseClick(item->window(), button, mods, at);
    }
    QPoint rowPoint(const char *grid, int row) const
    {
        QQuickItem *g = item(grid);
        int found = -1;
        for (int y = 0; g && y < int(g->height()); ++y) {
            int r = -1;
            QMetaObject::invokeMethod(g, "rowAt", Q_RETURN_ARG(int, r), Q_ARG(qreal, qreal(y)));
            if (r == row) {
                found = y;
                break;
            }
        }
        return g ? g->mapToScene(QPointF(g->width() / 2, found + 3)).toPoint() : QPoint();
    }
    void clickRow(const char *grid, int row, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        // Qt reads a second click within the double-click interval as a double click.
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window, Qt::LeftButton, mods, rowPoint(grid, row));
        QCoreApplication::processEvents();
    }
    QString accessibleName(const char *name) const
    {
        QAccessibleInterface *a = QAccessible::queryAccessibleInterface(item(name));
        return a ? a->text(QAccessible::Name) : QString();
    }
    void showPreviewByKey()
    {
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Q, Qt::ControlModifier);
    }

    void allTabsLeaveTheReference(const QString &path, bool searched)
    {
        const auto refId = *application->workspace().reference();
        const QByteArray before = hash(path);
        const auto history = session(refId).historySize();
        const auto texts = [&] {
            QStringList out;
            for (const auto *line : session(refId).document().lines())
                out << QString::fromUtf8(reinterpret_cast<const char *>(line->text.data()), qsizetype(line->text.size()));
            return out;
        };
        const QStringList lines = texts();
        const auto unchanged = [&] {
            QCOMPARE(texts(), lines);
            QCOMPARE(session(refId).historySize(), history);
            QVERIFY(!session(refId).isDirty());
            QCOMPARE(hash(path), before);
        };
        application->setFindQuestionHandler([](int, const QString &) { return 0; });
        auto replace = application->findReplaceSettings(1);
        replace[QStringLiteral("find")] = QStringLiteral("0");
        replace[QStringLiteral("replace")] = QStringLiteral("X");
        application->runFindReplace(QStringLiteral("replaceAllTabs"), replace);
        unchanged();
        const auto thirdId = tab(2);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session(thirdId).document().lines()[0]->text.data())),
                 QStringLiteral("tX"));
        application->runFindReplace(QStringLiteral("findAllTabs"),
                                    {{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("r")}});
        bool listed = false;
        for (const QVariant &row : application->findResults())
            listed = listed || (row.toMap().value(QStringLiteral("header")).toBool() &&
                                row.toMap().value(QStringLiteral("text")).toString() == QFileInfo(path).fileName());
        QCOMPARE(listed, searched);
        application->replaceFindResults(QStringLiteral("Y"));
        unchanged();
        application->selectLines({{QStringLiteral("find"), QStringLiteral("r")},
                                  {QStringLiteral("field"), 0},
                                  {QStringLiteral("mode"), 0},
                                  {QStringLiteral("action"), 6}},
                                 true);
        unchanged();
        QCOMPARE(application->workspace().reference(), std::optional(refId));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        edit = write(dir, "edit.ass",
                     "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e1\n"
                     "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,e2\n"
                     "Dialogue: 0,0:00:08.00,0:00:09.00,Default,,0,0,0,,e3\n");
        ref = write(dir, "ref.ass",
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,r0\n"
                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,r1\n"
                    "Dialogue: 0,0:00:01.50,0:00:02.50,Default,,0,0,0,,r2\n"
                    "Dialogue: 0,0:00:02.50,0:00:03.50,Default,,0,0,0,,r3\n"
                    "Dialogue: 0,0:00:10.00,0:00:11.00,Default,,0,0,0,,r4\n");
        third = write(dir, "third.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,t0\n");
    }
    void init()
    {
        application = new app::Application;
        // MUST KILL: a fake key clock steps 1 s per read so repeated hotkeys are never dropped
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(application->openFile(edit));
        QVERIFY(application->openFile(ref));
        QVERIFY(application->openFile(third));
        application->selectTab(0);
        QCOMPARE(application->currentTab(), 0);
        QCOMPARE(activeRow(session(tab(0))), 0);
    }
    void cleanup()
    {
        delete engine;
        delete application;
    }

    void theReferenceHighlightsCommentRows()
    {
        application->selectTab(1);
        QVERIFY(application->editor().setComment(true));
        application->selectTab(0);
        QVERIFY(application->showPreview());
        auto *grid = qobject_cast<ui::LineGrid *>(item("referenceGrid"));
        QVERIFY(grid);
        QTRY_COMPARE(grid->model()->rowCount(), 5);
        const QModelIndex first = grid->model()->index(0, 0);
        QVERIFY(first.data(ui::LineTableModel::CommentRole).toBool());
        QTRY_VERIFY(grid->width() > 0 && grid->height() > 2 * grid->rowHeight());
        QImage image(grid->size().toSize(), QImage::Format_ARGB32);
        QPainter painter(&image);
        grid->paint(&painter);
        painter.end();
        const auto &content = ui::theme::current().content;
        const QColor expected = first.data(ui::LineTableModel::SelectedRole).toBool()
                                    ? ui::selectionOver(content.gridComment, content.comparisonSelection)
                                    : content.gridComment;
        const QRectF text = grid->cellRect(0, grid->columnCount() - 1);
        QCOMPARE(image.pixelColor(int(text.right()) - 3, int(text.center().y())), expected);
        const QRectF dialogue = grid->cellRect(1, grid->columnCount() - 1);
        QVERIFY(image.pixelColor(int(dialogue.right()) - 3, int(dialogue.center().y())) != content.gridComment);
    }

    void previewNeedsAnotherTabAndNoReference()
    {
        QVERIFY(application->canShowPreview());
        auto *menu = named("gridMenu");
        QTest::mouseClick(window, Qt::RightButton, {}, rowPoint("editingGrid", 0));
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(named("showPreview")->property("enabled").toBool());
        QCOMPARE(named("showPreview")->property("text").toString(), QStringLiteral("Show subtitles preview\tCtrl-Q"));
        QMetaObject::invokeMethod(menu, "close");
        QVERIFY(application->showPreview());
        QVERIFY(!application->canShowPreview());
        QVERIFY(!application->showPreview());
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window, Qt::RightButton, {}, rowPoint("editingGrid", 0));
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(!named("showPreview")->property("enabled").toBool());
        QMetaObject::invokeMethod(menu, "close");
        delete engine;
        engine = nullptr;
        delete application;
        application = new app::Application;
        QVERIFY(application->openFile(edit));
        QVERIFY(!application->canShowPreview());
        QVERIFY(!application->showPreview());
        QVERIFY(!application->workspace().reference());
        // MUST KILL: an unused engine is created only so cleanup() has one to delete
        engine = new QQmlApplicationEngine;
    }

    void ctrlQShowsTheNextTabAsTheLinkedReference()
    {
        const QByteArray before = hash(ref);
        const auto refId = tab(1);
        const auto history = session(refId).historySize();
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), refId);
        QVERIFY(application->workspace().referenceIsTab());
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3));
        QCOMPARE(application->workspace().editingTarget(), tab(0));
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QVERIFY(shell().referenceLinked());
        QCOMPARE(shell().referenceMatchCount(), 2);
        QCOMPARE(shell().referenceMatchIndex(), 0);
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(status(), QStringLiteral("Match 1 of 2"));
        QVERIFY(item("referenceLinked")->property("checked").toBool());
        auto *tabItem = item("documentTab1");
        QVERIFY(tabItem);
        QCOMPARE(QAccessible::queryAccessibleInterface(tabItem)->text(QAccessible::Description),
                 QStringLiteral("Protected reference"));
        QVERIFY(!application->workspace().checkContentCommand(refId));
        QCOMPARE(session(refId).historySize(), history);
        QVERIFY(!session(refId).isDirty());
        QCOMPARE(hash(ref), before);
    }

    void theLinkedReferenceFollowsTheEditingLine()
    {
        showPreviewByKey();
        QCOMPARE(referenceRow(), 0);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down);
        QCOMPARE(activeRow(session(tab(0))), 1);
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(shell().referenceMatchCount(), 1);
        QCOMPARE(status(), QStringLiteral("Match 1 of 1"));
        clickRow("editingGrid", 2);
        QCOMPARE(activeRow(session(tab(0))), 2);
        QVERIFY(shell().referenceNoMatch());
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(status(), QStringLiteral("No matching line"));
        QVERIFY(shell().referenceHasNearest());
        QTRY_VERIFY(item("referenceNearest")->isVisible());
        click(item("referenceNearest"));
        QCOMPARE(referenceRow(), 4);
        QVERIFY(shell().referenceNoMatch());
        clickRow("editingGrid", 0);
        QCOMPARE(referenceRow(), 0);
        QVERIFY(!item("referenceNearest")->isVisible());
        application->selectReferenceLine(lineId(tab(1), 3));
        QCOMPARE(referenceRow(), 3);
        application->editor().textEdited(QStringLiteral("e1 edited"), 9);
        QVERIFY(application->editor().commit());
        QVERIFY(session(tab(0)).document().lines()[0]->text == std::u8string(u8"e1 edited"));
        QCOMPARE(referenceRow(), 3);
        application->editor().textEdited(QStringLiteral("e1 again"), 8);
        QVERIFY(application->editor().commitAndAdvance());
        QCOMPARE(activeRow(session(tab(0))), 1);
        QCOMPARE(referenceRow(), 2);
    }

    void candidatesAreCountedAndStepped()
    {
        showPreviewByKey();
        QTRY_VERIFY(item("referenceNextMatch")->isVisible());
        QVERIFY(!item("referencePreviousMatch")->isEnabled());
        QVERIFY(item("referenceNextMatch")->isEnabled());
        click(item("referenceNextMatch"));
        QCOMPARE(shell().referenceMatchIndex(), 1);
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(status(), QStringLiteral("Match 2 of 2"));
        QVERIFY(!item("referenceNextMatch")->isEnabled());
        QVERIFY(!application->stepReferenceMatch(1));
        click(item("referencePreviousMatch"));
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(status(), QStringLiteral("Match 1 of 2"));
        QCOMPARE(accessibleName("referenceLinked"), QStringLiteral("Follow the editing line"));
        QCOMPARE(accessibleName("referencePreviousMatch"), QStringLiteral("Previous match"));
        QCOMPARE(accessibleName("referenceNextMatch"), QStringLiteral("Next match"));
        QCOMPARE(accessibleName("referenceMatchStatus"), QStringLiteral("Match 1 of 2"));
        QCOMPARE(accessibleName("referenceGrid"), QStringLiteral("Reference lines (protected, read-only)"));
    }

    void navigationIsIndependentUntilLinked()
    {
        QVERIFY(application->openReference(ref));
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QVERIFY(!shell().referenceLinked());
        QCOMPARE(status(), QStringLiteral("Independent navigation"));
        QCOMPARE(referenceRow(), 0);
        const auto target = *application->workspace().editingTarget();
        clickRow("editingGrid", 1);
        QCOMPARE(activeRow(session(target)), 1);
        QCOMPARE(referenceRow(), 0);
        clickRow("referenceGrid", 1);
        QCOMPARE(referenceRow(), 1);
        QCOMPARE(activeRow(session(target)), 1);
        item("referenceGrid")->forceActiveFocus();
        press(Qt::Key_Down);
        QCOMPARE(referenceRow(), 2);
        press(Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(session(*application->workspace().reference()).selection().selected.size(), std::size_t(2));
        QCOMPARE(activeRow(session(target)), 1);
        press(Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(session(*application->workspace().reference()).selection().selected.size(), std::size_t(5));
        QCOMPARE(session(target).selection().selected.size(), std::size_t(1));
        QCOMPARE(activeRow(session(target)), 1);
        QCOMPARE(application->workspace().editingTarget(), target);
        click(item("referenceLinked"));
        QVERIFY(shell().referenceLinked());
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(shell().referenceMatchCount(), 1);
        clickRow("editingGrid", 0);
        QCOMPARE(referenceRow(), 0);
        click(item("referenceLinked"));
        QVERIFY(!shell().referenceLinked());
        clickRow("editingGrid", 1);
        QCOMPARE(referenceRow(), 0);
    }

    void focusNeverMakesTheReferenceWritable()
    {
        showPreviewByKey();
        const auto refId = tab(1), target = tab(0);
        const QByteArray before = hash(ref);
        const auto lines = session(refId).document().lines().size();
        const auto history = session(refId).historySize();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        item("referenceGrid")->forceActiveFocus();
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QGuiApplication::clipboard()->setText(QStringLiteral("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,pasted"));
        press(Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(shell().statusText(), QStringLiteral("The reference is protected (read-only)."));
        QCOMPARE(session(refId).document().lines().size(), lines);
        QCOMPARE(session(target).document().lines().size(), std::size_t(3));
        press(Qt::Key_C, Qt::ControlModifier);
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral(",r0")));
        press(Qt::Key_D, Qt::ControlModifier);
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QCOMPARE(session(target).document().lines().size(), std::size_t(4));
        QCOMPARE(session(refId).document().lines().size(), lines);
        QGuiApplication::clipboard()->clear();
        press(Qt::Key_X, Qt::ControlModifier);
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QCOMPARE(session(target).document().lines().size(), std::size_t(3));
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral(",e1")));
        QVERIFY(!QGuiApplication::clipboard()->text().contains(QStringLiteral(",r")));
        QCOMPARE(session(refId).document().lines().size(), lines);
        press(Qt::Key_Delete, Qt::ShiftModifier);
        QVERIFY(item("referenceGrid")->hasActiveFocus());
        QCOMPARE(session(target).document().lines().size(), std::size_t(2));
        QCOMPARE(session(refId).document().lines().size(), lines);
        QCOMPARE(application->workspace().editingTarget(), target);
        QVERIFY(!application->workspace().checkContentCommand(refId));
        QCOMPARE(session(refId).historySize(), history);
        QVERIFY(!session(refId).isDirty());
        QCOMPARE(hash(ref), before);
    }

    void allTabsReplaceNeverWritesATabReference()
    {
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), std::optional(tab(1)));
        allTabsLeaveTheReference(ref, true);
    }
    void allTabsReplaceNeverWritesAnOpenedReference()
    {
        const QString opened = write(dir, "opened.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,r0\n");
        QVERIFY(application->openReference(opened));
        const auto reference = application->workspace().reference();
        QVERIFY(reference);
        const auto tabs = application->workspace().tabs();
        QVERIFY(std::ranges::find(tabs, *reference) == tabs.end());
        allTabsLeaveTheReference(opened, false);
    }

    void closingTheReferenceKeepsItsTab()
    {
        showPreviewByKey();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QVERIFY(!item("referenceClose"));
        QObject *menu = named("panelOptionsMenu");
        click(item("referenceMatchStatus"), Qt::RightButton);
        QTRY_VERIFY(menu->property("visible").toBool());
        QObject *close = named("panelOptionsClose");
        QCOMPARE(close->property("text").toString(), QStringLiteral("Close reference"));
        QVERIFY(QMetaObject::invokeMethod(close, "triggered"));
        QVERIFY(!application->workspace().reference());
        QVERIFY(!shell().referenceLinked());
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3));
        QTRY_VERIFY(!item("referenceDock")->property("isOpen").toBool());
        QVERIFY(application->canShowPreview());
        QVERIFY(application->openReference(third));
        QVERIFY(!shell().referenceLinked());
    }

    void choosingThePreviewedTabEndsTheReference()
    {
        showPreviewByKey();
        application->selectTab(1);
        QCOMPARE(application->workspace().editingTarget(), tab(1));
        QVERIFY(!application->workspace().reference());
        QVERIFY(!shell().referenceLinked());
        QVERIFY(application->workspace().checkContentCommand(tab(1)).has_value());
        QTRY_VERIFY(!item("referenceDock")->property("isOpen").toBool());
    }

    void theTrayStaysAcrossTabSwitches()
    {
        const QByteArray before = hash(ref);
        const auto refId = tab(1);
        showPreviewByKey();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down);
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(shell().referenceMatchCount(), 1);
        application->selectTab(2);
        QCOMPARE(application->workspace().editingTarget(), tab(2));
        QCOMPARE(application->workspace().reference(), refId);
        QVERIFY(application->workspace().referenceIsTab());
        QVERIFY(shell().referenceLinked());
        QVERIFY(item("referenceDock")->property("isOpen").toBool());
        QCOMPARE(shell().referenceMatchCount(), 2);
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(status(), QStringLiteral("Match 1 of 2"));
        QVERIFY(!application->workspace().checkContentCommand(refId));
        application->selectTab(0);
        QCOMPARE(application->workspace().reference(), refId);
        QVERIFY(item("referenceDock")->property("isOpen").toBool());
        QCOMPARE(referenceRow(), 2);
        QCOMPARE(status(), QStringLiteral("Match 1 of 1"));
        QVERIFY(!session(refId).isDirty());
        QCOMPARE(hash(ref), before);
    }

    void theMenuListsTheOccurrencesInEveryOtherTab()
    {
        showPreviewByKey();
        auto texts = [](const QVariantList &rows) {
            QStringList out;
            for (const auto &r : rows)
                out << r.toMap().value(QStringLiteral("text")).toString() +
                           (r.toMap().value(QStringLiteral("checked")).toBool() ? QStringLiteral("*") : QString());
            return out;
        };
        QCOMPARE(texts(application->referenceOccurrences()),
                 (QStringList{"ref.ass (0 1)*", "ref.ass (2 1)", "third.ass (0 1)"}));
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QTest::mouseClick(window, Qt::RightButton, {}, rowPoint("referenceGrid", 0));
        QTRY_VERIFY(named("referenceOccurrenceMenu")->property("visible").toBool());
        QObject *thirdItem = named("referenceOccurrence2");
        QVERIFY(thirdItem);
        QCOMPARE(thirdItem->property("text").toString(), QStringLiteral("third.ass (0 1)"));
        QVERIFY(QMetaObject::invokeMethod(thirdItem, "triggered"));
        QCOMPARE(application->workspace().reference(), tab(2));
        QVERIFY(application->workspace().referenceIsTab());
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(shell().referenceMatchCount(), 1);
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3));
        QVERIFY(application->workspace().checkContentCommand(tab(1)).error() ==
                application::TargetRefusal::NotEditingTarget);
        clickRow("editingGrid", 2);
        QCOMPARE(texts(application->referenceOccurrences()), (QStringList{"ref.ass (4 0)", "third.ass (0 0)*"}));
        QVERIFY(application->chooseReferenceOccurrence(0));
        QCOMPARE(application->workspace().reference(), tab(1));
        QCOMPARE(referenceRow(), 4);
    }

    void aComparedGridPreviewsItsPartner()
    {
        QVERIFY(application->compareWithTab(2));
        clickRow("editingGrid", 1);
        showPreviewByKey();
        QVERIFY(!application->workspace().reference());
        clickRow("editingGrid", 0);
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), tab(2));
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(shell().referenceMatchCount(), 1);
        clickRow("editingGrid", 1);
        QVERIFY(shell().referenceNoMatch());
        QVERIFY(!shell().referenceHasNearest());
        QCOMPARE(referenceRow(), 0);
        QCOMPARE(application->comparedReferenceRow(0), 0);
        QCOMPARE(application->comparedReferenceRow(1), -1);
        application->setReferenceLinked(false);
        QCOMPARE(application->comparedReferenceRow(0), -1);
    }

    void compareWithAReferenceThatIsNoTab()
    {
        const QString other = write(dir, "other.ass",
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e1\n"
                                    "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,o2\n"
                                    "Dialogue: 0,0:00:08.00,0:00:09.00,Default,,0,0,0,,e3\n");
        const QByteArray before = hash(other);
        QVERIFY(application->openReference(other));
        const auto reference = application->workspace().reference();
        QVERIFY(reference);
        QCOMPARE(application->workspace().tabs().size(), std::size_t(3));
        const auto history = session(*reference).historySize();
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QObject *menu = named("panelOptionsMenu");
        click(item("referenceMatchStatus"), Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *compare = item("panelOptions_compareWithReference");
        QVERIFY(compare);
        QCOMPARE(compare->property("text").toString(), QStringLiteral("Compare with reference"));
        QVERIFY(compare->isEnabled());
        QTRY_VERIFY(compare->isVisible() && compare->width() > 0);
        click(compare);
        QVERIFY(application->comparison().active());
        QCOMPARE(application->comparison().first(), tab(0));
        QCOMPARE(application->comparison().second(), reference);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(states("editingGrid"), QStringLiteral("=x="));
        QCOMPARE(states("referenceGrid"), QStringLiteral("=x="));
        QCOMPARE(application->workspace().reference(), reference);
        QCOMPARE(session(*reference).historySize(), history);
        QVERIFY(!session(*reference).isDirty());
        QCOMPARE(hash(other), before);
        application->turnOffComparison();
        QCOMPARE(states("referenceGrid"), QStringLiteral("..."));
    }

    void closingANoTabReferenceKeepsItsComparisonAsATab()
    {
        const QString other = write(dir, "other.ass",
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e1\n"
                                    "Dialogue: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,o2\n"
                                    "Dialogue: 0,0:00:08.00,0:00:09.00,Default,,0,0,0,,e3\n");
        QVERIFY(application->openReference(other));
        const auto reference = *application->workspace().reference();
        QVERIFY(application->compareWithReference());
        QCOMPARE(states("editingGrid"), QStringLiteral("=x="));
        application->closeReference();
        QVERIFY(!application->workspace().reference());
        QCOMPARE(application->workspace().tabs().size(), std::size_t(4));
        QCOMPARE(tab(3), reference);
        QVERIFY(application->comparison().active());
        QCOMPARE(application->comparison().first(), tab(0));
        QCOMPARE(application->comparison().second(), reference);
        QCOMPARE(states("editingGrid"), QStringLiteral("=x="));
        QVERIFY(application->openReference(write(dir, "next.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,e1\n")));
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QCOMPARE(states("referenceGrid"), QStringLiteral("."));
        QCOMPARE(application->comparison().first(), tab(0));
        QCOMPARE(application->comparison().second(), reference);
        application->selectTab(3);
        QCOMPARE(application->workspace().editingTarget(), reference);
        QCOMPARE(states("editingGrid"), QStringLiteral("=x="));
        QVERIFY(application->closeEditingTarget());
        QVERIFY(!application->comparison().active());
        application->selectTab(0);
        QCOMPARE(states("editingGrid"), QStringLiteral("..."));
    }

    void aLinkedPartnerScrollsWithTheComparedGrid()
    {
        auto at = [](int s) {
            return QStringLiteral("%1:%2:%3.00").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0'))
                .arg(s % 60, 2, 10, QLatin1Char('0')).toLatin1();
        };
        QByteArray longLines, partnerLines;
        for (int i = 0; i < 5; ++i)
            partnerLines += "Dialogue: 0," + at(3000 + i) + "," + at(3000 + i) + ",Default,,0,0,0,,lead\n";
        for (int i = 0; i < 200; ++i) {
            const QByteArray line = "Dialogue: 0," + at(10 + i) + "," + at(11 + i) + ",Default,,0,0,0,,l" +
                                    QByteArray::number(i) + "\n";
            longLines += line;
            partnerLines += line;
        }
        QVERIFY(application->openFile(write(dir, "long.ass", longLines)));
        QVERIFY(application->openFile(write(dir, "partner.ass", partnerLines)));
        application->selectTab(3);
        application->settingsStore()->set("comparison.type", 1);
        QVERIFY(application->compareWithTab(4));
        showPreviewByKey();
        QCOMPARE(application->workspace().reference(), tab(4));
        QVERIFY(shell().referenceLinked());
        QTRY_VERIFY(item("referenceDock")->property("isOpen").toBool());
        QQuickItem *editing = item("editingGrid");
        QQuickItem *reference = item("referenceGrid");
        const qreal rh = editing->property("rowHeight").toReal();
        QVERIFY(rh > 0);
        QCOMPARE(reference->property("rowHeight").toReal(), rh);
        const int active = referenceRow();
        editing->setProperty("contentY", 30 * rh);
        QCOMPARE(editing->property("contentY").toReal(), 30 * rh);
        QCOMPARE(reference->property("contentY").toReal(), 35 * rh);
        QCOMPARE(referenceRow(), active);
        editing->setProperty("contentY", 31.5 * rh);
        QCOMPARE(reference->property("contentY").toReal(), 36 * rh);
        application->setReferenceLinked(false);
        editing->setProperty("contentY", 60 * rh);
        QCOMPARE(reference->property("contentY").toReal(), 36 * rh);
    }
};

QTEST_MAIN(ReferenceTest)
#include "reference_tests.moc"
