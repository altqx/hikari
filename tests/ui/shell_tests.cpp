// V1-S: the Classic shell bound to real Lines. Target labels, the protected
// reference, F6/Shift+F6 panel traversal and focus restoration.

#include "shell_controller.h"

#include "hikari/application/workspace.h"
#include "hikari/core/ass_load.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <cstring>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

core::Document documentOf(const char *events)
{
    std::string s = std::string("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
                                "Effect, Text\n") + events;
    std::vector<std::byte> b(s.size());
    std::memcpy(b.data(), s.data(), s.size());
    return core::loadAss(b).document;
}

} // namespace

class ShellTest : public QObject {
    Q_OBJECT

    application::Workspace workspace;
    ui::ShellController *shell = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    core::Document target = documentOf("Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,first\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n");
    core::Document reference = documentOf("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,ref\n");

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
    void init()
    {
        workspace = application::Workspace{};
        shell = new ui::ShellController(workspace);
        engine = new QQmlApplicationEngine;
        engine->setInitialProperties({{QStringLiteral("shell"), QVariant::fromValue(shell)}});
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
        delete shell;
    }

    void zeroDocumentState()
    {
        shell->refresh(nullptr, nullptr);
        QCOMPARE(panelTitle("gridPanel"), QStringLiteral("No document open"));
        QVERIFY(!item("referencePanel")->isVisible());
        QCOMPARE(item<QObject>("statusTargets")->property("text").toString(), QStringLiteral("No editing target"));
    }

    void labelsNameTheEditingTargetAndTheProtectedReference()
    {
        workspace.add("episode.ass");
        const auto ref = workspace.add("original.ass");
        QVERIFY(workspace.setReference(ref));
        shell->refresh(&target, &reference);
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
        const auto a = workspace.add("episode.ass");
        const auto ref = workspace.add("original.ass");
        QVERIFY(workspace.setReference(ref));
        shell->refresh(&target, &reference);
        item("referenceGrid")->forceActiveFocus();
        QCOMPARE(focusedPanel(), QStringLiteral("referencePanel"));
        QCOMPARE(workspace.editingTarget(), a);
        QCOMPARE(panelTitle("gridPanel"), QStringLiteral("Editing: episode.ass"));
        QVERIFY(!workspace.checkContentCommand(ref));
        QVERIFY(workspace.checkContentCommand(a).has_value());
    }

    void f6TraversesTheMajorPanels()
    {
        workspace.add("episode.ass");
        const auto ref = workspace.add("original.ass");
        QVERIFY(workspace.setReference(ref));
        shell->refresh(&target, &reference);
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
        workspace.add("episode.ass");
        shell->refresh(&target, nullptr);
        item("lineText")->forceActiveFocus();
        QCOMPARE(focusedPanel(), QStringLiteral("editorPanel"));
        press(Qt::Key_F6);
        QCOMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        press(Qt::Key_F6);
        QCOMPARE(focusedPanel(), QStringLiteral("videoPanel"));
    }

    void focusReturnsToWhereItWasInAPanel()
    {
        workspace.add("episode.ass");
        shell->refresh(&target, nullptr);
        item("styleField")->forceActiveFocus();
        press(Qt::Key_F6); // to the Grid
        QCOMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        press(Qt::Key_F6, Qt::ShiftModifier); // back to the editor
        QVERIFY(item("styleField")->hasActiveFocus());
        press(Qt::Key_F6);
        QVERIFY(item("editingGrid")->hasActiveFocus());
    }

    void gridActivationShowsTheLineReadOnly()
    {
        workspace.add("episode.ass");
        shell->refresh(&target, nullptr);
        auto *grid = item("editingGrid");
        grid->forceActiveFocus();
        press(Qt::Key_Down); // keyboard navigation asks for the next Line
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(item<QObject>("styleField")->property("text").toString(), QStringLiteral("Sign"));
        QVERIFY(item<QObject>("lineText")->property("readOnly").toBool());
    }
};

QTEST_MAIN(ShellTest)
#include "shell_tests.moc"
