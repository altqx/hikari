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
        presence.setFileName(QStringLiteral("episode01.ass"));
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
                                         {QStringLiteral("url"), QStringLiteral("https://hikarisub.altqx.com")}}}));
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
        presence.setFileName(QStringLiteral("episode01.ass"));
        QTRY_VERIFY(!discord.activities().isEmpty());
        QVERIFY(!text(discord.activities().last()).contains(QLatin1String("episode01")));
        settings.set("privacy.discordFileName", true);
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(),
                     QStringLiteral("Karaoke · episode01.ass"));
        presence.setFileName(QString());
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(), QStringLiteral("Karaoke"));
        presence.setFileName(QString(300, QLatin1Char('x')));
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
        QCOMPARE(discord.activities().last().value(QStringLiteral("state")).toString(), QStringLiteral("Editing"));
        application.workspaceLayout().setPreset(QStringLiteral("Typesetting"));
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(), QStringLiteral("Typesetting"));
        const QString path = home.filePath(QStringLiteral("episode02.ass"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[Script Info]\nScriptType: v4.00+\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                "MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,line\n");
        f.close();
        QVERIFY(application.openFile(path));
        QTRY_COMPARE(discord.activities().last().value(QStringLiteral("state")).toString(),
                     QStringLiteral("Typesetting · episode02.ass"));
        application.endSession();
    }
};

QTEST_MAIN(DiscordPresenceTest)
#include "discord_presence_tests.moc"
