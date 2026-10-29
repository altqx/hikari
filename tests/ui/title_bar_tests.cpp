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

struct Recorder {
    QHash<QWindow *, bool> dark;
    QHash<QWindow *, int> calls;
    void operator()(QWindow *window, bool isDark)
    {
        dark.insert(window, isDark);
        ++calls[window];
    }
};

}

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
        QWindow frameless;
        frameless.setFlags(Qt::Tool | Qt::FramelessWindowHint);
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
        QCOMPARE(ui::theme::current().code, Code::Dark);

        QWindow main;
        QQuickWindow dialog;
        dialog.setFlags(Qt::Dialog);
        dialog.setTransientParent(&main);
        QWindow floating;
        floating.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        QWindow popup;
        popup.setFlags(Qt::Popup);
        const QList<QWindow *> framed{&main, &dialog};
        for (QWindow *w : {&main, static_cast<QWindow *>(&dialog), &floating, &popup})
            w->create();
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
        store.setValue(kFollow, false);
        store.setValue(kTheme, QStringLiteral("light"));
        QCOMPARE(ui::theme::current().code, Code::Light);
        expect(false, "Light chosen");
        store.setValue(kTheme, QStringLiteral("highContrastBlack"));
        expect(true, "High contrast black chosen");
        store.setValue(kTheme, QStringLiteral("highContrastWhite"));
        expect(false, "High contrast white chosen");
        ui::theme::preview({{kTheme, QStringLiteral("dark")}, {kFollow, false}});
        expect(true, "Dark previewed");
        ui::theme::endPreview();
        expect(false, "the preview ended");
        store.setValue(kTheme, QStringLiteral("dark"));
        store.setValue(kFollow, true);
        ui::theme::forceSystemScheme(Qt::ColorScheme::Light);
        QCOMPARE(ui::theme::current().code, Code::Light);
        expect(false, "the system light");
        ui::theme::forceSystemScheme(Qt::ColorScheme::Dark);
        expect(true, "the system dark");

        QWindow later;
        later.create();
        QCOMPARE(recorder->dark.value(&later, false), true);

        // Qt sets the title bars itself on an application palette change.
        const int before = recorder->calls.value(&main);
        QGuiApplication::setPalette(QGuiApplication::palette());
        QPalette other = QGuiApplication::palette();
        other.setColor(QPalette::Window, Qt::red);
        QGuiApplication::setPalette(other);
        QTRY_VERIFY(recorder->calls.value(&main) > before);
        QCOMPARE(recorder->dark.value(&main), true);
    }

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
        ui::theme::forceSystemScheme(std::nullopt);
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
        // Qt sets the title bars itself on an application palette change.
        QGuiApplication::setPalette(QGuiApplication::palette());
        QTest::qWait(100);
        expect(true, "after an application palette change");
        store.setValue(kFollow, true);
        expect(ui::theme::isDark(ui::theme::current().code), "following the system");

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
