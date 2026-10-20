// TM1 (#232): Translation mode through the Translation preset. Choosing
// the preset for a Document without TLMode asks once ("Turn on Translation
// mode for this file?"); another preset keeps the mode on; Subtitles >
// Translation mode (GLOBAL_TRANSLATION_MODE) switches it, asking before off.
// The Original text is a panel of its own, its header holding the
// translation buttons and toggles, and it follows the editing target's mode
// as the Reference tray follows the comparison. A preset carries its mode
// through WorkspaceLayoutController::presetMode, which KP1 reuses.

#include "hikari/app/application.h"

#include "docking.h"
#include "line_editor_controller.h"
#include "workspace_layout.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace hikari;

namespace {

// An ASS file of three Lines, with TLMode: Yes when `translation`.
QString writeSubtitles(const QString &path, bool translation = false)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\n");
    if (translation)
        f.write("TLMode: Yes\nTLMode Style: TLMode Original\n");
    f.write("\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    for (int i = 0; i < 3; ++i) {
        if (translation)
            f.write(QStringLiteral("Comment: 0,0:00:0%1.00,0:00:0%2.00,TLMode Original,,0,0,0,,line %1\n").arg(i + 1).arg(i + 2).toUtf8());
        f.write(QStringLiteral("Dialogue: 0,0:00:0%1.00,0:00:0%2.00,Default,,0,0,0,,%3\n")
                    .arg(i + 1)
                    .arg(i + 2)
                    .arg(translation ? QStringLiteral("linia %1").arg(i + 1) : QStringLiteral("line %1").arg(i + 1))
                    .toUtf8());
    }
    return path;
}

} // namespace

class TranslationPresetTest : public QObject {
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
        // No dictionaries here: the first spell check would turn spelling off
        // and show its notice, which takes the focus.
        application->applySettings({{QStringLiteral("editor.spellchecker"), false}});
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        QVERIFY(QTest::qWaitForWindowExposed(window()));
        QTRY_VERIFY(window()->property("arrangementSettled").toBool());
        application->addPage();
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
    QString preset() const { return application->workspaceLayout().preset(); }
    bool translationMode() const { return application->editor().translationMode(); }
    QObject *question() const { return item<QObject>("translationModeQuestion"); }
    bool asked() const { return question()->property("visible").toBool(); }
    void answer(bool turnOn)
    {
        QTRY_VERIFY(asked());
        QVERIFY(QMetaObject::invokeMethod(question(), turnOn ? "accept" : "reject"));
        QTRY_VERIFY(!asked());
    }
    // The Original text field shows in the panel (true) or in the Line editor.
    bool originalInPanel() const
    {
        return item("lineText")->parentItem() == item("originalSlot");
    }
    QObject *modeAction() const { return item<QObject>("translationModeMenuItem")->property("action").value<QObject *>(); }
    void openFile(const char *name, bool translation)
    {
        QVERIFY(application->openFile(writeSubtitles(dir.filePath(QLatin1String(name)), translation)));
    }

private slots:
    void cleanup()
    {
        engine.reset();
        application.reset();
        for (const char *name : {"layout.json", "layout.json.bak", "layouts.json", "LastSession.txt", "hikari.ini"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

    // A preset carries a mode (Translation's is Translation mode): choosing
    // it starts the mode, choosing another afterwards leaves it. Reset this
    // layout starts nothing, nor does showing a tab.
    void aPresetCarriesItsMode()
    {
        QCOMPARE(ui::WorkspaceLayoutController::presetMode(QStringLiteral("Translation")), QStringLiteral("Translation"));
        for (const char *name : {"Editing", "Timing", "Typesetting", "Unknown"})
            QVERIFY2(ui::WorkspaceLayoutController::presetMode(QLatin1String(name)).isEmpty(), name);
        start();
        auto &layout = application->workspaceLayout();
        QSignalSpy chosen(&layout, &ui::WorkspaceLayoutController::presetModeChosen);
        QSignalSpy left(&layout, &ui::WorkspaceLayoutController::presetModeLeft);
        applyPreset("Timing");
        QCOMPARE(chosen.size(), 0);
        applyPreset("Translation");
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.first().first().toString(), QStringLiteral("Translation"));
        QVERIFY(layout.resetCurrentLayout());
        QCOMPARE(chosen.size(), 1);
        application->addPage(); // a copy of this tab's arrangement
        application->selectTab(0);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(left.size(), 0);
        applyPreset("Typesetting");
        QCOMPARE(left.size(), 1);
        QCOMPARE(left.first().first().toString(), QStringLiteral("Translation"));
        // A saved layout made from the Translation preset carries its mode.
        applyPreset("Translation");
        QVERIFY(layout.saveLayoutAs(QStringLiteral("My translation")));
        applyPreset("Editing");
        const auto before = chosen.size();
        QVERIFY(layout.applySavedLayout(QStringLiteral("My translation")));
        QCOMPARE(chosen.size(), before + 1);
        stop();
    }

    // A TLMode file opens in the Translation preset (WS1) with the Original
    // text panel above the Line editor: the Line editor's field is the
    // Translated text, and the panel's header holds the translation buttons
    // and toggles. The Translator mode check box is gone.
    void aTranslationFileShowsTheOriginalTextPanel()
    {
        start();
        openFile("tl.ass", true);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(translationMode());
        QVERIFY(!asked()); // the mode is on already
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(originalInPanel());
        QTRY_VERIFY(item("originalPanel")->isVisible());
        QCOMPARE(item("lineText")->property("text").toString(), QStringLiteral("line 1"));
        QCOMPARE(item("translationText")->property("text").toString(), QStringLiteral("linia 1"));
        QVERIFY(item("translationText")->isVisible());
        QVERIFY(!item("lineTextSlot")->isVisible());
        // Above the Line editor, in the same column.
        auto *original = item("originalPanel");
        auto *editor = item("editorPanel");
        QTRY_VERIFY(original->mapToScene(QPointF(0, 0)).y() < editor->mapToScene(QPointF(0, 0)).y());
        QCOMPARE(original->mapToScene(QPointF(0, 0)).x(), editor->mapToScene(QPointF(0, 0)).x());
        // The header's toolbar slot holds the buttons, and the Line editor
        // does not repeat them.
        auto *buttons = item("originalTextButtons");
        QTRY_VERIFY(buttons->isVisible());
        QCOMPARE(buttons->parentItem()->parentItem()->objectName(), QStringLiteral("dockToolbarSlot"));
        QVERIFY(!item("translationButtons")->isVisible());
        for (const char *name : {"originalPasteAll", "originalPasteSelection", "originalCommentOut", "originalNotConfirmed",
                                 "originalMovingTags"})
            QVERIFY2(item(name)->isVisible() && !item(name)->property("text").toString().isEmpty(), name);
        QVERIFY(!item<QObject>("translatorMode"));
        // Paste all from the header.
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("originalPasteAll"), "clicked"));
        QTRY_COMPARE(item("translationText")->property("text").toString(), QStringLiteral("line 1"));
        stop();
    }

    // Choosing the Translation preset for a Document without TLMode asks
    // once: Turn on is one step and shows the Original text panel; Not now
    // keeps the arrangement without the mode and is not asked again for that
    // Document. Another preset keeps the mode on, the panel with it.
    void choosingTranslationAsksOnce()
    {
        start();
        openFile("a.ass", false);
        QCOMPARE(preset(), QStringLiteral("Editing"));
        applyPreset("Translation");
        answer(false);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(!translationMode());
        QVERIFY(!isOpen("originalDock"));
        applyPreset("Editing");
        applyPreset("Translation");
        QTest::qWait(100);
        QVERIFY(!asked());
        QVERIFY(!translationMode());

        // Another file asks again; Turn on.
        application->addPage();
        openFile("b.ass", false);
        applyPreset("Editing");
        applyPreset("Translation");
        answer(true);
        QVERIFY(translationMode());
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(originalInPanel());
        QCOMPARE(item("lineText")->property("text").toString(), QStringLiteral("line 1"));

        // Another preset: the mode stays on, the panel too, in its place.
        applyPreset("Editing");
        QVERIFY(translationMode());
        QVERIFY(!asked());
        QVERIFY(isOpen("audioDock"));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(originalInPanel());
        // The first tab, mode off, has the panel closed.
        application->selectTab(0);
        QVERIFY(!translationMode());
        QTRY_VERIFY(!isOpen("originalDock"));
        application->selectTab(1);
        QTRY_VERIFY(isOpen("originalDock"));
        stop();
    }

    // A plain-text Document cannot hold the mode: the preset arranges the
    // panels and asks nothing, and the command is disabled.
    void plainTextGetsTheArrangementOnly()
    {
        start();
        QFile f(dir.filePath(QStringLiteral("p.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("one\ntwo\n");
        f.close();
        QVERIFY(application->openFile(f.fileName()));
        applyPreset("Translation");
        QTest::qWait(100);
        QVERIFY(!asked());
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(!modeAction()->property("enabled").toBool());
        stop();
    }

    // Subtitles > Translation mode: on at once, without changing the
    // layout; off after today's question (No by default), closing the
    // Original text panel.
    void theMenuCommandSwitchesTheMode()
    {
        start();
        openFile("a.ass", false);
        auto *action = modeAction();
        QVERIFY(action->property("enabled").toBool());
        QVERIFY(!action->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_VERIFY(translationMode());
        QVERIFY(action->property("checked").toBool());
        QCOMPARE(preset(), QStringLiteral("Editing"));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(isOpen("audioDock"));

        auto *confirm = item<QObject>("translatorModeOffConfirm");
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_VERIFY(confirm->property("visible").toBool());
        QVERIFY(translationMode());
        QVERIFY(action->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "reject"));
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QVERIFY(translationMode());
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_VERIFY(confirm->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
        QTRY_VERIFY(!translationMode());
        QTRY_VERIFY(!isOpen("originalDock"));
        QVERIFY(!originalInPanel());
        QCOMPARE(preset(), QStringLiteral("Editing"));
        stop();
    }

    // Closed by hand, the Original text panel gives the Original text back
    // to the Line editor, with the buttons under it, until the panel is shown
    // again (View > Panels) or the arrangement changes.
    void aClosedPanelPutsTheOriginalBackInTheLineEditor()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(QMetaObject::invokeMethod(dock("originalDock"), "close"));
        QTRY_VERIFY(!isOpen("originalDock"));
        QVERIFY(!originalInPanel());
        QTRY_VERIFY(item("lineText")->isVisible());
        QVERIFY(item("translationButtons")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("originalDock")))));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(originalInPanel());
        QVERIFY(!item("translationButtons")->isVisible());
        stop();
    }

    // Tab in the Original text goes on to the Translated text in the Line
    // editor, and Shift+Tab comes back, as in legacy's one EditBox; nothing
    // is typed.
    void tabCrossesFromTheOriginalToTheTranslation()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(originalInPanel());
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QTRY_VERIFY(original->isVisible() && translated->isVisible());
        original->forceActiveFocus();
        QTRY_VERIFY(original->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_Tab);
        QTRY_VERIFY(translated->hasActiveFocus());
        QCOMPARE(translated->property("focusReason").toInt(), int(Qt::TabFocusReason));
        QTest::keyClick(window(), Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_VERIFY(original->hasActiveFocus());
        QCOMPARE(original->property("text").toString(), QStringLiteral("line 1"));
        QCOMPARE(translated->property("text").toString(), QStringLiteral("linia 1"));
        stop();
    }

    // A narrow Original text panel folds its header's buttons into one
    // "More" button whose menu holds them in the same order.
    void aNarrowHeaderFoldsTheButtons()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(item("originalTextButtons") && item("originalTextButtons")->isVisible());
        auto *tools = item("originalTextButtons")->parentItem();
        QVERIFY(!item("originalTextMore")->isVisible());
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        auto *floating = item("originalPanel")->window();
        QVERIFY(floating && floating != window());
        floating->setWidth(220);
        QTRY_VERIFY(tools->property("folded").toBool());
        QVERIFY(item("originalTextMore")->isVisible());
        QVERIFY(!item("originalTextButtons")->isVisible());
        auto *menu = item<QObject>("originalTextMoreMenu");
        QCOMPARE(menu->property("count").toInt(), 6); // five items and the separator
        stop();
    }
};

QTEST_MAIN(TranslationPresetTest)
#include "translation_preset_tests.moc"
