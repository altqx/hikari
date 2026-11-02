#include "hikari/app/analytics.h"
#include "hikari/app/star_prompt.h"
#include "hikari/backends/desktop_handoff.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using hikari::app::Analytics;
using hikari::app::StarPrompt;
namespace desktop = hikari::backends::desktop;

class StarPromptTest : public QObject {
    Q_OBJECT
    QDateTime t;
    bool editing = true;
    std::unique_ptr<hikari::ui::SettingsStore> settings;
    std::unique_ptr<Analytics> analytics;

    StarPrompt::Config config()
    {
        StarPrompt::Config c;
        c.now = [this] { return t; };
        c.editing = [this] { return editing; };
        c.tickMs = 60 * 1000;
        return c;
    }
    void makeReady()
    {
        settings->set("starPrompt.days", 5);
        settings->set("starPrompt.editingSeconds", 3 * 60 * 60);
    }
    void advance(qint64 seconds) { t = t.addSecs(seconds); }
    qint64 counter(const char *id) const { return settings->integer64(id); }
    QStringList queuedEvents() const
    {
        QStringList events;
        for (const QJsonValue &e : analytics->queued())
            events << e.toObject().value(QStringLiteral("event")).toString();
        return events;
    }

private slots:
    void init()
    {
        t = QDateTime(QDate(2026, 11, 2), QTime(9, 0));
        editing = true;
        settings = std::make_unique<hikari::ui::SettingsStore>();
        Analytics::Config a;
        a.key = QStringLiteral("phc_test");
        a.firstFlushMs = a.flushIntervalMs = 60 * 60 * 1000;
        analytics = std::make_unique<Analytics>(*settings, a);
    }
    void cleanup()
    {
        analytics.reset();
        settings.reset();
    }

    void onByDefaultWithNothingCounted()
    {
        QVERIFY(settings->boolean("privacy.starPrompt"));
        for (const char *id : {"starPrompt.days", "starPrompt.editingSeconds", "starPrompt.shown", "starPrompt.snoozedUntil"})
            QVERIFY2(counter(id) == 0, id);
        QVERIFY(!settings->boolean("starPrompt.starred"));
    }

    void countsEachDayOnce()
    {
        {
            StarPrompt prompt(*settings, *analytics, config());
            QCOMPARE(counter("starPrompt.days"), 1);
            advance(60);
            prompt.tick();
            QCOMPARE(counter("starPrompt.days"), 1);
            advance(24 * 60 * 60);
            prompt.tick();
            QCOMPARE(counter("starPrompt.days"), 2);
        }
        StarPrompt again(*settings, *analytics, config());
        QCOMPARE(counter("starPrompt.days"), 2);
        QCOMPARE(settings->text("starPrompt.lastDay"), QStringLiteral("2026-11-03"));
    }

    void countsForegroundEditingOnly()
    {
        StarPrompt prompt(*settings, *analytics, config());
        advance(60);
        prompt.tick();
        QCOMPARE(counter("starPrompt.editingSeconds"), 60);
        editing = false;
        advance(60);
        prompt.tick();
        QCOMPARE(counter("starPrompt.editingSeconds"), 60);
        editing = true;
        advance(2 * 60 * 60);
        prompt.tick();
        QCOMPARE(counter("starPrompt.editingSeconds"), 180);
    }

    void waitsForDaysHoursAndTenMinutes()
    {
        StarPrompt prompt(*settings, *analytics, config());
        QSignalSpy due(&prompt, &StarPrompt::due);
        settings->set("starPrompt.days", 4);
        settings->set("starPrompt.editingSeconds", 3 * 60 * 60);
        advance(11 * 60);
        prompt.tick();
        QCOMPARE(due.size(), 0);
        settings->set("starPrompt.days", 5);
        settings->set("starPrompt.editingSeconds", 3 * 60 * 60 - 120);
        prompt.tick();
        QCOMPARE(due.size(), 0);

        StarPrompt fresh(*settings, *analytics, config());
        QSignalSpy freshDue(&fresh, &StarPrompt::due);
        makeReady();
        advance(9 * 60);
        fresh.tick();
        QCOMPARE(freshDue.size(), 0);
        QVERIFY(!fresh.offer(false, false, false));
        advance(60);
        fresh.tick();
        QCOMPARE(freshDue.size(), 1);
    }

    void blockedWhileBusy()
    {
        StarPrompt prompt(*settings, *analytics, config());
        makeReady();
        advance(10 * 60);
        QVERIFY(!prompt.offer(true, false, false));
        QVERIFY(!prompt.offer(false, true, false));
        QVERIFY(!prompt.offer(false, false, true));
        prompt.keyPressed();
        advance(5);
        QVERIFY(!prompt.waiting(false, false, false));
        QVERIFY(!prompt.offer(false, false, false));
        advance(5);
        QVERIFY(prompt.waiting(false, false, false));
        QCOMPARE(counter("starPrompt.shown"), 0);
    }

    void typingIsAnyKeyPress()
    {
        StarPrompt prompt(*settings, *analytics, config());
        makeReady();
        advance(10 * 60);
        QVERIFY(prompt.waiting(false, false, false));
        QObject target;
        QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QCoreApplication::sendEvent(&target, &press);
        QVERIFY(!prompt.waiting(false, false, false));
    }

    void quietOnAFirstRunOrAfterAnUpdate()
    {
        StarPrompt prompt(*settings, *analytics, config());
        QSignalSpy due(&prompt, &StarPrompt::due);
        makeReady();
        prompt.quietRun();
        advance(60 * 60);
        prompt.tick();
        QCOMPARE(due.size(), 0);
        QVERIFY(!prompt.offer(false, false, false));
    }

    void snoozesThirtyDaysAndShowsAtMostTwice()
    {
        StarPrompt prompt(*settings, *analytics, config());
        makeReady();
        advance(10 * 60);
        QVERIFY(prompt.offer(false, false, false));
        QCOMPARE(counter("starPrompt.shown"), 1);
        QVERIFY(!prompt.offer(false, false, false));
        advance(29 * 24 * 60 * 60);
        QVERIFY(!prompt.offer(false, false, false));
        advance(24 * 60 * 60);
        QVERIFY(prompt.offer(false, false, false));
        QCOMPARE(counter("starPrompt.shown"), 2);
        QVERIFY(prompt.finished());
        advance(365 * 24 * 60 * 60);
        QSignalSpy due(&prompt, &StarPrompt::due);
        prompt.tick();
        QCOMPARE(due.size(), 0);
        QVERIFY(!prompt.offer(false, false, false));

        StarPrompt nextRun(*settings, *analytics, config());
        advance(10 * 60);
        QVERIFY(!nextRun.offer(false, false, false));
    }

    void starOpensTheRepositoryAndEndsIt()
    {
        QList<QUrl> opened;
        const desktop::Routes before = desktop::replaceRoutes({[&](const QUrl &url) {
            opened << url;
            return true;
        }, {}, {}});
        StarPrompt prompt(*settings, *analytics, config());
        makeReady();
        advance(10 * 60);
        QVERIFY(prompt.offer(false, false, false));
        prompt.star();
        desktop::replaceRoutes(before);
        QCOMPARE(opened, (QList<QUrl>{QUrl(QStringLiteral("https://github.com/altqx/hikari"))}));
        QVERIFY(settings->boolean("starPrompt.starred"));
        QVERIFY(prompt.finished());
        advance(60 * 24 * 60 * 60);
        QVERIFY(!prompt.offer(false, false, false));
    }

    void theSettingTurnsItOff()
    {
        StarPrompt prompt(*settings, *analytics, config());
        QSignalSpy due(&prompt, &StarPrompt::due);
        makeReady();
        settings->set("privacy.starPrompt", false);
        advance(60 * 60);
        prompt.tick();
        QCOMPARE(due.size(), 0);
        QVERIFY(!prompt.offer(false, false, false));
        QCOMPARE(counter("starPrompt.editingSeconds"), 3 * 60 * 60);
    }

    void recordsTwoBareUsageEvents()
    {
        QVERIFY(Analytics::conforms(QStringLiteral("star_prompt_shown"), {}));
        QVERIFY(Analytics::conforms(QStringLiteral("star_prompt_clicked"), {}));
        QVERIFY(!Analytics::conforms(QStringLiteral("star_prompt_shown"), {{QStringLiteral("count"), 1}}));
        const desktop::Routes before = desktop::replaceRoutes({[](const QUrl &) { return true; }, {}, {}});
        StarPrompt prompt(*settings, *analytics, config());
        makeReady();
        advance(10 * 60);
        QVERIFY(prompt.offer(false, false, false));
        prompt.star();
        desktop::replaceRoutes(before);
        QCOMPARE(queuedEvents(), (QStringList{QStringLiteral("star_prompt_shown"), QStringLiteral("star_prompt_clicked")}));

        settings->set("privacy.usageData", false);
        settings->set("starPrompt.starred", false);
        settings->set("starPrompt.shown", 0);
        settings->set("starPrompt.snoozedUntil", 0);
        QVERIFY(prompt.offer(false, false, false));
        QVERIFY(analytics->queued().isEmpty());
    }
};

QTEST_MAIN(StarPromptTest)
#include "star_prompt_tests.moc"
