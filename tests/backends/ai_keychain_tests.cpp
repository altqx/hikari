#include "hikari/backends/ai_keychain.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QUuid>
#include <gtest/gtest.h>

#include <functional>

namespace app = hikari::application::ai;
using hikari::backends::ai::KeychainKeyStore;

namespace {

bool waitFor(const std::function<bool()> &done, int ms = 20'000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

class AiKeychainTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
#ifdef Q_OS_LINUX
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/hikari-no-session-bus");
#endif
        static int argc = 1;
        static char name[] = "ai_keychain_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    KeychainKeyStore store{QStringLiteral("HikariSub-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces)};
};

}

#ifdef Q_OS_LINUX
TEST_F(AiKeychainTest, WithoutAKeyringDaemonEveryCallIsUnavailable)
{
    std::optional<std::optional<app::KeyError>> written;
    store.write("p", "sk-never-stored", [&](std::optional<app::KeyError> e) { written = e; });
    ASSERT_TRUE(waitFor([&] { return written.has_value(); }));
    EXPECT_EQ(*written, app::KeyError::Unavailable);

    std::optional<app::KeyResult> read;
    store.read("p", [&](app::KeyResult r) { read = r; });
    ASSERT_TRUE(waitFor([&] { return read.has_value(); }));
    EXPECT_EQ(std::get<app::KeyError>(*read), app::KeyError::Unavailable);
}
#else
TEST_F(AiKeychainTest, KeysRoundTripThroughTheCredentialStore)
{
    std::optional<std::optional<app::KeyError>> written;
    store.write("p", "sk-round-trip", [&](std::optional<app::KeyError> e) { written = e; });
    ASSERT_TRUE(waitFor([&] { return written.has_value(); }));
    ASSERT_EQ(*written, std::nullopt);

    std::optional<app::KeyResult> read;
    store.read("p", [&](app::KeyResult r) { read = r; });
    ASSERT_TRUE(waitFor([&] { return read.has_value(); }));
    EXPECT_EQ(std::get<std::string>(*read), "sk-round-trip");

    std::optional<std::optional<app::KeyError>> removed;
    store.remove("p", [&](std::optional<app::KeyError> e) { removed = e; });
    ASSERT_TRUE(waitFor([&] { return removed.has_value(); }));
    EXPECT_EQ(*removed, std::nullopt);

    read.reset();
    store.read("p", [&](app::KeyResult r) { read = r; });
    ASSERT_TRUE(waitFor([&] { return read.has_value(); }));
    EXPECT_EQ(std::get<app::KeyError>(*read), app::KeyError::NotFound);
}
#endif
