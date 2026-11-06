#include "hikari/backends/ai_channel.h"
#include "hikari/backends/ai_sidecar_updater.h"
#include "minisign_signer.h"

#include "local_release.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <functional>
#include <optional>

using namespace hikari::backends::ai;
using hikari::test::FakeChannel;
using Release = hikari::test::FakeChannel::Release;

namespace {

const QString kRuntime = QStringLiteral("1.4.4-0123abcd");
const SidecarCompat kCompat{1, 1};

bool waitFor(const std::function<bool()> &done, int ms = 10'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

struct Outcome {
    std::optional<SidecarInstall> installed;
    std::optional<QString> staged;
    std::optional<qint64> described;
    std::optional<SidecarDownloadError> error;
    bool checked = false;
    QString detail;
};

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

class AiSidecarUpdaterTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "hikari_backends_ai_sidecar_updater_tests";
        static char *argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }

    void SetUp() override { channel.addRuntime(kRuntime, executable); }

    SidecarSource source() const { return SidecarSource::local(server.base(), {signer.publicKey()}); }

    Outcome run(const std::function<void(AiSidecarUpdater &)> &start, SidecarCompat compat = kCompat)
    {
        AiSidecarUpdater updater(root(), compat, source());
        Outcome outcome;
        QObject::connect(&updater, &AiSidecarUpdater::installed, [&](const SidecarInstall &i) { outcome.installed = i; });
        QObject::connect(&updater, &AiSidecarUpdater::downloaded, [&](const QString &v) { outcome.staged = v; });
        QObject::connect(&updater, &AiSidecarUpdater::described, [&](qint64 bytes) { outcome.described = bytes; });
        QObject::connect(&updater, &AiSidecarUpdater::checked, [&] { outcome.checked = true; });
        QObject::connect(&updater, &AiSidecarUpdater::failed, [&](SidecarDownloadError e, const QString &detail) {
            outcome.error = e;
            outcome.detail = detail;
        });
        start(updater);
        waitFor([&] { return outcome.installed || outcome.staged || outcome.described || outcome.error ||
                             (outcome.checked && !updater.busy()); });
        QCoreApplication::processEvents();
        return outcome;
    }
    Outcome install(SidecarCompat compat = kCompat)
    {
        return run([](AiSidecarUpdater &u) { u.install(); }, compat);
    }
    Outcome stage() { return run([](AiSidecarUpdater &u) { u.check(true); }); }
    AiSidecarUpdater reopened() const { return AiSidecarUpdater(root(), kCompat, source()); }

    QString root() const { return dir.filePath(QStringLiteral("ai/sidecar")); }
    QStringList entries(const QString &sub) const
    {
        return QDir(root() + QLatin1Char('/') + sub).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    }

    QTemporaryDir dir;
    hikari::test::LocalRelease server;
    hikari::test::MinisignSigner signer;
    FakeChannel channel{server};
    const QByteArray executable = QByteArray("#!/bin/sh\necho sidecar\n").repeated(2000);
};

TEST_F(AiSidecarUpdaterTest, InstallsTheNewestRuntimeAndBundleAndRemovesTheOldLayout)
{
    QDir().mkpath(root() + QStringLiteral("/0.0.9"));
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.addRelease({QStringLiteral("1.2.0")});
    channel.publish(signer);
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.installed) << outcome.detail.toStdString();
    EXPECT_EQ(outcome.installed->version, QStringLiteral("1.2.0"));
    EXPECT_EQ(outcome.installed->packages.at(QStringLiteral("openai")), QStringLiteral("7.45.0"));
    EXPECT_EQ(readAll(outcome.installed->runtime), executable);
    EXPECT_TRUE(QFile::permissions(outcome.installed->runtime) & QFile::ExeOwner);
    EXPECT_EQ(readAll(outcome.installed->bundle), QByteArray("--mode ok"));
    EXPECT_EQ(entries(QStringLiteral("bundle/1.2.0")),
              (QStringList{QStringLiteral("hikari-ai.js"), QStringLiteral("hikari-ai.js.jsc"),
                           QStringLiteral("release.json")}));
    EXPECT_FALSE(QDir(root() + QStringLiteral("/0.0.9")).exists());
    EXPECT_TRUE(entries(QStringLiteral("download")).isEmpty());
    const auto current = reopened().current();
    ASSERT_TRUE(current);
    EXPECT_EQ(current->bundle, outcome.installed->bundle);
}

TEST_F(AiSidecarUpdaterTest, DescribesTheFirstDownloadsSize)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    const Outcome outcome = run([](AiSidecarUpdater &u) { u.describe(); });
    ASSERT_TRUE(outcome.described);
    const qint64 runtime = server.files[channel.path(runtimeAssetName(kRuntime, sidecarPlatform()))].size();
    const qint64 bundle = server.files[channel.path(bundleAssetName(QStringLiteral("1.1.0")))].size();
    EXPECT_EQ(*outcome.described, runtime + bundle);
    EXPECT_FALSE(reopened().current());
}

TEST_F(AiSidecarUpdaterTest, RefusesAManifestSignedForAnotherVersion)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer, {}, QStringLiteral("1.0.0"));
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Manifest);
    EXPECT_FALSE(reopened().current());
}

TEST_F(AiSidecarUpdaterTest, RefusesAManifestSignedWithAnotherKey)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(hikari::test::MinisignSigner());
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Manifest);
}

TEST_F(AiSidecarUpdaterTest, RefusesAManifestWithoutASignature)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    server.files.erase(channel.path(QString::fromLatin1(kChannelSignature)));
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Manifest);
}

TEST_F(AiSidecarUpdaterTest, RefusesAMalformedManifestEvenWhenSigned)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publishBytes(signer, "{\"schema\": 2}", QStringLiteral("1.1.0"));
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Manifest);
}

TEST_F(AiSidecarUpdaterTest, ABundleWithTheWrongHashIsDeletedAndNeverInstalled)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    QByteArray &bundle = server.files[channel.path(bundleAssetName(QStringLiteral("1.1.0")))];
    bundle[bundle.size() - 1] = char(bundle.back() ^ 1);
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Mismatch);
    EXPECT_TRUE(entries(QStringLiteral("bundle")).isEmpty());
    EXPECT_TRUE(entries(QStringLiteral("download")).isEmpty());
    EXPECT_FALSE(reopened().current());
}

TEST_F(AiSidecarUpdaterTest, ARuntimeLongerThanTheManifestSaysIsDeleted)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    server.files[channel.path(runtimeAssetName(kRuntime, sidecarPlatform()))] += QByteArray(4096, 'x');
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_TRUE(*outcome.error == SidecarDownloadError::Mismatch || *outcome.error == SidecarDownloadError::Network);
    EXPECT_TRUE(entries(QStringLiteral("runtime")).isEmpty());
    EXPECT_TRUE(entries(QStringLiteral("download")).isEmpty());
}

TEST_F(AiSidecarUpdaterTest, UnpacksTheProviderSdkFilesNextToTheBundle)
{
    channel.addRelease({.version = QStringLiteral("1.1.0"),
                        .extra = {{QStringLiteral("hikari-ai-openai.js"), "openai"},
                                  {QStringLiteral("hikari-ai-openai.js.jsc"), "bytecode"}}});
    channel.publish(signer);
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.installed) << outcome.detail.toStdString();
    EXPECT_EQ(readAll(QFileInfo(outcome.installed->bundle).dir().filePath(QStringLiteral("hikari-ai-openai.js"))),
              QByteArray("openai"));
    EXPECT_EQ(entries(QStringLiteral("bundle/1.1.0")).size(), 5);
}

TEST_F(AiSidecarUpdaterTest, ABundleHoldingAnyOtherFileIsNeverUnpacked)
{
    channel.addRelease({.version = QStringLiteral("1.1.0"), .extra = {{QStringLiteral("../escape.js"), "x"}}});
    channel.publish(signer);
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Unpack);
    EXPECT_TRUE(entries(QStringLiteral("bundle")).isEmpty());
    EXPECT_FALSE(QFile::exists(root() + QStringLiteral("/bundle/escape.js")));
}

TEST_F(AiSidecarUpdaterTest, PicksTheNewestReleaseThisAppRuns)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.addRelease({.version = QStringLiteral("1.2.0"), .requiredApi = 2});
    channel.addRelease({.version = QStringLiteral("2.0.0"), .protocol = 2});
    channel.publish(signer);
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.installed);
    EXPECT_EQ(outcome.installed->version, QStringLiteral("1.1.0"));
}

TEST_F(AiSidecarUpdaterTest, OffersNothingWhenNoReleaseRunsHere)
{
    channel.addRelease({.version = QStringLiteral("2.0.0"), .protocol = 2});
    channel.publish(signer);
    const Outcome outcome = install();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::NoAsset);
}

TEST_F(AiSidecarUpdaterTest, StagesANewerReleaseAndSwitchesKeepingThePreviousOne)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    ASSERT_TRUE(install().installed);
    channel.addRelease({.version = QStringLiteral("1.2.0"), .bundle = "--mode slow"});
    channel.publish(signer);
    const int runtimeFetches = server.fetches(".gz");
    const Outcome outcome = stage();
    ASSERT_TRUE(outcome.staged) << outcome.detail.toStdString();
    EXPECT_EQ(*outcome.staged, QStringLiteral("1.2.0"));
    EXPECT_EQ(server.fetches(".gz"), runtimeFetches);

    AiSidecarUpdater updater = reopened();
    EXPECT_EQ(updater.current()->version, QStringLiteral("1.1.0"));
    EXPECT_EQ(updater.staged()->version, QStringLiteral("1.2.0"));
    EXPECT_EQ(updater.available(), QStringLiteral("1.2.0"));
    ASSERT_TRUE(updater.switchToStaged());
    EXPECT_EQ(updater.current()->version, QStringLiteral("1.2.0"));
    EXPECT_EQ(updater.previous(), QStringLiteral("1.1.0"));
    EXPECT_FALSE(updater.staged());
    EXPECT_TRUE(updater.available().isEmpty());
    EXPECT_FALSE(updater.switchToStaged());
}

TEST_F(AiSidecarUpdaterTest, KeepsOnlyTheCurrentAndPreviousBundles)
{
    for (const char *version : {"1.1.0", "1.2.0", "1.3.0"}) {
        channel.addRelease({QString::fromLatin1(version)});
        channel.publish(signer);
        ASSERT_TRUE(stage().staged);
        ASSERT_TRUE(reopened().switchToStaged());
    }
    EXPECT_EQ(entries(QStringLiteral("bundle")), (QStringList{QStringLiteral("1.2.0"), QStringLiteral("1.3.0")}));
    EXPECT_EQ(entries(QStringLiteral("runtime")), QStringList{kRuntime});
}

TEST_F(AiSidecarUpdaterTest, RejectingTheCurrentVersionReturnsToThePreviousAndSkipsIt)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    ASSERT_TRUE(install().installed);
    channel.addRelease({QStringLiteral("1.2.0")});
    channel.publish(signer);
    ASSERT_TRUE(stage().staged);
    ASSERT_TRUE(reopened().switchToStaged());

    reopened().reject(QStringLiteral("1.2.0"));
    AiSidecarUpdater updater = reopened();
    EXPECT_EQ(updater.current()->version, QStringLiteral("1.1.0"));
    EXPECT_TRUE(updater.previous().isEmpty());
    EXPECT_EQ(entries(QStringLiteral("bundle")), QStringList{QStringLiteral("1.1.0")});

    const Outcome again = stage();
    EXPECT_TRUE(again.checked);
    EXPECT_FALSE(again.staged);
    EXPECT_FALSE(again.error);
    EXPECT_TRUE(reopened().available().isEmpty());
}

TEST_F(AiSidecarUpdaterTest, ABlockedVersionStopsRunningAndThePreviousOneTakesOver)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    ASSERT_TRUE(install().installed);
    channel.addRelease({QStringLiteral("1.2.0")});
    channel.publish(signer);
    ASSERT_TRUE(stage().staged);
    ASSERT_TRUE(reopened().switchToStaged());

    channel.publish(signer, {QStringLiteral("1.2.0")});
    const Outcome outcome = run([](AiSidecarUpdater &u) { u.check(false); });
    EXPECT_TRUE(outcome.checked);
    EXPECT_EQ(reopened().current()->version, QStringLiteral("1.1.0"));
}

TEST_F(AiSidecarUpdaterTest, RefusesAManifestOlderThanOneAlreadySeen)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.addRelease({QStringLiteral("1.2.0")});
    channel.publish(signer);
    ASSERT_TRUE(install().installed);

    FakeChannel replay(server);
    replay.addRuntime(kRuntime, executable);
    replay.addRelease({QStringLiteral("1.1.0")});
    replay.publish(signer);
    const Outcome outcome = stage();
    ASSERT_TRUE(outcome.error);
    EXPECT_EQ(*outcome.error, SidecarDownloadError::Manifest);
    EXPECT_EQ(reopened().current()->version, QStringLiteral("1.2.0"));
}

TEST_F(AiSidecarUpdaterTest, AnUnchangedManifestIsNotDownloadedAgain)
{
    channel.addRelease({QStringLiteral("1.1.0")});
    channel.publish(signer);
    ASSERT_TRUE(install().installed);
    const int signatures = server.fetches(".minisig");
    const Outcome outcome = run([](AiSidecarUpdater &u) { u.check(false); });
    EXPECT_TRUE(outcome.checked);
    EXPECT_FALSE(outcome.error) << outcome.detail.toStdString();
    EXPECT_EQ(server.notModified, 1);
    EXPECT_EQ(server.fetches(".minisig"), signatures);
    EXPECT_TRUE(reopened().lastCheck().isValid());
}

TEST(AiChannel, ParsesTheManifestNewestFirst)
{
    hikari::test::LocalRelease server;
    FakeChannel channel(server);
    channel.addRuntime(kRuntime, "runtime");
    channel.addRelease({QStringLiteral("1.10.0")});
    channel.addRelease({QStringLiteral("1.9.0")});
    const auto parsed = parseChannel(channel.manifest(QStringLiteral("1.10.0"), {QStringLiteral("1.9.0")}));
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->releases.size(), 2u);
    EXPECT_EQ(parsed->releases[0].version, QStringLiteral("1.10.0"));
    EXPECT_EQ(parsed->blocked, QStringList{QStringLiteral("1.9.0")});
    EXPECT_TRUE(parsed->runtimeAsset(kRuntime, sidecarPlatform()));
    EXPECT_FALSE(parseChannel(channel.manifest(QStringLiteral("1.9.0"))));
}

TEST(AiChannel, RefusesAssetsUnderTheWrongName)
{
    hikari::test::LocalRelease server;
    FakeChannel channel(server);
    channel.addRuntime(kRuntime, "runtime");
    channel.addRelease({QStringLiteral("1.1.0")});
    QByteArray manifest = channel.manifest(QStringLiteral("1.1.0"));
    EXPECT_TRUE(parseChannel(manifest));
    EXPECT_FALSE(parseChannel(QByteArray(manifest).replace("hikari-ai-1.1.0.zip", "../hikari-ai-1.1.0.zip")));
    EXPECT_FALSE(parseChannel(QByteArray(manifest).replace("\"runtime\": \"1.4.4-0123abcd\"",
                                                           "\"runtime\": \"../1.4.4\"")));
}

TEST(AiChannel, ChoosesByProtocolApiBlockedAndSkipped)
{
    SidecarRelease newest{QStringLiteral("1.3.0"), 1, 2, kRuntime, {}, {}};
    SidecarRelease middle{QStringLiteral("1.2.0"), 1, 1, kRuntime, {}, {}};
    SidecarRelease oldest{QStringLiteral("1.1.0"), 1, 1, kRuntime, {}, {}};
    Channel channel{QStringLiteral("1.3.0"), {{kRuntime, {{sidecarPlatform(), {}}}}}, {newest, middle, oldest}, {}};
    EXPECT_EQ(chooseRelease(channel, {1, 2}, {}, sidecarPlatform())->version, QStringLiteral("1.3.0"));
    EXPECT_EQ(chooseRelease(channel, {1, 1}, {}, sidecarPlatform())->version, QStringLiteral("1.2.0"));
    EXPECT_EQ(chooseRelease(channel, {1, 1}, {QStringLiteral("1.2.0")}, sidecarPlatform())->version,
              QStringLiteral("1.1.0"));
    channel.blocked = {QStringLiteral("1.1.0")};
    EXPECT_FALSE(chooseRelease(channel, {1, 1}, {QStringLiteral("1.2.0")}, sidecarPlatform()));
    EXPECT_FALSE(chooseRelease(channel, {2, 9}, {}, sidecarPlatform()));
    EXPECT_FALSE(chooseRelease(channel, {1, 2}, {}, QStringLiteral("macos-arm64")));
}

TEST(AiChannel, ComparesVersionsBySemVer)
{
    EXPECT_GT(compareVersions(QStringLiteral("1.10.0"), QStringLiteral("1.9.0")), 0);
    EXPECT_LT(compareVersions(QStringLiteral("1.1.0-beta.1"), QStringLiteral("1.1.0")), 0);
    EXPECT_EQ(compareVersions(QStringLiteral("1.1.0"), QStringLiteral("1.1.0")), 0);
    EXPECT_LT(compareVersions(QStringLiteral("junk"), QStringLiteral("0.0.1")), 0);
}

}
