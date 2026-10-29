#include "hikari/app/application.h"
#include "hikari/app/localisation.h"
#include "hikari/app/style_manager_controller.h"
#include "hikari/application/hotkeys.h"

#include "docking.h"
#include "line_editor_controller.h"
#include "settings_store.h"
#include "tag_buttons_controller.h"

#include <QAccessible>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace hikari;

namespace {

QString writeFile(const QTemporaryDir &dir, const char *name, const char *events)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
            "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
            "MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
            "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
            "\n[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    f.write(events);
    return path;
}

}

class LineEditorOptionsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString tagged;
    QString signLine;
    QString missingStyle;
    std::unique_ptr<app::Application> application;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

    QString settingsFile() const { return dir.filePath(QStringLiteral("settings.toml")); }
    void start()
    {
        ui::SettingsStore(settingsFile()).set("editor.spellchecker", false);
        app::Application::Options options;
        options.settingsFile = settingsFile();
        options.playbackAudio = false; // CI runners without an audio device
        application = std::make_unique<app::Application>(options);
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
        engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    void stop()
    {
        engine.reset();
        application.reset();
        window = nullptr;
    }
    void restart()
    {
        stop();
        start();
    }
    QObject *root() const { return engine->rootObjects().first(); }
    QObject *named(const char *name) const
    {
        if (auto *found = root()->findChild<QObject *>(QLatin1String(name)))
            return found;
        for (QObject *o : root()->findChildren<QObject *>())
            if (o->objectName() == QLatin1String(name))
                return o;
        auto *loader = root()->findChild<QObject *>(QStringLiteral("settingsLoader"));
        if (auto *settings = loader ? loader->property("item").value<QObject *>() : nullptr) {
            if (settings->objectName() == QLatin1String(name))
                return settings;
            if (auto *found = settings->findChild<QObject *>(QLatin1String(name)))
                return found;
        }
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
    // A Repeater's old delegates stay QObject children until deleted: search the item tree first.
    QQuickItem *item(const char *name) const
    {
        if (auto *found = findItem(window->contentItem(), QLatin1String(name)))
            return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *q = qobject_cast<QQuickWindow *>(w))
                if (auto *found = findItem(q->contentItem(), QLatin1String(name)))
                    return found;
        for (QWindow *w : QGuiApplication::allWindows())
            if (auto *found = w->findChild<QQuickItem *>(QLatin1String(name)))
                return found;
        return nullptr;
    }
    bool showLine(const QString &file)
    {
        if (!application->openFile(file))
            return false;
        item("editingGrid")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Home);
        return QTest::qWaitFor([&] { return application->editor().hasLine(); });
    }
    QString shownText() const { return item("lineText")->property("text").toString(); }
    bool runGlobal(const QString &symbol)
    {
        QVariant ran;
        return QMetaObject::invokeMethod(window, "runGlobalHotkey", Q_RETURN_ARG(QVariant, ran), Q_ARG(QVariant, symbol))
               && ran.toBool();
    }
    void clickSettingAndOk(const char *setting, bool expectedBefore)
    {
        auto *action = named("settingsMenuItem")->property("action").value<QObject *>();
        QVERIFY(action && QMetaObject::invokeMethod(action, "trigger"));
        auto *dialog = named("settingsDialog");
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *content = dialog->property("contentItem").value<QQuickItem *>();
        auto *check = findItem(content, QStringLiteral("setting_") + QLatin1String(setting));
        QVERIFY(check);
        QCOMPARE(check->property("checked").toBool(), expectedBefore);
        QVERIFY(QMetaObject::invokeMethod(check, "click"));
        auto *ok = findItem(content, QStringLiteral("settingsOk"));
        QVERIFY(ok && QMetaObject::invokeMethod(ok, "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

    // A Popup is not an item: its controls live under its contentItem.
    static QQuickItem *inside(QObject *popup, const char *name)
    {
        auto *content = popup ? popup->property("contentItem").value<QQuickItem *>() : nullptr;
        if (!content)
            return nullptr;
        return findItem(content, QLatin1String(name));
    }
    void click(QQuickItem *control, Qt::MouseButton button = Qt::LeftButton)
    {
        QVERIFY(control);
        auto *w = control->window();
        QTRY_VERIFY(control->isVisible());
        QTest::mouseClick(w, button, {}, control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
    }
    void typeInto(QQuickItem *field, const QString &text)
    {
        QVERIFY(field);
        field->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(field, "selectAll"));
        QTest::keyClick(field->window(), Qt::Key_Backspace);
        for (const QChar c : text) // keyClicks takes widgets only
            QTest::keyClick(field->window(), c.toLatin1());
        QCOMPARE(field->property("text").toString(), text);
    }
    void fillTagButton(QObject *popover, const QString &name, const QString &tag, const char *icon = nullptr)
    {
        QTRY_VERIFY(popover->property("opened").toBool());
        typeInto(inside(popover, "tagName"), name);
        typeInto(inside(popover, "tagText"), tag);
        if (icon)
            click(inside(popover, icon));
        click(inside(popover, "saveTag"));
        QTRY_VERIFY(!popover->property("visible").toBool());
    }
    void choose(QObject *menu, const char *item)
    {
        auto *entry = named(item);
        QVERIFY(entry);
        QVERIFY(entry->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QVERIFY(QMetaObject::invokeMethod(entry, "triggered"));
    }
    void waitForStrip(int count)
    {
        QTRY_VERIFY([&] {
            for (int i = 0; i < count; ++i) {
                auto *b = item(qPrintable(QStringLiteral("tagButton%1").arg(i)));
                if (!b || !b->isVisible())
                    return false;
                if (i > 0) {
                    auto *a = item(qPrintable(QStringLiteral("tagButton%1").arg(i - 1)));
                    if (b->mapToScene({}).x() < a->mapToScene({}).x() + a->width())
                        return false;
                }
            }
            auto *last = item(qPrintable(QStringLiteral("tagButton%1").arg(count - 1)));
            auto *add = item("addTagButton");
            return !add || !add->isVisible() || last->mapToScene({}).x() + last->width() <= add->mapToScene({}).x();
        }());
    }
    QQuickItem *showing(const QString &name, int count) const
    {
        for (int i = 0; i < count; ++i) {
            auto *b = item(qPrintable(QStringLiteral("tagButton%1").arg(i)));
            if (b && b->isVisible() && b->property("text").toString() == name)
                return b;
        }
        return nullptr;
    }
    QString stripFromLeft(int count) const
    {
        QList<QQuickItem *> shown;
        for (int i = 0; i < count; ++i) {
            auto *b = item(qPrintable(QStringLiteral("tagButton%1").arg(i)));
            if (!b || !b->isVisible())
                return {};
            shown.append(b);
        }
        std::sort(shown.begin(), shown.end(),
                  [](QQuickItem *a, QQuickItem *b) { return a->mapToScene({}).x() < b->mapToScene({}).x(); });
        QString names;
        for (int i = 0; i < count; ++i) {
            if (i > 0 && shown[i]->mapToScene({}).x() < shown[i - 1]->mapToScene({}).x() + shown[i - 1]->width())
                return {};
            names += shown[i]->property("text").toString();
        }
        return names;
    }
    QQuickWindow *styleWindow() const { return root()->findChild<QQuickWindow *>(QStringLiteral("styleManager")); }

private slots:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        tagged = writeFile(dir, "tagged.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}Hi{\\i0} there\n");
        signLine = writeFile(dir, "sign.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,sign\n");
        missingStyle = writeFile(dir, "missing.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Karaoke,,0,0,0,,sung\n");
    }
    void init()
    {
        QFile::remove(settingsFile());
        start();
    }
    void cleanup() { stop(); }

    void framesSwitchLeftTheEditorAndPersists()
    {
        QVERIFY(showLine(tagged));
        QVERIFY(!item("showTimes"));
        QVERIFY(!item("showFrames"));
        QVERIFY(item("charsCounter"));
        QVERIFY(item("cpsCounter"));
        QVERIFY(!application->editor().showFrames());
        application->editor().setShowFrames(true);
        QVERIFY(application->settingsStore()->boolean("editor.timesToFramesSwitch"));
        restart();
        QVERIFY(application->editor().showFrames());
        clickSettingAndOk("editor.timesToFramesSwitch", true);
        QVERIFY(!application->editor().showFrames());
        restart();
        QVERIFY(!application->editor().showFrames());
    }

    void tagsShownByDefaultAndTheChoicePersists()
    {
        QVERIFY(!application->settingsStore()->contains("editor.hideTags"));
        QVERIFY(application->editor().showTags());
        QVERIFY(showLine(tagged));
        QCOMPARE(shownText(), QStringLiteral("{\\i1}Hi{\\i0} there"));
        QVERIFY(!item("showTags"));
        auto *action = named("hideEditorTagsMenuItem")->property("action").value<QObject *>();
        QVERIFY(action);
        QVERIFY(!action->property("checked").toBool());
        QVERIFY(runGlobal(QStringLiteral("GLOBAL_EDITOR_HIDE_TAGS")));
        QVERIFY(!application->editor().showTags());
        QTRY_COMPARE(shownText(), QStringLiteral("Hi there"));
        QVERIFY(action->property("checked").toBool());
        QVERIFY(application->settingsStore()->boolean("editor.hideTags"));
        QVERIFY(!application->settingsStore()->boolean("grid.hideTags"));
        restart();
        QVERIFY(!application->editor().showTags());
        QVERIFY(showLine(tagged));
        QCOMPARE(shownText(), QStringLiteral("Hi there"));
        clickSettingAndOk("editor.hideTags", true);
        QVERIFY(application->editor().showTags());
        QTRY_COMPARE(shownText(), QStringLiteral("{\\i1}Hi{\\i0} there"));
        action = named("hideEditorTagsMenuItem")->property("action").value<QObject *>();
        QVERIFY(!action->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QVERIFY(!application->editor().showTags());
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QVERIFY(application->editor().showTags());
        restart();
        QVERIFY(application->editor().showTags());
    }

    void hideTagsCommandIsBindable()
    {
        QVERIFY(showLine(tagged));
        const int id = application::hotkeyIdOf("GLOBAL_EDITOR_HIDE_TAGS");
        QVERIFY(id > 0);
        QCOMPARE(QString::fromStdString(application::hotkeyName(id)), QStringLiteral("Hide tags in the line editor"));
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_EDITOR_HIDE_TAGS"), 0), QString());
        application->hotkeys().gestureMap(id, QStringLiteral("Hide tags in the line editor"), QStringLiteral("Ctrl-Shift-Y"),
                                          0, QStringLiteral("cancel"));
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_EDITOR_HIDE_TAGS"), 0),
                 QStringLiteral("Ctrl-Shift-Y"));
        QTRY_COMPARE(named("hideEditorTagsMenuItem")->property("shortcutText").toString(), QStringLiteral("Ctrl-Shift-Y"));
        item("editingGrid")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!application->editor().showTags());
        QTest::keyClick(window, Qt::Key_Y, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(application->editor().showTags());
        restart();
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_EDITOR_HIDE_TAGS"), 0),
                 QStringLiteral("Ctrl-Shift-Y"));
    }

    void tagButtonsFromThePopoverAndTheMenu()
    {
        QVERIFY(showLine(tagged));
        auto &buttons = application->tagButtons();
        QCOMPARE(buttons.count(), 0);
        QVERIFY(!item("manageTagButtons"));
        auto *popover = named("tagButtonEditor");
        QVERIFY(popover);
        click(item("addTagButton"));
        QTRY_VERIFY(popover->property("opened").toBool());
        typeInto(inside(popover, "tagName"), QStringLiteral("Blur"));
        typeInto(inside(popover, "tagText"), QStringLiteral("blur2"));
        QTRY_VERIFY(inside(popover, "tagProblem")->isVisible());
        QVERIFY(!inside(popover, "saveTag")->isEnabled());
        QTest::keyClick(inside(popover, "tagText")->window(), Qt::Key_Return);
        QVERIFY(popover->property("visible").toBool());
        QCOMPARE(buttons.count(), 0);
        typeInto(inside(popover, "tagText"), QStringLiteral("\\blur2"));
        QVERIFY(!inside(popover, "tagProblem")->isVisible());
        QCOMPARE(inside(popover, "tagPreview")->property("text").toString(), QStringLiteral("Blur"));
        click(inside(popover, "saveTag"));
        QTRY_COMPARE(buttons.count(), 1);
        QCOMPARE(buttons.button(0).tag, QStringLiteral("\\blur2"));
        click(item("addTagButton"));
        fillTagButton(popover, QStringLiteral("Bold"), QStringLiteral("\\b1"), "tagIcon_tag-bold");
        QCOMPARE(buttons.count(), 2);
        QTRY_VERIFY(item("tagButton1"));
        QCOMPARE(item("tagButton1")->property("iconRole").toString(), QStringLiteral("tag-bold"));
        QCOMPARE(item("tagButton1")->property("text").toString(), QStringLiteral("Bold"));
        auto *menu = named("tagButtonMenu");
        QVERIFY(menu);
        waitForStrip(2);
        click(item("tagButton1"), Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(!named("tagButtonMoveRight")->property("enabled").toBool());
        choose(menu, "tagButtonMoveLeft");
        QTRY_COMPARE(buttons.button(0).name, QStringLiteral("Bold"));
        QCOMPARE(buttons.button(1).name, QStringLiteral("Blur"));
        QTRY_COMPARE(item("tagButton1")->property("text").toString(), QStringLiteral("Blur"));
        waitForStrip(2);
        click(item("tagButton1"), Qt::RightButton);
        QTRY_VERIFY(menu->property("opened").toBool());
        choose(menu, "tagButtonEdit");
        QTRY_VERIFY(popover->property("opened").toBool());
        QCOMPARE(inside(popover, "tagName")->property("text").toString(), QStringLiteral("Blur"));
        QCOMPARE(inside(popover, "tagText")->property("text").toString(), QStringLiteral("\\blur2"));
        fillTagButton(popover, QStringLiteral("Soft"), QStringLiteral("\\blur4"));
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.button(1).name, QStringLiteral("Soft"));
        QCOMPARE(buttons.button(1).tag, QStringLiteral("\\blur4"));
        restart();
        QVERIFY(showLine(tagged));
        QCOMPARE(application->tagButtons().count(), 2);
        QCOMPARE(application->tagButtons().button(0).icon, QStringLiteral("tag-bold"));
        QCOMPARE(application->tagButtons().button(1).name, QStringLiteral("Soft"));
        auto *menuAgain = named("tagButtonMenu");
        QTRY_COMPARE(item("tagButton0")->property("text").toString(), QStringLiteral("Bold"));
        waitForStrip(2);
        click(item("tagButton0"), Qt::RightButton);
        QTRY_VERIFY(menuAgain->property("opened").toBool());
        choose(menuAgain, "tagButtonRemove");
        QTRY_COMPARE(application->tagButtons().count(), 1);
        QCOMPARE(application->tagButtons().button(0).name, QStringLiteral("Soft"));
        QTRY_COMPARE(item("tagButton0")->property("text").toString(), QStringLiteral("Soft"));
        waitForStrip(1);
        item("lineText")->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(item("lineText"), "select", Q_ARG(int, 0), Q_ARG(int, 0)));
        click(item("tagButton0"));
        QTRY_VERIFY(shownText().contains(QStringLiteral("\\blur4")));
        QVERIFY(application->tagButtons().add(QString(), QStringLiteral("\\i1"), 0, QStringLiteral("tag-italic")));
        QTRY_VERIFY(item("tagButton1"));
        auto *iconOnly = QAccessible::queryAccessibleInterface(item("tagButton1"));
        QVERIFY(iconOnly);
        QCOMPARE(iconOnly->text(QAccessible::Name), QStringLiteral("\\i1"));
    }

    void tagButtonMovesFollowTheLayoutDirection_data()
    {
        QTest::addColumn<bool>("rightToLeft");
        QTest::newRow("left to right") << false;
        QTest::newRow("right to left") << true;
    }
    void tagButtonMovesFollowTheLayoutDirection()
    {
        QFETCH(bool, rightToLeft);
        if (rightToLeft)
            application->localisation().setPseudo(app::Localisation::Pseudo::RightToLeft);
        QCOMPARE(QGuiApplication::layoutDirection(), rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
        QVERIFY(showLine(tagged));
        auto &buttons = application->tagButtons();
        QVERIFY(buttons.add(QStringLiteral("A"), QStringLiteral("\\b1"), 0));
        QVERIFY(buttons.add(QStringLiteral("B"), QStringLiteral("\\i1"), 0));
        QVERIFY(buttons.add(QStringLiteral("C"), QStringLiteral("\\u1"), 0));
        QVERIFY(ui::Docking().resizeInLayout(QStringLiteral("Editor"), 600, 0, 0, 0));
        QTRY_COMPARE(stripFromLeft(3), rightToLeft ? QStringLiteral("CBA") : QStringLiteral("ABC"));
        auto *menu = named("tagButtonMenu");
        QVERIFY(menu);
        const auto openOn = [&](const QString &name) {
            QTRY_VERIFY(showing(name, 3));
            click(showing(name, 3), Qt::RightButton);
            QTRY_VERIFY(menu->property("opened").toBool());
        };
        openOn(QStringLiteral("B"));
        choose(menu, "tagButtonMoveLeft");
        QTRY_COMPARE(stripFromLeft(3), rightToLeft ? QStringLiteral("BCA") : QStringLiteral("BAC"));
        QCOMPARE(buttons.button(rightToLeft ? 2 : 0).name, QStringLiteral("B"));
        openOn(QStringLiteral("B"));
        QVERIFY(!named("tagButtonMoveLeft")->property("enabled").toBool());
        QVERIFY(named("tagButtonMoveRight")->property("enabled").toBool());
        choose(menu, "tagButtonMoveRight");
        QTRY_COMPARE(stripFromLeft(3), rightToLeft ? QStringLiteral("CBA") : QStringLiteral("ABC"));
        openOn(rightToLeft ? QStringLiteral("A") : QStringLiteral("C"));
        QVERIFY(named("tagButtonMoveLeft")->property("enabled").toBool());
        QVERIFY(!named("tagButtonMoveRight")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    }

    void tagButtonsListInSettings()
    {
        auto &buttons = application->tagButtons();
        QVERIFY(buttons.add(QStringLiteral("One"), QStringLiteral("\\b1"), 0));
        QVERIFY(buttons.add(QStringLiteral("Two"), QStringLiteral("\\i1"), 0, QStringLiteral("tag-italic")));
        auto *action = named("settingsMenuItem")->property("action").value<QObject *>();
        QVERIFY(action && QMetaObject::invokeMethod(action, "trigger"));
        auto *dialog = named("settingsDialog");
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *content = dialog->property("contentItem").value<QQuickItem *>();
        auto *list = findItem(content, QStringLiteral("settingsTagButtonList"));
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 2);
        list->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(findItem(content, QStringLiteral("settingsMoveTagButtonUp")), "click"));
        QCOMPARE(list->property("currentIndex").toInt(), 0);
        list->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(findItem(content, QStringLiteral("settingsRemoveTagButton")), "click"));
        QCOMPARE(list->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(findItem(content, QStringLiteral("settingsAddTagButton")), "click"));
        auto *popover = named("settingsTagButtonEditor");
        QVERIFY(popover);
        fillTagButton(popover, QStringLiteral("Three"), QStringLiteral("\\u1"), "tagIcon_tag-underline");
        QCOMPARE(list->property("count").toInt(), 2);
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.button(0).name, QStringLiteral("One"));
        QVERIFY(QMetaObject::invokeMethod(findItem(content, QStringLiteral("settingsOk")), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(buttons.count(), 2);
        QCOMPARE(buttons.button(0).name, QStringLiteral("Two"));
        QCOMPARE(buttons.button(0).icon, QStringLiteral("tag-italic"));
        QCOMPARE(buttons.button(1).name, QStringLiteral("Three"));
        QCOMPARE(buttons.button(1).icon, QStringLiteral("tag-underline"));
        restart();
        QCOMPARE(application->tagButtons().count(), 2);
        QCOMPARE(application->tagButtons().button(1).tag, QStringLiteral("\\u1"));
    }

    void editStyleOpensTheLinesStyle()
    {
        QVERIFY(showLine(signLine));
        auto *styles = styleWindow();
        QVERIFY(styles);
        QVERIFY(QMetaObject::invokeMethod(styles, "editStyle", Q_ARG(QVariant, QStringLiteral("Default"))));
        QTRY_VERIFY(styles->isVisible());
        QVERIFY(styles->property("editing").toBool());
        auto *nameField = findItem(styles->contentItem(), QStringLiteral("styleName"));
        QVERIFY(nameField);
        QCOMPARE(nameField->property("text").toString(), QStringLiteral("Default"));
        QVERIFY(QMetaObject::invokeMethod(item("styleEditButton"), "click"));
        QTRY_COMPARE(nameField->property("text").toString(), QStringLiteral("Sign"));
        QVERIFY(styles->property("editing").toBool());
        QCOMPARE(styles->property("assSelected").toList().value(0).toInt(), 1);
        QTRY_VERIFY(nameField->hasActiveFocus());
        styles->close();
        QVERIFY(QMetaObject::invokeMethod(item("styleEditButton"), "click"));
        QTRY_VERIFY(styles->isVisible());
        QTRY_COMPARE(nameField->property("text").toString(), QStringLiteral("Sign"));
        QTRY_VERIFY(nameField->hasActiveFocus());
        styles->close();
    }

    void styleManagerKeepsTheLastCatalog()
    {
        QVERIFY(application->styleManager().createCatalog(QStringLiteral("Kept")));
        QCOMPARE(application->styleManager().catalog(), QStringLiteral("Kept"));
        application->styleManager().saveCatalog();
        application->settingsStore()->set("convert.styleCatalog", QStringLiteral("Default"));
        application->openSettingsDialog();
        QCOMPARE(application->styleManager().catalog(), QStringLiteral("Default"));
        QCOMPARE(application->settingsStore()->text("styles.lastCatalog"), QStringLiteral("Kept"));
        application->resetSettingsPage(QVariantMap{}, QStringLiteral("conversion"));
        QCOMPARE(application->styleManager().catalog(), QStringLiteral("Default"));
        QCOMPARE(application->settingsStore()->text("styles.lastCatalog"), QStringLiteral("Kept"));
        restart();
        QCOMPARE(application->styleManager().catalog(), QStringLiteral("Kept"));
        QVERIFY(application->styleManager().deleteCatalog(QStringLiteral("Kept")));
        restart();
        QCOMPARE(application->styleManager().catalog(), QStringLiteral("Default"));
    }

    void editStyleOffersToCreateAMissingStyle()
    {
        QVERIFY(showLine(missingStyle));
        auto &controller = application->styleManager();
        QCOMPARE(controller.documentStyles(), (QStringList{QStringLiteral("Default"), QStringLiteral("Sign")}));
        auto *styles = styleWindow();
        const auto question = [styles] { return qvariant_cast<QQuickWindow *>(styles->findChild<QObject *>(QStringLiteral("styleMessages"))->property("box")); };
        const auto answer = [&](const char *id) {
            QQuickItem *button = findItem(question()->contentItem(), QStringLiteral("messageBoxChoice_") + QLatin1String(id));
            QVERIFY(button);
            QVERIFY(QMetaObject::invokeMethod(button, "click"));
            QTRY_VERIFY(!question()->isVisible());
        };
        QVERIFY(QMetaObject::invokeMethod(item("styleEditButton"), "click"));
        QTRY_VERIFY(question() && question()->isVisible());
        QCOMPARE(question()->property("text").toString(), QStringLiteral("This file has no style named \u201cKaraoke\u201d."));
        QVERIFY(styles->property("editing").toBool());
        answer("cancel");
        QCOMPARE(controller.documentStyles().size(), 2);
        QVERIFY(QMetaObject::invokeMethod(styles, "editStyle", Q_ARG(QVariant, QStringLiteral("Sign"))));
        QTRY_VERIFY(styles->property("editing").toBool());
        QVERIFY(QMetaObject::invokeMethod(item("styleEditButton"), "click"));
        QTRY_VERIFY(question()->isVisible());
        answer("cancel");
        QCOMPARE(styles->property("assSelected").toList().value(0).toInt(), 1);
        QCOMPARE(findItem(styles->contentItem(), QStringLiteral("styleName"))->property("text").toString(),
                 QStringLiteral("Sign"));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(item("styleEditButton"), "click"));
        QTRY_VERIFY(question()->isVisible());
        answer("create");
        QTRY_COMPARE(controller.documentStyles(),
                     (QStringList{QStringLiteral("Default"), QStringLiteral("Sign"), QStringLiteral("Karaoke")}));
        QCOMPARE(session->historySize(), steps + 1);
        QVERIFY(styles->property("editing").toBool());
        QCOMPARE(styles->property("assSelected").toList(), QVariantList{2});
        auto *nameField = findItem(styles->contentItem(), QStringLiteral("styleName"));
        QCOMPARE(nameField->property("text").toString(), QStringLiteral("Karaoke"));
        QTRY_VERIFY(nameField->hasActiveFocus());
        const auto values = styles->property("values").toMap();
        QCOMPARE(values.value(QStringLiteral("alignment")).toString(), QStringLiteral("2"));
        QCOMPARE(values.value(QStringLiteral("fontname")).toString(), QStringLiteral("Arial"));
        styles->close();
    }
};

QTEST_MAIN(LineEditorOptionsTest)
#include "line_editor_options_tests.moc"
