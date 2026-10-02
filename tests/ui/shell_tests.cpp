// V1-S: the Classic shell bound to real Lines. Target labels, the protected
// reference, F6/Shift+F6 panel traversal and focus restoration.

#include "hikari/app/application.h"

#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cstring>

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

    template <typename T = QQuickItem> T *item(const char *name) const
    {
        return window->findChild<T *>(QLatin1String(name));
    }
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
        QVERIFY(!item("referencePanel")->isVisible());
        QCOMPARE(item<QObject>("statusTargets")->property("text").toString(), QStringLiteral("No editing target"));
    }

    void labelsNameTheEditingTargetAndTheProtectedReference()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        QCOMPARE(panelTitle("gridPanel"), QStringLiteral("Editing: episode.ass"));
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
        grid->forceActiveFocus();
        press(Qt::Key_Down); // keyboard navigation asks for the next Line
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(item<QObject>("startField")->property("text").toString(), QStringLiteral("0:00:01.00"));
        QVERIFY(!item<QObject>("lineText")->property("readOnly").toBool());
    }

    void boldWrapsTheSelectionWithTagsHidden()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down); // the first Line, "first"
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
