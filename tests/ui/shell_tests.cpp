// V1-S: the Classic shell bound to real Lines. Target labels, the protected
// reference, F6/Shift+F6 panel traversal and focus restoration.

#include "hikari/app/application.h"
#include "hikari/core/ass_save.h"
#include "hikari/application/options_dialog.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/spell_checker.h"
#include "hikari/backends/simulated_output.h"
#include "docking.h"
#include "line_grid.h"
#include "line_table_model.h"
#include "audio_display_item.h"
#include "fake_font_service.h"
#include "hikari/backends/ffms_matroska.h"
#include "hikari/core/ass_save.h"
#include "media/mkv_fixture.h"
#include "hikari/application/video_sources.h"
#include "hikari/application/visual_crosshair.h"
#include "hikari/application/grid_split.h"
#include "hikari/application/visual_position.h"
#include "hikari/application/visual_rotation.h"
#include "hikari/application/visual_scale.h"
#include "hikari/application/grid_translation.h"
#include "icon_theme.h"
#include "theme.h"
#include "colour_picker_controller.h"
#include "screen_sampler.h"

#include <QAccessible>
#include <QMimeData>
#include <QPalette>
#include <QClipboard>
#include <QStyleHints>
#include <QDirIterator>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QSettings>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTranslator>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>

#ifndef _WIN32
#include <dirent.h>
#endif

#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <optional>
#include <future>

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

// A media fixture's name as Open video hands it on: in the platform's form
// (VideoController::openVideo; legacy's wxFileName paths were native).
QString nativeFixture(const char *name)
{
    return QDir::toNativeSeparators(QStringLiteral(HIKARI_MEDIA_FIXTURES "/") + QLatin1String(name));
}

// T1: a batch tool for the rail's gesture rule. A left press begins a
// gesture on the batch picker's targets, each move stages \pos at the
// pointer before each target's text, the release commits it.
class DragTool : public application::visual::VisualTool {
public:
    application::visual::Family family() const override { return application::visual::Family::Position; }
    void pointer(const application::visual::Pointer &e, application::visual::VisualHost &host) override
    {
        using application::visual::Pointer;
        if (e.kind == Pointer::Kind::Press && e.button == Pointer::Button::Left)
            (void)host.beginGesture(host.batchTargets(), "Visual positioning tool");
        auto *g = host.gesture();
        if (g && (e.kind == Pointer::Kind::Move || e.kind == Pointer::Kind::Press)) {
            const auto at = host.view().viewToScript({static_cast<float>(e.x), static_cast<float>(e.y)});
            const std::string pos = "{\\pos(" + std::to_string(static_cast<int>(at.x)) + "," +
                                    std::to_string(static_cast<int>(at.y)) + ")}";
            for (const auto &id : g->targets())
                g->stage(id, std::u8string(pos.begin(), pos.end()) + g->before(id).text);
        }
        if (g && e.kind == Pointer::Kind::Release && e.button == Pointer::Button::Left)
            (void)host.commitGesture();
    }
    application::visual::Overlay overlay(const application::visual::VisualHost &) const override { return {}; }
};

// A visual overlay polygon's first contour (VisualToolsController::overlay:
// "contours", lists of logical points).
QVariantList polygonPoints(const QVariantMap &shape)
{
    const QVariantList contours = shape.value(QStringLiteral("contours")).toList();
    return contours.isEmpty() ? QVariantList{} : contours.first().toList();
}

QString text(const core::LineRecord *line)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(line->text.data()), qsizetype(line->text.size()));
}


// Y6: a FontService for the font picker: installed families with the
// characters each covers (empty: every character), the external fonts
// named "External <file>", and a renderer that answers with its own face
// for an installed family and substitutes "DejaVu Sans" for the rest.
class PickerFonts final : public application::FontServicePort {
public:
    std::vector<std::pair<std::string, std::u32string>> families;
    int refreshes = 0;
    std::function<void()> onResolve; // runs first, on the resolving thread
    std::expected<application::FontReport, application::FontError>
    resolve(const application::FontEnvironment &, const std::vector<application::FontRequest> &requests) override
    {
        if (onResolve)
            onResolve();
        application::FontReport report;
        for (const auto &request : requests) {
            application::ResolvedFace face;
            face.stage = application::SelectionStage::Requested;
            const bool installed = std::any_of(families.begin(), families.end(),
                                               [&](const auto &f) { return f.first == request.family; });
            face.familyNames = {installed ? request.family : std::string("DejaVu Sans")};
            face.path = installed ? "/fonts/" + request.family + ".ttf" : "/fonts/DejaVuSans.ttf";
            report.faces.push_back(face);
            application::RequestReport r;
            r.request = request;
            r.faces = {report.faces.size() - 1};
            r.requestedFamilyFound = installed;
            r.substituted = !installed;
            report.requests.push_back(r);
        }
        return report;
    }
    std::vector<application::SystemFace> systemFaces() override
    {
        std::vector<application::SystemFace> out;
        for (const auto &f : families) {
            application::SystemFace face;
            face.families = {f.first};
            face.path = "/fonts/" + f.first + ".ttf";
            out.push_back(face);
        }
        return out;
    }
    std::vector<application::SystemFace> pickerFaces(const application::FontEnvironment &env) override
    {
        auto out = env.systemFonts ? systemFaces() : std::vector<application::SystemFace>();
        for (const auto &font : env.externalFonts) {
            application::SystemFace face;
            face.families = {"External " + std::filesystem::path(font.name).filename().string()};
            face.externalFile = font.name;
            out.push_back(face);
        }
        return out;
    }
    std::vector<bool> facesCover(const std::vector<application::SystemFace> &faces, const application::FontEnvironment &,
                                 const std::u32string &characters) override
    {
        std::vector<bool> out;
        for (const auto &face : faces) {
            std::u32string glyphs;
            for (const auto &f : families)
                if (f.first == face.families.front())
                    glyphs = f.second;
            bool all = true;
            for (const char32_t c : characters)
                all = all && (glyphs.empty() || glyphs.find(c) != std::u32string::npos);
            out.push_back(all);
        }
        return out;
    }
    void refresh() override { ++refreshes; }
    std::expected<application::FontCollection, application::FontError>
    collect(const std::vector<std::byte> &, const application::FontEnvironment &, const std::atomic<bool> *) override
    {
        return std::unexpected(application::FontError::RendererUnavailable);
    }
    std::expected<application::ReimportCheck, application::FontError>
    verifyReimport(const std::vector<std::byte> &, const application::FontCollection &, const std::string &) override
    {
        return std::unexpected(application::FontError::RendererUnavailable);
    }
};

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
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
    // D3: the header row (DockTabBar.qml) of the group whose current panel
    // is `panel` (a dock's uniqueName), in any window.
    QQuickItem *dockHeader(const QString &panel) const
    {
        std::function<QQuickItem *(QQuickItem *)> walk = [&](QQuickItem *from) -> QQuickItem * {
            if (from->objectName() == QLatin1String("dockHeader") && from->isVisible()
                && from->property("currentName").toString() == panel)
                return from;
            for (QQuickItem *child : from->childItems())
                if (QQuickItem *found = walk(child))
                    return found;
            return nullptr;
        };
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (auto *qw = qobject_cast<QQuickWindow *>(w); qw && qw->isVisible())
                if (QQuickItem *found = walk(qw->contentItem()))
                    return found;
        return nullptr;
    }
    // D3: clicks panel `panel`'s "⋯" button; whether its menu opened.
    bool openPanelMenu(const QString &panel)
    {
        QQuickItem *header = nullptr;
        if (!QTest::qWaitFor([&] { return (header = dockHeader(panel)) != nullptr; }))
            return false;
        QQuickItem *button = findItem(header, QStringLiteral("dockMenuButton"));
        if (!button || !button->isVisible())
            return false;
        auto *w = button->window();
        QTest::mouseClick(w, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        return QTest::qWaitFor([&] { return named("panelOptionsMenu")->property("visible").toBool(); });
    }
    // The open panel menu's items as shown: text, or "|" for a separator;
    // "(off)" after a disabled item, "(on)" after a checked one.
    QStringList panelMenuItems() const
    {
        QObject *menu = named("panelOptionsMenu");
        QStringList out;
        const int count = menu->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *entry = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, i));
            if (!entry || !entry->isVisible() || entry->height() <= 0)
                continue;
            if (entry->inherits("QQuickMenuSeparator")) {
                out << QStringLiteral("|");
                continue;
            }
            QString text = entry->property("text").toString();
            if (!entry->isEnabled())
                text += QStringLiteral(" (off)");
            if (entry->property("checked").toBool())
                text += QStringLiteral(" (on)");
            out << text;
        }
        return out;
    }
    // The scene top of the panel group holding `item` (its header's top).
    static qreal groupTop(QQuickItem *item)
    {
        for (QQuickItem *p = item; p; p = p->parentItem())
            if (p->objectName() == QLatin1String("dockGroup"))
                return p->mapToScene(QPointF(0, 0)).y();
        return item->mapToScene(QPointF(0, 0)).y();
    }
    // Whether `w` becomes the application's focus window. A window manager
    // may refuse a client's activation request (X without one; sway's default
    // focus_on_window_activation marks the window urgent instead).
    static bool becomesFocusWindow(QWindow *w, int ms = 3000)
    {
        return QTest::qWaitFor([&] { return QGuiApplication::focusWindow() == w; }, ms);
    }
    // A named object of the shell (menu items are not all QObject children).
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
    // A panel's name or description for assistive technology. Its dock's
    // title bar is its only visible header (no in-panel title row).
    QString panelAccessible(const char *panel, QAccessible::Text text = QAccessible::Name) const
    {
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item(panel));
        return iface ? iface->text(text) : QString();
    }
    // The panel that contains the active focus item.
    QString focusedPanel() const
    {
        for (QQuickItem *p = window->activeFocusItem(); p; p = p->parentItem())
            if (p->objectName().endsWith(QLatin1String("Panel")))
                return p->objectName();
        return {};
    }
    // O2: CheckLastKeyEvent ignores the same action again within 100 ms
    // (50 in the video); for tests pressing one key repeatedly, each key
    // comes a second after the last.
    void keysNeverRepeat()
    {
        auto t = std::make_shared<qint64>(0);
        application->hotkeys().setKeyClock([t] { return *t += 1000; });
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
        // K2: following the system takes Light or Dark from the platform's
        // colour scheme; the tests set it themselves (an unknown scheme keeps
        // the chosen theme, Dark by default).
        ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown);
        // The application's controls style (composition.cpp chooses it).
        ui::theme::chooseControlsStyle();
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
        QCOMPARE(panelAccessible("gridPanel"), QStringLiteral("Grid"));
        QCOMPARE(panelAccessible("gridPanel", QAccessible::Description), QStringLiteral("No document open"));
        QVERIFY(item("gridEmptyState")->isVisible());
        // D1: the Reference panel's dock is closed while there is no reference.
        QVERIFY(!item<QObject>("referenceDock")->property("isOpen").toBool());
        QCOMPARE(window->title(), QStringLiteral("HikariSub"));
        QCOMPARE(item<QObject>("statusText")->property("text").toString(), QString());
    }

    void labelsNameTheEditingTargetAndTheProtectedReference()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        QTRY_VERIFY(item<QObject>("referenceDock")->property("isOpen").toBool());
        QVERIFY(item("referencePanel")->isVisible());
        // The editing target is named by its Document tab and the window
        // title; the Grid shows it, so its dock is just "Grid". The Protected
        // reference has no tab: its tray's dock names it.
        QCOMPARE(window->title(), QStringLiteral("episode.ass - HikariSub"));
        QCOMPARE(item<QObject>("gridDock")->property("title").toString(), QStringLiteral("Grid"));
        QCOMPARE(item<QObject>("referenceDock")->property("title").toString(), QStringLiteral("Reference: original.ass"));
        QVERIFY(!item("gridEmptyState")->isVisible());
        // No in-panel title row repeats the dock's, and the status bar does
        // not name the target.
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("gridPanelTitle")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("statusTargets")));
        QVERIFY(!item<QObject>("statusText")->property("text").toString().contains(QLatin1String("episode.ass")));
        // Panels are named for assistive technology: the Grid by its role,
        // the editing target in its description (D1 native gate); the
        // Reference by the Document it shows.
        QCOMPARE(panelAccessible("gridPanel"), QStringLiteral("Grid"));
        QCOMPARE(panelAccessible("gridPanel", QAccessible::Description), QStringLiteral("Editing: episode.ass"));
        QCOMPARE(panelAccessible("referencePanel"), QStringLiteral("Reference (protected, read-only): original.ass"));
        QCOMPARE(panelAccessible("editorPanel"), QStringLiteral("Line editor: episode.ass"));
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
        QCOMPARE(panelAccessible("gridPanel", QAccessible::Description), QStringLiteral("Editing: episode.ass"));
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

    // E6: the Line editor's tag list (legacy TextEditor's PopupTagList at
    // 20d647c4): opened by "\" in an override block of the raw text, narrowed
    // by typing (an input method's commit included, its composition left
    // alone), Up/Down to choose, Enter or a click to put the tag, the
    // options menu, Escape to close; the field keeps focus throughout.
    void tagListCompletesTagsInTheRawText()
    {
        QVERIFY(application->openFile(episode));
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        auto *list = application->editor().tagList();
        // The hidden-tag view refuses ASS syntax: no list there.
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        QTest::keyClick(window, '{');
        QTest::keyClick(window, '\\');
        QCoreApplication::processEvents();
        QVERIFY(!list->open());
        application->editor().discard();
        application->editor().setShowTags(true);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        QTest::keyClick(window, '{');
        QTest::keyClick(window, '\\');
        QTRY_VERIFY(list->open());
        QVERIFY(list->popupShown());
        QCOMPARE(list->rows().size(), 20); // TEXT_EDITOR_TAG_LIST_OPTIONS 0: the type-0 tags
        auto *popup = item<QObject>("lineTextTagList");
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QVERIFY(text->hasActiveFocus());
        QTest::keyClick(window, 'b');
        QTRY_COMPARE(list->rows(), (QStringList{"be", "blur", "bord"}));
        // An input method's composition goes on under the list ...
        QInputMethodEvent preedit(QStringLiteral("e"), {});
        QCoreApplication::sendEvent(text, &preedit);
        QVERIFY(text->property("inputMethodComposing").toBool());
        QVERIFY(text->hasActiveFocus());
        QVERIFY(popup->property("visible").toBool());
        QCOMPARE(list->rows().size(), 3);
        QCOMPARE(list->selection(), 0);
        // ... and its committed text is typed.
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("l"));
        QCoreApplication::sendEvent(text, &commit);
        QTRY_COMPARE(list->rows(), (QStringList{"blur"}));
        QVERIFY(text->hasActiveFocus());
        press(Qt::Key_Return);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\blurfirst"));
        QCOMPARE(text->property("cursorPosition").toInt(), 6);
        QVERIFY(!list->open());
        QTRY_COMPARE(application->editor().text(), QStringLiteral("{\\blurfirst"));
        // Up and Down move the selection (wrapping), Return puts it.
        QTest::keyClick(window, '\\');
        QTRY_VERIFY(list->open());
        press(Qt::Key_Up);
        QCOMPARE(list->selection(), 19);
        press(Qt::Key_Down);
        press(Qt::Key_Down);
        QCOMPARE(list->selection(), 1); // alpha, be
        QCOMPARE(list->selectedAnnouncement(), QStringLiteral("\\be, Edge blur, 2 of 20"));
        press(Qt::Key_Return);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\blur\\befirst"));
        // The options menu, by a right click on a row (PopupWindow::
        // OnMouseEvent's RightUp, TextEditorTagList.cpp:131-149): each item
        // checked as TEXT_EDITOR_TAG_LIST_OPTIONS says, and the one clicked
        // flipped in the profile.
        QTest::keyClick(window, '\\');
        QTRY_VERIFY(list->open());
        auto *pointer = item("lineTextTagListPointer");
        QTRY_VERIFY(pointer && pointer->isVisible());
        const double rowHeight = popup->property("rowHeight").toDouble();
        auto *options = item<QObject>("lineTextTagListOptions");
        QVERIFY(options);
        const auto checkedItems = [&] {
            QString out;
            for (const char *name : {"lineTextTagListShowDescription", "lineTextTagListShowAllTags",
                                     "lineTextTagListShowVsfilterModTags"})
                out += item<QObject>(name)->property("checked").toBool() ? QLatin1Char('1') : QLatin1Char('0');
            return out;
        };
        QString shown; // the items' checked states while the menu is open
        const auto chooseOption = [&](const char *name) {
            const QPoint row0 = pointer->mapToScene(QPointF(10, rowHeight * 0.5)).toPoint();
            QTest::mouseMove(window, row0 - QPoint(0, 1)); // the list ignores its first pointer event
            QTest::mouseMove(window, row0);
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, row0);
            QTRY_VERIFY(options->property("opened").toBool());
            QVERIFY(popup->property("visible").toBool()); // the list stays under its menu
            shown = checkedItems();
            auto *choice = item(name);
            QTRY_VERIFY(choice->isVisible() && choice->width() > 0);
            QTest::mouseClick(choice->window(), Qt::LeftButton, Qt::NoModifier,
                              choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint());
            QTRY_VERIFY(!options->property("visible").toBool());
            QTRY_VERIFY(text->hasActiveFocus());
        };
        chooseOption("lineTextTagListShowAllTags");
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(shown, QStringLiteral("000"));
        QCOMPARE(application->settingsStore()->integer("textEditor.tagListOptions"), 1);
        QVERIFY(list->open());
        QCOMPARE(list->rows().size(), 50);
        chooseOption("lineTextTagListShowDescription");
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(shown, QStringLiteral("010"));
        QCOMPARE(application->settingsStore()->integer("textEditor.tagListOptions"), 5);
        QCOMPARE(list->rows().first(), QStringLiteral("1a - Transparency of primary color"));
        QCOMPARE(checkedItems(), QStringLiteral("110"));
        // The pointer: the first event is ignored, then the row under it is
        // selected and a left click puts it; the field keeps focus.
        const QPoint row2 = pointer->mapToScene(QPointF(10, rowHeight * 2.5)).toPoint();
        QTest::mouseMove(window, row2 - QPoint(0, 1));
        QTest::mouseMove(window, row2);
        QTRY_COMPARE(list->selection(), 2);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, row2);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\blur\\be\\3afirst"));
        QVERIFY(text->hasActiveFocus());
        // Escape closes the list and keeps the draft (it is not the editor's Escape).
        QTest::keyClick(window, '\\');
        QTRY_VERIFY(list->open());
        press(Qt::Key_Escape);
        QVERIFY(!list->open());
        QTRY_COMPARE(application->editor().text(), QStringLiteral("{\\blur\\be\\3a\\first"));
        // Left closes it (one of TextEditor's own accelerators) ...
        QTest::keyClick(window, 'f'); // the character after "\" opens it again
        QTRY_VERIFY(list->open());
        QCOMPARE(list->rows().first(), QStringLiteral("fad - Fading in / fading out of text"));
        press(Qt::Key_Left);
        QVERIFY(!list->open());
        // ... and so does the field losing focus.
        press(Qt::Key_Right);
        QTest::keyClick(window, '\\');
        QTRY_VERIFY(list->open());
        item("editingGrid")->forceActiveFocus();
        QVERIFY(!list->open());
        application->editor().discard();
    }

    // E6: GLOBAL_HIDE_TAGS, Subtitles > Hide tags (legacy SubsGrid::
    // HideOverrideTags): the Grid's text with each block swapped for
    // GRID_TAGS_SWAP_CHARACTER, kept in GRID_HIDE_TAGS.
    void hideTagsSwitchesTheGridsTags()
    {
        const QString tagged = writeFile(dir, "tagged.ass",
                                         "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\i1}Hi{\\i0} there\n");
        QVERIFY(application->openFile(tagged));
        auto *model = item("editingGrid")->property("model").value<QAbstractItemModel *>();
        const auto shown = [&] { return model->index(0, ui::LineTableModel::TextColumn).data().toString(); };
        QCOMPARE(shown(), QStringLiteral("{\\i1}Hi{\\i0} there"));
        auto *menuItem = named("hideTagsMenuItem");
        QVERIFY(menuItem);
        QCOMPARE(menuItem->property("iconRole").toString(), QStringLiteral("hide-tags"));
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        QCOMPARE(shown(), QStringLiteral("☀Hi☀ there"));
        QVERIFY(application->settingsStore()->boolean("grid.hideTags"));
        // A new swap character applies at once.
        application->settingsStore()->set("grid.tagsSwapCharacter", QStringLiteral("~"));
        QCOMPARE(shown(), QStringLiteral("~Hi~ there"));
        // The Global binding (none by default) runs the same switch.
        QVariant ran;
        QVERIFY(QMetaObject::invokeMethod(window, "runGlobalHotkey", Q_RETURN_ARG(QVariant, ran),
                                          Q_ARG(QVariant, QStringLiteral("GLOBAL_HIDE_TAGS"))));
        QVERIFY(ran.toBool());
        QCOMPARE(shown(), QStringLiteral("{\\i1}Hi{\\i0} there"));
        QVERIFY(!application->settingsStore()->boolean("grid.hideTags"));
        // The Line editor's own hidden-tag view has the set's icon.
        QCOMPARE(item("showTags")->property("iconRole").toString(), QStringLiteral("hide-tags"));
        QCOMPARE(item("showTags")->property("checked").toBool(), !application->editor().showTags());
    }

    // E6: GLOBAL_PREVIOUS_LINE / GLOBAL_NEXT_LINE (Ctrl+Up / Ctrl+Down,
    // SubsGrid::NextLine): shown Lines only, nothing before the first, a new
    // Line after the last; the Line selected alone.
    void ctrlUpAndDownMoveBetweenShownLines()
    {
        const QString three = writeFile(dir, "three.ass",
                                        "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,one\n"
                                        "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,two\n"
                                        "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,three\n");
        QVERIFY(application->openFile(three));
        keysNeverRepeat();
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("one"));
        // Hide "two".
        const auto lines = session->document().lines();
        session->setSelection(application::Selection{lines[1]->id, {lines[1]->id}, lines[1]->id, {}});
        QVERIFY(application->hideSelectedLines());
        session->setSelection(application::Selection{lines[0]->id, {lines[0]->id}, lines[0]->id, {}});
        application->editor().reloadFromSession();
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("one"));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Up, Qt::ControlModifier); // before the first: nothing
        QCOMPARE(session->selection().active, lines[0]->id);
        press(Qt::Key_Down, Qt::ControlModifier); // over the hidden "two"
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("three"));
        QCOMPARE(session->selection().selected, std::set<core::LineId>{lines[2]->id});
        press(Qt::Key_Up, Qt::ControlModifier);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("one"));
        // Nothing hidden: after the last Line a copy of it starting at its
        // End, five seconds long, without text; from the editor too.
        QVERIFY(application->turnOffFiltering());
        session->setSelection(application::Selection{lines[2]->id, {lines[2]->id}, lines[2]->id, {}});
        application->editor().reloadFromSession();
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("three"));
        const auto steps = session->historySize();
        text->forceActiveFocus();
        press(Qt::Key_Down, Qt::ControlModifier);
        QTRY_COMPARE(session->document().lines().size(), std::size_t(4));
        QCOMPARE(session->historySize(), steps + 1);
        const auto *added = session->document().lines()[3];
        QCOMPARE(session->selection().active, added->id);
        QCOMPARE(text->property("text").toString(), QString());
        QCOMPARE(added->start.value.microseconds(), 6'000'000);
        QCOMPARE(added->end.value.microseconds(), 11'000'000);
        press(Qt::Key_Up, Qt::ControlModifier);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("three"));
    }

    // E6: GLOBAL_REMOVE_TEXT (Alt+Delete; the Grid menu's "Delete text"):
    // the shown selected Lines' text emptied in one "Deleting text" step.
    void deleteTextEmptiesTheSelectedLines()
    {
        QVERIFY(application->openFile(episode));
        keysNeverRepeat();
        auto *session = application->files().session(*application->workspace().editingTarget());
        QTRY_COMPARE(item("lineText")->property("text").toString(), QStringLiteral("first"));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_A, Qt::ControlModifier);
        const auto steps = session->historySize();
        press(Qt::Key_Delete, Qt::AltModifier);
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Deleting text"));
        for (const auto *line : session->document().lines())
            QVERIFY(line->text.empty());
        QTRY_COMPARE(item("lineText")->property("text").toString(), QString());
        QVERIFY(application->editor().undo());
        QCOMPARE(text(session->document().lines()[1]), QStringLiteral("second"));
        // The Grid menu's item, before "Delete lines".
        auto *menuItem = named("deleteText");
        QVERIFY(menuItem);
        QCOMPARE(menuItem->property("text").toString(), QStringLiteral("Delete text"));
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Deleting text"));
    }

    // E6: the changed-Line mark (legacy State 1, saved 2): the number cell's
    // label colour, its shape and the state named for assistive technology.
    void changedLinesAreMarkedUntilSaved()
    {
        const QString copy = dir.filePath(QStringLiteral("changed.ass"));
        QFile::remove(copy);
        QVERIFY(QFile::copy(episode, copy));
        QVERIFY(application->openFile(copy));
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        QVERIFY(grid);
        auto *model = grid->model();
        const auto state = [&](int row) { return model->index(row, 0).data(ui::LineTableModel::LineStateRole).toInt(); };
        QCOMPARE(state(0), 0);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 5);
        QTest::keyClick(window, '!');
        QTRY_COMPARE(application->editor().text(), QStringLiteral("first!"));
        press(Qt::Key_Return, Qt::ControlModifier); // commit
        QTRY_COMPARE(state(0), 1);
        QCOMPARE(state(1), 0);
        QCOMPARE(grid->rowStateText(0), QStringLiteral("changed"));
        QVERIFY(application->editor().save());
        application->waitForWrites();
        QTRY_COMPARE(state(0), 2);
        QCOMPARE(grid->rowStateText(0), QStringLiteral("changed, saved"));
        QVERIFY(application->editor().undo());
        QTRY_COMPARE(state(0), 0);
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

    // Y6: the picker's fonts through a fake FontService, the Polish filter
    // characters, and two catalogs in legacy's file.
    PickerFonts *usePickerFonts()
    {
        {
            // Read when the catalogs are first used (LoadCatalogs' isInit).
            QFile f(QString::fromStdU16String((application->fontCatalogs().catalogDir() / u"FontCatalogs.txt").u16string()));
            f.open(QIODevice::WriteOnly);
            f.write("\xEF\xBB\xBF" "A={\r\n\tVerdana\r\n\tArial\r\n\tNot Installed\r\n}\r\nB={\r\n\tImpact\r\n}\r\n");
        }
        auto service = std::make_unique<PickerFonts>();
        service->families = {{"Arial", U"aą"}, {"Comic Sans MS", U"a"}, {"Impact", U"a"}, {"Tahoma", U"aą"}, {"Verdana", U"a"}};
        auto *raw = service.get();
        application->fontCatalogs().setFontService(std::move(service));
        application->settingsStore()->set("styles.editFilterText", QStringLiteral("ą"));
        application->settingsStore()->set("styles.editFilterTextOn", false);
        return raw;
    }
    QString fontAss(const char *name, const char *events)
    {
        const QString path = dir.filePath(QLatin1String(name));
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Tahoma,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                "10,10,10,1\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        f.write(events);
        return path;
    }
    QObject *openFontDialog(const QString &path)
    {
        if (!application->openFile(path))
            return nullptr;
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        if (!QTest::qWaitFor([&] { return !text->property("text").toString().isEmpty(); }))
            return nullptr;
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        auto *dialog = named("fontDialog");
        QMetaObject::invokeMethod(visualItem("changeFont"), "click");
        return QTest::qWaitFor([&] { return dialog->property("visible").toBool(); }) ? dialog : nullptr;
    }
    static void choose(QObject *combo, int index)
    {
        combo->setProperty("currentIndex", index);
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
    }

    // Y6: the font dialog's list comes from the FontService through the
    // catalog choice (FontDialog::ChangeCatalog, FontDialog.cpp:656-695) and
    // the Filter (GetFontsTable); the renderer reports what it selected.
    void fontDialogListsCatalogsAndTheFilter()
    {
        usePickerFonts();
        auto *dialog = openFontDialog(fontAss("picker.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"));
        QVERIFY(dialog);
        auto *list = dialogItem("fontDialog", "fontList");
        auto *choice = dialogItem("fontDialog", "fontCatalogChoice");
        const QStringList all{"Arial", "Comic Sans MS", "Impact", "Tahoma", "Verdana"};
        QCOMPARE(list->property("model").toStringList(), all);
        QCOMPARE(list->property("currentIndex").toInt(), 3); // SetSelectionByName("Tahoma")
        QCOMPARE(choice->property("model").toStringList(), (QStringList{"All fonts", "Without catalog", "A", "B"}));
        // A catalog: its installed fonts, sorted (GetCatalogFonts); the
        // Style's font is not among them, so the partial-name search picks.
        choose(choice, 2);
        QCOMPARE(list->property("model").toStringList(), (QStringList{"Arial", "Verdana"}));
        QCOMPARE(list->property("currentIndex").toInt(), 1); // "tahoma": past "arial", at "verdana"
        // "Without catalog": the fonts no catalog holds, as in the Style
        // editor (Y6-without-catalog; legacy's FontList::FindString answered
        // 0, removing the first font per catalog entry).
        choose(choice, 1);
        QCOMPARE(list->property("model").toStringList(), (QStringList{"Comic Sans MS", "Tahoma"}));
        // The Filter: the fonts with a glyph for each of STYLE_EDIT_FILTER_TEXT's
        // characters, and STYLE_EDIT_FILTER_TEXT_ON saved.
        choose(choice, 0);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontFilter"), "toggle"));
        QMetaObject::invokeMethod(dialogItem("fontDialog", "fontFilter"), "toggled");
        QCOMPARE(list->property("model").toStringList(), (QStringList{"Arial", "Tahoma"}));
        QVERIFY(application->settingsStore()->boolean("styles.editFilterTextOn"));
        // Typing selects as FontList::SetSelectionByPartialName.
        dialogItem("fontDialog", "fontName")->setProperty("text", QStringLiteral("ta"));
        QCOMPARE(list->property("currentIndex").toInt(), 1);
        // The renderer's selection for the typed name: substituted.
        QTRY_VERIFY(dialogItem("fontDialog", "fontResolution")->property("text").toString().contains(QStringLiteral("DejaVu Sans")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        application->fontCatalogs().waitResolved();
    }

    // Y6: "Manage font catalogs" (FontCatalogList.cpp): the fonts with their
    // catalogs, Add with its autosave copy, the Catalog column's menu over
    // the marked rows, rename with Merge, and FontCatalogs.txt written when
    // the window hides (CATALOG_CHANGED).
    void fontCatalogWindowManagesTheCatalogs()
    {
        usePickerFonts();
        auto &catalogs = application->fontCatalogs();
        catalogs.setAutosaveInterval(20);
        auto *dialog = openFontDialog(fontAss("manage.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontCatalogManage"), "click"));
        auto *window = named("fontDialogCatalogWindow");
        QVERIFY(window);
        QTRY_VERIFY(window->property("visible").toBool());
        const QStringList all{"Arial", "Comic Sans MS", "Impact", "Tahoma", "Verdana"};
        QCOMPARE(window->property("fonts").toStringList(), all);
        QCOMPARE(window->property("rowCatalogs").toStringList(), (QStringList{"A", "", "B", "", "A"}));
        QCOMPARE(dialogItem("fontDialogCatalogWindow", "fontCatalogList")->property("currentIndex").toInt(), 3);
        // Load's chooser is titled for catalog files (Y6-load-title).
        auto *load = window->findChild<QObject *>(QStringLiteral("fontCatalogLoadDialog"));
        QVERIFY(load);
        QCOMPARE(load->property("title").toString(), QStringLiteral("Choose font catalog file"));
        // Add: a new catalog in the choice, saved to FontCatalogsAutosave0.txt.
        auto *field = dialogItem("fontDialogCatalogWindow", "fontCatalogField");
        // The empty choice (legacy's) says what goes in it; typing hides that.
        auto *placeholder = dialogItem("fontDialogCatalogWindow", "fontCatalogFieldPlaceholder");
        QVERIFY(placeholder);
        QCOMPARE(field->property("editText").toString(), QString());
        QVERIFY(placeholder->isVisible());
        field->setProperty("editText", QStringLiteral("C"));
        QVERIFY(!placeholder->isVisible());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogAddCatalog"), "click"));
        QCOMPARE(catalogs.catalogNames(), (QStringList{"A", "B", "C"}));
        const QString dirPath = QString::fromStdU16String(catalogs.catalogDir().u16string());
        QTRY_VERIFY(readAll(dirPath + QStringLiteral("/FontCatalogsAutosave0.txt")).contains("C={\r\n}\r\n"));
        QCOMPARE(catalogs.autosaveStatus(), QStringLiteral("Autosave"));
        // The Catalog column's menu on Comic Sans MS with Tahoma marked: both join C.
        QVariantList marks{false, false, false, true, false};
        window->setProperty("marked", marks);
        QVERIFY(QMetaObject::invokeMethod(window, "chooseCatalog", Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("C")),
                                          Q_ARG(QVariant, true)));
        QCOMPARE(window->property("rowCatalogs").toStringList(), (QStringList{"A", "C", "B", "C", "A"}));
        QVERIFY(catalogs.isFontInCatalog(QStringLiteral("C"), QStringLiteral("Tahoma")));
        // Edit: C onto the existing B, merged; C leaves the choices.
        QVERIFY(catalogs.catalogExists(QStringLiteral("B")));
        QVERIFY(QMetaObject::invokeMethod(window, "renamed", Q_ARG(QVariant, QStringLiteral("C")), Q_ARG(QVariant, QStringLiteral("B")),
                                          Q_ARG(QVariant, 0)));
        QCOMPARE(catalogs.catalogNames(), (QStringList{"A", "B"}));
        QCOMPARE(window->property("rowCatalogs").toStringList(), (QStringList{"A", "B", "B", "B", "A"}));
        // Hiding the window saves FontCatalogs.txt (the font dialog's CATALOG_CHANGED).
        QVERIFY(QMetaObject::invokeMethod(window, "close"));
        QTRY_COMPARE(readAll(dirPath + QStringLiteral("/FontCatalogs.txt")),
                     QByteArray("\xEF\xBB\xBF" "A={\r\n\tVerdana\r\n\tArial\r\n\tNot Installed\r\n}\r\n"
                                "B={\r\n\tImpact\r\n\tComic Sans MS\r\n\tTahoma\r\n}\r\n"));
        QCOMPARE(dialogItem("fontDialog", "fontCatalogChoice")->property("model").toStringList(),
                 (QStringList{"All fonts", "Without catalog", "A", "B"}));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        application->fontCatalogs().waitResolved();
    }

    // Y6: the catalog window's Edit and Delete through their questions
    // (FontCatalogList.cpp:126-150, ChangeCatalogName at 739-776): Cancel
    // keeps both catalogs, Delete drops the existing list for the renamed
    // one's, and "Are you sure" removes a catalog. The font dialog keeps its
    // choice within the list when the window hides (FontDialog.cpp:466-474).
    void fontCatalogWindowEditAndDeleteAsk()
    {
        usePickerFonts();
        auto &catalogs = application->fontCatalogs();
        auto *dialog = openFontDialog(fontAss("edit.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"));
        QVERIFY(dialog);
        auto *choice = dialogItem("fontDialog", "fontCatalogChoice");
        choose(choice, 3); // B
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontCatalogManage"), "click"));
        auto *window = named("fontDialogCatalogWindow");
        QTRY_VERIFY(window->property("visible").toBool());
        const auto in = [window](const char *name) { return window->findChild<QObject *>(QLatin1String(name)); };
        auto *field = dialogItem("fontDialogCatalogWindow", "fontCatalogField");
        // Edit with B in the field: the question opens on B.
        field->setProperty("editText", QStringLiteral("b"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogEdit"), "click"));
        auto *edition = in("catalogEditionDialog");
        QTRY_VERIFY(edition->property("visible").toBool());
        QCOMPARE(in("catalogEditionCurrent")->property("currentIndex").toInt(), 1);
        // A onto the existing B: the question; Cancel changes nothing.
        in("catalogEditionCurrent")->setProperty("currentIndex", 0);
        in("catalogEditionNewName")->setProperty("text", QStringLiteral("B"));
        QVERIFY(QMetaObject::invokeMethod(in("catalogEditionOk"), "click"));
        auto *clash = in("catalogClashQuestion");
        QTRY_VERIFY(clash->property("visible").toBool());
        QVERIFY(!edition->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(in("catalogClashCancel"), "click"));
        QTRY_VERIFY(!clash->property("visible").toBool());
        QCOMPARE(catalogs.catalogNames(), (QStringList{"A", "B"}));
        QVERIFY(catalogs.isFontInCatalog(QStringLiteral("B"), QStringLiteral("Impact")));
        // Again, answering Delete: B's list goes, A's fonts become B's.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogEdit"), "click"));
        QTRY_VERIFY(edition->property("visible").toBool());
        in("catalogEditionCurrent")->setProperty("currentIndex", 0);
        in("catalogEditionNewName")->setProperty("text", QStringLiteral("B"));
        QVERIFY(QMetaObject::invokeMethod(in("catalogEditionOk"), "click"));
        QTRY_VERIFY(clash->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(in("catalogClashDelete"), "click"));
        QCOMPARE(catalogs.catalogNames(), QStringList{"B"});
        QVERIFY(!catalogs.isFontInCatalog(QStringLiteral("B"), QStringLiteral("Impact")));
        QVERIFY(catalogs.isFontInCatalog(QStringLiteral("B"), QStringLiteral("Verdana")));
        QCOMPARE(field->property("editText").toString(), QStringLiteral("B")); // catalog->SetValue
        QCOMPARE(window->property("rowCatalogs").toStringList(), (QStringList{"B", "", "", "", "B"}));
        // Hiding: the choice's B at 3 is past the new end, so the font dialog
        // takes the last entry (B, now at 2).
        QVERIFY(QMetaObject::invokeMethod(window, "close"));
        QTRY_COMPARE(choice->property("model").toStringList(), (QStringList{"All fonts", "Without catalog", "B"}));
        QCOMPARE(choice->property("currentIndex").toInt(), 2);
        QCOMPARE(choice->property("displayText").toString(), QStringLiteral("B"));
        QCOMPARE(dialogItem("fontDialog", "fontList")->property("model").toStringList(), (QStringList{"Arial", "Verdana"}));
        // Delete asks "Are you sure"; No keeps the catalog, Yes removes it.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontCatalogManage"), "click"));
        QTRY_VERIFY(window->property("visible").toBool());
        field->setProperty("editText", QStringLiteral("B"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogDelete"), "click"));
        auto *question = in("catalogDeleteQuestion");
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "reject"));
        QCOMPARE(catalogs.catalogNames(), QStringList{"B"});
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogDelete"), "click"));
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "accept"));
        QCOMPARE(catalogs.catalogNames(), QStringList{});
        QCOMPARE(field->property("editText").toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(window, "close"));
        // With no catalog, Insert's clamp puts "Without catalog" first
        // (ListControls.cpp:685-688); 2 is clamped to the last entry.
        QTRY_COMPARE(choice->property("model").toStringList(), (QStringList{"Without catalog", "All fonts"}));
        QCOMPARE(choice->property("currentIndex").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        catalogs.waitResolved();
    }

    // Y6-rename-listed: Edit renaming a catalog to a free name lists the new
    // name at once, in the old one's place: in the window's choice and, when
    // the window hides, in the font dialog's (legacy ChangeCatalogName,
    // FontCatalogList.cpp:739-776, dropped the old name and listed the new
    // one only after a restart).
    void fontCatalogRenameIsListedAtOnce()
    {
        usePickerFonts();
        auto &catalogs = application->fontCatalogs();
        auto *dialog = openFontDialog(fontAss("rename.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontCatalogManage"), "click"));
        auto *window = named("fontDialogCatalogWindow");
        QTRY_VERIFY(window->property("visible").toBool());
        const auto in = [window](const char *name) { return window->findChild<QObject *>(QLatin1String(name)); };
        auto *field = dialogItem("fontDialogCatalogWindow", "fontCatalogField");
        field->setProperty("editText", QStringLiteral("A"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogEdit"), "click"));
        QTRY_VERIFY(in("catalogEditionDialog")->property("visible").toBool());
        QCOMPARE(in("catalogEditionCurrent")->property("currentIndex").toInt(), 0);
        in("catalogEditionNewName")->setProperty("text", QStringLiteral("Latin"));
        QVERIFY(QMetaObject::invokeMethod(in("catalogEditionOk"), "click"));
        QCOMPARE(catalogs.catalogNames(), (QStringList{"Latin", "B"}));
        QCOMPARE(field->property("model").toStringList(), (QStringList{"Latin", "B"}));
        QCOMPARE(field->property("editText").toString(), QStringLiteral("Latin"));
        QCOMPARE(window->property("rowCatalogs").toStringList(), (QStringList{"Latin", "", "B", "", "Latin"}));
        QVERIFY(QMetaObject::invokeMethod(window, "close"));
        auto *choice = dialogItem("fontDialog", "fontCatalogChoice");
        QTRY_COMPARE(choice->property("model").toStringList(), (QStringList{"All fonts", "Without catalog", "Latin", "B"}));
        choose(choice, 2);
        QCOMPARE(dialogItem("fontDialog", "fontList")->property("model").toStringList(), (QStringList{"Arial", "Verdana"}));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        catalogs.waitResolved();
    }

    // Y6: the edition timer (FontCatalogManagement::saveInterval, 20 s) writes
    // FontCatalogsAutosave0, 1, 2 and then 0 again (FontCatalogList.cpp:200-210).
    void fontCatalogAutosaveRotatesThreeCopies()
    {
        auto &catalogs = application->fontCatalogs();
        QCOMPARE(catalogs.autosaveInterval(), 20000);
        catalogs.setAutosaveInterval(20);
        const QString dirPath = QString::fromStdU16String(catalogs.catalogDir().u16string());
        const auto copy = [&](int i) { return readAll(dirPath + QStringLiteral("/FontCatalogsAutosave%1.txt").arg(i)); };
        const char *names[] = {"W", "X", "Y", "Z"};
        const int files[] = {0, 1, 2, 0};
        for (int i = 0; i < 4; ++i) {
            QVERIFY(catalogs.addCatalog(QLatin1String(names[i])));
            QTRY_VERIFY(copy(files[i]).contains(QByteArray(names[i]) + "={\r\n}\r\n"));
            QCOMPARE(catalogs.autosaveStatus(), QStringLiteral("Autosave"));
        }
        QVERIFY(copy(1).contains("X={") && !copy(1).contains("Y={"));
        QVERIFY(copy(2).contains("Y={") && !copy(2).contains("Z={"));
        QVERIFY(!QFileInfo::exists(dirPath + QStringLiteral("/FontCatalogsAutosave3.txt")));
    }

    // Y6: ~FontCatalogList saves the catalogs (FontCatalogList.cpp:218-221):
    // once a catalog window was made, the application's end writes
    // FontCatalogs.txt with the edits made while it is still open. Without
    // a window the file is left as it was (not rewritten with a BOM).
    void fontCatalogsAreSavedAtTheEndOnceAWindowWasMade()
    {
        QTemporaryDir catalogDir;
        QVERIFY(catalogDir.isValid());
        const QString file = catalogDir.filePath(QStringLiteral("FontCatalogs.txt"));
        app::Application::Options options;
        options.fontCatalogDir = catalogDir.path();
        restartWith(options);
        usePickerFonts();
        const QByteArray plain("A={\r\n\tArial\r\n}\r\n");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(plain);
        }
        auto *dialog = openFontDialog(fontAss("end-none.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"));
        QVERIFY(dialog);
        QCOMPARE(dialogItem("fontDialog", "fontCatalogChoice")->property("model").toStringList(),
                 (QStringList{"All fonts", "Without catalog", "A"}));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        application->fontCatalogs().waitResolved();
        restartWith(options);
        QCOMPARE(readAll(file), plain);

        usePickerFonts();
        dialog = openFontDialog(fontAss("end-open.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,abc\n"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontCatalogManage"), "click"));
        auto *window = named("fontDialogCatalogWindow");
        QTRY_VERIFY(window->property("visible").toBool());
        dialogItem("fontDialogCatalogWindow", "fontCatalogField")->setProperty("editText", QStringLiteral("C"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialogCatalogWindow", "fontCatalogAddCatalog"), "click"));
        QVERIFY(!readAll(file).contains("C={")); // written when shown, before Add
        application->fontCatalogs().waitResolved();
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        QCOMPARE(readAll(file), QByteArray("\xEF\xBB\xBF" "A={\r\n\tVerdana\r\n\tArial\r\n\tNot Installed\r\n}\r\n"
                                           "B={\r\n\tImpact\r\n}\r\nC={\r\n}\r\n"));
        restartWith(app::Application::Options());
    }

    // Y6: the font dialog's renderer report resolves on one worker thread:
    // asking never waits for an earlier resolution, a request still waiting
    // is replaced by a newer one, and waitResolved waits for the latest.
    void fontResolutionDoesNotBlockAndAnswersTheLatest()
    {
        auto *service = usePickerFonts();
        auto &catalogs = application->fontCatalogs();
        std::promise<void> release;
        std::shared_future<void> released = release.get_future().share();
        auto entered = std::make_shared<std::atomic<int>>(0);
        service->onResolve = [entered, released] {
            if (entered->fetch_add(1) == 0)
                released.wait();
        };
        bool set = false;
        auto unblock = qScopeGuard([&] {
            if (!set)
                release.set_value(); // a failed check must not leave the worker held
        });
        QSignalSpy answers(&catalogs, &app::FontCatalogsController::resolutionReady);
        const int first = catalogs.resolveFamily(QStringLiteral("Arial"), false, false);
        QTRY_COMPARE(entered->load(), 1); // the first resolution is under way, held
        const int second = catalogs.resolveFamily(QStringLiteral("Tahoma"), false, false);
        const int third = catalogs.resolveFamily(QStringLiteral("Nowhere"), true, false);
        QCOMPARE(second, first + 1);
        QCOMPARE(third, first + 2);
        QVERIFY(!catalogs.waitResolved(50));
        release.set_value();
        set = true;
        QVERIFY(catalogs.waitResolved());
        QCOMPARE(answers.size(), 2);
        QCOMPARE(answers.at(0).at(0).toInt(), first);
        QCOMPARE(answers.at(0).at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("requested"));
        QCOMPARE(answers.at(1).at(0).toInt(), third);
        QCOMPARE(answers.at(1).at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("substituted"));
        QCOMPARE(entered->load(), 2);
    }

    // Y6: Add > "Add fonts from subtitles" (GetFontsFromASSDialog,
    // CollectFontsFromSubtitles): the Style fonts and \fn names join the
    // catalog, the catalogs are saved, and the choice is left with the catalog
    // with "All fonts" and "Without catalog" and the catalog chosen
    // (Y6-collect-choice; legacy's PutArray left the names alone).
    void fontsFromSubtitlesFillTheChosenCatalog()
    {
        usePickerFonts();
        auto &catalogs = application->fontCatalogs();
        auto *dialog = openFontDialog(fontAss("collect.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\fnImpact}abc\n"
                                                             "Comment: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\fnSkipped}x\n"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontDialog", "fontCatalogAdd"), "click"));
        auto *menu = named("fontDialogCatalogAddMenu");
        QTRY_VERIFY(menu->property("visible").toBool());
        QCOMPARE(menu->property("count").toInt(), 3); // the dialog's item and A, B
        QQuickItem *first = nullptr;
        QVERIFY(QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0)));
        QCOMPARE(first->objectName(), QStringLiteral("fontsFromSubtitlesItem"));
        QVERIFY(QMetaObject::invokeMethod(first, "triggered"));
        auto *from = named("fontDialogFromSubtitles");
        QTRY_VERIFY(from->property("visible").toBool());
        QCOMPARE(dialogItem("fontDialogFromSubtitles", "fontsFromSubtitlesCatalog")->property("editText").toString(), QStringLiteral("A"));
        QVERIFY(QMetaObject::invokeMethod(from, "accept"));
        QCOMPARE(catalogs.isFontInCatalog(QStringLiteral("A"), QStringLiteral("Tahoma")), true);
        QCOMPARE(catalogs.isFontInCatalog(QStringLiteral("A"), QStringLiteral("Impact")), true);
        QCOMPARE(catalogs.isFontInCatalog(QStringLiteral("A"), QStringLiteral("Skipped")), false);
        const QString dirPath = QString::fromStdU16String(catalogs.catalogDir().u16string());
        QVERIFY(readAll(dirPath + QStringLiteral("/FontCatalogs.txt")).contains("A={\r\n\tVerdana\r\n\tArial\r\n\tNot Installed\r\n\tTahoma\r\n\tImpact\r\n}"));
        auto *choice = dialogItem("fontDialog", "fontCatalogChoice");
        QCOMPARE(choice->property("model").toStringList(), (QStringList{"All fonts", "Without catalog", "A", "B"}));
        QCOMPARE(choice->property("currentIndex").toInt(), 2);
        QCOMPARE(choice->property("displayText").toString(), QStringLiteral("A"));
        // The catalog's installed fonts, sorted.
        QCOMPARE(dialogItem("fontDialog", "fontList")->property("model").toStringList(),
                 (QStringList{"Arial", "Impact", "Tahoma", "Verdana"}));
        // A check in the Add menu adds the font and saves (AddToCatalog).
        catalogs.toggleFontInCatalog(QStringLiteral("B"), QStringLiteral("Tahoma"), true);
        QVERIFY(readAll(dirPath + QStringLiteral("/FontCatalogs.txt")).contains("B={\r\n\tImpact\r\n\tTahoma\r\n}"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        application->fontCatalogs().waitResolved();
    }

    // Y6: the Style editor's font list (StyleChange::ChangeCatalog): "Without
    // catalog" removes each catalog font itself, and the filter text is the
    // one read when the editor was first made.
    void styleEditorFontListFollowsTheCatalogs()
    {
        usePickerFonts();
        QVERIFY(application->openFile(fontAss("style-editor.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n")));
        auto *root = engine->rootObjects().first();
        auto *window = root->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("styleManagerMenuItem")), "triggered"));
        QTRY_VERIFY(window->isVisible());
        const QVariantMap values = application->styleManager().beginEdit(false, 0);
        QVERIFY(QMetaObject::invokeMethod(window, "beginEditing", Q_ARG(QVariant, values), Q_ARG(QVariant, false)));
        auto *font = findItem(window->contentItem(), QStringLiteral("styleFont"));
        auto *bar = findItem(window->contentItem(), QStringLiteral("styleEditorCatalogBar"));
        auto *choice = findItem(bar, QStringLiteral("fontCatalogChoice"));
        QCOMPARE(font->property("model").toStringList(), (QStringList{"Arial", "Comic Sans MS", "Impact", "Tahoma", "Verdana"}));
        QCOMPARE(font->property("editText").toString(), QStringLiteral("Tahoma"));
        choose(choice, 1);
        QCOMPARE(font->property("model").toStringList(), (QStringList{"Comic Sans MS", "Tahoma"}));
        QCOMPARE(font->property("editText").toString(), QStringLiteral("Tahoma")); // PutArray keeps the text
        // The filter text changed after the editor was made is not read.
        application->settingsStore()->set("styles.editFilterText", QStringLiteral("z"));
        choose(choice, 0);
        QVERIFY(QMetaObject::invokeMethod(findItem(bar, QStringLiteral("fontFilter")), "toggle"));
        QMetaObject::invokeMethod(findItem(bar, QStringLiteral("fontFilter")), "toggled");
        QCOMPARE(font->property("model").toStringList(), (QStringList{"Arial", "Tahoma"}));
        QVERIFY(QMetaObject::invokeMethod(window, "closeManager"));
    }

    // Y6: the Style editor does not keep its choice within the list
    // (StyleChange.cpp:98-104): SetSelection past the end is ignored, so the
    // choice stays where PutArray(names) moved it before "All fonts" and
    // "Without catalog" were inserted, with that catalog's name as its text
    // (ListControls.cpp:459, 498-519, 685-688). B deleted under the choice
    // leaves index 0, every font listed, reading "A".
    void styleEditorChoiceIsNotKeptWithinTheList()
    {
        usePickerFonts();
        QVERIFY(application->openFile(fontAss("style-choice.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a\n")));
        auto *root = engine->rootObjects().first();
        auto *editor = root->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("styleManagerMenuItem")), "triggered"));
        QTRY_VERIFY(editor->isVisible());
        const QVariantMap values = application->styleManager().beginEdit(false, 0);
        QVERIFY(QMetaObject::invokeMethod(editor, "beginEditing", Q_ARG(QVariant, values), Q_ARG(QVariant, false)));
        auto *font = findItem(editor->contentItem(), QStringLiteral("styleFont"));
        auto *bar = findItem(editor->contentItem(), QStringLiteral("styleEditorCatalogBar"));
        auto *choice = findItem(bar, QStringLiteral("fontCatalogChoice"));
        choose(choice, 3); // B
        QCOMPARE(font->property("model").toStringList(), QStringList{"Impact"});
        QVERIFY(QMetaObject::invokeMethod(findItem(bar, QStringLiteral("fontCatalogManage")), "click"));
        QObject *window = nullptr;
        for (QObject *o : editor->findChildren<QObject *>())
            if (o->objectName() == QLatin1String("styleEditorCatalogWindow"))
                window = o;
        QVERIFY(window);
        QTRY_VERIFY(window->property("visible").toBool());
        auto *content = window->property("contentItem").value<QQuickItem *>();
        findItem(content, QStringLiteral("fontCatalogField"))->setProperty("editText", QStringLiteral("B"));
        QVERIFY(QMetaObject::invokeMethod(findItem(content, QStringLiteral("fontCatalogDelete")), "click"));
        auto *question = window->findChild<QObject *>(QStringLiteral("catalogDeleteQuestion"));
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "accept"));
        QVERIFY(QMetaObject::invokeMethod(window, "close"));
        QTRY_COMPARE(choice->property("model").toStringList(), (QStringList{"All fonts", "Without catalog", "A"}));
        QCOMPARE(choice->property("currentIndex").toInt(), 0);
        QCOMPARE(choice->property("displayText").toString(), QStringLiteral("A"));
        QCOMPARE(font->property("model").toStringList(), (QStringList{"Arial", "Comic Sans MS", "Impact", "Tahoma", "Verdana"}));
        // Choosing again: the entry's own text and list.
        choose(choice, 2);
        QCOMPARE(choice->property("displayText").toString(), QStringLiteral("A"));
        QCOMPARE(font->property("model").toStringList(), (QStringList{"Arial", "Verdana"}));
        choose(choice, 0);
        QCOMPARE(choice->property("displayText").toString(), QStringLiteral("All fonts"));
        QVERIFY(QMetaObject::invokeMethod(editor, "closeManager"));
    }

    // Y6: EXTERNAL_FONTS_DIRECTORY loads with the settings change and lists
    // with the installed fonts; refreshFonts and a font folder change make a
    // new font environment generation (F47-refresh).
    void externalFontsFolderListsAndRefreshes()
    {
        auto *service = usePickerFonts();
        auto &catalogs = application->fontCatalogs();
        catalogs.setWatchDelay(20);
        QTemporaryDir fonts;
        {
            QFile a(fonts.filePath(QStringLiteral("extra.ttf")));
            a.open(QIODevice::WriteOnly);
            a.write("font bytes");
            QFile b(fonts.filePath(QStringLiteral("notes.txt")));
            b.open(QIODevice::WriteOnly);
            b.write("text");
        }
        QSignalSpy changed(&catalogs, &app::FontCatalogsController::fontsChanged);
        const int before = catalogs.generation();
        application->settingsStore()->set("fonts.externalDirectory", fonts.path() + QStringLiteral("/"));
        QCOMPARE(catalogs.environment().externalFonts.size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(catalogs.environment().externalFonts[0].name), fonts.path() + QStringLiteral("/extra.ttf"));
        QVERIFY(catalogs.allFonts().contains(QStringLiteral("External extra.ttf")));
        QCOMPARE(catalogs.generation(), before + 1);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(catalogs.externalFontLeases().size(), std::size_t(1));
        // The explicit refresh.
        const int refreshes = service->refreshes;
        catalogs.refreshFonts();
        QCOMPARE(catalogs.generation(), before + 2);
        QCOMPARE(service->refreshes, refreshes + 1);
        // A new file in the watched external folder is read after the delay.
        {
            QFile c(fonts.filePath(QStringLiteral("more.otf")));
            c.open(QIODevice::WriteOnly);
            c.write("more bytes");
        }
        QTRY_COMPARE(catalogs.environment().externalFonts.size(), std::size_t(2));
        QTRY_VERIFY(catalogs.allFonts().contains(QStringLiteral("External more.otf")));
        // A folder that cannot be read: "Cannot load external font folder".
        application->settingsStore()->set("fonts.externalDirectory", fonts.path() + QStringLiteral("/missing/"));
        QVERIFY(catalogs.environment().externalFonts.empty());
        QVERIFY(application->log().history().contains(QStringLiteral("Cannot load external font folder")));
        application->settingsStore()->set("fonts.externalDirectory", QString());
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

    // Y7: a window of the shell by its name.
    static QQuickWindow *shellWindow(const char *name)
    {
        for (QWindow *w : QGuiApplication::allWindows())
            if (w->objectName() == QLatin1String(name))
                return qobject_cast<QQuickWindow *>(w);
        return nullptr;
    }
    static void sendMouse(QWindow *w, QEvent::Type type, QPoint local, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, QPointF(local), QPointF(w->mapToGlobal(local)), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &event);
    }
    // Y7: the colour buttons' right click opens the simple "Color picker"
    // (EditBox::AllColorClick, EditBox.cpp:862-919; SimpleColorPickerDialog,
    // ColorPicker.cpp:1374-1533): a right press outside it picks the screen
    // pixel under the pointer into the text at once, the release moves the
    // window to the pointer, the type choice adds the colour to the recent
    // ones, OK adds it again through the option text (no "Choose color"
    // exists yet), Cancel takes the change back; COLORPICKER_SWITCH_CLICKS
    // swaps the clicks; without a screen route the picker says why.
    void simpleColourPickerPicksFromTheScreen()
    {
        QVERIFY(application->openFile(episode)); // "first" and "second", no Styles
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        application->editor().setShowTags(true);
        auto &sampler = *application->colourPicker().sampler();
        sampler.setRoute({QStringLiteral("grab"), {}});
        sampler.setSource([](QPoint) {
            QImage image(7, 7, QImage::Format_RGB32);
            image.fill(qRgb(0x10, 0x20, 0x30));
            return image;
        });
        auto *settings = application->settingsStore();
        settings->set("colourPicker.recentColours", QStringLiteral("&H000000FF&"));
        application->colourPicker().loadFromString(QStringLiteral("&H000000FF&"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);

        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour3")));
        QQuickWindow *simple = shellWindow("simpleColourPicker");
        QVERIFY(simple);
        QTRY_VERIFY(simple->isVisible());
        QVERIFY(!named("colourDialog")->property("visible").toBool());
        auto *hex = findItem(simple->contentItem(), QStringLiteral("hexColour"));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&HFFFFFF&")); // white without a Style
        QVERIFY(sampler.tracking()); // OnShow: CaptureMouse

        // A right press outside picks the centre pixel at once.
        sendMouse(simple, QEvent::MouseButtonPress, QPoint(-40, 10), Qt::RightButton, Qt::RightButton);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\3c&H302010&}first"));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&H302010&"));
        // The release moves the window to the pointer (MoveToMousePosition).
        const QPoint pointer = simple->mapToGlobal(QPoint(-40, 10));
        sendMouse(simple, QEvent::MouseButtonRelease, QPoint(-40, 10), Qt::RightButton, Qt::NoButton);
        const QRect area = sampler.availableGeometryAt(pointer.x(), pointer.y());
        // wxWindow::GetSize and Move: the frame included.
        const QRect frame = simple->frameGeometry();
        const int expectedX = std::max(area.x(), std::min(pointer.x() - frame.width() / 2, area.right() + 1 - frame.width()));
        QTRY_COMPARE(simple->frameGeometry().x(), expectedX);
        int expectedY = pointer.y() + 15;
        if (expectedY + frame.height() > area.bottom() + 1) {
            expectedY -= frame.height() + 30;
            if (expectedY < area.y())
                expectedY = area.bottom() + 1 - frame.height();
        }
        QCOMPARE(simple->frameGeometry().y(), expectedY);
        // Events over the window are its own: no pick.
        sendMouse(simple, QEvent::MouseButtonPress, QPoint(2, 2), Qt::RightButton, Qt::RightButton);
        sendMouse(simple, QEvent::MouseButtonRelease, QPoint(2, 2), Qt::RightButton, Qt::NoButton);
        QCOMPARE(text->property("text").toString(), QStringLiteral("{\\3c&H302010&}first"));

        // The type choice: AddRecent (through the option text), then the
        // shadow colour in effect, alpha included, without a text change.
        auto *type = findItem(simple->contentItem(), QStringLiteral("simpleColourType"));
        QVERIFY(QMetaObject::invokeMethod(type, "activated", Q_ARG(int, 3)));
        QCOMPARE(settings->text("colourPicker.recentColours"), QStringLiteral("&H00302010& &H000000FF&"));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&HFFFFFF&"));
        QCOMPARE(text->property("text").toString(), QStringLiteral("{\\3c&H302010&}first"));
        // A pick now tags the shadow colour.
        sendMouse(simple, QEvent::MouseMove, QPoint(-40, 10), Qt::NoButton, Qt::RightButton);
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\4c&H302010&")));
        // Y7-simple-picker-type: the choice takes the colour in effect as
        // the reset of later picks. The shadow chosen again is &H302010&; a
        // white pick now tags it (legacy kept the white the picker opened
        // with as the reset, saw no change and wrote nothing).
        QVERIFY(QMetaObject::invokeMethod(type, "activated", Q_ARG(int, 3)));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&H302010&"));
        // Y7-recent-option-text: the same colour again stays first, one space
        // apart (legacy left "&H00302010&  &H000000FF&").
        QCOMPARE(settings->text("colourPicker.recentColours"), QStringLiteral("&H00302010& &H000000FF&"));
        sampler.setSource([](QPoint) {
            QImage image(7, 7, QImage::Format_RGB32);
            image.fill(qRgb(0xff, 0xff, 0xff));
            return image;
        });
        sendMouse(simple, QEvent::MouseMove, QPoint(-40, 12), Qt::NoButton, Qt::RightButton);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\4c&HFFFFFF&\\3c&H302010&}first"));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&HFFFFFF&"));
        QVERIFY(QMetaObject::invokeMethod(simple, "accept"));
        QTRY_VERIFY(!simple->isVisible());
        QVERIFY(!sampler.tracking());
        QCOMPARE(settings->text("colourPicker.recentColours"), QStringLiteral("&H00FFFFFF& &H00302010& &H000000FF&"));
        sampler.setSource([](QPoint) {
            QImage image(7, 7, QImage::Format_RGB32);
            image.fill(qRgb(0x10, 0x20, 0x30));
            return image;
        });

        // Cancel takes the picker's changes back.
        QVERIFY(application->editor().commit());
        const QString committed = text->property("text").toString();
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour1")));
        QTRY_VERIFY(simple->isVisible());
        sendMouse(simple, QEvent::MouseButtonPress, QPoint(-40, 10), Qt::RightButton, Qt::RightButton);
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\1c&H302010&")));
        QVERIFY(QMetaObject::invokeMethod(simple, "reject"));
        QTRY_VERIFY(!simple->isVisible());
        QTRY_COMPARE(text->property("text").toString(), committed);

        // Swapped: the left click opens the simple picker, the right click
        // "Choose color".
        application->colourPicker().setSwitchClicks(true);
        QVERIFY(QMetaObject::invokeMethod(visualItem("changeColour1"), "click"));
        QTRY_VERIFY(simple->isVisible());
        QVERIFY(QMetaObject::invokeMethod(simple, "reject"));
        QTRY_VERIFY(!simple->isVisible());
        auto *dialog = named("colourDialog");
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour1")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(dialogItem("colourDialog", "switchClicks")->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());

        // Without a route (Wayland without the portal) nothing is sampled and
        // the picker says why.
        application->colourPicker().setSwitchClicks(false);
        sampler.setRoute(ui::ScreenSampler::routeFor(QStringLiteral("wayland"), true, false));
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour1")));
        QTRY_VERIFY(simple->isVisible());
        QVERIFY(!sampler.tracking());
        auto *note = findItem(simple->contentItem(), QStringLiteral("simpleDropperUnavailable"));
        QVERIFY(note->isVisible());
        QCOMPARE(note->property("text").toString(), sampler.unavailableReason());
        QVERIFY(!findItem(simple->contentItem(), QStringLiteral("moveWindow"))->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(simple, "reject"));
        QTRY_VERIFY(!simple->isVisible());

        // The portal route (Wayland): no pointer capture, the move box off,
        // and a "Pick a colour from the screen" button asking the desktop,
        // whose pick tags the text at once; a cancel leaves the picker no
        // longer waiting, so a later answer to another ask is not its own; a
        // failure shows until the next ask.
        int asked = 0;
        sampler.setPortalRequest([&] { ++asked; });
        sampler.setRoute({QStringLiteral("portal"), {}});
        const QString before = text->property("text").toString();
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour1")));
        QTRY_VERIFY(simple->isVisible());
        QVERIFY(!sampler.tracking());
        QVERIFY(note->property("text").toString().isEmpty());
        QVERIFY(!findItem(simple->contentItem(), QStringLiteral("moveWindow"))->isEnabled());
        auto *portalPick = findItem(simple->contentItem(), QStringLiteral("simplePortalPick"));
        QVERIFY(portalPick->isVisible());
        QVERIFY(QMetaObject::invokeMethod(portalPick, "clicked"));
        QCOMPARE(asked, 1);
        QVERIFY(!portalPick->isEnabled());
        sampler.answerPortal(0, 0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0);
        QVERIFY(portalPick->isEnabled());
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\1c&H604020&")));
        QVERIFY(QMetaObject::invokeMethod(portalPick, "clicked"));
        sampler.answerPortal(1);
        QVERIFY(!simple->property("portalAsked").toBool());
        QVERIFY(QMetaObject::invokeMethod(portalPick, "clicked"));
        sampler.failPortalCall(QStringLiteral("No such interface"));
        QCOMPARE(note->property("text").toString(), QStringLiteral("No such interface"));
        QVERIFY(QMetaObject::invokeMethod(portalPick, "clicked"));
        QVERIFY(note->property("text").toString().isEmpty());
        sampler.answerPortal(1);
        QCOMPARE(asked, 4);
        sampler.setPortalRequest({});
        QVERIFY(QMetaObject::invokeMethod(simple, "reject"));
        QTRY_VERIFY(!simple->isVisible());
        QCOMPARE(text->property("text").toString(), before);
    }

    // Y7-simple-picker-type (approved departure): on a line format (one
    // colour, no colour types) the simple picker's type choice keeps the
    // colour it shows, and the recent colour it adds is that colour. Legacy
    // read the colour into a fresh AssColor that GetColor never sets below
    // ASS (EditBox.cpp:908-912, 932-953), so the picker turned black and its
    // next type choice added black to the recent colours.
    void simpleColourPickerKeepsTheColourOnLineFormats()
    {
        // MicroDVD: a line format whose colour is markup ({C:BBGGRR}); SRT
        // takes no colour (E1-nonass-font).
        const QString path = dir.filePath(QStringLiteral("simple-colour.sub"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{10}{20}first\n");
        }
        QVERIFY(application->openFile(path));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        application->editor().setShowTags(true);
        auto &sampler = *application->colourPicker().sampler();
        sampler.setRoute({QStringLiteral("grab"), {}});
        sampler.setSource([](QPoint) {
            QImage image(7, 7, QImage::Format_RGB32);
            image.fill(qRgb(0x10, 0x20, 0x30));
            return image;
        });
        auto *settings = application->settingsStore();
        settings->set("colourPicker.recentColours", QStringLiteral("&H000000FF&"));
        application->colourPicker().loadFromString(QStringLiteral("&H000000FF&"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);

        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour1")));
        QQuickWindow *simple = shellWindow("simpleColourPicker");
        QVERIFY(simple);
        QTRY_VERIFY(simple->isVisible());
        auto *hex = findItem(simple->contentItem(), QStringLiteral("hexColour"));
        auto *type = findItem(simple->contentItem(), QStringLiteral("simpleColourType"));
        const QString opened = hex->property("text").toString();
        // Before any pick: the choice keeps the colour the picker opened with.
        QVERIFY(QMetaObject::invokeMethod(type, "activated", Q_ARG(int, 2)));
        QCOMPARE(hex->property("text").toString(), opened);
        QCOMPARE(text->property("text").toString(), QStringLiteral("first"));

        // A pick, then the choice: the picked colour stays in the picker.
        sendMouse(simple, QEvent::MouseButtonPress, QPoint(-40, 10), Qt::RightButton, Qt::RightButton);
        QTRY_VERIFY(text->property("text").toString() != QStringLiteral("first"));
        const QString picked = text->property("text").toString();
        QCOMPARE(picked, QStringLiteral("{C:302010}first"));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&H302010&"));
        sendMouse(simple, QEvent::MouseButtonRelease, QPoint(-40, 10), Qt::RightButton, Qt::NoButton);
        QVERIFY(QMetaObject::invokeMethod(type, "activated", Q_ARG(int, 3)));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&H302010&"));
        QCOMPARE(text->property("text").toString(), picked);
        QVERIFY(settings->text("colourPicker.recentColours").startsWith(QStringLiteral("&H00302010& ")));
        // The next choice adds that colour again, not black.
        QVERIFY(QMetaObject::invokeMethod(type, "activated", Q_ARG(int, 0)));
        QCOMPARE(hex->property("text").toString(), QStringLiteral("&H302010&"));
        QVERIFY(settings->text("colourPicker.recentColours").startsWith(QStringLiteral("&H00302010& ")));
        QVERIFY(QMetaObject::invokeMethod(simple, "accept"));
        QTRY_VERIFY(!simple->isVisible());
        QCOMPARE(text->property("text").toString(), picked);
        QVERIFY(settings->text("colourPicker.recentColours").startsWith(QStringLiteral("&H00302010& ")));
    }

    // Y7: "Choose color"'s HSL and HSV values (UpdateFromRGB/HSL/HSV through
    // colorspace.cpp), the type choice's recent colour (GetColor), the ASS
    // and HTML fields, the screen dropper (OnDropperMouse: a left press on
    // the icon takes the pointer, a right release picks the centre, a press
    // on a captured pixel picks it, the alpha kept) and the "swap shortcuts"
    // box writing COLORPICKER_SWITCH_CLICKS.
    void colourDialogHslHsvAndTheScreenDropper()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        application->editor().setShowTags(true);
        auto &sampler = *application->colourPicker().sampler();
        sampler.setRoute({QStringLiteral("grab"), {}});
        sampler.setSource([](QPoint) {
            QImage image(7, 7, QImage::Format_RGB32);
            image.fill(qRgb(0x10, 0x20, 0x30));
            image.setPixel(0, 0, qRgb(1, 2, 3));
            return image;
        });
        auto *dialog = named("colourDialog");
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 0);
        QVERIFY(QMetaObject::invokeMethod(visualItem("changeColour1"), "click"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        // White: HSV saturation 0, value 255; HSL lightness 255.
        QCOMPARE(dialog->property("hsvSaturation").toInt(), 0);
        QCOMPARE(dialog->property("hsvValue").toInt(), 255);
        QCOMPARE(dialog->property("lightness").toInt(), 255);

        QVERIFY(QMetaObject::invokeMethod(dialog, "setRgb", Q_ARG(QVariant, 255), Q_ARG(QVariant, 128), Q_ARG(QVariant, 0)));
        QCOMPARE(dialog->property("hsvHue").toInt(), 21);
        QCOMPARE(dialog->property("hsvSaturation").toInt(), 255);
        QCOMPARE(dialog->property("hslHue").toInt(), 21);
        QCOMPARE(dialog->property("lightness").toInt(), 127);
        QCOMPARE(dialogItem("colourDialog", "assText")->property("text").toString(), QStringLiteral("&H0080FF&"));
        QCOMPARE(dialogItem("colourDialog", "htmlText")->property("text").toString(), QStringLiteral("#FF8000"));
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\1c&H0080FF&}first"));

        // The HSV hue field: hsv_to_rgb's special case 171 is blue, the HSL
        // values from hsv_to_hsl.
        auto *hsvHue = dialogItem("colourDialog", "hsvHue");
        hsvHue->setProperty("value", 171);
        QVERIFY(QMetaObject::invokeMethod(hsvHue, "valueModified"));
        QCOMPARE(dialog->property("blue").toInt(), 255);
        QCOMPARE(dialog->property("red").toInt(), 0);
        QCOMPARE(dialog->property("hslHue").toInt(), 171);
        QCOMPARE(dialog->property("lightness").toInt(), 127);
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("{\\1c&HFF0000&}first"));
        // The lightness field: hsl_to_rgb, and the HSV values from hsl_to_hsv.
        auto *lightness = dialogItem("colourDialog", "lightness");
        lightness->setProperty("value", 64);
        QVERIFY(QMetaObject::invokeMethod(lightness, "valueModified"));
        const auto rgb = application->colourPicker().hslToRgb(171, 255, 64);
        const auto hsv = application->colourPicker().hslToHsv(171, 255, 64);
        QCOMPARE(dialog->property("blue").toInt(), rgb.at(2).toInt());
        QCOMPARE(dialog->property("hsvValue").toInt(), hsv.at(2).toInt());
        // The HTML field reads html_to_color: three digits double up.
        auto *html = dialogItem("colourDialog", "htmlText");
        html->setProperty("text", QStringLiteral("#f80"));
        QVERIFY(QMetaObject::invokeMethod(html, "editingFinished"));
        QCOMPARE(dialog->property("green").toInt(), 136);

        // An alpha first: the dropper and the recent colours keep it.
        auto *alpha = dialogItem("colourDialog", "alpha");
        alpha->setProperty("value", 0x40);
        QVERIFY(QMetaObject::invokeMethod(alpha, "valueModified"));
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\1a&H40&")));

        // The dropper: the icon's left press takes the pointer; a move
        // anywhere captures, the right release picks the centre and lets go.
        const QPoint icon = centreOf(dialogItem("colourDialog", "eyedropperArea"));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, icon);
        QTRY_VERIFY(sampler.tracking());
        QVERIFY(dialog->property("dropping").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, icon); // the dropper's: a capture only
        QVERIFY(sampler.tracking());
        sendMouse(window, QEvent::MouseMove, QPoint(-30, -30), Qt::NoButton, Qt::NoButton);
        QVERIFY(sampler.tracking());
        sendMouse(window, QEvent::MouseButtonRelease, QPoint(-30, -30), Qt::RightButton, Qt::NoButton);
        QVERIFY(!sampler.tracking());
        QCOMPARE(dialog->property("red").toInt(), 0x10);
        QCOMPARE(dialog->property("alpha").toInt(), 0x40);
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\1c&H302010&")));
        // A left press on a captured pixel picks it.
        auto *cells = dialogItem("colourDialog", "dropperCells");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, cells->mapToScene(QPointF(3, 3)).toPoint());
        QCOMPARE(dialog->property("blue").toInt(), 3);
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\1c&H030201&")));
        // A second left press on the icon lets go without a pick.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, icon);
        QTRY_VERIFY(sampler.tracking());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, icon);
        sendMouse(window, QEvent::MouseButtonPress, QPoint(-30, -30), Qt::LeftButton, Qt::LeftButton);
        QVERIFY(!sampler.tracking());
        sendMouse(window, QEvent::MouseButtonRelease, QPoint(-30, -30), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(dialog->property("blue").toInt(), 3);

        // A recent colour's tap takes its red, green and blue and keeps the
        // alpha in effect (OnRecentSelect, ColorPicker.cpp:1130-1137:
        // SetColor(color, 0, true, false), setAlpha false at :717).
        application->colourPicker().loadFromString(QStringLiteral("&H80123456&"));
        auto *swatch = dialogItem("colourDialog", "recent0");
        QTRY_COMPARE(swatch->property("color").value<QColor>(), QColor(0x56, 0x34, 0x12));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(swatch));
        QCOMPARE(dialog->property("red").toInt(), 0x56);
        QCOMPARE(dialog->property("green").toInt(), 0x34);
        QCOMPARE(dialog->property("blue").toInt(), 0x12);
        QCOMPARE(dialog->property("alpha").toInt(), 0x40);
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\1c&H123456&")));
        QVERIFY(text->property("text").toString().contains(QStringLiteral("\\1a&H40&")));

        // The type choice puts the colour into the recent ones first.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("colourDialog", "colourType"), "activated", Q_ARG(int, 1)));
        const auto recent = application->colourPicker().recent().first().toMap();
        QCOMPARE(recent.value(QStringLiteral("r")).toInt(), 0x56);
        QCOMPARE(recent.value(QStringLiteral("a")).toInt(), 0x40);

        // The swap box writes the option at once.
        auto *swap = dialogItem("colourDialog", "switchClicks");
        QVERIFY(!swap->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(swap, "toggle"));
        QVERIFY(QMetaObject::invokeMethod(swap, "toggled"));
        QVERIFY(application->settingsStore()->boolean("colourPicker.switchClicks"));

        // The portal route (Wayland): the icon's press asks the desktop and
        // takes no pointer; its pick sets the colour with the alpha kept, as
        // the dropper's; a failure shows under the icon until the next ask;
        // a cancel changes nothing. The type choice above left the second
        // colour in effect with its own alpha; an alpha for it first.
        alpha->setProperty("value", 0x40);
        QVERIFY(QMetaObject::invokeMethod(alpha, "valueModified"));
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\2a&H40&")));
        int asked = 0;
        sampler.setPortalRequest([&] { ++asked; });
        sampler.setRoute({QStringLiteral("portal"), {}});
        auto *note = dialogItem("colourDialog", "dropperUnavailable");
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, icon);
        QCOMPARE(asked, 1);
        QVERIFY(sampler.portalBusy());
        QVERIFY(!sampler.tracking());
        QVERIFY(!dialog->property("dropping").toBool());
        QVERIFY(!dialogItem("colourDialog", "eyedropper")->isEnabled());
        sampler.answerPortal(0, 0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0);
        QVERIFY(!sampler.portalBusy());
        QVERIFY(!dialog->property("portalAsked").toBool());
        QCOMPARE(dialog->property("red").toInt(), 0x20);
        QCOMPARE(dialog->property("green").toInt(), 0x40);
        QCOMPARE(dialog->property("blue").toInt(), 0x60);
        QCOMPARE(dialog->property("alpha").toInt(), 0x40);
        QTRY_VERIFY(text->property("text").toString().contains(QStringLiteral("\\2c&H604020&")));
        QVERIFY(dialogItem("colourDialog", "eyedropper")->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, icon);
        QCOMPARE(asked, 2);
        sampler.failPortalCall(QStringLiteral("No such interface"));
        QCOMPARE(note->property("text").toString(), QStringLiteral("No such interface"));
        QVERIFY(note->isVisible());
        QVERIFY(!dialog->property("portalAsked").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, icon);
        QCOMPARE(asked, 3);
        QVERIFY(note->property("text").toString().isEmpty());
        sampler.answerPortal(1);
        QVERIFY(!dialog->property("portalAsked").toBool());
        QCOMPARE(dialog->property("red").toInt(), 0x20);
        QVERIFY(note->property("text").toString().isEmpty());
        sampler.setPortalRequest({});

        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY(!dialog->property("visible").toBool());

        // Unavailable: the icon is disabled and the reason shown.
        sampler.setRoute(ui::ScreenSampler::routeFor(QStringLiteral("wayland"), true, false));
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centreOf(visualItem("changeColour1")));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(!dialogItem("colourDialog", "eyedropper")->isEnabled());
        QCOMPARE(dialogItem("colourDialog", "dropperUnavailable")->property("text").toString(), sampler.unavailableReason());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
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

    // E3 / R4-uchardet: Paste translation reads the file through
    // OpenWrite::FileOpen as legacy OnPasteTextTl did: uchardet's charset, the
    // platform's line ends, and nothing at all from an empty file.
    void pasteTranslationReadsAsLegacyFileOpen()
    {
        const QString path = dir.filePath(QStringLiteral("tl-charset.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,A\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,B\n");
        }
        const QString empty = dir.filePath(QStringLiteral("tl-empty.txt"));
        {
            QFile f(empty);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        // Two Polish lines in cp1250 with CRLF.
        const QString polish = dir.filePath(QStringLiteral("tl-cp1250.txt"));
        {
            QFile f(polish);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1, \x9F" "d\x9F" "b\xB3o trawy.\r\n"
                    "Pchn\xB9\xE6 w t\xEA \xB3\xF3" "d\x9F je\xBF" "a lub o\x9Cm skrzy\xF1 fig.\r\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto steps = session->historySize();
        QVERIFY(!application->pasteTranslationFile(QUrl::fromLocalFile(empty)));
        QCOMPARE(session->historySize(), steps);
        QVERIFY(!session->document().scriptInfo(u8"TLMode"));
        QVERIFY(application->pasteTranslationFile(QUrl::fromLocalFile(polish)));
        const auto tr = [&](int row) {
            const auto &t = session->document().lines()[static_cast<std::size_t>(row)]->translation;
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        QCOMPARE(tr(0), QString::fromUtf8("Zażółć gęślą jaźń, źdźbło trawy."));
        QCOMPARE(tr(1), QString::fromUtf8("Pchnąć w tę łódź jeża lub ośm skrzyń fig."));
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
        window->requestActivate();
        if (!becomesFocusWindow(window))
            QSKIP("The window manager did not activate the main window");
        item("editingGrid")->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(root, "cyclePanels", Q_ARG(QVariant, -1)));
        // (QTest::qWaitForWindowActive would pass at once: Qt reports the
        // floating window active while its transient parent is.)
        if (!becomesFocusWindow(floating))
            QSKIP("The window manager did not activate the floating panel's window");
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

    // D1 Windows gate: Right on View > Panels did not open it. Menus open as
    // items of the main window (Popup.Item), so it stays the focus window and
    // the shell's Right and Left (Next and Previous frame) took the keys on
    // Windows. Right opens a submenu, Left closes it, as in legacy's menus
    // (Menu.cpp:1263-1276), and the keyboard's item shows its highlight.
    void menuArrowsOpenAndCloseSubmenus()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        auto *root = engine->rootObjects().first();
        QVERIFY(root->property("shellActive").toBool());
        QObject *nextFrame = nullptr;
        std::function<void(QQuickItem *)> find = [&](QQuickItem *from) {
            if (auto *o = from->findChild<QObject *>(QStringLiteral("globalHotkey_GLOBAL_NEXT_FRAME"), Qt::FindDirectChildrenOnly))
                nextFrame = o;
            for (QQuickItem *child : from->childItems())
                find(child);
        };
        find(window->contentItem());
        QVERIFY(nextFrame); // Right is a shell shortcut
        QSignalSpy frameSteps(nextFrame, SIGNAL(activated()));
        auto *view = named("viewMenu");
        auto *panels = named("panelsMenu");
        QVERIFY(view && panels);
        const auto current = [](QObject *menu) {
            const auto entries = menuItems(menu);
            const int index = menu->property("currentIndex").toInt();
            return index >= 0 && index < entries.size() ? entries[index] : nullptr;
        };
        // The keyboard's item: highlighted, with a background that shows.
        const auto showsHighlight = [](QQuickItem *entry) {
            auto *background = entry ? entry->property("background").value<QQuickItem *>() : nullptr;
            return entry && entry->property("highlighted").toBool() && background && background->isVisible()
                   && background->property("color").value<QColor>().alpha() > 0;
        };

        QVERIFY(QMetaObject::invokeMethod(view, "open"));
        QTRY_VERIFY(view->property("opened").toBool());
        QVERIFY(root->property("menuHasKeys").toBool());
        press(Qt::Key_Down); // View > Panels
        QCOMPARE(current(view)->property("subMenu").value<QObject *>(), panels);
        QVERIFY(showsHighlight(current(view)));
        press(Qt::Key_Right);
        QTRY_VERIFY(panels->property("opened").toBool());
        QVERIFY(showsHighlight(current(panels))); // its first panel
        auto *panelMenu = current(panels)->property("subMenu").value<QObject *>();
        QVERIFY(panelMenu);
        press(Qt::Key_Right);
        QTRY_VERIFY(panelMenu->property("opened").toBool());
        QVERIFY(showsHighlight(current(panelMenu))); // Show
        press(Qt::Key_Left);
        QTRY_VERIFY(!panelMenu->property("visible").toBool());
        QVERIFY(panels->property("visible").toBool());
        press(Qt::Key_Left);
        QTRY_VERIFY(!panels->property("visible").toBool());
        QVERIFY(view->property("visible").toBool());
        QCOMPARE(frameSteps.count(), 0);
        press(Qt::Key_Escape);
        QTRY_VERIFY(!view->property("visible").toBool());
        QTRY_VERIFY(root->property("shellActive").toBool());
    }

    // D1: every menu and menu item in src/ui is a ShellMenu or ShellMenuItem
    // (or a type built on them), so the keyboard's item shows its highlight on
    // Windows and K1's iconRole reaches every item. A plain Menu or MenuItem
    // is only the base of those two files.
    void everyMenuIsAShellMenu()
    {
        static const QRegularExpression plain(
            QStringLiteral(R"re((?:^|[^\w.])((?:\w+\.)?(?:Menu|MenuItem))\s*\{)re"), QRegularExpression::MultilineOption);
        int files = 0;
        QDirIterator it(QStringLiteral(HIKARI_UI_SOURCE_DIR), {QStringLiteral("*.qml")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString name = QFileInfo(path).fileName();
            if (name == QLatin1String("ShellMenu.qml") || name == QLatin1String("ShellMenuItem.qml"))
                continue;
            // K2: the controls style defines Menu itself (ShellMenu builds on it).
            if (path.startsWith(QStringLiteral(HIKARI_UI_SOURCE_DIR "/style/")))
                continue;
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            ++files;
            const QString text = QString::fromUtf8(file.readAll());
            for (auto m = plain.globalMatch(text); m.hasNext();) {
                const auto match = m.next();
                const int line = int(text.left(match.capturedStart(1)).count(QLatin1Char('\n'))) + 1;
                QFAIL(qPrintable(QStringLiteral("%1:%2: %3 { instead of ShellMenu / ShellMenuItem")
                                     .arg(name).arg(line).arg(match.captured(1))));
            }
        }
        QVERIFY(files > 1);
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
        // (their bodies start lower under the Grid's tab bar: compare the groups)
        QCOMPARE(qRound(groupTop(audio)), qRound(groupTop(gridPanel)));
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

    // D1 native gate: Float leaves the focus on the panel, in its new window;
    // F6 and Shift+F6 activate that window and the main one; Show focuses a
    // floating panel. Qt reports a floating panel's window active whenever
    // the main window is, so this checks the application's focus window.
    void floatF6AndShowActivateTheFloatingPanelsWindow()
    {
        QVERIFY(application->openFile(episode));
        auto *root = engine->rootObjects().first();
        item("editingGrid")->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(named("panelFloatEditor"), "triggered"));
        QTRY_VERIFY(item("editorPanel")->window() != window);
        QWindow *floating = item("editorPanel")->window();
        if (!becomesFocusWindow(floating))
            QSKIP("The window manager did not give the new floating window the focus");
        QTRY_VERIFY(item("editorPanel")->hasActiveFocus()); // focus-after-float

        // F6 from the floating Line editor: the Grid, in the main window.
        QTest::keyClick(floating, Qt::Key_F6);
        if (!becomesFocusWindow(window))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QTRY_COMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        // Shift+F6 from the Grid: back into the floating Line editor.
        press(Qt::Key_F6, Qt::ShiftModifier);
        if (!becomesFocusWindow(floating))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QTRY_VERIFY(item("editorPanel")->hasActiveFocus());
        // Shift+F6 again: Audio, in the main window.
        QTest::keyClick(floating, Qt::Key_F6, Qt::ShiftModifier);
        if (!becomesFocusWindow(window))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QTRY_COMPARE(focusedPanel(), QStringLiteral("audioPanel"));
        // F6 from Audio: into the floating Line editor.
        press(Qt::Key_F6);
        if (!becomesFocusWindow(floating))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QTRY_VERIFY(item("editorPanel")->hasActiveFocus());

        // View > Panels > Line editor > Show from the main window focuses it.
        window->requestActivate();
        item("editingGrid")->forceActiveFocus();
        if (!becomesFocusWindow(window))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QVERIFY(QMetaObject::invokeMethod(named("panelShowEditor"), "triggered"));
        if (!becomesFocusWindow(floating))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QTRY_VERIFY(item("editorPanel")->hasActiveFocus());

        // Dock: the panel keeps the focus, back in the main window.
        QVERIFY(QMetaObject::invokeMethod(named("panelDockEditor"), "triggered"));
        QTRY_COMPARE(item("editorPanel")->window(), window);
        if (!becomesFocusWindow(window))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        QTRY_COMPARE(focusedPanel(), QStringLiteral("editorPanel"));

        // Float it again: the main window keeps no focused item, so when it
        // is activated again (by the window manager) the Grid takes the focus.
        QVERIFY(QMetaObject::invokeMethod(named("panelFloatEditor"), "triggered"));
        QTRY_VERIFY(item("editorPanel")->window() != window);
        if (!becomesFocusWindow(item("editorPanel")->window()))
            QSKIP("The window manager refused the shell's activation request (its focus-stealing policy)");
        window->requestActivate();
        if (!becomesFocusWindow(window))
            QSKIP("The window manager did not activate the main window");
        QTRY_COMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        QVERIFY(root);
    }

    // D1 native gate: View > Move panel… shows the focused panel and the Grid
    // as "Next to" when it opens, and keyboard changes keep a value.
    void placementWindowShowsItsDefaultsAndKeyboardChanges()
    {
        auto *root = engine->rootObjects().first();
        auto *placement = root->findChild<QQuickWindow *>(QStringLiteral("placementWindow"));
        item("editingGrid")->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(named("movePanel"), "triggered"));
        QTRY_VERIFY(placement->isVisible());
        auto *panelBox = findItem(placement->contentItem(), QStringLiteral("placementPanel"));
        auto *kindBox = findItem(placement->contentItem(), QStringLiteral("placementKind"));
        auto *targetBox = findItem(placement->contentItem(), QStringLiteral("placementTarget"));
        QCOMPARE(panelBox->property("displayText").toString(), QStringLiteral("Grid"));
        QCOMPARE(kindBox->property("displayText").toString(), QStringLiteral("Tab with"));
        QCOMPARE(targetBox->property("currentIndex").toInt(), 3);
        QCOMPARE(targetBox->property("displayText").toString(), QStringLiteral("Grid"));
        // Panel: Up twice from the Grid is Audio; Next to: Down from the Grid is the Reference.
        QVERIFY(QTest::qWaitForWindowExposed(placement));
        placement->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), placement);
        panelBox->forceActiveFocus();
        QTest::keyClick(placement, Qt::Key_Up);
        QCOMPARE(panelBox->property("displayText").toString(), QStringLiteral("Line editor"));
        QTest::keyClick(placement, Qt::Key_Up);
        QCOMPARE(panelBox->property("currentIndex").toInt(), 1);
        QCOMPARE(panelBox->property("displayText").toString(), QStringLiteral("Audio"));
        kindBox->forceActiveFocus();
        QTest::keyClick(placement, Qt::Key_Down);
        QCOMPARE(kindBox->property("displayText").toString(), QStringLiteral("Left of"));
        targetBox->forceActiveFocus();
        QTest::keyClick(placement, Qt::Key_Down);
        QCOMPARE(targetBox->property("displayText").toString(), QStringLiteral("Reference"));
        QTest::keyClick(placement, Qt::Key_Up);
        QCOMPARE(targetBox->property("displayText").toString(), QStringLiteral("Grid"));
        // Move: Audio goes left of the Grid (not of another panel).
        QVERIFY(QMetaObject::invokeMethod(findItem(placement->contentItem(), QStringLiteral("placementMove")), "click"));
        auto *audio = item("audioPanel");
        auto *gridPanel = item("gridPanel");
        QTRY_VERIFY(audio->mapToScene(QPointF(0, 0)).x() < gridPanel->mapToScene(QPointF(0, 0)).x());
        // (their bodies start lower under the Grid's tab bar: compare the groups)
        QCOMPARE(qRound(groupTop(audio)), qRound(groupTop(gridPanel)));
        // The panel combo still names a panel after the move.
        QCOMPARE(panelBox->property("displayText").toString(), QStringLiteral("Audio"));
        placement->close();
    }

    // D1 native gate: the file drop target takes file drags only and is not
    // what the docking engine's own hit test finds over the panels, so
    // dragging a panel shows the drop indicators; files still drop (P2).
    void fileDropAreaLeavesPanelDragsToTheDockingEngine()
    {
        auto *dropArea = visualItem("dropArea");
        auto *docking = visualItem("dockingArea");
        QVERIFY(dropArea && docking);
        auto *gridPanel = item("gridPanel");
        const QPointF centre = gridPanel->mapToScene(QPointF(gridPanel->width() / 2, gridPanel->height() / 2));
        // The engine's hit test without drag-and-drop (X11, Windows): the
        // deepest visible item under the cursor, the last child winning.
        std::function<QQuickItem *(QQuickItem *, QPointF)> deepest = [&](QQuickItem *parent, QPointF scenePos) {
            QQuickItem *found = nullptr;
            for (QQuickItem *child : parent->childItems()) {
                if (!child->isVisible() || !child->contains(child->mapFromScene(scenePos)))
                    continue;
                QQuickItem *deeper = deepest(child, scenePos);
                found = deeper ? deeper : child;
            }
            return found;
        };
        QQuickItem *hit = deepest(window->contentItem(), centre);
        QVERIFY(hit);
        QVERIFY2(docking->isAncestorOf(hit), qPrintable(QStringLiteral("hit %1 %2").arg(
                                                 QString::fromLatin1(hit->metaObject()->className()), hit->objectName())));

        // Drag-and-drop (Wayland docking, files everywhere): a drag without
        // files is refused, a drag with files is taken.
        auto drag = [&](QMimeData *mime) {
            QDragEnterEvent enter(centre.toPoint(), Qt::CopyAction | Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            const bool contains = dropArea->property("containsDrag").toBool();
            QDragLeaveEvent leave;
            QCoreApplication::sendEvent(window, &leave);
            return contains;
        };
        QMimeData panel;
        panel.setData(QStringLiteral("application/x-kddockwidgets"), "Audio");
        QVERIFY(!drag(&panel));
        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(episode)});
        QVERIFY(drag(&files));
    }

    // D1 native gate, D3: the panels' headers, tabs and "⋯" buttons are
    // named for assistive technology; the Grid is in the tree, named "Grid".
    void dockingControlsAndTheGridAreAccessible()
    {
        // As with a screen reader running: Qt Quick fills in states (a tab's
        // selection) only while accessibility is active.
        QAccessible::setActive(true);
        const auto inactive = qScopeGuard([] { QAccessible::setActive(false); });
        QVERIFY(application->openFile(episode));
        QAccessibleInterface *top = QAccessible::queryAccessibleInterface(window);
        QVERIFY(top);
        std::function<QAccessibleInterface *(QAccessibleInterface *, QAccessible::Role, const QString &)> find =
            [&](QAccessibleInterface *from, QAccessible::Role role, const QString &name) -> QAccessibleInterface * {
            for (int i = 0; i < from->childCount(); ++i) {
                QAccessibleInterface *child = from->child(i);
                if (!child || child->role() == QAccessible::Cell)
                    continue;
                if (child->role() == role && child->text(QAccessible::Name) == name)
                    return child;
                if (QAccessibleInterface *found = find(child, role, name))
                    return found;
            }
            return nullptr;
        };
        // The Grid panel and its table.
        QAccessibleInterface *gridPanel = find(top, QAccessible::Pane, QStringLiteral("Grid"));
        QVERIFY(gridPanel);
        QCOMPARE(gridPanel->text(QAccessible::Description), QStringLiteral("Editing: episode.ass"));
        QAccessibleInterface *table = find(gridPanel, QAccessible::Table, QStringLiteral("Subtitle lines"));
        QVERIFY(table);
        QCOMPARE(table->tableInterface()->rowCount(), 2);
        QCOMPARE(table->parent()->text(QAccessible::Name), QStringLiteral("Grid"));
        // D3: each header names its panel (a lone panel's title bar) or
        // its tabs (page tabs with their selection); its "⋯" button, named
        // "<panel> options", opens the panel's menu, whose named items float,
        // dock and close it (docs/qt/docking.md: tabs expose their names,
        // selection and close/float actions to assistive technology).
        QAccessibleInterface *audioBar = find(top, QAccessible::TitleBar, QStringLiteral("Audio"));
        QVERIFY(audioBar);
        QAccessibleInterface *audioOptions = find(top, QAccessible::Button, QStringLiteral("Audio options"));
        QVERIFY(audioOptions);
        QVERIFY(audioOptions->actionInterface());
        QVERIFY(audioOptions->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()));
        // No float or close buttons any more.
        QVERIFY(!find(top, QAccessible::Button, QStringLiteral("Float Audio")));
        QVERIFY(!find(top, QAccessible::Button, QStringLiteral("Close Audio")));
        auto *audioDock = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("audioDock"));
        auto *menu = named("panelOptionsMenu");
        QVERIFY(menu);
        audioOptions->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QTRY_VERIFY(menu->property("visible").toBool());
        QCOMPARE(named("panelOptionsFloat")->property("text").toString(), QStringLiteral("Undock"));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsFloat"), "triggered"));
        QTRY_VERIFY(audioDock->property("isFloating").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close")); // (a click on the item closes it)
        QAccessibleInterface *floatingTop = QAccessible::queryAccessibleInterface(item("audioPanel")->window());
        QAccessibleInterface *floatingOptions = nullptr;
        QTRY_VERIFY((floatingOptions = find(floatingTop, QAccessible::Button, QStringLiteral("Audio options"))));
        floatingOptions->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QTRY_VERIFY(menu->property("visible").toBool());
        QCOMPARE(named("panelOptionsFloat")->property("text").toString(), QStringLiteral("Dock"));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsFloat"), "triggered"));
        QTRY_VERIFY(!audioDock->property("isFloating").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));

        // Tabs: Timing opens as a tab beside the Line editor; each tab is a
        // named page tab, the selected one carrying the group's "⋯" button.
        auto *timingDock = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("timingDock"));
        QVERIFY(QMetaObject::invokeMethod(named("panelShowTiming"), "triggered"));
        QTRY_VERIFY(timingDock->property("isOpen").toBool());
        top = QAccessible::queryAccessibleInterface(window);
        QTRY_VERIFY(find(top, QAccessible::PageTab, QStringLiteral("Shift times")));
        QAccessibleInterface *editorTab = find(top, QAccessible::PageTab, QStringLiteral("Line editor"));
        QVERIFY(editorTab);
        QAccessibleInterface *timingTab = find(top, QAccessible::PageTab, QStringLiteral("Shift times"));
        QVERIFY(timingTab->state().checked); // Timing, just shown, is the selected tab
        QVERIFY(!editorTab->state().checked);
        QVERIFY(find(top, QAccessible::PageTabList, QStringLiteral("Panels")));
        // The group's name is said once, by its tabs: no title bar above
        // them, and the button is the selected panel's.
        QVERIFY(!find(top, QAccessible::TitleBar, QStringLiteral("Line editor")));
        QVERIFY(!find(top, QAccessible::TitleBar, QStringLiteral("Shift times")));
        QAccessibleInterface *timingOptions = find(top, QAccessible::Button, QStringLiteral("Shift times options"));
        QVERIFY(timingOptions);
        QVERIFY(!find(top, QAccessible::Button, QStringLiteral("Line editor options")));
        // A tab selects its panel (its press action).
        QVERIFY(editorTab->actionInterface());
        editorTab->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QTRY_VERIFY(find(QAccessible::queryAccessibleInterface(window), QAccessible::Button, QStringLiteral("Line editor options")));
        timingTab = find(QAccessible::queryAccessibleInterface(window), QAccessible::PageTab, QStringLiteral("Shift times"));
        timingTab->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QAccessibleInterface *closeFrom = nullptr;
        QTRY_VERIFY((closeFrom = find(QAccessible::queryAccessibleInterface(window), QAccessible::Button,
                                      QStringLiteral("Shift times options"))));
        closeFrom->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsClose"), "triggered"));
        QTRY_VERIFY(!timingDock->property("isOpen").toBool());
    }

    // D3: a panel's "⋯" menu (MuseScore's, docs/research/musescore-docking.md
    // §1): Move panel…, Undock (Dock while it floats) and Close, each
    // disabled where the panel cannot do it (a dock that is not closable).
    // No item repeats a control of the header (the Reference tray's toolbar
    // keeps its commands), and the tray's one close is the menu's Close
    // reference. The button draws the three dots of K1's panel-menu.
    void panelMenuHoldsMoveUndockAndClose()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        auto *referenceDock = item<QObject>("referenceDock");
        QTRY_VERIFY(referenceDock->property("isOpen").toBool());
        auto *menu = named("panelOptionsMenu");
        QVERIFY(menu);
        QVERIFY(openPanelMenu(QStringLiteral("Reference")));
        const QStringList referenceItems = panelMenuItems();
        QCOMPARE(referenceItems, (QStringList{QStringLiteral("Move panel…"), QStringLiteral("Undock"),
                                              QStringLiteral("Close reference")}));
        // No label twice in one header: the toolbar's buttons are not menu items.
        QQuickItem *referenceHeader = dockHeader(QStringLiteral("Reference"));
        std::function<void(QQuickItem *)> noRepeat = [&](QQuickItem *from) {
            for (QQuickItem *child : from->childItems()) {
                if (child->isVisible() && child->inherits("QQuickAbstractButton")) {
                    const QString text = child->property("text").toString();
                    QVERIFY2(!referenceItems.contains(text), qPrintable(text));
                }
                noRepeat(child);
            }
        };
        noRepeat(findItem(referenceHeader, QStringLiteral("dockToolbarSlot")));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(findItem(referenceHeader, QStringLiteral("dockMenuButton"))->property("contentItem").value<QObject *>()
                     ->property("iconRole").toString(),
                 QStringLiteral("panel-menu"));
        // The "⋯" button shows as pressed while its menu is open.
        QVERIFY(openPanelMenu(QStringLiteral("Video")));
        QVERIFY(findItem(dockHeader(QStringLiteral("Video")), QStringLiteral("dockMenuButton"))->property("checked").toBool());
        QCOMPARE(panelMenuItems(), (QStringList{QStringLiteral("Move panel…"), QStringLiteral("Undock"), QStringLiteral("Close")}));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!findItem(dockHeader(QStringLiteral("Video")), QStringLiteral("dockMenuButton"))->property("checked").toBool());

        // Not closable: Close is off.
        auto *videoDock = item<QObject>("videoDock");
        QVERIFY(videoDock->setProperty("options", 1)); // DockWidgetOption_NotClosable
        QVERIFY(openPanelMenu(QStringLiteral("Video")));
        QCOMPARE(panelMenuItems(), (QStringList{QStringLiteral("Move panel…"), QStringLiteral("Undock"), QStringLiteral("Close (off)")}));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QVERIFY(videoDock->setProperty("options", 0));

        // Undock floats it; from its floating header the item reads Dock.
        QVERIFY(openPanelMenu(QStringLiteral("Video")));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsFloat"), "triggered"));
        QTRY_VERIFY(videoDock->property("isFloating").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(dockHeader(QStringLiteral("Video")) && dockHeader(QStringLiteral("Video"))->window() != window);
        QVERIFY(openPanelMenu(QStringLiteral("Video")));
        QCOMPARE(panelMenuItems(), (QStringList{QStringLiteral("Move panel…"), QStringLiteral("Dock"), QStringLiteral("Close")}));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsFloat"), "triggered"));
        QTRY_VERIFY(!videoDock->property("isFloating").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));

        // Move panel… opens the placement window on that panel.
        QVERIFY(openPanelMenu(QStringLiteral("Grid")));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsMove"), "triggered"));
        auto *placement = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("placementWindow"));
        QTRY_VERIFY(placement->isVisible());
        QCOMPARE(findItem(placement->contentItem(), QStringLiteral("placementPanel"))->property("displayText").toString(),
                 QStringLiteral("Grid"));
        placement->close();
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));

        // Close hides the panel (its content stays).
        QVERIFY(openPanelMenu(QStringLiteral("Video")));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsClose"), "triggered"));
        QTRY_VERIFY(!videoDock->property("isOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QVERIFY(QMetaObject::invokeMethod(named("panelShowVideo"), "triggered"));
        QTRY_VERIFY(videoDock->property("isOpen").toBool());

        // The Reference tray's Close reference ends the reference, and the
        // tray closes with it.
        QVERIFY(openPanelMenu(QStringLiteral("Reference")));
        QVERIFY(QMetaObject::invokeMethod(named("panelOptionsClose"), "triggered"));
        QTRY_VERIFY(!application->workspace().reference());
        QTRY_VERIFY(!referenceDock->property("isOpen").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    }

    // D3: the keyboard reaches a panel's header and its menu: Shift+Tab
    // from the panel's content to its header (the selected tab of a tab
    // bar), Right to the "⋯" button, Space to open the menu, Escape back to
    // the button; F6 goes on from a header to the next panel.
    void keyboardReachesThePanelHeaderAndItsMenu()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        QTRY_VERIFY(item<QObject>("referenceDock")->property("isOpen").toBool());
        auto *grid = item("editingGrid");
        grid->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        press(Qt::Key_Tab, Qt::ShiftModifier);
        QQuickItem *header = dockHeader(QStringLiteral("Grid"));
        QQuickItem *tab = header->property("currentTab").value<QQuickItem *>();
        QTRY_VERIFY2(tab->hasActiveFocus(), window->activeFocusItem() ? qPrintable(window->activeFocusItem()->objectName()) : "none");
        QVERIFY(findItem(tab, QStringLiteral("tabFocusRing"))->isVisible());
        QVERIFY(!findItem(header, QStringLiteral("focusRing"))->isVisible()); // the tab's ring, not the header's
        QQuickItem *button = findItem(header, QStringLiteral("dockMenuButton"));
        press(Qt::Key_Right);
        QTRY_VERIFY(button->hasActiveFocus());
        press(Qt::Key_Space);
        auto *menu = named("panelOptionsMenu");
        QTRY_VERIFY(menu->property("visible").toBool());
        QCOMPARE(panelMenuItems().last(), QStringLiteral("Close"));
        QTest::keyClick(QGuiApplication::focusWindow(), Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QTRY_VERIFY(button->hasActiveFocus());
        press(Qt::Key_Left);
        QTRY_VERIFY(tab->hasActiveFocus());
        // F6 from the Grid's header: the next panel, the Reference tray.
        press(Qt::Key_F6);
        QTRY_COMPARE(focusedPanel(), QStringLiteral("referencePanel"));
    }

    // D3: a double-click on a panel's header floats it, and on its floating
    // header docks it again; the floating panel is a borderless tool window
    // keeping its header.
    void doubleClickOnAHeaderFloatsAndDocks()
    {
        QVERIFY(application->openFile(episode));
        auto *audioDock = item<QObject>("audioDock");
        QQuickItem *header = dockHeader(QStringLiteral("Audio"));
        QVERIFY(header && header->property("titleMode").toBool());
        QTest::mouseDClick(window, Qt::LeftButton, {}, header->mapToScene(QPointF(60, header->height() / 2)).toPoint());
        QTRY_VERIFY(audioDock->property("isFloating").toBool());
        QQuickItem *floating = nullptr;
        QTRY_VERIFY((floating = dockHeader(QStringLiteral("Audio"))) && floating->window() != window);
        QWindow *w = floating->window();
        QVERIFY(w->flags().testFlag(Qt::Tool));
        QVERIFY(w->flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(floating->property("titleMode").toBool());
        QTRY_VERIFY(w->isExposed());
        QTest::mouseDClick(w, Qt::LeftButton, {}, floating->mapToScene(QPointF(60, floating->height() / 2)).toPoint());
        QTRY_VERIFY(!audioDock->property("isFloating").toBool());
        QTRY_COMPARE(dockHeader(QStringLiteral("Audio"))->window(), static_cast<QWindow *>(window));
    }

    // D3: the bottom panels keep a one-tab header even alone (the Grid, the
    // Reference tray, Search), the Reference tray's toolbar in it right of
    // the tab (MuseScore's toolbar slot) instead of a row of its own, with
    // no close button (Close reference is the menu's). The toolbar's free
    // part is header too: a double-click floats the panel and a right
    // click opens its menu, as a right click on a toolbar button does. The
    // header shows the move cursor, its buttons the arrow.
    void bottomPanelsKeepATabAndTheTrayToolbar()
    {
        QVERIFY(application->openFile(episode));
        QVERIFY(application->openReference(original));
        QTRY_VERIFY(item<QObject>("referenceDock")->property("isOpen").toBool());
        for (const QString &panel : {QStringLiteral("Grid"), QStringLiteral("Reference")}) {
            QQuickItem *header = nullptr;
            QTRY_VERIFY((header = dockHeader(panel)));
            QVERIFY2(!header->property("titleMode").toBool(), qPrintable(panel));
            QCOMPARE(header->property("count").toInt(), 1);
        }
        QQuickItem *header = dockHeader(QStringLiteral("Reference"));
        QCOMPARE(header->property("currentTitle").toString(), QStringLiteral("Reference: original.ass"));
        auto *bar = item("referenceBar");
        QVERIFY(bar);
        QTRY_COMPARE(bar->parentItem(), findItem(header, QStringLiteral("dockToolbarSlot")));
        QVERIFY(bar->isVisible());
        // right of the tab, inside the header
        const QRectF inHeader = bar->mapRectToItem(header, QRectF(0, 0, bar->width(), bar->height()));
        QVERIFY(inHeader.top() >= 0 && inHeader.bottom() <= header->height());
        QQuickItem *tab = header->property("currentTab").value<QQuickItem *>();
        QVERIFY(inHeader.left() >= tab->mapToItem(header, QPointF(tab->width(), 0)).x());
        // and it works there
        const bool linked = application->shell().referenceLinked();
        QTest::mouseClick(window, Qt::LeftButton, {}, centreOf(item("referenceLinked")));
        QTRY_COMPARE(application->shell().referenceLinked(), !linked);
        // No close button in the header: no tab-close mark, nothing named Close.
        std::function<void(QQuickItem *)> noClose = [&](QQuickItem *from) {
            for (QQuickItem *child : from->childItems()) {
                if (child->isVisible() && child->inherits("QQuickAbstractButton")) {
                    QVERIFY2(!child->property("text").toString().startsWith(QStringLiteral("Close")),
                             qPrintable(child->property("text").toString()));
                    QVERIFY(child->property("iconRole").toString() != QStringLiteral("tab-close"));
                }
                noClose(child);
            }
        };
        noClose(header);
        QVERIFY(!item("referenceClose"));
        // The cursor: the move cursor on the tab and the toolbar's free
        // part, the arrow on the "⋯" button and the toolbar's buttons.
        const auto cursorAt = [&](QPoint at) {
            QTest::mouseMove(window, at);
            QCoreApplication::processEvents();
            return window->cursor().shape();
        };
        auto *status = item("referenceMatchStatus");
        const QPoint statusPoint = status->mapToScene(QPointF(4, status->height() / 2)).toPoint();
        QTRY_COMPARE(cursorAt(centreOf(tab)), Qt::SizeAllCursor);
        QTRY_COMPARE(cursorAt(statusPoint), Qt::SizeAllCursor);
        QTRY_COMPARE(cursorAt(centreOf(findItem(header, QStringLiteral("dockMenuButton")))), Qt::ArrowCursor);
        QTRY_COMPARE(cursorAt(centreOf(item("referenceLinked"))), Qt::ArrowCursor);
        QTRY_COMPARE(cursorAt(centreOf(item("referenceGrid"))), Qt::ArrowCursor);
        // A right click on the toolbar's label, and on one of its buttons:
        // the tray's menu.
        auto *menu = named("panelOptionsMenu");
        for (QQuickItem *target : {static_cast<QQuickItem *>(status), item("referenceNextMatch")}) {
            QTest::mouseClick(window, Qt::RightButton, {}, target == status ? statusPoint : centreOf(target));
            QTRY_VERIFY2(menu->property("visible").toBool(), qPrintable(target->objectName()));
            QCOMPARE(panelMenuItems().last(), QStringLiteral("Close reference"));
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));
            QTRY_VERIFY(!menu->property("visible").toBool());
        }
        // A double-click on the label floats the tray; on its floating
        // header it docks again.
        auto *referenceDock = item<QObject>("referenceDock");
        QTest::mouseDClick(window, Qt::LeftButton, {}, statusPoint);
        QTRY_VERIFY(referenceDock->property("isFloating").toBool());
        QQuickItem *floating = nullptr;
        QTRY_VERIFY((floating = dockHeader(QStringLiteral("Reference"))) && floating->window() != window);
        QTRY_VERIFY(floating->window()->isExposed());
        QTRY_VERIFY(status->window() == floating->window() && status->isVisible());
        QTest::mouseDClick(floating->window(), Qt::LeftButton, {}, status->mapToScene(QPointF(4, status->height() / 2)).toPoint());
        QTRY_VERIFY(!referenceDock->property("isFloating").toBool());
        // Search, opened, is a tab bar too.
        QVERIFY(QMetaObject::invokeMethod(named("panelShowSearch"), "triggered"));
        QQuickItem *search = nullptr;
        QTRY_VERIFY((search = dockHeader(QStringLiteral("Search"))));
        QVERIFY(!search->property("titleMode").toBool());
    }

    // D3: the controls of `panel` its bounds cut off: a button, field,
    // label or list (anything that takes input or shows text) not wholly
    // inside the panel, or a text wider than its box that neither elides nor
    // wraps. Scrolled content counts as its view (the view must fit).
    static QStringList clippedIn(QQuickItem *panel)
    {
        QStringList out;
        const QRectF bounds(0, 0, panel->width(), panel->height());
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *from) {
            for (QQuickItem *child : from->childItems()) {
                if (!child->isVisible() || child->opacity() <= 0 || child->width() <= 0 || child->height() <= 0)
                    continue;
                const bool leaf = child->inherits("QQuickControl") && !child->inherits("QQuickPane")
                                  && !child->inherits("QQuickScrollView");
                const bool text = child->inherits("QQuickText") || child->inherits("QQuickTextInput")
                                  || child->inherits("QQuickTextEdit");
                const bool view = child->inherits("QQuickFlickable");
                if (leaf || text || view) {
                    const QRectF r = child->mapRectToItem(panel, QRectF(0, 0, child->width(), child->height()));
                    const QString name = child->objectName().isEmpty() ? QString::fromLatin1(child->metaObject()->className())
                                                                       : child->objectName();
                    if (r.left() < bounds.left() - 0.5 || r.top() < bounds.top() - 0.5 || r.right() > bounds.right() + 0.5
                        || r.bottom() > bounds.bottom() + 0.5)
                        out << QStringLiteral("%1 at %2,%3 %4x%5").arg(name).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
                    else if (child->inherits("QQuickText") && child->property("elide").toInt() == 3 /* ElideNone */
                             && child->property("wrapMode").toInt() == 0 /* NoWrap */
                             && child->implicitWidth() > child->width() + 1)
                        out << QStringLiteral("%1 text %2 in %3").arg(name).arg(child->implicitWidth()).arg(child->width());
                }
                if (!leaf && !view)
                    walk(child);
            }
        };
        walk(panel);
        return out;
    }

    // D3: each panel reports the smallest size its content takes unclipped
    // to the docking engine, which keeps it at least that big: in the
    // default arrangement, and when separators are dragged as far as they
    // go. Squeezed to the engine's limit from every side, no panel cuts off
    // a control (clippedIn), so a minimum too small fails. The Video
    // panel's follow choices wrap onto two rows rather than be cut off.
    void panelsKeepTheirMinimumSizes()
    {
        QVERIFY(application->openFile(episode));
        auto *docking = engine->singletonInstance<ui::Docking *>("Hikari.Ui", "Docking");
        QVERIFY(docking);
        const char *const panels[] = {"videoPanel", "audioPanel", "editorPanel", "gridPanel"};
        for (const char *name : panels) {
            QQuickItem *panel = item(name);
            const QSizeF minimum = panel->property("minimumSize").toSizeF();
            QVERIFY2(minimum.width() > 8 && minimum.height() > 8, name);
            QTRY_VERIFY2(panel->width() >= minimum.width() && panel->height() >= minimum.height(),
                         qPrintable(QStringLiteral("%1 %2x%3 < %4x%5").arg(QLatin1String(name)).arg(panel->width())
                                        .arg(panel->height()).arg(minimum.width()).arg(minimum.height())));
            const QSize engineMinimum = docking->minimumSize(panel->property("dockName").toString());
            QVERIFY2(engineMinimum.width() >= minimum.width() && engineMinimum.height() >= minimum.height(), name);
            QVERIFY2(clippedIn(panel).isEmpty(), qPrintable(QLatin1String(name) + QLatin1Char(' ') + clippedIn(panel).join(u"; ")));
        }
        // Each panel squeezed as far as the separators go, from its right
        // and bottom edges, then its left and top ones: the engine stops at
        // its minimum and nothing in it is cut off.
        const auto settled = [](QQuickItem *i) {
            const QSizeF size = i->size();
            QTest::qWait(60);
            return size == i->size();
        };
        for (const char *name : panels) {
            QQuickItem *panel = item(name);
            const QString dock = panel->property("dockName").toString();
            QVERIFY(docking->resizeInLayout(dock, 0, 0, -2000, -2000));
            QVERIFY(docking->resizeInLayout(dock, -2000, -2000, 0, 0));
            const QSizeF minimum = panel->property("minimumSize").toSizeF();
            QTRY_VERIFY(settled(panel));
            QVERIFY2(panel->width() >= minimum.width() - 0.5 && panel->height() >= minimum.height() - 0.5,
                     qPrintable(QStringLiteral("%1 %2x%3 < %4x%5").arg(QLatin1String(name)).arg(panel->width())
                                    .arg(panel->height()).arg(minimum.width()).arg(minimum.height())));
            const QStringList clipped = clippedIn(panel);
            QVERIFY2(clipped.isEmpty(), qPrintable(QStringLiteral("%1 at %2x%3: %4").arg(QLatin1String(name)).arg(panel->width())
                                                       .arg(panel->height()).arg(clipped.join(u"; "))));
            application->workspaceLayout().resetLayout();
            QTRY_VERIFY(settled(panel));
        }
        // The check sees a cut-off control: below its minimum (the engine
        // told a smaller one) the Line editor cuts its controls off.
        {
            QQuickItem *panel = item("editorPanel");
            const QString dock = panel->property("dockName").toString();
            QVERIFY(docking->setMinimumSize(dock, QSize(40, 40)));
            QVERIFY(docking->resizeInLayout(dock, 0, 0, -2000, -2000));
            QVERIFY(docking->resizeInLayout(dock, -2000, -2000, 0, 0));
            QTRY_VERIFY(settled(panel));
            QVERIFY2(!clippedIn(panel).isEmpty(), qPrintable(QStringLiteral("%1x%2").arg(panel->width()).arg(panel->height())));
            QVERIFY(QMetaObject::invokeMethod(panel, "reportMinimumSize"));
            QCOMPARE(docking->minimumSize(dock), panel->property("minimumSize").toSizeF().toSize());
            application->workspaceLayout().resetLayout();
            QTRY_VERIFY(settled(panel));
        }
        // Squeezing the Line editor stops at its minimum.
        auto *editorDock = item<QObject>("editorDock");
        QQuickItem *editor = item("editorPanel");
        const qreal minimumHeight = editor->property("minimumSize").toSizeF().height();
        const qreal before = editor->height();
        application->workspaceLayout().resizePanel(editorDock, application->workspaceLayout().panelSize(editorDock).width(), 60);
        QTRY_VERIFY(editor->height() < before || editor->height() <= minimumHeight + 0.5);
        QVERIFY2(editor->height() >= minimumHeight - 0.5, qPrintable(QString::number(editor->height())));
        // A narrow Video panel: the follow choices on two rows, none cut off.
        auto *videoDock = item<QObject>("videoDock");
        application->workspaceLayout().resizePanel(videoDock, 470, application->workspaceLayout().panelSize(videoDock).height());
        auto *choices = item("videoFollowChoices");
        QTRY_VERIFY(choices->property("wrapped").toBool());
        QQuickItem *video = item("videoPanel");
        QTRY_VERIFY(video->width() < 520);
        auto *playAfter = item("videoPlayAfter");
        QTRY_VERIFY2(playAfter->mapToItem(video, QPointF(playAfter->width(), 0)).x() <= video->width(),
                     qPrintable(QString::number(playAfter->mapToItem(video, QPointF(playAfter->width(), 0)).x())));
        QVERIFY2(clippedIn(video).isEmpty(), qPrintable(clippedIn(video).join(u"; ")));
    }

    // D1 native gate: a floating panel no screen shows (a removed monitor, a
    // layout from other screens) comes back onto the main window's screen,
    // and Show brings it back there too.
    void floatingPanelsOffEveryScreenComeBack()
    {
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
            QSKIP("On Wayland the compositor places windows (docs/qt/docking.md)");
        auto *editorDock = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("editorDock"));
        QVERIFY(editorDock->setProperty("isFloating", true));
        QTRY_VERIFY(item("editorPanel")->window() != window);
        QWindow *floating = item("editorPanel")->window();
        QTRY_VERIFY(floating->isVisible());
        const QRect screen = window->screen()->availableGeometry();
        auto titleStrip = [&] {
            const QRect frame = floating->frameGeometry();
            return QRect(frame.topLeft(), QSize(frame.width(), 30));
        };
        auto reachable = [&] { return screen.intersected(titleStrip()).width() >= 80; };
        // Off every screen, not only the main window's: beside the main
        // screen can be another monitor (the Windows gate VM has two).
        const QList<QScreen *> screens = QGuiApplication::screens();
        QRect allScreens;
        for (QScreen *s : screens)
            allScreens |= s->availableGeometry();
        auto offEveryScreen = [&] {
            const QRect strip = titleStrip();
            return std::none_of(screens.begin(), screens.end(),
                                [&](QScreen *s) { return s->availableGeometry().intersects(strip); });
        };
        floating->setFramePosition(QPoint(allScreens.right() + 400, screen.top() + 300));
        QTRY_VERIFY(offEveryScreen());
        QCOMPARE(application->workspaceLayout().keepFloatingPanelsOnScreen(), 1);
        QTRY_VERIFY(reachable());
        QCOMPARE(application->workspaceLayout().keepFloatingPanelsOnScreen(), 0); // nothing else to move

        floating->setFramePosition(QPoint(allScreens.left() - 3000, allScreens.top() - 2000));
        QTRY_VERIFY(offEveryScreen());
        QVERIFY(QMetaObject::invokeMethod(named("panelShowEditor"), "triggered"));
        QTRY_VERIFY(reachable());
        QVERIFY(screen.contains(floating->frameGeometry().topLeft()));
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
        QVERIFY(text.contains(QStringLiteral("Hunspell - Copyright"))); // F3: linked again (R2-hunspell)
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
        QVERIFY(application->saveAll().isEmpty()); // P9: nothing waits for the dialog
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

    // F4: Edit > Fix minor errors: check rules, find, review the results and
    // replace the checked finds as one step; replace all errors in the tab.
    // Driven with real mouse presses: the two modeless dialogs stay open while
    // the other one, the grid or the editor is clicked.
    void misspellReplacerFindsReviewsAndReplaces()
    {
        const QString path = writeFile(dir, "misspell.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello , world , again\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,a  b\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,fine\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("misspellDialog"));
        QVERIFY(dialog);
        auto *results = root->findChild<QObject *>(QStringLiteral("misspellResults"));
        QVERIFY(results);
        const auto centre = [](QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
        const auto click = [&](QQuickItem *item) {
            QVERIFY(item && item->isVisible());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre(item));
        };
        auto *menuItem = root->findChild<QObject *>(QStringLiteral("misspellMenuItem"));
        QVERIFY(QMetaObject::invokeMethod(menuItem->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialog->property("rules").toList().size(), 13);
        // Check "Remove space before comma or dot" in the list.
        QQuickItem *check = nullptr;
        QTRY_VERIFY((check = dialogItem("misspellDialog", "misspellRuleCheck0")));
        click(check);
        QTRY_VERIFY(application->misspellRules().at(0).toMap().value(QStringLiteral("checked")).toBool());
        // Find errors in current tab: a header with the file, then two finds.
        click(dialogItem("misspellDialog", "misspellFindTab"));
        QTRY_VERIFY(results->property("opened").toBool());
        const auto rows = results->property("rows").toList();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows[0].toMap().value(QStringLiteral("text")).toString(), QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("line")).toInt(), 1);
        QCOMPARE(rows[2].toMap().value(QStringLiteral("position")).toInt(), 13);
        // Legacy placement: the Multireplacer centred; the results at
        // wxDefaultPosition (R5-per-platform): on Windows 34x22 dialog units
        // (68x44 pixels) from the client origin below the menu bar, on Linux
        // the window manager's choice, here the window's top left. At the top
        // left the results leave the rules list and the Multireplacer's
        // buttons uncovered, so rules can be checked while they are shown; on
        // Windows they reach the rules list in this window, as legacy's did,
        // and are moved aside first.
        const auto sceneRect = [](QQuickItem *item) { return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())); };
        const auto popupRect = [&](QObject *popup) { return sceneRect(popup->property("background").value<QQuickItem *>()); };
        auto *addRule = dialogItem("misspellDialog", "misspellAddRule");
        auto *menuBar = root->property("menuBar").value<QQuickItem *>();
        QVERIFY(menuBar && menuBar->height() > 0);
        QCOMPARE(dialog->property("clientTop").toReal(), menuBar->height());
        const QRectF rulesArea = sceneRect(dialogItem("misspellDialog", "misspellRules"));
#ifdef _WIN32
        QCOMPARE(popupRect(results).topLeft(), QPointF(68, menuBar->height() + 44));
        QVERIFY(popupRect(results).intersects(rulesArea)); // as legacy's did here
        // moved aside: as far left as their title drag goes (48 px stay on screen)
        results->setProperty("x", 48 - results->property("width").toReal());
        results->setProperty("y", 0);
        const QRectF resultsArea = popupRect(results);
#else
        const QRectF resultsArea = popupRect(results);
        QCOMPARE(resultsArea.topLeft(), QPointF(0, 0));
#endif
        QVERIFY2(!resultsArea.intersects(rulesArea),
                 qPrintable(QStringLiteral("results %1,%2 %3x%4, rules %5,%6 %7x%8")
                                .arg(resultsArea.x()).arg(resultsArea.y()).arg(resultsArea.width()).arg(resultsArea.height())
                                .arg(rulesArea.x()).arg(rulesArea.y()).arg(rulesArea.width()).arg(rulesArea.height())));
        for (const char *button : {"misspellAddRule", "misspellEditRule", "misspellRemoveRule", "misspellFindTab",
                                   "misspellFindAllTabs", "misspellReplaceTab", "misspellReplaceAllTabs"})
            QVERIFY2(!resultsArea.intersects(sceneRect(dialogItem("misspellDialog", button))), button);
        QVERIFY2(qAbs(popupRect(dialog).center().x() - window->width() / 2.0) < 2,
                 qPrintable(QStringLiteral("dialog %1,%2 %3x%4 (implicit %5x%6, x %7), window %8")
                                .arg(popupRect(dialog).x()).arg(popupRect(dialog).y()).arg(popupRect(dialog).width())
                                .arg(popupRect(dialog).height()).arg(dialog->property("implicitWidth").toReal())
                                .arg(dialog->property("implicitHeight").toReal()).arg(dialog->property("x").toReal())
                                .arg(window->width())));
        click(dialogItem("misspellDialog", "misspellRuleCheck2"));
        QTRY_VERIFY(application->misspellRules().at(2).toMap().value(QStringLiteral("checked")).toBool());
        QVERIFY(results->property("visible").toBool());
        click(dialogItem("misspellDialog", "misspellRuleCheck2"));
        QTRY_VERIFY(!application->misspellRules().at(2).toMap().value(QStringLiteral("checked")).toBool());
        // Dragging a title moves its window (legacy HTCAPTION), and it stays
        // there when shown again.
        results->setProperty("x", 0); // from the top left
        results->setProperty("y", 0);
        auto *resultsTitle = results->property("header").value<QQuickItem *>();
        QVERIFY(resultsTitle);
        const QPoint grab = centre(resultsTitle);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, grab);
        QTest::mouseMove(window, grab + QPoint(20, 10));
        QTest::mouseMove(window, grab + QPoint(40, 30));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, grab + QPoint(40, 30));
        QTRY_COMPARE(popupRect(results).topLeft(), QPointF(40, 30));
        QVERIFY(QMetaObject::invokeMethod(results, "close"));
        QTRY_VERIFY(!results->property("visible").toBool());
        click(dialogItem("misspellDialog", "misspellFindTab"));
        QTRY_VERIFY(results->property("opened").toBool());
        QCOMPARE(popupRect(results).topLeft(), QPointF(40, 30));
        // The drag stops where the title would leave the window: its top
        // stays inside, and 48 pixels of the window stay in view sideways.
        const QPoint title = centre(resultsTitle);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, title);
        QTest::mouseMove(window, title - QPoint(10, 10));
        QTest::mouseMove(window, title - QPoint(5000, 5000));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, title - QPoint(5000, 5000));
        QTRY_COMPARE(popupRect(results).topLeft(), QPointF(48 - popupRect(results).width(), 0));
        QVERIFY(sceneRect(resultsTitle).right() > 0); // part of the title is still there to grab
        results->setProperty("x", 0);
        results->setProperty("y", 0);
        // Rules cannot change while the results are shown: a press on Add rule
        // keeps both windows open and is refused.
        click(addRule);
        QCOMPARE(application->log().lastMessage(), QStringLiteral("Cannot change rules\nwhen find results window is open"));
        QVERIFY(results->property("visible").toBool());
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(application->misspellRules().size(), 13);
        auto *resultsContent = results->property("contentItem").value<QQuickItem *>();
        // A click on the header's text folds its finds, another unfolds them.
        QQuickItem *header = nullptr, *firstRow = nullptr, *second = nullptr;
        QTRY_VERIFY((header = findItem(resultsContent, QStringLiteral("misspellResultHeader0"))));
        QTRY_VERIFY((firstRow = findItem(resultsContent, QStringLiteral("misspellResultRow1"))));
        click(header);
        QTRY_VERIFY(!firstRow->isVisible());
        click(header);
        QTRY_VERIFY(firstRow->isVisible());
        // Uncheck the second find with its checkbox.
        QTRY_VERIFY((second = findItem(resultsContent, QStringLiteral("misspellResultCheck2"))));
        click(second);
        QTRY_VERIFY(!second->property("checked").toBool());
        // A double click on a find shows its Line with the find selected.
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, centre(findItem(resultsContent, QStringLiteral("misspellResultRow2"))));
        QTRY_COMPARE(session->selection().active, std::optional(session->document().lines()[0]->id));
        QCOMPARE(application->editor().selectionStart(), 13);
        QCOMPARE(application->editor().selectionEnd(), 15);
        // A click in the main window outside both dialogs closes neither.
        const QPoint outside(window->width() - 12, window->height() / 2);
        QVERIFY(!popupRect(results).contains(outside) && !popupRect(dialog).contains(outside));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, outside);
        QTest::qWait(50);
        QVERIFY(results->property("visible").toBool());
        QVERIFY(dialog->property("visible").toBool());
        // Replace: one "Fixing minor errors" step with the checked find.
        const auto steps = session->historySize();
        auto *replace = findItem(resultsContent, QStringLiteral("misspellReplaceChecked"));
        click(replace);
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Fixing minor errors"));
        auto text = [&](std::size_t row) {
            const auto &t = session->document().lines()[row]->text;
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        QCOMPARE(text(0), QStringLiteral("Hello, world , again"));
        QVERIFY(!replace->property("enabled").toBool()); // once per search
        // Closing the results window leaves the Multireplacer open.
        QVERIFY(QMetaObject::invokeMethod(results, "close"));
        QTRY_VERIFY(!results->property("visible").toBool());
        QVERIFY(dialog->property("visible").toBool());
        // Replace all errors in current tab with "Remove doubled spaces" too.
        click(dialogItem("misspellDialog", "misspellRuleCheck1"));
        QTRY_VERIFY(application->misspellRules().at(1).toMap().value(QStringLiteral("checked")).toBool());
        click(dialogItem("misspellDialog", "misspellReplaceTab"));
        QTRY_COMPARE(session->historySize(), steps + 2);
        QCOMPARE(session->history().back().name, std::string("Fixing minor errors"));
        QCOMPARE(text(0), QStringLiteral("Hello, world, again"));
        QCOMPARE(text(1), QStringLiteral("a b"));
        // The rule buttons: add (unchecked), edit the chosen one, delete it.
        auto *find = dialogItem("misspellDialog", "misspellFind");
        find->setProperty("text", QStringLiteral("fine"));
        dialogItem("misspellDialog", "misspellReplace")->setProperty("text", QStringLiteral("good"));
        click(addRule);
        auto added = application->misspellRules();
        QCOMPARE(added.size(), 14);
        QCOMPARE(added.last().toMap().value(QStringLiteral("find")).toString(), QStringLiteral("fine"));
        QVERIFY(!added.last().toMap().value(QStringLiteral("checked")).toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "chooseRule", Q_ARG(QVariant, 13)));
        find->setProperty("text", QStringLiteral("fin"));
        click(dialogItem("misspellDialog", "misspellEditRule"));
        QCOMPARE(application->misspellRules().last().toMap().value(QStringLiteral("find")).toString(), QStringLiteral("fin"));
        click(dialogItem("misspellDialog", "misspellRemoveRule"));
        QCOMPARE(application->misspellRules().size(), 13);
        // The menu hides the dialog again.
        QVERIFY(QMetaObject::invokeMethod(menuItem->property("action").value<QObject *>(), "trigger"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

    // F4: a find in an untranslated Line in translation mode is selected in
    // the Original field (legacy GetEditor: TextEditOrig when TextEdit is
    // empty), one with a translation in the Translated field; rules that do
    // not compile are logged as wxRegEx::Compile logs them.
    void misspellFindsSelectTheSearchedFieldAndLogInvalidRules()
    {
        const QString path = dir.filePath(QStringLiteral("misspell-tl.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nTLMode: Yes\nTLMode Style: TLmode\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,TLmode,,0,0,0,,orig  one\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,tl  two\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,untranslated  three\n");
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        QVERIFY(application->editor().translationMode());
        for (int i = 0; i < 13; ++i)
            application->checkMisspellRule(i, i == 1); // "Remove doubled spaces"
        const auto rows = application->findMisspells({{QStringLiteral("lines"), 0}}, false);
        QCOMPARE(rows.size(), 3);
        // The translated Line's translation and the untranslated Line's text were searched.
        QCOMPARE(rows[1].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("tl  two"));
        QCOMPARE(rows[2].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("untranslated  three"));
        application->showMisspellFind(1);
        QCOMPARE(session->selection().active, std::optional(session->document().lines()[1]->id));
        QCOMPARE(application->editor().selectionRole(), 0);
        QCOMPARE(application->editor().selectionStart(), 12);
        QCOMPARE(application->editor().selectionEnd(), 14);
        application->showMisspellFind(0);
        QCOMPARE(application->editor().selectionRole(), 1);
        QCOMPARE(application->editor().selectionStart(), 2);
        // An invalid checked rule: logged for Find and Replace all; the
        // results' Replace logs every rule that does not compile.
        QVERIFY(application->addMisspellRule({{QStringLiteral("find"), QStringLiteral("(")}}));
        QVERIFY(application->addMisspellRule({{QStringLiteral("find"), QStringLiteral("[a")}}));
        application->checkMisspellRule(13, true);
        const QString invalid = QStringLiteral("Invalid regular expression '(': ");
        (void)application->findMisspells({{QStringLiteral("lines"), 0}}, false);
        QVERIFY2(application->log().lastMessage().startsWith(invalid), qPrintable(application->log().lastMessage()));
        application->replaceMisspellFinds({});
        QVERIFY2(application->log().lastMessage().startsWith(QStringLiteral("Invalid regular expression '[a': ")),
                 qPrintable(application->log().lastMessage()));
        application->replaceMisspells({{QStringLiteral("lines"), 0}}, false);
        QVERIFY2(application->log().lastMessage().startsWith(invalid), qPrintable(application->log().lastMessage()));
        // The valid checked rule still ran.
        const auto &line = *session->document().lines()[1];
        QCOMPARE(std::string(line.text.begin(), line.text.end()), std::string("untranslated three"));
    }

    // F4: Rules.txt beside the INI file, written when the application ends
    // (not when the list is empty) and read back by the next one.
    void misspellRulesPersist()
    {
        QDir().mkpath(dir.filePath(QStringLiteral("rules")));
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("rules/hikari.ini"));
        const QString rulesFile = dir.filePath(QStringLiteral("rules/Rules.txt"));
        {
            app::Application first(options);
            QCOMPARE(first.misspellRules().size(), 13);
            first.checkMisspellRule(2, true);
            QVERIFY(first.addMisspellRule({{QStringLiteral("description"), QStringLiteral("Mine")},
                                           {QStringLiteral("find"), QStringLiteral("teh")},
                                           {QStringLiteral("replace"), QStringLiteral("the")},
                                           {QStringLiteral("options"), 1}}));
        }
        QFile file(rulesFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        file.close();
        QVERIFY(bytes.startsWith("\xEF\xBB\xBF#HikariSub rules file\r\n0|0|1|0|0|0|0|0|0|0|0|0|0|0\r\n"));
        QVERIFY(bytes.endsWith("Mine\fteh\fthe\f1\r\n"));
        {
            app::Application second(options);
            const auto rules = second.misspellRules();
            QCOMPARE(rules.size(), 14);
            QVERIFY(rules[2].toMap().value(QStringLiteral("checked")).toBool());
            QCOMPARE(rules[13].toMap().value(QStringLiteral("options")).toInt(), 1);
            while (!second.misspellRules().isEmpty())
                QVERIFY(second.removeMisspellRule(0));
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes); // an empty list is not written
        file.close();
        // R4-uchardet: a file that is not UTF-8 is read in the charset uchardet
        // names (legacy OpenWrite::FileOpen and CheckCharSet), here cp1250.
        const QByteArray polish("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1. Pchn\xB9\xE6 w t\xEA \xB3\xF3"
                                "d\x9F je\xBF"
                                "a lub o\x9Cm skrzy\xF1 fig.");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("#HikariSub rules file\r\n1|1\r\n" + polish + "\fabc\fd\f0\r\n" + polish + "\fdef\fg\f0\r\n");
        file.close();
        {
            app::Application third(options);
            const auto rules = third.misspellRules();
            QCOMPARE(rules.size(), 2);
            QCOMPARE(rules[0].toMap().value(QStringLiteral("find")).toString(), QStringLiteral("abc"));
            QCOMPARE(rules[0].toMap().value(QStringLiteral("description")).toString(),
                     QString::fromUtf8("Zażółć gęślą jaźń. Pchnąć w tę łódź jeża lub ośm skrzyń fig."));
            QVERIFY(rules[0].toMap().value(QStringLiteral("checked")).toBool());
            // The Linux build keeps "\r" (R5-per-platform), so legacy read the
            // last checkbox token as "1\r" and lost that rule's state;
            // F4-rules-cr keeps it checked on both platforms.
            QVERIFY(rules[1].toMap().value(QStringLiteral("checked")).toBool());
        }
    }

    // Y9: GRID_SUBS_FROM_MKV (legacy SubsGrid::OnMkvSubs and Demux at
    // 20d647c4): enabled for a ".mkv"/".ogm" video name in any case
    // (Y9-mkv-case; legacy's SubsGrid.cpp:289 was case-sensitive); "The
    // file does not contain any subtitle tracks."; the question for a
    // modified Document; the track chooser; the loaded
    // track replaces the tab's Document, which keeps its video and is named
    // after it; Cancel and the helper's loss change nothing.
    void matroskaSubtitlesLoadIntoTheTab()
    {
        const QString subs = writeFile(dir, "mkvtab.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,before\n");
        QVERIFY(application->openFile(subs));
        auto &matroska = application->matroska();
        auto *item = named("gridSubsFromMkv");
        QVERIFY(item);
        QVERIFY(!item->property("enabled").toBool()); // no video
        const auto target = [&] { return *application->workspace().editingTarget(); };
        const auto before = target();
        const auto videoReady = [&](const QString &path) {
            return QTest::qWaitFor([&] {
                return application->video().hasVideo() &&
                       QDir::toNativeSeparators(QString::fromStdString(application->video().session().path())) ==
                           QDir::toNativeSeparators(path) &&
                       application->video().session().state() == application::VideoSession::State::Ready;
            }, 30000);
        };
        // Y9-mkv-case: an upper-case extension is offered (legacy's
        // VideoName.EndsWith(".mkv") was not). The copy of a video without
        // text tracks.
        const QString upper = dir.filePath(QStringLiteral("upper.MKV"));
        QVERIFY(QFile::copy(nativeFixture("cfr.mkv"), upper));
        application->video().openVideo(upper);
        QVERIFY(videoReady(upper));
        QTRY_VERIFY(matroska.available());
        QTRY_VERIFY(item->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        auto *noTracks = named("mkvNoTracksMessage");
        QTRY_VERIFY_WITH_TIMEOUT(noTracks->property("visible").toBool(), 20000);
        QVERIFY(QMetaObject::invokeMethod(noTracks, "accept"));
        QCOMPARE(matroska.state(), 0);
        QCOMPARE(target(), before);

        // The extraction fixture: an ASS and a SubRip track. A modified
        // Document asks first (Yes / No / Cancel).
        const QString mkv = nativeFixture("mkvextract.mkv");
        application->video().openVideo(mkv);
        QVERIFY(videoReady(mkv));
        QTRY_VERIFY(matroska.available());
        application->duplicateLines();
        QVERIFY(application->files().session(before)->isDirty());
        auto *question = named("mkvSaveQuestion");
        auto *chooser = named("mkvTrackChooser");
        QVERIFY(question && chooser);
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("mkvSaveQuestion", "mkvSaveCancel"), "click"));
        QTRY_VERIFY(!question->property("visible").toBool());
        QCOMPARE(matroska.state(), 0);
        // No: the tracks, then "Choose subtitle track"; its Cancel reads nothing.
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("mkvSaveQuestion", "mkvSaveNo"), "click"));
        QTRY_VERIFY_WITH_TIMEOUT(chooser->property("visible").toBool(), 20000);
        QCOMPARE(chooser->property("title").toString(), QStringLiteral("Choose subtitle track"));
        QCOMPARE(chooser->property("labels").toStringList(),
                 (QStringList{QStringLiteral("1 Signs (eng, ass)"), QStringLiteral("2 Full (pol, srt)")}));
        QVERIFY(QMetaObject::invokeMethod(chooser, "reject"));
        QTRY_COMPARE(matroska.state(), 0);
        QCOMPARE(target(), before);
        QVERIFY(application->files().session(before)->isDirty());

        // The helper lost while listing: nothing changes.
        QVERIFY(matroska.start());
        auto *port = dynamic_cast<backends::FfmsMatroska *>(&matroska.port());
        QVERIFY(port && port->helperHost());
        port->helperHost()->stop();
        QTRY_COMPARE(matroska.state(), 0);
        QCOMPARE(target(), before);

        // Yes saves the Document to its file first (Hikari->Save(false)); OK
        // on the first row: the ASS track replaces the tab's Document.
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("mkvSaveQuestion", "mkvSaveYes"), "click"));
        QTRY_VERIFY_WITH_TIMEOUT(chooser->property("visible").toBool(), 20000);
        QVERIFY(QMetaObject::invokeMethod(chooser, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(target() != before, 20000);
        application->waitForWrites();
        {
            QFile saved(subs);
            QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(saved.readAll().count(",before"), 2); // the duplicated Line was written
        }
        QTRY_COMPARE(matroska.state(), 0);
        auto *loaded = application->files().session(target());
        QVERIFY(loaded);
        const auto bytes = core::encodeAss(loaded->document());
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())),
                 QByteArray("[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 320\r\nPlayResY: 240\r\n"
                            "YCbCr Matrix: TV.601\r\n\r\n[V4+ Styles]\r\n"
                            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                            "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                            "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
                            "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,"
                            "0,1,2,2,2,10,10,10,1\r\n\r\n[Events]\r\n"
                            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                            "Dialogue: 0,0:00:00.20,0:00:00.60,Default,,0,0,0,,Sign one\r\n"
                            "Dialogue: 1,0:00:01.23,0:00:01.73,Default,Actor,0,0,0,,Sign two\r\n"));
        // Unsaved, with no file yet: Save asks for one, named after the video.
        QVERIFY(loaded->isDirty());
        QVERIFY(application->targetUntitled());
        QCOMPARE(QString::fromStdString(*application->workspace().title(target())), QStringLiteral("mkvextract.ass"));
        QCOMPARE(application->saveRoute(), QStringLiteral("dialog"));
        QCOMPARE(QDir::toNativeSeparators(application->saveDialogValues().value(QStringLiteral("file")).toUrl().toLocalFile()),
                 QDir::toNativeSeparators(QFileInfo(mkv).absolutePath() + QStringLiteral("/mkvextract")));
        // The tab keeps its video.
        QVERIFY(application->video().hasVideo());
        QCOMPARE(QDir::toNativeSeparators(QString::fromStdString(application->video().session().path())), mkv);
        QVERIFY(matroska.available());

        // GRID_SUBS_FROM_MKV from the Grid's hotkey path, the SubRip track:
        // an SRT Document named ".srt".
        const auto ass = target();
        auto *root = engine->rootObjects().first();
        QVariant handled;
        QVERIFY(QMetaObject::invokeMethod(root, "runGridHotkey", Q_RETURN_ARG(QVariant, handled),
                                          Q_ARG(QVariant, QStringLiteral("GRID_SUBS_FROM_MKV"))));
        QVERIFY(handled.toBool());
        QTRY_VERIFY(question->property("visible").toBool()); // the loaded Document is unsaved
        QVERIFY(QMetaObject::invokeMethod(dialogItem("mkvSaveQuestion", "mkvSaveNo"), "click"));
        QTRY_VERIFY_WITH_TIMEOUT(chooser->property("visible").toBool(), 20000);
        named("mkvTrackList")->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(chooser, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(target() != ass, 20000);
        auto *srt = application->files().session(target());
        QCOMPARE(srt->document().format(), core::SubtitleFormat::Srt);
        QCOMPARE(srt->document().lines().size(), std::size_t(1));
        QCOMPARE(srt->document().lines()[0]->text, std::u8string(u8"Pierwsza"));
        QCOMPARE(srt->document().lines()[0]->start.value.microseconds(), 500'000);
        QCOMPARE(srt->document().lines()[0]->end.value.microseconds(), 1'500'000);
        QCOMPARE(QString::fromStdString(*application->workspace().title(target())), QStringLiteral("mkvextract.srt"));
    }

    // Y9: the font collector's "Demux fonts from loaded MKV file"
    // (FontCollector.cpp:181-183, 440-541, 913-982): enabled for an MKV
    // video, kept in FONT_COLLECTOR_FROM_MKV (Y9-from-mkv-saved; legacy only
    // read it); a check-only run lists the MKV's fonts and writes nothing
    // (Y9-mkv-check-only); Start lists the font attachments, Apply writes
    // their bytes into the folder or the archive with legacy's messages.
    void fontCollectorDemuxesMkvFonts()
    {
        const QString subs = writeFile(dir, "mkvfonts.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x\n");
        QVERIFY(application->openFile(subs));
        const QString mkv = nativeFixture("mkvextract.mkv");
        application->video().openVideo(mkv);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().state() == application::VideoSession::State::Ready,
                                 30000);
        auto &collector = application->fontCollector();
        auto *dialog = named("fontCollectorDialog");
        QVERIFY(QMetaObject::invokeMethod(named("fontCollectorMenuItem"), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *fromMkv = dialogItem("fontCollectorDialog", "fontCollectorFromMkv");
        QVERIFY(fromMkv);
        QCOMPARE(fromMkv->property("text").toString(), QStringLiteral("Demux fonts from loaded MKV file"));
        // Enabled for an MKV video whatever the Options choice (at open).
        QVERIFY(dialogItem("fontCollectorDialog", "fontCollectorOption0")->property("checked").toBool());
        QVERIFY(fromMkv->property("enabled").toBool());
        QVERIFY(!fromMkv->property("checked").toBool());
        // Y9-from-mkv-saved: checking it writes FONT_COLLECTOR_FROM_MKV.
        QVERIFY(QMetaObject::invokeMethod(fromMkv, "click"));
        QVERIFY(collector.fromMkv());
        QVERIFY(application->settingsStore()->boolean("fontCollector.fromMkv"));
        // Y9-mkv-check-only: the check mode lists the MKV's fonts and writes
        // nothing (legacy ran COPY_MKV_FONTS into the last copypath, or
        // logged "Cannot create folder." without one, FontCollector.cpp:440-447).
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));
        const QString checked = collector.logText();
        QVERIFY2(checked.startsWith(QStringLiteral("Font named \"Extract Sans.ttf\".\nFont named \"Extract Serif.otf\".\n"
                                                   "\nFinished in 00:00:")),
                 qPrintable(checked));
        QVERIFY2(!checked.contains(QStringLiteral("Ready to")) && !checked.contains(QStringLiteral("Cannot create folder.")),
                 qPrintable(checked));
        QVERIFY(collector.copyPath().isEmpty());
        QVERIFY(!dialogItem("fontCollectorDialog", "fontCollectorSaveFolder")->property("enabled").toBool());
        collector.apply(true); // no review to apply
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));
        QCOMPARE(collector.logText(), checked);
        // Y9-from-mkv-saved: the box is checked again when the window reopens.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("fontCollectorMenuItem"), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(collector.fromMkv());
        QTRY_VERIFY(fromMkv->property("checked").toBool());
        // An Options change: only the copy modes keep it enabled.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorOption1"), "click"));
        QVERIFY(fromMkv->property("enabled").toBool());
        QVERIFY(fromMkv->property("checked").toBool());
        const QString folder = QDir::toNativeSeparators(dir.filePath(QStringLiteral("mkvfonts")));
        dialogItem("fontCollectorDialog", "fontCollectorPath")->setProperty("text", folder);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        const QString sep = QDir::separator();
        QCOMPARE(collector.logText(), QStringLiteral("Font named \"Extract Sans.ttf\".\nFont named \"Extract Serif.otf\".\n"
                                                     "\nReady to copy 2 fonts to \"%1\".\n")
                                          .arg(folder + sep));
        QVERIFY(!QFileInfo::exists(folder)); // nothing written before Apply
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorApply"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));
        const QString log = collector.logText();
        QVERIFY2(log.startsWith(QStringLiteral("Saved a font named \"Extract Sans.ttf\".\n \n"
                                               "Saved a font named \"Extract Serif.otf\".\n \n"
                                               "Completed successfully and copied 2 fonts.\nFinished in 00:00:")),
                 qPrintable(log));
        const auto fonts = mkvfixture::mkvExtractFonts();
        for (const auto &[file, index] : {std::pair{"Extract Sans.ttf", 0}, std::pair{"Extract Serif.otf", 1}}) {
            QFile f(QDir(folder).filePath(QLatin1String(file)));
            QVERIFY2(f.open(QIODevice::ReadOnly), file);
            QCOMPARE(f.readAll(), QByteArray::fromStdString(fonts[std::size_t(index)].data));
        }
        QVERIFY(dialogItem("fontCollectorDialog", "fontCollectorSaveFolder")->property("enabled").toBool());
        // The archive: one entry per font under its name.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorOption2"), "click"));
        const QString archive = QDir::toNativeSeparators(dir.filePath(QStringLiteral("mkvfonts.zip")));
        dialogItem("fontCollectorDialog", "fontCollectorPath")->setProperty("text", archive);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        QVERIFY(collector.logText().endsWith(QStringLiteral("Ready to add 2 fonts to the archive \"%1\".\n").arg(archive)));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorApply"), "click"));
        QVERIFY(collector.waitIdle());
        QFile zip(archive);
        QVERIFY(zip.open(QIODevice::ReadOnly));
        const QByteArray zipped = zip.readAll();
        QVERIFY(zipped.contains("Extract Sans.ttf") && zipped.contains("Extract Serif.otf"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());

        // Y9-from-mkv-saved: the kept value with a video that is not an MKV
        // file. The box shows it checked but disabled, and a disabled box
        // demuxes nothing: "Save to video / subtitles folder." writes beside
        // the subtitles, not beside the video.
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("notmkv")));
        const QString other = dir.filePath(QStringLiteral("notmkv/other.mp4"));
        QVERIFY(QFile::copy(nativeFixture("cfr.mkv"), other));
        application->video().openVideo(other);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().state() == application::VideoSession::State::Ready &&
                                     QDir::toNativeSeparators(QString::fromStdString(
                                         application->video().session().path())) == QDir::toNativeSeparators(other),
                                 30000);
        QVERIFY(QMetaObject::invokeMethod(named("fontCollectorMenuItem"), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY(fromMkv->property("checked").toBool());
        QVERIFY(!fromMkv->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorOption1"), "click"));
        QVERIFY(!fromMkv->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorSubsDirectory"), "click"));
        QVERIFY(application->settingsStore()->boolean("fontCollector.useSubsDirectory"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.copyPath(), QDir::toNativeSeparators(dir.filePath(QStringLiteral("Fonts"))) + sep);
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("notmkv/Fonts"))));
        // Unchecking writes it back.
        collector.setFromMkv(false);
        QVERIFY(!application->settingsStore()->boolean("fontCollector.fromMkv"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

    // Y8: Subtitles > Font collector (legacy FontCollectorDialog): check
    // counts families, Zip stages a review whose incomplete output is
    // written only after the acknowledgment and labelled, the log's Line
    // numbers and Styles lead to them, and the settings are kept.
    void fontCollectorChecksStagesAndWrites()
    {
        const QString path = dir.filePath(QStringLiteral("collect.ass"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, "
                    "SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, "
                    "Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                    "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
                    "Style: Sign,Nowhere,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
                    "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello\n"
                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\fnTimes}Ab\n"
                    "Dialogue: 0,0:00:05.00,0:00:06.00,Sign,,0,0,0,,x\n");
        }
        QVERIFY(application->openFile(path));
        auto fonts = std::make_unique<hikari::testing::FakeFonts>();
        fonts->faces = {{"Arial", 400, false, "/fonts/arial.ttf"}, {"Times", 400, false, "/fonts/times.ttf"}};
        // The whole Document also drew a character with a fallback file no
        // family named: its block is CheckPathAndGlyphs' "Found font \"%s\"."
        // (FontCollector.cpp:1088), which has no newline of its own.
        hikari::testing::FakeFonts::add(fonts->document, {"Gothic", 400, false, "/fonts/gothic.ttf", U"x"},
                                        application::SelectionStage::Fallback, U'x', "",
                                        application::NameMatch::None);
        auto &collector = application->fontCollector();
        collector.setFontService(std::move(fonts));
        QString revealed;
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("fontCollectorDialog"));
        QVERIFY(dialog);
        auto *menuItem = named("fontCollectorMenuItem");
        QVERIFY(menuItem && menuItem->property("enabled").toBool());
        // K1: the item and the window's title show the set's font-collector icon.
        QQmlExpression iconSource(qmlContext(menuItem), menuItem, QStringLiteral("icon.source.toString()"));
        QVERIFY(iconSource.evaluate().toString().startsWith(QLatin1String("image://hikari-icon/font-collector/")));
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *title = dialog->findChild<QQuickItem *>(QStringLiteral("fontCollectorDialogTitle"));
        QVERIFY(title);
        QCOMPARE(title->property("iconRole").toString(), QStringLiteral("font-collector"));
        // FONT_COLLECTOR_ACTION 0: the path controls are disabled.
        QVERIFY(dialogItem("fontCollectorDialog", "fontCollectorOption0")->property("checked").toBool());
        QVERIFY(!dialogItem("fontCollectorDialog", "fontCollectorPath")->property("enabled").toBool());
        QVERIFY(!dialogItem("fontCollectorDialog", "fontCollectorSaveFolder")->property("enabled").toBool());

        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        const QString check = collector.logText();
        QVERIFY2(check.startsWith(QStringLiteral("Found font \"Arial\"\nIn styles:\n - Default tabs: 1\n\n"
                                                 "Found font \"Times\"\nIn lines: 2\n\n"
                                                 "Font not found \"Nowhere\".\nIn styles:\n - Sign tabs: 1\n\n")),
                 qPrintable(check));
        QVERIFY2(check.contains(QStringLiteral("\nFinished, found 2 fonts.\nNot found 1 font.\n")), qPrintable(check));
        QVERIFY(check.contains(QStringLiteral("\nFinished in 00:00:")));
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));

        // A double click in the log (OnConsoleDoubleClick, FontCollector.cpp:269-294)
        // on a Line number goes to it, on a Style's tab to its first Line.
        // The click lands on the rich-text log at the character's caret
        // position, which is the offset in logText (legacy HitTest,
        // HikariTextCtrl.cpp:1312-1358: the nearest caret position).
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto lines = session->document().lines();
        auto *logView = dialogItem("fontCollectorDialog", "fontCollectorLog");
        QVERIFY(logView);
        const auto pointAt = [&](int offset) {
            QRectF r;
            QMetaObject::invokeMethod(logView, "positionToRectangle", Q_RETURN_ARG(QRectF, r), Q_ARG(int, offset));
            const QPointF local(r.x() + 1.5, r.center().y());
            int hit = -1;
            QMetaObject::invokeMethod(logView, "positionAt", Q_RETURN_ARG(int, hit), Q_ARG(qreal, local.x()),
                                      Q_ARG(qreal, local.y()));
            return std::pair{logView->mapToScene(local).toPoint(), hit};
        };
        for (const QString &at : {QStringLiteral("In lines: 2"), QStringLiteral(" - Sign tabs: 1"),
                                  QStringLiteral("Font not found")}) {
            const int offset = int(check.indexOf(at));
            QCOMPARE(pointAt(offset).second, offset);
        }
        const auto lineNumber = pointAt(int(check.indexOf(QStringLiteral("In lines: 2")) + 10));
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, lineNumber.first);
        QTRY_COMPARE(session->selection().active, std::optional(lines[1]->id));
        QSignalSpy styleRequested(&collector, &app::FontCollectorController::styleRequested);
        const auto styleTab = pointAt(int(check.indexOf(QStringLiteral(" - Sign tabs: 1")) + 14));
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, styleTab.first);
        QTRY_COMPARE(session->selection().active, std::optional(lines[2]->id));
        QCOMPARE(styleRequested.size(), 1);
        QCOMPARE(styleRequested.at(0).at(0).toString(), QStringLiteral("Sign"));
        QTRY_VERIFY(named("styleManager")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("styleManager"), "closeManager"));

        // Copy to selected folder without a folder: legacy's message.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorOption1"), "click"));
        QCOMPARE(application->settingsStore()->integer("fontCollector.action"), 1);
        QVERIFY(dialogItem("fontCollectorDialog", "fontCollectorPath")->property("enabled").toBool());
        dialogItem("fontCollectorDialog", "fontCollectorPath")->setProperty("text", QString());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        auto *message = root->findChild<QObject *>(QStringLiteral("fontCollectorMessage"));
        QTRY_VERIFY(message->property("visible").toBool());
        QCOMPARE(message->property("text").toString(), QStringLiteral("Select the folder where you want to copy fonts"));
        QVERIFY(QMetaObject::invokeMethod(message, "accept"));

        // FC-slash-linux: the path field takes '/' typed on Linux; on Windows
        // it excludes / * ? " < > | as legacy's HikariTextValidator did.
        auto *pathField = dialogItem("fontCollectorDialog", "fontCollectorPath");
        QTRY_VERIFY(!message->property("visible").toBool());
        pathField->forceActiveFocus();
        QTRY_VERIFY(pathField->hasActiveFocus());
        for (const char key : {'a', '/', 'b', '*', 'c'})
            QTest::keyClick(window, key);
#ifdef _WIN32
        QCOMPARE(pathField->property("text").toString(), QStringLiteral("abc"));
#else
        QCOMPARE(pathField->property("text").toString(), QStringLiteral("a/bc"));
#endif
        // FC-chooser-cancel: a cancelled folder or archive chooser keeps the
        // previous path (legacy stored the empty answer, FontCollector.cpp:
        // 418-433).
        const QString kept = QDir::toNativeSeparators(dir.filePath(QStringLiteral("kept")));
        collector.chooseDirectory(kept);
        pathField->setProperty("text", kept);
        for (const char *chooser : {"fontCollectorFolderDialog", "fontCollectorArchiveDialog"}) {
            auto *chooserDialog = root->findChild<QObject *>(QLatin1String(chooser));
            QVERIFY2(chooserDialog, chooser);
            QVERIFY(QMetaObject::invokeMethod(chooserDialog, "rejected"));
            QCOMPARE(pathField->property("text").toString(), kept);
            QCOMPARE(application->settingsStore()->text("fontCollector.directory"), kept);
        }
        collector.chooseDirectory(QString());
        QCOMPARE(application->settingsStore()->text("fontCollector.directory"), kept);

        // Zip: ".zip" is added; the review is incomplete (a font is not
        // found), so it writes only after the acknowledgment.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorOption2"), "click"));
        const QString archive = dir.filePath(QStringLiteral("collected"));
        dialogItem("fontCollectorDialog", "fontCollectorPath")->setProperty("text", QDir::toNativeSeparators(archive));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        QVERIFY(collector.reviewIncomplete());
        QCOMPARE(collector.copyPath(), QDir::toNativeSeparators(archive) + QStringLiteral(".zip"));
        const QString review = collector.logText();
        QVERIFY2(review.startsWith(QStringLiteral("Retrieved sizes and names of 2 fonts, elapsed time 00:00:")), qPrintable(review));
        // GetAssFonts' header ends its line (FontCollector.cpp:602); the
        // renderer's own file is in a block CheckPathAndGlyphs opened, whose
        // header does not (FontCollector.cpp:1088, FontLogContent::DoLog).
        QVERIFY2(review.contains(QStringLiteral("Found font \"Arial\"\nFound \"/fonts/arial.ttf\" font file.\nIn styles:\n")),
                 qPrintable(review));
        QVERIFY2(review.contains(QStringLiteral("Found font \"gothic.ttf\".The renderer also used \"/fonts/gothic.ttf\" "
                                                "(fallback); it is not collected.\n\n")),
                 qPrintable(review));
        QVERIFY(review.contains(QStringLiteral("Ready to add 2 fonts to the archive")));
        QVERIFY(!QFile::exists(archive + QStringLiteral(".zip")));
        auto *apply = dialogItem("fontCollectorDialog", "fontCollectorApply");
        QVERIFY(apply->property("visible").toBool());
        QVERIFY(!apply->property("enabled").toBool());
        auto *acknowledge = dialogItem("fontCollectorDialog", "fontCollectorAcknowledge");
        QVERIFY(acknowledge->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(acknowledge, "click"));
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QVERIFY(collector.waitIdle());
        QVERIFY(QFile::exists(archive + QStringLiteral(".zip")));
        const QString done = collector.logText();
        QVERIFY2(done.contains(QStringLiteral("Added font \"arial.ttf\" to the archive.\n")), qPrintable(done));
        QVERIFY2(done.contains(QStringLiteral("\nFinished, copied 2 fonts.\nNot found 1 font.\n")), qPrintable(done));
        QVERIFY(done.contains(QStringLiteral("The output is labelled incomplete")));
        QVERIFY(!done.contains(QStringLiteral("Completed Successfully")));
        QVERIFY(dialogItem("fontCollectorDialog", "fontCollectorSaveFolder")->property("enabled").toBool());
        QCOMPARE(application->settingsStore()->text("fontCollector.directory"), QDir::toNativeSeparators(archive));

        // The archive now exists: Start asks "The zip file already exists,
        // delete it?" (FontCollector.cpp:513-519). Legacy removed it on Yes
        // and wrote at once; with the staged review the Yes is carried to
        // Apply, so a review closed or refused keeps the user's archive.
        const QString zipPath = archive + QStringLiteral(".zip");
        {
            QFile f(zipPath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("previous archive");
        }
        auto *replaceQuestion = root->findChild<QObject *>(QStringLiteral("fontCollectorReplaceQuestion"));
        QVERIFY(replaceQuestion);
        const auto readZip = [&] {
            QFile f(zipPath);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QTRY_VERIFY(replaceQuestion->property("visible").toBool());
        QCOMPARE(replaceQuestion->property("text").toString(), QStringLiteral("The zip file already exists, delete it?"));
        QCOMPARE(replaceQuestion->property("title").toString(), QStringLiteral("Confirmation"));
        QVERIFY(QMetaObject::invokeMethod(replaceQuestion, "accept"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        QCOMPARE(readZip(), QByteArray("previous archive"));
        collector.close();
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Options));
        QCOMPARE(readZip(), QByteArray("previous archive"));
        // Yes again; the incomplete review is not acknowledged, so Apply is
        // refused and the archive stays.
        QTRY_VERIFY(!replaceQuestion->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QTRY_VERIFY(replaceQuestion->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(replaceQuestion, "accept"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        collector.apply(false);
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        QCOMPARE(readZip(), QByteArray("previous archive"));
        // Apply with the acknowledgment removes it and writes the new one.
        collector.apply(true);
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));
        QVERIFY(readZip().startsWith(QByteArray("PK\x03\x04", 4)));
        QTRY_VERIFY(!replaceQuestion->property("visible").toBool());

        // "Save to video / subtitles folder.": a folder beside the subtitles
        // named in the interface language, "Fonts" in English (FC-czcionki;
        // legacy always wrote "Czcionki", FontCollector.cpp:482).
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorOption1"), "click"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorSubsDirectory"), "click"));
        QVERIFY(application->settingsStore()->boolean("fontCollector.useSubsDirectory"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.copyPath(), QDir::toNativeSeparators(dir.filePath(QStringLiteral("Fonts"))) + QDir::separator());
        collector.apply(true);
        QVERIFY(collector.waitIdle());
        QCOMPARE(QDir(dir.filePath(QStringLiteral("Fonts"))).entryList(QDir::Files, QDir::Name),
                 (QStringList{QStringLiteral("INCOMPLETE - font collection.txt"), QStringLiteral("arial.ttf"),
                              QStringLiteral("times.ttf")}));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Czcionki"))));
        {
            // The name goes through the translation system: a Polish
            // interface names it "Czcionki".
            class Polish final : public QTranslator {
            public:
                bool isEmpty() const override { return false; }
                QString translate(const char *context, const char *source, const char *, int) const override
                {
                    return QByteArray(context) == "hikari::app::FontCollectorController" && QByteArray(source) == "Fonts"
                               ? QStringLiteral("Czcionki")
                               : QString();
                }
            } polish;
            QVERIFY(QCoreApplication::installTranslator(&polish));
            QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
            QVERIFY(collector.waitIdle());
            QCoreApplication::removeTranslator(&polish);
            QCOMPARE(collector.copyPath(),
                     QDir::toNativeSeparators(dir.filePath(QStringLiteral("Czcionki"))) + QDir::separator());
        }

        // Closing while Apply still runs past the wait: the late result
        // still finds its review (no read of a dropped one) and ends the job.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorStart"), "click"));
        QVERIFY(collector.waitIdle());
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Review));
        collector.setCloseWait(-1);
        collector.apply(true);
        collector.close();
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Working));
        QVERIFY(collector.waitIdle());
        collector.setCloseWait(30000);
        QCOMPARE(collector.stage(), int(app::FontCollectorController::Done));
        QVERIFY(!collector.reviewIncomplete());
        const QString late = collector.logText();
        QVERIFY2(late.startsWith(QStringLiteral("Found font \"Arial\"\nFound \"/fonts/arial.ttf\" font file.\n")),
                 qPrintable(late));
        QVERIFY2(late.contains(QStringLiteral("\nFinished in 00:00:")), qPrintable(late));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("fontCollectorDialog", "fontCollectorClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        application->settingsStore()->set("fontCollector.useSubsDirectory", false);
        application->settingsStore()->set("fontCollector.action", 0);
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

private:
    // F3: a session with a Dictionary folder beside its settings and the
    // production Hunspell backend (R2-hunspell), or no backend at all.
    void restartWithSpelling(const QString &home, bool backend, const QString &bundled = {})
    {
        delete engine;
        delete application;
        app::Application::Options options;
        options.settingsFile = home + QStringLiteral("/hikari.ini");
        if (!backend)
            options.spellingBackend = {};
        options.bundledDictionaryDir = bundled;
        application = new app::Application(options);
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
    // A tiny Hunspell dictionary (no affix rules, ISO8859-1 by default).
    static void writeDictionary(const QString &home, const QString &folder = QStringLiteral("Dictionary"))
    {
        QDir().mkpath(home + QLatin1Char('/') + folder);
        QFile aff(home + QLatin1Char('/') + folder + QStringLiteral("/en_US.aff"));
        QVERIFY(aff.open(QIODevice::WriteOnly));
        QFile dic(home + QLatin1Char('/') + folder + QStringLiteral("/en_US.dic"));
        QVERIFY(dic.open(QIODevice::WriteOnly));
        dic.write("5\nHello\nworld\nThe\ntext\ngood\n");
    }
    // A Menu's items in order (Menu.itemAt).
    static QList<QQuickItem *> menuItems(QObject *menu)
    {
        QList<QQuickItem *> out;
        const int count = menu->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *entry = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, i));
            out << entry;
        }
        return out;
    }
    static bool hasSubMenu(QObject *menu, const QString &title)
    {
        for (QQuickItem *entry : menuItems(menu)) {
            auto *sub = entry ? entry->property("subMenu").value<QObject *>() : nullptr;
            if (sub && sub->property("title").toString() == title)
                return true;
        }
        return false;
    }
    // A right click. Offscreen, a text field's context menu request carries
    // its caret's position rather than the click's, so a left click puts the
    // caret there first.
    void rightClick(const QPoint &point)
    {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    }
    // The scene point of a character of an editor field.
    QPoint fieldPoint(QQuickItem *field, int position) const
    {
        QRectF rect;
        QMetaObject::invokeMethod(field, "positionToRectangle", Q_RETURN_ARG(QRectF, rect), Q_ARG(int, position));
        return field->mapToScene(QPointF(rect.x() + 2, rect.center().y())).toPoint();
    }
    QVariantList appliedMarks(const char *field) const
    {
        QVariantList applied;
        if (auto *marks = item<QObject>(field))
            QMetaObject::invokeMethod(marks, "appliedRanges", Q_RETURN_ARG(QVariantList, applied));
        return applied;
    }

    // A session reading its settings from `file`, without a spelling
    // backend (no dictionary notice beside it).
    void restartWithSettings(const QString &file)
    {
        delete engine;
        delete application;
        app::Application::Options options;
        options.settingsFile = file;
        options.spellingBackend = {};
        application = new app::Application(options);
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

    // A4: a session whose players make no sound (playbackAudio off): the
    // audio box plays through the output without a device, at its pace.
    void restartWithoutSound()
    {
        app::Application::Options options;
        options.playbackAudio = false;
        restartWith(options);
    }
    void restartWith(const app::Application::Options &options)
    {
        delete engine;
        delete application;
        application = new app::Application(options);
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

    // V3: a session whose media helper is another program (no sound).
    void restartWithMediaHelper(const QString &helper)
    {
        delete engine;
        delete application;
        app::Application::Options options;
        options.playbackAudio = false;
        options.mediaHelper = helper;
        application = new app::Application(options);
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

private slots:
    // F3: the Grid's and the editor's marks, Subtitles > Check spelling
    // (Replace as one step, then the next word on another Line, then "No
    // spelling errors were found"), a suggestion from the editor and Add word.
    void spellCheckerWindowAndEditorMarks()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        restartWithSpelling(home.path(), true);
        const QString path = writeFile(dir, "spelling.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Teh {\\i1}text\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,good}\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *grid = application->shell().lines();
        QCOMPARE(grid->index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(), (QVariantList{6, 10}));
        QCOMPARE(grid->index(1, 0).data(ui::LineTableModel::SpellMarksRole).toList(), (QVariantList{0, 2}));
        QCOMPARE(grid->index(2, 0).data(ui::LineTableModel::SpellMarksRole).toList(), (QVariantList{4, 4}));
        // The editor shows Line 1 with the misspelling marked in its text.
        QTRY_COMPARE(appliedMarks("lineTextSpellMarks"), (QVariantList{6, 11}));

        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("spellCheckerDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("checkSpellingMenuItem")), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialogItem("spellCheckerDialog", "spellMisspell")->property("text").toString(), QStringLiteral("wrold"));
        QCOMPARE(dialogItem("spellCheckerDialog", "spellReplacement")->property("text").toString(), QStringLiteral("world"));
        QCOMPARE(application->editor().selectionStart(), 6);
        QCOMPARE(application->editor().selectionEnd(), 11);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("spellCheckerDialog", "spellReplace"), "click"));
        QCOMPARE(session->history().back().name, std::string("Correcting spelling errors"));
        auto text = [&](std::size_t row) {
            const auto &t = session->document().lines()[row]->text;
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        QCOMPARE(text(0), QStringLiteral("Hello world"));
        // The next word is on the second Line, which becomes active and shows it selected.
        QCOMPARE(dialogItem("spellCheckerDialog", "spellMisspell")->property("text").toString(), QStringLiteral("Teh"));
        QCOMPARE(session->selection().active, std::optional(session->document().lines()[1]->id));
        QCOMPARE(application->editor().text(), QStringLiteral("Teh text"));
        QCOMPARE(application->editor().selectionStart(), 0);
        QCOMPARE(application->editor().selectionEnd(), 3);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("spellCheckerDialog", "spellIgnore"), "click"));
        auto *message = root->findChild<QObject *>(QStringLiteral("spellMessage"));
        QTRY_VERIFY(message->property("visible").toBool());
        QCOMPARE(message->property("text").toString(), QStringLiteral("No spelling errors were found"));
        QVERIFY(QMetaObject::invokeMethod(message, "accept"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("spellCheckerDialog", "spellClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());

        // The editor (tags hidden): "Teh" marked in the shown text; a
        // suggestion replaces it as one step.
        QTRY_COMPARE(appliedMarks("lineTextSpellMarks"), (QVariantList{0, 3}));
        const QVariantMap found = application->editorMisspellAt(0, 1);
        QCOMPARE(found.value(QStringLiteral("word")).toString(), QStringLiteral("Teh"));
        QCOMPARE(found.value(QStringLiteral("suggestions")).toStringList().first(), QStringLiteral("The"));
        QVERIFY(application->replaceEditorMisspell(0, 1, QStringLiteral("The")));
        QCOMPARE(session->history().back().name, std::string("Correcting spelling errors in the text field"));
        QCOMPARE(text(1), QStringLiteral("The {\\i1}text"));
        QTRY_COMPARE(appliedMarks("lineTextSpellMarks"), QVariantList{});
        // Add word: the user dictionary beside the dictionaries.
        QVERIFY(!application->addEditorWord(QStringLiteral("42")));
        QVERIFY(application->addEditorWord(QStringLiteral("Hikari")));
        QFile user(home.filePath(QStringLiteral("Dictionary/UserDic.udic")));
        QVERIFY(user.open(QIODevice::ReadOnly));
        QCOMPARE(user.readAll(), QByteArray("\xEF\xBB\xBFHikari"));
        QCOMPARE(application->addedDictionaryWords(), QStringList{QStringLiteral("Hikari")});
        // Spelling off: no editor marks; the Grid keeps bracket errors only.
        application->setSpellingOn(false);
        QTRY_COMPARE(appliedMarks("lineTextSpellMarks"), QVariantList{});
        QCOMPARE(application->shell().lines()->index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(),
                 QVariantList{});
        QCOMPARE(application->shell().lines()->index(2, 0).data(ui::LineTableModel::SpellMarksRole).toList(),
                 (QVariantList{4, 4}));
        QCOMPARE(QSettings(home.filePath(QStringLiteral("hikari.ini")), QSettings::IniFormat)
                     .value(QStringLiteral("profile/editor.spellchecker")).toBool(),
                 false);
        application->editor().discard();
    }

    // F3: legacy SpellChecker::Get starts the checker once: with spelling off
    // at that time, turning it on checks nothing until the language is chosen
    // again; without dictionaries spelling turns itself off and says so.
    void spellingStartsOnceAndReportsMissingDictionaries()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        QSettings(home.filePath(QStringLiteral("hikari.ini")), QSettings::IniFormat).setValue(QStringLiteral("profile/editor.spellchecker"), false);
        restartWithSpelling(home.path(), true);
        const QString path = writeFile(dir, "spelling-off.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold\n");
        QVERIFY(application->openFile(path));
        auto marks = [&] { return application->shell().lines()->index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(); };
        QCOMPARE(marks(), QVariantList{});
        application->setSpellingOn(true);
        QCOMPARE(marks(), QVariantList{});
        QCOMPARE(application->dictionaries(),
                 (QVariantList{QVariantMap{{QStringLiteral("symbol"), QStringLiteral("en_US")},
                                           {QStringLiteral("name"), QStringLiteral("English")}}}));
        application->setDictionaryLanguage(QStringLiteral("en_US"));
        QCOMPARE(marks(), (QVariantList{0, 4}));

        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        restartWithSpelling(empty.path(), true);
        QVERIFY(application->openFile(path));
        auto *notice = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("spellingNotice"));
        QTRY_VERIFY(notice->property("visible").toBool());
        QCOMPARE(notice->property("text").toString(),
                 QStringLiteral("No dictionary files were found in the \"%1\" folder.\nSpell checking will be disabled")
                     .arg(QDir::toNativeSeparators(QDir(empty.path()).absoluteFilePath(QStringLiteral("Dictionary")))));
#ifndef _WIN32
        QVERIFY(!notice->property("text").toString().contains(QLatin1Char('\\'))); // native separators only
#endif
        QVERIFY(!application->spellingOn());
        QVERIFY(QMetaObject::invokeMethod(notice, "accept"));
    }

    // F3: without a spelling backend there is no spell checker: no notice,
    // no SPELLCHECKER_ON change, bracket marks only. The bundled Dictionary
    // folder (legacy's, beside the executable) is read after the user's.
    void spellingWithoutBackendAndBundledDictionaries()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        restartWithSpelling(home.path(), false);
        const QString path = writeFile(dir, "spelling-none.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold}\n");
        QVERIFY(application->openFile(path));
        auto marks = [&] { return application->shell().lines()->index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(); };
        QCOMPARE(marks(), (QVariantList{5, 5}));
        QTest::qWait(50);
        QVERIFY(!engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("spellingNotice"))->property("visible").toBool());
        QVERIFY(application->spellingOn());
        QVERIFY(!QSettings(home.filePath(QStringLiteral("hikari.ini")), QSettings::IniFormat).contains(QStringLiteral("profile/editor.spellchecker")));
        QVERIFY(application->dictionaries().isEmpty());

        // Only the bundled folder has a dictionary: it loads, and the user's
        // words go to the user's folder.
        QTemporaryDir user, bundled;
        QVERIFY(user.isValid() && bundled.isValid());
        writeDictionary(bundled.path());
        restartWithSpelling(user.path(), true, bundled.filePath(QStringLiteral("Dictionary")));
        QVERIFY(application->openFile(path));
        QCOMPARE(marks(), (QVariantList{5, 5, 0, 4}));
        QCOMPARE(application->dictionaries().size(), 1);
        QVERIFY(application->addEditorWord(QStringLiteral("wrold")));
        QVERIFY(QFile::exists(user.filePath(QStringLiteral("Dictionary/UserDic.udic"))));
        QVERIFY(!QFile::exists(bundled.filePath(QStringLiteral("Dictionary/UserDic.udic"))));
        QCOMPARE(marks(), (QVariantList{5, 5}));
    }

    // F3: the window's actions after the editor's draft committed (commit on
    // leave): the walk starts again instead of acting on the shown word,
    // also when the press on Replace is what brings the window back; Replace
    // with nothing to replace leaves the window as it is (legacy returns).
    void spellCheckerActionsAfterEditorTyping()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        restartWithSpelling(home.path(), true);
        const QString path = writeFile(dir, "spelling-typing.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,good\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("spellCheckerDialog"));
        // Legacy order: Resample subtitles, then Check spelling.
        auto *check = root->findChild<QObject *>(QStringLiteral("checkSpellingMenuItem"));
        auto *subtitles = check->property("menu").value<QObject *>();
        QVERIFY(subtitles);
        const auto entries = menuItems(subtitles);
        const auto at = [&](const char *name) {
            for (qsizetype i = 0; i < entries.size(); ++i)
                if (entries[i] && entries[i]->objectName() == QLatin1String(name))
                    return i;
            return qsizetype(-1);
        };
        QCOMPARE(at("checkSpellingMenuItem"), at("resampleMenuItem") + 1);

        QVERIFY(QMetaObject::invokeMethod(check, "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *misspell = dialogItem("spellCheckerDialog", "spellMisspell");
        auto *replacement = dialogItem("spellCheckerDialog", "spellReplacement");
        QCOMPARE(misspell->property("text").toString(), QStringLiteral("wrold"));
        const auto steps = session->historySize();

        // Back in the editor, typing in the found word's Line; then a press
        // on Replace brings the window back. The press does not swap the
        // word; the click commits the draft and starts again from its Line.
        auto *field = item("lineText");
        field->forceActiveFocus();
        application->editor().textEdited(QStringLiteral("Helo wrold"), 4);
        auto *replace = dialogItem("spellCheckerDialog", "spellReplace");
        const QPoint centre = replace->mapToScene(QPointF(replace->width() / 2, replace->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, centre);
        QCoreApplication::processEvents();
        QTRY_VERIFY(dialog->property("activeFocus").toBool());
        QTest::qWait(20);
        QCOMPARE(misspell->property("text").toString(), QStringLiteral("wrold"));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, centre);
        QTRY_COMPARE(misspell->property("text").toString(), QStringLiteral("Helo"));
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Edit Line"));
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->document().lines()[0]->text.data()),
                                   qsizetype(session->document().lines()[0]->text.size())),
                 QStringLiteral("Helo wrold"));
        // The same through the Application call (no press involved).
        application->editor().textEdited(QStringLiteral("Helo wrold xyzzy"), 4);
        QVariantMap state = application->spellCheckerReplace(QStringLiteral("Hello"), {});
        QVERIFY(state.value(QStringLiteral("restarted")).toBool());
        QCOMPARE(state.value(QStringLiteral("word")).toString(), QStringLiteral("Helo"));
        QCOMPARE(session->history().back().name, std::string("Edit Line"));

        // Replace with an empty field: nothing happens, the window keeps its word.
        replacement->setProperty("text", QString());
        QVERIFY(QMetaObject::invokeMethod(replace, "click"));
        QCOMPARE(misspell->property("text").toString(), QStringLiteral("Helo"));
        QVERIFY(!root->findChild<QObject *>(QStringLiteral("spellMessage"))->property("visible").toBool());
        QVERIFY(application->spellCheckerReplace(QString(), {}).value(QStringLiteral("unchanged")).toBool());
        replacement->setProperty("text", QStringLiteral("Hello"));
        QVERIFY(QMetaObject::invokeMethod(replace, "click"));
        QCOMPARE(session->history().back().name, std::string("Correcting spelling errors"));
        QCOMPARE(misspell->property("text").toString(), QStringLiteral("wrold"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("spellCheckerDialog", "spellClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        application->editor().discard();
    }

    // F3: legacy's other Replace path, a double click on a suggestion
    // (ID_SUGGESTIONS_LIST), after typing in the editor. The press that
    // brings the window back does not swap the list under the pointer, so the
    // double click lands on the suggestion the user saw; that Replace finds
    // its word stale (the draft committed) and starts the walk again instead
    // of acting, as the Replace button does.
    void spellCheckerSuggestionDoubleClickAfterEditorTyping()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        restartWithSpelling(home.path(), true);
        const QString path = writeFile(dir, "spelling-suggestion.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("spellCheckerDialog"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("checkSpellingMenuItem")), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *misspell = dialogItem("spellCheckerDialog", "spellMisspell");
        auto *list = dialogItem("spellCheckerDialog", "spellSuggestions");
        QCOMPARE(misspell->property("text").toString(), QStringLiteral("wrold"));
        const auto suggestion = [&](const QString &text) -> QQuickItem * {
            auto *content = list->property("contentItem").value<QQuickItem *>();
            for (QQuickItem *delegate : content->childItems())
                if (delegate->property("text").toString() == text)
                    return delegate;
            return nullptr;
        };
        QTRY_VERIFY(suggestion(QStringLiteral("world")));
        auto *world = suggestion(QStringLiteral("world"));
        const QPoint at = world->mapToScene(QPointF(world->width() / 2, world->height() / 2)).toPoint();

        auto *field = item("lineText");
        field->forceActiveFocus();
        application->editor().textEdited(QStringLiteral("Helo wrold"), 4);
        // The first press of the double click brings the window back.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        QCoreApplication::processEvents();
        QTRY_VERIFY(dialog->property("activeFocus").toBool());
        QTest::qWait(20);
        QCOMPARE(misspell->property("text").toString(), QStringLiteral("wrold"));
        QCOMPARE(suggestion(QStringLiteral("world")), world);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
        QCoreApplication::processEvents();
        QCOMPARE(dialogItem("spellCheckerDialog", "spellReplacement")->property("text").toString(), QStringLiteral("world"));
        // The second press and its double click (Qt sends both).
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(at), window->mapToGlobal(QPointF(at)), Qt::LeftButton,
                                Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &doubleClick);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_COMPARE(misspell->property("text").toString(), QStringLiteral("Helo"));
        const auto text = [&] {
            const auto &raw = session->document().lines()[0]->text;
            return QString::fromUtf8(reinterpret_cast<const char *>(raw.data()), qsizetype(raw.size()));
        };
        QCOMPARE(text(), QStringLiteral("Helo wrold"));
        QCOMPARE(session->history().back().name, std::string("Edit Line"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("spellCheckerDialog", "spellClose"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        application->editor().discard();
    }

    // F3: the editor's spelling menu and the double-click suggestion list.
    // The suggestions come first; "Spellchecker" and "Installed languages"
    // are only in the spell-checked field (the Translated one in translation
    // mode). A double click on a misspelling with
    // EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK lists its suggestions without
    // selecting the word.
    void editorSpellingMenuAndDoubleClick()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        restartWithSpelling(home.path(), true);
        const QString path = writeFile(dir, "spelling-menu.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold Teh\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Hello wrold Teh"));
        auto *menu = field->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        QVERIFY(menu);
        rightClick(fieldPoint(field, 7));
        QTRY_VERIFY(menu->property("visible").toBool());
        auto entries = menuItems(menu);
        QVERIFY(!entries.isEmpty());
        QCOMPARE(entries.first()->property("text").toString(), QStringLiteral("world"));
        QVERIFY(hasSubMenu(menu, QStringLiteral("Installed languages")));
        auto *spellingOn = field->findChild<QObject *>(QStringLiteral("lineTextSpellingOn"));
        QVERIFY(spellingOn && spellingOn->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(entries.first(), "triggered"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Hello world Teh"));
        QCOMPARE(session->history().back().name, std::string("Correcting spelling errors in the text field"));

        // Double click on "Teh" with the option on: the list, no selection.
        application->setSuggestionsOnDoubleClick(true);
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, fieldPoint(field, 13));
        auto *fix = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("fixSuggestions"));
        QTRY_VERIFY(fix->property("visible").toBool());
        QCOMPARE(fix->property("suggestions").toStringList().first(), QStringLiteral("The"));
        QCOMPARE(field->property("selectedText").toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(fix, "accept"));
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Hello world The"));
        QTRY_VERIFY(!fix->property("visible").toBool());
        application->setSuggestionsOnDoubleClick(false);

        // Translation mode: the Original field has neither entry.
        const QString tl = writeFile(dir, "spelling-tl.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold\n");
        QFile f(tl);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray body = f.readAll();
        f.close();
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[Script Info]\nTLMode: Yes\n\n" + body);
        f.close();
        restartWithSpelling(home.path(), true);
        QVERIFY(application->openFile(tl));
        field = item("lineText");
        menu = field->findChild<QObject *>(QStringLiteral("lineTextMenu"));
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("wrold"));
        // D3: the Line editor grows to its translation-mode minimum (its
        // rows laid out) before the field is clicked where it ends up.
        QTRY_VERIFY(item("translationText")->y() > field->y());
        QTRY_VERIFY(item("editorPanel")->height() >= item("editorPanel")->property("minimumSize").toSizeF().height());
        rightClick(fieldPoint(field, 1));
        QTRY_VERIFY(menu->property("visible").toBool());
        QVERIFY(!hasSubMenu(menu, QStringLiteral("Installed languages")));
        QVERIFY(!field->findChild<QObject *>(QStringLiteral("lineTextSpellingOn"))->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        auto *translated = item("translationText");
        auto *tlMenu = translated->findChild<QObject *>(QStringLiteral("translationTextMenu"));
        QVERIFY(tlMenu);
        rightClick(translated->mapToScene(QPointF(10, 10)).toPoint());
        QTRY_VERIFY(tlMenu->property("visible").toBool());
        QVERIFY(hasSubMenu(tlMenu, QStringLiteral("Installed languages")));
        QVERIFY(QMetaObject::invokeMethod(tlMenu, "close"));
        application->editor().discard();
    }

    // F1: Ctrl+H / Ctrl+F open the persistent Search tool (scope rail left,
    // results right): Replace all as one step, Find with the editor's
    // selection, F3, a question box that does not block and that F3 cannot
    // re-enter, the results and Replace checked, and the toggle that hides it.
    void searchToolReplacesFindsAndListsResults()
    {
        const QString path = writeFile(dir, "find.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Alpha beta\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,beta\n"
                                       "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,gamma beta\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *dock = item<QObject>("searchDock");
        QVERIFY(dock);
        QVERIFY(!dock->property("isOpen").toBool()); // tools start closed
        QStringList questions;
        application->setFindQuestionHandler([&](int, const QString &text) {
            questions << text;
            return 2; // No
        });
        // Ctrl+H from the Grid: the tool on "Find and replace", focused.
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_H, Qt::ControlModifier);
        QTRY_VERIFY(dock->property("isOpen").toBool());
        QTRY_VERIFY(item("searchPanel")->hasActiveFocus());
        auto *tool = item<QObject>("searchTool");
        QVERIFY(tool);
        QCOMPARE(tool->property("tab").toInt(), 1);
        // Scope rail on the left, results and change review on the right.
        const auto x = [&](const char *name) { return item(name)->mapToScene(QPointF(0, 0)).x(); };
        QVERIFY(x("findText") < x("findResultsPane"));
        QVERIFY(x("replaceAllButton") < x("findResultsPane"));
        // No results yet: the review footer waits for them.
        QVERIFY(item("findResultsEmpty")->isVisible());
        QVERIFY(!item("replaceCheckedButton")->isVisible());
        item("findText")->setProperty("editText", QStringLiteral("BETA"));
        item("findReplaceText")->setProperty("editText", QStringLiteral("B"));
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(item("replaceAllButton"), "click"));
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Replace all"));
        QCOMPARE(questions.back(), QStringLiteral("Replaced 3 times."));
        const auto text = [&](std::size_t row) {
            const auto &t = session->document().lines()[row]->text;
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        QCOMPARE(text(0), QStringLiteral("Alpha B"));
        QCOMPARE(text(2), QStringLiteral("gamma B"));
        application->editor().undo();
        QCOMPARE(text(0), QStringLiteral("Alpha beta"));
        QCOMPARE(application->findReplaceSettings(1).value(QStringLiteral("finds")).toStringList(),
                 QStringList{QStringLiteral("BETA")});
        // Ctrl+F in the tool switches to the Find tab (legacy SetValues: the
        // first recent search).
        press(Qt::Key_F, Qt::ControlModifier);
        QTRY_COMPARE(tool->property("tab").toInt(), 0);
        QCOMPARE(item("findText")->property("editText").toString(), QStringLiteral("BETA"));
        item("findText")->setProperty("editText", QStringLiteral("zzz"));
        // Ctrl+F from the Line editor with "beta" selected: the tool takes the
        // selection as it gains the focus (legacy OnActivate).
        auto *lineText = item("lineText");
        lineText->forceActiveFocus();
        QTRY_COMPARE(lineText->property("text").toString(), QStringLiteral("Alpha beta"));
        QVERIFY(QMetaObject::invokeMethod(lineText, "select", Q_ARG(int, 6), Q_ARG(int, 10)));
        press(Qt::Key_F, Qt::ControlModifier);
        QTRY_COMPARE(tool->property("tab").toInt(), 0);
        QTRY_VERIFY(item("searchPanel")->hasActiveFocus());
        QCOMPARE(item("findText")->property("editText").toString(), QStringLiteral("beta"));
        QVERIFY(QMetaObject::invokeMethod(item("findButton"), "click"));
        const auto lines = session->document().lines();
        QCOMPARE(session->selection().active, std::optional(lines[0]->id));
        QCOMPARE(session->selection().selected, (std::set<core::LineId>{lines[0]->id}));
        QCOMPARE(application->editor().selectionStart(), 6);
        QCOMPARE(application->editor().selectionEnd(), 10);
        // F3 goes on.
        press(Qt::Key_F3);
        QCOMPARE(session->selection().active, std::optional(lines[1]->id));
        press(Qt::Key_F3);
        QCOMPARE(session->selection().active, std::optional(lines[2]->id));
        // The end asks "Reached end" in a box that does not block: F3 (and any
        // other find) is refused while it waits.
        application->setFindQuestionHandler({});
        auto *question = item<QObject>("findQuestion");
        press(Qt::Key_F3);
        QTRY_VERIFY(question->property("visible").toBool());
        QCOMPARE(question->property("text").toString(), QStringLiteral("Reached end. Search from the beginning?"));
        QVERIFY(application->findBusy());
        press(Qt::Key_F3);
        application->findNext();
        application->runFindReplace(QStringLiteral("find"), {{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("zzz")}});
        QCOMPARE(question->property("queue").toList().size(), 0);
        QCOMPARE(session->selection().active, std::optional(lines[2]->id));
        // Yes: from the beginning.
        QVERIFY(QMetaObject::invokeMethod(question->findChild<QObject *>(QStringLiteral("findAnswerYes")), "click"));
        QTRY_VERIFY(!question->property("visible").toBool());
        QVERIFY(!application->findBusy());
        QCOMPARE(session->selection().active, std::optional(lines[0]->id));
        // Find all lists the matches on the right; a double click goes there.
        application->setFindQuestionHandler([&](int, const QString &text) {
            questions << text;
            return 2;
        });
        QVERIFY(QMetaObject::invokeMethod(item("findAllCurrentButton"), "click"));
        QTRY_VERIFY(item("findResultsList")->isVisible());
        const auto rows = application->findResults();
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("find.ass"));
        QCOMPARE(rows[2].toMap().value(QStringLiteral("line")).toString(), QStringLiteral("Line 2: "));
        QCOMPARE(rows[2].toMap().value(QStringLiteral("match")).toString(), QStringLiteral("beta"));
        QCOMPARE(item("findResultsList")->property("count").toInt(), 4);
        application->showFindResult(3);
        QCOMPARE(session->selection().active, std::optional(lines[2]->id));
        QCOMPARE(application->editor().selectionStart(), 6);
        // Replace the checked results but the second, as one step.
        application->toggleFindResult(2);
        item("findResultsReplace")->setProperty("editText", QStringLiteral("x"));
        QVERIFY(QMetaObject::invokeMethod(item("replaceCheckedButton"), "click"));
        QCOMPARE(text(0), QStringLiteral("Alpha x"));
        QCOMPARE(text(1), QStringLiteral("beta"));
        QCOMPARE(text(2), QStringLiteral("gamma x"));
        QCOMPARE(session->history().back().name, std::string("Replace all"));
        QVERIFY(!application->canReplaceFindResults());
        QVERIFY(!item("replaceCheckedButton")->isEnabled());
        // Ctrl+F again on the Find tab with the focus in the tool hides it.
        item("findText")->forceActiveFocus();
        press(Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(!dock->property("isOpen").toBool());
        application->editor().discard();
    }

    // F1: changing the editing target resets the search (legacy OnPageChanged
    // calls FR->Reset()): F3 starts over in the new Document instead of going
    // on from the row the old Document's search reached.
    void switchingDocumentsResetsTheSearch()
    {
        const QString a = writeFile(dir, "fa.ass",
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,y\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,y\n"
                                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,x1\n"
                                    "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,x2\n");
        const QString b = writeFile(dir, "fb.ass",
                                    "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,x3\n"
                                    "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,y\n"
                                    "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,y\n"
                                    "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,x4\n");
        QVERIFY(application->openFile(a));
        QVERIFY(application->openFile(b)); // a second Document; A stays the editing target
        const auto second = application->workspace().documents().back();
        QStringList questions;
        application->setFindQuestionHandler([&](int, const QString &text) {
            questions << text;
            return 2;
        });
        const QVariantMap find{{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("x")}};
        application->runFindReplace(QStringLiteral("find"), find);
        application->findNext(); // A's row 3: the search would go on at row 4, past B's end
        // A result in B makes B the editing target (row 3 active).
        application->runFindReplace(QStringLiteral("findAllTabs"), find);
        const auto rows = application->findResults();
        QCOMPARE(rows.size(), 6); // A + 2, B + 2
        application->showFindResult(5);
        QCOMPARE(application->workspace().editingTarget(), std::optional(second));
        auto *session = application->files().session(second);
        const auto lines = session->document().lines();
        QCOMPARE(session->selection().active, std::optional(lines[3]->id));
        // F3 starts over (All lines: row 0) instead of "Reached end" from row 4.
        application->findNext();
        QVERIFY2(questions.isEmpty(), qPrintable(questions.join(QLatin1Char('|'))));
        QCOMPARE(session->selection().active, std::optional(lines[0]->id));
        QCOMPARE(application->editor().selectionStart(), 0);
        QCOMPARE(application->editor().selectionEnd(), 1);
    }

    // U1-unicode-case: Find and replace, Select lines and the misspell
    // replacer's MoveCase ignore case for every letter whatever the interface
    // language (legacy's English interface folded A-Z only: captured, a plain
    // "łódź" never found "ŁÓDŹ"). A regular expression folds every letter too.
    void caseFoldingIgnoresCaseForEveryLetter()
    {
        const QString path = writeFile(dir, "fold.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Ąb żółw ŁÓDŹ\n"
                                                        "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,abc\n");
        for (const bool polish : {false, true}) {
            QTemporaryDir own;
            app::Application::Options options;
            options.settingsFile = own.filePath(QStringLiteral("hikari.ini"));
            options.catalogDir = own.filePath(QStringLiteral("Catalog"));
            if (polish) {
                app::Application first(options);
                first.settingsStore()->set("program.language", QStringLiteral("pl"));
            }
            app::Application a(options);
            QVERIFY(a.openFile(path));
            auto *session = a.files().session(*a.workspace().editingTarget());
            const auto text = [&] {
                const auto &t = session->document().lines()[0]->text;
                return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
            };
            // Select lines, plain and case folded (SelectLines' MakeLower).
            QCOMPARE(a.selectLines({{QStringLiteral("find"), QString::fromUtf8("łódź")}, {QStringLiteral("field"), 0},
                                    {QStringLiteral("mode"), 0}, {QStringLiteral("action"), 0}},
                                   false),
                     QStringLiteral("1 lines selected."));
            // The misspell replacer: MoveCase's iswupper and MakeUpper.
            for (int i = 0; i < a.misspellRules().size(); ++i)
                a.checkMisspellRule(i, false);
            QVERIFY(a.addMisspellRule({{QStringLiteral("find"), QString::fromUtf8("ąb")},
                                       {QStringLiteral("replace"), QStringLiteral("xy")}, {QStringLiteral("options"), 0}}));
            QVERIFY(a.addMisspellRule({{QStringLiteral("find"), QString::fromUtf8("żółw")},
                                       {QStringLiteral("replace"), QString::fromUtf8("żółw")}, {QStringLiteral("options"), 4}}));
            const int rules = int(a.misspellRules().size());
            a.checkMisspellRule(rules - 2, true);
            a.checkMisspellRule(rules - 1, true);
            a.replaceMisspells({{QStringLiteral("lines"), 0}}, false);
            QCOMPARE(text(), QString::fromUtf8("Xy ŻÓŁW ŁÓDŹ"));
            // Find and replace: plain, then a regular expression.
            QStringList questions;
            a.setFindQuestionHandler([&](int, const QString &q) {
                questions << q;
                return 2;
            });
            QVariantMap replace{{QStringLiteral("tab"), 1},
                                {QStringLiteral("find"), QString::fromUtf8("łódź")},
                                {QStringLiteral("replace"), QStringLiteral("x")}};
            a.runFindReplace(QStringLiteral("replaceAll"), replace);
            QCOMPARE(questions.back(), QStringLiteral("Replaced 1 times."));
            replace[QStringLiteral("regex")] = true;
            a.runFindReplace(QStringLiteral("replaceAll"), replace);
            QCOMPARE(text(), QString::fromUtf8("Xy ŻÓŁW x"));
        }
    }

    // F1: Replace in subtitles rewrites only the listed subtitle files that
    // changed, each after its backup; links are followed as legacy wxDir
    // follows them (a link cycle is cut), the charset is detected with
    // uchardet (R4-uchardet), comments and blank lines stay, and other files
    // stay byte-identical.
    void findReplaceInFilesTouchesOnlyItsTargets()
    {
        QTemporaryDir folder;
        QTemporaryDir outside;
        QTemporaryDir backup;
        const auto put = [](const QString &path, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        const QByteArray ass = "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a cat\n";
        put(folder.filePath(QStringLiteral("one.ass")),
            "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a cat\n"
            "Comment: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,a cat note\n\n"
            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,dog  \n");
        put(folder.filePath(QStringLiteral("sub/two.srt")), "1\n00:00:01,000 --> 00:00:02,000\ncat\n");
        put(folder.filePath(QStringLiteral("none.ass")), "[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,dog\n");
        put(folder.filePath(QStringLiteral("notes.doc")), "cat");
        put(folder.filePath(QStringLiteral(".hidden.ass")), ass);
        // UTF-16 with a BOM is read and written back as UTF-8 with a BOM.
        QByteArray utf16("\xFF\xFE");
        for (const QChar c : QStringLiteral("cat\r\n"))
            utf16.append(char(c.unicode() & 0xFF)).append(char(c.unicode() >> 8));
        put(folder.filePath(QStringLiteral("wide.txt")), utf16);
        // Not UTF-8: uchardet names the charset (ISO-8859-1 here), and the
        // changed file is written back as UTF-8 with a BOM.
        put(folder.filePath(QStringLiteral("latin.txt")),
            "Voil\xE0 l'\xE9t\xE9, \xE7" "a va tr\xE8s bien \xE0 la fa\xE7" "ade du caf\xE9 et du cat.\n");
        put(outside.filePath(QStringLiteral("far.ass")), ass);
#ifdef _WIN32
        // QFile::link makes a shortcut on Windows (.lnk data under this name),
        // a file rather than a link: real links, where this user may make them.
        std::error_code linkError, loopError;
        std::filesystem::create_symlink(outside.filePath(QStringLiteral("far.ass")).toStdU16String(),
                                        folder.filePath(QStringLiteral("link.ass")).toStdU16String(), linkError);
        std::filesystem::create_directory_symlink(folder.path().toStdU16String(),
                                                  folder.filePath(QStringLiteral("sub/loop")).toStdU16String(), loopError);
        const bool linked = !linkError;
        const bool looped = !loopError;
#else
        const bool linked = QFile::link(outside.filePath(QStringLiteral("far.ass")), folder.filePath(QStringLiteral("link.ass")));
        const bool looped = QFile::link(folder.path(), folder.filePath(QStringLiteral("sub/loop")));
#endif
        const auto snapshot = [&] {
            std::map<QString, QByteArray> files;
            for (const QString &base : {folder.path(), outside.path()}) {
                QDirIterator it(base, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                                QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString p = it.next();
                    QFile f(p);
                    if (f.open(QIODevice::ReadOnly))
                        files[p] = f.readAll();
                }
            }
            return files;
        };
        const auto before = snapshot();
        application->setReplaceBackupFolder(backup.path());
        // A slice per file: legacy's count without the running totals
        // (find_replace_tests covers those).
        application->setFindProcessorCount(64);
        QStringList questions;
        application->setFindQuestionHandler([&](int, const QString &text) {
            questions << text;
            return 1; // Yes
        });
        application->runFindReplace(QStringLiteral("replaceInFiles"),
                                    {{QStringLiteral("tab"), 2},
                                     {QStringLiteral("find"), QStringLiteral("cat")},
                                     {QStringLiteral("replace"), QStringLiteral("dog")},
                                     {QStringLiteral("folder"), folder.path()},
                                     {QStringLiteral("subfolders"), true}});
        std::set<QString> expected{QStringLiteral("one.ass"), QStringLiteral("two.srt"), QStringLiteral("wide.txt"),
                                   QStringLiteral("latin.txt")};
        if (linked)
            expected.insert(QStringLiteral("far.ass")); // through link.ass, which stays a link
#ifdef _WIN32
        // wxDir skips files with the hidden attribute on Windows; a leading
        // dot hides nothing there (R5-per-platform)
        expected.insert(QStringLiteral(".hidden.ass"));
#endif
        QCOMPARE(questions.size(), 2);
        QCOMPARE(questions[1], QStringLiteral("Replaced %1 times.").arg(expected.size())); // once in each file
        const auto after = snapshot();
        QCOMPARE(after.size(), before.size()); // no file appeared or vanished
        std::set<QString> changed;
        for (const auto &[p, bytes] : after)
            if (before.at(p) != bytes)
                changed.insert(QFileInfo(p).fileName());
        QCOMPARE(changed, expected);
        if (linked)
            QVERIFY(QFileInfo(folder.filePath(QStringLiteral("link.ass"))).isSymLink());
        // The comment, the blank line and the untouched Line stay as they were
        // (legacy dropped the first two: R3-hang-crash-loss).
        QCOMPARE(after.at(folder.filePath(QStringLiteral("one.ass"))),
                 QByteArray("\xEF\xBB\xBF[Events]\r\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a dog\r\n"
                            "Comment: 0,0:00:02.00,0:00:03.00,Default,,0,0,0,,a cat note\r\n\r\n"
                            "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,dog  \r\n"));
        QCOMPARE(after.at(folder.filePath(QStringLiteral("wide.txt"))),
                 QByteArray("\xEF\xBB\xBF" "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,dog\r\n"));
        QCOMPARE(after.at(folder.filePath(QStringLiteral("latin.txt"))),
                 QByteArray("\xEF\xBB\xBF" "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,") +
                     QStringLiteral("Voilà l'été, ça va très bien à la façade du café et du dog.\r\n").toUtf8());
        // The backups hold the files as they were.
        QFile copy(backup.filePath(QStringLiteral("one.ass")));
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), before.at(folder.filePath(QStringLiteral("one.ass"))));
        QVERIFY(QFileInfo::exists(backup.filePath(QStringLiteral("two.srt"))));
        QCOMPARE(QDir(backup.path()).entryList(QDir::Files | QDir::Hidden).size(), qsizetype(expected.size()));
        Q_UNUSED(looped);
        // A file result opens the file in place of an untouched Untitled
        // Document (legacy loaded it into a tab without a path).
        QVERIFY(application->reviewClose(QStringLiteral("new")).isEmpty());
        application->finishClose();
        QVERIFY(application->targetUntitled());
        application->runFindReplace(QStringLiteral("findInFiles"),
                                    {{QStringLiteral("tab"), 2},
                                     {QStringLiteral("find"), QStringLiteral("a dog")},
                                     {QStringLiteral("filters"), QStringLiteral("one.ass")},
                                     {QStringLiteral("folder"), folder.path()}});
        const auto found = application->findResults();
        QCOMPARE(found.size(), 2);
        application->showFindResult(1);
        QCOMPARE(application->workspace().documents().size(), std::size_t(1));
        QVERIFY(!application->targetUntitled());
        auto *opened = application->files().session(*application->workspace().editingTarget());
        QCOMPARE(opened->selection().active, std::optional(opened->document().lines()[0]->id));
        // An invalid folder says so.
        application->runFindReplace(QStringLiteral("findInFiles"),
                                    {{QStringLiteral("tab"), 2},
                                     {QStringLiteral("find"), QStringLiteral("cat")},
                                     {QStringLiteral("folder"), folder.filePath(QStringLiteral("missing"))}});
        QCOMPARE(questions.back(), QStringLiteral("Search path is invalid"));
    }

    // F1 (R5-per-platform): legacy wxDir::GetAllFiles lists each folder's
    // subfolders first, then its files, in the file system's order (no
    // sorting). A folder reached twice through a link is listed twice; only
    // a real cycle is cut (F1-link-loop).
    void findInFilesListsAsWxDirDoes()
    {
        QTemporaryDir folder;
        const auto put = [](const QString &path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,cat\n");
        };
        for (const char *name : {"zeta.ass", "alpha.ass", "mid.ass", "Beta.ass", "kappa.ass", "s2/b.ass", "s2/a.ass",
                                 "s1/c.ass", "s1/deeper/d.ass", "s1/Z.ass"})
            put(folder.filePath(QString::fromLatin1(name)));
#ifndef _WIN32
        QVERIFY(QFile::link(folder.filePath(QStringLiteral("s1")), folder.filePath(QStringLiteral("again"))));
        QVERIFY(QFile::link(folder.path(), folder.filePath(QStringLiteral("s1/up")))); // a cycle
#endif
        // The entries as the file system lists them (readdir; FindFirstFile).
        const auto entries = [](const QString &dir) {
            QStringList out;
#ifdef _WIN32
            QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            while (it.hasNext())
                out << it.nextFileInfo().fileName();
#else
            if (DIR *d = ::opendir(QFile::encodeName(dir).constData())) {
                while (const dirent *e = ::readdir(d)) {
                    const QString name = QFile::decodeName(e->d_name);
                    if (name != QLatin1String(".") && name != QLatin1String(".."))
                        out << name;
                }
                ::closedir(d);
            }
#endif
            return out;
        };
        QStringList expected;
        std::function<void(const QString &, QStringList)> walk = [&](const QString &dir, QStringList path) {
            const QString canonical = QFileInfo(dir).canonicalFilePath();
            if (path.contains(canonical))
                return;
            path << canonical;
            for (const QString &name : entries(dir))
                if (QFileInfo(dir + QLatin1Char('/') + name).isDir())
                    walk(dir + QLatin1Char('/') + name, path);
            for (const QString &name : entries(dir))
                if (!QFileInfo(dir + QLatin1Char('/') + name).isDir() && name.endsWith(QLatin1String(".ass")))
                    expected << QDir::toNativeSeparators(dir + QLatin1Char('/') + name);
        };
        walk(folder.path(), {});
#ifndef _WIN32
        QCOMPARE(expected.count(QDir::toNativeSeparators(folder.filePath(QStringLiteral("again/c.ass")))), 1);
        QCOMPARE(expected.size(), 13); // s1's three files twice
#endif
        application->runFindReplace(QStringLiteral("findInFiles"),
                                    {{QStringLiteral("tab"), 2},
                                     {QStringLiteral("find"), QStringLiteral("cat")},
                                     {QStringLiteral("filters"), QStringLiteral("*.ass")},
                                     {QStringLiteral("folder"), folder.path()},
                                     {QStringLiteral("subfolders"), true}});
        QStringList headers;
        for (const QVariant &row : application->findResults())
            if (row.toMap().value(QStringLiteral("header")).toBool())
                headers << row.toMap().value(QStringLiteral("text")).toString();
        QCOMPARE(headers, expected);
    }

    // F1: a file result while the Untitled editing target has changes: legacy
    // OpenFile asked to save first (here the close review) and loaded the
    // file into that tab; after Cancel the result's Line was taken from the
    // Untitled Document.
    void findResultInAChangedUntitledDocumentAsksFirst()
    {
        QTemporaryDir folder;
        {
            QFile f(folder.filePath(QStringLiteral("one.ass")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Events]\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,a cat\n");
        }
        QVERIFY(application->reviewClose(QStringLiteral("new")).isEmpty());
        application->finishClose();
        QVERIFY(application->targetUntitled());
        auto *untitled = application->files().session(*application->workspace().editingTarget());
        QVERIFY(untitled->run(application::Command{"Edit", untitled->revision(), {untitled->document().lines()[0]->id},
                                                   [&](core::Document &d) {
                                                       return d.editLine(untitled->document().lines()[0]->id,
                                                                         [](core::LineRecord &l) { l.text = u8"mine"; });
                                                   }}));
        QVERIFY(untitled->isDirty());
        application->runFindReplace(QStringLiteral("findInFiles"),
                                    {{QStringLiteral("tab"), 2},
                                     {QStringLiteral("find"), QStringLiteral("cat")},
                                     {QStringLiteral("folder"), folder.path()}});
        QCOMPARE(application->findResults().size(), 2);
        auto *review = item<QObject>("closeReview");
        QVERIFY(review);
        // Cancel: the Untitled Document stays, its row 0 is the result's Line.
        application->showFindResult(1);
        QTRY_VERIFY(review->property("visible").toBool());
        QVERIFY(application->findBusy());
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("closeCancel"), "click"));
        QTRY_VERIFY(!review->property("visible").toBool());
        QVERIFY(!application->findBusy());
        QVERIFY(application->targetUntitled());
        QVERIFY(untitled->isDirty());
        QCOMPARE(untitled->selection().active, std::optional(untitled->document().lines()[0]->id));
        // Discard: the file is loaded into that tab and its Line is shown.
        application->showFindResult(1);
        QTRY_VERIFY(review->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(item<QObject>("closeDiscardAll"), "click"));
        QTRY_VERIFY(!review->property("visible").toBool());
        QVERIFY(!application->findBusy());
        QVERIFY(!application->targetUntitled());
        QCOMPARE(application->workspace().documents().size(), std::size_t(1));
        auto *opened = application->files().session(*application->workspace().editingTarget());
        QCOMPARE(QString::fromStdString(application->files().destination(*application->workspace().editingTarget())->value),
                 QFileInfo(folder.filePath(QStringLiteral("one.ass"))).absoluteFilePath());
        QCOMPARE(opened->selection().active, std::optional(opened->document().lines()[0]->id));
        QCOMPARE(application->editor().selectionStart(), 2);
        QCOMPARE(application->editor().selectionEnd(), 5);
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

    // E5: a TLMode file for the translation mode controls: an Unconfirmed
    // pair, a translated pair and an untranslated one whose original has tags.
    // Without `tlMode`, the same file before translation mode: no TLMode
    // keys, and each pair's lines are Lines of their own.
    QString writeTranslationFile(const char *name, bool tlMode = true)
    {
        const QString path = dir.filePath(QLatin1String(name));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(tlMode ? "[Script Info]\nScriptType: v4.00+\nTLMode: Yes\nTLMode Style: O\n\n[V4+ Styles]\n"
                       : "[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n");
        f.write("Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,"
                "10,10,10,1\n"
                "Style: O,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,"
                "10,10,10,1\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,Gate\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,O,,0,0,0,,Tower\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
                "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,,{\\i1}Wall {\\b1}high\n"
                "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,\n");
        return path;
    }
    QPoint centreOf(QQuickItem *it) const
    {
        return it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint();
    }
    // In translation mode the Line editor's rows need more height than its
    // dock has in the default 1280 x 800 layout: the translation buttons end
    // at the dock's bottom edge, and with the Ubuntu runner's font metrics
    // their centre falls a few pixels below it, on the dock below. A test
    // clicking one gives the window the height for it first.
    void roomInTheEditorFor(QQuickItem *button)
    {
        if (window->height() < 1000)
            window->resize(window->width(), 1000);
        auto *panel = item("editorPanel");
        QTRY_VERIFY(panel->mapRectToScene(panel->boundingRect()).contains(button->mapRectToScene(button->boundingRect())));
    }
    static QString q8(const std::u8string &s)
    {
        return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), qsizetype(s.size()));
    }

    // E5: the editor's "Translator mode" check box (EditBox::OnTlMode,
    // SubsGrid::SetTlMode): one step each way, the turn-off asked first with
    // No as the default, and one Undo per switch.
    void translatorModeSwitchesWithOneStepEach()
    {
        auto *check = item("translatorMode");
        QVERIFY(check);
        QVERIFY(!check->isEnabled()); // no Document
        QVERIFY(application->openFile(writeTranslationFile("e5-switch.ass", false)));
        QTRY_VERIFY(check->isEnabled());
        auto *session = application->files().session(*application->workspace().editingTarget());
        QVERIFY(!check->property("checked").toBool());
        QVERIFY(!item("translationText")->isVisible());
        QVERIFY(!item("notConfirmed")->isVisible());
        const auto steps = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(check));
        QTRY_VERIFY(application->editor().translationMode());
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Turning on translator mode"));
        QCOMPARE(session->document().scriptInfo(u8"TLMode Style"), std::optional<std::u8string>(u8"TLmode"));
        QVERIFY(check->property("checked").toBool());
        QTRY_VERIFY(item("translationText")->isVisible());
        QVERIFY(item("notConfirmed")->isVisible());
        QVERIFY(item("movingTags")->isVisible());

        // Turning it off asks; No, the focused default, keeps it on.
        auto *confirm = item<QObject>("translatorModeOffConfirm");
        QVERIFY(confirm);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(check));
        QTRY_VERIFY(confirm->property("opened").toBool());
        QVERIFY(check->property("checked").toBool()); // still checked while it asks
        press(Qt::Key_Space); // the focused No
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QVERIFY(application->editor().translationMode());
        QCOMPARE(session->historySize(), steps + 1);
        // Yes turns it off as one step.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(check));
        QTRY_VERIFY(confirm->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
        QTRY_VERIFY(!application->editor().translationMode());
        QCOMPARE(session->historySize(), steps + 2);
        QCOMPARE(session->history().back().name, std::string("Turning off translator mode"));
        QVERIFY(!check->property("checked").toBool());
        QTRY_VERIFY(!item("translationText")->isVisible());
        // One Undo per switch.
        QVERIFY(application->editor().undo());
        QTRY_VERIFY(application->editor().translationMode());
        QVERIFY(check->property("checked").toBool());
        QVERIFY(application->editor().undo());
        QTRY_VERIFY(!application->editor().translationMode());
        QVERIFY(!check->property("checked").toBool());
    }

    // E5: legacy enables the check box for ASS only.
    void translatorModeIsForAssOnly()
    {
        const QString path = dir.filePath(QStringLiteral("e5.srt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("1\n00:00:01,000 --> 00:00:02,000\nA\n\n");
        }
        QVERIFY(application->openFile(path));
        QVERIFY(!item("translatorMode")->isEnabled());
        QVERIFY(!application->turnOnTranslationMode());
    }

    // E5: an ASS Document needs a file too (HikariSubFrame.cpp:2401,
    // SubsPath != ""): an Untitled one leaves the check box disabled.
    void translatorModeNeedsAFile()
    {
        application->addPage();
        auto *session = application->files().session(*application->workspace().editingTarget());
        QVERIFY(session);
        QCOMPARE(session->document().format(), core::SubtitleFormat::Ass);
        QVERIFY(!application->translatorModeAvailable());
        QVERIFY(!item("translatorMode")->isEnabled());
        QVERIFY(!application->turnOnTranslationMode());
        QVERIFY(!session->document().scriptInfo(u8"TLMode"));
        // The same text with a file enables it.
        QVERIFY(application->openFile(writeTranslationFile("e5-file.ass", false)));
        QTRY_VERIFY(item("translatorMode")->isEnabled());
    }

    // E5: the Grid's Moving translation text entries do nothing while the
    // original is not shown (SubsGrid::MoveTextTL returns without
    // showOriginal, SubsGrid.cpp:1037). "TLMode Showtl" outlives turning the
    // mode off (SetTlMode(false) deletes TLMode and its Style only,
    // SubsGridBase.cpp:1341-1351), so the menu entries stay enabled after the
    // mode is turned back on; SetTlMode(true) shows the original again only
    // with TL_MODE_SHOW_ORIGINAL (SubsGridBase.cpp:1331).
    void shiftTranslationNeedsTheOriginalShown_data()
    {
        QTest::addColumn<bool>("showOriginalSetting");
        QTest::newRow("option off") << false;
        QTest::newRow("option on") << true;
    }
    void shiftTranslationNeedsTheOriginalShown()
    {
        QFETCH(bool, showOriginalSetting);
        application->settingsStore()->set("translation.showOriginal", showOriginalSetting);
        const QString path = writeTranslationFile(showOriginalSetting ? "e5-shift-on.ass" : "e5-shift-off.ass");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QByteArray bytes = f.readAll();
            bytes.replace("TLMode: Yes\n", "TLMode: Yes\nTLMode Showtl: Yes\n");
            QVERIFY(f.resize(0));
            QVERIFY(f.seek(0));
            QCOMPARE(f.write(bytes), bytes.size());
        }
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        QVERIFY(application->turnOffTranslationMode());
        QVERIFY(application->turnOnTranslationMode());
        QVERIFY(application->canShiftTranslation()); // Showtl kept: the entries are enabled
        const auto first = session->document().lines()[0]->id;
        session->setSelection({first, {first}, {}, {}});
        const auto steps = session->historySize();
        const auto lines = session->document().lines().size();
        // "Add line" (Translation) appends a Line when it runs.
        const bool moved = application->shiftTranslation(int(application::TranslationMove::AddTranslationLine));
        QCOMPARE(moved, showOriginalSetting);
        QCOMPARE(session->historySize(), showOriginalSetting ? steps + 1 : steps);
        QCOMPARE(session->document().lines().size(), showOriginalSetting ? lines + 1 : lines);
    }

    // E5: "Not confirmed" (EditBox::OnDoubtfulTl) flips every selected Line
    // as one step and shows the active Line's flag.
    void notConfirmedFlipsTheSelectedLines()
    {
        QVERIFY(application->openFile(writeTranslationFile("e5-unconfirmed.ass")));
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *button = item("notConfirmed");
        QTRY_VERIFY(button->property("checked").toBool()); // the "\fD" pair loads Unconfirmed
        press(Qt::Key_A, Qt::ControlModifier);
        QTRY_COMPARE(session->selection().selected.size(), std::size_t(3));
        roomInTheEditorFor(button);
        const auto steps = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(button));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Mark unconfirmed"));
        const auto lines = session->document().lines();
        QVERIFY(!lines[0]->unconfirmed);
        QVERIFY(lines[1]->unconfirmed);
        QVERIFY(lines[2]->unconfirmed);
        QVERIFY(!button->property("checked").toBool());
        QVERIFY(application->editor().undo());
        QTRY_VERIFY(button->property("checked").toBool());
        QVERIFY(session->document().lines()[0]->unconfirmed);
        QVERIFY(!session->document().lines()[1]->unconfirmed);
    }

    // E5-unconfirmed-own-step: Alt+Down (EDITBOX_SET_DOUBTFUL) with typed text
    // records the text's step, then its own "Mark unconfirmed" step, and goes
    // to the next Line; one Undo takes the flip away and leaves the text
    // (legacy's flip joined the text's step, EditBox.cpp:1946-1962).
    void markUnconfirmedIsItsOwnStep()
    {
        QVERIFY(application->openFile(writeTranslationFile("e5-own-step.ass")));
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Down);
        auto *original = item("lineText");
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Tower"));
        const auto id = session->document().lines()[1]->id;
        QVERIFY(!session->document().lines()[1]->unconfirmed);
        original->forceActiveFocus();
        original->setProperty("cursorPosition", 5);
        QTest::keyClick(window, 's');
        QTRY_VERIFY(session->draftLine().has_value());
        const auto steps = session->historySize();
        press(Qt::Key_Down, Qt::AltModifier);
        QTRY_COMPARE(session->historySize(), steps + 2);
        QCOMPARE(session->history().back().name, std::string("Mark unconfirmed"));
        QVERIFY(session->document().lines()[1]->unconfirmed);
        QCOMPARE(session->document().lines()[1]->text, std::u8string(u8"Towers"));
        QVERIFY(session->selection().active != id); // the next Line
        QVERIFY(application->editor().undo());
        QVERIFY(!session->document().lines()[1]->unconfirmed);
        QCOMPARE(session->document().lines()[1]->text, std::u8string(u8"Towers"));
    }

    // E5: "Moving tags" (EditBox::SetTextWithTags): an untranslated Line is
    // shown split, the Translated field takes the focus, and the first change
    // makes both fields the draft.
    void movingTagsSplitsAnUntranslatedLine()
    {
        QVERIFY(application->openFile(writeTranslationFile("e5-moving.ass")));
        auto *session = application->files().session(*application->workspace().editingTarget());
        application->editor().setShowTags(true);
        auto *moving = item("movingTags");
        QVERIFY(!moving->property("checked").toBool());
        roomInTheEditorFor(moving);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(moving));
        QTRY_VERIFY(application->settingsStore()->boolean("translation.autoMoveTagsFromOriginal"));
        QVERIFY(moving->property("checked").toBool());
        QVERIFY(application->editor().moveTags());

        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End); // the untranslated "{\i1}Wall {\b1}high"
        auto *original = item("lineText");
        auto *translated = item("translationText");
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Wall high"));
        QCOMPARE(translated->property("text").toString(), QStringLiteral("{\\i1}{\\b1}"));
        QTRY_VERIFY(translated->hasActiveFocus());
        QCOMPARE(translated->property("cursorPosition").toInt(), 5);
        // Shown only: the Document and the draft are untouched.
        QVERIFY(!session->draftLine());
        QCOMPARE(q8(session->document().lines()[2]->text), QStringLiteral("{\\i1}Wall {\\b1}high"));
        // A translated Line is shown whole.
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Up);
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Tower"));
        QCOMPARE(translated->property("text").toString(), QStringLiteral("Wieza"));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End);
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Wall high"));
        QTRY_VERIFY(translated->hasActiveFocus());
        // Typing in the Translated field: both fields become the draft.
        QTest::keyClick(window, 'X');
        QTRY_COMPARE(translated->property("text").toString(), QStringLiteral("{\\i1}X{\\b1}"));
        QVERIFY(session->draftRecord());
        QCOMPARE(q8(session->draftRecord()->text), QStringLiteral("Wall high"));
        QCOMPARE(q8(session->draftRecord()->translation), QStringLiteral("{\\i1}X{\\b1}"));
        QVERIFY(application->editor().commit());
        QCOMPARE(q8(session->document().lines()[2]->text), QStringLiteral("Wall high"));
        QCOMPARE(q8(session->document().lines()[2]->translation), QStringLiteral("{\\i1}X{\\b1}"));
        // Undo takes the edit back; turning Moving tags off shows the Line whole.
        QVERIFY(application->editor().undo());
        QTRY_COMPARE(q8(session->document().lines()[2]->text), QStringLiteral("{\\i1}Wall {\\b1}high"));
        roomInTheEditorFor(moving);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(moving));
        QTRY_VERIFY(!application->settingsStore()->boolean("translation.autoMoveTagsFromOriginal"));
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("{\\i1}Wall {\\b1}high"));
        QCOMPARE(translated->property("text").toString(), QString());
    }

    // E5: Moving tags splits only with the crosshair (Visual <= CROSS,
    // EditBox.cpp:1811); another visual tool shows the Line whole.
    void movingTagsWaitsForTheCrosshair()
    {
        QVERIFY(application->openFile(writeTranslationFile("e5-moving-visual.ass")));
        application->editor().setShowTags(true);
        application->settingsStore()->set("translation.autoMoveTagsFromOriginal", true);
        QTRY_VERIFY(application->editor().moveTags());
        auto &tools = application->visualTools();
        QTRY_VERIFY(tools.railEnabled());
        tools.selectFamily(3);
        QCOMPARE(tools.activeFamily(), 3);
        auto *original = item("lineText");
        auto *translated = item("translationText");
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End); // the untranslated "{\i1}Wall {\b1}high"
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("{\\i1}Wall {\\b1}high"));
        QCOMPARE(translated->property("text").toString(), QString());
        // Back to the crosshair, the next SetTextWithTags splits it.
        tools.selectFamily(3);
        QCOMPARE(tools.activeFamily(), 0);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Up);
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Tower"));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End);
        QTRY_COMPARE(original->property("text").toString(), QStringLiteral("Wall high"));
        QCOMPARE(translated->property("text").toString(), QStringLiteral("{\\i1}{\\b1}"));
    }

    // E5: the Grid's "Original text" and "Translation" columns (legacy
    // showOriginal: TL_MODE_SHOW_ORIGINAL or "TLMode Showtl" when the file
    // loads; off when translation mode is turned off).
    void gridShowsTheTranslationColumnWithTheOriginal()
    {
        application->settingsStore()->set("translation.showOriginal", true);
        QVERIFY(application->openFile(writeTranslationFile("e5-columns.ass")));
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        QVERIFY(grid);
        auto *model = grid->model();
        QTRY_COMPARE(grid->columnTitle(grid->columnCount() - 1), QStringLiteral("Translation"));
        QCOMPARE(grid->columnTitle(grid->columnCount() - 2), QStringLiteral("Original text"));
        QCOMPARE(grid->cellText(0, grid->columnCount() - 2), QStringLiteral("Gate"));
        QCOMPARE(grid->cellText(0, grid->columnCount() - 1), QStringLiteral("Brama"));
        // The two share what the other columns leave (SubsGridWindow.cpp:481-485).
        QCOMPARE(grid->cellRect(0, grid->columnCount() - 1).width(), grid->cellRect(0, grid->columnCount() - 2).width());
        const int columns = grid->columnCount();
        QVERIFY(application->turnOffTranslationMode()); // Save translation's switch
        QTRY_COMPARE(grid->columnCount(), columns - 1);
        QCOMPARE(grid->columnTitle(grid->columnCount() - 1), QStringLiteral("Text"));
        QCOMPARE(grid->cellText(0, grid->columnCount() - 1), QStringLiteral("Brama"));
        Q_UNUSED(model);
    }

    // E5: without the original shown, a translated Line's "Text" is its
    // translation (SubsGridWindow.cpp:415).
    void gridTextShowsTheTranslationWithoutTheOriginal()
    {
        QVERIFY(application->openFile(writeTranslationFile("e5-text.ass")));
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        QTRY_COMPARE(grid->columnTitle(grid->columnCount() - 1), QStringLiteral("Text"));
        QCOMPARE(grid->cellText(0, grid->columnCount() - 1), QStringLiteral("Brama"));
        QCOMPARE(grid->cellText(2, grid->columnCount() - 1), QStringLiteral("{\\i1}Wall {\\b1}high"));
    }

    // E5: TL_MODE_HIDE_ORIGINAL_ON_VIDEO also writes the originals as Comments
    // when saving (SubsGrid::SaveFile).
    void hideOriginalOnVideoSavesTheOriginalsAsComments()
    {
        application->settingsStore()->set("translation.hideOriginalOnVideo", true);
        const QString path = writeTranslationFile("e5-hide.ass");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        session->setSelection({session->document().lines()[1]->id, {session->document().lines()[1]->id}, {}, {}});
        QVERIFY(application::toggleUnconfirmed(*session).has_value()); // something to save
        QVERIFY(application->editor().save());
        application->waitForWrites();
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray bytes = saved.readAll();
        // The unedited untranslated "Wall" pair keeps its two lines
        // (C03-preservation: unchanged raw content is retained), where legacy
        // SaveFile writes it as one line with the option on or off
        // (SubsGridBase.cpp:372-377, GetRaw(&raw, hasTextTl = false)).
        QVERIFY2(bytes.contains("Comment: 0,0:00:01.00,0:00:02.00,O,,0,0,0,\fD,Gate\n"
                                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Brama\n"
                                "Comment: 0,0:00:03.00,0:00:04.00,O,,0,0,0,\fD,Tower\n"
                                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,Wieza\n"
                                "Dialogue: 0,0:00:05.00,0:00:06.00,O,,0,0,0,,{\\i1}Wall {\\b1}high\n"),
                 bytes.constData());
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

        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
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
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
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
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
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
              {"audio.ramCache", "Load audio into RAM"},
#ifdef _WIN32
              {"audio.outputHostApi", nullptr}, // A4-wasapi-default, Windows only
#endif
             }},
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
                // The legacy label, its hard line breaks shown as spaces (the
                // check box wraps in the page's width).
                if (controls[i].label)
                    QCOMPARE(found[i]->property("text").toString(),
                             QString::fromUtf8(controls[i].label).replace(QLatin1Char('\n'), QLatin1Char(' ')));
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
        // and the Appearance page's settings (K2), which are not bound options
        for (const auto &s : application::settingDefinitions())
            if (s.id.starts_with("appearance."))
                bound.insert(std::string(s.id));
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

    // O1: HikariChoice::FindString (wxArrayString::Index without case)
    // ignores case for every letter whatever the interface language
    // (U1-unicode-case; legacy's English interface compared A-Z only).
    void settingsChoicesIgnoreCaseForEveryLetter()
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
            QVERIFY(!open.value(QStringLiteral("warnings")).toStringList().contains(missing));
            QCOMPARE(open.value(QStringLiteral("values")).toMap().value(QStringLiteral("convert.styleCatalog")).toInt(),
                     int(open.value(QStringLiteral("catalogs")).toStringList().indexOf(accented)));
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

    // O3: File > Import legacy settings... (a ShellMenuItem) reviews a
    // legacy installation's files, imports the chosen changes for the next
    // start and rolls them back; the legacy files are never written.
    void legacySettingsImportThroughTheWindow()
    {
        QTemporaryDir home, legacy;
        QVERIFY(home.isValid() && legacy.isValid());
        QByteArray config = "[HikariSub v0.0.1-rc.1]\r\n";
        for (int i = 1; i <= 11; ++i)
            config += "EDITBOX_TAG_BUTTON_VALUE" + QByteArray::number(i) + "=\r\n";
        config += "GRID_FONT=Arial\r\nPROGRAM_THEME=Mine\r\n";
        const QByteArray hotkeysText = "[Kainote v0.9.0.1500]\r\nGLOBAL_SAVE_SUBS G=Ctrl-Alt-S\r\n";
        QDir().mkpath(legacy.filePath(QStringLiteral("Config")));
        for (const auto &[name, bytes] : {std::pair{"Config/Config.txt", config}, std::pair{"Config/Hotkeys.txt", hotkeysText}}) {
            QFile f(legacy.filePath(QLatin1String(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        }
        restartWithSpelling(home.path(), false);
        auto *menuItem = named("importSettingsMenuItem");
        QVERIFY(menuItem);
        QVERIFY(QByteArray(menuItem->metaObject()->className()).startsWith("ShellMenuItem"));
        QVERIFY(menuItem->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        auto *dialog = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsImportDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto footerButton = [&](const char *name) {
            auto *footer = dialog->property("footer").value<QQuickItem *>();
            return footer ? findItem(footer, QLatin1String(name)) : nullptr;
        };
        QVERIFY(!footerButton("settingsImportImport")->property("enabled").toBool());

        auto &importer = application->settingsImport();
        QVERIFY(importer.addRoot(legacy.path()));
        dialogItem("settingsImportDialog", "settingsImportRoots")->setProperty("currentIndex", importer.roots().size() - 1);
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsImportDialog", "settingsImportRead"), "click"));
        QCOMPARE(importer.root(), QDir::cleanPath(legacy.path()));
        auto *rows = dialogItem("settingsImportDialog", "settingsImportRows");
        QTRY_VERIFY(rows->property("count").toInt() > 0);
        QVERIFY(importer.chosen().contains("setting:grid.font"));
        QVERIFY(importer.chosen().contains("shortcut:GLOBAL_SAVE_SUBS:G"));
        QVERIFY(dialogItem("settingsImportDialog", "settingsImportSummary")->property("text").toString().contains(
            QStringLiteral("1 excluded")));
        // The theme is shown, excluded, and cannot be chosen.
        QVariantMap theme;
        for (const auto &row : importer.rows())
            if (row.toMap().value(QStringLiteral("key")) == QStringLiteral("PROGRAM_THEME"))
                theme = row.toMap();
        QCOMPARE(theme.value(QStringLiteral("disposition")).toString(), QStringLiteral("excluded"));
        QVERIFY(!theme.value(QStringLiteral("selectable")).toBool());
        // Choosing a row far down the list keeps the list where it is
        // scrolled, and "Proposed choice" still reaches the clicked row.
        {
            const QVariantList all = importer.rows();
            int last = int(all.size()) - 1;
            while (last >= 0 && !all.at(last).toMap().value(QStringLiteral("selectable")).toBool())
                --last;
            QVERIFY(last > 0);
            const QString id = all.at(last).toMap().value(QStringLiteral("id")).toString();
            const bool proposed = importer.chosen().contains(id.toStdString());
            QVERIFY(QMetaObject::invokeMethod(rows, "positionViewAtEnd"));
            QQuickItem *box = nullptr;
            QTRY_VERIFY((box = findItem(rows, QStringLiteral("settingsImportChoose_") + id)) != nullptr);
            const QPointer<QQuickItem> sameBox(box);
            const qreal scrolled = rows->property("contentY").toReal();
            QVERIFY(scrolled > 0);
            QCOMPARE(box->property("checked").toBool(), proposed);
            QVERIFY(QMetaObject::invokeMethod(box, "click"));
            QCOMPARE(importer.chosen().contains(id.toStdString()), !proposed);
            QVERIFY(sameBox);
            QCOMPARE(box->property("checked").toBool(), !proposed);
            QCOMPARE(rows->property("contentY").toReal(), scrolled);
            QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsImportDialog", "settingsImportProposed"), "click"));
            QVERIFY(sameBox);
            QCOMPARE(box->property("checked").toBool(), proposed);
            QCOMPARE(rows->property("contentY").toReal(), scrolled);
        }
        QVERIFY(QMetaObject::invokeMethod(footerButton("settingsImportImport"), "click"));
        QTRY_VERIFY(dialogItem("settingsImportDialog", "settingsImportStatus")->property("text").toString().contains(
            QStringLiteral("starts again")));
        // Not in effect in this session.
        QCOMPARE(application->settingsStore()->text("grid.font"), QStringLiteral("Tahoma"));

        restartWithSpelling(home.path(), false);
        QCOMPARE(application->settingsStore()->text("grid.font"), QStringLiteral("Arial"));
        QVERIFY(application->settingsStore()->list(application::kHotkeysSetting.data())
                    .contains(QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-Alt-S")));
        QVERIFY(!application->settingsStore()->isSet(QStringLiteral("program.theme")));

        // Roll back, asked first, in effect at the next start.
        QVERIFY(QMetaObject::invokeMethod(named("importSettingsMenuItem"), "triggered"));
        dialog = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsImportDialog"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(footerButton("settingsImportRollback")->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(footerButton("settingsImportRollback"), "click"));
        auto *question = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("settingsImportRollbackQuestion"));
        QTRY_VERIFY(question->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "accept"));
        QVERIFY(application->settingsImport().pending());
        restartWithSpelling(home.path(), false);
        QCOMPARE(application->settingsStore()->text("grid.font"), QStringLiteral("Tahoma"));
        QVERIFY(!application->settingsStore()->isSet(application::kHotkeysSetting.data()));
        // The legacy files are as they were.
        QFile f(legacy.filePath(QStringLiteral("Config/Config.txt")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), config);
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
        // The list is F3's availableDictionaries over the same folders, in
        // the order wxDir lists them on this platform (R5-per-platform: no
        // sorting; F3's own tests pin that order against readdir and
        // FindFirstFile), each symbol shown by its language name.
        const std::filesystem::path folder(own.filePath(QStringLiteral("Dictionary")).toStdU16String());
        const auto expected = [&] {
            QStringList names;
            for (const auto &symbol : application::availableDictionaries(folder))
                names << (symbol == u"en_US" ? QStringLiteral("English") : QStringLiteral("Polski"));
            if (names.isEmpty())
                names << QStringLiteral("Put files .dic and .aff to \"Dictionary\" folder");
            return names;
        };
        const auto open = a.openSettingsDialog();
        // AvailableDics pairs the i-th .dic with the i-th .aff: de_DE.dic
        // shifts the pairs that follow it in the listing (the pairing stops
        // at the shorter list, R3-hang-crash-loss).
        QCOMPARE(open.value(QStringLiteral("dictionaries")).toStringList(), expected());
        QVERIFY(QFile::remove(own.filePath(QStringLiteral("Dictionary/de_DE.dic"))));
        const auto again = a.openSettingsDialog();
        const QStringList shown = again.value(QStringLiteral("dictionaries")).toStringList();
        QCOMPARE(shown, expected());
        if (shown.contains(QStringLiteral("Polski")))
            QCOMPARE(again.value(QStringLiteral("values")).toMap().value(QStringLiteral("editor.dictionaryLanguage")).toInt(),
                     shown.indexOf(QStringLiteral("Polski")));
#ifndef _WIN32
        // Linux: the pairing follows readdir order, as legacy Linux's wxDir
        // did, not a name sort.
        std::vector<std::u16string> readdirDic, readdirAff;
        if (DIR *d = ::opendir(folder.c_str())) {
            while (const dirent *e = ::readdir(d)) {
                const std::string name = e->d_name;
                if (name.size() > 4 && name.ends_with(".dic"))
                    readdirDic.push_back(QString::fromStdString(name.substr(0, name.size() - 4)).toStdU16String());
                if (name.size() > 4 && name.ends_with(".aff"))
                    readdirAff.push_back(QString::fromStdString(name.substr(0, name.size() - 4)).toStdU16String());
            }
            ::closedir(d);
        }
        std::vector<std::u16string> pairs;
        for (std::size_t i = 0; i < readdirDic.size() && i < readdirAff.size(); ++i)
            if (readdirDic[i] == readdirAff[i])
                pairs.push_back(readdirDic[i]);
        QVERIFY(application::availableDictionaries(folder) == pairs);
#endif
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

    // O1 with F1 and F4: FindReplaceDialog keeps what it holds through Set
    // default once created and reads the defaults when it is created after
    // it; DestroyDialogs (a changed program font) saves its options and
    // drops it with its recent lists, and destroys MisspellReplacer, whose
    // destructor writes Rules.txt (HikariSubFrame::DestroyDialogs, OnClose).
    void findAndMultireplacerFollowLegacyOwnersThroughSetDefault()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        restartWithSpelling(home.path(), false);
        auto &settings = *application->settingsStore();
        const QString ini = home.filePath(QStringLiteral("hikari.ini"));
        const QString path = writeFile(dir, "owners.ass",
                                       "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,alpha\n"
                                       "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,beta\n");
        QVERIFY(application->openFile(path));
        application->setFindQuestionHandler([](int, const QString &) { return 2; });
        auto *dock = item<QObject>("searchDock");
        QVERIFY(dock);

        // Never created: Set default leaves it the defaults to read.
        settings.set("find.options", 1); // CASE_SENSITIVE
        settings.set("find.styles", QStringLiteral("Sign"));
        settings.set("find.recentFinds", QStringList{QStringLiteral("old")});
        application->resetSettings({});
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_H, Qt::ControlModifier);
        QTRY_VERIFY(dock->property("isOpen").toBool());
        auto *tool = item<QObject>("searchTool");
        auto *matchCase = item("findMatchCase");
        QVERIFY(tool && matchCase);
        QVERIFY(tool->property("opened").toBool());
        QVERIFY(tool->property("finds").toStringList().isEmpty());
        QVERIFY(!matchCase->property("checked").toBool());
        QCOMPARE(item("findStyles")->property("text").toString(), QString());

        // Created: AddRecent writes the lists as SetTable does, from
        // FindReplace's own lists; the tab's options at its next save.
        item("findText")->setProperty("editText", QStringLiteral("alpha"));
        item("findReplaceText")->setProperty("editText", QStringLiteral("ALPHA"));
        matchCase->setProperty("checked", true);
        QVERIFY(QMetaObject::invokeMethod(item("replaceAllButton"), "click"));
        QCOMPARE(settings.list("find.recentFinds"), QStringList{QStringLiteral("alpha")});
        QCOMPARE(settings.list("find.recentReplacements"), QStringList{QStringLiteral("ALPHA")});
        QVERIFY(!settings.contains("findInSubs.recentFilters")); // only the Find in subtitles tab writes them
        QCOMPARE(settings.integer("find.options") & 1, 1);
        settings.sync();
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value(QStringLiteral("profile/find.recentFinds")).toStringList(),
                 QStringList{QStringLiteral("alpha")});
        application->resetSettings({});
        QVERIFY(!settings.contains("find.options"));
        QVERIFY(!settings.contains("find.recentFinds"));
        QVERIFY(matchCase->property("checked").toBool()); // the dialog's controls stay
        // Ctrl+F: SaveValues writes the tab left, SetValues reads it back;
        // the recent lists are FindReplace's, not the reset options.
        press(Qt::Key_F, Qt::ControlModifier);
        QTRY_COMPARE(tool->property("tab").toInt(), 0);
        QCOMPARE(settings.integer("find.options") & 1, 1);
        QVERIFY(matchCase->property("checked").toBool());
        QCOMPARE(tool->property("finds").toStringList(), QStringList{QStringLiteral("alpha")});
        QVERIFY(!settings.contains("find.recentFinds"));
        // The next AddRecent (the Find tab): the finds only.
        item("findText")->setProperty("editText", QStringLiteral("beta"));
        application->resetFindReplace(); // a Lines button: the search starts over (fromstart)
        QVERIFY(QMetaObject::invokeMethod(item("findButton"), "click"));
        QCOMPARE(settings.list("find.recentFinds"), (QStringList{QStringLiteral("beta"), QStringLiteral("alpha")}));
        QVERIFY(!settings.contains("find.recentReplacements"));

        // The Multireplacer, once shown, holds its rules.
        auto *root = engine->rootObjects().first();
        auto *misspell = root->findChild<QObject *>(QStringLiteral("misspellDialog"));
        QVERIFY(QMetaObject::invokeMethod(misspell, "openDialog"));
        QTRY_VERIFY(misspell->property("opened").toBool());
        QVERIFY(application->addMisspellRule({{QStringLiteral("description"), QStringLiteral("Owner rule")},
                                              {QStringLiteral("find"), QStringLiteral("zz")},
                                              {QStringLiteral("replace"), QStringLiteral("z")},
                                              {QStringLiteral("options"), 0}}));
        const QString rules = home.filePath(QStringLiteral("Rules.txt"));
        QVERIFY(!QFile::exists(rules));

        // A changed program font: DestroyDialogs.
        matchCase->setProperty("checked", false);
        application->resetSettings({});
        QSignalSpy findDestroyed(application, &app::Application::findReplaceDestroyed);
        QSignalSpy misspellDestroyed(application, &app::Application::misspellReplacerDestroyed);
        application->applySettings({{QStringLiteral("program.font"), QStringLiteral("Hikari Test Face")}});
        QCOMPARE(findDestroyed.count(), 1);
        QCOMPARE(misspellDestroyed.count(), 1);
        // FR->SaveOptions(): the tab's options are written; the tool is gone.
        QVERIFY(settings.contains("find.options"));
        QCOMPARE(settings.integer("find.options") & 1, 0);
        QTRY_VERIFY(!dock->property("isOpen").toBool());
        QVERIFY(!tool->property("opened").toBool());
        // ~MisspellReplacer wrote the rules; the window is closed and built
        // again (empty fields, centred) from Rules.txt at the next opening.
        QTRY_VERIFY(!misspell->property("visible").toBool());
        QVERIFY(!misspell->property("placed").toBool());
        QVERIFY(QFile::exists(rules));
        QVERIFY(application->misspellRules().last().toMap().value(QStringLiteral("description")).toString()
                == QStringLiteral("Owner rule"));
        // The next opening reads the recent lists again (reset to the defaults
        // before the destruction, so empty) and the stored options.
        press(Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(dock->property("isOpen").toBool());
        QVERIFY(tool->property("finds").toStringList().isEmpty());
        QVERIFY(!matchCase->property("checked").toBool());
        application->editor().discard();
    }

    // O1 with F3 and A1: the spelling and audio options are registry
    // settings the Options dialog writes. SetOptions acts on the spell
    // checker as legacy does (ClearErrs(true, value), SpellChecker::Destroy);
    // Set default leaves the Line editor's own switch; the audio box reads
    // its options when it opens.
    void spellingAndAudioOptionsComeFromTheRegistry()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        writeDictionary(home.path());
        restartWithSpelling(home.path(), true);
        auto &settings = *application->settingsStore();
        const QString ini = home.filePath(QStringLiteral("hikari.ini"));
        const QString path = writeFile(dir, "options-spelling.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,wrold\n");
        QVERIFY(application->openFile(path));
        auto marks = [&] { return application->shell().lines()->index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(); };
        QCOMPARE(marks(), (QVariantList{0, 4}));
        QCOMPARE(application->editorSpellingMarks(0), (QVariantList{0, 5}));
        QSignalSpy spelling(application, &app::Application::spellingChanged);
        application->applySettings({{QStringLiteral("editor.spellchecker"), false}});
        QVERIFY(spelling.count() > 0);
        QVERIFY(!application->spellingOn());
        QCOMPARE(marks(), QVariantList{});
        QCOMPARE(application->editorSpellingMarks(0), QVariantList{});
        settings.sync();
        QCOMPARE(QSettings(ini, QSettings::IniFormat).value(QStringLiteral("profile/editor.spellchecker")).toBool(), false);
        application->applySettings({{QStringLiteral("editor.spellchecker"), true}});
        QCOMPARE(marks(), (QVariantList{0, 4}));
        QCOMPARE(application->editorSpellingMarks(0), (QVariantList{0, 5}));
        application->applySettings({{QStringLiteral("editor.suggestionsOnDoubleClick"), true}});
        QVERIFY(application->suggestionsOnDoubleClick());
        // Set default: SPELLCHECKER_ON is on again for the menus and the
        // Grid, but the Line editor keeps the switch the menu turned off.
        application->setSpellingOn(false);
        QCOMPARE(application->editorSpellingMarks(0), QVariantList{});
        application->resetSettings({});
        QVERIFY(application->spellingOn());
        QVERIFY(!application->suggestionsOnDoubleClick());
        QCOMPARE(application->editorSpellingMarks(0), QVariantList{});
        // The menu turns both on again.
        application->setSpellingOn(true);
        QCOMPARE(application->editorSpellingMarks(0), (QVariantList{0, 5}));

        // AUDIO_RAM_CACHE, AUDIO_DELAY, AUDIO_CACHE_FILES_LIMIT and
        // ACCEPTED_AUDIO_STREAM as the next open reads them, legacy defaults first.
        auto audio = application->audioSettings();
        QVERIFY(!audio.ram);
        QCOMPARE(audio.delayMs, 0);
        QCOMPARE(audio.cacheFilesLimit, 10);
        QVERIFY(audio.acceptedStreams.empty());
        application->applySettings({{QStringLiteral("audio.ramCache"), true},
                                    {QStringLiteral("audio.delay"), -250},
                                    {QStringLiteral("audio.cacheFilesLimit"), 3},
                                    {QStringLiteral("video.acceptedAudioStream"), QStringLiteral("jpn;eng")}});
        audio = application->audioSettings();
        QVERIFY(audio.ram);
        QCOMPARE(audio.delayMs, -250);
        QCOMPARE(audio.cacheFilesLimit, 3);
        QCOMPARE(audio.acceptedStreams, (std::vector<std::string>{"jpn", "eng"}));
        settings.sync();
        const QSettings stored(ini, QSettings::IniFormat);
        QCOMPARE(stored.value(QStringLiteral("profile/audio.delay")).toInt(), -250);
        QVERIFY(!stored.contains(QStringLiteral("Audio/Delay")));
        application->editor().discard();
    }

    // A1: GLOBAL_OPEN_AUDIO through the real media helper. The audio-only
    // fixture's stereo samples mix down as legacy GetBuffer does, and the
    // waveform's zoomed-out columns cover whole 256-sample peak blocks
    // (legacy GetWaveForm at the default zoom 50: 1440 samples a column).
    void audioBoxOpensAFileAndShowsItsWaveform()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 and 3.00-4.00
        auto &audio = application->audio();
        QVERIFY(item("audioStatus")->isVisible());
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QVERIFY(audio.hasAudio());
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(item("audioDisplay")->isVisible());
        QVERIFY(!item("audioStatus")->isVisible());
        // legacy EditBox::LoadAudio focuses a new box's display
        QCOMPARE(focusedPanel(), QStringLiteral("audioPanel"));
        const auto *pcm = audio.box().audio();
        QCOMPARE(pcm->sampleRate(), 48000);
        QCOMPARE(pcm->sampleCount(), std::int64_t(96256));
        auto mono = [](std::int64_t i) {
            const auto v = static_cast<std::int16_t>(((i * 37) % 65536) - 32768);
            return static_cast<std::int16_t>((v + v / 2) / 2);
        };
        for (std::int64_t i : {0, 1, 885, 50000, 96255}) {
            std::int16_t got = 0;
            pcm->read(i, 1, &got);
            QCOMPARE(got, mono(i));
        }
        const auto &view = audio.view();
        QCOMPARE(view.samples(), 1440);
        QVERIFY(view.width() > 10);
        const int h = view.height();
        auto toY = [h](int sample) {
            const int y = h / 2 - (sample * (h / 2)) / 0x8000;
            return std::clamp(y, 0, h);
        };
        const auto &columns = audio.columns();
        for (int c : {0, 1, 5}) {
            const std::int64_t first = c * 1440 / 256 * 256;
            const std::int64_t last = std::min<std::int64_t>((c * 1440 + 1439) / 256 * 256 + 256, 96256);
            int lo = 32767, hi = -32768;
            for (std::int64_t i = first; i < last; ++i) {
                lo = std::min<int>(lo, mono(i));
                hi = std::max<int>(hi, mono(i));
            }
            QCOMPARE(columns.min[std::size_t(c)], toY(hi));
            QCOMPARE(columns.peak[std::size_t(c)], toY(lo));
        }
        // the same PCM at other zooms (legacy samples per column at 100% and
        // 10%: 2880 and 288): whole peak blocks from 1024 samples a column up,
        // every sample below
        for (const int samples : {2880, 288}) {
            const auto other = application::legacyWaveform(*pcm, 0, 6, h, samples, 1.f);
            for (int c = 0; c < 6; ++c) {
                std::int64_t first = std::int64_t(c) * samples, last = first + samples;
                if (samples >= 1024) {
                    first = first / 256 * 256;
                    last = std::min<std::int64_t>((last - 1) / 256 * 256 + 256, 96256);
                }
                int lo = 32767, hi = -32768;
                for (std::int64_t i = first; i < last; ++i) {
                    lo = std::min<int>(lo, mono(i));
                    hi = std::max<int>(hi, mono(i));
                }
                QCOMPARE(other.min[std::size_t(c)], toY(hi));
                QCOMPARE(other.peak[std::size_t(c)], toY(lo));
            }
        }
        // legacy's default disk cache, named for the track, channels and delay
        const auto cacheFile = audio.box().cacheFile();
        QCOMPARE(QString::fromStdString(cacheFile.filename().string()), QStringLiteral("audioonly_track0_2ch_0.w64"));
        // the active Line's boundaries: 1.00 s is column 33.3
        const auto scene = audio.scene([](application::AudioShape::Font, std::string_view) { return 30; });
        bool startMark = false;
        for (const auto &shape : scene)
            if (shape.kind == application::AudioShape::Kind::Line && shape.colour == audio.options().lineStart &&
                shape.x1 == 34 && shape.width == 2)
                startMark = true;
        QVERIFY(startMark);
        // the mouse over the waveform draws the cursor; over the ruler it does not
        auto *display = item("audioDisplay");
        const QPoint inside = display->mapToScene(QPointF(100, 10)).toPoint();
        // The pointer enters the display first (an enter draws no cursor).
        QTest::mouseMove(window, inside + QPoint(0, 1));
        QTest::mouseMove(window, inside);
        QTRY_VERIFY(audio.cursor().has_value());
        QCOMPARE(*audio.cursor(), 100.f);
        QTest::mouseMove(window, display->mapToScene(QPointF(100, h + 2)).toPoint());
        QTRY_VERIFY(!audio.cursor().has_value());
        // legacy SetRecent(2), then Close audio
        QCOMPARE(application->recentAudio().first().toMap().value(QStringLiteral("path")).toString(),
                 QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        // what the scene graph drew, in whichever renderer this run uses
        // (offscreen: the software adaptation; xvfb: RHI)
        QTest::mouseMove(window, QPoint(0, 0));
        QTRY_VERIFY(!audio.cursor().has_value());
        QCoreApplication::processEvents();
        const QImage drawn = window->grabWindow();
        const QPoint origin = display->mapToScene(QPointF(0, 0)).toPoint();
        auto pixel = [&](int x, int y) { return drawn.pixel(origin + QPoint(x, y)) | 0xFF000000u; };
        QCOMPARE(pixel(34, h / 2), audio.options().lineStart);   // the start boundary, over the waveform
        QCOMPARE(pixel(10, h / 2), audio.options().waveform);    // a column before the Lines
        // above its peak, below the focus border
        QVERIFY(audio.columns().min[10] > 8);
        QCOMPARE(pixel(10, audio.columns().min[10] / 2), audio.options().background);
        // the default arrangement gives the box legacy's AUDIO_BOX_HEIGHT
        // (170: the display with its ruler, the search bar and the button
        // row), so the waveform has most of it
        const qreal box = display->height() + item("audioScroll")->height() + 4 + item("audioButtons")->height() + 2;
        QVERIFY2(std::abs(box - 170) <= 2, qPrintable(QString::number(box))); // fitAudioBox, whatever the fonts
        QVERIFY2(h >= 90, qPrintable(QString::number(h)));
        // legacy D3DXCreateFontW: the cursor time and labels bold, the ruler not
        auto *displayItem = qobject_cast<ui::AudioDisplayItem *>(display);
        QVERIFY(displayItem);
        QVERIFY(displayItem->font(int(application::AudioShape::Font::Cursor)).bold());
        QVERIFY(displayItem->font(int(application::AudioShape::Font::Label)).bold());
        QVERIFY(!displayItem->font(int(application::AudioShape::Font::Scale)).bold());
        auto *close = item<QObject>("closeAudioMenuItem");
        QVERIFY(close->property("enabled").toBool());
        auto part = cacheFile;
        part += ".part";
        QVERIFY(std::filesystem::exists(part));
        audio.closeAudio();
        // a complete cache keeps its name (legacy ~ProviderFFMS2)
        QVERIFY(!std::filesystem::exists(part));
        QCOMPARE(std::filesystem::file_size(cacheFile), std::uintmax_t(96256) * 4);
        QVERIFY(!audio.hasAudio());
        QVERIFY(!close->property("enabled").toBool());
        QCOMPARE(item<QObject>("audioStatus")->property("text").toString(), QStringLiteral("No audio open"));
        // a file legacy cannot index opens nothing, silently
        audio.openAudio(dir.filePath(QStringLiteral("missing.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.hasAudio(), 20000);
        QVERIFY(audio.box().error().has_value());
    }

    // A2: legacy AudioBox's horizontal zoom, AUDIO_SCROLL_LEFT/RIGHT, the
    // wheel, the vertical zoom and volume sliders and their link, and
    // auto-scroll, on blank audio (44.1 kHz): each starts from the registry
    // when a box is made and is written back as it changes.
    void audioBoxZoomScrollAndGain()
    {
        keysNeverRepeat();
        QVERIFY(application->openFile(episode)); // 1.00-2.00 and 3.00-4.00
        auto &audio = application->audio();
        auto &settings = *application->settingsStore();
        QCOMPARE(audio.horizontalZoom(), 50);
        QCOMPARE(audio.verticalZoom(), 50);
        QCOMPARE(audio.volume(), 50);
        QVERIFY(!audio.linked());
        QVERIFY(audio.autoScroll());
        QVERIFY(!audio.spectrumOn());
        QVERIFY(!audio.spectrumNonLinear());
        audio.openDummy();
        QVERIFY(audio.ready());
        QVERIFY(item("audioSliders")->isVisible());
        QVERIFY(item("audioAutoScroll")->isVisible()); // the switches, in the button row
        const auto &view = audio.view();
        QCOMPARE(view.samples(), 1323);

        // the zoom slider: 100% is two minutes over 500 columns (legacy w1)
        audio.setHorizontalZoom(100);
        QCOMPARE(view.samples(), 10584);
        QCOMPARE(settings.integer("audio.horizontalZoom"), 100);
        QCOMPARE(item("audioHorizontalZoom")->property("value").toInt(), 100);

        // A (AUDIO_SCROLL_RIGHT, "Scroll left") and F in the display: 50 columns
        item("audioDisplay")->forceActiveFocus();
        press(Qt::Key_F);
        QCOMPARE(view.position(), 50);
        press(Qt::Key_F);
        QCOMPARE(view.position(), 100);
        press(Qt::Key_A);
        QCOMPARE(view.position(), 50);
        press(Qt::Key_A);
        press(Qt::Key_A);
        QCOMPARE(view.position(), 0);
        // and in the Grid (legacy TabPanel::SetAccels gives it the audio keys)
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_F);
        QCOMPARE(view.position(), 50);
        press(Qt::Key_A);
        QCOMPARE(view.position(), 0);

        // zooming keeps the view's centre (legacy SetSamplesPercent, pivot 0.5)
        audio.setScrollPosition(83); // the search bar's units are 12 columns
        QCOMPARE(view.position(), 996);
        audio.setHorizontalZoom(50);
        QCOMPARE(view.samples(), 1323);
        QCOMPARE(view.position(), (996LL * 10584 + (10584 - 1323) * 250) / 1323);

        // the wheel: a notch scrolls a third of the display, Shift zooms
        // around the mouse, and the zoom setting keeps a value past the slider
        const int w = view.width();
        std::int64_t before = view.position();
        audio.wheel(-120);
        QCOMPARE(view.position(), before + 120 * w / 360);
        before = view.positionSample();
        audio.wheel(120, false, true, 0); // zoom in one step, pivot at column 0
        QCOMPARE(audio.horizontalZoom(), 49);
        QCOMPARE(view.samples(), int(int(44100 * 120 / 500) * std::pow(0.49, 3)));
        QCOMPARE(view.positionSample() / view.samples(), view.position());
        QCOMPARE(view.position(), before / view.samples());
        audio.setHorizontalZoom(100);
        audio.wheel(-120, false, true, 0);
        QCOMPARE(audio.horizontalZoom(), 100);
        QCOMPARE(settings.integer("audio.horizontalZoom"), 101);
        QCOMPARE(view.samples(), 10584);
        // AUDIO_WHEEL_DEFAULT_TO_ZOOM swaps scrolling and zooming
        settings.set("audio.wheelDefaultToZoom", true);
        audio.wheel(120);
        QCOMPARE(audio.horizontalZoom(), 99);
        before = view.position();
        audio.wheel(-120, false, true, 0);
        QCOMPARE(audio.horizontalZoom(), 99);
        QCOMPARE(view.position(), before + 120 * w / 360);
        settings.set("audio.wheelDefaultToZoom", false);

        // Ctrl and the wheel over the display: the vertical zoom slider
        auto *display = item("audioDisplay");
        const QPointF at = display->mapToScene(QPointF(100, 10));
        QWheelEvent wheel(at, window->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QCOMPARE(audio.verticalZoom(), 51);
        QCOMPARE(view.scale(), application::audioScaleFromSlider(51));
        QCOMPARE(settings.integer("audio.verticalZoom"), 51);
        QCOMPARE(audio.volume(), 50); // not linked
        // a horizontal wheel is the vertical one (legacy reads the rotation,
        // not the axis): a tilt to the right, +120 to Windows and -120 in
        // Qt's x, scrolls back a third of the display; Ctrl with it is the
        // vertical zoom again
        audio.setScrollPosition(83);
        before = view.position();
        QWheelEvent right(at, window->mapToGlobal(at), QPoint(), QPoint(-120, 0), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &right);
        QCOMPARE(view.position(), before - 120 * w / 360);
        QWheelEvent left(at, window->mapToGlobal(at), QPoint(), QPoint(120, 0), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &left);
        QCOMPARE(view.position(), before);
        QWheelEvent ctrlRight(at, window->mapToGlobal(at), QPoint(), QPoint(-120, 0), Qt::NoButton,
                              Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &ctrlRight);
        QCOMPARE(audio.verticalZoom(), 52);
        audio.setVerticalZoom(51);

        // linking moves the volume to the vertical zoom; each then moves both
        QSignalSpy volumes(&audio, &ui::AudioController::volumeChanged);
        audio.setLinked(true);
        QCOMPARE(audio.volume(), 51);
        QCOMPARE(settings.integer("audio.volume"), 51);
        QVERIFY(settings.boolean("audio.link"));
        audio.setVolume(30);
        QCOMPARE(audio.verticalZoom(), 30);
        QCOMPARE(view.scale(), 0.216f);
        QCOMPARE(audio.playbackVolume(), 0.216f);
        QCOMPARE(settings.integer("audio.verticalZoom"), 30);
        audio.setVerticalZoom(80);
        QCOMPARE(audio.volume(), 80);
        QCOMPARE(audio.playbackVolume(), 1.3f); // the player's ramp above 50
        QCOMPARE(volumes.size(), 3);
        QCOMPARE(volumes.last().first().toFloat(), 1.3f);
        QCOMPARE(item("audioVolume")->property("value").toInt(), 80);
        audio.wheel(1200, true); // ten notches, the slider stops at 100
        QCOMPARE(audio.verticalZoom(), 90);
        QCOMPARE(audio.volume(), 90);
        audio.wheel(1200, true);
        QCOMPARE(audio.verticalZoom(), 100);
        audio.setVerticalZoom(80);

        // a new box starts from the settings: a zoom stored past the
        // slider's range, and a linked volume takes the vertical zoom's value
        settings.set("audio.horizontalZoom", 101);
        settings.set("audio.volume", 10);
        audio.closeAudio();
        audio.openDummy();
        QVERIFY(audio.ready());
        QCOMPARE(audio.horizontalZoom(), 100);
        QCOMPARE(audio.view().samples(), 10584);
        QCOMPARE(audio.volume(), 80);
        QCOMPARE(settings.integer("audio.volume"), 80);
        QCOMPARE(audio.view().scale(), application::audioScaleFromSlider(80));
        audio.setLinked(false);
        audio.setVolume(20);
        QCOMPARE(audio.verticalZoom(), 80);
        QCOMPARE(settings.integer("audio.volume"), 20);

        // auto-scroll (AUDIO_AUTO_SCROLL): another active Line is brought into view
        audio.setHorizontalZoom(10); // 10 samples a column: 3.00 is column 13230
        QCOMPARE(audio.view().samples(), 10);
        audio.setScrollPosition(0);
        auto *grid = item("editingGrid");
        grid->forceActiveFocus();
        press(Qt::Key_Down);
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("second"));
        // legacy MakeDialogueVisible: the start 50 columns from the left
        const auto followed = audio.view().position();
        QCOMPARE(followed, (132300 - 50 * 10) / 10);
        audio.setAutoScroll(false);
        QVERIFY(!settings.boolean("audio.autoScroll"));
        QVERIFY(!item<QObject>("audioAutoScroll")->property("checked").toBool());
        grid->forceActiveFocus();
        press(Qt::Key_Up);
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(audio.view().position(), followed);
        audio.setAutoScroll(true);
        application->editor().discard();
    }

    // A2: spectrum mode (AUDIO_SPECTRUM_ON) and its speech layout
    // (AUDIO_SPECTRUM_NON_LINEAR_ON): legacy RenderRange's picture over the
    // background in place of the waveform, drawn as it is by whichever
    // renderer this run uses.
    void audioSpectrumIsDrawn()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        auto &settings = *application->settingsStore();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        auto *toggle = item("audioSpectrumMode");
        QVERIFY(!toggle->property("checked").toBool());
        // the switch as a click toggles it (not hovered: a tooltip's popup
        // window upsets offscreen RHI grabs)
        QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
        QVERIFY(QMetaObject::invokeMethod(toggle, "toggled"));
        QTRY_VERIFY(audio.spectrumOn());
        QVERIFY(toggle->property("checked").toBool());
        QVERIFY(settings.boolean("audio.spectrumOn"));
        QVERIFY(item("audioDisplay")->hasActiveFocus()); // legacy OnSpectrumMode focuses the display
        const auto picture = audio.spectrumImage();
        QVERIFY(picture);
        const auto &view = audio.view();
        QCOMPARE(picture->width, view.width());
        QCOMPARE(picture->height, view.height());
        // the same view draws the same picture again without new transforms
        const auto lines = audio.spectrum()->transformedLines();
        QVERIFY(lines > 0);
        QCOMPARE(audio.spectrumImage(), picture);
        // zoom 50: 1440 samples a column, two transforms per 2048 samples
        QCOMPARE(audio.spectrum()->overlaps(), 2);
        // a fresh render of the view gives the same pixels
        application::AudioSpectrum fresh(1);
        std::vector<std::uint8_t> again(picture->bgra.size(), 0);
        fresh.render(*audio.box().audio(), 0, std::int64_t(view.width()) * 1440, again.data(), view.width(),
                     view.width(), view.height(), 50);
        for (std::size_t i = 0; i < again.size(); i += 4)
            QVERIFY(std::equal(again.begin() + i, again.begin() + i + 3, picture->bgra.begin() + i));

        // the scene: the picture, no waveform columns
        const auto scene = audio.scene([](application::AudioShape::Font, std::string_view) { return 30; });
        QCOMPARE(scene.at(1).kind, application::AudioShape::Kind::Image);
        // what the scene graph drew, before the active Line (column 10, 20)
        QTest::mouseMove(window, QPoint(0, 0));
        QTRY_VERIFY(!audio.cursor().has_value());
        QCoreApplication::processEvents();
        auto *display = item("audioDisplay");
        const QPoint origin = display->mapToScene(QPointF(0, 0)).toPoint();
        const int h = view.height();
        auto expected = [&](int x, int y) {
            const std::uint8_t *p = picture->bgra.data() + (std::size_t(y) * picture->width + x) * 4;
            return qRgb(p[2], p[1], p[0]);
        };
        int lit = 0;
        auto drawnAsRendered = [&] {
            const QImage drawn = window->grabWindow();
            lit = 0;
            for (int x : {10, 20})
                for (int y : {h / 8, h / 4, h / 2, 3 * h / 4, h - 4}) {
                    if ((drawn.pixel(origin + QPoint(x, y)) | 0xFF000000u) != expected(x, y))
                        return false;
                    lit += expected(x, y) != qRgb(0, 0, 0);
                }
            return true;
        };
        QTRY_VERIFY(drawnAsRendered()); // a grab can come before the window is ready again
        QVERIFY(lit > 0); // the fixture's sawtooth is broadband

        // the speech layout spreads the low bands over more rows
        audio.setSpectrumNonLinear(true);
        QVERIFY(settings.boolean("audio.spectrumNonLinearOn"));
        const auto speech = audio.spectrumImage();
        QVERIFY(speech != picture);
        QVERIFY(speech->bgra != picture->bgra);
        QCOMPARE(audio.spectrum()->transformedLines(), lines); // same transforms, other rows

        // the vertical zoom scales the power (legacy SetScaling)
        audio.setVerticalZoom(100);
        QVERIFY(audio.spectrumImage()->bgra != speech->bgra);
        QCOMPARE(audio.spectrum()->transformedLines(), lines);

        // off again: the waveform
        audio.setSpectrumOn(false);
        QVERIFY(!settings.boolean("audio.spectrumOn"));
        const auto waveform = audio.scene([](application::AudioShape::Font, std::string_view) { return 30; });
        for (const auto &shape : waveform)
            QVERIFY(shape.kind != application::AudioShape::Kind::Image);
        audio.setVerticalZoom(50);
        audio.setSpectrumNonLinear(false);
        audio.closeAudio();
        application->editor().discard();
    }

    // GLOBAL_OPEN_DUMMY_AUDIO: 2 h 30 min of 44.1 kHz silence, ready at once;
    // its name joins the recent list, which drops it as a missing file.
    void blankAudio()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        audio.openDummy();
        QVERIFY(audio.ready());
        QCOMPARE(audio.box().audio()->sampleCount(), std::int64_t(396'900'000));
        QCOMPARE(audio.view().samples(), 1323);
        for (int peak : audio.columns().peak)
            QCOMPARE(peak, audio.view().height() / 2);
        QVERIFY(application->recentAudio().isEmpty());
    }

    // A3: timing in the audio box over blank audio (44.1 kHz at zoom 50:
    // 1323 samples, 30 ms a column, the view at 0): legacy OnMouseEvent's
    // boundary drag, AudioBox's hotkeys (AUDIO_LEAD_IN C, AUDIO_LEAD_OUT V,
    // AUDIO_COMMIT Enter, AUDIO_PREVIOUS_ALT Z, AUDIO_NEXT_ALT X) with
    // AUDIO_AUTO_COMMIT on and off, the ruler's mark and
    // GLOBAL_SET_AUDIO_MARK_FROM_VIDEO; each commit one named step.
    // Z and X play the new Line, so the session's player has no device: a
    // host without one (Windows CI) fails the play, and the log window it
    // pops up takes the focus from the audio box.
    void audioTimingByMouseAndKeys()
    {
        restartWithoutSound();
        keysNeverRepeat(); // O2's CheckLastKeyEvent: fast runners press within 100 ms
        QVERIFY(application->openFile(episode)); // 1.00-2.00 and 3.00-4.00
        auto &audio = application->audio();
        auto &editor = application->editor();
        audio.openDummy();
        QVERIFY(audio.ready());
        QCOMPARE(audio.selectionStart(), 1000);
        QCOMPARE(audio.view().position(), 0);
        auto *display = item("audioDisplay");
        const int h = audio.view().height();
        auto at = [&](int x, int y = 10) { return display->mapToScene(QPointF(x, y)).toPoint(); };
        const auto steps = editor.history().size();
        auto lastStep = [&] { return editor.history().last(); };

        // the end (column 66) dragged to column 100: one step
        QTest::mousePress(window, Qt::LeftButton, {}, at(66));
        QCOMPARE(audio.timing().hold(), 2);
        QTest::mouseMove(window, at(100));
        QCOMPARE(audio.selectionEnd(), 3000);
        QVERIFY(audio.modified());
        QCOMPARE(display->cursor().shape(), Qt::SizeHorCursor);
        QTest::mouseRelease(window, Qt::LeftButton, {}, at(100));
        QCOMPARE(editor.history().size(), steps + 1);
        QCOMPARE(lastStep(), QStringLiteral("Changing time on audio spectrum, active line 1"));
        QCOMPARE(editor.endText(), QStringLiteral("0:00:03.00"));
        QVERIFY(!audio.modified());
        QVERIFY(display->hasActiveFocus());

        // the keys: lead-in and lead-out commit, Enter goes to the next Line
        QTest::keyClick(window, Qt::Key_C);
        QCOMPARE(editor.startText(), QStringLiteral("0:00:00.80"));
        QTest::keyClick(window, Qt::Key_V);
        QCOMPARE(editor.endText(), QStringLiteral("0:00:03.30"));
        QCOMPARE(editor.history().size(), steps + 3);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(editor.history().size(), steps + 3); // nothing left to commit
        QCOMPARE(editor.text(), QStringLiteral("second"));
        QCOMPARE(audio.selectionStart(), 3000);
        QTest::keyClick(window, Qt::Key_Z);
        QCOMPARE(editor.text(), QStringLiteral("first"));
        QCOMPARE(audio.selectionStart(), 800);
        QTest::keyClick(window, Qt::Key_X);
        QCOMPARE(editor.text(), QStringLiteral("second"));

        // the ruler's right click sets the mark
        QTest::mouseClick(window, Qt::RightButton, {}, at(200, h + 5));
        QVERIFY(audio.hasMark());
        QCOMPARE(audio.markMs(), 6000);
        QCOMPARE(editor.history().size(), steps + 3);
        // GLOBAL_SET_AUDIO_MARK_FROM_VIDEO without video: VideoBox::Tell is 0
        application->setAudioFromVideo(true);
        QCOMPARE(audio.markMs(), 0);
        QCOMPARE(audio.view().position(), 0);

        // without AUDIO_AUTO_COMMIT the times wait in the editor until Enter
        auto *autoCommit = item<QObject>("audioAutoCommit");
        QVERIFY(autoCommit->property("checked").toBool());
        application->settingsStore()->setValue(QStringLiteral("audio.autoCommit"), false);
        QVERIFY(!autoCommit->property("checked").toBool());
        QTest::mousePress(window, Qt::LeftButton, {}, at(100));
        QCOMPARE(audio.timing().hold(), 1);
        QTest::mouseMove(window, at(90));
        QTest::mouseRelease(window, Qt::LeftButton, {}, at(90));
        QCOMPARE(editor.history().size(), steps + 3);
        QVERIFY(audio.modified());
        QCOMPARE(editor.startText(), QStringLiteral("0:00:02.70"));
        QTest::keyClick(window, Qt::Key_Return);
        // the last Line: the commit and the appended Line are one step
        QCOMPARE(editor.history().size(), steps + 4);
        QCOMPARE(lastStep(), QStringLiteral("Adding a new line, active line 2"));
        QCOMPARE(editor.startText(), QStringLiteral("0:00:04.00"));
        QCOMPARE(editor.endText(), QStringLiteral("0:00:09.00"));
        QVERIFY(!audio.modified());
        // Undo takes both back
        QVERIFY(editor.undo());
        QCOMPARE(editor.history().size(), steps + 4);
        QCOMPARE(editor.historyCursor(), int(steps + 2));
        // with a mark the Timing tool may move the marker to audio time
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_I, Qt::ControlModifier);
        QTRY_VERIFY(item<QObject>("shiftToAudio"));
        QVERIFY(item<QObject>("shiftToAudio")->property("enabled").toBool());
        audio.closeAudio();
        QVERIFY(!item<QObject>("shiftToAudio")->property("enabled").toBool());
    }

    // GLOBAL_AUDIO_FROM_VIDEO and legacy RendererFFMS2::OpenFile: a video with
    // audio brings it into the box (sample 0 at the first frame), a video
    // without closes it; the box marks the video's keyframes and paused frame.
    void audioFollowsTheVideo()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        auto *fromVideo = item<QObject>("audioFromVideoMenuItem");
        QVERIFY(!fromVideo->property("enabled").toBool());
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audiodelay.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(fromVideo->property("enabled").toBool());
        QCOMPARE(audio.box().audio()->sampleCount(), std::int64_t(96256 + 24000));
        QCOMPARE(audio.path(), nativeFixture("audiodelay.mkv"));
        // CFR keyframes every 12 frames at 24000/1001: the legacy timebase's ms
        const auto timebase = application->video().session().legacyTimebase();
        QTRY_VERIFY(!audio.marks().keyframesMs.empty());
        QCOMPARE(audio.marks().keyframesMs.front(), 0);
        QCOMPARE(audio.marks().keyframesMs[1], timebase.msAt(12));
        QTRY_COMPARE(audio.marks().videoMs.value_or(-1),
                     timebase.msAt(application->video().session().shownFrame().value_or(-1)));
        // the video's track, opened in the box's own helper from the video's index file
        QVERIFY(audio.box().fromVideo());
        // drawn at ((ms - 20) / 10) * 10 (legacy DrawKeyframes): frames 0, 12,
        // 24 and 36 at 0, 500.5, 1001 and 1501.5 ms, 1440 samples (30 ms) a column
        QCOMPARE(audio.view().position(), 0);
        std::vector<float> drawn;
        for (const auto &shape : audio.scene([](application::AudioShape::Font, std::string_view) { return 30; }))
            if (shape.kind == application::AudioShape::Kind::Line && shape.colour == audio.options().keyframe)
                drawn.push_back(shape.x1);
        QCOMPARE(drawn, (std::vector<float>{0, 16, 32, 49}));
        // video frames redraw the box only when a mark moved (legacy redraws
        // the audio on its own events, not the video's)
        QSignalSpy redraws(&audio, &ui::AudioController::displayChanged);
        for (int i = 0; i < 5; ++i)
            emit application->video().changed();
        QCOMPARE(redraws.count(), 0);
        const int shown = application->video().session().shownFrame().value_or(-1);
        QVERIFY(application->video().stepFrames(1));
        QTRY_COMPARE(application->video().session().shownFrame().value_or(-1), shown + 1);
        QCOMPARE(audio.marks().videoMs.value_or(-1), timebase.msAt(shown + 1));
        QCOMPARE(redraws.count(), 1);
        // another file in the same box draws its own waveform
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        const auto &view = audio.view();
        const auto expected = application::legacyWaveform(*audio.box().audio(), view.position() * view.samples(),
                                                          view.width(), view.height(), view.samples(), view.scale());
        QCOMPARE(audio.columns().min, expected.min);
        QCOMPARE(audio.columns().peak, expected.peak);
        // Open audio from video opens the file again
        audio.closeAudio();
        application->openAudioFromVideo();
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        // a video without audio closes the box
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.hasAudio(), 20000);
    }

    // A1 marks on VFR media: keyframes at the legacy timebase's ms, drawn at
    // ((ms - 20) / 10) * 10 (legacy DrawKeyframes).
    void keyframeMarksOnVfrVideo()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/vfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        audio.openDummy();
        const auto timebase = application->video().session().legacyTimebase();
        std::vector<int> expected;
        for (int frame : application->video().session().keyframes())
            expected.push_back(timebase.msAt(frame));
        QTRY_COMPARE(audio.marks().keyframesMs, expected);
        QVERIFY(expected.size() > 1);
        const auto scene = audio.scene([](application::AudioShape::Font, std::string_view) { return 30; });
        std::vector<float> drawn;
        for (const auto &shape : scene)
            if (shape.kind == application::AudioShape::Kind::Line && shape.colour == audio.options().keyframe)
                drawn.push_back(shape.x1);
        const int last = audio.view().msAtX(audio.view().width());
        std::vector<float> legacy;
        for (int ms : expected)
            if (ms >= audio.view().msAtX(0) && ms <= last)
                legacy.push_back(float(static_cast<int>(audio.view().xAtMs(((ms - 20) / 10) * 10))));
        QCOMPARE(drawn, legacy);
        // by hand: keyframes 0, 12, 24 and 36 start at 0, 600, 1200 and 1800 ms
        // (durations 30, 50, 70 ms); at 44.1 kHz and 1323 samples (30 ms) a
        // column, -20, 580, 1180 and 1780 ms fall in columns 0, 19, 39 and 59
        QCOMPARE(expected, (std::vector<int>{0, 600, 1200, 1800}));
        QCOMPARE(audio.view().position(), 0);
        QCOMPARE(drawn, (std::vector<float>{0, 19, 39, 59}));
    }

    // Legacy ProviderFFMS2::Init with several audio tracks: "Choose the
    // track" lists them; the chosen one opens. For a video it asks before the
    // video is indexed: Cancel fails the open and the open video stays
    // (legacy "safe mode"); a chosen track is the video's, the box's and
    // general playback's.
    void audioTrackChooser()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/tracks.mkv"));
        auto *chooser = item<QObject>("audioTrackChooser");
        QTRY_VERIFY_WITH_TIMEOUT(chooser->property("opened").toBool(), 20000);
        QCOMPARE(audio.trackChoices(), (QStringList{QStringLiteral("1: Main [eng] (pcm_s16le)"),
                                                    QStringLiteral("2: Commentary [jpn] (pcm_s16le)")}));
        auto *list = item<QObject>("audioTrackList");
        QCOMPARE(list->property("currentIndex").toInt(), 0); // legacy SetSelection(0)
        list->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(chooser, "accept");
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QCOMPARE(audio.box().track(), 2);
        QCOMPARE(audio.box().audio()->peaks() != nullptr, true);
        std::int16_t s = -1;
        audio.box().audio()->read(1000, 1, &s);
        QCOMPARE(s, std::int16_t(0)); // the second track is silence
        // a video's tracks: asked before the open video is replaced
        auto &video = application->video();
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/tracks.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(chooser->property("opened").toBool(), 20000);
        QCOMPARE(video.session().state(), application::VideoSession::State::Ready);
        QCOMPARE(QString::fromStdString(video.session().path()), nativeFixture("cfr.mkv"));
        QMetaObject::invokeMethod(chooser, "reject");
        QTRY_VERIFY(!chooser->property("opened").toBool());
        QVERIFY(audio.trackChoices().isEmpty());
        QTest::qWait(200);
        QVERIFY(video.hasVideo()); // the open failed; the open video stays
        QCOMPARE(QString::fromStdString(video.session().path()), nativeFixture("cfr.mkv"));
        // the chosen track goes to the video, its playback and the box
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/tracks.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(chooser->property("opened").toBool(), 20000);
        list->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(chooser, "accept");
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), nativeFixture("tracks.mkv"), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QCOMPARE(video.session().audioTrack(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready() && audio.box().fromVideo(), 20000);
        QCOMPARE(audio.box().track(), 2);
        QVERIFY(audio.trackChoices().isEmpty()); // asked once, for the video
    }

    // A1: legacy's index files (Indices/<name>_<track>.ffindex). The video's
    // open writes one; the box opens the video's audio from it in its own
    // helper. On the next open of the video the index is read back, so the
    // box reads its complete cache file back instead of decoding (legacy
    // DiskCache(newIndex)). Opening files with a video open trims the cache
    // folder to AUDIO_CACHE_FILES_LIMIT (10) by last access, never the cache
    // in use.
    void indexFilesLetTheBoxReuseItsCache()
    {
        delete engine;
        engine = nullptr;
        delete application;
        QTemporaryDir folder;
        app::Application::Options options;
        options.indexDir = folder.filePath(QStringLiteral("Indices"));
        options.audioCacheDir = folder.filePath(QStringLiteral("AudioCache"));
        application = new app::Application(options);
        const QString clip = folder.filePath(QStringLiteral("clip.mkv"));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"), clip));
        auto &audio = application->audio();
        auto &video = application->video();
        video.openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(video.session().newIndex());
        QCOMPARE(video.session().audioTrack(), 1);
        QVERIFY(QFileInfo::exists(folder.filePath(QStringLiteral("Indices/clip_1.ffindex"))));
        QVERIFY(audio.box().fromVideo());
        QVERIFY(!audio.box().cacheReused());
        // a video without audio closes the box; the complete cache keeps its name
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.hasAudio(), 20000);
        const QString cache = folder.filePath(QStringLiteral("AudioCache/clip_track1_2ch_0.w64"));
        QVERIFY(QFileInfo::exists(cache));
        video.openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(!video.session().newIndex());
        QVERIFY(audio.box().cacheReused());
        QCOMPARE(audio.box().cacheFile(), std::filesystem::path(cache.toStdU16String())); // either separator
        // eleven older caches beside it, the one in use read longest ago
        const auto setAccessed = [](const QString &path, const QDateTime &when) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(when, QFileDevice::FileAccessTime));
        };
        const QDateTime base = QDateTime::currentDateTime().addDays(-30);
        for (int i = 0; i < 11; ++i) {
            const QString old = folder.filePath(QStringLiteral("AudioCache/old%1.w64").arg(i));
            QFile f(old);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
            f.close();
            setAccessed(old, base.addDays(i));
        }
        setAccessed(cache, base.addDays(-1));
        application->openDropped({}); // legacy OpenFiles ends with DeleteAudioCache
        QVERIFY(QFileInfo::exists(cache));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("AudioCache/old0.w64"))));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("AudioCache/old1.w64"))));
        QVERIFY(QFileInfo::exists(folder.filePath(QStringLiteral("AudioCache/old2.w64"))));
        QCOMPARE(QDir(folder.filePath(QStringLiteral("AudioCache"))).entryList(QDir::Files).size(), 10);
        // P9: one dropped file is OpenFile alone (HikariSubFrame.cpp:1856-1859),
        // without OpenFiles' DeleteAudioCache. OpenFile's (1407-1426) acts
        // with a video open after its LoadVideo: dropped keyframes return
        // before it (1335-1341) and trim nothing, though a video is open;
        // dropped subtitles trim nothing when dropped, and leave the tab
        // without a video (P6); a dropped video trims once it is ready.
        const auto cacheCount = [&] {
            return QDir(folder.filePath(QStringLiteral("AudioCache"))).entryList(QDir::Files).size();
        };
        for (int i = 0; i < 2; ++i) {
            const QString old = folder.filePath(QStringLiteral("AudioCache/older%1.w64").arg(i));
            QFile f(old);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
            f.close();
            setAccessed(old, base.addDays(-10 + i));
        }
        QCOMPARE(cacheCount(), 12);
        const QString keyframes = folder.filePath(QStringLiteral("drop_keyframes.txt"));
        {
            QFile k(keyframes);
            QVERIFY(k.open(QIODevice::WriteOnly));
            k.write("# keyframe format v1\nfps 0\n0\n24\n");
        }
        QCOMPARE(video.session().state(), application::VideoSession::State::Ready);
        QCOMPARE(application->openDropped({QUrl::fromLocalFile(keyframes)}).value(QStringLiteral("kind")).toString(),
                 QString());
        QCOMPARE(cacheCount(), 12);
        const QString subs = folder.filePath(QStringLiteral("subs/ep.ass"));
        QVERIFY(QDir().mkpath(folder.filePath(QStringLiteral("subs"))));
        {
            QFile f(subs);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\n"
                    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ep\n");
        }
        QVariantMap dropped = application->openDropped({QUrl::fromLocalFile(subs)});
        QCOMPARE(dropped.value(QStringLiteral("kind")).toString(), QStringLiteral("subtitles"));
        QCOMPARE(cacheCount(), 12);
        // as Main.qml's openSubtitles goes on
        const QVariantMap open = application->reviewOpen(dropped.value(QStringLiteral("path")).toString());
        QVERIFY(open.value(QStringLiteral("ok")).toBool());
        QVERIFY(open.value(QStringLiteral("rows")).toList().isEmpty());
        application->finishClose();
        QCOMPARE(video.session().state(), application::VideoSession::State::Closed);
        QCOMPARE(cacheCount(), 12);
        dropped = application->openDropped({QUrl::fromLocalFile(clip)});
        QCOMPARE(dropped.value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
        QCOMPARE(cacheCount(), 12);
        // as TabCommands.openVideoFile goes on: no subtitles named as the video
        QVERIFY(application->openVideoFile(clip).value(QStringLiteral("subtitles")).toString().isEmpty());
        application->openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(cacheCount(), 10, 20000);
        QVERIFY(QFileInfo::exists(cache));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("AudioCache/older0.w64"))));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("AudioCache/older1.w64"))));
        delete application;
        application = nullptr;
    }

    // When the video's index file cannot be written (an Indices folder that
    // cannot be written), the video's helper hands the index over in a
    // temporary file; the box opens the video's audio from it without
    // indexing again (legacy's box shared the video's index), and the file
    // goes once the box has opened.
    void indexHandedOverWhenIndicesCannotBeWritten()
    {
        delete engine;
        engine = nullptr;
        delete application;
        application = nullptr;
        QTemporaryDir folder;
        const QString indices = folder.filePath(QStringLiteral("Indices"));
        QVERIFY(QDir().mkpath(indices));
#ifdef _WIN32
        // a read-only folder still takes new files on Windows: the index
        // file's name is taken by a folder instead
        QVERIFY(QDir().mkpath(folder.filePath(QStringLiteral("Indices/clip_1.ffindex"))));
#else
        QVERIFY(QFile::setPermissions(indices, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const auto restore = qScopeGuard([&] { QFile::setPermissions(indices, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner); });
        if (QFile probe(folder.filePath(QStringLiteral("Indices/probe"))); probe.open(QIODevice::WriteOnly))
            QSKIP("the Indices folder stays writable for this user");
#endif
        app::Application::Options options;
        options.indexDir = indices;
        options.audioCacheDir = folder.filePath(QStringLiteral("AudioCache"));
        application = new app::Application(options);
        const QString clip = folder.filePath(QStringLiteral("clip.mkv"));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"), clip));
        auto &audio = application->audio();
        auto &video = application->video();
        video.openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(video.session().state() == application::VideoSession::State::Ready, 20000);
        const QString handoff = QString::fromStdString(video.session().indexHandoff());
        QVERIFY(!handoff.isEmpty());
        QVERIFY(!QFileInfo(folder.filePath(QStringLiteral("Indices/clip_1.ffindex"))).isFile());
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(audio.box().fromVideo());
        QCOMPARE(audio.box().indexing().second, 0); // the box's helper read the index
        QVERIFY(!audio.box().cacheReused()); // the video indexed now
        QVERIFY(!QFileInfo::exists(handoff));
        delete application;
        application = nullptr;
    }

    // Legacy DeleteOldAudioCache: the files read longest ago go until the
    // folder holds the limit; the cache in use, under its final name or still
    // being written (.part), is never one of them (legacy on Windows could not
    // remove an open file and took the next; on Linux it lost the cache: R3).
    void audioCacheTrimSkipsTheCacheInUse()
    {
        QTemporaryDir folder;
        const QDateTime base = QDateTime::currentDateTime().addDays(-30);
        const auto make = [&](const QString &name, int day) {
            QFile f(folder.filePath(name));
            QVERIFY(f.open(QIODevice::ReadWrite));
            f.write("x");
            QVERIFY(f.setFileTime(base.addDays(day), QFileDevice::FileAccessTime));
        };
        make(QStringLiteral("a.w64.part"), 0);
        make(QStringLiteral("b.w64"), 1);
        make(QStringLiteral("c.w64"), 2);
        make(QStringLiteral("d.w64"), 3);
        make(QStringLiteral("e.w64"), 4);
        const std::filesystem::path dir(folder.path().toStdU16String());
        ui::deleteOldAudioCache(dir, dir / "a.w64", 0); // a limit below 1 keeps everything
        QCOMPARE(QDir(folder.path()).entryList(QDir::Files).size(), 5);
        ui::deleteOldAudioCache(dir, dir / "a.w64", 2);
        QCOMPARE(QDir(folder.path()).entryList(QDir::Files),
                 (QStringList{QStringLiteral("a.w64.part"), QStringLiteral("e.w64")}));
        make(QStringLiteral("f.w64"), 5);
        ui::deleteOldAudioCache(dir, {}, 2); // nothing in use: the oldest goes
        QCOMPARE(QDir(folder.path()).entryList(QDir::Files),
                 (QStringList{QStringLiteral("e.w64"), QStringLiteral("f.w64")}));
    }

    // Legacy RendererFFMS2::OpenFile: a file with audio and no video given to
    // Open video goes to the audio box, and the open video stays.
    void audioOnlyFileOpenedAsVideo()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        const QString video = QString::fromStdString(application->video().session().path());
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QCOMPARE(audio.path(), nativeFixture("audioonly.mkv"));
        QVERIFY(!audio.box().fromVideo());
        QVERIFY(application->video().hasVideo());
        QCOMPARE(QString::fromStdString(application->video().session().path()), video);
        QCOMPARE(application->recentAudio().first().toMap().value(QStringLiteral("path")).toString(),
                 nativeFixture("audioonly.mkv"));
    }

    // ---- O2: the shortcut editor ----

    QVariantMap installedKeys(const char *window) const
    {
        return application->hotkeys().keys().value(QLatin1String(window)).toMap();
    }
    // The Options page's shown rows.
    QVariantList hotkeyRows() const { return application->hotkeys().rows(); }
    int hotkeyPosition(const QString &text) const
    {
        const auto rows = hotkeyRows();
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].toMap().value(QStringLiteral("text")).toString() == text)
                return i;
        return -1;
    }
    QVariantMap hotkeyRow(const QString &text) const
    {
        const int i = hotkeyPosition(text);
        return i < 0 ? QVariantMap() : hotkeyRows()[i].toMap();
    }
    QQuickWindow *mappingWindow(const char *name) const
    {
        return engine->rootObjects().first()->findChild<QQuickWindow *>(QLatin1String(name));
    }
    void keyTo(QQuickItem *target, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QKeyEvent press(QEvent::KeyPress, key, mods);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, mods);
        QCoreApplication::sendEvent(target, &release);
        QCoreApplication::processEvents();
    }
    QQuickItem *in(QQuickWindow *w, const char *name) const { return findItem(w->contentItem(), QLatin1String(name)); }
    void clickButton(QQuickWindow *w, const char *name)
    {
        auto *b = in(w, name);
        QVERIFY2(b, name);
        QVERIFY(QMetaObject::invokeMethod(b, "clicked"));
        QCoreApplication::processEvents();
    }
    void clickWith(QQuickItem *target, Qt::KeyboardModifiers mods)
    {
        const QPoint at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
        QTest::mouseClick(target->window(), Qt::LeftButton, mods, at);
        QCoreApplication::processEvents();
    }

    // Legacy OptionsDialog's Hotkeys page: after the Audio pages, every
    // action listed as AddHotkeysOnList lists it, filtered by window, and a
    // mapped key stays staged until OK, which saves and installs it.
    void hotkeysPageMapsAndOkInstalls()
    {
        QVERIFY(application->openFile(episode));
        auto &settings = *application->settingsStore();
        QVERIFY(!settings.contains("shortcuts.hotkeys")); // the defaults until saved
        QCOMPARE(installedKeys("global").value(QStringLiteral("GLOBAL_HISTORY")).toString(), QStringLiteral("Ctrl+Shift+H"));
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *pages = dialogItem("settingsDialog", "settingsPages");
        QVERIFY(pages);
        const auto model = pages->property("model").toList();
        QStringList names;
        for (const auto &p : model)
            names << p.toMap().value(QStringLiteral("name")).toString();
        // (legacy's Themes page is K2's Appearance page, #206)
        QCOMPARE(names, (QStringList{"Editor", "Conversion", "Advanced", "Video", "Audio", "Advanced", "Appearance",
                                     "Hotkeys", "Subtitle properties"}));
        QVERIFY(dialogItem("settingsDialog", "settingsPageHotkeys"));
        QCOMPARE(hotkeyRows().size(), 225); // V3: "Open video with FFMS2" retired, never listed
        QCOMPARE(hotkeyRows().first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Global Close current tab"));
        QCOMPARE(application->hotkeys().selected(), 0);
        // Choose filtering: Video shortcuts.
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyFilter"), "activated", Q_ARG(int, 5)));
        QCOMPARE(hotkeyRows().size(), int(std::ranges::count_if(application::hotkeyNames(), [](const auto &n) {
                     return application::hotkeyType(n.first) == application::VideoHotkey;
                 })));
        QCOMPARE(hotkeyRow(QStringLiteral("Video Play / Pause")).value(QStringLiteral("accel")).toString(), QStringLiteral("Space"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyFilter"), "activated", Q_ARG(int, 0)));
        // Map hotkey on Global History: the mapping window with the window choice.
        application->hotkeys().select(hotkeyPosition(QStringLiteral("Global History")));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyMap"), "clicked"));
        auto *mapping = mappingWindow("settingsHotkeyMapping");
        QVERIFY(mapping);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"History\"."));
        QVERIFY(in(mapping, "hotkeyWindowChoice")->isVisible());
        QCOMPARE(in(mapping, "hotkeyWindowChoice")->property("currentIndex").toInt(), 0);
        // A lone modifier waits; Ctrl+Shift+K is taken.
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_Control, Qt::ControlModifier);
        QVERIFY(mapping->isVisible());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!mapping->isVisible());
        auto row = hotkeyRow(QStringLiteral("Global History"));
        QCOMPARE(row.value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-Shift-K"));
        QVERIFY(row.value(QStringLiteral("keyModified")).toBool());
        QVERIFY(row.value(QStringLiteral("textModified")).toBool());
        // Staged: nothing installed or stored yet.
        QCOMPARE(installedKeys("global").value(QStringLiteral("GLOBAL_HISTORY")).toString(), QStringLiteral("Ctrl+Shift+H"));
        QVERIFY(!settings.contains("shortcuts.hotkeys"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(settings.list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_HISTORY G=Ctrl-Shift-K")));
        QCOMPARE(settings.list("shortcuts.audioHotkeys").size(), 23);
        QCOMPARE(installedKeys("global").value(QStringLiteral("GLOBAL_HISTORY")).toString(), QStringLiteral("Ctrl+Shift+K"));
        // The shell's table (every Global binding, the menus' too) follows.
        QVERIFY(application->hotkeys().globalShortcuts().contains(
            QVariantMap{{QStringLiteral("symbol"), QStringLiteral("GLOBAL_HISTORY")}, {QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+K")}}));
        // The new keys open History; the old ones do nothing.
        auto *history = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("historyWindow"));
        QVERIFY(history);
        item<QQuickItem>("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        press(Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!history->isVisible());
        press(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(history->isVisible());
        history->close();
        // The next start reads the stored bindings.
        app::HotkeysController again(application->automationHotkeys(), settings);
        QCOMPARE(again.accelOf(QStringLiteral("GLOBAL_HISTORY"), 0), QStringLiteral("Ctrl-Shift-K"));
        QCOMPARE(again.accelOf(QStringLiteral("AUDIO_COMMIT"), 4), QStringLiteral("Enter"));
    }

    // HkeysDialog's refusals keep the window open; taken keys ask first
    // (ItemHotkey::OnMapHotkey), per window.
    void hotkeysMappingRefusesAndAsks()
    {
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *mapping = mappingWindow("settingsHotkeyMapping");
        auto *keys = in(mapping, "hotkeyMappingKeys");
        auto *refusal = mapping->findChild<QQuickWindow *>(QStringLiteral("hotkeyRefusal"));
        auto *question = mapping->findChild<QQuickWindow *>(QStringLiteral("hotkeyQuestion"));
        QVERIFY(refusal && question);
        const auto map = [&](const QString &text) {
            application->hotkeys().select(hotkeyPosition(text));
            QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyMap"), "clicked"));
            QTRY_VERIFY(mapping->isVisible());
        };
        map(QStringLiteral("Global History"));
        keyTo(keys, Qt::Key_K); // a Global key without modifiers
        QTRY_VERIFY(refusal->isVisible());
        QCOMPARE(in(refusal, "hotkeyRefusalText")->property("text").toString(),
                 QStringLiteral("Global and editor shortcuts must include modifiers (e.g. Shift, Ctrl, Alt)."));
        QVERIFY(mapping->isVisible());
        clickButton(refusal, "hotkeyRefusalOk");
        keyTo(keys, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(in(refusal, "hotkeyRefusalText")->property("text").toString(),
                 QStringLiteral("You cannot use shortcuts for copying, cutting, and pasting."));
        clickButton(refusal, "hotkeyRefusalOk");
        keyTo(keys, Qt::Key_F4, Qt::AltModifier);
        QCOMPARE(in(refusal, "hotkeyRefusalText")->property("text").toString(),
                 QStringLiteral("You cannot use the program exit shortcut."));
        clickButton(refusal, "hotkeyRefusalOk");
        // Ctrl+S is Save's: the same window asks, without "Set anyway".
        keyTo(keys, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(question->isVisible());
        QVERIFY(!mapping->isVisible());
        QCOMPARE(in(question, "hotkeyQuestionText")->property("text").toString(),
                 QStringLiteral("This hotkey already exists for \"Save\".\nWhat to do?"));
        QVERIFY(in(question, "hotkeySwitch")->isVisible());
        QVERIFY(!in(question, "hotkeySetAnyway")->isVisible());
        clickButton(question, "hotkeyCancel");
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-Shift-H"));
        map(QStringLiteral("Global History"));
        keyTo(keys, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(question->isVisible());
        clickButton(question, "hotkeySwitch");
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-S"));
        QCOMPARE(hotkeyRow(QStringLiteral("Global Save")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-Shift-H"));
        // Another window: Subtitles Duplicate lines with the Editor's Ctrl+R.
        map(QStringLiteral("Subtitles Duplicate lines"));
        QCOMPARE(in(mapping, "hotkeyWindowChoice")->property("currentIndex").toInt(), 1);
        keyTo(keys, Qt::Key_R, Qt::ControlModifier);
        QTRY_VERIFY(question->isVisible());
        QCOMPARE(in(question, "hotkeyQuestionText")->property("text").toString(),
                 QStringLiteral("This shortcut already exists in another window as a shortcut for \"Editor Next "
                                "untranslated line\".\nWhat would you like to do?"));
        QVERIFY(in(question, "hotkeySetAnyway")->isVisible());
        clickButton(question, "hotkeySetAnyway");
        QCOMPARE(hotkeyRow(QStringLiteral("Subtitles Duplicate lines")).value(QStringLiteral("accel")).toString(),
                 QStringLiteral("Ctrl-R"));
        QCOMPARE(hotkeyRow(QStringLiteral("Editor Next untranslated line")).value(QStringLiteral("accel")).toString(),
                 QStringLiteral("Ctrl-R"));
        // The window choice: Save as a Video hotkey adds a row at the end.
        map(QStringLiteral("Global Save"));
        in(mapping, "hotkeyWindowChoice")->setProperty("currentIndex", 3);
        keyTo(keys, Qt::Key_F6);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(hotkeyRows().last().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Video Save"));
        QCOMPARE(application->hotkeys().selected(), -1);
        // Restore default, Delete, and undo in the list.
        application->hotkeys().select(hotkeyPosition(QStringLiteral("Global History")));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyRestore"), "clicked"));
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-Shift-H"));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyRemove"), "clicked"));
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QString());
        auto *list = dialogItem("settingsDialog", "hotkeyList");
        keyTo(list, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-Shift-H"));
        keyTo(list, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QString());
        // Escape closes the mapping window without a binding.
        map(QStringLiteral("Global Find"));
        keyTo(keys, Qt::Key_Escape);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(hotkeyRow(QStringLiteral("Global Find")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-F"));
        // Cancel: nothing installed or stored.
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(installedKeys("global").value(QStringLiteral("GLOBAL_SAVE_SUBS")).toString(), QStringLiteral("Ctrl+S"));
        QVERIFY(!application->settingsStore()->contains("shortcuts.hotkeys"));
        // O2-stale-copy (approved): the cancelled dialog's edits went with
        // it (legacy's static hotkeysCopy outlived the dialog, and the next
        // dialog's OK wrote them too); the next dialog writes its own only.
        dialog = openSettings();
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(hotkeyRow(QStringLiteral("Global History")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-Shift-H"));
        application->hotkeys().select(hotkeyPosition(QStringLiteral("Global Find")));
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyRemove"), "clicked"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsApply"), "click"));
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_HISTORY"), 0), QStringLiteral("Ctrl-Shift-H"));
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_SAVE_SUBS"), 0), QStringLiteral("Ctrl-S"));
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_SAVE_SUBS"), 3), QString());
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GLOBAL_SEARCH"), 0), QString());
        // Apply clears the changed marks and keeps the dialog.
        QVERIFY(!hotkeyRow(QStringLiteral("Global Find")).value(QStringLiteral("keyModified")).toBool());
        QVERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
    }

    // "Set default": Hkeys.ResetDefaults() in memory only; nothing is saved
    // or installed until legacy next saves (OK with a change, a gesture).
    void hotkeysSetDefaultResetsInMemoryOnly()
    {
        auto &store = *application->settingsStore();
        // Stored bindings are read as stored, however few (C04-short-file);
        // a file never written takes the defaults.
        store.set("shortcuts.hotkeys", QStringList{QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-K")});
        {
            app::HotkeysController fresh(application->automationHotkeys(), store);
            QCOMPARE(fresh.accelOf(QStringLiteral("GLOBAL_SAVE_SUBS"), 0), QStringLiteral("Ctrl-K"));
            QCOMPARE(fresh.accelOf(QStringLiteral("GLOBAL_HISTORY"), 0), QString());
            QCOMPARE(fresh.accelOf(QStringLiteral("AUDIO_COMMIT"), 4), QStringLiteral("Enter"));
        }
        store.reset(QStringLiteral("shortcuts.hotkeys"));
        // Changed and saved bindings, the scripts' included.
        auto &h = application->hotkeys();
        application->automationHotkeys().replaceCommitted({{"Script a.lua-1", "Ctrl+J"}});
        application->automationHotkeys().save();
        application->automationHotkeys().install();
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Global Save")));
        h.optionsMap(QStringLiteral("Ctrl-K"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QVERIFY(store.list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-K")));
        const QStringList saved = store.list("shortcuts.hotkeys");
        auto *dialog = openSettings();
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(hotkeyRow(QStringLiteral("Global Script a.lua-1")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-J"));
        QCOMPARE(hotkeyRow(QStringLiteral("Global Save")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-K"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsDefault"), "click"));
        QCOMPARE(hotkeyRow(QStringLiteral("Global Save")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-S"));
        QVERIFY(hotkeyRow(QStringLiteral("Global Script a.lua-1")).isEmpty()); // the scripts' go too
        QVERIFY(application->automationHotkeys().committedKeys().empty());
        // Stored and installed bindings stay as they were, even after Cancel.
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(store.list("shortcuts.hotkeys"), saved);
        QCOMPARE(store.list(application::kAutomationHotkeysSetting.data()).size(), 1);
        QCOMPARE(installedKeys("global").value(QStringLiteral("GLOBAL_SAVE_SUBS")).toString(), QStringLiteral("Ctrl+K"));
        // The next save writes them (a gesture saves the main file).
        h.gestureMap(application::hotkeyIdOf("GLOBAL_OPEN_SUBS"), QStringLiteral("Open subtitles"),
                     QStringLiteral("Ctrl-Shift-Q"), 0, QStringLiteral("cancel"));
        QVERIFY(store.list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-S")));
        QVERIFY(store.list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_OPEN_SUBS G=Ctrl-Shift-Q")));
        QCOMPARE(store.list(application::kAutomationHotkeysSetting.data()).size(), 0);
        QCOMPARE(installedKeys("global").value(QStringLiteral("GLOBAL_SAVE_SUBS")).toString(), QStringLiteral("Ctrl+S"));
    }

    // Hotkeys::OnMapHkey: Shift+click on a menu item (with the window
    // choice) or a mapped button (its own window) maps at once.
    void hotkeyGestureOnMappedButtonAndMenu()
    {
        QVERIFY(application->openFile(episode));
        item<QQuickItem>("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().editable());
        auto *bold = visualItem("tag_b");
        QVERIFY(bold);
        clickWith(bold, Qt::ShiftModifier);
        auto *mapping = mappingWindow("hotkeyMapping");
        QVERIFY(mapping);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"Add bold\"."));
        QVERIFY(!in(mapping, "hotkeyWindowChoice")->isVisible());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!mapping->isVisible());
        // Installed and saved at once.
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("EDITBOX_INSERT_BOLD"), 2), QStringLiteral("Ctrl-Shift-B"));
        QVERIFY(application->settingsStore()->list("shortcuts.hotkeys").contains(QStringLiteral("EDITBOX_INSERT_BOLD E=Ctrl-Shift-B")));
        // In the Line editor the new keys bold (the draft has the tags); Ctrl+B no longer does.
        auto *line = item<QQuickItem>("lineText");
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto draft = [&] {
            const std::u8string d = session->draftText().value_or(u8"");
            return QString::fromUtf8(reinterpret_cast<const char *>(d.data()), qsizetype(d.size()));
        };
        line->forceActiveFocus();
        QMetaObject::invokeMethod(line, "select", Q_ARG(int, 0), Q_ARG(int, 5));
        press(Qt::Key_B, Qt::ControlModifier);
        QVERIFY(!draft().contains(QLatin1String("\\b1")));
        press(Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(draft(), QStringLiteral("{\\b1}first{\\b0}"));
        // A plain click acts.
        clickWith(bold, Qt::NoModifier);
        QVERIFY(!mapping->isVisible());
        QVERIFY(draft() != QStringLiteral("{\\b1}first{\\b0}"));
        application->editor().discard();

        // A menu item: the Grid's Duplicate lines, with the window choice.
        auto *grid = item<QQuickItem>("editingGrid");
        auto *menu = grid->findChild<QObject *>(QStringLiteral("gridMenu"));
        QVERIFY(menu);
        const auto openMenu = [&] {
            const QPoint centre = grid->mapToScene(QPointF(grid->width() / 2, 30)).toPoint();
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centre);
        };
        openMenu();
        auto *duplicate = engine->rootObjects().first()->findChild<QQuickItem *>(QStringLiteral("duplicateLines"));
        QVERIFY(duplicate);
        QTRY_VERIFY(duplicate->isVisible() && duplicate->width() > 0);
        QCOMPARE(duplicate->property("text").toString(), QStringLiteral("&Duplicate lines\tCtrl-D"));
        const int lines = application->files().session(*application->workspace().editingTarget())->document().lines().size();
        clickWith(duplicate, Qt::ShiftModifier);
        QTRY_VERIFY(mapping->isVisible());
        QVERIFY(in(mapping, "hotkeyWindowChoice")->isVisible());
        QCOMPARE(in(mapping, "hotkeyWindowChoice")->property("currentIndex").toInt(), 1);
        QCOMPARE(int(application->files().session(*application->workspace().editingTarget())->document().lines().size()), lines);
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GRID_DUPLICATE_LINES"), 1), QStringLiteral("Ctrl-Shift-D"));
        QCOMPARE(duplicate->property("text").toString(), QStringLiteral("&Duplicate lines\tCtrl-Shift-D"));
        // The question when the keys are taken (here by itself: every
        // binding holding them counts).
        openMenu();
        QTRY_VERIFY(duplicate->isVisible() && duplicate->width() > 0);
        clickWith(duplicate, Qt::ShiftModifier);
        QTRY_VERIFY(mapping->isVisible());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        auto *question = mapping->findChild<QQuickWindow *>(QStringLiteral("hotkeyQuestion"));
        QVERIFY(question);
        QTRY_VERIFY(question->isVisible());
        QCOMPARE(in(question, "hotkeyQuestionText")->property("text").toString(),
                 QStringLiteral("This hotkey already exists for \"Duplicate lines\".\nWhat to do?"));
        clickButton(question, "hotkeyCancel");
        QCOMPARE(application->hotkeys().accelOf(QStringLiteral("GRID_DUPLICATE_LINES"), 1), QStringLiteral("Ctrl-Shift-D"));
        // In the Grid the new keys duplicate.
        QTRY_VERIFY(window->isActive()); // the question gave the activation back
        QCOMPARE(application->hotkeys().actionFor(1, Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier),
                 QStringLiteral("GRID_DUPLICATE_LINES"));
        grid->forceActiveFocus();
        press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(int(application->files().session(*application->workspace().editingTarget())->document().lines().size()),
                     lines + 1);
    }

    // The Editor, Grid and Video windows route their own bindings first; a
    // binding copied to another window acts there too (TabPanel::SetAccels).
    void hotkeysRouteByWindow()
    {
        auto &h = application->hotkeys();
        h.beginOptions();
        // Next untranslated line as a Subtitles hotkey, Duplicate lines as a Video one.
        h.select(hotkeyPosition(QStringLiteral("Editor Next unconfirmed line")));
        h.optionsMap(QStringLiteral("Ctrl-Shift-U"), 1, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Subtitles Duplicate lines")));
        h.optionsMap(QStringLiteral("Ctrl-Shift-L"), 3, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(h.actionFor(1, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier), QStringLiteral("EDITBOX_FIND_NEXT_DOUBTFUL"));
        QCOMPARE(h.actionFor(2, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier), QString());
        QCOMPARE(h.actionFor(3, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier), QStringLiteral("GRID_DUPLICATE_LINES"));
        QCOMPARE(h.actionFor(1, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier), QString());
        // Video play and seek bindings also act in the Grid; Editor keys the
        // text field keeps do not reach the bindings.
        QCOMPARE(h.actionFor(1, Qt::Key_Space, Qt::NoModifier), QStringLiteral("VIDEO_PLAY_PAUSE"));
        QCOMPARE(h.actionFor(1, Qt::Key_Up, Qt::NoModifier), QString()); // 1 minute forward: Video only
        QCOMPARE(h.actionFor(3, Qt::Key_Up, Qt::NoModifier), QStringLiteral("VIDEO_MINUTE_FORWARD"));
        QCOMPARE(h.actionFor(2, Qt::Key_B, Qt::ControlModifier), QStringLiteral("EDITBOX_INSERT_BOLD"));
        QCOMPARE(h.actionFor(2, Qt::Key_Return, Qt::ShiftModifier), QStringLiteral("EDITBOX_SPLIT_LINE"));
        QCOMPARE(h.actionFor(2, Qt::Key_Colon, Qt::ShiftModifier), QString());
        QCOMPARE(h.actionFor(3, Qt::Key_Semicolon, Qt::NoModifier), QStringLiteral("VIDEO_5_SECONDS_BACKWARD"));
        QCOMPARE(h.actionFor(2, Qt::Key_0, Qt::KeypadModifier), QString());
        // A Grid binding for the Editor window is dropped (legacy's
        // unreachable branch); hidden tag buttons have no hotkey.
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Subtitles Swap lines")));
        h.optionsMap(QStringLiteral("Ctrl-Shift-W"), 2, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Editor First tag button")));
        h.optionsMap(QStringLiteral("Alt-1"), 2, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(h.actionFor(2, Qt::Key_W, Qt::ControlModifier | Qt::ShiftModifier), QString());
        QCOMPARE(h.actionFor(2, Qt::Key_1, Qt::AltModifier), QString()); // EDITBOX_TAG_BUTTONS is 0
        // Duplicate lines bound for the Video window does nothing in the Grid.
        QVERIFY(application->openFile(episode));
        item<QQuickItem>("editingGrid")->forceActiveFocus();
        press(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(int(application->files().session(*application->workspace().editingTarget())->document().lines().size()), 2);
    }

    // GRID_FILTER_IGNORE_IN_ACTIONS through a hotkey toggles from the stored
    // option (SubsGrid::OnAccelerator), while the menu item toggles what the
    // Grid shows; "Set default" resets the option only.
    void ignoreFilteringHotkeyTogglesFromTheOption()
    {
        keysNeverRepeat();
        QVERIFY(application->openFile(episode));
        auto &h = application->hotkeys();
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Subtitles Ignore filtering in some actions")));
        h.optionsMap(QStringLiteral("Ctrl-Shift-G"), 1, QStringLiteral("cancel"));
        h.commitOptions();
        auto &filter = application->gridFilter();
        auto &settings = *application->settingsStore();
        filter.setIgnoreInActions(true);
        // "Set default": the option resets, the Grid keeps ignoring.
        auto *dialog = openSettings();
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsDefault"), "click"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(!settings.boolean("grid.ignoreFiltering"));
        QVERIFY(filter.ignoreInActions());
        // Set default reset the bindings in memory, not the installed ones.
        QCOMPARE(h.accelOf(QStringLiteral("GRID_FILTER_IGNORE_IN_ACTIONS"), 1), QStringLiteral("Ctrl-Shift-G"));
        item<QQuickItem>("editingGrid")->forceActiveFocus();
        press(Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(filter.ignoreInActions()); // !option: still on
        QVERIFY(settings.boolean("grid.ignoreFiltering"));
        press(Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!filter.ignoreInActions());
        QVERIFY(!settings.boolean("grid.ignoreFiltering"));
    }

    // S2's bindings are the script lines of the same map: listed after the
    // Global actions, asked about, and written back by name.
    void hotkeysShareTheAutomationBindings()
    {
        auto &scripts = application->automationHotkeys();
        scripts.replaceCommitted({{"Script a.lua-1", "Ctrl+K"}});
        scripts.save();
        scripts.install();
        auto &h = application->hotkeys();
        h.beginOptions();
        const int script = hotkeyPosition(QStringLiteral("Global Script a.lua-1"));
        QVERIFY(script > 0);
        QCOMPARE(hotkeyRows()[script].toMap().value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-K"));
        QCOMPARE(hotkeyRows()[script + 1].toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Subtitles Set new FPS"));
        h.select(script);
        QCOMPARE(h.mapTarget().value(QStringLiteral("windows")).toBool(), false); // no window choice for scripts
        h.select(hotkeyPosition(QStringLiteral("Global History")));
        const auto c = h.optionsConflict(QStringLiteral("Ctrl-K"), 0);
        QCOMPARE(c.value(QStringLiteral("message")).toString(),
                 QStringLiteral("This hotkey already exists for \"Script a.lua-1\".\nWhat to do?"));
        h.optionsMap(QStringLiteral("Ctrl-K"), 0, QStringLiteral("delete"));
        // Its row is not found by name, so the script keeps its keys.
        QCOMPARE(hotkeyRow(QStringLiteral("Global Script a.lua-1")).value(QStringLiteral("accel")).toString(), QStringLiteral("Ctrl-K"));
        h.select(hotkeyPosition(QStringLiteral("Global Script a.lua-1")));
        h.optionsMap(QStringLiteral("Ctrl-Shift-M"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(scripts.committedKeys().at("Script a.lua-1"), std::string("Ctrl+Shift+M"));
        QCOMPARE(application->settingsStore()->list(application::kAutomationHotkeysSetting.data()).first().split(QLatin1Char('\t')).value(1),
                 QStringLiteral("Ctrl+Shift+M"));
        // The gesture on a macro (OnMapHkey(-1, name)): its binding, or the next script id.
        h.gestureMap(application::hotkeyIdOf("GLOBAL_SEARCH"), QStringLiteral("Find"), QStringLiteral("Ctrl-Shift-F"), 0,
                     QStringLiteral("cancel"));
        QCOMPARE(h.gestureTarget(QStringLiteral("Script a.lua-1")).value(QStringLiteral("id")).toInt(), 30100);
        QCOMPARE(h.gestureTarget(QStringLiteral("Script b.lua-0")).value(QStringLiteral("id")).toInt(), 30101);
        h.gestureMap(30101, QStringLiteral("Script b.lua-0"), QStringLiteral("Ctrl-Shift-F"), 0, QStringLiteral("switch"));
        QCOMPARE(scripts.committedKeys().at("Script b.lua-0"), std::string("Ctrl+Shift+F"));
        QCOMPARE(h.accelOf(QStringLiteral("GLOBAL_SEARCH"), 0), QString()); // switched with b's none
        // The automation window's OK after a change saves and installs the
        // whole map, here the defaults "Set default" left in memory.
        h.resetDefaults();
        QCOMPARE(h.accelOf(QStringLiteral("GLOBAL_SEARCH"), 0), QString());
        scripts.begin();
        scripts.commit(); // no change: nothing else
        QCOMPARE(h.accelOf(QStringLiteral("GLOBAL_SEARCH"), 0), QString());
        scripts.setKeys(QStringLiteral("Script c.lua-0"), QStringLiteral("Ctrl+Shift+Y"));
        scripts.commit();
        QCOMPARE(h.accelOf(QStringLiteral("GLOBAL_SEARCH"), 0), QStringLiteral("Ctrl-F"));
        QVERIFY(application->settingsStore()->list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_SEARCH G=Ctrl-F")));
    }

    // The shell's table (HikariSubFrame::SetAccels) holds every Global
    // binding; an action of another window bound for the Global window runs
    // there (OnUseWindowHotkey), a Global id bound for a panel reaches the
    // frame; CheckLastKeyEvent ignores a repeat within 100 ms.
    void globalBindingsRouteEveryAction()
    {
        QVERIFY(application->openFile(episode));
        auto &h = application->hotkeys();
        qint64 now = 1000;
        h.setKeyClock([&] { return now; });
        const auto lines = [&] {
            return int(application->files().session(*application->workspace().editingTarget())->document().lines().size());
        };
        const auto table = [&] {
            QMap<QString, QString> out;
            for (const auto &v : h.globalShortcuts())
                out.insert(v.toMap().value(QStringLiteral("symbol")).toString(), v.toMap().value(QStringLiteral("keys")).toString());
            return out;
        };
        // Every default Global binding, the menus' too.
        QCOMPARE(table().value(QStringLiteral("GLOBAL_PREVIOUS_FRAME")), QStringLiteral("Left"));
        QCOMPARE(table().value(QStringLiteral("GLOBAL_HISTORY")), QStringLiteral("Ctrl+Shift+H"));
        QCOMPARE(table().value(QStringLiteral("GLOBAL_ADD_PAGE")), QStringLiteral("Ctrl+T"));
        QCOMPARE(table().value(QStringLiteral("GLOBAL_CONVERT_TO_TMP")), QStringLiteral("Ctrl+F12"));
        QCOMPARE(table().size(), 38);
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Subtitles Duplicate lines")));
        h.optionsMap(QStringLiteral("Ctrl-Alt-D"), 0, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Global History")));
        h.optionsMap(QStringLiteral("Ctrl-Alt-H"), 1, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Global Style manager")));
        h.optionsMap(QStringLiteral("Ctrl-Alt-M"), 0, QStringLiteral("cancel"));
        // An _ALT audio id answers as its main id only while that one is bound too.
        h.select(hotkeyPosition(QStringLiteral("Audio Play line alt")));
        h.optionsMap(QStringLiteral("Ctrl-Alt-Y"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(table().value(QStringLiteral("GRID_DUPLICATE_LINES")), QStringLiteral("Ctrl+Alt+D"));
        QCOMPARE(table().value(QStringLiteral("GLOBAL_OPEN_STYLE_MANAGER")), QStringLiteral("Ctrl+Alt+M"));
        QVERIFY(!table().contains(QStringLiteral("AUDIO_PLAY_LINE")));

        // From the Line editor's start field, Duplicate lines bound for the
        // Global window duplicates in the Grid; a repeat within 100 ms does not.
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        item("startField")->forceActiveFocus();
        press(Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(lines(), 3);
        now += 99;
        press(Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(lines(), 3);
        now += 1; // 100 ms after the last one that ran
        press(Qt::Key_D, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(lines(), 4);
        // Ctrl+I there is the Editor's (EDITBOX_INSERT_ITALIC, the whole
        // Line editor's table), not the Time shift window.
        auto *timingDock = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("timingDock"));
        press(Qt::Key_I, Qt::ControlModifier);
        QVERIFY(!timingDock->property("isOpen").toBool());
        application->editor().discard();
        // History bound for the Subtitles window: from the Grid only.
        auto *history = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("historyWindow"));
        QVERIFY(history);
        item("startField")->forceActiveFocus();
        press(Qt::Key_H, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(!history->isVisible());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_H, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(history->isVisible());
        history->close();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        // A Global action that had no route before: Style manager, remapped.
        auto *styles = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QVERIFY(styles);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_M, Qt::ControlModifier);
        QVERIFY(!styles->isVisible());
        press(Qt::Key_M, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(styles->isVisible());
        styles->close();
        h.setKeyClock({});
    }

    // P6's tab keys are Global bindings: remapped, the new keys switch tabs
    // and the old ones do not; Ctrl+T (GLOBAL_ADD_PAGE) adds one.
    void tabShortcutsFollowTheRegistry()
    {
        QVERIFY(application->openFile(episode));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_T, Qt::ControlModifier);
        QTRY_COMPARE(application->tabs().size(), 2);
        QCOMPARE(application->currentTab(), 1);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_PageDown, Qt::ControlModifier);
        QTRY_COMPARE(application->currentTab(), 0);
        auto &h = application->hotkeys();
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Global Next tab")));
        h.optionsMap(QStringLiteral("Alt-PgDn"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QVERIFY(application->settingsStore()->list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_NEXT_TAB G=Alt-PgDn")));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_PageDown, Qt::ControlModifier);
        QCOMPARE(application->currentTab(), 0);
        press(Qt::Key_PageDown, Qt::AltModifier);
        QTRY_COMPARE(application->currentTab(), 1);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_PageUp, Qt::ControlModifier); // GLOBAL_PREVIOUS_TAB keeps its keys
        QTRY_COMPARE(application->currentTab(), 0);
    }

    // FillTable's keypad names ("Num 0", "Num .", "Num +", "Num /", "Num Enter")
    // are the keypad's keys: Qt reports them with KeypadModifier.
    void keypadKeysFireTheirBindings()
    {
        QVERIFY(application->openFile(episode));
        auto &h = application->hotkeys();
        const auto lines = [&] {
            return int(application->files().session(*application->workspace().editingTarget())->document().lines().size());
        };
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Subtitles Duplicate lines")));
        h.optionsMap(QStringLiteral("Num +"), 1, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Subtitles Insert after")));
        h.optionsMap(QStringLiteral("Num ."), 1, QStringLiteral("anyway")); // Video's and Audio's keys too
        h.select(hotkeyPosition(QStringLiteral("Subtitles Insert before")));
        h.optionsMap(QStringLiteral("Num 5"), 1, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Global History")));
        h.optionsMap(QStringLiteral("Num /"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(h.actionFor(1, Qt::Key_Plus, Qt::KeypadModifier), QStringLiteral("GRID_DUPLICATE_LINES"));
        QCOMPARE(h.actionFor(1, Qt::Key_Plus, Qt::NoModifier), QString()); // '+' is not "Num +"
        QCOMPARE(h.actionFor(1, Qt::Key_Comma, Qt::KeypadModifier), QStringLiteral("GRID_INSERT_AFTER")); // a comma decimal key
        QCOMPARE(h.actionFor(1, Qt::Key_Insert, Qt::KeypadModifier), QString()); // Num Lock off: WXK_NUMPAD_INSERT, no name
        QCOMPARE(h.actionFor(3, Qt::Key_0, Qt::KeypadModifier), QStringLiteral("VIDEO_VOLUME_MINUS"));
        QCOMPARE(h.actionFor(3, Qt::Key_Period, Qt::KeypadModifier), QStringLiteral("VIDEO_VOLUME_PLUS"));
        QCOMPARE(h.actionFor(2, Qt::Key_Enter, Qt::KeypadModifier | Qt::ControlModifier), QString()); // the fixed numpad Enter
        auto *grid = item("editingGrid");
        grid->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Plus, Qt::KeypadModifier);
        QTRY_COMPARE(lines(), 3);
        press(Qt::Key_Plus);
        QCOMPARE(lines(), 3);
        press(Qt::Key_Period, Qt::KeypadModifier);
        QTRY_COMPARE(lines(), 4);
        press(Qt::Key_5, Qt::KeypadModifier);
        QTRY_COMPARE(lines(), 5);
        press(Qt::Key_5); // the digit row
        QCOMPARE(lines(), 5);
        // A Global keypad binding through the shell's table.
        auto *history = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("historyWindow"));
        press(Qt::Key_Slash);
        QVERIFY(!history->isVisible());
        press(Qt::Key_Slash, Qt::KeypadModifier);
        QTRY_VERIFY(history->isVisible());
        history->close();
    }

    // The gesture on the other menus (OnMenuSelected/OnMenuSelected1) and
    // the Grid's checkable items (not switched).
    void hotkeyGestureOnMenus()
    {
        QVERIFY(application->openFile(episode));
        auto &h = application->hotkeys();
        auto *mapping = mappingWindow("hotkeyMapping");
        QVERIFY(mapping);
        auto *root = engine->rootObjects().first();
        // Subtitles > Style manager: Shift+click maps GLOBAL_OPEN_STYLE_MANAGER.
        auto *menu = named("subtitlesMenu");
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));
        auto *styleItem = qobject_cast<QQuickItem *>(named("styleManagerMenuItem"));
        QVERIFY(styleItem);
        QTRY_VERIFY(styleItem->isVisible() && styleItem->width() > 0);
        clickWith(styleItem, Qt::ShiftModifier);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"Style manager\"."));
        QVERIFY(in(mapping, "hotkeyWindowChoice")->isVisible());
        auto *styles = root->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QVERIFY(!styles->isVisible());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(h.accelOf(QStringLiteral("GLOBAL_OPEN_STYLE_MANAGER"), 0), QStringLiteral("Alt-Ctrl-K"));
        QVERIFY(application->settingsStore()->list("shortcuts.hotkeys").contains(QStringLiteral("GLOBAL_OPEN_STYLE_MANAGER G=Alt-Ctrl-K")));
        // The Grid's checkable "Reverse filtering": mapped, not switched.
        auto *grid = item("editingGrid");
        const QPoint centre = grid->mapToScene(QPointF(grid->width() / 2, 30)).toPoint();
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centre);
        auto *filtering = named("filteringMenu");
        QVERIFY(filtering);
        QVERIFY(QMetaObject::invokeMethod(filtering, "open"));
        auto *invert = qobject_cast<QQuickItem *>(named("filterInvert"));
        QTRY_VERIFY(invert->isVisible() && invert->width() > 0);
        QVERIFY(!application->gridFilter().inverted());
        clickWith(invert, Qt::ShiftModifier);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyWindowChoice")->property("currentIndex").toInt(), 1);
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(h.accelOf(QStringLiteral("GRID_FILTER_INVERT"), 1), QStringLiteral("Alt-Ctrl-R"));
        QVERIFY(!application->gridFilter().inverted());
        QVERIFY(!invert->property("checked").toBool());
        QTRY_VERIFY(window->isActive());
    }

    // The video panel's buttons (BitmapButton: Shift held, the window
    // choice, the binding in the tooltip) and the frame keys, which are
    // Global bindings.
    void hotkeyGestureOnVideoButtonsAndFrameKeys()
    {
        QVERIFY(application->openFile(episode));
        auto &h = application->hotkeys();
        auto *mapping = mappingWindow("hotkeyMapping");
        auto *grid = item("editingGrid");
        // Stop: its tooltip names the binding (none by default); Shift+click
        // maps VIDEO_STOP with the window choice.
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        auto *stop = visualItem("stopVideo");
        QVERIFY(stop);
        QCOMPARE(stop->property("tip").toString(), QStringLiteral("Stop\nShortcut can be set using Shift + Click"));
        // (the subtitles' resolution question first: kept as it is)
        auto *mismatch = named("mismatchDialog");
        QTRY_VERIFY_WITH_TIMEOUT(mismatch->property("visible").toBool(), 20000);
        QVERIFY(QMetaObject::invokeMethod(mismatch, "reject"));
        QTRY_VERIFY(!mismatch->property("visible").toBool());
        clickWith(stop, Qt::ShiftModifier | Qt::ControlModifier);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"Stop\"."));
        QVERIFY(in(mapping, "hotkeyWindowChoice")->isVisible());
        QCOMPARE(in(mapping, "hotkeyWindowChoice")->property("currentIndex").toInt(), 3);
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_K);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(h.accelOf(QStringLiteral("VIDEO_STOP"), 3), QStringLiteral("K"));
        QCOMPARE(stop->property("tip").toString(), QStringLiteral("Stop (K)\nShortcut can be set using Shift + Click"));
        // The frame keys are GLOBAL_PREVIOUS_FRAME / GLOBAL_NEXT_FRAME:
        // from the Grid too, and remapped they follow the registry.
        QTRY_VERIFY(window->isActive());
        grid->forceActiveFocus();
        const int frame = application->video().frame();
        press(Qt::Key_Right);
        QTRY_COMPARE(application->video().frame(), frame + 1);
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Global Next frame")));
        h.optionsMap(QStringLiteral("Alt-N"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_Right);
        QCOMPARE(application->video().frame(), frame + 1);
        press(Qt::Key_N, Qt::AltModifier);
        QTRY_COMPARE(application->video().frame(), frame + 2);
        press(Qt::Key_Left);
        QTRY_COMPARE(application->video().frame(), frame + 1);
    }

    // The Options list's gutter while filtered (HikariListCtrl): a box where
    // rows are hidden, which shows them (VISIBLE_BLOCK) or hides them again.
    void hotkeysListGutterShowsHiddenRows()
    {
        auto *dialog = openSettings();
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto &h = application->hotkeys();
        QVERIFY(!h.listFiltered());
        dialogItem("settingsDialog", "settingsPages")->setProperty("currentIndex", 7); // Hotkeys
        QVERIFY(QMetaObject::invokeMethod(dialogItem("settingsDialog", "hotkeyFilter"), "activated", Q_ARG(int, 1)));
        QVERIFY(h.listFiltered());
        const int count = int(hotkeyRows().size());
        // The first set shortcut ("Global Close current tab", Ctrl-W) is the
        // first row: no block above it; rows without keys are hidden after
        // some, each such gap with its box.
        QCOMPARE(h.topBlock(), 0);
        int gap = -1;
        for (int i = 0; i < count && gap < 0; ++i)
            if (hotkeyRows()[i].toMap().value(QStringLiteral("block")).toInt() == 1)
                gap = i;
        QVERIFY(gap >= 0);
        const QByteArray box = "hotkeyBlock" + QByteArray::number(gap);
        QTRY_VERIFY(dialogItem("settingsDialog", box.constData()) && dialogItem("settingsDialog", box.constData())->isVisible());
        h.toggleBlock(gap);
        QVERIFY(hotkeyRows().size() > count);
        QVERIFY(hotkeyRows()[gap + 1].toMap().value(QStringLiteral("inBlock")).toBool());
        QCOMPARE(hotkeyRows()[gap].toMap().value(QStringLiteral("block")).toInt(), 2);
        h.toggleBlock(gap);
        QCOMPARE(int(hotkeyRows().size()), count);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

    // Hotkeys::GetHKey logs a key it cannot read when the bindings install.
    void invalidShortcutsAreLogged()
    {
        auto &store = *application->settingsStore();
        store.set("shortcuts.hotkeys", QStringList{QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-Bogus"),
                                                   QStringLiteral("GLOBAL_HISTORY G=Ctrl-Shift-H")});
        QStringList logged;
        app::HotkeysController h(application->automationHotkeys(), store, [&](const QString &m) { logged << m; });
        QCOMPARE(logged, QStringList{QStringLiteral("Shortcut \"Bogus\" is invalid")});
        QCOMPARE(h.accelOf(QStringLiteral("GLOBAL_SAVE_SUBS"), 0), QStringLiteral("Ctrl-Bogus"));
        QVERIFY(!h.keys().value(QStringLiteral("global")).toMap().contains(QStringLiteral("GLOBAL_SAVE_SUBS")));
    }

    // An Audio action bound for the Global window runs in the audio box
    // (OnUseWindowHotkey; nothing without one), and a two-hotkey button's
    // Ctrl+click maps its main hotkey as legacy's does (MappedButton computes
    // id - 10 and maps GetId()).
    void audioBindingsForTheGlobalWindowAndCtrlClick()
    {
        restartWithoutSound();
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        auto &h = application->hotkeys();
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Audio Play line")));
        h.optionsMap(QStringLiteral("Ctrl-Alt-P"), 0, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Audio Play line alt")));
        h.optionsMap(QStringLiteral("Ctrl-Alt-Y"), 0, QStringLiteral("cancel"));
        h.commitOptions();
        QStringList symbols;
        for (const auto &v : h.globalShortcuts())
            symbols << v.toMap().value(QStringLiteral("symbol")).toString();
        QCOMPARE(symbols.count(QStringLiteral("AUDIO_PLAY_LINE")), 2); // the _ALT one as its main id
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_P, Qt::ControlModifier | Qt::AltModifier); // no audio box: nothing
        QVERIFY(!audio.lastPlayRange());
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Y, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(audio.lastPlayRange().has_value());
        QCOMPARE(audio.lastPlayRange()->start, 48000);
        press(Qt::Key_H);
        // Ctrl+click on Play line (AUDIO_PLAY_LINE / AUDIO_PLAY_LINE_ALT) maps
        // the second hotkey (O2-second-hotkey).
        const QString mainBefore = h.accelOf(QStringLiteral("AUDIO_PLAY_LINE"), 4);
        auto *mapping = mappingWindow("hotkeyMapping");
        auto *playLine = visualItem("audioPlayLine");
        QVERIFY(playLine);
        QTRY_VERIFY(playLine->isVisible());
        clickWith(playLine, Qt::ControlModifier);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"Play line alt\"."));
        QVERIFY(!in(mapping, "hotkeyWindowChoice")->isVisible());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_U);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(h.accelOf(QStringLiteral("AUDIO_PLAY_LINE_ALT"), 4), QStringLiteral("U"));
        QCOMPARE(h.accelOf(QStringLiteral("AUDIO_PLAY_LINE"), 4), mainBefore);
        // Ctrl on a one-hotkey button acts.
        auto *stop = visualItem("audioStop");
        clickWith(stop, Qt::ControlModifier);
        QVERIFY(!mapping->isVisible());
    }

    // A macro's menu item: Shift maps its hotkey (OnMapHkey(-1, "Script
    // <file>-<n>"), no window choice), a S2 binding run from the shell.
    void hotkeyGestureOnAMacro()
    {
        QVERIFY(application->openFile(episode));
        application->automation().load(HIKARI_LUA_FIXTURES "/shell-fixture.lua");
        QTRY_VERIFY_WITH_TIMEOUT(named("macro_Prefix from dialog"), 30000);
        auto *macro = qobject_cast<QQuickItem *>(named("macro_Prefix from dialog"));
        QVERIFY(macro);
        QVERIFY(QMetaObject::invokeMethod(named("automationMenu"), "open"));
        QTRY_VERIFY(macro->isVisible() && macro->width() > 0 && macro->isEnabled());
        clickWith(macro, Qt::ShiftModifier);
        auto *mapping = mappingWindow("hotkeyMapping");
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"Script shell-fixture.lua-0\"."));
        QVERIFY(!in(mapping, "hotkeyWindowChoice")->isVisible());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(application->automationHotkeys().committedKeys().at("Script shell-fixture.lua-0"), std::string("Ctrl+Alt+J"));
        QVERIFY(!application->automation().running());
    }

    // S4: Automation > "Run the last loaded script" (HikariSubFrame.cpp:353,
    // after Refresh autoload scripts) on a Document naming no scripts shows
    // legacy's modal box (HikariSubFrame.cpp:920); a script editor asked for
    // while a box shows opens once it is closed (legacy's boxes are modal).
    void runTheLastLoadedScriptSaysWhenThereIsNone()
    {
        QVERIFY(application->openFile(episode));
        QObject *run = named("loadLastScriptMenuItem");
        QVERIFY(run);
        QCOMPARE(run->property("text").toString(), QStringLiteral("Run the last loaded script"));
        QVERIFY(QMetaObject::invokeMethod(run, "click"));
        auto *notice = named("automationNotice");
        QVERIFY(notice);
        QTRY_VERIFY(notice->property("visible").toBool());
        QCOMPARE(notice->property("title").toString(), QStringLiteral("Info"));
        QCOMPARE(named("automationNoticeText")->property("text").toString(),
                 QStringLiteral("This subtitle file does not have any scripts added"));
        auto *editor = named("scriptEditorDialog");
        QVERIFY(editor);
        emit application->automation().chooseScriptEditor(QStringLiteral("/scripts/x.lua"));
        QVERIFY(!editor->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(notice, "accept"));
        QTRY_VERIFY(!notice->property("visible").toBool());
        QTRY_VERIFY(editor->property("visible").toBool());
        QCOMPARE(editor->property("title").toString(), QStringLiteral("Select a script editor"));
        QCOMPARE(editor->property("script").toString(), QStringLiteral("/scripts/x.lua"));
        QVERIFY(QMetaObject::invokeMethod(editor, "reject"));
        QTRY_VERIFY(!editor->property("visible").toBool());
    }

    // S4: legacy runs a macro behind a modal progress dialog
    // (HikariSubFrame.cpp:916-936 returns only when it ends), so "Run the
    // last loaded script" cannot be asked for meanwhile; its item (and the
    // hotkey, which triggers the same Action) is disabled while a macro runs,
    // as the macro items are.
    void runTheLastLoadedScriptIsDisabledWhileAMacroRuns()
    {
        QVERIFY(application->openFile(episode));
        auto &automation = application->automation();
        automation.load(HIKARI_LUA_FIXTURES "/shell-fixture.lua");
        QTRY_VERIFY_WITH_TIMEOUT(named("macro_Wait for cancel"), 30000);
        QObject *run = named("loadLastScriptMenuItem");
        QVERIFY(run);
        QVERIFY(run->property("enabled").toBool());
        // "Wait for cancel" (shell-fixture.lua) runs until cancelled.
        QVERIFY(automation.run(HIKARI_LUA_FIXTURES "/shell-fixture.lua", 1));
        QTRY_VERIFY(!run->property("enabled").toBool());
        QVERIFY(!named("macro_Wait for cancel")->property("enabled").toBool());
        automation.cancelRun();
        QTRY_VERIFY_WITH_TIMEOUT(!automation.running(), 30000);
        QTRY_VERIFY(run->property("enabled").toBool());
    }

    // A4: legacy AudioBox's play commands hand the player the frames each
    // asks for (AudioDisplay::Play at 48 kHz: ms * 48); the cursor follows
    // the output's clock and the player stops 8192 frames past the end.
    void audioBoxPlaybackModes()
    {
        restartWithoutSound();
        keysNeverRepeat();
        QVERIFY(application->openFile(episode)); // 1.00-2.00 active, 3.00-4.00
        auto &audio = application->audio();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv")); // 96256 frames
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QCOMPARE(focusedPanel(), QStringLiteral("audioPanel"));
        using application::PlayRange;
        auto range = [&] { return audio.lastPlayRange().value_or(PlayRange{-1, -1}); };
        QVERIFY(!audio.playing());
        item("audioDisplay")->forceActiveFocus();

        // AUDIO_PLAY (Down): the selection; the cursor is drawn from 50
        // columns in (1440 frames a column) and goes at the end, and the
        // player stops by itself
        press(Qt::Key_Down);
        QCOMPARE(range(), (PlayRange{48000, 48000}));
        QVERIFY(audio.playing());
        QTRY_VERIFY_WITH_TIMEOUT(audio.cursor() && *audio.cursor() >= 50.f, 3000);
        // the mouse does not move the cursor while playing
        auto *display = item("audioDisplay");
        QTest::mouseMove(window, display->mapToScene(QPointF(10, 10)).toPoint());
        QVERIFY(!audio.cursor() || *audio.cursor() != 10.f);
        // (the mouse leaves, or its hover would draw it once playback ends)
        QTest::mouseMove(window, QPoint(0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.playing(), 5000);
        QVERIFY(!audio.cursor());
        QCOMPARE(audio.playback()->lastPositionMs(), 0); // not a Stop

        // each mode's keys (legacy AUDIO_HOTKEY defaults)
        const std::vector<std::tuple<Qt::Key, Qt::KeyboardModifiers, PlayRange>> keys{
            {Qt::Key_S, Qt::NoModifier, {48000, 48000}},       // AUDIO_PLAY_ALT
            {Qt::Key_Up, Qt::NoModifier, {48000, 48000}},      // AUDIO_PLAY_LINE
            {Qt::Key_R, Qt::NoModifier, {48000, 48000}},       // AUDIO_PLAY_LINE_ALT
            {Qt::Key_Q, Qt::NoModifier, {24000, 24000}},       // 500 ms before
            {Qt::Key_W, Qt::NoModifier, {96000, 255}},         // 500 ms after, cut at the last frame
            {Qt::Key_E, Qt::NoModifier, {48000, 24000}},       // first 500 ms
            {Qt::Key_D, Qt::NoModifier, {72000, 24000}},       // last 500 ms
            {Qt::Key_T, Qt::NoModifier, {48000, 96255 - 48000}}, // to the end, less its last frame
        };
        for (const auto &[key, mods, expected] : keys) {
            press(key, mods);
            QCOMPARE(range(), expected);
            // a range under legacy's 100 ms plays once and may already have
            // ended on the player's own thread
            if (expected.count >= 4800)
                QVERIFY(audio.playing());
        }
        // the mark plays need a mark: without one nothing plays
        press(Qt::Key_0, Qt::KeypadModifier);
        QCOMPARE(range(), (PlayRange{48000, 96255 - 48000}));
        // the ruler's mark (A3: a right click on the ruler, 30 ms a column)
        QCOMPARE(audio.view().position(), 0);
        QTest::mouseClick(window, Qt::RightButton, {},
                          item("audioDisplay")->mapToScene(QPointF(50, audio.view().height() + 5)).toPoint());
        QVERIFY(audio.hasMark());
        QCOMPARE(audio.markMs(), 1500);
        QTest::mouseMove(window, QPoint(0, 0));
        item("audioDisplay")->forceActiveFocus();
        press(Qt::Key_0, Qt::KeypadModifier); // AUDIO_MARK_PLAY_TIME (1000) before
        QCOMPARE(range(), (PlayRange{24000, 48000}));
        press(Qt::Key_Period, Qt::KeypadModifier);
        QCOMPARE(range(), (PlayRange{72000, 96255 - 72000}));
        application->settingsStore()->set("audio.markPlayTime", 250);
        press(Qt::Key_0, Qt::KeypadModifier);
        QCOMPARE(range(), (PlayRange{60000, 12000}));
        // modified keys are not the audio box's
        press(Qt::Key_T, Qt::ShiftModifier);
        QCOMPARE(range(), (PlayRange{60000, 12000}));

        // AUDIO_STOP (H): while playing it stops and remembers where; again,
        // it plays from there to the last end
        press(Qt::Key_T);
        QTRY_VERIFY_WITH_TIMEOUT(audio.playback()->lastPositionMs() == 0 && audio.cursor(), 3000);
        press(Qt::Key_H);
        QVERIFY(!audio.playing());
        QVERIFY(!audio.cursor());
        const int at = audio.playback()->lastPositionMs();
        QVERIFY(at > 1000);
        press(Qt::Key_H);
        QVERIFY(audio.playing());
        QCOMPARE(range(), (PlayRange{at * 48, 96255 - at * 48}));
        press(Qt::Key_H);
        QVERIFY(!audio.playing());

        // the buttons, which focus the display
        item("editingGrid")->forceActiveFocus();
        auto *first = visualItem("audioPlay500First"); // Repeater delegates
        auto *stop = visualItem("audioStop");
        QVERIFY(first && stop);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, first->mapToScene(QPointF(4, 4)).toPoint());
        QCOMPARE(range(), (PlayRange{48000, 24000}));
        QCOMPARE(focusedPanel(), QStringLiteral("audioPanel"));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          stop->mapToScene(QPointF(4, 4)).toPoint());
        QVERIFY(!audio.playing());
        // the middle double click plays the selection
        QTest::mouseDClick(window, Qt::MiddleButton, Qt::NoModifier, display->mapToScene(QPointF(200, 10)).toPoint());
        QCOMPARE(range(), (PlayRange{48000, 48000}));
        press(Qt::Key_H);
        // (the mouse leaves: over the waveform it takes the focus, AUDIO_AUTO_FOCUS)
        QTest::mouseMove(window, QPoint(0, 0));

        // from the Grid: the letters and the keypad (not Down and Up, which
        // move there), and the display takes the focus
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down); // the second Line, 3.00-4.00
        QVERIFY(!audio.playing());
        QCOMPARE(focusedPanel(), QStringLiteral("gridPanel"));
        press(Qt::Key_S);
        QCOMPARE(range(), (PlayRange{96255, 0})); // past the audio: its last frame, nothing
        QCOMPARE(focusedPanel(), QStringLiteral("audioPanel"));
        QTRY_VERIFY_WITH_TIMEOUT(!audio.playing(), 3000); // silence until 8192 frames on

        // a new file in the box stops the player; Close resets Stop's memory
        press(Qt::Key_T);
        QVERIFY(audio.playing());
        audio.closeAudio();
        QVERIFY(!audio.playing());
        audio.openDummy();
        QVERIFY(audio.ready());
        QCOMPARE(audio.playback()->lastEndMs(), 5000);
        item("audioDisplay")->forceActiveFocus();
        press(Qt::Key_H); // legacy's default: 0 to 5000 ms
        QCOMPARE(range(), (PlayRange{0, 220500}));
        press(Qt::Key_H);
    }

    // A4: legacy Play pauses a playing video; Stop pauses it and leaves the
    // audio alone.
    void audioPlaybackPausesThePlayingVideo()
    {
        restartWithoutSound();
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        auto &video = application->video();
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(video.play());
        QVERIFY(video.playing());
        audio.playToEnd();
        QVERIFY(!video.playing());
        QVERIFY(audio.playing());
        QVERIFY(video.play());
        audio.stopPlayback();
        QVERIFY(!video.playing());
        QVERIFY(audio.playing());
        audio.stopPlayback();
        QVERIFY(!audio.playing());
    }

    // O2 with A2-A4: the audio box's keys are its window's bindings in the
    // hotkey registry (AudioBox::SetAccels), and the Grid takes the Audio
    // bindings but AUDIO_COMMIT to AUDIO_NEXT (TabPanel::SetAccels, handed
    // to the box by SubsGrid::OnAccelerator). A remapped key acts there and
    // the old one no longer does; an _ALT id runs its main id (GetHKey's
    // id + 10), so AUDIO_PLAY_LINE_ALT plays the line as AUDIO_PLAY_LINE.
    void audioHotkeysFollowTheRegistry()
    {
        restartWithoutSound();
        keysNeverRepeat();
        QVERIFY(application->openFile(episode)); // 1.00-2.00 active, 3.00-4.00
        auto &audio = application->audio();
        auto &h = application->hotkeys();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        const auto &view = audio.view();
        // the defaults route by window
        QCOMPARE(h.actionFor(4, Qt::Key_A, Qt::NoModifier), QStringLiteral("AUDIO_SCROLL_RIGHT"));
        QCOMPARE(h.actionFor(4, Qt::Key_R, Qt::NoModifier), QStringLiteral("AUDIO_PLAY_LINE"));
        QCOMPARE(h.actionFor(4, Qt::Key_Return, Qt::NoModifier), QStringLiteral("AUDIO_COMMIT"));
        QCOMPARE(h.actionFor(4, Qt::Key_0, Qt::KeypadModifier), QStringLiteral("AUDIO_PLAY_BEFORE_MARK"));
        QCOMPARE(h.actionFor(1, Qt::Key_X, Qt::NoModifier), QStringLiteral("AUDIO_NEXT"));  // AUDIO_NEXT_ALT
        QCOMPARE(h.actionFor(1, Qt::Key_Right, Qt::NoModifier), QString());                // AUDIO_NEXT: not the Grid's
        QCOMPARE(h.actionFor(1, Qt::Key_Return, Qt::NoModifier), QString());               // AUDIO_COMMIT neither
        QCOMPARE(h.actionFor(1, Qt::Key_Down, Qt::NoModifier), QString());                 // AUDIO_PLAY neither
        QCOMPARE(h.actionFor(2, Qt::Key_A, Qt::NoModifier), QString());                    // nor the Line editor's

        // remapped through the registry (the Options page's Hotkeys list, OK)
        h.beginOptions();
        h.select(hotkeyPosition(QStringLiteral("Audio Scroll left")));      // AUDIO_SCROLL_RIGHT, A
        h.optionsMap(QStringLiteral("K"), 4, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Audio Play line alt")));    // R
        h.optionsMap(QStringLiteral("Y"), 4, QStringLiteral("cancel"));
        h.select(hotkeyPosition(QStringLiteral("Audio Commit")));           // Enter
        h.optionsMap(QStringLiteral("J"), 4, QStringLiteral("cancel"));
        h.commitOptions();
        QCOMPARE(h.accelOf(QStringLiteral("AUDIO_SCROLL_RIGHT"), 4), QStringLiteral("K"));
        QCOMPARE(h.accelOf(QStringLiteral("AUDIO_PLAY_LINE_ALT"), 4), QStringLiteral("Y"));
        QCOMPARE(h.accelOf(QStringLiteral("AUDIO_COMMIT"), 4), QStringLiteral("J"));
        QVERIFY(application->settingsStore()->list("shortcuts.audioHotkeys").contains(QStringLiteral("AUDIO_SCROLL_RIGHT A=K")));

        // in the display: K scrolls back 50 columns, A no longer does
        item("audioDisplay")->forceActiveFocus();
        audio.setHorizontalZoom(0); // the most columns
        audio.setScrollPosition(10);
        const auto at = view.position();
        QVERIFY(at >= 100);
        press(Qt::Key_K);
        QCOMPARE(view.position(), at - 50);
        press(Qt::Key_A);
        QCOMPARE(view.position(), at - 50);
        // in the Grid too
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_K);
        QCOMPARE(view.position(), at - 100);
        press(Qt::Key_A);
        QCOMPARE(view.position(), at - 100);

        // Y plays the line (AUDIO_PLAY_LINE_ALT as AUDIO_PLAY_LINE), R no longer
        using application::PlayRange;
        auto range = [&] { return audio.lastPlayRange().value_or(PlayRange{-1, -1}); };
        item("audioDisplay")->forceActiveFocus();
        press(Qt::Key_R);
        QVERIFY(!audio.lastPlayRange());
        press(Qt::Key_Y);
        QCOMPARE(range(), (PlayRange{48000, 48000}));
        press(Qt::Key_H);
        // from the Grid, the display takes the focus (the play handlers' SetFocus)
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Y);
        QCOMPARE(range(), (PlayRange{48000, 48000}));
        QCOMPARE(focusedPanel(), QStringLiteral("audioPanel"));
        press(Qt::Key_H);

        // J commits (without AUDIO_AUTO_COMMIT the times wait for it); Enter
        // no longer does; X (AUDIO_NEXT_ALT) in the Grid goes to the next Line
        application->settingsStore()->setValue(QStringLiteral("audio.autoCommit"), false);
        audio.setHorizontalZoom(50);
        audio.setScrollPosition(0);
        auto &editor = application->editor();
        auto *display = item("audioDisplay");
        const int startX = int(view.xAtMs(1000));
        QTest::mouseMove(window, display->mapToScene(QPointF(startX, 10)).toPoint());
        QTest::mousePress(window, Qt::LeftButton, {}, display->mapToScene(QPointF(startX, 10)).toPoint());
        QTest::mouseMove(window, display->mapToScene(QPointF(startX - 10, 10)).toPoint());
        QTest::mouseRelease(window, Qt::LeftButton, {}, display->mapToScene(QPointF(startX - 10, 10)).toPoint());
        QVERIFY(audio.modified());
        const auto steps = editor.history().size();
        press(Qt::Key_Return);
        QVERIFY(audio.modified());
        QCOMPARE(editor.history().size(), steps);
        press(Qt::Key_J);
        QVERIFY(!audio.modified());
        QCOMPARE(editor.history().size(), steps + 1);
        QCOMPARE(editor.text(), QStringLiteral("second")); // AUDIO_COMMIT goes on to the next Line
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Z); // AUDIO_PREVIOUS_ALT
        QCOMPARE(editor.text(), QStringLiteral("first"));
        QTest::mouseMove(window, QPoint(0, 0));
    }

    // A3: legacy AudioDisplay::Commit ran EditBox::OnEdit after an
    // auto-commit unless DISABLE_LIVE_VIDEO_EDITING (the video's preview of
    // the edit). The rewrite's video shows the committed Document, and the
    // box's commit is one: the video takes the new times either way.
    // A5: karaoke mode in the audio box with the real mouse and keys: the
    // Karaoke button (zoom in by 20, into AUDIO_HORIZONTAL_ZOOM), the
    // syllables' plays through the player (the output without a device),
    // Next through the syllables into the next Line, a boundary dragged, a
    // letter split and a boundary joined, each one "Changing time on audio
    // spectrum" step, the automatic split switch, and the button off again.
    void audioKaraokeMode()
    {
        restartWithoutSound();
        keysNeverRepeat();
        const QString path = writeFile(dir, "karaoke.ass",
                                       "Dialogue: 0,0:00:00.20,0:00:01.20,Default,,0,0,0,,{\\k20}ka{\\k30}ra{\\k50}oke\n"
                                       "Dialogue: 0,0:00:01.30,0:00:01.90,Default,,0,0,0,,kara oke\n");
        QVERIFY(application->openFile(path));
        auto &audio = application->audio();
        auto &editor = application->editor();
        auto *store = application->settingsStore();
        editor.setShowTags(true); // the editor's text with its tags
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv")); // 48 kHz, 96256 frames
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(!audio.karaoke());
        QCOMPARE(audio.horizontalZoom(), 50);
        auto *display = item("audioDisplay");
        const auto &view = audio.view();
        auto at = [&](int x, int y) { return display->mapToScene(QPointF(x, y)).toPoint(); };
        // the switches as a click toggles them (as audioSpectrumIsDrawn: the
        // row runs past a narrow panel, and a hovered tooltip's popup upsets
        // offscreen grabs)
        auto click = [&](const char *name) {
            auto *button = item(name);
            QVERIFY(button);
            QVERIFY(QMetaObject::invokeMethod(button, "toggle"));
            QVERIFY(QMetaObject::invokeMethod(button, "toggled"));
        };
        using application::PlayRange;
        auto range = [&] { return audio.lastPlayRange().value_or(PlayRange{-1, -1}); };
        const auto steps = editor.history().size();
        auto lastStep = [&] { return editor.history().last(); };

        // the Karaoke button: the Line in syllables, 20 closer, the display focused
        click("audioKaraoke");
        QVERIFY(audio.karaoke());
        QCOMPARE(audio.karaokeModel().count(), 3);
        QVERIFY(audio.karaokeModel().times() == (std::vector<int>{400, 700, 1200}));
        QCOMPARE(audio.horizontalZoom(), 30);
        QCOMPARE(view.samplesPercent(), 30);
        QVERIFY(store->boolean("audio.karaoke"));
        // A5-karaoke-zoom-option: the horizontal zoom's setting (legacy wrote AUDIO_VERTICAL_ZOOM)
        QCOMPARE(store->integer("audio.horizontalZoom"), 30);
        QCOMPARE(store->integer("audio.verticalZoom"), 50);
        QCOMPARE(audio.verticalZoom(), 50);
        QVERIFY(item("audioKaraoke")->property("checked").toBool());
        QCOMPARE(focusedPanel(), QStringLiteral("audioPanel"));

        // AUDIO_PLAY plays the current syllable, AUDIO_PLAY_LINE the Line
        press(Qt::Key_Down);
        QCOMPARE(range(), (PlayRange{9600, 9600}));
        QVERIFY(audio.playing());
        press(Qt::Key_Up);
        QCOMPARE(range(), (PlayRange{9600, 48000}));
        // AUDIO_NEXT: the next syllable, playing it; the 500 ms plays read it too
        press(Qt::Key_Right);
        QCOMPARE(audio.currentSyllable(), 1);
        QCOMPARE(range(), (PlayRange{19200, 14400}));
        press(Qt::Key_E); // first 500 ms of 400-700
        QCOMPARE(range(), (PlayRange{19200, 14400}));
        press(Qt::Key_W); // 500 ms after 700
        QCOMPARE(range(), (PlayRange{33600, 24000}));
        // AUDIO_PREVIOUS goes back a syllable while it plays
        press(Qt::Key_Left);
        QCOMPARE(audio.currentSyllable(), 0);
        QCOMPARE(range(), (PlayRange{9600, 9600}));
        press(Qt::Key_H);
        QTRY_VERIFY_WITH_TIMEOUT(!audio.playing(), 3000);

        // drag the first boundary (400 ms) to the right: one step with the text
        const int y = 30; // the boundaries' rows (below the letters' 20)
        QVERIFY(view.height() > y);
        const int x0 = int(view.xAtMs(400));
        QTest::mouseMove(window, at(x0 - 1, y)); // (the first move into the item enters it)
        QTest::mouseMove(window, at(x0, y));
        QCOMPARE(display->cursor().shape(), Qt::SizeHorCursor);
        QTest::mousePress(window, Qt::LeftButton, {}, at(x0, y));
        QCOMPARE(audio.timing().hold(), 5);
        const int x1 = int(view.xAtMs(500)) + 1;
        QTest::mouseMove(window, at(x1, y));
        QTest::mouseRelease(window, Qt::LeftButton, {}, at(x1, y));
        const int moved = application::legacyZeroIt(view.msAtX(x1));
        QCOMPARE(audio.karaokeModel().times()[0], moved);
        QCOMPARE(editor.history().size(), steps + 1);
        QCOMPARE(lastStep(), QStringLiteral("Changing time on audio spectrum, active line 1"));
        const auto expected = QStringLiteral("{\\k%1}ka{\\k%2}ra{\\k50}oke").arg((moved - 200) / 10).arg((700 - moved) / 10);
        QCOMPARE(editor.text(), expected);

        // over the letters: no mouse cursor; a click splits "oke" before its "k"
        const int okeStart = int(view.xAtMs(700)), okeEnd = int(view.xAtMs(1200));
        int split = -1;
        for (int x = okeStart; x < okeEnd && split < 0; x++) {
            QTest::mouseMove(window, at(x, 8));
            if (audio.karaokeModel().hover == 2 && audio.karaokeModel().character == 1)
                split = x;
        }
        QVERIFY(split > 0);
        QVERIFY(!audio.cursor());
        QTest::mouseClick(window, Qt::LeftButton, {}, at(split, 8));
        QCOMPARE(audio.karaokeModel().count(), 4);
        QCOMPARE(audio.currentSyllable(), 2);
        QCOMPARE(audio.karaokeModel().times()[2], 950); // ZEROIT(700 + 500 / 2)
        QCOMPARE(editor.history().size(), steps + 2);
        QVERIFY(editor.text().endsWith(QStringLiteral("{\\k25}o{\\k25}ke")));
        // below the letters the cursor is back
        QTest::mouseMove(window, at(okeEnd - 3, y));
        QVERIFY(audio.cursor());

        // a middle click on the new boundary joins the syllables again
        QTest::mouseMove(window, at(int(view.xAtMs(950)), y));
        QTest::mouseClick(window, Qt::MiddleButton, {}, at(int(view.xAtMs(950)), y));
        QCOMPARE(audio.karaokeModel().count(), 3);
        QCOMPARE(editor.history().size(), steps + 3);
        QVERIFY(editor.text().endsWith(QStringLiteral("{\\k50}oke")));
        // a right click plays the syllable under it, which becomes the current one
        QTest::mouseClick(window, Qt::RightButton, {}, at(int(view.xAtMs(1000)), y));
        QCOMPARE(audio.currentSyllable(), 2);
        QCOMPARE(range(), (PlayRange{33600, 24000}));
        press(Qt::Key_H);
        QTest::mouseMove(window, QPoint(0, 0));
        // Undo takes the join back
        QVERIFY(editor.undo());
        QVERIFY(editor.text().endsWith(QStringLiteral("{\\k25}o{\\k25}ke")));

        // Next past the last syllable: the next Line, split automatically
        // (AUDIO_KARAOKE_SPLIT_MODE is on by default)
        item("audioDisplay")->forceActiveFocus();
        QCOMPARE(audio.karaokeModel().count(), 4); // Undo's Line split again
        for (int i = 0; i < 4; i++)
            press(Qt::Key_Right);
        QCOMPARE(editor.text(), QStringLiteral("kara oke"));
        QCOMPARE(audio.currentSyllable(), 0);
        QCOMPARE(audio.karaokeModel().count(), 4); // ka ra o ke
        QCOMPARE(range(), (PlayRange{62400, 7200}));
        click("audioKaraokeSplit"); // the switch: at spaces only
        QVERIFY(!audio.karaokeSplitMode());
        QVERIFY(!store->boolean("audio.karaokeSplitMode"));
        QCOMPARE(audio.karaokeModel().count(), 2);
        click("audioKaraokeSplit");
        QCOMPARE(audio.karaokeModel().count(), 4);

        // off again: the zoom it had, nothing drawn for karaoke
        click("audioKaraoke");
        QVERIFY(!audio.karaoke());
        QCOMPARE(audio.horizontalZoom(), 50);
        QVERIFY(!store->boolean("audio.karaoke"));
        QCOMPARE(store->integer("audio.horizontalZoom"), 50);
        press(Qt::Key_H);
    }

    void audioAutoCommitReachesTheVideo()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 active
        auto &audio = application->audio();
        auto &video = application->video();
        application->settingsStore()->setValue(QStringLiteral("video.dontAskForBadResolution"), true);
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!video.times().isEmpty(), 20000);
        auto *display = item("audioDisplay");
        const auto &view = audio.view();
        for (const bool disabled : {false, true}) {
            application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), disabled);
            const QString before = video.times();
            const int endX = int(view.xAtMs(audio.selectionEnd()));
            QTest::mouseMove(window, display->mapToScene(QPointF(endX, 10)).toPoint());
            QTest::mousePress(window, Qt::LeftButton, {}, display->mapToScene(QPointF(endX, 10)).toPoint());
            QTest::mouseMove(window, display->mapToScene(QPointF(endX + 20, 10)).toPoint());
            QTest::mouseRelease(window, Qt::LeftButton, {}, display->mapToScene(QPointF(endX + 20, 10)).toPoint());
            QVERIFY(!audio.modified()); // committed
            QTRY_VERIFY(video.times() != before); // the video's Line has the new end
            QTest::mouseMove(window, QPoint(0, 0));
        }
    }

    // A3: GLOBAL_SET_AUDIO_FROM_VIDEO and GLOBAL_SET_AUDIO_MARK_FROM_VIDEO
    // are enabled with the audio box and the editor (legacy OnMenuOpened:
    // ABox != nullptr && editor).
    void setAudioFromVideoNeedsTheBoxAndTheEditor()
    {
        auto *position = item<QObject>("setAudioFromVideoMenuItem");
        auto *mark = item<QObject>("setAudioMarkFromVideoMenuItem");
        QVERIFY(position && mark);
        auto &audio = application->audio();
        audio.openDummy();
        QVERIFY(audio.ready());
        QVERIFY(!application->workspace().editingTarget());
        QVERIFY(!position->property("enabled").toBool());
        QVERIFY(!mark->property("enabled").toBool());
        QVERIFY(application->openFile(episode));
        QVERIFY(position->property("enabled").toBool());
        QVERIFY(mark->property("enabled").toBool());
        audio.closeAudio();
        QVERIFY(!position->property("enabled").toBool());
        QVERIFY(!mark->property("enabled").toBool());
    }

    // K2 (superseding A2-theme-colours): the audio display's colours are the
    // theme layer's, fixed per theme, not settings. Dark keeps legacy's dark
    // theme (config.cpp:451-472: the spectrum's AUDIO_SPECTRUM_* among them),
    // Light has its matched values, and the selection-type marks
    // (AUDIO_SELECTION_BACKGROUND at legacy's 0x37 alpha,
    // AUDIO_WAVEFORM_SELECTED) take the accent. A theme change draws the
    // display and the spectrum again at once (legacy ChangeColors,
    // AudioSpectrum::ChangeColours).
    void audioDisplayTakesTheThemeColours()
    {
        auto &settings = *application->settingsStore();
        for (const char *id : {"audio.spectrumBackground", "audio.spectrumEcho", "audio.spectrumInner"})
            QVERIFY2(!application::findSetting(id), id);
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        audio.setSpectrumOn(true);
        // Dark (the default, the platform's scheme unknown): legacy's values.
        QCOMPARE(ui::theme::current().code, ui::theme::Code::Dark);
        const application::AudioDisplayOptions legacy;
        QCOMPARE(audio.options().background, 0xFF36393Eu);
        QCOMPARE(audio.options().spectrumBackground, 0xFF000000u);
        QCOMPARE(audio.options().spectrumEcho, 0xFF674FD7u);
        QCOMPARE(audio.options().spectrumInner, 0xFFF4F4F4u);
        QCOMPARE(audio.options().cursor, legacy.cursor);
        QCOMPARE(audio.options().waveform, legacy.waveform);
        QCOMPARE(audio.options().keyframe, legacy.keyframe);
        QCOMPARE(audio.options().selectionBackground, 0x379CDBC9u); // the green accent at 0x37
        QCOMPARE(audio.options().waveformSelected, 0xFF9CDBC9u);
        const auto first = audio.spectrumImage();
        QVERIFY(first);
        // Light: drawn again with the Light theme's colours.
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("light"));
        const auto &light = ui::theme::current().content.audio;
        QCOMPARE(audio.options().background, light.background);
        QCOMPARE(audio.options().spectrumBackground, 0xFFFFFFFFu);
        QCOMPARE(audio.options().selectionBackground, 0x37145C4Cu);
        const auto again = audio.spectrumImage();
        QVERIFY(again && again != first);
        // the same picture a fresh renderer draws with the theme's colours
        application::AudioSpectrum fresh;
        fresh.setColours(light.spectrumBackground, light.spectrumEcho, light.spectrumInner);
        fresh.setScaling(audio.view().scale());
        const auto &view = audio.view();
        std::vector<std::uint8_t> expected(std::size_t(view.width()) * view.height() * 4, 0);
        for (std::size_t i = 3; i < expected.size(); i += 4)
            expected[i] = 0xFF;
        fresh.render(*audio.box().audio(), view.position() * view.samples(), (view.position() + view.width()) * view.samples(),
                     expected.data(), view.width(), view.width(), view.height(), view.samplesPercent());
        QVERIFY(again->bgra == expected);
        // Another accent: the selection marks follow, the content stays.
        settings.setValue(QStringLiteral("appearance.lightAccent"), QStringLiteral("blue"));
        const QColor blue = ui::theme::accent(false, QStringLiteral("blue")).accent;
        QCOMPARE(audio.options().waveformSelected, blue.rgba());
        QCOMPARE(audio.options().selectionBackground, (blue.rgba() & 0x00FFFFFFu) | 0x37000000u);
        QCOMPARE(audio.options().spectrumBackground, 0xFFFFFFFFu);
        // Both high-contrast themes have their own fixed colours.
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("highContrastBlack"));
        QCOMPARE(audio.options().background, 0xFF000000u);
        QCOMPARE(audio.options().waveformSelected, 0xFFFFFF00u);
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("highContrastWhite"));
        QCOMPARE(audio.options().background, 0xFFFFFFFFu);
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark"));
        QCOMPARE(audio.options().spectrumEcho, 0xFF674FD7u);
        QCOMPARE(audio.options().background, 0xFF36393Eu);
    }

    // K2: the Options dialog's Appearance page, in legacy's Themes page's
    // place (after MuseScore 4, docs/research/musescore-appearance.md): the
    // four themes, "Follow system theme", the mode's seven accent swatches,
    // and in high contrast the accent, text-and-icons and border pickers with
    // their reset. Every choice previews live; OK and Apply save it, Cancel takes it
    // back (after Apply, back to the applied one); choosing a theme by hand
    // turns following off; Light and Dark remember their own accent; "Set
    // default" leaves the appearance; a focused card or swatch shows the
    // focus ring.
    void appearancePagePreviewsSavesAndRestores()
    {
        auto &settings = *application->settingsStore();
        auto restore = qScopeGuard([] { ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown); });
        auto *root = engine->rootObjects().first();
        const auto code = [] { return ui::theme::codeName(ui::theme::current().code); };
        const auto accent = [] { return ui::theme::current().roles.accent; };
        const auto click = [&](const char *name) {
            auto *control = dialogItem("settingsDialog", name);
            QVERIFY2(control, name);
            QVERIFY2(QMetaObject::invokeMethod(control, "click"), name);
        };
        const auto checked = [&](const char *name) { return dialogItem("settingsDialog", name)->property("checked").toBool(); };
        QCOMPARE(code(), QStringLiteral("dark"));
        QCOMPARE(root->property("color").value<QColor>(), QColor(0x17, 0x1B, 0x20)); // Dark's bg

        // The page: four themes, seven swatches, the pickers hidden.
        const auto openAppearance = [&] {
            auto *shown = openSettings();
            if (!QTest::qWaitFor([&] { return shown->property("visible").toBool(); }))
                return static_cast<QObject *>(nullptr);
            dialogItem("settingsDialog", "settingsPages")->setProperty("currentIndex", 6); // Appearance
            return shown;
        };
        auto *dialog = openAppearance();
        QVERIFY(dialog);
        QTRY_VERIFY(dialogItem("settingsDialog", "settingsPageAppearance")->isVisible());
        for (const char *theme : {"appearanceTheme_light", "appearanceTheme_dark", "appearanceTheme_highContrastWhite",
                                  "appearanceTheme_highContrastBlack"})
            QVERIFY2(dialogItem("settingsDialog", theme), theme);
        QVERIFY(checked("appearanceTheme_dark"));
        QVERIFY(checked("appearanceFollowSystem"));
        for (const auto &a : ui::theme::accents(true))
            QVERIFY2(dialogItem("settingsDialog", qPrintable(QStringLiteral("accent_") + QLatin1String(a.key))), a.key);
        QVERIFY(checked("accent_green"));
        QVERIFY(dialogItem("settingsDialog", "appearanceAccents")->property("visible").toBool());
        QVERIFY(!dialogItem("settingsDialog", "appearanceHighContrast")->property("visible").toBool());
        QCOMPARE(QAccessible::queryAccessibleInterface(dialogItem("settingsDialog", "accent_blue"))->text(QAccessible::Name),
                 QStringLiteral("Blue"));
        // Keyboard focus on a theme card or a swatch: the focus role's ring,
        // 2 wide and 3 beyond it (visual-language.md, "Keyboard focus ring"),
        // apart from the accent or text border marking the chosen one.
        for (const char *name : {"appearanceTheme_dark", "accent_green"}) {
            auto *control = dialogItem("settingsDialog", name);
            auto *background = control->property("background").value<QQuickItem *>();
            QVERIFY2(background, name);
            auto *ring = background->findChild<QQuickItem *>(QStringLiteral("focusRing"));
            QVERIFY2(ring, name);
            QVERIFY2(!ring->isVisible(), name);
            control->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY2(ring->isVisible(), name);
            const auto *border = ring->property("border").value<QObject *>();
            QCOMPARE(border->property("color").value<QColor>(), ui::theme::current().roles.focus);
            QCOMPARE(border->property("color").value<QColor>(), ui::theme::current().roles.text);
            QCOMPARE(border->property("width").toInt(), 2);
            QVERIFY(ui::theme::current().roles.focus != ui::theme::current().roles.accent);
            QCOMPARE(ring->width(), control->width() + 10);
            QCOMPARE(ring->x(), -5.0);
            dialogItem("settingsDialog", "appearanceFollowSystem")->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY2(!ring->isVisible(), name);
        }

        // Light by hand: following turns off, the window retints at once
        // (the preview), nothing is saved yet.
        click("appearanceTheme_light");
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("light"));
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("appearance.followSystem")).toBool(), false);
        QVERIFY(!checked("appearanceFollowSystem"));
        QCOMPARE(code(), QStringLiteral("light"));
        QCOMPARE(root->property("color").value<QColor>(), QColor(0xE5, 0xE9, 0xEC));
        auto *controls = root->property("palette").value<QObject *>();
        QTRY_COMPARE(controls->property("window").value<QColor>(), QColor(0xF9, 0xFA, 0xFB));
        QCOMPARE(accent(), QColor(0x14, 0x5C, 0x4C));
        click("accent_blue");
        QCOMPARE(accent(), ui::theme::accent(false, QStringLiteral("blue")).accent);
        QVERIFY(checked("accent_blue"));
        QCOMPARE(settings.text("appearance.theme"), QStringLiteral("dark"));
        // Cancel takes the preview back.
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(code(), QStringLiteral("dark"));
        QCOMPARE(accent(), QColor(0x9C, 0xDB, 0xC9));
        QTRY_COMPARE(controls->property("window").value<QColor>(), QColor(0x20, 0x26, 0x2D));
        QVERIFY(!settings.isSet(QStringLiteral("appearance.theme")));
        QVERIFY(!settings.isSet(QStringLiteral("appearance.lightAccent")));

        // Again, then OK saves it; Dark keeps its own accent.
        QVERIFY((dialog = openAppearance()) != nullptr);
        click("appearanceTheme_light");
        click("accent_blue");
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(settings.text("appearance.theme"), QStringLiteral("light"));
        QVERIFY(!settings.boolean("appearance.followSystem"));
        QCOMPARE(settings.text("appearance.lightAccent"), QStringLiteral("blue"));
        QCOMPARE(settings.text("appearance.darkAccent"), QStringLiteral("green"));
        QCOMPARE(code(), QStringLiteral("light"));
        QCOMPARE(accent(), ui::theme::accent(false, QStringLiteral("blue")).accent);
        // Apply saves too and keeps the dialog open, the applied appearance
        // showing; a later change previews, and Cancel takes back only that.
        QVERIFY((dialog = openAppearance()) != nullptr);
        click("appearanceTheme_dark");
        QVERIFY(checked("accent_green"));
        click("accent_red");
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsApply"), "click"));
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(settings.text("appearance.theme"), QStringLiteral("dark"));
        QCOMPARE(settings.text("appearance.darkAccent"), QStringLiteral("red"));
        QCOMPARE(settings.text("appearance.lightAccent"), QStringLiteral("blue"));
        QCOMPARE(code(), QStringLiteral("dark"));
        QCOMPARE(accent(), ui::theme::accent(true, QStringLiteral("red")).accent);
        QTRY_COMPARE(controls->property("window").value<QColor>(), QColor(0x20, 0x26, 0x2D));
        QVERIFY(checked("accent_red"));
        click("accent_purple");
        QCOMPARE(accent(), ui::theme::accent(true, QStringLiteral("purple")).accent);
        QCOMPARE(settings.text("appearance.darkAccent"), QStringLiteral("red"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(code(), QStringLiteral("dark"));
        QCOMPARE(accent(), ui::theme::accent(true, QStringLiteral("red")).accent);
        QCOMPARE(settings.text("appearance.darkAccent"), QStringLiteral("red"));
        QCOMPARE(settings.text("appearance.lightAccent"), QStringLiteral("blue"));

        // High contrast: the pickers in place of the swatches; a pick
        // previews, persists with OK and resets to the theme's default.
        QVERIFY((dialog = openAppearance()) != nullptr);
        click("appearanceTheme_highContrastBlack");
        QCOMPARE(code(), QStringLiteral("highContrastBlack"));
        QTRY_VERIFY(dialogItem("settingsDialog", "appearanceHighContrast")->property("visible").toBool());
        QVERIFY(!dialogItem("settingsDialog", "appearanceAccents")->property("visible").toBool());
        for (const char *pick : {"highContrastPick_appearance.highContrastBlack.accent",
                                 "highContrastPick_appearance.highContrastBlack.text",
                                 "highContrastPick_appearance.highContrastBlack.border"})
            QVERIFY2(dialogItem("settingsDialog", pick), pick);
        QVERIFY(QMetaObject::invokeMethod(dialog, "put", Q_ARG(QVariant, QStringLiteral("appearance.highContrastBlack.accent")),
                                          Q_ARG(QVariant, QStringLiteral("#00FF00"))));
        QVERIFY(QMetaObject::invokeMethod(dialog, "put", Q_ARG(QVariant, QStringLiteral("appearance.highContrastBlack.border")),
                                          Q_ARG(QVariant, QStringLiteral("#FF00FF"))));
        QCOMPARE(accent(), QColor(0x00, 0xFF, 0x00));
        QCOMPARE(ui::theme::current().roles.line, QColor(0xFF, 0x00, 0xFF));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(settings.text("appearance.highContrastBlack.accent"), QStringLiteral("#00FF00"));
        QCOMPARE(settings.text("appearance.highContrastBlack.border"), QStringLiteral("#FF00FF"));
        QCOMPARE(accent(), QColor(0x00, 0xFF, 0x00));
        // High contrast white keeps its own colours.
        QCOMPARE(settings.text("appearance.highContrastWhite.accent"), QStringLiteral("#0037B3"));
        // "Set default" leaves the appearance; the reset puts the picks back.
        QVERIFY((dialog = openAppearance()) != nullptr);
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsDefault"), "click"));
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("appearance.highContrastBlack.accent")).toString(),
                 QStringLiteral("#00FF00"));
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("highContrastBlack"));
        click("resetHighContrast");
        QCOMPARE(settingsValues(dialog).value(QStringLiteral("appearance.highContrastBlack.accent")).toString(),
                 QStringLiteral("#FFFF00"));
        QCOMPARE(accent(), QColor(0xFF, 0xFF, 0x00));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(settings.text("appearance.highContrastBlack.accent"), QStringLiteral("#FFFF00"));
        QCOMPARE(settings.text("appearance.highContrastBlack.border"), QStringLiteral("#FFFFFF"));

        // Follow system theme: Light or Dark from the platform's scheme,
        // live; it keeps high contrast (black <-> white), never turns it on
        // or off.
        QVERIFY((dialog = openAppearance()) != nullptr);
        click("appearanceFollowSystem");
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(settings.boolean("appearance.followSystem"));
        ui::theme::forceSystemScheme(Qt::ColorScheme::Light);
        QCOMPARE(code(), QStringLiteral("highContrastWhite"));
        ui::theme::forceSystemScheme(Qt::ColorScheme::Dark);
        QCOMPARE(code(), QStringLiteral("highContrastBlack"));
        settings.setValue(QStringLiteral("appearance.theme"), QStringLiteral("dark"));
        QCOMPARE(code(), QStringLiteral("dark"));
        QCOMPARE(accent(), ui::theme::accent(true, QStringLiteral("red")).accent);
        ui::theme::forceSystemScheme(Qt::ColorScheme::Light);
        QCOMPARE(code(), QStringLiteral("light"));
        QCOMPARE(accent(), ui::theme::accent(false, QStringLiteral("blue")).accent); // Light's own accent
        QCOMPARE(root->property("color").value<QColor>(), QColor(0xE5, 0xE9, 0xEC));
        QTRY_COMPARE(controls->property("window").value<QColor>(), QColor(0xF9, 0xFA, 0xFB));
        // Choosing a theme by hand on the page turns following off again.
        QVERIFY((dialog = openAppearance()) != nullptr);
        QVERIFY(checked("appearanceFollowSystem"));
        QVERIFY(checked("appearanceTheme_light"));
        click("appearanceTheme_dark");
        QVERIFY(!checked("appearanceFollowSystem"));
        QCOMPARE(code(), QStringLiteral("dark")); // the system says Light, the choice wins
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsOk"), "click"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(!settings.boolean("appearance.followSystem"));
        ui::theme::forceSystemScheme(Qt::ColorScheme::Light);
        QCOMPARE(code(), QStringLiteral("dark"));
    }

    // K2: live switching retints every surface without a restart: the
    // window's background, the controls' palette, the panels' title bars,
    // the other windows, the icons, the Grid (painted rows) and the audio
    // display.
    void themeSwitchRetintsEverySurface()
    {
        auto &settings = *application->settingsStore();
        QVERIFY(application->openFile(episode));
        application->audio().openDummy();
        QTRY_VERIFY(application->audio().ready());
        auto *root = engine->rootObjects().first();
        auto *grid = item("editingGrid");
        QVERIFY(grid);
        const auto gridPixel = [&] {
            const QImage shot = window->grabWindow();
            const qreal dpr = window->effectiveDevicePixelRatio();
            const QPointF at = grid->mapToScene(QPointF(grid->width() - 4, grid->height() - 4));
            return QColor(shot.pixel(qRound(at.x() * dpr), qRound(at.y() * dpr)));
        };
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        for (const auto theme : ui::theme::kCodes) {
            settings.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(theme));
            const auto &roles = ui::theme::current().roles;
            QCOMPARE(root->property("color").value<QColor>(), roles.background);
            auto *controls = root->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), roles.panel);
            QTRY_COMPARE(controls->property("highlight").value<QColor>(), roles.accent);
            // D3: a lone panel's header in the body's colour, a tab strip
            // `raised` with the selected tab in the body's colour.
            int headers = 0;
            for (auto *bar : root->findChildren<QQuickItem *>(QStringLiteral("dockHeader"))) {
                if (!bar->isVisible())
                    continue;
                ++headers;
                auto *strip = findItem(bar, QStringLiteral("dockHeaderStrip"));
                QVERIFY(strip);
                const bool titleMode = bar->property("titleMode").toBool();
                QCOMPARE(strip->property("color").value<QColor>(), titleMode ? roles.field : roles.raised);
                if (!titleMode)
                    QCOMPARE(findItem(bar->property("currentTab").value<QQuickItem *>(), QStringLiteral("dockTabBackground"))
                                 ->property("color").value<QColor>(), roles.field);
            }
            QVERIFY(headers >= 4);
            // every other window (a Window draws white unless told)
            const auto windows = root->findChildren<QQuickWindow *>();
            QVERIFY(windows.size() >= 10);
            for (auto *other : windows)
                QVERIFY2(other->color() == roles.panel || other->color() == roles.background,
                         qPrintable(other->objectName() + QLatin1Char(' ') + other->color().name()));
            QCOMPARE(ui::IconTheme::colours()[0], roles.text);
            // the Grid's empty area below the rows: the panel surface
            QTRY_COMPARE(gridPixel(), roles.panel);
            QCOMPARE(application->audio().options().background, ui::theme::current().content.audio.background);
        }
    }

    // K2 focus (visual-language.md, "Keyboard focus"): the panel holding
    // keyboard focus rings its header (D3: its dock header row), 2 wide just
    // inside it in the focus role (the theme's text colour), and the ring
    // moves with the focus; the panel has no accent border (D1 drew a
    // focused panel's border 2 wide in the accent). The Grid's focused
    // cell, its current row, has its own ring, painted when the Grid takes
    // the focus and gone when it leaves. Every theme.
    void keyboardFocusRingsThePanelHeaderAndTheGridRow()
    {
        auto &settings = *application->settingsStore();
        QVERIFY(application->openFile(episode));
        auto *root = engine->rootObjects().first();
        auto *grid = item("editingGrid");
        auto *text = item("lineText");
        auto *gridPanel = visualItem("gridPanel");
        QVERIFY(grid && text && gridPanel);
        grid->forceActiveFocus(Qt::TabFocusReason);
        press(Qt::Key_Home);
        const auto ringed = [&] {
            QStringList out;
            for (auto *bar : root->findChildren<QQuickItem *>(QStringLiteral("dockHeader"))) {
                auto *ring = bar->findChild<QQuickItem *>(QStringLiteral("focusRing"));
                if (!ring)
                    return QStringList{QStringLiteral("a header without a ring")};
                if (bar->isVisible() && ring->isVisible())
                    out << bar->property("currentTitle").toString();
            }
            return out;
        };
        const auto ringOf = [&](const QString &title) -> QQuickItem * {
            for (auto *bar : root->findChildren<QQuickItem *>(QStringLiteral("dockHeader")))
                if (bar->isVisible() && bar->property("currentTitle").toString() == title)
                    return bar->findChild<QQuickItem *>(QStringLiteral("focusRing"));
            return nullptr;
        };
        const auto pixel = [&](QQuickItem *on, QPointF at) {
            const QImage shot = window->grabWindow();
            const qreal dpr = window->effectiveDevicePixelRatio();
            const QPointF p = on->mapToScene(at) * dpr;
            return QColor(shot.pixel(int(std::floor(p.x())), int(std::floor(p.y()))));
        };
        const double rh = grid->property("rowHeight").toDouble();
        // the ring's top side on the first row, below the header (its left
        // side lies under a selected row's marker)
        const QPointF currentRowRing(grid->width() / 2, rh + 1.5);
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        for (const auto theme : ui::theme::kCodes) {
            settings.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(theme));
            const auto &roles = ui::theme::current().roles;
            QCOMPARE(roles.focus, roles.text);
            grid->forceActiveFocus(Qt::TabFocusReason);
            QTRY_COMPARE(ringed(), QStringList{QStringLiteral("Grid")});
            auto *ring = ringOf(QStringLiteral("Grid"));
            QVERIFY(ring);
            const auto *border = ring->property("border").value<QObject *>();
            QCOMPARE(border->property("color").value<QColor>(), roles.focus);
            QCOMPARE(border->property("width").toInt(), 2);
            auto *bar = ring->parentItem();
            QVERIFY(QRectF(0, 0, bar->width(), bar->height()).contains(ring->mapRectToItem(bar, ring->boundingRect())));
            QTRY_COMPARE(pixel(ring, QPointF(0.5, ring->height() / 2)), roles.focus);
            QTRY_COMPARE(pixel(grid, currentRowRing), roles.focus);
            // D3: the panel has no frame of its own (the separators are the
            // boundaries between panels): its body's colour to its edge.
            for (const QPointF at : {QPointF(0.5, gridPanel->height() / 2), QPointF(1.5, gridPanel->height() / 2)})
                QTRY_COMPARE(pixel(gridPanel, at), roles.field);
            // The Line editor takes the focus: its header is ringed, the
            // Grid's ring and its row's ring are gone.
            text->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(ringed().size() == 1 && ringed().front().startsWith(QStringLiteral("Line editor")));
            QTRY_VERIFY(pixel(grid, currentRowRing) != roles.focus);
        }
    }

    // A4-wasapi-default: the audio box's output is made with the host API
    // audio.outputHostApi names (Windows: 0 WASAPI, 1 DirectSound; here the
    // Windows choice is asked for and the output recorded). A change takes
    // effect when the output next opens: at once while idle, after the
    // playback while playing.
    void audioOutputFollowsTheHostApiSetting()
    {
        delete engine;
        delete application;
        std::vector<std::string> made;
        app::Application::Options options;
        options.playbackAudio = false;
        options.outputHostApiSetting = true;
        options.makeAudioOutput = [&made](const backends::PortAudioOutput::Options &o) {
            made.push_back(o.hostApi);
            return std::make_unique<backends::SimulatedOutput>();
        };
        application = new app::Application(options);
        engine = new QQmlApplicationEngine;
        hikari::ui::attachDocking(*engine);
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(made.empty()); // PortAudio starts at the first play
        audio.playLine();
        QVERIFY(audio.playing());
        QCOMPARE(made, std::vector<std::string>{"Windows WASAPI"});
        // while playing the output stays; after the stop the next play makes it again
        auto &settings = *application->settingsStore();
        settings.setValue(QStringLiteral("audio.outputHostApi"), 1);
        audio.stopPlayback();
        QCOMPARE(made.size(), std::size_t(1));
        audio.playLine();
        QCOMPARE(made, (std::vector<std::string>{"Windows WASAPI", "Windows DirectSound"}));
        // while idle the output is let go at once
        audio.stopPlayback();
        QVERIFY(!audio.playing());
        settings.setValue(QStringLiteral("audio.outputHostApi"), 0);
        audio.playLine();
        QCOMPARE(made, (std::vector<std::string>{"Windows WASAPI", "Windows DirectSound", "Windows WASAPI"}));
        audio.stopPlayback();
        // the same choice again keeps the output
        audio.playLine();
        QCOMPARE(made.size(), std::size_t(3));
        audio.stopPlayback();
        // elsewhere the setting does nothing: PortAudio's own default
        QCOMPARE(backends::PortAudioOutput::hostApiForSetting(1, false), backends::PortAudioOutput::defaultHostApi());
        QCOMPARE(backends::PortAudioOutput::hostApiForSetting(1, true), std::string("Windows DirectSound"));
        QCOMPARE(backends::PortAudioOutput::hostApiForSetting(0, true), std::string("Windows WASAPI"));
    }

    // T1: the tool rail, the shared view placing the frame, the crosshair
    // over the video, VIDEO_COPY_COORDS at the pointer and a Ctrl+click's
    // \pos as one history step.
private:
    QString visualDocument(const char *name, const char *extra = "")
    {
        const QString path = dir.filePath(QLatin1String(name));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        // The video's own resolution, so no resolution question pops up.
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n");
        f.write(extra);
        return path;
    }
    QPoint videoPoint(QPointF local) const
    {
        return item("visualOverlay")->mapToScene(local).toPoint();
    }
    // T2: a Line's \move arguments, as written.
    static std::optional<QStringList> moveArguments(const QString &text)
    {
        static const QRegularExpression move(QStringLiteral("\\\\move\\(([^)]*)\\)"));
        const auto m = move.match(text);
        if (!m.hasMatch())
            return std::nullopt;
        const QStringList args = m.captured(1).split(QLatin1Char(','));
        return args.size() == 6 ? std::optional(args) : std::nullopt;
    }
    // T2: the \move Move's two points write (Move::SetMove and
    // ChangeVisual, VisualMove.cpp:405-418, 460-479): the start stays, the
    // end is the start plus the points' distance in the view scaled by the
    // \move's time over the time from its start to the video's frame, and
    // the times are the \move's relative to the Line's start.
    void checkTwoPointMove(const QString &text, application::visual::PointF fromScript, QPoint p1, QPoint p2,
                           int moveStart, int moveEnd, int lineStart, int videoMs)
    {
        using application::visual::PointF;
        const auto &view = application->visualTools().videoView();
        auto device = [&](QPoint scene) {
            const QPointF l = item("visualOverlay")->mapFromScene(QPointF(scene));
            return PointF{float(view.toDevice(l.x())), float(view.toDevice(l.y()))};
        };
        const PointF from = view.scriptToView(fromScript);
        const PointF a = device(p1), b = device(p2);
        const float scale = float(moveEnd - moveStart) / float(videoMs - moveStart);
        const PointF to = view.viewToScript({from.x + (b.x - a.x) * scale, from.y + (b.y - a.y) * scale});
        auto ft = [](float v) {
            const auto t = application::legacy::floatText(v);
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        const auto args = moveArguments(text);
        QVERIFY2(args, qPrintable(text));
        const double expected[4] = {fromScript.x, fromScript.y, to.x, to.y};
        for (int i = 0; i < 4; ++i)
            QVERIFY2(std::abs((*args)[i].toDouble() - expected[i]) <= 0.01,
                     qPrintable(QStringLiteral("%1: %2 for %3").arg(text).arg(i).arg(expected[i])));
        QCOMPARE((*args)[4], ft(float(moveStart - lineStart)));
        QCOMPARE((*args)[5], ft(float(moveEnd - lineStart)));
    }

private slots:

    void visualToolRailCrosshairAndCopyCoordinates()
    {
        // VIDEO_COPY_COORDS has no default key and no row in the shortcut
        // editor (legacy Hotkeys.h:66 gives it no name), so it is bound the
        // way legacy's Hotkeys.txt would: a Video window line.
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString ini = home.filePath(QStringLiteral("hikari.ini"));
        ui::SettingsStore(ini).set("shortcuts.hotkeys", QStringList{QStringLiteral("VIDEO_COPY_COORDS V=Ctrl-Shift-K")});
        restartWithSettings(ini);
        QCOMPARE(application->hotkeys().actionFor(3, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier),
                 QStringLiteral("VIDEO_COPY_COORDS"));
        QVERIFY(application->openFile(visualDocument("visual.ass")));
        auto &tools = application->visualTools();
        // The rail: legacy VideoToolbar's eleven families, the crosshair on.
        for (int i = 0; i < application::visual::kFamilyCount; ++i) {
            auto *button = visualItem(qPrintable(QStringLiteral("visualTool%1").arg(i)));
            QVERIFY(button);
            const auto tip = application::visual::families()[i].tooltip;
            QCOMPARE(button->property("text").toString(), QString::fromLatin1(tip.data(), tip.size()));
            QCOMPARE(button->property("checked").toBool(), i == 0);
            QVERIFY(button->isEnabled());
            // K1: the family's icon of the set (legacy's bitmaps). The rail is
            // an icon-only tool strip (visual-language.md, "Tool strips"):
            // the icon is the button's content, the name its tooltip and
            // accessible name, the on family the style's checked button,
            // reached by Tab.
            auto *icon = visualItem(qPrintable(QStringLiteral("visualToolIcon%1").arg(i)));
            QVERIFY(icon && icon->isVisible());
            QVERIFY(icon->property("valid").toBool());
            QCOMPARE(button->property("contentItem").value<QQuickItem *>(), icon);
            QCOMPARE(button->property("display").toInt(), 0); // AbstractButton.IconOnly
            const QString name = QString::fromLatin1(tip.data(), tip.size());
            QVERIFY2(QQmlProperty(button, QStringLiteral("ToolTip.text"), qmlContext(button)).read().toString().startsWith(name),
                     qPrintable(name));
            QCOMPARE(QQmlProperty(button, QStringLiteral("Accessible.name"), qmlContext(button)).read().toString(), name);
            QAccessibleInterface *a11y = QAccessible::queryAccessibleInterface(button);
            QVERIFY(a11y);
            QCOMPARE(a11y->text(QAccessible::Name), name);
            QVERIFY(button->property("focusPolicy").toInt() & Qt::TabFocus);
            QVERIFY(button->width() <= 32);
        }
        QCOMPARE(visualItem("visualToolIcon0")->property("iconRole").toString(), QStringLiteral("tool-crosshair"));
        QCOMPARE(visualItem("visualToolIcon10")->property("iconRole").toString(), QStringLiteral("tool-all-tags"));
        // No family name is drawn on the rail, which stays a narrow strip.
        {
            QQuickItem *rail = visualItem("visualToolRail");
            QVERIFY(rail);
            QVERIFY2(rail->width() <= 40, qPrintable(QString::number(rail->width())));
            QStringList names;
            for (const auto &f : application::visual::families())
                names << QString::fromLatin1(f.tooltip.data(), f.tooltip.size());
            QList<QQuickItem *> pending{rail};
            while (!pending.isEmpty()) {
                QQuickItem *it = pending.takeLast();
                pending << it->childItems();
                if (it->isVisible() && it->inherits("QQuickText"))
                    QVERIFY2(!names.contains(it->property("text").toString()), qPrintable(it->property("text").toString()));
            }
        }
        // A Video panel too short for the eleven families: the rail shows a
        // scroll bar beside them (the families below are reached by the
        // mouse and the bar says there are more), still a narrow strip; tall
        // enough, no bar.
        {
            QQuickItem *rail = visualItem("visualToolRail");
            auto *bar = visualItem("visualToolRailScrollBar");
            QVERIFY(bar);
            const QSize was = window->size();
            window->resize(1100, 520);
            QTRY_VERIFY(rail->property("overflows").toBool());
            QTRY_VERIFY(bar->isVisible());
            QVERIFY2(rail->width() <= 40, qPrintable(QString::number(rail->width())));
            auto *last = visualItem("visualTool10");
            QVERIFY(last->mapToItem(rail, QPointF(0, 0)).x() + last->width() <= bar->mapToItem(rail, QPointF(0, 0)).x());
            window->resize(1600, 1400);
            QTRY_VERIFY(!rail->property("overflows").toBool());
            QTRY_VERIFY(!bar->isVisible());
            window->resize(was);
        }
        // The crosshair has no options or values to edit and edits no batch:
        // no tool strip below the video, and no "Targets" readout anywhere.
        QVERIFY(!visualItem("visualToolValues")->isVisible());
        QVERIFY(!visualItem("visualBatch"));
        QVERIFY(tools.overlay().isEmpty()); // no video: no tools (VideoBox state None)
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(item("videoPresenter")->property("presentedGeneration").toULongLong() > 0, 20000);
        // The presenter draws the frame in the shared view's rectangle
        // (legacy UpdateRects in device pixels).
        const auto &view = tools.videoView();
        QCOMPARE(item("videoPresenter")->property("videoRect").toRectF(), tools.videoRect());
        QCOMPARE(view.frameWidth(), 320);
        QCOMPARE(view.frameHeight(), 240);
        const auto r = view.videoRect();
        QVERIFY(r.width() > 0 && r.height() > 0);
        QVERIFY(r.left == 0 || r.top == 0); // fitted: bars on one axis at most
        QCOMPARE(view.scriptWidth(), 320);

        // The crosshair follows the pointer over the video.
        const QPointF centre = tools.videoRect().center();
        const QPoint p = videoPoint(centre);
        QTest::mouseMove(window, p);
        QTRY_VERIFY(!tools.overlay().isEmpty());
        QVERIFY(tools.hideCursor());
        const QVariantList shapes = tools.overlay();
        QCOMPARE(shapes.size(), 5); // two black lines, two white, the label
        const QString label = shapes.last().toMap().value(QStringLiteral("text")).toString();
        const QStringList xy = label.split(QStringLiteral(", "));
        QCOMPARE(xy.size(), 2);
        const int dx = view.toDevice(centre.x()), dy = view.toDevice(centre.y());
        const auto script = view.viewToScript({float(dx), float(dy)});
        QVERIFY2(std::abs(xy[0].toInt() - script.x) <= 2 && std::abs(xy[1].toInt() - script.y) <= 2, qPrintable(label));
        QTRY_COMPARE(visualItem("visualValue_position")->property("text").toString(), label);

        // VIDEO_COPY_COORDS at the pointer, in legacy's text form "x,y",
        // through the Video window binding: VideoBox::OnAccelerator takes the
        // pointer's position (VideoBox.cpp:1151). Approved departure
        // T1-copy-coords-view: the text is the script position under the
        // pointer, the crosshair's conversion (its coefficients,
        // VisualCross.cpp:80-92, and the tools' zoom, 94-97) truncated as
        // its label is, computed here, not with copyCoordinatesText. Legacy
        // OnCopyCoords (VideoBox.cpp:1395-1412) scaled by the whole client
        // less one pixel, which a bar puts off.
        QGuiApplication::clipboard()->clear();
        item("videoPanel")->forceActiveFocus();
        const QPoint k = p + QPoint(7, 5);
        QTest::mouseMove(window, k);
        QTRY_VERIFY(!tools.overlay().isEmpty());
        press(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        const QPointF at = item("visualOverlay")->mapFromScene(QPointF(k));
        const int devX = view.toDevice(at.x()), devY = view.toDevice(at.y());
        QVERIFY(r.left > 0 || r.top > 0); // a bar, where legacy's text was off
        const float crossX = float(view.scriptWidth()) / float(r.width() - (r.left ? 0 : 1));
        const float crossY = float(view.scriptHeight()) / float(r.height() - (r.top ? 0 : 1));
        const int sx = int(((devX / view.zoomScale().x) + view.zoomMove().x) * crossX);
        const int sy = int(((devY / view.zoomScale().y) + view.zoomMove().y) * crossY);
        const QString copied = QStringLiteral("%1,%2").arg(sx).arg(sy);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), copied);
        QCOMPARE(tools.copied(), copied);
        // The crosshair's label at the same pointer reads the same position.
        QCOMPARE(visualItem("visualValue_position")->property("text").toString(), QStringLiteral("%1, %2").arg(sx).arg(sy));
        const float legacyX = float(view.scriptWidth()) / float(view.clientWidth() - 1);
        const float legacyY = float(view.scriptHeight()) / float(view.clientHeight() - view.panelHeight() - 1);
        QVERIFY(copied != QStringLiteral("%1,%2").arg(int(float(devX) * legacyX)).arg(int(float(devY) * legacyY)));

        // Ctrl+click: \pos at the pointer into the active Line, one step.
        auto *session = application->files().session(*application->workspace().editingTarget());
        const std::size_t steps = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, p);
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual positioning tool"));
        QVERIFY2(text(session->document().lines()[0]).startsWith(QStringLiteral("{\\pos(")),
                 qPrintable(text(session->document().lines()[0])));
        QCOMPARE(text(session->document().lines()[1]), QStringLiteral("second"));
        // Leaving the video hides it.
        QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2));
        QTRY_VERIFY(tools.overlay().isEmpty());
        QVERIFY(!tools.hideCursor());
    }

    // T1: the transaction rule (accepted on #55) through the real panel: a
    // drag commits on release as one step, Esc during it restores the
    // pre-gesture draft, the batch picker's Lines are the targets whatever
    // the active Line, and a tool other than the crosshair is blocked with
    // the legacy warning outside its Line's time (VIDEO_VISUAL_WARNINGS_OFF
    // hides the text).
    void visualGesturesCommitOnReleaseAndEscCancels()
    {
        QVERIFY(application->openFile(visualDocument("gesture.ass")));
        auto &tools = application->visualTools();
        tools.setTool(application::visual::Family::Position, std::make_unique<DragTool>());
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().frame() == 24, 20000); // the active Line's start
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool1"), "click"));
        QCOMPARE(tools.activeFamily(), 1);
        QVERIFY(visualItem("visualTool1")->property("checked").toBool());
        QVERIFY(!visualItem("visualTool0")->property("checked").toBool());

        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto lines = session->document().lines();
        const core::LineId first = lines[0]->id, second = lines[1]->id;
        QVERIFY(session->editDraftText(first, u8"typed"));
        const std::size_t steps = session->historySize();
        const QPoint p = videoPoint(tools.videoRect().center());
        // Esc during the drag: nothing reaches the Document, the draft stays.
        QTest::mouseMove(window, p);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseMove(window, p + QPoint(10, 6));
        QTRY_VERIFY(tools.gestureActive());
        QCOMPARE(text(session->document().lines()[0]), QStringLiteral("first"));
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!tools.gestureActive());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p + QPoint(10, 6));
        QCOMPARE(session->historySize(), steps);
        QVERIFY(session->draftText() == std::optional<std::u8string>(u8"typed"));
        QCOMPARE(text(session->document().lines()[0]), QStringLiteral("first"));

        // A drag commits on release: the draft first (its own step), then
        // one step for the whole drag.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(window, p + QPoint(2 * i, i));
        QCOMPARE(session->historySize(), steps);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p + QPoint(10, 5));
        QTRY_COMPARE(session->historySize(), steps + 2);
        QCOMPARE(session->history()[session->historySize() - 2].name, std::string("Edit Line"));
        QCOMPARE(session->history().back().name, std::string("Visual positioning tool"));
        QVERIFY2(text(session->document().lines()[0]).endsWith(QStringLiteral(")}typed")),
                 qPrintable(text(session->document().lines()[0])));
        QCOMPARE(text(session->document().lines()[1]), QStringLiteral("second"));

        // The batch picker: both Lines picked, then the selection and the
        // active Line move to the second alone; the drag still edits both.
        // It is an icon-only button whose state is its checked look (the
        // count in its tooltip); Clear shows only while there is a batch.
        auto *pick = visualItem("visualPickBatch");
        QVERIFY(pick && pick->isVisible());
        QCOMPARE(pick->property("display").toInt(), 0); // AbstractButton.IconOnly
        QCOMPARE(pick->property("iconRole").toString(), QStringLiteral("pick-lines"));
        QCOMPARE(QQmlProperty(pick, QStringLiteral("Accessible.name"), qmlContext(pick)).read().toString(),
                 QStringLiteral("Pick selected lines"));
        QVERIFY(!pick->property("checked").toBool());
        QVERIFY(!visualItem("visualClearBatch")->isVisible());
        application->selectAllLines();
        QVERIFY(QMetaObject::invokeMethod(pick, "click"));
        QCOMPARE(tools.batchCount(), 2);
        QTRY_VERIFY(pick->property("checked").toBool());
        QVERIFY(pick->property("tip").toString().contains(QStringLiteral("2 line")));
        QTRY_VERIFY(visualItem("visualClearBatch")->isVisible());
        QCOMPARE(visualItem("visualClearBatch")->property("iconRole").toString(), QStringLiteral("clear"));
        application->selectLine(second.value);
        const std::size_t before = session->historySize();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseMove(window, p + QPoint(4, 4));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p + QPoint(4, 4));
        QTRY_COMPARE(session->historySize(), before + 1);
        QVERIFY(text(session->document().lines()[0]).startsWith(QStringLiteral("{\\pos(")));
        QVERIFY(text(session->document().lines()[1]).startsWith(QStringLiteral("{\\pos(")));
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualClearBatch"), "click"));
        QCOMPARE(tools.batchCount(), 0);
        QTRY_VERIFY(!visualItem("visualClearBatch")->isVisible());
        QVERIFY(!pick->property("checked").toBool());

        // Outside the Line's time: the warning, and the tool takes nothing.
        QVERIFY(application->video().showFrameAt(0));
        QTRY_COMPARE(tools.warning(), QStringLiteral("Line is not visible on video\nor has zero duration"));
        QTRY_VERIFY(item("visualWarning")->isVisible());
        // Approved departure T1-warning-centre: centred on the video
        // rectangle (legacy's wx path, Visuals.cpp:479-493), not in the
        // window's corner to the video's far edges as legacy's Direct3D path
        // drew it (Visuals.cpp:531-546), off-centre with a bar.
        {
            const QQuickItem *warning = item("visualWarning");
            const QRectF video = tools.videoRect();
            QVERIFY(video.left() > 0 || video.top() > 0);
            QVERIFY2(std::abs(warning->x() + warning->width() / 2 - video.center().x()) <= 0.5,
                     qPrintable(QStringLiteral("%1 in %2").arg(warning->x()).arg(video.left())));
            QVERIFY2(std::abs(warning->y() + warning->height() / 2 - video.center().y()) <= 0.5,
                     qPrintable(QStringLiteral("%1 in %2").arg(warning->y()).arg(video.top())));
        }
        const std::size_t blocked = session->historySize();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p);
        QVERIFY(!tools.gestureActive());
        QCOMPARE(session->historySize(), blocked);
        application->settingsStore()->setValue(QStringLiteral("video.visualWarningsOff"), true);
        QTRY_COMPARE(tools.warning(), QString());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p);
        QCOMPARE(session->historySize(), blocked); // still blocked (blockevents)

        // The family again goes back to the crosshair.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool1"), "click"));
        QCOMPARE(tools.activeFamily(), 0);
    }

    // T2: Position and Move on the video with the real mouse and keys. The
    // Line is a 40-pixel square drawn by libass centred on its \pos (\an5),
    // so the handle the tool draws and libass's rendering of the \pos it
    // writes are compared in the frame's pixels; the video shows the staged
    // \pos while the button is down (legacy's dummy rendering). Then the
    // rail's options (by rectangle: legacy's +2 and alignment arithmetic,
    // checked against the rendered square's edges), a double click, a key
    // nudge, Esc, and Move's drag and two points with the video stepped.
    void visualPositionAndMoveTools()
    {
        using application::visual::PointF;
        const char *extra =
            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an5\\pos(100,80)\\bord0\\shad0\\p1}m 0 0 l 40 0 40 40 0 40\n";
        QVERIFY(application->openFile(visualDocument("t2.ass", extra)));
        auto &tools = application->visualTools();
        auto *session = application->files().session(*application->workspace().editingTarget());
        const core::LineId sign = session->document().lines()[2]->id;
        auto signText = [&] {
            for (const auto *l : session->document().lines())
                if (l->id == sign)
                    return text(l);
            return QString();
        };
        application->selectLine(sign.value);
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().frame() == 24, 20000);
        const auto &view = tools.videoView();
        auto ft = [](float v) {
            const auto t = application::legacy::floatText(v);
            return QString::fromUtf8(reinterpret_cast<const char *>(t.data()), qsizetype(t.size()));
        };
        // The rendered square (alpha above the bottom-aligned Lines): its
        // bounds in frame pixels.
        auto rendered = [&]() -> std::optional<QRect> {
            const auto o = application->video().session().lastOverlay();
            if (!o || o->empty)
                return std::nullopt;
            int minX = o->width, minY = o->height, maxX = -1, maxY = -1;
            for (int y = 0; y < o->height * 7 / 10; ++y)
                for (int x = 0; x < o->width; ++x)
                    if (o->pixels[static_cast<std::size_t>(y) * o->stride + x * 4 + 3] > 127) {
                        minX = std::min(minX, x), maxX = std::max(maxX, x);
                        minY = std::min(minY, y), maxY = std::max(maxY, y);
                    }
            if (maxX < 0)
                return std::nullopt;
            return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        };
        // A script point in frame pixels (PlayRes is the video's size).
        auto frameOf = [&](PointF script) {
            return QPointF(script.x * view.frameWidth() / view.scriptWidth(),
                           script.y * view.frameHeight() / view.scriptHeight());
        };
        auto near = [](QPointF a, QPointF b, double d) { return std::abs(a.x() - b.x()) <= d && std::abs(a.y() - b.y()) <= d; };
        auto squareAt = [&](PointF script) {
            const auto r = rendered();
            return r && near(QRectF(*r).adjusted(0, 0, 1, 1).center(), frameOf(script), 1.0);
        };
        // The tool's handle square in the overlay (logical coordinates).
        auto handle = [&]() -> std::optional<QPointF> {
            for (const QVariant &v : tools.overlay()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("type")).toString() != QStringLiteral("polygon"))
                    continue;
                const QVariantList pts = polygonPoints(m);
                if (pts.size() != 4)
                    continue;
                return (pts[0].toPointF() + pts[2].toPointF()) / 2;
            }
            return std::nullopt;
        };
        auto deviceToFrame = [&](QPointF logical) {
            const QRectF video = tools.videoRect();
            return QPointF((logical.x() - video.left()) * view.frameWidth() / video.width(),
                           (logical.y() - video.top()) * view.frameHeight() / video.height());
        };

        // Position: the rail, its options with the set's icons, the values.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool1"), "click"));
        QCOMPARE(tools.activeFamily(), 1);
        QQuickItem *byRect = visualItem("visualOption_byRectangle");
        QVERIFY(byRect);
        QCOMPARE(byRect->property("iconRole").toString(), QStringLiteral("frame-to-scale"));
        QCOMPARE(visualItem("visualOption_x")->property("iconRole").toString(), QStringLiteral("scale-x"));
        QCOMPARE(visualItem("visualOption_y")->property("iconRole").toString(), QStringLiteral("scale-y"));
        QVERIFY(!visualItem("visualOption_x")->isEnabled()); // greyed without the rectangle
        QVERIFY(visualItem("visualOption_alignment"));
        QCOMPARE(visualItem("visualValue_x")->property("text").toString(), QStringLiteral("100"));
        QCOMPARE(visualItem("visualValue_y")->property("text").toString(), QStringLiteral("80"));
        // The handle and libass's square at the same frame point.
        QTRY_VERIFY(squareAt({100, 80}));
        QVERIFY(handle());
        QVERIFY2(near(deviceToFrame(*handle()), frameOf({100, 80}), 0.5),
                 qPrintable(QStringLiteral("%1,%2").arg(deviceToFrame(*handle()).x()).arg(deviceToFrame(*handle()).y())));

        // A drag: the video shows the staged \pos, the release commits it as
        // one step named as legacy's, and libass draws it under the handle.
        const std::size_t steps = session->historySize();
        const QPoint start = videoPoint(*handle());
        const QPoint end = start + QPoint(12, 7);
        QTest::mouseMove(window, start);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, end);
        QTRY_VERIFY(tools.gestureActive());
        const QPointF moved = item("visualOverlay")->mapFromScene(QPointF(end));
        const PointF h0 = view.scriptToView({100, 80});
        const PointF h1{h0.x + float(view.toDevice(moved.x()) - view.toDevice(item("visualOverlay")->mapFromScene(QPointF(start)).x())),
                        h0.y + float(view.toDevice(moved.y()) - view.toDevice(item("visualOverlay")->mapFromScene(QPointF(start)).y()))};
        const PointF staged = view.viewToScript(h1);
        QTRY_VERIFY(squareAt(staged)); // the preview
        QCOMPARE(session->historySize(), steps);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual positioning tool"));
        const QString dragged = QStringLiteral("{\\an5\\pos(%1,%2)\\bord0\\shad0\\p1}m 0 0 l 40 0 40 40 0 40")
                                    .arg(ft(staged.x), ft(staged.y));
        QCOMPARE(signText(), dragged);
        QTRY_VERIFY(squareAt(staged));
        QTRY_VERIFY(handle() && near(deviceToFrame(*handle()), frameOf(staged), 0.5));

        // Esc during a drag: nothing written, the handle back where the text has it.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::mouseMove(window, end + QPoint(20, 20));
        QTRY_VERIFY(tools.gestureActive());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!tools.gestureActive());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end + QPoint(20, 20));
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(signText(), dragged);
        QTRY_VERIFY(squareAt(staged));
        QTRY_VERIFY(handle() && near(deviceToFrame(*handle()), frameOf(staged), 0.5));

        // A key nudge (D: a script pixel right) with the video focused, one step.
        visualItem("videoPanel")->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_D);
        QTRY_COMPARE(session->historySize(), steps + 2);
        {
            PointF p = view.scriptToView({QString(ft(staged.x)).toFloat(), QString(ft(staged.y)).toFloat()});
            p.x += (1.f / view.coeffW()) * view.zoomScale().x;
            const PointF s = view.viewToScript(p);
            QCOMPARE(signText(), QStringLiteral("{\\an5\\pos(%1,%2)\\bord0\\shad0\\p1}m 0 0 l 40 0 40 40 0 40")
                                     .arg(ft(s.x), ft(s.y)));
        }

        // A double click puts the Line at the pointer (legacy LeftDClick);
        // the release commits it.
        const QPoint dclick = videoPoint(tools.videoRect().center());
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, dclick);
        QTRY_VERIFY(!tools.gestureActive());
        {
            const QPointF local = item("visualOverlay")->mapFromScene(QPointF(dclick));
            const PointF s = view.viewToScript({float(view.toDevice(local.x())), float(view.toDevice(local.y()))});
            QCOMPARE(signText(), QStringLiteral("{\\an5\\pos(%1,%2)\\bord0\\shad0\\p1}m 0 0 l 40 0 40 40 0 40")
                                     .arg(ft(s.x), ft(s.y)));
            QTRY_VERIFY(squareAt(s));
        }

        // By rectangle (bottom-left): the square's left edge two script
        // pixels in from the rectangle's left, its bottom on the rectangle's
        // bottom (Position::SetPosition, VisualPosition.cpp:616-715).
        QVERIFY(QMetaObject::invokeMethod(byRect, "click"));
        QTRY_VERIFY(visualItem("visualOption_x")->isEnabled());
        {
            const QRectF video = tools.videoRect();
            const QPoint a = videoPoint(video.topLeft() + QPointF(video.width() * 0.2, video.height() * 0.15));
            const QPoint b = videoPoint(video.topLeft() + QPointF(video.width() * 0.6, video.height() * 0.45));
            const std::size_t before = session->historySize();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
            QTest::mouseMove(window, (a + b) / 2);
            QTest::mouseMove(window, b);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b);
            QTRY_COMPARE(session->historySize(), before + 1);
            auto script = [&](QPoint scene) {
                const QPointF l = item("visualOverlay")->mapFromScene(QPointF(scene));
                return view.viewToScript({float(view.toDevice(l.x())), float(view.toDevice(l.y()))});
            };
            const PointF ra = script(a), rb = script(b);
            const auto r = rendered();
            QVERIFY(r);
            QVERIFY2(std::abs(r->left() - frameOf({ra.x + 2, 0}).x()) <= 1.0,
                     qPrintable(QStringLiteral("left %1, rectangle %2").arg(r->left()).arg(ra.x)));
            QVERIFY2(std::abs(r->bottom() + 1 - frameOf({0, rb.y}).y()) <= 1.0,
                     qPrintable(QStringLiteral("bottom %1, rectangle %2").arg(r->bottom()).arg(rb.y)));
            // The alignment choice re-places it at once: Center.
            const std::size_t placed = session->historySize();
            QVERIFY(QMetaObject::invokeMethod(visualItem("visualOption_alignment"), "activated", Q_ARG(int, 4)));
            QTRY_COMPARE(session->historySize(), placed + 1);
            QTRY_VERIFY(squareAt({(ra.x + rb.x) / 2, (ra.y + rb.y) / 2}));
        }
        QVERIFY(QMetaObject::invokeMethod(byRect, "click")); // off again

        // Move: drag the start handle; \move from there to the old point,
        // with the Line's frame times, one step.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool2"), "click"));
        QCOMPARE(tools.activeFamily(), 2);
        QVERIFY(visualItem("visualOption_twoPoints"));
        QCOMPARE(visualItem("visualOption_twoPoints")->property("iconRole").toString(), QStringLiteral("two-points"));
        const QString before = signText();
        const PointF from = view.scriptToView({QString(visualItem("visualValue_x1")->property("text").toString()).toFloat(),
                                               QString(visualItem("visualValue_y1")->property("text").toString()).toFloat()});
        const QPoint f0 = videoPoint(QPointF(view.toLogical(from.x), view.toLogical(from.y)));
        const std::size_t moveSteps = session->historySize();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, f0);
        QTest::mouseMove(window, f0 + QPoint(-30, -20));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, f0 + QPoint(-30, -20));
        QTRY_COMPARE(session->historySize(), moveSteps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual movement tool"));
        QVERIFY2(signText().startsWith(QStringLiteral("{\\an5\\move(")), qPrintable(signText()));
        QVERIFY(signText() != before);
        bool arrow = false;
        for (const QVariant &v : tools.overlay())
            arrow |= polygonPoints(v.toMap()).size() == 3;
        QVERIFY(arrow); // DrawArrow's head

        // Two points: the first on this frame, the second three frames
        // later; the \move's end extrapolated over its time (Move::SetMove,
        // VisualMove.cpp:460-479) from the frame the video shows (Tell).
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualOption_twoPoints"), "click"));
        QTRY_VERIFY(visualItem("visualOption_twoPoints")->property("checked").toBool());
        const auto beforeTwo = moveArguments(signText());
        QVERIFY(beforeTwo);
        const QPoint p1 = videoPoint(tools.videoRect().center() + QPointF(-40, -30));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p1);
        const std::size_t twoSteps = session->historySize();
        QVERIFY(application->video().stepFrames(3));
        QTRY_COMPARE(application->video().frame(), 27);
        QTRY_COMPARE(application->video().session().shownFrame(), std::optional<int>(27));
        const QPoint p2 = p1 + QPoint(15, 6);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p2);
        QTRY_COMPARE(session->historySize(), twoSteps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual movement tool"));
        // The \move's own times (1 and 960 ms after the Line's start).
        checkTwoPointMove(signText(), {float((*beforeTwo)[0].toDouble()), float((*beforeTwo)[1].toDouble())}, p1, p2,
                          1000 + (*beforeTwo)[4].toInt(), 1000 + (*beforeTwo)[5].toInt(), 1000, int(tools.videoTimeMs()));
        if (QTest::currentTestFailed())
            return;
        // On the frame of the first point: legacy's message, nothing written.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool2"), "click")); // back to the crosshair
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool2"), "click")); // a new Move
        QVERIFY(visualItem("visualOption_twoPoints")->property("checked").toBool()); // the toolbar's state stays
        const std::size_t sameSteps = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p1);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p2);
        QTRY_COMPARE(application->log().lastMessage(),
                     QStringLiteral("Video must be set at least one frame after the line's start time"));
        QCOMPARE(session->historySize(), sameSteps);
    }

    // T2: VisualToolOptions.qml names each toggle's K1 role literally (so
    // icon_tests sees the roles placed); every family's toggles must show the
    // role its tool gives (ToolOption::iconRole), so a new or reordered
    // option without its role in the QML fails here.
    void visualToolOptionIconsFollowTheModel()
    {
        QVERIFY(application->openFile(visualDocument("t2icons.ass")));
        auto &tools = application->visualTools();
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        int toggles = 0;
        for (int family = 1; family < 11; ++family) {
            tools.selectFamily(family);
            QCOMPARE(tools.activeFamily(), family);
            for (const QVariant &v : tools.options()) {
                const QVariantMap o = v.toMap();
                if (o.value(QStringLiteral("kind")).toString() != QStringLiteral("toggle"))
                    continue;
                const QString name = o.value(QStringLiteral("name")).toString();
                QQuickItem *button = nullptr;
                QTRY_VERIFY2((button = visualItem(qPrintable(QStringLiteral("visualOption_") + name))), qPrintable(name));
                QCOMPARE(button->property("iconRole").toString(), o.value(QStringLiteral("iconRole")).toString());
                QVERIFY2(!o.value(QStringLiteral("iconRole")).toString().isEmpty(), qPrintable(name));
                ++toggles;
            }
        }
        QVERIFY(toggles >= 4); // Position's three and Move's two points
    }

    // T2: Position's helper cross (a middle click) dragged, then the Line out
    // of the shown frame's time: the render that blocks the tool ends the
    // drag (Position::Draw's nothintoshow, VisualPosition.cpp:107-113:
    // movingHelperLine = false), so when the Line shows again the pointer no
    // longer carries the cross.
    void visualPositionHelperDragEndsWhenBlocked()
    {
        // cfr.mkv's 48 frames: frame 24 shows the Line, frame 40 (1.67 s) not.
        const char *extra = "Dialogue: 0,0:00:01.00,0:00:01.50,Default,,0,0,0,,{\\an5\\pos(100,80)}sign\n";
        QVERIFY(application->openFile(visualDocument("t2helper.ass", extra)));
        auto &tools = application->visualTools();
        auto *session = application->files().session(*application->workspace().editingTarget());
        const core::LineId sign = session->document().lines()[2]->id;
        application->selectLine(sign.value);
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().frame() == 24, 20000);
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool1"), "click"));
        QCOMPARE(tools.activeFamily(), 1);
        // The options and values rows take their place first: the video
        // rectangle settles before points are taken in it.
        QTRY_VERIFY(visualItem("visualValue_x"));
        QRectF settled;
        int polls = 0;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const QRectF r = tools.videoRect();
            polls = r == settled ? polls + 1 : 0;
            settled = r;
            return polls >= 5;
        }(), 20000);
        const auto &view = tools.videoView();
        // The helper's square (DrawRect with size 4: eight device pixels).
        auto helper = [&]() -> std::optional<QPointF> {
            for (const QVariant &v : tools.overlay()) {
                const QVariantMap m = v.toMap();
                const QVariantList pts = polygonPoints(m);
                if (m.value(QStringLiteral("type")).toString() != QStringLiteral("polygon") || pts.size() != 4)
                    continue;
                const QPointF a = pts[0].toPointF(), c = pts[2].toPointF();
                if (std::abs(view.toDevice(c.x() - a.x()) - 8) < 0.5)
                    return (a + c) / 2;
            }
            return std::nullopt;
        };
        auto near = [](std::optional<QPointF> a, QPointF b) {
            return a && std::abs(a->x() - b.x()) <= 1.5 && std::abs(a->y() - b.y()) <= 1.5;
        };
        const QPointF centre = tools.videoRect().center();
        const QPoint at = videoPoint(centre + QPointF(-40, -30));
        QTest::mouseMove(window, at);
        QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, at);
        QTRY_VERIFY(near(helper(), item("visualOverlay")->mapFromScene(QPointF(at))));
        // The drag: the cross follows the pointer.
        const QPoint dragged = at + QPoint(20, 10);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        QTest::mouseMove(window, dragged);
        QTRY_VERIFY(near(helper(), item("visualOverlay")->mapFromScene(QPointF(dragged))));
        // Out of the Line's time: blocked, the release never reaches the tool.
        QVERIFY(application->video().showFrameAt(40));
        QTRY_COMPARE_WITH_TIMEOUT(application->video().frame(), 40, 20000);
        QTRY_VERIFY(!tools.warning().isEmpty());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragged);
        QVERIFY(application->video().showFrameAt(24));
        QTRY_COMPARE_WITH_TIMEOUT(application->video().frame(), 24, 20000);
        QTRY_VERIFY(tools.warning().isEmpty());
        QTRY_VERIFY(near(helper(), item("visualOverlay")->mapFromScene(QPointF(dragged))));
        // A plain move leaves the cross where the drag left it.
        QTest::mouseMove(window, dragged + QPoint(30, 25));
        QVERIFY(near(helper(), item("visualOverlay")->mapFromScene(QPointF(dragged))));
        QCOMPARE(text(session->document().lines()[2]), QStringLiteral("{\\an5\\pos(100,80)}sign"));
    }

    // T2: the two-point move on VFR media (frames of 30, 50 and 70 ms). The
    // \move's times are the Line's first and last frames' (GetMoveTimes,
    // Visuals.cpp:607-622, from the media's legacy Timebase), the end is
    // extrapolated from the frame the video shows (Move::SetMove,
    // VisualMove.cpp:460-479); on the frame of the first point legacy's
    // message and nothing written.
    void visualMoveTwoPointsOnVfrVideo()
    {
        const char *extra = "Dialogue: 0,0:00:00.60,0:00:01.20,Default,,0,0,0,,{\\an5\\pos(160,120)}sign\n";
        QVERIFY(application->openFile(visualDocument("t2vfr.ass", extra)));
        auto &tools = application->visualTools();
        auto *session = application->files().session(*application->workspace().editingTarget());
        const core::LineId sign = session->document().lines()[2]->id;
        auto signText = [&] {
            for (const auto *l : session->document().lines())
                if (l->id == sign)
                    return text(l);
            return QString();
        };
        application->selectLine(sign.value);
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("vfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(application->video().session().shownFrame(), std::optional<int>(12), 20000);
        QCOMPARE(tools.videoTimeMs(), 600);
        const auto timebase = application->video().session().legacyTimebase();
        const int moveStart = 600 + std::abs(timebase.msAt(timebase.frameAt(600)) - 600);
        const int moveEnd = 600 + (600 - std::abs(1200 - timebase.msAt(timebase.frameAt(1200) - 1)));
        // by hand: frame 12 starts at 600 ms, frame 23 (the last before 1200) at 1130
        QCOMPARE(moveStart, 600);
        QCOMPARE(moveEnd, 1130);

        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool2"), "click"));
        QCOMPARE(tools.activeFamily(), 2);
        QQuickItem *two = visualItem("visualOption_twoPoints");
        QVERIFY(two);
        if (!two->property("checked").toBool())
            QVERIFY(QMetaObject::invokeMethod(two, "click"));
        QTRY_VERIFY(visualItem("visualOption_twoPoints")->property("checked").toBool());
        const std::size_t steps = session->historySize();
        const QPoint p1 = videoPoint(tools.videoRect().center() + QPointF(-30, 10));
        const QPoint p2 = p1 + QPoint(40, 12);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p1);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p2);
        QTRY_COMPARE(application->log().lastMessage(),
                     QStringLiteral("Video must be set at least one frame after the line's start time"));
        QCOMPARE(session->historySize(), steps);
        QCOMPARE(signText(), QStringLiteral("{\\an5\\pos(160,120)}sign"));
        // Three frames on (750 ms): grabbing the second point writes the \move.
        QVERIFY(application->video().stepFrames(3));
        QTRY_COMPARE(application->video().session().shownFrame(), std::optional<int>(15));
        QCOMPARE(tools.videoTimeMs(), 750);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p2);
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual movement tool"));
        QVERIFY2(signText().startsWith(QStringLiteral("{\\an5\\move(")), qPrintable(signText()));
        checkTwoPointMove(signText(), {160, 120}, p1, p2, moveStart, moveEnd, 600, 750);
    }

    // T3: Scale and the rotations through the real panel. The family's
    // options row is legacy VideoToolbar's second row (ScaleItem,
    // RotationZItem, RotationXYItem) with the K1 icons, legacy's greying and
    // links; a drag of the width arrow writes \fscx as one step; Esc during
    // it leaves the text and puts the arrows back; the two-point angle's
    // first click waits for the second and Esc drops it (the card's
    // evidence, #178); an X/Y drag writes \fry by the distance.
    void visualScaleAndRotationsThroughThePanel()
    {
        using namespace application::visual;
        QVERIFY(application->openFile(visualDocument(
            "transform.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\pos(160,120)}third\n")));
        auto &tools = application->visualTools();
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().frame() == 24, 20000);
        auto *session = application->files().session(*application->workspace().editingTarget());
        const core::LineId third = session->document().lines()[2]->id;
        application->selectLine(third.value);
        QTRY_VERIFY(session->selection().active == third);
        const auto &view = tools.videoView();
        const auto thirdText = [&] { return text(session->document().lines()[2]); };

        // The crosshair has no options.
        QVERIFY(!visualItem("visualToolOptions")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool3"), "click"));
        QCOMPARE(tools.activeFamily(), 3);
        QTRY_VERIFY(visualItem("visualToolOptions")->isVisible());
        // ScaleItem (VideoToolbar.cpp:83-89, 900-958): width and the aspect
        // ratio on; the rectangle's four options greyed without it.
        const char *names[] = {"rectangle", "scaleX", "aspectRatio", "scaleY", "originalRectangle", "changeAll",
                               "preserveProportions"};
        const char *roles[] = {"frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation",
                               "resample"};
        const auto option = [&](const char *name) {
            return visualItem(qPrintable(QStringLiteral("visualOption_") + QLatin1String(name)));
        };
        const auto states = [&](std::array<bool, 7> checked, std::array<bool, 7> enabled) {
            for (int i = 0; i < 7; ++i) {
                QQuickItem *b = option(names[i]);
                QVERIFY2(b, names[i]);
                QCOMPARE(b->property("iconRole").toString(), QLatin1String(roles[i]));
                QVERIFY2(b->property("checked").toBool() == checked[static_cast<std::size_t>(i)], names[i]);
                QVERIFY2(b->isEnabled() == enabled[static_cast<std::size_t>(i)], names[i]);
                QVERIFY(!b->property("tip").toString().isEmpty());
            }
        };
        states({false, true, true, false, false, false, false}, {true, false, false, false, false, true, true});
        QCOMPARE(option("scaleX")->property("tip").toString(), QStringLiteral("Scale width"));
        // The rectangle switches "change all" on and keeps it on.
        QVERIFY(QMetaObject::invokeMethod(option("rectangle"), "click"));
        QTRY_VERIFY(option("rectangle")->property("checked").toBool());
        states({true, true, true, false, false, true, false}, {true, false, true, false, true, false, true});
        QVERIFY(QMetaObject::invokeMethod(option("rectangle"), "click"));
        QTRY_VERIFY(!option("rectangle")->property("checked").toBool());
        states({false, true, true, false, false, true, false}, {true, false, false, false, false, true, true});
        QVERIFY(QMetaObject::invokeMethod(option("changeAll"), "click"));
        QTRY_VERIFY(!option("changeAll")->property("checked").toBool());

        // The width arrow (from the Line's position to the right): a drag of
        // 20 pixels makes \fscx 120 at the device pixel ratio 1, one step.
        auto *scale = dynamic_cast<ScaleTool *>(tools.tool());
        QVERIFY(scale);
        QCOMPARE(scale->from(), view.scriptToView({160, 120}));
        const QPoint p = videoPoint(QPointF(view.toLogical(scale->to().x), view.toLogical(scale->from().y)));
        const std::size_t steps = session->historySize();
        // The Line editor's caret, which FindTag's mode 0 reads in the raw
        // text (TagFindReplace.cpp:40-41): inside the first block, the tags
        // shown.
        application->editor().setShowTags(true);
        auto *field = item("lineText");
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("{\\pos(160,120)}third"));
        QVERIFY(QMetaObject::invokeMethod(field, "select", Q_ARG(int, 10), Q_ARG(int, 10)));
        QCOMPARE(application->editor().rawFieldSelection(0), (std::pair<long, long>{10, 10}));
        QTest::mouseMove(window, p);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseMove(window, p + QPoint(10, 0));
        QTest::mouseMove(window, p + QPoint(20, 0));
        QTRY_VERIFY(tools.gestureActive());
        QCOMPARE(thirdText(), QStringLiteral("{\\pos(160,120)}third")); // nothing reaches the Document yet
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p + QPoint(20, 0));
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual scaling tool"));
        const QString fscx = QStringLiteral("\\fscx") +
                             QString::number(100 + view.toDevice(20));
        QVERIFY2(thirdText().contains(fscx), qPrintable(thirdText()));
        // The caret goes to the tag in the written text (Visuals::SetVisual,
        // Visuals.cpp:813-815, kept through the Send at 818-825): the editor
        // takes it after the commit's reload.
        const int tagAt = static_cast<int>(thirdText().indexOf(QStringLiteral("\\fscx")));
        QVERIFY(tagAt != 10);
        QTRY_COMPARE(field->property("text").toString(), thirdText());
        QTRY_COMPARE(field->property("cursorPosition").toInt(), tagAt);
        QCOMPARE(application->editor().rawFieldSelection(0), (std::pair<long, long>{tagAt, tagAt}));
        QTRY_COMPARE(visualItem("visualValue_fscx")->property("text").toString(), fscx.mid(5));
        // Esc during a drag: no step, and the arrows go back to the text's.
        const PointF arrowsBefore = scale->to();
        const QPoint q = videoPoint(QPointF(view.toLogical(scale->to().x), view.toLogical(scale->from().y)));
        QTest::mouseMove(window, q);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, q);
        QTest::mouseMove(window, q + QPoint(15, 0));
        QTRY_VERIFY(tools.gestureActive());
        QVERIFY(tools.escapable());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!tools.gestureActive());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, q + QPoint(15, 0));
        QCOMPARE(session->historySize(), steps + 1);
        QVERIFY2(thirdText().contains(fscx), qPrintable(thirdText()));
        QCOMPARE(scale->to(), arrowsBefore);
        application->editor().setShowTags(false);

        // The rectangle mode against the measured text: GetTextSize through
        // GetLineTextExtents (Visuals.cpp:953-1133), here the app's
        // QtTextMeasurePort on the Line's Style with the text's \fscx put in
        // (TagValueToStyle), "third", the descent and leading passed swapped
        // as legacy did.
        QVERIFY(QMetaObject::invokeMethod(option("rectangle"), "click"));
        QTRY_VERIFY(option("rectangle")->property("checked").toBool());
        const auto ctx = transform::context(tools);
        auto measuring = transform::lineStyle(ctx, u8"Default");
        const QByteArray writtenX = QByteArray::number(100 + view.toDevice(20));
        measuring.scaleX = std::u8string(reinterpret_cast<const char8_t *>(writtenX.constData()));
        std::vector<std::string> fields;
        for (const auto &field8 : core::legacy::styleRawFields(measuring))
            fields.emplace_back(field8.begin(), field8.end());
        const auto measured = tools.textMeasure()->measure(fields, "third");
        QVERIFY(measured && measured->width > 0 && measured->height > 0);
        QCOMPARE(scale->originalSize().x, static_cast<float>(measured->width));
        QCOMPARE(scale->originalSize().y,
                 static_cast<float>(measured->height) -
                     (static_cast<float>(measured->descent) - static_cast<float>(measured->externalLeading)));
        // A drawn rectangle sets the scale (Scale::SetScale, VisualScale.cpp:
        // 678-700): its width less the border over the measured width, times
        // the \fscx the text has ("change all", which the rectangle switches
        // on, reads it from the start); the aspect ratio links \fscy. One step.
        const std::size_t rectangleSteps = session->historySize();
        const QPoint r0 = videoPoint(tools.videoRect().center() + QPointF(-40, -15)), r1 = r0 + QPoint(70, 30);
        QTest::mouseMove(window, r0);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, r0);
        QTest::mouseMove(window, r0 + QPoint(35, 15));
        QTest::mouseMove(window, r1);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, r1);
        QTRY_COMPARE(session->historySize(), rectangleSteps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual scaling tool"));
        QVERIFY(scale->rectangleVisible());
        const auto &sizing = scale->sizingRectangle();
        const float written = static_cast<float>(100 + view.toDevice(20)) / 100.f;
        const float expected =
            written * ((std::fabs(sizing[1].x - sizing[0].x) - scale->border().x) / scale->originalSize().x);
        QCOMPARE(scale->scale().x, expected);
        QCOMPARE(scale->scale().y, expected);
        const QString rectangleScale = QString::fromStdU16String(transform::getfloat(expected * 100));
        QVERIFY2(thirdText().contains(QStringLiteral("\\fscx") + rectangleScale) &&
                     thirdText().contains(QStringLiteral("\\fscy") + rectangleScale),
                 qPrintable(thirdText() + QStringLiteral(" / ") + rectangleScale));
        QVERIFY(QMetaObject::invokeMethod(option("rectangle"), "click"));
        QTRY_VERIFY(!option("rectangle")->property("checked").toBool());

        // Z rotation: RotationZItem (VideoToolbar.cpp:78-80, 827-865).
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool4"), "click"));
        QCOMPARE(tools.activeFamily(), 4);
        QTRY_VERIFY(option("twoPoints"));
        QCOMPARE(option("twoPoints")->property("iconRole").toString(), QStringLiteral("two-points"));
        QCOMPARE(option("changeAll")->property("iconRole").toString(), QStringLiteral("tool-scale-rotation"));
        QCOMPARE(option("preserveProportions")->property("iconRole").toString(), QStringLiteral("resample"));
        QVERIFY(!option("scaleX"));
        // "Preserve proportions" switches "change all" on and greys it.
        QVERIFY(QMetaObject::invokeMethod(option("preserveProportions"), "click"));
        QTRY_VERIFY(option("changeAll")->property("checked").toBool());
        QVERIFY(!option("changeAll")->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(option("preserveProportions"), "click"));
        QTRY_VERIFY(option("changeAll")->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(option("changeAll"), "click"));
        QTRY_VERIFY(!option("changeAll")->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(option("twoPoints"), "click"));
        QTRY_VERIFY(option("twoPoints")->property("checked").toBool());
        auto *rz = dynamic_cast<RotationZTool *>(tools.tool());
        QVERIFY(rz && rz->hasTwoPoints());
        const QPointF centre = tools.videoRect().center();
        const QPoint a = videoPoint(centre + QPointF(-40, 10)), b = videoPoint(centre + QPointF(40, -10));
        const std::size_t before = session->historySize();
        // The first click places a point and waits; Esc drops it.
        QTest::mouseMove(window, a);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, a);
        QTRY_VERIFY(rz->visibility()[0]);
        QVERIFY(!rz->visibility()[1]);
        QVERIFY(tools.escapable());
        QVERIFY(!tools.gestureActive());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!rz->visibility()[0]);
        QVERIFY(!tools.escapable());
        QCOMPARE(session->historySize(), before);
        // Two clicks: the angle of the line through them, one step.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, a);
        QTRY_VERIFY(rz->visibility()[0]);
        QTest::mouseMove(window, b);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, b);
        QTRY_COMPARE(session->historySize(), before + 1);
        QCOMPARE(session->history().back().name, std::string("Visual Z-axis rotation tool"));
        const PointF p0 = rz->twoPoints()[0], p1 = rz->twoPoints()[1];
        float angle = std::atan2((p0.y - p1.y), (p0.x - p1.x)) * (180.f / 3.1415926536f); // VisualRotationZ.cpp:388-394
        angle = std::fmod(-angle + 180 + 360.f, 360.f);
        const QString frz = QStringLiteral("\\frz") + QString::fromStdU16String(transform::getfloat(angle));
        QVERIFY2(thirdText().contains(frz), qPrintable(thirdText() + QStringLiteral(" / ") + frz));
        QVERIFY(!tools.escapable()); // both points placed: nothing pending

        // X/Y rotation: RotationXYItem's one option; the grid is drawn.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool5"), "click"));
        QCOMPARE(tools.activeFamily(), 5);
        QTRY_VERIFY(option("changeAll"));
        QCOMPARE(option("changeAll")->property("tip").toString(), QStringLiteral("Changing all X/Y-rotation tags"));
        QVERIFY(!option("twoPoints"));
        // The grid as the panel draws it (the window without the panel,
        // VisualRotationXY.cpp:49-50; its shapes are checked against D3DX in
        // VisualOverlay.RotationXYGridProjectsAsD3DX): the three arrow cones'
        // twelve triangles, 44 grid lines, the three axes and the cross, the
        // axes meeting at the \org.
        auto *xyTool = dynamic_cast<RotationXYTool *>(tools.tool());
        QVERIFY(xyTool);
        QTRY_COMPARE(tools.overlay().size(), qsizetype(12 + 44 + 3 + 2));
        const QVariantMap yAxis = tools.overlay()[12 + 44].toMap();
        QCOMPARE(yAxis.value(QStringLiteral("type")).toString(), QStringLiteral("line"));
        QVERIFY(std::fabs(yAxis.value(QStringLiteral("x2")).toDouble() - view.toLogical(xyTool->org().x)) < 1e-3);
        QVERIFY(std::fabs(yAxis.value(QStringLiteral("y2")).toDouble() - view.toLogical(xyTool->org().y)) < 1e-3);
        const std::size_t xy = session->historySize();
        const QPoint c = videoPoint(centre + QPointF(30, 30));
        QTest::mouseMove(window, c);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(window, c + QPoint(25, 0));
        QTRY_VERIFY(tools.gestureActive());
        const PointF pressedAt = xyTool->firstmove(); // the press (VisualRotationXY.cpp:214)
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, c + QPoint(25, 0));
        QTRY_COMPARE(session->historySize(), xy + 1);
        QCOMPARE(session->history().back().name, std::string("Visual X/Y-axis rotation tool"));
        QVERIFY2(thirdText().contains(QStringLiteral("\\fry%1").arg(view.toDevice(25))), qPrintable(thirdText()));
        // Its own step does not reset the tool (legacy sent it with the
        // visual dummy flag, so no SetVisual followed): the press point stays
        // (a reset would make it `to`, VisualRotationXY.cpp:259).
        QCOMPARE(xyTool->firstmove(), pressedAt);
        QVERIFY(!(xyTool->firstmove() == xyTool->to()));
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool5"), "click"));
        QCOMPARE(tools.activeFamily(), 0);
        QTRY_VERIFY(!visualItem("visualToolOptions")->isVisible());
    }

    // T1: legacy disables the rail for a Document that is not ASS
    // (VideoToolbar::DisableVisuals); the crosshair stays the tool. (An ASS
    // Document's rail is enabled: visualToolRailCrosshairAndCopyCoordinates.)
    void visualRailIsDisabledForOtherFormats()
    {
        const QString path = dir.filePath(QStringLiteral("visual.srt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("1\n00:00:01,000 --> 00:00:02,000\nfirst\n\n");
        }
        QVERIFY(application->openFile(path));
        auto &tools = application->visualTools();
        QVERIFY(!tools.railEnabled());
        QVERIFY(!visualItem("visualTool1")->isEnabled());
        tools.selectFamily(3);
        QCOMPARE(tools.activeFamily(), 0);
    }

    // T1: converting the Document moves the crosshair with it: to ASS
    // makes it (HikariSubFrame::OnConversion, DisableVisuals(false),
    // HikariSubFrame.cpp:1169), from ASS deletes it (RemoveVisual(true,
    // true), HikariSubFrame.cpp:1166-1168; RendererVideo.cpp:1133-1146), and
    // an edit never brings it back (SubsGrid::ShowEditOnVideo runs no
    // SetVisual below CHANGEPOS, SubsGridBase.cpp:1168).
    void visualCrosshairFollowsTheDocumentFormat()
    {
        const QString path = dir.filePath(QStringLiteral("cross.srt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("1\n00:00:01,000 --> 00:00:02,000\nfirst\n\n2\n00:00:03,000 --> 00:00:04,000\nsecond\n\n");
        }
        QVERIFY(application->openFile(path));
        auto &tools = application->visualTools();
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        auto *session = application->files().session(*application->workspace().editingTarget());
        const QPoint p = videoPoint(tools.videoRect().center());
        // SRT: no crosshair, not even after an edit, and a Ctrl+click
        // writes nothing.
        QVERIFY(!tools.tool());
        QVERIFY(session->editDraftText(session->document().lines()[0]->id, u8"edited"));
        QVERIFY(application->editor().commit());
        QVERIFY(!tools.tool());
        QTest::mouseMove(window, p);
        QVERIFY(tools.overlay().isEmpty());
        QVERIFY(!tools.hideCursor());
        std::size_t steps = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, p);
        QCOMPARE(session->historySize(), steps);

        // Converted to ASS (at the video's resolution, so no resolution
        // question): the crosshair, hidden until the next move.
        application->setConversionOptions({{QStringLiteral("resolutionWidth"), 320},
                                           {QStringLiteral("resolutionHeight"), 240}});
        QVERIFY(application->previewConversion(QStringLiteral("ass")).value(QStringLiteral("ok")).toBool());
        QVERIFY(application->acceptConversion());
        QCOMPARE(session->document().format(), core::SubtitleFormat::Ass);
        QVERIFY(tools.railEnabled());
        QVERIFY(tools.tool());
        QVERIFY(tools.overlay().isEmpty());
        QTest::mouseMove(window, p + QPoint(2, 1));
        QTRY_COMPARE(tools.overlay().size(), 5);

        // Back to SRT: gone, and an edit does not bring it back.
        QVERIFY(application->previewConversion(QStringLiteral("srt")).value(QStringLiteral("ok")).toBool());
        QVERIFY(application->acceptConversion());
        QCOMPARE(session->document().format(), core::SubtitleFormat::Srt);
        QVERIFY(!tools.railEnabled());
        QVERIFY(!tools.tool());
        QVERIFY(tools.overlay().isEmpty());
        QVERIFY(session->editDraftText(session->document().lines()[1]->id, u8"typed"));
        QVERIFY(application->editor().commit());
        QVERIFY(!tools.tool());
        QTest::mouseMove(window, p + QPoint(4, 3));
        QVERIFY(tools.overlay().isEmpty());
        QVERIFY(!tools.hideCursor());
        steps = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, p + QPoint(4, 3));
        QCOMPARE(session->historySize(), steps);
    }

    // T1: the visual tools edit the editing target only, so the Protected
    // reference refuses their writes by never being given to them. With the
    // reference the only Document there is no target: the rail is disabled
    // and a Ctrl+click or middle click (legacy Cross's \pos, VisualCross.cpp:
    // 102-126) writes nothing. With a target too, the crosshair is live and
    // its clicks go to the target, never the reference. (The shell's
    // reference EditSession is not built protected, DocumentFiles; the
    // Gesture's own refusal of a protected session is unit-tested only,
    // VisualGesture.ProtectedReferenceRefusesWrites.)
    void visualToolsNeverWriteTheReference()
    {
        QVERIFY(application->openReference(original));
        QVERIFY(!application->workspace().editingTarget());
        auto *reference = application->files().session(*application->workspace().reference());
        QVERIFY(reference);
        const auto revision = reference->revision();
        const std::size_t steps = reference->historySize();
        auto &tools = application->visualTools();
        QVERIFY(!tools.session());
        QVERIFY(!tools.railEnabled());
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        const QPoint p = videoPoint(tools.videoRect().center());
        QTest::mouseMove(window, p);
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, p);
        QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, p);
        QVERIFY(!tools.gestureActive());
        QCOMPARE(reference->revision(), revision);
        QCOMPARE(reference->historySize(), steps);
        QCOMPARE(text(reference->document().lines()[0]), QStringLiteral("ref"));

        // With an editing target as well, the tools are live and still edit
        // the target only: the shell gives them the editing target, which
        // the Workspace never lets the reference be.
        QVERIFY(application->openFile(visualDocument("visual.ass")));
        QVERIFY(application->workspace().editingTarget());
        QVERIFY(application->workspace().reference());
        auto *target = application->files().session(*application->workspace().editingTarget());
        QCOMPARE(tools.session(), target);
        QVERIFY(tools.railEnabled());
        application->video().openVideo(nativeFixture("cfr.mkv")); // the new target's video closed
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        const QPoint q = videoPoint(tools.videoRect().center()) + QPoint(5, 3); // a move: not where the pointer is
        QTest::mouseMove(window, q);
        QTRY_COMPARE(tools.overlay().size(), 5); // the crosshair
        const std::size_t targetSteps = target->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, q);
        QTRY_COMPARE(target->historySize(), targetSteps + 1);
        QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, q);
        QTRY_COMPARE(target->historySize(), targetSteps + 2);
        QVERIFY(text(target->document().lines()[0]).startsWith(QStringLiteral("{\\pos(")));
        QCOMPARE(reference->revision(), revision);
        QCOMPARE(reference->historySize(), steps);
        QCOMPARE(text(reference->document().lines()[0]), QStringLiteral("ref"));
    }

    // T4: the clips through the Video panel. The rectangle clip is drawn by
    // a drag and committed on release as one step, its mask and outline drawn
    // over the video; the tool's own row (legacy ClipRectangleItem, VectorItem)
    // shows Invert clip and the vector clip's point modes with the K1 icons and
    // legacy's help texts. A click adds a vector point, the mask Line goes into
    // the video's subtitles, a D key nudge commits on its release, and the
    // "Double m" refusal is legacy's modal "Warning" box.
    void visualClipsDrawEditAndInvert()
    {
        QVERIFY(application->openFile(visualDocument("clip.ass")));
        auto &tools = application->visualTools();
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        // The active Line's start shown, not only requested: the tools take
        // no events outside the shown frame's Line (Visuals::Draw's blockevents).
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame() == std::optional<int>(24), 20000);
        auto *session = application->files().session(*application->workspace().editingTarget());
        const core::LineId first = session->document().lines()[0]->id;
        const core::LineId second = session->document().lines()[1]->id;
        // A tool's own row changes the panel's height, and the overlay's new
        // viewport resets the tool (legacy UpdateVideoWindow's SetCurVisual):
        // the layout settles before a gesture, as it does before a user's.
        const auto settle = [&] {
            QRectF last;
            return QTest::qWaitFor([&] {
                QTest::qWait(50);
                const QRectF r = tools.videoRect();
                const bool same = r == last && !r.isEmpty();
                last = r;
                return same;
            }, 5000);
        };

        // The rectangle clip.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool6"), "click"));
        QCOMPARE(tools.activeFamily(), 6);
        QTRY_VERIFY(visualItem("visualOption_invert"));
        QVERIFY(settle());
        const QRectF v = tools.videoRect();
        const QPoint a = videoPoint(v.topLeft() + QPointF(v.width() / 4, v.height() / 4));
        const QPoint b = videoPoint(v.topLeft() + QPointF(v.width() * 3 / 4, v.height() * 3 / 4));
        QCOMPARE(visualItem("visualOption_invert")->property("iconRole").toString(), QStringLiteral("clip-invert"));
        QCOMPARE(visualItem("visualOption_invert")->property("tip").toString(), QStringLiteral("Invert clip"));
        const std::size_t steps = session->historySize();
        QTest::mouseMove(window, a);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(window, a + (b - a) * i / 4);
        QVERIFY(tools.gestureActive());
        QCOMPARE(text(session->document().lines()[0]), QStringLiteral("first"));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, b);
        QTRY_COMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Visual rectangular clipping tool"));
        QVERIFY2(text(session->document().lines()[0]).startsWith(QStringLiteral("{\\clip(")),
                 qPrintable(text(session->document().lines()[0])));
        int polygons = 0, lines = 0;
        for (const QVariant &shape : tools.overlay()) {
            const QString type = shape.toMap().value(QStringLiteral("type")).toString();
            polygons += type == QLatin1String("polygon");
            lines += type == QLatin1String("line");
        }
        QCOMPARE(polygons, 1); // the darkened outside
        QCOMPARE(lines, 4);    // the outline
        // Invert clip: \iclip, recorded as legacy did (VISUAL_VECTOR_CLIP).
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualOption_invert"), "click"));
        QTRY_VERIFY(text(session->document().lines()[0]).startsWith(QStringLiteral("{\\iclip(")));
        QCOMPARE(session->history().back().name, std::string("Visual vector clipping tool"));
        // A D key nudge: one step on its release.
        const QString beforeNudge = text(session->document().lines()[0]);
        const std::size_t nudge = session->historySize();
        QTest::keyPress(window, Qt::Key_D);
        QVERIFY(tools.gestureActive());
        QCOMPARE(session->historySize(), nudge);
        QTest::keyRelease(window, Qt::Key_D);
        QTRY_COMPARE(session->historySize(), nudge + 1);
        QVERIFY(text(session->document().lines()[0]) != beforeNudge);

        // The vector clip: its modes, Add line on.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool7"), "click"));
        QCOMPARE(tools.activeFamily(), 7);
        QTRY_VERIFY(visualItem("visualOption_mode5"));
        QVERIFY(settle());
        const char *roles[] = {"vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete"};
        for (int i = 0; i < 6; ++i) {
            QQuickItem *mode = visualItem(qPrintable(QStringLiteral("visualOption_mode%1").arg(i)));
            QVERIFY(mode);
            QCOMPARE(mode->property("iconRole").toString(), QLatin1String(roles[i]));
            QCOMPARE(mode->property("checked").toBool(), i == 1);
        }
        QCOMPARE(visualItem("visualOption_mode2")->property("tip").toString(), QStringLiteral("Add B\u00e9zier curve"));
        // On the second Line (no clip): a click adds the first point.
        application->selectLine(second.value);
        QTRY_COMPARE(*session->selection().active, second);
        const std::size_t vector = session->historySize();
        QTest::mouseMove(window, a);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, a);
        QTRY_COMPARE(session->historySize(), vector + 1);
        QCOMPARE(session->history().back().name, std::string("Visual vector clipping tool"));
        QVERIFY2(text(session->document().lines()[1]).startsWith(QStringLiteral("{\\clip(m ")),
                 qPrintable(text(session->document().lines()[1])));
        // The mask: the active Line's copy on the top layer, in the subtitles.
        const auto bytes = tools.subtitles(session->document());
        const QByteArray script(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
        QVERIFY2(script.contains("Dialogue: 2147483647,") && script.contains("\\1a&H77&\\pos(0,0)\\an7\\iclip(m "),
                 script.constData());
        // Separate point: another "m" right after the "m" is refused with
        // legacy's modal HikariMessageBox titled "Warning"
        // (VisualClips.cpp:1013-1016), a box in the window closed by OK.
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualOption_mode4"), "click"));
        QTRY_VERIFY(visualItem("visualOption_mode4")->property("checked").toBool());
        QVERIFY(!visualItem("visualOption_mode1")->property("checked").toBool());
        const std::size_t refused = session->historySize();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, b);
        QTRY_COMPARE(tools.notice(), QStringLiteral("Double \"m\" was blocked because of Vsfilter bug"));
        QObject *box = named("visualNotice");
        QVERIFY(box);
        QTRY_VERIFY(box->property("visible").toBool());
        QCOMPARE(box->property("title").toString(), QStringLiteral("Warning"));
        QCOMPARE(box->property("modal").toBool(), true);
        QCOMPARE(named("visualNoticeText")->property("text").toString(),
                 QStringLiteral("Double \"m\" was blocked because of Vsfilter bug"));
        QCOMPARE(session->historySize(), refused);
        QVERIFY(!QGuiApplication::modalWindow()); // in the window: nothing an offscreen run cannot close
        QVERIFY(QMetaObject::invokeMethod(box, "accept"));
        QTRY_VERIFY(!box->property("visible").toBool());
        QCOMPARE(tools.notice(), QString());
        QCOMPARE(text(session->document().lines()[0]).left(7), QStringLiteral("{\\iclip"));
        Q_UNUSED(first);
    }

    // T4: what the video shows, through the application's wiring (the
    // controller's preview to the video session, legacy's RenderSubs with
    // AppendClipMask): the vector clip's mask Line darkens the outside, and
    // during a drag the Line's staged text is rendered while the Document
    // keeps the old one. The Line is a white drawing over the whole frame
    // inside its clip, so the overlay's alpha tells the two apart: opaque
    // inside the clip, the mask's 0x88 (\1a&H77&) outside it. Another active
    // Line mid-drag cancels the gesture (legacy's SetVisual dropped the
    // unsent preview), and nothing is committed to either Line.
    void visualClipPreviewReachesTheVideo()
    {
        QVERIFY(application->openFile(visualDocument(
            "clipvideo.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an7\\pos(0,0)\\bord0\\shad0"
                             "\\clip(m 0 0 l 160 0 160 120 0 120)\\p1}m 0 0 l 320 0 320 240 0 240\n")));
        auto &tools = application->visualTools();
        // V6: the video opens at the active Line (OPEN_VIDEO_AT_ACTIVE_LINE; legacy's
        // default opens it at its first frame, VideoBox.cpp:407-410).
        application->settingsStore()->set("video.openAtActiveLine", true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(!tools.videoRect().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame() == std::optional<int>(24), 20000);
        auto *session = application->files().session(*application->workspace().editingTarget());
        const core::LineId first = session->document().lines()[0]->id;
        const core::LineId drawn = session->document().lines()[2]->id;
        const QString original = text(session->document().lines()[2]);
        application->selectLine(drawn.value);
        QTRY_COMPARE(*session->selection().active, drawn);
        // The overlay's alpha at a script point (the script is the frame's size).
        const auto alpha = [&](int x, int y) -> int {
            const auto overlay = application->video().session().lastOverlay();
            if (!overlay || overlay->empty || x >= overlay->width || y >= overlay->height)
                return 0;
            return overlay->pixels[static_cast<std::size_t>(y) * overlay->stride + static_cast<std::size_t>(x) * 4 + 3];
        };
        // The crosshair: the Line clipped, no mask.
        QTRY_COMPARE(alpha(80, 60), 255);
        QCOMPARE(alpha(230, 170), 0);

        const auto settle = [&] {
            QRectF last;
            return QTest::qWaitFor([&] {
                QTest::qWait(50);
                const QRectF r = tools.videoRect();
                const bool same = r == last && !r.isEmpty();
                last = r;
                return same;
            }, 5000);
        };
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualTool7"), "click"));
        QTRY_VERIFY(visualItem("visualOption_mode0"));
        QVERIFY(settle());
        // The mask Line reaches the video: the outside darkened.
        QTRY_COMPARE(alpha(230, 170), 0x88);
        QCOMPARE(alpha(80, 60), 255);

        // Move points: drag the corner at (160, 120) to (300, 220).
        QVERIFY(QMetaObject::invokeMethod(visualItem("visualOption_mode0"), "click"));
        QTRY_VERIFY(visualItem("visualOption_mode0")->property("checked").toBool());
        const QRectF v = tools.videoRect();
        const auto at = [&](qreal x, qreal y) {
            return videoPoint(v.topLeft() + QPointF(x * v.width() / 320, y * v.height() / 240));
        };
        // The script point a pointer's device pixel writes: a point grabbed
        // within a pixel of it follows the pointer through T1's inverse
        // (DrawingAndClip::OnMouseEvent's GetCalculatedOutPos, "%6.0f"). A
        // video rectangle smaller than the script leaves script integers
        // without a pixel of their own (312x234 with Fedora's fonts puts 300
        // and 220 between two), so the drop writes what its pixel maps to.
        const auto &view = tools.videoView();
        const auto device = [&](QPoint scene) {
            const QPointF l = item("visualOverlay")->mapFromScene(QPointF(scene));
            return application::visual::PointF{float(view.toDevice(l.x())), float(view.toDevice(l.y()))};
        };
        const auto written = [&](QPoint scene) {
            const auto s = view.viewToScript(device(scene));
            return QString::asprintf("%.0f %.0f", double(s.x), double(s.y));
        };
        const auto corner = view.scriptToView({160, 120});
        QVERIFY(std::abs(corner.x - device(at(160, 120)).x) < 1 && std::abs(corner.y - device(at(160, 120)).y) < 1);
        const std::size_t steps = session->historySize();
        QTest::mouseMove(window, at(160, 120));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(160, 120));
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(window, at(160 + 35 * i, 120 + 25 * i));
        QVERIFY(tools.gestureActive());
        // The staged clip is on the video: (230, 170) is inside it now.
        QTRY_COMPARE(alpha(230, 170), 255);
        QCOMPARE(text(session->document().lines()[2]), original);
        QCOMPARE(session->historySize(), steps);

        // Another active Line mid-drag: the gesture is dropped, the release
        // commits nothing, and the video shows the Line as it is.
        application->selectLine(first.value);
        QTRY_COMPARE(*session->selection().active, first);
        QVERIFY(!tools.gestureActive());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(300, 220));
        QVERIFY(!tools.gestureActive());
        QCOMPARE(session->historySize(), steps);
        QCOMPARE(text(session->document().lines()[2]), original);
        QCOMPARE(text(session->document().lines()[0]), QStringLiteral("first"));
        QTRY_COMPARE(alpha(230, 170), 0);
        QCOMPARE(alpha(80, 60), 255);

        // Back on the drawn Line, the same drag commits the staged clip.
        application->selectLine(drawn.value);
        QTRY_COMPARE(*session->selection().active, drawn);
        QTRY_COMPARE(alpha(230, 170), 0x88);
        QTest::mouseMove(window, at(160, 120));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(160, 120));
        for (int i = 1; i <= 4; ++i)
            QTest::mouseMove(window, at(160 + 35 * i, 120 + 25 * i));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(300, 220));
        QTRY_COMPARE(session->historySize(), steps + 1);
        const QString clip = QStringLiteral("\\clip(m 0 0 l 160 0 %1 0 120)").arg(written(at(300, 220)));
        QVERIFY2(text(session->document().lines()[2]).contains(clip),
                 qPrintable(text(session->document().lines()[2]) + QStringLiteral(" lacks ") + clip));
        QTRY_COMPARE(alpha(230, 170), 255);
    }

    // K1: the video transport buttons show the set's icons (legacy VideoBox's
    // bitmap buttons, VideoBox.cpp:156-165), keep their names and tooltips,
    // and every visible icon takes the theme layer's colours live (K2): the
    // Light, Dark and high-contrast themes, an accent preset and a
    // high-contrast pick, with the controls' palette following.
    void videoTransportIconsFollowTheThemeLive()
    {
        restartWithoutSound(); // play / pause below plays the fixture
        auto &settings = *application->settingsStore();
        const auto setTheme = [&](const char *code) {
            settings.setValue(QStringLiteral("appearance.followSystem"), false);
            settings.setValue(QStringLiteral("appearance.theme"), QString::fromLatin1(code));
        };
        struct Button {
            const char *name, *role, *accessibleName;
        };
        const Button buttons[] = {{"playPause", "media-play", "Play"},
                                  {"playActualLine", "play-line", "Play the current line"},
                                  {"stopVideo", "media-stop", "Stop"},
                                  {"previousFrame", "frame-previous", "Previous frame"},
                                  {"nextFrame", "frame-next", "Next frame"}};
        const auto icon = [&](const char *name) {
            auto *button = visualItem(name);
            return button ? button->property("contentItem").value<QQuickItem *>() : nullptr;
        };
        const auto colours = [&] {
            QStringList out;
            for (const auto &b : buttons)
                out << icon(b.name)->property("color").value<QColor>().name();
            return out;
        };
        const auto all = [](const QString &colour) { return QStringList(5, colour); };
        // Whether the window shows `colour` inside each button.
        const auto shown = [&](const QColor &colour) {
            const QImage image = window->grabWindow();
            const qreal dpr = window->effectiveDevicePixelRatio();
            for (const auto &b : buttons) {
                auto *item = icon(b.name);
                const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                bool found = false;
                for (int y = int(r.top() * dpr); y < int(r.bottom() * dpr) && !found; ++y)
                    for (int x = int(r.left() * dpr); x < int(r.right() * dpr) && !found; ++x)
                        found = QColor(image.pixel(x, y)) == colour;
                if (!found)
                    return false;
            }
            return true;
        };
        // With HIKARI_ICON_SHEET_DIR set, the transport row is saved for review.
        const auto saveTransport = [&](const QString &name) {
            const QString out = qEnvironmentVariable("HIKARI_ICON_SHEET_DIR");
            if (out.isEmpty())
                return;
            QRectF row;
            for (const auto &b : buttons) {
                auto *item = visualItem(b.name);
                row |= item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            }
            const qreal dpr = window->effectiveDevicePixelRatio();
            const QRect r = QRectF(row.topLeft() * dpr, row.size() * dpr).toAlignedRect().adjusted(-8, -8, 8, 8);
            QDir().mkpath(out);
            QVERIFY(window->grabWindow().copy(r).save(out + QLatin1Char('/') + name));
        };
        for (const auto &b : buttons) {
            auto *button = visualItem(b.name);
            QVERIFY2(button, b.name);
            QCOMPARE(button->property("iconRole").toString(), QLatin1String(b.role));
            QVERIFY(icon(b.name)->property("valid").toBool());
            QCOMPARE(QAccessible::queryAccessibleInterface(button)->text(QAccessible::Name), QLatin1String(b.accessibleName));
            QVERIFY(!button->property("tip").toString().isEmpty());
        }
        QCOMPARE(visualItem("stopVideo")->property("tip").toString(), QStringLiteral("Stop\nShortcut can be set using Shift + Click"));
        // The Light theme. No video: the buttons are disabled, in the
        // theme's disabled colour.
        setTheme("light");
        QCOMPARE(colours(), all(QStringLiteral("#74808b")));
        QVERIFY(shown(QColor(0x74, 0x80, 0x8B)));
        saveTransport(QStringLiteral("transport-light-disabled.png"));
        // With a video they are enabled: the icon colour with its accent.
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        application->video().stepFrames(1); // Previous frame enabled too
        QTRY_COMPARE(colours(), all(QStringLiteral("#202832")));
        QCOMPARE(icon("previousFrame")->property("accentColor").value<QColor>().name(),
                 QStringLiteral("#145c4c"));
        QTRY_VERIFY(shown(QColor(0x20, 0x28, 0x32)));
        saveTransport(QStringLiteral("transport-light.png"));
        // Play / pause swaps its icon as legacy ChangeButtonBMP swaps the
        // bitmap (VideoBox.cpp:262, 1414-1418).
        application->video().togglePlay();
        QTRY_COMPARE(visualItem("playPause")->property("iconRole").toString(), QStringLiteral("media-pause"));
        QCOMPARE(QAccessible::queryAccessibleInterface(visualItem("playPause"))->text(QAccessible::Name), QStringLiteral("Pause"));
        application->video().togglePlay();
        QTRY_COMPARE(visualItem("playPause")->property("iconRole").toString(), QStringLiteral("media-play"));
        // Dark: the icons follow at once.
        setTheme("dark");
        QTRY_COMPARE(colours(), all(QStringLiteral("#e8edf2")));
        // (the controls take the theme's palette at the next event loop pass)
        auto *controls = engine->rootObjects().first()->property("palette").value<QObject *>();
        QVERIFY(controls);
        QTRY_COMPARE(controls->property("window").value<QColor>(), QColor(0x20, 0x26, 0x2D));
        QCOMPARE(icon("previousFrame")->property("accentColor").value<QColor>().name(), QStringLiteral("#9cdbc9"));
        QTRY_VERIFY(shown(QColor(0xE8, 0xED, 0xF2)));
        saveTransport(QStringLiteral("transport-dark.png"));
        // Dark's purple accent preset: the accent layer follows.
        settings.setValue(QStringLiteral("appearance.darkAccent"), QStringLiteral("purple"));
        QTRY_COMPARE(icon("previousFrame")->property("accentColor").value<QColor>(),
                     ui::theme::accent(true, QStringLiteral("purple")).accent);
        QCOMPARE(colours(), all(QStringLiteral("#e8edf2")));
        // High contrast black, then its "Text and icons" pick.
        setTheme("highContrastBlack");
        QTRY_COMPARE(colours(), all(QStringLiteral("#ffffff")));
        QCOMPARE(icon("previousFrame")->property("accentColor").value<QColor>().name(), QStringLiteral("#ffff00"));
        settings.setValue(QStringLiteral("appearance.highContrastBlack.text"), QStringLiteral("#FF8800"));
        QTRY_COMPARE(colours(), all(QStringLiteral("#ff8800")));
        QTRY_VERIFY(shown(QColor(0xFF, 0x88, 0x00)));
        setTheme("dark");
        QTRY_COMPARE(colours(), all(QStringLiteral("#e8edf2")));
        // Back at the first frame Previous frame is disabled: the dark
        // theme's disabled colour, accent included.
        application->video().stepFrames(-1);
        QTRY_COMPARE(icon("previousFrame")->property("color").value<QColor>().name(), QStringLiteral("#75818d"));
        QCOMPARE(icon("previousFrame")->property("accentColor").value<QColor>().name(), QStringLiteral("#75818d"));
        QCOMPARE(icon("nextFrame")->property("color").value<QColor>().name(), QStringLiteral("#e8edf2"));
    }

    // K1: representative surfaces show the set's icons and keep their
    // accessible names: the audio box's buttons (22 px squares, as legacy's
    // MappedButtons), the Line editor's tag buttons, the menus (the icon as
    // the item's image source), the document tabs (close, new and the
    // modified mark), the Search tool's tabs, a dialog's title and the
    // windows' icons.
    void iconSurfacesShowTheSetWithTheirNames()
    {
        const auto name = [](QObject *object) {
            auto *a = QAccessible::queryAccessibleInterface(object);
            return a ? a->text(QAccessible::Name) : QString();
        };
        const auto icon = [](QQuickItem *control) {
            auto *content = control ? control->property("contentItem").value<QQuickItem *>() : nullptr;
            return content && content->property("valid").toBool() ? content->property("iconRole").toString() : QString();
        };
        const auto url = [](QObject *item) {
            QQmlExpression expression(qmlContext(item), item, QStringLiteral("icon.source.toString()"));
            return expression.evaluate().toString();
        };
        struct Expected {
            const char *object, *role, *name;
        };
        // The audio box (its buttons exist before audio is open).
        for (const Expected &e : {Expected{"audioPrevious", "audio-previous-line", "Play the previous line"},
                                  Expected{"audioCommit", "commit", "Apply changes"},
                                  Expected{"audioPlay500After", "play-after-end", "Play 500ms after the end time"},
                                  Expected{"audioKaraoke", "karaoke", "Enable / disable karaoke creation"},
                                  Expected{"audioSpectrumNonLinear", "spectrum-nonlinear", "Enhance speech frequencies in the spectrum"}}) {
            auto *button = visualItem(e.object);
            QVERIFY2(button, e.object);
            QCOMPARE(icon(button), QLatin1String(e.role));
            QCOMPARE(name(button), QLatin1String(e.name));
            QCOMPARE(button->property("display").toInt(), 0); // AbstractButton.IconOnly
            QCOMPARE(QSizeF(button->implicitWidth(), button->implicitHeight()), QSizeF(22, 22));
        }
        QCOMPARE(icon(visualItem("audioLink")), QStringLiteral("link"));
        QCOMPARE(name(visualItem("audioLink")), QStringLiteral("Link the volume and stretch sliders"));
        // The Line editor's tag buttons.
        for (const Expected &e : {Expected{"tag_b", "tag-bold", "Bold"}, Expected{"tag_s", "tag-strikeout", "Strikeout"},
                                  Expected{"changeFont", "tag-font", "Font selection"},
                                  Expected{"changeColour1", "colour-primary", "Primary color"},
                                  Expected{"changeColour4", "colour-shadow", "Shadow color"}}) {
            auto *button = visualItem(e.object);
            QVERIFY2(button, e.object);
            QCOMPARE(icon(button), QLatin1String(e.role));
            QCOMPARE(name(button), QLatin1String(e.name));
            QVERIFY(button->property("tip").toString().startsWith(QLatin1String(e.name)));
        }
        // The menus: the item's image is the set's icon in the palette's colours.
        auto *root = engine->rootObjects().first();
        const QString normal = ui::IconTheme::colours()[0].name().mid(1);
        const QString accent = ui::IconTheme::colours()[1].name().mid(1);
        const QString disabled = ui::IconTheme::colours()[3].name().mid(1);
        for (const Expected &e : {Expected{"settingsMenuItem", "settings", ""}, Expected{"aboutMenuItem", "about", ""},
                                  Expected{"openAudioMenuItem", "open-audio", ""}, Expected{"recentSubtitlesMenu", "recent-subtitles", ""},
                                  Expected{"loadLastSessionMenuItem", "last-session", ""}}) {
            auto *menuItem = root->findChild<QObject *>(QLatin1String(e.object));
            QVERIFY2(menuItem, e.object);
            QCOMPARE(url(menuItem), QStringLiteral("image://hikari-icon/%1/%2/%3/0").arg(QLatin1String(e.role), normal, accent));
        }
        // a disabled item (no document: Save is disabled), the whole icon in the disabled colour
        QCOMPARE(url(root->findChild<QObject *>(QStringLiteral("saveMenuItem"))),
                 QStringLiteral("image://hikari-icon/save/%1/%1/0").arg(disabled));
        // The document tabs: close, new and the modified mark.
        QVERIFY(application->openFile(episode));
        QTRY_VERIFY(visualItem("documentTabClose0") && visualItem("documentTabClose0")->isVisible());
        auto *close = visualItem("documentTabClose0");
        QCOMPARE(icon(close), QStringLiteral("tab-close"));
        QCOMPARE(name(close), QStringLiteral("Close episode.ass"));
        QCOMPARE(QSizeF(close->implicitWidth(), close->implicitHeight()), QSizeF(18, 18));
        QCOMPARE(icon(visualItem("newTabButton")), QStringLiteral("tab-new"));
        QCOMPARE(name(visualItem("newTabButton")), QStringLiteral("Open new tab"));
        QVERIFY(!visualItem("documentTabModified0")->isVisible());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 5);
        QTest::keyClick(window, 'x');
        press(Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(visualItem("documentTab0")->property("text").toString(), QStringLiteral("1*episode.ass"));
        QTRY_VERIFY(visualItem("documentTabModified0")->isVisible());
        QCOMPARE(visualItem("documentTabModified0")->property("iconRole").toString(), QStringLiteral("document-modified"));
        // The Search tool's tabs (Ctrl+H opens the tool).
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_H, Qt::ControlModifier);
        QObject *tool = nullptr;
        QTRY_VERIFY((tool = item<QObject>("searchTool")) != nullptr);
        auto *findTab = tool->findChild<QObject *>(QStringLiteral("findTab"));
        auto *replaceTab = tool->findChild<QObject *>(QStringLiteral("replaceTab"));
        QVERIFY(findTab && replaceTab);
        QCOMPARE(url(findTab), QStringLiteral("image://hikari-icon/search/%1/%2/0").arg(normal, accent));
        QCOMPARE(url(replaceTab), QStringLiteral("image://hikari-icon/find-replace/%1/%2/0").arg(normal, accent));
        QCOMPARE(findTab->property("text").toString(), QStringLiteral("Find"));
        // A dialog's title shows its icon.
        auto *dialog = openSettings();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *title = dialog->findChild<QQuickItem *>(QStringLiteral("settingsDialogTitle"));
        QVERIFY(title && title->isVisible());
        QCOMPARE(title->property("text").toString(), QStringLiteral("Options"));
        auto *titleIcon = title->findChild<QQuickItem *>(QStringLiteral("dialogIcon"));
        QVERIFY(titleIcon && titleIcon->property("valid").toBool());
        QCOMPARE(titleIcon->property("iconRole").toString(), QStringLiteral("settings"));
        QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
        // The windows' icons.
        for (const char *w : {"historyWindow", "styleManager", "automationManagerWindow"}) {
            auto *shown = root->findChild<QQuickWindow *>(QLatin1String(w));
            QVERIFY2(shown && !shown->icon().isNull(), w);
        }
    }

    // K1, K2: screenshots of the wired surfaces for review (the main window,
    // the audio box, the Line editor, the File menu and the Options dialog's
    // Appearance page) in the four themes of the theme layer, written to
    // HIKARI_SURFACE_SHOT_DIR when it is set (main-window-<theme>.png, ...).
    // Keyboard focus is shown: the Grid has it in the main window (its
    // panel header and current row ringed), Follow system theme on the
    // Appearance page.
    void surfaceScreenshots()
    {
        const QString out = qEnvironmentVariable("HIKARI_SURFACE_SHOT_DIR");
        if (out.isEmpty())
            QSKIP("HIKARI_SURFACE_SHOT_DIR is not set");
        QVERIFY(QDir().mkpath(out));
        restartWithoutSound();
        auto &settings = *application->settingsStore();
        window->resize(1600, 900);
        QVERIFY(application->openFile(episode));
        application->video().openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        application->video().stepFrames(1);
        // (the resolution question the video asks, which comes a moment later)
        auto *mismatch = engine->rootObjects().first()->findChild<QObject *>(QStringLiteral("mismatchDialog"));
        if (mismatch && QTest::qWaitFor([&] { return mismatch->property("visible").toBool(); }, 3000))
            QMetaObject::invokeMethod(mismatch, "close");
        QTRY_VERIFY(!mismatch || !mismatch->property("visible").toBool());
        application->audio().openDummy();
        QTRY_VERIFY(application->audio().ready());
        QTRY_VERIFY(item("audioButtons")->isVisible());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        auto *text = item("lineText");
        QTRY_COMPARE(text->property("text").toString(), QStringLiteral("first"));
        text->forceActiveFocus();
        text->setProperty("cursorPosition", 5);
        QTest::keyClick(window, 'x');
        press(Qt::Key_Return, Qt::ControlModifier); // a modified tab
        const auto crop = [](QQuickItem *item) {
            const qreal dpr = item->window()->effectiveDevicePixelRatio();
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            return QRectF(r.topLeft() * dpr, r.size() * dpr).toAlignedRect();
        };
        auto *root = engine->rootObjects().first();
        settings.setValue(QStringLiteral("appearance.followSystem"), false);
        for (const auto code : ui::theme::kCodes) {
            settings.setValue(QStringLiteral("appearance.theme"), ui::theme::codeName(code));
            auto *controls = root->property("palette").value<QObject *>();
            QTRY_COMPARE(controls->property("window").value<QColor>(), ui::theme::current().roles.panel);
            const QString suffix = QLatin1Char('-') + ui::theme::codeName(code) + QStringLiteral(".png");
            item("editingGrid")->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(item("editingGrid")->hasActiveFocus());
            QTest::qWait(200);
            const QImage shot = window->grabWindow();
            QVERIFY(shot.save(out + QStringLiteral("/main-window") + suffix));
            QVERIFY(shot.copy(crop(visualItem("audioPanel"))).save(out + QStringLiteral("/audio-box") + suffix));
            QVERIFY(shot.copy(crop(visualItem("editorPanel"))).save(out + QStringLiteral("/line-editor") + suffix));
            // The File menu, open.
            auto *bar = root->findChild<QQuickItem *>(QStringLiteral("fileMenuBarItem"));
            auto *menu = bar->property("menu").value<QObject *>();
            QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QQuickItem *, bar), Q_ARG(QPointF, QPointF(0, bar->height()))));
            QTRY_VERIFY(menu->property("opened").toBool());
            auto *content = menu->property("contentItem").value<QQuickItem *>();
            QTest::qWait(300);
            const QImage menuShot = content->window()->grabWindow();
            QRect r = crop(content);
            r = (content->window() == window ? r.adjusted(-4, -4, 4, 4) : menuShot.rect()).intersected(menuShot.rect());
            QVERIFY(menuShot.copy(r).save(out + QStringLiteral("/file-menu") + suffix));
            QMetaObject::invokeMethod(menu, "close");
            QTRY_VERIFY(!menu->property("visible").toBool());
            // K2: the Options dialog's Appearance page.
            auto *dialog = openSettings();
            QTRY_VERIFY(dialog->property("opened").toBool());
            dialogItem("settingsDialog", "settingsPages")->setProperty("currentIndex", 6);
            QTRY_VERIFY(dialogItem("settingsDialog", "settingsPageAppearance")->isVisible());
            dialogItem("settingsDialog", "appearanceFollowSystem")->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(dialogItem("settingsDialog", "appearanceFollowSystem")->property("visualFocus").toBool());
            QTest::qWait(300);
            const QImage dialogShot = window->grabWindow();
            auto *dialogItemRoot = dialog->property("contentItem").value<QQuickItem *>()->parentItem();
            QVERIFY(dialogShot.copy(crop(dialogItemRoot).adjusted(-8, -8, 8, 8).intersected(dialogShot.rect()))
                        .save(out + QStringLiteral("/appearance-page") + suffix));
            QVERIFY(QMetaObject::invokeMethod(settingsButton("settingsCancel"), "click"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
    }

    // V6: Insert start / end time from video (the Video menu, GLOBAL_SET_START_TIME
    // Ctrl+Left and GLOBAL_SET_END_TIME Ctrl+Right): every selected Line gets the
    // shown frame's legacy start (or end) representative plus GRID_INSERT_*_OFFSET,
    // to centiseconds, one step each (HikariSubFrame.cpp:750-760, SubsGrid::SetStartTime).
    void insertTimesFromTheVideo()
    {
        QVERIFY(application->openFile(episode)); // 1.00-2.00 and 3.00-4.00 s
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto *start = item<QObject>("setStartTimeMenuItem");
        auto *end = item<QObject>("setEndTimeMenuItem");
        QVERIFY(start && end);
        QVERIFY(!start->property("enabled").toBool()); // OnMenuOpened: a video and the editor
        QVERIFY(!application->setTimeFromVideo(false));
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame() == std::optional<int>(0), 20000);
        QVERIFY(start->property("enabled").toBool());
        QVERIFY(application->video().showFrameAt(24)); // 1001 ms
        QTRY_COMPARE(application->video().session().shownFrame().value_or(-1), 24);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(start, "click"));
        // StartTimeFor(24) = 959 + (1001 - 959) / 2 + 5 = 985, to centiseconds 980
        QTRY_COMPARE(session->document().lines()[0]->start.value.microseconds(), 980'000);
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Setting start time"));
        // Ctrl+Right: EndTimeFor(24) = 1001 + (1042 - 1001) / 2 + 5 = 1026, to 1020
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Right, Qt::ControlModifier);
        QTRY_COMPARE(session->document().lines()[0]->end.value.microseconds(), 1'020'000);
        QCOMPARE(session->history().back().name, std::string("Setting end time"));
        QCOMPARE(session->historySize(), steps + 2);
        // V6-insert-time-no-op: the same frame again changes nothing and
        // records no step (legacy recorded one)
        QVERIFY(application->setTimeFromVideo(true));
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 1'020'000);
        QCOMPARE(session->historySize(), steps + 2);
        // the offset counts before the centiseconds: ZEROIT(985 + 25) = 1010
        application->settingsStore()->set("grid.insertStartOffset", 25);
        press(Qt::Key_Left, Qt::ControlModifier);
        QTRY_COMPARE(session->document().lines()[0]->start.value.microseconds(), 1'010'000);
        QCOMPARE(session->document().lines()[1]->start.value.microseconds(), 3'000'000); // not selected
        // each is one step: Undo takes the last back
        QVERIFY(application->editor().undo());
        QTRY_COMPARE(session->document().lines()[0]->start.value.microseconds(), 980'000);
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 1'020'000);
    }

    // V6: GLOBAL_SELECT_FROM_VIDEO (F2, SubsGrid::SelVideoLine) and the Grid's
    // "Select all lines visible on video" (GRID_SELECT_VISIBLE_LINES, SelectVisible).
    void selectLinesFromTheVideo()
    {
        const QString path = writeFile(dir, "v6-select.ass",
                                       "Dialogue: 0,0:00:00.00,0:00:00.50,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:00.40,0:00:01.20,Default,,0,0,0,,b\n"
                                       "Comment: 0,0:00:00.40,0:00:01.20,Default,,0,0,0,,note\n"
                                       "Dialogue: 0,0:00:01.50,0:00:01.90,Default,,0,0,0,,c\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto lines = session->document().lines();
        QVERIFY(!application->selectLineFromVideo()); // no video: nothing
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().session().shownFrame() == std::optional<int>(0), 20000);
        QVERIFY(application->video().showFrameAt(34)); // 34 * 1001 / 24 = 1417 ms
        QTRY_COMPARE(application->video().session().shownFrame().value_or(-1), 34);
        keysNeverRepeat();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_F2);
        // no Line holds 1417 ms; c starts 83 ms later, b 1017 ms earlier
        QTRY_COMPARE(session->selection().active, std::optional(lines[3]->id));
        QCOMPARE(session->selection().selected, std::set<core::LineId>{lines[3]->id});
        QVERIFY(application->video().showFrameAt(11)); // 458 ms: a and b
        QTRY_COMPARE(application->video().session().shownFrame().value_or(-1), 11);
        press(Qt::Key_F2);
        QTRY_COMPARE(session->selection().active, std::optional(lines[0]->id)); // the first holding it
        auto *menuItem = named("selectVisibleLines");
        QVERIFY(menuItem);
        QVERIFY(QMetaObject::invokeMethod(menuItem, "triggered"));
        // Start - 5 <= 458 < End - 5, the Comment left out; the first becomes active
        QTRY_COMPARE(session->selection().selected, (std::set<core::LineId>{lines[0]->id, lines[1]->id}));
        QCOMPARE(session->selection().active, std::optional(lines[0]->id));
        QVERIFY(application->video().showFrameAt(47)); // 1960 ms: none
        QTRY_COMPARE(application->video().session().shownFrame().value_or(-1), 47);
        QVERIFY(application->selectLinesVisibleOnVideo());
        QVERIFY(session->selection().selected.empty()); // legacy cleared the selection
        QCOMPARE(session->selection().active, std::optional(lines[0]->id));
        // V6-select-shown-fallback: with a hidden, F2's fallbacks are the first
        // shown Line, b; legacy took the hidden row 0
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home); // a
        QTRY_COMPARE(session->selection().selected, std::set<core::LineId>{lines[0]->id});
        auto *root = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("hideSelectedLines")), "triggered"));
        QTRY_VERIFY(application->shell().filtered());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End); // c
        QTRY_COMPARE(session->selection().active, std::optional(lines[3]->id));
        // 1960 ms, the video's last frame: c's start is 460 ms back and the
        // video's end 0 ms ahead, so the later fallback
        press(Qt::Key_F2);
        QTRY_COMPARE(session->selection().active, std::optional(lines[1]->id));
        QCOMPARE(session->selection().selected, std::set<core::LineId>{lines[1]->id});
        press(Qt::Key_End); // c
        QTRY_COMPARE(session->selection().active, std::optional(lines[3]->id));
        // 83 ms: before every shown Line's start (b's 317 ms ahead, nothing
        // shown starts in (0, 83)), so the earlier fallback
        QVERIFY(application->video().showFrameAt(2));
        QTRY_COMPARE(application->video().session().shownFrame().value_or(-1), 2);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_F2);
        QTRY_COMPARE(session->selection().active, std::optional(lines[1]->id));
        QCOMPARE(session->selection().selected, std::set<core::LineId>{lines[1]->id});
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("turnOffFiltering")), "triggered"));
        QTRY_VERIFY(!application->shell().filtered());
    }

    // V6: GLOBAL_SNAP_WITH_START / _END (Shift+Left / Shift+Right,
    // HikariSubFrame::OnAudioSnap): the nearest keyframe (or other Line's
    // boundary) within 5 s, needing the audio box and an indexed video; one
    // "Snapping to keyframe" step.
    void snapToKeyframeNeedsTheAudioBox()
    {
        const QString path = writeFile(dir, "v6-snap.ass",
                                       "Dialogue: 0,0:00:00.00,0:00:00.30,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:00.45,0:00:00.95,Default,,0,0,0,,b\n"
                                       "Dialogue: 0,0:00:01.10,0:00:01.50,Default,,0,0,0,,c\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Down); // b
        QVERIFY(!application->snapToKeyframe(true)); // no audio box, no video
        application->video().openVideo(nativeFixture("audiodelay.mkv")); // keyframes every 12 frames
        auto &audio = application->audio();
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QTRY_VERIFY(!application->video().session().keyframes().empty());
        const auto steps = session->historySize();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Left, Qt::ShiftModifier);
        // keyframe 12 (500 ms): StartTimeFor(12) = 458 + 21 + 5 = 484, to 480
        QTRY_COMPARE(session->document().lines()[1]->start.value.microseconds(), 480'000);
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->history().back().name, std::string("Snapping to keyframe"));
        press(Qt::Key_Right, Qt::ShiftModifier);
        // keyframe 24 (1001 ms): StartTimeFor(24) = 985, to 980
        QTRY_COMPARE(session->document().lines()[1]->end.value.microseconds(), 980'000);
        QCOMPARE(session->historySize(), steps + 2);
        // the start again: 480 is on keyframe 12 (a zero difference does not
        // count); the previous Line's End (AUDIO_INACTIVE_LINES_DISPLAY_MODE 1)
        // is 180 ms away, nearer than keyframe 0 at 480 ms (HikariSubFrame.cpp:2539-2583)
        QVERIFY(application->snapToKeyframe(true));
        QCOMPARE(session->document().lines()[1]->start.value.microseconds(), 300'000);
        QCOMPARE(session->historySize(), steps + 3);
        // V6-snap-next-line: the End 980 sits on keyframe 24; mode 1 now
        // reaches the next Line, whose Start (1.10 s, 120 ms away) is nearer
        // than keyframes 12 and 36 (500 ms); legacy stopped before it and took 480
        QVERIFY(application->snapToKeyframe(false));
        QCOMPARE(session->document().lines()[1]->end.value.microseconds(), 1'100'000);
        QCOMPARE(session->historySize(), steps + 4);
        audio.closeAudio();
        QVERIFY(!application->snapToKeyframe(false)); // the audio box is gone
    }

    // V6: the video toolbar's "Move video to selected line on:"
    // (MOVE_VIDEO_TO_ACTIVE_LINE) and OPEN_VIDEO_AT_ACTIVE_LINE: by legacy's
    // defaults the video opens at its first frame and stays when the active
    // Line changes; a double click moves it (the End column to the end);
    // "Every line change" follows each change; "Editing line when paused"
    // follows an edit.
    void videoFollowsTheActiveLineAsChosen()
    {
        auto &settings = *application->settingsStore();
        QCOMPARE(settings.integer("video.moveToActiveLine"), 0);
        QCOMPARE(settings.integer("video.playAfterSelection"), 0);
        QVERIFY(!settings.boolean("video.openAtActiveLine"));
        auto *seekAfter = item<QObject>("videoSeekAfter");
        auto *playAfter = item<QObject>("videoPlayAfter");
        QVERIFY(seekAfter && playAfter);
        QCOMPARE(seekAfter->property("model").toStringList(),
                 (QStringList{"Double-clicking a line (always on)", "Every line change",
                              "Clicking a line or editing when paused", "Clicking a line or editing",
                              "Editing line when paused", "Editing"}));
        QCOMPARE(playAfter->property("model").toStringList(),
                 (QStringList{"Nothing", "Audio to the line end time", "Video and audio to the line end time",
                              "Video and audio to the next line start time"}));
        const QString path = writeFile(dir, "v6-follow.ass",
                                       "Dialogue: 0,0:00:00.50,0:00:01.00,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:01.00,0:00:01.50,Default,,0,0,0,,b\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto lines = session->document().lines();
        auto &video = application->video();
        video.openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.session().shownFrame() == std::optional<int>(0), 20000); // not at a's start
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Down); // b
        QTest::qWait(200);
        QCOMPARE(video.frame(), 0); // "Double-clicking a line": a key moves nothing
        // a double click on b's End cell: the frame shown just before 1.50 s
        auto *grid = qobject_cast<ui::LineGrid *>(item("editingGrid"));
        int endColumn = -1;
        for (int c = 0; c < grid->columnCount(); ++c)
            if (grid->columnTitle(c) == QLatin1String("End"))
                endColumn = c;
        QVERIFY(endColumn >= 0);
        const QPoint endCell = grid->mapToScene(grid->cellRect(1, endColumn).center()).toPoint();
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, endCell);
        QTRY_COMPARE(video.frame(), 35); // FrameShownAt(1499): 35 starts at 1459
        // and on the Start cell, the frame at or after 1.00 s
        int startColumn = endColumn - 1;
        QCOMPARE(grid->columnTitle(startColumn), QStringLiteral("Start"));
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier,
                           grid->mapToScene(grid->cellRect(1, startColumn).center()).toPoint());
        QTRY_COMPARE(video.frame(), 24);
        // "Every line change", chosen in the list
        QVERIFY(QMetaObject::invokeMethod(seekAfter, "activated", Q_ARG(int, 1)));
        QCOMPARE(settings.integer("video.moveToActiveLine"), 1);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Up); // a: 500 ms is frame 12
        QTRY_COMPARE(video.frame(), 12);
        press(Qt::Key_Down);
        QTRY_COMPARE(video.frame(), 24);
        // "Editing line when paused": an edit brings the video to the Line's start
        settings.set("video.moveToActiveLine", 4);
        QCOMPARE(seekAfter->property("currentIndex").toInt(), 4);
        QVERIFY(video.showFrameAt(40));
        QTRY_COMPARE(video.frame(), 40);
        application->editor().setStartText(QStringLiteral("0:00:01.10"));
        QVERIFY(application->editor().commit());
        QTRY_COMPARE(video.frame(), 27); // 1.10 s: frame 27 starts at 1126
        // OPEN_VIDEO_AT_ACTIVE_LINE: the next open starts at b's start
        settings.set("video.openAtActiveLine", true);
        video.openVideo(nativeFixture("vfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo() && video.session().path() == nativeFixture("vfr.mkv").toStdString(), 20000);
        const auto timebase = video.session().legacyTimebase();
        QTRY_COMPARE(video.frame(), std::min(timebase.frameAt(1100), video.frameCount() - 1));
    }

    // V6: an edit committed because the active Line changes. Legacy
    // EditBox::SetLine (EditBox.cpp:383-384) sends the old Line's edit before
    // it loads the new one, so ShowEditOnVideo (SubsGridBase.cpp:1165-1177)
    // seeks to the EDITED Line's start, and modes 2-5 do not seek on the Line
    // change itself (EditBox.cpp:448). Enter is the other way round:
    // SubsGrid::ChangeLine (SubsGridBase.cpp:150-154) runs NextLine before
    // SetModified, so ShowEditOnVideo seeks to the NEW Line's start.
    void videoFollowsAnEditCommittedOnALineChange()
    {
        auto &settings = *application->settingsStore();
        settings.set("video.openAtActiveLine", false);
        settings.set("video.playAfterSelection", 0);
        const QString path = writeFile(dir, "v6-leave.ass",
                                       "Dialogue: 0,0:00:00.50,0:00:01.00,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:01.00,0:00:01.50,Default,,0,0,0,,b\n"
                                       "Dialogue: 0,0:00:01.50,0:00:02.00,Default,,0,0,0,,c\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        const auto lines = session->document().lines();
        const auto a = lines[0]->id, b = lines[1]->id, c = lines[2]->id;
        auto &video = application->video();
        video.openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.session().shownFrame() == std::optional<int>(0), 20000);
        settings.set("video.moveToActiveLine", 4); // "Editing line when paused"
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QCOMPARE(session->selection().active, std::optional(a));
        QVERIFY(video.showFrameAt(40));
        QTRY_COMPARE(video.frame(), 40);
        // a's Start in the editor, not committed; Down commits it on leaving a
        application->editor().setStartText(QStringLiteral("0:00:00.75"));
        item("editingGrid")->forceActiveFocus();
        QCOMPARE(session->draftLine(), std::optional(a));
        QCOMPARE(video.frame(), 40);
        press(Qt::Key_Down);
        QTRY_COMPARE(session->selection().active, std::optional(b));
        QCOMPARE(session->document().lines()[0]->start.value.microseconds(), 750'000);
        QTRY_COMPARE(video.frame(), 18); // a's new start, 0.75 s; not b's 1.00 s (frame 24)
        QTest::qWait(200);
        QCOMPARE(video.frame(), 18);
        // Enter with b's Start pending: c becomes active, then the video goes to c's start
        application->editor().setStartText(QStringLiteral("0:00:01.25"));
        item("lineText")->forceActiveFocus();
        QCOMPARE(session->draftLine(), std::optional(b));
        press(Qt::Key_Return);
        QTRY_COMPARE(session->selection().active, std::optional(c));
        QCOMPARE(session->document().lines()[1]->start.value.microseconds(), 1'250'000);
        QTRY_COMPARE(video.frame(), 36); // c's start, 1.50 s; not b's new 1.25 s (frame 30)
        QTest::qWait(200);
        QCOMPARE(video.frame(), 36);
        settings.set("video.moveToActiveLine", 0);
    }

    // V6: "On moving to another line play:" (VIDEO_PLAY_AFTER_SELECTION) after
    // Enter (SubsGrid::NextLine, autoPlay): the video plays the new Line to
    // the frame before its end (the next Line's start for the last choice),
    // then pauses; a key move plays nothing.
    void playAfterMovingToAnotherLine()
    {
        restartWithoutSound();
        const QString path = writeFile(dir, "v6-play.ass",
                                       "Dialogue: 0,0:00:00.20,0:00:00.40,Default,,0,0,0,,a\n"
                                       "Dialogue: 0,0:00:00.50,0:00:00.80,Default,,0,0,0,,b\n"
                                       "Dialogue: 0,0:00:01.00,0:00:01.20,Default,,0,0,0,,c\n");
        QVERIFY(application->openFile(path));
        auto *session = application->files().session(*application->workspace().editingTarget());
        auto &video = application->video();
        video.openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.session().shownFrame() == std::optional<int>(0), 20000);
        application->settingsStore()->set("video.playAfterSelection", 2); // video to the line end
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down); // a key: no autoPlay
        QTest::qWait(200);
        QVERIFY(!video.playing());
        auto *text = item("lineText");
        text->forceActiveFocus();
        press(Qt::Key_Return); // b becomes active, as NextLine
        QTRY_COMPARE(session->selection().active, std::optional(session->document().lines()[2]->id));
        QTRY_VERIFY_WITH_TIMEOUT(video.playing(), 10000);
        // c plays from 1.00 s to the frame before 1.20 s (PlayEndBefore: frame 28 at 1167), then pauses
        QTRY_VERIFY_WITH_TIMEOUT(!video.playing(), 10000);
        QTRY_VERIFY(video.frame() >= 28 && video.frame() <= 29);
    }

private:
    // E4: the Line editor's metadata fields (legacy EditBox at 20d647c4).
    QString writeStyled(const char *name, const char *events)
    {
        const QString path = dir.filePath(QLatin1String(name));
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                "MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
                "Style: Sign,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,8,10,10,10,1\n"
                "Style: Alpha,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,7,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
        f.write(events);
        return path;
    }
    application::EditSession *targetSession() const
    {
        return application->files().session(*application->workspace().editingTarget());
    }
    // QTest::keyClicks takes widgets only: one key press per character.
    void typeText(const QString &text)
    {
        for (const QChar c : text)
            QTest::keyClick(window, c.toLatin1());
        QCoreApplication::processEvents();
    }
    // Types into a field as a user does: focus, select everything, type.
    void typeInto(const char *name, const QString &text)
    {
        auto *field = item(name);
        QVERIFY(field);
        field->forceActiveFocus();
        QMetaObject::invokeMethod(field, "selectAll");
        typeText(text);
        QCoreApplication::processEvents();
    }

private slots:
    // Field round trips through the draft: each field shows the Line, a typed
    // value is pending until Enter sends it as one step (EDITBOX_COMMIT_GO_NEXT_LINE,
    // EditBox::OnNewline), Undo restores it; the Comment box sends at once.
    void lineFieldsRoundTripThroughTheDraft()
    {
        QVERIFY(application->openFile(writeStyled("fields.ass",
                                                  "Dialogue: 1,0:00:01.00,0:00:02.50,Sign,Ann,0,0,0,Banner;0,first\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,Bob,0,0,0,,second\n")));
        auto *session = targetSession();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("first"));
        QCOMPARE(item<QObject>("layerField")->property("text").toString(), QStringLiteral("1"));
        QCOMPARE(item<QObject>("durationField")->property("text").toString(), QStringLiteral("0:00:01.50"));
        QCOMPARE(item<QObject>("commentBox")->property("checked").toBool(), false);
        // The Style list is sorted (HikariChoice::Sort) and shows the Line's Style.
        QCOMPARE(application->editor().styleNames(), (QStringList{"Alpha", "Default", "Sign"}));
        QCOMPARE(item<QObject>("styleChoice")->property("currentText").toString(), QStringLiteral("Sign"));
        QCOMPARE(item<QObject>("actorBox")->property("editText").toString(), QStringLiteral("Ann"));
        QCOMPARE(item<QObject>("effectBox")->property("editText").toString(), QStringLiteral("Banner;0"));
        // RebuildActorEffectLists: each value once, sorted.
        QCOMPARE(application->editor().actors(), (QStringList{"Ann", "Bob"}));
        QCOMPARE(application->editor().effects(), QStringList{"Banner;0"});
        const auto steps = session->historySize();

        typeInto("layerField", QStringLiteral("4"));
        typeInto("marginLeftField", QStringLiteral("12")); // finishing the Layer field puts it in the draft
        QCOMPARE(session->document().lines()[0]->layer.value, 1); // pending only
        QVERIFY(session->draftLine());
        press(Qt::Key_Return); // Enter in a field: commit and go to the next Line
        QCOMPARE(session->document().lines()[0]->layer.value, 4);
        QCOMPARE(session->document().lines()[0]->marginLeft.value, 12);
        QCOMPARE(session->historySize(), steps + 1);
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("second"));
        QVERIFY(application->editor().undo());
        QCOMPARE(session->document().lines()[0]->layer.value, 1);
        QCOMPARE(session->document().lines()[0]->marginLeft.value, 0);

        // The Actor box: typing is pending, Enter sends it and rebuilds the list.
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_End);
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("second"));
        auto *actor = item("actorBox");
        auto *actorText = actor->property("contentItem").value<QQuickItem *>();
        QVERIFY(actorText);
        actorText->forceActiveFocus();
        QMetaObject::invokeMethod(actorText, "selectAll");
        typeText(QStringLiteral("Cy,"));
        QCOMPARE(application->editor().actor(), QStringLiteral("Cy")); // commas cannot be typed
        QCOMPARE(session->document().lines()[1]->actor, u8"Bob");
        press(Qt::Key_Return);
        QCOMPARE(session->document().lines()[1]->actor, u8"Cy");
        QCOMPARE(application->editor().actors(), (QStringList{"Ann", "Cy"}));

        // The Comment box sends the Line at once (OnCommit) and empties the counters.
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("first"));
        const auto beforeComment = session->historySize();
        auto *comment = item("commentBox");
        QTest::mouseClick(window, Qt::LeftButton, {}, comment->mapToScene(QPointF(8, comment->height() / 2)).toPoint());
        QTRY_VERIFY(session->document().lines()[0]->comment);
        QCOMPARE(session->historySize(), beforeComment + 1);
        QVERIFY(item<QObject>("commentBox")->property("checked").toBool());
        QCOMPARE(item<QObject>("charsCounter")->property("text").toString(), QString());
        QCOMPARE(item<QObject>("cpsCounter")->property("text").toString(), QString());
        QVERIFY(application->editor().undo());
        QVERIFY(!item<QObject>("commentBox")->property("checked").toBool());

        // The Style choice sends at once too.
        QVERIFY(QMetaObject::invokeMethod(item("styleChoice"), "activated", Q_ARG(int, 0)));
        QCOMPARE(session->document().lines()[0]->style, u8"Alpha");
        QCOMPARE(item<QObject>("styleChoice")->property("currentText").toString(), QStringLiteral("Alpha"));
        // A pick from the Effect list sends it.
        QVERIFY(application->editor().chooseEffect(QStringLiteral("Banner;0")));
        QCOMPARE(session->document().lines()[0]->effect, u8"Banner;0");
    }

    // E63-invalid-commit: an End before the Start blocks the draft with the
    // other fields in it; the legacy preference commits it.
    void lineFieldsWaitBehindAnInvalidTime()
    {
        QVERIFY(application->openFile(writeStyled("invalid.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n")));
        auto *session = targetSession();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        typeInto("layerField", QStringLiteral("2"));
        typeInto("endField", QStringLiteral("0:00:00.50"));
        press(Qt::Key_Return);
        QCOMPARE(item<QObject>("editorProblem")->property("text").toString(), QStringLiteral("End is before Start."));
        QCOMPARE(session->document().lines()[0]->layer.value, 0);
        QVERIFY(session->draftLine());
        session->setInvalidCommitPolicy(application::InvalidCommitPolicy::Legacy);
        QVERIFY(application->editor().commit());
        QCOMPARE(session->document().lines()[0]->layer.value, 2);
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 500'000);
    }

    // Enter in Start, End or Duration commits and stays on the Line with
    // EDITBOX_DONT_GO_TO_NEXT_LINE_ON_TIMES_EDIT (EditBox::OnNewline,
    // EditBox.cpp:987-1001); elsewhere, and without it, it goes on.
    void enterInTheTimeFieldsFollowsTheOption()
    {
        QVERIFY(application->openFile(writeStyled("times.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n"
                                                  "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,third\n")));
        auto *session = targetSession();
        const auto lines = session->document().lines();
        const auto first = lines[0]->id, second = lines[1]->id;
        application->settingsStore()->setValue(QStringLiteral("editor.dontGoToNextLineOnTimesEdit"), true);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        typeInto("startField", QStringLiteral("0:00:01.50"));
        press(Qt::Key_Return);
        QCOMPARE(session->document().lines()[0]->start.value.microseconds(), 1'500'000);
        QCOMPARE(session->selection().active, std::optional(first)); // stays
        // Ctrl+Enter (EDITBOX_COMMIT) applies and stays, in any field.
        typeInto("marginVerticalField", QStringLiteral("5"));
        press(Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(session->document().lines()[0]->marginVertical.value, 5);
        QCOMPARE(session->selection().active, std::optional(first));
        typeInto("layerField", QStringLiteral("3"));
        press(Qt::Key_Return); // not a time field: goes on
        QCOMPARE(session->document().lines()[0]->layer.value, 3);
        QCOMPARE(session->selection().active, std::optional(second));
        application->settingsStore()->setValue(QStringLiteral("editor.dontGoToNextLineOnTimesEdit"), false);
        typeInto("endField", QStringLiteral("0:00:04.50"));
        press(Qt::Key_Return);
        QCOMPARE(session->document().lines()[1]->end.value.microseconds(), 4'500'000);
        QCOMPARE(session->selection().active, std::optional(session->document().lines()[2]->id));
    }

    // DurEdit: End = Start + the duration (OnEdit's durFocus branch,
    // EditBox.cpp:1551-1555), followed as it is typed with live editing on.
    // E4-duration-live-off: without live editing the Duration still moves
    // End, when the field applies (Enter or leaving it) as Start and End do;
    // legacy's Send kept the End there (EditBox.cpp:576-580).
    void durationMovesTheEndWhateverLiveEditing()
    {
        QVERIFY(application->openFile(writeStyled("duration.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n")));
        auto *session = targetSession();
        application->settingsStore()->setValue(QStringLiteral("editor.dontGoToNextLineOnTimesEdit"), true);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        auto *end = item<QObject>("endField");
        typeInto("durationField", QStringLiteral("0:00:03.25"));
        // OnEdit runs on each NUMBER_CHANGED: End follows before Enter.
        QCOMPARE(end->property("text").toString(), QStringLiteral("0:00:04.25"));
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 2'000'000);
        press(Qt::Key_Return);
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 4'250'000);
        QCOMPARE(end->property("text").toString(), QStringLiteral("0:00:04.25"));

        // Live editing off: nothing follows while it is typed; Enter moves End.
        application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), true);
        typeInto("durationField", QStringLiteral("0:00:01.00"));
        QCOMPARE(end->property("text").toString(), QStringLiteral("0:00:04.25"));
        QVERIFY(!session->draftLine());
        press(Qt::Key_Return);
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 2'000'000);
        QCOMPARE(end->property("text").toString(), QStringLiteral("0:00:02.00"));
        QCOMPARE(item<QObject>("durationField")->property("text").toString(), QStringLiteral("0:00:01.00"));

        // Leaving the field applies it too, from the Start just typed, and
        // Enter sends both as one step.
        typeInto("startField", QStringLiteral("0:00:01.50"));
        typeInto("durationField", QStringLiteral("0:00:02.00")); // Start applies on leaving
        item("editingGrid")->forceActiveFocus();                 // Duration applies on leaving
        QCOMPARE(session->draftRecord()->start.value.microseconds(), 1'500'000);
        QCOMPARE(session->draftRecord()->end.value.microseconds(), 3'500'000);
        QCOMPARE(end->property("text").toString(), QStringLiteral("0:00:03.50"));
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 2'000'000); // not sent yet
        QVERIFY(application->editor().commit());
        QCOMPARE(session->document().lines()[0]->start.value.microseconds(), 1'500'000);
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 3'500'000);
        application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), false);
        application->settingsStore()->setValue(QStringLiteral("editor.dontGoToNextLineOnTimesEdit"), false);
    }

    // Start and End go to the Line copy as they are typed (TimeCtrl's
    // NUMBER_CHANGED runs EditBox::OnEdit, EditBox.cpp:343-346): Duration and
    // the counters follow before Enter, and the focused field takes the
    // warning colour while Start is after End (EditBox.cpp:1526-1545). Send
    // and SetLine put the text colour back (EditBox.cpp:546-554, 394-402).
    void timeFieldsApplyAsTheyAreTyped()
    {
        QVERIFY(application->openFile(writeStyled("typed.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\\Nfirst\\Nfirst\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n")));
        auto *session = targetSession();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        auto *start = item("startField");
        auto *end = item("endField");
        auto *duration = item<QObject>("durationField");
        auto *cps = item<QObject>("cpsCounter");
        QCOMPARE(cps->property("text").toString(), QStringLiteral("Characters per second: 15<=15"));
        const auto colour = [](QQuickItem *field, const char *role) {
            return field->property("palette").value<QObject *>()->property(role).value<QColor>();
        };

        typeInto("startField", QStringLiteral("0:00:01.50"));
        QVERIFY(start->hasActiveFocus());
        QCOMPARE(start->property("text").toString(), QStringLiteral("0:00:01.50"));
        QCOMPARE(duration->property("text").toString(), QStringLiteral("0:00:00.50"));
        QCOMPARE(cps->property("text").toString(), QStringLiteral("Characters per second: 30<=15"));
        QCOMPARE(session->draftRecord()->start.value.microseconds(), 1'500'000);
        QCOMPARE(session->document().lines()[0]->start.value.microseconds(), 1'000'000); // not sent yet
        QVERIFY(!application->editor().startWarning());

        // End before Start: End, which has the focus, is in the warning colour
        // (WINDOW_WARNING_ELEMENTS: the theme layer's warning role).
        typeInto("endField", QStringLiteral("0:00:01.20"));
        QVERIFY(application->editor().endWarning());
        QVERIFY(!application->editor().startWarning());
        QCOMPARE(end->property("color").value<QColor>(), ui::theme::current().roles.warning);
        QCOMPARE(duration->property("text").toString(), QStringLiteral("0:00:00.00")); // clamped at 0
        typeInto("endField", QStringLiteral("0:00:03.00"));
        QVERIFY(!application->editor().endWarning());
        QCOMPARE(end->property("color").value<QColor>(), colour(end, "text"));
        QCOMPARE(duration->property("text").toString(), QStringLiteral("0:00:01.50"));

        // Start after End: Start is warned; Send puts the colour back even
        // when the Line is not sent (OnCommit, EditBox.cpp:546-554).
        typeInto("startField", QStringLiteral("0:00:04.00"));
        QVERIFY(application->editor().startWarning());
        QCOMPARE(start->property("color").value<QColor>(), ui::theme::current().roles.warning);
        application->editor().commit();
        QVERIFY(!application->editor().startWarning());
        application->editor().discard();

        // Legacy's else-if: once Start was warned (changedBackGround stays
        // set), a later End warning is not cleared by a good End, only by
        // Send or SetLine.
        typeInto("endField", QStringLiteral("0:00:00.50"));
        QVERIFY(application->editor().endWarning());
        typeInto("endField", QStringLiteral("0:00:03.00"));
        QVERIFY(application->editor().endWarning());
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down); // SetLine
        QTRY_COMPARE(item<QObject>("lineText")->property("text").toString(), QStringLiteral("second"));
        QVERIFY(!application->editor().endWarning());
        QCOMPARE(session->document().lines()[0]->end.value.microseconds(), 3'000'000);

        // Without live editing OnEdit never runs: nothing follows until Enter.
        application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), true);
        typeInto("startField", QStringLiteral("0:00:03.50"));
        QCOMPARE(duration->property("text").toString(), QStringLiteral("0:00:01.00"));
        QVERIFY(!session->draftLine());
        typeInto("endField", QStringLiteral("0:00:01.00"));
        QVERIFY(!application->editor().endWarning());
        item("editingGrid")->forceActiveFocus(); // End applies on leaving
        QCOMPARE(session->draftRecord()->end.value.microseconds(), 1'000'000);
        QVERIFY(!application->editor().endWarning());
        application->editor().discard();
        application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), false);
    }

    // EditBox::UpdateChars against legacy TextData: "Wraps: <counts>/43" and
    // "Characters per second: <n><=15", warned over 43 characters a wrap and
    // over 15 per second; the translation's counts in translation mode.
    void countersFollowTheEditedText()
    {
        QVERIFY(application->openFile(writeStyled("counters.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\\Nsecond line\n")));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        auto *chars = item<QObject>("charsCounter");
        auto *cps = item<QObject>("cpsCounter");
        QCOMPARE(chars->property("text").toString(), QStringLiteral("Wraps: 5/10/43"));
        QCOMPARE(cps->property("text").toString(), QStringLiteral("Characters per second: 15<=15"));
        QCOMPARE(application->editor().cpsWarning(), false);
        auto *text = item("lineText");
        text->forceActiveFocus();
        press(Qt::Key_End, Qt::ControlModifier);
        typeText(QStringLiteral(" more words here"));
        QTRY_COMPARE(chars->property("text").toString(), QStringLiteral("Wraps: 5/23/43"));
        QCOMPARE(cps->property("text").toString(), QStringLiteral("Characters per second: 28<=15"));
        QVERIFY(application->editor().cpsWarning());
        QVERIFY(!application->editor().charsWarning());
        typeText(QStringLiteral(" and many more words to pass the limit"));
        QTRY_VERIFY(application->editor().charsWarning());
        application->editor().discard();
    }

    // EditBox::SetAlignment and OnAnChoice: the shown Line's position (the
    // Style's, or its text's \an), and a choice puts \an<n> in the first block.
    void alignmentChoiceTagsTheText()
    {
        QVERIFY(application->openFile(writeStyled("an.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,first\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\an4\\b1}second\n")));
        auto *session = targetSession();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        auto *choice = item<QObject>("alignmentChoice");
        QCOMPARE(choice->property("currentIndex").toInt(), 7); // Sign's alignment 8
        QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, 2)));
        QCOMPARE(choice->property("currentIndex").toInt(), 2);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->draftRecord()->text.c_str())),
                 QStringLiteral("{\\an3}first"));
        QVERIFY(item("lineText")->hasActiveFocus());
        application->editor().commit();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Down);
        QTRY_COMPARE(choice->property("currentIndex").toInt(), 3); // the text's \an4
        QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, 8)));
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char *>(session->draftRecord()->text.c_str())),
                 QStringLiteral("{\\an9\\b1}second")); // replaced in place
        application->editor().discard();
        // Several Lines: each one's first block, as one "Editing multiple lines" step.
        application->selectAllLines();
        QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, 0)));
        QCOMPARE(session->history().back().name, std::string("Editing multiple lines"));
        QCOMPARE(session->document().lines()[0]->text, u8"{\\an1}first");
        QCOMPARE(session->document().lines()[1]->text, u8"{\\an1\\b1}second");
    }

    // SubsGrid::ChangeLine: with several Lines selected the sent fields go to
    // each of them in one step.
    void severalSelectedLinesTakeTheSentFields()
    {
        QVERIFY(application->openFile(writeStyled("several.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n"
                                                  "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,third\n")));
        auto *session = targetSession();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        press(Qt::Key_Down, Qt::ShiftModifier);
        QTRY_COMPARE(session->selection().selected.size(), std::size_t(2));
        const auto steps = session->historySize();
        QVERIFY(QMetaObject::invokeMethod(item("styleChoice"), "activated", Q_ARG(int, 2))); // Sign
        QCOMPARE(session->historySize(), steps + 1);
        QCOMPARE(session->document().lines()[0]->style, u8"Sign");
        QCOMPARE(session->document().lines()[1]->style, u8"Sign");
        QCOMPARE(session->document().lines()[2]->style, u8"Default");
        QCOMPARE(session->document().lines()[1]->text, u8"second"); // text was not sent
    }

    // The Edit button opens the Style manager on the shown Line's Style (Y1).
    void styleEditOpensTheStyleManagerOnTheStyle()
    {
        QVERIFY(application->openFile(writeStyled("edit.ass", "Dialogue: 0,0:00:01.00,0:00:02.00,Sign,,0,0,0,,first\n")));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        auto *styles = engine->rootObjects().first()->findChild<QQuickWindow *>(QStringLiteral("styleManager"));
        QVERIFY(styles);
        QVERIFY(QMetaObject::invokeMethod(item("styleEditButton"), "click"));
        QTRY_VERIFY(styles->isVisible());
        const auto selected = styles->property("assSelected").toList(); // Default, Sign, Alpha: Sign
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.value(0).toInt(), 1);
        styles->close();
    }

    // The format decides the fields (EditBox::HideControls): SRT has times but
    // no ASS fields; TMPlayer no End or Duration; MicroDVD and MPL2 show
    // frames and deciseconds (SubsTime::raw).
    void formatsShowTheirOwnFields()
    {
        const QString srt = dir.filePath(QStringLiteral("fields.srt"));
        {
            QFile f(srt);
            f.open(QIODevice::WriteOnly);
            f.write("1\r\n00:00:01,234 --> 00:00:02,500\r\nfirst\r\n\r\n");
        }
        QVERIFY(application->openFile(srt));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        QCOMPARE(item<QObject>("startField")->property("text").toString(), QStringLiteral("00:00:01,234"));
        QCOMPARE(item<QObject>("durationField")->property("text").toString(), QStringLiteral("00:00:01,266"));
        QVERIFY(!item<QObject>("layerField")->property("enabled").toBool());
        QVERIFY(!item<QObject>("styleChoice")->property("enabled").toBool());
        QVERIFY(!item<QObject>("actorBox")->property("enabled").toBool());
        QVERIFY(!item<QObject>("alignmentChoice")->property("enabled").toBool());
        QVERIFY(item<QObject>("endField")->property("enabled").toBool());
        typeInto("startField", QStringLiteral("00:00:01,500"));
        press(Qt::Key_Return);
        QCOMPARE(targetSession()->document().lines()[0]->start.value.microseconds(), 1'500'000);
    }
    void mpl2FieldsShowDeciseconds()
    {
        const QString mpl2 = dir.filePath(QStringLiteral("fields.txt"));
        {
            QFile f(mpl2);
            f.open(QIODevice::WriteOnly);
            f.write("[12][25]first\r\n");
        }
        QVERIFY(application->openFile(mpl2));
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_COMPARE(item<QObject>("startField")->property("text").toString(), QStringLiteral("12"));
        QCOMPARE(item<QObject>("durationField")->property("text").toString(), QStringLiteral("13"));
        typeInto("endField", QStringLiteral("31"));
        press(Qt::Key_Return);
        QCOMPARE(targetSession()->document().lines()[0]->end.value.microseconds(), 3'100'000);
    }

    // The Times/Frames switch: enabled once a video is open; with an exact
    // timebase the fields and the Grid show frames (TimeCtrl::SetTime,
    // SubsGridWindow.cpp:348-357), typed frames take the legacy midpoint
    // times (StartTimeFor/EndTimeFor, ZEROIT), and the switch is saved.
    void framesSwitchShowsVideoFrames_data()
    {
        QTest::addColumn<QString>("video");
        QTest::addColumn<QString>("start");
        QTest::addColumn<QString>("end");
        QTest::addColumn<QString>("duration");
        QTest::addColumn<QString>("typed");
        QTest::addColumn<qint64>("typedStartMs");
        // 24000/1001: frame 24 starts at 1001 ms, 36 at 1501.5 ms.
        QTest::newRow("cfr") << "cfr.mkv" << "24" << "35" << "12" << "30" << qint64(1230);
        // Frame durations cycle 30, 50, 70 ms: frame 3k starts at 150k ms, 3k+1 at
        // 150k + 30, 3k+2 at 150k + 80. The frame at or after 1000 ms is 21
        // (1050); 1500 ms is frame 30's start, so the last shown is 29. Typed
        // frame 3: min(80 + 70 / 2 + 5, 150) = 120 ms.
        QTest::newRow("vfr") << "vfr.mkv" << "21" << "29" << "9" << "3" << qint64(120);
    }
    void framesSwitchShowsVideoFrames()
    {
        QFETCH(QString, video);
        QVERIFY(application->openFile(writeStyled("frames.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:01.50,Default,,0,0,0,,first\n")));
        auto *session = targetSession();
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        QVERIFY(!application->editor().framesAvailable());
        application->settingsStore()->setValue(QStringLiteral("video.dontAskForBadResolution"), true);
        application->video().openVideo(nativeFixture(qPrintable(video)));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        QTRY_VERIFY(application->editor().framesAvailable());
        QVERIFY(item<QObject>("showFrames")->property("enabled").toBool());
        const auto click = [&](const char *name) {
            auto *radio = item(name);
            QTest::mouseClick(window, Qt::LeftButton, {}, radio->mapToScene(QPointF(10, radio->height() / 2)).toPoint());
        };
        click("showFrames");
        QTRY_VERIFY(application->editor().showFrames());
        QCOMPARE(application->settingsStore()->value(QStringLiteral("editor.timesToFramesSwitch")).toBool(), true);
        QFETCH(QString, start);
        QFETCH(QString, end);
        QFETCH(QString, duration);
        QCOMPARE(item<QObject>("startField")->property("text").toString(), start);
        QCOMPARE(item<QObject>("endField")->property("text").toString(), end);
        QCOMPARE(item<QObject>("durationField")->property("text").toString(), duration);
        auto *model = item("editingGrid")->property("model").value<QAbstractItemModel *>();
        QCOMPARE(model->index(0, ui::LineTableModel::StartColumn).data().toString(), start);
        QCOMPARE(model->index(0, ui::LineTableModel::EndColumn).data().toString(), end);
        QFETCH(QString, typed);
        QFETCH(qint64, typedStartMs);
        typeInto("startField", typed);
        press(Qt::Key_Return);
        QCOMPARE(session->document().lines()[0]->start.value.microseconds() / 1000, typedStartMs);
        // Back to times: the Grid and the fields show times again.
        click("showTimes");
        QTRY_VERIFY(!application->editor().showFrames());
        QCOMPARE(model->index(0, ui::LineTableModel::StartColumn).data().toString(),
                 QString::fromUtf8(reinterpret_cast<const char *>(core::legacy::assTimeText(typedStartMs).c_str())));
        QCOMPARE(application->settingsStore()->value(QStringLiteral("editor.timesToFramesSwitch")).toBool(), false);
    }

    // Live video editing (EditBox::OnEdit's OpenSubsLater): the video's
    // subtitles follow the draft while typing; with DISABLE_LIVE_VIDEO_EDITING
    // only a commit reaches them.
    void liveVideoEditingFollowsTheDraft()
    {
        QVERIFY(application->openFile(writeStyled("live.ass",
                                                  "Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,first\n"
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,second\n")));
        application->settingsStore()->setValue(QStringLiteral("video.dontAskForBadResolution"), true);
        application->video().openVideo(nativeFixture("cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(application->video().hasVideo(), 20000);
        item("editingGrid")->forceActiveFocus();
        press(Qt::Key_Home);
        QTRY_VERIFY(application->editor().hasLine());
        const auto script = [&] {
            const auto &bytes = application->videoScript();
            return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
        };
        auto *text = item("lineText");
        text->forceActiveFocus();
        press(Qt::Key_End);
        typeText(QStringLiteral(" live"));
        QTRY_VERIFY(script().contains("first live"));
        application->editor().discard();
        QTRY_VERIFY(!script().contains("first live"));
        // A time field follows as it is typed, before Enter (EditBox.cpp:343-346).
        typeInto("endField", QStringLiteral("0:00:00.50"));
        QTRY_VERIFY(script().contains("0:00:00.00,0:00:00.50,Default"));
        application->editor().discard();
        QTRY_VERIFY(!script().contains("0:00:00.00,0:00:00.50,Default"));
        application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), true);
        text->forceActiveFocus();
        press(Qt::Key_End);
        typeText(QStringLiteral(" later"));
        QCoreApplication::processEvents();
        QVERIFY(!script().contains("first later"));
        QVERIFY(application->editor().commit());
        QTRY_VERIFY(script().contains("first later"));
        application->settingsStore()->setValue(QStringLiteral("video.disableLiveEditing"), false);
    }

    // F1: a match in the Actor or Effect field selects it in that box
    // (findreplace.cpp:366-371).
    void findSelectsActorAndEffectMatches()
    {
        QVERIFY(application->openFile(writeStyled("find.ass",
                                                  "Dialogue: 0,0:00:01.00,0:00:02.00,Default,Narrator,0,0,0,,first\n"
                                                  "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,fade-in,second\n")));
        application->setFindQuestionHandler([](int, const QString &) { return 2; });
        application->runFindReplace(QStringLiteral("find"), {{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("rat")},
                                                             {QStringLiteral("field"), 2}});
        auto *actorText = item("actorBox")->property("contentItem").value<QQuickItem *>();
        QTRY_COMPARE(actorText->property("selectedText").toString(), QStringLiteral("rat"));
        application->runFindReplace(QStringLiteral("find"), {{QStringLiteral("tab"), 0}, {QStringLiteral("find"), QStringLiteral("in")},
                                                             {QStringLiteral("field"), 3}});
        auto *effectText = item("effectBox")->property("contentItem").value<QQuickItem *>();
        QTRY_COMPARE(effectText->property("selectedText").toString(), QStringLiteral("in"));
        QCOMPARE(effectText->property("selectionStart").toInt(), 5);
    }

    void theReferenceIsNeverEdited()
    {
        QVERIFY(application->openReference(original)); // the only Document is protected
        QVERIFY(!application->workspace().editingTarget());
        QVERIFY(!application->editor().editable());
        QVERIFY(item<QObject>("lineText")->property("readOnly").toBool());
    }

    // V3: the Video menu's recent lists (legacy SetRecent/AppendRecent,
    // HikariSubFrame.cpp:1510-1591): a video that loads is added (latest
    // first, kept in the profile), missing local files leave the list when
    // it is shown, a row opens its video; keyframes join their list whether
    // or not they loaded (SetRecent(3)); the dialogs start in legacy's
    // folders, and the Open audio dialog falls back to the latest recent
    // video's folder (A1 left).
    void videoRecentListsAndDialogFolders()
    {
        QTemporaryDir own;
        QVERIFY(own.isValid());
        const QString ini = own.filePath(QStringLiteral("hikari.ini"));
        const QString clip = own.filePath(QStringLiteral("clip.mkv"));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), clip));
        const QString gone = QDir::toNativeSeparators(own.filePath(QStringLiteral("gone.mkv")));
        ui::SettingsStore(ini).set("recent.video", QStringList{gone});
        restartWithSettings(ini);
        QVERIFY(application->openFile(episode));
        // the Open audio dialog without a video: the latest recent video's folder
        QCOMPARE(application->audioDialogFolder(), QUrl::fromLocalFile(own.path()));
        // legacy OnMenuOpened's default case (HikariSubFrame.cpp:2270-2272):
        // Open keyframes wants a video loaded
        auto *openKeys = named("openKeyframesMenuItem");
        QVERIFY(openKeys);
        QVERIFY(!openKeys->property("enabled").toBool());
        auto *menu = named("recentVideoMenu");
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QCOMPARE(menu->property("rows").toList().size(), 0); // pruned: "None"
        QCOMPARE(application->settingsStore()->list("recent.video"), QStringList());
        auto &video = application->video();
        video.openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QVERIFY(openKeys->property("enabled").toBool());
        const QString native = QDir::toNativeSeparators(clip);
        QCOMPARE(application->settingsStore()->list("recent.video"), QStringList{native});
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), nativeFixture("cfr.mkv"), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QCOMPARE(application->settingsStore()->list("recent.video"), (QStringList{nativeFixture("cfr.mkv"), native}));
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        const auto rows = menu->property("rows").toList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("1 cfr.mkv"));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("2 clip.mkv"));
        auto *second = named("recentVideo1");
        QVERIFY(second);
        QVERIFY(QMetaObject::invokeMethod(second, "triggered"));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), native, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QCOMPARE(application->settingsStore()->list("recent.video").first(), native);
        // the video and keyframes dialogs: the subtitles' and the video's folders
        QCOMPARE(application->videoDialogFolder(), QUrl::fromLocalFile(QFileInfo(episode).absolutePath()));
        QCOMPARE(application->keyframesDialogFolder(), QUrl::fromLocalFile(own.path()));
        // keyframes: the list takes the file, loaded or not
        const QString keys = own.filePath(QStringLiteral("keys.txt"));
        {
            QFile f(keys);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# keyframe format v1\nfps 0\n0\n12\n");
        }
        const QString bad = own.filePath(QStringLiteral("bad.txt"));
        {
            QFile f(bad);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("not keyframes\n");
        }
        QCOMPARE(application->openKeyframesFile(QUrl::fromLocalFile(keys).toString()), QString());
        QCOMPARE(video.session().keyframes(), (std::vector<int>{0, 12}));
        QCOMPARE(application->openKeyframesFile(bad), QStringLiteral("Invalid keyframes format"));
        QCOMPARE(application->settingsStore()->list("recent.keyframes"),
                 (QStringList{QDir::toNativeSeparators(bad), QDir::toNativeSeparators(keys)}));
        auto *keyMenu = named("recentKeyframesMenu");
        QVERIFY(keyMenu && keyMenu->property("enabled").toBool());
        QVERIFY(QFile::remove(bad));
        QVERIFY(QMetaObject::invokeMethod(keyMenu, "aboutToShow"));
        QCOMPARE(keyMenu->property("rows").toList().size(), 1);
        QVERIFY(QMetaObject::invokeMethod(named("recentKeyframes0"), "triggered"));
        QCOMPARE(video.session().keyframes(), (std::vector<int>{0, 12}));
        // without a video the keyframes dialog starts in the latest recent keyframes' folder
        QVERIFY(video.unloadVideo());
        QVERIFY(!keyMenu->property("enabled").toBool()); // legacy OnMenuOpened: a video loaded
        QVERIFY(!openKeys->property("enabled").toBool());
        // and OnMenuSelected checks it for the hotkey too (HikariSubFrame.cpp:681-687)
        QVERIFY(!named("openKeyframesMenuItem")->property("action").value<QObject *>()->property("enabled").toBool());
        QCOMPARE(application->keyframesDialogFolder(), QUrl::fromLocalFile(own.path()));
        QCOMPARE(application->audioDialogFolder(), QUrl::fromLocalFile(own.path()));
    }

    // V3: VideoBox::OpenKeyframes (VideoBox.cpp:1722-1742) with the audio
    // box and no video: the file's frames at 24000/1001 fps become the box's
    // keyframes and their snap times (AudioDisplay.cpp:2506-2509), a file
    // kept for a video to come is dropped (m_KeyframesFileName.Empty()), and
    // a file without keyframes gives "Invalid keyframes format" and leaves
    // the box's keyframes.
    void keyframesWithTheAudioBoxAndNoVideo()
    {
        restartWithoutSound();
        QTemporaryDir own;
        QVERIFY(own.isValid());
        auto write = [&](const char *name, const QByteArray &text) {
            const QString path = own.filePath(QString::fromLatin1(name));
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(text);
            return path;
        };
        const QString kept = write("kept.txt", "# keyframe format v1\nfps 0\n0\n7\n");
        const QString keys = write("keys.txt", "# keyframe format v1\nfps 0\n0\n24\n48\n");
        const QString bad = write("bad.txt", "not keyframes\n");
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        auto &video = application->video();
        // without video or audio the file waits for a video
        QCOMPARE(application->openKeyframesFile(kept), QString());
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        QVERIFY(!video.hasVideo());
        QVERIFY(audio.marks().keyframesMs.empty());
        QCOMPARE(application->openKeyframesFile(keys), QString());
        const auto ms = application::keyframesWithoutVideo({0, 24, 48});
        QCOMPARE(audio.marks().keyframesMs, ms);
        std::vector<int> snap;
        for (const int keyMs : ms)
            snap.push_back(application::keyframeSnapWithoutVideo(keyMs));
        QCOMPARE(audio.keyframeSnapTimes(), snap);
        QCOMPARE(application->openKeyframesFile(bad), QStringLiteral("Invalid keyframes format"));
        QCOMPARE(audio.marks().keyframesMs, ms);
        QCOMPARE(application->settingsStore()->list("recent.keyframes"),
                 (QStringList{QDir::toNativeSeparators(bad), QDir::toNativeSeparators(keys), QDir::toNativeSeparators(kept)}));
        // the kept file was dropped: a video opened now keeps its own keyframes
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QVERIFY(video.session().keyframes() != (std::vector<int>{0, 7}));
    }

    // V3: a failed video open is logged once with legacy ProviderFFMS2::Init's
    // message for its stage (ProviderFFMS2.cpp:164-388): "Indexing error
    // occurred: %s" with FFMS2's text, "Cannot create VideoSource.", "Cannot
    // convert video to RGBA". Where legacy only wrote a debug message (the
    // indexer could not be made) the panel's status is logged, as it is for
    // a refused dummy text (ProviderDummy logs nothing), never the failure
    // of the file before it. The helper fails at the stage its file names.
    void aFailedVideoOpenLogsLegacysMessageForItsStage()
    {
        restartWithMediaHelper(QStringLiteral(HIKARI_FAILING_MEDIA_HELPER));
        QTemporaryDir own;
        QVERIFY(own.isValid());
        QVERIFY(application->openFile(episode));
        auto &video = application->video();
        auto &log = application->log();
        auto fail = [&](const QString &path) {
            video.openVideo(path);
            const std::string opened = application::isDummyVideo(path.toStdString())
                                           ? path.toStdString()
                                           : QDir::toNativeSeparators(path).toStdString();
            QTRY_VERIFY_WITH_TIMEOUT(video.session().path() == opened &&
                                         video.session().state() == application::VideoSession::State::Failed,
                                     20000);
        };
        auto file = [&](const char *name) {
            const QString path = own.filePath(QString::fromLatin1(name));
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write("not a video");
            return path;
        };
        const struct {
            const char *name;
            QString message;
        } stages[] = {{"indexing.mkv", QStringLiteral("Indexing error occurred: fake indexing error")},
                      {"source.mkv", QStringLiteral("Cannot create VideoSource.")},
                      {"convert.mkv", QStringLiteral("Cannot convert video to RGBA")}};
        for (const auto &stage : stages) {
            const qsizetype before = log.history().count(stage.message);
            fail(file(stage.name));
            QCOMPARE(log.lastMessage(), stage.message);
            QVERIFY(log.shown());
            QCOMPARE(log.history().count(stage.message), before + 1); // once
            log.close();
        }
        fail(file("indexer.mkv"));
        QVERIFY2(log.lastMessage().startsWith(QStringLiteral("Video unavailable")), qPrintable(log.lastMessage()));
        QCOMPARE(log.lastMessage(), video.status());
        log.close();
        // a refused dummy text after a failed file: the status, not the file's message
        fail(file("source.mkv"));
        QCOMPARE(log.lastMessage(), QStringLiteral("Cannot create VideoSource."));
        log.close();
        fail(QStringLiteral("?dummy:25:0:8:4:1:2:3:"));
        QVERIFY2(log.lastMessage().startsWith(QStringLiteral("Video unavailable")), qPrintable(log.lastMessage()));
        QVERIFY(!video.session().openFailure());
        log.close();
    }

    // V3-unload-video: VIDEO_DELETE_FILE ("Unload video") empties the Video
    // panel as before any video; the file is never touched (legacy moved it
    // to the recycle bin), the audio box stays until GLOBAL_CLOSE_AUDIO, and
    // the tab no longer has the video.
    void unloadVideoLeavesTheFileAndTheAudioBox()
    {
        QTemporaryDir own;
        QVERIFY(own.isValid());
        const QString clip = own.filePath(QStringLiteral("clip.mkv"));
        QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audio.mkv"), clip));
        const QByteArray before = [&] {
            QFile f(clip);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        const QDateTime modified = QFileInfo(clip).lastModified();
        QVERIFY(application->openFile(episode));
        auto *item = named("unloadVideoMenuItem");
        QVERIFY(item);
        QVERIFY(!item->property("enabled").toBool()); // legacy: Enable(GetState() != None)
        QCOMPARE(QString::fromStdString(application::hotkeyName(application::hotkeyIdOf("VIDEO_DELETE_FILE"))),
                 QStringLiteral("Unload video"));
        auto &video = application->video();
        auto &audio = application->audio();
        video.openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready() && audio.box().fromVideo(), 20000);
        QTRY_VERIFY(application->tabs().first().toMap().value(QStringLiteral("tip")).toString().contains(QStringLiteral("clip.mkv")));
        QVERIFY(item->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(item, "triggered"));
        QCOMPARE(video.session().state(), application::VideoSession::State::Closed);
        QVERIFY(!video.hasVideo());
        QCOMPARE(video.status(), QStringLiteral("No video open"));
        QVERIFY(!visualItem("videoPresenter")->isVisible());
        QVERIFY(!item->property("enabled").toBool());
        QVERIFY(audio.hasAudio()); // the audio box stays
        QVERIFY(!application->tabs().first().toMap().value(QStringLiteral("tip")).toString().contains(QStringLiteral("clip.mkv")));
        QVERIFY(QFileInfo::exists(clip));
        QCOMPARE(QFileInfo(clip).lastModified(), modified);
        {
            QFile f(clip);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), before);
        }
        // the Video window's binding, once mapped, does the same
        video.openVideo(clip);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QVariant handled;
        QVERIFY(QMetaObject::invokeMethod(engine->rootObjects().first(), "runVideoHotkey", Q_RETURN_ARG(QVariant, handled),
                                          Q_ARG(QVariant, QStringLiteral("VIDEO_DELETE_FILE"))));
        QVERIFY(handled.toBool());
        QVERIFY(!video.hasVideo());
        QVERIFY(QFileInfo::exists(clip));
        // GLOBAL_CLOSE_AUDIO closes the box
        auto *closeAudio = named("closeAudioMenuItem")->property("action").value<QObject *>();
        QVERIFY(closeAudio);
        QVERIFY(QMetaObject::invokeMethod(closeAudio, "trigger"));
        QTRY_VERIFY(!audio.hasAudio());
    }

    // V3: GLOBAL_OPEN_DUMMY_VIDEO (legacy DummyVideo, ProviderDummy): the
    // dialog's defaults and frame count, a refused frame rate logged, the
    // dummy's frames, duration and colour, the audio box left as it was, the
    // recent list taking the dummy's text (and pruning it when shown, as
    // legacy's IsMissingLocalFile does).
    void dummyVideoFromItsDialog()
    {
        QVERIFY(application->openFile(episode));
        auto &audio = application->audio();
        audio.openAudio(QStringLiteral(HIKARI_MEDIA_FIXTURES "/audioonly.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(audio.ready(), 20000);
        auto *dialog = named("dummyVideoDialog");
        QVERIFY(dialog);
        auto *action = named("dummyVideoMenuItem")->property("action").value<QObject *>();
        QVERIFY(action);
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyFrames")->property("text").toString(),
                 QStringLiteral("This gives 35964 frames"));
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyFps")->property("editText").toString(), QStringLiteral("23.976"));
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyWidth")->property("value").toInt(), 1920);
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyHeight")->property("value").toInt(), 1080);
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyColour")->property("text").toString(), QStringLiteral("&HFEA32F&"));
        // V3-dummy-colour-text: the colour is ASS text with a swatch beside
        // it (legacy: a colour button); the swatch follows the text
        auto *swatch = dialogItem("dummyVideoDialog", "dummyColourSwatch");
        QVERIFY(swatch);
        QCOMPARE(swatch->property("color").value<QColor>(), QColor(47, 163, 254));
        dialogItem("dummyVideoDialog", "dummyColour")->setProperty("text", QStringLiteral("&H0000FF&"));
        QCOMPARE(swatch->property("color").value<QColor>(), QColor(255, 0, 0));
        dialogItem("dummyVideoDialog", "dummyColour")->setProperty("text", QStringLiteral("&HFEA32F&"));
        // a preset fills the size (OnResolutionChoose)
        auto *resolution = dialogItem("dummyVideoDialog", "dummyResolution");
        resolution->setProperty("currentIndex", 0);
        QVERIFY(QMetaObject::invokeMethod(resolution, "activated", Q_ARG(int, 0)));
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyWidth")->property("value").toInt(), 640);
        QCOMPARE(dialogItem("dummyVideoDialog", "dummyHeight")->property("value").toInt(), 480);
        // a refused rate: logged, nothing opens
        dialogItem("dummyVideoDialog", "dummyFps")->setProperty("editText", QStringLiteral("10"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QCOMPARE(application->log().lastMessage(), QStringLiteral("Invalid FPS value."));
        QVERIFY(!application->video().loaded());
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        dialogItem("dummyVideoDialog", "dummyFps")->setProperty("editText", QStringLiteral("25"));
        dialogItem("dummyVideoDialog", "dummyDuration")->setProperty("text", QStringLiteral("0:00:02.00"));
        dialogItem("dummyVideoDialog", "dummyWidth")->setProperty("value", 320);
        dialogItem("dummyVideoDialog", "dummyHeight")->setProperty("value", 240);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        auto &video = application->video();
        QTRY_VERIFY(video.hasVideo());
        QVERIFY(video.dummy());
        QCOMPARE(QString::fromStdString(video.session().path()), QStringLiteral("?dummy:25.000000:50:320:240:47:163:254:"));
        QCOMPARE(video.frameCount(), 50);
        QCOMPARE(video.session().legacyTimebase().msAt(49), 1960);
        QTRY_VERIFY(video.session().lastFrame());
        const auto frame = video.session().lastFrame();
        QCOMPARE(frame->width, 320);
        QCOMPARE(frame->height, 240);
        QCOMPARE(int(frame->bgra[0]), 254);
        QCOMPARE(int(frame->bgra[1]), 163);
        QCOMPARE(int(frame->bgra[2]), 47);
        QVERIFY(audio.ready()); // legacy loads the dummy without the audio (dontLoadAudio)
        QCOMPARE(audio.path(), nativeFixture("audioonly.mkv"));
        QVERIFY(!video.play()); // no file for the general player
        QCOMPARE(application->settingsStore()->list("recent.video").first(), QStringLiteral("?dummy:25.000000:50:320:240:47:163:254:"));
        QVERIFY(application->recentVideos().isEmpty()); // not a file: pruned when shown
    }

    // V3: VIDEO_PREVIOUS_FILE / VIDEO_NEXT_FILE (legacy VideoBox::NextFile,
    // OnPrew, OnNext): the transport's buttons ask first, then the folder's
    // next video in the file system's own listing order opens (other files
    // skipped); past the last one the video goes back to its start and play
    // toggles.
    void nextFileWalksTheVideosFolder()
    {
        restartWithoutSound();
        QTemporaryDir own;
        QVERIFY(own.isValid());
        for (const char *name : {"a.mkv", "b.MKV", "c.mkv"})
            QVERIFY(QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), own.filePath(QLatin1String(name))));
        {
            QFile f(own.filePath(QStringLiteral("notes.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        // the listing as legacy's wxDir::GetAllFiles reads it (unsorted)
        QStringList videos;
        for (const QString &name : QDir(own.path()).entryList(QDir::Files, QDir::Unsorted))
            if (!name.endsWith(QLatin1String(".txt")))
                videos << QDir::toNativeSeparators(own.filePath(name));
        QCOMPARE(videos.size(), 3);
        QVERIFY(application->openFile(episode));
        auto &video = application->video();
        video.openVideo(videos[0]);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        auto *question = named("videoFileQuestion");
        QVERIFY(question);
        // No: nothing happens
        QVERIFY(QMetaObject::invokeMethod(visualItem("nextFile"), "click"));
        QTRY_VERIFY(question->property("opened").toBool());
        QCOMPARE(dialogItem("videoFileQuestion", "videoFileQuestionText")->property("text").toString(),
                 QStringLiteral("Are you sure you want to index the next video?"));
        QVERIFY(QMetaObject::invokeMethod(question, "reject"));
        QTRY_VERIFY(!question->property("opened").toBool());
        QCOMPARE(QString::fromStdString(video.session().path()), videos[0]);
        // Yes: the listing's next video
        QVERIFY(QMetaObject::invokeMethod(visualItem("nextFile"), "click"));
        QTRY_VERIFY(question->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(question, "accept"));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), videos[1], 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QVERIFY(application->nextVideoFile(true));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), videos[2], 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        // past the last: Seek(0) and Pause(false) plays from the start
        video.showFrameAt(10);
        QTRY_COMPARE(video.session().shownFrame(), std::optional<int>(10));
        QVERIFY(application->nextVideoFile(true));
        QCOMPARE(QString::fromStdString(video.session().path()), videos[2]);
        QCOMPARE(video.session().requestedFrame(), std::optional<int>(0));
        QVERIFY(video.playing());
        QVERIFY(application->nextVideoFile(true)); // again: pauses at the start
        QVERIFY(!video.playing());
        // the previous button asks about the previous video
        QVERIFY(QMetaObject::invokeMethod(visualItem("previousFile"), "click"));
        QTRY_VERIFY(question->property("opened").toBool());
        QCOMPARE(dialogItem("videoFileQuestion", "videoFileQuestionText")->property("text").toString(),
                 QStringLiteral("Are you sure you want to index the previous video?"));
        QVERIFY(QMetaObject::invokeMethod(question, "accept"));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), videos[1], 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
    }

    // V3-next-file-no-recent: with no video and an empty recent list the
    // previous/next file does nothing (legacy VideoBox::NextFile read
    // videorec[videorec.size() - 1] of the empty list, VideoBox.cpp:691).
    void nextFileWithNoVideoAndNoRecentDoesNothing()
    {
        QTemporaryDir own;
        QVERIFY(own.isValid());
        restartWithSettings(own.filePath(QStringLiteral("hikari.ini")));
        QVERIFY(application->openFile(episode));
        auto &video = application->video();
        QVERIFY(application->settingsStore()->list("recent.video").isEmpty());
        QVERIFY(!video.loaded());
        QVERIFY(!application->nextVideoFile(true));
        QVERIFY(!application->nextVideoFile(false));
        // the transport's buttons ask, and Yes changes nothing
        auto *question = named("videoFileQuestion");
        QVERIFY(question);
        for (const char *button : {"nextFile", "previousFile"}) {
            QVERIFY(QMetaObject::invokeMethod(visualItem(button), "click"));
            QTRY_VERIFY(question->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(question, "accept"));
            QTRY_VERIFY(!question->property("opened").toBool());
        }
        QTest::qWait(100);
        QVERIFY(!video.loaded());
        QVERIFY(!video.indexing());
        QVERIFY(video.session().path().empty());
        QVERIFY(application->settingsStore()->list("recent.video").isEmpty());
    }

    // V3: chapters from the media helper at legacy positions (whole ms of
    // their starts), the chapter menu and its mark, VIDEO_NEXT_CHAPTER /
    // VIDEO_PREVIOUS_CHAPTER (M / N in the Video window) with prevchap; the
    // stream menu's audio tracks and a choice reaching general playback.
    void chaptersAndStreamsOfTheVideo()
    {
        restartWithoutSound();
        application->settingsStore()->set("video.acceptedAudioStream", QStringLiteral("eng")); // no track question
        QVERIFY(application->openFile(episode));
        auto &video = application->video();
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/tracks.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(video.chapterCount(), 2, 20000);
        auto rows = video.chapters();
        QCOMPARE(rows[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Opening"));
        QCOMPARE(rows[0].toMap().value(QStringLiteral("time")).toString(), QStringLiteral("[0:00:00.00]"));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Second"));
        QCOMPARE(rows[1].toMap().value(QStringLiteral("time")).toString(), QStringLiteral("[0:00:01.00]"));
        video.showFrameAt(0);
        QTRY_COMPARE(video.session().shownFrame(), std::optional<int>(0));
        QVERIFY(video.chapters()[0].toMap().value(QStringLiteral("checked")).toBool());
        QVERIFY(!video.chapters()[1].toMap().value(QStringLiteral("checked")).toBool());
        auto *menu = named("videoChaptersMenu");
        QVERIFY(menu && menu->property("enabled").toBool());
        // V3-video-menu-entries: Unload video, the streams and the chapters
        // sit in the Video menu until V4's context menu hosts them
        {
            auto *videoMenu = named("videoMenu");
            QVERIFY(videoMenu);
            QSet<QObject *> hosted;
            const int count = videoMenu->property("count").toInt();
            for (int i = 0; i < count; ++i) {
                QQuickItem *entry = nullptr;
                QVERIFY(QMetaObject::invokeMethod(videoMenu, "itemAt", Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, i)));
                if (!entry)
                    continue;
                hosted.insert(entry);
                if (auto *sub = entry->property("subMenu").value<QObject *>())
                    hosted.insert(sub);
            }
            QVERIFY(hosted.contains(named("unloadVideoMenuItem")));
            QVERIFY(hosted.contains(named("videoStreamsMenu")));
            QVERIFY(hosted.contains(menu));
        }
        QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
        QCOMPARE(menu->property("rows").toList().size(), 2);
        // the chapter at 1000 ms: the frame at or after it, 24 at 1001 ms
        QVERIFY(QMetaObject::invokeMethod(named("videoChapter1"), "triggered"));
        QTRY_COMPARE(video.session().shownFrame(), std::optional<int>(24));
        QVERIFY(video.chapters()[1].toMap().value(QStringLiteral("checked")).toBool());
        // M / N in the Video window
        keysNeverRepeat();
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_M); // next: from the last, the first
        QTRY_COMPARE(video.session().shownFrame(), std::optional<int>(0));
        press(Qt::Key_N); // previous at the first, jumped to last: wraps to the last
        QTRY_COMPARE(video.session().shownFrame(), std::optional<int>(24));
        // the stream menu: both audio tracks, the accepted one playing
        QTRY_COMPARE_WITH_TIMEOUT(video.streamCount(), 2, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(video.streams()[1].toMap().value(QStringLiteral("label")).toString(),
                                  QStringLiteral("A: Commentary [jpn] (pcm_s16le)"), 20000);
        QCOMPARE(video.streams()[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("A: Main [eng] (pcm_s16le)"));
        QVERIFY(video.streams()[0].toMap().value(QStringLiteral("checked")).toBool());
        auto *streams = named("videoStreamsMenu");
        QVERIFY(streams && streams->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(streams, "aboutToShow"));
        QVERIFY(QMetaObject::invokeMethod(named("videoStream1"), "triggered"));
        QVERIFY(video.streams()[1].toMap().value(QStringLiteral("checked")).toBool());
        QVERIFY(video.play());
        QTRY_COMPARE_WITH_TIMEOUT(application->generalPlayer().description().activeAudio, 1, 20000);
        // switched while playing, at once
        QVERIFY(video.selectStream(0));
        QTRY_COMPARE_WITH_TIMEOUT(application->generalPlayer().description().activeAudio, 0, 20000);
        QVERIFY(video.pause());
        // a new video: no chapters until its own arrive, prevchap again
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(video.session().path()), nativeFixture("cfr.mkv"), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        QCOMPARE(video.chapterCount(), 0);
        QVERIFY(!named("videoChaptersMenu")->property("enabled").toBool());
        QVERIFY(!video.nextChapter());
    }

    // V3: indexing shows its progress with Cancel in the Video panel (legacy
    // ProgressSink "Indexing video", here inline); Cancel leaves no video and
    // logs nothing. GLOBAL_VIDEO_INDEXING is in no menu and its stored binding
    // is dropped with a notice.
    void indexingCancelsAndTheFfms2ToggleIsGone()
    {
        QVERIFY(application->openFile(episode));
        auto &video = application->video();
        auto *progress = visualItem("videoIndexing");
        QVERIFY(progress);
        QVERIFY(!progress->isVisible());
        // straight to the session, so the click comes before the helper can answer
        video.session().open(nativeFixture("longgop.mkv").toStdString());
        QVERIFY(video.indexing());
        QVERIFY(progress->isVisible());
        QCOMPARE(findItem(progress, QStringLiteral("videoIndexingProgress"))->property("indeterminate").toBool(), true);
        // V3-indexing-inline: a strip inside the Video panel, not a modal
        // window (legacy ProgressSink): no modal popup or window opens
        {
            bool inPanel = false;
            for (QQuickItem *up = progress->parentItem(); up; up = up->parentItem())
                inPanel = inPanel || up == item("videoPanel");
            QVERIFY(inPanel);
            for (QObject *o : engine->rootObjects().first()->findChildren<QObject *>())
                if (o->inherits("QQuickPopup"))
                    QVERIFY2(!(o->property("modal").toBool() && o->property("opened").toBool()), qPrintable(o->objectName()));
            QCOMPARE(QGuiApplication::modalWindow(), nullptr);
            QVERIFY(video.indexing());
        }
        const QString logged = application->log().lastMessage();
        QVERIFY(QMetaObject::invokeMethod(findItem(progress, QStringLiteral("cancelIndexing")), "click"));
        QCOMPARE(video.session().state(), application::VideoSession::State::Closed);
        QVERIFY(!video.indexing());
        QVERIFY(!progress->isVisible());
        QTest::qWait(300);
        QCOMPARE(video.session().state(), application::VideoSession::State::Closed);
        QCOMPARE(application->log().lastMessage(), logged);
        // the panel works again afterwards
        video.openVideo(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"));
        QTRY_VERIFY_WITH_TIMEOUT(video.hasVideo(), 20000);
        // nothing names the FFMS2 toggle (the Video settings page's "FFMS2
        // video seeking method" is another option and stays)
        auto *root = engine->rootObjects().first();
        for (QObject *o : root->findChildren<QObject *>())
            if (o->metaObject()->indexOfProperty("text") >= 0)
                QVERIFY2(!o->property("text").toString().contains(QStringLiteral("Open video with FFMS2")),
                         qPrintable(o->objectName()));
        // a stored binding of it: dropped with a notice, and not written again
        QTemporaryDir own;
        QVERIFY(own.isValid());
        const QString ini = own.filePath(QStringLiteral("hikari.ini"));
        ui::SettingsStore(ini).set("shortcuts.hotkeys", QStringList{QStringLiteral("GLOBAL_VIDEO_INDEXING G=Ctrl-Shift-I"),
                                                                    QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-S")});
        restartWithSettings(ini);
        QVERIFY(application->log().history().contains(QStringLiteral("Ctrl-Shift-I")));
        QVERIFY(application->log().history().contains(QStringLiteral("Open video with FFMS2")));
        QCOMPARE(application->settingsStore()->list("shortcuts.hotkeys"), QStringList{QStringLiteral("GLOBAL_SAVE_SUBS G=Ctrl-S")});
    }

    // V4 (#183): the Video panel's zoom, aspect ratio, volume, context menu
    // and snapshots, against legacy VideoBox / RendererVideo at 20d647c4.
    // The video is a copy of the cfr fixture in a folder of its own (the
    // snapshots are written beside it); the Document matches its 320x240
    // and draws a shape from 0 s to 5 s, so the overlay needs no font.
    QString v4Folder(QTemporaryDir &folder)
    {
        const QString video = folder.filePath(QStringLiteral("clip.mkv"));
        if (!QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/cfr.mkv"), video))
            return {};
        QFile f(folder.filePath(QStringLiteral("clip.ass")));
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                "MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,20,&H4030A0E0,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n"
                "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\pos(10,10)\\p1}m 0 0 l 120 0 100 80 0 90{\\p0}\n");
        return f.fileName();
    }
    bool v4Open(QTemporaryDir &folder)
    {
        const QString subtitles = v4Folder(folder);
        if (subtitles.isEmpty() || !application->openFile(subtitles))
            return false;
        application->video().openVideo(folder.filePath(QStringLiteral("clip.mkv")));
        return QTest::qWaitFor([&] {
            const auto &s = application->video().session();
            return item("videoPresenter")->property("presentedGeneration").toULongLong() > 0 && s.lastFrame()
                   && s.lastOverlay() && !s.lastOverlay()->empty;
        }, 20000);
    }
    void wheelAt(QPoint at, int notches, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QWheelEvent wheel(at, window->mapToGlobal(at), QPoint(), QPoint(0, 120 * notches), Qt::NoButton, mods,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QCoreApplication::processEvents();
    }
    // The CPU-BGRA/libass reference (tests/ui/presenter_tests.cpp): the
    // frame, then the premultiplied overlay source-over, as RGB.
    static QImage cpuReference(const application::IndexedFrame &f, const application::OverlayFrame *o)
    {
        QImage out(f.width, f.height, QImage::Format_RGB888);
        for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x) {
                const std::byte *p = f.bgra.data() + static_cast<std::size_t>(y) * f.stride + x * 4;
                int b = std::to_integer<int>(p[0]), g = std::to_integer<int>(p[1]), r = std::to_integer<int>(p[2]);
                if (o && !o->empty) {
                    const std::uint8_t *q = &o->pixels[static_cast<std::size_t>(y) * o->stride + x * 4];
                    const int inv = 255 - q[3];
                    b = q[0] + (b * inv + 127) / 255;
                    g = q[1] + (g * inv + 127) / 255;
                    r = q[2] + (r * inv + 127) / 255;
                }
                out.setPixel(x, y, qRgb(r, g, b));
            }
        return out;
    }
    static bool sameImage(const QImage &a, const QImage &b)
    {
        return a.size() == b.size()
               && a.convertToFormat(QImage::Format_RGB888) == b.convertToFormat(QImage::Format_RGB888);
    }
    void triggerAction(const char *menuItem)
    {
        auto *action = named(menuItem)->property("action").value<QObject *>();
        QVERIFY(action);
        QVERIFY(QMetaObject::invokeMethod(action, "trigger"));
        QCoreApplication::processEvents();
    }

private slots:

    // GLOBAL_VIDEO_ZOOM / GLOBAL_RESET_VIDEO_ZOOM on the Video menu, Return
    // and Ctrl+Shift+Z in the Video panel, the wheel over the video, the
    // zoom mode's frame; VIDEO_ASPECT_RATIO's dialog; the volume keys,
    // slider and wheel.
    void videoViewZoomAspectAndVolume()
    {
        restartWithoutSound();
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(v4Open(folder));
        auto &view = application->videoView();
        auto &tools = application->visualTools();
        auto *presenter = item("videoPresenter");
        // The Video menu's items (HikariSubFrame.cpp:283-284), K1's zoom icon.
        auto *zoomItem = qobject_cast<QQuickItem *>(named("videoZoomMenuItem"));
        QVERIFY(zoomItem);
        QCOMPARE(zoomItem->property("iconRole").toString(), QStringLiteral("zoom"));
        QCOMPARE(zoomItem->property("text").toString(), QStringLiteral("Zoom video"));
        QVERIFY(zoomItem->isEnabled());
        QVERIFY(!named("resetVideoZoomMenuItem")->property("enabled").toBool()); // no zoom yet
        // GLOBAL_VIDEO_ZOOM: the zoom mode at VIDEO_ZOOM_PERCENT (unset: 2x)
        // around the centre; the presenter shows that part of the frame and
        // the frame's outline is drawn.
        triggerAction("videoZoomMenuItem");
        QVERIFY(view.zoomMode());
        QCOMPARE(view.zoomPercent(), 200);
        // Half the frame around its centre, in legacy's integer source
        // rectangle (sourceFromZoomRect rounds the window's float zoom
        // rectangle, so the panel's size can move it a pixel; the exact
        // arithmetic is video_view_tests' against the legacy captures).
        const QRectF zoomedSource = tools.sourceRect();
        QVERIFY2(std::abs(zoomedSource.width() - 160) <= 1 && std::abs(zoomedSource.height() - 120) <= 1,
                 qPrintable(QDebug::toString(zoomedSource)));
        QVERIFY(std::abs(zoomedSource.center().x() - 160) <= 1 && std::abs(zoomedSource.center().y() - 120) <= 1);
        QCOMPARE(presenter->property("sourceRect").toRectF(), zoomedSource);
        QTRY_VERIFY(item("videoZoomFrame")->isVisible());
        const QRectF video = tools.videoRect();
        const QRectF frame = view.zoomFrame();
        QCOMPARE(frame.center().x(), video.center().x() - 0.5); // to width - 1
        // The tools are not drawn meanwhile, and the pointer is shown.
        QTest::mouseMove(window, videoPoint(video.center()));
        QTRY_VERIFY(tools.overlay().isEmpty());
        QVERIFY(!tools.hideCursor());
        // The zoom mode's drag pans (ZoomMouseHandle): left 20 pixels.
        const QPoint c = videoPoint(video.center());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, c);
        QTest::mouseMove(window, c - QPoint(20, 0));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, c - QPoint(20, 0));
        QTRY_VERIFY(tools.sourceRect().x() < 80);
        // Return in the zoom mode leaves it, keeping the zoom.
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_Return);
        QVERIFY(!view.zoomMode());
        QVERIFY(view.zoomed());
        QTRY_VERIFY(!item("videoZoomFrame")->isVisible());
        QVERIFY(named("resetVideoZoomMenuItem")->property("enabled").toBool());
        // GLOBAL_RESET_VIDEO_ZOOM: the whole frame.
        triggerAction("resetVideoZoomMenuItem");
        QVERIFY(!view.zoomed());
        // The whole frame, every row and column at any panel size (approved
        // departure V4-reset-zoom-rows; legacy's float division could drop
        // the last, RendererVideo.cpp:771-778).
        QCOMPARE(tools.sourceRect(), QRectF(0, 0, 320, 240));
        // The wheel over the video with the crosshair: a tenth a step, at the pointer.
        wheelAt(c, 5);
        QCOMPARE(view.zoomPercent(), 150);
        QVERIFY(!view.zoomMode());
        wheelAt(c, -10); // never below 1
        QCOMPARE(view.zoomPercent(), 100);
        wheelAt(c, 3);
        // Legacy truncates the zoom rectangle's float ratio (VideoBox.cpp:
        // 1301-1315), so 1.3 reads 129 or 130 with the panel's width (the
        // icon-only rail left the video a width where it reads 129).
        const int zoomed = view.zoomPercent();
        QVERIFY2(zoomed == 130 || zoomed == 129, qPrintable(QString::number(zoomed)));
        // Ctrl+wheel resized legacy's video window (VideoBox.cpp:500-511,
        // TabPanel::SetVideoWindowSizes); the docked panel's size is the
        // layout's, so it does nothing: no zoom, no size, no volume
        // (approved departure V4-ctrl-wheel).
        {
            const QSizeF panelSize = item("videoPanel")->size();
            const QRectF videoArea = tools.videoRect();
            const int volumeBefore = view.volume();
            wheelAt(c, 3, Qt::ControlModifier);
            wheelAt(c, -3, Qt::ControlModifier);
            QCOMPARE(view.zoomPercent(), zoomed);
            QCOMPARE(item("videoPanel")->size(), panelSize);
            QCOMPARE(tools.videoRect(), videoArea);
            QCOMPARE(view.volume(), volumeBefore);
        }
        // Ctrl+Shift+Z in the Video panel resets it.
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(view.zoomPercent(), 100);

        // VIDEO_ASPECT_RATIO (no default key): the dialog at the pointer,
        // "Aspect ratio: 1.333" for 320x240; a slider at 350000 is 0.5.
        auto *root = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root, "runVideoHotkey", Q_ARG(QVariant, QStringLiteral("VIDEO_ASPECT_RATIO"))));
        auto *dialog = named("aspectRatioDialog");
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *label = named("aspectRatioLabel");
        QCOMPARE(label->property("text").toString(), QStringLiteral("Aspect ratio: 1.333"));
        auto *slider = named("aspectRatioSlider");
        QCOMPARE(slider->property("value").toInt(), 525000); // 0.75 * 700000
        slider->setProperty("value", 350000);
        QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
        QCOMPARE(label->property("text").toString(), QStringLiteral("Aspect ratio: 2.000"));
        QCOMPARE(tools.videoView().aspectRatio(), 0.5f);
        const auto r = tools.videoView().videoRect();
        // Letterboxed at 2:1, to the pixel: an odd video width (the visual
        // tool rail widens by its scroll bar in a short panel) has no exact
        // half.
        QVERIFY2(std::abs(r.height() * 2 - r.width()) <= 1, qPrintable(QStringLiteral("%1x%2").arg(r.width()).arg(r.height())));
        QCOMPARE(presenter->property("videoRect").toRectF(), tools.videoRect());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(tools.videoView().aspectRatio(), 0.5f); // until the next video opens

        // The volume (VIDEO_VOLUME): Num . / Num 0 step 2 below 1 and above -91.
        auto &player = application->generalPlayer();
        auto *volume = item("videoVolume");
        QVERIFY(volume && volume->isEnabled());
        QCOMPARE(view.volume(), 0);
        QCOMPARE(player.volume(), 1.0);
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_Period, Qt::KeypadModifier);
        QCOMPARE(view.volume(), 0); // 2 is not below 1
        press(Qt::Key_0, Qt::KeypadModifier);
        QTest::qWait(60); // the same action within 50 ms is dropped (VideoBox.cpp:1137)
        press(Qt::Key_0, Qt::KeypadModifier);
        QCOMPARE(view.volume(), -4);
        QCOMPARE(volume->property("value").toInt(), -4);
        QCOMPARE(application->settingsStore()->integer("video.volume"), -4);
        QCOMPARE(player.volume(), std::pow(10.0, -16.0 / 100.0 / 20.0));
        // The wheel over the panel: three a step.
        auto *times = item("videoTimes");
        const QPoint onPanel = times->mapToScene(QPointF(times->width() / 2, times->height() / 2)).toPoint();
        wheelAt(onPanel, -2);
        QCOMPARE(view.volume(), -10);
        // Ctrl+wheel returned first outside fullscreen: no volume (VideoBox.cpp:508-518).
        wheelAt(onPanel, -2, Qt::ControlModifier);
        QCOMPARE(view.volume(), -10);
        // The slider.
        volume->setProperty("value", -40);
        QVERIFY(QMetaObject::invokeMethod(volume, "moved"));
        QCOMPARE(view.volume(), -40);
        QCOMPARE(player.volume(), std::pow(10.0, -1600.0 / 100.0 / 20.0));
        // VIDEO_HIDE_PROGRESS_BAR switches VIDEO_PROGRESS_BAR (drawn in fullscreen, V5).
        QVERIFY(application->settingsStore()->boolean("video.progressBar"));
        QVERIFY(QMetaObject::invokeMethod(root, "runVideoHotkey", Q_ARG(QVariant, QStringLiteral("VIDEO_HIDE_PROGRESS_BAR"))));
        QVERIFY(!application->settingsStore()->boolean("video.progressBar"));
    }

    // The video's context menu (VideoBox::ContextMenu) on a right click and
    // the menu key; its snapshots against the CPU-BGRA/libass reference at
    // the paused frame, the PNGs beside the video and the clipboard read
    // back; Shift+click maps a Video window hotkey (O2 left this gesture);
    // VIDEO_PAUSE_ON_CLICK.
    void videoContextMenuAndSnapshots()
    {
        restartWithoutSound();
        // A folder named with "_7_": legacy numbered every snapshot by the
        // first "_<digits>_" of the path, so each save was number 1 and
        // overwrote the last (V4-snapshot-number).
        QTemporaryDir folder(QDir::tempPath() + QStringLiteral("/hikari_7_XXXXXX"));
        QVERIFY(folder.isValid());
        QVERIFY(v4Open(folder));
        // The recent lists (VideoBox.cpp:952-965): the first twenty of each,
        // by file name; a missing file skips its own row only (approved
        // departure V4-recent-rows; legacy's `continue` skipped the row of
        // both lists, so b.mkv went with missing.ass). recent.video is read
        // here; its writer is GLOBAL_RECENT_VIDEO's (V3 #182).
        for (const char *name : {"b.mkv", "c.mkv"}) {
            QFile f(folder.filePath(QLatin1String(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        application->settingsStore()->set(
            "recent.subtitles", QStringList{folder.filePath(QStringLiteral("clip.ass")), folder.filePath(QStringLiteral("missing.ass"))});
        application->settingsStore()->set("recent.video", QStringList{folder.filePath(QStringLiteral("clip.mkv")),
                                                                      folder.filePath(QStringLiteral("b.mkv")),
                                                                      folder.filePath(QStringLiteral("c.mkv"))});
        auto &tools = application->visualTools();
        const QPoint c = videoPoint(tools.videoRect().center());
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, c);
        auto *menu = named("videoContextMenu");
        QTRY_VERIFY(menu->property("opened").toBool());
        auto enabled = [&](const char *name) { return named(name)->property("enabled").toBool(); };
        auto text = [&](const char *name) { return named(name)->property("text").toString(); };
        QTRY_VERIFY(named("videoMenuRecentVideos1"));
        QCOMPARE(text("videoMenuRecentSubtitles0"), QStringLiteral("clip.ass"));
        QVERIFY(!named("videoMenuRecentSubtitles1"));
        QCOMPARE(text("videoMenuRecentVideos0"), QStringLiteral("clip.mkv"));
        QCOMPARE(text("videoMenuRecentVideos1"), QStringLiteral("b.mkv"));
        QCOMPARE(text("videoMenuRecentVideos2"), QStringLiteral("c.mkv"));
        QVERIFY(!named("videoMenuRecentVideos3"));
        // Open video and Open subtitles carry their Global bindings.
        auto label = [&](const QString &name, const char *symbol) {
            const QString key = application->hotkeys().accelOf(QLatin1String(symbol), 0);
            return key.isEmpty() ? name : name + QLatin1Char('\t') + key;
        };
        QVERIFY(!application->hotkeys().accelOf(QStringLiteral("GLOBAL_OPEN_SUBS"), 0).isEmpty());
        QCOMPARE(text("videoMenuOpenVideo"), label(QStringLiteral("Open video"), "GLOBAL_OPEN_VIDEO"));
        QCOMPARE(text("videoMenuOpenSubtitles"), label(QStringLiteral("Open subtitles"), "GLOBAL_OPEN_SUBS"));
        QCOMPARE(named("videoMenuPlayPause")->property("text").toString(), QStringLiteral("Play\tSpace"));
        QVERIFY(enabled("videoMenuPlayPause"));
        QVERIFY(!enabled("videoMenuStop")); // not playing
        QVERIFY(!enabled("videoMenuProgressBar")); // fullscreen only
        QVERIFY(enabled("videoMenuAspectRatio"));
        for (const char *name : {"videoMenuSaveSubbedFrame", "videoMenuCopySubbedFrame", "videoMenuSaveFrame", "videoMenuCopyFrame"})
            QVERIFY2(enabled(name), name);

        // The snapshots: the paused frame as RGB; with subtitles, the
        // overlay composited as the CPU reference does.
        const auto &session = application->video().session();
        const auto shownFrame = session.lastFrame();
        const auto overlay = session.lastOverlay();
        QCOMPARE(shownFrame->index, 0);
        const QImage plain = cpuReference(*shownFrame, nullptr);
        const QImage subbed = cpuReference(*shownFrame, overlay.get());
        QVERIFY(plain != subbed);
        QGuiApplication::clipboard()->clear();
        QVERIFY(QMetaObject::invokeMethod(named("videoMenuCopySubbedFrame"), "triggered"));
        QTRY_VERIFY(!QGuiApplication::clipboard()->image().isNull());
        QVERIFY(sameImage(QGuiApplication::clipboard()->image(), subbed));
        QVERIFY(QMetaObject::invokeMethod(named("videoMenuCopyFrame"), "triggered"));
        QTRY_VERIFY(sameImage(QGuiApplication::clipboard()->image(), plain));
        // Saved beside the video as <name>_<n>_<time>.png, numbered from 1.
        QVERIFY(QMetaObject::invokeMethod(named("videoMenuSaveSubbedFrame"), "triggered"));
        const QString first = folder.filePath(QStringLiteral("clip_1_00;00;00,000.png"));
        QCOMPARE(QDir::fromNativeSeparators(application->videoView().lastSnapshot()), first);
        QVERIFY(sameImage(QImage(first), subbed));
        auto *root = engine->rootObjects().first();
        QVERIFY(QMetaObject::invokeMethod(root, "runVideoHotkey", Q_ARG(QVariant, QStringLiteral("VIDEO_SAVE_FRAME_TO_PNG"))));
        const QString second = folder.filePath(QStringLiteral("clip_2_00;00;00,000.png"));
        QCOMPARE(QDir::fromNativeSeparators(application->videoView().lastSnapshot()), second);
        QVERIFY(sameImage(QImage(second), plain));
        // A gap is filled first (the lowest number not taken).
        QVERIFY(QFile::remove(first));
        QTest::qWait(60); // VideoBox::OnAccelerator drops the same action within 50 ms (VideoBox.cpp:1137)
        QVERIFY(QMetaObject::invokeMethod(root, "runVideoHotkey", Q_ARG(QVariant, QStringLiteral("VIDEO_SAVE_FRAME_TO_PNG"))));
        QCOMPARE(QDir::fromNativeSeparators(application->videoView().lastSnapshot()), first);
        QVERIFY(QFile::exists(second));
        // A later frame is named by its time (frame 24: 1001 ms).
        application->video().showFrameAt(24);
        QTRY_COMPARE(session.lastFrame() ? session.lastFrame()->index : -1, 24);
        QVERIFY(QMetaObject::invokeMethod(root, "runVideoHotkey", Q_ARG(QVariant, QStringLiteral("VIDEO_COPY_SUBBED_FRAME_TO_CLIPBOARD"))));
        QTRY_VERIFY(sameImage(QGuiApplication::clipboard()->image(),
                              cpuReference(*session.lastFrame(), session.lastOverlay().get())));
        QVERIFY(QMetaObject::invokeMethod(root, "runVideoHotkey", Q_ARG(QVariant, QStringLiteral("VIDEO_SAVE_SUBBED_FRAME_TO_PNG"))));
        QCOMPARE(QDir::fromNativeSeparators(application->videoView().lastSnapshot()),
                 folder.filePath(QStringLiteral("clip_3_00;00;01,001.png")));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());

        // Shift+click maps the item's Video window hotkey, with the window
        // choice, instead of running it; the shortcut editor lists it.
        auto &h = application->hotkeys();
        auto *mapping = mappingWindow("hotkeyMapping");
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, c);
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *aspectItem = qobject_cast<QQuickItem *>(named("videoMenuAspectRatio"));
        QTRY_VERIFY(aspectItem->isVisible() && aspectItem->width() > 0);
        clickWith(aspectItem, Qt::ShiftModifier);
        QTRY_VERIFY(mapping->isVisible());
        QCOMPARE(in(mapping, "hotkeyMappingText")->property("text").toString(),
                 QStringLiteral("Please enter a hotkey for \"Change aspect ratio\"."));
        QVERIFY(in(mapping, "hotkeyWindowChoice")->isVisible());
        QCOMPARE(in(mapping, "hotkeyWindowChoice")->property("currentIndex").toInt(), 3);
        QVERIFY(!named("aspectRatioDialog")->property("visible").toBool());
        keyTo(in(mapping, "hotkeyMappingKeys"), Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!mapping->isVisible());
        QCOMPARE(h.accelOf(QStringLiteral("VIDEO_ASPECT_RATIO"), 3), QStringLiteral("Alt-Ctrl-J"));
        QVERIFY(application->settingsStore()->list("shortcuts.hotkeys").contains(QStringLiteral("VIDEO_ASPECT_RATIO V=Alt-Ctrl-J")));
        h.beginOptions();
        QCOMPARE(hotkeyRow(QStringLiteral("Video Change aspect ratio")).value(QStringLiteral("accel")).toString(),
                 QStringLiteral("Alt-Ctrl-J"));
        // The new binding opens the dialog from the Video panel.
        QTRY_VERIFY(window->isActive());
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(named("aspectRatioDialog")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(named("aspectRatioDialog"), "close"));
        QTRY_VERIFY(!named("aspectRatioDialog")->property("visible").toBool());
        // The menu key opens the menu at the pointer.
        item("videoPanel")->forceActiveFocus();
        press(Qt::Key_Menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());

        // VIDEO_PAUSE_ON_CLICK: a left click plays or pauses (not with Ctrl).
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, c);
        QVERIFY(!application->video().playing());
        application->settingsStore()->set("video.pauseOnClick", true);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, c);
        QTRY_VERIFY(application->video().playing());
        QVERIFY(!application->videoView().canSnapshot()); // legacy: only while paused
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, c);
        QTRY_VERIFY(!application->video().playing());
    }

    // The Script properties YCbCr matrix reaches the video's colours: the
    // shown frame is decoded again with it (legacy ProviderFFMS2::SetColorSpace
    // and Render while paused).
    void scriptPropertiesMatrixRecoloursTheVideo()
    {
        restartWithoutSound();
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/color709.mkv"), folder.filePath(QStringLiteral("clip.mkv")));
        QFile f(folder.filePath(QStringLiteral("clip.ass")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 320\nPlayResY: 240\nYCbCr Matrix: TV.709\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:00.00,0:00:00.10,Default,,0,0,0,,x\n");
        f.close();
        QVERIFY(application->openFile(f.fileName()));
        application->video().openVideo(folder.filePath(QStringLiteral("clip.mkv")));
        const auto &session = application->video().session();
        QTRY_VERIFY_WITH_TIMEOUT(session.lastFrame(), 20000);
        QCOMPARE(session.colourMatrix().applied(), std::string("TV.709"));
        // The top-left patch (Y' 180, Cb 60, Cr 200, limited range) as
        // R, G, B; its red saturates under both matrices, so all three
        // channels are compared with each matrix's equations.
        auto pixel = [&] {
            const auto &fr = *session.lastFrame();
            const std::byte *p = fr.bgra.data() + std::size_t(fr.height / 4) * fr.stride + std::size_t(fr.width / 4) * 4;
            return std::array<int, 3>{std::to_integer<int>(p[2]), std::to_integer<int>(p[1]), std::to_integer<int>(p[0])};
        };
        auto expected = [](double kr, double kb) {
            const double y = (180 - 16) / 219.0, pb = (60 - 128) / 224.0, pr = (200 - 128) / 224.0;
            const double r = y + 2 * (1 - kr) * pr, b = y + 2 * (1 - kb) * pb, g = (y - kr * r - kb * b) / (1 - kr - kb);
            auto code = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
            return std::array<int, 3>{code(r), code(g), code(b)};
        };
        auto near = [](const std::array<int, 3> &a, const std::array<int, 3> &b) {
            for (int k = 0; k < 3; ++k)
                if (std::abs(a[k] - b[k]) > 3)
                    return false;
            return true;
        };
        const auto bt709 = expected(0.2126, 0.0722), bt601 = expected(0.299, 0.114); // 255,167,47 and 255,159,54
        QVERIFY(!near(bt709, bt601));
        QVERIFY(near(pixel(), bt709));
        // Script properties (Y3): the matrix changed to TV.601 (index 1).
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("scriptPropertiesDialog"));
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("assProperties")), "triggered"));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *matrix = dialogItem("scriptPropertiesDialog", "propMatrix");
        QCOMPARE(matrix->property("currentText").toString(), QStringLiteral("TV.709"));
        matrix->setProperty("currentIndex", 1);
        QCOMPARE(matrix->property("currentText").toString(), QStringLiteral("TV.601"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(near(pixel(), bt601), 10000);
        QCOMPARE(session.colourMatrix().applied(), std::string("TV.601"));
        auto *edited = application->files().session(*application->workspace().editingTarget());
        QCOMPARE(edited->document().scriptInfo(u8"YCbCr Matrix"), std::optional<std::u8string>(u8"TV.601"));
        // Undo brings the Document's TV.709 back: the source's own matrix again.
        application->editor().discard();
        QVERIFY(QMetaObject::invokeMethod(root, "runGlobalHotkey", Q_ARG(QVariant, QStringLiteral("GLOBAL_UNDO"))));
        QTRY_VERIFY_WITH_TIMEOUT(near(pixel(), bt709), 10000);
        QCOMPARE(session.colourMatrix().applied(), std::string("TV.709"));
    }

    // An untagged HD video is converted with the matrix it is named by,
    // BT.709 (approved departure V4-untagged-matrix: legacy left the
    // converter's BT.601 under the name TV.709 unless the Document said
    // "TV.709", so choosing TV.709 then changed nothing,
    // ProviderFFMS2.cpp:396-412 and 955-962).
    void untaggedHdVideoIsConvertedAsItsMatrix()
    {
        restartWithoutSound();
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile::copy(QStringLiteral(HIKARI_MEDIA_FIXTURES "/colorhd.mkv"), folder.filePath(QStringLiteral("clip.mkv")));
        QFile f(folder.filePath(QStringLiteral("clip.ass")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nYCbCr Matrix: PC.709\n\n[Events]\n"
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:00.00,0:00:00.10,Default,,0,0,0,,x\n");
        f.close();
        QVERIFY(application->openFile(f.fileName()));
        application->video().openVideo(folder.filePath(QStringLiteral("clip.mkv")));
        const auto &session = application->video().session();
        QTRY_VERIFY_WITH_TIMEOUT(session.lastFrame(), 20000);
        QCOMPARE(session.colourMatrix().source(), std::string("TV.709"));
        QCOMPARE(session.colourMatrix().applied(), std::string("TV.709"));
        // The fixture's top-left patch (Y' 180, Cb 60, Cr 200, limited range).
        auto pixel = [&] {
            const auto &fr = *session.lastFrame();
            const std::byte *p = fr.bgra.data() + std::size_t(fr.height / 4) * fr.stride + std::size_t(fr.width / 4) * 4;
            return std::array<int, 3>{std::to_integer<int>(p[2]), std::to_integer<int>(p[1]), std::to_integer<int>(p[0])};
        };
        auto expected = [](double kr, double kb) {
            const double y = (180 - 16) / 219.0, pb = (60 - 128) / 224.0, pr = (200 - 128) / 224.0;
            const double r = y + 2 * (1 - kr) * pr, b = y + 2 * (1 - kb) * pb, g = (y - kr * r - kb * b) / (1 - kr - kb);
            auto code = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
            return std::array<int, 3>{code(r), code(g), code(b)};
        };
        auto near = [](const std::array<int, 3> &a, const std::array<int, 3> &b) {
            for (int k = 0; k < 3; ++k)
                if (std::abs(a[k] - b[k]) > 3)
                    return false;
            return true;
        };
        const auto bt709 = expected(0.2126, 0.0722), bt601 = expected(0.299, 0.114);
        QVERIFY2(near(pixel(), bt709), "the untagged HD frame is converted as BT.709, its name");
        // Script properties: TV.601 converts it as BT.601, TV.709 back as BT.709.
        auto *root = engine->rootObjects().first();
        auto *dialog = root->findChild<QObject *>(QStringLiteral("scriptPropertiesDialog"));
        auto choose = [&](int index, const QString &name) {
            QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>(QStringLiteral("assProperties")), "triggered"));
            QTRY_VERIFY(dialog->property("visible").toBool());
            auto *matrix = dialogItem("scriptPropertiesDialog", "propMatrix");
            matrix->setProperty("currentIndex", index);
            QCOMPARE(matrix->property("currentText").toString(), name);
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
        };
        choose(1, QStringLiteral("TV.601"));
        QTRY_VERIFY_WITH_TIMEOUT(near(pixel(), bt601), 10000);
        QCOMPARE(session.colourMatrix().applied(), std::string("TV.601"));
        choose(3, QStringLiteral("TV.709"));
        QTRY_VERIFY_WITH_TIMEOUT(near(pixel(), bt709), 10000);
        QCOMPARE(session.colourMatrix().applied(), std::string("TV.709"));
    }

};

QTEST_MAIN(ShellTest)
#include "shell_tests.moc"
