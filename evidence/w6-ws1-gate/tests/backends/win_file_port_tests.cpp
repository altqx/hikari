// F2-W: the Windows write owner on real files. Copied-file writes, deep
// paths, interruption (cancel before publish), sharing violations and
// cleanup of the temporary file.

#include "hikari/backends/win_file_port.h"

#include <gtest/gtest.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

using namespace hikari;
using namespace hikari::application;
namespace fs = std::filesystem;

namespace {

std::string slurp(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

std::vector<std::byte> bytesOf(std::string_view s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

struct WinPortTest : ::testing::Test {
    fs::path dir;
    std::mutex mutex;
    std::vector<std::pair<PermitId, WriteOutcome>> reported;

    void SetUp() override
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("hikari-f2w-" + std::to_string(stamp));
        fs::create_directories(dir);
    }
    void TearDown() override { fs::remove_all(dir); }

    backends::WinFilePort::Completion record()
    {
        return [this](PermitId p, WriteOutcome o) {
            std::lock_guard lock(mutex);
            reported.emplace_back(p, o);
        };
    }
    WriteOutcome only()
    {
        std::lock_guard lock(mutex);
        EXPECT_EQ(reported.size(), 1u);
        return reported.empty() ? WriteOutcome::Failed : reported.front().second;
    }
    std::string key(const fs::path &p) { return reinterpret_cast<const char *>(p.u8string().c_str()); }
    bool onlyFile(const fs::path &expected)
    {
        int n = 0;
        for (const auto &e : fs::directory_iterator(expected.parent_path())) {
            (void)e;
            ++n;
        }
        return n == 1 && fs::exists(expected);
    }
};

} // namespace

TEST_F(WinPortTest, WritesANewFile)
{
    backends::WinFilePort port(record());
    port.startWrite(PermitId{1}, {key(dir / "new.ass")}, bytesOf("hello"));
    port.waitIdle();
    EXPECT_EQ(only(), WriteOutcome::Written);
    EXPECT_EQ(slurp(dir / "new.ass"), "hello");
    EXPECT_TRUE(onlyFile(dir / "new.ass"));
}

TEST_F(WinPortTest, ReplacesACopiedFile)
{
    fs::copy_file(HIKARI_FIXTURE_DIR "/inputs/unknown-sections.ass", dir / "copy.ass");
    backends::WinFilePort port(record());
    port.startWrite(PermitId{1}, {key(dir / "copy.ass")}, bytesOf("replaced"));
    port.waitIdle();
    EXPECT_EQ(only(), WriteOutcome::Written);
    EXPECT_EQ(slurp(dir / "copy.ass"), "replaced");
    EXPECT_TRUE(onlyFile(dir / "copy.ass"));
}

TEST_F(WinPortTest, DeepPathsBeyondMaxPath)
{
    fs::path deep = dir;
    while (deep.native().size() < 300)
        deep /= L"a-fairly-long-directory-name";
    ASSERT_TRUE(fs::create_directories(backends::windowsPath(key(deep))));
    backends::WinFilePort port(record());
    port.startWrite(PermitId{1}, {key(deep / "deep.ass")}, bytesOf("deep"));
    port.waitIdle();
    EXPECT_EQ(only(), WriteOutcome::Written);
    EXPECT_EQ(slurp(fs::path(backends::windowsPath(key(deep / "deep.ass")))), "deep");
    fs::remove_all(fs::path(backends::windowsPath(key(dir))));
    fs::create_directories(dir);
}

TEST_F(WinPortTest, SharingViolationKeepsTheOriginalAndCleansUp)
{
    {
        std::ofstream(dir / "busy.ass", std::ios::binary) << "original";
    }
    // Another program holds the file open without FILE_SHARE_DELETE.
    HANDLE holder = ::CreateFileW((dir / "busy.ass").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(holder, INVALID_HANDLE_VALUE);
    backends::WinFilePort port(record());
    port.startWrite(PermitId{1}, {key(dir / "busy.ass")}, bytesOf("new"));
    port.waitIdle();
    ::CloseHandle(holder);
    EXPECT_EQ(only(), WriteOutcome::Failed);
    EXPECT_EQ(slurp(dir / "busy.ass"), "original");
    EXPECT_TRUE(onlyFile(dir / "busy.ass")); // no temporary file left behind
}

TEST_F(WinPortTest, ReadOnlyDestinationIsNotOverwritten)
{
    {
        std::ofstream(dir / "ro.ass", std::ios::binary) << "original";
    }
    ::SetFileAttributesW((dir / "ro.ass").c_str(), FILE_ATTRIBUTE_READONLY);
    backends::WinFilePort port(record());
    port.startWrite(PermitId{1}, {key(dir / "ro.ass")}, bytesOf("new"));
    port.waitIdle();
    ::SetFileAttributesW((dir / "ro.ass").c_str(), FILE_ATTRIBUTE_NORMAL);
    EXPECT_EQ(only(), WriteOutcome::Failed);
    EXPECT_EQ(slurp(dir / "ro.ass"), "original");
    EXPECT_TRUE(onlyFile(dir / "ro.ass"));
}

TEST_F(WinPortTest, CancelBeforePublishKeepsTheOriginal)
{
    {
        std::ofstream(dir / "c.ass", std::ios::binary) << "original";
    }
    backends::WinFilePort *self = nullptr;
    backends::WinFilePort port(record(), {.chunkSize = 4, .beforePublish = [&](PermitId p) { self->requestCancel(p); }});
    self = &port;
    port.startWrite(PermitId{1}, {key(dir / "c.ass")}, bytesOf("interrupted write"));
    port.waitIdle();
    EXPECT_EQ(only(), WriteOutcome::Cancelled);
    EXPECT_EQ(slurp(dir / "c.ass"), "original");
    EXPECT_TRUE(onlyFile(dir / "c.ass"));
}

TEST_F(WinPortTest, MissingDirectoryFails)
{
    backends::WinFilePort port(record());
    port.startWrite(PermitId{1}, {key(dir / "missing" / "x.ass")}, bytesOf("x"));
    port.waitIdle();
    EXPECT_EQ(only(), WriteOutcome::Failed);
}
