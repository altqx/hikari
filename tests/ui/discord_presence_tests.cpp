#include "hikari/app/application.h"
#include "hikari/app/discord_ipc.h"
#include "hikari/app/discord_presence.h"
#include "settings_store.h"
#include "workspace_layout.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QtTest>

#include <algorithm>
#include <memory>

using hikari::app::DiscordIpc;
using hikari::app::DiscordPresence;
namespace discord = hikari::app::discord;

namespace {

class FakeDiscord : public QObject {
    Q_OBJECT
public:
    explicit FakeDiscord(const QTemporaryDir &dir)
#ifdef _WIN32
        : m_name(QStringLiteral("hikari-test-discord-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
#else
        : m_name(dir.filePath(QStringLiteral("discord-ipc-0")))
#endif
    {
        Q_UNUSED(dir)
        connect(&m_server, &QLocalServer::newConnection, this, &FakeDiscord::accept);
    }
    bool listen()
    {
        QLocalServer::removeServer(m_name);
        return m_server.listen(m_name);
    }
    void stop()
    {
        m_server.close();
        drop();
    }
    void drop()
    {
        if (m_client)
            m_client->abort();
    }
    QString candidate() const
    {
#ifdef _WIN32
        return QStringLiteral("\\\\.\\pipe\\") + m_name;
#else
        return m_name;
#endif
    }
    void send(quint32 opcode, const QJsonObject &payload)
    {
        m_client->write(DiscordIpc::encode(opcode, payload));
        m_client->flush();
    }
    bool clientGone() const { return !m_client || m_client->state() == QLocalSocket::UnconnectedState; }

    // The `activity` of each SET_ACTIVITY, or an empty object for a clear.
    QList<QJsonObject> activities() const
    {
        QList<QJsonObject> found;
        for (const DiscordIpc::Message &m : received)
            if (m.opcode == DiscordIpc::Frame &&
                m.payload.value(QStringLiteral("cmd")).toString() == QLatin1String("SET_ACTIVITY"))
                found << m.payload.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
        return found;
    }
    QList<DiscordIpc::Message> received;
    int connections = 0;
    bool answerHandshake = true;

private:
    void accept()
    {
        m_client = m_server.nextPendingConnection();
        ++connections;
        m_buffer.clear();
        connect(m_client, &QLocalSocket::readyRead, this, [this, client = m_client] {
            m_buffer += client->readAll();
            bool broken = false;
            while (const auto message = DiscordIpc::decode(m_buffer, &broken)) {
                received << *message;
                if (message->opcode == DiscordIpc::Handshake && answerHandshake)
                    send(DiscordIpc::Frame, {{QStringLiteral("cmd"), QStringLiteral("DISPATCH")},
                                             {QStringLiteral("evt"), QStringLiteral("READY")},
                                             {QStringLiteral("data"), QJsonObject{{QStringLiteral("v"), 1}}}});
            }
            QVERIFY(!broken);
        });
    }

    QString m_name;
    QLocalServer m_server;
    QPointer<QLocalSocket> m_client;
    QByteArray m_buffer;
};

QString text(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

DiscordPresence::Tab tab(const QString &fileName = {}, quint64 id = 1)
{
    return {true, id, fileName, {}, {}};
}

discord::Scene scene(const QString &fileName = QStringLiteral("[Team] Show - 03.ass"))
{
    discord::Scene s;
    s.tabOpen = true;
    s.layoutMode = QStringLiteral("Timing");
    s.fileName = fileName;
    s.title = QStringLiteral("Show title");
    s.projectName = QStringLiteral("Show S1");
    s.since = 100;
    return s;
}

QString field(const QJsonObject &activity, const char *name)
{
    return activity.value(QLatin1String(name)).toString();
}

QString lastField(const FakeDiscord &discord, const char *name)
{
    const auto sent = discord.activities();
    return sent.isEmpty() ? QString() : field(sent.last(), name);
}

QString smallImage(const QJsonObject &activity)
{
    return activity.value(QStringLiteral("assets")).toObject().value(QStringLiteral("small_image")).toString();
}

}

class DiscordPresenceTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    DiscordPresence::Config config(const FakeDiscord &discord, int updateGapMs = 0)
    {
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("123456789");
        c.candidates = {discord.candidate()};
        c.retryMs = 50;
        c.updateGapMs = updateGapMs;
        c.pid = 4242;
        return c;
    }

private slots:
    void framesRoundTrip()
    {
        const QJsonObject payload{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")}};
        QByteArray buffer = DiscordIpc::encode(DiscordIpc::Frame, payload);
        QCOMPARE(buffer.size(), 8 + QJsonDocument(payload).toJson(QJsonDocument::Compact).size());
        QCOMPARE(buffer.at(0), char(1));
        QByteArray partial = buffer.left(buffer.size() - 1);
        bool broken = true;
        QVERIFY(!DiscordIpc::decode(partial, &broken));
        QVERIFY(!broken);
        buffer += DiscordIpc::encode(DiscordIpc::Ping, {});
        const auto first = DiscordIpc::decode(buffer, &broken);
        QVERIFY(first);
        QCOMPARE(first->opcode, quint32(DiscordIpc::Frame));
        QCOMPARE(first->payload, payload);
        const auto second = DiscordIpc::decode(buffer, &broken);
        QVERIFY(second);
        QCOMPARE(second->opcode, quint32(DiscordIpc::Ping));
        QVERIFY(buffer.isEmpty());

        QByteArray huge = DiscordIpc::encode(DiscordIpc::Frame, {});
        qToLittleEndian<quint32>(quint32(DiscordIpc::kMaxPayload + 1), huge.data() + 4);
        QVERIFY(!DiscordIpc::decode(huge, &broken));
        QVERIFY(broken);
        QByteArray notJson = QByteArray::fromHex("0100000003000000") + "abc";
        QVERIFY(!DiscordIpc::decode(notJson, &broken));
        QVERIFY(broken);
    }

    void candidatesCoverEachPlatform()
    {
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), QStringLiteral("/run/user/1000"));
        environment.insert(QStringLiteral("TMPDIR"), QStringLiteral("/tmp/"));
        const QStringList candidates = DiscordIpc::defaultCandidates(environment);
#ifdef _WIN32
        QCOMPARE(candidates.size(), 10);
        QCOMPARE(candidates.first(), QStringLiteral("\\\\.\\pipe\\discord-ipc-0"));
        QCOMPARE(candidates.last(), QStringLiteral("\\\\.\\pipe\\discord-ipc-9"));
#else
        QCOMPARE(candidates.first(), QStringLiteral("/run/user/1000/discord-ipc-0"));
        for (const char *path : {"/run/user/1000/discord-ipc-9", "/run/user/1000/app/com.discordapp.Discord/discord-ipc-0",
                                 "/run/user/1000/snap.discord/discord-ipc-0", "/tmp/discord-ipc-0"})
            QVERIFY2(candidates.contains(QLatin1String(path)), path);
        QCOMPARE(candidates.count(QStringLiteral("/tmp/discord-ipc-0")), 1);
#endif
    }

    void theBuildCarriesTheHikariSubApplication()
    {
        QCOMPARE(DiscordPresence::defaultClientId(), QStringLiteral("1566681222510411967"));
    }

    void offByDefault()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        QVERIFY(!settings.boolean("privacy.discordPresence"));
        QVERIFY(!settings.boolean("privacy.discordFileName"));
        DiscordPresence presence(settings, config(discord));
        QTest::qWait(200);
        QCOMPARE(discord.connections, 0);
        QVERIFY(presence.activity().isEmpty());
    }

    void showsTheModeAfterTheHandshake()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        DiscordPresence presence(settings, config(discord));
        presence.setMode(QStringLiteral("Timing"));
        presence.setTab(tab(QStringLiteral("episode01.ass")));
        const qint64 before = QDateTime::currentSecsSinceEpoch();
        settings.set("privacy.discordPresence", true);
        QTRY_COMPARE(discord.activities().size(), 1);
        const DiscordIpc::Message &handshake = discord.received.first();
        QCOMPARE(handshake.opcode, quint32(DiscordIpc::Handshake));
        QCOMPARE(handshake.payload, (QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), QStringLiteral("123456789")}}));
        const DiscordIpc::Message &set = discord.received.at(1);
        QCOMPARE(set.payload.value(QStringLiteral("args")).toObject().value(QStringLiteral("pid")).toInteger(), 4242);
        QVERIFY(!set.payload.value(QStringLiteral("nonce")).toString().isEmpty());

        const QJsonObject a = discord.activities().first();
        QCOMPARE(a.value(QStringLiteral("details")).toString(), QStringLiteral("Editing subtitles"));
        QCOMPARE(a.value(QStringLiteral("state")).toString(), QStringLiteral("Timing"));
        const qint64 start = a.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger();
        QVERIFY(start >= before && start <= QDateTime::currentSecsSinceEpoch());
        const QJsonObject assets = a.value(QStringLiteral("assets")).toObject();
        QCOMPARE(assets.value(QStringLiteral("large_image")).toString(), QStringLiteral("hikari"));
        QCOMPARE(assets.value(QStringLiteral("small_image")).toString(), QStringLiteral("timing"));
        QCOMPARE(assets.value(QStringLiteral("small_text")).toString(), QStringLiteral("Timing"));
        QCOMPARE(a.value(QStringLiteral("buttons")).toArray(),
                 (QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("Get HikariSub")},
                                         {QStringLiteral("url"), QStringLiteral("https://hikarisub.org")}}}));
        QVERIFY2(!text(a).contains(QLatin1String("episode01")), qPrintable(text(a)));
    }

    void eachModeHasItsImage_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("image");
        QTest::newRow("Editing") << QStringLiteral("Editing") << QStringLiteral("editing");
        QTest::newRow("Timing") << QStringLiteral("Timing") << QStringLiteral("timing");
        QTest::newRow("Translation") << QStringLiteral("Translation") << QStringLiteral("translation");
        QTest::newRow("Typesetting") << QStringLiteral("Typesetting") << QStringLiteral("typesetting");
        QTest::newRow("Karaoke") << QStringLiteral("Karaoke") << QStringLiteral("karaoke");
        QTest::newRow("unknown") << QStringLiteral("Mine") << QStringLiteral("editing");
    }
    void eachModeHasItsImage()
    {
        QFETCH(QString, preset);
        QFETCH(QString, image);
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("1");
        c.candidates = {dir.filePath(QStringLiteral("none"))};
        DiscordPresence presence(settings, c);
        presence.setTab(tab());
        presence.setMode(preset);
        const QJsonObject assets = presence.activity().value(QStringLiteral("assets")).toObject();
        QCOMPARE(assets.value(QStringLiteral("small_image")).toString(), image);
    }

    void theFileNameNeedsItsOwnOptIn()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord));
        presence.setMode(QStringLiteral("Karaoke"));
        presence.setTab(tab(QStringLiteral("episode01.ass")));
        QTRY_VERIFY(!discord.activities().isEmpty());
        QVERIFY(!text(discord.activities().last()).contains(QLatin1String("episode01")));
        settings.set("privacy.discordFileName", true);
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(),
                     QStringLiteral("Karaoke · episode01.ass"));
        presence.setTab(tab());
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(), QStringLiteral("Karaoke"));
        presence.setTab(tab(QString(300, QLatin1Char('x'))));
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString().size(), 128);
    }

    void turningItOffClearsAtOnce()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord, 60 * 1000));
        QTRY_COMPARE(discord.activities().size(), 1);
        settings.set("privacy.discordPresence", false);
        QTRY_COMPARE(discord.activities().size(), 2);
        QVERIFY(discord.activities().last().isEmpty());
        QTRY_VERIFY(discord.clientGone());
        QTest::qWait(200);
        QCOMPARE(discord.connections, 1);
        QVERIFY(presence.activity().isEmpty());
    }

    void updatesAreRateLimited()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord, 600));
        presence.setTab(tab());
        QTRY_COMPARE(discord.activities().size(), 1);
        QElapsedTimer clock;
        clock.start();
        for (const char *mode : {"Timing", "Typesetting", "Translation", "Karaoke"})
            presence.setMode(QLatin1String(mode));
        QTest::qWait(300);
        QCOMPARE(discord.activities().size(), 1);
        QTRY_COMPARE(discord.activities().size(), 2);
        QVERIFY(clock.elapsed() >= 500);
        QCOMPARE(discord.activities().last().value(QStringLiteral("state")).toString(), QStringLiteral("Karaoke"));
        presence.setMode(QStringLiteral("Karaoke"));
        QTest::qWait(800);
        QCOMPARE(discord.activities().size(), 2);
    }

    void waitsForDiscordToStart()
    {
        FakeDiscord discord(dir);
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord));
        QTest::qWait(200);
        QVERIFY(!presence.connected());
        QVERIFY(discord.listen());
        QTRY_COMPARE(discord.activities().size(), 1);
        QVERIFY(presence.connected());
    }

    void reconnectsWhenDiscordRestarts()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord));
        QTRY_COMPARE(discord.activities().size(), 1);
        discord.drop();
        QTRY_COMPARE(discord.connections, 2);
        QTRY_COMPARE(discord.activities().size(), 2);
        discord.send(DiscordIpc::Close, {{QStringLiteral("code"), 1000}});
        QTRY_COMPARE(discord.connections, 3);
        QTRY_COMPARE(discord.activities().size(), 3);
    }

    void answersPings()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord));
        QTRY_COMPARE(discord.activities().size(), 1);
        const QJsonObject ping{{QStringLiteral("n"), 7}};
        discord.send(DiscordIpc::Ping, ping);
        QTRY_VERIFY(std::ranges::any_of(discord.received, [&](const DiscordIpc::Message &m) {
            return m.opcode == DiscordIpc::Pong && m.payload == ping;
        }));
    }

    void givesUpOnASilentServer()
    {
        FakeDiscord discord(dir);
        discord.answerHandshake = false;
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence presence(settings, config(discord));
        QTRY_COMPARE(discord.connections, 1);
        QTRY_COMPARE_WITH_TIMEOUT(discord.connections, 2, 8000);
        QVERIFY(discord.activities().isEmpty());
    }

    void clearsWhenClosed()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        auto presence = std::make_unique<DiscordPresence>(settings, config(discord));
        QTRY_COMPARE(discord.activities().size(), 1);
        presence.reset();
        QTRY_COMPARE(discord.activities().size(), 2);
        QVERIFY(discord.activities().last().isEmpty());
        QTRY_VERIFY(discord.clientGone());
    }

    void quietWithoutDiscord()
    {
        QTest::failOnWarning();
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("1");
        c.candidates = {dir.filePath(QStringLiteral("discord-ipc-0")), dir.filePath(QStringLiteral("discord-ipc-1"))};
        c.retryMs = 20;
        DiscordPresence presence(settings, c);
        QTest::qWait(300);
        QVERIFY(!presence.connected());
        settings.set("privacy.discordPresence", false);
    }

    void theStandardStateJoinsWhatTheUserAllows()
    {
        discord::Choices c;
        QCOMPARE(field(discord::compose(c, scene()), "state"), QStringLiteral("Timing"));
        c.fileName = true;
        c.projectName = true;
        QCOMPARE(field(discord::compose(c, scene()), "state"), QStringLiteral("Timing · [Team] Show - 03.ass · Show S1"));
        c.mode = false;
        QCOMPARE(field(discord::compose(c, scene()), "state"), QStringLiteral("[Team] Show - 03.ass · Show S1"));
        c.fileName = c.projectName = false;
        QVERIFY(!discord::compose(c, scene()).contains(QStringLiteral("state")));
        QCOMPARE(field(discord::compose(c, scene()), "details"), QStringLiteral("Editing subtitles"));
    }

    void ownTextFillsOnlyAllowedPlaceholders()
    {
        discord::Choices c;
        c.ownText = true;
        c.details = QStringLiteral("{show} ep {episode}");
        c.state = QStringLiteral("{mode} {file} {project} {title} {unknown}");
        QJsonObject a = discord::compose(c, scene());
        QCOMPARE(field(a, "details"), QStringLiteral("Show title ep"));
        QCOMPARE(field(a, "state"), QStringLiteral("Timing   Show title {unknown}"));
        c.fileName = c.projectName = true;
        a = discord::compose(c, scene());
        QCOMPARE(field(a, "details"), QStringLiteral("Show title ep 03"));
        QCOMPARE(field(a, "state"), QStringLiteral("Timing [Team] Show - 03.ass Show S1 Show title {unknown}"));
    }

    void ownLinesAreCutAndShortOnesLeftOut()
    {
        discord::Choices c;
        c.ownText = true;
        c.details = QStringLiteral("{file}x");
        c.state = QString(200, QLatin1Char('y'));
        const QJsonObject a = discord::compose(c, scene());
        QVERIFY(!a.contains(QStringLiteral("details")));
        QCOMPARE(field(a, "state").size(), 128);
        QVERIFY(field(a, "state").endsWith(QChar(0x2026)));
    }

    void findsTheEpisode_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("episode");
        QTest::newRow("dash") << QStringLiteral("[Group] Show - 03 [1080p].ass") << QStringLiteral("03");
        QTest::newRow("E") << QStringLiteral("Show E12.ass") << QStringLiteral("12");
        QTest::newRow("SxxEyy") << QStringLiteral("Show.S01E07.ass") << QStringLiteral("07");
        QTest::newRow("none") << QStringLiteral("Movie 2024.ass") << QString();
        QTest::newRow("in a word") << QStringLiteral("Episode.ass") << QString();
    }
    void findsTheEpisode()
    {
        QFETCH(QString, file);
        QFETCH(QString, episode);
        QCOMPARE(discord::episodeIn(file), episode);
    }

    void pluginTextShowsOnlyInItsPlaceholder()
    {
        discord::Choices c;
        discord::Scene s = scene();
        s.pluginText = QStringLiteral("Ep 5 of 12");
        QVERIFY(!text(discord::compose(c, s)).contains(QLatin1String("Ep 5")));
        c.ownText = true;
        c.details = QStringLiteral("Tracker: {plugin}");
        QCOMPARE(field(discord::compose(c, s), "details"), QStringLiteral("Tracker: Ep 5 of 12"));
    }

    void buttonsFollowTheirRows()
    {
        discord::Choices c;
        c.link = true;
        c.linkLabel = QStringLiteral("Our fansub team and all its many shows");
        c.linkUrl = QStringLiteral("http://example.org");
        QJsonArray buttons = discord::compose(c, scene()).value(QStringLiteral("buttons")).toArray();
        QCOMPARE(buttons.size(), 1);
        c.linkUrl = QStringLiteral("https://example.org/team");
        buttons = discord::compose(c, scene()).value(QStringLiteral("buttons")).toArray();
        QCOMPARE(buttons.size(), 2);
        QCOMPARE(buttons.at(1).toObject().value(QStringLiteral("label")).toString().size(), 32);
        QCOMPARE(buttons.at(1).toObject().value(QStringLiteral("url")).toString(), QStringLiteral("https://example.org/team"));
        c.getButton = c.link = false;
        QVERIFY(!discord::compose(c, scene()).contains(QStringLiteral("buttons")));
    }

    void homeHasNoSmallImage()
    {
        discord::Choices c;
        discord::Scene s;
        const QJsonObject a = discord::compose(c, s);
        QCOMPARE(field(a, "details"), QStringLiteral("In HikariSub"));
        QCOMPARE(field(a, "state"), QStringLiteral("Home"));
        QVERIFY(smallImage(a).isEmpty());
        c.modeIcon = false;
        QVERIFY(smallImage(discord::compose(c, scene())).isEmpty());
    }

    void idleShowsClearsOrKeeps()
    {
        discord::Choices c;
        discord::Scene s = scene();
        s.idle = true;
        const QJsonObject idle = discord::compose(c, s);
        QCOMPARE(field(idle, "details"), QStringLiteral("Idle"));
        QVERIFY(!idle.contains(QStringLiteral("state")));
        QVERIFY(!idle.contains(QStringLiteral("timestamps")));
        QVERIFY(smallImage(idle).isEmpty());
        c.idle = QStringLiteral("clear");
        QVERIFY(discord::compose(c, s).isEmpty());
        c.idle = QStringLiteral("keep");
        QCOMPARE(field(discord::compose(c, s), "state"), QStringLiteral("Timing"));
    }

    void aProjectHidesOrSetsItsText()
    {
        discord::Choices c;
        discord::Scene s = scene();
        s.project.kind = discord::ProjectChoice::Hide;
        QVERIFY(discord::compose(c, s).isEmpty());
        s.project = {discord::ProjectChoice::Own, QStringLiteral("Secret show"), QStringLiteral("Working on {show}"), {}};
        const QJsonObject a = discord::compose(c, s);
        QCOMPARE(field(a, "details"), QStringLiteral("Working on Secret show"));
        QCOMPARE(field(a, "state"), QStringLiteral("Timing"));
    }

    void activitiesShowInOrderAndCanBeUnticked()
    {
        discord::Choices c;
        c.activities = {QStringLiteral("watching"), QStringLiteral("effects"), QStringLiteral("styling"),
                        QStringLiteral("spelling"), QStringLiteral("reviewing"), QStringLiteral("exporting")};
        const auto going = [](std::initializer_list<discord::Activity> on) {
            return [on](discord::Activity a) { return std::ranges::find(on, a) != on.end(); };
        };
        using enum discord::Activity;
        QCOMPARE(discord::shownActivity(c, going({Exporting, Styling})), std::optional(Styling));
        QCOMPARE(discord::shownActivity(c, going({Agent})), std::nullopt);
        c.activities.removeAll(QStringLiteral("styling"));
        QCOMPARE(discord::shownActivity(c, going({Exporting, Styling})), std::optional(Exporting));
        discord::Scene s = scene();
        s.activity = Spelling;
        const QJsonObject a = discord::compose(c, s);
        QCOMPARE(field(a, "state"), QStringLiteral("Spell-checking"));
        QCOMPARE(smallImage(a), QStringLiteral("spelling"));
    }

    void theAgentIsOffByDefaultAndTheRestOn()
    {
        hikari::ui::SettingsStore settings;
        const discord::Choices c = discord::Choices::fromStore(settings);
        QCOMPARE(c.activities, (QStringList{QStringLiteral("watching"), QStringLiteral("effects"), QStringLiteral("styling"),
                                            QStringLiteral("spelling"), QStringLiteral("reviewing"), QStringLiteral("exporting")}));
        QVERIFY(c.mode && c.modeIcon && c.getButton && !c.fileName && !c.projectName && !c.ownText && !c.link);
        QCOMPARE(c.time, QStringLiteral("started"));
        QCOMPARE(c.idle, QStringLiteral("idle"));
        QCOMPARE(settings.value(QStringLiteral("privacy.discordIdleMinutes")).toInt(), 10);
    }

    void goesIdleWithoutInputAndBackOnInput()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        settings.set("privacy.discordIdleMinutes", 2);
        DiscordPresence::Config c = config(discord);
        c.minuteMs = 100;
        DiscordPresence presence(settings, c);
        presence.setTab(tab());
        QTRY_COMPARE(lastField(discord, "state"), QStringLiteral("Editing"));
        QTRY_COMPARE(lastField(discord, "details"), QStringLiteral("Idle"));
        presence.noteInput();
        QVERIFY(!presence.idle());
        QTRY_COMPARE(lastField(discord, "state"), QStringLiteral("Editing"));
    }

    void theBackgroundCountsOnlyWhenAsked()
    {
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        settings.set("privacy.discordIdleMinutes", 1);
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("1");
        c.candidates = {dir.filePath(QStringLiteral("none"))};
        c.minuteMs = 50;
        DiscordPresence presence(settings, c);
        presence.setInBackground(true);
        QTest::qWait(200);
        QVERIFY(!presence.idle());
        settings.set("privacy.discordIdleBackground", true);
        QTRY_VERIFY(presence.idle());
    }

    void timeFollowsItsChoice()
    {
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("1");
        c.candidates = {dir.filePath(QStringLiteral("none"))};
        DiscordPresence presence(settings, c);
        presence.setTab(tab());
        QVERIFY(presence.activity().contains(QStringLiteral("timestamps")));
        settings.set("privacy.discordTime", QStringLiteral("hidden"));
        QVERIFY(!presence.activity().contains(QStringLiteral("timestamps")));
        settings.set("privacy.discordTime", QStringLiteral("file"));
        QTest::qWait(1100);
        const qint64 opened = QDateTime::currentSecsSinceEpoch();
        presence.setTab(tab({}, 2));
        const qint64 start = presence.activity().value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger();
        QVERIFY(start >= opened);
    }

    void onlyThePickedPluginsTextShows()
    {
        hikari::ui::SettingsStore settings;
        settings.set("privacy.discordPresence", true);
        settings.set("privacy.discordText", QStringLiteral("own"));
        settings.set("privacy.discordDetails", QStringLiteral("Ep: {plugin}"));
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("1");
        c.candidates = {dir.filePath(QStringLiteral("none"))};
        DiscordPresence presence(settings, c);
        presence.setTab(tab());
        presence.setPluginText("tracker", "5 of 12");
        presence.setPluginText("other", "nope");
        QCOMPARE(field(presence.activity(), "details"), QStringLiteral("Ep:"));
        settings.set("privacy.discordPlugin", QStringLiteral("tracker"));
        QCOMPARE(field(presence.activity(), "details"), QStringLiteral("Ep: 5 of 12"));
        presence.keepPluginTexts({QStringLiteral("other")});
        QCOMPARE(field(presence.activity(), "details"), QStringLiteral("Ep:"));
        QVERIFY(!text(presence.activity()).contains(QLatin1String("nope")));
    }

    void thePreviewUsesStagedValues()
    {
        hikari::ui::SettingsStore settings;
        DiscordPresence::Config c;
        c.clientId = QStringLiteral("1");
        c.candidates = {dir.filePath(QStringLiteral("none"))};
        DiscordPresence presence(settings, c);
        presence.setTab(tab());
        const QVariantMap staged{{QStringLiteral("privacy.discordText"), QStringLiteral("own")},
                                 {QStringLiteral("privacy.discordDetails"), QStringLiteral("Typesetting OP")}};
        QCOMPARE(presence.preview(staged).value(QStringLiteral("details")).toString(), QStringLiteral("Typesetting OP"));
        QVERIFY(presence.activity().isEmpty());
    }

    void followsTheWorkspaceAndTheDocument()
    {
        FakeDiscord discord(dir);
        QVERIFY(discord.listen());
        QTemporaryDir home;
        hikari::app::Application::Options options;
        options.settingsFile = home.filePath(QStringLiteral("settings.toml"));
        options.playbackAudio = false;
        options.presence = config(discord);
        hikari::app::Application application(options);
        application.settingsStore()->set("privacy.discordPresence", true);
        application.settingsStore()->set("privacy.discordFileName", true);
        QTRY_COMPARE(discord.activities().size(), 1);
        QCOMPARE(discord.activities().last().value(QStringLiteral("state")).toString(), QStringLiteral("Home"));
        QVERIFY(smallImage(discord.activities().last()).isEmpty());
        application.workspaceLayout().setPreset(QStringLiteral("Typesetting"));
        const QString path = home.filePath(QStringLiteral("episode02.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                "MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line\n");
        f.close();
        QVERIFY(application.openFile(path));
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(),
                     QStringLiteral("Typesetting · episode02.ass"));
        application.presenceActivity(0, QStringLiteral("styling"), true);
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(),
                     QStringLiteral("Styling · episode02.ass"));
        QCOMPARE(smallImage(discord.activities().last()), QStringLiteral("styling"));
        application.presenceActivity(0, QStringLiteral("styling"), false);
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(),
                     QStringLiteral("Typesetting · episode02.ass"));
        application.endSession();
    }
};

QTEST_MAIN(DiscordPresenceTest)
#include "discord_presence_tests.moc"
