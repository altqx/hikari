#include "hikari/app/plugin_marketplace.h"

#include "fake_network.h"
#include "test_signer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hikari::app::MarketplaceFeed;
using hikari::app::PluginManager;
using hikari::app::PluginMarketplace;
using hikari::test::FakeNetwork;
namespace test = hikari::plugins::test;

namespace {

const QString kBase = QStringLiteral("https://marketplace.test/");

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(HIKARI_PLUGIN_FIXTURES "/") + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString authorKey()
{
    QFile file(QStringLiteral(HIKARI_MINISIGN_FIXTURES "/a.pub"));
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).split(QLatin1Char('\n')).value(1) : QString();
}

struct Version {
    QString number;
    QByteArray package;
    QString extra;
    QString url;
};

QString versionJson(const Version &v)
{
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(v.package, QCryptographicHash::Sha256).toHex());
    return QStringLiteral("{\"version\": \"%1\", \"api\": 1, \"tier\": \"restricted\", \"name\": \"Hello\", \"summary\": \"Says hello\","
                          " \"license\": \"MIT\", \"authors\": [\"Ana\"], \"permissions\": {\"network\": {\"hosts\": [\"api.example.com\"],"
                          " \"reason\": \"Looks words up\"}}, \"url\": \"%2\", \"sha256\": \"%3\", \"size\": %4,"
                          " \"published\": \"2026-11-01T10:00:00Z\", \"readme\": \"%5readme.md\", \"icon\": \"https://elsewhere.test/i.png\","
                          " \"screenshots\": [{\"url\": \"%5s.png\", \"caption\": \"Main\", \"width\": 2, \"height\": 2}]%6}")
        .arg(v.number, v.url.isEmpty() ? kBase + QStringLiteral("p/com.example.hello/") + v.number + QStringLiteral(".hikariplugin") : v.url,
             hash)
        .arg(v.package.size())
        .arg(kBase, v.extra);
}

}

class PluginMarketplaceTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    test::Signer m_market = test::signer(9);
    std::unique_ptr<FakeNetwork> m_network;
    std::unique_ptr<PluginManager> m_manager;
    std::unique_ptr<PluginMarketplace> m_marketplace;

    QString serialFile() const { return m_dir.filePath(QStringLiteral("user/marketplace.json")); }

    void make()
    {
        m_marketplace.reset();
        m_manager = std::make_unique<PluginManager>(PluginManager::Folders{
            m_dir.filePath(QStringLiteral("user/plugins")), m_dir.filePath(QStringLiteral("user/plugin-data")),
            m_dir.filePath(QStringLiteral("cache/plugins")), m_dir.filePath(QStringLiteral("user/plugins.json"))});
        m_marketplace = std::make_unique<PluginMarketplace>(*m_manager, nullptr, MarketplaceFeed{QUrl(kBase), {m_market.key}},
                                                            serialFile(), QStringLiteral("0.5.0"), m_network.get());
    }

    void publish(int serial, const QList<Version> &versions, const QString &signedComment = {})
    {
        QStringList listed;
        for (const Version &v : versions) {
            listed << versionJson(v);
            m_network->files.insert(kBase + QStringLiteral("p/com.example.hello/") + v.number + QStringLiteral(".hikariplugin"),
                                    v.package);
        }
        const QByteArray index =
            QStringLiteral("{\"format\": 1, \"serial\": %1, \"generated\": \"2026-11-02T10:00:00Z\", \"apiVersions\": {\"1\": \"0.1.0\"},"
                           " \"featured\": [], \"plugins\": [{\"id\": \"com.example.hello\", \"author\": \"ana\", \"authorName\": \"Ana Author\","
                           " \"repo\": \"https://github.com/ana/hello\", \"publicKey\": \"%2\", \"categories\": [\"Timing\"], \"versions\": [%3]}]}")
                .arg(serial)
                .arg(authorKey(), listed.join(QStringLiteral(", ")))
                .toUtf8();
        const std::string comment = signedComment.isEmpty()
                                        ? "file:index.json serial:" + std::to_string(serial) + " generated:2026-11-02T10:00:00Z"
                                        : signedComment.toStdString();
        const auto *data = reinterpret_cast<const std::byte *>(index.constData());
        m_network->files.insert(kBase + QStringLiteral("index.json"), index);
        m_network->files.insert(kBase + QStringLiteral("index.json.minisig"),
                                QByteArray::fromStdString(test::sign(m_market, {data, std::size_t(index.size())}, comment)));
    }

    bool refreshed()
    {
        m_marketplace->refresh();
        return QTest::qWaitFor([&] { return m_marketplace->status() != QLatin1String("loading"); });
    }

    QVariantMap listing() const
    {
        const QVariantList rows = m_marketplace->listings();
        return rows.size() == 1 ? rows.first().toMap() : QVariantMap();
    }

    QVariantMap downloaded(const QString &id)
    {
        QSignalSpy reviewed(m_marketplace.get(), &PluginMarketplace::reviewed);
        m_marketplace->download(id);
        if (reviewed.isEmpty() && !reviewed.wait(5000))
            return {};
        return reviewed.first().first().toMap();
    }

    void installFromFile(const char *name)
    {
        QTemporaryDir files;
        const QString path = files.filePath(QString::fromLatin1(name));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(fixture(name));
        out.close();
        QFile signature(path + QStringLiteral(".minisig"));
        QVERIFY(signature.open(QIODevice::WriteOnly));
        signature.write(fixture((std::string(name) + ".minisig").c_str()));
        signature.close();
        QVERIFY(m_manager->review(QUrl::fromLocalFile(path)).value(QStringLiteral("ok")).toBool());
        QCOMPARE(m_manager->install(false), QString());
    }

private slots:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path() + QStringLiteral("/user"));
        m_network = std::make_unique<FakeNetwork>();
        make();
    }

    void cleanup()
    {
        m_marketplace.reset();
        m_manager.reset();
        m_network.reset();
    }

    void aRefreshAsksForTheWholeListOnly()
    {
        installFromFile("hello-1.0.0.hikariplugin");
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        QCOMPARE(m_marketplace->status(), QStringLiteral("ready"));
        QCOMPARE(m_network->requests.size(), 2);
        for (const QNetworkRequest &request : std::as_const(m_network->requests)) {
            QVERIFY(!request.url().hasQuery());
            QCOMPARE(request.rawHeaderList().size(), 1);
            QCOMPARE(request.rawHeader("User-Agent"), QByteArray("HikariSub/0.5.0"));
            QCOMPARE(request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
        }
        QCOMPARE(m_network->requests.at(0).url(), QUrl(kBase + QStringLiteral("index.json")));
        QCOMPARE(m_network->requests.at(1).url(), QUrl(kBase + QStringLiteral("index.json.minisig")));
    }

    void startWithoutTheSettingMakesNoRequest()
    {
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        m_marketplace->start();
        QTest::qWait(50);
        QVERIFY(m_network->requests.isEmpty());
        PluginMarketplace none(*m_manager, nullptr, MarketplaceFeed{}, serialFile(), QStringLiteral("0.5.0"), m_network.get());
        QVERIFY(!none.available());
        none.refresh();
        QTest::qWait(50);
        QVERIFY(m_network->requests.isEmpty());
    }

    void listingsShowTheNewestVersionThisHikariSubRuns()
    {
        const QString changelog = QStringLiteral(", \"changelog\": \"Faster\"");
        publish(3, {{QStringLiteral("2.0.0"), fixture("hello-2.0.0-other-key.hikariplugin"), {}, {}},
                    {QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), changelog, {}}});
        QVERIFY(refreshed());
        QVariantMap row = listing();
        QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("com.example.hello"));
        QCOMPARE(row.value(QStringLiteral("version")).toString(), QStringLiteral("2.0.0"));
        QCOMPARE(row.value(QStringLiteral("action")).toString(), QStringLiteral("install"));
        QCOMPARE(row.value(QStringLiteral("authorName")).toString(), QStringLiteral("Ana Author"));
        QCOMPARE(row.value(QStringLiteral("needs")).toString(), QStringLiteral("0.1.0"));
        QCOMPARE(row.value(QStringLiteral("published")).toString(), QStringLiteral("2026-11-01"));
        QCOMPARE(row.value(QStringLiteral("permissions")).toStringList().size(), 1);
        QVERIFY(row.value(QStringLiteral("permissions")).toStringList().first().contains(QStringLiteral("api.example.com")));
        QCOMPARE(row.value(QStringLiteral("icon")).toUrl(), QUrl());
        QCOMPARE(row.value(QStringLiteral("screenshots")).toList().size(), 1);
        QCOMPARE(m_marketplace->categories(), QStringList{QStringLiteral("Timing")});
    }

    void anIndexNotSignedForThisListIsIgnored()
    {
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}},
                QStringLiteral("file:index.json serial:4 generated:2026-11-02T10:00:00Z"));
        QVERIFY(refreshed());
        QCOMPARE(m_marketplace->status(), QStringLiteral("failed"));
        QVERIFY(m_marketplace->problem().contains(QStringLiteral("another file")));
        QVERIFY(m_marketplace->listings().isEmpty());

        m_network->files.insert(kBase + QStringLiteral("index.json.minisig"), QByteArray("not a signature"));
        QVERIFY(refreshed());
        QCOMPARE(m_marketplace->status(), QStringLiteral("failed"));

        m_network->files.remove(kBase + QStringLiteral("index.json"));
        QVERIFY(refreshed());
        QVERIFY(m_marketplace->problem().contains(QStringLiteral("Not found")));
    }

    void anOlderListIsIgnoredAndTheHighestSerialIsKept()
    {
        publish(5, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        QCOMPARE(m_marketplace->status(), QStringLiteral("ready"));
        QCOMPARE(hikari::plugins::loadHighestSerial(serialFile().toStdU16String()), 5);

        make();
        publish(4, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        QCOMPARE(m_marketplace->status(), QStringLiteral("failed"));
        QVERIFY(m_marketplace->problem().contains(QStringLiteral("older list")));
        QCOMPARE(hikari::plugins::loadHighestSerial(serialFile().toStdU16String()), 5);
    }

    void aListedPackageInstallsFromTheMarketplace()
    {
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        const QVariantMap review = downloaded(QStringLiteral("com.example.hello"));
        QVERIFY2(review.value(QStringLiteral("ok")).toBool(), qPrintable(review.value(QStringLiteral("message")).toString()));
        QVERIFY(review.value(QStringLiteral("marketplace")).toBool());
        QCOMPARE(review.value(QStringLiteral("signedBy")).toString(), QStringLiteral("Ana Author"));
        QCOMPARE(review.value(QStringLiteral("keyId")).toString(), QStringLiteral("EFCDAB8967452301"));
        QCOMPARE(m_network->requests.last().url(), QUrl(kBase + QStringLiteral("p/com.example.hello/1.1.0.hikariplugin")));
        QCOMPARE(m_manager->install(false), QString());
        QCOMPARE(m_manager->plugins().first().toMap().value(QStringLiteral("source")).toString(), QStringLiteral("marketplace"));
        QCOMPARE(listing().value(QStringLiteral("action")).toString(), QStringLiteral("installed"));
        QVERIFY(m_marketplace->offers().isEmpty());
    }

    void aDownloadThatIsNotTheListedPackageIsRefused()
    {
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        m_network->files.insert(kBase + QStringLiteral("p/com.example.hello/1.1.0.hikariplugin"), fixture("hello-1.0.0.hikariplugin"));
        QVariantMap review = downloaded(QStringLiteral("com.example.hello"));
        QVERIFY(!review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("message")).toString().contains(QStringLiteral("marketplace lists")));

        publish(4, {{QStringLiteral("1.1.0"), fixture("hello-1.0.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        review = downloaded(QStringLiteral("com.example.hello"));
        QVERIFY(!review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("message")).toString().contains(QStringLiteral("version 1.0.0")));
        QVERIFY(m_manager->plugins().isEmpty());
    }

    void aDownloadOutsideTheMarketplaceIsNotFetched()
    {
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {},
                     QStringLiteral("https://elsewhere.test/hello.hikariplugin")}});
        QVERIFY(refreshed());
        const qsizetype before = m_network->requests.size();
        const QVariantMap review = downloaded(QStringLiteral("com.example.hello"));
        QVERIFY(!review.value(QStringLiteral("ok")).toBool());
        QCOMPARE(m_network->requests.size(), before);
    }

    void aDownloadLargerThanListedIsCut()
    {
        QByteArray package = fixture("hello-1.1.0.hikariplugin");
        publish(3, {{QStringLiteral("1.1.0"), package, {}, {}}});
        m_network->files.insert(kBase + QStringLiteral("p/com.example.hello/1.1.0.hikariplugin"), package + QByteArray(64, 'x'));
        QVERIFY(refreshed());
        const QVariantMap review = downloaded(QStringLiteral("com.example.hello"));
        QVERIFY(!review.value(QStringLiteral("ok")).toBool());
        QVERIFY(review.value(QStringLiteral("message")).toString().contains(QStringLiteral("Cancelled")));
    }

    void aFileInstallTheMarketplaceListsOffersTheMove()
    {
        installFromFile("hello-1.0.0.hikariplugin");
        publish(3, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        QVariantMap offer = m_marketplace->offers().value(QStringLiteral("com.example.hello")).toMap();
        QVERIFY(offer.value(QStringLiteral("move")).toBool());
        QCOMPARE(offer.value(QStringLiteral("update")).toString(), QString());
        QCOMPARE(m_marketplace->updates(), 0);
        QCOMPARE(listing().value(QStringLiteral("action")).toString(), QStringLiteral("update"));

        QVERIFY(downloaded(QStringLiteral("com.example.hello")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(m_manager->install(false), QString());
        QVERIFY(m_marketplace->offers().isEmpty());
    }

    void aMarketplaceInstallCountsANewerListedVersion()
    {
        publish(3, {{QStringLiteral("1.0.0"), fixture("hello-1.0.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        QVERIFY(downloaded(QStringLiteral("com.example.hello")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(m_manager->install(false), QString());

        publish(4, {{QStringLiteral("1.1.0"), fixture("hello-1.1.0.hikariplugin"), {}, {}},
                    {QStringLiteral("1.0.0"), fixture("hello-1.0.0.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        const QVariantMap offer = m_marketplace->offers().value(QStringLiteral("com.example.hello")).toMap();
        QCOMPARE(offer.value(QStringLiteral("update")).toString(), QStringLiteral("1.1.0"));
        QVERIFY(!offer.value(QStringLiteral("move")).toBool());
        QCOMPARE(m_marketplace->updates(), 1);
    }

    void aSideloadSignedByAnotherKeyIsNotOfferedTheMove()
    {
        installFromFile("hello-2.0.0-other-key.hikariplugin");
        publish(3, {{QStringLiteral("2.0.0"), fixture("hello-2.0.0-other-key.hikariplugin"), {}, {}}});
        QVERIFY(refreshed());
        QVERIFY(m_marketplace->offers().isEmpty());
    }
};

QTEST_GUILESS_MAIN(PluginMarketplaceTest)
#include "plugin_marketplace_tests.moc"
