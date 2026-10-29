#include "hikari/app/application.h"

#include "docking.h"
#include "line_editor_controller.h"
#include "theme.h"
#include "workspace_layout.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace hikari;

namespace {

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

}

class TranslationPresetTest : public QObject {
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
        // MUST KILL: spell check off so its no-dictionary notice does not take the focus
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
    static QQuickItem *findItem(QQuickItem *from, const char *name)
    {
        if (from->objectName() == QLatin1String(name))
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = findItem(child, name))
                return found;
        return nullptr;
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
        for (const char *name : {"layout.json", "layout.json.bak", "layouts.json", "last-session.json", "settings.toml", "hotkeys.toml", "state.json"})
            QFile::remove(dir.filePath(QLatin1String(name)));
    }

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
        application->addPage();
        application->selectTab(0);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(left.size(), 0);
        applyPreset("Typesetting");
        QCOMPARE(left.size(), 1);
        QCOMPARE(left.first().first().toString(), QStringLiteral("Translation"));
        applyPreset("Translation");
        QVERIFY(layout.saveLayoutAs(QStringLiteral("My translation")));
        applyPreset("Editing");
        const auto before = chosen.size();
        QVERIFY(layout.applySavedLayout(QStringLiteral("My translation")));
        QCOMPARE(chosen.size(), before + 1);
        stop();
    }

    void aTranslationFileShowsTheOriginalTextPanel()
    {
        start();
        openFile("tl.ass", true);
        QCOMPARE(preset(), QStringLiteral("Translation"));
        QVERIFY(translationMode());
        QVERIFY(!asked());
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(originalInPanel());
        QTRY_VERIFY(item("originalPanel")->isVisible());
        QCOMPARE(item("lineText")->property("text").toString(), QStringLiteral("line 1"));
        QCOMPARE(item("translationText")->property("text").toString(), QStringLiteral("linia 1"));
        QVERIFY(item("translationText")->isVisible());
        QVERIFY(!item("lineTextSlot")->isVisible());
        auto *original = item("originalPanel");
        auto *editor = item("editorPanel");
        QTRY_VERIFY(original->mapToScene(QPointF(0, 0)).y() < editor->mapToScene(QPointF(0, 0)).y());
        QCOMPARE(original->mapToScene(QPointF(0, 0)).x(), editor->mapToScene(QPointF(0, 0)).x());
        auto *buttons = item("originalTextButtons");
        QTRY_VERIFY(buttons->isVisible());
        QCOMPARE(buttons->parentItem()->parentItem()->objectName(), QStringLiteral("dockToolbarSlot"));
        QVERIFY(!item("translationButtons")->property("active").toBool());
        for (const char *name : {"originalPasteAll", "originalPasteSelection", "originalCommentOut", "originalNotConfirmed",
                                 "originalMovingTags"})
            QVERIFY2(item(name)->isVisible() && !item(name)->property("text").toString().isEmpty(), name);
        QVERIFY(!item<QObject>("translatorMode"));
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("originalPasteAll"), "clicked"));
        QTRY_COMPARE(item("translationText")->property("text").toString(), QStringLiteral("line 1"));
        stop();
    }

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

        application->addPage();
        openFile("b.ass", false);
        applyPreset("Editing");
        applyPreset("Translation");
        answer(true);
        QVERIFY(translationMode());
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(originalInPanel());
        QCOMPARE(item("lineText")->property("text").toString(), QStringLiteral("line 1"));

        applyPreset("Editing");
        QVERIFY(translationMode());
        QVERIFY(!asked());
        QVERIFY(isOpen("audioDock"));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(originalInPanel());
        application->selectTab(0);
        QVERIFY(!translationMode());
        QTRY_VERIFY(!isOpen("originalDock"));
        application->selectTab(1);
        QTRY_VERIFY(isOpen("originalDock"));
        stop();
    }

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

    void aClosedPanelPutsTheOriginalBackInTheLineEditor()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(QMetaObject::invokeMethod(dock("originalDock"), "close"));
        QTRY_VERIFY(!isOpen("originalDock"));
        QVERIFY(!originalInPanel());
        QTRY_VERIFY(item("lineText")->isVisible());
        QVERIFY(item("translationButtons")->property("active").toBool());
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(dock("originalDock")))));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(!isFloating("originalDock"));
        QVERIFY(originalInPanel());
        QVERIFY(!item("translationButtons")->property("active").toBool());
        stop();
    }

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

    void tabCrossesIntoAnotherWindow()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QQuickWindow *floating = original->window();
        QVERIFY(floating && floating != window());
        floating->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), floating);
        original->forceActiveFocus();
        QTRY_VERIFY(original->hasActiveFocus());
        QTest::keyClick(floating, Qt::Key_Tab);
        QTRY_COMPARE(QGuiApplication::focusWindow(), window());
        QTRY_VERIFY(translated->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_COMPARE(QGuiApplication::focusWindow(), floating);
        QTRY_VERIFY(original->hasActiveFocus());
        stop();
    }

    void tabRaisesTheOtherTabOfTheGroup()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVariant placed;
        QVERIFY(QMetaObject::invokeMethod(window(), "placePanel", Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, QVariant::fromValue(dock("originalDock"))), Q_ARG(QVariant, 0),
                                          Q_ARG(QVariant, QVariant::fromValue(dock("editorDock")))));
        QVERIFY(placed.toBool());
        QVERIFY(originalInPanel());
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QTRY_VERIFY(original->isVisible());
        QTRY_VERIFY(!translated->isVisible());
        original->forceActiveFocus();
        QTRY_VERIFY(original->hasActiveFocus());
        QTest::keyClick(window(), Qt::Key_Tab);
        QTRY_VERIFY(translated->isVisible());
        QTRY_VERIFY(translated->hasActiveFocus());
        QVERIFY(!original->isVisible());
        QTest::keyClick(window(), Qt::Key_Backtab, Qt::ShiftModifier);
        QTRY_VERIFY(original->isVisible());
        QTRY_VERIFY(original->hasActiveFocus());
        QCOMPARE(translated->property("text").toString(), QStringLiteral("linia 1"));
        stop();
    }

    void tagButtonsActOnTheFieldFocusedLast()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        auto *original = item("lineText");
        auto *translated = item("translationText");
        auto *bold = findItem(window()->contentItem(), "tag_b");
        QVERIFY(bold);
        QTRY_VERIFY(translated->isVisible() && bold->isVisible());
        translated->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(translated->hasActiveFocus());
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        QQuickWindow *floating = original->window();
        QVERIFY(floating && floating != window());
        floating->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), floating);
        original->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(original->hasActiveFocus());
        window()->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), window());
        QTRY_VERIFY(translated->hasActiveFocus());
        QTest::mouseClick(window(), Qt::LeftButton, {}, bold->mapToScene(bold->boundingRect().center()).toPoint());
        QTRY_VERIFY(original->property("text").toString() != QStringLiteral("line 1"));
        QCOMPARE(translated->property("text").toString(), QStringLiteral("linia 1"));
        stop();
    }

    void onlyTheCommandAsksBeforeOff()
    {
        start();
        openFile("a.ass", false);
        application->addPage();
        openFile("tl.ass", true);
        QVERIFY(translationMode());
        auto *confirm = item<QObject>("translatorModeOffConfirm");
        application->selectTab(0);
        QTRY_VERIFY(!modeAction()->property("checked").toBool());
        QTest::qWait(100);
        QVERIFY(!confirm->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(modeAction(), "trigger"));
        QTRY_VERIFY(translationMode());
        QVERIFY(modeAction()->property("checked").toBool());
        QVERIFY(application->editor().undo());
        QTRY_VERIFY(!translationMode());
        QTRY_VERIFY(!modeAction()->property("checked").toBool());
        QTest::qWait(100);
        QVERIFY(!confirm->property("visible").toBool());
        stop();
    }

    void movingTagsFocusReachesTheMainWindow()
    {
        start();
        QFile f(dir.filePath(QStringLiteral("tags.ass")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLMode Original\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,{\\i1}Wall\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n");
        f.close();
        application->setMoveTags(false);
        QVERIFY(application->openFile(f.fileName()));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        QQuickWindow *floating = item("originalPanel")->window();
        QVERIFY(floating && floating != window());
        floating->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), floating);
        item("lineText")->forceActiveFocus();
        application->setMoveTags(true);
        QTRY_COMPARE(QGuiApplication::focusWindow(), window());
        QTRY_VERIFY(item("translationText")->hasActiveFocus());
        application->setMoveTags(false);
        stop();
    }

    void movingTagsFocusRaisesTheLineEditorTab()
    {
        start();
        QFile f(dir.filePath(QStringLiteral("tags.ass")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLMode Original\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,{\\i1}Wall\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n");
        f.close();
        application->setMoveTags(false);
        QVERIFY(application->openFile(f.fileName()));
        QTRY_VERIFY(isOpen("originalDock"));
        QVariant placed;
        QVERIFY(QMetaObject::invokeMethod(window(), "placePanel", Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, QVariant::fromValue(dock("originalDock"))), Q_ARG(QVariant, 0),
                                          Q_ARG(QVariant, QVariant::fromValue(dock("editorDock")))));
        QVERIFY(placed.toBool());
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QTRY_VERIFY(original->isVisible());
        QTRY_VERIFY(!translated->isVisible());
        original->forceActiveFocus();
        application->setMoveTags(true);
        QTRY_VERIFY(translated->isVisible());
        QTRY_VERIFY(translated->hasActiveFocus());
        application->setMoveTags(false);
        stop();
    }

    void alignmentFocusReachesTheFloatingOriginal()
    {
        start();
        QFile f(dir.filePath(QStringLiteral("empty.ass")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLMode Original\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,Wall\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,\n");
        f.close();
        QVERIFY(application->openFile(f.fileName()));
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        auto *original = item("lineText");
        QQuickWindow *floating = original->window();
        QVERIFY(floating && floating != window());
        window()->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), window());
        auto *translated = item("translationText");
        translated->forceActiveFocus();
        QTRY_VERIFY(translated->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("alignmentChoice"), "chosen"));
        QTRY_COMPARE(QGuiApplication::focusWindow(), floating);
        QTRY_VERIFY(original->hasActiveFocus());
        stop();
    }

    void theOffQuestionActivatesTheMainWindow()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        QQuickWindow *floating = item("originalPanel")->window();
        QVERIFY(floating && floating != window());
        floating->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), floating);
        auto *confirm = item<QObject>("translatorModeOffConfirm");
        QVERIFY(QMetaObject::invokeMethod(modeAction(), "trigger"));
        QTRY_VERIFY(confirm->property("visible").toBool());
        auto *box = qobject_cast<QQuickWindow *>(confirm);
        QVERIFY(box);
        QTRY_COMPARE(QGuiApplication::focusWindow(), box);
        QTest::keyClick(box, Qt::Key_Escape);
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QVERIFY(translationMode());
        stop();
    }

    void theOriginalPanelOpensInTranslationModeOnly()
    {
        start();
        openFile("a.ass", false);
        QVERIFY(!translationMode());
        auto *original = dock("originalDock");
        auto *grid = dock("gridDock");
        QVariant placed;
        QVERIFY(QMetaObject::invokeMethod(window(), "placePanel", Q_RETURN_ARG(QVariant, placed),
                                          Q_ARG(QVariant, QVariant::fromValue(original)), Q_ARG(QVariant, 3),
                                          Q_ARG(QVariant, QVariant::fromValue(grid))));
        QVERIFY(!placed.toBool());
        QVERIFY(QMetaObject::invokeMethod(window(), "showPanel", Q_ARG(QVariant, QVariant::fromValue(original))));
        QTest::qWait(100);
        QVERIFY(!isOpen("originalDock"));
        auto *placement = window()->findChild<QQuickWindow *>(QStringLiteral("placementWindow"));
        QVERIFY(placement);
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("movePanel"), "triggered"));
        QTRY_VERIFY(placement->isVisible());
        auto *panelBox = findItem(placement->contentItem(), "placementPanel");
        auto *move = findItem(placement->contentItem(), "placementMove");
        QVERIFY(panelBox && move);
        const auto docks = window()->property("dockList").toList();
        int originalIndex = -1;
        for (int i = 0; i < docks.size(); ++i)
            if (docks[i].value<QObject *>() == original)
                originalIndex = i;
        QVERIFY(originalIndex >= 0);
        panelBox->setProperty("currentIndex", originalIndex);
        QVERIFY(!move->isEnabled());
        panelBox->setProperty("currentIndex", originalIndex == 0 ? 1 : 0);
        QVERIFY(move->isEnabled());
        placement->hide();
        stop();
    }

    void anotherTabClosesTheModeQuestions()
    {
        start();
        openFile("a.ass", false);
        application->addPage();
        openFile("tl.ass", true);
        QVERIFY(translationMode());
        auto *confirm = item<QObject>("translatorModeOffConfirm");
        QVERIFY(QMetaObject::invokeMethod(modeAction(), "trigger"));
        QTRY_VERIFY(confirm->property("visible").toBool());
        application->selectTab(0);
        QTRY_VERIFY(!confirm->property("visible").toBool());
        application->selectTab(1);
        QVERIFY(translationMode());

        application->selectTab(0);
        applyPreset("Translation");
        QTRY_VERIFY(asked());
        application->selectTab(1);
        QTRY_VERIFY(!asked());
        application->selectTab(0);
        QVERIFY(!translationMode());
        applyPreset("Editing");
        applyPreset("Translation");
        QTRY_VERIFY(asked());
        answer(false);
        stop();
    }

    void theOriginalJoinsAFloatingLineEditor()
    {
        start();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(QMetaObject::invokeMethod(dock("originalDock"), "close"));
        QTRY_VERIFY(!isOpen("originalDock"));
        QVERIFY(dock("editorDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("editorDock"));
        QQuickWindow *floating = item("editorPanel")->window();
        QVERIFY(floating && floating != window());
        QVERIFY(QMetaObject::invokeMethod(window(), "addOriginal"));
        QTRY_VERIFY(isOpen("originalDock"));
        QTRY_COMPARE(item("originalPanel")->window(), floating);
        QVERIFY(item("originalPanel")->window() != window());
        QVERIFY(item("originalPanel")->mapToScene({0, 0}).y() < item("editorPanel")->mapToScene({0, 0}).y());
        stop();
    }

    void anotherTabKeepsItsOriginalPanel()
    {
        start();
        openFile("a.ass", false);
        application->addPage();
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        QVERIFY(dock("originalDock")->setProperty("isFloating", true));
        QTRY_VERIFY(isFloating("originalDock"));
        application->selectTab(0);
        QTRY_VERIFY(!isOpen("originalDock"));
        QSignalSpy opened(dock("originalDock"), SIGNAL(isOpenChanged(bool)));
        application->selectTab(1);
        QTRY_VERIFY(isOpen("originalDock"));
        QTest::qWait(200);
        for (const auto &change : std::as_const(opened))
            QVERIFY2(change.at(0).toBool(), "closed on the way in");
        QVERIFY(isFloating("originalDock"));
        stop();
    }

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
        QCOMPARE(menu->property("count").toInt(), 6);
        stop();
    }

    void translationModeScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        start();
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        openFile("tl.ass", true);
        QTRY_VERIFY(isOpen("originalDock"));
        for (const auto code : ui::theme::kCodes) {
            const QString theme = ui::theme::codeName(code);
            settings.setValue(QStringLiteral("appearance.theme"), theme);
            auto *controls = window()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
            const auto shot = [&](QQuickWindow *w, const QString &state) {
                QTest::qWait(300);
                QVERIFY(w->grabWindow().save(out + QStringLiteral("/translation-") + state + QLatin1Char('-') + theme
                                             + QStringLiteral(".png")));
            };
            applyPreset("Translation");
            QTRY_VERIFY(isOpen("originalDock"));
            item("translationText")->forceActiveFocus();
            shot(window(), QStringLiteral("preset"));
            applyPreset("Editing");
            QTRY_VERIFY(isOpen("originalDock"));
            shot(window(), QStringLiteral("editing"));
            QVERIFY(dock("originalDock")->setProperty("isFloating", true));
            QTRY_VERIFY(isFloating("originalDock"));
            QQuickWindow *floating = item("originalPanel")->window();
            floating->resize(260, 140);
            QTRY_VERIFY(item("originalTextMore")->isVisible());
            shot(floating, QStringLiteral("folded"));
            QVERIFY(dock("originalDock")->setProperty("isFloating", false));
            QTRY_VERIFY(!isFloating("originalDock"));
        }
        for (const auto code : ui::theme::kCodes) {
            const QString theme = ui::theme::codeName(code);
            settings.setValue(QStringLiteral("appearance.theme"), theme);
            auto *controls = window()->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
            application->addPage();
            openFile(qPrintable(QStringLiteral("plain-%1.ass").arg(theme)), false);
            applyPreset("Editing");
            applyPreset("Translation");
            QTRY_VERIFY(asked());
            QTest::qWait(300);
            QVERIFY(window()->grabWindow().save(out + QStringLiteral("/translation-question-") + theme + QStringLiteral(".png")));
            answer(false);
        }
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark"));
        stop();
    }

    void tagsFromOriginalScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        for (const char *theme : {"dark", "light"}) {
            start();
            auto &settings = *application->settingsStore();
            settings.setValue(QStringLiteral("appearance.followSystem"), false);
            settings.setValue(QStringLiteral("appearance.theme"), QLatin1String(theme));
            QFile f(dir.filePath(QStringLiteral("tags-%1.ass").arg(QLatin1String(theme))));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLMode Original\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,{\\an8}I love {\\i1}Tokyo{\\i0} and {\\b1}you{\\b0}\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}J'adore Tokyo et toi\n"
                    "Comment: 0,0:00:03.00,0:00:04.00,TLMode Original,,0,0,0,,{\\i1}Hello {\\b1}world{\\b0}{\\i0}\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\i1}Hola {\\b1}mundo{\\b0}{\\i0}\n"
                    "Comment: 0,0:00:05.00,0:00:06.00,TLMode Original,,0,0,0,,text0{\\i1}text1{\\i0\\b1}text2{\\b0}text3\n"
                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,texte0 texte1 texte2\n");
            f.close();
            QVERIFY(application->openFile(f.fileName()));
            QTRY_VERIFY(isOpen("originalDock"));
            applyPreset("Translation");
            QTRY_VERIFY(!application->editor().tagRuns().isEmpty());
            item("translationText")->forceActiveFocus();
            QTest::qWait(400);
            QVERIFY(window()->grabWindow().save(out + QStringLiteral("/tags-suggested-%1.png").arg(QLatin1String(theme))));
            QVERIFY(application->editor().acceptSuggestedRuns());
            const int toi = item("translationText")->property("text").toString().lastIndexOf(QStringLiteral("toi"));
            QVERIFY(toi > 0);
            QVERIFY(application->editor().placeRun(2, toi, toi + 3));
            QTest::qWait(400);
            QVERIFY(window()->grabWindow().save(out + QStringLiteral("/tags-placed-%1.png").arg(QLatin1String(theme))));
            stop();
        }
    }

    void tagsFromOriginalLibassScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        struct Case {
            const char *name, *original, *translation;
            std::function<void(ui::LineEditorController &)> steps;
        };
        const Case cases[] = {
            {"fsp-no-digits", "a {\\fspabc}b{\\fsp0} c", "x b y", {}},
            {"reset-missing-style", "a {\\rGone}b{\\r} c", "x b y", {}},
            {"spaced-transform", "a {\\ t(\\fs20)\\ t(\\bord2)}B{\\r} c", "x Y z",
             [](ui::LineEditorController &e) { e.placeRun(1, 2, 3); }},
            {"adjacent-transforms", "a{\\t(\\fs30)}A{\\t(\\bord2)}B{\\r}c", "XY",
             [](ui::LineEditorController &e) {
                 e.placeRun(1, 0, 1);
                 e.placeRun(2, 1, 2);
             }},
            {"reset-between-runs", "a{\\fs30}b{\\b1}c{\\fs20}d{\\r}z", "AB",
             [](ui::LineEditorController &e) {
                 e.placeRun(1, 0, 1);
                 e.placeRun(2, 1, 2);
                 e.applyTagButton(1, QStringLiteral("{\\r}"), 2, 1, 1);
                 e.setShowTags(true);
             }},
        };
        for (const auto &c : cases) {
            start();
            auto &settings = *application->settingsStore();
            settings.setValue(QStringLiteral("appearance.followSystem"), false);
            settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark"));
            QFile f(dir.filePath(QStringLiteral("tm2-%1.ass").arg(QLatin1String(c.name))));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: TLMode Original\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
            f.write(QStringLiteral("Comment: 0,0:00:01.00,0:00:02.00,TLMode Original,,0,0,0,,%1\n"
                                   "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,%2\n")
                        .arg(QLatin1String(c.original), QLatin1String(c.translation))
                        .toUtf8());
            f.close();
            QVERIFY(application->openFile(f.fileName()));
            QTRY_VERIFY(isOpen("originalDock"));
            applyPreset("Translation");
            application->editor().setShowTags(false);
            QTRY_VERIFY(application->editor().moveTags());
            item("translationText")->forceActiveFocus();
            if (c.steps)
                c.steps(application->editor());
            QTest::qWait(400);
            const auto scene = [&](const char *name) {
                auto *it = item(name);
                return QRectF(it->mapToScene(QPointF()), it->size());
            };
            QRectF area = scene("translationText").united(scene("runStrip")).adjusted(-8, -8, 8, 8);
            const qreal ratio = window()->devicePixelRatio();
            area = QRectF(area.topLeft() * ratio, area.size() * ratio);
            QVERIFY(window()->grabWindow().copy(area.toAlignedRect()).save(
                out + QStringLiteral("/tm2-%1.png").arg(QLatin1String(c.name))));
            stop();
        }
    }
};

QTEST_MAIN(TranslationPresetTest)
#include "translation_preset_tests.moc"
