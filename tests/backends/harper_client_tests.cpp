#include "hikari/backends/harper_client.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using hikari::backends::GrammarFinding;
using hikari::backends::HarperClient;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 30'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

class Checker {
public:
    Checker() : client(QStringLiteral(HIKARI_HARPER_LS), dir.path())
    {
        QObject::connect(&client, &HarperClient::checked, [this](quint64 ticket, const QList<GrammarFinding> &f) {
            results[ticket] = f;
        });
        QObject::connect(&client, &HarperClient::failed, [this](const QString &message) { failures << message; });
    }
    std::optional<QList<GrammarFinding>> check(const QString &text)
    {
        const quint64 ticket = client.check(text);
        if (!waitFor([&] { return results.contains(ticket) || !failures.isEmpty(); }))
            return std::nullopt;
        if (!results.contains(ticket))
            return std::nullopt;
        return results[ticket];
    }
    static const GrammarFinding *find(const QList<GrammarFinding> &findings, const QString &text, const QString &on)
    {
        for (const auto &f : findings)
            if (text.mid(f.start, f.end - f.start) == on)
                return &f;
        return nullptr;
    }
    static QJsonObject settings(QJsonObject linters, const QString &dialect = QStringLiteral("American"))
    {
        return {{QStringLiteral("dialect"), dialect}, {QStringLiteral("linters"), linters}};
    }

    QTemporaryDir dir;
    HarperClient client;
    std::map<quint64, QList<GrammarFinding>> results;
    QStringList failures;
};

}

class HarperClientTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "harper_client_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }
};

TEST_F(HarperClientTest, StartsOnTheFirstCheckAndFindsGrammarWithReplacements)
{
    Checker c;
    EXPECT_FALSE(c.client.running());
    EXPECT_EQ(c.client.starts(), 0);
    const QString text = QStringLiteral("Their going to the store, I has a apple.");
    const auto findings = c.check(text);
    ASSERT_TRUE(findings) << c.failures.join(u'\n').toStdString();
    EXPECT_TRUE(c.client.running());
    EXPECT_EQ(c.client.starts(), 1);

    const auto *article = Checker::find(*findings, text, QStringLiteral("a"));
    ASSERT_NE(article, nullptr);
    EXPECT_EQ(article->rule, QStringLiteral("AnA"));
    EXPECT_FALSE(article->message.isEmpty());
    EXPECT_EQ(article->suggestions, QStringList{QStringLiteral("an")});
    EXPECT_FALSE(article->word);
    const auto *their = Checker::find(*findings, text, QStringLiteral("Their"));
    ASSERT_NE(their, nullptr);
    EXPECT_EQ(their->rule, QStringLiteral("TheirToTheyre"));
    EXPECT_EQ(their->suggestions, QStringList{QStringLiteral("They're")});
    ASSERT_NE(Checker::find(*findings, text, QStringLiteral("has")), nullptr);

    const auto clean = c.check(QStringLiteral("This sentence is fine."));
    ASSERT_TRUE(clean);
    EXPECT_TRUE(clean->isEmpty());
    EXPECT_EQ(c.client.starts(), 1);
}

TEST_F(HarperClientTest, PositionsAreUtf16AndSpellingFindingsAreWords)
{
    Checker c;
    const QString text = QStringLiteral("\U0001F600 I like the color of tomorow.");
    const auto findings = c.check(text);
    ASSERT_TRUE(findings);
    const auto *word = Checker::find(*findings, text, QStringLiteral("tomorow"));
    ASSERT_NE(word, nullptr);
    EXPECT_EQ(word->rule, QStringLiteral("SpellCheck"));
    EXPECT_TRUE(word->word);
    EXPECT_TRUE(word->suggestions.contains(QStringLiteral("tomorrow")));
}

TEST_F(HarperClientTest, SettingsSwitchRulesAndTheDialectAndRestart)
{
    Checker c;
    const QString text = QStringLiteral("I like the color of tomorow.");
    c.client.setSettings(Checker::settings({{QStringLiteral("SpellCheck"), false}}));
    auto findings = c.check(text);
    ASSERT_TRUE(findings);
    EXPECT_EQ(Checker::find(*findings, text, QStringLiteral("tomorow")), nullptr);

    c.client.setSettings(Checker::settings({}, QStringLiteral("British")));
    EXPECT_FALSE(c.client.running());
    findings = c.check(text);
    ASSERT_TRUE(findings);
    EXPECT_EQ(c.client.starts(), 2);
    const auto *colour = Checker::find(*findings, text, QStringLiteral("color"));
    ASSERT_NE(colour, nullptr);
    EXPECT_TRUE(colour->suggestions.contains(QStringLiteral("colour")));
}

TEST_F(HarperClientTest, WordsInTheUserDictionaryAreAccepted)
{
    Checker c;
    const QString dictionary = c.dir.filePath(QStringLiteral("harper-words.txt"));
    QFile file(dictionary);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("Hikari\ntomorow\n");
    file.close();
    QJsonObject settings = Checker::settings({});
    settings.insert(QStringLiteral("userDictPath"), dictionary);
    c.client.setSettings(settings);
    const QString text = QStringLiteral("Hikari will be here tomorow.");
    const auto findings = c.check(text);
    ASSERT_TRUE(findings);
    EXPECT_EQ(Checker::find(*findings, text, QStringLiteral("tomorow")), nullptr);
    EXPECT_EQ(Checker::find(*findings, text, QStringLiteral("Hikari")), nullptr);
}

TEST_F(HarperClientTest, CancelledChecksAreDroppedAndStopEndsTheProcess)
{
    Checker c;
    const quint64 first = c.client.check(QStringLiteral("I has a apple."));
    c.client.cancel(first);
    const auto second = c.check(QStringLiteral("This is is a test."));
    ASSERT_TRUE(second);
    EXPECT_FALSE(c.results.contains(first));
    c.client.stop();
    EXPECT_FALSE(c.client.running());
    QCoreApplication::processEvents();
    EXPECT_TRUE(c.failures.isEmpty());
}

TEST_F(HarperClientTest, ChecksInFlightTogetherEachGetTheirFindings)
{
    Checker c;
    std::vector<std::pair<quint64, QString>> sent;
    for (int i = 0; i < 24; ++i) {
        const QString text = i % 3 == 0 ? QStringLiteral("I has a apple.") : QStringLiteral("This line is fine.");
        sent.emplace_back(c.client.check(text), text);
    }
    ASSERT_TRUE(waitFor([&] { return c.results.size() == sent.size() || !c.failures.isEmpty(); }))
        << c.results.size() << " of " << sent.size();
    for (const auto &[ticket, text] : sent) {
        ASSERT_TRUE(c.results.contains(ticket));
        EXPECT_EQ(Checker::find(c.results[ticket], text, QStringLiteral("a")) != nullptr,
                  text.startsWith(QLatin1String("I has")))
            << ticket;
    }
    EXPECT_EQ(c.client.starts(), 1);
}

// harper-ls 3.0.0 deadlocked when two of its texts were opened together.
TEST_F(HarperClientTest, ChecksSentWhileOthersRunAllEnd)
{
    Checker c;
    int sent = 0;
    const auto send = [&] {
        c.client.check(sent++ % 5 == 0 ? QStringLiteral("I has a apple.") : QStringLiteral("This line is fine."));
    };
    QObject::connect(&c.client, &HarperClient::checked, [&] {
        if (sent < 80)
            send();
    });
    for (int i = 0; i < 4; ++i)
        send();
    EXPECT_TRUE(waitFor([&] { return c.results.size() == 80 || !c.failures.isEmpty(); }))
        << c.results.size() << " of 80";
    EXPECT_TRUE(c.failures.isEmpty());
}

TEST_F(HarperClientTest, ChecksCancelledTogetherLetTheNextEnd)
{
    Checker c;
    std::vector<quint64> waiting;
    std::set<quint64> cancelled;
    int ended = 0;
    const auto send = [&] {
        for (int i = 0; i < 4; ++i)
            waiting.push_back(c.client.check(i % 2 ? QStringLiteral("I has a apple.") : QStringLiteral("This line is fine.")));
    };
    QObject::connect(&c.client, &HarperClient::checked, [&](quint64 ticket) {
        std::erase(waiting, ticket);
        if (++ended >= 20)
            return;
        QTimer::singleShot(ended % 4 * 5, [&] {
            for (const quint64 t : std::exchange(waiting, {})) {
                cancelled.insert(t);
                c.client.cancel(t);
            }
            send();
        });
    });
    send();
    EXPECT_TRUE(waitFor([&] { return ended >= 20 || !c.failures.isEmpty(); })) << ended << " of 20";
    EXPECT_TRUE(c.failures.isEmpty());
    for (const quint64 ticket : cancelled)
        EXPECT_FALSE(c.results.contains(ticket)) << ticket;
}

TEST_F(HarperClientTest, AMissingProgramFailsTheCheck)
{
    HarperClient client(QStringLiteral("/no/such/harper-ls"), QString());
    QStringList failures;
    QObject::connect(&client, &HarperClient::failed, [&](const QString &m) { failures << m; });
    client.check(QStringLiteral("text"));
    EXPECT_TRUE(waitFor([&] { return !failures.isEmpty(); }));
    EXPECT_FALSE(client.running());
}
