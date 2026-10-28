// K2 on Windows: the title bar the window system draws on every framed
// window follows the theme (src/ui/title_bars.h): dark under Dark and High
// contrast black, light under Light and High contrast white, set when a
// window gets its native window, on every theme change (Options, the live
// preview, the system's scheme while following it) and again after an
// application palette change, on which Qt sets the frame itself. Off
// Windows the window system's call is replaced (titlebars::overrideSetter)
// and the calls are checked; on Windows (QT_QPA_PLATFORM=windows, the
// .windows entry) the title bar's attribute is read back from the window
// system (DwmGetWindowAttribute).

#include "settings_store.h"
#include "theme.h"
#include "title_bars.h"

#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QPointer>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QWindow>
#include <QtTest>

#include <memory>

using namespace hikari;
using ui::theme::Code;

namespace {

const QString kTheme = QLatin1String(ui::theme::kThemeSetting);
const QString kFollow = QLatin1String(ui::theme::kFollowSystemSetting);

// What the theme set on each window's title bar last, and how often.
struct Recorder {
    QHash<QWindow *, bool> dark;
    QHash<QWindow *, int> calls;
    void operator()(QWindow *window, bool isDark)
    {
        dark.insert(window, isDark);
        ++calls[window];
    }
};

} // namespace

class TitleBarTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        ui::theme::useSettings(nullptr);
        ui::theme::endPreview();
        ui::theme::forceSystemScheme(Qt::ColorScheme::Unknown);
    }

    void cleanup()
    {
        ui::titlebars::overrideSetter(nullptr);
        ui::theme::useSettings(nullptr);
        ui::theme::endPreview();
    }

    void framedWindowsAreThemed()
    {
        QWindow normal;
        QVERIFY(ui::titlebars::themed(&normal));
        QWindow dialog;
        dialog.setFlags(Qt::Dialog);
        QVERIFY(ui::titlebars::themed(&dialog));
        QWindow tool;
        tool.setFlags(Qt::Tool);
        QVERIFY(ui::titlebars::themed(&tool));
        // no title bar of the window system's
        QWindow frameless;
        frameless.setFlags(Qt::Tool | Qt::FramelessWindowHint); // a floating panel
        QVERIFY(!ui::titlebars::themed(&frameless));
        QWindow popup;
        popup.setFlags(Qt::Popup);
        QVERIFY(!ui::titlebars::themed(&popup));
        QWindow tip;
        tip.setFlags(Qt::ToolTip);
        QVERIFY(!ui::titlebars::themed(&tip));
        QWindow child(&normal);
        QVERIFY(!ui::titlebars::themed(&child));
        QVERIFY(!ui::titlebars::themed(nullptr));
    }

    // Every framed top-level window, at its creation and on every switch:
    // a theme chosen in Options, the live preview, the system's scheme
    // while following it, and an application palette change.
    void everyFramedWindowFollowsTheTheme()
    {
        auto recorder = std::make_shared<Recorder>();
        ui::titlebars::overrideSetter([recorder](QWindow *w, bool dark) { (*recorder)(w, dark); });
        ui::SettingsStore store;
        const QPalette palette = QGuiApplication::palette();
        const auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            QGuiApplication::setPalette(palette);
        });
        ui::theme::useSettings(&store);
        QCOMPARE(ui::theme::current().code, Code::Dark); // following, the scheme unknown

        QWindow main;
        QQuickWindow dialog;
        dialog.setFlags(Qt::Dialog);
        dialog.setTransientParent(&main);
        QWindow floating; // a floating panel: frameless
        floating.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        QWindow popup;
        popup.setFlags(Qt::Popup);
        const QList<QWindow *> framed{&main, &dialog};
        for (QWindow *w : {&main, static_cast<QWindow *>(&dialog), &floating, &popup})
            w->create();
        // at creation
        for (QWindow *w : framed) {
            QVERIFY2(recorder->dark.contains(w), qPrintable(w->objectName()));
            QCOMPARE(recorder->dark.value(w), true);
        }
        QVERIFY(!recorder->calls.contains(&floating));
        QVERIFY(!recorder->calls.contains(&popup));

        const auto expect = [&](bool dark, const char *what) {
            for (QWindow *w : framed)
                QVERIFY2(recorder->dark.value(w, !dark) == dark, what);
            QVERIFY2(!recorder->calls.contains(&floating) && !recorder->calls.contains(&popup), what);
        };
        // chosen in Options (following the system turned off)
        store.setValue(kFollow, false);
        store.setValue(kTheme, QStringLiteral("light"));
        QCOMPARE(ui::theme::current().code, Code::Light);
        expect(false, "Light chosen");
        store.setValue(kTheme, QStringLiteral("highContrastBlack"));
        expect(true, "High contrast black chosen");
        store.setValue(kTheme, QStringLiteral("highContrastWhite"));
        expect(false, "High contrast white chosen");
        // the Options dialog's live preview, and its end
        ui::theme::preview({{kTheme, QStringLiteral("dark")}, {kFollow, false}});
        expect(true, "Dark previewed");
        ui::theme::endPreview();
        expect(false, "the preview ended");
        // following the system: its scheme, live
        store.setValue(kTheme, QStringLiteral("dark"));
        store.setValue(kFollow, true);
        ui::theme::forceSystemScheme(Qt::ColorScheme::Light);
        QCOMPARE(ui::theme::current().code, Code::Light);
        expect(false, "the system light");
        ui::theme::forceSystemScheme(Qt::ColorScheme::Dark);
        expect(true, "the system dark");

        // A window made later takes the current theme at its creation.
        QWindow later;
        later.create();
        QCOMPARE(recorder->dark.value(&later, false), true);

        // An application palette change has Qt set the frames itself: the
        // theme's are set again after it.
        const int before = recorder->calls.value(&main);
        QGuiApplication::setPalette(QGuiApplication::palette());
        QPalette other = QGuiApplication::palette();
        other.setColor(QPalette::Window, Qt::red);
        QGuiApplication::setPalette(other);
        QTRY_VERIFY(recorder->calls.value(&main) > before);
        QCOMPARE(recorder->dark.value(&main), true);
    }

    // Windows: the title bar's attribute as the window system has it, for a
    // shown main window and dialog, through switches; then Qt's own setting
    // of the frame on a palette change must not take it back.
    void windowsTitleBarFollowsTheTheme()
    {
        if (QGuiApplication::platformName() != QLatin1String("windows"))
            QSKIP("reads the title bar from Windows (QT_QPA_PLATFORM=windows)");
        ui::SettingsStore store;
        const QPalette palette = QGuiApplication::palette();
        const auto restore = qScopeGuard([&] {
            ui::theme::useSettings(nullptr);
            ui::theme::forceSystemScheme(std::nullopt);
            QGuiApplication::setPalette(palette);
        });
        ui::theme::forceSystemScheme(std::nullopt); // the system's own scheme
        ui::theme::useSettings(&store);
        store.setValue(kFollow, false);
        store.setValue(kTheme, QStringLiteral("dark"));

        QQuickWindow main;
        main.setTitle(QStringLiteral("title bar test"));
        main.resize(320, 200);
        main.show();
        QVERIFY(QTest::qWaitForWindowExposed(&main));
        QQuickWindow dialog;
        dialog.setFlags(Qt::Dialog);
        dialog.setTransientParent(&main);
        dialog.setTitle(QStringLiteral("title bar test dialog"));
        dialog.resize(200, 120);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        if (!ui::titlebars::isDark(&main))
            QSKIP("the window system reports no title bar attribute (no desktop window manager)");

        const auto expect = [&](bool dark, const char *what) {
            for (QWindow *w : {static_cast<QWindow *>(&main), static_cast<QWindow *>(&dialog)})
                QTRY_VERIFY2(ui::titlebars::isDark(w) == std::optional<bool>(dark), what);
        };
        expect(true, "Dark chosen, at creation");
        store.setValue(kTheme, QStringLiteral("light"));
        expect(false, "Light chosen");
        store.setValue(kTheme, QStringLiteral("highContrastBlack"));
        expect(true, "High contrast black chosen");
        ui::theme::preview({{kTheme, QStringLiteral("light")}, {kFollow, false}});
        expect(false, "Light previewed");
        ui::theme::endPreview();
        expect(true, "the preview ended");
        // Qt sets the frames again on a palette change (from its colour
        // scheme, which the theme asked for, and the window's palette).
        QGuiApplication::setPalette(QGuiApplication::palette());
        QTest::qWait(100);
        expect(true, "after an application palette change");
        // Following the system: the system's side.
        store.setValue(kFollow, true);
        expect(ui::theme::isDark(ui::theme::current().code), "following the system");

        // A dialog made while the theme is dark is dark from the start.
        store.setValue(kFollow, false);
        store.setValue(kTheme, QStringLiteral("dark"));
        QQuickWindow later;
        later.setFlags(Qt::Dialog);
        later.setTransientParent(&main);
        later.resize(200, 120);
        later.show();
        QVERIFY(QTest::qWaitForWindowExposed(&later));
        QTRY_COMPARE(ui::titlebars::isDark(&later), std::optional<bool>(true));
    }
};

QTEST_MAIN(TitleBarTests)
#include "title_bar_tests.moc"
