// AS1 (#471): a "/" in a macro's registered name opens submenus of the
// Automation menu, by Aegisub's rules (src/menu.cpp AutomationMenu), with the
// real Lua helper (lua-fixtures/nested-macros.lua). The nested items run
// their macro, a hotkey on a nested macro runs it, and the Automation panel
// and Settings → Hotkeys keep the full name.

#include "hikari/app/application.h"
#include "hikari/app/hotkeys_controller.h"
#include "docking.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#include <algorithm>

Q_IMPORT_QML_PLUGIN(Hikari_UiPlugin)

using namespace hikari;

namespace {

const QString kScript = QStringLiteral(HIKARI_LUA_FIXTURES "/nested-macros.lua");
constexpr auto kDeep = "Script nested-macros.lua-1"; // "Group/Sub/Deep"

QQuickItem *itemAt(QObject *menu, int index)
{
    QQuickItem *entry = nullptr;
    QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, index));
    return entry;
}

// A menu's entries from `from` on, one per line, a submenu's under it two
// spaces in: `menu "Title"` or `item "Text" (objectName)`.
void describe(QObject *menu, int from, int depth, QStringList &out)
{
    const QString indent(depth * 2, QLatin1Char(' '));
    for (int i = from; i < menu->property("count").toInt(); ++i) {
        QQuickItem *entry = itemAt(menu, i);
        if (!entry)
            continue;
        if (auto *submenu = entry->property("subMenu").value<QObject *>()) {
            out << indent + QStringLiteral("menu \"%1\" (%2)").arg(submenu->property("title").toString(), submenu->objectName());
            describe(submenu, 0, depth + 1, out);
        } else {
            out << indent + QStringLiteral("item \"%1\" (%2)").arg(entry->property("text").toString(), entry->objectName());
        }
    }
}

} // namespace

class AutomationSubmenusTests : public QObject {
    Q_OBJECT

    QTemporaryDir dir;
    std::unique_ptr<app::Application> app;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QObject *root = nullptr;
    QQuickWindow *window = nullptr;

    app::Application::Options options()
    {
        app::Application::Options o;
        o.settingsFile = dir.filePath(QStringLiteral("hikari.ini"));
        o.playbackAudio = false;
        return o;
    }
    QObject *named(const char *name) const { return root->findChild<QObject *>(QLatin1String(name)); }
    QString firstLine() const
    {
        const auto &text = app->files().session(*app->workspace().editingTarget())->document().lines().front()->text;
        return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), qsizetype(text.size()));
    }
    // The Automation menu's macros: what follows its separator.
    QStringList macroMenu() const
    {
        QObject *menu = named("automationMenu");
        int at = 0;
        while (at < menu->property("count").toInt() && itemAt(menu, at) != named("macrosSeparator"))
            ++at;
        QStringList out;
        describe(menu, at + 1, 0, out);
        return out;
    }

private slots:
    void init()
    {
        app = std::make_unique<app::Application>(options());
        const QString path = dir.filePath(QStringLiteral("as1.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Gate\n");
        f.close();
        QVERIFY(app->openFile(path));
        app->automation().load(kScript.toStdString());
        QDeadlineTimer deadline(30'000);
        const auto ready = [this] {
            return std::ranges::any_of(app->automation().scripts(), [](const auto &s) {
                return s.path == kScript.toStdString() && s.state == application::ScriptStatus::State::Ready;
            });
        };
        while (!ready() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(ready());
        engine = std::make_unique<QQmlApplicationEngine>();
        ui::attachDocking(*engine);
        engine->setInitialProperties(app->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        root = engine->rootObjects().first();
        window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(window);
        window->resize(1280, 800);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // An earlier case's session left open: No to the crash prompt, a
        // modal that keeps the macros' shortcuts off.
        if (auto *prompt = named("crashSessionPrompt"); prompt && prompt->property("visible").toBool())
            QVERIFY(QMetaObject::invokeMethod(prompt, "reject"));
        QTRY_VERIFY(!root->property("workspaceLayout").value<QObject *>()->property("modalOpen").toBool());
    }
    void cleanup()
    {
        engine.reset();
        root = nullptr;
        window = nullptr;
        app.reset();
    }

    // Each piece of a name but the last is a submenu; names sharing a prefix
    // share its submenus, in the order the first of them registered; empty
    // pieces stay (a submenu with no title, an item with no text); a macro
    // and a submenu of the same name both show, in either order.
    void nestedNamesMakeSubmenus()
    {
        const QStringList expected{
            QStringLiteral(R"(item "Flat" (macro_Flat))"),
            QStringLiteral(R"(menu "Group" (macroMenu_Group))"),
            QStringLiteral(R"(  menu "Sub" (macroMenu_Group/Sub))"),
            QStringLiteral(R"(    item "Deep" (macro_Group/Sub/Deep))"),
            QStringLiteral(R"(    item "Deeper" (macro_Group/Sub/Deeper))"),
            QStringLiteral(R"(  item "Shallow" (macro_Group/Shallow))"),
            QStringLiteral(R"(menu "Other" (macroMenu_Other))"),
            QStringLiteral(R"(  item "Item" (macro_Other/Item))"),
            QStringLiteral(R"(item "Group" (macro_Group))"),
            QStringLiteral(R"(item "Solo" (macro_Solo))"),
            QStringLiteral(R"(menu "Solo" (macroMenu_Solo))"),
            QStringLiteral(R"(  item "Child" (macro_Solo/Child))"),
            QStringLiteral(R"(menu "Gap" (macroMenu_Gap))"),
            QStringLiteral(R"(  menu "" (macroMenu_Gap/))"),
            QStringLiteral(R"(    item "Inner" (macro_Gap//Inner))"),
            QStringLiteral(R"(menu "Trailing" (macroMenu_Trailing))"),
            QStringLiteral(R"(  item "" (macro_Trailing/))"),
            QStringLiteral(R"(menu "" (macroMenu_))"),
            QStringLiteral(R"(  item "Leading" (macro_/Leading))"),
        };
        QTRY_COMPARE(macroMenu(), expected);

        // A nested item runs its macro.
        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        auto *deep = named("macro_Group/Sub/Deep");
        QVERIFY(deep);
        QVERIFY(QMetaObject::invokeMethod(deep, "triggered"));
        QVERIFY(done.wait(30'000));
        QVERIFY(done.first().at(0).toBool());
        QCOMPARE(firstLine(), QStringLiteral("Gate Group/Sub/Deep"));

        // Reloading the script builds the tree again, once.
        app->automation().reload(kScript.toStdString());
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(app->automation().scripts(), [](const auto &s) {
            return s.path == kScript.toStdString() && s.state == application::ScriptStatus::State::Ready;
        }), 30'000);
        QTRY_COMPARE(macroMenu(), expected);
    }

    // A hotkey addresses a nested macro by its full name and runs it from
    // the window; Settings → Hotkeys and the Automation panel show that
    // name whole.
    void aHotkeyRunsANestedMacro()
    {
        auto &h = app->hotkeys();
        const QVariantMap target = h.gestureTarget(QLatin1String(kDeep));
        QVERIFY2(target.value("name").toString().endsWith(QLatin1String("Group/Sub/Deep")), qPrintable(target.value("name").toString()));
        QVERIFY(h.gestureConflict(target.value("id").toInt(), QStringLiteral("Ctrl-Alt-J"), 0).isEmpty());
        h.gestureMap(target.value("id").toInt(), target.value("name").toString(), QStringLiteral("Ctrl-Alt-J"), 0,
                     QStringLiteral("cancel"));
        QCOMPARE(app->automationHotkeys().keys().value(QLatin1String(kDeep)).toString(), QStringLiteral("Ctrl+Alt+J"));

        QSignalSpy done(&app->automation(), &app::AutomationShell::runCompleted);
        QTRY_VERIFY(window->isActive());
        QTest::keyClick(window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(done.wait(30'000));
        QVERIFY(done.first().at(0).toBool());
        QCOMPARE(firstLine(), QStringLiteral("Gate Group/Sub/Deep"));

        h.beginOptions();
        h.filter(7);
        QStringList macros;
        for (const QVariant &v : h.rows())
            macros << v.toMap().value(QStringLiteral("macro")).toString();
        QVERIFY2(macros.contains(QStringLiteral("Group/Sub/Deep")), qPrintable(macros.join('\n')));

        QVERIFY(QMetaObject::invokeMethod(root, "showPanel", Q_ARG(QVariant, QVariant::fromValue(named("automationDock")))));
        QTRY_VERIFY(named("automationDock")->property("isOpen").toBool());
        const auto panelRow = [&] {
            QList<QQuickItem *> items;
            for (QWindow *w : QGuiApplication::topLevelWindows())
                if (auto *quick = qobject_cast<QQuickWindow *>(w))
                    items << quick->contentItem();
            while (!items.isEmpty()) {
                QQuickItem *row = items.takeLast();
                if (row->objectName().startsWith(QLatin1String("macro_")) && row->objectName().endsWith(QLatin1String("_1"))
                    && row->property("text").toString() == QLatin1String("Group/Sub/Deep"))
                    return true;
                items << row->childItems();
            }
            return false;
        };
        QTRY_VERIFY(panelRow());
    }

    // Evidence for the card: the menu open on Group ▸ Sub, light and dark.
    void screenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        auto &settings = *app->settingsStore();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        auto restore = qScopeGuard([&] { settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark")); });
        const auto hover = [&](QQuickItem *entry) {
            QTRY_VERIFY(entry->isVisible() && entry->width() > 0);
            QTest::mouseMove(entry->window(), entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2)).toPoint());
        };
        const auto entryOf = [&](QObject *menu, const QString &objectName) -> QQuickItem * {
            for (int i = 0; i < menu->property("count").toInt(); ++i) {
                QQuickItem *entry = itemAt(menu, i);
                const auto *sub = entry ? entry->property("subMenu").value<QObject *>() : nullptr;
                if (entry && (entry->objectName() == objectName || (sub && sub->objectName() == objectName)))
                    return entry;
            }
            return nullptr;
        };
        for (const QString &code : {QStringLiteral("light"), QStringLiteral("dark")}) {
            settings.setValue(QStringLiteral("appearance.theme"), code);
            auto *menu = named("automationMenu");
            QVERIFY(QMetaObject::invokeMethod(menu, "open"));
            QTRY_VERIFY(menu->property("opened").toBool());
            QQuickItem *group = entryOf(menu, QStringLiteral("macroMenu_Group"));
            QVERIFY(group);
            hover(group);
            auto *groupMenu = named("macroMenu_Group");
            QTRY_VERIFY(groupMenu->property("opened").toBool());
            QQuickItem *sub = entryOf(groupMenu, QStringLiteral("macroMenu_Group/Sub"));
            QVERIFY(sub);
            hover(sub);
            auto *subMenu = named("macroMenu_Group/Sub");
            QTRY_VERIFY(subMenu->property("opened").toBool());
            hover(entryOf(subMenu, QStringLiteral("macro_Group/Sub/Deep")));
            QTest::qWait(400);
            QImage image = QGuiApplication::platformName() == QLatin1String("xcb")
                               ? window->screen()->grabWindow(0, window->x(), window->y(), window->width(), window->height()).toImage()
                               : window->grabWindow();
            QVERIFY(image.save(out + QStringLiteral("/as1-menu-") + code + QStringLiteral(".png")));
            QVERIFY(QMetaObject::invokeMethod(menu, "dismiss"));
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
    }
};

QTEST_MAIN(AutomationSubmenusTests)
#include "automation_submenus_tests.moc"
