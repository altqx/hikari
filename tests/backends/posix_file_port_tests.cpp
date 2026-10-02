// F2-L: native Linux write owner on a real temporary directory.

#include "hikari/backends/posix_file_port.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

using namespace hikari;
using application::DestinationKey;
using application::PermitId;
using application::WriteOutcome;
namespace fs = std::filesystem;

namespace {

std::vector<std::byte> bytes(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

std::string read(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct PortTest : ::testing::Test {
    fs::path dir;
    std::mutex mutex;
    std::map<std::uint64_t, WriteOutcome> outcomes;

    void SetUp() override
    {
        dir = fs::temp_directory_path() / ("hikari-f2l-" + std::to_string(::getpid()) + "-" +
                                           ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    void TearDown() override
    {
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::add);
        fs::remove_all(dir);
    }
    backends::PosixFilePort::Completion record()
    {
        return [this](PermitId id, WriteOutcome o) {
            std::lock_guard lock(mutex);
            outcomes[id.value] = o;
        };
    }
    std::size_t leftovers() const
    {
        std::size_t n = 0;
        for (const auto &e : fs::directory_iterator(dir))
            n += e.path().filename().string().find(".hikari-tmp-") != std::string::npos;
        return n;
    }
};

} // namespace

TEST_F(PortTest, WritesANewFile)
{
    backends::PosixFilePort port(record());
    port.startWrite(PermitId{1}, DestinationKey{(dir / "new.ass").string()}, bytes("hello\r\n"));
    port.waitIdle();
    EXPECT_EQ(outcomes[1], WriteOutcome::Written);
    EXPECT_EQ(read(dir / "new.ass"), "hello\r\n");
    EXPECT_EQ(leftovers(), 0u);
}

TEST_F(PortTest, ReplacesAtomicallyAndKeepsPermissions)
{
    const auto target = dir / "subs.ass";
    std::ofstream(target) << "old content";
    fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    backends::PosixFilePort port(record(), {.chunkSize = 3});
    port.startWrite(PermitId{2}, DestinationKey{target.string()}, bytes("new content, written in chunks"));
    port.waitIdle();
    EXPECT_EQ(outcomes[2], WriteOutcome::Written);
    EXPECT_EQ(read(target), "new content, written in chunks");
    EXPECT_EQ(fs::status(target).permissions() & fs::perms::all,
              fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    EXPECT_EQ(leftovers(), 0u);
}

TEST_F(PortTest, MissingDirectoryFails)
{
    backends::PosixFilePort port(record());
    port.startWrite(PermitId{3}, DestinationKey{(dir / "absent" / "x.ass").string()}, bytes("x"));
    port.waitIdle();
    EXPECT_EQ(outcomes[3], WriteOutcome::Failed);
}

TEST_F(PortTest, ReadOnlyDirectoryFailsAndKeepsTheOriginal)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root ignores directory permissions";
    const auto target = dir / "keep.ass";
    std::ofstream(target) << "original";
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
    backends::PosixFilePort port(record());
    port.startWrite(PermitId{4}, DestinationKey{target.string()}, bytes("replacement"));
    port.waitIdle();
    EXPECT_EQ(outcomes[4], WriteOutcome::Failed);
    EXPECT_EQ(read(target), "original");
}

TEST_F(PortTest, PublishFailureLeavesNoTemporaryFile)
{
    // A directory at the destination makes the rename fail after a full write.
    fs::create_directories(dir / "occupied.ass" / "child");
    backends::PosixFilePort port(record());
    port.startWrite(PermitId{5}, DestinationKey{(dir / "occupied.ass").string()}, bytes("data"));
    port.waitIdle();
    EXPECT_EQ(outcomes[5], WriteOutcome::Failed);
    EXPECT_TRUE(fs::is_directory(dir / "occupied.ass"));
    EXPECT_EQ(leftovers(), 0u);
}

TEST_F(PortTest, CancelBeforePublishKeepsTheOriginal)
{
    const auto target = dir / "subs.ass";
    std::ofstream(target) << "original";
    backends::PosixFilePort *portPtr = nullptr;
    backends::PosixFilePort port(record(), {.beforePublish = [&](PermitId id) { portPtr->requestCancel(id); }});
    portPtr = &port;
    port.startWrite(PermitId{6}, DestinationKey{target.string()}, bytes("never published"));
    port.waitIdle();
    EXPECT_EQ(outcomes[6], WriteOutcome::Cancelled);
    EXPECT_EQ(read(target), "original");
    EXPECT_EQ(leftovers(), 0u);
}

TEST_F(PortTest, EndToEndThroughTheCoordinator)
{
    std::vector<application::WriteResult> results;
    std::vector<std::pair<PermitId, WriteOutcome>> reports;
    std::mutex m;
    backends::PosixFilePort port([&](PermitId id, WriteOutcome o) {
        std::lock_guard lock(m);
        reports.emplace_back(id, o);
    });
    application::WriteCoordinator coordinator(port, [&](const application::WriteResult &r) { results.push_back(r); });
    const DestinationKey key{(dir / "doc.ass").string()};
    coordinator.associate(application::DocumentId{1}, key);
    const auto permit = coordinator.requestPermit(application::DocumentId{1}, key, 9);
    ASSERT_TRUE(permit);
    ASSERT_TRUE(coordinator.write(*permit, bytes("[Script Info]\n")));
    port.waitIdle();
    // The application thread delivers the worker's report to the coordinator.
    for (const auto &[id, o] : reports)
        coordinator.complete(id, o);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].outcome, WriteOutcome::Written);
    EXPECT_EQ(results[0].revision, 9u);
    EXPECT_EQ(read(dir / "doc.ass"), "[Script Info]\n");
}
