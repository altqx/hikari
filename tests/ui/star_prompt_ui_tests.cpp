#include "hikari/app/application.h"
#include "hikari/app/star_prompt.h"
#include "hikari/backends/desktop_handoff.h"

#include "docking.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <memory>

using namespace hikari;

class StarPromptUiTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QDateTime t = QDateTime(QDate(2026, 11, 2), QTime(9, 0));

    static QQuickItem *find(QQuickItem *from, const std::function<bool(QQuickItem *)> &match)
    {
        if (match(from))
            return from;
        for (QQuickItem *child : from->childItems())
            if (QQuickItem *found = find(child, match))
                return found;
        return nullptr;
    }
    static QQuickItem *starToast(QQuickWindow *window)
    {
        QQuickItem *title = find(window->contentItem(), [](QQuickItem *item) {
            return item->objectName() == QLatin1String("toastTitle") &&
                   item->property("text").toString() == QLatin1String("Enjoying HikariSub?");
        });
        while (title && !title->objectName().startsWith(QLatin1String("toast_")))
            title = title->parentItem();
        return title;
    }

private slots:
    void showsOnceThenStarringEndsIt()
    {
        QList<QUrl> opened;
        const backends::desktop::Routes before = backends::desktop::replaceRoutes({[&](const QUrl &url) {
            opened << url;
            return true;
        }, {}, {}});
        app::Application::Options options;
        options.settingsFile = dir.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        options.starPrompt.now = [this] { return t; };
        options.starPrompt.editing = [] { return true; };
        options.starPrompt.tickMs = 24 * 60 * 60 * 1000;
        auto application = std::make_unique<app::Application>(options);
        auto engine = std::make_unique<QQmlApplicationEngine>();
        QVERIFY(ui::attachDocking(*engine));
        engine->setInitialProperties(application->qmlProperties());
        engine->loadFromModule("Hikari.Ui", "Main");
        QVERIFY(!engine->rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->property("arrangementSettled").toBool());

        application->settingsStore()->set("starPrompt.days", 5);
        application->settingsStore()->set("starPrompt.editingSeconds", 3 * 60 * 60);
        t = t.addSecs(9 * 60);
        application->starPrompt().tick();
        QTest::qWait(100);
        QVERIFY(!starToast(window));

        t = t.addSecs(60);
        application->starPrompt().tick();
        QTRY_VERIFY(starToast(window));
        QQuickItem *toast = starToast(window);
        QCOMPARE(find(toast, [](QQuickItem *i) { return i->objectName() == QLatin1String("toastText"); })
                     ->property("text").toString(),
                 QStringLiteral("A star on GitHub helps others find it."));
        QVERIFY(find(toast, [](QQuickItem *i) { return i->property("text").toString() == QLatin1String("Not now"); }));
        QQuickItem *star = find(toast, [](QQuickItem *i) {
            return i->inherits("QQuickAbstractButton") && i->property("text").toString() == QLatin1String("Star on GitHub");
        });
        QVERIFY(star);
        QTRY_VERIFY(star->isVisible() && star->width() > 0);
        QTest::mouseClick(window, Qt::LeftButton, {}, star->mapToScene(QPointF(star->width() / 2, star->height() / 2)).toPoint());
        QTRY_VERIFY(!starToast(window));
        QCOMPARE(opened, (QList<QUrl>{app::StarPrompt::repositoryUrl()}));
        QCOMPARE(application->settingsStore()->integer("starPrompt.shown"), 1);

        t = t.addDays(60);
        application->starPrompt().tick();
        QTest::qWait(200);
        QVERIFY(!starToast(window));
        backends::desktop::replaceRoutes(before);
        application->endSession();
        engine.reset();
        application.reset();
    }
};

QTEST_MAIN(StarPromptUiTest)
#include "star_prompt_ui_tests.moc"
